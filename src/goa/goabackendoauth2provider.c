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
#include <stdlib.h>

#include <rest/oauth2-proxy.h>
#include <webkit/webkit.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauth2provider.h"

/**
 * SECTION:goabackendoauth2provider
 * @title: GoaBackendOAuth2Provider
 * @short_description: Abstract base class for OAuth 2.0 providers
 *
 * #GoaBackendOAuth2Provider is a base class for all OAuth 2.0 based providers.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendOAuth2Provider, goa_backend_oauth2_provider, GOA_TYPE_BACKEND_PROVIDER);

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
goa_backend_oauth2_provider_build_authorization_uri_default (GoaBackendOAuth2Provider  *provider,
                                                             const gchar               *authorization_uri,
                                                             const gchar               *escaped_redirect_uri,
                                                             const gchar               *escaped_client_id,
                                                             const gchar               *escaped_scope)
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
 * goa_backend_oauth2_provider_build_authorization_uri:
 * @provider: A #GoaBackendOAuth2Provider.
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
 * the goa_backend_oauth2_provider_get_authorization_uri(), goa_backend_oauth2_provider_get_redirect_uri(), goa_backend_oauth2_provider_get_client_id()
 * and goa_backend_oauth2_provider_get_scope() methods with the latter
 * three escaped using g_uri_escape_string().
 *
 * Returns: (transfer full): An authorization URI that must be freed with g_free().
 */
gchar *
goa_backend_oauth2_provider_build_authorization_uri (GoaBackendOAuth2Provider  *provider,
                                                     const gchar               *authorization_uri,
                                                     const gchar               *escaped_redirect_uri,
                                                     const gchar               *escaped_client_id,
                                                     const gchar               *escaped_scope)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_redirect_uri != NULL, NULL);
  g_return_val_if_fail (escaped_client_id != NULL, NULL);
  g_return_val_if_fail (escaped_scope != NULL, NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->build_authorization_uri (provider,
                                                                                    authorization_uri,
                                                                                    escaped_redirect_uri,
                                                                                    escaped_client_id,
                                                                                    escaped_scope);
}

/**
 * goa_backend_oauth2_provider_get_authorization_uri:
 * @provider: A #GoaBackendOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.1">authorization
 * endpoint</ulink> used for authenticating the user and obtaining
 * authorization.
 *
 * You should not include any parameters in the returned URI. If you
 * need to include additional parameters than the standard ones,
 * override #GoaBackendOAuth2ProviderClass.build_authorization_uri -
 * see goa_backend_oauth2_provider_build_authorization_uri() for more
 * details.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_backend_oauth2_provider_get_authorization_uri (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_authorization_uri (provider);
}

/**
 * goa_backend_oauth2_provider_get_token_uri:
 * @provider: A #GoaBackendOAuth2Provider.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/draft-ietf-oauth-v2-15#section-2.2">token
 * endpoint</ulink> used for obtaining an access token.
 *
 * You should not include any parameters in the returned URI.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_backend_oauth2_provider_get_token_uri (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_token_uri (provider);
}

/**
 * goa_backend_oauth2_provider_get_redirect_uri:
 * @provider: A #GoaBackendOAuth2Provider.
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
goa_backend_oauth2_provider_get_redirect_uri (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_redirect_uri (provider);
}

/**
 * goa_backend_oauth2_provider_get_scope:
 * @provider: A #GoaBackendOAuth2Provider.
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
goa_backend_oauth2_provider_get_scope (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_scope (provider);
}

/**
 * goa_backend_oauth2_provider_get_client_id:
 * @provider: A #GoaBackendOAuth2Provider.
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
goa_backend_oauth2_provider_get_client_id (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_client_id (provider);
}

/**
 * goa_backend_oauth2_provider_get_client_secret:
 * @provider: A #GoaBackendOAuth2Provider.
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
goa_backend_oauth2_provider_get_client_secret (GoaBackendOAuth2Provider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_client_secret (provider);
}

/**
 * goa_backend_oauth2_provider_get_identity:
 * @provider: A #GoaBackendOAuth2Provider.
 * @access_token: A valid OAuth 2.0 access token.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Method that returns the identity corresponding to
 * @access_token. This is a pure virtual method - a subclass must
 * provide an implementation.
 *
 * The identity is needed because all authentication happens out of
 * band. The only requirement is that the returned identity is unique
 * - for example, for #GoaBackendGoogleProvider the returned identity
 * is the email address, for #GoaBackendFacebookProvider it's the user
 * name.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this method was called from. You can then call
 * goa_backend_oauth2_provider_get_identity_finish() to get the result
 * of the operation.
 */
