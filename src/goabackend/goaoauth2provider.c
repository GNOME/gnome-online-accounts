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

#include <rest/rest.h>
#include <libsoup/soup.h>
#include <libsecret/secret.h>
#include <json-glib/json-glib.h>

#include "goaauthflowbutton.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goautils.h"
#include "goaoauth2provider.h"
#include "goaoauth2provider-priv.h"
#include "goarestproxy.h"

#define OAUTH2_DBUS_HANDLER_NAME  "org.gnome.OnlineAccounts.OAuth2"
#define OAUTH2_DBUS_HANDLER_PATH  "/org/gnome/OnlineAccounts/OAuth2"
#define OAUTH2_DBUS_HANDLER_IFACE "org.gnome.OnlineAccounts.OAuth2"

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

#define GOA_OAUTH2_CODE_CHALLENGE_METHOD_S256 "S256"

/* Error Responses for Authorization Response
 *
 * See:
 *  - https://datatracker.ietf.org/doc/html/rfc6749#section-4.2
 *  - https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow#error-codes-for-authorization-endpoint-errors
 */
#define GOA_OAUTH2_AUTH_ERROR_INVALID_REQUEST           "invalid_request"
#define GOA_OAUTH2_AUTH_ERROR_UNAUTHORIZED_CLIENT       "unauthorized_client"
#define GOA_OAUTH2_AUTH_ERROR_ACCESS_DENIED             "access_denied"
#define GOA_OAUTH2_AUTH_ERROR_UNSUPPORTED_RESPONSE_TYPE "unsupported_response_type"
#define GOA_OAUTH2_AUTH_ERROR_INVALID_SCOPE             "invalid_scope"
#define GOA_OAUTH2_AUTH_ERROR_SERVER_ERROR              "server_error"
#define GOA_OAUTH2_AUTH_ERROR_TEMPORARILY_UNAVAILABLE   "temporarily_unavailable"

/* Error Responses for Access Token Request
 *
 * See:
 *  - https://datatracker.ietf.org/doc/html/rfc6749#section-5.2
 *  - https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow#error-codes-for-token-endpoint-errors
 */
#define GOA_OAUTH2_ACCESS_ERROR_INVALID_REQUEST         "invalid_request"
#define GOA_OAUTH2_ACCESS_ERROR_INVALID_CLIENT          "invalid_client"
#define GOA_OAUTH2_ACCESS_ERROR_INVALID_GRANT           "invalid_grant"
#define GOA_OAUTH2_ACCESS_ERROR_UNAUTHORIZED_CLIENT     "unauthorized_client"
#define GOA_OAUTH2_ACCESS_ERROR_UNSUPPORTED_GRANT_TYPE  "unsupported_grant_type"
#define GOA_OAUTH2_ACCESS_ERROR_INVALID_SCOPE           "invalid_scope"

