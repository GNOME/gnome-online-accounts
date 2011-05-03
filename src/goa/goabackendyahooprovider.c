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
#include "goabackendyahooprovider.h"

/**
 * GoaBackendYahooProvider:
 *
 * The #GoaBackendYahooProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendYahooProvider
{
  /*< private >*/
  GoaBackendOAuthProvider parent_instance;
};

typedef struct _GoaBackendYahooProviderClass GoaBackendYahooProviderClass;

struct _GoaBackendYahooProviderClass
{
  GoaBackendOAuthProviderClass parent_class;
};

/**
 * SECTION:goabackendyahooprovider
 * @title: GoaBackendYahooProvider
 * @short_description: A provider for Yahoo
 *
 * #GoaBackendYahooProvider is used for handling Yahoo accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendYahooProvider, goa_backend_yahoo_provider, GOA_TYPE_BACKEND_OAUTH_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "yahoo",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaBackendProvider *_provider)
{
  return "yahoo";
}

static const gchar *
get_name (GoaBackendProvider *_provider)
{
  return _("Yahoo Account");
}

static const gchar *
get_consumer_key (GoaBackendOAuthProvider *provider)
{
  return "dj0yJmk9VnBYMGpGRVFBUVl3JmQ9WVdrOWNWZDZiVTUwTldNbWNHbzlPVFF5TURrNE5UWXkmcz1jb25zdW1lcnNlY3JldCZ4PTQ0";
}

static const gchar *
get_consumer_secret (GoaBackendOAuthProvider *provider)
{
  return "33dd9ebe9f5724deabe657eff1de7c3f151cf7eb";
}

static const gchar *
get_request_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.login.yahoo.com/oauth/v2/get_request_token";
}

static const gchar *
get_authorization_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.login.yahoo.com/oauth/v2/request_auth";
}

static const gchar *
get_token_uri (GoaBackendOAuthProvider *provider)
{
  return "https://api.login.yahoo.com/oauth/v2/get_token";
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
      g_object_unref (data->simple);
      g_object_unref (data->call);
      g_slice_free (GetIdentityData, data);
    }
}

static void
get_identity_cb (RestProxyCall *call,
                 const GError  *error,
                 GObject       *weak_object,
                 gpointer       user_data)
{
  GetIdentityData *data = user_data;
  if (error != NULL)
    {
      g_simple_async_result_set_from_error (data->simple, error);
      goto out;
    }
  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             get_identity_data_ref (data),
                                             (GDestroyNotify) get_identity_data_unref);
 out:
  g_simple_async_result_complete_in_idle (data->simple);
  get_identity_data_unref (data);
}


static void
get_identity (GoaBackendOAuthProvider  *provider,
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

  proxy = oauth_proxy_new_with_token (goa_backend_oauth_provider_get_consumer_key (provider),
                                      goa_backend_oauth_provider_get_consumer_secret (provider),
                                      access_token,
                                      access_token_secret,
                                      "http://social.yahooapis.com/v1/me/guid",
                                      FALSE);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "GET");
  rest_proxy_call_add_param (data->call, "format", "json");

  error = NULL;
  if (!rest_proxy_call_async (data->call,
                              get_identity_cb,
                              NULL, /* weak_object */
                              data,
                              &error))
    {
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete_in_idle (data->simple);
      get_identity_data_unref (data);
    }
  g_object_unref (proxy);
}


static gchar *
get_identity_finish (GoaBackendOAuthProvider  *provider,
                     GAsyncResult              *res,
                     GError                   **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetIdentityData *data;
  gchar *ret;
  JsonParser *parser;
  JsonObject *json_object;
  JsonObject *json_data_object;

  ret = NULL;
  parser = NULL;

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  if (rest_proxy_call_get_status_code (data->call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting guid, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (data->call),
                   rest_proxy_call_get_status_message (data->call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (data->call),
                                   rest_proxy_call_get_payload_length (data->call),
                                   error))
    {
      g_prefix_error (error, _("Error parsing response as JSON: "));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  json_data_object = json_object_get_object_member (json_object, "guid");
  if (json_data_object == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find guid member in JSON data"));
      goto out;
    }
  ret = g_strdup (json_object_get_string_member (json_data_object, "value"));
  if (ret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find value member in JSON data"));
      goto out;
    }
 out:
  if (parser != NULL)
    g_object_unref (parser);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_yahoo_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaAccount *account;
  GoaYahooAccount *yahoo_account;
  gboolean ret;
  gchar *guid;

  guid = NULL;
  account = NULL;
  yahoo_account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_BACKEND_PROVIDER_CLASS (goa_backend_yahoo_provider_parent_class)->build_object (provider,
                                                                                            object,
                                                                                            key_file,
                                                                                            group,
                                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  yahoo_account = goa_object_get_yahoo_account (GOA_OBJECT (object));
  if (yahoo_account == NULL)
    {
      yahoo_account = goa_yahoo_account_skeleton_new ();
      goa_object_skeleton_set_yahoo_account (object, yahoo_account);
    }

  guid = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (guid == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   guid,
                   goa_account_get_id (account));
      goto out;
    }

  goa_yahoo_account_set_guid (yahoo_account, guid);

  ret = TRUE;

 out:
  g_free (guid);
  if (yahoo_account != NULL)
    g_object_unref (yahoo_account);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

static gboolean
get_use_external_browser (GoaBackendOAuthProvider *provider)
{
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_yahoo_provider_init (GoaBackendYahooProvider *client)
{
}

static void
goa_backend_yahoo_provider_class_init (GoaBackendYahooProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuthProviderClass *oauth_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = goa_backend_yahoo_provider_build_object;

  oauth_class = GOA_BACKEND_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity             = get_identity;
  oauth_class->get_identity_finish      = get_identity_finish;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_use_external_browser = get_use_external_browser;
}
