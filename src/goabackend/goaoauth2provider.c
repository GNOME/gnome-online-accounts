/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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
#include <stdlib.h>

#include <rest/oauth2-proxy.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <webkit2/webkit2.h>

#include "goaprovider.h"
#include "goautils.h"
#include "goawebview.h"
#include "goaoauth2provider.h"
#include "goaoauth2provider-priv.h"
#include "goaoauth2provider-web-extension.h"
#include "goaoauth2provider-web-view.h"
#include "goarestproxy.h"

/**
 * SECTION:goaoauth2provider
 * @title: GoaOAuth2Provider
 * @short_description: Abstract base class for OAuth 2.0 providers
 *
 * #GoaOAuth2Provider is an abstract base class for <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15">OAuth
 * 2.0</ulink> based providers.
 *
 * Subclasses must implement
 * #GoaOAuth2ProviderClass.get_authorization_uri,
 * #GoaOAuth2ProviderClass.get_token_uri,
 * #GoaOAuth2ProviderClass.get_redirect_uri,
 * #GoaOAuth2ProviderClass.get_scope,
 * #GoaOAuth2ProviderClass.get_client_id,
 * #GoaOAuth2ProviderClass.get_client_secret and
 * #GoaOAuth2ProviderClass.get_identity_sync methods.
 *
 * Additionally, the
 * #GoaProviderClass.get_provider_type,
 * #GoaProviderClass.get_provider_name,
 * #GoaProviderClass.build_object (this should chain up to its
 * parent class) methods must be implemented.
 *
 * Note that the #GoaProviderClass.add_account,
 * #GoaProviderClass.refresh_account and
 * #GoaProviderClass.ensure_credentials_sync methods do not
 * need to be implemented - this type implements these methods..
 */

struct _GoaOAuth2ProviderPrivate
{
  GtkDialog *dialog;
  GError *error;
  GMainLoop *loop;

  const gchar *existing_identity;

  gchar *account_object_path;

  gchar *authorization_code;
  gchar *access_token;
  gint   access_token_expires_in;
  gchar *refresh_token;
  gchar *identity;
  gchar *presentation_identity;
  gchar *password;
};

G_LOCK_DEFINE_STATIC (provider_lock);

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GoaOAuth2Provider, goa_oauth2_provider, GOA_TYPE_PROVIDER);

