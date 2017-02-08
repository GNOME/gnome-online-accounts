/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 – 2017 Red Hat, Inc.
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

#include <glib-unix.h>

#include <locale.h>
#include <gio/gio.h>
#include <libintl.h>

#include "goaidentityservice.h"

int
main (int    argc,
      char **argv)
{
  GMainLoop *loop;
  GoaIdentityService *service;
  GError *error;
  int ret = 1;

  setlocale (LC_ALL, "");
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  loop = g_main_loop_new (NULL, FALSE);
  service = goa_identity_service_new ();

  error = NULL;
  goa_identity_service_activate (service, &error);

  if (error != NULL) {
      g_warning ("couldn't activate identity service: %s", error->message);
      g_error_free (error);
      goto out;
  }

  g_main_loop_run (loop);

  goa_identity_service_deactivate (service);

  ret = 0;
out:
  g_object_unref (service);
  g_main_loop_unref (loop);

  return ret;
}
