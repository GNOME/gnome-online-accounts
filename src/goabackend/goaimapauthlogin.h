/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#ifndef __GOA_IMAP_AUTH_LOGIN_H__
#define __GOA_IMAP_AUTH_LOGIN_H__

#include <glib.h>
#include <glib-object.h>

#include "goamailauth.h"

G_BEGIN_DECLS

#define GOA_TYPE_IMAP_AUTH_LOGIN (goa_imap_auth_login_get_type ())
G_DECLARE_FINAL_TYPE (GoaImapAuthLogin, goa_imap_auth_login, GOA, IMAP_AUTH_LOGIN, GoaMailAuth);

GoaMailAuth *goa_imap_auth_login_new       (const gchar       *user_name,
                                            const gchar       *password);

G_END_DECLS

#endif /* __GOA_IMAP_AUTH_LOGIN_H__ */
