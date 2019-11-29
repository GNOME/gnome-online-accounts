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

#include <rest/oauth-proxy.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <webkit2/webkit2.h>

#include "goaprovider.h"
#include "goautils.h"
#include "goawebview.h"
#include "goaoauthprovider.h"
#include "goasouplogger.h"

/**
 * SECTION:goaoauthprovider
 * @title: GoaOAuthProvider
 * @short_description: Abstract base class for OAuth 1.0a providers
 *
 * #GoaOAuthProvider is an abstract base class for OAuth 1.0a
 * compliant implementations as defined by <ulink
 * url="http://tools.ietf.org/html/rfc5849">RFC
 * 5849</ulink>. Additionally, the code works with providers
 * implementing <ulink
 * url="http://oauth.googlecode.com/svn/spec/ext/session/1.0/drafts/1/spec.html">OAuth
 * Session 1.0 Draft 1</ulink> for refreshing access tokens.
 *
 * Subclasses must implement
 * #GoaOAuthProviderClass.get_consumer_key,
 * #GoaOAuthProviderClass.get_consumer_secret,
 * #GoaOAuthProviderClass.get_request_uri,
 * #GoaOAuthProviderClass.get_authorization_uri,
 * #GoaOAuthProviderClass.get_token_uri,
 * #GoaOAuthProviderClass.get_callback_uri and
 * #GoaOAuthProviderClass.get_identity_sync methods.
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
 * need to be implemented - this type implements these methods.
 */

G_LOCK_DEFINE_STATIC (provider_lock);

G_DEFINE_ABSTRACT_TYPE (GoaOAuthProvider, goa_oauth_provider, GOA_TYPE_PROVIDER);

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
goa_oauth_provider_get_use_mobile_browser_default (GoaOAuthProvider  *provider)
{
  return FALSE;
}

/**
 * goa_oauth_provider_get_use_mobile_browser:
 * @provider: A #GoaOAuthProvider.
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
goa_oauth_provider_get_use_mobile_browser (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_use_mobile_browser (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth_provider_is_deny_node_default (GoaOAuthProvider *provider, WebKitDOMNode *node)
{
  return FALSE;
}

/**
 * goa_oauth_provider_is_deny_node:
 * @provider: A #GoaOAuthProvider.
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
goa_oauth_provider_is_deny_node (GoaOAuthProvider *provider, WebKitDOMNode *node)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->is_deny_node (provider, node);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth_provider_is_password_node_default (GoaOAuthProvider *provider, WebKitDOMHTMLInputElement *element)
{
  return FALSE;
}

/**
 * goa_oauth_provider_is_password_node:
 * @provider: A #GoaOAuthProvider.
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
goa_oauth_provider_is_password_node (GoaOAuthProvider *provider, WebKitDOMHTMLInputElement *element)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  g_return_val_if_fail (WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (element), FALSE);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->is_password_node (provider, element);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth_provider_add_account_key_values_default (GoaOAuthProvider  *provider,
                                                   GVariantBuilder   *builder)
{
  /* do nothing */
}

/**
 * goa_oauth_provider_add_account_key_values:
 * @provider: A #GoaProvider.
 * @builder: A #GVariantBuilder for a <literal>a{ss}</literal> variant.
 *
 * Hook for implementations to add key/value pairs to the key-file
 * when creating an account.
 *
 * This is a virtual method where the default implementation does nothing.
 */
void
goa_oauth_provider_add_account_key_values (GoaOAuthProvider  *provider,
                                           GVariantBuilder   *builder)
{
  g_return_if_fail (GOA_IS_OAUTH_PROVIDER (provider));
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->add_account_key_values (provider, builder);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
goa_oauth_provider_build_authorization_uri_default (GoaOAuthProvider  *provider,
                                                    const gchar       *authorization_uri,
                                                    const gchar       *escaped_oauth_token)
{
  return g_strdup_printf ("%s"
                          "?oauth_token=%s",
                          authorization_uri,
                          escaped_oauth_token);
}

/**
 * goa_oauth_provider_build_authorization_uri:
 * @provider: A #GoaOAuthProvider.
 * @authorization_uri: An authorization URI.
 * @escaped_oauth_token: An escaped oauth token.
 *
 * Builds the URI that can be opened in a web browser (or embedded web
 * browser widget) to start authenticating an user.
 *
 * The default implementation just returns the expected URI
 * (e.g. <literal>http://example.com/dialog/oauth?auth_token=1234567890</literal>)
 * - override (and chain up) if you e.g. need to to pass additional
 * parameters.
 *
 * The @authorization_uri parameter originate from the result of the
 * the goa_oauth_provider_get_authorization_uri() method. The
 * @escaped_oauth_token parameter is the temporary credentials identifier
 * escaped using g_uri_escape_string().
 *
 * Returns: (transfer full): An authorization URI that must be freed with g_free().
 */
gchar *
goa_oauth_provider_build_authorization_uri (GoaOAuthProvider  *provider,
                                            const gchar       *authorization_uri,
                                            const gchar       *escaped_oauth_token)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_oauth_token != NULL, NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->build_authorization_uri (provider,
                                                                                   authorization_uri,
                                                                                   escaped_oauth_token);
}

