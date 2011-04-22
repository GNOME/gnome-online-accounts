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

/**
 * SECTION:goabackendservice
 * @title: GoaBackendService
 * @short_description: Abstract base class for services
 *
 * #GoaBackendService is the abstract type that all services implement.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendService, goa_backend_service, G_TYPE_OBJECT);

static void
goa_backend_service_init (GoaBackendService *client)
{
}

static void
goa_backend_service_class_init (GoaBackendServiceClass *klass)
{
}

/**
 * goa_backend_service_get_service_type:
 * @service: A #GoaBackendService.
 *
 * Gets the type of @service.
 *
 * Returns: (transfer none): A string owned by @service, do not free.
 */
const gchar *
goa_backend_service_get_service_type (GoaBackendService *service)
{
  g_return_val_if_fail (GOA_IS_BACKEND_SERVICE (service), NULL);
  return GOA_BACKEND_SERVICE_GET_CLASS (service)->get_service_type (service);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_service_get_for_service_type:
 * @service_type: A service type.
 *
 * Looks up the "goa-backend-service" extension points and returns a
 * #GoaBackendService for @service_type, if any.
 *
 * Returns: (transfer full): A #GoaBackendService (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaBackendService *
goa_backend_service_get_for_service_type (const gchar *service_type)
{
  GIOExtension *extension;
  GIOExtensionPoint *extension_point;
  GoaBackendService *ret;

  ret = NULL;

  extension_point = g_io_extension_point_lookup (GOA_BACKEND_SERVICE_EXTENSION_POINT_NAME);
  extension = g_io_extension_point_get_extension_by_name (extension_point, service_type);
  if (extension != NULL)
    ret = GOA_BACKEND_SERVICE (g_object_new (g_io_extension_get_type (extension), NULL));
  return ret;
}

