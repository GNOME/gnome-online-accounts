/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2017 Red Hat, Inc.
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

#ifndef __GOA_IDENTITY_MANAGER_ERROR_H__
#define __GOA_IDENTITY_MANAGER_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

#define GOA_IDENTITY_MANAGER_ERROR (goa_identity_manager_error_quark ())

#define GOA_IDENTITY_MANAGER_ERROR_NUM_ENTRIES (GOA_IDENTITY_MANAGER_ERROR_UNSUPPORTED_CREDENTIALS + 1)

typedef enum
{
  GOA_IDENTITY_MANAGER_ERROR_INITIALIZING,           /* org.gnome.Identity.Manager.Error.Initializing */
  GOA_IDENTITY_MANAGER_ERROR_IDENTITY_NOT_FOUND,     /* org.gnome.Identity.Manager.Error.IdentityNotFound */
  GOA_IDENTITY_MANAGER_ERROR_CREATING_IDENTITY,      /* org.gnome.Identity.Manager.Error.CreatingIdentity */
  GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS,  /* org.gnome.Identity.Manager.Error.AccessingCredentials */
  GOA_IDENTITY_MANAGER_ERROR_UNSUPPORTED_CREDENTIALS /* org.gnome.Identity.Manager.Error.UnsupportedCredentials */
} GoaIdentityManagerError;

GQuark goa_identity_manager_error_quark (void);

G_END_DECLS

#endif /* __GOA_IDENTITY_MANAGER_ERROR_H__ */
