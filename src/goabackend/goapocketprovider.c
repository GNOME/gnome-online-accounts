/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2013 – 2017 Red Hat, Inc.
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

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauth2provider.h"
#include "goaoauth2provider-priv.h"
#include "goapocketprovider.h"
#include "goaobjectskeletonutils.h"
#include "goarestproxy.h"

#define V3_OAUTH_AUTHORIZE_URL "https://getpocket.com/v3/oauth/authorize"

struct _GoaPocketProvider
{
  GoaOAuth2Provider parent_instance;

  gchar *authorization_uri;

  /* request token as gathered from Step 2:
   * http://getpocket.com/developer/docs/authentication */
  gchar *code;
  gchar *identity;
};

typedef struct _GoaPocketProviderClass GoaPocketProviderClass;

struct _GoaPocketProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

G_DEFINE_TYPE_WITH_CODE (GoaPocketProvider, goa_pocket_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_POCKET_NAME,
                                                         0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_POCKET_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_strdup (_("Pocket"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_READ_LATER;
}

static const gchar *
get_request_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://getpocket.com/v3/oauth/request";
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://getpocket.com/auth/authorize";
}

static const gchar *
get_token_uri (GoaOAuth2Provider *oauth2_provider)
{
  return NULL;
}

static const gchar *
get_redirect_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://localhost";
}

static const gchar *
get_client_id (GoaOAuth2Provider *oauth2_provider)
{
  return GOA_POCKET_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *oauth2_provider)
{
  return NULL;
}

static gchar *
build_authorization_uri (GoaOAuth2Provider  *oauth2_provider,
                         const gchar        *authorization_uri,
                         const gchar        *escaped_redirect_uri,
                         const gchar        *escaped_client_id,
                         const gchar        *escaped_scope)
{
  GoaPocketProvider *self = GOA_POCKET_PROVIDER (oauth2_provider);
  RestProxy *proxy = NULL;
  RestProxyCall *call = NULL;
  const gchar *payload;
  gchar *code, *url = NULL;
  GError *error = NULL;
  GHashTable *hash;

  if (self->authorization_uri != NULL)
    goto end;

  proxy = goa_rest_proxy_new (get_request_uri (oauth2_provider), FALSE);
  call = rest_proxy_new_call (proxy);

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "consumer_key", GOA_POCKET_CLIENT_ID);
  rest_proxy_call_add_param (call, "redirect_uri", get_redirect_uri (oauth2_provider));

  if (!rest_proxy_call_sync (call, &error))
    {
      g_debug ("Call to %s failed: %s", get_redirect_uri (oauth2_provider), error->message);
      g_error_free (error);
      goto out;
    }

  payload = rest_proxy_call_get_payload (call);
  hash = soup_form_decode (payload);
  code = g_strdup (g_hash_table_lookup (hash, "code"));
  g_hash_table_unref (hash);

  if (!code)
    {
      g_debug ("Failed to get code from answer to %s", get_redirect_uri (oauth2_provider));
      goto out;
    }

  self->authorization_uri = g_strdup_printf ("%s"
                                             "?request_token=%s"
                                             "&redirect_uri=%s",
                                             authorization_uri,
                                             code,
                                             escaped_redirect_uri);

  self->code = code;

 end:
  url = g_strdup (self->authorization_uri);

 out:
  g_clear_object (&call);
  g_clear_object (&proxy);
  return url;
}

static gboolean
decide_navigation_policy (GoaOAuth2Provider               *oauth2_provider,
                          WebKitWebView                   *web_view,
                          WebKitNavigationPolicyDecision  *decision)
{
  GoaPocketProvider *self = GOA_POCKET_PROVIDER (oauth2_provider);
  WebKitNavigationAction *action;
  WebKitURIRequest *request;
  gboolean ret = FALSE;
  const gchar *uri;

  action = webkit_navigation_policy_decision_get_navigation_action (decision);
  request = webkit_navigation_action_get_request (action);
  uri = webkit_uri_request_get_uri (request);
  if (!g_str_has_prefix (uri, "https://getpocket.com/a/"))
    goto out;

  webkit_uri_request_set_uri (request, self->authorization_uri);
  webkit_web_view_load_request (web_view, request);

  ret = TRUE;

 out:
  return ret;
}

