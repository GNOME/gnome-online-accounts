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

#include <rest/oauth-proxy.h>
#include <webkit/webkit.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauthprovider.h"

/**
 * SECTION:goabackendoauthprovider
 * @title: GoaBackendOAuthProvider
 * @short_description: Abstract base class for OAuth 1.0a providers
 *
 * #GoaBackendOAuthProvider is an abstract base class for OAuth 1.0a
 * compliant implementations as defined by <ulink
 * url="http://tools.ietf.org/html/rfc5849">RFC
 * 5849</ulink>. Additionally, the code works with providers
 * implementing <ulink
 * url="http://oauth.googlecode.com/svn/spec/ext/session/1.0/drafts/1/spec.html">OAuth
 * Session 1.0 Draft 1</ulink> for refreshing access tokens.
 *
 * Subclasses must implement
 * #GoaBackendOAuthProviderClass.get_consumer_key,
 * #GoaBackendOAuthProviderClass.get_consumer_secret,
 * #GoaBackendOAuthProviderClass.get_request_uri,
 * #GoaBackendOAuthProviderClass.get_authorization_uri,
 * #GoaBackendOAuthProviderClass.get_token_uri,
 * #GoaBackendOAuthProviderClass.get_callback_uri,
 * #GoaBackendOAuthProviderClass.get_identity and
 * #GoaBackendOAuthProviderClass.get_identity_finish methods.
 *
 * Additionally, the
 * #GoaBackendProviderClass.get_provider_type,
 * #GoaBackendProviderClass.get_name,
 * #GoaBackendProviderClass.build_object (this should chain up to its
 * parent class) methods must be implemented.
 *
 * Note that the #GoaBackendProviderClass.add_account,
 * #GoaBackendProviderClass.refresh_account,
 * #GoaBackendProviderClass.ensure_credentials and
 * #GoaBackendProviderClass.ensure_credentials_finish methods do not
 * need to be implemented - this type implements these methods.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendOAuthProvider, goa_backend_oauth_provider, GOA_TYPE_BACKEND_PROVIDER);

G_LOCK_DEFINE_STATIC (queue_lock);

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

static void
_print_response (RestProxyCall *call)
{
  GHashTable *headers;
  GHashTable *form;
  GHashTableIter iter;
  const gchar *key;
  const gchar *value;
  const gchar *payload;

  headers = rest_proxy_call_get_response_headers (call);
  if (headers != NULL)
    {
      g_hash_table_iter_init (&iter, headers);
      while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &value))
        g_print ("Header %s: %s\n", key, value);
      g_hash_table_unref (headers);
    }

  payload = rest_proxy_call_get_payload (call);
  if (payload != NULL)
    {
      form = soup_form_decode (payload);
      if (form != NULL)
        {
          g_hash_table_iter_init (&iter, form);
          while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &value))
            g_print ("Form   %s: %s\n", key, value);
          g_hash_table_unref (form);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_oauth_provider_get_use_external_browser_default (GoaBackendOAuthProvider  *provider)
{
  return FALSE;
}

/**
 * goa_backend_oauth_provider_get_use_external_browser:
 * @provider: A #GoaBackendOAuthProvider.
 *
 * Returns whether an external browser (the users default browser)
 * should be used for user interaction.
 *
 * If an external browser is used, then the dialogs presented in
 * goa_backend_provider_add_account() and
 * goa_backend_provider_refresh_account() will show a simple "Paste
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
goa_backend_oauth_provider_get_use_external_browser (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), FALSE);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_use_external_browser (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
goa_backend_oauth_provider_build_authorization_uri_default (GoaBackendOAuthProvider  *provider,
                                                            const gchar              *authorization_uri,
                                                            const gchar              *escaped_oauth_token)
{
  return g_strdup_printf ("%s"
                          "?oauth_token=%s",
                          authorization_uri,
                          escaped_oauth_token);
}

/**
 * goa_backend_oauth_provider_build_authorization_uri:
 * @provider: A #GoaBackendOAuthProvider.
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
 * the goa_backend_oauth_provider_get_authorization_uri() method. The
 * @escaped_oauth_token parameter is the temporary credentials identifier
 * escaped using g_uri_escape_string().
 *
 * Returns: (transfer full): An authorization URI that must be freed with g_free().
 */
gchar *
goa_backend_oauth_provider_build_authorization_uri (GoaBackendOAuthProvider  *provider,
                                                    const gchar               *authorization_uri,
                                                    const gchar               *escaped_oauth_token)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (authorization_uri != NULL, NULL);
  g_return_val_if_fail (escaped_oauth_token != NULL, NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->build_authorization_uri (provider,
                                                                                   authorization_uri,
                                                                                   escaped_oauth_token);
}

/**
 * goa_backend_oauth_provider_get_consumer_key:
 * @provider: A #GoaBackendOAuthProvider.
 *
 * Gets the consumer key identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_backend_oauth_provider_get_consumer_key (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_consumer_key (provider);
}

/**
 * goa_backend_oauth_provider_get_consumer_secret:
 * @provider: A #GoaBackendOAuthProvider.
 *
 * Gets the consumer secret identifying the client.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider - do not free.
 */
const gchar *
goa_backend_oauth_provider_get_consumer_secret (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_consumer_secret (provider);
}

