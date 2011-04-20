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
#include <glib/gi18n.h>

#include "goadaemon.h"

struct _GoaDaemon
{
  GObject parent_instance;

  GDBusConnection *connection;

  GDBusObjectManagerServer *object_manager;

  GoaManager *manager;
};

typedef struct
{
  GObjectClass parent_class;
} GoaDaemonClass;


G_DEFINE_TYPE (GoaDaemon, goa_daemon, G_TYPE_OBJECT);

static void
goa_daemon_finalize (GObject *object)
{
  GoaDaemon *daemon = GOA_DAEMON (object);

  g_object_unref (daemon->manager);
  g_object_unref (daemon->object_manager);
  g_object_unref (daemon->connection);

  G_OBJECT_CLASS (goa_daemon_parent_class)->finalize (object);
}

static void
goa_daemon_init (GoaDaemon *daemon)
{
  static volatile GQuark goa_error_domain = 0;
  GDBusObjectSkeleton *object;

  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_domain = GOA_ERROR;
  goa_error_domain; /* shut up -Wunused-but-set-variable */

  /* TODO: maybe nicer to pass in a GDBusConnection* construct property */
  daemon->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  /* Create object manager */
  daemon->object_manager = g_dbus_object_manager_server_new (daemon->connection, "/org/gnome/OnlineAccounts");

  /* Create and export Manager */
  daemon->manager = goa_manager_skeleton_new ();
  /* TODO: connect to signals for handling AddAccount()/RemoveAccount() methods */
  object = g_dbus_object_skeleton_new ("/org/gnome/OnlineAccounts/Manager");
  g_dbus_object_skeleton_add_interface (object, G_DBUS_INTERFACE_SKELETON (daemon->manager));
  g_dbus_object_manager_server_export (daemon->object_manager, object);
  g_object_unref (object);
}

static void
goa_daemon_class_init (GoaDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_daemon_finalize;
}

GoaDaemon *
goa_daemon_new (void)
{
  return GOA_DAEMON (g_object_new (GOA_TYPE_DAEMON, NULL));
}