static gboolean
is_authorization_error (GError *error)
{
  gboolean ret;

  g_return_val_if_fail (error != NULL, FALSE);

  ret = FALSE;
  if (error->domain == REST_PROXY_ERROR)
    {
      if (SOUP_STATUS_IS_CLIENT_ERROR (error->code))
        ret = TRUE;
    }
  else if (g_error_matches (error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
    {
      ret = TRUE;
    }
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_oauth2_provider_get_use_pkce_default (GoaOAuth2Provider  *self)
{
  return FALSE;
}

/**
 * goa_oauth2_provider_get_use_pkce:
 * @self: A #GoaOAuth2Provider.
 *
 * Returns whether the OAuth2 provider supports Proof Key for Code
 * Exchange (PKCE) as defined by <ulink
 * url="https://tools.ietf.org/html/rfc7636">RFC7636</ulink>.
 *
 * This is a virtual method where the default implementation returns
 * %FALSE.
 *
 * Returns: %TRUE if the provider supports PKCE, %FALSE otherwise.
 */
gboolean
goa_oauth2_provider_get_use_pkce (GoaOAuth2Provider *self)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), FALSE);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->get_use_pkce (self);
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
goa_oauth2_provider_build_authorization_uri_default (GoaOAuth2Provider *self,
                                                     const gchar       *authorization_uri,
                                                     const gchar       *escaped_redirect_uri,
                                                     const gchar       *escaped_client_id,
                                                     const gchar       *escaped_scope,
                                                     const gchar       *code_challenge_method,
                                                     const gchar       *code_challenge)
{
  GString *ret;

  g_debug ("%s: ********** ENTER\n", __FUNCTION__);
  ret = g_string_new (NULL);
  g_string_append_printf (ret,
                          "%s"
                          "?response_type=code"
                          "&redirect_uri=%s"
                          "&client_id=%s"
                          "&scope=%s",
                          authorization_uri,
                          escaped_redirect_uri,
                          escaped_client_id,
                          escaped_scope);

  if (code_challenge_method != NULL && code_challenge != NULL)
    {
      g_string_append_printf (ret,
                              "&code_challenge_method=%s"
                              "&code_challenge=%s",
                              code_challenge_method,
                              code_challenge);
    }

  g_debug ("%s: ====> %s\n", __FUNCTION__, ret->str);
  return g_string_free (ret, FALSE);
}

/**
 * goa_oauth2_provider_build_authorization_uri:
 * @self: A #GoaOAuth2Provider.
 * @authorization_uri: An authorization URI.
 * @escaped_redirect_uri: An escaped redirect URI
 * @escaped_client_id: An escaped client id
 * @escaped_scope: (nullable): The escaped scope or %NULL
 * @code_challenge_method: (nullable): The code challenge method or %NULL
 * @code_challenge: (nullable): The code challenge or %NULL
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
 * three escaped using g_uri_escape_string(). The @code_challenge_method and
 * @code_challenge are used to support Proof Key for Code Exchange (PKCE) as
 * defined by <ulink url="https://tools.ietf.org/html/rfc7636">RFC7636</ulink>.
 *
 * Returns: (transfer full): An authorization URI that must be freed with g_free().
 */
gchar *
goa_oauth2_provider_build_authorization_uri (GoaOAuth2Provider *self,
                                             const gchar       *authorization_uri,
                                             const gchar       *escaped_redirect_uri,
                                             const gchar       *escaped_client_id,
                                             const gchar       *escaped_scope,
                                             const gchar       *code_challenge_method,
                                             const gchar       *code_challenge)
{
  g_return_val_if_fail (GOA_IS_OAUTH2_PROVIDER (self), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_redirect_uri != NULL, NULL);
  g_return_val_if_fail (escaped_client_id != NULL, NULL);
  return GOA_OAUTH2_PROVIDER_GET_CLASS (self)->build_authorization_uri (self,
                                                                        authorization_uri,
                                                                        escaped_redirect_uri,
                                                                        escaped_client_id,
                                                                        escaped_scope,
                                                                        code_challenge_method,
                                                                        code_challenge);
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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  GCancellable *cancellable;

  char *authorization_code;
  char *access_token;
  int access_token_expires_in;
  char *identity;
  char *password;
  char *presentation_identity;
  char *refresh_token;
  char *request_uri;
  GoaAuthFlowFlags flags;

  char *client_id;
  char *client_secret;
  char *token_uri;
  char *authorization_uri;
  char *redirect_uri;
  char *code_verifier;
} AccountData;

/* <private>
 * account_data_sync:
 * @self: a `GoaOAuth2Provider`
 * @data: a `AccountData` struct
 *
 * Sync the OAuth 2.0 client configuration from the account keyfile, with a
 * fallback for the compile-time defaults.
 */
static void
account_data_sync (GoaOAuth2Provider *self,
                   AccountData       *data)
{
  GoaObject *object = data->object;
  char *tmp = NULL;

  g_free (data->client_id);
  g_free (data->client_secret);
  g_free (data->token_uri);
  g_free (data->authorization_uri);
  g_free (data->redirect_uri);
  g_free (data->code_verifier);

  if (object == NULL || (tmp = goa_util_lookup_keyfile_string (object, "OAuth2ClientId")) == NULL)
      tmp = g_strdup (goa_oauth2_provider_get_client_id (self));
  data->client_id = tmp;

  if (object == NULL || (tmp = goa_util_lookup_keyfile_string (object, "OAuth2ClientSecret")) == NULL)
      tmp = g_strdup (goa_oauth2_provider_get_client_secret (self));
  data->client_secret = tmp;

  if (object == NULL || (tmp = goa_util_lookup_keyfile_string (object, "OAuth2TokenUri")) == NULL)
      tmp = g_strdup (goa_oauth2_provider_get_token_uri (self));
  data->token_uri = tmp;

  if (object == NULL || (tmp = goa_util_lookup_keyfile_string (object, "OAuth2AuthorizationUri")) == NULL)
      tmp = g_strdup (goa_oauth2_provider_get_authorization_uri (self));
  data->authorization_uri = tmp;

  if (object == NULL || (tmp = goa_util_lookup_keyfile_string (object, "OAuth2RedirectUri")) == NULL)
      tmp = g_strdup (goa_oauth2_provider_get_redirect_uri (self));
  data->redirect_uri = tmp;

  g_debug ("- client_id=%s", data->client_id);
  g_debug ("- client_secret=%s", data->client_secret);
  g_debug ("- token_uri=%s", data->token_uri);
  g_debug ("- authorization_uri=%s", data->authorization_uri);
  g_debug ("- redirect_uri=%s", data->redirect_uri);
}

static void
account_data_free (gpointer user_data)
{
  AccountData *data = (AccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_object (&data->cancellable);

  g_clear_pointer (&data->client_id, g_free);
  g_clear_pointer (&data->client_secret, g_free);
  g_clear_pointer (&data->token_uri, g_free);
  g_clear_pointer (&data->authorization_uri, g_free);
  g_clear_pointer (&data->redirect_uri, g_free);
  g_clear_pointer (&data->code_verifier, g_free);

  g_clear_pointer (&data->authorization_code, g_free);
  g_clear_pointer (&data->access_token, g_free);
  g_clear_pointer (&data->identity, g_free);
  g_clear_pointer (&data->password, g_free);
  g_clear_pointer (&data->presentation_identity, g_free);
  g_clear_pointer (&data->refresh_token, g_free);
  g_clear_pointer (&data->request_uri, g_free);

  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_tokens_sync (GoaOAuth2Provider  *self,
                 AccountData        *data,
                 const gchar        *authorization_code,
                 const gchar        *refresh_token,
                 gchar             **out_refresh_token,
                 gint               *out_access_token_expires_in,
                 GCancellable       *cancellable,
                 GError            **error)
{
  g_autoptr (GError) rest_error = NULL;
  GError *tokens_error = NULL;
  RestProxy *proxy;
  RestProxyCall *call;
  gchar *ret = NULL;
  gchar *ret_access_token = NULL;
  gint ret_access_token_expires_in = 0;
  gchar *ret_refresh_token = NULL;
  const gchar *payload;
  gsize payload_length;

  proxy = goa_rest_proxy_new (data->token_uri, FALSE);
  call = rest_proxy_new_call (proxy);

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "client_id", data->client_id);

  if (data->client_secret != NULL)
    rest_proxy_call_add_param (call, "client_secret", data->client_secret);

  if (data->code_verifier != NULL)
    rest_proxy_call_add_param (call, "code_verifier", data->code_verifier);

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
      rest_proxy_call_add_param (call, "redirect_uri", data->redirect_uri);
      rest_proxy_call_add_param (call, "code", authorization_code);
    }

  if (!goa_rest_proxy_call_sync (call, cancellable, &rest_error))
    {
      guint status_code;

      status_code = rest_proxy_call_get_status_code (call);
      if (SOUP_STATUS_IS_CLIENT_ERROR (status_code))
        {
          g_autoptr (JsonParser) parser = NULL;
          JsonObject *object;
          const char *response_err;
          const char *response_desc;

          payload = rest_proxy_call_get_payload (call);
          payload_length = rest_proxy_call_get_payload_length (call);

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
              goto out;
            }

          object = json_node_get_object (json_parser_get_root (parser));
          response_err = json_object_get_string_member_with_default (object, "error", NULL);
          response_desc = json_object_get_string_member_with_default (object, "error_description", NULL);

          /* Some OAuth providers have access policies controlled by the service. These cases may
           * require re-authentication rather than simply refreshing.
           */
          if (g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_INVALID_CLIENT) == 0
              || g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_INVALID_GRANT) == 0)
            {
              g_debug ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
              g_set_error_literal (error,
                                   GOA_ERROR,
                                   GOA_ERROR_NOT_AUTHORIZED,
                                   response_desc ? response_desc : response_err);
            }
          else if (g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_UNAUTHORIZED_CLIENT) == 0
                   || g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_UNSUPPORTED_GRANT_TYPE) == 0)
            {
              g_warning ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
              g_set_error_literal (error,
                                   GOA_ERROR,
                                   GOA_ERROR_NOT_SUPPORTED,
                                   response_desc ? response_desc : response_err);
            }
          else if (g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_INVALID_REQUEST) == 0
                   || g_strcmp0 (response_err, GOA_OAUTH2_ACCESS_ERROR_INVALID_SCOPE) == 0)
            {
              g_critical ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
              g_set_error_literal (error,
                                   GOA_ERROR,
                                   GOA_ERROR_FAILED,
                                   response_desc ? response_desc : response_err);
            }
          else
            {
              g_debug ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
              g_set_error_literal (error,
                                   GOA_ERROR,
                                   GOA_ERROR_FAILED,
                                   response_desc ? response_desc : response_err);
            }
        }
      else
        {
          g_debug ("%s(): %s", G_STRFUNC, rest_error->message);
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Expected status 200 when requesting access token, instead got status %d (%s)"),
                       status_code,
                       rest_proxy_call_get_status_message (call));
        }

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

