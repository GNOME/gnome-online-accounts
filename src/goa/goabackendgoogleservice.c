/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "goabackendservice.h"
#include "goabackendgoogleservice.h"

/**
 * GoaBackendGoogleService:
 *
 * The #GoaBackendGoogleService structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendGoogleService
{
  /*< private >*/
  GoaBackendService parent_instance;
};

typedef struct _GoaBackendGoogleServiceClass GoaBackendGoogleServiceClass;

struct _GoaBackendGoogleServiceClass
{
  GoaBackendServiceClass parent_class;
};

static const gchar *goa_backend_google_service_get_service_type (GoaBackendService *_service);

/**
 * SECTION:goabackendgoogleservice
 * @title: GoaBackendGoogleService
 * @short_description: Google Services
 *
 * #GoaBackendGoogleService is used for handling Google services.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendGoogleService, goa_backend_google_service, GOA_TYPE_BACKEND_SERVICE,
                         g_io_extension_point_implement (GOA_BACKEND_SERVICE_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

static void
goa_backend_google_service_init (GoaBackendGoogleService *client)
{
}

static void
goa_backend_google_service_class_init (GoaBackendGoogleServiceClass *klass)
{
  GoaBackendServiceClass *service_klass;

  service_klass = GOA_BACKEND_SERVICE_CLASS (klass);
  service_klass->get_service_type = goa_backend_google_service_get_service_type;
}

static const gchar *
goa_backend_google_service_get_service_type (GoaBackendService *_service)
{
  return "google";
}

/* ---------------------------------------------------------------------------------------------------- */
