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
#define GOA_WEB_VIEW(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GOA_TYPE_WEB_VIEW, GoaWebView))
#define GOA_IS_WEB_VIEW(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GOA_TYPE_WEB_VIEW))

typedef struct _GoaWebView      GoaWebView;
typedef struct _GoaWebViewClass GoaWebViewClass;

GType                  goa_web_view_get_type               (void) G_GNUC_CONST;
GtkWidget             *goa_web_view_new                    (GoaProvider *provider,
                                                            const gchar *existing_identity);
GtkWidget             *goa_web_view_get_view               (GoaWebView *self);
void                   goa_web_view_fake_mobile            (GoaWebView *self);
void                   goa_web_view_add_cookies            (GoaWebView *self,
                                                            GSList     *cookies);

G_END_DECLS

#endif /* __GOA_WEB_VIEW_H__ */