static gboolean
parse_request_uri (GoaOAuth2Provider  *self,
                   GTask              *task,
                   const char         *requested_uri,
                   GError            **error)
{
  AccountData *data = g_task_get_task_data (task);
  g_autoptr(GHashTable) key_value_pairs = NULL;
  g_autoptr(GUri) uri = NULL;
  g_autoptr(GUri) redirect_uri = NULL;
  const char *fragment;
  const char *query;
  const char *response_err;
  const char *response_desc;

  g_assert (error == NULL || *error == NULL);

  uri = g_uri_parse (requested_uri, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, error);
  if (uri == NULL)
    return FALSE;

  redirect_uri = g_uri_parse (data->redirect_uri, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, error);
  if (redirect_uri == NULL)
    return FALSE;

  if (g_strcmp0 (g_uri_get_scheme (uri), g_uri_get_scheme (redirect_uri)) != 0
      || g_strcmp0 (g_uri_get_path (uri), g_uri_get_path (redirect_uri)) != 0)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Invalid URI: %s"),
                   requested_uri);
      return FALSE;
    }

  /* Three cases:
   * 1) we can either have the backend handle the URI for us, or
   * 2) we can either have the access_token and other information
   *    directly in the fragment part of the URI, or
   * 3) the auth code can be in the query part of the URI, with which
   *    we'll obtain the token later.
   */
  if (GOA_OAUTH2_PROVIDER_GET_CLASS (self)->process_redirect_url)
    {
      g_autofree char *url = NULL;

      url = g_uri_to_string (uri);
      if (!goa_oauth2_provider_process_redirect_url (self, url, &data->access_token, error))
        {
          g_prefix_error (error, _("Authorization response: "));
          ((GError *)*error)->domain = GOA_ERROR;
          ((GError *)*error)->code = GOA_ERROR_NOT_AUTHORIZED;

          return FALSE;
        }

      return TRUE;
    }

  fragment = g_uri_get_fragment (uri);
  if (fragment != NULL)
    {
      /* fragment is encoded into a key/value pairs for the token and
       * expiration values, using the same syntax as a URL query */
      key_value_pairs = soup_form_decode (fragment);

      /* We might use oauth2_proxy_extract_access_token() here but
       * we can also extract other information.
       */
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

          data->refresh_token = g_strdup (g_hash_table_lookup (key_value_pairs, "refresh_token"));
        }
      g_clear_pointer (&key_value_pairs, g_hash_table_unref);

      if (data->access_token != NULL)
        return TRUE;
    }

  query = g_uri_get_query (uri);
  if (query != NULL)
    {
      key_value_pairs = soup_form_decode (query);

      data->authorization_code = g_strdup (g_hash_table_lookup (key_value_pairs, "code"));
      if (data->authorization_code != NULL)
        return TRUE;
    }
  else
    {
      g_warning ("Did not find access_token, code or error in response");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
    }

  /* In case we don't find the access_token or auth code, then look
   * for the error in the query part of the URI.
   */
  response_err = (const char *) g_hash_table_lookup (key_value_pairs, "error");
  response_desc = (const char *) g_hash_table_lookup (key_value_pairs, "error_description");
  if (g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_ACCESS_DENIED) == 0)
    {
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_AUTHORIZED,
                           response_desc ? response_desc : response_err);
    }
  else if (g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_UNAUTHORIZED_CLIENT) == 0
           || g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_UNSUPPORTED_RESPONSE_TYPE) == 0)
    {
      g_warning ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_SUPPORTED,
                           response_desc ? response_desc : response_err);
    }
  else if (g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_INVALID_REQUEST) == 0
           || g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_INVALID_SCOPE) == 0)
    {
      g_critical ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           response_desc ? response_desc : response_err);
    }
  else if (g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_SERVER_ERROR) == 0
           || g_strcmp0 (response_err, GOA_OAUTH2_AUTH_ERROR_TEMPORARILY_UNAVAILABLE) == 0)
    {
      g_warning ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           response_desc ? response_desc : response_err);
    }
  else
    {
      g_debug ("%s(): [%s] %s", G_STRFUNC, response_err, response_desc);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           response_desc ? response_desc : response_err);
    }

  return FALSE;
}

