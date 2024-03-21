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

#ifndef __GOA_OAUTH2_PROVIDER_H__
#define __GOA_OAUTH2_PROVIDER_H__

#include <gio/gio.h>

#include "goabackend/goaprovider.h"
#include "goabackend/goaprovider-priv.h"

G_BEGIN_DECLS

#define GOA_TYPE_OAUTH2_PROVIDER         (goa_oauth2_provider_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaOAuth2Provider, goa_oauth2_provider, GOA, OAUTH2_PROVIDER, GoaProvider);

typedef struct _GoaOAuth2ProviderPrivate GoaOAuth2ProviderPrivate;

const gchar *goa_oauth2_provider_get_authorization_uri        (GoaOAuth2Provider             *provider);
const gchar *goa_oauth2_provider_get_token_uri                (GoaOAuth2Provider             *provider);
const gchar *goa_oauth2_provider_get_redirect_uri             (GoaOAuth2Provider             *provider);
const gchar *goa_oauth2_provider_get_scope                    (GoaOAuth2Provider             *provider);
const gchar *goa_oauth2_provider_get_client_id                (GoaOAuth2Provider             *provider);
const gchar *goa_oauth2_provider_get_client_secret            (GoaOAuth2Provider             *provider);
gchar       *goa_oauth2_provider_get_identity_sync            (GoaOAuth2Provider             *provider,
                                                               const gchar              *access_token,
                                                               gchar                   **out_presentation_identity,
                                                               GCancellable             *cancellable,
                                                               GError                  **error);
gchar       *goa_oauth2_provider_get_access_token_sync        (GoaOAuth2Provider        *provider,
                                                               GoaObject                *object,
                                                               gboolean                 force_refresh,
                                                               gint                     *out_access_token_expires_in,
                                                               GCancellable             *cancellable,
                                                               GError                  **error);
gchar       *goa_oauth2_provider_build_authorization_uri      (GoaOAuth2Provider             *provider,
                                                               const gchar                   *authorization_uri,
                                                               const gchar                   *escaped_redirect_uri,
                                                               const gchar                   *escaped_client_id,
                                                               const gchar                   *escaped_scope,
                                                               const gchar                   *code_challenge_method,
                                                               const gchar                   *code_challenge);
gboolean     goa_oauth2_provider_get_use_pkce                 (GoaOAuth2Provider             *provider);

gboolean     goa_oauth2_provider_get_use_mobile_browser       (GoaOAuth2Provider             *provider);
void         goa_oauth2_provider_add_account_key_values       (GoaOAuth2Provider             *provider,
                                                               GVariantBuilder               *builder);
gboolean     goa_oauth2_provider_process_redirect_url         (GoaOAuth2Provider             *provider,
                                                               const gchar                   *redirect_url,
                                                               gchar                        **authorization_code,
                                                               GError                       **error);

G_END_DECLS

#endif /* __GOA_OAUTH2_PROVIDER_H__ */
