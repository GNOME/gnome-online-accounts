/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#include "goaimapauthlogin.h"
#include "goamailclient.h"
#include "goaimapsmtpprovider.h"
#include "goaprovider.h"
#include "goasmtpauth.h"
#include "goautils.h"

struct _GoaImapSmtpProvider
{
  GoaProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaImapSmtpProvider, goa_imap_smtp_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_IMAP_SMTP_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_IMAP_SMTP_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup (_("IMAP and SMTP"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_MAIL;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_MAIL;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("mail-unread-symbolic");
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
  GoaMail *mail = NULL;
  GoaPasswordBased *password_based = NULL;
  gboolean enabled;
  gboolean imap_accept_ssl_errors;
  gboolean imap_use_ssl;
  gboolean imap_use_tls;
  gboolean ret = FALSE;
  gboolean smtp_accept_ssl_errors;
  gboolean smtp_auth_login = FALSE;
  gboolean smtp_auth_plain = FALSE;
  gboolean smtp_use_auth;
  gboolean smtp_use_ssl;
  gboolean smtp_use_tls;
  gchar *email_address = NULL;
  gchar *imap_host = NULL;
  gchar *imap_username = NULL;
  gchar *name = NULL;
  gchar *smtp_host = NULL;
  gchar *smtp_username = NULL;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_imap_smtp_provider_parent_class)->build_object (provider,
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
  enabled = g_key_file_get_boolean (key_file, group, "Enabled", NULL);
  if (enabled)
    {
      if (mail == NULL)
        {
          email_address = g_key_file_get_string (key_file, group, "EmailAddress", NULL);
          name = g_key_file_get_string (key_file, group, "Name", NULL);

          imap_host = g_key_file_get_string (key_file, group, "ImapHost", NULL);
          imap_username = g_key_file_get_string (key_file, group, "ImapUserName", NULL);
          if (imap_username == NULL)
            imap_username = g_strdup (g_get_user_name ());
          imap_use_ssl = g_key_file_get_boolean (key_file, group, "ImapUseSsl", NULL);
          imap_use_tls = g_key_file_get_boolean (key_file, group, "ImapUseTls", NULL);
          imap_accept_ssl_errors = g_key_file_get_boolean (key_file, group, "ImapAcceptSslErrors", NULL);

          smtp_host = g_key_file_get_string (key_file, group, "SmtpHost", NULL);
          smtp_use_auth = g_key_file_get_boolean (key_file, group, "SmtpUseAuth", NULL);
          if (smtp_use_auth)
            {
              smtp_username = g_key_file_get_string (key_file, group, "SmtpUserName", NULL);
              if (smtp_username == NULL)
                smtp_username = g_strdup (g_get_user_name ());

              smtp_auth_login = g_key_file_get_boolean (key_file, group, "SmtpAuthLogin", NULL);
              smtp_auth_plain = g_key_file_get_boolean (key_file, group, "SmtpAuthPlain", NULL);
              /* For backwards compatibility: if authentication is
               * used, assume PLAIN as the SASL scheme if nothing is
               * specified.
               */
              if (!smtp_auth_login && !smtp_auth_plain)
                smtp_auth_plain = TRUE;
            }
          smtp_use_ssl = g_key_file_get_boolean (key_file, group, "SmtpUseSsl", NULL);
          smtp_use_tls = g_key_file_get_boolean (key_file, group, "SmtpUseTls", NULL);
          smtp_accept_ssl_errors = g_key_file_get_boolean (key_file, group, "SmtpAcceptSslErrors", NULL);

          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail),
                        "email-address", email_address,
                        "name", name,
                        "imap-supported", TRUE,
                        "imap-host", imap_host,
                        "imap-user-name", imap_username,
                        "imap-use-ssl", imap_use_ssl,
                        "imap-use-tls", imap_use_tls,
                        "imap-accept-ssl-errors", imap_accept_ssl_errors,
                        "smtp-supported", TRUE,
                        "smtp-host", smtp_host,
                        "smtp-user-name", smtp_username,
                        "smtp-use-auth", smtp_use_auth,
                        "smtp-auth-login", smtp_auth_login,
                        "smtp-auth-plain", smtp_auth_plain,
                        "smtp-use-ssl", smtp_use_ssl,
                        "smtp-use-tls", smtp_use_tls,
                        "smtp-accept-ssl-errors", smtp_accept_ssl_errors,
                        NULL);
          goa_object_skeleton_set_mail (object, mail);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !enabled);
      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "Enabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&account);
  g_clear_object (&mail);
  g_clear_object (&password_based);
  g_free (email_address);
  g_free (imap_host);
  g_free (imap_username);
  g_free (name);
  g_free (smtp_host);
  g_free (smtp_username);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaTlsType
get_tls_type_from_object (GoaObject *object, const gchar *ssl_key, const gchar *starttls_key)
{
  GoaTlsType tls_type;

  if (goa_util_lookup_keyfile_boolean (object, ssl_key))
    tls_type = GOA_TLS_TYPE_SSL;
  else if (goa_util_lookup_keyfile_boolean (object, starttls_key))
    tls_type = GOA_TLS_TYPE_STARTTLS;
  else
    tls_type = GOA_TLS_TYPE_NONE;

  return tls_type;
}

static GoaTlsType
get_tls_type_from_string_id (const gchar *str)
{
  GoaTlsType tls_type;

  if (g_strcmp0 (str, "none") == 0)
    tls_type = GOA_TLS_TYPE_NONE;
  else if (g_strcmp0 (str, "starttls") == 0)
    tls_type = GOA_TLS_TYPE_STARTTLS;
  else if (g_strcmp0 (str, "ssl") == 0)
    tls_type = GOA_TLS_TYPE_SSL;
  else
    g_assert_not_reached ();

  return tls_type;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  GVariant *credentials = NULL;
  GoaMailAuth *imap_auth = NULL;
  GoaMailAuth *smtp_auth = NULL;
  GoaMailClient *mail_client = NULL;
  GoaTlsType imap_tls_type;
  GoaTlsType smtp_tls_type;
  gboolean imap_accept_ssl_errors;
  gboolean ret = FALSE;
  gboolean smtp_accept_ssl_errors;
  gchar *domain = NULL;
  gchar *email_address = NULL;
  gchar *imap_password = NULL;
  gchar *imap_server = NULL;
  gchar *imap_username = NULL;
  gchar *smtp_password = NULL;
  gchar *smtp_server = NULL;
  gchar *smtp_username = NULL;

  if (!goa_utils_get_credentials (provider, object, "imap-password", NULL, &imap_password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  mail_client = goa_mail_client_new ();

  /* IMAP */

  imap_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "ImapAcceptSslErrors");
  imap_server = goa_util_lookup_keyfile_string (object, "ImapHost");
  imap_username = goa_util_lookup_keyfile_string (object, "ImapUserName");
  imap_tls_type = get_tls_type_from_object (object, "ImapUseSsl", "ImapUseTls");

  imap_auth = goa_imap_auth_login_new (imap_username, imap_password);
  if (!goa_mail_client_check_sync (mail_client,
                                   imap_server,
                                   imap_tls_type,
                                   imap_accept_ssl_errors,
                                   (imap_tls_type == GOA_TLS_TYPE_SSL) ? 993 : 143,
                                   imap_auth,
                                   cancellable,
                                   error))
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is a field name. The
                           * second %s is the IMAP
                           * username (eg., rishi), and the (%s, %d)
                           * is the error domain and code.
                           */
                          _("Invalid %s with username “%s” (%s, %d): "),
                          "imap-password",
                          imap_username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  /* SMTP */

  if (!goa_util_lookup_keyfile_boolean (object, "SmtpUseAuth"))
    goto smtp_done;

  if (!goa_utils_get_credentials (provider, object, "smtp-password", NULL, &smtp_password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  smtp_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "SmtpAcceptSslErrors");
  smtp_server = goa_util_lookup_keyfile_string (object, "SmtpHost");
  smtp_username = goa_util_lookup_keyfile_string (object, "SmtpUserName");
  smtp_tls_type = get_tls_type_from_object (object, "SmtpUseSsl", "SmtpUseTls");

  email_address = goa_util_lookup_keyfile_string (object, "EmailAddress");
  goa_utils_parse_email_address (email_address, NULL, &domain);
  smtp_auth = goa_smtp_auth_new (domain, smtp_username, smtp_password);
  if (!goa_mail_client_check_sync (mail_client,
                                   smtp_server,
                                   smtp_tls_type,
                                   smtp_accept_ssl_errors,
                                   (smtp_tls_type == GOA_TLS_TYPE_SSL) ? 465 : 587,
                                   smtp_auth,
                                   cancellable,
                                   error))
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is a field name. The
                           * second %s is the SMTP
                           * username (eg., rishi), and the (%s, %d)
                           * is the error domain and code.
                           */
                          _("Invalid %s with username “%s” (%s, %d): "),
                          "smtp-password",
                          smtp_username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

 smtp_done:

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  ret = TRUE;

 out:
  g_clear_object (&imap_auth);
  g_clear_object (&smtp_auth);
  g_clear_object (&mail_client);
  g_free (domain);
  g_free (email_address);
  g_free (imap_password);
  g_free (imap_server);
  g_free (imap_username);
  g_free (smtp_password);
  g_free (smtp_server);
  g_free (smtp_username);
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_combo_box (GtkWidget     *grid,
               gint           row,
               const gchar   *text,
               GtkWidget    **out_combo_box)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *combo_box;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  combo_box = gtk_combo_box_text_new ();
  gtk_widget_set_hexpand (combo_box, TRUE);
  gtk_grid_attach (GTK_GRID (grid), combo_box, 1, row, 3, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
  if (out_combo_box != NULL)
    *out_combo_box = combo_box;
}

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *notebook;
  GtkWidget *forward_button;
  GtkWidget *progress_grid;

  GtkWidget *email_address;
  GtkWidget *name;

  GtkWidget *imap_server;
  GtkWidget *imap_username;
  GtkWidget *imap_password;
  GtkWidget *imap_encryption;

  GtkWidget *smtp_server;
  GtkWidget *smtp_username;
  GtkWidget *smtp_password;
  GtkWidget *smtp_encryption;

  gchar *account_object_path;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_email_address_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add;
  const gchar *email;

  email = gtk_entry_get_text (GTK_ENTRY (data->email_address));
  can_add = goa_utils_parse_email_address (email, NULL, NULL);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
}

static void
on_imap_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add;

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->imap_password)) != 0
            && gtk_entry_get_text_length (GTK_ENTRY (data->imap_server)) != 0
            && gtk_entry_get_text_length (GTK_ENTRY (data->imap_username)) != 0;
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
}

static void
on_smtp_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add = FALSE;

