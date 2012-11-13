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
 * Authors: Debarshi Ray <debarshir@gnome.org>
 *          Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <libsecret/secret.h>

#include "goaprovider.h"
#include "goalogging.h"
#include "goautils.h"

static const SecretSchema secret_password_schema =
{
  "org.gnome.OnlineAccounts", SECRET_SCHEMA_DONT_MATCH_NAME,
  {
    { "goa-identity", SECRET_SCHEMA_ATTRIBUTE_STRING },
    { "NULL", 0 }
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

void
goa_utils_set_dialog_title (GoaProvider *provider, GtkDialog *dialog, gboolean add_account)
{
  gchar *provider_name;
  gchar *title;

  provider_name = goa_provider_get_provider_name (GOA_PROVIDER (provider), NULL);
  /* Translators: the %s is the name of the provider. eg., Google. */
  title = g_strdup_printf (_("%s account"), provider_name);
  gtk_window_set_title (GTK_WINDOW (dialog), title);
  g_free (title);
  g_free (provider_name);
}

gboolean
goa_utils_delete_credentials_sync (GoaProvider   *provider,
                                   GoaAccount    *object,
                                   GCancellable  *cancellable,
                                   GError       **error)
{
  gboolean ret;
  gchar *password_key;
  const gchar *identity;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_ACCOUNT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  password_key = NULL;

  identity = goa_account_get_id (object);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  identity);

  if (!secret_password_clear_sync (&secret_password_schema,
                                   cancellable,
                                   NULL,
                                   "goa-identity", password_key,
                                   NULL))
    {
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to delete credentials from the keyring"));
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
  gchar *password;
  const gchar *identity;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  password_key = NULL;
  password = NULL;

  identity = goa_account_get_id (goa_object_peek_account (object));

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  identity);

  password = secret_password_lookup_sync (&secret_password_schema,
                                          cancellable,
                                          NULL,
                                          "goa-identity", password_key,
                                          NULL);
  if (password == NULL)
    {
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to retrieve credentials from the keyring"));
      goto out;
    }

  ret = g_variant_parse (NULL, /* GVariantType */
                         password,
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
  g_free (password);
  g_free (password_key);
  return ret;
}

gboolean
goa_utils_store_credentials_for_id_sync (GoaProvider   *provider,
                                         const gchar   *id,
                                         GVariant      *credentials,
                                         GCancellable  *cancellable,
                                         GError       **error)
{
  gboolean ret;
  gchar *credentials_str;
  gchar *password_description;
  gchar *password_key;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (id != NULL && id[0] != '\0', FALSE);
  g_return_val_if_fail (credentials != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  credentials_str = g_variant_print (credentials, TRUE);
  g_variant_ref_sink (credentials);
  g_variant_unref (credentials);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  id);
  /* Translators: The %s is the type of the provider, e.g. 'google' or 'facebook' */
  password_description = g_strdup_printf (_("GOA %s credentials for identity %s"),
                                          goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                          id);

  if (!secret_password_store_sync (&secret_password_schema,
                                   SECRET_COLLECTION_DEFAULT, /* default keyring */
                                   password_description,
                                   credentials_str,
                                   cancellable,
                                   NULL,
                                   "goa-identity", password_key,
                                   NULL))
    {
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to store credentials in the keyring"));
      goto out;
    }

  ret = TRUE;

 out:
  g_free (credentials_str);
  g_free (password_key);
  g_free (password_description);
  return ret;
}

gboolean
goa_utils_store_credentials_for_object_sync (GoaProvider   *provider,
                                             GoaObject     *object,
                                             GVariant      *credentials,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
  const gchar *id;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (credentials != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  id = goa_account_get_id (goa_object_peek_account (object));
  return goa_utils_store_credentials_for_id_sync (provider, id, credentials, cancellable, error);
}

void
goa_utils_keyfile_remove_key (GoaAccount *account, const gchar *key)
{
  GError *error;
  GKeyFile *key_file;
  gchar *contents;
  gchar *group;
  gchar *path;
  gsize length;

  contents = NULL;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      goa_warning ("Error loading keyfile %s: %s (%s, %d)",
                   path,
                   error->message,
                   g_quark_to_string (error->domain),
                   error->code);
      g_error_free (error);
      goto out;
    }

  g_key_file_remove_key (key_file, group, key, NULL);
  contents = g_key_file_to_data (key_file, &length, NULL);

  error = NULL;
  if (!g_file_set_contents (path, contents, length, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      goa_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (contents);
  g_key_file_free (key_file);
  g_free (group);
  g_free (path);
}

void
goa_utils_keyfile_set_boolean (GoaAccount *account, const gchar *key, gboolean value)
{
  GError *error;
  GKeyFile *key_file;
  gchar *contents;
  gchar *group;
  gchar *path;
  gsize length;

  contents = NULL;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      goa_warning ("Error loading keyfile %s: %s (%s, %d)",
                   path,
                   error->message,
                   g_quark_to_string (error->domain),
                   error->code);
      g_error_free (error);
      goto out;
    }

  g_key_file_set_boolean (key_file, group, key, value);
  contents = g_key_file_to_data (key_file, &length, NULL);

  error = NULL;
  if (!g_file_set_contents (path, contents, length, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      goa_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (contents);
  g_key_file_free (key_file);
  g_free (group);
  g_free (path);
}

void
goa_utils_keyfile_set_string (GoaAccount *account, const gchar *key, const gchar *value)
{
  GError *error;
  GKeyFile *key_file;
  gchar *contents;
  gchar *group;
  gchar *path;
  gsize length;

  contents = NULL;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      goa_warning ("Error loading keyfile %s: %s (%s, %d)",
                   path,
                   error->message,
                   g_quark_to_string (error->domain),
                   error->code);
      g_error_free (error);
      goto out;
    }

  g_key_file_set_string (key_file, group, key, value);
  contents = g_key_file_to_data (key_file, &length, NULL);

  error = NULL;
  if (!g_file_set_contents (path, contents, length, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      goa_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (contents);
  g_key_file_free (key_file);
  g_free (group);
  g_free (path);
}
