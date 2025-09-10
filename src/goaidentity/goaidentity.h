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

#define GOA_TYPE_IDENTITY (goa_identity_get_type ())
G_DECLARE_INTERFACE (GoaIdentity, goa_identity, GOA, IDENTITY, GObject);

struct _GoaIdentityInterface
{
  GTypeInterface base_interface;

  const char * (* get_identifier)            (GoaIdentity *identity);
  gboolean     (* is_signed_in)              (GoaIdentity *identity);
};

typedef enum
{
  GOA_IDENTITY_SIGN_IN_FLAGS_NONE                        = 0,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_RENEWAL            = 1,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_FORWARDING         = 1 << 1,
  GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_PROXYING           = 1 << 2
} GoaIdentitySignInFlags;

const char  *goa_identity_get_identifier            (GoaIdentity *identity);
gboolean     goa_identity_is_signed_in              (GoaIdentity *identity);

G_END_DECLS

#endif /* __GOA_IDENTITY_H__ */