static gboolean
is_authorization_error (GError *error)
{
  gboolean ret;

  g_return_val_if_fail (error != NULL, FALSE);

  ret = FALSE;
  if (error->domain == REST_PROXY_ERROR || error->domain == SOUP_HTTP_ERROR)
    {
      if (SOUP_STATUS_IS_CLIENT_ERROR (error->code))
        ret = TRUE;
    }
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_get_use_mobile_browser_default (GoaOAuth2Provider  *self)
{
  return FALSE;
}

/**
 * goa_oauth2_provider_get_use_mobile_browser:
 * @self: A #GoaOAuth2Provider.
 *
 * Returns whether there is a need for the embedded browser to identify
 * itself as running on a mobile phone in order to get a more compact
 * version of the approval page.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if the embedded browser should identify itself as
 * running on a mobile platform, %FALSE otherwise.
 */
gboolean
goa_oauth2_provider_get_use_mobile_browser (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_use_mobile_browser (self);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_is_deny_node_default (GoaOAuth2Provider *self, WebKitDOMNode *node)
{
  return FALSE;
}

/**
 * goa_oauth2_provider_is_deny_node:
 * @self: A #GoaOAuth2Provider.
 * @node: A WebKitDOMNode.
 *
 * Checks whether @node is the HTML UI element that the user can use
 * to deny permission to access his account. Usually they are either a
 * WebKitDOMHTMLButtonElement or a WebKitDOMHTMLInputElement.
 *
 * Please note that providers may have multiple such elements in their
 * UI and this method should catch all of them.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if the @node can be used to deny permission.
 */
gboolean
goa_oauth2_provider_is_deny_node (GoaOAuth2Provider *self, WebKitDOMNode *node)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->is_deny_node (self, node);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_is_password_node_default (GoaOAuth2Provider *self, WebKitDOMHTMLInputElement *element)
{
  return FALSE;
}

/**
 * goa_oauth2_provider_is_password_node:
 * @self: A #GoaOAuth2Provider.
 * @element: A WebKitDOMHTMLInputElement
 *
 * Checks whether @element is the HTML UI element that the user can
 * use to enter her password. This can be used to offer a
 * #GoaPasswordBased interface by saving the user's
 * password. Providers usually frown upon doing this, so this is not
 * recommended.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if @element can be used to enter the password.
 */
gboolean
goa_oauth2_provider_is_password_node (GoaOAuth2Provider *self, WebKitDOMHTMLInputElement *element)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  g_return_val_if_fail (WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (element), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->is_password_node (self, element);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth2_provider_add_account_key_values_default (GoaOAuth2Provider *self,
                                                    GVariantBuilder   *builder)
{
  /* do nothing */
}

/**
 * goa_oauth2_provider_add_account_key_values:
 * @self: A #GoaProvider.
 * @builder: A #GVariantBuilder for a <literal>a{ss}</literal> variant.
 *
 * Hook for implementations to add key/value pairs to the key-file
 * when creating an account.
 *
 * This is a virtual method where the default implementation does nothing.
 */
void
goa_oauth2_provider_add_account_key_values (GoaOAuth2Provider  *self,
                                            GVariantBuilder    *builder)
{
  g_return_if_fail (GOA_IS_OAUTH2_PROVIDER (self));
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->add_account_key_values (self, builder);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
goa_oauth2_provider_build_authorization_uri_default (GoaOAuth2Provider  *self,
                                                     const gchar        *authorization_uri,
                                                     const gchar        *escaped_redirect_uri,
                                                     const gchar        *escaped_client_id,
                                                     const gchar        *escaped_scope)
{
  return g_strdup_printf ("%s"
                          "?response_type=code"
                          "&redirect_uri=%s"
                          "&client_id=%s"
                          "&scope=%s",
                          authorization_uri,
                          escaped_redirect_uri,
                          escaped_client_id,
                          escaped_scope);
}

/**
 * goa_oauth2_provider_build_authorization_uri:
 * @self: A #GoaOAuth2Provider.
 * @authorization_uri: An authorization URI.
 * @escaped_redirect_uri: An escaped redirect URI
 * @escaped_client_id: An escaped client id
 * @escaped_scope: (allow-none): The escaped scope or %NULL
 *
 * Builds the URI that can be opened in a web browser (or embedded web
 * browser widget) to start authenticating an user.
 *
 * The default implementation just returns the expected URI
 * (e.g. <literal>http://example.com/dialog/oauth2?response_type=code&redirect_uri=https%3A%2F%2Fclient%2Eexample%2Ecom%2Fcb&client_id=foo&scope=email%20stuff</literal>)
 * - override (and chain up) if you e.g. need to to pass additional
 * parameters.
 *
 * The @authorization_uri, @escaped_redirect_uri, @escaped_client_id
 * and @escaped_scope parameters originate from the result of the
 * the goa_oauth2_provider_get_authorization_uri(), goa_oauth2_provider_get_redirect_uri(), goa_oauth2_provider_get_client_id()
 * and goa_oauth2_provider_get_scope() methods with the latter
 * three escaped using g_uri_escape_string().
 *
 * Returns: (transfer full): An authorization URI that must be freed with g_free().
 */
gchar *
goa_oauth2_provider_build_authorization_uri (GoaOAuth2Provider  *self,
                                             const gchar        *authorization_uri,
                                             const gchar        *escaped_redirect_uri,
                                             const gchar        *escaped_client_id,
                                             const gchar        *escaped_scope)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_redirect_uri != NULL, NULL);
  g_return_val_if_fail (escaped_client_id != NULL, NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->build_authorization_uri (self,
                                                                        authorization_uri,
                                                                        escaped_redirect_uri,
                                                                        escaped_client_id,
                                                                        escaped_scope);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_decide_navigation_policy_default (GoaOAuth2Provider               *self,
                                                      WebKitWebView                   *web_view,
                                                      WebKitNavigationPolicyDecision  *decision)
{
  return FALSE;
}

/*
 * goa_oauth2_provider_decide_navigation_policy_default:
 * @self: A #GoaOAuth2Provider.
 * @decision: A #WebKitNavigationPolicyDecision
 *
 * Certain OAuth2-like, but not exactly <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15">OAuth2</ulink>,
 * providers may not send us to the redirect URI, as expected. They
 * might need some special handling for that. This is a provider
 * specific hook to accommodate them.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if @provider decided what to do with @decision,
 * %FALSE otherwise.
 */
gboolean
goa_oauth2_provider_decide_navigation_policy (GoaOAuth2Provider               *self,
                                              WebKitWebView                   *web_view,
                                              WebKitNavigationPolicyDecision  *decision)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  g_return_val_if_fail (WEBKIT_IS_WEB_VIEW (web_view), FALSE);
  g_return_val_if_fail (WEBKIT_IS_NAVIGATION_POLICY_DECISION (decision), FALSE);

  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->decide_navigation_policy (self, web_view, decision);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_oauth2_provider_process_redirect_url:
 * @self: A #GoaOAuth2Provider.
 * @redirect_url: A redirect URI from the web browser
 * @authorization_code: (out): Return location for access token
 * @error: Return location for error or %NULL
 *
 * Certain OAuth2-like, but not exactly <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15">OAuth2</ulink>,
 * providers do not follow the standard mechanisms for extracting the
 * access token or auth code from the redirect URI. They use some
 * non-standard technique to do so. This is a provider specific hook
 * to accommodate them and will only be used if implemented.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation if needed.
 *
 * Returns: %TRUE if @provider could process @redirect_url, %FALSE
 * otherwise.
 */
gboolean
goa_oauth2_provider_process_redirect_url (GoaOAuth2Provider  *self,
                                          const gchar        *redirect_url,
                                          gchar             **authorization_code,
                                          GError            **error)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  g_return_val_if_fail (redirect_url != NULL, FALSE);
  g_return_val_if_fail (authorization_code != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->process_redirect_url (self, redirect_url, authorization_code, error);
}

/**
 * goa_oauth2_provider_get_authorization_uri:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1">authorization
 * endpoint</ulink> used for authenticating the user and obtaining
 * authorization.
 *
 * You should not include any parameters in the returned URI. If you
 * need to include additional parameters than the standard ones,
 * override #GoaOAuth2ProviderClass.build_authorization_uri -
 * see goa_oauth2_provider_build_authorization_uri() for more
 * details.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_authorization_uri (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_authorization_uri (self);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
goa_oauth2_provider_get_token_uri_default (GoaOAuth2Provider  *self)
{
  return NULL;
}

/**
 * goa_oauth2_provider_get_token_uri:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.2">token
 * endpoint</ulink> used for obtaining an access token.
 *
 * A token URI is only needed when the OAuth2 provider does not support
 * a separate client-side flow. In such cases, override
 * #GoaOAuth2ProviderClass.get_token_uri. You should not include any
 * parameters in the returned URI.
 *
 * This is a virtual method where the default implementation returns
 * %NULL.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_token_uri (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_token_uri (self);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_oauth2_provider_get_redirect_uri:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1.1">redirect_uri</ulink>
 * used when requesting authorization.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_redirect_uri (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_redirect_uri (self);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
goa_oauth2_provider_get_scope_default (GoaOAuth2Provider *self)
{
  return NULL;
}

/**
 * goa_oauth2_provider_get_scope:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1.1">scope</ulink>
 * used when requesting authorization.
 *
 * This is a virtual method where the default implementation returns
 * %NULL.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_scope (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_scope (self);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_oauth2_provider_get_client_id:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-3">client_id</ulink>
 * identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_client_id (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_client_id (self);
}

/**
 * goa_oauth2_provider_get_client_secret:
 * @self: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-3">client_secret</ulink>
 * associated with the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @self - do not free.
 */
const gchar *
goa_oauth2_provider_get_client_secret (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_client_secret (self);
}

/**
 * goa_oauth2_provider_get_identity_sync:
 * @self: A #GoaOAuth2Provider.
 * @access_token: A valid OAuth 2.0 access token.
 * @out_presentation_identity: (out): Return location for presentation identity or %NULL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for @error or %NULL.
 *
 * Method that returns the identity corresponding to
 * @access_token.
 *
 * The identity is needed because all authentication happens out of
 * band. In addition to the identity, an implementation also returns a
 * <emphasis>presentation identity</emphasis> that is more suitable
 * for presentation (the identity could be a GUID for example).
 *
 * The calling thread is blocked while the identity is obtained.
 *
 * Returns: The identity or %NULL if error is set. The returned string
 * must be freed with g_free().
 */
gchar *
goa_oauth2_provider_get_identity_sync (GoaOAuth2Provider    *self,
                                       const gchar          *access_token,
                                       gchar               **out_presentation_identity,
                                       GCancellable         *cancellable,
                                       GError              **error)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  g_return_val_if_fail (access_token != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_identity_sync (self,
                                                                  access_token,
                                                                  out_presentation_identity,
                                                                  cancellable,
                                                                  error);
}

/**
 * goa_oauth2_provider_is_identity_node:
 * @self: A #GoaOAuth2Provider.
 * @element: A WebKitDOMHTMLInputElement.
 *
 * Checks whether @element is the HTML UI element that the user can
 * use to identify herself at the provider.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if the @element can be used to deny permission.
 */
gboolean
goa_oauth2_provider_is_identity_node (GoaOAuth2Provider *self, WebKitDOMHTMLInputElement *element)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->is_identity_node (self, element);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_tokens_sync (GoaOAuth2Provider  *self,
                 const gchar        *authorization_code,
                 const gchar        *refresh_token,
                 gchar             **out_refresh_token,
                 gint               *out_access_token_expires_in,
                 GCancellable       *cancellable,
                 GError            **error)
{
  GError *tokens_error = NULL;
  RestProxy *proxy;
  RestProxyCall *call;
  gchar *ret = NULL;
  guint status_code;
  gchar *ret_access_token = NULL;
  gint ret_access_token_expires_in = 0;
  gchar *ret_refresh_token = NULL;
  const gchar *payload;
  gsize payload_length;
  const gchar *client_secret;

  proxy = goa_rest_proxy_new (goa_oauth2_provider_get_token_uri (self), FALSE);
  call = rest_proxy_new_call (proxy);

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "client_id", goa_oauth2_provider_get_client_id (self));

  client_secret = goa_oauth2_provider_get_client_secret (self);
  if (client_secret != NULL)
    rest_proxy_call_add_param (call, "client_secret", client_secret);

  if (refresh_token != NULL)
    {
      /* Swell, we have a refresh code - just use that */
      rest_proxy_call_add_param (call, "grant_type", "refresh_token");
      rest_proxy_call_add_param (call, "refresh_token", refresh_token);
    }
  else
    {
      /* No refresh code.. request an access token using the authorization code instead */
      rest_proxy_call_add_param (call, "grant_type", "authorization_code");
      rest_proxy_call_add_param (call, "redirect_uri", goa_oauth2_provider_get_redirect_uri (self));
      rest_proxy_call_add_param (call, "code", authorization_code);
    }

  /* TODO: cancellable support? */
  if (!rest_proxy_call_sync (call, error))
    goto out;

  status_code = rest_proxy_call_get_status_code (call);
  if (status_code != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting access token, instead got status %d (%s)"),
                   status_code,
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  payload = rest_proxy_call_get_payload (call);
  payload_length = rest_proxy_call_get_payload_length (call);
  /* some older OAuth2 implementations does not return json - handle that too */
  if (g_str_has_prefix (payload, "access_token="))
    {
      GHashTable *hash;
      const gchar *expires_in_str;

      g_debug ("Response is not JSON - possibly old OAuth2 implementation");

      hash = soup_form_decode (payload);
      ret_access_token = g_strdup (g_hash_table_lookup (hash, "access_token"));
      if (ret_access_token == NULL)
        {
          g_warning ("Did not find access_token in non-JSON data");
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Could not parse response"));
          g_hash_table_unref (hash);
          goto out;
        }
      /* refresh_token is optional */
      ret_refresh_token = g_hash_table_lookup (hash, "refresh_token");
      /* expires_in is optional */
      expires_in_str = g_hash_table_lookup (hash, "expires_in");
      /* sometimes "expires_in" appears as "expires" */
      if (expires_in_str == NULL)
        expires_in_str = g_hash_table_lookup (hash, "expires");
      if (expires_in_str != NULL)
        ret_access_token_expires_in = atoi (expires_in_str);
      g_hash_table_unref (hash);
    }
  else
    {
      JsonParser *parser;
      JsonObject *object;

      parser = json_parser_new ();
      if (!json_parser_load_from_data (parser, payload, payload_length, &tokens_error))
        {
          g_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
                     tokens_error->message,
                     g_quark_to_string (tokens_error->domain),
                     tokens_error->code);
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Could not parse response"));
          g_object_unref (parser);
          goto out;
        }
      object = json_node_get_object (json_parser_get_root (parser));
      if (!json_object_has_member (object, "access_token"))
        {
          g_warning ("Did not find access_token in JSON data");
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Could not parse response"));
          g_object_unref (parser);
          goto out;
        }

      ret_access_token = g_strdup (json_object_get_string_member (object, "access_token"));

      /* refresh_token is optional... */
      if (json_object_has_member (object, "refresh_token"))
        ret_refresh_token = g_strdup (json_object_get_string_member (object, "refresh_token"));
      if (json_object_has_member (object, "expires_in"))
        ret_access_token_expires_in = json_object_get_int_member (object, "expires_in");
      g_object_unref (parser);
    }

  ret = ret_access_token;
  ret_access_token = NULL;
  if (out_access_token_expires_in != NULL)
    *out_access_token_expires_in = ret_access_token_expires_in;
  if (out_refresh_token != NULL)
    {
      *out_refresh_token = ret_refresh_token;
      ret_refresh_token = NULL;
    }

 out:
  g_clear_error (&tokens_error);
  g_free (ret_access_token);
  g_free (ret_refresh_token);
  g_clear_object (&call);
  g_clear_object (&proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_web_view_deny_click (GoaWebView *web_view, gpointer user_data)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (user_data);
  GoaOAuth2ProviderPrivate *priv;

  priv = goa_oauth2_provider_get_instance_private (self);
  gtk_dialog_response (priv->dialog, GTK_RESPONSE_CANCEL);
}

static void
on_web_view_password_submit (GoaWebView *web_view, const gchar *password, gpointer user_data)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (user_data);
  GoaOAuth2ProviderPrivate *priv;

  priv = goa_oauth2_provider_get_instance_private (self);

  g_free (priv->password);
  priv->password = g_strdup (password);
}

static gboolean
on_web_view_decide_policy (WebKitWebView            *web_view,
                           WebKitPolicyDecision     *decision,
                           WebKitPolicyDecisionType  decision_type,
                           gpointer                  user_data)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (user_data);
  GoaOAuth2ProviderPrivate *priv;
  GHashTable *key_value_pairs;
  WebKitNavigationAction *action;
  WebKitURIRequest *request;
  SoupURI *uri;
  const gchar *fragment;
  const gchar *oauth2_error;
  const gchar *query;
  const gchar *redirect_uri;
  const gchar *requested_uri;
  gint response_id = GTK_RESPONSE_NONE;

  priv = goa_oauth2_provider_get_instance_private (self);

  if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
    goto default_behaviour;

  if (goa_oauth2_provider_decide_navigation_policy (self,
                                                    web_view,
                                                    WEBKIT_NAVIGATION_POLICY_DECISION (decision)))
    {
      response_id = 0;
      goto ignore_request;
    }

  /* TODO: use oauth2_proxy_extract_access_token() */

  action = webkit_navigation_policy_decision_get_navigation_action (WEBKIT_NAVIGATION_POLICY_DECISION (decision));
  request = webkit_navigation_action_get_request (action);
  requested_uri = webkit_uri_request_get_uri (request);
  redirect_uri = goa_oauth2_provider_get_redirect_uri (self);
  if (!g_str_has_prefix (requested_uri, redirect_uri))
    goto default_behaviour;

  uri = soup_uri_new (requested_uri);
  fragment = soup_uri_get_fragment (uri);
  query = soup_uri_get_query (uri);

  /* Three cases:
   * 1) we can either have the backend handle the URI for us, or
   * 2) we can either have the access_token and other information
   *    directly in the fragment part of the URI, or
   * 3) the auth code can be in the query part of the URI, with which
   *    we'll obtain the token later.
   */
  if (GOA_OAUTH2_PROVIDER_GET_CLASS (self)->process_redirect_url)
    {
      gchar *url;

      url = soup_uri_to_string (uri, FALSE);
      if (!goa_oauth2_provider_process_redirect_url (self, url, &priv->access_token, &priv->error))
        {
          g_prefix_error (&priv->error, _("Authorization response: "));
          priv->error->domain = GOA_ERROR;
          priv->error->code = GOA_ERROR_NOT_AUTHORIZED;
          response_id = GTK_RESPONSE_CLOSE;
        }
      else
        response_id = GTK_RESPONSE_OK;

      g_free (url);
      goto ignore_request;
    }

  if (fragment != NULL)
    {
      /* fragment is encoded into a key/value pairs for the token and
       * expiration values, using the same syntax as a URL query */
      key_value_pairs = soup_form_decode (fragment);

      /* We might use oauth2_proxy_extract_access_token() here but
       * we can also extract other information.
       */
      priv->access_token = g_strdup (g_hash_table_lookup (key_value_pairs, "access_token"));
      if (priv->access_token != NULL)
        {
          gchar *expires_in_str = NULL;

          expires_in_str = g_hash_table_lookup (key_value_pairs, "expires_in");
          /* sometimes "expires_in" appears as "expires" */
          if (expires_in_str == NULL)
            expires_in_str = g_hash_table_lookup (key_value_pairs, "expires");

          if (expires_in_str != NULL)
            priv->access_token_expires_in = atoi (expires_in_str);

          priv->refresh_token = g_strdup (g_hash_table_lookup (key_value_pairs, "refresh_token"));

          response_id = GTK_RESPONSE_OK;
        }
      g_hash_table_unref (key_value_pairs);
    }

  if (priv->access_token != NULL)
    goto ignore_request;

  if (query != NULL)
    {
      key_value_pairs = soup_form_decode (query);

      priv->authorization_code = g_strdup (g_hash_table_lookup (key_value_pairs, "code"));
      if (priv->authorization_code != NULL)
        response_id = GTK_RESPONSE_OK;

      g_hash_table_unref (key_value_pairs);
    }

  if (priv->authorization_code != NULL)
    goto ignore_request;

  /* In case we don't find the access_token or auth code, then look
   * for the error in the query part of the URI.
   */
  key_value_pairs = soup_form_decode (query);
  oauth2_error = (const gchar *) g_hash_table_lookup (key_value_pairs, "error");
  if (g_strcmp0 (oauth2_error, GOA_OAUTH2_ACCESS_DENIED) == 0)
    response_id = GTK_RESPONSE_CANCEL;
  else
    {
      g_set_error (&priv->error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_AUTHORIZED,
                   _("Authorization response: %s"),
                   oauth2_error);
      response_id = GTK_RESPONSE_CLOSE;
    }
  g_hash_table_unref (key_value_pairs);
  goto ignore_request;

 ignore_request:
  g_assert (response_id != GTK_RESPONSE_NONE);
  if (response_id < 0)
    gtk_dialog_response (priv->dialog, response_id);
  webkit_policy_decision_ignore (decision);
  return TRUE;

 default_behaviour:
  return FALSE;
}

static gboolean
get_tokens_and_identity (GoaOAuth2Provider  *self,
                         gboolean            add_account,
                         const gchar        *existing_identity,
                         GtkDialog          *dialog,
                         GtkBox             *vbox)
{
  GoaOAuth2ProviderPrivate *priv;
  gboolean ret = FALSE;
  gchar *url;
  GtkWidget *embed;
  GtkWidget *grid;
  GtkWidget *web_view;
  const gchar *scope;
  gchar *escaped_redirect_uri = NULL;
  gchar *escaped_client_id = NULL;
  gchar *escaped_scope = NULL;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  g_return_val_if_fail ((!add_account && existing_identity != NULL && existing_identity[0] != '\0')
                        || (add_account && existing_identity == NULL), FALSE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), FALSE);
  g_return_val_if_fail (GTK_IS_BOX (vbox), FALSE);

  priv = goa_oauth2_provider_get_instance_private (self);
  g_return_val_if_fail (priv->error == NULL, FALSE);

  /* TODO: check with NM whether we're online, if not - return error */

  priv->dialog = dialog;
  priv->existing_identity = existing_identity;

  g_clear_pointer (&priv->password, g_free);
  g_clear_pointer (&priv->identity, g_free);
  g_clear_pointer (&priv->presentation_identity, g_free);
  g_clear_pointer (&priv->authorization_code, g_free);
  g_clear_pointer (&priv->access_token, g_free);
  g_clear_pointer (&priv->refresh_token, g_free);

  /* TODO: use oauth2_proxy_build_login_url_full() */
  escaped_redirect_uri = g_uri_escape_string (goa_oauth2_provider_get_redirect_uri (self), NULL, TRUE);
  escaped_client_id = g_uri_escape_string (goa_oauth2_provider_get_client_id (self), NULL, TRUE);
  scope = goa_oauth2_provider_get_scope (self);
  if (scope != NULL)
    escaped_scope = g_uri_escape_string (goa_oauth2_provider_get_scope (self), NULL, TRUE);
  else
    escaped_scope = NULL;
  url = goa_oauth2_provider_build_authorization_uri (self,
                                                     goa_oauth2_provider_get_authorization_uri (self),
                                                     escaped_redirect_uri,
                                                     escaped_client_id,
                                                     escaped_scope);

  goa_utils_set_dialog_title (GOA_PROVIDER (self), dialog, add_account);

  grid = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid);

  web_view = goa_web_view_new (GOA_PROVIDER (self), existing_identity);
  gtk_widget_set_hexpand (web_view, TRUE);
  gtk_widget_set_vexpand (web_view, TRUE);
  embed = goa_web_view_get_view (GOA_WEB_VIEW (web_view));

  if (goa_oauth2_provider_get_use_mobile_browser (self))
    goa_web_view_fake_mobile (GOA_WEB_VIEW (web_view));

  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (embed), url);
  g_signal_connect (embed,
                    "decide-policy",
                    G_CALLBACK (on_web_view_decide_policy),
                    self);
  g_signal_connect (web_view, "deny-click", G_CALLBACK (on_web_view_deny_click), self);
  g_signal_connect (web_view, "password-submit", G_CALLBACK (on_web_view_password_submit), self);

  gtk_container_add (GTK_CONTAINER (grid), web_view);
  gtk_window_set_default_size (GTK_WINDOW (dialog), -1, -1);

  gtk_widget_show_all (GTK_WIDGET (vbox));
  gtk_dialog_run (GTK_DIALOG (dialog));

  /* We can have either the auth code, with which we'll obtain the token, or
   * the token directly if we are using a client side flow, since we don't
   * need to pass the code to the remote application.
   */
  if (priv->authorization_code == NULL && priv->access_token == NULL)
    {
      if (priv->error == NULL)
        {
          g_set_error (&priv->error,
                       GOA_ERROR,
                       GOA_ERROR_DIALOG_DISMISSED,
                       _("Dialog was dismissed"));
        }
      goto out;
    }
  g_assert (priv->error == NULL);

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (priv->authorization_code != NULL)
    {
      /* OK, we now have the authorization code... now we need to get the
       * email address (to e.g. check if the account already exists on
       * @client).. for that we need to get a (short-lived) access token
       * and a refresh_token
       */

      /* TODO: run in worker thread */
      priv->access_token = get_tokens_sync (self,
                                            priv->authorization_code,
                                            NULL, /* refresh_token */
                                            &priv->refresh_token,
                                            &priv->access_token_expires_in,
                                            NULL, /* GCancellable */
                                            &priv->error);
      if (priv->access_token == NULL)
        {
          g_prefix_error (&priv->error, _("Error getting an Access Token: "));
          goto out;
        }
    }

  g_assert (priv->access_token != NULL);

  /* TODO: run in worker thread */
  priv->identity = goa_oauth2_provider_get_identity_sync (self,
                                                          priv->access_token,
                                                          &priv->presentation_identity,
                                                          NULL, /* TODO: GCancellable */
                                                          &priv->error);
  if (priv->identity == NULL)
    {
      g_prefix_error (&priv->error, _("Error getting identity: "));
      goto out;
    }

  ret = TRUE;

 out:
  g_free (url);
  g_free (escaped_redirect_uri);
  g_free (escaped_client_id);
  g_free (escaped_scope);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *res,
                gpointer      user_data)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (user_data);
  GoaOAuth2ProviderPrivate *priv;

  priv = goa_oauth2_provider_get_instance_private (self);

  goa_manager_call_add_account_finish (manager,
                                       &priv->account_object_path,
                                       res,
                                       &priv->error);
  g_main_loop_quit (priv->loop);
}

