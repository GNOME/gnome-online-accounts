/*
 * Copyright (C) 2013 Bastien Nocera <hadess@hadess.net>
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
 * Author: Bastien Nocera <hadess@hadess.net>
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
  GoaAccount *account = NULL;
  GoaOAuth2Based *oauth2 = NULL;
  char *access_token = NULL;
  RestProxy *proxy;
  RestProxyCall *call;
  const char *url;
  const char *tweet_id = NULL;

  if (argc != 2 && argc != 3) {
    g_print ("Usage: %s URL [TWEET ID]\n", argv[0]);
    return 1;
  }
  url = argv[1];
  if (argv[2] != NULL)
    tweet_id = argv[2];

  client = goa_client_new_sync (NULL, &error);
  if (!client) {
    g_error ("Could not create GoaClient: %s", error->message);
    return 1;
  }

  accounts = goa_client_get_accounts (client);

  /* Find a Pocket account */
  for (l = accounts; l != NULL; l = l->next) {
    GoaObject *object = GOA_OBJECT (l->data);

    account = goa_object_peek_account (object);
    if (g_strcmp0 (goa_account_get_provider_type (account), "pocket") == 0) {
      g_object_ref (account);
      oauth2 = goa_object_get_oauth2_based (object);
      break;
    }
  }

  g_list_free_full (accounts, (GDestroyNotify) g_object_unref);

  g_assert (account);
  g_assert (oauth2);

  if (!goa_oauth2_based_call_get_access_token_sync (oauth2, &access_token, NULL, NULL, &error)) {
    g_error ("Could not get access token: %s", error->message);
    return 1;
  }

  g_print ("Got access tokens\n");

  proxy = rest_proxy_new ("https://getpocket.com/", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_set_function (call, "v3/add");
  rest_proxy_call_add_param (call, "consumer_key", goa_oauth2_based_get_client_id (oauth2));
  rest_proxy_call_add_param (call, "access_token", access_token);
  rest_proxy_call_add_param (call, "url", url);
  if (tweet_id)
    rest_proxy_call_add_param (call, "tweet_id", tweet_id);

  g_print ("Adding to Pocket...\n");

  if (!rest_proxy_call_sync (call, &error)) {
    g_error ("Cannot add to Pocket: %s", error->message);
    return 1;
  }

  g_print ("Added!\n");

  return 0;
}
