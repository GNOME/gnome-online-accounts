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

#include "goabackendprovider.h"

/**
 * SECTION:goabackendprovider
 * @title: GoaBackendProvider
 * @short_description: Abstract base class for providers
 *
 * #GoaBackendProvider is the base type that all providers implement.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendProvider, goa_backend_provider, G_TYPE_OBJECT);

static void
goa_backend_provider_init (GoaBackendProvider *client)
{
}

static void
goa_backend_provider_class_init (GoaBackendProviderClass *klass)
{
}

/**
 * goa_backend_provider_get_provider_type:
 * @provider: A #GoaBackendProvider.
 *
 * Gets the type of @provider.
 *
 * Returns: (transfer none): A string owned by @provider, do not free.
 */
const gchar *
goa_backend_provider_get_provider_type (GoaBackendProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), NULL);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_provider_type (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_get_for_provider_type:
 * @provider_type: A provider type.
 *
 * Looks up the "goa-backend-provider" extension points and returns a
 * #GoaBackendProvider for @provider_type, if any.
 *
 * Returns: (transfer full): A #GoaBackendProvider (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaBackendProvider *
goa_backend_provider_get_for_provider_type (const gchar *provider_type)
{
  GIOExtension *extension;
  GIOExtensionPoint *extension_point;
  GoaBackendProvider *ret;

  ret = NULL;

  extension_point = g_io_extension_point_lookup (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME);
  extension = g_io_extension_point_get_extension_by_name (extension_point, provider_type);
  if (extension != NULL)
    ret = GOA_BACKEND_PROVIDER (g_object_new (g_io_extension_get_type (extension), NULL));
  return ret;
}

