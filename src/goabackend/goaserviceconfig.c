/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
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

#include <glib/gprintf.h>
#include <gio/gio.h>

#include "goabackendenums.h"
#include "goabackendenumtypes.h"
#include "goaserviceconfig.h"

typedef struct
{
  GObject parent_instance;

  char *service;
} GoaServiceConfigPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GoaServiceConfig, goa_service_config, G_TYPE_OBJECT)

typedef enum
{
  PROP_SERVICE = 1,
} GoaServiceConfigProperty;

static GParamSpec *properties[PROP_SERVICE + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_service_config_real_build_variant (GoaServiceConfig *config,
                                       GVariantBuilder  *builder)
{
  g_assert (GOA_IS_SERVICE_CONFIG (config));
  g_assert (builder != NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_service_config_finalize (GObject *object)
{
  GoaServiceConfig *self = GOA_SERVICE_CONFIG (object);
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (self);

  g_clear_pointer (&priv->service, g_free);

  G_OBJECT_CLASS (goa_service_config_parent_class)->finalize (object);
}

static void
goa_service_config_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GoaServiceConfig *self = GOA_SERVICE_CONFIG (object);
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (self);

  switch ((GoaServiceConfigProperty) prop_id)
    {
    case PROP_SERVICE:
      g_value_set_string (value, priv->service);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_service_config_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  GoaServiceConfig *self = GOA_SERVICE_CONFIG (object);
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (self);

  switch ((GoaServiceConfigProperty) prop_id)
    {
    case PROP_SERVICE:
      g_assert (priv->service == NULL);
      priv->service = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_service_config_init (GoaServiceConfig *self)
{
}

static void
goa_service_config_class_init (GoaServiceConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaServiceConfigClass *config_class = GOA_SERVICE_CONFIG_CLASS (klass);

  object_class->finalize = goa_service_config_finalize;
  object_class->get_property = goa_service_config_get_property;
  object_class->set_property = goa_service_config_set_property;

  config_class->build_variant = goa_service_config_real_build_variant;

  /**
   * GoaServiceProvider:service: (getter get_service)
   *
   * The service type name, such as `imap` or `caldav`.
   */
  properties[PROP_SERVICE] =
    g_param_spec_string ("service", NULL, NULL,
                         "unknown",
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_config_new:
 * @service: (nullable): a service type
 *
 * Create a new, empty service configuration.
 *
 * Returns: (transfer full): a service configuration
 */
GoaServiceConfig *
goa_service_config_new (const char *service)
{
  return g_object_new (GOA_TYPE_SERVICE_CONFIG,
                       "service", service,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_config_get_service: (get-property service)
 * @config: a `GoaServiceConfig`
 *
 * Get the service type of @config, or %NULL if unknown.
 *
 * Returns: (transfer none) (not nullable): a service type
 */
const char *
goa_service_config_get_service (GoaServiceConfig *config)
{
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (config);

  g_return_val_if_fail (GOA_IS_SERVICE_CONFIG (config), "unknown");

  return priv->service;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_config_build_variant: (vfunc build_variant)
 * @config: a `GoaServiceConfig`
 * @builder: a `GVariantBuilder`
 *
 * Set the parameter @name to @value, or remove it if %NULL.
 */
void
goa_service_config_build_variant (GoaServiceConfig *config,
                                  GVariantBuilder  *builder)
{
  g_return_if_fail (GOA_IS_SERVICE_CONFIG (config));
  g_return_if_fail (builder != NULL);

  GOA_SERVICE_CONFIG_GET_CLASS (config)->build_variant (config, builder);
}

