/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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

#if !defined (__GOA_INSIDE_GOA_H__) && !defined (GOA_COMPILATION)
#error "Only <goa/goa.h> can be included directly."
#endif

#ifndef __GOA_CLIENT_H__
#define __GOA_CLIENT_H__

#include <goa/goatypes.h>
#include <goa/goa-generated.h>

G_BEGIN_DECLS

#define GOA_TYPE_CLIENT  (goa_client_get_type ())
#define GOA_CLIENT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_CLIENT, GoaClient))
#define GOA_IS_CLIENT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_CLIENT))

GType               goa_client_get_type           (void) G_GNUC_CONST;
void                goa_client_new                (GCancellable        *cancellable,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);
GoaClient          *goa_client_new_finish         (GAsyncResult        *res,
                                                   GError             **error);
GoaClient          *goa_client_new_sync           (GCancellable        *cancellable,
                                                   GError             **error);
GDBusObjectManager *goa_client_get_object_manager (GoaClient           *client);
GoaManager         *goa_client_get_manager        (GoaClient           *client);
GList              *goa_client_get_accounts       (GoaClient           *client);
GoaObject          *goa_client_lookup_by_id       (GoaClient           *client,
                                                   const gchar         *id);


G_END_DECLS

#endif /* __GOA_CLIENT_H__ */
