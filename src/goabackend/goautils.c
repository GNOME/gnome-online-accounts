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
#include <gnome-keyring.h>

#include "goaprovider.h"
#include "goautils.h"

static const GnomeKeyringPasswordSchema keyring_password_schema =
{
  GNOME_KEYRING_ITEM_GENERIC_SECRET,
  {
    { "goa-identity", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
    { NULL, 0 }
  }
};

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

GtkWidget *
goa_utils_create_add_refresh_label (GoaProvider *provider, gboolean add_account)
{
  GtkWidget *label;
  gchar *markup;
  gchar *provider_name;
  gchar *template;

  label = gtk_label_new (NULL);

  /* Translators: the %s is the name of the provider. eg., Google. */
  template = g_strconcat ("<b>", (add_account) ? _("Add %s") : _("Refresh %s"), "</b>", NULL);
  provider_name = goa_provider_get_provider_name (provider, NULL);
  markup = g_strdup_printf (template, provider_name);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);
  g_free (provider_name);
  g_free (template);

  return label;
}

gboolean
goa_utils_delete_credentials_sync (GoaProvider   *provider,
                                   GoaAccount    *object,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  gboolean ret;
  gchar *password_key;
  GnomeKeyringResult result;
  const gchar *identity;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_ACCOUNT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* TODO: use GCancellable */
  ret = FALSE;

  password_key = NULL;

  identity = goa_account_get_id (object);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  identity);

  result = gnome_keyring_delete_password_sync (&keyring_password_schema,
                                               "goa-identity", password_key,
                                               NULL);
  if (result != GNOME_KEYRING_RESULT_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to delete credentials from the keyring: %s"),
                   gnome_keyring_result_to_message (result));
      goto out;
    }

  ret = TRUE;

 out:
  g_free (password_key);
  return ret;
}

GVariant *
goa_utils_lookup_credentials_sync (GoaProvider   *provider,
                                   GoaObject     *object,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  gchar *password_key;
  GVariant *ret;
  GnomeKeyringResult result;
  gchar *returned_password;
  const gchar *identity;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  password_key = NULL;
  returned_password = NULL;

  identity = goa_account_get_id (goa_object_peek_account (object));

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  identity);

  result = gnome_keyring_find_password_sync (&keyring_password_schema,
                                             &returned_password,
                                             "goa-identity", password_key,
                                             NULL);
  if (result != GNOME_KEYRING_RESULT_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to retrieve credentials from the keyring: %s"),
                   gnome_keyring_result_to_message (result));
      goto out;
    }

  ret = g_variant_parse (NULL, /* GVariantType */
                         returned_password,
                         NULL, /* limit */
                         NULL, /* endptr */
                         error);
  if (ret == NULL)
    {
      g_prefix_error (error, _("Error parsing result obtained from the keyring: "));
      goto out;
    }

  if (g_variant_is_floating (ret))
    g_variant_ref_sink (ret);

 out:
  gnome_keyring_free_password (returned_password);
  g_free (password_key);
  return ret;
}

gboolean
goa_utils_store_credentials_sync (GoaProvider   *provider,
                                  GoaObject     *object,
                                  GVariant      *credentials,
                                  GCancellable  *cancellable,
                                  GError       **error)
{
  gboolean ret;
  gchar *credentials_str;
  gchar *password_description;
  gchar *password_key;
  GnomeKeyringResult result;
  const gchar *identity;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (credentials != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* TODO: use GCancellable */
  ret = FALSE;

  identity = goa_account_get_id (goa_object_peek_account (object));

  credentials_str = g_variant_print (credentials, TRUE);
  g_variant_ref_sink (credentials);
  g_variant_unref (credentials);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  identity);
  /* Translators: The %s is the type of the provider, e.g. 'google' or 'yahoo' */
  password_description = g_strdup_printf (_("GOA %s credentials for identity %s"),
                                          goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                          identity);
  result = gnome_keyring_store_password_sync (&keyring_password_schema,
                                              NULL, /* default keyring */
                                              password_description,
                                              credentials_str,
                                              "goa-identity", password_key,
                                              NULL);
  if (result != GNOME_KEYRING_RESULT_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to store credentials in the keyring: %s"),
                   gnome_keyring_result_to_message (result));
      goto out;
    }

  ret = TRUE;

 out:
  g_free (credentials_str);
  g_free (password_key);
  g_free (password_description);
  return ret;
}
