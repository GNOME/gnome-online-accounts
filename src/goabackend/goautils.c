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

#ifdef GOA_TELEPATHY_ENABLED
#include <telepathy-glib/telepathy-glib.h>
#endif

#include "goautils.h"

static const SecretSchema secret_password_schema =
{
  "org.gnome.OnlineAccounts", SECRET_SCHEMA_DONT_MATCH_NAME,
  {
    { "goa-identity", SECRET_SCHEMA_ATTRIBUTE_STRING },
    { "NULL", 0 }
  }
};

typedef struct
{
  GoaClient *client;
  GoaObject *object;
  GoaProvider *provider;
} AttentionNeededData;

static AttentionNeededData *
attention_needed_data_new (GoaClient *client, GoaObject *object, GoaProvider *provider)
{
  AttentionNeededData *data;

  data = g_slice_new0 (AttentionNeededData);
  data->client = g_object_ref (client);
  data->object = g_object_ref (object);
  data->provider = g_object_ref (provider);

  return data;
}

static void
attention_needed_data_free (AttentionNeededData *data)
{
  g_object_unref (data->client);
  g_object_unref (data->object);
  g_object_unref (data->provider);
  g_slice_free (AttentionNeededData, data);
}

static void
goa_utils_account_add_attention_needed_info_bar_response (GtkInfoBar *info_bar,
                                                          gint        response_id,
                                                          gpointer    user_data)
{
  AttentionNeededData *data = (AttentionNeededData *) user_data;
  GtkWidget *parent;
  GError *error;

  g_return_if_fail (response_id == GTK_RESPONSE_OK);

  parent = gtk_widget_get_toplevel (GTK_WIDGET (info_bar));
  if (!gtk_widget_is_toplevel (parent))
    {
      g_warning ("Unable to find a toplevel GtkWindow");
      return;
    }

  error = NULL;
  if (!goa_provider_refresh_account (data->provider, data->client, data->object, GTK_WINDOW (parent), &error))
    {
      if (!(error->domain == GOA_ERROR && error->code == GOA_ERROR_DIALOG_DISMISSED))
        {
          GtkWidget *dialog;
          dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_ERROR,
                                           GTK_BUTTONS_CLOSE,
                                           _("Error logging into the account"));
          gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                    "%s",
                                                    error->message);
          gtk_widget_show_all (dialog);
          gtk_dialog_run (GTK_DIALOG (dialog));
          gtk_widget_destroy (dialog);
        }
      g_error_free (error);
    }
}

void
goa_utils_account_add_attention_needed (GoaClient *client, GoaObject *object, GoaProvider *provider, GtkBox *vbox)
{
  AttentionNeededData *data;
  GoaAccount *account;
  GtkWidget *button;
  GtkWidget *content_area;
  GtkWidget *info_bar;
  GtkWidget *label;
  GtkWidget *labels_grid;
  gchar *markup = NULL;

  account = goa_object_peek_account (object);
  if (!goa_account_get_attention_needed (account))
    goto out;

  info_bar = gtk_info_bar_new ();
  gtk_container_add (GTK_CONTAINER (vbox), info_bar);

  content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));
  gtk_widget_set_margin_start (content_area, 6);

  labels_grid = gtk_grid_new ();
  gtk_widget_set_halign (labels_grid, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand (labels_grid, TRUE);
  gtk_widget_set_valign (labels_grid, GTK_ALIGN_CENTER);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (labels_grid), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_column_spacing (GTK_GRID (labels_grid), 0);
  gtk_container_add (GTK_CONTAINER (content_area), labels_grid);

  label = gtk_label_new ("");
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  markup = g_strdup_printf ("<b>%s</b>", _("Credentials have expired"));
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_container_add (GTK_CONTAINER (labels_grid), label);

  label = gtk_label_new (_("Sign in to enable this account."));
  gtk_widget_set_halign (label, GTK_ALIGN_START);
  gtk_container_add (GTK_CONTAINER (labels_grid), label);

  button = gtk_info_bar_add_button (GTK_INFO_BAR (info_bar), _("_Sign In"), GTK_RESPONSE_OK);
  gtk_widget_set_margin_end (button, 6);

  data = attention_needed_data_new (client, object, provider);
  g_signal_connect_data (info_bar,
                         "response",
                         G_CALLBACK (goa_utils_account_add_attention_needed_info_bar_response),
                         data,
                         (GClosureNotify) attention_needed_data_free,
                         0);

 out:
  g_free (markup);
}

