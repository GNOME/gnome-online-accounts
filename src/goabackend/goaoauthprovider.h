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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_OAUTH_PROVIDER_H__
#define __GOA_OAUTH_PROVIDER_H__

#include <goabackend/goaprovider.h>
#include <goabackend/goaprovider-priv.h>
#include <rest/rest-proxy-call.h>
#include <webkitdom/webkitdom.h>

G_BEGIN_DECLS

#define GOA_TYPE_OAUTH_PROVIDER         (goa_oauth_provider_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaOAuthProvider, goa_oauth_provider, GOA, OAUTH_PROVIDER, GoaProvider);

/**
 * GoaOAuthProvider:
 *
 * The #GoaOAuthProvider structure contains only private data and should
 * only be accessed using the provided API.
 */

/**
 * GoaOAuthProviderClass:
 * @parent_class: The parent class.
 * @get_consumer_key: Virtual function for goa_oauth_provider_get_consumer_key().
 * @get_consumer_secret: Virtual function for goa_oauth_provider_get_consumer_secret().
 * @get_request_uri: Virtual function for goa_oauth_provider_get_request_uri().
 * @get_authorization_uri: Virtual function for goa_oauth_provider_get_authorization_uri().
 * @get_token_uri: Virtual function for goa_oauth_provider_get_token_uri().
 * @get_callback_uri: Virtual function for goa_oauth_provider_get_callback_uri().
 * @get_identity_sync: Virtual function for goa_oauth_provider_get_identity_sync().
 * @parse_request_token_error: Virtual function for goa_oauth_provider_parse_request_token_error().
 * @build_authorization_uri: Virtual function for goa_oauth_provider_build_authorization_uri().
 * @get_use_mobile_browser: Virtual function for goa_oauth_provider_get_use_mobile_browser().
 * @get_request_uri_params: Virtual function for goa_oauth_provider_get_request_uri_params().
 * @add_account_key_values: Virtual function for goa_oauth_provider_add_account_key_values().
 * @is_deny_node: Virtual function for goa_oauth_provider_is_deny_node().
 * @is_identity_node: Virtual function for goa_oauth_provider_is_identity_node().
 * @is_password_node: Virtual function for goa_oauth_provider_is_password_node().
 *
 * Class structure for #GoaOAuthProvider.
 */
struct _GoaOAuthProviderClass
{
  GoaProviderClass parent_class;

  /* pure virtual */
  const gchar *(*get_consumer_key)             (GoaOAuthProvider             *provider);
  const gchar *(*get_consumer_secret)          (GoaOAuthProvider             *provider);
  const gchar *(*get_request_uri)              (GoaOAuthProvider             *provider);
  const gchar *(*get_authorization_uri)        (GoaOAuthProvider             *provider);
  const gchar *(*get_token_uri)                (GoaOAuthProvider             *provider);
  const gchar *(*get_callback_uri)             (GoaOAuthProvider             *provider);

  gchar       *(*get_identity_sync)            (GoaOAuthProvider             *provider,
                                                const gchar                  *access_token,
                                                const gchar                  *access_token_secret,
                                                gchar                       **out_presentation_identity,
                                                GCancellable                 *cancellable,
                                                GError                      **error);

  gchar       *(*parse_request_token_error)    (GoaOAuthProvider             *provider,
                                                RestProxyCall                *call);

  /* virtual but with default implementation */
  gchar       *(*build_authorization_uri)      (GoaOAuthProvider             *provider,
                                                const gchar                  *authorization_uri,
                                                const gchar                  *escaped_oauth_token);
  gboolean     (*get_use_mobile_browser)       (GoaOAuthProvider             *provider);
  gchar      **(*get_request_uri_params)       (GoaOAuthProvider             *provider);
  void         (*add_account_key_values)       (GoaOAuthProvider             *provider,
                                                GVariantBuilder              *builder);

  /* pure virtual */
  gboolean     (*is_identity_node)             (GoaOAuthProvider             *provider,
                                                WebKitDOMHTMLInputElement    *element);

  /* virtual but with default implementation */
  gboolean     (*is_deny_node)                 (GoaOAuthProvider             *provider,
                                                WebKitDOMNode                *node);
  gboolean     (*is_password_node)             (GoaOAuthProvider             *provider,
                                                WebKitDOMHTMLInputElement    *element);
};

const gchar *goa_oauth_provider_get_consumer_key             (GoaOAuthProvider             *provider);
const gchar *goa_oauth_provider_get_consumer_secret          (GoaOAuthProvider             *provider);
const gchar *goa_oauth_provider_get_request_uri              (GoaOAuthProvider             *provider);
gchar      **goa_oauth_provider_get_request_uri_params       (GoaOAuthProvider             *provider);
const gchar *goa_oauth_provider_get_authorization_uri        (GoaOAuthProvider             *provider);
const gchar *goa_oauth_provider_get_token_uri                (GoaOAuthProvider             *provider);
const gchar *goa_oauth_provider_get_callback_uri             (GoaOAuthProvider             *provider);
gchar       *goa_oauth_provider_get_identity_sync            (GoaOAuthProvider          *provider,
                                                              const gchar               *access_token,
                                                              const gchar               *access_token_secret,
                                                              gchar                    **out_presentation_identity,
                                                              GCancellable              *cancellable,
                                                              GError                   **error);
gboolean     goa_oauth_provider_is_deny_node                 (GoaOAuthProvider             *provider,
                                                              WebKitDOMNode                *node);
gboolean     goa_oauth_provider_is_identity_node             (GoaOAuthProvider             *provider,
                                                              WebKitDOMHTMLInputElement    *element);
gboolean     goa_oauth_provider_is_password_node             (GoaOAuthProvider             *provider,
                                                              WebKitDOMHTMLInputElement    *element);
gchar       *goa_oauth_provider_parse_request_token_error    (GoaOAuthProvider             *provider,
                                                              RestProxyCall                *call);
gchar       *goa_oauth_provider_get_access_token_sync        (GoaOAuthProvider          *provider,
                                                              GoaObject                 *object,
                                                              gboolean                   force_refresh,
                                                              gchar                    **out_access_token_secret,
                                                              gint                      *out_access_token_expires_in,
                                                              GCancellable              *cancellable,
                                                              GError                   **error);
gchar       *goa_oauth_provider_build_authorization_uri      (GoaOAuthProvider             *provider,
                                                              const gchar                  *authorization_uri,
                                                              const gchar                  *escaped_oauth_token);
gboolean     goa_oauth_provider_get_use_mobile_browser       (GoaOAuthProvider             *provider);
void         goa_oauth_provider_add_account_key_values       (GoaOAuthProvider             *provider,
                                                              GVariantBuilder              *builder);

G_END_DECLS

#endif /* __GOA_OAUTH_PROVIDER_H__ */