static gint64
duration_to_abs_usec (gint duration_sec)
{
  gint64 ret;
  GTimeVal now;

  g_get_current_time (&now);
  ret = ((gint64) now.tv_sec) * 1000L * 1000L + ((gint64) now.tv_usec);
  ret += ((gint64) duration_sec) * 1000L * 1000L;
  return ret;
}

static gint
abs_usec_to_duration (gint64 abs_usec)
{
  gint64 ret;
  GTimeVal now;

  g_get_current_time (&now);
  ret = abs_usec - (((gint64) now.tv_sec) * 1000L * 1000L + ((gint64) now.tv_usec));
  ret /= 1000L * 1000L;
  return ret;
}

static void
add_credentials_key_values (GoaOAuth2Provider *self,
                            GVariantBuilder *credentials)
{
  GoaOAuth2ProviderPrivate *priv;

  priv = goa_oauth2_provider_get_instance_private (self);

  if (priv->authorization_code != NULL)
    g_variant_builder_add (credentials, "{sv}", "authorization_code",
                           g_variant_new_string (priv->authorization_code));
  g_variant_builder_add (credentials, "{sv}", "access_token", g_variant_new_string (priv->access_token));
  if (priv->access_token_expires_in > 0)
    g_variant_builder_add (credentials, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (priv->access_token_expires_in)));
  if (priv->refresh_token != NULL)
    g_variant_builder_add (credentials, "{sv}", "refresh_token", g_variant_new_string (priv->refresh_token));
  if (priv->password != NULL)
    g_variant_builder_add (credentials, "{sv}", "password", g_variant_new_string (priv->password));
}

