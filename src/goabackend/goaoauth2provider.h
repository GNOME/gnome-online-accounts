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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_OAUTH2_PROVIDER_H__
#define __GOA_OAUTH2_PROVIDER_H__

#include <goabackend/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_OAUTH2_PROVIDER         (goa_oauth2_provider_get_type ())
#define GOA_OAUTH2_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_OAUTH2_PROVIDER, GoaOAuth2Provider))
#define GOA_OAUTH2_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_OAUTH2_PROVIDER, GoaOAuth2ProviderClass))
#define GOA_OAUTH2_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_OAUTH2_PROVIDER, GoaOAuth2ProviderClass))
#define GOA_IS_OAUTH2_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_OAUTH2_PROVIDER))

#define GOA_IS_OAUTH2_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_OAUTH2_PROVIDER))

typedef struct _GoaOAuth2ProviderClass GoaOAuth2ProviderClass;
typedef struct _GoaOAuth2ProviderPrivate GoaOAuth2ProviderPrivate;

/**
 * GoaOAuth2Provider:
 *
 * The #GoaOAuth2Provider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaOAuth2Provider
{
  /*< private >*/
  GoaProvider parent_instance;
  GoaOAuth2ProviderPrivate *priv;
};

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
 * @get_use_external_browser: Virtual function for goa_oauth2_provider_get_use_external_browser().
 *
 * Class structure for #GoaOAuth2Provider.
 */
struct _GoaOAuth2ProviderClass
{
  GoaProviderClass parent_class;

  /* pure virtual */
  const gchar *(*get_authorization_uri)    (GoaOAuth2Provider  *provider);
  const gchar *(*get_token_uri)            (GoaOAuth2Provider  *provider);
  const gchar *(*get_redirect_uri)         (GoaOAuth2Provider  *provider);
  const gchar *(*get_scope)                (GoaOAuth2Provider  *provider);
  const gchar *(*get_client_id)            (GoaOAuth2Provider  *provider);
  const gchar *(*get_client_secret)        (GoaOAuth2Provider  *provider);
  gchar       *(*get_identity_sync)        (GoaOAuth2Provider  *provider,
                                            const gchar        *access_token,
                                            gchar             **out_name,
                                            GCancellable       *cancellable,
                                            GError            **error);

  /* virtual but with default implementation */
  gchar       *(*build_authorization_uri)  (GoaOAuth2Provider  *provider,
                                            const gchar        *authorization_uri,
                                            const gchar        *escaped_redirect_uri,
                                            const gchar        *escaped_client_id,
                                            const gchar        *escaped_scope);
  gboolean     (*get_use_external_browser) (GoaOAuth2Provider  *provider);

  /*< private >*/
  /* Padding for future expansion */
  gpointer goa_reserved[32];
};

GType        goa_oauth2_provider_get_type                 (void) G_GNUC_CONST;
const gchar *goa_oauth2_provider_get_authorization_uri    (GoaOAuth2Provider  *provider);
const gchar *goa_oauth2_provider_get_token_uri            (GoaOAuth2Provider  *provider);
const gchar *goa_oauth2_provider_get_redirect_uri         (GoaOAuth2Provider  *provider);
const gchar *goa_oauth2_provider_get_scope                (GoaOAuth2Provider  *provider);
const gchar *goa_oauth2_provider_get_client_id            (GoaOAuth2Provider  *provider);
const gchar *goa_oauth2_provider_get_client_secret        (GoaOAuth2Provider  *provider);
gchar       *goa_oauth2_provider_get_identity_sync        (GoaOAuth2Provider  *provider,
                                                           const gchar        *access_token,
                                                           gchar             **out_name,
                                                           GCancellable       *cancellable,
                                                           GError            **error);
gchar       *goa_oauth2_provider_get_access_token_sync    (GoaOAuth2Provider  *provider,
                                                           GoaObject          *object,
                                                           gboolean            force_refresh,
                                                           gint               *out_access_token_expires_in,
                                                           GCancellable       *cancellable,
                                                           GError            **error);
gchar       *goa_oauth2_provider_build_authorization_uri  (GoaOAuth2Provider  *provider,
                                                           const gchar        *authorization_uri,
                                                           const gchar        *escaped_redirect_uri,
                                                           const gchar        *escaped_client_id,
                                                           const gchar        *escaped_scope);
gboolean     goa_oauth2_provider_get_use_external_browser (GoaOAuth2Provider  *provider);

G_END_DECLS

#endif /* __GOA_OAUTH2_PROVIDER_H__ */
