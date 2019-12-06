/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#ifndef __GOA_WEB_VIEW_H__
#define __GOA_WEB_VIEW_H__

#include <gtk/gtk.h>

#include "goaprovider.h"

G_BEGIN_DECLS

#define GOA_TYPE_WEB_VIEW            (goa_web_view_get_type ())
G_DECLARE_FINAL_TYPE (GoaWebView, goa_web_view, GOA, WEB_VIEW, GtkOverlay);

GtkWidget             *goa_web_view_new                    (GoaProvider *provider,
                                                            const gchar *existing_identity);
GtkWidget             *goa_web_view_get_view               (GoaWebView *self);
void                   goa_web_view_fake_mobile            (GoaWebView *self);

G_END_DECLS

#endif /* __GOA_WEB_VIEW_H__ */