void
goa_backend_oauth2_provider_get_identity (GoaBackendOAuth2Provider *provider,
                                          const gchar              *access_token,
                                          GCancellable             *cancellable,
                                          GAsyncReadyCallback       callback,
                                          gpointer                  user_data)
{
  g_return_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider));
  g_return_if_fail (access_token != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_identity (provider, access_token, cancellable, callback, user_data);
}

/**
 * goa_backend_oauth2_provider_get_identity_finish:
 * @provider: A #GoaBackendOAuth2Provider.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_oauth2_provider_get_access_token().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_oauth2_provider_get_identity().
 *
 * Returns: The identity or %NULL if error is set. The returned string
 * must be freed with g_free().
 */
gchar *
goa_backend_oauth2_provider_get_identity_finish (GoaBackendOAuth2Provider *provider,
                                                 GAsyncResult             *res,
                                                 GError                   **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  return GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS (provider)->get_identity_finish (provider, res, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;
  GSimpleAsyncResult *simple;
  RestProxyCall *call;
  gchar *access_token;
  gchar *refresh_token;
  gint expires_in;
} GetTokensData;

static GetTokensData *
get_tokens_data_ref (GetTokensData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
get_tokens_data_unref (GetTokensData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->simple);
      g_object_unref (data->call);
      g_free (data->access_token);
      g_free (data->refresh_token);
      g_slice_free (GetTokensData, data);
    }
}

static void
get_tokens_on_call_cb (RestProxyCall  *call,
                       const GError   *error,
                       GObject        *weak_object,
                       gpointer        user_data)
{
  GetTokensData *data = user_data;
  guint status_code;
  const gchar *payload;
  gsize payload_length;

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (data->simple, error);
      goto out;
    }

  status_code = rest_proxy_call_get_status_code (call);
  if (status_code != 200)
    {
      g_simple_async_result_set_error (data->simple,
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
      data->access_token = g_strdup (g_hash_table_lookup (hash, "access_token"));
      if (data->access_token == NULL)
        {
          g_simple_async_result_set_error (data->simple,
                                           GOA_ERROR,
                                           GOA_ERROR_FAILED,
                                           _("Didn't find access_token in non-JSON data"));
          g_hash_table_unref (hash);
          goto out;
        }
      /* refresh_token is optional */
      data->refresh_token = g_hash_table_lookup (hash, "refresh_token");
      /* expires_in is optional */
      expires_in_str = g_hash_table_lookup (hash, "expires_in");
      /* sometimes "expires_in" appears as "expires" */
      if (expires_in_str == NULL)
        expires_in_str = g_hash_table_lookup (hash, "expires");
      if (expires_in_str != NULL)
        data->expires_in = atoi (expires_in_str);
      g_hash_table_unref (hash);
    }
  else
    {
      GError *local_error;
      JsonParser *parser;
      JsonObject *object;

      local_error = NULL;
      parser = json_parser_new ();
      if (!json_parser_load_from_data (parser, payload, payload_length, &local_error))
        {
          g_prefix_error (&local_error, _("Error parsing response as JSON: "));
          g_simple_async_result_take_error (data->simple, local_error);
          g_object_unref (parser);
          goto out;
        }
      object = json_node_get_object (json_parser_get_root (parser));
      data->access_token = g_strdup (json_object_get_string_member (object, "access_token"));
      if (data->access_token == NULL)
        {
          g_simple_async_result_set_error (data->simple,
                                           GOA_ERROR,
                                           GOA_ERROR_FAILED,
                                           _("Didn't find access_token in JSON data"));
          goto out;
        }
      /* refresh_token is optional... */
      if (json_object_has_member (object, "refresh_token"))
        data->refresh_token = g_strdup (json_object_get_string_member (object, "refresh_token"));
      if (json_object_has_member (object, "expires_in"))
        data->expires_in = json_object_get_int_member (object, "expires_in");
      g_object_unref (parser);
    }


  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             get_tokens_data_ref (data),
                                             (GDestroyNotify) get_tokens_data_unref);

 out:
  g_simple_async_result_complete (data->simple);
  get_tokens_data_unref (data);
}

