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

#ifndef __GOA_SERVICE_AUTH_H__
#define __GOA_SERVICE_AUTH_H__

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GOA_AUTH_METHOD_PASSWORD_CLEARTEXT:
 *
 * Send password in the clear (dangerous, if SSL isn't used either). AUTH
 * PLAIN, LOGIN or protocol-native login.
 */
#define GOA_AUTH_METHOD_PASSWORD_CLEARTEXT "password-cleartext"

/**
 * GOA_AUTH_METHOD_PASSWORD_ENCRYPTED:
 *
 * A secure encrypted password mechanism. Can be CRAM-MD5 or DIGEST-MD5.
 * Not NTLM.
 */
#define GOA_AUTH_METHOD_PASSWORD_ENCRYPTED "password-encrypted"

/**
 * GOA_AUTH_METHOD_NTLM:
 *
 * Use NTLM (or NTLMv2 or successors), the Windows login mechanism.
 */
#define GOA_AUTH_METHOD_NTLM               "NTLM"

/**
 * GOA_AUTH_METHOD_GSSAPI:
 *
 * Use Kerberos / GSSAPI, a single-signon mechanism used for big sites.
 */
#define GOA_AUTH_METHOD_GSSAPI             "GSSAPI"

/**
 * GOA_AUTH_METHOD_CLIENT_IP_ADDRESS:
 *
 * The server recognizes this user based on the IP address. No authentication
 * needed, the server will require no username nor password.
 */
#define GOA_AUTH_METHOD_CLIENT_IP_ADDRESS  "client-IP-address"

/**
 * GOA_AUTH_METHOD_TLS_CLIENT_CERT:
 *
 * On the SSL/TLS layer, the server requests a client certificate and the
 * client sends one (possibly after letting the user select/confirm one),
 * if available.
 */
#define GOA_AUTH_METHOD_TLS_CLIENT_CERT    "TLS-client-cert"

/**
 * GOA_AUTH_METHOD_OAUTH2:
 *
 * mAuth. Should be added only as second alternative.
 */
#define GOA_AUTH_METHOD_OAUTH2             "OAuth2"

/**
 * GOA_AUTH_METHOD_NONE:
 *
 * No authentication.
 */
#define GOA_AUTH_METHOD_NONE               "none"


#define GOA_TYPE_SERVICE_AUTH (goa_service_auth_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaServiceAuth, goa_service_auth, GOA, SERVICE_AUTH, GObject);

struct _GoaServiceAuthClass
{
  GObjectClass          parent_class;

  /* < private >*/
  gpointer              reserved[8];
};

const char           *goa_service_auth_get_method      (GoaServiceAuth       *auth);

G_END_DECLS

#endif /* __GOA_SERVICE_AUTH_H__ */
