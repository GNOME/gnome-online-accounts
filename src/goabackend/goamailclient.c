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

#include "goamailclient.h"
#include "goautils.h"

/* The timeout used for non-IDLE commands */
#define COMMAND_TIMEOUT_SEC 30

struct _GoaMailClient
{
  /*< private >*/
  GObject parent_instance;
};

typedef struct _GoaMailClientClass GoaMailClientClass;

struct _GoaMailClientClass
{
  GObjectClass parent_class;
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
