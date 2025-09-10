/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright © 2025 GNOME Foundation Inc.
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

#ifndef __GOA_IDENTITY_ERROR_H__
#define __GOA_IDENTITY_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

#define GOA_IDENTITY_ERROR (goa_identity_error_quark ())

#define GOA_IDENTITY_ERROR_NUM_ENTRIES (GOA_IDENTITY_ERROR_PARSING_IDENTIFIER + 1)

typedef enum
{
  GOA_IDENTITY_ERROR_NOT_FOUND,               /* org.gnome.Identity.NotFound */
  GOA_IDENTITY_ERROR_VERIFYING,               /* org.gnome.Identity.Verifying */
  GOA_IDENTITY_ERROR_RENEWING,                /* org.gnome.Identity.Renewing */
  GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE, /* org.gnome.Identity.CredentialsUnavailable */
  GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS, /* org.gnome.Identity.EnumeratingCredentials */
  GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,  /* org.gnome.Identity.AllocatingCredentials */
  GOA_IDENTITY_ERROR_AUTHENTICATION_FAILED,   /* org.gnome.Identity.AuthenticationFailed */
  GOA_IDENTITY_ERROR_SAVING_CREDENTIALS,      /* org.gnome.Identity.SavingCredentials */
  GOA_IDENTITY_ERROR_REMOVING_CREDENTIALS,    /* org.gnome.Identity.RemovingCredentials */
  GOA_IDENTITY_ERROR_PARSING_IDENTIFIER,      /* org.gnome.Identity.ParsingIdentifier */
} GoaIdentityError;

GQuark goa_identity_error_quark (void);

G_END_DECLS

#endif /* __GOA_IDENTITY_ERROR_H__ */
