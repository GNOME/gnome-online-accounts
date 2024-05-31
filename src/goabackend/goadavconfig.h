/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2024 GNOME Foundation Inc.
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

#ifndef __GOA_DAV_CONFIG_H__
#define __GOA_DAV_CONFIG_H__

#include <glib-object.h>

#include "goaserviceconfig.h"

G_BEGIN_DECLS

#define GOA_TYPE_DAV_CONFIG (goa_dav_config_get_type ())
G_DECLARE_FINAL_TYPE (GoaDavConfig, goa_dav_config, GOA, DAV_CONFIG, GoaServiceConfig);

GoaDavConfig        *goa_dav_config_new              (const char          *service,
                                                      const char          *uri,
                                                      const char          *username);
const char          *goa_dav_config_get_uri          (GoaDavConfig        *config);
void                 goa_dav_config_set_uri          (GoaDavConfig        *config,
                                                      const char          *uri);
const char          *goa_dav_config_get_username     (GoaDavConfig        *config);
void                 goa_dav_config_set_username     (GoaDavConfig        *config,
                                                      const char          *username);

G_END_DECLS

#endif /* __GOA_DAV_CONFIG_H__ */
