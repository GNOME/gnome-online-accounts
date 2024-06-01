/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#ifndef __GOA_DAV_CLIENT_H__
#define __GOA_DAV_CLIENT_H__

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include "goabackendenums.h"
#include "goadavconfig.h"

G_BEGIN_DECLS

#define GOA_TYPE_DAV_CLIENT (goa_dav_client_get_type ())
G_DECLARE_FINAL_TYPE (GoaDavClient, goa_dav_client, GOA, DAV_CLIENT, GObject)

GoaDavClient        *goa_dav_client_new                        (void);
void                 goa_dav_client_check                      (GoaDavClient         *self,
                                                                GoaDavConfig         *config,
                                                                const char           *password,
                                                                gboolean              accept_ssl_errors,
                                                                GCancellable         *cancellable,
                                                                GAsyncReadyCallback   callback,
                                                                gpointer              user_data);
gboolean             goa_dav_client_check_finish               (GoaDavClient         *self,
                                                                GAsyncResult         *res,
                                                                GError              **error);
gboolean             goa_dav_client_check_sync                 (GoaDavClient         *self,
                                                                GoaDavConfig         *config,
                                                                const char           *password,
                                                                gboolean              accept_ssl_errors,
                                                                GCancellable         *cancellable,
                                                                GError              **error);
void                 goa_dav_client_discover                   (GoaDavClient         *self,
                                                                const char           *uri,
                                                                const char           *username,
                                                                const char           *password,
                                                                gboolean              accept_ssl_errors,
                                                                GCancellable         *cancellable,
                                                                GAsyncReadyCallback   callback,
                                                                gpointer              user_data);
GPtrArray           *goa_dav_client_discover_finish            (GoaDavClient         *self,
                                                                GAsyncResult         *res,
                                                                GError              **error);

G_END_DECLS

#endif /* __GOA_DAV_CLIENT_H__ */
