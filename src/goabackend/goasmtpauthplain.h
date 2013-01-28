/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2013 Red Hat, Inc.
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
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Debarshi Ray <debarshir@gnome.org>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_SMTP_AUTH_PLAIN_H__
#define __GOA_SMTP_AUTH_PLAIN_H__

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include <goabackend/goabackendtypes.h>

#include "goamailauth.h"

G_BEGIN_DECLS

#define GOA_TYPE_SMTP_AUTH_PLAIN         (goa_smtp_auth_plain_get_type ())
#define GOA_SMTP_AUTH_PLAIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_SMTP_AUTH_PLAIN, GoaSmtpAuthPlain))
#define GOA_IS_SMTP_AUTH_PLAIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_SMTP_AUTH_PLAIN))

typedef struct _GoaSmtpAuthPlain GoaSmtpAuthPlain;

GType        goa_smtp_auth_plain_get_type  (void) G_GNUC_CONST;
GoaMailAuth *goa_smtp_auth_plain_new       (GoaProvider       *provider,
                                            GoaObject         *object,
                                            const gchar       *domain,
                                            const gchar       *user_name,
                                            const gchar       *password);

G_END_DECLS

#endif /* __GOA_SMTP_AUTH_PLAIN_H__ */