static void
get_tokens (GoaBackendOAuth2Provider *provider,
            const gchar              *authorization_code,
            const gchar              *refresh_token,
            GCancellable             *cancellable,
            GAsyncReadyCallback       callback,
            gpointer                  user_data)
{
  GetTokensData *data;
  RestProxy *proxy;
  GError *error;

  g_return_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider));
  g_return_if_fail (authorization_code != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (GetTokensData);
  data->ref_count = 1;
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            get_tokens);

  proxy = rest_proxy_new (goa_backend_oauth2_provider_get_token_uri (provider), FALSE);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "POST");
  rest_proxy_call_add_header (data->call, "Content-Type", "application/x-www-form-urlencoded");
  if (refresh_token != NULL)
    {
      /* Swell, we have a refresh code - just use that */
      rest_proxy_call_add_param (data->call, "client_id", goa_backend_oauth2_provider_get_client_id (provider));
      rest_proxy_call_add_param (data->call, "client_secret", goa_backend_oauth2_provider_get_client_secret (provider));
      rest_proxy_call_add_param (data->call, "grant_type", "refresh_token");
      rest_proxy_call_add_param (data->call, "refresh_token", refresh_token);
    }
  else
    {
      /* No refresh code.. request an access token using the authorization code instead */
      rest_proxy_call_add_param (data->call, "client_id", goa_backend_oauth2_provider_get_client_id (provider));
      rest_proxy_call_add_param (data->call, "client_secret", goa_backend_oauth2_provider_get_client_secret (provider));
      rest_proxy_call_add_param (data->call, "grant_type", "authorization_code");
      rest_proxy_call_add_param (data->call, "code", authorization_code);
      rest_proxy_call_add_param (data->call, "redirect_uri", goa_backend_oauth2_provider_get_redirect_uri (provider));
    }

  error = NULL;
  if (!rest_proxy_call_async (data->call,
                              get_tokens_on_call_cb,
                              NULL, /* TODO: weak_object for GCancellable */
                              data,
                              &error))
    {
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete_in_idle (data->simple);
      get_tokens_data_unref (data);
    }

  g_object_unref (proxy);
}

static gchar *
get_tokens_finish (GoaBackendOAuth2Provider   *provider,
                   gchar                     **out_refresh_token,
                   gint                       *out_expires_in,
                   GAsyncResult               *res,
                   GError                    **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetTokensData *data;
  gchar *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (provider), get_tokens), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  data = g_simple_async_result_get_op_res_gpointer (simple);
  if (out_refresh_token != NULL)
    {
      *out_refresh_token = data->refresh_token;
      data->refresh_token = NULL;
    }
  if (out_expires_in != NULL)
    {
      *out_expires_in = data->expires_in;
    }

  ret = data->access_token;
  data->access_token = NULL;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaBackendOAuth2Provider *provider;
  GtkDialog *dialog;
  gchar *authorization_code;
  GError *error;
  GMainLoop *loop;

  gchar *access_token;
  gchar *refresh_token;
  gint expires_in;

  gchar *identity;
} IdentifyData;

