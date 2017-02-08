/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#ifndef __GOA_IDENTITY_SERVICE_H__
#define __GOA_IDENTITY_SERVICE_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "org.gnome.Identity.h"

G_BEGIN_DECLS
#define GOA_TYPE_IDENTITY_SERVICE           (goa_identity_service_get_type ())
#define GOA_IDENTITY_SERVICE(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GOA_TYPE_IDENTITY_SERVICE, GoaIdentityService))
#define GOA_IDENTITY_SERVICE_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GOA_TYPE_IDENTITY_SERVICE, GoaIdentityServiceClass))
#define GOA_IS_IDENTITY_SERVICE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GOA_TYPE_IDENTITY_SERVICE))
#define GOA_IS_IDENTITY_SERVICE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GOA_TYPE_IDENTITY_SERVICE))
#define GOA_IDENTITY_SERVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GOA_TYPE_IDENTITY_SERVICE, GoaIdentityServiceClass))
typedef struct _GoaIdentityService GoaIdentityService;
typedef struct _GoaIdentityServiceClass GoaIdentityServiceClass;
typedef struct _GoaIdentityServicePrivate GoaIdentityServicePrivate;

struct _GoaIdentityService
{
  GoaIdentityServiceManagerSkeleton  parent_instance;
  GoaIdentityServicePrivate         *priv;
};

struct _GoaIdentityServiceClass
{
  GoaIdentityServiceManagerSkeletonClass  parent_class;
};

GType goa_identity_service_get_type (void);
GoaIdentityService *goa_identity_service_new (void);
gboolean goa_identity_service_activate   (GoaIdentityService  *service,
                                          GError             **error);
void     goa_identity_service_deactivate (GoaIdentityService  *service);

G_END_DECLS
#endif /* __GOA_IDENTITY_SERVICE_H__ */
