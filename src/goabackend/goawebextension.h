/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2015 Damián Nohales
 * Copyright © 2015 – 2017 Red Hat, Inc.
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

#ifndef __GOA_WEB_EXTENSION_H__
#define __GOA_WEB_EXTENSION_H__

#include <glib-object.h>
#include <webkit2/webkit-web-extension.h>

G_BEGIN_DECLS

#define GOA_TYPE_WEB_EXTENSION            (goa_web_extension_get_type())
#define GOA_WEB_EXTENSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_WEB_EXTENSION, GoaWebExtension))
#define GOA_IS_WEB_EXTENSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_WEB_EXTENSION))

typedef struct _GoaWebExtension      GoaWebExtension;
typedef struct _GoaWebExtensionClass GoaWebExtensionClass;

GType                goa_web_extension_get_type   (void);
GoaWebExtension     *goa_web_extension_new        (WebKitWebExtension *wk_extension,
                                                   const gchar        *provider_type,
                                                   const gchar        *existing_identity);

G_END_DECLS

#endif /* __GOA_WEB_EXTENSION_H__ */
