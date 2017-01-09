/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#include <libsoup/soup.h>

#include "goahttpclient.h"
#include "goasouplogger.h"
#include "goautils.h"

struct _GoaHttpClient
{
  GObject parent_instance;
};

G_DEFINE_TYPE (GoaHttpClient, goa_http_client, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_http_client_init (GoaHttpClient *self)
{
}

static void
goa_http_client_class_init (GoaHttpClientClass *klass)
{
}

/* ---------------------------------------------------------------------------------------------------- */

GoaHttpClient *
goa_http_client_new (void)
{
  return GOA_HTTP_CLIENT (g_object_new (GOA_TYPE_HTTP_CLIENT, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;
  SoupMessage *msg;
  SoupSession *session;
  gboolean accept_ssl_errors;
  gulong cancellable_id;
} CheckData;

typedef struct
{
  gchar *password;
  gchar *username;
} CheckAuthData;

static void
http_client_check_data_free (gpointer user_data)
{
  CheckData *data = user_data;

  if (data->cancellable_id > 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_object_unref (data->cancellable);
    }

  /* soup_session_queue_message stole the references to data->msg */
  g_object_unref (data->session);
  g_slice_free (CheckData, data);
}

static void
http_client_check_auth_data_free (gpointer data, GClosure *closure)
{
  CheckAuthData *auth = data;

  g_free (auth->password);
  g_free (auth->username);
  g_slice_free (CheckAuthData, auth);
}

static void
http_client_authenticate (SoupSession *session,
                         SoupMessage *msg,
                         SoupAuth *auth,
                         gboolean retrying,
                         gpointer user_data)
{
  CheckAuthData *data = user_data;

  if (retrying)
    return;

  soup_auth_authenticate (auth, data->username, data->password);
}

static void
http_client_request_started (SoupSession *session, SoupMessage *msg, SoupSocket *socket, gpointer user_data)
{
  CheckData *data;
  GTask *task = G_TASK (user_data);
  GError *error;
  GTlsCertificateFlags cert_flags;

  data = g_task_get_task_data (task);
  error = NULL;

  if (!data->accept_ssl_errors
      && soup_message_get_https_status (msg, NULL, &cert_flags)
      && cert_flags != 0)
    {
      goa_utils_set_error_ssl (&error, cert_flags);
      g_task_return_error (task, error);
      soup_session_abort (data->session);
    }
}

static void
http_client_check_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
  CheckData *data;
  GTask *task = G_TASK (user_data);
  gboolean cancelled;

  data = g_task_get_task_data (task);

  cancelled = g_task_return_error_if_cancelled (task);
  soup_session_abort (data->session);

  g_return_if_fail (cancelled);
}

static gboolean
http_client_check_free_in_idle (gpointer user_data)
{
  GTask *task = G_TASK (user_data);

  g_object_unref (task);
  return G_SOURCE_REMOVE;
}

static void
http_client_check_response_cb (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
  GError *error;
  GMainContext *context;
  GSource *source;
  GTask *task = G_TASK (user_data);

  error = NULL;

  /* status == SOUP_STATUS_CANCELLED, if we are being aborted by the
   * GCancellable or due to an SSL error. The GTask was already
   * 'returned' by the respective callbacks.
   */
  if (msg->status_code == SOUP_STATUS_CANCELLED)
    goto out;
  else if (msg->status_code != SOUP_STATUS_OK)
    {
      g_warning ("goa_http_client_check() failed: %u — %s", msg->status_code, msg->reason_phrase);
      goa_utils_set_error_soup (&error, msg);
      g_task_return_error (task, error);
      goto out;
    }

  g_task_return_boolean (task, TRUE);

 out:
  /* We might be invoked from a GCancellable::cancelled
   * handler, and unreffing the GTask will disconnect the
   * handler. Since disconnecting from inside the handler will cause a
   * deadlock [1], we use an idle handler to break them up.
   *
   * [1] https://bugzilla.gnome.org/show_bug.cgi?id=705395
   */

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT_IDLE);
  g_source_set_callback (source, http_client_check_free_in_idle, task, NULL);
  g_source_set_name (source, "[goa] http_client_check_free_in_idle");

  context = g_task_get_context (task);
  g_source_attach (source, context);
  g_source_unref (source);
}

void
goa_http_client_check (GoaHttpClient       *self,
                       const gchar         *uri,
                       const gchar         *username,
                       const gchar         *password,
                       gboolean             accept_ssl_errors,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  CheckData *data;
  CheckAuthData *auth;
  GTask *task;
  SoupLogger *logger;

  g_return_if_fail (GOA_IS_HTTP_CLIENT (self));
  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (username != NULL && username[0] != '\0');
  g_return_if_fail (password != NULL && password[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_http_client_check);

  data = g_slice_new0 (CheckData);
  g_task_set_task_data (task, data, http_client_check_data_free);

  data->session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, FALSE,
                                                 NULL);

  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  data->accept_ssl_errors = accept_ssl_errors;
  data->msg = soup_message_new (SOUP_METHOD_GET, uri);

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (http_client_check_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  auth = g_slice_new0 (CheckAuthData);
  auth->username = g_strdup (username);
  auth->password = g_strdup (password);
  g_signal_connect_data (data->session,
                         "authenticate",
                         G_CALLBACK (http_client_authenticate),
                         auth,
                         http_client_check_auth_data_free,
                         0);

  g_signal_connect (data->session, "request-started", G_CALLBACK (http_client_request_started), task);
  soup_session_queue_message (data->session, data->msg, http_client_check_response_cb, g_object_ref (task));

  g_object_unref (task);
}

gboolean
goa_http_client_check_finish (GoaHttpClient *self, GAsyncResult *res, GError **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_HTTP_CLIENT (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_http_client_check, FALSE);

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
http_client_check_sync_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  CheckSyncData *data = user_data;

  data->op_res = goa_http_client_check_finish (GOA_HTTP_CLIENT (source_object), res, data->error);
  g_main_loop_quit (data->loop);
}

gboolean
goa_http_client_check_sync (GoaHttpClient       *self,
                            const gchar         *uri,
                            const gchar         *username,
                            const gchar         *password,
                            gboolean             accept_ssl_errors,
                            GCancellable        *cancellable,
                            GError             **error)
{
  CheckSyncData data;
  GMainContext *context = NULL;

  g_return_val_if_fail (GOA_IS_HTTP_CLIENT (self), FALSE);
  g_return_val_if_fail (uri != NULL && uri[0] != '\0', FALSE);
  g_return_val_if_fail (username != NULL && username[0] != '\0', FALSE);
  g_return_val_if_fail (password != NULL && password[0] != '\0', FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  data.error = error;

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);

  goa_http_client_check (self,
                         uri,
                         username,
                         password,
                         accept_ssl_errors,
                         cancellable,
                         http_client_check_sync_cb,
                         &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.op_res;
}
