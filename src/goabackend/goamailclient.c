/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <rest/rest-xml-parser.h>

#include "goamailconfig.h"
#include "goasouplogger.h"
#include "goautils.h"

#include "goamailclient.h"

/* The timeout used for non-IDLE commands */
#define COMMAND_TIMEOUT_SEC 30

/* Mail Autoconfig URIs
 *
 * See:
 *  - Mail provider (https://benbucksch.github.io/autoconfig-spec/draft-autoconfig-1.html#section-4.1)
 *  - Central database (https://benbucksch.github.io/autoconfig-spec/draft-autoconfig-1.html#section-4.2)
 *  - MX (https://benbucksch.github.io/autoconfig-spec/draft-autoconfig-1.html#section-4.3)
 *  - Autoconfig Spec issue #7 (https://github.com/benbucksch/autoconfig-spec/issues/7)
 */
#define AUTOCONFIG_DOMAIN_ADDRESS_FMT      "https://autoconfig.%s/.well-known/mail-v1.xml?emailaddress=%s"
#define AUTOCONFIG_DOMAIN_ADDRESS_OLD_FMT  "https://autoconfig.%s/mail/config-v1.1.xml?emailaddress=%s"
#define AUTOCONFIG_DOMAIN_FMT              "https://%s/.well-known/autoconfig/mail/config-v1.1.xml"
#define AUTOCONFIG_DOMAIN_ADDRESS_OLD_FMT  "https://autoconfig.%s/mail/config-v1.1.xml?emailaddress=%s"
#define AUTOCONFIG_DOMAIN_OLD_FMT          "http://autoconfig.%s/mail/config-v1.1.xml"
#define AUTOCONFIG_MX_DOMAIN_ADDRESS_FMT   AUTOCONFIG_DOMAIN_ADDRESS_FMT
#define AUTOCONFIG_DATABASE_ISPDB_FMT      "https://v1.ispdb.net/%s"
#define AUTOCONFIG_DATABSE_THUNDERBIRD_FMT "https://autoconfig.thunderbird.net/v1.1/%s"

#define AUTOCONFIG_SERVER_TYPE_IMAP "imap"
#define AUTOCONFIG_SERVER_TYPE_POP3 "pop3"
#define AUTOCONFIG_SERVER_TYPE_JMAP "jmap"
#define AUTOCONFIG_SERVER_TYPE_SMTP "smtp"

#define AUTOCONFIG_SOCKET_TYPE_PLAIN    "plain"
#define AUTOCONFIG_SOCKET_TYPE_STARTTLS "STARTTLS"
#define AUTOCONFIG_SOCKET_TYPE_SSL      "SSL"

struct _GoaMailClient
{
  /*< private >*/
  GObject parent_instance;
};

