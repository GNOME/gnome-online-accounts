/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2013, 2015 Red Hat, Inc.
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

#ifndef __GOA_MAIL_CLIENT_H__
#define __GOA_MAIL_CLIENT_H__

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include "goabackendenums-priv.h"
#include "goamailauth.h"

G_BEGIN_DECLS

#define GOA_TYPE_MAIL_CLIENT         (goa_mail_client_get_type ())
#define GOA_MAIL_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_MAIL_CLIENT, GoaMailClient))
#define GOA_IS_MAIL_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_MAIL_CLIENT))

typedef struct _GoaMailClient GoaMailClient;

GType           goa_mail_client_get_type          (void) G_GNUC_CONST;
GoaMailClient  *goa_mail_client_new               (void);
void            goa_mail_client_check             (GoaMailClient        *self,
                                                   const gchar          *host_and_port,
                                                   GoaTlsType            tls_type,
                                                   gboolean              accept_ssl_errors,
                                                   guint16               default_port,
                                                   GoaMailAuth          *auth,
                                                   GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean        goa_mail_client_check_finish      (GoaMailClient        *self,
                                                   GAsyncResult         *res,
                                                   GError              **error);
gboolean        goa_mail_client_check_sync        (GoaMailClient        *self,
                                                   const gchar          *host_and_port,
                                                   GoaTlsType            tls_type,
                                                   gboolean              accept_ssl_errors,
                                                   guint16               default_port,
                                                   GoaMailAuth          *auth,
                                                   GCancellable         *cancellable,
                                                   GError              **error);

G_END_DECLS

#endif /* __GOA_MAIL_CLIENT_H__ */
