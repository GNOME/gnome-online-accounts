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
 *         Cosimo Alfarano <cosimo.alfarano@collabora.co.uk>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include <stdlib.h>

#include <rest/oauth2-proxy.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "goalogging.h"
#include "goaprovider.h"
#include "goautils.h"
#include "goawebview.h"
#include "goaoauth2provider.h"

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

G_LOCK_DEFINE_STATIC (provider_lock);

G_DEFINE_ABSTRACT_TYPE (GoaOAuth2Provider, goa_oauth2_provider, GOA_TYPE_PROVIDER);

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
goa_oauth2_provider_get_use_external_browser_default (GoaOAuth2Provider  *provider)
{
  return FALSE;
}

/**
 * goa_oauth2_provider_get_use_external_browser:
 * @provider: A #GoaOAuth2Provider.
 *
 * Returns whether an external browser (the users default browser)
 * should be used for user interaction.
 *
 * If an external browser is used, then the dialogs presented in
 * goa_provider_add_account() and
 * goa_provider_refresh_account() will show a simple "Paste
 * authorization code here" instructions along with an entry and
 * button.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if the user interaction should happen in an external
 * browser, %FALSE to use an embedded browser widget.
 */
gboolean
goa_oauth2_provider_get_use_external_browser (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_use_external_browser (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_get_use_mobile_browser_default (GoaOAuth2Provider  *provider)
{
  return FALSE;
}

/**
 * goa_oauth_provider_get_use_mobile_browser:
 * @provider: A #GoaOAuth2Provider.
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
goa_oauth2_provider_get_use_mobile_browser (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_use_mobile_browser (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth2_provider_add_account_key_values_default (GoaOAuth2Provider *provider,
                                                    GVariantBuilder   *builder)
{
  /* do nothing */
}

/**
 * goa_oauth2_provider_add_account_key_values:
 * @provider: A #GoaProvider.
 * @builder: A #GVariantBuilder for a <literal>a{ss}</literal> variant.
 *
 * Hook for implementations to add key/value pairs to the key-file
 * when creating an account.
 *
 * This is a virtual method where the default implementation does nothing.
 */
void
goa_oauth2_provider_add_account_key_values (GoaOAuth2Provider  *provider,
                                            GVariantBuilder    *builder)
{
  g_return_if_fail (GOA_IS_OAUTH2_PROVIDER (provider));
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->add_account_key_values (provider, builder);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
goa_oauth2_provider_build_authorization_uri_default (GoaOAuth2Provider  *provider,
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
 * @provider: A #GoaOAuth2Provider.
 * @authorization_uri: An authorization URI.
 * @escaped_redirect_uri: An escaped redirect URI
 * @escaped_client_id: An escaped client id
 * @escaped_scope: The escaped scope.
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
goa_oauth2_provider_build_authorization_uri (GoaOAuth2Provider  *provider,
                                             const gchar        *authorization_uri,
                                             const gchar        *escaped_redirect_uri,
                                             const gchar        *escaped_client_id,
                                             const gchar        *escaped_scope)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_redirect_uri != NULL, NULL);
  g_return_val_if_fail (escaped_client_id != NULL, NULL);
  g_return_val_if_fail (escaped_scope != NULL, NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->build_authorization_uri (provider,
                                                                                    authorization_uri,
                                                                                    escaped_redirect_uri,
                                                                                    escaped_client_id,
                                                                                    escaped_scope);
}

/**
 * goa_oauth2_provider_get_authorization_uri:
 * @provider: A #GoaOAuth2Provider.
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
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_authorization_uri (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_authorization_uri (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
goa_oauth2_provider_get_token_uri_default (GoaOAuth2Provider  *provider)
{
  return NULL;
}

/**
 * goa_oauth2_provider_get_token_uri:
 * @provider: A #GoaOAuth2Provider.
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
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_token_uri (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_token_uri (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_oauth2_provider_get_redirect_uri:
 * @provider: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1.1">redirect_uri</ulink>
 * used when requesting authorization.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_redirect_uri (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_redirect_uri (provider);
}

/**
 * goa_oauth2_provider_get_scope:
 * @provider: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1.1">scope</ulink>
 * used when requesting authorization.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_scope (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_scope (provider);
}

/**
 * goa_oauth2_provider_get_client_id:
 * @provider: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-3">client_id</ulink>
 * identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_client_id (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_client_id (provider);
}

/**
 * goa_oauth2_provider_get_client_secret:
 * @provider: A #GoaOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-3">client_secret</ulink>
 * associated with the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_client_secret (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_client_secret (provider);
}

/**
 * goa_oauth2_provider_get_authentication_cookie:
 * @provider: A #GoaOAuth2Provider.
 *
 * Gets the name of a cookie whose presence indicates that the user has been able to
 * log in during the authorization step. This is used to modify the embedded web
 * browser UI that is presented to the user.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth2_provider_get_authentication_cookie (GoaOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_authentication_cookie (provider);
}

/**
 * goa_oauth2_provider_get_identity_sync:
 * @provider: A #GoaOAuth2Provider.
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
goa_oauth2_provider_get_identity_sync (GoaOAuth2Provider    *provider,
                                       const gchar          *access_token,
                                       gchar               **out_presentation_identity,
                                       GCancellable         *cancellable,
                                       GError              **error)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (access_token != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->get_identity_sync (provider, access_token, out_presentation_identity, cancellable, error);
}

/**
 * goa_oauth2_provider_is_deny_node:
 * @provider: A #GoaOAuth2Provider.
 * @node: A #WebKitDOMNode.
 *
 * Checks whether @node is the HTML UI element that the user can use
 * to deny permission to access his account. Usually they are either a
 * #WebKitDOMHTMLButtonElement or a #WebKitDOMHTMLInputElement.
 *
 * Please note that providers may have multiple such elements in their
 * UI and this method should catch all of them.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if the @node can be used to deny permission.
 */
gboolean
goa_oauth2_provider_is_deny_node (GoaOAuth2Provider *provider, WebKitDOMNode *node)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->is_deny_node (provider, node);
}

/**
 * goa_oauth2_provider_is_identity_node:
 * @provider: A #GoaOAuth2Provider.
 * @node: A #WebKitDOMHTMLInputElement.
 *
 * Checks whether @node is the HTML UI element that the user can use
 * to identify herself at the provider.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if the @node can be used to deny permission.
 */
gboolean
goa_oauth2_provider_is_identity_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (provider)->is_identity_node (provider, element);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_tokens_sync (GoaOAuth2Provider  *provider,
                 const gchar        *authorization_code,
                 const gchar        *refresh_token,
                 gchar             **out_refresh_token,
                 gint               *out_access_token_expires_in,
                 GCancellable       *cancellable,
                 GError            **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  gchar *ret;
  guint status_code;
  gchar *ret_access_token;
  gint ret_access_token_expires_in;
  gchar *ret_refresh_token;
  const gchar *payload;
  gsize payload_length;
  const gchar *client_secret;

  ret = NULL;
  ret_access_token = NULL;
  ret_access_token_expires_in = 0;
  ret_refresh_token = NULL;

  proxy = rest_proxy_new (goa_oauth2_provider_get_token_uri (provider), FALSE);
  call = rest_proxy_new_call (proxy);

  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "client_id", goa_oauth2_provider_get_client_id (provider));
  rest_proxy_call_add_param (call, "redirect_uri", goa_oauth2_provider_get_redirect_uri (provider));

  client_secret = goa_oauth2_provider_get_client_secret (provider);
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
      hash = soup_form_decode (payload);
      ret_access_token = g_strdup (g_hash_table_lookup (hash, "access_token"));
      if (ret_access_token == NULL)
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Didn't find access_token in non-JSON data"));
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
      if (!json_parser_load_from_data (parser, payload, payload_length, error))
        {
          g_prefix_error (error, _("Error parsing response as JSON: "));
          g_object_unref (parser);
          goto out;
        }
      object = json_node_get_object (json_parser_get_root (parser));
      ret_access_token = g_strdup (json_object_get_string_member (object, "access_token"));
      if (ret_access_token == NULL)
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Didn't find access_token in JSON data"));
          goto out;
        }
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
  g_free (ret_access_token);
  g_free (ret_refresh_token);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
