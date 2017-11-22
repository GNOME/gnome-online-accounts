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

#ifndef __GOA_MAIL_AUTH_H__
#define __GOA_MAIL_AUTH_H__

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GOA_TYPE_MAIL_AUTH         (goa_mail_auth_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaMailAuth, goa_mail_auth, GOA, MAIL_AUTH, GObject);

typedef struct _GoaMailAuthPrivate GoaMailAuthPrivate;

struct _GoaMailAuthClass
{
  GObjectClass parent_class;
  gboolean    (*is_needed)        (GoaMailAuth      *self);
  gboolean    (*run_sync)         (GoaMailAuth      *self,
                                   GCancellable     *cancellable,
                                   GError          **error);
  gboolean    (*starttls_sync)    (GoaMailAuth      *self,
                                   GCancellable     *cancellable,
                                   GError          **error);
};

gboolean              goa_mail_auth_is_needed          (GoaMailAuth         *self);
void                  goa_mail_auth_run                (GoaMailAuth         *self,
                                                        GCancellable        *cancellable,
                                                        GAsyncReadyCallback  callback,
                                                        gpointer             user_data);
gboolean              goa_mail_auth_run_finish         (GoaMailAuth         *self,
                                                        GAsyncResult        *res,
                                                        GError             **error);
gboolean              goa_mail_auth_run_sync           (GoaMailAuth         *self,
                                                        GCancellable        *cancellable,
                                                        GError             **error);
void                  goa_mail_auth_starttls           (GoaMailAuth         *self,
                                                        GCancellable        *cancellable,
                                                        GAsyncReadyCallback  callback,
                                                        gpointer             user_data);
gboolean              goa_mail_auth_starttls_finish    (GoaMailAuth         *self,
                                                        GAsyncResult        *res,
                                                        GError             **error);
gboolean              goa_mail_auth_starttls_sync      (GoaMailAuth         *self,
                                                        GCancellable        *cancellable,
                                                        GError             **error);
GDataInputStream     *goa_mail_auth_get_input          (GoaMailAuth         *self);
void                  goa_mail_auth_set_input          (GoaMailAuth         *self,
                                                        GDataInputStream    *input);
GDataOutputStream    *goa_mail_auth_get_output         (GoaMailAuth         *self);
void                  goa_mail_auth_set_output         (GoaMailAuth         *self,
                                                        GDataOutputStream   *input);

G_END_DECLS

#endif /* __GOA_MAIL_AUTH_H__ */