static gboolean
identity_from_auth (GoaOAuth2Provider  *self,
                    GTask              *task,
                    GError            **error)
{
  AccountData *data = g_task_get_task_data (task);

  /* We can have either the auth code, with which we'll obtain the token, or
   * the token directly if we are using a client side flow, since we don't
   * need to pass the code to the remote application.
   */
  if (data->authorization_code != NULL)
    {
      /* OK, we now have the authorization code... now we need to get the
       * email address (to e.g. check if the account already exists on
       * @client).. for that we need to get a (short-lived) access token
       * and a refresh_token
       */

      /* TODO: run in worker thread */
      data->access_token = get_tokens_sync (self,
                                            data,
                                            data->authorization_code,
                                            NULL, /* refresh_token */
                                            &data->refresh_token,
                                            &data->access_token_expires_in,
                                            g_task_get_cancellable (task),
                                            error);
      if (data->access_token == NULL)
        {
          g_prefix_error (error, _("Error getting an Access Token: "));
          return FALSE;
        }
    }

  g_assert (data->access_token != NULL);

  /* TODO: run in worker thread */
  data->identity = goa_oauth2_provider_get_identity_sync (self,
                                                          data->access_token,
                                                          &data->presentation_identity,
                                                          g_task_get_cancellable (task),
                                                          error);

  if (data->identity == NULL)
    {
      g_prefix_error (error, _("Error getting identity: "));
      return FALSE;
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_copy_activated (GoaAuthflowButton *widget,
                   AccountData       *data)
{
  data->flags |= GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI;
  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_BUSY);
  goa_authflow_button_set_active (widget, TRUE);
}

static void
on_link_activated (GoaAuthflowButton *widget,
                   AccountData       *data)
{
  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_BUSY);
  goa_authflow_button_set_active (widget, TRUE);
}

static void
create_account_details_ui (GoaProvider *provider,
                           AccountData *data,
                           gboolean     new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *content;
  GtkWidget *button;
  g_autofree char *provider_name = NULL;
  g_autofree char *description = NULL;

  provider_name = goa_provider_get_provider_name (provider, NULL);
  description = g_strdup_printf (_("Sign in to %s with your browser"), provider_name);

  button = goa_authflow_button_new ();
  g_signal_connect (button,
                    "copy-activated",
                    G_CALLBACK (on_copy_activated),
                    data);
  g_signal_connect (button,
                    "link-activated",
                    G_CALLBACK (on_link_activated),
                    data);

  content = g_object_new (ADW_TYPE_STATUS_PAGE,
                          "icon-name",   "web-browser-symbolic",
                          "description", description,
                          "child",       button,
                          NULL);
  goa_provider_dialog_push_content (dialog, NULL, content);

  /* Set the default widget after it's a child of the window */
  adw_dialog_set_default_widget (ADW_DIALOG (dialog), button);
}