G_GNUC_UNUSED static void
print_header (const gchar *name,
              const gchar *value,
              gpointer     user_data)
{
  g_print ("header: `%s' -> `%s'\n", name, value);
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
  //g_debug ("requested_uri is %s", requested_uri);
  redirect_uri = goa_backend_oauth2_provider_get_redirect_uri (data->provider);
  if (g_str_has_prefix (requested_uri, redirect_uri))
    {
      SoupMessage *message;
      SoupURI *uri;
      GHashTable *key_value_pairs;

      message = webkit_network_request_get_message (request);
      uri = soup_message_get_uri (message);
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
      return TRUE; /* ignore the request */
    }
  else
    {
      return FALSE; /* make default behavior apply */
    }
}

static void
get_tokens_and_identity_on_get_tokens_cb (GoaBackendOAuth2Provider *provider,
                                          GAsyncResult             *res,
                                          gpointer                  user_data)
{
  IdentifyData *data = user_data;
  data->access_token = get_tokens_finish (provider,
                                          &data->refresh_token,
                                          &data->expires_in,
                                          res,
                                          &data->error);
  g_main_loop_quit (data->loop);
}

static void
get_tokens_and_identity_on_get_identity_cb (GoaBackendOAuth2Provider *provider,
                                            GAsyncResult             *res,
                                            gpointer                  user_data)
{
  IdentifyData *data = user_data;
  data->identity = goa_backend_oauth2_provider_get_identity_finish (provider,
                                                                    res,
                                                                    &data->error);
  g_main_loop_quit (data->loop);
}

static gboolean
get_tokens_and_identity (GoaBackendOAuth2Provider *provider,
                         GtkDialog                *dialog,
                         GtkBox                   *vbox,
                         gchar                   **out_authorization_code,
                         gchar                   **out_access_token,
                         gchar                   **out_refresh_token,
                         gchar                   **out_identity,
                         GError                  **error)
{
  gboolean ret;
  SoupSession *webkit_soup_session;
  SoupCookieJar *cookie_jar;
  GtkWidget *scrolled_window;
  GtkWidget *web_view;
  gchar *url;
  IdentifyData data;
  gchar *escaped_redirect_uri;
  gchar *escaped_client_id;
  gchar *escaped_scope;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), FALSE);
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
  data.loop = g_main_loop_new (NULL, FALSE);

  /* TODO: use oauth2_proxy_build_login_url_full() */
  escaped_redirect_uri = g_uri_escape_string (goa_backend_oauth2_provider_get_redirect_uri (provider), NULL, TRUE);
  escaped_client_id = g_uri_escape_string (goa_backend_oauth2_provider_get_client_id (provider), NULL, TRUE);
  escaped_scope = g_uri_escape_string (goa_backend_oauth2_provider_get_scope (provider), NULL, TRUE);
  url = goa_backend_oauth2_provider_build_authorization_uri (provider,
                                                              goa_backend_oauth2_provider_get_authorization_uri (provider),
                                                              escaped_redirect_uri,
                                                              escaped_client_id,
                                                              escaped_scope);
  //g_debug ("url = %s", url);

  /* Ensure we use an empty non-persistent cookie to avoid login
   * credentials being reused...
   */
  webkit_soup_session = webkit_get_default_session ();
  soup_session_remove_feature_by_type (webkit_soup_session, SOUP_TYPE_COOKIE_JAR);
  cookie_jar = soup_cookie_jar_new ();
  soup_session_add_feature (webkit_soup_session, SOUP_SESSION_FEATURE (cookie_jar));

  /* TODO: we might need to add some more web browser UI to make this
   * work...
   */
  web_view = webkit_web_view_new ();
  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), url);
  g_signal_connect (web_view,
                    "navigation-policy-decision-requested",
                    G_CALLBACK (on_web_view_navigation_policy_decision_requested),
                    &data);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (scrolled_window, 500, 400);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_window), web_view);
  gtk_container_add (GTK_CONTAINER (vbox), scrolled_window);
  gtk_widget_show_all (scrolled_window);
  data.dialog = dialog;
  gtk_dialog_run (GTK_DIALOG (dialog));
  if (data.authorization_code == NULL)
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

  /* OK, we now have the authorization code... now we need to get the
   * email address (to e.g. check if the account already exists on
   * @client).. for that we need to get a (short-lived) access token
   * and a refresh_token
   */
  get_tokens (provider,
              data.authorization_code,
              NULL, /* refresh_token */
              NULL, /* GCancellable */
              (GAsyncReadyCallback) get_tokens_and_identity_on_get_tokens_cb,
              &data);
  g_main_loop_run (data.loop);
  if (data.access_token == NULL)
    goto out;

  goa_backend_oauth2_provider_get_identity (provider,
                                            data.access_token,
                                            NULL, /* TODO: GCancellable */
                                            (GAsyncReadyCallback) get_tokens_and_identity_on_get_identity_cb,
                                            &data);
  g_main_loop_run (data.loop);
  if (data.identity == NULL)
    goto out;

  ret = TRUE;

 out:
  if (ret)
    {
      g_warn_if_fail (data.error == NULL);
      if (out_authorization_code != NULL)
        *out_authorization_code = g_strdup (data.authorization_code);
      if (out_access_token != NULL)
        *out_access_token = g_strdup (data.access_token);
      if (out_refresh_token != NULL)
        *out_refresh_token = g_strdup (data.refresh_token);
      if (out_identity != NULL)
        *out_identity = g_strdup (data.identity);
    }
  else
    {
      g_warn_if_fail (data.error != NULL);
      g_propagate_error (error, data.error);
    }

  g_free (data.identity);
  g_object_unref (cookie_jar);
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

