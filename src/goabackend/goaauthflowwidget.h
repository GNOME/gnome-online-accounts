/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_AUTHFLOW_WIDGET_H__
#define __GOA_AUTHFLOW_WIDGET_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOA_TYPE_AUTHFLOW_WIDGET (goa_authflow_widget_get_type ())

G_DECLARE_FINAL_TYPE (GoaAuthflowWidget, goa_authflow_widget, GOA, AUTHFLOW_WIDGET, GtkBox)

GtkWidget           *goa_authflow_widget_new                (void);

G_END_DECLS

#endif /* __GOA_AUTHFLOW_WIDGET_H__ */