G_DEFINE_TYPE (GoaMailClient, goa_mail_client, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_client_init (GoaMailClient *self)
{
}

static void
goa_mail_client_class_init (GoaMailClientClass *klass)
{
}

/* ---------------------------------------------------------------------------------------------------- */

GoaMailClient *
goa_mail_client_new (void)
{
  return GOA_MAIL_CLIENT (g_object_new (GOA_TYPE_MAIL_CLIENT, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GDataInputStream *input;
  GDataOutputStream *output;
  GIOStream *tls_conn;
  GSocket *socket;
  GSocketClient *sc;
  GSocketConnection *conn;
  GTlsCertificateFlags cert_flags;
  GoaMailAuth *auth;
  GoaTlsType tls_type;
  gboolean accept_ssl_errors;
  gchar *host_and_port;
  guint16 default_port;
} CheckData;

static void
mail_client_check_data_free (CheckData *data)
{
  g_object_unref (data->sc);
  g_object_unref (data->auth);
  g_clear_object (&data->input);
  g_clear_object (&data->output);
  g_clear_object (&data->socket);
  g_clear_object (&data->conn);
  g_clear_object (&data->tls_conn);
  g_free (data->host_and_port);
  g_slice_free (CheckData, data);
}

static gboolean
mail_client_check_accept_certificate_cb (GTlsConnection *conn,
                                         GTlsCertificate *peer_cert,
                                         GTlsCertificateFlags errors,
                                         gpointer user_data)
{
  CheckData *data = user_data;

  /* Fail the connection if the certificate is invalid. */
  data->cert_flags = errors;
  return FALSE;
}

static void
mail_client_check_event_cb (GSocketClient *sc,
                            GSocketClientEvent event,
                            GSocketConnectable *connectable,
                            GIOStream *connection,
                            gpointer user_data)
{
  CheckData *data = user_data;

  if (event != G_SOCKET_CLIENT_TLS_HANDSHAKING)
    return;

  data->tls_conn = g_object_ref (connection);
  if (data->accept_ssl_errors)
    g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (data->tls_conn), 0);

  g_signal_connect (data->tls_conn,
                    "accept-certificate",
                    G_CALLBACK (mail_client_check_accept_certificate_cb),
                    data);
}

static void
mail_client_check_auth_run_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data;
  GError *error;

  data = g_task_get_task_data (task);

  error = NULL;
  if (!goa_mail_auth_run_finish (data->auth, res, &error))
    {
      g_warning ("goa_mail_auth_run() failed: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      g_task_return_error (task, error);
      goto out;
    }

  g_io_stream_close (G_IO_STREAM (data->conn), NULL, NULL);
  g_task_return_boolean (task, TRUE);

 out:
  g_object_unref (G_OBJECT (task));
}

static void
mail_client_check_tls_conn_handshake_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data;
  GCancellable *cancellable;
  GDataInputStream *input;
  GDataOutputStream *output;
  GInputStream *base_input;
  GError *error;
  GOutputStream *base_output;

  input = NULL;
  output = NULL;

  cancellable = g_task_get_cancellable (task);
  data = g_task_get_task_data (task);

  error = NULL;
  if (!g_tls_connection_handshake_finish (G_TLS_CONNECTION (data->tls_conn), res, &error))
    {
      g_warning ("g_tls_connection_handshake() failed: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      /* GIO sets G_TLS_ERROR_BAD_CERTIFICATE when it should be
       * setting G_TLS_ERROR_HANDSHAKE. Hence, lets check the
       * GTlsCertificate flags to accommodate future GIO fixes.
       */
      if (data->cert_flags != 0)
        {
          GError *tls_error;

          tls_error = NULL;
          goa_utils_set_error_ssl (&tls_error, data->cert_flags);
          g_task_return_error (task, tls_error);
          g_error_free (error);
        }
      else
        {
          error->domain = GOA_ERROR;
          error->code = GOA_ERROR_FAILED; /* TODO: more specific */
          g_task_return_error (task, error);
        }

      goto out;
    }

  g_clear_object (&data->conn);
  data->conn = g_tcp_wrapper_connection_new (data->tls_conn, data->socket);

  base_input = g_io_stream_get_input_stream (G_IO_STREAM (data->conn));
  input = g_data_input_stream_new (base_input);
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (input), FALSE);
  g_data_input_stream_set_newline_type (input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
  goa_mail_auth_set_input (data->auth, input);

  base_output = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));
  output = g_data_output_stream_new (base_output);
  g_filter_output_stream_set_close_base_stream (G_FILTER_OUTPUT_STREAM (output), FALSE);
  goa_mail_auth_set_output (data->auth, output);

  goa_mail_auth_run (data->auth, cancellable, mail_client_check_auth_run_cb, g_object_ref (task));

 out:
  g_clear_object (&input);
  g_clear_object (&output);
  g_object_unref (G_OBJECT (task));
}

