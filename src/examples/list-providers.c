/*
 * Copyright © 2012 Intel Corp
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

#include <config.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#define GOA_BACKEND_API_IS_SUBJECT_TO_CHANGE
#include <goabackend/goabackend.h>

#include <locale.h>

typedef struct
{
  GMainLoop *loop;
  GList *providers;
  GError *error;
} GetAllData;

static void
get_all_cb (GObject      *source,
            GAsyncResult *res,
            gpointer      user_data)
{
  GetAllData *data = user_data;

  goa_provider_get_all_finish (&data->providers, res, &data->error);
  g_main_loop_quit (data->loop);
}

int
main (int argc, char **argv)
{
  GetAllData data = {0,};
  GoaProvider *provider;
  GList *l;

  setlocale (LC_ALL, "");

  data.loop = g_main_loop_new (NULL, FALSE);
  goa_provider_get_all (get_all_cb, &data);
  g_main_loop_run (data.loop);

  if (data.error != NULL) {
    g_printerr ("Failed to list providers: %s (%s, %d)\n",
        data.error->message,
        g_quark_to_string (data.error->domain),
        data.error->code);
    g_error_free (data.error);
    goto out;
  }

  for (l = data.providers; l != NULL; l = l->next) {
    char *provider_name;

    provider = GOA_PROVIDER (l->data);
    provider_name = goa_provider_get_provider_name (provider, NULL);
    g_print ("Got provider %s\n", provider_name);
    g_free (provider_name);
    provider = NULL;
  }

out:
  g_main_loop_unref (data.loop);
  g_list_free_full (data.providers, g_object_unref);

  return 0;
}