/**
 * goa_oauth_provider_get_consumer_key:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the consumer key identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_consumer_key (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_consumer_key (provider);
}

/**
 * goa_oauth_provider_get_consumer_secret:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the consumer secret identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_consumer_secret (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_consumer_secret (provider);
}

/**
 * goa_oauth_provider_get_request_uri:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the request uri.
 *
 * http://tools.ietf.org/html/rfc5849#section-2.1
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_request_uri (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_request_uri (provider);
}

/**
 * goa_oauth_provider_get_request_uri_params:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets additional parameters for the request URI.
 *
 * http://tools.ietf.org/html/rfc5849#section-2.1
 *
 * This is a virtual method where the default implementation returns
 * %NULL.
 *
 * Returns: (transfer full): %NULL (for no parameters) or a
 * %NULL-terminated array of (key, value) pairs that will be added to
 * the URI. The caller will free the returned value with g_strfreev().
 */
gchar **
goa_oauth_provider_get_request_uri_params (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_request_uri_params (provider);
}

static gchar **
goa_oauth_provider_get_request_uri_params_default (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return NULL;
}

/**
 * goa_oauth_provider_get_authorization_uri:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the authorization uri.
 *
 * http://tools.ietf.org/html/rfc5849#section-2.2
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_authorization_uri (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_authorization_uri (provider);
}

/**
 * goa_oauth_provider_get_token_uri:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the token uri.
 *
 * http://tools.ietf.org/html/rfc5849#section-2.3
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_token_uri (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_token_uri (provider);
}

/**
 * goa_oauth_provider_get_callback_uri:
 * @provider: A #GoaOAuthProvider.
 *
 * Gets the callback uri.
 *
 * http://tools.ietf.org/html/rfc5849#section-2.1
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_oauth_provider_get_callback_uri (GoaOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_callback_uri (provider);
}

/**
 * goa_oauth_provider_get_identity_sync:
 * @provider: A #GoaOAuthProvider.
 * @access_token: A valid OAuth 1.0 access token.
 * @access_token_secret: The valid secret for @access_token.
 * @out_presentation_identity: (out): Return location for presentation identity or %NULL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Method that returns the identity corresponding to @access_token and
 * @access_token_secret.
 *
 * The identity is needed because all authentication happens out of
 * band. In addition to the identity, an implementation also returns a
 * <emphasis>presentation identity</emphasis> that is more suitable
 * for presentation (the identity could be a GUID for example).
 *
 * The calling thread is blocked while the identity is obtained.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: The identity or %NULL if error is set. The returned string
 * must be freed with g_free().
 */
gchar *
goa_oauth_provider_get_identity_sync (GoaOAuthProvider *provider,
                                      const gchar      *access_token,
                                      const gchar      *access_token_secret,
                                      gchar           **out_presentation_identity,
                                      GCancellable     *cancellable,
                                      GError          **error)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (access_token != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->get_identity_sync (provider,
                                                                     access_token,
                                                                     access_token_secret,
                                                                     out_presentation_identity,
                                                                     cancellable,
                                                                     error);
}

/**
 * goa_oauth_provider_is_identity_node:
 * @provider: A #GoaOAuthProvider.
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
goa_oauth_provider_is_identity_node (GoaOAuthProvider *provider, WebKitDOMHTMLInputElement *element)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->is_identity_node (provider, element);
}

/**
 * goa_oauth_provider_parse_request_token_error:
 * @provider: A #GoaOAuthProvider.
 * @call: The #RestProxyCall that was used to fetch the request token.
 *
 * Tries to parse the headers and payload within @call to provide a
 * human readable error message in case the request token could not
 * be fetched.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: A human readable error message or %NULL if the cause of the
 * error could not be determined. The returned string must be freed with
 * g_free().
 */
