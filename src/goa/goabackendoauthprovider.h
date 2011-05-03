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
#error "Only <goa/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_OAUTH_PROVIDER_H__
#define __GOA_BACKEND_OAUTH_PROVIDER_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_OAUTH_PROVIDER         (goa_backend_oauth_provider_get_type ())
#define GOA_BACKEND_OAUTH_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_BACKEND_OAUTH_PROVIDER, GoaBackendOAuthProvider))
#define GOA_BACKEND_OAUTH_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_BACKEND_OAUTH_PROVIDER, GoaBackendOAuthProviderClass))
#define GOA_BACKEND_OAUTH_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_BACKEND_OAUTH_PROVIDER, GoaBackendOAuthProviderClass))
#define GOA_IS_BACKEND_OAUTH_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_BACKEND_OAUTH_PROVIDER))

#define GOA_IS_BACKEND_OAUTH_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_BACKEND_OAUTH_PROVIDER))

typedef struct _GoaBackendOAuthProviderClass GoaBackendOAuthProviderClass;
typedef struct _GoaBackendOAuthProviderPrivate GoaBackendOAuthProviderPrivate;

/**
 * GoaBackendOAuthProvider:
 *
 * The #GoaBackendOAuthProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendOAuthProvider
{
  /*< private >*/
  GoaBackendProvider parent_instance;
  GoaBackendOAuthProviderPrivate *priv;
};

/**
 * GoaBackendOAuthProviderClass:
 * @parent_class: The parent class.
 * @get_consumer_key: Virtual function for goa_backend_oauth_provider_get_consumer_key().
 * @get_consumer_secret: Virtual function for goa_backend_oauth_provider_get_consumer_secret().
 * @get_request_uri: Virtual function for goa_backend_oauth_provider_get_request_uri().
 * @get_authorization_uri: Virtual function for goa_backend_oauth_provider_get_authorization_uri().
 * @get_token_uri: Virtual function for goa_backend_oauth_provider_get_token_uri().
 * @get_callback_uri: Virtual function for goa_backend_oauth_provider_get_callback_uri().
 * @get_identity: Virtual function for goa_backend_oauth_provider_get_identity().
 * @get_identity_finish: Virtual function for goa_backend_oauth_provider_get_identity_finish().
 * @build_authorization_uri: Virtual function for goa_backend_oauth_provider_build_authorization_uri().
 * @get_use_external_browser: Virtual function for goa_backend_oauth_provider_get_use_external_browser().
 * @get_request_uri_params: Virtual function for goa_backend_oauth_provider_get_request_uri_params().
 *
 * Class structure for #GoaBackendOAuthProvider.
 */
struct _GoaBackendOAuthProviderClass
{
  GoaBackendProviderClass parent_class;

  /* pure virtual */
  const gchar *(*get_consumer_key)      (GoaBackendOAuthProvider  *provider);
  const gchar *(*get_consumer_secret)   (GoaBackendOAuthProvider  *provider);
  const gchar *(*get_request_uri)       (GoaBackendOAuthProvider  *provider);
  const gchar *(*get_authorization_uri) (GoaBackendOAuthProvider  *provider);
  const gchar *(*get_token_uri)         (GoaBackendOAuthProvider  *provider);
  const gchar *(*get_callback_uri)      (GoaBackendOAuthProvider  *provider);

  void         (*get_identity)           (GoaBackendOAuthProvider  *provider,
                                          const gchar               *access_token,
                                          const gchar               *access_token_secret,
                                          GCancellable              *cancellable,
                                          GAsyncReadyCallback        callback,
                                          gpointer                   user_data);
  gchar       *(*get_identity_finish)    (GoaBackendOAuthProvider  *provider,
                                          gchar                   **out_name,
                                          GAsyncResult              *res,
                                          GError                   **error);

  /* virtual but with default implementation */
  gchar     *(*build_authorization_uri)  (GoaBackendOAuthProvider  *provider,
                                         const gchar               *authorization_uri,
                                         const gchar               *escaped_oauth_token);
  gboolean   (*get_use_external_browser) (GoaBackendOAuthProvider  *provider);
  gchar    **(*get_request_uri_params)   (GoaBackendOAuthProvider  *provider);

  /*< private >*/
  /* Padding for future expansion */
  gpointer goa_reserved[32];
};

GType        goa_backend_oauth_provider_get_type                 (void) G_GNUC_CONST;
const gchar *goa_backend_oauth_provider_get_consumer_key         (GoaBackendOAuthProvider  *provider);
const gchar *goa_backend_oauth_provider_get_consumer_secret      (GoaBackendOAuthProvider  *provider);
const gchar *goa_backend_oauth_provider_get_request_uri          (GoaBackendOAuthProvider  *provider);
gchar      **goa_backend_oauth_provider_get_request_uri_params   (GoaBackendOAuthProvider  *provider);
const gchar *goa_backend_oauth_provider_get_authorization_uri    (GoaBackendOAuthProvider  *provider);
const gchar *goa_backend_oauth_provider_get_token_uri            (GoaBackendOAuthProvider  *provider);
const gchar *goa_backend_oauth_provider_get_callback_uri         (GoaBackendOAuthProvider  *provider);
void         goa_backend_oauth_provider_get_identity             (GoaBackendOAuthProvider  *provider,
                                                                  const gchar              *access_token,
                                                                  const gchar              *access_token_secret,
                                                                  GCancellable             *cancellable,
                                                                  GAsyncReadyCallback       callback,
                                                                  gpointer                  user_data);
gchar       *goa_backend_oauth_provider_get_identity_finish      (GoaBackendOAuthProvider  *provider,
                                                                  gchar                   **out_name,
                                                                  GAsyncResult             *res,
                                                                  GError                   **error);
void         goa_backend_oauth_provider_get_access_token         (GoaBackendOAuthProvider  *provider,
                                                                  GoaObject                *object,
                                                                  gboolean                  force_refresh,
                                                                  GCancellable             *cancellable,
                                                                  GAsyncReadyCallback       callback,
                                                                  gpointer                  user_data);
gchar       *goa_backend_oauth_provider_get_access_token_finish  (GoaBackendOAuthProvider  *provider,
                                                                  gchar                    **out_access_token_secret,
                                                                  gint                     *out_access_token_expires_in,
                                                                  GAsyncResult             *res,
                                                                  GError                  **error);

/* ---------------------------------------------------------------------------------------------------- */

gchar       *goa_backend_oauth_provider_build_authorization_uri  (GoaBackendOAuthProvider  *provider,
                                                                  const gchar               *authorization_uri,
                                                                  const gchar               *escaped_oauth_token);
gboolean     goa_backend_oauth_provider_get_use_external_browser (GoaBackendOAuthProvider  *provider);

G_END_DECLS

#endif /* __GOA_BACKEND_OAUTH_PROVIDER_H__ */
