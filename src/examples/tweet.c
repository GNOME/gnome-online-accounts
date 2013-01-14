/*
 * Copyright (C) 2012 Intel Corp
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
 * Author: Ross Burton <ross.burton@intel.com>
 */

#include <config.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

#include <rest/oauth-proxy.h>

int
main (int argc, char **argv)
{
  GError *error = NULL;
  GoaClient *client;
  GList *accounts, *l;
  GoaAccount *account = NULL;
  GoaOAuthBased *oauth = NULL;
  char *access_token = NULL, *access_token_secret = NULL;
  RestProxy *proxy;
  RestProxyCall *call;

  client = goa_client_new_sync (NULL, &error);
  if (!client) {
    g_error ("Could not create GoaClient: %s", error->message);
    return 1;
  }

  accounts = goa_client_get_accounts (client);

  /* Find a twitter account */
  for (l = accounts; l != NULL; l = l->next) {
    GoaObject *object = GOA_OBJECT (l->data);

    account = goa_object_peek_account (object);
    if (g_strcmp0 (goa_account_get_provider_type (account),
                   "twitter") == 0) {
      g_object_ref (account);
      oauth = goa_object_get_oauth_based (object);
      break;
    }
  }

  g_list_free_full (accounts, (GDestroyNotify) g_object_unref);

  g_assert (account);
  g_assert (oauth);

  if (!goa_account_call_ensure_credentials_sync (account, NULL, NULL, &error)) {
    g_error ("Could not ensure credentials are valid: %s", error->message);
    return 1;
  }

  g_print ("Credentials valid\n");

  if (!goa_oauth_based_call_get_access_token_sync (oauth, &access_token, &access_token_secret, NULL, NULL, &error)) {
    g_error ("Could not get access token: %s", error->message);
    return 1;
  }

  g_print ("Got access tokens\n");

  proxy = oauth_proxy_new_with_token
    (goa_oauth_based_get_consumer_key (oauth),
     goa_oauth_based_get_consumer_secret (oauth),
     access_token, access_token_secret,
     "https://api.twitter.com/", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_set_function (call, "1/statuses/update.xml");
  rest_proxy_call_add_param (call, "status", "Hello from GOA+rest");

  g_print ("Tweeting...\n");

  if (!rest_proxy_call_sync (call, &error)) {
    g_error ("Cannot tweet: %s", error->message);
    return 1;
  }

  g_print ("Tweeted!\n");

  return 0;
}
