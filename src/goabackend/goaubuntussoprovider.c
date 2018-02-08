/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2018 Canonical Ltd
 *
 * Authors: Andrea Azzarone <andrea.azzarone@canonical.com>
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
#include <snapd-glib/snapd-glib.h>

#include "goaprovider.h"
#include "goaubuntussoprovider.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

/* ---------------------------------------------------------------------------------------------------- */

struct _GoaUbuntuSSOProvider
{
  GoaProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaUbuntuSSOProvider, goa_ubuntu_sso_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                         g_define_type_id,
                         GOA_UBUNTU_SSO_NAME,
                         0));

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *stack;

  GtkWidget *username_entry;
  GtkWidget *password_entry;
  GtkWidget *otp_entry;
  GtkWidget *login_radio;
  GtkWidget *register_radio;
  GtkWidget *reset_radio;

  SnapdClient *snapd_client;

  gchar  *macaroon;
  gchar **discharges;
  gchar  *account_object_path;

  GError *error;
} AddAccountData;

static void
add_account_data_init (AddAccountData *data)
{
  memset (data, 0, sizeof (AddAccountData));
  data->cancellable = g_cancellable_new ();
  data->loop = g_main_loop_new (NULL, FALSE);
  data->snapd_client = snapd_client_new ();
}

static void
add_account_data_clear (AddAccountData *data)
{
  g_clear_object (&data->cancellable);
  g_clear_pointer (&data->loop, (GDestroyNotify) g_main_loop_unref);
  g_clear_object (&data->snapd_client);
  g_free (data->macaroon);
  g_strfreev (data->discharges);
  g_free (data->account_object_path);
  g_clear_error (&data->error);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(AddAccountData, add_account_data_clear)

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_UBUNTU_SSO_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Ubuntu Single Sign-On"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_TICKETING;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaPasswordBased *password_based = NULL;
  gboolean ret = FALSE;

  // Chain up
  if (!GOA_PROVIDER_CLASS (goa_ubuntu_sso_provider_parent_class)->build_object (provider,
                                                                                object,
                                                                                key_file,
                                                                                group,
                                                                                connection,
                                                                                just_added,
                                                                                error))
    goto out;

  password_based = goa_object_get_password_based (GOA_OBJECT (object));
  if (password_based == NULL)
    {
      password_based = goa_password_based_skeleton_new ();
      // Ensure D-Bus method invocations run in their own thread
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (password_based),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      goa_object_skeleton_set_password_based (object, password_based);
      g_signal_connect (password_based,
                        "handle-get-password",
                        G_CALLBACK (on_handle_get_password),
                        NULL);
    }

  ret = TRUE;

 out:
  g_clear_object (&password_based);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider   *provider,
                         GoaObject     *object,
                         gint          *out_expires_in,
                         GCancellable  *cancellable,
                         GError       **error)
{
  g_autofree gchar *macaroon = NULL;
  g_autofree gchar *discharges_str = NULL;
  g_autoptr(GVariant) discharges_var = NULL;
  g_autofree const gchar **discharges = NULL;
  g_autoptr(SnapdClient) snapd_client = NULL;
  g_autoptr(SnapdAuthData) auth_data = NULL;

  if (!goa_utils_get_credentials (provider, object, "macaroon", NULL, &macaroon, cancellable, error))
    goto edit_error_and_return;

  if (!goa_utils_get_credentials (provider, object, "discharges", NULL, &discharges_str, cancellable, error))
    goto edit_error_and_return;

  if (discharges_str)
    discharges_var = g_variant_parse (G_VARIANT_TYPE ("as"), discharges_str, NULL, NULL, NULL);
  if (discharges_var)
    discharges = g_variant_get_strv (discharges_var, NULL);

  snapd_client = snapd_client_new ();
  auth_data = snapd_auth_data_new (macaroon, (gchar **) discharges);
  snapd_client_set_auth_data (snapd_client, auth_data);
  snapd_client_check_buy_sync (snapd_client, cancellable, error);

  if (error != NULL)
    {
      if (g_error_matches (*error, SNAPD_ERROR, SNAPD_ERROR_AUTH_DATA_REQUIRED))
        goto edit_error_and_return;
      else
        g_clear_error (error);
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  return TRUE;

edit_error_and_return:
  if (error != NULL)
    {
      (*error)->domain = GOA_ERROR;
      (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
    }
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *grid,
           gint           row,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, FALSE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_widget_set_size_request (entry, 250, -1);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 2, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);

  if (out_entry != NULL)
    *out_entry = entry;
}

static void
add_radio (GtkWidget      *grid,
           gint            row,
           const gchar    *text,
           GtkRadioButton *group_source,
           GtkWidget     **out_radio)
{
  GtkWidget *radio;

  radio = gtk_radio_button_new_with_mnemonic_from_widget (group_source, text);
  gtk_widget_set_hexpand (radio, FALSE);
  gtk_widget_set_halign (radio, GTK_ALIGN_START);
  gtk_grid_attach (GTK_GRID (grid), radio, 0, row, 3, 1);

  if (out_radio != NULL)
    *out_radio = radio;
}

static void
update_widgets (AddAccountData *data)
{
  gboolean can_add = TRUE;
  const gchar *visible_page;

  visible_page = gtk_stack_get_visible_child_name (GTK_STACK (data->stack));
  if (g_strcmp0 (visible_page, "login") == 0)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->login_radio)))
        {
          can_add =  gtk_entry_get_text_length (GTK_ENTRY (data->username_entry)) > 0
                     && gtk_entry_get_text_length (GTK_ENTRY (data->password_entry)) > 0;
          gtk_widget_set_sensitive (data->password_entry, TRUE);
        }
      else
        {
          gtk_widget_set_sensitive (data->password_entry, FALSE);
        }
    }
  else if (g_strcmp0 (visible_page, "otp") == 0)
    {
      can_add = gtk_entry_get_text_length (GTK_ENTRY (data->otp_entry)) > 0;
    }

  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
}

