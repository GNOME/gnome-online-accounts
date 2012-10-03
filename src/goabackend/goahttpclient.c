/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"
#include <glib.h>
#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>

#include "goalogging.h"
#include "goahttpclient.h"

struct _GoaHttpClient
{
  GObject parent_instance;
};

typedef struct _GoaHttpClientClass GoaHttpClientClass;

struct _GoaHttpClientClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (GoaHttpClient, goa_http_client, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_http_client_init (GoaHttpClient *client)
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
  gulong cancellable_id;
} CheckData;

typedef struct
{
  gchar *password;
  gchar *username;
} CheckAuthData;

static void
http_client_check_data_free (CheckData *data)
{
  if (data->cancellable_id > 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_object_unref (data->cancellable);
    }

  /* soup_session_queue_message stole the references to data->msg */
  g_object_unref (data->res);
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
  gboolean op_res;

  error = NULL;
  op_res = FALSE;

  if (msg->status_code == SOUP_STATUS_CANCELLED)
    goto out;
  else if (msg->status_code != SOUP_STATUS_OK)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Code: %u - Unexpected response from server"),
                   msg->status_code);
      goto out;
    }

  op_res = TRUE;

 out:
  g_simple_async_result_set_op_res_gboolean (data->res, op_res);
  if (error != NULL)
    g_simple_async_result_take_error (data->res, error);

  g_simple_async_result_complete_in_idle (data->res);
  http_client_check_data_free (data);
}

void
goa_http_client_check (GoaHttpClient       *client,
                       const gchar         *uri,
                       const gchar         *username,
                       const gchar         *password,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  CheckData *data;
  CheckAuthData *auth;

  g_return_if_fail (GOA_IS_HTTP_CLIENT (client));
  g_return_if_fail (uri != NULL || uri[0] != '\0');
  g_return_if_fail (username != NULL || username[0] != '\0');
  g_return_if_fail (password != NULL || password[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (CheckData);
  data->res = g_simple_async_result_new (G_OBJECT (client), callback, user_data, goa_http_client_check);
  data->session = soup_session_async_new_with_options (SOUP_SESSION_USE_THREAD_CONTEXT, TRUE,
                                                       NULL);

  data->msg = soup_message_new (SOUP_METHOD_GET, uri);
  soup_message_headers_append (data->msg->request_headers, "Connection", "close");

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

  soup_session_queue_message (data->session, data->msg, http_client_check_response_cb, data);
}

gboolean
goa_http_client_check_finish (GoaHttpClient *client, GAsyncResult *res, GError **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (client), goa_http_client_check), FALSE);
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
goa_http_client_check_sync (GoaHttpClient       *client,
                            const gchar         *uri,
                            const gchar         *username,
                            const gchar         *password,
                            GCancellable        *cancellable,
                            GError             **error)
{
  CheckSyncData data;
  GMainContext *context = NULL;

  data.error = error;

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);

  goa_http_client_check (client,
                         uri,
                         username,
                         password,
                         cancellable,
                         http_client_check_sync_cb,
                         &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.op_res;
}