/**
 * goa_backend_oauth_provider_get_request_uri:
 * @provider: A #GoaBackendOAuthProvider.
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
goa_backend_oauth_provider_get_request_uri (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_request_uri (provider);
}

/**
 * goa_backend_oauth_provider_get_request_uri_params:
 * @provider: A #GoaBackendOAuthProvider.
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
goa_backend_oauth_provider_get_request_uri_params (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_request_uri_params (provider);
}

static gchar **
goa_backend_oauth_provider_get_request_uri_params_default (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return NULL;
}

/**
 * goa_backend_oauth_provider_get_authorization_uri:
 * @provider: A #GoaBackendOAuthProvider.
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
goa_backend_oauth_provider_get_authorization_uri (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_authorization_uri (provider);
}

/**
 * goa_backend_oauth_provider_get_token_uri:
 * @provider: A #GoaBackendOAuthProvider.
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
goa_backend_oauth_provider_get_token_uri (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_token_uri (provider);
}

/**
 * goa_backend_oauth_provider_get_callback_uri:
 * @provider: A #GoaBackendOAuthProvider.
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
goa_backend_oauth_provider_get_callback_uri (GoaBackendOAuthProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_callback_uri (provider);
}

/**
 * goa_backend_oauth_provider_get_identity:
 * @provider: A #GoaBackendOAuthProvider.
 * @access_token: A valid OAuth 1.0 access token.
 * @access_token_secret: The valid secret for @access_token.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Method that returns the identity corresponding to @access_token and
 * @access_token_secret. This is a pure virtual method - a subclass
 * must provide an implementation.
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
 * goa_backend_oauth_provider_get_identity_finish() to get the result
 * of the operation.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 */
void
goa_backend_oauth_provider_get_identity (GoaBackendOAuthProvider *provider,
                                         const gchar              *access_token,
                                         const gchar              *access_token_secret,
                                         GCancellable             *cancellable,
                                         GAsyncReadyCallback       callback,
                                         gpointer                  user_data)
{
  g_return_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider));
  g_return_if_fail (access_token != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_identity (provider, access_token, access_token_secret, cancellable, callback, user_data);
}

/**
 * goa_backend_oauth_provider_get_identity_finish:
 * @provider: A #GoaBackendOAuthProvider.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_oauth_provider_get_identity().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_oauth_provider_get_identity().
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: The identity or %NULL if error is set. The returned string
 * must be freed with g_free().
 */
gchar *
goa_backend_oauth_provider_get_identity_finish (GoaBackendOAuthProvider *provider,
                                                 GAsyncResult             *res,
                                                 GError                   **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  return GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS (provider)->get_identity_finish (provider, res, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;
  GSimpleAsyncResult *simple;
  RestProxyCall *call;
  gchar *access_token;
  gchar *access_token_secret;
  gchar *session_handle;
  gint access_token_expires_in;
  gint session_handle_expires_in;
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
      g_object_unref (data->call);
      g_free (data->access_token);
      g_free (data->access_token_secret);
      g_free (data->session_handle);
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
  GHashTable *f;
  const gchar *expires_in_str;

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

  f = soup_form_decode (rest_proxy_call_get_payload (call));
  data->access_token = g_strdup (g_hash_table_lookup (f, "oauth_token"));
  data->access_token_secret = g_strdup (g_hash_table_lookup (f, "oauth_token_secret"));
  data->session_handle = g_strdup (g_hash_table_lookup (f, "oauth_session_handle"));
  expires_in_str = g_hash_table_lookup (f, "oauth_expires_in");
  if (expires_in_str != NULL)
    data->access_token_expires_in = atoi (expires_in_str);
  expires_in_str = g_hash_table_lookup (f, "oauth_authorization_expires_in");
  if (expires_in_str != NULL)
    data->session_handle_expires_in = atoi (expires_in_str);
  g_hash_table_unref (f);

  if (data->access_token == NULL || data->access_token_secret == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Missing access_token or access_token_secret headers in response"));
      goto out;
    }

  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             get_tokens_data_ref (data),
                                             (GDestroyNotify) get_tokens_data_unref);

 out:
  g_simple_async_result_complete (data->simple);
  g_object_unref (data->simple);
  get_tokens_data_unref (data);
}

static void
get_tokens (GoaBackendOAuthProvider *provider,
            const gchar              *token,
            const gchar              *token_secret,
            const gchar              *session_handle, /* may be NULL */
            const gchar              *verifier, /* may be NULL */
            GCancellable             *cancellable,
            GAsyncReadyCallback       callback,
            gpointer                  user_data)
{
  GetTokensData *data;
  RestProxy *proxy;
  GError *error;

  g_return_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (GetTokensData);
  data->ref_count = 1;
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            get_tokens);

  proxy = oauth_proxy_new (goa_backend_oauth_provider_get_consumer_key (provider),
                           goa_backend_oauth_provider_get_consumer_secret (provider),
                           goa_backend_oauth_provider_get_token_uri (provider),
                           FALSE);
  oauth_proxy_set_token (OAUTH_PROXY (proxy), token);
  oauth_proxy_set_token_secret (OAUTH_PROXY (proxy), token_secret);
  data->call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (data->call, "POST");
  if (verifier != NULL)
    rest_proxy_call_add_param (data->call, "oauth_verifier", verifier);
  if (session_handle != NULL)
    rest_proxy_call_add_param (data->call, "oauth_session_handle", session_handle);
  error = NULL;
  if (!rest_proxy_call_async (data->call,
                              get_tokens_on_call_cb,
                              NULL, /* TODO: weak_object for GCancellable */
                              data,
                              &error))
    {
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      get_tokens_data_unref (data);
    }
  g_object_unref (proxy);
}

