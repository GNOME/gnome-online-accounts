/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#include "goaenums.h"
#include "goaerror.h"

static const GDBusErrorEntry dbus_error_entries[] =
{
  {GOA_ERROR_FAILED,                       "org.freedesktop.Goa.Error.Failed"},
  {GOA_ERROR_NOT_SUPPORTED,                "org.freedesktop.Goa.Error.NotSupported"},
  {GOA_ERROR_DIALOG_DISMISSED,             "org.gnome.OnlineAccounts.Error.DialogDismissed"},
  {GOA_ERROR_ACCOUNT_EXISTS,               "org.gnome.OnlineAccounts.Error.AccountExists"},
  {GOA_ERROR_NOT_AUTHORIZED,               "org.gnome.OnlineAccounts.Error.NotAuthorized"},
  {GOA_ERROR_SSL,                          "org.gnome.OnlineAccounts.Error.SSL"}
};

/**
 * goa_error_quark:
 *
 * Registers an error quark for `Goa` errors and a domain for D-Bus errors
 *
 * Returns: The error quark used for `Goa` errors.
 **/
GQuark
goa_error_quark (void)
{
  G_STATIC_ASSERT (G_N_ELEMENTS (dbus_error_entries) == GOA_ERROR_NUM_ENTRIES);
  static gsize quark = 0;
  g_dbus_error_register_error_domain ("goa-error-quark",
                                      &quark,
                                      dbus_error_entries,
                                      G_N_ELEMENTS (dbus_error_entries));
  return (GQuark) quark;
}