gchar *
goa_oauth_provider_parse_request_token_error (GoaOAuthProvider *provider, RestProxyCall *call)
{
  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  return GOA_OAUTH_PROVIDER_GET_CLASS (provider)->parse_request_token_error (provider, call);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_tokens_sync (GoaOAuthProvider  *provider,
                 const gchar       *token,
                 const gchar       *token_secret,
                 const gchar       *session_handle, /* may be NULL */
                 const gchar       *verifier,       /* may be NULL */
                 gchar            **out_access_token_secret,
                 gint              *out_access_token_expires_in,
                 gchar            **out_session_handle,
                 gint              *out_session_handle_expires_in,
                 GCancellable      *cancellable,
                 GError           **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  SoupLogger *logger = NULL;
  gchar *ret = NULL;
  guint status_code;
  GHashTable *f;
  const gchar *expires_in_str;
  gchar *ret_access_token = NULL;
  gchar *ret_access_token_secret = NULL;
  gint ret_access_token_expires_in = 0;
  gchar *ret_session_handle = NULL;
  gint ret_session_handle_expires_in = 0;

  proxy = oauth_proxy_new (goa_oauth_provider_get_consumer_key (provider),
                           goa_oauth_provider_get_consumer_secret (provider),
                           goa_oauth_provider_get_token_uri (provider),
                           FALSE);
  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  rest_proxy_add_soup_feature (proxy, SOUP_SESSION_FEATURE (logger));
  oauth_proxy_set_token (OAUTH_PROXY (proxy), token);
  oauth_proxy_set_token_secret (OAUTH_PROXY (proxy), token_secret);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  if (verifier != NULL)
    rest_proxy_call_add_param (call, "oauth_verifier", verifier);
  if (session_handle != NULL)
    rest_proxy_call_add_param (call, "oauth_session_handle", session_handle);
  /* TODO: cancellable support? */
  if (!rest_proxy_call_sync (call, error))
    goto out;

  status_code = rest_proxy_call_get_status_code (call);
  if (status_code != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   /* Translators: the %d is a HTTP status code and the %s is a textual description of it */
                   _("Expected status 200 when requesting access token, instead got status %d (%s)"),
                   status_code,
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  f = soup_form_decode (rest_proxy_call_get_payload (call));
  ret_access_token = g_strdup (g_hash_table_lookup (f, "oauth_token"));
  ret_access_token_secret = g_strdup (g_hash_table_lookup (f, "oauth_token_secret"));
  ret_session_handle = g_strdup (g_hash_table_lookup (f, "oauth_session_handle"));
  expires_in_str = g_hash_table_lookup (f, "oauth_expires_in");
  if (expires_in_str != NULL)
    ret_access_token_expires_in = atoi (expires_in_str);
  expires_in_str = g_hash_table_lookup (f, "oauth_authorization_expires_in");
  if (expires_in_str != NULL)
    ret_session_handle_expires_in = atoi (expires_in_str);
  g_hash_table_unref (f);

  if (ret_access_token == NULL || ret_access_token_secret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Missing access_token or access_token_secret headers in response"));
      goto out;
    }

  ret = ret_access_token; ret_access_token = NULL;
  if (out_access_token_secret != NULL)
    {
      *out_access_token_secret = ret_access_token_secret;
      ret_access_token_secret = NULL;
    }
  if (out_access_token_expires_in != NULL)
    *out_access_token_expires_in = ret_access_token_expires_in;
  if (out_session_handle != NULL)
    {
      *out_session_handle = ret_session_handle;
      ret_session_handle = NULL;
    }
  if (out_session_handle_expires_in != NULL)
    *out_session_handle_expires_in = ret_session_handle_expires_in;

 out:
  g_free (ret_access_token);
  g_free (ret_access_token_secret);
  g_free (ret_session_handle);
  g_clear_object (&call);
  g_clear_object (&proxy);
  g_clear_object (&logger);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaOAuthProvider *provider;
  GtkDialog *dialog;
  GError *error;
  GMainLoop *loop;

  gchar *password;

  gchar *oauth_verifier;

  const gchar *existing_identity;

  gchar *identity;
  gchar *presentation_identity;

  gchar *request_token;
  gchar *request_token_secret;
  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;
  gchar *session_handle;
  gint session_handle_expires_in;
} IdentifyData;

static void
on_web_view_deny_click (GoaWebView *web_view, gpointer user_data)
{
  IdentifyData *data = user_data;
  gtk_dialog_response (data->dialog, GTK_RESPONSE_CANCEL);
}

static void
on_web_view_password_submit (GoaWebView *web_view, const gchar *password, gpointer user_data)
{
  IdentifyData *data = user_data;

  g_free (data->password);
  data->password = g_strdup (password);
}

static gboolean
on_web_view_decide_policy (WebKitWebView            *web_view,
                           WebKitPolicyDecision     *decision,
                           WebKitPolicyDecisionType  decision_type,
                           gpointer                  user_data)
{
  GHashTable *key_value_pairs;
  IdentifyData *data = user_data;
  SoupURI *uri;
  WebKitNavigationAction *action;
  WebKitURIRequest *request;
  const gchar *query;
  const gchar *redirect_uri;
  const gchar *requested_uri;
  gint response_id = GTK_RESPONSE_NONE;

  if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
    return FALSE;

  /* TODO: use oauth_proxy_extract_access_token() */

  action = webkit_navigation_policy_decision_get_navigation_action (WEBKIT_NAVIGATION_POLICY_DECISION (decision));
  request = webkit_navigation_action_get_request (action);
  requested_uri = webkit_uri_request_get_uri (request);
  redirect_uri = goa_oauth_provider_get_callback_uri (data->provider);

  if (!g_str_has_prefix (requested_uri, redirect_uri))
    goto default_behaviour;

  uri = soup_uri_new (requested_uri);
  query = soup_uri_get_query (uri);

  if (query != NULL)
    {
      key_value_pairs = soup_form_decode (query);

      data->oauth_verifier = g_strdup (g_hash_table_lookup (key_value_pairs, "oauth_verifier"));
      if (data->oauth_verifier != NULL)
        response_id = GTK_RESPONSE_OK;

      g_hash_table_unref (key_value_pairs);
    }

  if (data->oauth_verifier != NULL)
    goto ignore_request;

  /* TODO: The only OAuth1 provider is Flickr. It doesn't send any
   * error code and only redirects to the URI specified in the Flickr
   * App Garden. Re-evaluate when the situation changes.
   */
  response_id = GTK_RESPONSE_CANCEL;
  goto ignore_request;

 ignore_request:
  g_assert (response_id != GTK_RESPONSE_NONE);
  gtk_dialog_response (data->dialog, response_id);
  webkit_policy_decision_ignore (decision);
  return TRUE;

 default_behaviour:
  return FALSE;
}

static void
rest_proxy_call_cb (RestProxyCall *call, const GError *error, GObject *weak_object, gpointer user_data)
{
  IdentifyData *data = user_data;
  g_main_loop_quit (data->loop);
}

static gboolean
get_tokens_and_identity (GoaOAuthProvider *provider,
                         gboolean          add_account,
                         const gchar      *existing_identity,
                         GtkDialog        *dialog,
                         GtkBox           *vbox,
                         gchar           **out_access_token,
                         gchar           **out_access_token_secret,
                         gint             *out_access_token_expires_in,
                         gchar           **out_session_handle,
                         gint             *out_session_handle_expires_in,
                         gchar           **out_identity,
                         gchar           **out_presentation_identity,
                         gchar           **out_password,
                         GError          **error)
{
  gboolean ret = FALSE;
  gchar *url = NULL;
  IdentifyData data;
  gchar *escaped_request_token = NULL;
  RestProxy *proxy = NULL;
  RestProxyCall *call = NULL;
  SoupLogger *logger = NULL;
  GHashTable *f;
  GtkWidget *embed;
  GtkWidget *grid;
  GtkWidget *spinner;
  GtkWidget *web_view;
  gchar **request_params = NULL;
  guint n;

  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  g_return_val_if_fail ((!add_account && existing_identity != NULL && existing_identity[0] != '\0')
                        || (add_account && existing_identity == NULL), FALSE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), FALSE);
  g_return_val_if_fail (GTK_IS_BOX (vbox), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* TODO: check with NM whether we're online, if not - return error */

  memset (&data, '\0', sizeof (IdentifyData));
  data.provider = provider;
  data.dialog = dialog;
  data.loop = g_main_loop_new (NULL, FALSE);
  data.existing_identity = existing_identity;

  proxy = oauth_proxy_new (goa_oauth_provider_get_consumer_key (provider),
                           goa_oauth_provider_get_consumer_secret (provider),
                           goa_oauth_provider_get_request_uri (provider), FALSE);
  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  rest_proxy_add_soup_feature (proxy, SOUP_SESSION_FEATURE (logger));

  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_param (call, "oauth_callback", goa_oauth_provider_get_callback_uri (provider));

  request_params = goa_oauth_provider_get_request_uri_params (provider);
  if (request_params != NULL)
    {
      g_assert (g_strv_length (request_params) % 2 == 0);
      for (n = 0; request_params[n] != NULL; n += 2)
        rest_proxy_call_add_param (call, request_params[n], request_params[n+1]);
    }
  if (!rest_proxy_call_async (call, rest_proxy_call_cb, NULL, &data, &data.error))
    {
      g_prefix_error (&data.error, _("Error getting a Request Token: "));
      goto out;
    }

  goa_utils_set_dialog_title (GOA_PROVIDER (provider), dialog, add_account);

  grid = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_hexpand (spinner, TRUE);
  gtk_widget_set_halign (spinner, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand (spinner, TRUE);
  gtk_widget_set_valign (spinner, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request (GTK_WIDGET (spinner), 24, 24);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (grid), spinner);
  gtk_widget_show_all (GTK_WIDGET (vbox));

  g_main_loop_run (data.loop);
  gtk_container_remove (GTK_CONTAINER (grid), spinner);

  if (rest_proxy_call_get_status_code (call) != 200)
    {
      gchar *msg;

      msg = goa_oauth_provider_parse_request_token_error (provider, call);
      if (msg == NULL)
        /* Translators: the %d is a HTTP status code and the %s is a textual description of it */
        msg = g_strdup_printf (_("Expected status 200 for getting a Request Token, instead got status %d (%s)"),
                               rest_proxy_call_get_status_code (call),
                               rest_proxy_call_get_status_message (call));

      g_set_error_literal (&data.error, GOA_ERROR, GOA_ERROR_FAILED, msg);
      g_free (msg);
      goto out;
    }
  f = soup_form_decode (rest_proxy_call_get_payload (call));
  data.request_token = g_strdup (g_hash_table_lookup (f, "oauth_token"));
  data.request_token_secret = g_strdup (g_hash_table_lookup (f, "oauth_token_secret"));
  g_hash_table_unref (f);
  if (data.request_token == NULL || data.request_token_secret == NULL)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Missing request_token or request_token_secret headers in response"));
      goto out;
    }

  escaped_request_token = g_uri_escape_string (data.request_token, NULL, TRUE);
  url = goa_oauth_provider_build_authorization_uri (provider,
                                                            goa_oauth_provider_get_authorization_uri (provider),
                                                            escaped_request_token);

  web_view = goa_web_view_new (GOA_PROVIDER (provider), existing_identity);
  gtk_widget_set_hexpand (web_view, TRUE);
  gtk_widget_set_vexpand (web_view, TRUE);
  embed = goa_web_view_get_view (GOA_WEB_VIEW (web_view));

  if (goa_oauth_provider_get_use_mobile_browser (provider))
    goa_web_view_fake_mobile (GOA_WEB_VIEW (web_view));

  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (embed), url);
  g_signal_connect (embed,
                    "decide-policy",
                    G_CALLBACK (on_web_view_decide_policy),
                    &data);
  g_signal_connect (web_view, "deny-click", G_CALLBACK (on_web_view_deny_click), &data);
  g_signal_connect (web_view, "password-submit", G_CALLBACK (on_web_view_password_submit), &data);

  gtk_container_add (GTK_CONTAINER (grid), web_view);
  gtk_window_set_default_size (GTK_WINDOW (dialog), -1, -1);

  gtk_widget_show_all (GTK_WIDGET (vbox));
  gtk_dialog_run (GTK_DIALOG (dialog));

  if (data.oauth_verifier == NULL)
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

  /* OK, we are done interacting with the user ... but before we can
   * make up our mind, there are two more RPC calls to make and these
   * call may take some time. So hide the dialog immediately.
   */
  gtk_widget_hide (GTK_WIDGET (dialog));

  /* OK, we now have the request token... we can exchange that for a
   * (short-lived) access token and session_handle (used to refresh the
   * access token)..
   */

  /* TODO: run in worker thread */
  data.access_token = get_tokens_sync (provider,
                                       data.request_token,
                                       data.request_token_secret,
                                       NULL, /* session_handle */
                                       data.oauth_verifier,
                                       &data.access_token_secret,
                                       &data.access_token_expires_in,
                                       &data.session_handle,
                                       &data.session_handle_expires_in,
                                       NULL, /* GCancellable */
                                       &data.error);
  if (data.access_token == NULL)
    {
      g_prefix_error (&data.error, _("Error getting an Access Token: "));
      goto out;
    }

  /* TODO: run in worker thread */
  data.identity = goa_oauth_provider_get_identity_sync (provider,
                                                                data.access_token,
                                                                data.access_token_secret,
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
  g_clear_object (&call);

  if (ret)
    {
      g_warn_if_fail (data.error == NULL);
      if (out_access_token != NULL)
        *out_access_token = g_strdup (data.access_token);
      if (out_access_token_secret != NULL)
        *out_access_token_secret = g_strdup (data.access_token_secret);
      if (out_access_token_expires_in != NULL)
        *out_access_token_expires_in = data.access_token_expires_in;
      if (out_session_handle != NULL)
        *out_session_handle = g_strdup (data.session_handle);
      if (out_session_handle_expires_in != NULL)
        *out_session_handle_expires_in = data.session_handle_expires_in;
      if (out_identity != NULL)
        *out_identity = g_strdup (data.identity);
      if (out_presentation_identity != NULL)
        *out_presentation_identity = g_strdup (data.presentation_identity);
      if (out_password != NULL)
        *out_password = g_strdup (data.password);
    }
  else
    {
      g_warn_if_fail (data.error != NULL);
      g_propagate_error (error, data.error);
    }

  g_free (data.password);
  g_free (data.presentation_identity);
  g_free (data.identity);
  g_free (url);

  g_free (data.oauth_verifier);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_free (data.access_token);
  g_free (data.access_token_secret);
  g_free (escaped_request_token);

  g_free (data.request_token);
  g_free (data.request_token_secret);

  g_strfreev (request_params);
  g_clear_object (&proxy);
  g_clear_object (&logger);
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

static GoaObject *
goa_oauth_provider_add_account (GoaProvider *_provider,
                                GoaClient   *client,
                                GtkDialog   *dialog,
                                GtkBox      *vbox,
                                GError     **error)
{
  GoaOAuthProvider *provider = GOA_OAUTH_PROVIDER (_provider);
  GoaObject *ret = NULL;
  gchar *access_token = NULL;
  gchar *access_token_secret = NULL;
  gint access_token_expires_in;
  gchar *session_handle = NULL;
  gint session_handle_expires_in;
  gchar *identity = NULL;
  gchar *presentation_identity = NULL;
  gchar *password = NULL;
  AddData data;
  GVariantBuilder credentials;
  GVariantBuilder details;

  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  memset (&data, '\0', sizeof (AddData));
  data.loop = g_main_loop_new (NULL, FALSE);

  if (!get_tokens_and_identity (provider,
                                TRUE,
                                NULL,
                                dialog,
                                vbox,
                                &access_token,
                                &access_token_secret,
                                &access_token_expires_in,
                                &session_handle,
                                &session_handle_expires_in,
                                &identity,
                                &presentation_identity,
                                &password,
                                &data.error))
    goto out;

  /* OK, got the identity... see if there's already an account
   * of this type with the given identity
   */
  if (!goa_utils_check_duplicate (client,
                                  identity,
                                  presentation_identity,
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  (GoaPeekInterfaceFunc) goa_object_peek_oauth_based,
                                  &data.error))
    goto out;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "access_token", g_variant_new_string (access_token));
  g_variant_builder_add (&credentials, "{sv}", "access_token_secret", g_variant_new_string (access_token_secret));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&credentials, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (access_token_expires_in)));
  if (session_handle != NULL)
    g_variant_builder_add (&credentials, "{sv}", "session_handle", g_variant_new_string (session_handle));
  if (session_handle_expires_in > 0)
    g_variant_builder_add (&credentials, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (session_handle_expires_in)));
  if (password != NULL)
    g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  goa_oauth_provider_add_account_key_values (provider, &details);

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
  g_free (password);
  g_free (access_token);
  g_free (access_token_secret);
  g_free (session_handle);
  g_free (data.account_object_path);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth_provider_refresh_account (GoaProvider  *_provider,
                                    GoaClient    *client,
                                    GoaObject    *object,
                                    GtkWindow    *parent,
                                    GError      **error)
{
  GoaOAuthProvider *provider = GOA_OAUTH_PROVIDER (_provider);
  GoaAccount *account;
  GtkWidget *dialog;
  gchar *access_token = NULL;
  gchar *access_token_secret = NULL;
  gchar *password = NULL;
  gint access_token_expires_in;
  gchar *session_handle = NULL;
  gint session_handle_expires_in;
  gchar *identity = NULL;
  const gchar *existing_identity;
  const gchar *existing_presentation_identity;
  GVariantBuilder builder;
  gboolean ret = FALSE;

  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
  if (!get_tokens_and_identity (provider,
                                FALSE,
                                existing_presentation_identity,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                &access_token,
                                &access_token_secret,
                                &access_token_expires_in,
                                &session_handle,
                                &session_handle_expires_in,
                                &identity,
                                NULL, /* out_presentation_identity */
                                &password,
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
                   _("Was asked to log in as %s, but logged in as %s"),
                   existing_identity,
                   identity);
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  g_variant_builder_add (&builder, "{sv}", "access_token_secret", g_variant_new_string (access_token_secret));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (access_token_expires_in)));
  if (session_handle != NULL)
    g_variant_builder_add (&builder, "{sv}", "session_handle", g_variant_new_string (session_handle));
  if (session_handle_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (session_handle_expires_in)));
  if (password != NULL)
    g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));
  /* TODO: run in worker thread */
  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (provider),
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable  */
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
  g_free (access_token_secret);
  g_free (password);
  g_free (session_handle);
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
 * goa_oauth_provider_get_access_token_sync:
 * @provider: A #GoaOAuthProvider.
 * @object: A #GoaObject.
 * @force_refresh: If set to %TRUE, forces a refresh of the access token, if possible.
 * @out_access_token_secret: (out): The secret for the return access token.
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
goa_oauth_provider_get_access_token_sync (GoaOAuthProvider   *provider,
                                          GoaObject          *object,
                                          gboolean            force_refresh,
                                          gchar             **out_access_token_secret,
                                          gint               *out_access_token_expires_in,
                                          GCancellable       *cancellable,
                                          GError            **error)
{
  GVariant *credentials = NULL;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;
  gchar *access_token = NULL;
  gchar *access_token_secret = NULL;
  gchar *session_handle = NULL;
  gchar *access_token_for_refresh = NULL;
  gchar *access_token_secret_for_refresh = NULL;
  gchar *session_handle_for_refresh = NULL;
  gchar *password = NULL;
  gint access_token_expires_in = 0;
  gint session_handle_expires_in = 0;
  gboolean success = FALSE;
  GVariantBuilder builder;
  gchar *ret = NULL;
  GMutex *lock;

  g_return_val_if_fail (GOA_IS_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* provider_lock is too coarse, use a per-object lock instead */
  G_LOCK (provider_lock);
  lock = g_object_get_data (G_OBJECT (object), "-goa-oauth-provider-get-access-token-lock");
  if (lock == NULL)
    {
      lock = g_slice_new0 (GMutex);
      g_mutex_init (lock);
      g_object_set_data_full (G_OBJECT (object),
                              "-goa-oauth-provider-get-access-token-lock",
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
      else if (g_strcmp0 (key, "access_token_secret") == 0)
        access_token_secret = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "access_token_expires_at") == 0)
        access_token_expires_in = goa_utils_convert_abs_usec_to_duration_sec (g_variant_get_int64 (value));
      else if (g_strcmp0 (key, "session_handle") == 0)
        session_handle = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "session_handle_expires_at") == 0)
        session_handle_expires_in = goa_utils_convert_abs_usec_to_duration_sec (g_variant_get_int64 (value));
      else if (g_strcmp0 (key, "password") == 0)
        password = g_variant_dup_string (value, NULL);
      g_variant_unref (value);
    }

  if (access_token == NULL || access_token_secret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_AUTHORIZED,
                   _("Credentials do not contain access_token or access_token_secret"));
      goto out;
    }

  /* if we can't refresh the token, just return it no matter what */
  if (session_handle == NULL)
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
  access_token_for_refresh        = access_token; access_token = NULL;
  access_token_secret_for_refresh = access_token_secret; access_token_secret = NULL;
  session_handle_for_refresh      = session_handle; session_handle = NULL;
  access_token = get_tokens_sync (provider,
                                  access_token_for_refresh,
                                  access_token_secret_for_refresh,
                                  session_handle_for_refresh,
                                  NULL, /* verifier */
                                  &access_token_secret,
                                  &access_token_expires_in,
                                  &session_handle,
                                  &session_handle_expires_in,
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

  /* Good. Now update the keyring with the refreshed credentials */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  g_variant_builder_add (&builder, "{sv}", "access_token_secret", g_variant_new_string (access_token_secret));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (access_token_expires_in)));
  if (session_handle != NULL)
    g_variant_builder_add (&builder, "{sv}", "session_handle", g_variant_new_string (session_handle));
  if (session_handle_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (session_handle_expires_in)));
  if (password != NULL)
    g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));

  /* TODO: run in worker thread */
  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (provider),
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
      if (out_access_token_secret != NULL)
        {
          *out_access_token_secret = access_token_secret; access_token_secret = NULL;
        }
      if (out_access_token_expires_in != NULL)
        *out_access_token_expires_in = access_token_expires_in;
    }
  g_free (access_token);
  g_free (access_token_secret);
  g_free (session_handle);
  g_free (access_token_for_refresh);
  g_free (access_token_secret_for_refresh);
  g_free (session_handle_for_refresh);
  g_free (password);
  g_clear_pointer (&credentials, g_variant_unref);

  g_mutex_unlock (lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_access_token (GoaOAuthBased         *object,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data);

static gboolean
goa_oauth_provider_build_object (GoaProvider         *provider,
                                 GoaObjectSkeleton   *object,
                                 GKeyFile            *key_file,
                                 const gchar         *group,
                                 GDBusConnection     *connection,
                                 gboolean             just_added,
                                 GError             **error)
{
  GoaOAuthBased *oauth_based;
  gchar *identity;

  identity = NULL;

  oauth_based = goa_object_get_oauth_based (GOA_OBJECT (object));
  if (oauth_based != NULL)
    goto out;

  oauth_based = goa_oauth_based_skeleton_new ();
  goa_oauth_based_set_consumer_key (oauth_based,
                                    goa_oauth_provider_get_consumer_key (GOA_OAUTH_PROVIDER (provider)));
  goa_oauth_based_set_consumer_secret (oauth_based,
                                       goa_oauth_provider_get_consumer_secret (GOA_OAUTH_PROVIDER (provider)));
  /* Ensure D-Bus method invocations run in their own thread */
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (oauth_based),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  goa_object_skeleton_set_oauth_based (object, oauth_based);
  g_signal_connect (oauth_based,
                    "handle-get-access-token",
                    G_CALLBACK (on_handle_get_access_token),
                    NULL);

 out:
  g_object_unref (oauth_based);
  g_free (identity);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth_provider_ensure_credentials_sync (GoaProvider    *_provider,
                                            GoaObject      *object,
                                            gint           *out_expires_in,
                                            GCancellable   *cancellable,
                                            GError        **error)
{
  GoaOAuthProvider *provider = GOA_OAUTH_PROVIDER (_provider);
  gboolean ret = FALSE;
  gchar *access_token = NULL;
  gchar *access_token_secret = NULL;
  gint access_token_expires_in;
  gchar *identity = NULL;
  gboolean force_refresh = FALSE;

 again:
  access_token = goa_oauth_provider_get_access_token_sync (provider,
                                                                   object,
                                                                   force_refresh,
                                                                   &access_token_secret,
                                                                   &access_token_expires_in,
                                                                   cancellable,
                                                                   error);
  if (access_token == NULL)
    goto out;

  identity = goa_oauth_provider_get_identity_sync (provider,
                                                           access_token,
                                                           access_token_secret,
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
          g_free (access_token_secret); access_token_secret = NULL;
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
  g_free (access_token_secret);
  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth_provider_init (GoaOAuthProvider *client)
{
}

static void
goa_oauth_provider_class_init (GoaOAuthProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->add_account                = goa_oauth_provider_add_account;
  provider_class->refresh_account            = goa_oauth_provider_refresh_account;
  provider_class->build_object               = goa_oauth_provider_build_object;
  provider_class->ensure_credentials_sync    = goa_oauth_provider_ensure_credentials_sync;

  klass->build_authorization_uri  = goa_oauth_provider_build_authorization_uri_default;
  klass->get_use_mobile_browser   = goa_oauth_provider_get_use_mobile_browser_default;
  klass->is_deny_node             = goa_oauth_provider_is_deny_node_default;
  klass->is_password_node         = goa_oauth_provider_is_password_node_default;
  klass->get_request_uri_params   = goa_oauth_provider_get_request_uri_params_default;
  klass->add_account_key_values   = goa_oauth_provider_add_account_key_values_default;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in a thread dedicated to handling @invocation */
static gboolean
on_handle_get_access_token (GoaOAuthBased         *interface,
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
  gchar *access_token_secret = NULL;
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
  access_token = goa_oauth_provider_get_access_token_sync (GOA_OAUTH_PROVIDER (provider),
                                                                   object,
                                                                   FALSE, /* force_refresh */
                                                                   &access_token_secret,
                                                                   &access_token_expires_in,
                                                                   NULL, /* GCancellable* */
                                                                   &error);
  if (access_token == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
    }
  else
    {
      goa_oauth_based_complete_get_access_token (interface,
                                                 invocation,
                                                 access_token,
                                                 access_token_secret,
                                                 access_token_expires_in);
    }
  g_free (access_token);
  g_free (access_token_secret);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