static gchar *
get_tokens_finish (GoaBackendOAuthProvider   *provider,
                   gchar                     **out_access_token_secret,
                   gint                       *out_access_token_expires_in,
                   gchar                     **out_session_handle,
                   gint                       *out_session_handle_expires_in,
                   GAsyncResult               *res,
                   GError                    **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetTokensData *data;
  gchar *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (provider), get_tokens), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  data = g_simple_async_result_get_op_res_gpointer (simple);
  if (out_access_token_secret != NULL)
    {
      *out_access_token_secret = data->access_token_secret;
      data->access_token_secret = NULL;
    }
  if (out_access_token_expires_in != NULL)
    {
      *out_access_token_expires_in = data->access_token_expires_in;
    }
  if (out_session_handle != NULL)
    {
      *out_session_handle = data->session_handle;
      data->session_handle = NULL;
    }
  if (out_session_handle_expires_in != NULL)
    {
      *out_session_handle_expires_in = data->session_handle_expires_in;
    }

  ret = data->access_token;
  data->access_token = NULL;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaBackendOAuthProvider *provider;
  GtkDialog *dialog;
  GError *error;
  GMainLoop *loop;

  gchar *oauth_verifier;

  gchar *identity;

  gchar *request_token;
  gchar *request_token_secret;
  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;
  gchar *session_handle;
  gint session_handle_expires_in;
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

  /* TODO: use oauth_proxy_extract_access_token() */

  requested_uri = webkit_network_request_get_uri (request);
  redirect_uri = goa_backend_oauth_provider_get_callback_uri (data->provider);
  if (g_str_has_prefix (requested_uri, redirect_uri))
    {
      SoupMessage *message;
      SoupURI *uri;
      GHashTable *key_value_pairs;

      message = webkit_network_request_get_message (request);
      uri = soup_message_get_uri (message);
      key_value_pairs = soup_form_decode (uri->query);

      /* TODO: error handling? */
      data->oauth_verifier = g_strdup (g_hash_table_lookup (key_value_pairs, "oauth_verifier"));
      if (data->oauth_verifier != NULL)
        {
          gtk_dialog_response (data->dialog, GTK_RESPONSE_OK);
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
on_entry_changed (GtkEditable *editable,
                  gpointer     user_data)
{
  IdentifyData *data = user_data;
  gboolean sensitive;

  g_free (data->oauth_verifier);
  data->oauth_verifier = g_strdup (gtk_entry_get_text (GTK_ENTRY (editable)));
  sensitive = data->oauth_verifier != NULL && (strlen (data->oauth_verifier) > 0);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, sensitive);
}

static void
get_tokens_and_identity_on_get_identity_cb (GoaBackendOAuthProvider *provider,
                                            GAsyncResult             *res,
                                            gpointer                  user_data)
{
  IdentifyData *data = user_data;
  data->identity = goa_backend_oauth_provider_get_identity_finish (provider,
                                                                   res,
                                                                   &data->error);
  if (&data->error != NULL)
    g_prefix_error (&data->error, _("Error getting identity: "));
  g_main_loop_quit (data->loop);
}

static void
get_tokens_and_identity_on_get_tokens_cb (GoaBackendOAuthProvider *provider,
                                          GAsyncResult            *res,
                                          gpointer                 user_data)
{
  IdentifyData *data = user_data;

  data->access_token = get_tokens_finish (provider,
                                          &data->access_token_secret,
                                          &data->access_token_expires_in,
                                          &data->session_handle,
                                          &data->session_handle_expires_in,
                                          res,
                                          &data->error);
  if (&data->error != NULL)
    g_prefix_error (&data->error, _("Error getting an Access Token: "));
  g_main_loop_quit (data->loop);
}

static gboolean
get_tokens_and_identity (GoaBackendOAuthProvider *provider,
                         GtkDialog                *dialog,
                         GtkBox                   *vbox,
                         gchar                   **out_access_token,
                         gchar                   **out_access_token_secret,
                         gint                     *out_access_token_expires_in,
                         gchar                   **out_session_handle,
                         gint                     *out_session_handle_expires_in,
                         gchar                   **out_identity,
                         GError                  **error)
{
  gboolean ret;
  gchar *url;
  IdentifyData data;
  gchar *escaped_request_token;
  RestProxy *proxy;
  RestProxyCall *call;
  GHashTable *f;
  gboolean use_external_browser;
  gchar **request_params;
  guint n;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), FALSE);
  g_return_val_if_fail (GTK_IS_BOX (vbox), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;
  escaped_request_token = NULL;
  proxy = NULL;
  call = NULL;
  url = NULL;
  request_params = NULL;

  use_external_browser = goa_backend_oauth_provider_get_use_external_browser (provider);

  /* TODO: check with NM whether we're online, if not - return error */

  memset (&data, '\0', sizeof (IdentifyData));
  data.provider = provider;
  data.loop = g_main_loop_new (NULL, FALSE);

  proxy = oauth_proxy_new (goa_backend_oauth_provider_get_consumer_key (provider),
                           goa_backend_oauth_provider_get_consumer_secret (provider),
                           goa_backend_oauth_provider_get_request_uri (provider), FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "POST");
  if (use_external_browser)
    rest_proxy_call_add_param (call, "oauth_callback", "oob");
  else
    rest_proxy_call_add_param (call, "oauth_callback", goa_backend_oauth_provider_get_callback_uri (provider));

  request_params = goa_backend_oauth_provider_get_request_uri_params (provider);
  if (request_params != NULL)
    {
      g_assert (g_strv_length (request_params) % 2 == 0);
      for (n = 0; request_params[n] != NULL; n += 2)
        rest_proxy_call_add_param (call, request_params[n], request_params[n+1]);
    }

  if (!rest_proxy_call_sync (call, &data.error))
    {
      g_prefix_error (&data.error, _("Error getting a Request Token: "));
      goto out;
    }
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected 200 for getting a Request Token, got %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }
  _print_response (call);
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
  url = goa_backend_oauth_provider_build_authorization_uri (provider,
                                                            goa_backend_oauth_provider_get_authorization_uri (provider),
                                                            escaped_request_token);
  if (use_external_browser)
    {
      GtkWidget *label;
      GtkWidget *entry;
      gchar *escaped_url;
      gchar *markup;

      escaped_url = g_markup_escape_text (url, -1);
      markup = g_strdup_printf (_("Paste token obtained from the <a href=\"%s\">authorization page</a>:"),
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
      GtkWidget *scrolled_window;
      GtkWidget *web_view;
      SoupSession *webkit_soup_session;
      SoupCookieJar *cookie_jar;

      /* Ensure we use an empty non-persistent cookie to avoid login
       * credentials being reused...
       */
      webkit_soup_session = webkit_get_default_session ();
      soup_session_remove_feature_by_type (webkit_soup_session, SOUP_TYPE_COOKIE_JAR);
      cookie_jar = soup_cookie_jar_new ();
      soup_session_add_feature (webkit_soup_session, SOUP_SESSION_FEATURE (cookie_jar));
      g_object_unref (cookie_jar);

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
    }
  data.dialog = dialog;
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

  g_print ("woot verifier is %s\n", data.oauth_verifier);

  /* OK, we now have the request token... we can exchange that for a
   * (short-lived) access token and session_handle (used to refresh the
   * access token)..
   */
  get_tokens (provider,
              data.request_token,
              data.request_token_secret,
              NULL, /* session_handle */
              data.oauth_verifier,
              NULL, /* GCancellable */
              (GAsyncReadyCallback) get_tokens_and_identity_on_get_tokens_cb,
              &data);
  g_main_loop_run (data.loop);
  if (data.access_token == NULL)
    goto out;

  goa_backend_oauth_provider_get_identity (provider,
                                           data.access_token,
                                           data.access_token_secret,
                                           NULL, /* TODO: GCancellable */
                                           (GAsyncReadyCallback) get_tokens_and_identity_on_get_identity_cb,
                                           &data);
  g_main_loop_run (data.loop);
  if (data.identity == NULL)
    goto out;

  ret = TRUE;

 out:
  if (call != NULL)
    g_object_unref (call);

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
    }
  else
    {
      g_warn_if_fail (data.error != NULL);
      g_propagate_error (error, data.error);
    }

  g_free (data.identity);
  g_free (url);

  g_free (data.oauth_verifier);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  g_free (data.access_token);
  g_free (data.access_token_secret);
  g_free (escaped_request_token);

  g_free (data.request_token);
  g_free (data.request_token_secret);

  g_strfreev (request_params);
  if (proxy != NULL)
    g_object_unref (proxy);
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
goa_backend_oauth_provider_add_account (GoaBackendProvider *_provider,
                                         GoaClient          *client,
                                         GtkDialog          *dialog,
                                         GtkBox             *vbox,
                                         GError            **error)
{
  GoaBackendOAuthProvider *provider = GOA_BACKEND_OAUTH_PROVIDER (_provider);
  GoaObject *ret;
  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;
  gchar *session_handle;
  gint session_handle_expires_in;
  gchar *identity;
  GList *accounts;
  GList *l;
  AddData data;
  GVariantBuilder builder;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  access_token = NULL;
  access_token_secret = NULL;
  identity = NULL;
  accounts = NULL;

  memset (&data, '\0', sizeof (AddData));
  data.loop = g_main_loop_new (NULL, FALSE);

  if (!get_tokens_and_identity (provider,
                                dialog,
                                vbox,
                                &access_token,
                                &access_token_secret,
                                &access_token_expires_in,
                                &session_handle,
                                &session_handle_expires_in,
                                &identity,
                                &data.error))
    goto out;

  /* OK, got the identity... see if there's already an account
   * of this type with the given identity
   */
  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      GoaAccount *account;
      GoaOAuthBased *oauth_based;
      const gchar *identity_from_object;

      account = goa_object_peek_account (object);
      oauth_based = goa_object_peek_oauth_based (object);
      if (oauth_based == NULL)
        continue;

      if (g_strcmp0 (goa_account_get_provider_type (account),
                     goa_backend_provider_get_provider_type (GOA_BACKEND_PROVIDER (provider))) != 0)
        continue;

      identity_from_object = goa_oauth_based_get_identity (oauth_based);
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

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  g_variant_builder_add (&builder, "{sv}", "access_token_secret", g_variant_new_string (access_token_secret));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (access_token_expires_in)));
  if (session_handle != NULL)
    g_variant_builder_add (&builder, "{sv}", "session_handle", g_variant_new_string (session_handle));
  if (session_handle_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (session_handle_expires_in)));
  goa_backend_provider_store_credentials (GOA_BACKEND_PROVIDER (provider),
                                          identity,
                                          g_variant_builder_end (&builder),
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

  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
  g_free (identity);
  g_free (access_token);
  g_free (access_token_secret);
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
goa_backend_oauth_provider_refresh_account (GoaBackendProvider  *_provider,
                                             GoaClient           *client,
                                             GoaObject           *object,
                                             GtkWindow           *parent,
                                             GError             **error)
{
  GoaBackendOAuthProvider *provider = GOA_BACKEND_OAUTH_PROVIDER (_provider);
  GtkWidget *dialog;
  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;
  gchar *session_handle;
  gint session_handle_expires_in;
  gchar *identity;
  const gchar *existing_identity;
  GVariantBuilder builder;
  gboolean ret;
  RefreshAccountData data;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  access_token = NULL;
  access_token_secret = NULL;
  identity = NULL;

  memset (&data, '\0', sizeof (RefreshAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_widget_show_all (dialog);

  if (!get_tokens_and_identity (provider,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                &access_token,
                                &access_token_secret,
                                &access_token_expires_in,
                                &session_handle,
                                &session_handle_expires_in,
                                &identity,
                                error))
    goto out;

  existing_identity = goa_oauth_based_get_identity (goa_object_peek_oauth_based (object));
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
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (access_token));
  g_variant_builder_add (&builder, "{sv}", "access_token_secret", g_variant_new_string (access_token_secret));
  if (access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (access_token_expires_in)));
  if (session_handle != NULL)
    g_variant_builder_add (&builder, "{sv}", "session_handle", g_variant_new_string (session_handle));
  if (session_handle_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (session_handle_expires_in)));
  goa_backend_provider_store_credentials (GOA_BACKEND_PROVIDER (provider),
                                          identity,
                                          g_variant_builder_end (&builder),
                                          NULL, /* GCancellable */
                                          (GAsyncReadyCallback) refresh_credentials_store_credentials_cb,
                                          &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      goto out;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (object),
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  gtk_widget_destroy (dialog);

  g_free (identity);
  g_free (access_token);
  g_free (access_token_secret);
  g_main_loop_unref (data.loop);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;

  GoaBackendOAuthProvider *provider;
  GoaObject *object;
  GSimpleAsyncResult *simple;
  GCancellable *cancellable;

  gboolean force_refresh;

  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;
  gchar *session_handle;
  gint session_handle_expires_in;
} GetAccessTokenData;

static void goa_backend_oauth_provider_dequeue_and_do_next (GetAccessTokenData *data);

static void
get_access_token_data_unref (GetAccessTokenData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      goa_backend_oauth_provider_dequeue_and_do_next (data);

      g_object_unref (data->object);
      if (data->cancellable != NULL)
        g_object_unref (data->cancellable);
      g_free (data->access_token);
      g_free (data->access_token_secret);
      g_free (data->session_handle);
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
get_access_token_store_credentials_cb (GoaBackendProvider   *provider,
                                       GAsyncResult         *res,
                                       gpointer              user_data)
{
  GetAccessTokenData *data = user_data;
  GError *error;

  error = NULL;
  if (!goa_backend_provider_store_credentials_finish (provider, res, &error))
    {
      g_warning ("Error storing credentials in keyring: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_simple_async_result_take_error (data->simple, error);
    }
  else
    {
      //g_debug ("Returning refreshed credentials (expires in %d seconds)", data->access_token_expires_in);

      g_simple_async_result_set_op_res_gpointer (data->simple,
                                                 get_access_token_data_ref (data),
                                                 (GDestroyNotify) get_access_token_data_unref);
    }
  g_simple_async_result_complete_in_idle (data->simple);
  g_object_unref (data->simple);
  get_access_token_data_unref (data);
}

static void
get_access_token_get_tokens_cb (GoaBackendOAuthProvider  *provider,
                                GAsyncResult              *res,
                                gpointer                   user_data)
{
  GetAccessTokenData *data = user_data;
  GVariantBuilder builder;
  GError *error;
  const gchar *identity;

  error = NULL;
  data->access_token = get_tokens_finish (provider,
                                          &data->access_token_secret,
                                          &data->access_token_expires_in,
                                          &data->session_handle,
                                          &data->session_handle_expires_in,
                                          res,
                                          &error);
  if (data->access_token == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       is_authorization_error (error) ? GOA_ERROR_NOT_AUTHORIZED : GOA_ERROR_FAILED,
                                       _("Failed to refresh access token: %s (%s, %d)"),
                                       error->message, g_quark_to_string (error->domain), error->code);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      g_error_free (error);
      goto out;
    }

  /* Good. Now update the keyring with the refreshed credentials */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (data->access_token));
  g_variant_builder_add (&builder, "{sv}", "access_token_secret", g_variant_new_string (data->access_token_secret));
  if (data->access_token_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "access_token_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (data->access_token_expires_in)));
  if (data->session_handle != NULL)
    g_variant_builder_add (&builder, "{sv}", "session_handle", g_variant_new_string (data->session_handle));
  if (data->session_handle_expires_in > 0)
    g_variant_builder_add (&builder, "{sv}", "session_handle_expires_at",
                           g_variant_new_int64 (duration_to_abs_usec (data->session_handle_expires_in)));

  identity = goa_oauth_based_get_identity (goa_object_peek_oauth_based (data->object));
  goa_backend_provider_store_credentials (GOA_BACKEND_PROVIDER (provider),
                                          identity,
                                          g_variant_builder_end (&builder),
                                          data->cancellable, /* GCancellable */
                                          (GAsyncReadyCallback) get_access_token_store_credentials_cb,
                                          get_access_token_data_ref (data));
 out:
  get_access_token_data_unref (data);
}

static void
get_access_token_lookup_credentials_cb (GoaBackendProvider *provider,
                                        GAsyncResult       *res,
                                        gpointer            user_data)
{
  GetAccessTokenData *data = user_data;
  GVariant *credentials;
  GVariantIter iter;
  const gchar *key;
  GVariant *value;
  GError *error;

  error = NULL;
  credentials = goa_backend_provider_lookup_credentials_finish (provider, res, &error);
  if (credentials == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_AUTHORIZED,
                                       _("Credentials not found in keyring: %s (%s, %d)"),
                                       error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  g_variant_iter_init (&iter, credentials);
  while (g_variant_iter_next (&iter, "{&sv}", &key, &value))
    {
      if (g_strcmp0 (key, "access_token") == 0)
        data->access_token = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "access_token_secret") == 0)
        data->access_token_secret = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "access_token_expires_at") == 0)
        data->access_token_expires_in = abs_usec_to_duration (g_variant_get_int64 (value));
      else if (g_strcmp0 (key, "session_handle") == 0)
        data->session_handle = g_variant_dup_string (value, NULL);
      else if (g_strcmp0 (key, "session_handle_expires_at") == 0)
        data->session_handle_expires_in = abs_usec_to_duration (g_variant_get_int64 (value));
      g_variant_unref (value);
    }

  if (data->access_token == NULL || data->access_token_secret == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_AUTHORIZED,
                                       _("Credentials does not contain access_token or access_token_secret"));
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  /* if we can't refresh the token, just return it no matter what */
  if (data->session_handle == NULL)
    {
      //g_debug ("Returning locally cached credentials that cannot be refreshed");
      g_simple_async_result_set_op_res_gpointer (data->simple,
                                                 get_access_token_data_ref (data),
                                                 (GDestroyNotify) get_access_token_data_unref);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  /* If access_token is still "fresh enough" (e.g. more than ten
   * minutes of life left in it), just return it unless we've been
   * asked to forcibly refresh it
   */
  if (!data->force_refresh && data->access_token_expires_in > 10*60)
    {
      //g_debug ("Returning locally cached credentials (expires in %d seconds)", data->access_token_expires_in);
      g_simple_async_result_set_op_res_gpointer (data->simple,
                                                 get_access_token_data_ref (data),
                                                 (GDestroyNotify) get_access_token_data_unref);
      g_simple_async_result_complete_in_idle (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  //g_debug ("Refreshing locally cached credentials (expires in %d seconds, force_refresh=%d)", data->access_token_expires_in, data->force_refresh);

  /* Otherwise, refresh it */
  get_tokens (data->provider,
              data->access_token,
              data->access_token_secret,
              data->session_handle,
              NULL, /* verifier */
              data->cancellable,
              (GAsyncReadyCallback) get_access_token_get_tokens_cb,
              get_access_token_data_ref (data));

 out:
  get_access_token_data_unref (data);
  if (credentials != NULL)
    g_variant_unref (credentials);
}

/* must hold lock while calling */
static void
goa_backend_oauth_provider_get_access_token_do_one (GetAccessTokenData *data)
{
  const gchar *identity;

  //g_debug ("running %p", data);

  /* First, get the credentials from the keyring */
  identity = goa_oauth_based_get_identity (goa_object_peek_oauth_based (data->object));
  goa_backend_provider_lookup_credentials (GOA_BACKEND_PROVIDER (data->provider),
                                           identity,
                                           data->cancellable,
                                           (GAsyncReadyCallback) get_access_token_lookup_credentials_cb,
                                           data);
}

/**
 * goa_backend_oauth_provider_get_access_token:
 * @provider: A #GoaBackendOAuthProvider.
 * @object: A #GoaObject.
 * @force_refresh: If set to %TRUE, forces a refresh of the access token, if possible.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Gets an access token for @object. The resulting token is typically
 * read from the local cache so most of the time only a local
 * roundtrip to the storage for the token cache
 * (e.g. <command>gnome-keyring-daemon</command>) is needed. However,
 * the operation may involve refreshing the token with the service
 * provider so a full network round-trip may be needed.
 *
 * Note that multiple calls are serialized to avoid multiple
 * outstanding requests to the service provider.
 *
 * This operation may fail if e.g. unable to refresh the credentials
 * or if network connectivity is not available. Note that even if a
 * token is returned, the returned token isn't guaranteed to work -
 * use goa_backend_provider_ensure_credentials() if you need stronger
 * guarantees.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this method was called from. You can then call
 * goa_backend_oauth_provider_get_access_token_finish() to get the
 * result of the operation.
 */
void
goa_backend_oauth_provider_get_access_token (GoaBackendOAuthProvider  *provider,
                                             GoaObject                *object,
                                             gboolean                  force_refresh,
                                             GCancellable             *cancellable,
                                             GAsyncReadyCallback       callback,
                                             gpointer                  user_data)
{
  GetAccessTokenData *data;
  GPtrArray *queue;

  g_return_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_slice_new0 (GetAccessTokenData);
  data->ref_count = 1;
  data->provider = provider;
  data->object = g_object_ref (object);
  data->cancellable = cancellable != NULL ? g_object_ref (cancellable) : NULL;
  data->force_refresh = force_refresh;
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            goa_backend_oauth_provider_get_access_token);

  G_LOCK (queue_lock);
  queue = g_object_get_data (G_OBJECT (data->object), "goa-oauth-get-access-token-queue");
  if (queue == NULL)
    {
      queue = g_ptr_array_new ();
      g_object_set_data_full (G_OBJECT (data->object),
                              "goa-oauth-get-access-token-queue",
                              queue,
                              (GDestroyNotify) g_ptr_array_unref);
      g_ptr_array_add (queue, data);
      //g_debug ("nothing in queue");
      goa_backend_oauth_provider_get_access_token_do_one (data);
    }
  else
    {
      //g_debug ("enqueing %p", data);
      g_ptr_array_add (queue, data);
    }
  G_UNLOCK (queue_lock);
}

static void
goa_backend_oauth_provider_dequeue_and_do_next (GetAccessTokenData *data)
{
  GPtrArray *queue;
  GetAccessTokenData *next;

  G_LOCK (queue_lock);
  queue = g_object_get_data (G_OBJECT (data->object), "goa-oauth-get-access-token-queue");
  //g_debug ("dequeing %p", data);
  g_assert (queue != NULL);
  g_assert (data == g_ptr_array_remove_index (queue, 0));
  if (queue->len == 0)
    {
      //g_debug ("queue is now empty");
      g_object_set_data (G_OBJECT (data->object), "goa-oauth-get-access-token-queue", NULL);
    }
  else
    {
      next = g_ptr_array_index (queue, 0);
      //g_debug ("next in queue is %p", next);
      goa_backend_oauth_provider_get_access_token_do_one (next);
    }
  G_UNLOCK (queue_lock);
}

/**
 * goa_backend_oauth_provider_get_access_token_finish:
 * @provider: A #GoaBackendOAuthProvider.
 * @out_access_token_secret: (out): The secret for the return access token.
 * @out_access_token_expires_in: (out): Return location for how many seconds the returned token is valid for (0 if unknown) or %NULL.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_oauth_provider_get_access_token().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_oauth_provider_get_access_token().
 *
 * Returns: The access token or %NULL if error is set. The returned string must be freed with g_free().
 */
gchar *
goa_backend_oauth_provider_get_access_token_finish (GoaBackendOAuthProvider   *provider,
                                                    gchar                    **out_access_token_secret,
                                                    gint                      *out_access_token_expires_in,
                                                    GAsyncResult              *res,
                                                    GError                   **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetAccessTokenData *data;
  gchar *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (provider),
                                                        goa_backend_oauth_provider_get_access_token), NULL);

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  ret = g_strdup (data->access_token);
  if (out_access_token_secret != NULL)
    *out_access_token_secret = g_strdup (data->access_token_secret);
  if (out_access_token_expires_in != NULL)
    *out_access_token_expires_in = data->access_token_expires_in;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_access_token (GoaOAuthBased        *object,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data);

