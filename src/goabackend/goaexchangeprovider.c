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

#include "goaewsclient.h"
#include "goaprovider.h"
#include "goaexchangeprovider.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

struct _GoaExchangeProvider
{
  GoaProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaExchangeProvider, goa_exchange_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_EXCHANGE_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_EXCHANGE_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Microsoft Exchange"));
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
         GOA_PROVIDER_FEATURE_MAIL |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS;
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
  GoaAccount *account = NULL;
  GoaExchange *exchange = NULL;
  GoaMail *mail = NULL;
  GoaPasswordBased *password_based = NULL;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean mail_enabled;
  gboolean ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_exchange_provider_parent_class)->build_object (provider,
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
      /* Ensure D-Bus method invocations run in their own thread */
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (password_based),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      goa_object_skeleton_set_password_based (object, password_based);
      g_signal_connect (password_based,
                        "handle-get-password",
                        G_CALLBACK (on_handle_get_password),
                        NULL);
    }

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          const gchar *email_address;

          email_address = goa_account_get_presentation_identity (account);
          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail), "email-address", email_address, NULL);
          goa_object_skeleton_set_mail (object, mail);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  /* Calendar */
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  goa_object_skeleton_attach_calendar (object, NULL, calendar_enabled, FALSE);

  /* Contacts */
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  goa_object_skeleton_attach_contacts (object, NULL, contacts_enabled, FALSE);

  /* Exchange */
  exchange = goa_object_get_exchange (GOA_OBJECT (object));
  if (exchange == NULL)
    {
      gboolean accept_ssl_errors;
      gchar *host;

      accept_ssl_errors = g_key_file_get_boolean (key_file, group, "AcceptSslErrors", NULL);
      host = g_key_file_get_string (key_file, group, "Host", NULL);
      exchange = goa_exchange_skeleton_new ();
      g_object_set (G_OBJECT (exchange),
                    "accept-ssl-errors", accept_ssl_errors,
                    "host", host,
                    NULL);
      goa_object_skeleton_set_exchange (object, exchange);
      g_free (host);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);

      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "MailEnabled");
      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "ContactsEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&exchange);
  g_clear_object (&mail);
  g_clear_object (&password_based);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  GoaAccount *account;
  GoaEwsClient *ews_client = NULL;
  GoaExchange *exchange;
  gboolean accept_ssl_errors;
  gboolean ret = FALSE;
  const gchar *email_address;
  const gchar *server;
  gchar *username = NULL;
  gchar *password = NULL;

  if (!goa_utils_get_credentials (provider, object, "password", &username, &password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  account = goa_object_peek_account (object);
  email_address = goa_account_get_presentation_identity (account);
  exchange = goa_object_peek_exchange (object);
  accept_ssl_errors = goa_exchange_get_accept_ssl_errors (exchange);
  server = goa_exchange_get_host (exchange);

  ews_client = goa_ews_client_new ();
  if (!goa_ews_client_autodiscover_sync (ews_client,
                                         email_address,
                                         password,
                                         username,
                                         server,
                                         accept_ssl_errors,
                                         cancellable,
                                         error))
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is the username
                           * (eg., debarshi.ray@gmail.com or rishi), and the
                           * (%s, %d) is the error domain and code.
                           */
                          _("Invalid password with username “%s” (%s, %d): "),
                          username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  ret = TRUE;

 out:
  g_clear_object (&ews_client);
  g_free (username);
  g_free (password);
  return ret;
}

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

  GtkWidget *email_address;
  GtkWidget *password;

  GtkWidget *expander;
  GtkWidget *username;
  GtkWidget *server;

  gchar *account_object_path;

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

static void
on_email_address_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add = FALSE;
  const gchar *email;
  gchar *domain = NULL;
  gchar *url = NULL;
  gchar *username = NULL;

  email = gtk_entry_get_text (GTK_ENTRY (data->email_address));
  if (!goa_utils_parse_email_address (email, &username, &domain))
    goto out;

  if (data->username != NULL)
    gtk_entry_set_text (GTK_ENTRY (data->username), username);

  if (data->server != NULL)
    gtk_entry_set_text (GTK_ENTRY (data->server), domain);

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (url);
  g_free (domain);
  g_free (username);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
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
  add_entry (grid1, row++, _("_E-mail"), &data->email_address);
  add_entry (grid1, row++, _("_Password"), &data->password);
  if (new_account)
    {
      data->expander = gtk_expander_new_with_mnemonic (_("_Custom"));
      gtk_expander_set_expanded (GTK_EXPANDER (data->expander), FALSE);
      gtk_expander_set_resize_toplevel (GTK_EXPANDER (data->expander), TRUE);
      gtk_container_add (GTK_CONTAINER (grid0), data->expander);

      grid1 = gtk_grid_new ();
      gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
      gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
      gtk_container_add (GTK_CONTAINER (data->expander), grid1);

      row = 0;
      add_entry (grid1, row++, _("User_name"), &data->username);
      add_entry (grid1, row++, _("_Server"), &data->server);
    }

  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus ((new_account) ? data->email_address : data->password);

  g_signal_connect (data->email_address, "changed", G_CALLBACK (on_email_address_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_email_address_or_password_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  data->progress_grid = gtk_grid_new ();
  gtk_widget_set_no_show_all (data->progress_grid, TRUE);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), data->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_widget_show (spinner);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_show (label);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

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

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

static void
autodiscover_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaEwsClient *client = GOA_EWS_CLIENT (source_object);
  AddAccountData *data = user_data;

  goa_ews_client_autodiscover_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->connect_button, TRUE);
  gtk_widget_hide (data->progress_grid);
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaEwsClient *ews_client = NULL;
  GoaObject *ret = NULL;
  gboolean accept_ssl_errors = FALSE;
  const gchar *email_address;
  const gchar *server;
  const gchar *password;
  const gchar *username;
  const gchar *provider_type;
  gint response;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  ews_client = goa_ews_client_new ();

 ews_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  email_address = gtk_entry_get_text (GTK_ENTRY (data.email_address));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  server = gtk_entry_get_text (GTK_ENTRY (data.server));

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  username,
                                  email_address,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &data.error))
    goto out;

  g_cancellable_reset (data.cancellable);
  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               data.cancellable,
                               autodiscover_cb,
                               &data);
  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
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
      gchar *markup;

      if (data.error->code == GOA_ERROR_SSL)
        {
          gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Ignore"));
          accept_ssl_errors = TRUE;
        }
      else
        {
          gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));
          accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                _("Error connecting to Microsoft Exchange server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_expander_set_expanded (GTK_EXPANDER (data.expander), TRUE);
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto ews_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "Host", server);
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (accept_ssl_errors) ? "true" : "false");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                username,
                                email_address,
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

  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, &data);

  g_free (data.account_object_path);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.cancellable);
  g_clear_object (&ews_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider    *provider,
                 GoaClient      *client,
                 GoaObject      *object,
                 GtkWindow      *parent,
                 GError        **error)
{
  AddAccountData data;
  GVariantBuilder builder;
  GoaAccount *account;
  GoaEwsClient *ews_client = NULL;
  GoaExchange *exchange;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean accept_ssl_errors;
  gboolean ret = FALSE;
  const gchar *email_address;
  const gchar *server;
  const gchar *password;
  const gchar *username;
  gint response;

  g_return_val_if_fail (GOA_IS_EXCHANGE_PROVIDER (provider), FALSE);
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
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (vbox), 12);

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = GTK_DIALOG (dialog);
  data.error = NULL;

  create_account_details_ui (provider, GTK_DIALOG (dialog), GTK_BOX (vbox), FALSE, &data);

  account = goa_object_peek_account (object);
  email_address = goa_account_get_presentation_identity (account);
  gtk_entry_set_text (GTK_ENTRY (data.email_address), email_address);
  gtk_editable_set_editable (GTK_EDITABLE (data.email_address), FALSE);

  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  ews_client = goa_ews_client_new ();

 ews_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  username = goa_account_get_identity (account);

  exchange = goa_object_peek_exchange (object);
  accept_ssl_errors = goa_exchange_get_accept_ssl_errors (exchange);
  server = goa_exchange_get_host (exchange);

  g_cancellable_reset (data.cancellable);
  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               data.cancellable,
                               autodiscover_cb,
                               &data);
  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
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
      gchar *markup;

      markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                _("Error connecting to Microsoft Exchange server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto ews_again;
    }

  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    &data.error))
    goto out;

  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);

  gtk_widget_destroy (dialog);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.cancellable);
  g_clear_object (&ews_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_exchange_provider_init (GoaExchangeProvider *self)
{
}

static void
goa_exchange_provider_class_init (GoaExchangeProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *id, /* unused */
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  const gchar *account_id;
  const gchar *method_name;
  const gchar *provider_type;
  gchar *password = NULL;

  /* TODO: maybe log what app is requesting access */

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  account_id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  g_debug ("Handling %s for account (%s, %s)", method_name, provider_type, account_id);

  provider = goa_provider_get_for_provider_type (provider_type);

  error = NULL;
  if (!goa_utils_get_credentials (provider, object, "password", NULL, &password, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  goa_password_based_complete_get_password (interface, invocation, password);

 out:
  g_free (password);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
