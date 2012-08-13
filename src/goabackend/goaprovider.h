/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Debarshi Ray <debarshir@gnome.org>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_PROVIDER_H__
#define __GOA_PROVIDER_H__

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

/**
 * GoaProvider:
 *
 * The #GoaProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaProvider
{
  /*< private >*/
  GObject parent_instance;
  GoaProviderPrivate *priv;
};

/**
 * GoaProviderClass:
 * @parent_class: The parent class.
 * @get_provider_type: Virtual function for goa_provider_get_provider_type().
 * @get_provider_name: Virtual function for goa_provider_get_provider_name().
 * @get_provider_icon: Virtual function for goa_provider_get_provider_icon().
 * @add_account: Virtual function for goa_provider_add_account().
 * @refresh_account: Virtual function for goa_provider_refresh_account().
 * @build_object: Virtual function for goa_provider_build_object().
 * @ensure_credentials_sync: Virtual function for goa_provider_ensure_credentials_sync().
 * @get_credentials_generation: Virtual function for goa_provider_get_credentials_generation().
 *
 * Class structure for #GoaProvider.
 */
struct _GoaProviderClass
{
  GObjectClass parent_class;

  /* pure virtual */
  const gchar *(*get_provider_type) (GoaProvider        *provider);
  gchar       *(*get_provider_name) (GoaProvider        *provider,
                                     GoaObject          *object);
  GIcon       *(*get_provider_icon) (GoaProvider        *provider,
                                     GoaObject          *object);
  GoaObject   *(*add_account)       (GoaProvider        *provider,
                                     GoaClient          *client,
                                     GtkDialog          *dialog,
                                     GtkBox             *vbox,
                                     GError            **error);
  gboolean     (*refresh_account)   (GoaProvider        *provider,
                                     GoaClient          *client,
                                     GoaObject          *object,
                                     GtkWindow          *parent,
                                     GError            **error);
  gboolean     (*build_object)      (GoaProvider        *provider,
                                     GoaObjectSkeleton  *object,
                                     GKeyFile           *key_file,
                                     const gchar        *group,
                                     GDBusConnection    *connection,
                                     gboolean            just_added,
                                     GError            **error);
  /* virtual but with default implementation */
  gboolean (*ensure_credentials_sync) (GoaProvider         *provider,
                                       GoaObject           *object,
                                       gint                *out_expires_in,
                                       GCancellable        *cancellable,
                                       GError             **error);
  void     (*show_account)            (GoaProvider         *provider,
                                       GoaClient           *client,
                                       GoaObject           *object,
                                       GtkBox              *vbox,
                                       GtkGrid             *left,
                                       GtkGrid             *right);
  guint    (*get_credentials_generation) (GoaProvider   *provider);

  /*< private >*/
  /* Padding for future expansion */
  gpointer goa_reserved[32];
};

GType        goa_provider_get_type                  (void) G_GNUC_CONST;
const gchar *goa_provider_get_provider_type         (GoaProvider         *provider);
gchar       *goa_provider_get_provider_name         (GoaProvider         *provider,
                                                     GoaObject           *object);
GIcon       *goa_provider_get_provider_icon         (GoaProvider         *provider,
                                                     GoaObject           *object);
GoaObject   *goa_provider_add_account               (GoaProvider         *provider,
                                                     GoaClient           *client,
                                                     GtkDialog           *dialog,
                                                     GtkBox              *vbox,
                                                     GError             **error);
gboolean     goa_provider_refresh_account           (GoaProvider         *provider,
                                                     GoaClient           *client,
                                                     GoaObject           *object,
                                                     GtkWindow           *parent,
                                                     GError             **error);
void         goa_provider_show_account              (GoaProvider         *provider,
                                                     GoaClient           *client,
                                                     GoaObject           *object,
                                                     GtkBox              *vbox,
                                                     GtkGrid             *left,
                                                     GtkGrid             *right);
gboolean     goa_provider_build_object              (GoaProvider         *provider,
                                                     GoaObjectSkeleton   *object,
                                                     GKeyFile            *key_file,
                                                     const gchar         *group,
                                                     GDBusConnection     *connection,
                                                     gboolean             just_added,
                                                     GError             **error);
void         goa_provider_ensure_credentials        (GoaProvider         *provider,
                                                     GoaObject           *object,
                                                     GCancellable        *cancellable,
                                                     GAsyncReadyCallback  callback,
                                                     gpointer             user_data);
gboolean     goa_provider_ensure_credentials_finish (GoaProvider         *provider,
                                                     gint                *out_expires_in,
                                                     GAsyncResult        *res,
                                                     GError             **error);
gboolean     goa_provider_ensure_credentials_sync   (GoaProvider         *provider,
                                                     GoaObject           *object,
                                                     gint                *out_expires_in,
                                                     GCancellable        *cancellable,
                                                     GError             **error);
guint        goa_provider_get_credentials_generation (GoaProvider        *provider);


/**
 * GOA_PROVIDER_EXTENSION_POINT_NAME:
 *
 * Extension point for #GoaProvider implementations.
 */
#define GOA_PROVIDER_EXTENSION_POINT_NAME "goa-backend-provider"

GList        *goa_provider_get_all (void);
GoaProvider  *goa_provider_get_for_provider_type (const gchar *provider_type);

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *goa_util_add_row_widget (GtkGrid      *left,
                                    GtkGrid      *right,
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

void       goa_util_add_account_info (GtkGrid *left, GtkGrid *right, GoaObject *object);

GtkWidget *goa_util_add_row_switch_from_keyfile_with_blurb (GtkGrid      *left,
                                                            GtkGrid      *right,
                                                            GoaObject    *object,
                                                            const gchar  *label_text,
                                                            const gchar  *key,
                                                            const gchar  *blurb);

G_END_DECLS

#endif /* __GOA_PROVIDER_H__ */
