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

#include <stdint.h>

#include <glib-object.h>

#include "goabackendenums-priv.h"
#include "goabackendenumtypes-priv.h"
#include "goaserviceconfig.h"

#include "goamailconfig.h"

struct _GoaMailConfig
{
  GoaServiceConfig parent_instance;

  char *username;
  char *hostname;
  uint16_t port;
  GoaTlsType encryption;
};

G_DEFINE_FINAL_TYPE (GoaMailConfig, goa_mail_config, GOA_TYPE_SERVICE_CONFIG)

typedef enum
{
  PROP_ENCRYPTION = 1,
  PROP_HOSTNAME,
  PROP_PORT,
  PROP_USERNAME,
} GoaMailConfigProperty;

static GParamSpec *properties[PROP_USERNAME + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_config_finalize (GObject *object)
{
  GoaMailConfig *self = GOA_MAIL_CONFIG (object);

  g_clear_pointer (&self->hostname, g_free);
  g_clear_pointer (&self->username, g_free);

  G_OBJECT_CLASS (goa_mail_config_parent_class)->finalize (object);
}

static void
goa_mail_config_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GoaMailConfig *self = GOA_MAIL_CONFIG (object);

  switch ((GoaMailConfigProperty) prop_id)
    {
    case PROP_ENCRYPTION:
      g_value_set_enum (value, self->encryption);
      break;

    case PROP_HOSTNAME:
      g_value_set_string (value, self->hostname);
      break;

    case PROP_PORT:
      g_value_set_uint (value, self->port);
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
goa_mail_config_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GoaMailConfig *self = GOA_MAIL_CONFIG (object);

  switch ((GoaMailConfigProperty) prop_id)
    {
    case PROP_ENCRYPTION:
      goa_mail_config_set_encryption (self, g_value_get_enum (value));
      break;

    case PROP_HOSTNAME:
      goa_mail_config_set_hostname (self, g_value_get_string (value));
      break;

    case PROP_PORT:
      goa_mail_config_set_port (self, g_value_get_uint (value));
      break;

    case PROP_USERNAME:
      goa_mail_config_set_username (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_config_init (GoaMailConfig *self)
{
}

static void
goa_mail_config_class_init (GoaMailConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = goa_mail_config_finalize;
  object_class->get_property = goa_mail_config_get_property;
  object_class->set_property = goa_mail_config_set_property;

  /**
   * GoaMailConfig:encryption: (getter get_encryption) (setter set_encryption)
   *
   * The encryption protocol (e.g. TLS or STARTTLS).
   */
  properties[PROP_ENCRYPTION] =
    g_param_spec_enum ("encryption", NULL, NULL,
                       GOA_TYPE_TLS_TYPE,
                       GOA_TLS_TYPE_NONE,
                       (G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaMailConfig:hostname: (getter get_hostname) (setter set_hostname)
   *
   * The hostname.
   */
  properties[PROP_HOSTNAME] =
    g_param_spec_string ("hostname", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaMailConfig:port: (getter get_port) (setter set_port)
   *
   * The port.
   */
  properties[PROP_PORT] =
    g_param_spec_uint ("port", NULL, NULL,
                       0, G_MAXUINT16,
                       0,
                       (G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaMailConfig:uri: (getter get_username) (setter set_username)
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
 * goa_mail_config_new:
 * @service: (not nullable): a service type
 *
 * Create a new, empty service configuration.
 *
 * Returns: (transfer full): a service configuration
 */
GoaMailConfig *
goa_mail_config_new (const char *service)
{
  g_return_val_if_fail (service != NULL, NULL);

  return g_object_new (GOA_TYPE_MAIL_CONFIG,
                       "service", service,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_mail_config_get_encryption: (get-property encryption)
 * @config: a `GoaMailConfig`
 *
 * Get the encryption.
 *
 * Returns: a `GoaTlsType` encryption.
 */
GoaTlsType
goa_mail_config_get_encryption (GoaMailConfig *config)
{
  g_return_val_if_fail (GOA_IS_MAIL_CONFIG (config), GOA_TLS_TYPE_NONE);

  return config->encryption;
}

/**
 * goa_mail_config_set_encryption: (set-property encryption)
 * @config: a `GoaMailConfig`
 * @encryption: a `GoaTlsType`
 *
 * Set the encryption.
 */
void
goa_mail_config_set_encryption (GoaMailConfig *config,
                                GoaTlsType     encryption)
{
  g_return_if_fail (GOA_IS_MAIL_CONFIG (config));
  g_return_if_fail (encryption >= GOA_TLS_TYPE_NONE && encryption <= GOA_TLS_TYPE_SSL);

  if (config->encryption != encryption)
    {
      config->encryption = encryption;
      g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_ENCRYPTION]);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_mail_config_get_hostname: (get-property hostname)
 * @config: a `GoaMailConfig`
 *
 * Get the hostname.
 *
 * Returns: (transfer none) (nullable): a hostname, or %NULL if unknown
 */
const char *
goa_mail_config_get_hostname (GoaMailConfig *config)
{
  g_return_val_if_fail (GOA_IS_MAIL_CONFIG (config), NULL);

  return config->hostname;
}

/**
 * goa_mail_config_set_hostname: (set-property hostname)
 * @config: a `GoaMailConfig`
 * @hostname: (nullable): a hostname
 *
 * Set the hostname.
 */
void
goa_mail_config_set_hostname (GoaMailConfig *config,
                              const char    *hostname)
{
  g_return_if_fail (GOA_IS_MAIL_CONFIG (config));

  if (g_set_str (&config->hostname, hostname))
    g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_HOSTNAME]);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_mail_config_get_port: (get-property port)
 * @config: a `GoaMailConfig`
 *
 * Get the port.
 *
 * If the port is unset and the service is IMAP or SMTP, a default port will
 * be returned based on [property@Goa.MailConfig:encryption].
 *
 * Returns: a port
 */
uint16_t
goa_mail_config_get_port (GoaMailConfig *config)
{
  g_return_val_if_fail (GOA_IS_MAIL_CONFIG (config), 0);

  if (config->port == 0)
    {
      const char *service = NULL;

      service = goa_service_config_get_service (GOA_SERVICE_CONFIG (config));
      if (g_ascii_strcasecmp (service, GOA_SERVICE_TYPE_IMAP) == 0)
        {
          return (config->encryption == GOA_TLS_TYPE_SSL) ? 993 : 143;
        }
      else if (g_ascii_strcasecmp (service, GOA_SERVICE_TYPE_SMTP) == 0)
        {
          return (config->encryption == GOA_TLS_TYPE_SSL) ? 465 : 587;
        }
    }

  return config->port;
}

/**
 * goa_mail_config_set_port: (set-property port)
 * @config: a `GoaMailConfig`
 * @port: a valid port
 *
 * Set the port.
 */
void
goa_mail_config_set_port (GoaMailConfig *config,
                          uint16_t       port)
{
  g_return_if_fail (GOA_IS_MAIL_CONFIG (config));
  g_return_if_fail (port <= G_MAXUINT16);

  if (config->port != port)
    {
      config->port = port;
      g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_PORT]);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_mail_config_get_username: (get-property username)
 * @config: a `GoaMailConfig`
 *
 * Get the username, or %NULL if unknown.
 *
 * Returns: (transfer none) (nullable): a username
 */
const char *
goa_mail_config_get_username (GoaMailConfig *config)
{
  g_return_val_if_fail (GOA_IS_MAIL_CONFIG (config), NULL);

  return config->username;
}

/**
 * goa_mail_config_set_username: (set-property username)
 * @config: a `GoaMailConfig`
 * @username: (nullable): a username or email
 *
 * Set the username.
 */
void
goa_mail_config_set_username (GoaMailConfig *config,
                              const char    *username)
{
  g_return_if_fail (GOA_IS_MAIL_CONFIG (config));

  if (g_set_str (&config->username, username))
    g_object_notify_by_pspec (G_OBJECT (config), properties[PROP_USERNAME]);
}