static void
store_credentials_cb (GoaBackendProvider   *provider,
                      GAsyncResult         *res,
                      gpointer              user_data)
{
  AddData *data = user_data;
  goa_backend_provider_store_credentials_finish (provider,
                                                 res,
                                                 &data->error);
  g_main_loop_quit (data->loop);
}

static GoaObject *
goa_backend_oauth2_provider_add_account (GoaBackendProvider *_provider,
                                         GoaClient          *client,
                                         GtkDialog          *dialog,
                                         GtkBox             *vbox,
                                         GError            **error)
{
  GoaBackendOAuth2Provider *provider = GOA_BACKEND_OAUTH2_PROVIDER (_provider);
  GoaObject *ret;
  gchar *authorization_code;
  gchar *access_token;
  gchar *refresh_token;
  gchar *identity;
  GList *accounts;
  GList *l;
  AddData data;
  GHashTable *credentials;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  authorization_code = NULL;
  access_token = NULL;
  refresh_token = NULL;
  identity = NULL;
  accounts = NULL;
  credentials = NULL;

  memset (&data, '\0', sizeof (AddData));
  data.loop = g_main_loop_new (NULL, FALSE);

  if (!get_tokens_and_identity (provider,
                                dialog,
                                vbox,
                                &authorization_code,
                                &access_token,
                                &refresh_token,
                                &identity,
                                &data.error))
    goto out;

  /* OK, got the email address... see if there's already an account
   * with this email address
   */
  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      const gchar *identity_from_object;

      identity_from_object = goa_oauth2_based_get_identity (goa_object_peek_oauth2_based (object));
      if (g_strcmp0 (identity_from_object, identity) == 0)
        {
          g_set_error (&data.error,
                       GOA_ERROR,
                       GOA_ERROR_ACCOUNT_EXISTS,
                       _("There is already an account for the identity %s"),
                       identity);
          goto out;
        }
    }

  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_backend_provider_get_provider_type (GOA_BACKEND_PROVIDER (provider)),
                                identity, /* Name */
                                g_variant_new_parsed ("{'Identity': %s}",
                                                      identity),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  credentials = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (credentials, "authorization_code", authorization_code);
  if (refresh_token != NULL)
    g_hash_table_insert (credentials, "refresh_token", refresh_token);
  goa_backend_provider_store_credentials (GOA_BACKEND_PROVIDER (provider),
                                          identity,
                                          credentials,
                                          NULL, /* GCancellable */
                                          (GAsyncReadyCallback) store_credentials_cb,
                                          &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      g_assert (ret == NULL);
    }
  else
    {
      g_assert (ret != NULL);
    }

  if (credentials != NULL)
    g_hash_table_unref (credentials);
  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
  g_free (identity);
  g_free (authorization_code);
  g_free (access_token);
  g_free (refresh_token);
  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  GError *error;
} RefreshAccountData;

