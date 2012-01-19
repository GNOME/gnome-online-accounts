/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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

#include "goalogging.h"
#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauthprovider.h"
#include "goatwitterprovider.h"

/**
 * GoaTwitterProvider:
 *
 * The #GoaTwitterProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaTwitterProvider
{
  /*< private >*/
  GoaOAuthProvider parent_instance;
};

typedef struct _GoaTwitterProviderClass GoaTwitterProviderClass;

struct _GoaTwitterProviderClass
{
  GoaOAuthProviderClass parent_class;
};

/**
 * SECTION:goatwitterprovider
 * @title: GoaTwitterProvider
 * @short_description: A provider for Twitter
 *
 * #GoaTwitterProvider is used for handling Twitter accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaTwitterProvider, goa_twitter_provider, GOA_TYPE_OAUTH_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "twitter",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "twitter";
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Twitter"));
}

static const gchar *
get_consumer_key (GoaOAuthProvider *provider)
{
  return GOA_TWITTER_CONSUMER_KEY;
}

static const gchar *
get_consumer_secret (GoaOAuthProvider *provider)
{
  return GOA_TWITTER_CONSUMER_SECRET;
}

static const gchar *
get_request_uri (GoaOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/request_token";
}

static const gchar *
get_authorization_uri (GoaOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/authorize";
}

static const gchar *
get_token_uri (GoaOAuthProvider *provider)
{
  return "https://api.twitter.com/oauth/access_token";
}

static const gchar *
get_callback_uri (GoaOAuthProvider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuthProvider  *provider,
                   const gchar       *access_token,
                   const gchar       *access_token_secret,
                   gchar            **out_presentation_identity,
                   GCancellable      *cancellable,
                   GError           **error)
{
  GError *identity_error;
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *id;
  gchar *presentation_identity;

  ret = NULL;

  identity_error = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  id = NULL;
  presentation_identity = NULL;

  /* TODO: cancellable */

  proxy = oauth_proxy_new_with_token (goa_oauth_provider_get_consumer_key (provider),
                                      goa_oauth_provider_get_consumer_secret (provider),
                                      access_token,
                                      access_token_secret,
                                      "https://api.twitter.com/1/account/verify_credentials.json",
                                      FALSE);
  call = rest_proxy_new_call (proxy);

  if (!rest_proxy_call_sync (call, error))
    goto out;
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting guid, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   &identity_error))
    {
      goa_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
                   identity_error->message,
                   g_quark_to_string (identity_error->domain),
                   identity_error->code);
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  id = g_strdup (json_object_get_string_member (json_object, "id_str"));
  if (id == NULL)
    {
      goa_warning ("Did not find id_str in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  presentation_identity = g_strdup (json_object_get_string_member (json_object, "screen_name"));
  if (presentation_identity == NULL)
    {
      goa_warning ("Did not find screen_name in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  ret = id;
  id = NULL;
  if (out_presentation_identity != NULL)
    {
      *out_presentation_identity = presentation_identity;
      presentation_identity = NULL;
    }

 out:
  g_clear_error (&identity_error);
  g_free (id);
  g_free (presentation_identity);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  gboolean ret;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_twitter_provider_parent_class)->build_object (provider,
                                                                             object,
                                                                             key_file,
                                                                             group,
                                                                             connection,
                                                                             just_added,
                                                                             error))
    goto out;

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_external_browser (GoaOAuthProvider *provider)
{
  /* For some reason this only works in a browser - bad callback URL? TODO: investigate */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *left,
              GtkGrid             *right)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_twitter_provider_parent_class)->show_account (provider,
                                                                        client,
                                                                        object,
                                                                        vbox,
                                                                        left,
                                                                        right);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_twitter_provider_init (GoaTwitterProvider *client)
{
}

static void
goa_twitter_provider_class_init (GoaTwitterProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuthProviderClass *oauth_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type     = get_provider_type;
  provider_class->get_provider_name     = get_provider_name;
  provider_class->build_object          = build_object;
  provider_class->show_account          = show_account;

  oauth_class = GOA_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity_sync        = get_identity_sync;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_use_external_browser = get_use_external_browser;
}
