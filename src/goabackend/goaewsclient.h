/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012, 2013, 2015 Red Hat, Inc.
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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_EWS_CLIENT_H__
#define __GOA_EWS_CLIENT_H__

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GOA_TYPE_EWS_CLIENT         (goa_ews_client_get_type ())
#define GOA_EWS_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_EWS_CLIENT, GoaEwsClient))
#define GOA_IS_EWS_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_EWS_CLIENT))

typedef struct _GoaEwsClient GoaEwsClient;

GType           goa_ews_client_get_type            (void) G_GNUC_CONST;
GoaEwsClient   *goa_ews_client_new                 (void);
void            goa_ews_client_autodiscover        (GoaEwsClient        *self,
                                                    const gchar         *email,
                                                    const gchar         *password,
                                                    const gchar         *username,
                                                    const gchar         *server,
                                                    gboolean             accept_ssl_errors,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             gpointer);
gboolean        goa_ews_client_autodiscover_finish (GoaEwsClient        *self,
                                                    GAsyncResult        *res,
                                                    GError             **error);
gboolean        goa_ews_client_autodiscover_sync   (GoaEwsClient        *self,
                                                    const gchar         *email,
                                                    const gchar         *password,
                                                    const gchar         *username,
                                                    const gchar         *server,
                                                    gboolean             accept_ssl_errors,
                                                    GCancellable        *cancellable,
                                                    GError             **error);

G_END_DECLS

#endif /* __GOA_EWS_CLIENT_H__ */
