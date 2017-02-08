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

#ifndef __GOA_KERBEROS_IDENTITY_MANAGER_H__
#define __GOA_KERBEROS_IDENTITY_MANAGER_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "goaidentitymanager.h"
#include "goakerberosidentity.h"

G_BEGIN_DECLS
#define GOA_TYPE_KERBEROS_IDENTITY_MANAGER           (goa_kerberos_identity_manager_get_type ())
#define GOA_KERBEROS_IDENTITY_MANAGER(obj)           (G_TYPE_CHECK_INSTANCE_CAST (obj, GOA_TYPE_KERBEROS_IDENTITY_MANAGER, GoaKerberosIdentityManager))
#define GOA_KERBEROS_IDENTITY_MANAGER_CLASS(cls)     (G_TYPE_CHECK_CLASS_CAST (cls, GOA_TYPE_KERBEROS_IDENTITY_MANAGER, GoaKerberosIdentityManagerClass))
#define GOA_IS_KERBEROS_IDENTITY_MANAGER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE (obj, GOA_TYPE_KERBEROS_IDENTITY_MANAGER))
#define GOA_IS_KERBEROS_IDENTITY_MANAGER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE (obj, GOA_TYPE_KERBEROS_IDENTITY_MANAGER))
#define GOA_KERBEROS_IDENTITY_MANAGER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GOA_TYPE_KERBEROS_IDENTITY_MANAGER, GoaKerberosIdentityManagerClass))
typedef struct _GoaKerberosIdentityManager GoaKerberosIdentityManager;
typedef struct _GoaKerberosIdentityManagerClass GoaKerberosIdentityManagerClass;
typedef struct _GoaKerberosIdentityManagerPrivate GoaKerberosIdentityManagerPrivate;
struct _GoaKerberosIdentityManager
{
  GObject parent_instance;
  GoaKerberosIdentityManagerPrivate *priv;
};

struct _GoaKerberosIdentityManagerClass
{
  GObjectClass parent_class;
};

GType goa_kerberos_identity_manager_get_type (void);
GoaIdentityManager *goa_kerberos_identity_manager_new (GCancellable  *cancellable,
                                                       GError       **error);

void goa_kerberos_identity_manager_start_test (GoaKerberosIdentityManager  *manager,
                                               GError                     **error);
G_END_DECLS
#endif /* __GOA_KERBEROS_IDENTITY_MANAGER_H__ */
