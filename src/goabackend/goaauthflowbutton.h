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

#ifndef __GOA_AUTHFLOW_BUTTON_H__
#define __GOA_AUTHFLOW_BUTTON_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOA_TYPE_AUTHFLOW_BUTTON (goa_authflow_button_get_type ())

G_DECLARE_FINAL_TYPE (GoaAuthflowButton, goa_authflow_button, GOA, AUTHFLOW_BUTTON, GtkBox)

GtkWidget           *goa_authflow_button_new                (void);
gboolean             goa_authflow_button_get_active         (GoaAuthflowButton *widget);
void                 goa_authflow_button_set_active         (GoaAuthflowButton *widget,
                                                             gboolean           active);

G_END_DECLS

#endif /* __GOA_AUTHFLOW_BUTTON_H__ */

