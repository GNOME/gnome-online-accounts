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

typedef struct
{
  GObject parent_instance;

  char *method;
} GoaServiceAuthPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GoaServiceAuth, goa_service_auth, G_TYPE_OBJECT)

typedef enum
{
  PROP_METHOD = 1,
} GoaServiceAuthProperty;

static GParamSpec *properties[PROP_METHOD + 1] = { NULL, };

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_service_auth_finalize (GObject *object)
{
  GoaServiceAuth *self = GOA_SERVICE_AUTH (object);
  GoaServiceAuthPrivate *priv = goa_service_auth_get_instance_private (self);

  g_clear_pointer (&priv->method, g_free);

  G_OBJECT_CLASS (goa_service_auth_parent_class)->finalize (object);
}

static void
goa_service_auth_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GoaServiceAuth *self = GOA_SERVICE_AUTH (object);
  GoaServiceAuthPrivate *priv = goa_service_auth_get_instance_private (self);

  switch ((GoaServiceAuthProperty) prop_id)
    {
    case PROP_METHOD:
      g_value_set_string (value, priv->method);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_service_auth_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GoaServiceAuth *self = GOA_SERVICE_AUTH (object);
  GoaServiceAuthPrivate *priv = goa_service_auth_get_instance_private (self);

  switch ((GoaServiceAuthProperty) prop_id)
    {
    case PROP_METHOD:
      g_assert (priv->method == NULL);
      priv->method = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_service_auth_init (GoaServiceAuth *self)
{
}

static void
goa_service_auth_class_init (GoaServiceAuthClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = goa_service_auth_finalize;
  object_class->get_property = goa_service_auth_get_property;
  object_class->set_property = goa_service_auth_set_property;

  /**
   * GoaServiceProvider:method: (getter get_method)
   *
   * The authentication method, such as `OAuth2` or `password-cleartext`.
   */
  properties[PROP_METHOD] =
    g_param_spec_string ("method", NULL, NULL,
                         "unknown",
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_service_auth_get_method: (get-property method)
 * @auth: a `GoaServiceAuth`
 *
 * Get the authentication method of @auth, or "unknown" if not set.
 *
 * Returns: (transfer none) (not nullable): an authentication method
 */
const char *
goa_service_auth_get_method (GoaServiceAuth *auth)
{
  GoaServiceAuthPrivate *priv = goa_service_auth_get_instance_private (auth);

  g_return_val_if_fail (GOA_IS_SERVICE_AUTH (auth), "unknown");

  return priv->method;
}