typedef struct
{
  GoaOAuth2Provider *provider;
  GtkDialog *dialog;
  GError *error;
  GMainLoop *loop;

  gchar *authorization_code;
  gchar *access_token;
  gint access_token_expires_in;
  gchar *refresh_token;

  const gchar *existing_identity;

  gchar *identity;
  gchar *presentation_identity;
} IdentifyData;

static void
check_cookie (SoupCookie *cookie, gpointer user_data)
{
  IdentifyData *data = user_data;
  GList *children;
  GList *l;
  GoaOAuth2Provider *provider = data->provider;
  GtkDialog *dialog = data->dialog;
  GtkWidget *action_area;
  const gchar *auth_cookie;
  const gchar *name;

  auth_cookie = goa_oauth2_provider_get_authentication_cookie (provider);
  name = soup_cookie_get_name (cookie);
  if (g_strcmp0 (auth_cookie, name) != 0)
    return;

  action_area = gtk_dialog_get_action_area (dialog);
  children = gtk_container_get_children (GTK_CONTAINER (action_area));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *child = l->data;
      gtk_container_remove (GTK_CONTAINER (action_area), child);
    }
  g_list_free (children);
}

static void
on_dom_node_click (WebKitDOMNode *element, WebKitDOMEvent *event, gpointer user_data)
{
  IdentifyData *data = user_data;
  gtk_dialog_response (data->dialog, GTK_RESPONSE_CANCEL);
}

