/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2015 Felipe Borges
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

#include <config.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

#include <rest/rest-proxy.h>
#include <rest/rest-proxy-call.h>

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GoaClient *client;
  GList *accounts, *l;
  GoaAccount *account;
  GoaOAuth2Based *oauth2 = NULL;
  char *access_token = NULL;
  RestProxy *proxy;
  RestProxyCall *call;
  gchar *sig, *api_signature;
  const char *user;
  const char *message = NULL;

  if (argc != 2 && argc != 3) {
    g_print ("Usage: %s USER [MESSAGE]\n", argv[0]);
    return 1;
  }
  user = argv[1];
  message = argv[2];

  client = goa_client_new_sync (NULL, &error);
  if (!client) {
    g_error ("Could not create GoaClient: %s", error->message);
    return 1;
  }

  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next) {
    account = goa_object_get_account (GOA_OBJECT (l->data));
    if (g_strcmp0(goa_account_get_provider_name (account), "Last.fm") == 0) {
        g_object_ref (account);
        oauth2 = goa_object_get_oauth2_based (GOA_OBJECT (l->data));
        break;
    }
  }

  g_list_free_full (accounts, (GDestroyNotify) g_object_unref);

  g_assert (account);
  g_assert (oauth2);

  if (!goa_oauth2_based_call_get_access_token_sync (oauth2, &access_token, NULL, NULL, &error)) {
    g_error ("Could not get access token %s\n", error->message);
    return 1;
  }

  g_print ("Got access token\n");

  sig = g_strdup_printf ("api_key%s"
                         "message%s"
                         "methoduser.shout"
                         "sk%s"
                         "user%s%s",
                         goa_oauth2_based_get_client_id (oauth2),
                         message,
                         access_token,
                         user,
                         goa_oauth2_based_get_client_secret (oauth2));

  api_signature = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig, -1);

  proxy = rest_proxy_new ("https://ws.audioscrobbler.com/2.0/", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "api_key", goa_oauth2_based_get_client_id (oauth2));
  rest_proxy_call_add_param (call, "method", "user.shout");
  rest_proxy_call_add_param (call, "message", message);
  rest_proxy_call_add_param (call, "user", user);
  rest_proxy_call_add_param (call, "sk", access_token);
  rest_proxy_call_add_param (call, "api_sig", api_signature);

  if (!rest_proxy_call_sync (call, &error)) {
    g_error ("Cannot shout message to user %s: %s", user, error->message);
    return 1;
  }

  g_print ("Message sent!\n");

  return 0;
}
