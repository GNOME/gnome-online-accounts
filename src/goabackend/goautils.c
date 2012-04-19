/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goautils.h"

gboolean
goa_utils_check_duplicate (GoaClient              *client,
                           const gchar            *identity,
                           const gchar            *provider_type,
                           GoaPeekInterfaceFunc    func,
                           GError                **error)
{
  GList *accounts;
  GList *l;
  gboolean ret;

  ret = FALSE;

  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      GoaAccount *account;
      gpointer *interface;
      const gchar *identity_from_object;
      const gchar *provider_type_from_object;

      account = goa_object_peek_account (object);
      interface = (*func) (object);
      if (interface == NULL)
        continue;

      provider_type_from_object = goa_account_get_provider_type (account);
      if (g_strcmp0 (provider_type_from_object, provider_type) != 0)
        continue;

      identity_from_object = goa_account_get_identity (account);
      if (g_strcmp0 (identity_from_object, identity) == 0)
        {
          const gchar *presentation_identity;
          const gchar *provider_name;

          presentation_identity = goa_account_get_presentation_identity (account);
          provider_name = goa_account_get_provider_name (account);
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_ACCOUNT_EXISTS,
                       _("A %s account already exists for %s"),
                       provider_name,
                       presentation_identity);
          goto out;
        }
    }

  ret = TRUE;

 out:
  g_list_free_full (accounts, g_object_unref);
  return ret;
}
