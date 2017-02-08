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

#ifndef __GOA_IDENTITY_MANAGER_PRIVATE_H__
#define __GOA_IDENTITY_MANAGER_PRIVATE_H__

#include <glib.h>
#include <glib-object.h>

#include "goaidentitymanager.h"

G_BEGIN_DECLS
void _goa_identity_manager_emit_identity_added (GoaIdentityManager *identity_manager,
                                                GoaIdentity        *identity);
void _goa_identity_manager_emit_identity_removed (GoaIdentityManager *identity_manager,
                                                  GoaIdentity        *identity);
void _goa_identity_manager_emit_identity_refreshed (GoaIdentityManager *identity_manager,
                                                    GoaIdentity        *identity);
void _goa_identity_manager_emit_identity_renamed (GoaIdentityManager *identity_manager,
                                                  GoaIdentity        *identity);

void _goa_identity_manager_emit_identity_expiring (GoaIdentityManager *identity_manager,
                                                   GoaIdentity        *identity);

void _goa_identity_manager_emit_identity_needs_renewal (GoaIdentityManager *identity_manager,
                                                        GoaIdentity        *identity);
void _goa_identity_manager_emit_identity_expired (GoaIdentityManager *identity_manager,
                                                  GoaIdentity        *identity);
G_END_DECLS
#endif /* __GOA_IDENTITY_MANAGER_PRIVATE_H__ */
