/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2019 Red Hat, Inc.
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

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goafedoraprovider.h"
#include "goakerberosprovider-priv.h"
#include "goautils.h"

struct _GoaFedoraProvider
{
  GoaKerberosProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaFedoraProvider, goa_fedora_provider, GOA_TYPE_KERBEROS_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_FEDORA_NAME,
                                                         0));

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_FEDORA_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Fedora"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED | GOA_PROVIDER_FEATURE_TICKETING;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-fedora");
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;
  GMainLoop *loop;

  GoaObject *object_temporary;

  GtkDialog *dialog;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *username;
  GtkWidget *password;

  gboolean waiting_for_temporary_account;

  gchar *account_object_path;
  gchar *identity;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *grid,
           gint           row,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 3, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_entry != NULL)
    *out_entry = entry;
}

static gchar *
normalize_principal (const gchar *principal)
{
  gchar *domain = NULL;
  gchar *realm = NULL;
  gchar *ret = NULL;
  gchar *username = NULL;

  if (principal == 0 || principal[0] == '\0')
    goto out;

  if (!goa_utils_parse_email_address (principal, &username, &domain))
    {
      gchar *at;

      at = strchr (principal, '@');
      if (at != NULL)
        goto out;

      username = g_strdup (principal);
      domain = g_strdup (GOA_FEDORA_REALM);
    }

  realm = g_utf8_strup (domain, -1);
  if (g_strcmp0 (realm, GOA_FEDORA_REALM) != 0)
    goto out;

  ret = g_strconcat (username, "@", realm, NULL);

 out:
  g_free (domain);
  g_free (realm);
  g_free (username);
  return ret;
}

static void
on_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;
  gboolean can_add = FALSE;
  const gchar *username;
  gchar *principal = NULL;

  username = gtk_entry_get_text (GTK_ENTRY (data->username));
  principal = normalize_principal (username);
  if (principal == NULL)
    goto out;

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (principal);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkWidget      *vbox,
                           gboolean        new_account,
                           AddAccountData *data)
{
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *label;
  GtkWidget *spinner;
  gint row;
  gint width;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  data->cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_hexpand (data->cluebar, TRUE);
  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (grid0), data->cluebar);

  data->cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (data->cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_container_add (GTK_CONTAINER (grid0), grid1);

  row = 0;
  add_entry (grid1, row++, _("User_name"), &data->username);
  add_entry (grid1, row++, _("_Password"), &data->password);
  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus (data->username);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_username_or_password_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  data->progress_grid = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), data->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_opacity (spinner, 0.0);
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_set_opacity (label, 0.0);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

  gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
  gtk_window_set_default_size (GTK_WINDOW (data->dialog), width, -1);
}

static void
show_progress_ui (GtkContainer *container, gboolean progress)
{
  GList *children;
  GList *l;

  children = gtk_container_get_children (container);
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *widget = GTK_WIDGET (l->data);
      gdouble opacity;

      opacity = progress ? 1.0 : 0.0;
      gtk_widget_set_opacity (widget, opacity);
    }

  g_list_free (children);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;

  goa_manager_call_add_account_finish (manager, &data->account_object_path, res, &data->error);
  if (data->error != NULL)
    g_dbus_error_strip_remote_error (data->error);

  g_main_loop_quit (data->loop);
}

static void
client_account_added_cb (GoaClient *client, GoaObject *object, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;
  GoaAccount *account;
  const gchar *identity;
  const gchar *provider_type;

  account = goa_object_peek_account (object);
  g_return_if_fail (GOA_IS_ACCOUNT (account));

  if (!goa_account_get_is_temporary (account))
    goto out;

  provider_type = goa_account_get_provider_type (account);
  g_message ("client_account_added_cb: %s", provider_type);
  if (g_strcmp0 (provider_type, GOA_FEDORA_NAME) != 0)
    goto out;

  identity = goa_account_get_identity (account);
  g_message ("client_account_added_cb: %s", identity);
  if (g_strcmp0 (identity, data->identity) != 0)
    goto out;

  g_return_if_fail (data->object_temporary == NULL);
  data->object_temporary = g_object_ref (object);

  if (data->waiting_for_temporary_account)
    {
      g_main_loop_quit (data->loop);
      gtk_widget_set_sensitive (data->connect_button, TRUE);
      show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);

      data->waiting_for_temporary_account = FALSE;
    }

 out:
  return;
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