static void
refresh_credentials_store_credentials_cb (GoaBackendProvider   *provider,
                                          GAsyncResult         *res,
                                          gpointer              user_data)
{
  RefreshAccountData *data = user_data;
  goa_backend_provider_store_credentials_finish (provider,
                                                 res,
                                                 &data->error);
  g_main_loop_quit (data->loop);
}

static gboolean
goa_backend_oauth2_provider_refresh_account (GoaBackendProvider  *_provider,
                                             GoaClient           *client,
                                             GoaObject           *object,
                                             GtkWindow           *parent,
                                             GError             **error)
{
  GoaBackendOAuth2Provider *provider = GOA_BACKEND_OAUTH2_PROVIDER (_provider);
  GtkWidget *dialog;
  gchar *authorization_code;
  gchar *access_token;
  gchar *refresh_token;
  gchar *identity;
  const gchar *existing_identity;
  GHashTable *credentials;
  gboolean ret;
  RefreshAccountData data;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  authorization_code = NULL;
  access_token = NULL;
  refresh_token = NULL;
  identity = NULL;
  credentials = NULL;

  memset (&data, '\0', sizeof (RefreshAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                        NULL);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_widget_show_all (dialog);

  if (!get_tokens_and_identity (provider,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                &authorization_code,
                                &access_token,
                                &refresh_token,
                                &identity,
                                error))
    goto out;

  existing_identity = goa_oauth2_based_get_identity (goa_object_peek_oauth2_based (object));
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

  credentials = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (credentials, "authorization_code", authorization_code);
  if (refresh_token != NULL)
    g_hash_table_insert (credentials, "refresh_token", refresh_token);
  goa_backend_provider_store_credentials (GOA_BACKEND_PROVIDER (provider),
                                          identity,
                                          credentials,
                                          NULL, /* GCancellable */
                                          (GAsyncReadyCallback) refresh_credentials_store_credentials_cb,
                                          &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      goto out;
    }

  goa_account_call_clear_attention_needed (goa_object_peek_account (object),
                                           NULL, /* GCancellable */
                                           NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  gtk_widget_destroy (dialog);

  if (credentials != NULL)
    g_hash_table_unref (credentials);
  g_free (identity);
  g_free (authorization_code);
  g_free (access_token);
  g_free (refresh_token);
  g_main_loop_unref (data.loop);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  GoaBackendOAuth2Provider *provider;
  GoaObject *object;
  GSimpleAsyncResult *simple;
  gchar *key;

  gchar *access_token;
  gint expires_in;
} GetAccessTokenData;

static void
get_access_token_data_unref (GetAccessTokenData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->object);
      g_object_unref (data->simple);
      g_free (data->access_token);
      g_free (data->key);
      g_slice_free (GetAccessTokenData, data);
    }
}

