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

#include "config.h"

#include <glib-object.h>

#include "goaserviceconfig.h"

#include "goadavconfig.h"

struct _GoaDavConfig
{
  GoaServiceConfig parent_instance;

  char *uri;
  char *username;
};

G_DEFINE_FINAL_TYPE (GoaDavConfig, goa_dav_config, GOA_TYPE_SERVICE_CONFIG)

typedef enum
{
  PROP_URI = 1,
  PROP_USERNAME,
} GoaDavConfigProperty;

static GParamSpec *properties[PROP_USERNAME + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_dav_config_finalize (GObject *object)
{
  GoaDavConfig *self = GOA_DAV_CONFIG (object);

  g_clear_pointer (&self->uri, g_free);
  g_clear_pointer (&self->username, g_free);

  G_OBJECT_CLASS (goa_dav_config_parent_class)->finalize (object);
}

static void
goa_dav_config_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GoaDavConfig *self = GOA_DAV_CONFIG (object);

  switch ((GoaDavConfigProperty) prop_id)
    {
    case PROP_URI:
      g_value_set_string (value, self->uri);
      break;

    case PROP_USERNAME:
      g_value_set_string (value, self->username);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_dav_config_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GoaDavConfig *self = GOA_DAV_CONFIG (object);

  switch ((GoaDavConfigProperty) prop_id)
    {
    case PROP_URI:
      goa_dav_config_set_uri (self, g_value_get_string (value));
      break;

    case PROP_USERNAME:
      goa_dav_config_set_username (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_dav_config_init (GoaDavConfig *self)
{
}

static void
goa_dav_config_class_init (GoaDavConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = goa_dav_config_finalize;
  object_class->get_property = goa_dav_config_get_property;
  object_class->set_property = goa_dav_config_set_property;

  /**
   * GoaDavConfig:uri: (getter get_uri) (setter set_uri)
   *
   * The DAV resource URI.
   */
  properties[PROP_URI] =
    g_param_spec_string ("uri", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaDavConfig:uri: (getter get_username) (setter set_username)
   *
   * The username or email.
   */
  properties[PROP_USERNAME] =
    g_param_spec_string ("username", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_dav_config_new:
 * @service: (not nullable): a service type
 * @uri: (nullable): a URI
 * @username: (nullable): a username or email
 *
 * Create a new DAV configuration.
 *
 * Returns: (transfer full): a service configuration
 */
GoaDavConfig *
goa_dav_config_new (const char *service,
                    const char *uri,
                    const char *username)
{
  g_return_val_if_fail (service != NULL, NULL);
  g_return_val_if_fail (g_str_equal (service, GOA_SERVICE_TYPE_CALDAV) ||
                        g_str_equal (service, GOA_SERVICE_TYPE_CARDDAV) ||
                        g_str_equal (service, GOA_SERVICE_TYPE_WEBDAV), NULL);

  return g_object_new (GOA_TYPE_DAV_CONFIG,
                       "service",  service,
                       "uri",      uri,
                       "username", username,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_dav_config_get_uri:
 * @config: a `GoaDavConfig`
 *
 * Get the URI, or %NULL if unknown.
 *
 * Returns: (transfer none) (nullable): a URI
 */
const char *
goa_dav_config_get_uri (GoaDavConfig *config)
{
  g_return_val_if_fail (GOA_IS_DAV_CONFIG (config), NULL);

  return config->uri;
}

/**
 * goa_dav_config_set_uri:
 * @config: a `GoaDavConfig`
 * @uri: (nullable): a URI
 *
 * Set the URI.
 */
void
goa_dav_config_set_uri (GoaDavConfig *config,
                        const char   *uri)
{
  g_return_if_fail (GOA_IS_DAV_CONFIG (config));

  if (g_set_str (&config->uri, uri))
    g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_URI]);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_dav_config_get_username:
 * @config: a `GoaDavConfig`
 *
 * Get the username, or %NULL if unknown.
 *
 * Returns: (transfer none) (nullable): a username
 */
const char *
goa_dav_config_get_username (GoaDavConfig *config)
{
  g_return_val_if_fail (GOA_IS_DAV_CONFIG (config), NULL);

  return config->username;
}

/**
 * goa_dav_config_set_username:
 * @config: a `GoaDavConfig`
 * @username: (nullable): a username or email
 *
 * Set the username.
 */
void
goa_dav_config_set_username (GoaDavConfig *config,
                             const char   *username)
{
  g_return_if_fail (GOA_IS_DAV_CONFIG (config));

  if (g_set_str (&config->username, username))
    g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_USERNAME]);
}

