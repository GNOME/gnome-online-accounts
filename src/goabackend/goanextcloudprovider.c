/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
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

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goaprovider.h"
#include "goanextcloudprovider.h"

struct _GoaNextcloudProvider
{
  GoaOwncloudProviderClass parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaNextcloudProvider, goa_nextcloud_provider, GOA_TYPE_OWNCLOUD_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_NEXTCLOUD_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_NEXTCLOUD_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Nextcloud"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_nextcloud_provider_init (GoaNextcloudProvider *self)
{
}

static void
goa_nextcloud_provider_class_init (GoaNextcloudProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
}

/* ---------------------------------------------------------------------------------------------------- */