/* ---------------------------------------------------------------------------------------------------- */

static GDBusNodeInfo *oauth2_handler_dbus_info = NULL;
static const char oauth2_handler_dbus_xml[] =
  "<node>"
  "  <interface name='"OAUTH2_DBUS_HANDLER_IFACE"'>"
  "    <method name='Response'>"
  "      <arg type='s' name='client-id' direction='in'/>"
  "      <arg type='s' name='response' direction='in'/>"
  "    </method>"
  "  </interface>"
  "</node>";

typedef struct
{
  char *request_uri;
  GoaAuthFlowFlags flags;
  GCancellable *cancellable;
  unsigned long cancellable_id;

  /* D-Bus */
  GDBusConnection *connection;
  unsigned int name_owner_id;
  unsigned int register_object_id;
} AuthorizeUriData;

static void
authorize_uri_task_complete (GCancellable *cancellable,
                             GTask        *task)
{
  AuthorizeUriData *data = g_task_get_task_data (task);

  g_assert (data != NULL);

  /* If @cancellable is not %NULL, we're in the callback and the task needs to
   * be completed manually. Otherwise, it's safe to disconnect the callback.
   */
  if (cancellable != NULL)
    {
      g_task_return_error_if_cancelled (task);
    }
  else if (data->cancellable != NULL && data->cancellable_id != 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_clear_object (&data->cancellable);
    }

  if (data->register_object_id != 0)
    {
      g_dbus_connection_unregister_object (data->connection, data->register_object_id);
      data->register_object_id = 0;
    }

  if (data->name_owner_id != 0)
    {
      g_bus_unown_name (data->name_owner_id);
      data->name_owner_id = 0;
    }

  g_clear_object (&data->connection);
}

static void
authorize_uri_data_free (gpointer user_data)
{
  AuthorizeUriData *data = user_data;

  /* If the cancellable is triggered, we couldn't disconnect in the callback
   * without deadlocking. Either way, this function is only called after
   * the D-Bus bits are released and drop their references on the task.
   */
  if (data->cancellable != NULL && data->cancellable_id != 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_clear_object (&data->cancellable);
    }

  g_clear_pointer (&data->request_uri, g_free);
  g_free (data);
}

static void
oauth2_handler_dbus_method_call (GDBusConnection       *connection,
                                 const char            *sender,
                                 const char            *object_path,
                                 const char            *interface_name,
                                 const char            *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
  GTask *task = G_TASK (user_data);

  if (g_strcmp0 (method_name, "Response") == 0)
    {
      const char *client_id = NULL;
      const char *response = NULL;
      GError *error = NULL;

      g_variant_get (parameters, "(&s&s)", &client_id, &response);
      if (g_uri_is_valid (response, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, &error))
        {
          g_debug ("Received OAuth2 response for client ID \"%s\"", client_id);

          g_dbus_method_invocation_return_value (invocation, NULL);
          authorize_uri_task_complete (NULL, task);
          g_task_return_pointer (task, g_strdup (response), g_free);
        }
      else
        {
          g_debug ("Received invalid OAuth2 response for client ID \"%s\"", client_id);

          g_dbus_method_invocation_return_error (invocation,
                                                 G_DBUS_ERROR,
                                                 G_DBUS_ERROR_INVALID_ARGS,
                                                 "Invalid URI \"%s\"",
                                                 response);
          authorize_uri_task_complete (NULL, task);
          g_task_return_error (task, g_steal_pointer (&error));
        }

      return;
    }

  g_dbus_method_invocation_return_error (invocation,
                                         G_DBUS_ERROR,
                                         G_DBUS_ERROR_UNKNOWN_METHOD,
                                         "Unknown method %s on %s",
                                         method_name,
                                         interface_name);
}

static const GDBusInterfaceVTable oauth2_handler_dbus_vtable =
  {
    oauth2_handler_dbus_method_call,
    NULL,
    NULL
  };

static void
authorize_uri_launch_uri_cb (GObject      *object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!g_app_info_launch_default_for_uri_finish (result, &error))
    {
      authorize_uri_task_complete (NULL, task);
      g_task_return_error (task, g_steal_pointer (&error));
    }
}

static void
on_oauth2_bus_acquired (GDBusConnection *connection,
                        const char      *name,
                        gpointer         user_data)
{
  static size_t guard = 0;
  GTask *task = G_TASK (user_data);
  AuthorizeUriData *data = g_task_get_task_data (task);
  GError *error = NULL;

  if (g_once_init_enter (&guard))
    {
      oauth2_handler_dbus_info = g_dbus_node_info_new_for_xml (oauth2_handler_dbus_xml, NULL);
      g_once_init_leave (&guard, 1);
    }

  data->connection = g_object_ref (connection);
  data->register_object_id =
    g_dbus_connection_register_object (data->connection,
                                       OAUTH2_DBUS_HANDLER_PATH,
                                       oauth2_handler_dbus_info->interfaces[0],
                                       &oauth2_handler_dbus_vtable,
                                       g_object_ref (task),
                                       g_object_unref,
                                       &error);

  if (data->register_object_id == 0)
    {
      authorize_uri_task_complete (NULL, task);
      g_task_return_error (task, g_steal_pointer (&error));
    }
}

