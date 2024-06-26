/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2013 – 2017 Red Hat, Inc.
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

#ifndef __GOA_PROVIDER_PRIV_H__
#define __GOA_PROVIDER_PRIV_H__

#include <gio/gio.h>
#include <goa/goa.h>
#include <goabackend/goaprovider.h>
#include <goabackend/goabackendenums.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * GoaProviderFeaturesInfo:
 *
 * Structure for metadata about a provider feature.
 */
typedef struct
{
  GoaProviderFeatures feature;
  const char *property;
  const char *blurb;
} GoaProviderFeaturesInfo;

GoaProviderFeaturesInfo *goa_provider_get_provider_features_infos (void);

/**
 * GoaProvider:
 *
 * The #GoaProvider structure contains only private data and should
 * only be accessed using the provided API.
 */

/**
 * GoaProviderClass:
 * @parent_class: The parent class.
 * @get_provider_type: Virtual function for goa_provider_get_provider_type().
 * @get_provider_name: Virtual function for goa_provider_get_provider_name().
 * @get_provider_icon: Virtual function for goa_provider_get_provider_icon().
 * @get_provider_group: Virtual function for goa_provider_get_provider_group().
 * @get_provider_features: Virtual function for goa_provider_get_provider_features().
 * @add_account: Virtual function for goa_provider_add_account().
 * @refresh_account: Virtual function for goa_provider_refresh_account().
 * @build_object: Virtual function for goa_provider_build_object().
 * @ensure_credentials_sync: Virtual function for goa_provider_ensure_credentials_sync().
 * @show_account: Virtual function for goa_provider_show_account().
 * @get_credentials_generation: Virtual function for goa_provider_get_credentials_generation().
 *
 * Class structure for #GoaProvider.
 */
struct _GoaProviderClass
{
  GObjectClass parent_class;

  /* pure virtual */
  void                    (*add_account)                  (GoaProvider            *self,
                                                           GoaClient              *client,
                                                           GtkWidget              *parent,
                                                           GCancellable           *cancellable,
                                                           GAsyncReadyCallback     callback,
                                                           gpointer                user_data);
  void                    (*refresh_account)              (GoaProvider            *self,
                                                           GoaClient              *client,
                                                           GoaObject              *object,
                                                           GtkWidget              *parent,
                                                           GCancellable           *cancellable,
                                                           GAsyncReadyCallback     callback,
                                                           gpointer                user_data);
  GoaProviderFeatures     (*get_provider_features)        (GoaProvider            *self);
  GoaProviderGroup        (*get_provider_group)           (GoaProvider            *self);
  gchar                  *(*get_provider_name)            (GoaProvider            *self,
                                                           GoaObject              *object);
  const gchar            *(*get_provider_type)            (GoaProvider            *self);

  /* virtual but with default implementation */
  gboolean                (*build_object)                 (GoaProvider            *self,
                                                           GoaObjectSkeleton      *object,
                                                           GKeyFile               *key_file,
                                                           const gchar            *group,
                                                           GDBusConnection        *connection,
                                                           gboolean                just_added,
                                                           GError                **error);
  gboolean                (*ensure_credentials_sync)      (GoaProvider            *self,
                                                           GoaObject              *object,
                                                           gint                   *out_expires_in,
                                                           GCancellable           *cancellable,
                                                           GError                **error);
  guint                   (*get_credentials_generation)   (GoaProvider            *self);
  GIcon                  *(*get_provider_icon)            (GoaProvider            *self,
                                                           GoaObject              *object);
  GoaObject              *(*add_account_finish)           (GoaProvider            *self,
                                                           GAsyncResult           *result,
                                                           GError                **error);
  gboolean                (*refresh_account_finish)       (GoaProvider            *self,
                                                           GAsyncResult           *result,
                                                           GError                **error);
  void                    (*remove_account)               (GoaProvider            *self,
                                                           GoaObject              *object,
                                                           GCancellable           *cancellable,
                                                           GAsyncReadyCallback     callback,
                                                           gpointer                user_data);
  gboolean                (*remove_account_finish)        (GoaProvider            *self,
                                                           GAsyncResult           *res,
                                                           GError                **error);
  void                    (*show_account)                 (GoaProvider            *self,
                                                           GoaClient              *client,
                                                           GoaObject              *object,
                                                           GtkWidget              *parent,
                                                           GCancellable           *cancellable,
                                                           GAsyncReadyCallback     callback,
                                                           gpointer                user_data);
  gboolean                (*show_account_finish)          (GoaProvider            *self,
                                                           GAsyncResult           *result,
                                                           GError                **error);
};

/**
 * GOA_PROVIDER_EXTENSION_POINT_NAME:
 *
 * Extension point for #GoaProvider implementations.
 */
#define GOA_PROVIDER_EXTENSION_POINT_NAME "goa-backend-provider"

void        goa_provider_ensure_builtins_loaded                (void);

void        goa_provider_ensure_extension_points_registered    (void);

gboolean    goa_provider_build_object                          (GoaProvider            *self,
                                                                GoaObjectSkeleton      *object,
                                                                GKeyFile               *key_file,
                                                                const gchar            *group,
                                                                GDBusConnection        *connection,
                                                                gboolean                just_added,
                                                                GError                **error);

void        goa_provider_ensure_credentials                    (GoaProvider             *self,
                                                                GoaObject               *object,
                                                                GCancellable            *cancellable,
                                                                GAsyncReadyCallback      callback,
                                                                gpointer                 user_data);

gboolean    goa_provider_ensure_credentials_finish             (GoaProvider             *self,
                                                                gint                    *out_expires_in,
                                                                GAsyncResult            *res,
                                                                GError                 **error);

gboolean    goa_provider_ensure_credentials_sync               (GoaProvider             *self,
                                                                GoaObject               *object,
                                                                gint                    *out_expires_in,
                                                                GCancellable            *cancellable,
                                                                GError                 **error);

void        goa_provider_remove_account                        (GoaProvider             *self,
                                                                GoaObject               *object,
                                                                GCancellable            *cancellable,
                                                                GAsyncReadyCallback      callback,
                                                                gpointer                 user_data);

gboolean    goa_provider_remove_account_finish                 (GoaProvider             *self,
                                                                GAsyncResult            *res,
                                                                GError                 **error);

G_END_DECLS

#endif /* __GOA_PROVIDER_PRIV_H__ */
