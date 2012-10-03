/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_TYPES_H__
#define __GOA_BACKEND_TYPES_H__

#include <goa/goa.h>
#include <goabackend/goabackendenums.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

struct _GoaEwsClient;
typedef struct _GoaEwsClient GoaEwsClient;

struct _GoaHttpClient;
typedef struct _GoaHttpClient GoaHttpClient;

struct _GoaProvider;
typedef struct _GoaProvider GoaProvider;

struct _GoaExchangeProvider;
typedef struct _GoaExchangeProvider GoaExchangeProvider;

struct _GoaKerberosProvider;
typedef struct _GoaKerberosProvider GoaKerberosProvider;

struct _GoaOAuthProvider;
typedef struct _GoaOAuthProvider GoaOAuthProvider;

struct _GoaOAuth2Provider;
typedef struct _GoaOAuth2Provider GoaOAuth2Provider;

struct _GoaGoogleProvider;
typedef struct _GoaGoogleProvider GoaGoogleProvider;

struct _GoaFacebookProvider;
typedef struct _GoaFacebookProvider GoaFacebookProvider;

struct _GoaEditableLabel;
typedef struct _GoaEditableLabel GoaEditableLabel;

struct _GoaWindowsLiveProvider;
typedef struct _GoaWindowsLiveProvider GoaWindowsLiveProvider;

G_END_DECLS

#endif /* __GOA_BACKEND_TYPES_H__ */
