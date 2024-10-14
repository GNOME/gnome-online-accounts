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

G_DEFINE_ENUM_TYPE (GoaAuthState, goa_auth_state,
  G_DEFINE_ENUM_VALUE (GOA_AUTH_STATE_UNKNOWN, "unknown"),
  G_DEFINE_ENUM_VALUE (GOA_AUTH_STATE_ACCEPTED, "accepted"),
  G_DEFINE_ENUM_VALUE (GOA_AUTH_STATE_REJECTED, "rejected"),
  G_DEFINE_ENUM_VALUE (GOA_AUTH_STATE_REQUIRED, "required"),
  G_DEFINE_ENUM_VALUE (GOA_AUTH_STATE_SSL_ERROR, "ssl-error"))

typedef struct
{
  GObject parent_instance;

  char *service;
  gboolean accept_ssl_errors;
  GoaAuthState auth_state;
} GoaServiceConfigPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GoaServiceConfig, goa_service_config, G_TYPE_OBJECT)

typedef enum
{
  PROP_ACCEPT_SSL_ERRORS = 1,
  PROP_AUTH_STATE,
  PROP_SERVICE,
} GoaServiceConfigProperty;

static GParamSpec *properties[PROP_SERVICE + 1] = { NULL, };

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
    case PROP_ACCEPT_SSL_ERRORS:
      g_value_set_boolean (value, priv->accept_ssl_errors);
      break;

    case PROP_AUTH_STATE:
      g_value_set_enum (value, priv->auth_state);
      break;

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
    case PROP_ACCEPT_SSL_ERRORS:
      goa_service_config_set_accept_ssl_errors (self, g_value_get_boolean (value));
      break;

    case PROP_AUTH_STATE:
      goa_service_config_set_auth_state (self, g_value_get_enum (value));
      break;

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

  object_class->finalize = goa_service_config_finalize;
  object_class->get_property = goa_service_config_get_property;
  object_class->set_property = goa_service_config_set_property;

  /**
   * GoaServiceConfig:accept-ssl-errors: (getter get_accept_ssl_errors) (setter set_accept_ssl_errors)
   *
   * The authentication state of the configuration.
   */
  properties[PROP_ACCEPT_SSL_ERRORS] =
    g_param_spec_boolean ("accept-ssl-errors", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaServiceConfig:auth-state: (getter get_auth_state) (setter set_auth_state)
   *
   * The authentication state of the configuration.
   */
  properties[PROP_AUTH_STATE] =
    g_param_spec_enum ("auth-state", NULL, NULL,
                       GOA_TYPE_AUTH_STATE,
                       GOA_AUTH_STATE_UNKNOWN,
                       (G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaServiceConfig:service: (getter get_service)
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
 * goa_service_config_get_accept_ssl_errors: (get-property accept-ssl-errors)
 * @config: a `GoaServiceConfig`
 *
 * Get whether certificate errors are ignored when authenticating @config.
 *
 * Returns: %TRUE if SSL errors are ignored, %FALSE otherwise
 */
gboolean
goa_service_config_get_accept_ssl_errors (GoaServiceConfig *config)
{
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (config);

  g_return_val_if_fail (GOA_IS_SERVICE_CONFIG (config), FALSE);

  return priv->accept_ssl_errors;
}

/**
 * goa_service_config_set_accept_ssl_errors: (set-property accept-ssl-errors)
 * @config: a `GoaServiceConfig`
 * @accept_ssl_errors: %TRUE to ignore SSL errors, or %FALSE
 *
 * Set whether certificate errors are ignored when authenticating @config.
 */
void
goa_service_config_set_accept_ssl_errors (GoaServiceConfig *config,
                                          gboolean          accept_ssl_errors)
{
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (config);

  g_return_if_fail (GOA_IS_SERVICE_CONFIG (config));

  accept_ssl_errors = !!accept_ssl_errors;
  if (priv->accept_ssl_errors != accept_ssl_errors)
    {
      priv->accept_ssl_errors = accept_ssl_errors;
      g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_ACCEPT_SSL_ERRORS]);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_config_get_auth_state: (get-property auth-state)
 * @config: a `GoaServiceConfig`
 *
 * Get the authentication state of @config.
 *
 * Returns: the authentication state
 */
GoaAuthState
goa_service_config_get_auth_state (GoaServiceConfig *config)
{
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (config);

  g_return_val_if_fail (GOA_IS_SERVICE_CONFIG (config), GOA_AUTH_STATE_UNKNOWN);

  return priv->auth_state;
}

/**
 * goa_service_config_set_auth_state: (set-property auth-state)
 * @config: a `GoaServiceConfig`
 * @auth_state: the new authentication state
 *
 * Set the authentication of @config.
 */
void
goa_service_config_set_auth_state (GoaServiceConfig *config,
                                   GoaAuthState      auth_state)
{
  GoaServiceConfigPrivate *priv = goa_service_config_get_instance_private (config);

  g_return_if_fail (GOA_IS_SERVICE_CONFIG (config));
  g_return_if_fail (auth_state >= GOA_AUTH_STATE_UNKNOWN
                    && auth_state <= GOA_AUTH_STATE_SSL_ERROR);

  if (priv->auth_state != auth_state)
    {
      priv->auth_state = auth_state;
      g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_AUTH_STATE]);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_config_get_service: (get-property service)
 * @config: a `GoaServiceConfig`
 *
 * Get the service type of @config, or "unknown" if not set.
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

