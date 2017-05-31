/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 Pranav Kant
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>. *
 */

#ifndef GOA_DLNA_SERVER_MANAGER_H
#define GOA_DLNA_SERVER_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GOA_TYPE_DLNA_SERVER_MANAGER (goa_dlna_server_manager_get_type ())
G_DECLARE_FINAL_TYPE (GoaDlnaServerManager, goa_dlna_server_manager, GOA, DLNA_SERVER_MANAGER, GObject);

GoaDlnaServerManager       *goa_dlna_server_manager_dup_singleton (void);

GList                      *goa_dlna_server_manager_dup_servers   (GoaDlnaServerManager *self);

G_END_DECLS

#endif /* GOA_DLNA_SERVER_MANAGER_H */
