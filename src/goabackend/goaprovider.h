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

#ifndef __GOA_PROVIDER_H__
#define __GOA_PROVIDER_H__

#include <gtk/gtk.h>
#include <goa/goa.h>
#include <goabackend/goabackendenums.h>

G_BEGIN_DECLS

#define GOA_TYPE_PROVIDER         (goa_provider_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaProvider, goa_provider, GOA, PROVIDER, GObject);

typedef struct _GoaProviderPrivate GoaProviderPrivate;

const gchar           *goa_provider_get_provider_type            (GoaProvider            *self);

gchar                 *goa_provider_get_provider_name            (GoaProvider            *self,
                                                                  GoaObject              *object);

GIcon                 *goa_provider_get_provider_icon            (GoaProvider            *self,
                                                                  GoaObject              *object);

G_DEPRECATED_FOR(goa_provider_get_provider_features)
GoaProviderGroup       goa_provider_get_provider_group           (GoaProvider            *self);

GoaProviderFeatures    goa_provider_get_provider_features        (GoaProvider            *self);

G_DEPRECATED
void                   goa_provider_set_preseed_data             (GoaProvider            *self,
                                                                  GVariant               *preseed_data);

G_DEPRECATED
GVariant              *goa_provider_get_preseed_data             (GoaProvider            *self);

void                   goa_provider_add_account                  (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GtkWidget              *parent,
                                                                  GCancellable           *cancellable,
                                                                  GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);
GoaObject             *goa_provider_add_account_finish           (GoaProvider            *self,
                                                                  GAsyncResult           *result,
                                                                  GError                **error);

void                   goa_provider_refresh_account              (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GoaObject              *object,
                                                                  GtkWidget              *parent,
                                                                  GCancellable           *cancellable,
                                                                  GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);
gboolean               goa_provider_refresh_account_finish       (GoaProvider            *self,
                                                                  GAsyncResult           *result,
                                                                  GError                **error);

void                   goa_provider_show_account                 (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GoaObject              *object,
                                                                  GtkWidget              *parent,
                                                                  GCancellable           *cancellable,
                                                                  GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);
gboolean               goa_provider_show_account_finish          (GoaProvider            *self,
                                                                  GAsyncResult           *result,
                                                                  GError                **error);

guint                  goa_provider_get_credentials_generation   (GoaProvider            *self);

void                   goa_provider_get_all                      (GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);

gboolean               goa_provider_get_all_finish               (GList                 **out_providers,
                                                                  GAsyncResult           *result,
                                                                  GError                **error);

GoaProvider           *goa_provider_get_for_provider_type        (const gchar            *provider_type);

/* ---------------------------------------------------------------------------------------------------- */

gchar *
goa_util_lookup_keyfile_string (GoaObject    *object,
                                const gchar  *key);

gboolean
goa_util_lookup_keyfile_boolean (GoaObject    *object,
                                 const gchar  *key);

void
goa_util_account_notify_property_cb (GObject *object, GParamSpec *pspec, gpointer user_data);

GKeyFile *      goa_util_open_goa_conf               (void);
gboolean        goa_util_provider_feature_is_enabled (GKeyFile *goa_conf,
                                                      const gchar *provider_type,
                                                      GoaProviderFeatures feature);

G_END_DECLS

#endif /* __GOA_PROVIDER_H__ */
