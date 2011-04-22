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

#ifndef __GOA_BACKEND_PROVIDER_H__
#define __GOA_BACKEND_PROVIDER_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_PROVIDER         (goa_backend_provider_get_type ())
#define GOA_BACKEND_PROVIDER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_BACKEND_PROVIDER, GoaBackendProvider))
#define GOA_BACKEND_PROVIDER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_BACKEND_PROVIDER, GoaBackendProviderClass))
#define GOA_BACKEND_PROVIDER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_BACKEND_PROVIDER, GoaBackendProviderClass))
#define GOA_IS_BACKEND_PROVIDER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_BACKEND_PROVIDER))
#define GOA_IS_BACKEND_PROVIDER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_BACKEND_PROVIDER))

typedef struct _GoaBackendProviderClass GoaBackendProviderClass;
typedef struct _GoaBackendProviderPrivate GoaBackendProviderPrivate;

/**
 * GoaBackendProvider:
 *
 * The #GoaBackendProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendProvider
{
  /*< private >*/
  GObject parent_instance;
  GoaBackendProviderPrivate *priv;
};

/**
 * GoaBackendProviderClass:
 * @parent_class: The parent class.
 * @get_provider_type: Virtual function for goa_backend_provider_get_provider_type().
 *
 * Class structure for #GoaBackendProvider.
 */
struct _GoaBackendProviderClass
{
  GObjectClass parent_class;

  const gchar *(*get_provider_type) (GoaBackendProvider *provider);

  /*< private >*/
  /* Padding for future expansion */
  gpointer goa_reserved[32];
};

GType               goa_backend_provider_get_type         (void) G_GNUC_CONST;
const gchar        *goa_backend_provider_get_provider_type (GoaBackendProvider *provider);

/**
 * GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME:
 *
 * Extension point for #GoaBackendProvider implementations.
 */
#define GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME "goa-backend-provider"

GoaBackendProvider  *goa_backend_provider_get_for_provider_type (const gchar *provider_type);

G_END_DECLS

#endif /* __GOA_BACKEND_PROVIDER_H__ */
