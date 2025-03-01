/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
 * Contributor: Jan-Michael Brummer <jan-michael.brummer1@volkswagen.de>
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

#include <glib.h>
#include <gio/gio.h>

static char *uri_str = NULL;

static struct
{
  const char *client_id;
}
oauth2_providers[] =
{
#ifdef GOA_GOOGLE_ENABLED
  {
    .client_id = GOA_GOOGLE_CLIENT_ID,
  },
#endif
#ifdef GOA_WINDOWS_LIVE_ENABLED
  {
    .client_id = GOA_WINDOWS_LIVE_CLIENT_ID,
  },
#endif
#ifdef GOA_MS_GRAPH_ENABLED
    {
    .client_id = GOA_MS_GRAPH_CLIENT_ID,
    },
#endif
  { NULL },
};

static gboolean
get_oauth2_provider (const char  *needle,
                     const char **client_out)
{
  g_return_val_if_fail (needle != NULL, FALSE);

  for (unsigned int i = 0; oauth2_providers[i].client_id != NULL; i++)
    {
      if (g_str_equal (needle, oauth2_providers[i].client_id))
        {
          if (client_out)
            *client_out = oauth2_providers[i].client_id;

          return TRUE;
        }
    }

  return FALSE;
}

static void
got_bus_cb (GObject      *source_object,
            GAsyncResult *result,
            gpointer      user_data)
{
  GMainLoop *loop = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GDBusMessage) message = NULL;
  g_autoptr (GVariant) body = NULL;

  connection = g_bus_get_finish (result, &error);
  if (!connection)
    {
      g_warning ("Failed to get D-Bus session: %s", error ? error->message : "Unknown error");
      g_main_loop_quit (loop);
      return;
    }

  message = g_dbus_message_new_method_call ("org.gnome.OnlineAccounts.OAuth2",
                                            "/org/gnome/OnlineAccounts/OAuth2",
                                            "org.gnome.OnlineAccounts.OAuth2",
                                            "Response");
  body = g_variant_new ("(s)", uri_str);

  g_dbus_message_set_body (message, body);
  g_dbus_connection_send_message (connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
  if (error)
      g_warning ("Failed to send D-Bus message: %s", error ? error->message : "Unknown error");

  g_main_loop_quit (loop);
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (GUri) uri = NULL;
  const char *scheme = NULL;
  const char *path = NULL;
  const char *client_id = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GMainLoop) loop = NULL;

  g_set_prgname ("goa-oauth2-handler");

  if (argc < 2)
    {
      g_printerr ("%s: Missing URI\n", argv[0]);
      return EXIT_FAILURE;
    }

  uri = g_uri_parse (argv[1], G_URI_FLAGS_NONE, &error);
  if (uri == NULL)
    {
      g_printerr ("%s: Invalid URI: %s\n", argv[0], error->message);
      return EXIT_FAILURE;
    }

  /* Google apps may use a reverse-DNS form of the client ID as the URI scheme
   * See: https://developers.google.com/identity/protocols/oauth2/native-app
   */
  scheme = g_uri_get_scheme (uri);
  if (scheme != NULL)
    {
      g_auto (GStrv) strv = g_strsplit (scheme, ".", -1);
      g_autoptr (GString) tmp = g_string_new ("");

      for (unsigned int i = 0; strv[i] != NULL; i++)
        {
          if (i > 0)
            g_string_prepend_c (tmp, '.');
          g_string_prepend (tmp, strv[i]);
        }

      get_oauth2_provider (tmp->str, &client_id);
    }

  /* Treat first path segment as client id */
  if (client_id == NULL)
    {
      path = g_uri_get_path (uri);
      if (path != NULL && *path != '\0')
        {
          g_auto (GStrv) strv = NULL;

          strv = g_strsplit (*path == '/' ? path +1 : path, "/", 1);
          client_id = g_strdup (strv[0]);
        }
    }

  if (client_id == NULL)
    {
      g_printerr ("%s: Unknown provider\n", argv[0]);
      return EXIT_FAILURE;
    }


  uri_str = argv[1];
  loop = g_main_loop_new (NULL, FALSE);

  g_bus_get (G_BUS_TYPE_SESSION, NULL, got_bus_cb, loop);

  g_main_loop_run (loop);

  return EXIT_SUCCESS;
}
