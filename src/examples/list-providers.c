/*
 * Copyright (C) 2012 Intel Corp
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Ross Burton <ross.burton@intel.com>
 */

#include <config.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goabackend/goabackend.h>

int
main (int argc, char **argv)
{
  GoaProvider *provider;
  GList *providers, *l;

  g_type_init ();

  providers = goa_provider_get_all ();
  for (l = providers; l != NULL; l = l->next) {
    char *provider_name;

    provider = GOA_PROVIDER (l->data);
    provider_name = goa_provider_get_provider_name (provider, NULL);
    g_print ("Got provider %s\n", provider_name);
    g_free (provider_name);
    provider = NULL;
  }

  return 0;
}
