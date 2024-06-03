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

#ifndef __GOA_MAIL_CONFIG_H__
#define __GOA_MAIL_CONFIG_H__

#include <stdint.h>

#include <glib-object.h>

#include "goabackendenums-priv.h"
#include "goaserviceconfig.h"

G_BEGIN_DECLS

#define GOA_TYPE_MAIL_CONFIG (goa_mail_config_get_type ())
G_DECLARE_FINAL_TYPE (GoaMailConfig, goa_mail_config, GOA, MAIL_CONFIG, GoaServiceConfig);

GoaMailConfig       *goa_mail_config_new                       (const char         *service);
GoaTlsType           goa_mail_config_get_encryption            (GoaMailConfig      *config);
void                 goa_mail_config_set_encryption            (GoaMailConfig      *config,
                                                                GoaTlsType          encryption);
const char          *goa_mail_config_get_hostname              (GoaMailConfig      *config);
void                 goa_mail_config_set_hostname              (GoaMailConfig      *config,
                                                                const char         *hostname);
uint16_t             goa_mail_config_get_port                  (GoaMailConfig      *config);
void                 goa_mail_config_set_port                  (GoaMailConfig      *config,
                                                                uint16_t            port);
const char          *goa_mail_config_get_username              (GoaMailConfig      *config);
void                 goa_mail_config_set_username              (GoaMailConfig      *config,
                                                                const char         *username);

G_END_DECLS

#endif /* __GOA_MAIL_CONFIG_H__ */