  if (gtk_entry_get_text_length (GTK_ENTRY (data->smtp_server)) == 0)
    goto out;

  /* If you provide a password then you must provide a username, and
   * vice versa. If the server does not require authentication then
   * both should be left empty.
   */
  if (gtk_entry_get_text_length (GTK_ENTRY (data->smtp_password)) != 0
      && gtk_entry_get_text_length (GTK_ENTRY (data->smtp_username)) == 0)
    goto out;

  if (gtk_entry_get_text_length (GTK_ENTRY (data->smtp_password)) == 0
      && gtk_entry_get_text_length (GTK_ENTRY (data->smtp_username)) != 0)
    goto out;

  can_add = TRUE;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
}

static void
create_encryption_ui (GtkWidget  *grid,
                      gint        row,
                      GtkWidget **out_combo_box)
{
  /* Translators: the following four strings are used to show a
   * combo box similar to the one in the evolution module.
   * Encryption: None
   *             STARTTLS after connecting
   *             SSL on a dedicated port
   */
  add_combo_box (grid, row, _("_Encryption"), out_combo_box);
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (*out_combo_box),
                             "none",
                             _("None"));
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (*out_combo_box),
                             "starttls",
                             _("STARTTLS after connecting"));
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (*out_combo_box),
                             "ssl",
                             _("SSL on a dedicated port"));
  gtk_combo_box_set_active_id (GTK_COMBO_BOX (*out_combo_box), "starttls");
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
  const gchar *real_name;

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
  gtk_label_set_max_width_chars (GTK_LABEL (data->cluebar_label), 36);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  data->notebook = gtk_notebook_new ();
  gtk_notebook_set_show_border (GTK_NOTEBOOK (data->notebook), FALSE);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (data->notebook), FALSE);
  gtk_container_add (GTK_CONTAINER (grid0), data->notebook);

  /* Introduction*/

  if (new_account)
    {
      grid1 = gtk_grid_new ();
      gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
      gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
      gtk_notebook_append_page (GTK_NOTEBOOK (data->notebook), grid1, NULL);

      row = 0;
      add_entry (grid1, row++, _("_E-mail"), &data->email_address);
      add_entry (grid1, row++, _("_Name"), &data->name);

      real_name = g_get_real_name ();
      if (g_strcmp0 (real_name, "Unknown") != 0)
        gtk_entry_set_text (GTK_ENTRY (data->name), real_name);

      g_signal_connect (data->email_address, "changed", G_CALLBACK (on_email_address_changed), data);
    }

  /* IMAP */

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_notebook_append_page (GTK_NOTEBOOK (data->notebook), grid1, NULL);

  row = 0;
  add_entry (grid1, row++, _("IMAP _Server"), &data->imap_server);
  add_entry (grid1, row++, _("User_name"), &data->imap_username);
  add_entry (grid1, row++, _("_Password"), &data->imap_password);
  gtk_entry_set_visibility (GTK_ENTRY (data->imap_password), FALSE);

  if (new_account)
    create_encryption_ui (grid1, row++, &data->imap_encryption);

  g_signal_connect (data->imap_server, "changed", G_CALLBACK (on_imap_changed), data);
  g_signal_connect (data->imap_username, "changed", G_CALLBACK (on_imap_changed), data);
  g_signal_connect (data->imap_password, "changed", G_CALLBACK (on_imap_changed), data);

  /* SMTP */

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_notebook_append_page (GTK_NOTEBOOK (data->notebook), grid1, NULL);

  row = 0;
  add_entry (grid1, row++, _("SMTP _Server"), &data->smtp_server);
  add_entry (grid1, row++, _("User_name"), &data->smtp_username);
  add_entry (grid1, row++, _("_Password"), &data->smtp_password);
  gtk_entry_set_visibility (GTK_ENTRY (data->smtp_password), FALSE);

  if (new_account)
    create_encryption_ui (grid1, row++, &data->smtp_encryption);

  g_signal_connect (data->smtp_server, "changed", G_CALLBACK (on_smtp_changed), data);
  g_signal_connect (data->smtp_username, "changed", G_CALLBACK (on_smtp_changed), data);
  g_signal_connect (data->smtp_password, "changed", G_CALLBACK (on_smtp_changed), data);

  /* -- */

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->forward_button = gtk_dialog_add_button (data->dialog, _("_Forward"), GTK_RESPONSE_OK);
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
guess_imap_smtp (AddAccountData *data)
{
  const gchar *email_address;
  gchar *imap_server = NULL;
  gchar *smtp_server = NULL;
  gchar *username = NULL;
  gchar *domain = NULL;

  email_address = gtk_entry_get_text (GTK_ENTRY (data->email_address));
  if (!goa_utils_parse_email_address (email_address, &username, &domain))
    goto out;

  /* TODO: Consult http://api.gnome.org/evolution/autoconfig/1.1/<domain> */

  imap_server = g_strconcat ("imap.", domain, NULL);
  smtp_server = g_strconcat ("smtp.", domain, NULL);

  gtk_entry_set_text (GTK_ENTRY (data->imap_username), username);
  gtk_entry_set_text (GTK_ENTRY (data->smtp_username), username);
  gtk_entry_set_text (GTK_ENTRY (data->imap_server), imap_server);
  gtk_entry_set_text (GTK_ENTRY (data->smtp_server), smtp_server);

 out:
  g_free (imap_server);
  g_free (smtp_server);
  g_free (username);
  g_free (domain);
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
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

static void
mail_check_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaMailClient *client = GOA_MAIL_CLIENT (source_object);
  AddAccountData *data = user_data;

  goa_mail_client_check_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->forward_button, TRUE);
  show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);
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
  GoaMailAuth *imap_auth = NULL;
  GoaMailAuth *smtp_auth = NULL;
  GoaMailClient *mail_client = NULL;
  GoaObject *ret = NULL;
  GoaTlsType imap_tls_type;
  GoaTlsType smtp_tls_type;
  gboolean imap_accept_ssl_errors;
  gboolean smtp_accept_ssl_errors;
  gboolean smtp_auth_login;
  gboolean smtp_auth_plain;
  gboolean smtp_use_auth;
  const gchar *email_address;
  const gchar *encryption;
  const gchar *imap_password;
  const gchar *imap_server;
  const gchar *imap_username;
  const gchar *name;
  const gchar *provider_type;
  const gchar *smtp_password;
  const gchar *smtp_server;
  const gchar *smtp_username;
  gchar *domain = NULL;
  gint response;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  mail_client = goa_mail_client_new ();

  /* Introduction */

  gtk_notebook_set_current_page (GTK_NOTEBOOK (data.notebook), 0);
  gtk_widget_grab_focus (data.email_address);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  email_address = gtk_entry_get_text (GTK_ENTRY (data.email_address));
  name = gtk_entry_get_text (GTK_ENTRY (data.name));

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  email_address,
                                  email_address,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &data.error))
    goto out;

  guess_imap_smtp (&data);

  /* IMAP */

  gtk_notebook_next_page (GTK_NOTEBOOK (data.notebook));
  gtk_widget_grab_focus (data.imap_password);

  imap_accept_ssl_errors = FALSE;

 imap_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);

  encryption = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data.imap_encryption));
  imap_tls_type = get_tls_type_from_string_id (encryption);

  imap_password = gtk_entry_get_text (GTK_ENTRY (data.imap_password));
  imap_server = gtk_entry_get_text (GTK_ENTRY (data.imap_server));
  imap_username = gtk_entry_get_text (GTK_ENTRY (data.imap_username));

  g_cancellable_reset (data.cancellable);
  imap_auth = goa_imap_auth_login_new (imap_username, imap_password);
  goa_mail_client_check (mail_client,
                         imap_server,
                         imap_tls_type,
                         imap_accept_ssl_errors,
                         (imap_tls_type == GOA_TLS_TYPE_SSL) ? 993 : 143,
                         imap_auth,
                         data.cancellable,
                         mail_check_cb,
                         &data);

  gtk_widget_set_sensitive (data.forward_button, FALSE);
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
      gchar *markup;

      if (data.error->code == GOA_ERROR_SSL)
        {
          gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Ignore"));
          imap_accept_ssl_errors = TRUE;
        }
      else
        {
          gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Try Again"));
          imap_accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s</b>\n%s",
                                _("Error connecting to IMAP server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_object (&imap_auth);
      goto imap_again;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);
  gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Forward"));

  /* SMTP */

  /* Re-use the username and password from the IMAP page */
  gtk_entry_set_text (GTK_ENTRY (data.smtp_username), imap_username);
  gtk_entry_set_text (GTK_ENTRY (data.smtp_password), imap_password);

  gtk_notebook_next_page (GTK_NOTEBOOK (data.notebook));
  gtk_widget_grab_focus (data.smtp_password);

  smtp_accept_ssl_errors = FALSE;

 smtp_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);

  encryption = gtk_combo_box_get_active_id (GTK_COMBO_BOX (data.smtp_encryption));
  smtp_tls_type = get_tls_type_from_string_id (encryption);

  smtp_password = gtk_entry_get_text (GTK_ENTRY (data.smtp_password));
  smtp_server = gtk_entry_get_text (GTK_ENTRY (data.smtp_server));
  smtp_username = gtk_entry_get_text (GTK_ENTRY (data.smtp_username));

  g_cancellable_reset (data.cancellable);
  goa_utils_parse_email_address (email_address, NULL, &domain);
  smtp_auth = goa_smtp_auth_new (domain, smtp_username, smtp_password);
  goa_mail_client_check (mail_client,
                         smtp_server,
                         smtp_tls_type,
                         smtp_accept_ssl_errors,
                         (smtp_tls_type == GOA_TLS_TYPE_SSL) ? 465 : 587,
                         smtp_auth,
                         data.cancellable,
                         mail_check_cb,
                         &data);

  gtk_widget_set_sensitive (data.forward_button, FALSE);
  show_progress_ui (GTK_CONTAINER (data.progress_grid), TRUE);
  g_main_loop_run (data.loop);

  smtp_use_auth = goa_mail_auth_is_needed (smtp_auth);
  smtp_auth_login = goa_smtp_auth_is_login (GOA_SMTP_AUTH (smtp_auth));
  smtp_auth_plain = goa_smtp_auth_is_plain (GOA_SMTP_AUTH (smtp_auth));

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
          gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Ignore"));
          smtp_accept_ssl_errors = TRUE;
        }
      else
        {
          gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Try Again"));
          smtp_accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s</b>\n%s",
                                _("Error connecting to SMTP server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_object (&smtp_auth);
      g_clear_pointer (&domain, g_free);
      goto smtp_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "imap-password", g_variant_new_string (imap_password));
  if (smtp_use_auth)
    g_variant_builder_add (&credentials, "{sv}", "smtp-password", g_variant_new_string (smtp_password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Enabled", "true");
  g_variant_builder_add (&details, "{ss}", "EmailAddress", email_address);
  g_variant_builder_add (&details, "{ss}", "Name", name);
  g_variant_builder_add (&details, "{ss}", "ImapHost", imap_server);
  g_variant_builder_add (&details, "{ss}", "ImapUserName", imap_username);
  g_variant_builder_add (&details, "{ss}",
                         "ImapUseSsl", (imap_tls_type == GOA_TLS_TYPE_SSL) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}",
                         "ImapUseTls", (imap_tls_type == GOA_TLS_TYPE_STARTTLS) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}",
                         "ImapAcceptSslErrors", (imap_accept_ssl_errors) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "SmtpHost", smtp_server);
  g_variant_builder_add (&details, "{ss}", "SmtpUseAuth", (smtp_use_auth) ? "true" : "false");
  if (smtp_use_auth)
    {
      g_variant_builder_add (&details, "{ss}", "SmtpUserName", smtp_username);
      g_variant_builder_add (&details, "{ss}", "SmtpAuthLogin", (smtp_auth_login) ? "true" : "false");
      g_variant_builder_add (&details, "{ss}", "SmtpAuthPlain", (smtp_auth_plain) ? "true" : "false");
    }
  g_variant_builder_add (&details, "{ss}",
                         "SmtpUseSsl", (smtp_tls_type == GOA_TLS_TYPE_SSL) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}",
                         "SmtpUseTls", (smtp_tls_type == GOA_TLS_TYPE_STARTTLS) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}",
                         "SmtpAcceptSslErrors", (smtp_accept_ssl_errors) ? "true" : "false");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                email_address,
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

  g_free (domain);
  g_free (data.account_object_path);
  g_clear_pointer (&data.loop, (GDestroyNotify) g_main_loop_unref);
  g_clear_object (&data.cancellable);
  g_clear_object (&imap_auth);
  g_clear_object (&smtp_auth);
  g_clear_object (&mail_client);
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
  GoaMailAuth *imap_auth = NULL;
  GoaMailAuth *smtp_auth = NULL;
  GoaMailClient *mail_client = NULL;
  GoaTlsType imap_tls_type;
  GoaTlsType smtp_tls_type;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean imap_accept_ssl_errors;
  gboolean ret = FALSE;
  gboolean smtp_accept_ssl_errors;
  gboolean smtp_use_auth;
  const gchar *imap_password;
  const gchar *smtp_password;
  gchar *domain = NULL;
  gchar *email_address = NULL;
  gchar *imap_server = NULL;
  gchar *imap_username = NULL;
  gchar *smtp_server = NULL;
  gchar *smtp_username = NULL;
  gint response;

  g_return_val_if_fail (GOA_IS_IMAP_SMTP_PROVIDER (provider), FALSE);
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

  email_address = goa_util_lookup_keyfile_string (object, "EmailAddress");

  imap_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "ImapAcceptSslErrors");
  smtp_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "SmtpAcceptSslErrors");

  imap_tls_type = get_tls_type_from_object (object, "ImapUseSsl", "ImapUseTls");
  smtp_tls_type = get_tls_type_from_object (object, "SmtpUseSsl", "SmtpUseTls");

  imap_server = goa_util_lookup_keyfile_string (object, "ImapHost");
  gtk_entry_set_text (GTK_ENTRY (data.imap_server), imap_server);
  gtk_editable_set_editable (GTK_EDITABLE (data.imap_server), FALSE);

  imap_username = goa_util_lookup_keyfile_string (object, "ImapUserName");
  gtk_entry_set_text (GTK_ENTRY (data.imap_username), imap_username);
  gtk_editable_set_editable (GTK_EDITABLE (data.imap_username), FALSE);

  smtp_use_auth = goa_util_lookup_keyfile_boolean (object, "SmtpUseAuth");
  if (smtp_use_auth)
    {
      smtp_server = goa_util_lookup_keyfile_string (object, "SmtpHost");
      gtk_entry_set_text (GTK_ENTRY (data.smtp_server), smtp_server);
      gtk_editable_set_editable (GTK_EDITABLE (data.smtp_server), FALSE);

      smtp_username = goa_util_lookup_keyfile_string (object, "SmtpUserName");
      gtk_entry_set_text (GTK_ENTRY (data.smtp_username), smtp_username);
      gtk_editable_set_editable (GTK_EDITABLE (data.smtp_username), FALSE);
    }

  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  mail_client = goa_mail_client_new ();

  /* IMAP */

  gtk_notebook_set_current_page (GTK_NOTEBOOK (data.notebook), 0);
  gtk_widget_grab_focus (data.imap_password);

 imap_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);

  imap_password = gtk_entry_get_text (GTK_ENTRY (data.imap_password));
  g_cancellable_reset (data.cancellable);
  imap_auth = goa_imap_auth_login_new (imap_username, imap_password);
  goa_mail_client_check (mail_client,
                         imap_server,
                         imap_tls_type,
                         imap_accept_ssl_errors,
                         (imap_tls_type == GOA_TLS_TYPE_SSL) ? 993 : 143,
                         imap_auth,
                         data.cancellable,
                         mail_check_cb,
                         &data);

  gtk_widget_set_sensitive (data.forward_button, FALSE);
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
      gchar *markup;

      markup = g_strdup_printf ("<b>%s</b>\n%s",
                                _("Error connecting to IMAP server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_object (&imap_auth);
      goto imap_again;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);
  gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Forward"));

  /* SMTP */

  if (!smtp_use_auth)
    goto smtp_done;

  /* Re-use the password from the IMAP page */
  gtk_entry_set_text (GTK_ENTRY (data.smtp_password), imap_password);

  gtk_notebook_next_page (GTK_NOTEBOOK (data.notebook));
  gtk_widget_grab_focus (data.smtp_password);

 smtp_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  gtk_widget_hide (data.cluebar);

  smtp_password = gtk_entry_get_text (GTK_ENTRY (data.smtp_password));
  g_cancellable_reset (data.cancellable);
  goa_utils_parse_email_address (email_address, NULL, &domain);
  smtp_auth = goa_smtp_auth_new (domain, smtp_username, smtp_password);
  goa_mail_client_check (mail_client,
                         smtp_server,
                         smtp_tls_type,
                         smtp_accept_ssl_errors,
                         (smtp_tls_type == GOA_TLS_TYPE_SSL) ? 465 : 587,
                         smtp_auth,
                         data.cancellable,
                         mail_check_cb,
                         &data);

  gtk_widget_set_sensitive (data.forward_button, FALSE);
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
      gchar *markup;

      markup = g_strdup_printf ("<b>%s</b>\n%s",
                                _("Error connecting to SMTP server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_button_set_label (GTK_BUTTON (data.forward_button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_object (&smtp_auth);
      g_clear_pointer (&domain, g_free);
      goto smtp_again;
    }

 smtp_done:

  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "imap-password", g_variant_new_string (imap_password));
  if (smtp_use_auth)
    g_variant_builder_add (&builder, "{sv}", "smtp-password", g_variant_new_string (smtp_password));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    &data.error))
    goto out;

  account = goa_object_peek_account (object);
  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);

  gtk_widget_destroy (dialog);
  g_free (domain);
  g_free (email_address);
  g_free (imap_server);
  g_free (imap_username);
  g_free (smtp_server);
  g_free (smtp_username);
  g_clear_pointer (&data.loop, (GDestroyNotify) g_main_loop_unref);
  g_clear_object (&data.cancellable);
  g_clear_object (&imap_auth);
  g_clear_object (&smtp_auth);
  g_clear_object (&mail_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_label (GtkWidget *grid, gint row, const gchar *left, const gchar *right)
{
  GtkStyleContext *context;
  GtkWidget *label;

  label = gtk_label_new (left);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  label = gtk_label_new (right);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
  gtk_label_set_width_chars (GTK_LABEL (label), 24);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_grid_attach (GTK_GRID (grid), label, 1, row, 3, 1);
}

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              G_GNUC_UNUSED GtkGrid *dummy1,
              G_GNUC_UNUSED GtkGrid *dummy2)
{
  GtkWidget *grid;
  const gchar *username;
  gchar *value_str;
  gchar *value_str_1;
  gint row = 0;

  goa_utils_account_add_attention_needed (client, object, provider, vbox);

  grid = gtk_grid_new ();
  gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (grid, TRUE);
  gtk_widget_set_margin_end (grid, 72);
  gtk_widget_set_margin_start (grid, 72);
  gtk_widget_set_margin_top (grid, 24);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_container_add (GTK_CONTAINER (vbox), grid);

  goa_utils_account_add_header (object, GTK_GRID (grid), row++);

  username = g_get_user_name ();

  value_str = goa_util_lookup_keyfile_string (object, "EmailAddress");
  show_label (GTK_WIDGET (grid), row++, _("E-mail"), value_str);
  g_free (value_str);

  value_str = goa_util_lookup_keyfile_string (object, "Name");
  show_label (GTK_WIDGET (grid), row++, _("Name"), value_str);
  g_free (value_str);

  value_str = goa_util_lookup_keyfile_string (object, "ImapHost");
  value_str_1 = goa_util_lookup_keyfile_string (object, "ImapUserName");
  if (g_strcmp0 (username, value_str_1) != 0)
    {
      gchar *tmp;

      tmp = g_strconcat (value_str_1, "@", value_str, NULL);
      show_label (GTK_WIDGET (grid), row++, _("IMAP"), tmp);
      g_free (tmp);
    }
  else
      show_label (GTK_WIDGET (grid), row++, _("IMAP"), value_str);
  g_free (value_str_1);
  g_free (value_str);

  value_str = goa_util_lookup_keyfile_string (object, "SmtpHost");
  value_str_1 = goa_util_lookup_keyfile_string (object, "SmtpUserName");
  if (value_str_1 != NULL && g_strcmp0 (username, value_str_1) != 0)
    {
      gchar *tmp;

      tmp = g_strconcat (value_str_1, "@", value_str, NULL);
      show_label (GTK_WIDGET (grid), row++, _("SMTP"), tmp);
      g_free (tmp);
    }
  else
      show_label (GTK_WIDGET (grid), row++, _("SMTP"), value_str);
  g_free (value_str_1);
  g_free (value_str);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_imap_smtp_provider_init (GoaImapSmtpProvider *provider)
{
}

static void
goa_imap_smtp_provider_class_init (GoaImapSmtpProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
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
  if (!goa_utils_get_credentials (provider, object, id, NULL, &password, NULL, &error))
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
