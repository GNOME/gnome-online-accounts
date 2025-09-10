/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2025 GNOME Foundation Inc.
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

#include <gio/gio.h>

#include "goaidentityerror.h"

static const GDBusErrorEntry dbus_error_entries[] =
{
  {GOA_IDENTITY_ERROR_NOT_FOUND,                  "org.gnome.Identity.Error.NotFound"},
  {GOA_IDENTITY_ERROR_VERIFYING,                  "org.gnome.Identity.Error.Verifying"},
  {GOA_IDENTITY_ERROR_RENEWING,                   "org.gnome.Identity.Error.Renewing"},
  {GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,    "org.gnome.Identity.Error.CredentialsUnavailable"},
  {GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS,    "org.gnome.Identity.Error.EnumeratingCredentials"},
  {GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,     "org.gnome.Identity.Error.AllocatingCredentials"},
  {GOA_IDENTITY_ERROR_AUTHENTICATION_FAILED,      "org.gnome.Identity.Error.AuthenticationFailed"},
  {GOA_IDENTITY_ERROR_SAVING_CREDENTIALS,         "org.gnome.Identity.Error.SavingCredentials"},
  {GOA_IDENTITY_ERROR_REMOVING_CREDENTIALS,       "org.gnome.Identity.Error.RemovingCredentials"},
  {GOA_IDENTITY_ERROR_PARSING_IDENTIFIER,         "org.gnome.Identity.Error.ParsingIdentifier"}
};

GQuark
goa_identity_error_quark (void)
{
  G_STATIC_ASSERT (G_N_ELEMENTS (dbus_error_entries) == GOA_IDENTITY_ERROR_NUM_ENTRIES);
  static gsize quark = 0;
  g_dbus_error_register_error_domain ("goa-identity-error",
                                      &quark,
                                      dbus_error_entries,
                                      G_N_ELEMENTS (dbus_error_entries));
  return (GQuark) quark;
}
