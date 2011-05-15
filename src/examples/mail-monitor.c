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
#include <stdlib.h>

#include "goa/goa.h"

static void
on_message_received (GoaMailMonitor *monitor,
                     const gchar    *guid,
                     const gchar    *from,
                     const gchar    *subject,
                     const gchar    *excerpt,
                     const gchar    *uri,
                     GVariant       *extras,
                     gpointer        user_data)
{
  g_print ("Message Received:\n"
           " GUID:    %s\n"
           " From:    %s\n"
           " Subject: %s\n"
           " URI:     %s\n"
           " Excerpt: %s\n"
           "\n",
           guid,
           from,
           subject,
           uri,
           excerpt);
}

static void
on_notify_connected (GObject    *object,
                     GParamSpec *pspec,
                     gpointer    user_data)
{
  GoaMailMonitor *monitor = GOA_MAIL_MONITOR (object);
  g_print ("Connected: %d\n"
           "\n",
           goa_mail_monitor_get_connected (monitor));
}

int
main (int   argc,
      char *argv[])
{
  gint ret;
  GMainLoop *loop;
  GError *error;
  GoaClient *client;
  GList *accounts;
  GList *l;
  GoaMailMonitor *monitor;
  gchar *opt_account;
  GOptionEntry opt_entries[] =
    {
      { "account", 'a', 0, G_OPTION_ARG_STRING, &opt_account, "The account to monitor", NULL },
      { NULL}
    };
  GOptionContext *opt_context;
  gchar *monitor_object_path;

  ret = 1;
  monitor = NULL;
  client = NULL;
  accounts = NULL;
  opt_account = NULL;
  monitor_object_path = NULL;

  g_type_init ();

  opt_context = g_option_context_new ("goa mail monitor example");
  error = NULL;
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s\n", error->message);
      g_error_free (error);
      goto out;
    }
  if (opt_account == NULL)
    {
      g_printerr ("Incorrect usage, try --help.\n");
      goto out;
    }

  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  client = goa_client_new_sync (NULL, /* GCancellable */
                                &error);
  if (client == NULL)
    {
      g_printerr ("Error creating a GOA client: %s (%s, %d)\n",
                  error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      GoaAccount *account;
      GoaMail *mail;

      account = goa_object_peek_account (object);
      if (account == NULL)
        continue;

      if (!(g_strcmp0 (goa_account_get_id (account), opt_account) == 0 ||
            g_strcmp0 (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), opt_account) == 0))
        continue;

      mail = goa_object_peek_mail (object);
      if (mail == NULL)
        {
          g_printerr ("Given account does not implement the Mail interface\n");
          goto out;
        }

      /* Start monitoring account */
      g_print ("Monitoring incoming messages for %s\n", goa_account_get_id (account));
      error = NULL;
      if (!goa_mail_call_create_monitor_sync (mail,
                                              &monitor_object_path,
                                              NULL, /* GCancellable */
                                              &error))
        {
          g_printerr ("Error creating mail monitor: %s (%s, %d)\n",
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          goto out;
        }

      error = NULL;
      monitor = goa_mail_monitor_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                         G_DBUS_PROXY_FLAGS_NONE,
                                                         "org.gnome.OnlineAccounts",
                                                         monitor_object_path,
                                                         NULL, /* GCancellable */
                                                         &error);
      if (monitor == NULL)
        {
          g_printerr ("Error creating monitor proxy: %s (%s, %d)\n",
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          goto out;
        }
      g_signal_connect (monitor,
                        "notify::connected",
                        G_CALLBACK (on_notify_connected),
                        NULL);
      g_signal_connect (monitor,
                        "message-received",
                        G_CALLBACK (on_message_received),
                        NULL);
      break;
    }

  if (monitor_object_path == NULL)
    {
      g_printerr ("Didn't find requested account.\n");
      goto out;
    }

  g_main_loop_run (loop);

  ret = 0;

 out:
  if (monitor != NULL)
    g_object_unref (monitor);
  if (client != NULL)
    g_object_unref (client);
  g_free (monitor_object_path);
  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
  g_free (opt_account);
  g_option_context_free (opt_context);
  return ret;
}