static GoaObject *
goa_oauth2_provider_add_account (GoaProvider *provider,
                                         GoaClient          *client,
                                         GtkDialog          *dialog,
                                         GtkBox             *vbox,
                                         GError            **error)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (provider);
  GoaOAuth2ProviderPrivate *priv;
  GoaObject *ret = NULL;
  GVariantBuilder credentials;
  GVariantBuilder details;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  priv = goa_oauth2_provider_get_instance_private (self);

  if (!get_tokens_and_identity (self, TRUE, NULL, dialog, vbox))
    goto out;

  /* OK, got the identity... see if there's already an account
   * of this type with the given identity
   */
  if (!goa_utils_check_duplicate (client,
                                  priv->identity,
                                  priv->presentation_identity,
                                  goa_provider_get_provider_type (GOA_PROVIDER (self)),
                                  (GoaPeekInterfaceFunc) goa_object_peek_oauth2_based,
                                  &priv->error))
    goto out;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (self, &credentials);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  goa_oauth2_provider_add_account_key_values (self, &details);

  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (GOA_PROVIDER (self)),
                                priv->identity,
                                priv->presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                self);
  priv->loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (priv->loop);
  if (priv->error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      priv->account_object_path));

 out:
  /* We might have an object even when priv->error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (priv->error != NULL)
    {
      g_propagate_error (error, priv->error);
      priv->error = NULL;
    }
  else
    g_assert (ret != NULL);

  g_clear_pointer (&priv->account_object_path, g_free);
  g_clear_pointer (&priv->loop, g_main_loop_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_refresh_account (GoaProvider  *provider,
                                     GoaClient    *client,
                                     GoaObject    *object,
                                     GtkWindow    *parent,
                                     GError      **error)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (provider);
  GoaOAuth2ProviderPrivate *priv;
  GoaAccount *account;
  GtkWidget *dialog;
  const gchar *existing_identity;
  const gchar *existing_presentation_identity;
  GVariantBuilder builder;
  gboolean ret = FALSE;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  priv = goa_oauth2_provider_get_instance_private (self);

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL
                                        | GTK_DIALOG_DESTROY_WITH_PARENT
                                        | GTK_DIALOG_USE_HEADER_BAR,
                                        NULL,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_widget_show_all (dialog);

  account = goa_object_peek_account (object);

  /* We abuse presentation identity here because for some providers
   * identity can be a machine readable ID, which can not be used to
   * log in via the provider's web interface.
   */
  existing_presentation_identity = goa_account_get_presentation_identity (account);
  if (!get_tokens_and_identity (self,
                                FALSE,
                                existing_presentation_identity,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog)))))
    goto out;

  /* Changes made to the web interface by the providers can break our
   * DOM parsing. So we should still query and check the identity
   * afterwards.
   */
  existing_identity = goa_account_get_identity (account);
  if (g_strcmp0 (priv->identity, existing_identity) != 0)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Was asked to log in as %s, but logged in as %s"),
                   existing_identity,
                   priv->identity);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (self, &builder);
  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (self),
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    error))
    goto out;

  goa_account_call_ensure_credentials (goa_object_peek_account (object),
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  if (priv->error != NULL)
    {
      g_propagate_error (error, priv->error);
      priv->error = NULL;
    }

  gtk_widget_destroy (dialog);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
free_mutex (GMutex *mutex)
{
  g_mutex_clear (mutex);
  g_slice_free (GMutex, mutex);
}

/**
 * goa_oauth2_provider_get_access_token_sync:
 * @self: A #GoaOAuth2Provider.
 * @object: A #GoaObject.
 * @force_refresh: If set to %TRUE, forces a refresh of the access token, if possible.
 * @out_access_token_expires_in: (out): Return location for how many seconds the returned token is valid for (0 if unknown) or %NULL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously gets an access token for @object. The calling thread
 * is blocked while the operation is pending.
 *
 * The resulting token is typically read from the local cache so most
 * of the time only a local roundtrip to the storage for the token
 * cache (e.g. <command>gnome-keyring-daemon</command>) is
 * needed. However, the operation may involve refreshing the token
 * with the service provider so a full network round-trip may be
 * needed.
 *
 * Note that multiple calls are serialized to avoid multiple
 * outstanding requests to the service provider.
 *
 * This operation may fail if e.g. unable to refresh the credentials
 * or if network connectivity is not available. Note that even if a
 * token is returned, the returned token isn't guaranteed to work -
 * use goa_provider_ensure_credentials_sync() if you need
 * stronger guarantees.
 *
 * Returns: The access token or %NULL if error is set. The returned
 * string must be freed with g_free().
 */
gchar *
goa_oauth2_provider_get_access_token_sync (GoaOAuth2Provider  *self,
                                           GoaObject          *object,
                                           gboolean            force_refresh,
                                           gint               *out_access_token_expires_in,
                                           GCancellable       *cancellable,
                                           GError            **error)
{
  GVariant *credentials = NULL;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;
  gchar *authorization_code = NULL;
  gchar *access_token = NULL;
  gint access_token_expires_in = 0;
  gchar *refresh_token = NULL;
  gchar *old_refresh_token = NULL;
  gchar *password = NULL;
  gboolean success = FALSE;
  GVariantBuilder builder;
  gchar *ret = NULL;
  GMutex *lock;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* provider_lock is too coarse, use a per-object lock instead */
  G_LOCK (provider_lock);
  lock = g_object_get_data (G_OBJECT (object), "-goa-oauth2-provider-get-access-token-lock");
  if (lock == NULL)
    {
      lock = g_slice_new0 (GMutex);
      g_mutex_init (lock);
      g_object_set_data_full (G_OBJECT (object),
                              "-goa-oauth2-provider-get-access-token-lock",
                              lock,
                              (GDestroyNotify) free_mutex);
    }
  G_UNLOCK (provider_lock);

  g_mutex_lock (lock);

  /* First, get the credentials from the keyring */
  credentials = goa_utils_lookup_credentials_sync (GOA_PROVIDER (self),
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  g_variant_iter_init (&iter, credentials);
  while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
    {
      if (g_strcmp0 (key, "access_token") == 0)
        access_token = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "access_token_expires_at") == 0)
        access_token_expires_in = abs_usec_to_duration (g_variant_get_int64 (value));
      else if (g_strcmp0 (key, "refresh_token") == 0)
        refresh_token = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "authorization_code") == 0)
        authorization_code = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "password") == 0)
        password = g_variant_dup_string (value, NULL);
      g_variant_unref (value);
    }

  if (access_token == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_AUTHORIZED,
                   _("Credentials do not contain access_token"));
      goto out;
    }

  /* if we can't refresh the token, just return it no matter what */
  if (refresh_token == NULL)
    {
      g_debug ("Returning locally cached credentials that cannot be refreshed");
      success = TRUE;
      goto out;
    }

  /* If access_token is still "fresh enough" (e.g. more than ten
   * minutes of life left in it), just return it unless we've been
   * asked to forcibly refresh it
   */
  if (!force_refresh && access_token_expires_in > 10*60)
    {
      g_debug ("Returning locally cached credentials (expires in %d seconds)", access_token_expires_in);
      success = TRUE;
      goto out;
    }

  g_debug ("Refreshing locally cached credentials (expires in %d seconds, force_refresh=%d)", access_token_expires_in, force_refresh);

  /* Otherwise, refresh it */
  old_refresh_token = refresh_token; refresh_token = NULL;
  g_free (access_token); access_token = NULL;
  access_token = get_tokens_sync (self,
                                  authorization_code,
                                  old_refresh_token,
                                  &refresh_token,
                                  &access_token_expires_in,
                                  cancellable,
                                  error);
  if (access_token == NULL)
    {
      if (error != NULL)
        {
          g_prefix_error (error, _("Failed to refresh access token (%s, %d): "),
                          g_quark_to_string ((*error)->domain), (*error)->code);
          (*error)->code = is_authorization_error (*error) ? GOA_ERROR_NOT_AUTHORIZED : GOA_ERROR_FAILED;
          (*error)->domain = GOA_ERROR;
        }
      goto out;
    }

  /* It's not a sure thing we get a new refresh_token, so use our old
   * old if we didn't get a new one
   */
  if (refresh_token == NULL)
    {
      refresh_token = old_refresh_token;
      old_refresh_token = NULL;
    }

  /* Good. Now update the keyring with the refreshed credentials */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "authorization_code", g_variant_new_string (authorization_code));
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (access_token_expires_in)));
  if (refresh_token != NULL)
    g_variant_builder_add (&builder, "{sv}", "refresh_token", g_variant_new_string (refresh_token));
  if (password != NULL)
    g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));

  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (self),
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    cancellable,
                                                    error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  success = TRUE;

 out:
  if (success)
    {
      ret = access_token; access_token = NULL;
      g_assert (ret != NULL);
      if (out_access_token_expires_in != NULL)
        *out_access_token_expires_in = access_token_expires_in;
    }
  g_free (authorization_code);
  g_free (access_token);
  g_free (refresh_token);
  g_free (old_refresh_token);
  g_free (password);
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);

  g_mutex_unlock (lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_access_token (GoaOAuth2Based        *object,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data);

static gboolean
goa_oauth2_provider_build_object (GoaProvider         *provider,
                                  GoaObjectSkeleton   *object,
                                  GKeyFile            *key_file,
                                  const gchar         *group,
                                  GDBusConnection     *connection,
                                  gboolean             just_added,
                                  GError             **error)
{
  GoaOAuth2Based *oauth2_based;

  oauth2_based = goa_object_get_oauth2_based (GOA_OBJECT (object));
  if (oauth2_based != NULL)
    goto out;

  oauth2_based = goa_oauth2_based_skeleton_new ();
  goa_oauth2_based_set_client_id (oauth2_based,
                                    goa_oauth2_provider_get_client_id (GOA_OAUTH2_PROVIDER (provider)));
  goa_oauth2_based_set_client_secret (oauth2_based,
                                       goa_oauth2_provider_get_client_secret (GOA_OAUTH2_PROVIDER (provider)));
  /* Ensure D-Bus method invocations run in their own thread */
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (oauth2_based),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  goa_object_skeleton_set_oauth2_based (object, oauth2_based);
  g_signal_connect (oauth2_based,
                    "handle-get-access-token",
                    G_CALLBACK (on_handle_get_access_token),
                    NULL);

 out:
  g_object_unref (oauth2_based);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_ensure_credentials_sync (GoaProvider   *provider,
                                             GoaObject     *object,
                                             gint          *out_expires_in,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (provider);
  gboolean ret = FALSE;
  gchar *access_token = NULL;
  gint access_token_expires_in;
  gchar *identity = NULL;
  gboolean force_refresh = FALSE;

 again:
  access_token = goa_oauth2_provider_get_access_token_sync (self,
                                                            object,
                                                            force_refresh,
                                                            &access_token_expires_in,
                                                            cancellable,
                                                            error);
  if (access_token == NULL)
    goto out;

  identity = goa_oauth2_provider_get_identity_sync (self,
                                                    access_token,
                                                    NULL, /* out_presentation_identity */
                                                    cancellable,
                                                    error);
  if (identity == NULL)
    {
      /* OK, try again, with forcing the locally cached credentials to be refreshed */
      if (!force_refresh)
        {
          force_refresh = TRUE;
          g_free (access_token); access_token = NULL;
          g_clear_error (error);
          goto again;
        }
      else
        {
          goto out;
        }
    }

  /* TODO: maybe check with the identity we have */
  ret = TRUE;
  if (out_expires_in != NULL)
    *out_expires_in = access_token_expires_in;

 out:
  g_free (identity);
  g_free (access_token);
  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth2_provider_finalize (GObject *object)
{
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (object);
  GoaOAuth2ProviderPrivate *priv;

  priv = goa_oauth2_provider_get_instance_private (self);

  g_clear_pointer (&priv->loop, g_main_loop_unref);

  g_free (priv->account_object_path);
  g_free (priv->password);
  g_free (priv->identity);
  g_free (priv->presentation_identity);
  g_free (priv->authorization_code);
  g_free (priv->access_token);
  g_free (priv->refresh_token);

  G_OBJECT_CLASS (goa_oauth2_provider_parent_class)->finalize (object);
}

static void
goa_oauth2_provider_init (GoaOAuth2Provider *self)
{
}

static void
goa_oauth2_provider_class_init (GoaOAuth2ProviderClass *klass)
{
  GObjectClass *object_class;
  GoaProviderClass *provider_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = goa_oauth2_provider_finalize;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->add_account                = goa_oauth2_provider_add_account;
  provider_class->refresh_account            = goa_oauth2_provider_refresh_account;
  provider_class->build_object               = goa_oauth2_provider_build_object;
  provider_class->ensure_credentials_sync    = goa_oauth2_provider_ensure_credentials_sync;

  klass->build_authorization_uri  = goa_oauth2_provider_build_authorization_uri_default;
  klass->decide_navigation_policy = goa_oauth2_provider_decide_navigation_policy_default;
  klass->get_token_uri            = goa_oauth2_provider_get_token_uri_default;
  klass->get_scope                = goa_oauth2_provider_get_scope_default;
  klass->get_use_mobile_browser   = goa_oauth2_provider_get_use_mobile_browser_default;
  klass->is_deny_node             = goa_oauth2_provider_is_deny_node_default;
  klass->is_password_node         = goa_oauth2_provider_is_password_node_default;
  klass->add_account_key_values   = goa_oauth2_provider_add_account_key_values_default;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in a thread dedicated to handling @invocation */
static gboolean
on_handle_get_access_token (GoaOAuth2Based        *interface,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  const gchar *id;
  const gchar *method_name;
  const gchar *provider_type;
  gchar *access_token = NULL;
  gint access_token_expires_in;

  /* TODO: maybe log what app is requesting access */

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);

  id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  g_debug ("Handling %s for account (%s, %s)", method_name, provider_type, id);

  provider = goa_provider_get_for_provider_type (provider_type);

  error = NULL;
  access_token = goa_oauth2_provider_get_access_token_sync (GOA_OAUTH2_PROVIDER (provider),
                                                            object,
                                                            FALSE, /* force_refresh */
                                                            &access_token_expires_in,
                                                            NULL, /* GCancellable* */
                                                            &error);
  if (access_token == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
    }
  else
    {
      goa_oauth2_based_complete_get_access_token (interface,
                                                  invocation,
                                                  access_token,
                                                  access_token_expires_in);
    }
  g_free (access_token);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
