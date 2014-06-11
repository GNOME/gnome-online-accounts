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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <glib-unix.h>

#include <signal.h>
#include <gio/gio.h>

#include "goadaemon.h"
#include "goatpaccountlinker.h"


/* ---------------------------------------------------------------------------------------------------- */

static GMainLoop *loop = NULL;
static gboolean opt_replace = FALSE;
static gboolean opt_no_sigint = FALSE;
static GOptionEntry opt_entries[] =
{
  {"replace", 0, 0, G_OPTION_ARG_NONE, &opt_replace, "Replace existing daemon", NULL},
  {"no-sigint", 0, 0, G_OPTION_ARG_NONE, &opt_no_sigint, "Do not handle SIGINT for controlled shutdown", NULL},
  {NULL }
};
static GoaDaemon *the_daemon = NULL;
static GoaTpAccountLinker *tp_linker = NULL;

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  if (connection != NULL)
    the_daemon = goa_daemon_new ();
  g_debug ("Connected to the session bus");
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_info ("Lost (or failed to acquire) the name %s on the session message bus", name);
  g_main_loop_quit (loop);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("Acquired the name %s on the session message bus", name);

  tp_linker = goa_tp_account_linker_new ();
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
  GOptionContext *opt_context;
  gint ret;
  guint name_owner_id;

  ret = 1;
  loop = NULL;
  opt_context = NULL;
  name_owner_id = 0;

  opt_context = g_option_context_new ("GNOME Online Accounts daemon");
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  error = NULL;
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_critical ("Error parsing options: %s", error->message);
      g_error_free (error);
      goto out;
    }

  g_message ("goa-daemon version %s starting", PACKAGE_VERSION);

  loop = g_main_loop_new (NULL, FALSE);

  if (!opt_no_sigint)
    {
      g_unix_signal_add (SIGINT, on_sigint, NULL);
    }

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
  if (the_daemon != NULL)
    g_object_unref (the_daemon);
  if (tp_linker != NULL)
    g_object_unref (tp_linker);
  if (name_owner_id != 0)
    g_bus_unown_name (name_owner_id);
  if (loop != NULL)
    g_main_loop_unref (loop);
  if (opt_context != NULL)
    g_option_context_free (opt_context);

  g_message ("goa-daemon version %s exiting", PACKAGE_VERSION);

  return ret;
}