static void
on_oauth2_name_acquired (GDBusConnection *connection,
                         const char      *name,
                         gpointer         user_data)
{
  GTask *task = G_TASK (user_data);
  GCancellable *cancellable = g_task_get_cancellable (task);
  AuthorizeUriData *data = g_task_get_task_data (task);

  if ((data->flags & GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI) == 0)
    {
      g_app_info_launch_default_for_uri_async (data->request_uri,
                                               NULL,
                                               cancellable,
                                               (GAsyncReadyCallback) authorize_uri_launch_uri_cb,
                                               g_object_ref (task));
    }
}

static void
on_oauth2_name_lost (GDBusConnection *connection,
                     const char      *name,
                     gpointer         user_data)
{
  GTask *task = G_TASK (user_data);

  if (connection == NULL)
    g_warning ("%s(): Failed to connect to the session bus", G_STRFUNC);
  else
    g_warning ("%s(): Failed to own %s on the session bus", G_STRFUNC, name);

  authorize_uri_task_complete (NULL, task);
  g_task_return_new_error_literal (task,
                                   GOA_ERROR,
                                   GOA_ERROR_FAILED,
                                   _("Service not available"));
}

/* ---------------------------------------------------------------------------------------------------- */

/*< private >
 * goa_oauth2_provider_authorize_uri:
 * @self: a `GoaOAuth2Provider`
 * @request_uri: a request URI
 * @flags: flags for the authorization flow
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Request authorization of @uri in the default application (i.e. web browser).
 *
 * The @flags argument controls how the authorization flow is executed, such as
 * whether the URI will be launched automatically.
 *
 * Call [method@Goa.OAuth2Provider.authorize_uri_finish] to get the result.
 */
void
goa_oauth2_provider_authorize_uri (GoaOAuth2Provider   *provider,
                                   const char          *request_uri,
                                   GoaAuthFlowFlags     flags,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  AuthorizeUriData *data;

  g_return_if_fail (GOA_IS_PROVIDER (provider));
  g_return_if_fail (request_uri != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AuthorizeUriData, 1);
  data->request_uri = g_strdup (request_uri);
  data->flags = flags;

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, goa_oauth2_provider_authorize_uri);
  g_task_set_task_data (task, data, authorize_uri_data_free);

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (authorize_uri_task_complete),
                                                    task,
                                                    NULL);
    }

  data->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                        OAUTH2_DBUS_HANDLER_NAME,
                                        G_BUS_NAME_OWNER_FLAGS_NONE,
                                        on_oauth2_bus_acquired,
                                        on_oauth2_name_acquired,
                                        on_oauth2_name_lost,
                                        g_object_ref (task),
                                        g_object_unref);
}

/*< private >
 * goa_oauth2_provider_authorize_uri_finish:
 * @provider: a `GoaOAuth2Provider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a return location for an error
 *
 * Finish an operation started with [class@Goa.OAuth2Provider.authorize_uri].
 *
 * Returns: (transfer full): the response URI, or %NULL with @error set
 */
char *
goa_oauth2_provider_authorize_uri_finish (GoaOAuth2Provider  *provider,
                                          GAsyncResult       *result,
                                          GError            **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_task_is_valid (result, provider), NULL);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == goa_oauth2_provider_authorize_uri, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
oauth2_task_prepare_request_uri (GTask *task)
{
  GoaOAuth2Provider *self = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);
  const char *scope;
  g_autofree char *escaped_redirect_uri = NULL;
  g_autofree char *escaped_client_id = NULL;
  g_autofree char *escaped_scope = NULL;
  g_autofree gchar *code_challenge = NULL;

  account_data_sync (GOA_OAUTH2_PROVIDER (self), data);

  /* TODO: use oauth2_proxy_build_login_url_full() */
  escaped_redirect_uri = g_uri_escape_string (data->redirect_uri, NULL, TRUE);
  escaped_client_id = g_uri_escape_string (data->client_id, NULL, TRUE);
  scope = goa_oauth2_provider_get_scope (self);
  if (scope != NULL)
    escaped_scope = g_uri_escape_string (goa_oauth2_provider_get_scope (self), NULL, TRUE);

  if (goa_oauth2_provider_get_use_pkce (self))
    {
      data->code_verifier = goa_utils_generate_code_verifier ();
      code_challenge = goa_utils_generate_code_challenge (data->code_verifier);
    }

  data->request_uri = goa_oauth2_provider_build_authorization_uri (self,
                                                                   data->authorization_uri,
                                                                   escaped_redirect_uri,
                                                                   escaped_client_id,
                                                                   escaped_scope,
                                                                   code_challenge != NULL ? GOA_OAUTH2_CODE_CHALLENGE_METHOD_S256 : NULL,
                                                                   code_challenge);
}