static void
on_username_or_password_changed (GtkEditable *editable,
                                 gpointer user_data)
{
  AddAccountData *data = user_data;
  update_widgets (data);
}

static void
on_otp_changed (GtkEditable *editable,
                gpointer user_data)
{
  AddAccountData *data = user_data;
  update_widgets (data);
}

static void
on_radio_button_toggled_cb (GtkToggleButton *togglebutton,
                            gpointer         user_data)
{
  AddAccountData *data = user_data;
  update_widgets (data);
}

static void
show_progress_ui (GtkContainer *container,
                  gboolean progress)
{
  g_autoptr(GList) children = gtk_container_get_children (container);
  for (GList *l = children; l != NULL; l = l->next)
    {
      GtkWidget *widget = GTK_WIDGET (l->data);
      gdouble opacity;

      opacity = progress ? 1.0 : 0.0;
      gtk_widget_set_opacity (widget, opacity);
    }
}

static char *
get_item (const char *buffer, const char *name)
{
  char *label, *start, *end, *result;
  char end_char;

  result = NULL;
  start = NULL;
  end = NULL;
  label = g_strconcat (name, "=", NULL);
  if ((start = strstr (buffer, label)) != NULL)
    {
      start += strlen (label);
      end_char = '\n';
      if (*start == '"')
        {
          start++;
          end_char = '"';
        }

      end = strchr (start, end_char);
    }

    if (start != NULL && end != NULL)
      {
        result = g_strndup (start, end - start);
      }

  g_free (label);

  return result;
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           gboolean        new_account,
                           const gchar    *existing_identity,
                           AddAccountData *data)
{
  GtkWidget *content_box;
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *grid2;
  GtkWidget *label;
  GtkWidget *footer_box;
  GtkWidget *privacy_button;
  GObject *revealer;
  GtkWidget *spinner;
  gint row;
  gint width;
  g_autofree gchar *buffer = NULL;
  g_autofree gchar* privacy_policy = NULL;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  content_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_show (content_box);
  gtk_container_add (GTK_CONTAINER (vbox), content_box);

  data->cluebar = gtk_info_bar_new ();
  gtk_widget_set_hexpand (data->cluebar, TRUE);
  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (content_box), data->cluebar);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 12);
  gtk_widget_set_margin_bottom (grid0, 0);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (content_box), grid0);

  // GtkInfoBar fails to show after gtk_widget_hide has been called. Apply
  // a workaround as suggested in https://bugzilla.gnome.org/show_bug.cgi?id=710888
  revealer = gtk_widget_get_template_child (GTK_WIDGET (data->cluebar), GTK_TYPE_INFO_BAR, "revealer");
  if (revealer)
    {
      gtk_revealer_set_transition_type (GTK_REVEALER (revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
      gtk_revealer_set_transition_duration (GTK_REVEALER (revealer), 0);
    }

  data->cluebar_label = gtk_label_new ("");
  gtk_widget_set_hexpand (data->cluebar_label, TRUE);
  gtk_label_set_xalign (GTK_LABEL (data->cluebar_label), 0.0);
  gtk_label_set_line_wrap (GTK_LABEL (data->cluebar_label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (data->cluebar_label), 50);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  data->stack = gtk_stack_new ();
  gtk_container_add (GTK_CONTAINER (grid0), data->stack);

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_stack_add_named (GTK_STACK (data->stack), grid1, "login");

  row = 0;
  add_entry (grid1, row++, _("_Email address:"), &data->username_entry);
  add_radio (grid1, row++, _("I have an Ubuntu Single Sign-On account"), NULL, &data->login_radio);
  add_entry (grid1, row++, _("_Password:"), &data->password_entry);
  add_radio (grid1, row++, _("I want to register for an account now"), GTK_RADIO_BUTTON (data->login_radio), &data->register_radio);
  add_radio (grid1, row++, _("I've forgotten my password"), GTK_RADIO_BUTTON (data->login_radio), &data->reset_radio);
  gtk_entry_set_visibility (GTK_ENTRY (data->password_entry), FALSE);

  if (new_account)
    {
      gtk_widget_grab_focus (data->username_entry);
    }
  else
    {
      gtk_widget_set_sensitive (data->username_entry, FALSE);
      gtk_entry_set_text (GTK_ENTRY (data->username_entry), existing_identity);
      gtk_widget_set_sensitive (data->register_radio, FALSE);
      gtk_widget_grab_focus (data->password_entry);
    }

  g_signal_connect (data->username_entry, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->password_entry, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->login_radio, "toggled", G_CALLBACK (on_radio_button_toggled_cb), data);
  g_signal_connect (data->register_radio, "toggled", G_CALLBACK (on_radio_button_toggled_cb), data);
  g_signal_connect (data->reset_radio, "toggled", G_CALLBACK (on_radio_button_toggled_cb), data);

  grid2 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid2), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid2), 12);
  gtk_stack_add_named (GTK_STACK (data->stack), grid2, "otp");

  row = 0;
  add_entry (grid2, row++, _("Passc_ode:"), &data->otp_entry);

  g_signal_connect (data->otp_entry, "changed", G_CALLBACK (on_otp_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  footer_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_widget_show (footer_box);
  gtk_container_add (GTK_CONTAINER (grid0), footer_box);

  data->progress_grid = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_box_pack_start (GTK_BOX (footer_box), data->progress_grid, TRUE, TRUE, 0);

  spinner = gtk_spinner_new ();
  gtk_widget_set_opacity (spinner, 0.0);
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_set_opacity (label, 0.0);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

  if (g_file_get_contents ("/etc/os-release", &buffer, NULL, NULL))
    {
      privacy_policy = get_item (buffer, "PRIVACY_POLICY_URL");

      if (privacy_policy)
        {
          privacy_button = gtk_link_button_new_with_label (privacy_policy,
                                                           _("Privacy Policy"));
          gtk_widget_show (privacy_button);
          gtk_box_pack_end (GTK_BOX (footer_box), privacy_button, FALSE, FALSE, 0);
        }
    }

  if (new_account)
   {
     gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
     gtk_window_set_default_size (GTK_WINDOW (data->dialog), width, -1);
   }
  else
    {
      GtkWindow *parent;

      /* Keep in sync with GoaPanelAddAccountDialog in
       * gnome-control-center.
       */
      parent = gtk_window_get_transient_for (GTK_WINDOW (data->dialog));
      if (parent != NULL)
        {
          gtk_window_get_size (parent, &width, NULL);
          gtk_window_set_default_size (GTK_WINDOW (data->dialog), (gint) (0.5 * width), -1);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar*
get_snapd_error_message (GError *error)
{
  g_return_val_if_fail (error != NULL, NULL);

  g_debug ("Error logging in snapd: %s (%s, %d)",
           error->message, g_quark_to_string (error->domain), error->code);

  if (g_error_matches (error, SNAPD_ERROR, SNAPD_ERROR_AUTH_DATA_REQUIRED))
    return _("Provided email/password is not correct");
  else if (g_error_matches (error, SNAPD_ERROR, SNAPD_ERROR_TWO_FACTOR_INVALID))
    return _("The provided 2-factor key is not recognised");
  else
    return _("Something went wrong, please try again");
}

static void
dialog_response_cb (GtkDialog *dialog,
                    gint response_id,
                    gpointer user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

static void
snapd_login_ready_cb (GObject *object,
                      GAsyncResult *result,
                      gpointer user_data)
{
  AddAccountData *data = user_data;
  g_autoptr(SnapdUserInformation) user_information = NULL;
  SnapdAuthData *auth_data;

  user_information = snapd_client_login2_finish (data->snapd_client, result, &data->error);
  if (user_information != NULL)
    {
      auth_data = snapd_user_information_get_auth_data (user_information);
      data->macaroon = g_strdup (snapd_auth_data_get_macaroon (auth_data));
      data->discharges = g_strdupv (snapd_auth_data_get_discharges (auth_data));
    }

  update_widgets (data);
  show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);
  g_main_loop_quit (data->loop);
}

static void
add_account_cb (GoaManager *manager,
                GAsyncResult *res,
                gpointer user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

static void
add_credentials_key_values (GVariantBuilder *builder,
                            AddAccountData *data)
{
  g_autofree gchar *discharges_str = NULL;
  g_autoptr(GVariant) discharges_var = NULL;

  g_variant_builder_add (builder, "{sv}", "macaroon", g_variant_new_string (data->macaroon));
  discharges_var = g_variant_new_strv ((const gchar * const*) data->discharges, -1);
  discharges_str = g_variant_print (discharges_var, FALSE);
  g_variant_builder_add (builder, "{sv}", "discharges", g_variant_new_string (discharges_str));
}

static gboolean
get_tokens_and_identity (GoaProvider    *provider,
                         gboolean        add_account,
                         const gchar    *existing_identity,
                         GtkDialog      *dialog,
                         GtkBox         *vbox,
                         AddAccountData *data)
{
  gboolean ret = FALSE;
  const gchar *password;
  const gchar *username;
  const gchar *otp;
  gint response;

  g_return_val_if_fail (GOA_IS_UBUNTU_SSO_PROVIDER (provider), FALSE);
  g_return_val_if_fail ((!add_account && existing_identity != NULL && existing_identity[0] != '\0')
                        || (add_account && existing_identity == NULL), FALSE);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), FALSE);
  g_return_val_if_fail (GTK_IS_BOX (vbox), FALSE);
  g_return_val_if_fail (data != NULL, FALSE);

  data->dialog = dialog;

  create_account_details_ui (provider, dialog, vbox, add_account, existing_identity, data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), data);

 login_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data->error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_widget_hide (data->cluebar);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->register_radio)))
    {
      g_app_info_launch_default_for_uri ("https://login.ubuntu.com/+new_account", NULL, NULL);
      goto login_again;
    }
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->reset_radio)))
    {
      g_app_info_launch_default_for_uri ("https://login.ubuntu.com/+forgot_password", NULL, NULL);
      goto login_again;
    }

  username = gtk_entry_get_text (GTK_ENTRY (data->username_entry));
  password = gtk_entry_get_text (GTK_ENTRY (data->password_entry));
  otp = gtk_entry_get_text (GTK_ENTRY (data->otp_entry));
  otp = otp && strlen (otp) > 0 ? otp : NULL;

  g_clear_object (&data->cancellable);
  data->cancellable = g_cancellable_new ();

  snapd_client_login2_async (data->snapd_client,
                             username, password, otp,
                             data->cancellable, snapd_login_ready_cb, data);

  gtk_widget_set_sensitive (data->connect_button, FALSE);
  show_progress_ui (GTK_CONTAINER (data->progress_grid), TRUE);
  g_main_loop_run (data->loop);

  if (g_cancellable_is_cancelled (data->cancellable))
    {
      g_prefix_error (&data->error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data->error->domain),
                      data->error->code);
      data->error->domain = GOA_ERROR;
      data->error->code = GOA_ERROR_DIALOG_DISMISSED;
      goto out;
    }
  else if (data->error != NULL)
    {
      if (data->error->code == SNAPD_ERROR_TWO_FACTOR_REQUIRED)
        {
          g_autofree gchar *markup = NULL;

          g_clear_error (&data->error);
          gtk_button_set_label (GTK_BUTTON (data->connect_button), _("C_onnect"));
          gtk_stack_set_visible_child_name (GTK_STACK (data->stack), "otp");
          gtk_widget_set_sensitive (data->connect_button, FALSE);

          markup = g_strdup_printf ("<b>%s</b>", _("Please enter a passcode from your authentication device or app"));
          gtk_label_set_markup (GTK_LABEL (data->cluebar_label), markup);
          gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_INFO);
          gtk_widget_set_no_show_all (data->cluebar, FALSE);
          gtk_widget_show_all (data->cluebar);
        }
      else if (data->error->code == SNAPD_ERROR_PERMISSION_DENIED)
        {
          g_clear_error (&data->error);
        }
      else
        {
          g_autofree gchar *markup = NULL;

          gtk_button_set_label (GTK_BUTTON (data->connect_button), _("_Try Again"));
          markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                    _("Error connecting to Ubuntu Single Sign-On server"),
                                    get_snapd_error_message (data->error));
          g_clear_error (&data->error);
          gtk_label_set_markup (GTK_LABEL (data->cluebar_label), markup);
          gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_ERROR);
          gtk_widget_set_no_show_all (data->cluebar, FALSE);
          gtk_widget_show_all (data->cluebar);
        }

      goto login_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  ret = TRUE;

 out:
  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, data);

  return ret;
}

