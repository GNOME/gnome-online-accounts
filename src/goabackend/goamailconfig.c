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

#include <glib/gprintf.h>
#include <gio/gio.h>

#include "goabackendenums.h"
#include "goabackendenumtypes.h"
#include "goamailconfig.h"

struct _GoaMailConfig
{
  GoaServiceConfig parent_instance;

  char *hostname;
  uint16_t port;
  char *username;
};

G_DEFINE_FINAL_TYPE (GoaMailConfig, goa_mail_config, GOA_TYPE_SERVICE_CONFIG)

typedef enum
{
  PROP_HOSTNAME = 1,
  PROP_PORT,
  PROP_USERNAME,
} GoaMailConfigProperty;

static GParamSpec *properties[PROP_USERNAME + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

inline void
goa_mail_config_build_variant (GoaServiceConfig *config,
                               GVariantBuilder  *builder)
{
  GoaMailConfig *self = GOA_MAIL_CONFIG (config);
  const char *service = "unknown";

  g_return_if_fail (GOA_IS_MAIL_CONFIG (self));
  g_return_if_fail (builder != NULL);

  service = goa_service_config_get_service (config);
  if (g_str_equal (service, GOA_SERVICE_TYPE_IMAP))
    {
      g_variant_builder_add (builder, "{ss}", "ImapHost", self->hostname);
      g_variant_builder_add (builder, "{ss}", "ImapUserName", self->username);
      g_variant_builder_add (builder, "{ss}", "ImapUseSsl", "true");
      g_variant_builder_add (builder, "{ss}", "ImapUseTls", "true");
      g_variant_builder_add (builder, "{ss}", "ImapAcceptSslErrors", "false");
    }
  else if (g_str_equal (service, GOA_SERVICE_TYPE_SMTP))
    {
      g_variant_builder_add (builder, "{ss}", "SmtpHost", self->hostname);
      g_variant_builder_add (builder, "{ss}", "SmtpUserName", self->username);
      g_variant_builder_add (builder, "{ss}", "SmtpUseSsl", "true");
      g_variant_builder_add (builder, "{ss}", "SmtpUseTls", "true");
      g_variant_builder_add (builder, "{ss}", "SmtpUseAuth", "true");
      g_variant_builder_add (builder, "{ss}", "SmtpAuthLogin", "true");
      g_variant_builder_add (builder, "{ss}", "SmtpAuthPlain", "true");
      g_variant_builder_add (builder, "{ss}", "SmtpAcceptSslErrors", "false");
    }
}

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
GoaServiceConfig *
goa_mail_config_new (const char *service)
{
  return g_object_new (GOA_TYPE_MAIL_CONFIG,
                       "service", service,
                       NULL);
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
 * Returns: a port, or `0` for protocol default
 */
uint16_t
goa_mail_config_get_port (GoaMailConfig *config)
{
  g_return_val_if_fail (GOA_IS_MAIL_CONFIG (config), 0);

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

