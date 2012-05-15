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

GtkWidget       *goa_utils_create_add_refresh_label (GoaProvider *provider, gboolean add_account);

G_END_DECLS

#endif /* __GOA_UTILS_H__ */