static gboolean
goa_backend_oauth_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaOAuthBased *oauth_based;
  gchar *identity;

  identity = NULL;

  oauth_based = goa_object_get_oauth_based (GOA_OBJECT (object));
  if (oauth_based != NULL)
    goto out;

  oauth_based = goa_oauth_based_skeleton_new ();
  goa_object_skeleton_set_oauth_based (object, oauth_based);
  g_signal_connect (oauth_based,
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
  goa_oauth_based_set_identity (oauth_based, identity);

 out:
  g_object_unref (oauth_based);
  g_free (identity);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  volatile gint ref_count;
  GSimpleAsyncResult *simple;
  GoaObject *object;
  GCancellable *cancellable;

  gchar *access_token;
  gchar *access_token_secret;
  gint   access_token_expires_in;

  gboolean forced;

  gchar *identity;

} CheckCredentialsData;

static CheckCredentialsData *
ensure_credentials_data_ref (CheckCredentialsData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
ensure_credentials_data_unref (CheckCredentialsData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->object);
      if (data->cancellable != NULL)
        g_object_unref (data->cancellable);
      g_free (data->identity);
      g_free (data->access_token);
      g_free (data->access_token_secret);
      g_slice_free (CheckCredentialsData, data);
    }
}

static void
ensure_credentials_get_access_token_cb (GoaBackendOAuthProvider *provider,
                                        GAsyncResult            *res,
                                        gpointer                 user_data);

