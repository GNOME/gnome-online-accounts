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
  GSimpleAsyncResult *res;
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

static gboolean
http_client_check_data_free (gpointer user_data)
{
  CheckData *data = user_data;

  g_simple_async_result_complete_in_idle (data->res);

  if (data->cancellable_id > 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_object_unref (data->cancellable);
    }

  /* soup_session_queue_message stole the references to data->msg */
  g_object_unref (data->res);
  g_object_unref (data->session);
  g_slice_free (CheckData, data);

  return G_SOURCE_REMOVE;
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
  CheckData *data = user_data;
  GError *error;
  GTlsCertificateFlags cert_flags;

  error = NULL;

  if (!data->accept_ssl_errors
      && soup_message_get_https_status (msg, NULL, &cert_flags)
      && cert_flags != 0)
    {
      goa_utils_set_error_ssl (&error, cert_flags);
      g_simple_async_result_take_error (data->res, error);
      soup_session_abort (data->session);
    }
}

static void
http_client_check_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
  CheckData *data = user_data;
  soup_session_abort (data->session);
}

static void
http_client_check_response_cb (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
  GError *error;
  CheckData *data = user_data;
  GMainContext *context;
  GSource *source;
  gboolean op_res;

  error = NULL;
  op_res = FALSE;

  /* status == SOUP_STATUS_CANCELLED, if we are being aborted by the
   * GCancellable or due to an SSL error.
   */
  if (msg->status_code == SOUP_STATUS_CANCELLED)
    goto out;
  else if (msg->status_code != SOUP_STATUS_OK)
    {
      g_warning ("goa_http_client_check() failed: %u — %s", msg->status_code, msg->reason_phrase);
      goa_utils_set_error_soup (&error, msg);
      goto out;
    }

  op_res = TRUE;

 out:
  /* error == NULL, if we are being aborted by the GCancellable or
   * due to an SSL error.
   */
  g_simple_async_result_set_op_res_gboolean (data->res, op_res);
  if (error != NULL)
    g_simple_async_result_take_error (data->res, error);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT_IDLE);
  g_source_set_callback (source, http_client_check_data_free, data, NULL);
  g_source_set_name (source, "[goa] http_client_check_data_free");

  context = g_main_context_get_thread_default ();
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
  SoupLogger *logger;

  g_return_if_fail (GOA_IS_HTTP_CLIENT (self));
  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (username != NULL && username[0] != '\0');
  g_return_if_fail (password != NULL && password[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (CheckData);
  data->res = g_simple_async_result_new (G_OBJECT (self), callback, user_data, goa_http_client_check);
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
      data->cancellable_id = g_cancellable_connect (data->cancellable,
                                                    G_CALLBACK (http_client_check_cancelled_cb),
                                                    data,
                                                    NULL);
      g_simple_async_result_set_check_cancellable (data->res, data->cancellable);
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

  g_signal_connect (data->session, "request-started", G_CALLBACK (http_client_request_started), data);
  soup_session_queue_message (data->session, data->msg, http_client_check_response_cb, data);
}

gboolean
goa_http_client_check_finish (GoaHttpClient *self, GAsyncResult *res, GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (GOA_IS_HTTP_CLIENT (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (self), goa_http_client_check), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (res);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return g_simple_async_result_get_op_res_gboolean (simple);
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