static gboolean
oauth2_task_handle_response_uri (GTask       *task,
                                 const char  *uri,
                                 GError     **error)
{
  GoaOAuth2Provider *self = g_task_get_source_object (task);

  if (!parse_request_uri (self, task, uri, error))
    return FALSE;

  if (!identity_from_auth (self, task, error))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_credentials_key_values (GTask           *task,
                            GVariantBuilder *credentials)
{
  AccountData *data = g_task_get_task_data (task);

  if (data->authorization_code != NULL)
    g_variant_builder_add (credentials, "{sv}", "authorization_code",
                           g_variant_new_string (data->authorization_code));
  g_variant_builder_add (credentials, "{sv}", "access_token", g_variant_new_string (data->access_token));
  if (data->access_token_expires_in > 0)
    g_variant_builder_add (credentials, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (data->access_token_expires_in)));
  if (data->refresh_token != NULL)
    g_variant_builder_add (credentials, "{sv}", "refresh_token", g_variant_new_string (data->refresh_token));
  if (data->password != NULL)
    g_variant_builder_add (credentials, "{sv}", "password", g_variant_new_string (data->password));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_credentials_cb (GoaManager   *manager,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AccountData *data = g_task_get_task_data (task);
  g_autofree char *object_path = NULL;
  GDBusObject *ret = NULL;
  GError *error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, error);
      return;
    }

  ret = g_dbus_object_manager_get_object (goa_client_get_object_manager (data->client),
                                          object_path);
  goa_provider_task_return_account (task, GOA_OBJECT (ret));
}

