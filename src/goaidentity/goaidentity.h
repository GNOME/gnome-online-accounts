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

#ifndef __GOA_IDENTITY_H__
#define __GOA_IDENTITY_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS
#define GOA_TYPE_IDENTITY             (goa_identity_get_type ())
#define GOA_IDENTITY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_IDENTITY, GoaIdentity))
#define GOA_IDENTITY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_IDENTITY, GoaIdentityInterface))
#define GOA_IS_IDENTITY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_IDENTITY))
#define GOA_IDENTITY_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE((obj), GOA_TYPE_IDENTITY, GoaIdentityInterface))
#define GOA_IDENTITY_ERROR            (goa_identity_error_quark ())
typedef struct _GoaIdentity GoaIdentity;
typedef struct _GoaIdentityInterface GoaIdentityInterface;

struct _GoaIdentityInterface
{
  GTypeInterface base_interface;

  const char * (* get_identifier)            (GoaIdentity *identity);
  gboolean     (* is_signed_in)              (GoaIdentity *identity);
};

typedef enum
{
  GOA_IDENTITY_ERROR_NOT_FOUND,
  GOA_IDENTITY_ERROR_VERIFYING,
  GOA_IDENTITY_ERROR_RENEWING,
  GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,
  GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS,
  GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,
  GOA_IDENTITY_ERROR_AUTHENTICATION_FAILED,
  GOA_IDENTITY_ERROR_SAVING_CREDENTIALS,
  GOA_IDENTITY_ERROR_REMOVING_CREDENTIALS,
  GOA_IDENTITY_ERROR_PARSING_IDENTIFIER,
} GoaIdentityError;

typedef enum
{
  GOA_IDENTITY_SIGN_IN_FLAGS_NONE                        = 0,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_RENEWAL            = 1,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_FORWARDING         = 1 << 1,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_PROXYING           = 1 << 2
} GoaIdentitySignInFlags;

GType  goa_identity_get_type    (void);
GQuark goa_identity_error_quark (void);

const char  *goa_identity_get_identifier            (GoaIdentity *identity);
gboolean     goa_identity_is_signed_in              (GoaIdentity *identity);


G_END_DECLS
#endif /* __GOA_IDENTITY_H__ */
