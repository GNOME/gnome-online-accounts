/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012, 2013, 2015 Red Hat, Inc.
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

#include <glib/gi18n-lib.h>
#include <libsecret/secret.h>
#include <telepathy-glib/telepathy-glib.h>

#include "goaprovider.h"
#include "goautils.h"

static const SecretSchema secret_password_schema =
{
  "org.gnome.OnlineAccounts", SECRET_SCHEMA_DONT_MATCH_NAME,
  {
    { "goa-identity", SECRET_SCHEMA_ATTRIBUTE_STRING },
    { "NULL", 0 }
  }
};

void
goa_utils_initialize_client_factory (void)
{
  static gsize once_init_value = 0;

  if (g_once_init_enter (&once_init_value))
    {
      TpSimpleClientFactory *factory;
      TpAccountManager *account_manager;
      GQuark account_features[] = {TP_ACCOUNT_FEATURE_ADDRESSING,
                                   TP_ACCOUNT_FEATURE_STORAGE,
                                   TP_ACCOUNT_FEATURE_CONNECTION,
                                   0};
      GQuark connection_features[] = {TP_CONNECTION_FEATURE_AVATAR_REQUIREMENTS,
                                      TP_CONNECTION_FEATURE_CONTACT_INFO,
                                      0};

      /* We make sure that new instances of Telepathy objects will have all
       * the features we need.
       */
      factory = tp_simple_client_factory_new (NULL);
      tp_simple_client_factory_add_account_features (factory, account_features);
      tp_simple_client_factory_add_connection_features (factory, connection_features);

      account_manager = tp_account_manager_new_with_factory (factory);
      tp_account_manager_set_default (account_manager);

      g_object_unref (account_manager);
      g_object_unref (factory);

      g_once_init_leave (&once_init_value, 1);
    }
}

gboolean
goa_utils_check_duplicate (GoaClient              *client,
                           const gchar            *identity,
                           const gchar            *presentation_identity,
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
      const gchar *presentation_identity_from_object;
      const gchar *provider_type_from_object;

      account = goa_object_peek_account (object);
      interface = (*func) (object);
      if (interface == NULL)
        continue;

      provider_type_from_object = goa_account_get_provider_type (account);
      if (g_strcmp0 (provider_type_from_object, provider_type) != 0)
        continue;

      identity_from_object = goa_account_get_identity (account);
      presentation_identity_from_object = goa_account_get_presentation_identity (account);
      if (g_strcmp0 (identity_from_object, identity) == 0
          && g_strcmp0 (presentation_identity_from_object, presentation_identity) == 0)
        {
          const gchar *provider_name;

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
  const gchar *id;
  GError *sec_error = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_ACCOUNT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  password_key = NULL;

  id = goa_account_get_id (object);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  id);

  secret_password_clear_sync (&secret_password_schema,
                              cancellable,
                              &sec_error,
                              "goa-identity", password_key,
                              NULL);
  if (sec_error != NULL)
    {
      g_warning ("secret_password_clear_sync() failed: %s", sec_error->message);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to delete credentials from the keyring"));
      g_error_free (sec_error);
      goto out;
    }

  g_debug ("Cleared keyring credentials for id: %s", id);
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
  const gchar *id;
  GError *sec_error = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  password_key = NULL;
  password = NULL;

  id = goa_account_get_id (goa_object_peek_account (object));

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                  goa_provider_get_credentials_generation (GOA_PROVIDER (provider)),
                                  id);

  password = secret_password_lookup_sync (&secret_password_schema,
                                          cancellable,
                                          &sec_error,
                                          "goa-identity", password_key,
                                          NULL);
  if (sec_error != NULL)
    {
      g_warning ("secret_password_lookup_sync() failed: %s", sec_error->message);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to retrieve credentials from the keyring"));
      g_error_free (sec_error);
      goto out;
    }
  else if (password == NULL)
    {
      g_warning ("secret_password_lookup_sync() returned NULL");
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("No credentials found in the keyring"));
      goto out;
    }

  g_debug ("Retrieved keyring credentials for id: %s", id);

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
  GError *sec_error = NULL;

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
  /* Translators: The %s is the type of the provider, e.g. 'google' or 'yahoo' */
  password_description = g_strdup_printf (_("GOA %s credentials for identity %s"),
                                          goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                          id);

  if (!secret_password_store_sync (&secret_password_schema,
                                   SECRET_COLLECTION_DEFAULT, /* default keyring */
                                   password_description,
                                   credentials_str,
                                   cancellable,
                                   &sec_error,
                                   "goa-identity", password_key,
                                   NULL))
    {
      g_warning ("secret_password_store_sync() failed: %s", sec_error->message);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to store credentials in the keyring"));
      g_error_free (sec_error);
      goto out;
    }

  g_debug ("Stored keyring credentials for identity: %s", id);
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (contents);
  g_key_file_free (key_file);
  g_free (group);
  g_free (path);
}

gboolean
goa_utils_parse_email_address (const gchar *email, gchar **out_username, gchar **out_domain)
{
  gchar *at;
  gchar *dot;

  if (email == NULL || email[0] == '\0')
    return FALSE;

  at = strchr (email, '@');
  if (at == NULL || *(at + 1) == '\0')
    return FALSE;

  dot = strchr (at + 1, '.');
  if (dot == NULL || *(dot + 1) == '\0')
    return FALSE;

  if (out_username != NULL)
    {
      *out_username = g_strdup (email);
      (*out_username)[at - email] = '\0';
    }

  if (out_domain != NULL)
    *out_domain = g_strdup (at + 1);

  return TRUE;
}

void
goa_utils_set_error_ssl (GError **err, GTlsCertificateFlags flags)
{
  const gchar *error_msg;

  switch (flags)
    {
    case G_TLS_CERTIFICATE_UNKNOWN_CA:
      error_msg = _("The signing certificate authority is not known.");
      break;

    case G_TLS_CERTIFICATE_BAD_IDENTITY:
      error_msg = _("The certificate does not match the expected identity of the site that it was "
                    "retrieved from.");
      break;

    case G_TLS_CERTIFICATE_NOT_ACTIVATED:
      error_msg = _("The certificate’s activation time is still in the future.");
      break;

    case G_TLS_CERTIFICATE_EXPIRED:
      error_msg = _("The certificate has expired.");
      break;

    case G_TLS_CERTIFICATE_REVOKED:
      error_msg = _("The certificate has been revoked.");
      break;

    case G_TLS_CERTIFICATE_INSECURE:
      error_msg = _("The certificate’s algorithm is considered insecure.");
      break;

    default:
      error_msg = _("Invalid certificate.");
      break;
    }

  g_set_error_literal (err, GOA_ERROR, GOA_ERROR_SSL, error_msg);
}