static void
mail_client_check_auth_starttls_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data;
  GCancellable *cancellable;
  GSocketConnectable *server_identity;
  GError *error;

  server_identity = NULL;

  cancellable = g_task_get_cancellable (task);
  data = g_task_get_task_data (task);

  error = NULL;
  if (!goa_mail_auth_starttls_finish (data->auth, res, &error))
    {
      g_warning ("goa_mail_auth_starttls() failed: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      g_task_return_error (task, error);
      goto out;
    }

  error = NULL;
  server_identity = g_network_address_parse (data->host_and_port, data->default_port, &error);
  if (server_identity == NULL)
    {
      g_task_return_error (task, error);
      goto out;
    }

  error = NULL;
  data->tls_conn = g_tls_client_connection_new (G_IO_STREAM (data->conn), server_identity, &error);
  if (data->tls_conn == NULL)
    {
      g_task_return_error (task, error);
      goto out;
    }

  if (data->accept_ssl_errors)
    g_tls_client_connection_set_validation_flags (G_TLS_CLIENT_CONNECTION (data->tls_conn), 0);

  g_signal_connect (data->tls_conn,
                    "accept-certificate",
                    G_CALLBACK (mail_client_check_accept_certificate_cb),
                    data);

  g_tls_connection_handshake_async (G_TLS_CONNECTION (data->tls_conn),
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    mail_client_check_tls_conn_handshake_cb,
                                    g_object_ref (task));

 out:
  g_clear_object (&server_identity);
  g_object_unref (G_OBJECT (task));
}

static void
mail_client_check_connect_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data;
  GCancellable *cancellable;
  GDataInputStream *input;
  GDataOutputStream *output;
  GInputStream *base_input;
  GError *error;
  GOutputStream *base_output;

  cancellable = g_task_get_cancellable (task);
  data = g_task_get_task_data (task);

  error = NULL;
  data->conn = g_socket_client_connect_to_host_finish (data->sc, res, &error);
  if (data->conn == NULL)
    {
      g_warning ("g_socket_client_connect_to_host() failed: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      /* GIO sets G_TLS_ERROR_BAD_CERTIFICATE when it should be
       * setting G_TLS_ERROR_HANDSHAKE. Hence, lets check the
       * GTlsCertificate flags to accommodate future GIO fixes.
       */
      if (data->cert_flags != 0)
        {
          GError *tls_error;

          tls_error = NULL;
          goa_utils_set_error_ssl (&tls_error, data->cert_flags);
          g_task_return_error (task, tls_error);
          g_error_free (error);
        }
      else
        {
          error->domain = GOA_ERROR;
          error->code = GOA_ERROR_FAILED; /* TODO: more specific */
          g_task_return_error (task, error);
        }

      goto out;
    }

  /* fail quickly */
  data->socket = g_object_ref (g_socket_connection_get_socket (data->conn));
  g_socket_set_timeout (data->socket, COMMAND_TIMEOUT_SEC);

  base_input = g_io_stream_get_input_stream (G_IO_STREAM (data->conn));
  input = g_data_input_stream_new (base_input);
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (input), FALSE);
  g_data_input_stream_set_newline_type (input, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
  goa_mail_auth_set_input (data->auth, input);
  g_object_unref (input);

  base_output = g_io_stream_get_output_stream (G_IO_STREAM (data->conn));
  output = g_data_output_stream_new (base_output);
  g_filter_output_stream_set_close_base_stream (G_FILTER_OUTPUT_STREAM (output), FALSE);
  goa_mail_auth_set_output (data->auth, output);
  g_object_unref (output);

  if (data->tls_type == GOA_TLS_TYPE_STARTTLS)
    goa_mail_auth_starttls (data->auth, cancellable, mail_client_check_auth_starttls_cb, g_object_ref (task));
  else
    goa_mail_auth_run (data->auth, cancellable, mail_client_check_auth_run_cb, g_object_ref (task));

 out:
  g_object_unref (G_OBJECT (task));
}

