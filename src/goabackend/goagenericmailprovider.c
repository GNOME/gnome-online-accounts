/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "goalogging.h"
#include "goaprovider.h"
#include "goagenericmailprovider.h"
#include "goaimapclient.h"
#include "goaimapauthlogin.h"
#include "goaeditablelabel.h"

#include "goaimapmail.h"

/**
 * GoaGenericMailProvider:
 *
 * The #GoaGenericMailProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaGenericMailProvider
{
  /*< private >*/
  GoaProvider parent_instance;
};

typedef struct _GoaGenericMailProviderClass GoaGenericMailProviderClass;

struct _GoaGenericMailProviderClass
{
  GoaProviderClass parent_class;
};

/**
 * SECTION:goagenericmailprovider
 * @title: GoaGenericMailProvider
 * @short_description: A provider for standards-based mail servers
 *
 * #GoaGenericMailProvider is used to access standards-based Internet
 * mail servers.
 */

G_DEFINE_TYPE_WITH_CODE (GoaGenericMailProvider, goa_generic_mail_provider, GOA_TYPE_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "generic_mail",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "generic_mail";
}

static const gchar *
get_name (GoaProvider *_provider)
{
  return _("Mail Account");
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
              GError             **error)
{
  GoaAccount *account;
  GoaMail *mail;
  GoaPasswordBased *password_based;
  gboolean ret;
  gchar *imap_host_and_port;
  gboolean imap_use_tls;
  gboolean imap_ignore_bad_tls;
  gchar *imap_user_name;
  gchar *imap_password;
  gchar *email_address;
  gboolean enabled;

  account = NULL;
  mail = NULL;
  password_based = NULL;
  imap_host_and_port = NULL;
  imap_user_name = NULL;
  imap_password = NULL;
  email_address = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_generic_mail_provider_parent_class)->build_object (provider,
                                                                                  object,
                                                                                  key_file,
                                                                                  group,
                                                                                  error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* mail */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  enabled = g_key_file_get_boolean (key_file, group, "Enabled", NULL);
  if (enabled)
    {
      email_address = g_key_file_get_string (key_file, group, "EmailAddress", NULL);
      imap_host_and_port = g_key_file_get_string (key_file, group, "ImapHost", NULL);
      imap_use_tls = g_key_file_get_boolean (key_file, group, "ImapUseTls", NULL);
      imap_ignore_bad_tls = g_key_file_get_boolean (key_file, group, "ImapIgnoreBadTls", NULL);
      imap_user_name = g_key_file_get_string (key_file, group, "ImapUserName", NULL);
      imap_password = g_key_file_get_string (key_file, group, "ImapPassword", NULL);
      if (imap_host_and_port != NULL)
        {
          if (mail == NULL)
            {
              GoaImapAuth *auth;
              if (imap_user_name == NULL)
                imap_user_name = g_strdup (g_get_user_name ());
              auth = goa_imap_auth_login_new (provider, GOA_OBJECT (object), imap_user_name, imap_password);
              mail = goa_imap_mail_new (imap_host_and_port, imap_use_tls, imap_ignore_bad_tls, auth);
              goa_object_skeleton_set_mail (object, mail);
              g_object_unref (auth);
            }
        }
      else
        {
          if (mail != NULL)
            goa_object_skeleton_set_mail (object, NULL);
        }
      /* TODO: support substitutions a'la ${USERNAME}@redhat.com for e.g. system-wide .conf files */
      goa_mail_set_email_address (mail, email_address);
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

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

  ret = TRUE;

 out:
  g_free (email_address);
  if (password_based != NULL)
    g_object_unref (password_based);
  if (mail != NULL)
    g_object_unref (mail);
  g_free (imap_host_and_port);
  g_free (imap_user_name);
  g_free (imap_password);
  if (account != NULL)
    g_object_unref (account);
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
  /* TODO: we could try and log into the mail server etc. but for now we don't */
  if (out_expires_in != NULL)
    *out_expires_in = 0;
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *table,
           guint          row,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new (NULL);
  gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), text);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label,
                    0, 1,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry,
                    1, 2,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_entry != NULL)
    *out_entry = entry;
}

