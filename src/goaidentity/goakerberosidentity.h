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

#ifndef __GOA_KERBEROS_IDENTITY_H__
#define __GOA_KERBEROS_IDENTITY_H__

#include <glib.h>
#include <glib-object.h>

#include <krb5.h>
#include "goaidentityinquiry.h"

G_BEGIN_DECLS

#define GOA_TYPE_KERBEROS_IDENTITY (goa_kerberos_identity_get_type ())
G_DECLARE_FINAL_TYPE (GoaKerberosIdentity, goa_kerberos_identity, GOA, KERBEROS_IDENTITY, GObject);

typedef enum
{
  GOA_KERBEROS_IDENTITY_DESCRIPTION_REALM,
  GOA_KERBEROS_IDENTITY_DESCRIPTION_USERNAME_AND_REALM,
  GOA_KERBEROS_IDENTITY_DESCRIPTION_USERNAME_ROLE_AND_REALM
} GoaKerberosIdentityDescriptionLevel;

GoaIdentity *goa_kerberos_identity_new (krb5_context   kerberos_context,
                                        krb5_ccache    cache,
                                        GError       **error);

gboolean goa_kerberos_identity_has_credentials_cache (GoaKerberosIdentity  *self,
                                                      krb5_ccache           credentials_cache);
void goa_kerberos_identity_add_credentials_cache (GoaKerberosIdentity  *self,
                                                  krb5_ccache           cache);

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
void goa_kerberos_identity_refresh (GoaKerberosIdentity *identity);
gboolean goa_kerberos_identity_renew (GoaKerberosIdentity  *self,
                                      GError              **error);
gboolean goa_kerberos_identity_erase (GoaKerberosIdentity *self,
                                      GError              **error);

char *goa_kerberos_identity_get_principal_name (GoaKerberosIdentity *self);
char *goa_kerberos_identity_get_realm_name     (GoaKerberosIdentity *self);
char *goa_kerberos_identity_get_preauthentication_source     (GoaKerberosIdentity *self);

G_END_DECLS

#endif /* __GOA_KERBEROS_IDENTITY_H__ */
