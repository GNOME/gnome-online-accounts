/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2017 Red Hat, Inc.
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

#include "goarestproxy.h"
#include "goasouplogger.h"

struct _GoaRestProxy
{
  RestProxy parent_instance;
};

G_DEFINE_TYPE (GoaRestProxy, goa_rest_proxy, REST_TYPE_PROXY);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_rest_proxy_constructed (GObject *object)
{
  GoaRestProxy *self = GOA_REST_PROXY (object);
  SoupLogger *logger = NULL;

  G_OBJECT_CLASS (goa_rest_proxy_parent_class)->constructed (object);

  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  rest_proxy_add_soup_feature (REST_PROXY (self), SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);
}

static void
goa_rest_proxy_init (GoaRestProxy *self)
{
}

static void
goa_rest_proxy_class_init (GoaRestProxyClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = goa_rest_proxy_constructed;
}

/* ---------------------------------------------------------------------------------------------------- */

RestProxy *
goa_rest_proxy_new (const gchar  *url_format,
                    gboolean      binding_required)
{
  return g_object_new (GOA_TYPE_REST_PROXY, "url-format", url_format, "binding-required", binding_required, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  gboolean success;
  GError **error;
} CallSyncData;

static void
goa_rest_proxy_call_sync_cb (RestProxyCall *call,
                             GAsyncResult  *result,
                             gpointer       user_data)
{
  CallSyncData *data = (CallSyncData *)user_data;

  data->success = rest_proxy_call_invoke_finish (call, result, data->error);
  g_main_loop_quit (data->loop);
}

/**
 * goa_rest_proxy_call_sync:
 * @call: a `RestProxyCall`
 * @cancellable: (nullable): a cancellable for the operation
 * @error: (nullable): a return location for an error
 *
 * An alternative to [method@Rest.ProxyCall.sync] that takes a [class@Gio.Cancellable] argument.
 *
 * Returns: %TRUE, or %FALSE with @error set
 */
gboolean
goa_rest_proxy_call_sync (RestProxyCall  *call,
                          GCancellable   *cancellable,
                          GError        **error)
{
  CallSyncData data;
  GMainContext *context = NULL;

  g_return_val_if_fail (REST_IS_PROXY_CALL (call), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);
  data.error = error;

  rest_proxy_call_invoke_async (call,
                                cancellable,
                                (GAsyncReadyCallback) goa_rest_proxy_call_sync_cb,
                                &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.success;
}

