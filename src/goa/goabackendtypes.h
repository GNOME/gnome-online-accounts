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

#ifndef __GOA_BACKEND_TYPES_H__
#define __GOA_BACKEND_TYPES_H__

#include <goa/goa.h>
#include <goa/goabackendenums.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

struct _GoaBackendProvider;
typedef struct _GoaBackendProvider GoaBackendProvider;

struct _GoaBackendOAuthProvider;
typedef struct _GoaBackendOAuthProvider GoaBackendOAuthProvider;

struct _GoaBackendOAuth2Provider;
typedef struct _GoaBackendOAuth2Provider GoaBackendOAuth2Provider;

struct _GoaBackendGoogleProvider;
typedef struct _GoaBackendGoogleProvider GoaBackendGoogleProvider;

struct _GoaBackendFacebookProvider;
typedef struct _GoaBackendFacebookProvider GoaBackendFacebookProvider;

struct _GoaBackendYahooProvider;
typedef struct _GoaBackendYahooProvider GoaBackendYahooProvider;

struct _GoaBackendTwitterProvider;
typedef struct _GoaBackendTwitterProvider GoaBackendTwitterProvider;

struct _GoaBackendImapAuth;
typedef struct _GoaBackendImapAuth GoaBackendImapAuth;

struct _GoaBackendImapAuthOAuth;
typedef struct _GoaBackendImapAuthOAuth GoaBackendImapAuthOAuth;

struct _GoaBackendImapClient;
typedef struct _GoaBackendImapClient GoaBackendImapClient;

struct _GoaBackendImapMessage;
typedef struct _GoaBackendImapMessage GoaBackendImapMessage;

struct _GoaBackendImapMail;
typedef struct _GoaBackendImapMail GoaBackendImapMail;

G_END_DECLS

#endif /* __GOA_BACKEND_TYPES_H__ */
