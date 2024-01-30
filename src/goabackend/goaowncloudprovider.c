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

#include <string.h>

#include <glib/gi18n-lib.h>

#include "goaprovider.h"
#include "goaowncloudprovider.h"
#include "goawebdavprovider.h"
#include "goawebdavprovider-priv.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

struct _GoaOwncloudProvider
{
  GoaWebDavProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaOwncloudProvider, goa_owncloud_provider, GOA_TYPE_WEBDAV_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_OWNCLOUD_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_OWNCLOUD_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Nextcloud"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_FILES;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-owncloud");
}

/* ---------------------------------------------------------------------------------------------------- */

static struct
{
  const char *key;
  const char *endpoint;
} migration_table[] = {
    {
      .key = "CalDavUri",
      .endpoint = "remote.php/dav",
    },
    {
      .key = "CardDavUri",
      .endpoint = "remote.php/dav",
    },
    {
      .key = "Uri",
      .endpoint = "remote.php/webdav",
    },
};

static gboolean
migrate_account (GKeyFile    *key_file,
                 const char  *group,
                 GError     **error)
{
  g_autofree char *uri = NULL;
  g_autofree char *base_uri = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GError) warning = NULL;

  /* This shouldn't ever fail, but we check anyways */
  uri = g_key_file_get_string (key_file, group, "Uri", error);
  if (uri == NULL || !g_uri_is_valid (uri, G_URI_FLAGS_PARSE_RELAXED, error))
    return FALSE;

  /* Only URIs missing "remote.php" need migration */
  if (g_strrstr (uri, "remote.php") != NULL)
    return TRUE;

  if (!g_str_has_suffix (uri, "/"))
    base_uri = g_strconcat (uri, "/", NULL);
  else
    base_uri = g_strdup (uri);

  for (size_t i = 0; i < G_N_ELEMENTS (migration_table); i++)
    {
      g_autofree char *value = NULL;

      value = g_strconcat (base_uri, migration_table[i].endpoint, NULL);
      g_key_file_set_string (key_file, group, migration_table[i].key, value);
    }

  path = g_build_filename (g_get_user_config_dir (), "goa-1.0", "accounts.conf", NULL);
  if (!g_key_file_save_to_file (key_file, path, &warning))
    g_warning ("Failed to save account migration: %s", warning->message);

  return TRUE;
}

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const char          *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  if (!migrate_account (key_file, group, error))
    return FALSE;

  /* Chain up to WebDAV provider */
  return GOA_PROVIDER_CLASS (goa_owncloud_provider_parent_class)->build_object (provider,
                                                                                object,
                                                                                key_file,
                                                                                group,
                                                                                connection,
                                                                                just_added,
                                                                                error);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_owncloud_provider_init (GoaOwncloudProvider *self)
{
}

static void
goa_owncloud_provider_class_init (GoaOwncloudProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->build_object               = build_object;
}
