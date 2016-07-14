/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012, 2015 Red Hat, Inc.
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
#include <goabackend/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_PROVIDER         (goa_provider_get_type ())
#define GOA_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_PROVIDER, GoaProvider))
#define GOA_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_PROVIDER, GoaProviderClass))
#define GOA_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_PROVIDER, GoaProviderClass))
#define GOA_IS_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_PROVIDER))
#define GOA_IS_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_PROVIDER))

typedef struct _GoaProviderClass GoaProviderClass;
typedef struct _GoaProviderPrivate GoaProviderPrivate;

GType                  goa_provider_get_type                     (void) G_GNUC_CONST;

const gchar           *goa_provider_get_provider_type            (GoaProvider            *self);

gchar                 *goa_provider_get_provider_name            (GoaProvider            *self,
                                                                  GoaObject              *object);

GIcon                 *goa_provider_get_provider_icon            (GoaProvider            *self,
                                                                  GoaObject              *object);

G_DEPRECATED_FOR(goa_provider_get_provider_features)
GoaProviderGroup       goa_provider_get_provider_group           (GoaProvider            *self);

GoaProviderFeatures    goa_provider_get_provider_features        (GoaProvider            *self);

void                   goa_provider_set_preseed_data             (GoaProvider            *self,
                                                                  GVariant               *preseed_data);

GVariant              *goa_provider_get_preseed_data             (GoaProvider            *self);

GoaObject             *goa_provider_add_account                  (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GtkDialog              *dialog,
                                                                  GtkBox                 *vbox,
                                                                  GError                **error);

gboolean               goa_provider_refresh_account              (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GoaObject              *object,
                                                                  GtkWindow              *parent,
                                                                  GError                **error);

void                   goa_provider_show_account                 (GoaProvider            *self,
                                                                  GoaClient              *client,
                                                                  GoaObject              *object,
                                                                  GtkBox                 *vbox,
                                                                  GtkGrid                *grid,
                                                                  GtkGrid                *dummy);

gboolean               goa_provider_build_object                 (GoaProvider            *self,
                                                                  GoaObjectSkeleton      *object,
                                                                  GKeyFile               *key_file,
                                                                  const gchar            *group,
                                                                  GDBusConnection        *connection,
                                                                  gboolean                just_added,
                                                                  GError                **error);

void                   goa_provider_ensure_credentials           (GoaProvider            *self,
                                                                  GoaObject              *object,
                                                                  GCancellable           *cancellable,
                                                                  GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);

gboolean               goa_provider_ensure_credentials_finish    (GoaProvider            *self,
                                                                  gint                   *out_expires_in,
                                                                  GAsyncResult           *res,
                                                                  GError                **error);

gboolean               goa_provider_ensure_credentials_sync      (GoaProvider            *self,
                                                                  GoaObject              *object,
                                                                  gint                   *out_expires_in,
                                                                  GCancellable           *cancellable,
                                                                  GError                **error);

guint                  goa_provider_get_credentials_generation   (GoaProvider            *self);

void                   goa_provider_get_all                      (GAsyncReadyCallback     callback,
                                                                  gpointer                user_data);

gboolean               goa_provider_get_all_finish               (GList                 **out_providers,
                                                                  GAsyncResult           *result,
                                                                  GError                **error);

GoaProvider           *goa_provider_get_for_provider_type        (const gchar            *provider_type);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *goa_util_add_row_widget (GtkGrid      *grid,
                                    gint          row,
                                    const gchar  *label_text,
                                    GtkWidget    *widget);

gchar *
goa_util_lookup_keyfile_string (GoaObject    *object,
                                const gchar  *key);

gboolean
goa_util_lookup_keyfile_boolean (GoaObject    *object,
                                 const gchar  *key);

void
goa_util_account_notify_property_cb (GObject *object, GParamSpec *pspec, gpointer user_data);

void       goa_util_add_account_info (GtkGrid *grid, gint row, GoaObject *object);

GtkWidget *goa_util_add_row_switch_from_keyfile_with_blurb (GtkGrid      *grid,
                                                            gint          row,
                                                            GoaObject    *object,
                                                            const gchar  *label_text,
                                                            const gchar  *key,
                                                            const gchar  *blurb);

G_END_DECLS

#endif /* __GOA_PROVIDER_H__ */
