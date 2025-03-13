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

#define OAUTH2_DBUS_HANDLER_NAME  "org.gnome.OnlineAccounts.OAuth2"
#define OAUTH2_DBUS_HANDLER_PATH  "/org/gnome/OnlineAccounts/OAuth2"
#define OAUTH2_DBUS_HANDLER_IFACE "org.gnome.OnlineAccounts.OAuth2"

int
main (int    argc,
      char **argv)
{
  g_autoptr (GUri) uri = NULL;
  const char *scheme = NULL;
  const char *path = NULL;
  const char *client_id = NULL;
  g_autoptr (GDBusConnection) connection = NULL;
  g_autoptr (GVariant) reply = NULL;
  g_autoptr (GError) error = NULL;

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
  if (scheme != NULL && strstr (scheme, "."))
    {
      g_auto (GStrv) strv = g_strsplit (scheme, ".", -1);
      GString *tmp = g_string_new ("");

      for (unsigned int i = 0; strv[i] != NULL; i++)
        {
          if (i > 0)
            g_string_prepend_c (tmp, '.');
          g_string_prepend (tmp, strv[i]);
        }

      client_id = g_string_free (tmp, FALSE);
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

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL)
    {
      g_printerr ("Failed to get session bus: %s\n", error->message);
      return EXIT_FAILURE;
    }

  reply = g_dbus_connection_call_sync (connection,
                                       OAUTH2_DBUS_HANDLER_NAME,
                                       OAUTH2_DBUS_HANDLER_PATH,
                                       OAUTH2_DBUS_HANDLER_IFACE,
                                       "Response",
                                       g_variant_new ("(ss)", client_id, argv[1] /* redirect URI */),
                                       NULL,
                                       G_DBUS_CALL_FLAGS_NO_AUTO_START, /* not applicable */
                                       -1,
                                       NULL,
                                       &error);
  if (reply == NULL)
    {
      g_printerr ("Failed to forward authorization response: %s\n", error->message);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