static void
add_account_authorize_uri_cb (GoaOAuth2Provider *provider,
                              GAsyncResult      *result,
                              gpointer           user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  GoaOAuth2Provider *self = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  g_autofree char *response_uri = NULL;
  g_autoptr(GError) error = NULL;

  response_uri = goa_oauth2_provider_authorize_uri_finish (provider, result, &error);
  if (response_uri == NULL)
    {
      /* If the dialog was closed, the task has already been completed.
       */
      if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_DONE)
        goa_provider_task_return_error (task, g_steal_pointer (&error));

      return;
    }

  if (!oauth2_task_handle_response_uri (task, response_uri, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* If this is duplicate account we're finished */
  if (!goa_utils_check_duplicate (data->client,
                                  data->identity,
                                  data->presentation_identity,
                                  goa_provider_get_provider_type (GOA_PROVIDER (self)),
                                  (GoaPeekInterfaceFunc) goa_object_peek_oauth2_based,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Account is confirmed */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (task, &credentials);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  goa_oauth2_provider_add_account_key_values (self, &details);

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (GOA_PROVIDER (self)),
                                data->identity,
                                data->presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_steal_pointer (&task));
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaOAuth2Provider *self = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_DONE)
    {
      g_cancellable_cancel (data->cancellable);
      return;
    }

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_BUSY)
    {
      oauth2_task_prepare_request_uri (task);

      if ((data->flags & GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI) != 0)
        {
          gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (dialog)),
                                  data->request_uri);
          goa_provider_dialog_add_toast (dialog, adw_toast_new (_("Copied to clipboard")));
        }

      goa_oauth2_provider_authorize_uri (self,
                                         data->request_uri,
                                         data->flags,
                                         data->cancellable,
                                         (GAsyncReadyCallback) add_account_authorize_uri_cb,
                                         g_object_ref (task));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth2_provider_add_account (GoaProvider         *provider,
                                 GoaClient           *client,
                                 GtkWidget           *parent,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  AccountData *data;
  g_autoptr(GTask) task = NULL;

  data = g_new0 (AccountData, 1);
  data->client = g_object_ref (client);
  data->cancellable = g_cancellable_new ();

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, goa_oauth2_provider_add_account);
  g_task_set_task_data (task, data, account_data_free);

  /* If the parent is a provider dialog, then a derived class is chaining up
   * and has handled the setup UI.
   */
  if (GOA_IS_PROVIDER_DIALOG (parent))
    {
      data->dialog = GOA_PROVIDER_DIALOG (parent);
      data->flags = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (data->dialog),
                                                         "goa-auth-flow-flags"));
      /* The signal handler holds the reference to @task.
       */
      g_signal_connect_object (data->dialog,
                               "notify::state",
                               G_CALLBACK (add_account_action_cb),
                               g_object_ref (task),
                               G_CONNECT_DEFAULT);
      g_object_set_data (G_OBJECT (task), "goa-provider-dialog", parent);
      add_account_action_cb (data->dialog, NULL, task);
    }
  else
    {
      data->dialog = goa_provider_dialog_new (provider, client, parent);
      create_account_details_ui (provider, data, TRUE);
      g_signal_connect_object (data->dialog,
                               "notify::state",
                               G_CALLBACK (add_account_action_cb),
                               task,
                               0 /* G_CONNECT_DEFAULT */);
      goa_provider_task_run_in_dialog (task, data->dialog);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account_credentials_cb (GoaAccount   *account,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!goa_account_call_ensure_credentials_finish (account, NULL, res, &error))
    {
      goa_provider_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
refresh_account_authorize_uri_cb (GoaOAuth2Provider *provider,
                                  GAsyncResult      *result,
                                  gpointer           user_data)
{
  g_autoptr(GTask) task = G_TASK (user_data);
  GoaOAuth2Provider *self = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autofree char *response_uri = NULL;
  GoaAccount *account;
  const char *existing_identity;
  GVariantBuilder credentials;
  g_autoptr(GError) error = NULL;

  response_uri = goa_oauth2_provider_authorize_uri_finish (provider, result, &error);
  if (response_uri == NULL)
    {
      /* If the dialog was closed, the task has already been completed.
       */
      if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_DONE)
        goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (!oauth2_task_handle_response_uri (task, response_uri, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Confirm the correct identity was re-authorized */
  account = goa_object_peek_account (data->object);
  existing_identity = goa_account_get_identity (account);
  if (g_strcmp0 (data->identity, existing_identity) != 0)
    {
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Was asked to log in as %s, but logged in as %s"),
                               existing_identity,
                               data->identity);
      return;
    }

  /* Account is confirmed */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (task, &credentials);

  // TODO: run in worker thread
  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (self),
                                                    data->object,
                                                    g_variant_builder_end (&credentials),
                                                    cancellable,
                                                    &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_steal_pointer (&task));
}

static void
refresh_account_action_cb (GoaProviderDialog *dialog,
                           GParamSpec        *pspec,
                           GTask             *task)
{
  GoaOAuth2Provider *self = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_DONE)
    {
      g_cancellable_cancel (data->cancellable);
      return;
    }

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_BUSY)
    {
      oauth2_task_prepare_request_uri (task);

      if ((data->flags & GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI) != 0)
        {
          gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (dialog)),
                                  data->request_uri);
          goa_provider_dialog_add_toast (dialog, adw_toast_new (_("Copied to clipboard")));
        }

      goa_oauth2_provider_authorize_uri (self,
                                         data->request_uri,
                                         data->flags,
                                         data->cancellable,
                                         (GAsyncReadyCallback) refresh_account_authorize_uri_cb,
                                         g_object_ref (task));
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_oauth2_provider_refresh_account (GoaProvider         *provider,
                                     GoaClient           *client,
                                     GoaObject           *object,
                                     GtkWidget           *parent,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  AccountData *data;
  g_autoptr(GTask) task = NULL;

  g_assert (GOA_IS_OAUTH2_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WIDGET (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AccountData, 1);
  data->client = g_object_ref (client);
  data->object = g_object_ref (object);
  data->cancellable = g_cancellable_new ();

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, goa_oauth2_provider_refresh_account);
  g_task_set_task_data (task, data, account_data_free);

  /* If the parent is a provider dialog, then a derived class is chaining up
   * and has handled the setup UI.
   */
  if (GOA_IS_PROVIDER_DIALOG (parent))
    {
      data->dialog = GOA_PROVIDER_DIALOG (parent);
      data->flags = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (data->dialog),
                                                         "goa-auth-flow-flags"));
      /* The signal handler holds the reference to @task.
       */
      g_signal_connect_object (data->dialog,
                               "notify::state",
                               G_CALLBACK (refresh_account_action_cb),
                               g_object_ref (task),
                               G_CONNECT_DEFAULT);
      g_object_set_data (G_OBJECT (task), "goa-provider-dialog", parent);
      refresh_account_action_cb (data->dialog, NULL, task);
    }
  else
    {
      data->dialog = goa_provider_dialog_new (provider, client, parent);
      create_account_details_ui (provider, data, TRUE);
      g_signal_connect_object (data->dialog,
                               "notify::state",
                               G_CALLBACK (refresh_account_action_cb),
                               task,
                               0 /* G_CONNECT_DEFAULT */);
      goa_provider_task_run_in_dialog (task, data->dialog);
    }
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
  AccountData *data = NULL;
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
        access_token_expires_in = goa_utils_convert_abs_usec_to_duration_sec (g_variant_get_int64 (value));
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
  data = g_new0 (AccountData, 1);
  data->object = g_object_ref (object);
  account_data_sync (self, data);

  old_refresh_token = refresh_token; refresh_token = NULL;
  g_free (access_token); access_token = NULL;
  access_token = get_tokens_sync (self,
                                  data,
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
                           g_variant_new_int64 (goa_utils_convert_duration_sec_to_abs_usec (access_token_expires_in)));
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
  g_clear_pointer (&credentials, g_variant_unref);
  g_clear_pointer (&data, account_data_free);

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
  GoaOAuth2Provider *self = GOA_OAUTH2_PROVIDER (provider);
  GoaOAuth2Based *oauth2_based;
  AccountData *data = NULL;

  oauth2_based = goa_object_get_oauth2_based (GOA_OBJECT (object));
  if (oauth2_based != NULL)
    goto out;

  data = g_new0 (AccountData, 1);
  data->object = g_object_ref (GOA_OBJECT (object));
  account_data_sync (self, data);

  oauth2_based = goa_oauth2_based_skeleton_new ();
  goa_oauth2_based_set_client_id (oauth2_based, data->client_id);
  goa_oauth2_based_set_client_secret (oauth2_based, data->client_secret);

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
  g_clear_pointer (&data, account_data_free);
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

  access_token = goa_oauth2_provider_get_access_token_sync (self,
                                                            object,
                                                            force_refresh,
                                                            &access_token_expires_in,
                                                            cancellable,
                                                            error);
  if (access_token == NULL)
    goto out;

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
goa_oauth2_provider_init (GoaOAuth2Provider *self)
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
  klass->get_scope                = goa_oauth2_provider_get_scope_default;
  klass->get_use_pkce             = goa_oauth2_provider_get_use_pkce_default;
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
