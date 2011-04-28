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

#ifndef __GOA_BACKEND_OAUTH2_PROVIDER_H__
#define __GOA_BACKEND_OAUTH2_PROVIDER_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_OAUTH2_PROVIDER         (goa_backend_oauth2_provider_get_type ())
#define GOA_BACKEND_OAUTH2_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_BACKEND_OAUTH2_PROVIDER, GoaBackendOAuth2Provider))
#define GOA_BACKEND_OAUTH2_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_BACKEND_OAUTH2_PROVIDER, GoaBackendOAuth2ProviderClass))
#define GOA_BACKEND_OAUTH2_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_BACKEND_OAUTH2_PROVIDER, GoaBackendOAuth2ProviderClass))
#define GOA_IS_BACKEND_OAUTH2_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_BACKEND_OAUTH2_PROVIDER))

#define GOA_IS_BACKEND_OAUTH2_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_BACKEND_OAUTH2_PROVIDER))

typedef struct _GoaBackendOAuth2ProviderClass GoaBackendOAuth2ProviderClass;
typedef struct _GoaBackendOAuth2ProviderPrivate GoaBackendOAuth2ProviderPrivate;

/**
 * GoaBackendOAuth2Provider:
 *
 * The #GoaBackendOAuth2Provider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendOAuth2Provider
{
  /*< private >*/
  GoaBackendProvider parent_instance;
  GoaBackendOAuth2ProviderPrivate *priv;
};

/**
 * GoaBackendOAuth2ProviderClass:
 * @parent_class: The parent class.
 * @get_dialog_uri: Virtual function for goa_backend_oauth2_provider_get_dialog_uri().
 * @get_authorization_uri: Virtual function for goa_backend_oauth2_provider_get_authorization_uri().
 * @get_redirect_uri: Virtual function for goa_backend_oauth2_provider_get_redirect_uri().
 * @get_scope: Virtual function for goa_backend_oauth2_provider_get_scope().
 * @get_client_id: Virtual function for goa_backend_oauth2_provider_get_client_id().
 * @get_client_secret: Virtual function for goa_backend_oauth2_provider_get_client_secret().
 * @get_identity_sync: Virtual function for goa_backend_oauth2_provider_get_identity_sync().
 * @get_identity_from_object: Virtual function for goa_backend_oauth2_provider_get_identity_from_object().
 *
 * Class structure for #GoaBackendOAuth2Provider.
 */
struct _GoaBackendOAuth2ProviderClass
{
  GoaBackendProviderClass parent_class;

  const gchar *(*get_dialog_uri)        (GoaBackendOAuth2Provider *provider);
  const gchar *(*get_authorization_uri) (GoaBackendOAuth2Provider *provider);
  const gchar *(*get_redirect_uri)      (GoaBackendOAuth2Provider *provider);
  const gchar *(*get_scope)             (GoaBackendOAuth2Provider *provider);
  const gchar *(*get_client_id)         (GoaBackendOAuth2Provider *provider);
  const gchar *(*get_client_secret)     (GoaBackendOAuth2Provider *provider);

  gchar *(*get_identity_sync)   (GoaBackendOAuth2Provider *provider,
                                 const gchar              *access_token,
                                 GCancellable             *cancellable,
                                 GError                   **error);

  const gchar *(*get_identity_from_object) (GoaBackendOAuth2Provider *provider,
                                            GoaObject                *object);
};

GType        goa_backend_oauth2_provider_get_type                 (void) G_GNUC_CONST;
const gchar *goa_backend_oauth2_provider_get_dialog_uri           (GoaBackendOAuth2Provider  *provider);
const gchar *goa_backend_oauth2_provider_get_authorization_uri    (GoaBackendOAuth2Provider  *provider);
const gchar *goa_backend_oauth2_provider_get_redirect_uri         (GoaBackendOAuth2Provider  *provider);
const gchar *goa_backend_oauth2_provider_get_scope                (GoaBackendOAuth2Provider  *provider);
const gchar *goa_backend_oauth2_provider_get_client_id            (GoaBackendOAuth2Provider  *provider);
const gchar *goa_backend_oauth2_provider_get_client_secret        (GoaBackendOAuth2Provider  *provider);
gchar       *goa_backend_oauth2_provider_get_identity_sync        (GoaBackendOAuth2Provider  *provider,
                                                                   const gchar               *access_token,
                                                                   GCancellable              *cancellable,
                                                                   GError                   **error);
const gchar *goa_backend_oauth2_provider_get_identity_from_object (GoaBackendOAuth2Provider *provider,
                                                                   GoaObject                *object);
void         goa_backend_oauth2_provider_get_access_token         (GoaBackendOAuth2Provider   *provider,
                                                                   GoaObject                  *object,
                                                                   GCancellable               *cancellable,
                                                                   GAsyncReadyCallback         callback,
                                                                   gpointer                    user_data);
gchar       *goa_backend_oauth2_provider_get_access_token_finish (GoaBackendOAuth2Provider    *provider,
                                                                  gint                        *out_expires_in,
                                                                  GAsyncResult                *res,
                                                                  GError                     **error);

G_END_DECLS

#endif /* __GOA_BACKEND_OAUTH2_PROVIDER_H__ */
