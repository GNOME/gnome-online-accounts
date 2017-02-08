/* -*- Mode: C; tab-width: 8; ident-tabs-mode: nil; c-basic-offset: 8 -*-
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

#ifndef __GOA_KERBEROS_IDENTITY_INQUIRY_H__
#define __GOA_KERBEROS_IDENTITY_INQUIRY_H__

#include <stdint.h>

#include <glib.h>
#include <glib-object.h>

#include "goaidentityinquiry.h"
#include "goakerberosidentity.h"

G_BEGIN_DECLS
#define GOA_TYPE_KERBEROS_IDENTITY_INQUIRY             (goa_kerberos_identity_inquiry_get_type ())
#define GOA_KERBEROS_IDENTITY_INQUIRY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_KERBEROS_IDENTITY_INQUIRY, GoaKerberosIdentityInquiry))
#define GOA_KERBEROS_IDENTITY_INQUIRY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_KERBEROS_IDENTITY_INQUIRY, GoaKerberosIdentityInquiryClass))
#define GOA_IS_KERBEROS_IDENTITY_INQUIRY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_KERBEROS_IDENTITY_INQUIRY))
#define GOA_IS_KERBEROS_IDENTITY_INQUIRY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GOA_TYPE_KERBEROS_IDENTITY_INQUIRY))
#define GOA_KERBEROS_IDENTITY_INQUIRY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GOA_TYPE_KERBEROS_IDENTITY_INQUIRY, GoaKerberosIdentityInquiryClass))
typedef struct _GoaKerberosIdentityInquiry GoaKerberosIdentityInquiry;
typedef struct _GoaKerberosIdentityInquiryClass GoaKerberosIdentityInquiryClass;
typedef struct _GoaKerberosIdentityInquiryPrivate GoaKerberosIdentityInquiryPrivate;
typedef struct _GoaKerberosIdentityInquiryIter GoaKerberosIdentityInquiryIter;

typedef enum
{
  GOA_KERBEROS_IDENTITY_QUERY_MODE_INVISIBLE,
  GOA_KERBEROS_IDENTITY_QUERY_MODE_VISIBLE
} GoaKerberosIdentityQueryMode;

struct _GoaKerberosIdentityInquiry
{
  GObject parent;

  GoaKerberosIdentityInquiryPrivate *priv;
};

struct _GoaKerberosIdentityInquiryClass
{
  GObjectClass parent_class;
};

GType goa_kerberos_identity_inquiry_get_type (void);

GoaIdentityInquiry *goa_kerberos_identity_inquiry_new (GoaKerberosIdentity *identity,
                                                       const char          *name,
                                                       const char          *banner,
                                                       krb5_prompt          prompts[],
                                                       int                  number_of_prompts);

#endif /* __GOA_KERBEROS_IDENTITY_INQUIRY_H__ */
