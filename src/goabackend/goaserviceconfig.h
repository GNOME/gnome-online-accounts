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

#ifndef __GOA_SERVICE_CONFIG_H__
#define __GOA_SERVICE_CONFIG_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define GOA_SERVICE_TYPE_CALDAV  "caldav"
#define GOA_SERVICE_TYPE_CARDDAV "carddav"
#define GOA_SERVICE_TYPE_WEBDAV  "webdav"
#define GOA_SERVICE_TYPE_IMAP    "imap"
#define GOA_SERVICE_TYPE_JMAP    "jmap"
#define GOA_SERVICE_TYPE_POP3    "pop3"
#define GOA_SERVICE_TYPE_SMTP    "smtp"

#define GOA_TYPE_SERVICE_CONFIG (goa_service_config_get_type ())
G_DECLARE_DERIVABLE_TYPE (GoaServiceConfig, goa_service_config, GOA, SERVICE_CONFIG, GObject);

struct _GoaServiceConfigClass
{
  GObjectClass          parent_class;

  /* < private >*/
  gpointer              reserved[8];
};

const char           *goa_service_config_get_service      (GoaServiceConfig     *config);

G_END_DECLS

#endif /* __GOA_SERVICE_CONFIG_H__ */
