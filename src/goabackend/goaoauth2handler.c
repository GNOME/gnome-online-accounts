/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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
#include <libsecret/secret.h>


static const SecretSchema oauth2_schema =
{
  .name = "org.gnome.OnlineAccounts.OAuth2",
  .flags = SECRET_SCHEMA_NONE,
  .attributes = {
    {
      .name = "goa-oauth2-client",
      .type = SECRET_SCHEMA_ATTRIBUTE_STRING,
    },
    {
      .name = "goa-oauth2-provider",
      .type = SECRET_SCHEMA_ATTRIBUTE_STRING,
    },
    { "NULL", 0 }
  }
};

static struct
{
  const char *client_id;
  const char *provider;
}
oauth2_providers[] =
{
#ifdef GOA_GOOGLE_ENABLED
  {
    .client_id = GOA_GOOGLE_CLIENT_ID,
    .provider = GOA_GOOGLE_NAME,
  },
#endif
#ifdef GOA_WINDOWS_LIVE_ENABLED
  {
    .client_id = GOA_WINDOWS_LIVE_CLIENT_ID,
    .provider = GOA_WINDOWS_LIVE_NAME,
  },
#endif
#ifdef GOA_MS_GRAPH_ENABLED
    {
    .client_id = GOA_MS_GRAPH_CLIENT_ID,
    .provider = GOA_MS_GRAPH_NAME,
    },
#endif
  { NULL, NULL },
};

static gboolean
get_oauth2_provider (const char  *needle,
                     const char **client_out,
                     const char **provider_out)
{
  g_return_val_if_fail (needle != NULL, FALSE);

  for (unsigned int i = 0; oauth2_providers[i].client_id != NULL; i++)
    {
      if (g_str_equal (needle, oauth2_providers[i].client_id))
        {
          if (client_out)
            *client_out = oauth2_providers[i].client_id;

          if (provider_out)
            *provider_out = oauth2_providers[i].provider;

          return TRUE;
        }
    }

  return FALSE;
}

int
main (int    argc,
      char **argv)
{
  g_autoptr (GUri) uri = NULL;
  const char *scheme = NULL;
  const char *path = NULL;
  const char *client_id = NULL;
  const char *provider_type = NULL;
  g_autoptr (GError) error = NULL;

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

      get_oauth2_provider (tmp->str, &client_id, &provider_type);
    }

  /* Windows Live uses goa-oauth2:// with the client ID as the first path segment
   */
  if (client_id == NULL)
    {
      path = g_uri_get_path (uri);
      if (path != NULL && *path != '\0')
        {
          g_auto (GStrv) strv = NULL;

          strv = g_strsplit (*path == '/' ? path +1 : path, "/", 1);
          get_oauth2_provider (strv[0], &client_id, &provider_type);
        }
    }

  if (client_id == NULL)
    {
      g_printerr ("%s: Unknown provider\n", argv[0]);
      return EXIT_FAILURE;
    }

  if (!secret_password_store_sync (&oauth2_schema,
                                   SECRET_COLLECTION_SESSION,
                                   "GNOME Online Accounts OAuth2 URI",
                                   argv[1], /* Secret */
                                   NULL,
                                   &error,
                                   "goa-oauth2-client", client_id,
                                   "goa-oauth2-provider", provider_type,
                                   NULL))
    {
      if (error != NULL)
        g_printerr ("%s: Failed to store OAuth2 URI: %s\n", argv[0], error->message);

      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