static gboolean
process_redirect_url (GoaOAuth2Provider            *oauth2_provider,
                      const gchar                  *redirect_url,
                      gchar                       **access_token,
                      GError                      **error)
{
  GoaPocketProvider *self = GOA_POCKET_PROVIDER (oauth2_provider);
  RestProxy *proxy;
  RestProxyCall *call;
  GHashTable *hash;
  const gchar *payload;
  gboolean ret = FALSE;

  proxy = goa_rest_proxy_new (V3_OAUTH_AUTHORIZE_URL, FALSE);
  call = rest_proxy_new_call (proxy);

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "consumer_key", GOA_POCKET_CLIENT_ID);
  rest_proxy_call_add_param (call, "code", self->code);

  if (!rest_proxy_call_sync (call, error))
    goto out;

  payload = rest_proxy_call_get_payload (call);
  hash = soup_form_decode (payload);
  self->identity = g_strdup (g_hash_table_lookup (hash, "username"));
  *access_token = g_strdup (g_hash_table_lookup (hash, "access_token"));
  g_hash_table_unref (hash);

  if (self->identity == NULL|| *access_token == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("No username or access_token"));
      g_clear_pointer (&self->identity, g_free);
      g_clear_pointer (access_token, g_free);
      goto out;
    }

  ret = TRUE;

out:
  g_clear_object (&call);
  g_clear_object (&proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *oauth2_provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  GoaPocketProvider *self = GOA_POCKET_PROVIDER (oauth2_provider);
  if (out_presentation_identity != NULL)
    *out_presentation_identity = g_strdup (self->identity);
  if (!self->identity)
    g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, "Identity is saved to disk already");
  return g_strdup (self->identity);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuth2Provider *oauth2_provider, WebKitDOMNode *node)
{
  WebKitDOMElement *element;
  gboolean ret = FALSE;
  gchar *id = NULL;
  gchar *class = NULL;
  gchar *text = NULL;

  if (!WEBKIT_DOM_IS_ELEMENT (node))
    goto out;

  element = WEBKIT_DOM_ELEMENT (node);

  /* Desktop version */
  id = webkit_dom_element_get_id (element);
  if (g_strcmp0 (id, "denyButton") == 0)
    {
      ret = TRUE;
      goto out;
    }

  /* Mobile version */
  class = webkit_dom_element_get_class_name (element);
  if (g_strcmp0 (class, "toolbarButton") != 0)
    goto out;

  /* FIXME: This only seems to work if we don't click on the "Sign Up"
   * button, does the check need to be done again? */
  text = webkit_dom_node_get_text_content (node);
  if (g_strcmp0 (text, "Cancel") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (id);
  g_free (class);
  g_free (text);
  return ret;
}

static gboolean
is_identity_node (GoaOAuth2Provider *oauth2_provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret = FALSE;
  gchar *name;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "feed_id") != 0)
    goto out;

  ret = TRUE;

out:
  g_free (name);
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
  GoaAccount *account = NULL;
  gboolean read_later_enabled;
  gboolean ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_pocket_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Read Later */
  read_later_enabled = g_key_file_get_boolean (key_file, group, "ReadLaterEnabled", NULL);
  goa_object_skeleton_attach_read_later (object, read_later_enabled);

  if (just_added)
    {
      goa_account_set_read_later_disabled (account, !read_later_enabled);

      g_signal_connect (account,
                        "notify::read-later-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ReadLaterEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider *oauth2_provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "ReadLaterEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_pocket_provider_init (GoaPocketProvider *self)
{
}

static void
goa_pocket_provider_finalize (GObject *object)
{
  GoaPocketProvider *self = GOA_POCKET_PROVIDER (object);

  g_free (self->authorization_uri);
  g_clear_pointer (&self->code, g_free);
  g_clear_pointer (&self->identity, g_free);

  G_OBJECT_CLASS (goa_pocket_provider_parent_class)->finalize (object);
}

static void
goa_pocket_provider_class_init (GoaPocketProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);
  GoaOAuth2ProviderClass *oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);

  object_class->finalize = goa_pocket_provider_finalize;

  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object               = build_object;

  oauth2_class->build_authorization_uri   = build_authorization_uri;
  oauth2_class->decide_navigation_policy  = decide_navigation_policy;
  oauth2_class->get_authorization_uri     = get_authorization_uri;
  oauth2_class->get_token_uri             = get_token_uri;
  oauth2_class->get_redirect_uri          = get_redirect_uri;
  oauth2_class->get_client_id             = get_client_id;
  oauth2_class->get_client_secret         = get_client_secret;
  oauth2_class->get_identity_sync         = get_identity_sync;
  oauth2_class->is_deny_node              = is_deny_node;
  oauth2_class->is_identity_node          = is_identity_node;
  oauth2_class->add_account_key_values    = add_account_key_values;
  oauth2_class->process_redirect_url      = process_redirect_url;
}