static void
ensure_credentials_get_identity_cb (GoaBackendOAuthProvider *provider,
                                    GAsyncResult            *res,
                                    gpointer                 user_data)
{
  CheckCredentialsData *data = user_data;
  GError *error;

  error = NULL;
  data->identity = goa_backend_oauth_provider_get_identity_finish (provider,
                                                                   res,
                                                                   &error);
  if (data->identity == NULL)
    {
      /* OK, try again, with forcing the locally cached credentials to be refreshed */
      if (!data->forced)
        {
          data->forced = TRUE;
          g_free (data->access_token);
          g_free (data->access_token_secret);
          data->access_token = NULL;
          data->access_token_secret = NULL;
          data->access_token_expires_in = 0;
          goa_backend_oauth_provider_get_access_token (GOA_BACKEND_OAUTH_PROVIDER (provider),
                                                       data->object,
                                                       TRUE, /* force_refresh */
                                                       data->cancellable,
                                                       (GAsyncReadyCallback) ensure_credentials_get_access_token_cb,
                                                       ensure_credentials_data_ref (data));
          goto out;
        }
      else
        {
          g_simple_async_result_take_error (data->simple, error);
          g_simple_async_result_complete (data->simple);
          g_object_unref (data->simple);
          goto out;
        }
    }

  /* TODO: maybe check with the identity we have */

  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             ensure_credentials_data_ref (data),
                                             (GDestroyNotify) ensure_credentials_data_unref);
  g_simple_async_result_complete (data->simple);
  g_object_unref (data->simple);

 out:
  ensure_credentials_data_unref (data);
}

