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

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauth2provider.h"
#include "goabackendfacebookprovider.h"

/**
 * GoaBackendFacebookProvider:
 *
 * The #GoaBackendFacebookProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendFacebookProvider
{
  /*< private >*/
  GoaBackendOAuth2Provider parent_instance;
};

typedef struct _GoaBackendFacebookProviderClass GoaBackendFacebookProviderClass;

struct _GoaBackendFacebookProviderClass
{
  GoaBackendOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goabackendfacebookprovider
 * @title: GoaBackendFacebookProvider
 * @short_description: A provider for Facebook
 *
 * #GoaBackendFacebookProvider is used for handling Facebook accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendFacebookProvider, goa_backend_facebook_provider, GOA_TYPE_BACKEND_OAUTH2_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "facebook",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaBackendProvider *_provider)
{
  return "facebook";
}

static const gchar *
get_name (GoaBackendProvider *_provider)
{
  return _("Facebook Account");
}

static const gchar *
get_dialog_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://www.facebook.com/dialog/oauth";
}


static const gchar *
get_authorization_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://graph.facebook.com/oauth/access_token";
}


static const gchar *
get_redirect_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth2?callback=1";
}

static const gchar *
get_scope (GoaBackendOAuth2Provider *provider)
{
  /* see https://developers.facebook.com/docs/authentication/permissions/ */
  return
    "user_events,"
    "read_mailbox,"
    "offline_access";
}

static const gchar *
get_client_id (GoaBackendOAuth2Provider *provider)
{
  return "103995033022129";
}

static const gchar *
get_client_secret (GoaBackendOAuth2Provider *provider)
{
  return "c3a9f8d49188a6dd8b596df48a90b94a";
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
get_identity (GoaBackendOAuth2Provider  *provider,
              const gchar               *access_token,
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

  proxy = rest_proxy_new ("https://graph.facebook.com/me", FALSE);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "GET");
  rest_proxy_call_add_param (data->call, "access_token", access_token);

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
get_identity_finish (GoaBackendOAuth2Provider  *provider,
                     GAsyncResult              *res,
                     GError                   **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetIdentityData *data;
  gchar *ret;
  JsonParser *parser;
  JsonObject *json_object;

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
                   _("Expected status 200 when requesting email address, instead got status %d (%s)"),
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
  ret = g_strdup (json_object_get_string_member (json_object, "username"));
  if (ret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find username member in JSON data"));
      goto out;
    }

 out:
  if (parser != NULL)
    g_object_unref (parser);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_facebook_provider_build_object (GoaBackendProvider  *provider,
                                            GoaObjectSkeleton   *object,
                                            GKeyFile            *key_file,
                                            const gchar         *group,
                                            GError             **error)
{
  GoaAccount *account;
  GoaFacebookAccount *facebook_account;
  gboolean ret;
  gchar *user_name;

  user_name = NULL;
  account = NULL;
  facebook_account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_BACKEND_PROVIDER_CLASS (goa_backend_facebook_provider_parent_class)->build_object (provider,
                                                                                              object,
                                                                                              key_file,
                                                                                              group,
                                                                                              error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  facebook_account = goa_object_get_facebook_account (GOA_OBJECT (object));
  if (facebook_account == NULL)
    {
      facebook_account = goa_facebook_account_skeleton_new ();
      goa_object_skeleton_set_facebook_account (object, facebook_account);
    }

  user_name = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (user_name == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   user_name,
                   goa_account_get_id (account));
      goto out;
    }

  goa_facebook_account_set_user_name (facebook_account, user_name);

  ret = TRUE;

 out:
  if (facebook_account != NULL)
    g_object_unref (facebook_account);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_facebook_provider_init (GoaBackendFacebookProvider *client)
{
}

static void
goa_backend_facebook_provider_class_init (GoaBackendFacebookProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = goa_backend_facebook_provider_build_object;

  oauth2_class = GOA_BACKEND_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_dialog_uri           = get_dialog_uri;
  oauth2_class->get_authorization_uri    = get_authorization_uri;
  oauth2_class->get_redirect_uri         = get_redirect_uri;
  oauth2_class->get_scope                = get_scope;
  oauth2_class->get_client_id            = get_client_id;
  oauth2_class->get_client_secret        = get_client_secret;
  oauth2_class->get_identity             = get_identity;
  oauth2_class->get_identity_finish      = get_identity_finish;
}
