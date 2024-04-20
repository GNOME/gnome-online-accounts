/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2020 Red Hat, Inc.
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

#include <glib-unix.h>

#include <locale.h>
#include <signal.h>
#include <gio/gio.h>
#include <libintl.h>

#include "goadaemon.h"

/* ---------------------------------------------------------------------------------------------------- */

static GMainLoop *loop = NULL;
static gboolean opt_replace = FALSE;
static gboolean opt_version = FALSE;
static GOptionEntry opt_entries[] =
{
  {"replace", 0, 0, G_OPTION_ARG_NONE, &opt_replace, "Replace existing daemon", NULL},
  {"version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print version", NULL},
  {NULL }
};
static GoaDaemon *the_daemon = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (name != NULL && name[0] != '\0');

  the_daemon = goa_daemon_new (connection);
  g_debug ("Connected to the session bus");
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_return_if_fail (connection == NULL || G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (name != NULL && name[0] != '\0');

  g_info ("Lost (or failed to acquire) the name %s on the session message bus", name);
  g_main_loop_quit (loop);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
  g_return_if_fail (name != NULL && name[0] != '\0');

  g_debug ("Acquired the name %s on the session message bus", name);
}

static gboolean
on_sigint (gpointer user_data)
{
  g_info ("Caught SIGINT. Initiating shutdown.");
  g_main_loop_quit (loop);
  return FALSE;
}

int
main (int    argc,
      char **argv)
{
  GError *error;
  GOptionContext *opt_context = NULL;
  gint ret = 1;
  guint name_owner_id = 0;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  opt_context = g_option_context_new ("GNOME Online Accounts daemon");
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  error = NULL;
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_critical ("Error parsing options: %s", error->message);
      g_error_free (error);
      goto out;
    }

  if (opt_version)
    {
      g_print ("goa-daemon %s\n", PACKAGE_VERSION);
      return EXIT_SUCCESS;
    }

  g_message ("goa-daemon version %s starting", PACKAGE_VERSION);

  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGINT, on_sigint, NULL);

  name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                  "org.gnome.OnlineAccounts",
                                  G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                    (opt_replace ? G_BUS_NAME_OWNER_FLAGS_REPLACE : 0),
                                  on_bus_acquired,
                                  on_name_acquired,
                                  on_name_lost,
                                  NULL,
                                  NULL);

  g_debug ("Entering main event loop");

  g_main_loop_run (loop);

  ret = 0;

 out:
  g_clear_object (&the_daemon);
  if (name_owner_id != 0)
    g_bus_unown_name (name_owner_id);
  g_clear_pointer (&loop, g_main_loop_unref);
  g_clear_pointer (&opt_context, g_option_context_free);

  g_message ("goa-daemon version %s exiting", PACKAGE_VERSION);

  return ret;
}
