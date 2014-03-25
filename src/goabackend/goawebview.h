/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __GOA_WEB_VIEW_H__
#define __GOA_WEB_VIEW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOA_TYPE_WEB_VIEW            (goa_web_view_get_type ())
#define GOA_WEB_VIEW(object)         (G_TYPE_CHECK_INSTANCE_CAST ((object), GOA_TYPE_WEB_VIEW, GoaWebView))
#define GOA_WEB_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_WEB_VIEW, GoaWebViewClass))
#define GOA_IS_WEB_VIEW(object)      (G_TYPE_CHECK_INSTANCE_TYPE ((object), GOA_TYPE_WEB_VIEW))
#define GOA_IS_WEB_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GOA_TYPE_WEB_VIEW))
#define GOA_WEB_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GOA_TYPE_WEB_VIEW, GoaWebViewClass))

typedef struct _GoaWebView        GoaWebView;
typedef struct _GoaWebViewClass   GoaWebViewClass;
typedef struct _GoaWebViewPrivate GoaWebViewPrivate;

struct _GoaWebView
{
  GtkOverlay parent_instance;
  GoaWebViewPrivate *priv;
};

struct _GoaWebViewClass
{
  GtkOverlayClass parent_class;
};

GType                  goa_web_view_get_type               (void) G_GNUC_CONST;
GtkWidget             *goa_web_view_new                    (void);
GtkWidget             *goa_web_view_get_view               (GoaWebView *self);
void                   goa_web_view_fake_mobile            (GoaWebView *self);
void                   goa_web_view_add_cookies            (GoaWebView *self,
                                                            GSList     *cookies);

G_END_DECLS

#endif /* __GOA_WEB_VIEW_H__ */
