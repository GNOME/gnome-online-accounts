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
#include "goamailconfig.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
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
  GKeyFile *goa_conf;
  const gchar *provider_type;
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

  provider_type = goa_provider_get_provider_type (provider);
  goa_conf = goa_util_open_goa_conf ();
  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_MAIL) &&
            g_key_file_get_boolean (key_file, group, "Enabled", NULL);

  g_clear_pointer (&goa_conf, g_key_file_free);

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
                        "smtp-supported", (smtp_host != NULL),
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
  GoaTlsType tls_type = GOA_TLS_TYPE_SSL;

  if (goa_util_lookup_keyfile_boolean (object, ssl_key))
    tls_type = GOA_TLS_TYPE_SSL;
  else if (goa_util_lookup_keyfile_boolean (object, starttls_key))
    tls_type = GOA_TLS_TYPE_STARTTLS;
  else
    g_debug ("%s(): ignoring unencrypted setting", G_STRFUNC);

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
      if (error != NULL && g_error_matches (*error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
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
      if (error != NULL && g_error_matches (*error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
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
  g_clear_pointer (&credentials, g_variant_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

enum
{
  CONFIG_PAGE_EMAIL,
  CONFIG_PAGE_IMAP,
  CONFIG_PAGE_SMTP,
};

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;

  GoaMailAuth *smtp_auth;
  gboolean imap_accept_ssl_errors;
  gboolean imap_had_ssl_errors;
  gboolean smtp_accept_ssl_errors;
  gboolean smtp_had_ssl_errors;
  gboolean smtp_use_auth;
  gboolean smtp_auth_login;
  gboolean smtp_auth_plain;

  GtkWidget *name;
  GtkWidget *email_address;
  GtkWidget *email_password;

  GtkWidget *imap_group;
  GtkWidget *imap_server;
  GtkWidget *imap_username;
  GtkWidget *imap_password;
  GtkWidget *imap_encryption;

  GtkWidget *smtp_group;
  GtkWidget *smtp_server;
  GtkWidget *smtp_username;
  GtkWidget *smtp_password;
  GtkWidget *smtp_encryption;

  GCancellable *discovery;
  GtkWidget *discovery_status;
  GtkWidget *discovery_spinner;
  GtkWidget *imap_discovery;
  GtkWidget *smtp_discovery;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);

  g_cancellable_cancel (data->discovery);
  g_clear_object (&data->discovery);
  g_clear_object (&data->smtp_auth);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_login_changed (GtkWidget      *widget,
                  GParamSpec     *pspec,
                  AddAccountData *data)
{
  const char *username;
  const char *password;
  const char *server;
  GoaDialogState state = GOA_DIALOG_IDLE;

  /* Discovery */
  g_cancellable_cancel (data->discovery);
  g_clear_object (&data->discovery);

  if ((data->discovery_status != NULL)
      && (data->imap_password != widget && data->smtp_password != widget))
    {
      gtk_widget_set_visible (data->discovery_status, FALSE);
      gtk_widget_set_visible (data->imap_discovery, FALSE);
      gtk_widget_set_visible (data->smtp_discovery, FALSE);
    }

  /* IMAP */
  server = gtk_editable_get_text (GTK_EDITABLE (data->imap_server));
  username = gtk_editable_get_text (GTK_EDITABLE (data->imap_username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->imap_password));

  if ((server == NULL || *server == '\0')
      || (username == NULL || *username == '\0')
      || (password == NULL || *password == '\0'))
    {
      goa_provider_dialog_set_state (data->dialog, state);
      return;
    }

  /* SMTP */
  server = gtk_editable_get_text (GTK_EDITABLE (data->smtp_server));
  username = gtk_editable_get_text (GTK_EDITABLE (data->smtp_username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->smtp_password));

  if (server == NULL || *server == '\0')
    {
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_READY);
      return;
    }
  else
    {
      if ((username != NULL && *username != '\0')
          && (password != NULL && *password != '\0'))
        state = GOA_DIALOG_READY;

      /* If the server does not require authentication both should be empty
       */
      else if ((username == NULL || *username == '\0')
               && (password == NULL || *password == '\0'))
        state = GOA_DIALOG_READY;
    }

  goa_provider_dialog_set_state (data->dialog, state);
}

static gpointer
find_preferred_config (GPtrArray  *services,
                       const char *name)
{
  if (services == NULL || name == NULL)
    return NULL;

  for (unsigned int i = 0; i < services->len; i++)
    {
      GoaServiceConfig *config = g_ptr_array_index (services, i);

      if (g_ascii_strcasecmp (goa_service_config_get_service (config), name) == 0)
        return config;
    }

  return NULL;
}

static void
update_account_details_ui (AddAccountData *data,
                           GPtrArray      *services)
{
  GoaMailConfig *imap_config = find_preferred_config (services, GOA_SERVICE_TYPE_IMAP);
  GoaMailConfig *smtp_config = find_preferred_config (services, GOA_SERVICE_TYPE_SMTP);
  GoaTlsType imap_encryption = GOA_TLS_TYPE_SSL;
  GoaTlsType smtp_encryption = GOA_TLS_TYPE_SSL;

  /* IMAP */
  gtk_editable_set_text (GTK_EDITABLE (data->imap_server),
                         imap_config ? goa_mail_config_get_hostname (imap_config) : "");
  gtk_editable_set_text (GTK_EDITABLE (data->imap_username),
                         imap_config ? goa_mail_config_get_username (imap_config) : "");
  if (imap_config != NULL)
    {
      imap_encryption = goa_mail_config_get_encryption (imap_config);
      if (imap_encryption == GOA_TLS_TYPE_NONE)
        imap_encryption = GOA_TLS_TYPE_SSL;
    }
  g_object_set (data->imap_encryption, "selected", imap_encryption - 1, NULL);

  if (imap_config != NULL)
    {
      gtk_accessible_announce (GTK_ACCESSIBLE (data->imap_group),
                               adw_preferences_group_get_title (ADW_PREFERENCES_GROUP (data->imap_group)),
                               GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
      gtk_accessible_announce (GTK_ACCESSIBLE (data->imap_group),
                               _("Auto-detected"),
                               GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
    }

  /* SMTP */
  gtk_editable_set_text (GTK_EDITABLE (data->smtp_server),
                         smtp_config ? goa_mail_config_get_hostname (smtp_config) : "");
  gtk_editable_set_text (GTK_EDITABLE (data->smtp_username),
                         smtp_config ? goa_mail_config_get_username (smtp_config) : "");
  if (smtp_config != NULL)
    {
      smtp_encryption = goa_mail_config_get_encryption (smtp_config);
      if (smtp_encryption == GOA_TLS_TYPE_NONE)
        smtp_encryption = GOA_TLS_TYPE_SSL;
    }
  g_object_set (data->smtp_encryption, "selected", smtp_encryption - 1, NULL);

  if (smtp_config != NULL)
    {
      gtk_accessible_announce (GTK_ACCESSIBLE (data->smtp_group),
                               adw_preferences_group_get_title (ADW_PREFERENCES_GROUP (data->smtp_group)),
                               GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
      gtk_accessible_announce (GTK_ACCESSIBLE (data->smtp_group),
                               _("Auto-detected"),
                               GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
    }

  /* Discovery */
  g_cancellable_cancel (data->discovery);
  g_clear_object (&data->discovery);
  gtk_widget_set_visible (data->discovery_status, (imap_config || smtp_config));
  gtk_widget_set_visible (data->discovery_spinner, FALSE);
  gtk_widget_set_visible (data->imap_discovery, (imap_config != NULL));
  gtk_widget_set_visible (data->smtp_discovery, (smtp_config != NULL));
}

static void
goa_mail_client_discover_cb (GoaMailClient  *client,
                             GAsyncResult   *result,
                             AddAccountData *data)
{
  g_autoptr(GPtrArray) services = NULL;
  g_autoptr(GError) error = NULL;

  services = goa_mail_client_discover_finish (client, result, &error);
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  if (error != NULL)
    {
      g_debug ("%s(): %s", G_STRFUNC, error->message);
      goa_provider_dialog_add_toast (data->dialog,
                                     adw_toast_new (_("Unable to auto-detect IMAP and SMTP settings")));
    }

  update_account_details_ui (data, services);
}

static void
on_email_changed (GtkEditable    *editable,
                  AddAccountData *data)
{
  g_autoptr(GoaMailClient) client = NULL;
  const char *email_address;

  update_account_details_ui (data, NULL);

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  if (!goa_utils_parse_email_address (email_address, NULL, NULL))
    {
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
      return;
    }

  data->discovery = g_cancellable_new ();
  gtk_widget_set_visible (data->discovery_status,
                          TRUE);
  gtk_widget_set_visible (data->discovery_spinner,
                          TRUE);

  client = goa_mail_client_new ();
  goa_mail_client_discover (client,
                            email_address,
                            data->discovery,
                            (GAsyncReadyCallback)goa_mail_client_discover_cb,
                            data);
}

static void
on_password_changed (GtkEditable    *editable,
                     AddAccountData *data)
{
  const char *email_address;
  const char *password;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  if (!goa_utils_parse_email_address (email_address, NULL, NULL))
    {
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
      return;
    }

  password = gtk_editable_get_text (GTK_EDITABLE (data->email_password));
  if (password == NULL || *password == '\0')
    {
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
      return;
    }

  gtk_editable_set_text (GTK_EDITABLE (data->imap_password), password);
  gtk_editable_set_text (GTK_EDITABLE (data->smtp_password), password);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  /* Keep in sync with GoaTlsType */
  static const char * const encryption_types[] = {
    N_("STARTTLS after connecting"), // GOA_TLS_TYPE_STARTTLS
    N_("SSL on a dedicated port"),   // GOA_TLS_TYPE_SSL
    NULL,
  };

  goa_provider_dialog_add_page (dialog,
                                _("Email"),
                                _("Connect to an email account"));

  /* General */
  group = goa_provider_dialog_add_group (dialog, NULL);
  data->name = goa_provider_dialog_add_entry (dialog, group, _("_Name"));
  data->email_address = goa_provider_dialog_add_entry (dialog, group, _("_Email"));
  data->email_password = goa_provider_dialog_add_password_entry (dialog, group, _("_Password"));
  goa_provider_dialog_add_description (dialog, NULL, _("IMAP and SMTP details will be auto-detected from your service provider when possible"));

  if (new_account)
    {
      const char *real_name;
      GtkWidget *icon;

      real_name = g_get_real_name ();
      if (g_strcmp0 (real_name, "Unknown") != 0)
        gtk_editable_set_text (GTK_EDITABLE (data->name), real_name);

      /* Discovery */
      data->discovery_status = g_object_new (GTK_TYPE_BOX,
                                             "margin-start",   8,
                                             "margin-end",     2,
                                             "width-request",  24,
                                             "height-request", 24,
                                             NULL);
      adw_entry_row_add_suffix (ADW_ENTRY_ROW (data->email_address), data->discovery_status);

      icon = gtk_image_new_from_icon_name ("emblem-default-symbolic");
      gtk_widget_set_tooltip_text (GTK_WIDGET (icon), _("Auto-detected"));
      gtk_widget_add_css_class (GTK_WIDGET (icon), "success");
      gtk_box_append (GTK_BOX (data->discovery_status), GTK_WIDGET (icon));

      data->discovery_spinner = gtk_spinner_new ();
      g_object_bind_property (data->discovery_spinner, "visible",
                              icon,                    "visible",
                              G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
      g_object_bind_property (data->discovery_spinner, "visible",
                              data->discovery_spinner, "spinning",
                              G_BINDING_SYNC_CREATE);
      gtk_box_append (GTK_BOX (data->discovery_status), data->discovery_spinner);
    }

  g_signal_connect (data->email_address, "changed", G_CALLBACK (on_email_changed), data);
  g_signal_connect (data->email_password, "changed", G_CALLBACK (on_password_changed), data);

  /* IMAP */
  data->imap_group = goa_provider_dialog_add_group (dialog, _("IMAP Settings"));
  data->imap_server = goa_provider_dialog_add_entry (dialog, data->imap_group, _("IMAP _Server"));
  data->imap_username = goa_provider_dialog_add_entry (dialog, data->imap_group, _("User_name"));
  data->imap_password = goa_provider_dialog_add_password_entry (dialog, data->imap_group, _("_Password"));

  if (new_account)
    {
      GtkWidget *discovery_image;
      GtkWidget *discovery_label;

      data->imap_encryption = goa_provider_dialog_add_combo (dialog,
                                                             data->imap_group,
                                                             _("Encryption"),
                                                             (GStrv)encryption_types);
      g_object_set (data->imap_encryption, "selected", GOA_TLS_TYPE_SSL - 1, NULL);
      g_signal_connect (data->imap_encryption, "notify::selected", G_CALLBACK (on_login_changed), data);

      /* Discovery */
      data->imap_discovery = g_object_new (GTK_TYPE_BOX,
                                           "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP,
                                           "spacing",         8,
                                           NULL);
      gtk_widget_add_css_class (data->imap_discovery, "success");
      discovery_label = gtk_label_new (_("Auto-detected"));
      gtk_accessible_update_relation (GTK_ACCESSIBLE (data->imap_discovery),
                                      GTK_ACCESSIBLE_RELATION_LABELLED_BY, discovery_label, NULL,
                                      -1);
      gtk_box_append (GTK_BOX (data->imap_discovery), discovery_label);
      discovery_image = g_object_new (GTK_TYPE_IMAGE,
                                      "accessible-role", GTK_ACCESSIBLE_ROLE_PRESENTATION,
                                      "icon-name",       "emblem-default-symbolic",
                                      NULL);
      gtk_box_append (GTK_BOX (data->imap_discovery), discovery_image);
      adw_preferences_group_set_header_suffix (ADW_PREFERENCES_GROUP (data->imap_group),
                                               data->imap_discovery);
    }

  goa_provider_dialog_add_description (dialog, data->imap_server, _("Example server: imap.example.com"));

  g_signal_connect (data->imap_server, "notify::text", G_CALLBACK (on_login_changed), data);
  g_signal_connect (data->imap_username, "notify::text", G_CALLBACK (on_login_changed), data);
  g_signal_connect (data->imap_password, "notify::text", G_CALLBACK (on_login_changed), data);

  /* SMTP */
  data->smtp_group = goa_provider_dialog_add_group (dialog, _("SMTP Settings"));
  data->smtp_server = goa_provider_dialog_add_entry (dialog, data->smtp_group, _("SMTP _Server"));
  data->smtp_username = goa_provider_dialog_add_entry (dialog, data->smtp_group, _("User_name"));
  data->smtp_password = goa_provider_dialog_add_password_entry (dialog, data->smtp_group, _("_Password"));

  if (new_account)
    {
      GtkWidget *discovery_image;
      GtkWidget *discovery_label;

      data->smtp_encryption = goa_provider_dialog_add_combo (dialog,
                                                             data->smtp_group,
                                                             _("Encryption"),
                                                             (GStrv)encryption_types);
      g_object_set (data->smtp_encryption, "selected", GOA_TLS_TYPE_SSL - 1, NULL);
      g_signal_connect (data->smtp_encryption, "notify::selected", G_CALLBACK (on_login_changed), data);

      /* Discovery */
      data->smtp_discovery = g_object_new (GTK_TYPE_BOX,
                                           "accessible-role", GTK_ACCESSIBLE_ROLE_GROUP,
                                           "spacing",         8,
                                           NULL);
      gtk_widget_add_css_class (data->smtp_discovery, "success");
      discovery_label = gtk_label_new (_("Auto-detected"));
      gtk_accessible_update_relation (GTK_ACCESSIBLE (data->smtp_discovery),
                                      GTK_ACCESSIBLE_RELATION_LABELLED_BY, discovery_label, NULL,
                                      -1);
      gtk_box_append (GTK_BOX (data->smtp_discovery), discovery_label);
      discovery_image = g_object_new (GTK_TYPE_IMAGE,
                                      "accessible-role", GTK_ACCESSIBLE_ROLE_PRESENTATION,
                                      "icon-name",       "emblem-default-symbolic",
                                      NULL);
      gtk_box_append (GTK_BOX (data->smtp_discovery), discovery_image);
      adw_preferences_group_set_header_suffix (ADW_PREFERENCES_GROUP (data->smtp_group),
                                               data->smtp_discovery);
    }

  goa_provider_dialog_add_description (dialog, data->smtp_server, _("Example server: smtp.example.com"));

  g_signal_connect (data->smtp_server, "notify::text", G_CALLBACK (on_login_changed), data);
  g_signal_connect (data->smtp_username, "notify::text", G_CALLBACK (on_login_changed), data);
  g_signal_connect (data->smtp_password, "notify::text", G_CALLBACK (on_login_changed), data);

  update_account_details_ui (data, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_credentials_cb (GoaManager   *manager,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GDBusObject *ret = NULL;
  g_autofree char *object_path = NULL;
  GError *error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, error);
      return;
    }

  ret = g_dbus_object_manager_get_object (goa_client_get_object_manager (data->client),
                                          object_path);
  goa_provider_task_return_account (task, GOA_OBJECT (ret));
}

static void
add_account_store_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *name = NULL;
  const char *email_address = NULL;
  const char *imap_server = NULL;
  const char *imap_username = NULL;
  const char *imap_password = NULL;
  GoaTlsType imap_tls_type = GOA_TLS_TYPE_SSL;
  const char *smtp_server = NULL;
  const char *smtp_username = NULL;
  const char *smtp_password = NULL;
  GoaTlsType smtp_tls_type = GOA_TLS_TYPE_SSL;

  /* Account is confirmed */
  name = gtk_editable_get_text (GTK_EDITABLE (data->name));
  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));

  imap_server = gtk_editable_get_text (GTK_EDITABLE (data->imap_server));
  imap_username = gtk_editable_get_text (GTK_EDITABLE (data->imap_username));
  imap_password = gtk_editable_get_text (GTK_EDITABLE (data->imap_password));
  g_object_get (data->imap_encryption, "selected", &imap_tls_type, NULL);
  imap_tls_type += 1;

  smtp_server = gtk_editable_get_text (GTK_EDITABLE (data->smtp_server));
  smtp_username = gtk_editable_get_text (GTK_EDITABLE (data->smtp_username));
  smtp_password = gtk_editable_get_text (GTK_EDITABLE (data->smtp_password));
  g_object_get (data->smtp_encryption, "selected", &smtp_tls_type, NULL);
  smtp_tls_type += 1;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "imap-password", g_variant_new_string (imap_password));
  if (data->smtp_use_auth)
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
                         "ImapAcceptSslErrors", (data->imap_accept_ssl_errors) ? "true" : "false");

  if (smtp_server != NULL && *smtp_server != '\0')
    {
      g_variant_builder_add (&details, "{ss}", "SmtpHost", smtp_server);
      g_variant_builder_add (&details, "{ss}", "SmtpUseAuth", (data->smtp_use_auth) ? "true" : "false");
      if (data->smtp_use_auth)
        {
          g_variant_builder_add (&details, "{ss}", "SmtpUserName", smtp_username);
          g_variant_builder_add (&details, "{ss}", "SmtpAuthLogin", (data->smtp_auth_login) ? "true" : "false");
          g_variant_builder_add (&details, "{ss}", "SmtpAuthPlain", (data->smtp_auth_plain) ? "true" : "false");
        }
      g_variant_builder_add (&details, "{ss}",
                             "SmtpUseSsl", (smtp_tls_type == GOA_TLS_TYPE_SSL) ? "true" : "false");
      g_variant_builder_add (&details, "{ss}",
                             "SmtpUseTls", (smtp_tls_type == GOA_TLS_TYPE_STARTTLS) ? "true" : "false");
      g_variant_builder_add (&details, "{ss}",
                             "SmtpAcceptSslErrors", (data->smtp_accept_ssl_errors) ? "true" : "false");
    }

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                email_address,
                                email_address,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));
}

static void
add_account_check_smtp_cb (GoaMailClient *client,
                           GAsyncResult  *result,
                           gpointer       user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (!goa_mail_client_check_finish (client, result, &error))
    {
      data->smtp_had_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  data->smtp_use_auth = goa_mail_auth_is_needed (data->smtp_auth);
  data->smtp_auth_login = goa_smtp_auth_is_login (GOA_SMTP_AUTH (data->smtp_auth));
  data->smtp_auth_plain = goa_smtp_auth_is_plain (GOA_SMTP_AUTH (data->smtp_auth));

  /* Proceed to credential storage */
  add_account_store_credentials (task);
}

static void
add_account_action_smtp (GTask *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GoaMailClient) mail_client = NULL;
  const char *email_address;
  const char *smtp_password;
  const char *smtp_server;
  const char *smtp_username;
  GoaTlsType smtp_tls_type;
  g_autofree char *domain = NULL;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  smtp_server = gtk_editable_get_text (GTK_EDITABLE (data->smtp_server));
  smtp_username = gtk_editable_get_text (GTK_EDITABLE (data->smtp_username));
  smtp_password = gtk_editable_get_text (GTK_EDITABLE (data->smtp_password));
  g_object_get (data->smtp_encryption, "selected", &smtp_tls_type, NULL);
  smtp_tls_type += 1;

  /* If no server was provided, this is an IMAP-only account
   */
  if (smtp_server == NULL || *smtp_server == '\0')
    {
      add_account_store_credentials (task);
      return;
    }

  g_clear_object (&data->smtp_auth);
  goa_utils_parse_email_address (email_address, NULL, &domain);
  data->smtp_auth = goa_smtp_auth_new (domain, smtp_username, smtp_password);

  mail_client = goa_mail_client_new ();
  goa_mail_client_check (mail_client,
                         smtp_server,
                         smtp_tls_type,
                         data->smtp_accept_ssl_errors,
                         (smtp_tls_type == GOA_TLS_TYPE_SSL) ? 465 : 587,
                         data->smtp_auth,
                         cancellable,
                         (GAsyncReadyCallback) add_account_check_smtp_cb,
                         g_object_ref (task));
}

static void
add_account_check_imap_cb (GoaMailClient *client,
                           GAsyncResult  *result,
                           gpointer       user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (!goa_mail_client_check_finish (client, result, &error))
    {
      data->imap_had_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  add_account_action_smtp (task);
}

static void
add_account_action_imap (GTask *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GoaMailClient) mail_client = NULL;
  g_autoptr(GoaMailAuth) imap_auth = NULL;
  const char *imap_server;
  const char *imap_username;
  const char *imap_password;
  GoaTlsType imap_tls_type;

  imap_server = gtk_editable_get_text (GTK_EDITABLE (data->imap_server));
  imap_username = gtk_editable_get_text (GTK_EDITABLE (data->imap_username));
  imap_password = gtk_editable_get_text (GTK_EDITABLE (data->imap_password));
  g_object_get (data->imap_encryption, "selected", &imap_tls_type, NULL);
  imap_tls_type += 1;

  imap_auth = goa_imap_auth_login_new (imap_username, imap_password);

  mail_client = goa_mail_client_new ();
  goa_mail_client_check (mail_client,
                         imap_server,
                         imap_tls_type,
                         data->imap_accept_ssl_errors,
                         (imap_tls_type == GOA_TLS_TYPE_SSL) ? 993 : 143,
                         imap_auth,
                         cancellable,
                         (GAsyncReadyCallback) add_account_check_imap_cb,
                         g_object_ref (task));
}

static void
add_account_action_email (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;
  const char *email_address;
  const char *provider_type;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));

  /* If this is duplicate account we're finished */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (data->client,
                                  email_address,
                                  email_address,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  add_account_action_imap (task);
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  if (data->imap_had_ssl_errors)
    {
      data->imap_had_ssl_errors = FALSE;
      data->imap_accept_ssl_errors = TRUE;
    }

  if (data->smtp_had_ssl_errors)
    {
      data->smtp_had_ssl_errors = FALSE;
      data->smtp_accept_ssl_errors = TRUE;
    }

    add_account_action_email (task);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account (GoaProvider         *provider,
             GoaClient           *client,
             GtkWidget           *parent,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
  AddAccountData *data;
  g_autoptr(GTask) task = NULL;

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new_full (provider, client, parent, 480, 600);
  data->client = g_object_ref (client);

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, add_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, TRUE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (add_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  gtk_widget_grab_focus (data->email_address);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account_credentials_cb (GoaAccount   *account,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!goa_account_call_ensure_credentials_finish (account, NULL, res, &error))
    {
      goa_provider_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
refresh_account_store_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  const char *imap_password;
  const char *smtp_password;
  g_autoptr(GError) error = NULL;

  /* Account is confirmed */
  imap_password = gtk_editable_get_text (GTK_EDITABLE (data->imap_password));
  smtp_password = gtk_editable_get_text (GTK_EDITABLE (data->smtp_password));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "imap-password", g_variant_new_string (imap_password));
  if (data->smtp_use_auth)
    g_variant_builder_add (&credentials, "{sv}", "smtp-password", g_variant_new_string (smtp_password));

  // TODO: run in worker thread
  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    data->object,
                                                    g_variant_builder_end (&credentials),
                                                    cancellable,
                                                    &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_object_ref (task));
}

static void
refresh_account_check_smtp_cb (GoaMailClient *client,
                               GAsyncResult  *result,
                               gpointer       user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (!goa_mail_client_check_finish (client, result, &error))
    {
      data->smtp_had_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  data->smtp_use_auth = goa_mail_auth_is_needed (data->smtp_auth);
  data->smtp_auth_login = goa_smtp_auth_is_login (GOA_SMTP_AUTH (data->smtp_auth));
  data->smtp_auth_plain = goa_smtp_auth_is_plain (GOA_SMTP_AUTH (data->smtp_auth));

  /* Proceed to credential storage */
  refresh_account_store_credentials (task);
}

static void
refresh_account_action_smtp (GTask *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GoaMailClient) mail_client = NULL;
  const char *email_address;
  const char *smtp_password;
  const char *smtp_server;
  const char *smtp_username;
  GoaTlsType smtp_tls_type;
  g_autofree char *domain = NULL;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  smtp_server = gtk_editable_get_text (GTK_EDITABLE (data->smtp_server));
  smtp_username = gtk_editable_get_text (GTK_EDITABLE (data->smtp_username));
  smtp_password = gtk_editable_get_text (GTK_EDITABLE (data->smtp_password));
  g_object_get (data->smtp_encryption, "selected", &smtp_tls_type, NULL);
  smtp_tls_type += 1;

  g_clear_object (&data->smtp_auth);
  goa_utils_parse_email_address (email_address, NULL, &domain);
  data->smtp_auth = goa_smtp_auth_new (domain, smtp_username, smtp_password);

  mail_client = goa_mail_client_new ();
  goa_mail_client_check (mail_client,
                         smtp_server,
                         smtp_tls_type,
                         data->smtp_accept_ssl_errors,
                         (smtp_tls_type == GOA_TLS_TYPE_SSL) ? 465 : 587,
                         data->smtp_auth,
                         cancellable,
                         (GAsyncReadyCallback) refresh_account_check_smtp_cb,
                         g_object_ref (task));
}

static void
refresh_account_check_imap_cb (GoaMailClient *client,
                               GAsyncResult  *result,
                               gpointer       user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  g_autoptr(GError) error = NULL;

  if (!goa_mail_client_check_finish (client, result, &error))
    {
      data->imap_had_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  /* Skip SMTP and proceed to credential storage */
  if (!data->smtp_use_auth)
    {
      g_debug ("%s(): skipping SMTP authorization", G_STRFUNC);
      refresh_account_store_credentials (task);
      return;
    }

  /* Proceed to SMTP */
  refresh_account_action_smtp (task);
}

static void
refresh_account_action_imap (GTask *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GoaMailClient) mail_client = NULL;
  g_autoptr(GoaMailAuth) imap_auth = NULL;
  const char *imap_password;
  const char *imap_server;
  const char *imap_username;
  GoaTlsType imap_tls_type;

  imap_password = gtk_editable_get_text (GTK_EDITABLE (data->imap_password));
  imap_server = gtk_editable_get_text (GTK_EDITABLE (data->imap_server));
  imap_username = gtk_editable_get_text (GTK_EDITABLE (data->imap_username));
  g_object_get (data->imap_encryption, "selected", &imap_tls_type, NULL);
  imap_tls_type += 1;

  imap_auth = goa_imap_auth_login_new (imap_username, imap_password);

  mail_client = goa_mail_client_new ();
  goa_mail_client_check (mail_client,
                         imap_server,
                         imap_tls_type,
                         data->imap_accept_ssl_errors,
                         (imap_tls_type == GOA_TLS_TYPE_SSL) ? 993 : 143,
                         imap_auth,
                         cancellable,
                         (GAsyncReadyCallback) refresh_account_check_imap_cb,
                         g_object_ref (task));
}

static void
refresh_account_action_cb (GoaProviderDialog *dialog,
                           GParamSpec        *pspec,
                           GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  if (data->imap_had_ssl_errors)
    {
      data->imap_had_ssl_errors = FALSE;
      data->imap_accept_ssl_errors = TRUE;
    }

  if (data->smtp_had_ssl_errors)
    {
      data->smtp_had_ssl_errors = FALSE;
      data->smtp_accept_ssl_errors = TRUE;
    }

  refresh_account_action_imap (task);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account (GoaProvider         *provider,
                 GoaClient           *client,
                 GoaObject           *object,
                 GtkWidget           *parent,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
  AddAccountData *data;
  g_autoptr(GTask) task = NULL;
  gboolean smtp_use_auth;
  g_autofree char *email_address = NULL;
  g_autofree char *imap_server = NULL;
  g_autofree char *imap_username = NULL;
  GoaTlsType imap_tls_type = GOA_TLS_TYPE_SSL;
  g_autofree char *smtp_server = NULL;
  g_autofree char *smtp_username = NULL;
  GoaTlsType smtp_tls_type = GOA_TLS_TYPE_SSL;

  g_assert (GOA_IS_IMAP_SMTP_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WIDGET (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new_full (provider, client, parent, 480, 600);
  data->client = g_object_ref (client);

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, add_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, FALSE);

  email_address = goa_util_lookup_keyfile_string (object, "EmailAddress");
  gtk_editable_set_text (GTK_EDITABLE (data->email_address), email_address);
  gtk_editable_set_editable (GTK_EDITABLE (data->email_address), FALSE);

  data->imap_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "ImapAcceptSslErrors");
  data->smtp_accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "SmtpAcceptSslErrors");

  imap_tls_type = get_tls_type_from_object (object, "ImapUseSsl", "ImapUseTls");
  g_object_set (data->imap_encryption, "selected", imap_tls_type - 1, NULL);

  imap_server = goa_util_lookup_keyfile_string (object, "ImapHost");
  gtk_editable_set_text (GTK_EDITABLE (data->imap_server), imap_server);
  gtk_editable_set_editable (GTK_EDITABLE (data->imap_server), FALSE);

  imap_username = goa_util_lookup_keyfile_string (object, "ImapUserName");
  gtk_editable_set_text (GTK_EDITABLE (data->imap_username), imap_username);
  gtk_editable_set_editable (GTK_EDITABLE (data->imap_username), FALSE);

  smtp_use_auth = goa_util_lookup_keyfile_boolean (object, "SmtpUseAuth");
  if (smtp_use_auth)
    {
      smtp_server = goa_util_lookup_keyfile_string (object, "SmtpHost");
      gtk_editable_set_text (GTK_EDITABLE (data->smtp_server), smtp_server);
      gtk_editable_set_editable (GTK_EDITABLE (data->smtp_server), FALSE);

      smtp_username = goa_util_lookup_keyfile_string (object, "SmtpUserName");
      gtk_editable_set_text (GTK_EDITABLE (data->smtp_username), smtp_username);
      gtk_editable_set_editable (GTK_EDITABLE (data->smtp_username), FALSE);

      smtp_tls_type = get_tls_type_from_object (object, "SmtpUseSsl", "SmtpUseTls");
      g_object_set (data->smtp_encryption, "selected", smtp_tls_type - 1, NULL);
    }

  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (refresh_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkWidget *
create_show_account_ui (GoaProvider *self,
                        GoaObject   *object)
{
  GtkWidget *ret;
  GtkWidget *row;
  char *subtitle = NULL;
  char *username = NULL;

  ret = adw_preferences_group_new ();

  /* Name & E-mail */
  subtitle = goa_util_lookup_keyfile_string (object, "Name");
  row = g_object_new (ADW_TYPE_ACTION_ROW,
                      "title",    _("Name"),
                      "subtitle", subtitle,
                      NULL);
  gtk_widget_add_css_class (row, "property");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (ret), row);
  g_free (subtitle);

  subtitle = goa_util_lookup_keyfile_string (object, "EmailAddress");
  row = g_object_new (ADW_TYPE_ACTION_ROW,
                      "title",    _("Email"),
                      "subtitle", subtitle,
                      NULL);
  gtk_widget_add_css_class (row, "property");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (ret), row);
  g_free (subtitle);

  /* IMAP */
  subtitle = goa_util_lookup_keyfile_string (object, "ImapHost");
  username = goa_util_lookup_keyfile_string (object, "ImapUserName");
  if (g_strcmp0 (g_get_user_name (), subtitle) != 0)
    {
      g_autofree char *domain = g_steal_pointer (&subtitle);
      subtitle = g_strconcat (username, "@", domain, NULL);
    }

  row = g_object_new (ADW_TYPE_ACTION_ROW,
                      "title",    _("IMAP"),
                      "subtitle", subtitle,
                      NULL);
  gtk_widget_add_css_class (row, "property");
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (ret), row);
  g_free (subtitle);
  g_free (username);

  /* SMTP */
  subtitle = goa_util_lookup_keyfile_string (object, "SmtpHost");
  if (subtitle != NULL)
    {
      username = goa_util_lookup_keyfile_string (object, "SmtpUserName");
      if (g_strcmp0 (g_get_user_name (), subtitle) != 0)
        {
          g_autofree char *domain = g_steal_pointer (&subtitle);
          subtitle = g_strconcat (username, "@", domain, NULL);
        }

      row = g_object_new (ADW_TYPE_ACTION_ROW,
                          "title",    _("SMTP"),
                          "subtitle", subtitle,
                          NULL);
      gtk_widget_add_css_class (row, "property");
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (ret), row);
      g_free (username);
      g_free (subtitle);
    }

  return ret;
}

static void
show_account (GoaProvider         *self,
              GoaClient           *client,
              GoaObject           *object,
              GtkWidget           *parent,
              GCancellable        *cancellable,
              GAsyncReadyCallback  callback,
              gpointer             user_data)
{
  GoaProviderDialog *dialog;
  g_autoptr(GTask) task = NULL;
  GtkWidget *content;

  dialog = goa_provider_dialog_new (self, client, parent);
  content = create_show_account_ui (self, object);
  goa_provider_dialog_push_account (dialog, object, content);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, show_account);
  goa_provider_task_run_in_dialog (task, dialog);
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
