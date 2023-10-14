/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 Andy Holmes <andyholmes@gnome.org>
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

#ifndef __GOA_UTILS_PRIV_H__
#define __GOA_UTILS_PRIV_H__

#include <glib.h>
#include <gtk/gtk.h>

#include "goaprovider.h"

G_BEGIN_DECLS

void             goa_utils_dialog_add_combo_box (GtkWidget    *grid,
                                                 gint          row,
                                                 const gchar  *text,
                                                 GtkWidget   **out_combo_box);

void             goa_utils_dialog_add_entry     (GtkWidget    *grid,
                                                 gint          row,
                                                 const gchar  *text,
                                                 GtkWidget   **out_entry);

G_END_DECLS

#endif /* __GOA_UTILS_PRIV_H__ */