static void
add_check_button (GtkWidget     *table,
                  guint          row,
                  const gchar   *text,
                  GtkWidget    **out_check_button)
{
  GtkWidget *button;
  button = gtk_check_button_new_with_mnemonic (text);
  gtk_table_attach (GTK_TABLE (table), button,
                    1, 2,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
  if (out_check_button != NULL)
    *out_check_button = button;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;

  GtkWidget *intro_address_entry;
  GtkWidget *intro_desc_entry;

  GtkWidget *imap_server_entry;
  GtkWidget *imap_user_entry;
  GtkWidget *imap_password_entry;
  GtkWidget *imap_show_password;
  GtkWidget *imap_use_tls;
  GtkWidget *imap_ignore_bad_tls;

  GtkWidget *smtp_server_entry;
  GtkWidget *smtp_user_entry;
  GtkWidget *smtp_password_entry;
  GtkWidget *smtp_show_password;
  GtkWidget *smtp_use_tls;
  GtkWidget *smtp_ignore_bad_tls;

  gchar *account_object_path;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */


static void
update_intro (AddAccountData *data)
{
  const gchar *address;
  const gchar *desc;
  gboolean address_valid;
  gboolean desc_valid;

  address_valid = FALSE;
  desc_valid = FALSE;

  address = gtk_entry_get_text (GTK_ENTRY (data->intro_address_entry));
  if (address != NULL)
    {
      const gchar *s;
      /* TODO: could do more validation */
      s = strstr (address, "@");
      if (s != NULL && strstr (s + 1, ".") != NULL)
        address_valid = TRUE;
    }

  desc = gtk_entry_get_text (GTK_ENTRY (data->intro_desc_entry));
  if (desc != NULL && strlen (desc) > 0)
    {
      desc_valid = TRUE;
    }

  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, address_valid && desc_valid);
}

static void
on_intro_entry_changed (GtkEditable *editable,
                        gpointer     user_data)
{
  AddAccountData *data = user_data;
  update_intro (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_imap_show_password_toggled (GtkToggleButton   *button,
                               gpointer           user_data)
{
  AddAccountData *data = user_data;
  gtk_entry_set_visibility (GTK_ENTRY (data->imap_password_entry),
                            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->imap_show_password)));
}

static void
on_imap_tls_toggled (GtkToggleButton   *button,
                     gpointer           user_data)
{
  AddAccountData *data = user_data;
  gtk_widget_set_sensitive (data->imap_ignore_bad_tls,
                            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->imap_use_tls)));
}

static void
update_imap (AddAccountData *data)
{
  gboolean can_go_forward;

  can_go_forward = FALSE;
  if (gtk_entry_get_text_length (GTK_ENTRY (data->imap_server_entry)) > 0 &&
      gtk_entry_get_text_length (GTK_ENTRY (data->imap_user_entry)) > 0 &&
      gtk_entry_get_text_length (GTK_ENTRY (data->imap_password_entry)) > 0)
    {
      can_go_forward = TRUE;
    }
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_go_forward);
}

static void
on_imap_entry_changed (GtkEditable *editable,
                       gpointer     user_data)
{
  AddAccountData *data = user_data;
  update_imap (data);
}

static gboolean
check_imap_settings (AddAccountData  *data,
                     GError         **error)
{
  GoaImapClient *client;
  GoaImapAuth *auth;
  gboolean ret;
  GError *local_error;

  ret = FALSE;

  auth = goa_imap_auth_login_new (NULL,
                                  NULL,
                                  gtk_entry_get_text (GTK_ENTRY (data->imap_user_entry)),
                                  gtk_entry_get_text (GTK_ENTRY (data->imap_password_entry)));

  /* TODO: run in thread and show spinner while connecting */
  client = goa_imap_client_new ();
  if (!goa_imap_client_connect_sync (client,
                                     gtk_entry_get_text (GTK_ENTRY (data->imap_server_entry)),
                                     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->imap_use_tls)),
                                     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->imap_ignore_bad_tls)),
                                     auth,
                                     NULL, /* cancellable */
                                     error))
    goto out;

  ret = TRUE;

 out:
  local_error = NULL;
  if (!goa_imap_client_disconnect_sync (client,
                                        NULL, /* GCancellable */
                                        &local_error))
    {
      goa_warning ("Error closing connection: %s (%s, %d)",
                   local_error->message, g_quark_to_string (local_error->domain), local_error->code);
      g_error_free (local_error);
    }
  g_object_unref (client);
  g_object_unref (auth);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_smtp_show_password_toggled (GtkToggleButton   *button,
                               gpointer           user_data)
{
  AddAccountData *data = user_data;
  gtk_entry_set_visibility (GTK_ENTRY (data->smtp_password_entry),
                            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->smtp_show_password)));
}

