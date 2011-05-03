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
#include "goabackendgoogleprovider.h"

/**
 * GoaBackendGoogleProvider:
 *
 * The #GoaBackendGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendGoogleProvider
{
  /*< private >*/
  GoaBackendOAuthProvider parent_instance;
};

typedef struct _GoaBackendGoogleProviderClass GoaBackendGoogleProviderClass;

struct _GoaBackendGoogleProviderClass
{
  GoaBackendOAuthProviderClass parent_class;
};

/**
 * SECTION:goabackendgoogleprovider
 * @title: GoaBackendGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaBackendGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendGoogleProvider, goa_backend_google_provider, GOA_TYPE_BACKEND_OAUTH_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaBackendProvider *_provider)
{
  return "google";
}

static const gchar *
get_name (GoaBackendProvider *_provider)
{
  return _("Google Account");
}

static const gchar *
get_consumer_key (GoaBackendOAuthProvider *provider)
{
  return "anonymous";
}

static const gchar *
get_consumer_secret (GoaBackendOAuthProvider *provider)
{
  return "anonymous";
}

static const gchar *
get_request_uri (GoaBackendOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthGetRequestToken";
}

static gchar **
get_request_uri_params (GoaBackendOAuthProvider *provider)
{
  GPtrArray *p;
  p = g_ptr_array_new ();
  g_ptr_array_add (p, g_strdup ("xoauth_displayname"));
  g_ptr_array_add (p, g_strdup ("GNOME"));

  g_ptr_array_add (p, g_strdup ("scope"));
  g_ptr_array_add (p, g_strdup (
    /* Display email address: cf. https://sites.google.com/site/oauthgoog/Home/emaildisplayscope */
    "https://www.googleapis.com/auth/userinfo#email "
    /* IMAP, SMTP access: http://code.google.com/apis/gmail/oauth/protocol.html */
    "https://mail.google.com/ "
    /* Calendar data API: http://code.google.com/apis/calendar/data/2.0/developers_guide.html */
    "https://www.google.com/calendar/feeds"));
  g_ptr_array_add (p, NULL);
  return (gchar **) g_ptr_array_free (p, FALSE);
}


static const gchar *
get_authorization_uri (GoaBackendOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthAuthorizeToken";
}

static const gchar *
get_token_uri (GoaBackendOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthGetAccessToken";
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
  JsonObject *json_data_object;
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
                                       _("Expected status 200 when requesting email address, instead got status %d (%s)"),
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
  json_data_object = json_object_get_object_member (json_object, "data");
  if (json_data_object == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Didn't find data member in JSON data"));
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }
  data->id = g_strdup (json_object_get_string_member (json_data_object, "email"));
  if (data->id == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Didn't find email member in JSON data"));
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
                           "https://www.googleapis.com/userinfo/email",
                           FALSE);
  oauth_proxy_set_token (OAUTH_PROXY (proxy), access_token);
  oauth_proxy_set_token_secret (OAUTH_PROXY (proxy), access_token_secret);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "GET");
  rest_proxy_call_add_param (data->call, "alt", "json");

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
                     gchar                   **out_name,
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
    *out_name = g_strdup (data->id);

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_google_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaAccount *account;
  GoaGoogleAccount *google_account;
  gboolean ret;
  gchar *email_address;

  email_address = NULL;
  account = NULL;
  google_account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_BACKEND_PROVIDER_CLASS (goa_backend_google_provider_parent_class)->build_object (provider,
                                                                                            object,
                                                                                            key_file,
                                                                                            group,
                                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  google_account = goa_object_get_google_account (GOA_OBJECT (object));
  if (google_account == NULL)
    {
      google_account = goa_google_account_skeleton_new ();
      goa_object_skeleton_set_google_account (object, google_account);
    }

  email_address = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (email_address == NULL /* || !is_valid_email_address () */)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   email_address,
                   goa_account_get_id (account));
      goto out;
    }

  goa_google_account_set_email_address (google_account, email_address);

  ret = TRUE;

 out:
  g_free (email_address);
  if (google_account != NULL)
    g_object_unref (google_account);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_external_browser (GoaBackendOAuthProvider *provider)
{
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_google_provider_init (GoaBackendGoogleProvider *client)
{
}

static void
goa_backend_google_provider_class_init (GoaBackendGoogleProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuthProviderClass *oauth_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = goa_backend_google_provider_build_object;

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
