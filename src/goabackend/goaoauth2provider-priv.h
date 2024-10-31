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

#ifndef __GOA_OAUTH2_PROVIDER_PRIV_H__
#define __GOA_OAUTH2_PROVIDER_PRIV_H__

#include <gio/gio.h>
#include <goabackend/goaoauth2provider.h>
#include <goabackend/goaprovider-priv.h>

G_BEGIN_DECLS

/**
 * GoaAuthFlowFlags:
 * @GOA_AUTH_FLOW_DEFAULT: default operation for the given flow
 * @GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI: do not launch the given request URI,
 *   allowing the client control the launch context.
 *
 * Flags for authorization flows.
 */
typedef enum
{
  GOA_AUTH_FLOW_DEFAULT           = 0,
  GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI = (1 << 0),
} GoaAuthFlowFlags;

/**
 * GoaOAuth2Provider:
 *
 * The #GoaOAuth2Provider structure contains only private data and should
 * only be accessed using the provided API.
 */

/**
 * GoaOAuth2ProviderClass:
 * @parent_class: The parent class.
 * @get_authorization_uri: Virtual function for goa_oauth2_provider_get_authorization_uri().
 * @get_token_uri: Virtual function for goa_oauth2_provider_get_token_uri().
 * @get_redirect_uri: Virtual function for goa_oauth2_provider_get_redirect_uri().
 * @get_scope: Virtual function for goa_oauth2_provider_get_scope().
 * @get_client_id: Virtual function for goa_oauth2_provider_get_client_id().
 * @get_client_secret: Virtual function for goa_oauth2_provider_get_client_secret().
 * @get_identity_sync: Virtual function for goa_oauth2_provider_get_identity_sync().
 * @build_authorization_uri: Virtual function for goa_oauth2_provider_build_authorization_uri().
 * @get_use_mobile_browser: Virtual function for goa_oauth2_provider_get_use_mobile_browser().
 * @add_account_key_values: Virtual function for goa_oauth2_provider_add_account_key_values().
 * @process_redirect_url: Virtual function for goa_oauth2_provider_process_redirect_url().
 *
 * Class structure for #GoaOAuth2Provider.
 */
struct _GoaOAuth2ProviderClass
{
  GoaProviderClass parent_class;

  /* pure virtual */
  const gchar *(*get_authorization_uri)        (GoaOAuth2Provider            *provider);
  const gchar *(*get_redirect_uri)             (GoaOAuth2Provider            *provider);
  const gchar *(*get_client_id)                (GoaOAuth2Provider            *provider);
  const gchar *(*get_client_secret)            (GoaOAuth2Provider            *provider);
  gchar       *(*get_identity_sync)            (GoaOAuth2Provider            *provider,
                                                const gchar                  *access_token,
                                                gchar                       **out_presentation_identity,
                                                GCancellable                 *cancellable,
                                                GError                      **error);

  /* virtual but with default implementation */
  gchar       *(*build_authorization_uri)      (GoaOAuth2Provider            *provider,
                                                const gchar                  *authorization_uri,
                                                const gchar                  *escaped_redirect_uri,
                                                const gchar                  *escaped_client_id,
                                                const gchar                  *escaped_scope,
                                                const gchar                  *code_challenge_method,
                                                const gchar                  *code_challenge);
  const gchar *(*get_token_uri)                (GoaOAuth2Provider            *provider);
  const gchar *(*get_scope)                    (GoaOAuth2Provider            *provider);
  gboolean     (*get_use_pkce)                 (GoaOAuth2Provider            *provider);
  gboolean     (*get_use_mobile_browser)       (GoaOAuth2Provider            *provider);
  void         (*add_account_key_values)       (GoaOAuth2Provider            *provider,
                                                GVariantBuilder              *builder);

  /* virtual but with default implementation */
  gboolean     (*process_redirect_url)         (GoaOAuth2Provider            *provider,
                                                const gchar                  *redirect_url,
                                                gchar                       **access_token,
                                                GError                      **error);
};

void    goa_oauth2_provider_authorize_uri        (GoaOAuth2Provider            *provider,
                                                  const char                   *request_uri,
                                                  GoaAuthFlowFlags              flags,
                                                  GCancellable                 *cancellable,
                                                  GAsyncReadyCallback           callback,
                                                  gpointer                      user_data);
char   *goa_oauth2_provider_authorize_uri_finish (GoaOAuth2Provider            *provider,
                                                  GAsyncResult                 *result,
                                                  GError                      **error);

G_END_DECLS

#endif /* __GOA_OAUTH2_PROVIDER_PRIV_H__ */
