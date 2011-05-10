/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goa/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_IMAP_CLIENT_H__
#define __GOA_BACKEND_IMAP_CLIENT_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_IMAP_CLIENT         (goa_backend_imap_client_get_type ())
#define GOA_BACKEND_IMAP_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_BACKEND_IMAP_CLIENT, GoaBackendImapClient))
#define GOA_IS_BACKEND_IMAP_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_BACKEND_IMAP_CLIENT))

GType                  goa_backend_imap_client_get_type         (void) G_GNUC_CONST;
void                   goa_backend_imap_client_new              (const gchar           *host_and_port,
                                                                 gboolean               use_tls,
                                                                 GoaBackendImapAuth    *auth,
                                                                 const gchar           *criteria,
                                                                 guint                  query_offset,
                                                                 guint                  query_size,
                                                                 GCancellable          *cancellable,
                                                                 GAsyncReadyCallback    callback,
                                                                 gpointer               user_data);
GoaBackendImapClient  *goa_backend_imap_client_new_finish       (GAsyncResult          *res,
                                                                 GError               **error);
GoaBackendImapClient  *goa_backend_imap_client_new_sync         (const gchar           *host_and_port,
                                                                 gboolean               use_tls,
                                                                 GoaBackendImapAuth    *auth,
                                                                 const gchar           *criteria,
                                                                 guint                  query_offset,
                                                                 guint                  query_size,
                                                                 GCancellable          *cancellable,
                                                                 GError               **error);

gboolean               goa_backend_imap_client_get_closed       (GoaBackendImapClient  *client);
void                   goa_backend_imap_client_close            (GoaBackendImapClient  *client);

gint                   goa_backend_imap_client_get_num_unread   (GoaBackendImapClient  *client);
gint                   goa_backend_imap_client_get_num_messages (GoaBackendImapClient  *client);
GList                 *goa_backend_imap_client_get_messages     (GoaBackendImapClient  *client);

void                   goa_backend_imap_client_refresh          (GoaBackendImapClient  *client,
                                                                 GCancellable          *cancellable,
                                                                 GAsyncReadyCallback    callback,
                                                                 gpointer               user_data);
gboolean               goa_backend_imap_client_refresh_finish   (GoaBackendImapClient  *client,
                                                                 GAsyncResult          *res,
                                                                 GError               **error);
gboolean               goa_backend_imap_client_refresh_sync     (GoaBackendImapClient  *client,
                                                                 GCancellable          *cancellable,
                                                                 GError               **error);


G_END_DECLS

#endif /* __GOA_BACKEND_IMAP_CLIENT_H__ */