static void
on_web_view_document_load_finished (WebKitWebView *web_view, WebKitWebFrame *frame, gpointer user_data)
{
  IdentifyData *data = user_data;
  GSList *slist;
  GoaOAuth2Provider *provider = data->provider;
  SoupCookieJar *cookie_jar;
  SoupSession *session;
  WebKitDOMDocument *document;
  WebKitDOMNodeList *elements;
  gulong element_count;
  gulong i;

  session = webkit_get_default_session ();
  cookie_jar = SOUP_COOKIE_JAR (soup_session_get_feature (session, SOUP_TYPE_COOKIE_JAR));
  slist = soup_cookie_jar_all_cookies (cookie_jar);
  g_slist_foreach (slist, (GFunc) check_cookie, data);
  g_slist_free_full (slist, (GDestroyNotify) soup_cookie_free);

  document = webkit_web_view_get_dom_document (WEBKIT_WEB_VIEW (web_view));
  elements = webkit_dom_document_get_elements_by_tag_name (document, "*");
  element_count = webkit_dom_node_list_get_length (elements);

  for (i = 0; i < element_count; i++)
    {
      WebKitDOMNode *element = webkit_dom_node_list_item (elements, i);

      if (goa_oauth2_provider_is_deny_node (provider, element))
        {
          webkit_dom_event_target_add_event_listener (WEBKIT_DOM_EVENT_TARGET (element),
                                                      "click",
                                                      G_CALLBACK (on_dom_node_click),
                                                      FALSE,
                                                      data);
        }
      else if (data->existing_identity != NULL
               && WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (element)
               && goa_oauth2_provider_is_identity_node (provider, WEBKIT_DOM_HTML_INPUT_ELEMENT (element)))
        {
          webkit_dom_html_input_element_set_value (WEBKIT_DOM_HTML_INPUT_ELEMENT (element),
                                                   data->existing_identity);
          webkit_dom_html_input_element_set_read_only (WEBKIT_DOM_HTML_INPUT_ELEMENT (element), TRUE);
        }
    }
}