void
goa_mail_client_check (GoaMailClient       *self,
                       const gchar         *host_and_port,
                       GoaTlsType           tls_type,
                       gboolean             accept_ssl_errors,
                       guint16              default_port,
                       GoaMailAuth         *auth,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  CheckData *data;
  GTask *task;

  g_return_if_fail (GOA_IS_MAIL_CLIENT (self));
  g_return_if_fail (host_and_port != NULL && host_and_port[0] != '\0');
  g_return_if_fail (GOA_IS_MAIL_AUTH (auth));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_mail_client_check);

  data = g_slice_new0 (CheckData);
  g_task_set_task_data (task, data, (GDestroyNotify) mail_client_check_data_free);

  data->sc = g_socket_client_new ();
  if (tls_type == GOA_TLS_TYPE_SSL)
    {
      g_socket_client_set_tls (data->sc, TRUE);
      g_signal_connect (data->sc, "event", G_CALLBACK (mail_client_check_event_cb), data);
    }

  data->host_and_port = g_strdup (host_and_port);
  data->tls_type = tls_type;
  data->accept_ssl_errors = accept_ssl_errors;
  data->default_port = default_port;
  data->auth = g_object_ref (auth);

  g_socket_client_connect_to_host_async (data->sc,
                                         data->host_and_port,
                                         data->default_port,
                                         cancellable,
                                         mail_client_check_connect_cb,
                                         g_object_ref (task));

  g_object_unref (task);
}

gboolean
goa_mail_client_check_finish (GoaMailClient *self, GAsyncResult *res, GError **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_MAIL_CLIENT (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_mail_client_check, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GError **error;
  GMainLoop *loop;
  gboolean op_res;
} CheckSyncData;

static void
mail_client_check_sync_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  CheckSyncData *data = user_data;

  data->op_res = goa_mail_client_check_finish (GOA_MAIL_CLIENT (source_object), res, data->error);
  g_main_loop_quit (data->loop);
}

