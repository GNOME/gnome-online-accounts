/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2014 Pranav Kant
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

#define GOA_DLNA_SERVER_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
   GOA_TYPE_DLNA_SERVER_MANAGER, GoaDlnaServerManager))

#define GOA_DLNA_SERVER_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
   GOA_TYPE_DLNA_SERVER_MANAGER, GoaDlnaServerManagerClass))

#define GOA_IS_DLNA_SERVER_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
   GOA_TYPE_DLNA_SERVER_MANAGER))

#define GOA_IS_DLNA_SERVER_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
   GOA_TYPE_DLNA_SERVER_MANAGER))

#define GOA_DLNA_SERVER_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
   GOA_TYPE_DLNA_SERVER_MANAGER, GoaDlnaServerManagerClass))

typedef struct _GoaDlnaServerManager        GoaDlnaServerManager;
typedef struct _GoaDlnaServerManagerClass   GoaDlnaServerManagerClass;
typedef struct _GoaDlnaServerManagerPrivate GoaDlnaServerManagerPrivate;

struct _GoaDlnaServerManager
{
  GObject parent_instance;
  GoaDlnaServerManagerPrivate *priv;
};

struct _GoaDlnaServerManagerClass
{
  GObjectClass parent_class;
};

GType                       goa_dlna_server_manager_get_type      (void) G_GNUC_CONST;

GoaDlnaServerManager       *goa_dlna_server_manager_dup_singleton (void);

GList                      *goa_dlna_server_manager_dup_servers   (GoaDlnaServerManager *self);

G_END_DECLS

#endif /* GOA_DLNA_SERVER_MANAGER_H */
