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

#include <glib-object.h>
#include <glib/gi18n.h>

#include "goaidentity.h"

G_DEFINE_INTERFACE (GoaIdentity, goa_identity, G_TYPE_OBJECT);

static void
goa_identity_default_init (GoaIdentityInterface *interface)
{
  g_object_interface_install_property (interface,
                                       g_param_spec_string ("identifier",
                                                            "identifier",
                                                            "identifier",
                                                            NULL, G_PARAM_READABLE));
  g_object_interface_install_property (interface,
                                       g_param_spec_boolean ("is-signed-in",
                                                             "Is signed in",
                                                             "Whether or not identity is currently signed in",
                                                             FALSE,
                                                             G_PARAM_READABLE));
  g_object_interface_install_property (interface,
                                       g_param_spec_int64 ("start-timestamp",
                                                           "Start Timestamp",
                                                           "A timestamp of when the identities credentials first became valid",
                                                           -1,
                                                           G_MAXINT64,
                                                           -1, G_PARAM_READABLE));
  g_object_interface_install_property (interface,
                                       g_param_spec_int64 ("renewal-timestamp",
                                                           "Renewal Timestamp",
                                                           "A timestamp of when the identities credentials can no longer be renewed",
                                                           -1,
                                                           G_MAXINT64,
                                                           -1, G_PARAM_READABLE));
  g_object_interface_install_property (interface,
                                       g_param_spec_int64 ("expiration-timestamp",
                                                           "Expiration Timestamp",
                                                           "A timestamp of when the identities credentials expire",
                                                           -1,
                                                           G_MAXINT64,
                                                           -1, G_PARAM_READABLE));
}

const char *
goa_identity_get_identifier (GoaIdentity *self)
{
  return GOA_IDENTITY_GET_IFACE (self)->get_identifier (self);
}

gboolean
goa_identity_is_signed_in (GoaIdentity *self)
{
  return GOA_IDENTITY_GET_IFACE (self)->is_signed_in (self);
}