static GoaObject *
add_account (GoaProvider *provider,
             GoaClient   *client,
             GtkDialog   *dialog,
             GtkBox      *vbox,
             GError     **error)
{
  g_auto(AddAccountData) data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaObject *ret = NULL;
  const gchar *username;

  g_return_val_if_fail (GOA_IS_UBUNTU_SSO_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  add_account_data_init (&data);
  if (!get_tokens_and_identity (provider, TRUE, NULL, dialog, vbox, &data))
    goto out;

  username = gtk_entry_get_text (GTK_ENTRY (data.username_entry));

  /* OK, got the identity... see if there's already an account
   * of this type with the given identity
   */
  if (!goa_utils_check_duplicate (client,
                                  username,
                                  username,
                                  goa_provider_get_provider_type (provider),
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &data.error))
    goto out;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (&credentials, &data);
  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                username,
                                username,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, // GCancellable*
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
    {
      g_propagate_error (error, data.error);
      data.error = NULL;
    }
  else
    g_assert (ret != NULL);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider  *provider,
                 GoaClient    *client,
                 GoaObject    *object,
                 GtkWindow    *parent,
                 GError      **error)
{
  g_auto(AddAccountData) data;
  GoaAccount *account;
  GtkWidget *dialog;
  const gchar *existing_presentation_identity;
  GVariantBuilder builder;
  gboolean ret = FALSE;

  g_return_val_if_fail (GOA_IS_UBUNTU_SSO_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL
                                        | GTK_DIALOG_DESTROY_WITH_PARENT
                                        | GTK_DIALOG_USE_HEADER_BAR,
                                        NULL,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_widget_show_all (dialog);

  account = goa_object_peek_account (object);

  existing_presentation_identity = goa_account_get_presentation_identity (account);
  add_account_data_init (&data);
  if (!get_tokens_and_identity (provider,
                                FALSE,
                                existing_presentation_identity,
                                GTK_DIALOG (dialog),
                                GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                                &data))
    {
      goto out;
    }

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  add_credentials_key_values (&builder, &data);

  if (!goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (provider),
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    error))
    goto out;

  goa_account_call_ensure_credentials (goa_object_peek_account (object),
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      data.error = NULL;
    }

  gtk_widget_destroy (dialog);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_ubuntu_sso_provider_init (GoaUbuntuSSOProvider *self)
{
}

static void
goa_ubuntu_sso_provider_class_init (GoaUbuntuSSOProviderClass *klass)
{
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);

  provider_class->get_provider_type       = get_provider_type;
  provider_class->get_provider_name       = get_provider_name;
  provider_class->get_provider_group      = get_provider_group;
  provider_class->get_provider_features   = get_provider_features;
  provider_class->add_account             = add_account;
  provider_class->refresh_account         = refresh_account;
  provider_class->build_object            = build_object;
  provider_class->ensure_credentials_sync = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *id,
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  g_autoptr(GoaProvider) provider = NULL;
  GError *error;
  const gchar *account_id;
  const gchar *method_name;
  const gchar *provider_type;
  g_autofree gchar *password = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  account_id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  g_debug ("Handling %s for account (%s, %s)", method_name, provider_type, account_id);

  provider = goa_provider_get_for_provider_type (provider_type);

  error = NULL;
  if (!goa_utils_get_credentials (provider, object, id, NULL, &password, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      return TRUE; // invocation was handled
    }

  goa_password_based_complete_get_password (interface, invocation, password);

  return TRUE; // invocation was handled
}
