/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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
                           const gchar            *presentation_identity,
                           const gchar            *provider_type,
                           GoaPeekInterfaceFunc    func,
                           GError                **error)
{
  GList *accounts;
  GList *l;
  gboolean ret = FALSE;

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
      if (func != NULL)
        {
          interface = (*func) (object);
          if (interface == NULL)
            continue;
        }

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

gint
goa_utils_convert_abs_usec_to_duration_sec (gint64 abs_usec)
{
  gint64 now;
  gint64 ret;

  now = g_get_real_time ();
  ret = abs_usec - now;
  ret /= 1000L * 1000L;
  return (gint) ret;
}

gint64
goa_utils_convert_duration_sec_to_abs_usec (gint duration_sec)
{
  gint64 now;
  gint64 ret;

  now = g_get_real_time ();
  ret = now + ((gint64) duration_sec) * 1000L * 1000L;
  return ret;
}

gchar *
goa_utils_data_input_stream_read_line (GDataInputStream  *stream,
                                       gsize             *length,
                                       GCancellable      *cancellable,
                                       GError           **error)
{
  GError *local_error = NULL;
  gchar *ret = NULL;

  ret = g_data_input_stream_read_line (stream, length, cancellable, &local_error);

  /* Handle g_data_input_stream_read_line returning NULL without
   * setting an error when there was no content to read.
   */
  if (ret == NULL && local_error == NULL)
    {
      g_set_error (&local_error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Could not parse response"));
    }

  if (local_error != NULL)
    g_propagate_error (error, local_error);

  return ret;
}

gboolean
goa_utils_delete_credentials_for_account_sync (GoaProvider   *provider,
                                               GoaAccount    *object,
                                               GCancellable  *cancellable,
                                               GError       **error)
{
  const gchar *id;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_ACCOUNT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  id = goa_account_get_id (object);
  return goa_utils_delete_credentials_for_id_sync (provider, id, cancellable, error);
}

gboolean
goa_utils_delete_credentials_for_id_sync (GoaProvider   *provider,
                                          const gchar   *id,
                                          GCancellable  *cancellable,
                                          GError       **error)
{
  gboolean ret = FALSE;
  gchar *password_key = NULL;
  GError *sec_error = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (id != NULL && id[0] != '\0', FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (provider),
                                  goa_provider_get_credentials_generation (provider),
                                  id);

  secret_password_clear_sync (&secret_password_schema,
                              cancellable,
                              &sec_error,
                              "goa-identity", password_key,
                              NULL);
  if (sec_error != NULL)
    {
      g_debug ("secret_password_clear_sync() failed: %s", sec_error->message);
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
  gchar *password_key = NULL;
  GVariant *ret = NULL;
  gchar *password = NULL;
  const gchar *id;
  GError *sec_error = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  id = goa_account_get_id (goa_object_peek_account (object));

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (provider),
                                  goa_provider_get_credentials_generation (provider),
                                  id);

  password = secret_password_lookup_sync (&secret_password_schema,
                                          cancellable,
                                          &sec_error,
                                          "goa-identity", password_key,
                                          NULL);
  if (sec_error != NULL)
    {
      g_debug ("secret_password_lookup_sync() failed: %s", sec_error->message);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("Failed to retrieve credentials from the keyring"));
      g_error_free (sec_error);
      goto out;
    }
  else if (password == NULL)
    {
      g_debug ("secret_password_lookup_sync() returned NULL");
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
  gboolean ret = FALSE;
  gchar *credentials_str;
  gchar *password_description;
  gchar *password_key;
  GError *sec_error = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (id != NULL && id[0] != '\0', FALSE);
  g_return_val_if_fail (credentials != NULL, FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  credentials_str = g_variant_print (credentials, TRUE);
  g_variant_ref_sink (credentials);
  g_variant_unref (credentials);

  password_key = g_strdup_printf ("%s:gen%d:%s",
                                  goa_provider_get_provider_type (provider),
                                  goa_provider_get_credentials_generation (provider),
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
      g_debug ("secret_password_store_sync() failed: %s", sec_error->message);
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

gboolean
goa_utils_keyfile_copy_group (GKeyFile     *src_key_file,
                              const gchar  *src_group_name,
                              GKeyFile     *dest_key_file,
                              const gchar  *dest_group_name)
{
  GError *error;
  gboolean ret_val = FALSE;
  gchar **keys = NULL;
  gsize i;

  error = NULL;
  keys = g_key_file_get_keys (src_key_file, src_group_name, NULL, &error);
  if (error != NULL)
    {
      g_debug ("Error getting keys from group %s: %s (%s, %d)",
               src_group_name,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      g_error_free (error);
      goto out;
    }

  for (i = 0; keys[i] != NULL; i++)
    {
      gchar *dest_value;
      gchar *src_value;

      error = NULL;
      src_value = g_key_file_get_value (src_key_file, src_group_name, keys[i], &error);
      if (error != NULL)
        {
          g_debug ("Error reading key %s from group %s: %s (%s, %d)",
                   keys[i],
                   src_group_name,
                   error->message,
                   g_quark_to_string (error->domain),
                   error->code);
          g_error_free (error);
          continue;
        }

      error = NULL;
      dest_value = g_key_file_get_value (dest_key_file, dest_group_name, keys[i], &error);
      if (error != NULL)
        {
          if (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
              && !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND))
            {
              g_debug ("Error reading key %s from group %s: %s (%s, %d)",
                       keys[i],
                       src_group_name,
                       error->message,
                       g_quark_to_string (error->domain),
                       error->code);
            }

          g_error_free (error);
        }

      if (g_strcmp0 (dest_value, src_value) != 0)
        {
          g_key_file_set_value (dest_key_file, dest_group_name, keys[i], src_value);
          ret_val = TRUE;
        }

      g_free (dest_value);
      g_free (src_value);
    }

 out:
  g_strfreev (keys);
  return ret_val;
}

gboolean
goa_utils_keyfile_get_boolean (GKeyFile *key_file, const gchar *group_name, const gchar *key)
{
  GError *error;
  gboolean ret;

  error = NULL;
  ret = g_key_file_get_boolean (key_file, group_name, key, &error);
  if (error != NULL)
    {
      if (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND))
        {
          g_debug ("Error reading key %s from group %s in keyfile: %s (%s, %d)",
                   key,
                   group_name,
                   error->message,
                   g_quark_to_string (error->domain),
                   error->code);
        }

      g_error_free (error);
    }

  return ret;
}

void
goa_utils_keyfile_remove_key (GoaAccount *account, const gchar *key)
{
  GError *error;
  GKeyFile *key_file;
  gchar *group;
  gchar *path;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      g_debug ("Error loading keyfile %s: %s (%s, %d)",
               path,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      g_error_free (error);
      goto out;
    }

  if (!g_key_file_remove_key (key_file, group, key, NULL))
    goto out;

  error = NULL;
  if (!g_key_file_save_to_file (key_file, path, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      g_debug ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_key_file_unref (key_file);
  g_free (group);
  g_free (path);
}

void
goa_utils_keyfile_set_boolean (GoaAccount *account, const gchar *key, gboolean value)
{
  GError *error;
  GKeyFile *key_file;
  gboolean needs_update = FALSE;
  gboolean old_value;
  gchar *group;
  gchar *path;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      g_debug ("Error loading keyfile %s: %s (%s, %d)",
               path,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      g_error_free (error);
      goto out;
    }

  error = NULL;
  old_value = g_key_file_get_boolean (key_file, group, key, &error);
  if (error != NULL)
    {
      g_debug ("Error reading key %s from keyfile %s: %s (%s, %d)",
               key,
               path,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      needs_update = TRUE;
      g_error_free (error);
    }
  else if (old_value != value)
    {
      needs_update = TRUE;
    }

  if (!needs_update)
    goto out;

  g_key_file_set_boolean (key_file, group, key, value);

  error = NULL;
  if (!g_key_file_save_to_file (key_file, path, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      g_debug ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_key_file_unref (key_file);
  g_free (group);
  g_free (path);
}

void
goa_utils_keyfile_set_string (GoaAccount *account, const gchar *key, const gchar *value)
{
  GError *error;
  GKeyFile *key_file;
  gboolean needs_update = FALSE;
  gchar *group;
  gchar *old_value = NULL;
  gchar *path;

  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                  &error))
    {
      g_debug ("Error loading keyfile %s: %s (%s, %d)",
               path,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      g_error_free (error);
      goto out;
    }

  error = NULL;
  old_value = g_key_file_get_string (key_file, group, key, &error);
  if (error != NULL)
    {
      g_debug ("Error reading key %s from keyfile %s: %s (%s, %d)",
               key,
               path,
               error->message,
               g_quark_to_string (error->domain),
               error->code);
      needs_update = TRUE;
      g_error_free (error);
    }
  else if (g_strcmp0 (old_value, value) != 0)
    {
      needs_update = TRUE;
    }

  if (!needs_update)
    goto out;

  g_key_file_set_string (key_file, group, key, value);

  error = NULL;
  if (!g_key_file_save_to_file (key_file, path, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      g_debug ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_key_file_unref (key_file);
  g_free (group);
  g_free (old_value);
  g_free (path);
}

gboolean
goa_utils_parse_email_address (const gchar *email, gchar **out_username, gchar **out_domain)
{
  gchar *at;

  if (email == NULL || email[0] == '\0')
    return FALSE;

  at = strchr (email, '@');
  if (at == NULL || at == email || *(at + 1) == '\0')
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

/**
 * goa_utils_normalize_url:
 * @base_uri: a URI
 * @uri_ref: (nullable): an absolute or relative URI
 * @server: (nullable) (out): location for server name
 *
 * Normalize @base_uri to an http(s) URL, with a trailing `/`. The port will be
 * included if and only if it is non-standard for the URL type.
 *
 * If @uri_ref is given it will be resolved relative to @base_uri, before
 * the trailing `/` is applied.
 *
 * If @server is not %NULL, it will be set to the hostname and path, including
 * the port (if non-standard).
 *
 * Returns: (transfer full): a new http(s) URL string, or %NULL
 */
char *
goa_utils_normalize_url (const char  *base_uri,
                         const char  *uri_ref,
                         char       **server)
{
  g_autoptr (GUri) uri = NULL;
  g_autoptr (GUri) uri_out = NULL;
  const char *scheme;
  const char *path;
  g_autofree char *new_path = NULL;
  g_autofree char *uri_string = NULL;
  int std_port = 0;

  g_return_val_if_fail (base_uri != NULL && *base_uri != '\0', NULL);
  g_return_val_if_fail (server == NULL || *server == NULL, NULL);

  /* dav(s) is used by DNS-SD and gvfs */
  scheme = g_uri_peek_scheme (base_uri);
  if (scheme == NULL)
    {
      uri_string = g_strconcat ("https://", base_uri, NULL);
      scheme = "https";
      std_port = 443;
    }
  else if (g_str_equal (scheme, "https")
           || g_str_equal (scheme, "davs"))
    {
      uri_string = g_strdup (base_uri);
      scheme = "https";
      std_port = 443;
    }
  else if (g_str_equal (scheme, "http")
           || g_str_equal (scheme, "dav"))
    {
      uri_string = g_strdup (base_uri);
      scheme = "http";
      std_port = 80;
    }
  else
    {
      g_debug ("%s(): Unsupported URI scheme \"%s\"", G_STRFUNC, scheme);
      return NULL;
    }

  uri = g_uri_parse (uri_string, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
  if (uri == NULL || g_uri_get_host (uri) == NULL || g_uri_get_host (uri)[0] == '\0')
    return NULL;

  if (uri_ref != NULL)
    {
      uri_out = g_uri_parse_relative (uri, uri_ref, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
      if (uri_out == NULL)
        return NULL;

      g_uri_unref (uri);
      uri = g_steal_pointer (&uri_out);
    }

  path = g_uri_get_path (uri);
  if (!g_str_has_suffix (path, "/"))
    new_path = g_strconcat (path, "/", NULL);

  uri_out = g_uri_build (g_uri_get_flags (uri),
                         scheme,
                         g_uri_get_userinfo (uri),
                         g_uri_get_host (uri),
                         g_uri_get_port (uri),
                         new_path ? new_path : path,
                         g_uri_get_query (uri),
                         g_uri_get_fragment (uri));

  if (server != NULL)
    {
      g_autofree char *port_string = NULL;
      g_autofree char *pretty_path = NULL;
      int port;

      port = g_uri_get_port (uri_out);
      port_string = g_strdup_printf (":%d", port);

      path = g_uri_get_path (uri_out);
      pretty_path = g_strndup (path, strlen (path) - 1);

      *server = g_strconcat (g_uri_get_host (uri), (port == std_port || port == -1) ? "" : port_string, pretty_path, NULL);
    }

  return g_uri_to_string (uri_out);
}

void
goa_utils_set_error_soup (GError **err, SoupMessage *msg)
{
  gchar *error_msg = NULL;
  gint error_code = GOA_ERROR_FAILED; /* TODO: more specific */
  guint status_code;

  if (err && *err)
    {
      g_debug ("%s(): amending error (%s:%u:%s)",
               G_STRFUNC,
               g_quark_to_string ((*err)->domain),
               (*err)->code,
               (*err)->message);
    }

  status_code = soup_message_get_status (msg);
  switch (status_code)
    {
    case SOUP_STATUS_METHOD_NOT_ALLOWED:
    case SOUP_STATUS_INTERNAL_SERVER_ERROR:
    case SOUP_STATUS_NOT_IMPLEMENTED:
      error_msg = g_strdup (_("Not supported"));
      break;

    case SOUP_STATUS_NOT_FOUND:
      error_msg = g_strdup (_("Not found"));
      break;

    case SOUP_STATUS_UNAUTHORIZED:
    case SOUP_STATUS_FORBIDDEN:
    case SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED:
    case SOUP_STATUS_PRECONDITION_FAILED:
      {
        error_msg = g_strdup (_("Authentication failed"));
        error_code = GOA_ERROR_NOT_AUTHORIZED;
      }
      break;

    default:
      error_msg = g_strdup_printf (_("Code: %u — Unexpected response from server"), status_code);
      break;
    }

  g_set_error_literal (err, GOA_ERROR, error_code, error_msg);
  g_free (error_msg);
}

void
goa_utils_set_error_ssl (GError **err, GTlsCertificateFlags flags)
{
  const gchar *error_msg;

  if (err && *err)
    {
      g_debug ("%s(): amending error (%s:%u:%s)",
               G_STRFUNC,
               g_quark_to_string ((*err)->domain),
               (*err)->code,
               (*err)->message);
    }

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

    case G_TLS_CERTIFICATE_GENERIC_ERROR:
    case G_TLS_CERTIFICATE_VALIDATE_ALL:
    default:
      error_msg = _("Invalid certificate.");
      break;
    }

  g_set_error_literal (err, GOA_ERROR, GOA_ERROR_SSL, error_msg);
}

gboolean
goa_utils_get_credentials (GoaProvider    *provider,
                           GoaObject      *object,
                           const gchar    *id,
                           gchar         **out_username,
                           gchar         **out_password,
                           GCancellable   *cancellable,
                           GError        **error)
{
  GVariant *credentials = NULL;
  GoaAccount *account = NULL;
  gboolean ret = FALSE;
  gchar *username = NULL;
  gchar *password = NULL;

  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    goto out;

  account = goa_object_get_account (object);
  username = goa_account_dup_identity (account);

  if (!g_variant_lookup (credentials, id, "s", &password))
    {
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Did not find %s with identity “%s” in credentials"),
                   id, username);
      goto out;
    }

  if (out_username)
    {
      *out_username = username;
      username = NULL;
    }

  if (out_password)
    {
      *out_password = password;
      password = NULL;
    }

  ret = TRUE;

out:
  g_clear_object (&account);
  g_clear_pointer (&credentials, g_variant_unref);
  g_free (username);
  g_free (password);
  return ret;
}

static void
replace_all (gchar *str, gchar find, gchar replace)
{
  gchar *current_pos = strchr (str, find);
  while (current_pos)
    {
      *current_pos = replace;
      current_pos = strchr (current_pos,find);
    }
}

gchar *
goa_utils_base64_url_encode (const guchar *data, gsize len)
{
  gchar *ret;

  g_return_val_if_fail (data != NULL, NULL);

  ret = g_base64_encode (data, len);
  replace_all (ret, '+', '-');
  replace_all (ret, '/', '_');
  replace_all (ret, '=', '\0');

  return ret;
}

gchar *
goa_utils_generate_code_verifier (void)
{
  g_autoptr(GRand) rand = NULL;
  guint32 ints[8];
  gchar *ret = NULL;

  /* Generates a 'code_verifier' as described in section 4.1 of RFC 7636. */
  rand = g_rand_new_with_seed (time (NULL));
  for (int i = 0; i < 8; ++i)
    ints[i] = g_rand_int (rand);

  ret = goa_utils_base64_url_encode ((const guchar *) ints,
                                     sizeof(guint32) * 8);

  return ret;
}

gchar *
goa_utils_generate_code_challenge (const gchar *code_verifier)
{
  g_autoptr(GChecksum) checksum = NULL;
  g_autofree guint8 *digest = NULL;
  gsize digest_len;

  checksum = g_checksum_new (G_CHECKSUM_SHA256);

  digest_len = g_checksum_type_get_length (G_CHECKSUM_SHA256);
  digest = g_malloc (digest_len);

  g_checksum_update (checksum, (const guchar *) code_verifier, -1);
  g_checksum_get_digest (checksum, digest, &digest_len);

  return goa_utils_base64_url_encode (digest, digest_len);
}