static void
ensure_credentials_get_access_token_cb (GoaBackendOAuthProvider *provider,
                                        GAsyncResult            *res,
                                        gpointer                 user_data)
{
  CheckCredentialsData *data = user_data;
  GError *error;

  error = NULL;
  data->access_token = goa_backend_oauth_provider_get_access_token_finish (provider,
                                                                           &data->access_token_secret,
                                                                           &data->access_token_expires_in,
                                                                           res,
                                                                           &error);
  if (data->access_token == NULL)
    {
      g_simple_async_result_take_error (data->simple, error);
      g_simple_async_result_complete (data->simple);
      g_object_unref (data->simple);
      goto out;
    }

  goa_backend_oauth_provider_get_identity (provider,
                                           data->access_token,
                                           data->access_token_secret,
                                           data->cancellable,
                                           (GAsyncReadyCallback) ensure_credentials_get_identity_cb,
                                           ensure_credentials_data_ref (data));

 out:
  ensure_credentials_data_unref (data);
}

static void
goa_backend_oauth_provider_ensure_credentials (GoaBackendProvider   *provider,
                                               GoaObject            *object,
                                               GCancellable         *cancellable,
                                               GAsyncReadyCallback   callback,
                                               gpointer              user_data)
{
  CheckCredentialsData *data;

  data = g_slice_new0 (CheckCredentialsData);
  data->object = g_object_ref (object);
  data->ref_count = 1;
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            goa_backend_oauth_provider_ensure_credentials);
  data->cancellable = cancellable != NULL ? g_object_ref (cancellable) : NULL;
  goa_backend_oauth_provider_get_access_token (GOA_BACKEND_OAUTH_PROVIDER (provider),
                                               data->object,
                                               FALSE, /* force_refresh */
                                               data->cancellable,
                                               (GAsyncReadyCallback) ensure_credentials_get_access_token_cb,
                                               data);
}

