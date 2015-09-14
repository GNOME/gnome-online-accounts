/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Willem van Engen <gnome@willem.engen.nl>
 * Copyright (C) 2012 Red Hat, Inc.
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
#include <glib/gi18n-lib.h>

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauthprovider.h"
#include "goaflickrprovider.h"

/**
 * GoaFlickrProvider:
 *
 * The #GoaFlickrProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaFlickrProvider
{
  /*< private >*/
  GoaOAuthProvider parent_instance;
};

typedef struct _GoaFlickrProviderClass GoaFlickrProviderClass;

struct _GoaFlickrProviderClass
{
  GoaOAuthProviderClass parent_class;
};

/**
 * SECTION:goaflickrprovider
 * @title: GoaFlickrProvider
 * @short_description: A provider for Flickr
 *
 * #GoaFlickrProvider is used for handling Flickr accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaFlickrProvider, goa_flickr_provider, GOA_TYPE_OAUTH_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_FLICKR_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return GOA_FLICKR_NAME;
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Flickr"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED | GOA_PROVIDER_FEATURE_PHOTOS;
}

static const gchar *
get_consumer_key (GoaOAuthProvider *provider)
{
  return GOA_FLICKR_CONSUMER_KEY;
}

static const gchar *
get_consumer_secret (GoaOAuthProvider *provider)
{
  return GOA_FLICKR_CONSUMER_SECRET;
}

static const gchar *
get_request_uri (GoaOAuthProvider *provider)
{
  return "https://m.flickr.com/services/oauth/request_token";
}

static const gchar *
get_authorization_uri (GoaOAuthProvider *provider)
{
  return "https://m.flickr.com/services/oauth/authorize";
}

static const gchar *
get_token_uri (GoaOAuthProvider *provider)
{
  return "https://m.flickr.com/services/oauth/access_token";
}

static const gchar *
get_callback_uri (GoaOAuthProvider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth";
}

static const gchar *
get_authentication_cookie (GoaOAuthProvider *provider)
{
  return "cookie_session";
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
                                      "https://api.flickr.com/services/rest",
                                      FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_add_param (call, "method", "flickr.test.login");
  rest_proxy_call_add_param (call, "format", "json");
  rest_proxy_call_add_param (call, "nojsoncallback", "1");
  rest_proxy_call_set_method (call, "GET");

  if (!rest_proxy_call_sync (call, error))
    goto out;
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting your identity, instead got status %d (%s)"),
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
      g_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
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
  json_object = json_object_get_object_member (json_object, "user");
  if (json_object == NULL)
    {
      g_warning ("Did not find user in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  id = g_strdup (json_object_get_string_member (json_object, "id"));
  if (id == NULL)
    {
      g_warning ("Did not find user.id in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  json_object = json_object_get_object_member (json_object, "username");
  if (json_object == NULL)
    {
      g_warning ("Did not find user.username in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  presentation_identity = g_strdup (json_object_get_string_member (json_object, "_content"));
  if (presentation_identity == NULL)
    {
      g_warning ("Did not find user.username._content in JSON data");
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
  g_clear_object (&parser);
  g_clear_error (&identity_error);
  g_clear_object (&call);
  g_clear_object (&proxy);
  g_free (id);
  g_free (presentation_identity);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuthProvider *provider, WebKitDOMNode *node)
{
  WebKitDOMElement *element;
  gboolean ret;
  gchar *id;

  id = NULL;
  ret = FALSE;

  if (!WEBKIT_DOM_IS_HTML_ANCHOR_ELEMENT (node))
    goto out;

  element = WEBKIT_DOM_ELEMENT (node);
  id = webkit_dom_element_get_id (element);
  if (g_strcmp0 (id, "auth-disallow") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (id);
  return ret;
}

static gboolean
is_identity_node (GoaOAuthProvider *provider, WebKitDOMHTMLInputElement *element)
{
  /* Flickr does not provide a way to query the string used by the
   * user to log in via the web interface. The user id and username
   * returned by flickr.test.login [1] are not what we are looking
   * for.
   *
   * [1] http://www.flickr.com/services/api/flickr.test.login.html
   */
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
parse_request_token_error (GoaOAuthProvider *provider, RestProxyCall *call)
{
  const gchar *payload;
  gchar *msg;
  guint status;

  msg = NULL;

  payload = rest_proxy_call_get_payload (call);
  status = rest_proxy_call_get_status_code (call);

  if (status == 401 && g_strcmp0 (payload, "oauth_problem=timestamp_refused") == 0)
    msg = g_strdup (_("Your system time is invalid. Check your date and time settings."));

  return msg;
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
  GoaAccount *account;
  GoaPhotos *photos;
  gboolean photos_enabled;
  gboolean ret;

  account = NULL;
  photos = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_flickr_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Photos */
  photos = goa_object_get_photos (GOA_OBJECT (object));
  photos_enabled = g_key_file_get_boolean (key_file, group, "PhotosEnabled", NULL);

  if (photos_enabled)
    {
      if (photos == NULL)
        {
          photos = goa_photos_skeleton_new ();
          goa_object_skeleton_set_photos (object, photos);
        }
    }
  else
    {
      if (photos != NULL)
        goa_object_skeleton_set_photos (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_photos_disabled (account, !photos_enabled);

      g_signal_connect (account,
                        "notify::photos-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "PhotosEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&photos);
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *grid,
              G_GNUC_UNUSED GtkGrid *dummy)
{
  gint row;

  row = 0;

  goa_util_add_account_info (grid, row++, object);

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   /* Translators: This is a label for a series of
                                                    * options switches. For example: “Use for Mail”. */
                                                   _("Use for"),
                                                   "photos-disabled",
                                                   _("_Photos"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuthProvider  *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "PhotosEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_flickr_provider_init (GoaFlickrProvider *client)
{
}

static void
goa_flickr_provider_class_init (GoaFlickrProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuthProviderClass *oauth_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type     = get_provider_type;
  provider_class->get_provider_name     = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object          = build_object;
  provider_class->show_account          = show_account;

  oauth_class = GOA_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity_sync        = get_identity_sync;
  oauth_class->is_deny_node             = is_deny_node;
  oauth_class->is_identity_node         = is_identity_node;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_authentication_cookie = get_authentication_cookie;
  oauth_class->parse_request_token_error = parse_request_token_error;
  oauth_class->add_account_key_values    = add_account_key_values;
}