static void
sign_in_identity_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (source_object);
  AddAccountData *data = (AddAccountData *) user_data;
  gchar *object_path = NULL;

  object_path = goa_kerberos_provider_sign_in_finish (GOA_KERBEROS_PROVIDER (self), res, &data->error);
  if (data->error != NULL)
    g_dbus_error_strip_remote_error (data->error);

  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->connect_button, TRUE);
  show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);

  g_free (object_path);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (provider);
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaAccount *account_temporary;
  GoaObject *ret = NULL;
  const gchar *id;
  const gchar *password;
  const gchar *provider_type;
  const gchar *username;
  gchar *principal = NULL;
  gint response;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  /* Needs data.identity to be set before signing in the principal
   * further below.
   */
  g_signal_connect (client, "account-added", G_CALLBACK (client_account_added_cb), &data);

  create_account_details_ui (provider, dialog, GTK_WIDGET (vbox), TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

 sign_in_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED, _("Dialog was dismissed"));
      goto out;
    }

  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));

  principal = normalize_principal (username);
  provider_type = goa_provider_get_provider_type (provider);

  if (!goa_utils_check_duplicate (client,
                                  principal,
                                  principal,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &data.error))
    goto out;

  if (!goa_utils_check_duplicate (client,
                                  principal,
                                  principal,
                                  GOA_KERBEROS_NAME,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &data.error))
    goto out;

  g_clear_object (&data.cancellable);
  data.cancellable = g_cancellable_new ();

  /* For the GoaClient::account-added handler. */
  g_free (data.identity);
  data.identity = g_strdup (principal);

  /* We currently don't prompt the user to choose a preauthentication
   * source during initial sign in so we assume there's no
   * preauthentication source
   */
  goa_kerberos_provider_sign_in (GOA_KERBEROS_PROVIDER (self),
                                 principal,
                                 password,
                                 NULL,
                                 data.cancellable,
                                 sign_in_identity_cb,
                                 &data);

  gtk_widget_set_sensitive (data.connect_button, FALSE);
  show_progress_ui (GTK_CONTAINER (data.progress_grid), TRUE);
  g_main_loop_run (data.loop);

  if (g_cancellable_is_cancelled (data.cancellable))
    {
      g_prefix_error (&data.error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data.error->domain),
                      data.error->code);
      data.error->domain = GOA_ERROR;
      data.error->code = GOA_ERROR_DIALOG_DISMISSED;
      goto out;
    }
  else if (data.error != NULL)
    {
      gchar *markup = NULL;

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));

      markup = g_strdup_printf ("<b>%s:</b>\n%s", _("Error connecting to Fedora"), data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_pointer (&principal, g_free);
      goto sign_in_again;
    }

  if (data.object_temporary == NULL)
    {
      data.waiting_for_temporary_account = TRUE;

      gtk_widget_set_sensitive (data.connect_button, FALSE);
      show_progress_ui (GTK_CONTAINER (data.progress_grid), TRUE);
      g_main_loop_run (data.loop);
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  account_temporary = goa_object_peek_account (data.object_temporary);
  id = goa_account_get_id (account_temporary);

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Id", id);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "false");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                provider_type,
                                principal,
                                principal,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  /* We might have an object even when data.error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (ret != NULL);

  g_signal_handlers_disconnect_by_func (client, client_account_added_cb, &data);
  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, &data);

  g_free (data.account_object_path);
  g_free (data.identity);
  g_free (principal);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.object_temporary);
  g_clear_object (&data.cancellable);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_fedora_provider_init (GoaFedoraProvider *provider)
{
}

static void
goa_fedora_provider_class_init (GoaFedoraProviderClass *fedora_class)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (fedora_class);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->add_account                = add_account;
}