gboolean
goa_mail_client_check_sync (GoaMailClient  *self,
                            const gchar    *host_and_port,
                            GoaTlsType      tls_type,
                            gboolean        accept_ssl_errors,
                            guint16         default_port,
                            GoaMailAuth    *auth,
                            GCancellable   *cancellable,
                            GError        **error)
{
  CheckSyncData data;
  GMainContext *context = NULL;

  data.error = error;

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);

  goa_mail_client_check (self,
                         host_and_port,
                         tls_type,
                         accept_ssl_errors,
                         default_port,
                         auth,
                         cancellable,
                         mail_client_check_sync_cb,
                         &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.op_res;
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaMailConfig *
goa_mail_config_new_from_autoconfig_xml (RestXmlNode *server,
                                         const char  *email_address,
                                         const char  *email_localpart,
                                         const char  *email_domain)
{
  g_autoptr (GoaMailConfig) config = NULL;
  const char *server_type = NULL;
  RestXmlNode *username = NULL;
  RestXmlNode *hostname = NULL;
  RestXmlNode *port = NULL;
  RestXmlNode *socket_type = NULL;

  g_return_val_if_fail (g_str_equal (server->name, "incomingServer")
                        || g_str_equal (server->name, "outgoingServer"), NULL);

  server_type = rest_xml_node_get_attr (server, "type");
  g_return_val_if_fail (server_type != NULL, NULL);

  config = goa_mail_config_new (server_type);

  hostname = rest_xml_node_find (server, "hostname");
  if (hostname != NULL && hostname->content != NULL)
    goa_mail_config_set_hostname (GOA_MAIL_CONFIG (config), hostname->content);

  socket_type = rest_xml_node_find (server, "socketType");
  if (socket_type != NULL && socket_type->content != NULL)
    {
      if (g_ascii_strcasecmp (socket_type->content, AUTOCONFIG_SOCKET_TYPE_SSL) == 0)
        goa_mail_config_set_encryption (GOA_MAIL_CONFIG (config), GOA_TLS_TYPE_SSL);
      else if (g_ascii_strcasecmp (socket_type->content, AUTOCONFIG_SOCKET_TYPE_STARTTLS) == 0)
        goa_mail_config_set_encryption (GOA_MAIL_CONFIG (config), GOA_TLS_TYPE_STARTTLS);
      else if (g_ascii_strcasecmp (socket_type->content, AUTOCONFIG_SOCKET_TYPE_PLAIN) == 0)
        goa_mail_config_set_encryption (GOA_MAIL_CONFIG (config), GOA_TLS_TYPE_NONE);
    }

  port = rest_xml_node_find (server, "port");
  if (port != NULL && port->content != NULL)
    {
      guint64 port_ = g_ascii_strtoull (port->content, NULL, 10);

      if (port_ > 0 && port_ < UINT16_MAX)
        goa_mail_config_set_port (GOA_MAIL_CONFIG (config), (uint16_t)port_);
    }

  username = rest_xml_node_find (server, "username");
  if (username != NULL && username->content != NULL)
    {
      if (g_ascii_strcasecmp (username->content, "%EMAILADDRESS%") == 0)
        goa_mail_config_set_username (GOA_MAIL_CONFIG (config), email_address);
      else if (g_ascii_strcasecmp (username->content, "%EMAILLOCALPART%") == 0)
        goa_mail_config_set_username (GOA_MAIL_CONFIG (config), email_localpart);
      else if (g_ascii_strcasecmp (username->content, "%EMAILDOMAIN%") == 0)
        goa_mail_config_set_username (GOA_MAIL_CONFIG (config), email_domain);
    }

  return g_steal_pointer (&config);
}

/*< private >
 * mail_client_parse_autoconfig_xml:
 * @xml_bytes: Autoconfig XML
 * @email_address: a valid email address
 * @error: (nullable): return location for an error
 *
 * Parse an autoconfig XML document and return a list of services.
 *
 * See:
 *  - https://github.com/benbucksch/autoconfig-spec/
 *  - https://benbucksch.github.io/autoconfig-spec/draft-autoconfig-1.html
 *  - https://github.com/thunderbird/autoconfig
 *
 * Returns: (transfer container) (element-type Goa.MailConfig): a list of discovered services
 */
static GPtrArray *
mail_client_parse_autoconfig_xml (GBytes      *xml_bytes,
                                  const char  *email_address,
                                  GError     **error)
{
  g_autoptr(GPtrArray) services = NULL;
  g_autofree char *email_localpart = NULL;
  g_autofree char *email_domain = NULL;
  g_autoptr(RestXmlParser) parser = NULL;
  g_autoptr(RestXmlNode) root_node = NULL;
  RestXmlNode *email_node = NULL;
  const char *xml_data = NULL;
  gsize xml_len = 0;

  goa_utils_parse_email_address (email_address, &email_localpart, &email_domain);
  xml_data = g_bytes_get_data (xml_bytes, &xml_len);

  parser = rest_xml_parser_new ();
  root_node = rest_xml_parser_parse_from_data (parser, xml_data, xml_len);
  if (root_node == NULL || g_strcmp0 (root_node->name, "clientConfig") != 0)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Failed to parse Autoconfig XML");
      return NULL;
    }

  services = g_ptr_array_new_with_free_func (g_object_unref);

  /* Mail Services
   */
  email_node = rest_xml_node_find (root_node, "emailProvider");
  if (email_node != NULL)
    {
      RestXmlNode *incoming_server = NULL;
      RestXmlNode *outgoing_server = NULL;

      incoming_server = rest_xml_node_find (email_node, "incomingServer");
      for (RestXmlNode *node = incoming_server; node != NULL; node = node->next)
        {
          GoaMailConfig *server = NULL;

          server = goa_mail_config_new_from_autoconfig_xml (node,
                                                            email_address,
                                                            email_localpart,
                                                            email_domain);
          if (server != NULL)
            g_ptr_array_add (services, g_steal_pointer (&server));
        }

      outgoing_server = rest_xml_node_find (email_node, "outgoingServer");
      for (RestXmlNode *node = outgoing_server; node != NULL; node = node->next)
        {
          GoaMailConfig *server = NULL;

          server = goa_mail_config_new_from_autoconfig_xml (node,
                                                            email_address,
                                                            email_localpart,
                                                            email_domain);
          if (server != NULL)
            g_ptr_array_add (services, g_steal_pointer (&server));
        }
    }

  return g_steal_pointer (&services);
}

