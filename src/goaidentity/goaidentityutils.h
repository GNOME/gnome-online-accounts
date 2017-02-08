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

#ifndef __GOA_IDENTITY_UTILS_H__
#define __GOA_IDENTITY_UTILS_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

void goa_identity_utils_split_identifier (const char  *identifier,
                                          char       **user,
                                          char       **domain);
char *goa_identity_utils_escape_object_path (const char *data,
                                             gsize       length);
void goa_identity_utils_register_error_domain (GQuark error_domain,
                                               GType  error_enum);

G_END_DECLS
#endif /* __GOA_IDENTITY_UTILS_H__ */