void
goa_utils_account_add_header (GoaObject *object, GtkGrid *grid, gint row)
{
  GIcon *icon;
  GoaAccount *account;
  GtkWidget *image;
  GtkWidget *label;
  const gchar *icon_str;
  const gchar *identity;
  const gchar *name;
  gchar *markup;

  account = goa_object_peek_account (object);

  icon_str = goa_account_get_provider_icon (account);
  icon = g_icon_new_for_string (icon_str, NULL);
  image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
  g_object_unref (icon);
  gtk_widget_set_halign (image, GTK_ALIGN_END);
  gtk_widget_set_hexpand (image, TRUE);
  gtk_widget_set_margin_bottom (image, 12);
  gtk_grid_attach (grid, image, 0, row, 1, 1);

  name = goa_account_get_provider_name (account);
  identity = goa_account_get_presentation_identity (account);
  markup = g_strdup_printf ("<b>%s</b>\n%s",
                            name,
                            (identity == NULL || identity[0] == '\0') ? "\xe2\x80\x94" : identity);
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
  gtk_label_set_width_chars (GTK_LABEL (label), 24);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_set_margin_bottom (label, 12);
  g_free (markup);
  gtk_grid_attach (grid, label, 1, row, 3, 1);
}

void
goa_utils_initialize_client_factory (void)
{
  static gsize once_init_value = 0;

  if (g_once_init_enter (&once_init_value))
    {
#ifdef GOA_TELEPATHY_ENABLED
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
#endif

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

void
goa_utils_set_dialog_title (GoaProvider *provider, GtkDialog *dialog, gboolean add_account)
{
  gchar *provider_name;
  gchar *title;

  provider_name = goa_provider_get_provider_name (GOA_PROVIDER (provider), NULL);
  /* Translators: this is the title of the "Add Account" and "Refresh
   * Account" dialogs. The %s is the name of the provider. eg.,
   * 'Google'.
   */
  title = g_strdup_printf (_("%s Account"), provider_name);
  gtk_window_set_title (GTK_WINDOW (dialog), title);
  g_free (title);
  g_free (provider_name);
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
      g_warning ("Error getting keys from group %s: %s (%s, %d)",
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
          g_warning ("Error reading key %s from group %s: %s (%s, %d)",
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
              g_warning ("Error reading key %s from group %s: %s (%s, %d)",
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
          g_warning ("Error reading key %s from group %s in keyfile: %s (%s, %d)",
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("Error reading key %s from keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
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
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
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
      g_warning ("Error reading key %s from keyfile %s: %s (%s, %d)",
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
      g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
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
goa_utils_set_error_soup (GError **err, SoupMessage *msg)
{
  gchar *error_msg = NULL;
  gint error_code = GOA_ERROR_FAILED; /* TODO: more specific */

  switch (msg->status_code)
    {
    case SOUP_STATUS_CANT_RESOLVE:
      error_msg = g_strdup (_("Cannot resolve hostname"));
      break;

    case SOUP_STATUS_CANT_RESOLVE_PROXY:
      error_msg = g_strdup (_("Cannot resolve proxy hostname"));
      break;

    case SOUP_STATUS_INTERNAL_SERVER_ERROR:
    case SOUP_STATUS_NOT_FOUND:
      error_msg = g_strdup (_("Cannot find WebDAV endpoint"));
      break;

    case SOUP_STATUS_UNAUTHORIZED:
      error_msg = g_strdup (_("Authentication failed"));
      error_code = GOA_ERROR_NOT_AUTHORIZED;
      break;

    default:
      error_msg = g_strdup_printf (_("Code: %u — Unexpected response from server"), msg->status_code);
      break;
    }

  g_set_error_literal (err, GOA_ERROR, error_code, error_msg);
  g_free (error_msg);
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
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);
  g_free (username);
  g_free (password);
  return ret;
}
