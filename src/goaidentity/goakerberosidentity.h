/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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

#ifndef __GOA_KERBEROS_IDENTITY_H__
#define __GOA_KERBEROS_IDENTITY_H__

#include <glib.h>
#include <glib-object.h>

#include <krb5.h>
#include "goaidentityinquiry.h"

G_BEGIN_DECLS
#define GOA_TYPE_KERBEROS_IDENTITY             (goa_kerberos_identity_get_type ())
#define GOA_KERBEROS_IDENTITY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_KERBEROS_IDENTITY, GoaKerberosIdentity))
#define GOA_KERBEROS_IDENTITY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_KERBEROS_IDENTITY, GoaKerberosIdentityClass))
#define GOA_IS_KERBEROS_IDENTITY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_KERBEROS_IDENTITY))
#define GOA_IS_KERBEROS_IDENTITY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GOA_TYPE_KERBEROS_IDENTITY))
#define GOA_KERBEROS_IDENTITY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GOA_TYPE_KERBEROS_IDENTITY, GoaKerberosIdentityClass))
typedef struct _GoaKerberosIdentity GoaKerberosIdentity;
typedef struct _GoaKerberosIdentityClass GoaKerberosIdentityClass;
typedef struct _GoaKerberosIdentityPrivate GoaKerberosIdentityPrivate;
typedef enum _GoaKerberosIdentityDescriptionLevel
  GoaKerberosIdentityDescriptionLevel;

enum _GoaKerberosIdentityDescriptionLevel
{
  GOA_KERBEROS_IDENTITY_DESCRIPTION_REALM,
  GOA_KERBEROS_IDENTITY_DESCRIPTION_USERNAME_AND_REALM,
  GOA_KERBEROS_IDENTITY_DESCRIPTION_USERNAME_ROLE_AND_REALM
};

struct _GoaKerberosIdentity
{
  GObject parent;

  GoaKerberosIdentityPrivate *priv;
};

struct _GoaKerberosIdentityClass
{
  GObjectClass parent_class;
};

GType goa_kerberos_identity_get_type (void);

GoaIdentity *goa_kerberos_identity_new (krb5_context   kerberos_context,
                                        krb5_ccache    cache,
                                        GError       **error);

gboolean goa_kerberos_identity_sign_in (GoaKerberosIdentity     *self,
                                        const char              *principal_name,
                                        gconstpointer            initial_password,
                                        const char              *preauth_source,
                                        GoaIdentitySignInFlags   flags,
                                        GoaIdentityInquiryFunc   inquiry_func,
                                        gpointer                 inquiry_data,
                                        GDestroyNotify           destroy_notify,
                                        GCancellable            *cancellable,
                                        GError                 **error);
void goa_kerberos_identity_update (GoaKerberosIdentity *identity,
                                   GoaKerberosIdentity *new_identity);
gboolean goa_kerberos_identity_renew (GoaKerberosIdentity  *self,
                                      GError              **error);
gboolean goa_kerberos_identity_erase (GoaKerberosIdentity *self,
                                      GError              **error);

char *goa_kerberos_identity_get_principal_name (GoaKerberosIdentity *self);
char *goa_kerberos_identity_get_realm_name     (GoaKerberosIdentity *self);
char *goa_kerberos_identity_get_preauthentication_source     (GoaKerberosIdentity *self);
G_END_DECLS
#endif /* __GOA_KERBEROS_IDENTITY_H__ */
