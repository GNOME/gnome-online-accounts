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

#ifndef __GOA_BACKEND_IMAP_AUTH_H__
#define __GOA_BACKEND_IMAP_AUTH_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_IMAP_AUTH         (goa_backend_imap_auth_get_type ())
#define GOA_BACKEND_IMAP_AUTH(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_BACKEND_IMAP_AUTH, GoaBackendImapAuth))
#define GOA_BACKEND_IMAP_AUTH_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_BACKEND_IMAP_AUTH, GoaBackendImapAuthClass))
#define GOA_BACKEND_IMAP_AUTH_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_BACKEND_IMAP_AUTH, GoaBackendImapAuthClass))
#define GOA_IS_BACKEND_IMAP_AUTH(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_BACKEND_IMAP_AUTH))
#define GOA_IS_BACKEND_IMAP_AUTH_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_BACKEND_IMAP_AUTH))

struct _GoaBackendImapAuthClass;
struct _GoaBackendImapAuthPrivate;
typedef struct _GoaBackendImapAuthClass   GoaBackendImapAuthClass;
typedef struct _GoaBackendImapAuthPrivate GoaBackendImapAuthPrivate;

/**
 * GoaBackendImapAuth:
 *
 * The #GoaBackendImapAuth structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _GoaBackendImapAuth
{
  /*< private >*/
  GObject parent_instance;
  GoaBackendImapAuthPrivate *priv;
};

/**
 * GoaBackendImapAuthClass:
 * @parent_class: The parent class
 * @run_sync: Virtual function for the goa_backend_imap_auth_run_sync() method.
 *
 * Class structure for #GoaBackendImapAuth.
 */
struct _GoaBackendImapAuthClass
{
  GObjectClass parent_class;
  gboolean (*run_sync) (GoaBackendImapAuth  *auth,
                        GDataInputStream    *input,
                        GDataOutputStream   *output,
                        GCancellable        *cancellable,
                        GError             **error);
  /*< private >*/
  /* Padding for future expansion */
  gpointer goa_reserved[8];
};

GType     goa_backend_imap_auth_get_type (void) G_GNUC_CONST;
gboolean  goa_backend_imap_auth_run_sync (GoaBackendImapAuth  *auth,
                                          GDataInputStream    *input,
                                          GDataOutputStream   *output,
                                          GCancellable        *cancellable,
                                          GError             **error);

G_END_DECLS

#endif /* __GOA_BACKEND_IMAP_AUTH_H__ */