static GetAccessTokenData *
get_access_token_data_ref (GetAccessTokenData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
get_access_token_on_get_tokens_cb (GoaBackendOAuth2Provider  *provider,
                                   GAsyncResult              *res,
                                   gpointer                   user_data)
{
  GetAccessTokenData *data = user_data;
  GError *error;

  error = NULL;
  data->access_token = get_tokens_finish (provider,
                                          NULL, /* out_refresh_token */
                                          &data->expires_in,
                                          res,
                                          &error);
  if (data->access_token == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_AUTHORIZED,
                                       _("Failed to get tokens: %s (%s, %d)"),
                                       error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (data->simple,
                                                 get_access_token_data_ref (data),
                                                 (GDestroyNotify) get_access_token_data_unref);
    }
  g_simple_async_result_complete_in_idle (data->simple);
  get_access_token_data_unref (data);
}

static void
lookup_credentials_cb (GoaBackendProvider *provider,
                       GAsyncResult       *res,
                       gpointer            user_data)
{
  GetAccessTokenData *data = user_data;
  GHashTable *credentials;
  const gchar *authorization_code;
  const gchar *refresh_token;
  GError *error;

  error = NULL;
  credentials = goa_backend_provider_lookup_credentials_finish (provider, res, &error);
  if (credentials == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_AUTHORIZED, /* force NeedsAttention to TRUE */
                                       _("Credentials not found in keyring: %s"),
                                       error->message);
      g_error_free (error);
      g_simple_async_result_complete_in_idle (data->simple);
      get_access_token_data_unref (data);
      goto out;
    }

  authorization_code = g_hash_table_lookup (credentials, "authorization_code");
  refresh_token = g_hash_table_lookup (credentials, "refresh_token");

  if (authorization_code == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_AUTHORIZED, /* force NeedsAttention to TRUE */
                                       _("Credentials does not contain authorization_code"));
      g_simple_async_result_complete_in_idle (data->simple);
      get_access_token_data_unref (data);
      goto out;
    }

  get_tokens (data->provider,
              authorization_code,
              refresh_token,
              NULL, /* GCancellable */
              (GAsyncReadyCallback) get_access_token_on_get_tokens_cb,
              data);

 out:
  g_hash_table_unref (credentials);
}

/**
 * goa_backend_oauth2_provider_get_access_token:
 * @provider: A #GoaBackendOAuth2Provider.
 * @object: A #GoaObject.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Gets an access token for @object. The token is obtained using
 * either the <emphasis>authorization code</emphasis> or
 * <emphasis>refresh token</emphasis> obtained when the service was
 * authorized. In either case, the network is involved so the request
 * will take a long time.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this method was called from. You can then call
 * goa_backend_oauth2_provider_get_access_token_finish() to get the
 * result of the operation.
 */
void
goa_backend_oauth2_provider_get_access_token (GoaBackendOAuth2Provider   *provider,
                                              GoaObject                  *object,
                                              GCancellable               *cancellable,
                                              GAsyncReadyCallback         callback,
                                              gpointer                    user_data)
{
  GetAccessTokenData *data;
  const gchar *identity;

  g_return_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (GetAccessTokenData);
  data->ref_count = 1;
  data->provider = provider;
  data->object = g_object_ref (object);
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            goa_backend_oauth2_provider_get_access_token);

  identity = goa_oauth2_based_get_identity (goa_object_peek_oauth2_based (object));

  /* First, get the authorization code from the keyring */
  goa_backend_provider_lookup_credentials (GOA_BACKEND_PROVIDER (provider),
                                           identity,
                                           cancellable,
                                           (GAsyncReadyCallback) lookup_credentials_cb,
                                           data);
}

/**
 * goa_backend_oauth2_provider_get_access_token_finish:
 * @provider: A #GoaBackendOAuth2Provider.
 * @out_expires_in: (out): Return location for how many seconds the returned token is valid (0 if unknown) for or %NULL.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_oauth2_provider_get_access_token().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_oauth2_provider_get_access_token().
 *
 * Returns: The access token or %NULL if error is set. The returned string must be freed with g_free().
 */
gchar *
goa_backend_oauth2_provider_get_access_token_finish (GoaBackendOAuth2Provider   *provider,
                                                     gint                       *out_expires_in,
                                                     GAsyncResult               *res,
                                                     GError                    **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetAccessTokenData *data;
  gchar *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH2_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (provider),
                                                        goa_backend_oauth2_provider_get_access_token), NULL);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  ret = g_strdup (data->access_token);
  if (out_expires_in != NULL)
    *out_expires_in = data->expires_in;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_access_token (GoaOAuth2Based        *object,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data);