static gboolean
goa_backend_oauth_provider_ensure_credentials_finish (GoaBackendProvider  *provider,
                                                      gint                *out_expires_in,
                                                      GAsyncResult        *res,
                                                      GError             **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  CheckCredentialsData *data;
  gboolean ret;

  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), FALSE);

  ret = FALSE;

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  ret = TRUE;
  data = g_simple_async_result_get_op_res_gpointer (simple);
  if (out_expires_in != NULL)
    *out_expires_in = data->access_token_expires_in;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_oauth_provider_init (GoaBackendOAuthProvider *client)
{
}

static void
goa_backend_oauth_provider_class_init (GoaBackendOAuthProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->add_account                = goa_backend_oauth_provider_add_account;
  provider_class->refresh_account            = goa_backend_oauth_provider_refresh_account;
  provider_class->build_object               = goa_backend_oauth_provider_build_object;
  provider_class->ensure_credentials         = goa_backend_oauth_provider_ensure_credentials;
  provider_class->ensure_credentials_finish  = goa_backend_oauth_provider_ensure_credentials_finish;

  klass->build_authorization_uri  = goa_backend_oauth_provider_build_authorization_uri_default;
  klass->get_use_external_browser = goa_backend_oauth_provider_get_use_external_browser_default;
  klass->get_request_uri_params   = goa_backend_oauth_provider_get_request_uri_params_default;
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
get_access_token_cb (GoaBackendOAuthProvider  *provider,
                     GAsyncResult              *res,
                     gpointer                   user_data)
{
  AccessTokenData *data = user_data;
  GError *error;
  gchar *access_token;
  gchar *access_token_secret;
  gint access_token_expires_in;

  error = NULL;
  access_token = goa_backend_oauth_provider_get_access_token_finish (provider,
                                                                     &access_token_secret,
                                                                     &access_token_expires_in,
                                                                     res,
                                                                     &error);
  if (access_token == NULL)
    {
      g_dbus_method_invocation_return_gerror (data->invocation, error);
      g_error_free (error);
    }
  else
    {
      goa_oauth_based_complete_get_access_token (goa_object_peek_oauth_based (data->object),
                                                 data->invocation,
                                                 access_token,
                                                 access_token_secret,
                                                 access_token_expires_in);
      g_free (access_token);
    }
  access_token_data_free (data);
}

static gboolean
on_handle_get_access_token (GoaOAuthBased        *interface,
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
  provider = goa_backend_provider_get_for_provider_type (goa_account_get_provider_type (account));

  data = g_new0 (AccessTokenData, 1);
  data->object = g_object_ref (object);
  data->invocation = invocation;
  goa_backend_oauth_provider_get_access_token (GOA_BACKEND_OAUTH_PROVIDER (provider),
                                               object,
                                               FALSE, /* force_refresh */
                                               NULL, /* GCancellable* */
                                               (GAsyncReadyCallback) get_access_token_cb,
                                               data);
  g_object_unref (provider);

  return TRUE; /* invocation was handled */
}
