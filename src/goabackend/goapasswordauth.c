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

#include "goaserviceauth.h"

#include "goapasswordauth.h"

typedef struct
{
  char *password;
  char *username;
} GoaPasswordAuthPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GoaPasswordAuth, goa_password_auth, GOA_TYPE_SERVICE_AUTH)

typedef enum
{
  PROP_PASSWORD = 1,
  PROP_USERNAME,
} GoaPasswordAuthProperty;

static GParamSpec *properties[PROP_USERNAME + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_password_auth_finalize (GObject *object)
{
  GoaPasswordAuth *self = GOA_PASSWORD_AUTH (object);
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (self);

  g_clear_pointer (&priv->password, g_free);
  g_clear_pointer (&priv->username, g_free);

  G_OBJECT_CLASS (goa_password_auth_parent_class)->finalize (object);
}

static void
goa_password_auth_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  GoaPasswordAuth *self = GOA_PASSWORD_AUTH (object);
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (self);

  switch ((GoaPasswordAuthProperty) prop_id)
    {
    case PROP_PASSWORD:
      g_value_set_string (value, priv->password);
      break;

    case PROP_USERNAME:
      g_value_set_string (value, priv->username);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_password_auth_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  GoaPasswordAuth *self = GOA_PASSWORD_AUTH (object);

  switch ((GoaPasswordAuthProperty) prop_id)
    {
    case PROP_PASSWORD:
      goa_password_auth_set_password (self, g_value_get_string (value));
      break;

    case PROP_USERNAME:
      goa_password_auth_set_username (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_password_auth_init (GoaPasswordAuth *self)
{
}

static void
goa_password_auth_class_init (GoaPasswordAuthClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = goa_password_auth_finalize;
  object_class->get_property = goa_password_auth_get_property;
  object_class->set_property = goa_password_auth_set_property;

  /**
   * GoaPasswordAuth:password: (getter get_password) (setter set_password)
   *
   * The DAV resource URI.
   */
  properties[PROP_PASSWORD] =
    g_param_spec_string ("password", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  /**
   * GoaPasswordAuth:password: (getter get_username) (setter set_username)
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
 * goa_password_auth_new:
 * @method: (not nullable): a method type
 * @password: (nullable): a password
 * @username: (nullable): a username or email
 *
 * Create a new DAV authuration.
 *
 * Returns: (transfer full): a service authuration
 */
GoaPasswordAuth *
goa_password_auth_new (const char *method,
                       const char *password,
                       const char *username)
{
  g_return_val_if_fail (method != NULL, NULL);
  g_return_val_if_fail (g_str_equal (method, GOA_AUTH_METHOD_PASSWORD_CLEARTEXT) ||
                        g_str_equal (method, GOA_AUTH_METHOD_PASSWORD_ENCRYPTED), NULL);

  return g_object_new (GOA_TYPE_PASSWORD_AUTH,
                       "method",   method,
                       "password", password,
                       "username", username,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_password_auth_get_password:
 * @auth: a `GoaPasswordAuth`
 *
 * Get the URI, or %NULL if unknown.
 *
 * Returns: (transfer none) (nullable): a URI
 */
const char *
goa_password_auth_get_password (GoaPasswordAuth *auth)
{
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (auth);

  g_return_val_if_fail (GOA_IS_PASSWORD_AUTH (auth), NULL);

  return priv->password;
}

/**
 * goa_password_auth_set_password:
 * @auth: a `GoaPasswordAuth`
 * @password: (nullable): a URI
 *
 * Set the URI.
 */
void
goa_password_auth_set_password (GoaPasswordAuth *auth,
                                const char      *password)
{
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (auth);

  g_return_if_fail (GOA_IS_PASSWORD_AUTH (auth));

  if (g_set_str (&priv->password, password))
    g_object_notify_by_pspec (G_OBJECT (auth), properties[PROP_PASSWORD]);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_password_auth_get_username:
 * @auth: a `GoaPasswordAuth`
 *
 * Get the username, or %NULL if unknown.
 *
 * Returns: (transfer none) (nullable): a username
 */
const char *
goa_password_auth_get_username (GoaPasswordAuth *auth)
{
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (auth);

  g_return_val_if_fail (GOA_IS_PASSWORD_AUTH (auth), NULL);

  return priv->username;
}

/**
 * goa_password_auth_set_username:
 * @auth: a `GoaPasswordAuth`
 * @username: (nullable): a username or email
 *
 * Set the username.
 */
void
goa_password_auth_set_username (GoaPasswordAuth *auth,
                             const char   *username)
{
  GoaPasswordAuthPrivate *priv = goa_password_auth_get_instance_private (auth);

  g_return_if_fail (GOA_IS_PASSWORD_AUTH (auth));

  if (g_set_str (&priv->username, username))
    g_object_notify_by_pspec (G_OBJECT (auth), properties[PROP_USERNAME]);
}