static gboolean
goa_backend_oauth2_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaOAuth2Based *oauth2_based;
  gchar *identity;

  identity = NULL;

  oauth2_based = goa_object_get_oauth2_based (GOA_OBJECT (object));
  if (oauth2_based != NULL)
    goto out;

  oauth2_based = goa_oauth2_based_skeleton_new ();
  goa_object_skeleton_set_oauth2_based (object, oauth2_based);
  g_signal_connect (oauth2_based,
                    "handle-get-access-token",
                    G_CALLBACK (on_handle_get_access_token),
                    NULL);

  identity = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (identity == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "No Identity key");
      goto out;
    }
  goa_oauth2_based_set_identity (oauth2_based, identity);

 out:
  g_object_unref (oauth2_based);
  g_free (identity);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_oauth2_provider_init (GoaBackendOAuth2Provider *client)
{
}

static void
goa_backend_oauth2_provider_class_init (GoaBackendOAuth2ProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->add_account                = goa_backend_oauth2_provider_add_account;
  provider_class->refresh_account            = goa_backend_oauth2_provider_refresh_account;
  provider_class->build_object               = goa_backend_oauth2_provider_build_object;

  klass->build_authorization_uri = goa_backend_oauth2_provider_build_authorization_uri_default;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaObject *object;
  GDBusMethodInvocation *invocation;
} AccessTokenData;

static void
access_token_data_free (AccessTokenData *data)
{
  g_object_unref (data->object);
  g_free (data);
}

static void
get_access_token_cb (GoaBackendOAuth2Provider  *provider,
                     GAsyncResult              *res,
                     gpointer                   user_data)
{
  AccessTokenData *data = user_data;
  GError *error;
  gchar *access_token;
  gint expires_in;

  error = NULL;
  access_token = goa_backend_oauth2_provider_get_access_token_finish (provider, &expires_in, res, &error);
  if (access_token == NULL)
    {
      if (error->domain == GOA_ERROR && error->code == GOA_ERROR_NOT_AUTHORIZED)
        {
          GoaAccount *account;
          account = goa_object_peek_account (data->object);
          if (!goa_account_get_attention_needed (account))
            {
              goa_account_set_attention_needed (account, TRUE);
              g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
              /* TODO: syslog */
              g_print ("Setting AttentionNeeded to TRUE because GetAccessToken() failed for %s with: %s (%s, %d)\n",
                       g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)),
                       error->message, g_quark_to_string (error->domain), error->code);
            }
        }
      g_dbus_method_invocation_return_gerror (data->invocation, error);
      g_error_free (error);
    }
  else
    {
      GoaAccount *account;
      account = goa_object_peek_account (data->object);

      /* clear AttentionNeeded flag if set */
      if (goa_account_get_attention_needed (account))
        {
          goa_account_set_attention_needed (account, FALSE);
          g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
        }
      goa_oauth2_based_complete_get_access_token (goa_object_peek_oauth2_based (data->object),
                                                  data->invocation,
                                                  access_token,
                                                  expires_in);
      g_free (access_token);
    }
  access_token_data_free (data);
}

static gboolean
on_handle_get_access_token (GoaOAuth2Based        *interface,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaBackendProvider *provider;
  AccessTokenData *data;

  /* TODO: maybe log what app is requesting access */

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  provider = goa_backend_provider_get_for_provider_type (goa_account_get_account_type (account));

  data = g_new0 (AccessTokenData, 1);
  data->object = g_object_ref (object);
  data->invocation = invocation;
  goa_backend_oauth2_provider_get_access_token (GOA_BACKEND_OAUTH2_PROVIDER (provider),
                                                object,
                                                NULL, /* GCancellable* */
                                                (GAsyncReadyCallback) get_access_token_cb,
                                                data);
  g_object_unref (provider);

  return TRUE; /* invocation was handled */
}
