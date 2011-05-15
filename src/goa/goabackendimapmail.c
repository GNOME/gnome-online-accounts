/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include <glib-unix.h>

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>
#include <stdlib.h>

#include "goabackendimapauth.h"
#include "goabackendimapclient.h"
#include "goabackendimapmail.h"

/**
 * GoaBackendImapMail:
 *
 * The #GoaBackendImapMail structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendImapMail
{
  /*< private >*/
  GoaMailSkeleton parent_instance;

  gchar *host_and_port;
  gboolean use_tls;
  GoaBackendImapAuth *auth;
};

typedef struct _GoaBackendImapMailClass GoaBackendImapMailClass;

struct _GoaBackendImapMailClass
{
  GoaMailSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_HOST_AND_PORT,
  PROP_USE_TLS,
  PROP_AUTH
};

/**
 * SECTION:goabackendimapmail
 * @title: GoaBackendImapMail
 * @short_description: Implementation of the #GoaMail interface for IMAP servers
 *
 * #GoaBackendImapMail is an implementation of the #GoaMail D-Bus
 * interface that uses a #GoaBackendImapClient instance to speak to a
 * remote IMAP server.
 */

static void goa_backend_imap_mail__goa_mail_iface_init (GoaMailIface *iface);

G_DEFINE_TYPE_WITH_CODE (GoaBackendImapMail, goa_backend_imap_mail, GOA_TYPE_MAIL_SKELETON,
                         G_IMPLEMENT_INTERFACE (GOA_TYPE_MAIL, goa_backend_imap_mail__goa_mail_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_mail_finalize (GObject *object)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  g_free (mail->host_and_port);
  g_object_unref (mail->auth);

  G_OBJECT_CLASS (goa_backend_imap_mail_parent_class)->finalize (object);
}

static void
goa_backend_imap_mail_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      g_value_set_string (value, mail->host_and_port);
      break;

    case PROP_USE_TLS:
      g_value_set_boolean (value, mail->use_tls);
      break;

    case PROP_AUTH:
      g_value_set_object (value, mail->auth);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_mail_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      mail->host_and_port = g_value_dup_string (value);
      break;

    case PROP_USE_TLS:
      mail->use_tls = g_value_get_boolean (value);
      break;

    case PROP_AUTH:
      mail->auth = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_mail_init (GoaBackendImapMail *mail)
{
  /* Ensure D-Bus method invocations run in their own thread */
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (mail),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
goa_backend_imap_mail_class_init (GoaBackendImapMailClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = goa_backend_imap_mail_finalize;
  gobject_class->set_property = goa_backend_imap_mail_set_property;
  gobject_class->get_property = goa_backend_imap_mail_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_HOST_AND_PORT,
                                   g_param_spec_string ("host-and-port",
                                                        "host-and-port",
                                                        "host-and-port",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_USE_TLS,
                                   g_param_spec_boolean ("use-tls",
                                                         "use-tls",
                                                         "use-tls",
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_AUTH,
                                   g_param_spec_object ("auth",
                                                        "auth",
                                                        "auth",
                                                        GOA_TYPE_BACKEND_IMAP_AUTH,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_mail_new:
 * @host_and_port: The name and optionally port to connect to.
 * @use_tls: Whether TLS should be used.
 * @auth: Object used for authenticating the connection.
 *
 * Creates a new #GoaMail object.
 *
 * Returns: (type GoaBackendImapMail): A new #GoaMail instance.
 */
GoaMail *
goa_backend_imap_mail_new (const gchar         *host_and_port,
                           gboolean             use_tls,
                           GoaBackendImapAuth  *auth)
{
  g_return_val_if_fail (host_and_port != NULL, NULL);
  return GOA_MAIL (g_object_new (GOA_TYPE_BACKEND_IMAP_MAIL,
                                 "host-and-port", host_and_port,
                                 "use-tls", use_tls,
                                 "auth", auth,
                                 NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  GoaBackendImapMail *mail;
  GoaMailMonitor *monitor;

  /* Used so we can nuke the monitor if the creator vanishes */
  guint name_watcher_id;

  /* Use to communicate with the thread running the IMAP client */
  GCancellable *imap_cancellable;
  gboolean      imap_request_close;
  GMutex       *imap_counter_lock;
  GCond        *imap_counter_cond;
  gint          imap_num_refreshes;
  gint          imap_num_connections_failed;
} MonitorData;

static MonitorData *
monitor_data_ref (MonitorData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
monitor_data_unref (MonitorData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_clear_object (&data->mail);
      g_clear_object (&data->monitor);
      if (data->name_watcher_id)
        g_bus_unwatch_name (data->name_watcher_id);
      g_clear_object (&data->imap_cancellable);
      if (data->imap_counter_lock != NULL)
        g_mutex_free (data->imap_counter_lock);
      if (data->imap_counter_cond != NULL)
        g_cond_free (data->imap_counter_cond);
      g_slice_free (MonitorData, data);
    }
}

static void
nuke_monitor (MonitorData *data)
{
  /* unexport the D-Bus object */
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (data->monitor));
  /* nuke the running IMAP client */
  data->imap_request_close = TRUE;
  g_mutex_lock (data->imap_counter_lock);
  g_cancellable_cancel (data->imap_cancellable);
  g_mutex_unlock (data->imap_counter_lock);
  monitor_data_unref (data);
}

static void
on_monitor_owner_vanished (GDBusConnection *connection,
                           const gchar     *name,
                           gpointer         user_data)
{
  MonitorData *data = user_data;
  /* yippee ki yay motherfucker */
  nuke_monitor (data);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  MonitorData *monitor_data;
  gint         num_exists;
  gint         last_num_exists;

  gint         uidvalidity;
} ImapClientData;


static gboolean
parse_int (const gchar *s,
           gint        *out_result)
{
  gboolean ret;
  gchar *endp;
  gint result;

  g_return_val_if_fail (s != NULL, FALSE);

  ret = FALSE;
  result = strtol (s, &endp, 0);
  if (result == 0 && endp == s)
    goto out;

  if (out_result != NULL)
    *out_result = result;

  ret = TRUE;

 out:
  return ret;
}

static gboolean
fetch_check (const gchar *data,
             guint       *pos,
             const gchar *key)
{
  gsize key_len;
  gboolean ret;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  ret = FALSE;

  key_len = strlen (key);
  if (strncmp (data + *pos, key, key_len) == 0 && data[*pos + key_len] == ' ')
    {
      ret = TRUE;
      *pos += key_len + 1;
      goto out;
    }
 out:
  return ret;
}

static gchar *
fetch_string (const gchar *data,
              guint       *pos)
{
  gchar *ret;
  guint start_pos;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  start_pos = *pos;

  while (data[*pos] != ' ' && data[*pos] != ')' && data[*pos] != '\0')
    *pos += 1;

  ret = g_strndup (data + start_pos, *pos - start_pos);

  return ret;
}

static gboolean
fetch_int (const gchar *data,
           guint       *pos,
           gint        *out_value)
{
  gchar *str_value;
  gboolean ret;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);
  g_return_val_if_fail (out_value != NULL, FALSE);

  str_value = NULL;
  ret = FALSE;

  str_value = fetch_string (data, pos);
  if (str_value == NULL)
    goto out;

  if (!parse_int (str_value, out_value))
    goto out;

  ret = TRUE;

 out:
  g_free (str_value);
  return ret;
}

static gchar *
fetch_literal_string (const gchar *data,
                      guint       *pos,
                      guint       *out_len)
{
  gchar *ret;
  guint start_pos;
  guint len;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  start_pos = *pos;

  if (data[*pos] != '{')
    goto out;
  *pos += 1;
  while (g_ascii_isdigit (data[*pos]))
    *pos += 1;
  if (strncmp (data + *pos, "}\r\n", 3) != 0)
    goto out;
  *pos += 3;

  if (!parse_int (data + start_pos + 1, (gint*) &len))
    goto out;

  ret = g_strndup (data + *pos, len);
  *pos += len;

  if (out_len != NULL)
    *out_len = len;

 out:
  return ret;
}

/* TODO: try a little harder to make this a conformant RFC822 parser */
static GHashTable *
parse_rfc822_headers (const gchar *rfc822_headers)
{
  GHashTable *ret;
  gchar **lines;
  guint n;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  lines = g_strsplit (rfc822_headers, "\r\n", -1);
  for (n = 0; lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      const gchar *s;

      if (line[0] == '\0')
        continue;

      s = strstr (line, ": ");
      if (s != NULL)
        {
          gchar *key;
          gchar *value;
          key = g_strndup (line, s - line);
          value = g_strdup (s + 2);
          g_hash_table_insert (ret, key, value);
        }
      else
        {
          g_warning ("%s: ignoring mysterious line `%s' whilst parsing `%s'",
                     G_STRFUNC, line, rfc822_headers);
        }
    }
  g_strfreev (lines);

  return ret;
}

/* Simple FETCH response parser only handling a subset of FETCH
 * responses, see
 *
 *   http://tools.ietf.org/html/rfc3501#section-7.4.2
 *
 * for more details.
 */
static void
imap_client_handle_fetch_response (ImapClientData  *data,
                                   guint            message_seqnum,
                                   const gchar     *params)
{
  guint n;
  gboolean parsed;
  gboolean has_uid;
  gint uid;
  gchar *rfc822_headers;
  guint rfc822_headers_len;
  gchar *excerpt;
  guint excerpt_len;
  GHashTable *headers;
  const gchar *from_header;
  const gchar *subject_header;
  gchar *uid_str;
  gchar *uri;
  /* GVariantBuilder extras_builder; */

  g_return_if_fail (message_seqnum >= 1);
  g_return_if_fail (params != NULL);

  uid = 0;
  has_uid = FALSE;
  excerpt = NULL;
  rfc822_headers = NULL;
  headers = NULL;
  uid_str = NULL;
  uri = NULL;
  parsed = FALSE;

  if (params[0] != '(')
    goto out;
  n = 1;
  while (params[n] != ')' && params[n] != '\0')
    {
      if (fetch_check (params, &n, "UID"))
        {
          if (!fetch_int (params, &n, &uid))
            goto out;
          has_uid = TRUE;
        }
      else if (fetch_check (params, &n, "BODY[HEADER.FIELDS (Date From To Cc Subject)]"))
        {
          rfc822_headers = fetch_literal_string (params, &n, &rfc822_headers_len);
          if (rfc822_headers == NULL)
            goto out;
        }
      else if (fetch_check (params, &n, "BODY[TEXT]<0>"))
        {
          excerpt = fetch_literal_string (params, &n, &excerpt_len);
          if (excerpt == NULL)
            goto out;
        }
      else
        {
          /* Don't know how to handle unknown params so fail completely */
          goto out;
        }
      /* advance to next value in FETCH response list, if any */
      while (params[n] == ' ')
        n++;
    }

  if (!has_uid || rfc822_headers == NULL || excerpt == NULL)
    goto out;

  /* OK, message is valid */
  parsed = TRUE;

  uid_str = g_strdup_printf ("%" G_GUINT64_FORMAT,
                             ((guint64) data->uidvalidity << 32) | ((guint64) uid));
  headers = parse_rfc822_headers (rfc822_headers);
  from_header = g_hash_table_lookup (headers, "From");
  subject_header = g_hash_table_lookup (headers, "Subject");
  if (from_header == NULL)
    from_header = "";
  if (subject_header == NULL)
    subject_header = "";

  /* TODO: set this */
  uri = g_strdup ("");

  /* extras is currently unused (and not currently in the D-Bus signature) */
  /* g_variant_builder_init (&extras_builder, G_VARIANT_TYPE_VARDICT); */

  /* Emit D-Bus message */
  goa_mail_monitor_emit_message_received (data->monitor_data->monitor,
                                          uid_str,
                                          from_header,
                                          subject_header,
                                          excerpt,
                                          uri);
  /* g_variant_builder_end (&extras_builder)); */
 out:
  if (!parsed)
    {
      /* Use g_warning() since we want bug-reports to improve the FETCH parser */
      g_warning ("Was unable to parse FETCH response for message with sequence number %d and parameters `%s'. "
                 "Please report this to %s",
                 message_seqnum,
                 params,
                 PACKAGE_BUGREPORT);
    }
  g_free (uri);
  g_free (uid_str);
  if (headers != NULL)
    g_hash_table_unref (headers);
  g_free (rfc822_headers);
  g_free (excerpt);
}

static void
imap_client_on_untagged_response (GoaBackendImapClient  *client,
                                  const gchar           *response,
                                  gpointer               user_data)
{
  ImapClientData *data = user_data;
  gchar s[64+1];
  gint i;
  gint n;

  if (sscanf (response, "%d %64s", &i, s) == 2 && g_strcmp0 (s, "EXISTS") == 0)
    {
      data->num_exists = i;
    }
  else if (sscanf (response, "%d %64s", &i, s) == 2 && g_strcmp0 (s, "EXPUNGE") == 0)
    {
      /* See http://tools.ietf.org/html/rfc3501#section-7.4.1 */
      data->num_exists -= 1;
      data->last_num_exists -= 1;
    }
  else if (sscanf (response, "OK [UIDVALIDITY %d]", &i) == 1)
    {
      data->uidvalidity = i;
    }
  else if (sscanf (response, "%d %64s%n", &i, s, &n) == 2 && g_strcmp0 (s, "FETCH") == 0)
    {
      const gchar *params;
      params = response + n;
      while (g_ascii_isspace (*params))
        params++;
      imap_client_handle_fetch_response (data, i, params);
    }
  else
    {
      /* g_debug ("unhandled untagged response `%s'", response); */
    }
}

static void
imap_client_sync_single (ImapClientData *data)
{
  GError *error;
  gchar *response;
  GoaBackendImapClient *client;

  /* Get ourselves an IMAP client and connect to the server */
  data->num_exists = -1;
  client = goa_backend_imap_client_new ();
  error = NULL;
  if (!goa_backend_imap_client_connect_sync (client,
                                             data->monitor_data->mail->host_and_port,
                                             data->monitor_data->mail->use_tls,
                                             data->monitor_data->mail->auth,
                                             NULL, /* GCancellable */
                                             &error))
    goto out;

  /* Houston, we have a connection */
  goa_mail_monitor_set_connected (data->monitor_data->monitor, TRUE);

  g_signal_connect (client,
                    "untagged-response",
                    G_CALLBACK (imap_client_on_untagged_response),
                    data);

  /* First, select the INBOX - this is guaranteed to emit the EXISTS untagged response */
  error = NULL;
  response = goa_backend_imap_client_run_command_sync (client,
                                                       "SELECT INBOX",
                                                       NULL, /* GCancellable */
                                                       &error);
  if (response == NULL)
    goto out;
  g_free (response);

  if (data->num_exists == -1)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Expected EXISTS untagged response for SELECT but received none");
      goto out;
    }
  data->last_num_exists = data->num_exists;

  /* This is the main loop where we idle, then refresh, then idle,
   * then refresh again and around and around she goes...
   */
  while (TRUE)
    {
      /* If the connection is closed/severed, this is the way we find out since
       * the IDLE command submitted above disables timeouts
       */
      response = goa_backend_imap_client_run_command_sync (client,
                                                           "NOOP",
                                                           NULL, /* GCancellable */
                                                           &error);
      if (response == NULL)
        goto out;
      g_free (response);

      /* Fetch newly received messages, if any - the D-Bus signal will
       * get emitted from imap_client_handle_fetch_response() that
       * will be called while the command is pending
       */
      if (data->num_exists > data->last_num_exists)
        {
          GString *request_str;
          guint num_new_messages;
          guint n;

          num_new_messages = data->num_exists - data->last_num_exists;
          request_str = g_string_new ("FETCH ");
          for (n = 0; n < num_new_messages; n++)
            {
              if (n > 0)
                g_string_append_c (request_str, ',');
              g_string_append_printf (request_str, "%d", data->last_num_exists + 1 + n);
            }

          g_string_append (request_str,
                           " ("
                           "UID "
                           "BODY.PEEK[HEADER.FIELDS (Date From To Cc Subject)] "
                           "BODY.PEEK[TEXT]<0.1000>"
                           ")");
          error = NULL;
          response = goa_backend_imap_client_run_command_sync (client,
                                                               request_str->str,
                                                               NULL, /* GCancellable */
                                                               &error);
          g_string_free (request_str, TRUE);
          if (response == NULL)
            goto out;
          g_free (response);
        }
      data->last_num_exists = data->num_exists;

      /* Wake up waiters */
      g_mutex_lock (data->monitor_data->imap_counter_lock);
      data->monitor_data->imap_num_refreshes += 1;
      g_cond_broadcast (data->monitor_data->imap_counter_cond);
      g_mutex_unlock (data->monitor_data->imap_counter_lock);

      /* Never idle for more than 25 minutes cf. the recommendation
       * in RFC 2177: http://tools.ietf.org/html/rfc2177
       */
      error = NULL;
      if (!goa_backend_imap_client_idle_sync (client,
                                              25 * 1000,
                                              data->monitor_data->imap_cancellable,
                                              &error))
        {
          if (error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED)
            {
              g_cancellable_reset (data->monitor_data->imap_cancellable);
              g_error_free (error);
              error = NULL;
            }
          else
            {
              goto out;
            }
        }

      /* Check if asked to close */
      if (data->monitor_data->imap_request_close)
        goto out;

    } /* Main loop */

 out:
  /* We no longer have a connection */
  goa_mail_monitor_set_connected (data->monitor_data->monitor, FALSE);

  /* Wake up waiters */
  g_mutex_lock (data->monitor_data->imap_counter_lock);
  data->monitor_data->imap_num_connections_failed += 1;
  g_cond_broadcast (data->monitor_data->imap_counter_cond);
  g_mutex_unlock (data->monitor_data->imap_counter_lock);

  if (error != NULL)
    {
      /* g_debug ("leaving serve loop: error: %s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code); */
      g_error_free (error);
    }
  else
    {
      /* g_debug ("leaving serve loop without error"); */
    }
  g_signal_handlers_disconnect_by_func (client,
                                        G_CALLBACK (imap_client_on_untagged_response),
                                        data);
  error = NULL;
  if (!goa_backend_imap_client_disconnect_sync (client,
                                                NULL, /* GCancellable */
                                                &error))
    {
      g_warning ("Error closing connection: %s (%s, %d)",
                 G_STRFUNC,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
  g_object_unref (client);
}

static void
imap_client_sync (MonitorData *data)
{
  ImapClientData *imap_data;

  imap_data = g_slice_new0 (ImapClientData);
  imap_data->monitor_data = monitor_data_ref (data);

  while (TRUE)
    {
      GPollFD poll_fd;

      /* tries connecting - blocks until the connection is closed */
      imap_client_sync_single (imap_data);

      if (data->imap_request_close)
        goto out;

      /* Wait to get woken up */
      if (g_cancellable_make_pollfd (data->imap_cancellable, &poll_fd))
        {
          gint poll_ret;
          do
            {
              poll_ret = g_poll (&poll_fd, 1, -1);
            }
          while (poll_ret == -1 && errno == EINTR);
          g_cancellable_release_fd (data->imap_cancellable);
          g_cancellable_reset (data->imap_cancellable);
        }

      if (data->imap_request_close)
        goto out;
    }

 out:

  /* Wake up waiters (if any) */
  g_mutex_lock (data->imap_counter_lock);
  data->imap_num_refreshes = -1;
  data->imap_num_connections_failed = -1;
  g_cond_broadcast (data->imap_counter_cond);
  g_mutex_unlock (data->imap_counter_lock);

  monitor_data_unref (imap_data->monitor_data);
  g_slice_free (ImapClientData, imap_data);
}


/* ---------------------------------------------------------------------------------------------------- */

/* runs in thread dedicated to the method invocation */
static gboolean
monitor_on_handle_refresh (GoaMailMonitor        *monitor,
                           GDBusMethodInvocation *invocation,
                           gpointer               user_data)
{
  MonitorData *data = user_data;
  gint orig_imap_num_connections_failed;
  gint orig_imap_num_refreshes;
  gboolean refreshed, connection_failed, closed;
  gint num_attempts;

  monitor_data_ref (data);

  num_attempts = 0;
 again:
  g_mutex_lock (data->imap_counter_lock);
  orig_imap_num_refreshes = data->imap_num_refreshes;
  orig_imap_num_connections_failed = data->imap_num_connections_failed;
  /* Wake up the IMAP client thread - this will cause either a connection
   * failure or a refresh
   */
  g_cancellable_cancel (data->imap_cancellable);
  g_cond_wait (data->imap_counter_cond, data->imap_counter_lock);
  num_attempts += 1;

  closed = (orig_imap_num_refreshes == -1);
  refreshed = (orig_imap_num_refreshes != data->imap_num_refreshes);
  connection_failed = (orig_imap_num_connections_failed != data->imap_num_connections_failed);
  g_mutex_unlock (data->imap_counter_lock);

  g_warn_if_fail (refreshed || connection_failed || closed);

  if (refreshed)
    {
      goa_mail_monitor_complete_refresh (monitor, invocation);
      goto out;
    }

  if (closed)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED,
                                             "The monitor was closed");
      goto out;
    }

  /* Try at least three times to cope with broken connections */
  if (connection_failed && num_attempts < 3)
    goto again;

  if (connection_failed)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED,
                                             "Failed to reconnect (tried %d times)",
                                             num_attempts);
      goto out;
    }

  /* should never end up here but if we do, make sure
   * the bug reporters can give us something useful
   */
  g_warning ("Unexpected state while trying to refresh: refreshed=%d connection_failed=%d closed=%d num_attempts=%d",
             refreshed, connection_failed, closed, num_attempts);
  g_dbus_method_invocation_return_error (invocation,
                                         GOA_ERROR,
                                         GOA_ERROR_FAILED,
                                         "Failed with unexpected state: "
                                         "refreshed=%d connection_failed=%d closed=%d num_attempts=%d",
                                         refreshed, connection_failed, closed, num_attempts);

 out:
  monitor_data_unref (data);
  return TRUE; /* invocation was handled */
}

/* runs in thread dedicated to the method invocation */
static gboolean
monitor_on_handle_close (GoaMailMonitor        *monitor,
                         GDBusMethodInvocation *invocation,
                         gpointer               user_data)
{
  MonitorData *data = user_data;
  /* yippee ki yay motherfucker */
  nuke_monitor (data);
  goa_mail_monitor_complete_close (monitor, invocation);
  return TRUE; /* invocation was handled */
}


/* runs in thread dedicated to the method invocation */
static gboolean
handle_create_monitor (GoaMail                *_mail,
                       GDBusMethodInvocation  *invocation)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (_mail);
  gchar *monitor_object_path;
  GError *error;
  MonitorData *data;
  static gint _g_monitor_count = 0;

  monitor_object_path = NULL;

  data = g_slice_new0 (MonitorData);
  data->ref_count = 1;
  data->mail = g_object_ref (mail);
  data->monitor = goa_mail_monitor_skeleton_new ();
  /* Be optimistic that the connection works - if this is not so,
   * imap_client_sync() will clear the flag
   */
  goa_mail_monitor_set_connected (data->monitor, TRUE);
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (data->monitor),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  g_signal_connect (data->monitor,
                    "handle-refresh",
                    G_CALLBACK (monitor_on_handle_refresh),
                    data);
  g_signal_connect (data->monitor,
                    "handle-close",
                    G_CALLBACK (monitor_on_handle_close),
                    data);

  monitor_object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/mail_monitors/%d", _g_monitor_count++);
  error = NULL;
  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (data->monitor),
                                         g_dbus_method_invocation_get_connection (invocation),
                                         monitor_object_path,
                                         &error))
    {
      g_prefix_error (&error, "Error exporting mail monitor: ");
      g_dbus_method_invocation_return_gerror (invocation, error);
      monitor_data_unref (data);
      goto out;
    }

  data->name_watcher_id = g_bus_watch_name_on_connection (g_dbus_method_invocation_get_connection (invocation),
                                                          g_dbus_method_invocation_get_sender (invocation),
                                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                          NULL, /* name_appeared_handler */
                                                          on_monitor_owner_vanished,
                                                          data,
                                                          NULL);

  /* OK, we're in business - finish the invocation
   *
   * TODO: set up things so only caller can access the created object?
   */
  goa_mail_complete_create_monitor (GOA_MAIL (mail), invocation, monitor_object_path);

  /* Create the IMAP client - this blocks our thread until the owner
   * vanishes or the Close() method is called ...
   *
   * The data->imap_cancellable member can be used to wake up the loop
   * to check for data->imap_request_close member or just to
   * refresh...
   */
  data->imap_cancellable = g_cancellable_new ();
  data->imap_counter_lock = g_mutex_new ();
  data->imap_counter_cond = g_cond_new ();
  imap_client_sync (data);

 out:
  g_free (monitor_object_path);
  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_mail__goa_mail_iface_init (GoaMailIface *iface)
{
  iface->handle_create_monitor = handle_create_monitor;
}

/* ---------------------------------------------------------------------------------------------------- */