/* ---------------------------------------------------------------------------------------------------- */

static int
sort_mx_domains (gconstpointer a,
                 gconstpointer b)
{
  GVariant *variant_a = (GVariant *)a;
  GVariant *variant_b = (GVariant *)b;
  uint16_t pref_a, pref_b;

  g_variant_get (variant_a, "(qs)", &pref_a, NULL);
  g_variant_get (variant_b, "(qs)", &pref_b, NULL);

  return pref_a > pref_b;
}

typedef struct
{
  SoupSession *session;
  char *email_address;
  GQueue uris;
  gboolean mx_fallback;
} DiscoverData;

static void
discover_data_free (gpointer user_data)
{
  DiscoverData *data = user_data;

  g_clear_object (&data->session);
  g_clear_pointer (&data->email_address, g_free);
  g_queue_clear_full (&data->uris, g_free);
  g_free (data);
}

static void mail_client_discover_iterate (GTask *task);

static void
goa_mail_client_autoconfig_cb (SoupSession  *session,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  DiscoverData *data = g_task_get_task_data (task);
  SoupMessage *response = NULL;
  GPtrArray *services = NULL;
  g_autoptr (GBytes) body = NULL;
  g_autoptr (GError) error = NULL;

  body = soup_session_send_and_read_finish (session, result, &error);
  if (error != NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      goto next;
    }

  response = soup_session_get_async_result_message (session, result);
  if (soup_message_get_status (response) != SOUP_STATUS_OK)
    goto next;

  services = mail_client_parse_autoconfig_xml (body, data->email_address, NULL);
  if (services != NULL)
    {
      g_task_return_pointer (task, g_steal_pointer (&services), (GDestroyNotify) g_ptr_array_unref);
      return;
    }

next:
  mail_client_discover_iterate (task);
}

static void
goa_mail_client_mx_domain_cb (GResolver    *resolver,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  DiscoverData *data = g_task_get_task_data (task);
  const char *mx_raw_domain = NULL;
  const char *mx_base_domain = NULL;
  g_autofree char *mx_full_domain = NULL;
  g_autolist(GVariant) records = NULL;
  g_autoptr(GError) error = NULL;

  /* Resolve the highest priority MX domain for %EMAILDOMAIN% */
  records = g_resolver_lookup_records_finish (resolver, result, &error);
  if (records == NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

      g_debug ("%s(): %s", G_STRFUNC, error->message);
      goto out;
    }

  records = g_list_sort (records, sort_mx_domains);
  g_variant_get ((GVariant *)records->data, "(q&s)", NULL, &mx_raw_domain);
  mx_full_domain = g_utf8_casefold (mx_raw_domain, -1);
  mx_base_domain = soup_tld_get_base_domain (mx_full_domain, NULL);

  /* Lookup by the highest priority %MXFULLDOMAIN% and %MXBASEDOMAIN */
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_MX_DOMAIN_ADDRESS_FMT, mx_full_domain, data->email_address));
  if (mx_base_domain != NULL && g_strcmp0 (mx_base_domain, mx_full_domain) != 0)
    g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_MX_DOMAIN_ADDRESS_FMT, mx_base_domain, data->email_address));

  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DATABASE_ISPDB_FMT, mx_full_domain));
  if (mx_base_domain != NULL && g_strcmp0 (mx_base_domain, mx_full_domain) != 0)
    g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DATABASE_ISPDB_FMT, mx_base_domain));

out:
  mail_client_discover_iterate (task);
}