static gboolean
on_web_view_navigation_policy_decision_requested (WebKitWebView             *webView,
                                                  WebKitWebFrame            *frame,
                                                  WebKitNetworkRequest      *request,
                                                  WebKitWebNavigationAction *navigation_action,
                                                  WebKitWebPolicyDecision   *policy_decision,
                                                  gpointer                   user_data)
{
  IdentifyData *data = user_data;
  const gchar *redirect_uri;
  const gchar *requested_uri;

  /* TODO: use oauth2_proxy_extract_access_token() */

  requested_uri = webkit_network_request_get_uri (request);
  redirect_uri = goa_oauth2_provider_get_redirect_uri (data->provider);
  if (g_str_has_prefix (requested_uri, redirect_uri))
    {
      SoupMessage *message;
      SoupURI *uri;

      message = webkit_network_request_get_message (request);
      uri = soup_message_get_uri (message);

      /* Two cases:
       * the data we look for might be in the query part or in the fragment
       * part of the URI.
       *
       * Currently only facebook client-side flow uses the fragment to pass
       * the access_token and expiration, though, so we are actually looking
       * for the auth code in the query and the access_token in the fragment.
       * This might need changes (FIXME) to be more generalized */
      if (soup_uri_get_query (uri) != NULL)
        {
          /* case 1, the data we are expecting is in the query part of the URI */
          GHashTable *key_value_pairs;

          key_value_pairs = soup_form_decode (uri->query);

          data->authorization_code = g_strdup (g_hash_table_lookup (key_value_pairs, "code"));
          if (data->authorization_code != NULL)
            {
              gtk_dialog_response (data->dialog, GTK_RESPONSE_OK);
            }
          else
            {
              g_set_error (&data->error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_AUTHORIZED,
                           _("Authorization response was \"%s\""),
                           (const gchar *) g_hash_table_lookup (key_value_pairs, "error"));
              gtk_dialog_response (data->dialog, GTK_RESPONSE_CLOSE);
            }
          g_hash_table_unref (key_value_pairs);
          webkit_web_policy_decision_ignore (policy_decision);
        }
      else if (soup_uri_get_fragment (uri) != NULL)
        {
          /* case 2, the data we are expecting is in the fragment part of the
           * URI */
          GHashTable *key_value_pairs;

          /* fragment is encoded into a key/value pairs for the token and
           * expiration values, using the same syntax as a URL query */
          key_value_pairs = soup_form_decode (soup_uri_get_fragment (uri));

          /* we might use oauth2_proxy_extract_access_token() here but we need
           * also to extract the expire time */
          data->access_token = g_strdup (g_hash_table_lookup (key_value_pairs, "access_token"));
          if (data->access_token != NULL)
            {
              gchar *expires_in_str = NULL;

              expires_in_str = g_hash_table_lookup (key_value_pairs, "expires_in");
              /* sometimes "expires_in" appears as "expires" */
              if (expires_in_str == NULL)
                expires_in_str = g_hash_table_lookup (key_value_pairs, "expires");

              if (expires_in_str != NULL)
                 data->access_token_expires_in = atoi (expires_in_str);

              gtk_dialog_response (data->dialog, GTK_RESPONSE_OK);
            }
          else
            {
              g_set_error (&data->error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_AUTHORIZED,
                           _("Authorization response was \"%s\""),
                           (const gchar *) g_hash_table_lookup (key_value_pairs, "error"));
              gtk_dialog_response (data->dialog, GTK_RESPONSE_CLOSE);
            }
          g_hash_table_unref (key_value_pairs);
          webkit_web_policy_decision_ignore (policy_decision);
        }
      else
        {
          /* this actually means that something unexpected happened, either we
           * did something wrong or the provider's flow changed */
          goa_debug ("URI format not recognized, DEFAULT BEHAVIOUR");
          return FALSE;
        }
      return TRUE; /* ignore the request */
    }
  else
    {
      return FALSE; /* make default behavior apply */
    }
}

static void
on_entry_changed (GtkEditable *editable,
                  gpointer     user_data)
{
  IdentifyData *data = user_data;
  gboolean sensitive;

  g_free (data->authorization_code);
  data->authorization_code = g_strdup (gtk_entry_get_text (GTK_ENTRY (editable)));
  sensitive = data->authorization_code != NULL && (strlen (data->authorization_code) > 0);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, sensitive);
}

