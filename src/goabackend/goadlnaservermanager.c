/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 Pranav Kant
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>. *
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "goadleynaservermanager.h"
#include "goadleynaservermediadevice.h"
#include "goadlnaservermanager.h"

struct _GoaDlnaServerManager
{
  GObject parent_instance;
  DleynaServerManager *proxy;
  GHashTable *servers;
};

enum
{
  SERVER_FOUND,
  SERVER_LOST,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GoaDlnaServerManager, goa_dlna_server_manager, G_TYPE_OBJECT);

static GObject *goa_dlna_server_manager_singleton = NULL;

static void
goa_dlna_server_manager_server_new_cb (GObject      *source_object,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  GoaDlnaServerManager *self = GOA_DLNA_SERVER_MANAGER (user_data);
  DleynaServerMediaDevice *server;
  GError *error = NULL;
  const gchar *object_path;

  server = dleyna_server_media_device_proxy_new_for_bus_finish (res, &error);
  if (error != NULL)
    {
      g_warning ("Unable to load server object: %s", error->message);
      g_error_free (error);
      goto out;
    }

  object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (server));

  g_debug ("%s '%s' %s %s",
           G_STRFUNC,
           dleyna_server_media_device_get_friendly_name (server),
           dleyna_server_media_device_get_udn (server),
           object_path);
  g_hash_table_insert (self->servers, (gpointer) object_path, server);
  g_signal_emit (self, signals[SERVER_FOUND], 0, server);

 out:
  g_object_unref (self);
}

static void
goa_dlna_server_manager_server_found_cb (GoaDlnaServerManager *self,
                                         const gchar *object_path,
                                         gpointer *data)
{
  dleyna_server_media_device_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                "com.intel.dleyna-server",
                                                object_path,
                                                NULL, /* GCancellable */
                                                goa_dlna_server_manager_server_new_cb,
                                                g_object_ref (self));
}

static void
goa_dlna_server_manager_server_lost_cb (GoaDlnaServerManager *self,
                                        const gchar *object_path,
                                        gpointer *data)
{
  DleynaServerMediaDevice *server;

  server = DLEYNA_SERVER_MEDIA_DEVICE (g_hash_table_lookup (self->servers, object_path));
  g_return_if_fail (server != NULL);

  g_hash_table_steal (self->servers, object_path);
  g_debug ("%s '%s' %s %s",
           G_STRFUNC,
           dleyna_server_media_device_get_friendly_name (server),
           dleyna_server_media_device_get_udn (server),
           object_path);
  g_signal_emit (self, signals[SERVER_LOST], 0, server);
  g_object_unref (server);
}

static void
goa_dlna_server_manager_proxy_get_servers_cb (GObject      *source_object,
                                              GAsyncResult *res,
                                              gpointer      user_data)
{
  GoaDlnaServerManager *self = GOA_DLNA_SERVER_MANAGER (user_data);
  GError *error = NULL;
  gchar **object_paths = NULL;
  guint i;

  dleyna_server_manager_call_get_servers_finish (self->proxy, &object_paths, res, &error);
  if (error != NULL)
    {
      g_warning ("Unable to fetch the list of available servers: %s", error->message);
      g_error_free (error);
      goto out;
    }

  for (i = 0; object_paths[i] != NULL; i++)
    goa_dlna_server_manager_server_found_cb (self, object_paths[i], NULL);

  g_signal_connect_swapped (self->proxy, "found-server", G_CALLBACK (goa_dlna_server_manager_server_found_cb), self);
  g_signal_connect_swapped (self->proxy, "lost-server", G_CALLBACK (goa_dlna_server_manager_server_lost_cb), self);

 out:
  g_strfreev (object_paths);
  g_object_unref (self);
}

static void
goa_dlna_server_manager_proxy_new_cb (GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
  GoaDlnaServerManager *self = GOA_DLNA_SERVER_MANAGER (user_data);
  GError *error = NULL;

  self->proxy = dleyna_server_manager_proxy_new_for_bus_finish (res, &error);
  if (error != NULL)
    {
      g_warning ("Unable to connect to the dLeynaServer.Manager DBus object: %s",
                 error->message);
      g_error_free (error);
      goto out;
    }

  g_debug ("%s DLNA server manager initialized", G_STRFUNC);

  dleyna_server_manager_call_get_servers (self->proxy,
                                          NULL,
                                          goa_dlna_server_manager_proxy_get_servers_cb,
                                          g_object_ref (self));

 out:
  g_object_unref (self);
}

static void
goa_dlna_server_manager_dispose (GObject *object)
{
  GoaDlnaServerManager *self = GOA_DLNA_SERVER_MANAGER (object);

  g_clear_pointer (&self->servers, (GDestroyNotify) g_hash_table_unref);
  g_clear_object (&self->proxy);

  G_OBJECT_CLASS (goa_dlna_server_manager_parent_class)->dispose (object);
}

static GObject *
goa_dlna_server_manager_constructor (GType type,
                                     guint n_construct_params,
                                     GObjectConstructParam *construct_params)
{
  if (goa_dlna_server_manager_singleton == NULL)
    {
      goa_dlna_server_manager_singleton =
        G_OBJECT_CLASS (goa_dlna_server_manager_parent_class)->constructor (type,
                                                                            n_construct_params,
                                                                            construct_params);
      g_object_add_weak_pointer (goa_dlna_server_manager_singleton,
                                 (gpointer) &goa_dlna_server_manager_singleton);
      return goa_dlna_server_manager_singleton;
    }

  return g_object_ref (goa_dlna_server_manager_singleton);
}

static void
goa_dlna_server_manager_init (GoaDlnaServerManager *self)
{
  dleyna_server_manager_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                           G_DBUS_PROXY_FLAGS_NONE,
                                           "com.intel.dleyna-server",
                                           "/com/intel/dLeynaServer",
                                           NULL, /* GCancellable */
                                           goa_dlna_server_manager_proxy_new_cb,
                                           g_object_ref (self));
  self->servers = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
}

static void
goa_dlna_server_manager_class_init (GoaDlnaServerManagerClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->constructor = goa_dlna_server_manager_constructor;
  object_class->dispose = goa_dlna_server_manager_dispose;

  signals[SERVER_FOUND] = g_signal_new ("server-found",
                                        G_TYPE_FROM_CLASS (class),
                                        G_SIGNAL_RUN_LAST,
                                        0,
                                        NULL,
                                        NULL,
                                        g_cclosure_marshal_VOID__OBJECT,
                                        G_TYPE_NONE,
                                        1,
                                        DLEYNA_SERVER_TYPE_MEDIA_DEVICE);

  signals[SERVER_LOST] = g_signal_new ("server-lost",
                                       G_TYPE_FROM_CLASS (class),
                                       G_SIGNAL_RUN_LAST,
                                       0,
                                       NULL,
                                       NULL,
                                       g_cclosure_marshal_VOID__OBJECT,
                                       G_TYPE_NONE,
                                       1,
                                       DLEYNA_SERVER_TYPE_MEDIA_DEVICE);
}

GoaDlnaServerManager *
goa_dlna_server_manager_dup_singleton (void)
{
  return g_object_new (GOA_TYPE_DLNA_SERVER_MANAGER, NULL);
}

GList *
goa_dlna_server_manager_dup_servers (GoaDlnaServerManager *self)
{
  GList *servers;

  servers = g_hash_table_get_values (self->servers);
  g_list_foreach (servers, (GFunc) g_object_ref, NULL);

  return servers;
}
