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
#include "goabackendgoogleprovider.h"

/**
 * GoaBackendGoogleProvider:
 *
 * The #GoaBackendGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendGoogleProvider
{
  /*< private >*/
  GoaBackendProvider parent_instance;
};

typedef struct _GoaBackendGoogleProviderClass GoaBackendGoogleProviderClass;

struct _GoaBackendGoogleProviderClass
{
  GoaBackendProviderClass parent_class;
};

static const gchar *goa_backend_google_provider_get_provider_type (GoaBackendProvider *_provider);

/**
 * SECTION:goabackendgoogleprovider
 * @title: GoaBackendGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaBackendGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendGoogleProvider, goa_backend_google_provider, GOA_TYPE_BACKEND_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

static void
goa_backend_google_provider_init (GoaBackendGoogleProvider *client)
{
}

static void
goa_backend_google_provider_class_init (GoaBackendGoogleProviderClass *klass)
{
  GoaBackendProviderClass *provider_klass;

  provider_klass = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_klass->get_provider_type = goa_backend_google_provider_get_provider_type;
}

static const gchar *
goa_backend_google_provider_get_provider_type (GoaBackendProvider *_provider)
{
  return "google";
}

/* ---------------------------------------------------------------------------------------------------- */