static gboolean
get_tokens_and_identity (GoaOAuth2Provider  *provider,
                         gboolean            add_account,
                         const gchar        *existing_identity,
                         GtkDialog          *dialog,
                         GtkBox             *vbox,
                         gchar             **out_authorization_code,
                         gchar             **out_access_token,
                         gint               *out_access_token_expires_in,
                         gchar             **out_refresh_token,
                         gchar             **out_identity,
                         gchar             **out_presentation_identity,
                         GError            **error)
{
  gboolean ret;
  gchar *url;
  GtkWidget *grid;
  IdentifyData data;
  gchar *escaped_redirect_uri;
  gchar *escaped_client_id;
  gchar *escaped_scope;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  g_return_val_if_fail ((!add_account && existing_identity != NULL && existing_identity[0] != '\0')
                        || (add_account && existing_identity == NULL), FALSE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), FALSE);
  g_return_val_if_fail (GTK_IS_BOX (vbox), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;
  escaped_redirect_uri = NULL;
  escaped_client_id = NULL;
  escaped_scope = NULL;

  /* TODO: check with NM whether we're online, if not - return error */

  memset (&data, '\0', sizeof (IdentifyData));
  data.provider = provider;
  data.dialog = dialog;
  data.loop = g_main_loop_new (NULL, FALSE);
  data.existing_identity = existing_identity;

  /* TODO: use oauth2_proxy_build_login_url_full() */
  escaped_redirect_uri = g_uri_escape_string (goa_oauth2_provider_get_redirect_uri (provider), NULL, TRUE);
  escaped_client_id = g_uri_escape_string (goa_oauth2_provider_get_client_id (provider), NULL, TRUE);
  escaped_scope = g_uri_escape_string (goa_oauth2_provider_get_scope (provider), NULL, TRUE);
  url = goa_oauth2_provider_build_authorization_uri (provider,
                                                     goa_oauth2_provider_get_authorization_uri (provider),
                                                     escaped_redirect_uri,
                                                     escaped_client_id,
                                                     escaped_scope);

  goa_utils_set_dialog_title (GOA_PROVIDER (provider), dialog, add_account);

  grid = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid), 5);
  gtk_widget_set_margin_bottom (grid, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid);

  if (goa_oauth2_provider_get_use_external_browser (provider))
    {
      GtkWidget *label;
      GtkWidget *entry;
      gchar *escaped_url;
      gchar *markup;

      escaped_url = g_markup_escape_text (url, -1);
      /* Translators: The verb "Paste" is used when asking the user to paste a string from a web browser window */
      markup = g_strdup_printf (_("Paste authorization code obtained from the <a href=\"%s\">authorization page</a>:"),
                                escaped_url);
      g_free (escaped_url);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label), markup);
      g_free (markup);
      gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, TRUE, 0);
      entry = gtk_entry_new ();
      gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
      gtk_box_pack_start (GTK_BOX (vbox), entry, FALSE, TRUE, 0);
      gtk_widget_grab_focus (entry);
      gtk_widget_show_all (GTK_WIDGET (vbox));

      gtk_dialog_add_button (dialog, GTK_STOCK_OK, GTK_RESPONSE_OK);
      gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);
      gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, FALSE);
      g_signal_connect (entry, "changed", G_CALLBACK (on_entry_changed), &data);

      if (!gtk_show_uri (NULL,
                         url,
                         GDK_CURRENT_TIME,
                         &data.error))
        {
          goto out;
        }
    }
  else
    {
      GtkWidget *web_view;
      GtkWidget *embed;

      web_view = goa_web_view_new ();
      gtk_widget_set_hexpand (web_view, TRUE);
      gtk_widget_set_vexpand (web_view, TRUE);
      embed = goa_web_view_get_view (GOA_WEB_VIEW (web_view));

      if (goa_oauth2_provider_get_use_mobile_browser (provider))
        goa_web_view_fake_mobile (GOA_WEB_VIEW (web_view));

      webkit_web_view_load_uri (WEBKIT_WEB_VIEW (embed), url);
      g_signal_connect (embed,
                        "document-load-finished",
                        G_CALLBACK (on_web_view_document_load_finished),
                        &data);
      g_signal_connect (embed,
                        "navigation-policy-decision-requested",
                        G_CALLBACK (on_web_view_navigation_policy_decision_requested),
                        &data);

      gtk_container_add (GTK_CONTAINER (grid), web_view);
    }

  gtk_widget_show_all (GTK_WIDGET (vbox));
  gtk_dialog_run (GTK_DIALOG (dialog));

  /* we can have either the auth code, with which we'll obtain the token, or
   * the token directly if we are using a client side flow, since we don't
   * need to pass the code to the remote application */
  if (data.authorization_code == NULL && data.access_token == NULL)
    {
      if (data.error == NULL)
        {
          g_set_error (&data.error,
                       GOA_ERROR,
                       GOA_ERROR_DIALOG_DISMISSED,
                       _("Dialog was dismissed"));
        }
      goto out;
    }
  g_assert (data.error == NULL);

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (data.authorization_code != NULL)
    {
      /* OK, we now have the authorization code... now we need to get the
       * email address (to e.g. check if the account already exists on
       * @client).. for that we need to get a (short-lived) access token
       * and a refresh_token
       */

      /* TODO: run in worker thread */
      data.access_token = get_tokens_sync (provider,
                                           data.authorization_code,
                                           NULL, /* refresh_token */
                                           &data.refresh_token,
                                           &data.access_token_expires_in,
                                           NULL, /* GCancellable */
                                           &data.error);
      if (data.access_token == NULL)
        {
          g_prefix_error (&data.error, _("Error getting an Access Token: "));
          goto out;
        }
    }

  g_assert (data.access_token != NULL);

  /* TODO: run in worker thread */
  data.identity = goa_oauth2_provider_get_identity_sync (provider,
                                                         data.access_token,
                                                         &data.presentation_identity,
                                                         NULL, /* TODO: GCancellable */
                                                         &data.error);
  if (data.identity == NULL)
    {
      g_prefix_error (&data.error, _("Error getting identity: "));
      goto out;
    }

  ret = TRUE;

 out:
  if (ret)
    {
      g_warn_if_fail (data.error == NULL);
      if (out_authorization_code != NULL)
        *out_authorization_code = g_strdup (data.authorization_code);
      if (out_access_token != NULL)
        *out_access_token = g_strdup (data.access_token);
      if (out_access_token_expires_in != NULL)
        *out_access_token_expires_in = data.access_token_expires_in;
      if (out_refresh_token != NULL)
        *out_refresh_token = g_strdup (data.refresh_token);
      if (out_identity != NULL)
        *out_identity = g_strdup (data.identity);
      if (out_presentation_identity != NULL)
        *out_presentation_identity = g_strdup (data.presentation_identity);
    }
  else
    {
      g_warn_if_fail (data.error != NULL);
      g_propagate_error (error, data.error);
    }

  g_free (data.identity);
  g_free (data.presentation_identity);
  g_free (url);

  g_free (data.authorization_code);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  g_free (data.access_token);
  g_free (data.refresh_token);
  g_free (escaped_redirect_uri);
  g_free (escaped_client_id);
  g_free (escaped_scope);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GError *error;
  GMainLoop *loop;
  gchar *account_object_path;
} AddData;

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *res,
                gpointer      user_data)
{
  AddData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
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

static GoaObject *
goa_oauth2_provider_add_account (GoaProvider *_provider,
                                         GoaClient          *client,
                                         GtkDialog          *dialog,
                                         GtkBox             *vbox,
                                         GError            **error)
{
  GoaOAuth2Provider *provider = GOA_OAUTH2_PROVIDER (_provider);
  GoaObject *ret;
  gchar *authorization_code;
  gchar *access_token;
  gint access_token_expires_in;
  gchar *refresh_token;
  gchar *identity;
  gchar *presentation_identity;
  AddData data;
  GVariantBuilder credentials;
  GVariantBuilder details;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  authorization_code = NULL;
  access_token = NULL;
  refresh_token = NULL;
  identity = NULL;
  presentation_identity = NULL;

  memset (&data, '\0', sizeof (AddData));
  data.loop = g_main_loop_new (NULL, FALSE);

  if (!get_tokens_and_identity (provider,
                                TRUE,
                                NULL,
                                dialog,
                                vbox,
                                &authorization_code,
                                &access_token,
                                &access_token_expires_in,
                                &refresh_token,
                                &identity,
                                &presentation_identity,
                                &data.error))
    goto out;

  /* OK, got the identity... see if there's already an account
   * of this type with the given identity
   */
  if (!goa_utils_check_duplicate (client,
                                  identity,
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  (GoaPeekInterfaceFunc) goa_object_peek_oauth2_based,
                                  &data.error))
    goto out;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  if (authorization_code != NULL)
    g_variant_builder_add (&credentials, "{sv}", "authorization_code", g_variant_new_string (authorization_code));
  g_variant_builder_add (&credentials, "{sv}", "access_token", g_variant_new_string (access_token));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&credentials, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (access_token_expires_in)));
  if (refresh_token != NULL)
    g_variant_builder_add (&credentials, "{sv}", "refresh_token", g_variant_new_string (refresh_token));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  goa_oauth2_provider_add_account_key_values (provider, &details);

  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                identity,
                                presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  /* We might have an object even when data.error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (ret != NULL);

  g_free (identity);
  g_free (presentation_identity);
  g_free (refresh_token);
  g_free (access_token);
  g_free (authorization_code);
  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_refresh_account (GoaProvider  *_provider,
                                     GoaClient    *client,
                                     GoaObject    *object,
                                     GtkWindow    *parent,
                                     GError      **error)
{
  GoaOAuth2Provider *provider = GOA_OAUTH2_PROVIDER (_provider);
  GoaAccount *account;
  GtkWidget *dialog;
  gchar *authorization_code;
  gchar *access_token;
  gint access_token_expires_in;
  gchar *refresh_token;
  gchar *identity;
  const gchar *existing_identity;
  const gchar *existing_presentation_identity;
  GVariantBuilder builder;
  gboolean ret;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  authorization_code = NULL;
  access_token = NULL;
  refresh_token = NULL;
  identity = NULL;

  ret = FALSE;

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
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
  if (!get_tokens_and_identity (provider,
                                FALSE,
                                existing_presentation_identity,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                &authorization_code,
                                &access_token,
                                &access_token_expires_in,
                                &refresh_token,
                                &identity,
                                NULL, /* out_presentation_identity */
                                error))
    goto out;

  /* Changes made to the web interface by the providers can break our
   * DOM parsing. So we should still query and check the identity
   * afterwards.
   */
  existing_identity = goa_account_get_identity (account);
  if (g_strcmp0 (identity, existing_identity) != 0)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Was asked to login as %s, but logged in as %s"),
                   existing_identity,
                   identity);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  if (authorization_code != NULL)
    g_variant_builder_add (&builder, "{sv}", "authorization_code", g_variant_new_string (authorization_code));
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (access_token_expires_in)));
  if (refresh_token != NULL)
    g_variant_builder_add (&builder, "{sv}", "refresh_token", g_variant_new_string (refresh_token));
  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (provider),
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
  gtk_widget_destroy (dialog);

  g_free (identity);
  g_free (access_token);
  g_free (authorization_code);
  g_free (refresh_token);
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
 * @provider: A #GoaOAuth2Provider.
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
goa_oauth2_provider_get_access_token_sync (GoaOAuth2Provider  *provider,
                                           GoaObject          *object,
                                           gboolean            force_refresh,
                                           gint               *out_access_token_expires_in,
                                           GCancellable       *cancellable,
                                           GError            **error)
{
  GVariant *credentials;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;
  gchar *authorization_code;
  gchar *access_token;
  gint access_token_expires_in;
  gchar *refresh_token;
  gchar *old_refresh_token;
  gboolean success;
  GVariantBuilder builder;
  gchar *ret;
  GMutex *lock;

  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  credentials = NULL;
  authorization_code = NULL;
  access_token = NULL;
  refresh_token = NULL;
  old_refresh_token = NULL;
  access_token_expires_in = 0;
  success = FALSE;

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
  credentials = goa_utils_lookup_credentials_sync (GOA_PROVIDER (provider),
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    {
      if (error != NULL)
        {
          g_prefix_error (error, _("Credentials not found in keyring (%s, %d): "),
                          g_quark_to_string ((*error)->domain), (*error)->code);
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
      goa_debug ("Returning locally cached credentials that cannot be refreshed");
      success = TRUE;
      goto out;
    }

  /* If access_token is still "fresh enough" (e.g. more than ten
   * minutes of life left in it), just return it unless we've been
   * asked to forcibly refresh it
   */
  if (!force_refresh && access_token_expires_in > 10*60)
    {
      goa_debug ("Returning locally cached credentials (expires in %d seconds)", access_token_expires_in);
      success = TRUE;
      goto out;
    }

  goa_debug ("Refreshing locally cached credentials (expires in %d seconds, force_refresh=%d)", access_token_expires_in, force_refresh);

  /* Otherwise, refresh it */
  old_refresh_token = refresh_token; refresh_token = NULL;
  g_free (access_token); access_token = NULL;
  access_token = get_tokens_sync (provider,
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

  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (provider),
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    cancellable,
                                                    error))
    {
      if (error != NULL)
        {
          g_prefix_error (error, _("Error storing credentials in keyring (%s, %d): "),
                          g_quark_to_string ((*error)->domain), (*error)->code);
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
  if (credentials != NULL)
    g_variant_unref (credentials);

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
goa_oauth2_provider_ensure_credentials_sync (GoaProvider   *_provider,
                                             GoaObject     *object,
                                             gint          *out_expires_in,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
  GoaOAuth2Provider *provider = GOA_OAUTH2_PROVIDER (_provider);
  gboolean ret;
  gchar *access_token;
  gint access_token_expires_in;
  gchar *identity;
  gboolean force_refresh;

  ret = FALSE;
  access_token = NULL;
  identity = NULL;
  force_refresh = FALSE;

 again:
  access_token = goa_oauth2_provider_get_access_token_sync (provider,
                                                            object,
                                                            force_refresh,
                                                            &access_token_expires_in,
                                                            cancellable,
                                                            error);
  if (access_token == NULL)
    goto out;

  identity = goa_oauth2_provider_get_identity_sync (provider,
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
goa_oauth2_provider_init (GoaOAuth2Provider *client)
{
}

static void
goa_oauth2_provider_class_init (GoaOAuth2ProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->add_account                = goa_oauth2_provider_add_account;
  provider_class->refresh_account            = goa_oauth2_provider_refresh_account;
  provider_class->build_object               = goa_oauth2_provider_build_object;
  provider_class->ensure_credentials_sync    = goa_oauth2_provider_ensure_credentials_sync;

  klass->build_authorization_uri  = goa_oauth2_provider_build_authorization_uri_default;
  klass->get_token_uri            = goa_oauth2_provider_get_token_uri_default;
  klass->get_use_external_browser = goa_oauth2_provider_get_use_external_browser_default;
  klass->get_use_mobile_browser   = goa_oauth2_provider_get_use_mobile_browser_default;
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
  gchar *access_token;
  gint access_token_expires_in;

  /* TODO: maybe log what app is requesting access */

  access_token = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));

  error = NULL;
  access_token = goa_oauth2_provider_get_access_token_sync (GOA_OAUTH2_PROVIDER (provider),
                                                            object,
                                                            FALSE, /* force_refresh */
                                                            &access_token_expires_in,
                                                            NULL, /* GCancellable* */
                                                            &error);
  if (access_token == NULL)
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
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
