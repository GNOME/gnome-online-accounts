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
#include <glib/gi18n-lib.h>

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauthprovider.h"
#include "goabackendtwitterprovider.h"

/**
 * GoaBackendTwitterProvider:
 *
 * The #GoaBackendTwitterProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendTwitterProvider
{
  /*< private >*/
  GoaBackendOAuthProvider parent_instance;
};

typedef struct _GoaBackendTwitterProviderClass GoaBackendTwitterProviderClass;

struct _GoaBackendTwitterProviderClass
{
  GoaBackendOAuthProviderClass parent_class;
};

/**
 * SECTION:goabackendtwitterprovider
 * @title: GoaBackendTwitterProvider
 * @short_description: A provider for Twitter
 *
 * #GoaBackendTwitterProvider is used for handling Twitter accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendTwitterProvider, goa_backend_twitter_provider, GOA_TYPE_BACKEND_OAUTH_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "twitter",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaBackendProvider *_provider)
{
  return "twitter";
}

static const gchar *
get_name (GoaBackendProvider *_provider)
{
  return _("Twitter Account");
}

static const gchar *
get_consumer_key (GoaBackendOAuthProvider *provider)
{
  return "tlVEAXvkgqr0VUFyqVQ";
}

static const gchar *
get_consumer_secret (GoaBackendOAuthProvider *provider)
{
  return "RN2FBARWy7scDmWFwfhIA6Qwf6kPYxZ0PIpVWzgpdU";
}

static const gchar *
get_request_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/request_token";
}

static gchar **
get_request_uri_params (GoaBackendOAuthProvider *provider)
{
  return NULL;
  GPtrArray *p;
  p = g_ptr_array_new ();
  g_ptr_array_add (p, g_strdup ("xoauth_displayname"));
  g_ptr_array_add (p, g_strdup ("GNOME"));

  g_ptr_array_add (p, g_strdup ("scope"));
  g_ptr_array_add (p, g_strdup (
    /* Display email address: cf. https://sites.twitter.com/site/oauthgoog/Home/emaildisplayscope */
    "https://www.twitterapis.com/auth/userinfo#email "
    /* IMAP, SMTP access: http://code.twitter.com/apis/gmail/oauth/protocol.html */
    "https://mail.twitter.com/ "
    /* Calendar data API: http://code.twitter.com/apis/calendar/data/2.0/developers_guide.html */
    "https://www.twitter.com/calendar/feeds"));
  g_ptr_array_add (p, NULL);
  return (gchar **) g_ptr_array_free (p, FALSE);
}


static const gchar *
get_authorization_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/authorize";
}

static const gchar *
get_token_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/access_token";
}

static const gchar *
get_callback_uri (GoaBackendOAuthProvider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth";
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;
  GSimpleAsyncResult *simple;
  RestProxyCall *call;
  gchar *id;
  gchar *name;
} GetIdentityData;

static GetIdentityData *
get_identity_data_ref (GetIdentityData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
get_identity_data_unref (GetIdentityData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->call);
      g_free (data->id);
      g_free (data->name);
      g_slice_free (GetIdentityData, data);
    }
}

static void
get_identity_cb (RestProxyCall *call,
                 const GError  *passed_error,
                 GObject       *weak_object,
                 gpointer       user_data)
{
  GetIdentityData *data = user_data;
  JsonParser *parser;
  JsonObject *json_object;
  GError *error;

  parser = NULL;

  if (passed_error != NULL)
    {
      g_simple_async_result_set_from_error (data->simple, passed_error);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  if (rest_proxy_call_get_status_code (data->call) != 200)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Expected status 200 when verifying credentials, instead got status %d (%s)"),
                                       rest_proxy_call_get_status_code (data->call),
                                       rest_proxy_call_get_status_message (data->call));
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  parser = json_parser_new ();
  error = NULL;
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (data->call),
                                   rest_proxy_call_get_payload_length (data->call),
                                   &error))
    {
      g_prefix_error (&error, _("Error parsing response as JSON: "));
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  data->id = g_strdup (json_object_get_string_member (json_object, "id_str"));
  if (data->id == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Didn't find id_str in JSON data"));
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }
  data->name = g_strdup (json_object_get_string_member (json_object, "screen_name"));
  if (data->name == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Didn't find screen_name in JSON data"));
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             get_identity_data_ref (data),
                                             (GDestroyNotify) get_identity_data_unref);
  g_simple_async_result_complete_in_idle (data->simple);
  g_object_unref (data->simple);

 out:
  get_identity_data_unref (data);
  if (parser != NULL)
    g_object_unref (parser);
}


static void
get_identity (GoaBackendOAuthProvider   *provider,
              const gchar               *access_token,
              const gchar               *access_token_secret,
              GCancellable              *cancellable,
              GAsyncReadyCallback       callback,
              gpointer                  user_data)
{
  GetIdentityData *data;
  RestProxy *proxy;
  GError *error;

  data = g_slice_new0 (GetIdentityData);
  data->ref_count = 1;
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            get_identity);

  proxy = oauth_proxy_new (goa_backend_oauth_provider_get_consumer_key (provider),
                           goa_backend_oauth_provider_get_consumer_secret (provider),
                           "https://api.twitter.com/1/account/verify_credentials.json",
                           FALSE);
  oauth_proxy_set_token (OAUTH_PROXY (proxy), access_token);
  oauth_proxy_set_token_secret (OAUTH_PROXY (proxy), access_token_secret);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "GET");

  error = NULL;
  if (!rest_proxy_call_async (data->call,
                              get_identity_cb,
                              NULL, /* weak_object */
                              data,
                              &error))
    {
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      get_identity_data_unref (data);
    }
  g_object_unref (proxy);
}


static gchar *
get_identity_finish (GoaBackendOAuthProvider   *provider,
                     gchar                    **out_name,
                     GAsyncResult              *res,
                     GError                   **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetIdentityData *data;
  gchar *ret;

  ret = NULL;

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  data = g_simple_async_result_get_op_res_gpointer (simple);
  ret = g_strdup (data->id);
  if (out_name != NULL)
    *out_name = g_strdup (data->name);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_twitter_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaAccount *account;
  GoaTwitterAccount *twitter_account;
  gboolean ret;
  gchar *id;

  id = NULL;
  account = NULL;
  twitter_account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_BACKEND_PROVIDER_CLASS (goa_backend_twitter_provider_parent_class)->build_object (provider,
                                                                                            object,
                                                                                            key_file,
                                                                                            group,
                                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  twitter_account = goa_object_get_twitter_account (GOA_OBJECT (object));
  if (twitter_account == NULL)
    {
      twitter_account = goa_twitter_account_skeleton_new ();
      goa_object_skeleton_set_twitter_account (object, twitter_account);
    }

  id = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (id == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   id,
                   goa_account_get_id (account));
      goto out;
    }

  goa_twitter_account_set_id (twitter_account, id);

  ret = TRUE;

 out:
  g_free (id);
  if (twitter_account != NULL)
    g_object_unref (twitter_account);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_external_browser (GoaBackendOAuthProvider *provider)
{
  /* For some reason this only works in a browser - bad callback URL? TODO: investigate */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_twitter_provider_init (GoaBackendTwitterProvider *client)
{
}

static void
goa_backend_twitter_provider_class_init (GoaBackendTwitterProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuthProviderClass *oauth_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = goa_backend_twitter_provider_build_object;

  oauth_class = GOA_BACKEND_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity             = get_identity;
  oauth_class->get_identity_finish      = get_identity_finish;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_request_uri_params   = get_request_uri_params;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_use_external_browser = get_use_external_browser;
}
