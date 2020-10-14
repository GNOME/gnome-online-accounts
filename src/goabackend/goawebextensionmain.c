/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2015 Damián Nohales
 * Copyright © 2015 – 2017 Red Hat, Inc.
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

#include <gmodule.h>
#include <webkit2/webkit-web-extension.h>

#include "goawebextension.h"

static GoaWebExtension *the_extension;

/* Silence -Wmissing-prototypes */
void webkit_web_extension_initialize_with_user_data (WebKitWebExtension *wk_extension, GVariant *user_data);

G_MODULE_EXPORT void
webkit_web_extension_initialize_with_user_data (WebKitWebExtension *wk_extension, GVariant *user_data)
{
  const gchar *existing_identity;
  const gchar *provider_type;

  g_variant_get (user_data, "(&s&s)", &provider_type, &existing_identity);
  the_extension = goa_web_extension_new (wk_extension, provider_type, existing_identity);
}

static void __attribute__((destructor))
goa_web_extension_shutdown (void)
{
  g_clear_object (&the_extension);
}
