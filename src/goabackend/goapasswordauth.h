/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2024 GNOME Foundation Inc.
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

#ifndef __GOA_PASSWORD_AUTH_H__
#define __GOA_PASSWORD_AUTH_H__

#include <glib-object.h>

#include "goaserviceauth.h"

G_BEGIN_DECLS

#define GOA_TYPE_PASSWORD_AUTH (goa_password_auth_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaPasswordAuth, goa_password_auth, GOA, PASSWORD_AUTH, GoaServiceAuth);

struct _GoaPasswordAuthClass
{
  GoaServiceAuthClass   parent_class;

  /* < private >*/
  gpointer              reserved[8];
};

GoaPasswordAuth     *goa_password_auth_new           (const char          *method,
                                                      const char          *username,
                                                      const char          *password);
const char          *goa_password_auth_get_username  (GoaPasswordAuth     *auth);
void                 goa_password_auth_set_username  (GoaPasswordAuth     *auth,
                                                      const char          *username);
const char          *goa_password_auth_get_password  (GoaPasswordAuth     *auth);
void                 goa_password_auth_set_password  (GoaPasswordAuth     *auth,
                                                      const char          *password);

G_END_DECLS

#endif /* __GOA_PASSWORD_AUTH_H__ */