static void
mail_client_discover_iterate (GTask *task)
{
  DiscoverData *data = g_task_get_task_data (task);

  if (!g_queue_is_empty (&data->uris))
    {
      g_autoptr(SoupMessage) message = NULL;
      g_autofree char *uri = NULL;

      uri = g_queue_pop_head (&data->uris);
      message = soup_message_new (SOUP_METHOD_GET, uri);

      if (message != NULL)
        {
          soup_session_send_and_read_async (data->session,
                                            message,
                                            G_PRIORITY_DEFAULT,
                                            g_task_get_cancellable (task),
                                            (GAsyncReadyCallback)goa_mail_client_autoconfig_cb,
                                            g_object_ref (task));
        }
      else
        {
          g_warning ("Failed to create message for \"%s\"", uri);
          mail_client_discover_iterate (task);
        }
    }
  else if (data->mx_fallback)
    {
      g_autoptr(GResolver) resolver = NULL;
      g_autofree char *email_domain = NULL;

      goa_utils_parse_email_address (data->email_address, NULL, &email_domain);
      resolver = g_resolver_get_default ();
      g_resolver_lookup_records_async (resolver,
                                       email_domain,
                                       G_RESOLVER_RECORD_MX,
                                       g_task_get_cancellable (task),
                                       (GAsyncReadyCallback)goa_mail_client_mx_domain_cb,
                                       g_object_ref (task));
      data->mx_fallback = FALSE;
    }
  else
    {
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               "Failed to autoconfig '%s'",
                               data->email_address);
    }
}

/**
 * goa_mail_client_discover:
 * @self: a `GoaMailClient`
 * @email_address: a well-formed email address
 * @cancellable: (nullable): a cancellable for the operation
 * @callback: (scope async): a callback to be invoked when the operation is complete
 * @user_data: (closure): user supplied data
 *
 * Discover IMAP/SMTP configuration for the domain of @email.
 *
 * Call [method@Goa.MailClient.discover_finish] to get the result.
 */
void
goa_mail_client_discover (GoaMailClient       *self,
                          const char          *email_address,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(SoupLogger) logger = NULL;
  g_autofree char *email_domain = NULL;
  DiscoverData *data;

  g_return_if_fail (GOA_IS_MAIL_CLIENT (self));
  g_return_if_fail (email_address != NULL && *email_address != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  if (!goa_utils_parse_email_address (email_address, NULL, &email_domain))
    {
      g_task_report_new_error (self, callback, user_data, goa_mail_client_discover,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_ARGUMENT,
                               _("Invalid email address “%s”"),
                               email_address);
    }

  data = g_new0 (DiscoverData, 1);
  data->email_address = g_strdup (email_address);
  data->mx_fallback = TRUE;

  data->session = soup_session_new ();
  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));
  soup_session_set_timeout (data->session, 15);
  soup_session_set_user_agent (data->session, "gnome-online-accounts/" PACKAGE_VERSION " ");

  /* Lookup by %EMAILDOMAIN% */
  g_queue_init (&data->uris);
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DOMAIN_ADDRESS_FMT, email_domain, email_address));
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DOMAIN_ADDRESS_OLD_FMT, email_domain, email_address));
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DOMAIN_FMT, email_domain));
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DOMAIN_OLD_FMT, email_domain));
  g_queue_push_tail (&data->uris, g_strdup_printf (AUTOCONFIG_DATABASE_ISPDB_FMT, email_domain));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_mail_client_discover);
  g_task_set_task_data (task, data, discover_data_free);

  mail_client_discover_iterate (task);
}

/**
 * goa_mail_client_discover_finish:
 * @self: a `GoaMailClient`
 * @result: a `GAsyncResult`
 * @error: (nullable): return location for an error
 *
 * Finish an operation started by [method@Goa.MailClient.discover].
 *
 * Returns: (transfer container) (element-type Goa.MailConfig): a list of discovered services,
 *   which may include DAV services, or %NULL with @error set
 */
GPtrArray *
goa_mail_client_discover_finish (GoaMailClient  *self,
                                 GAsyncResult   *result,
                                 GError        **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (GOA_IS_MAIL_CLIENT (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == goa_mail_client_discover, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (task, error);
}