static void
on_smtp_tls_toggled (GtkToggleButton   *button,
                     gpointer           user_data)
{
  AddAccountData *data = user_data;
  gtk_widget_set_sensitive (data->smtp_ignore_bad_tls,
                            gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->smtp_use_tls)));
}

static void
update_smtp (AddAccountData *data)
{
  gboolean can_go_forward;

  can_go_forward = FALSE;
  if (gtk_entry_get_text_length (GTK_ENTRY (data->smtp_server_entry)) > 0 &&
      gtk_entry_get_text_length (GTK_ENTRY (data->smtp_user_entry)) > 0 &&
      gtk_entry_get_text_length (GTK_ENTRY (data->smtp_password_entry)) > 0)
    {
      can_go_forward = TRUE;
    }
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_go_forward);
}

static void
on_smtp_entry_changed (GtkEditable *editable,
                       gpointer     user_data)
{
  AddAccountData *data = user_data;
  update_smtp (data);
}

static gboolean
check_smtp_settings (AddAccountData  *data,
                     GError         **error)
{
  /* TODO */
  g_warning ("TODO check_smtp_settings()");
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
intro_auto_set_other_pages (AddAccountData *data)
{
  const gchar *address;
  const gchar *s;
  gchar *user_name;
  gchar *imap_server;
  gchar *smtp_server;

  /* These are just guesses - the use will be able to override himself */

  address = gtk_entry_get_text (GTK_ENTRY (data->intro_address_entry));
  s = strstr (address, "@");
  g_assert (s != NULL);
  user_name = g_strndup (address, s - address);
  imap_server = g_strdup_printf ("imap.%s", s + 1);
  smtp_server = g_strdup_printf ("smtp.%s", s + 1);

  gtk_entry_set_text (GTK_ENTRY (data->imap_user_entry), user_name);
  gtk_entry_set_text (GTK_ENTRY (data->imap_server_entry), imap_server);
  gtk_entry_set_text (GTK_ENTRY (data->smtp_server_entry), smtp_server);

  g_free (user_name);
  g_free (imap_server);
  g_free (smtp_server);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *res,
                gpointer      user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *_provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  GoaGenericMailProvider *provider = GOA_GENERIC_MAIL_PROVIDER (_provider);
  AddAccountData data;
  GoaObject *ret;
  GtkWidget *header_label;
  GtkWidget *table;
  GtkWidget *notebook;
  guint row;
  gint response;
  GError *local_error;
  gchar *s;
  GVariantBuilder builder;

  ret = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  data.cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (data.cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_no_show_all (data.cluebar, TRUE);
  data.cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (data.cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data.cluebar))),
                     data.cluebar_label);
  gtk_box_pack_start (GTK_BOX (vbox), data.cluebar, FALSE, TRUE, 0);

  header_label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (header_label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox), header_label, FALSE, TRUE, 0);

  notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
  gtk_widget_set_margin_left (notebook, 12);
  gtk_box_pack_start (GTK_BOX (vbox), notebook, FALSE, TRUE, 0);

  gtk_widget_show_all (GTK_WIDGET (vbox));
  gtk_dialog_add_button (dialog, "_Forward", GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (dialog, GTK_RESPONSE_OK, FALSE);

  /* ------------------------------------------------------------ */
  /* Set up the Intro page */

  row = 0;
  table = gtk_table_new (2, 2, FALSE);
  gtk_notebook_insert_page (GTK_NOTEBOOK (notebook), table, NULL, -1);
  gtk_table_set_row_spacings (GTK_TABLE (table), 12);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  add_entry (table, row++, _("Email _Address"), &data.intro_address_entry);
  add_entry (table, row++, _("_Description"), &data.intro_desc_entry);
  g_signal_connect (data.intro_address_entry,
                    "changed",
                    G_CALLBACK (on_intro_entry_changed),
                    &data);
  g_signal_connect (data.intro_desc_entry,
                    "changed",
                    G_CALLBACK (on_intro_entry_changed),
                    &data);
  gtk_widget_show_all (table);

  /* ------------------------------------------------------------ */
  /* Show the Intro page */

  s = g_strconcat ("<b>", _("New Mail Account"), "</b>", NULL);
  gtk_label_set_markup (GTK_LABEL (header_label), s);
  g_free (s);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  gtk_widget_grab_focus (data.intro_address_entry);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   "dismissed");
      goto out;
    }

  /* ------------------------------------------------------------ */
  /* Then set up the IMAP page */

  row = 0;
  table = gtk_table_new (2, 2, FALSE);
  gtk_notebook_insert_page (GTK_NOTEBOOK (notebook), table, NULL, -1);
  gtk_table_set_row_spacings (GTK_TABLE (table), 12);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  add_entry (table, row++, _("IMAP _Server"), &data.imap_server_entry);
  add_entry (table, row++, _("_User Name"), &data.imap_user_entry);
  add_entry (table, row++, _("_Password"), &data.imap_password_entry);
  add_check_button (table, row++, _("Sho_w Password"), &data.imap_show_password);
  add_check_button (table, row++, _("Use s_ecure connection"), &data.imap_use_tls);
  add_check_button (table, row++, _("_Don't check certificates"), &data.imap_ignore_bad_tls);
  gtk_entry_set_text (GTK_ENTRY (data.imap_user_entry), g_get_user_name ());
  gtk_entry_set_visibility (GTK_ENTRY (data.imap_password_entry), FALSE);
  g_signal_connect (data.imap_show_password,
                    "toggled",
                    G_CALLBACK (on_imap_show_password_toggled),
                    &data);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data.imap_use_tls), TRUE);
  g_signal_connect (data.imap_use_tls,
                    "toggled",
                    G_CALLBACK (on_imap_tls_toggled),
                    &data);
  g_signal_connect (data.imap_server_entry,
                    "changed",
                    G_CALLBACK (on_imap_entry_changed),
                    &data);
  g_signal_connect (data.imap_user_entry,
                    "changed",
                    G_CALLBACK (on_imap_entry_changed),
                    &data);
  g_signal_connect (data.imap_password_entry,
                    "changed",
                    G_CALLBACK (on_imap_entry_changed),
                    &data);
  gtk_widget_show_all (table);

  /* ------------------------------------------------------------ */
  /* Then set up the SMTP page */

  row = 0;
  table = gtk_table_new (2, 2, FALSE);
  gtk_notebook_insert_page (GTK_NOTEBOOK (notebook), table, NULL, -1);
  gtk_table_set_row_spacings (GTK_TABLE (table), 12);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  add_entry (table, row++, _("SMTP _Server"), &data.smtp_server_entry);
  add_entry (table, row++, _("_User Name"), &data.smtp_user_entry);
  add_entry (table, row++, _("_Password"), &data.smtp_password_entry);
  add_check_button (table, row++, _("Sho_w Password"), &data.smtp_show_password);
  add_check_button (table, row++, _("Use s_ecure connection"), &data.smtp_use_tls);
  add_check_button (table, row++, _("_Don't check certificates"), &data.smtp_ignore_bad_tls);
  gtk_entry_set_text (GTK_ENTRY (data.smtp_user_entry), g_get_user_name ());
  gtk_entry_set_visibility (GTK_ENTRY (data.smtp_password_entry), FALSE);
  g_signal_connect (data.smtp_show_password,
                    "toggled",
                    G_CALLBACK (on_smtp_show_password_toggled),
                    &data);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (data.smtp_use_tls), TRUE);
  g_signal_connect (data.smtp_use_tls,
                    "toggled",
                    G_CALLBACK (on_smtp_tls_toggled),
                    &data);
  g_signal_connect (data.smtp_server_entry,
                    "changed",
                    G_CALLBACK (on_smtp_entry_changed),
                    &data);
  g_signal_connect (data.smtp_user_entry,
                    "changed",
                    G_CALLBACK (on_smtp_entry_changed),
                    &data);
  g_signal_connect (data.smtp_password_entry,
                    "changed",
                    G_CALLBACK (on_smtp_entry_changed),
                    &data);
  gtk_widget_show_all (table);

  /* ------------------------------------------------------------ */
  /* Then prefill other pages from the obtained info */
  intro_auto_set_other_pages (&data);

  /* ------------------------------------------------------------ */
  /* Then show the IMAP page */

  s = g_strconcat ("<b>", _("Receiving Mail"), "</b>", NULL);
  gtk_label_set_markup (GTK_LABEL (header_label), s);
  g_free (s);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 1);
  gtk_widget_grab_focus (data.imap_server_entry);

 imap_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   "dismissed");
      goto out;
    }

  /* Check that IMAP settings work */
  local_error = NULL;
  if (!check_imap_settings (&data, &local_error))
    {
      gchar *markup;
      markup = g_strdup_printf ("<b>%s:</b> %s",
                                _("Error connecting to IMAP server"),
                                local_error->message);
      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      g_free (markup);
      g_error_free (local_error);
      goto imap_again;
    }

  /* hide cluebar errors */
  gtk_widget_hide (data.cluebar);

  /* ------------------------------------------------------------ */
  /* Then show the SMTP page */

  s = g_strconcat ("<b>", _("Sending Mail"), "</b>", NULL);
  gtk_label_set_markup (GTK_LABEL (header_label), s);
  g_free (s);

  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 2);
  gtk_widget_grab_focus (data.smtp_server_entry);

  /* Re-use the password from the IMAP page */
  gtk_entry_set_text (GTK_ENTRY (data.smtp_password_entry),
                      gtk_entry_get_text (GTK_ENTRY (data.imap_password_entry)));

 smtp_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   "dismissed");
      goto out;
    }

  /* Check that SMTP settings work */
  local_error = NULL;
  if (!check_smtp_settings (&data, &local_error))
    {
      gchar *markup;
      markup = g_strdup_printf ("<b>%s:</b> %s",
                                _("Error connecting to SMTP server"),
                                local_error->message);
      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      g_free (markup);
      g_error_free (local_error);
      goto smtp_again;
    }

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&builder, "{ss}", "Enabled", "true");
  g_variant_builder_add (&builder, "{ss}", "EmailAddress",
                         gtk_entry_get_text (GTK_ENTRY (data.intro_address_entry)));
  g_variant_builder_add (&builder, "{ss}", "ImapHost",
                         gtk_entry_get_text (GTK_ENTRY (data.imap_server_entry)));
  g_variant_builder_add (&builder, "{ss}", "ImapUserName",
                         gtk_entry_get_text (GTK_ENTRY (data.imap_user_entry)));
  g_variant_builder_add (&builder, "{ss}", "ImapUseTls",
                         gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.imap_use_tls))  ? "true" : "false");
  g_variant_builder_add (&builder, "{ss}", "ImapIgnoreBadTls",
                         gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.imap_ignore_bad_tls)) ? "true" : "false");
  g_variant_builder_add (&builder, "{ss}", "SmtpHost",
                         gtk_entry_get_text (GTK_ENTRY (data.smtp_server_entry)));
  g_variant_builder_add (&builder, "{ss}", "SmtpUserName",
                         gtk_entry_get_text (GTK_ENTRY (data.smtp_user_entry)));
  g_variant_builder_add (&builder, "{ss}", "SmtpUseTls",
                         gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.smtp_use_tls))  ? "true" : "false");
  g_variant_builder_add (&builder, "{ss}", "SmtpIgnoreBadTls",
                         gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data.smtp_ignore_bad_tls)) ? "true" : "false");
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (GOA_PROVIDER (provider)),
                                gtk_entry_get_text (GTK_ENTRY (data.intro_desc_entry)),
                                g_variant_builder_end (&builder),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      goto out;
    }

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));
  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "imap-password",
                         g_variant_new_string (gtk_entry_get_text (GTK_ENTRY (data.imap_password_entry))));
  g_variant_builder_add (&builder, "{sv}", "smtp-password",
                         g_variant_new_string (gtk_entry_get_text (GTK_ENTRY (data.smtp_password_entry))));
  if (!goa_provider_store_credentials_sync (GOA_PROVIDER (provider),
                                            ret,
                                            g_variant_builder_end (&builder),
                                            NULL, /* GCancellable */
                                            error))
    goto out;

 out:
  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkTable            *table)
{
  gchar *email_address;
  gchar *imap_host;
  gchar *imap_user_name;
  gboolean imap_use_tls;
  gboolean imap_ignore_bad_tls;
  gchar *smtp_host;
  gchar *smtp_user_name;
  gboolean smtp_use_tls;
  gboolean smtp_ignore_bad_tls;
  GString *str;

  /* Chain up */
  GOA_PROVIDER_CLASS (goa_generic_mail_provider_parent_class)->show_account (provider, client, object, vbox, table);

  email_address = goa_util_lookup_keyfile_string (object, "EmailAddress");
  goa_util_add_row_label (table, _("Email Address"), email_address);

  imap_host = goa_util_lookup_keyfile_string (object, "ImapHost");
  imap_user_name = goa_util_lookup_keyfile_string (object, "ImapUserName");
  imap_use_tls = goa_util_lookup_keyfile_boolean (object, "ImapUseTls");
  imap_ignore_bad_tls = goa_util_lookup_keyfile_boolean (object, "ImapIgnoreBadTls");
  str = g_string_new (imap_host);
  if (g_strcmp0 (g_get_user_name (), imap_user_name) != 0)
    g_string_append_printf (str, "\n<small>%s: %s</small>",
                            _("User Name"),
                            imap_user_name);
  if (imap_use_tls)
    {
      if (imap_ignore_bad_tls)
        g_string_append_printf (str, "\n<small><span foreground=\"red\">%s</span></small>",
                                _("Transport Security without Certificate Checks"));
    }
  else
    {
      g_string_append_printf (str, "\n<small><span foreground=\"red\">%s</span></small>",
                              _("No Transport Security"));
    }
  goa_util_add_row_label (table, _("IMAP Server"), str->str);
  g_string_free (str, TRUE);

  smtp_host = goa_util_lookup_keyfile_string (object, "SmtpHost");
  smtp_user_name = goa_util_lookup_keyfile_string (object, "SmtpUserName");
  smtp_use_tls = goa_util_lookup_keyfile_boolean (object, "SmtpUseTls");
  smtp_ignore_bad_tls = goa_util_lookup_keyfile_boolean (object, "SmtpIgnoreBadTls");
  str = g_string_new (smtp_host);
  if (g_strcmp0 (g_get_user_name (), smtp_user_name) != 0)
    g_string_append_printf (str, "\n<small>%s: %s</small>",
                            _("User Name"),
                            smtp_user_name);
  if (smtp_use_tls)
    {
      if (smtp_ignore_bad_tls)
        g_string_append_printf (str, "\n<small><span foreground=\"red\">%s</span></small>",
                                _("Transport Security without Certificate Checks"));
    }
  else
    {
      g_string_append_printf (str, "\n<small><span foreground=\"red\">%s</span></small>",
                              _("No Transport Security"));
    }
  goa_util_add_row_label (table, _("SMTP Server"), str->str);
  g_string_free (str, TRUE);

  goa_util_add_row_switch_from_keyfile (table, object, _("Enabled"), "Enabled");

  /* TODO: we could have a "Edit" button */

  g_free (smtp_host);
  g_free (smtp_user_name);
  g_free (imap_host);
  g_free (imap_user_name);
  g_free (email_address);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_generic_mail_provider_init (GoaGenericMailProvider *provider)
{
}

static void
goa_generic_mail_provider_class_init (GoaGenericMailProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->add_account                = add_account;
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
  GVariant *credentials;
  gchar *password;

  /* TODO: maybe log what app is requesting access */

  password = NULL;
  credentials = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));

  error = NULL;
  credentials = goa_provider_lookup_credentials_sync (provider,
                                                      object,
                                                      NULL, /* GCancellable* */
                                                      &error);
  if (credentials == NULL)
    {
      g_prefix_error (&error, "Error looking up credentials in keyring: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!g_variant_lookup (credentials, id, "s", &password))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED,
                                             "Did not find password with id `%s' in credentials",
                                             id);
      goto out;
    }

  goa_password_based_complete_get_password (interface, invocation, password);

 out:
  g_free (password);
  if (credentials != NULL)
    g_variant_unref (credentials);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}

