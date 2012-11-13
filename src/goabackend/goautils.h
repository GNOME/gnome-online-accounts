/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_UTILS_H__
#define __GOA_UTILS_H__

#include <glib.h>
#include <gtk/gtk.h>
#include <goabackend/goabackendtypes.h>

G_BEGIN_DECLS

typedef gpointer (*GoaPeekInterfaceFunc)   (GoaObject *);

gboolean         goa_utils_check_duplicate (GoaClient              *client,
                                            const gchar            *identity,
                                            const gchar            *provider_type,
                                            GoaPeekInterfaceFunc    func,
                                            GError                **error);

void             goa_utils_set_dialog_title (GoaProvider *provider, GtkDialog *dialog, gboolean add_account);

gboolean         goa_utils_delete_credentials_sync (GoaProvider    *provider,
                                                    GoaAccount     *account,
                                                    GCancellable   *cancellable,
                                                    GError        **error);

GVariant        *goa_utils_lookup_credentials_sync (GoaProvider    *provider,
                                                    GoaObject      *object,
                                                    GCancellable   *cancellable,
                                                    GError        **error);

gboolean         goa_utils_store_credentials_for_id_sync (GoaProvider    *provider,
                                                          const gchar    *id,
                                                          GVariant       *credentials,
                                                          GCancellable   *cancellable,
                                                          GError        **error);

gboolean         goa_utils_store_credentials_for_object_sync (GoaProvider    *provider,
                                                              GoaObject      *object,
                                                              GVariant       *credentials,
                                                              GCancellable   *cancellable,
                                                              GError        **error);

void             goa_utils_keyfile_remove_key (GoaAccount *account, const gchar *key);

void             goa_utils_keyfile_set_boolean (GoaAccount *account, const gchar *key, gboolean value);

void             goa_utils_keyfile_set_string (GoaAccount *account, const gchar *key, const gchar *value);

G_END_DECLS

#endif /* __GOA_UTILS_H__ */
