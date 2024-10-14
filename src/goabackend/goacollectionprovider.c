/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#include <string.h>

#include <glib/gi18n-lib.h>

#include "goadavclient.h"
#include "goadavconfig.h"
#include "goamailclient.h"
#include "goamailconfig.h"
#include "goaobjectskeletonutils.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goaserviceconfig.h"
#include "goautils.h"

#include "goacollectionprovider.h"
#include "goacollectionprovider-priv.h"

G_DEFINE_TYPE_WITH_CODE (GoaCollectionProvider, goa_collection_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_COLLECTION_NAME,
							 0))


/* ---------------------------------------------------------------------------------------------------- */

static const char *
get_provider_type (GoaProvider *provider)
{
  return GOA_COLLECTION_NAME;
}

static char *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup ("Collection");
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_MAIL |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_FILES;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-symbolic");
}

/* ---------------------------------------------------------------------------------------------------- */

static char *
uri_encode_identity (const char *uri_string,
                     const char *identity,
                     gboolean    for_vfs)
{
  g_autoptr (GUri) uri = NULL;
  const char *scheme = NULL;

  scheme = g_uri_peek_scheme (uri_string);
  if (scheme == NULL)
    return NULL;

  if (for_vfs)
    {
      if (g_str_equal (scheme, "davs") || g_str_equal (scheme, "https"))
        scheme = "davs";
      else if (g_str_equal (scheme, "dav") || g_str_equal (scheme, "http"))
        scheme = "dav";
      else
        return NULL;
    }

  uri = g_uri_parse (uri_string, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
  if (uri != NULL)
    {
      g_autoptr (GUri) uri_tmp = NULL;
      g_autofree char *encoded_identity = NULL;

      encoded_identity = g_uri_escape_string (identity, NULL, FALSE);
      uri_tmp = g_uri_build_with_user (g_uri_get_flags (uri),
                                       scheme,
                                       encoded_identity,
                                       g_uri_get_password (uri),
                                       g_uri_get_auth_params (uri),
                                       g_uri_get_host (uri),
                                       g_uri_get_port (uri),
                                       g_uri_get_path (uri),
                                       g_uri_get_query (uri),
                                       g_uri_get_fragment (uri));

      if (uri_tmp != NULL)
        return g_uri_to_string (uri_tmp);
    }

  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const char            *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const char          *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  g_autoptr (GoaAccount) account = NULL;
  g_autoptr (GoaPasswordBased) password_based = NULL;
  g_autoptr (GoaMail) mail = NULL;
  g_autofree char *uri_caldav = NULL;
  g_autofree char *uri_carddav = NULL;
  g_autofree char *uri_vfs = NULL;
  const char *identity;
  gboolean accept_ssl_errors;
  gboolean mail_enabled = FALSE;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean files_enabled;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_collection_provider_parent_class)->build_object (provider,
                                                                                object,
                                                                                key_file,
                                                                                group,
                                                                                connection,
                                                                                just_added,
                                                                                error))
    return FALSE;

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
  identity = goa_account_get_identity (account);
  accept_ssl_errors = g_key_file_get_boolean (key_file, group, "AcceptSslErrors", NULL);

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          gboolean imap_accept_ssl_errors;
          gboolean imap_use_ssl;
          gboolean imap_use_tls;
          gboolean smtp_accept_ssl_errors;
          gboolean smtp_auth_login = FALSE;
          gboolean smtp_auth_plain = FALSE;
          gboolean smtp_use_auth;
          gboolean smtp_use_ssl;
          gboolean smtp_use_tls;
          g_autofree char *email_address = NULL;
          g_autofree char *imap_host = NULL;
          g_autofree char *imap_username = NULL;
          g_autofree char *name = NULL;
          g_autofree char *smtp_host = NULL;
          g_autofree char *smtp_username = NULL;

          email_address = g_key_file_get_string (key_file, group, "EmailAddress", NULL);
          name = g_key_file_get_string (key_file, group, "Name", NULL);

          imap_host = g_key_file_get_string (key_file, group, "ImapHost", NULL);
          imap_username = g_key_file_get_string (key_file, group, "ImapUserName", NULL);
          imap_use_ssl = g_key_file_get_boolean (key_file, group, "ImapUseSsl", NULL);
          imap_use_tls = g_key_file_get_boolean (key_file, group, "ImapUseTls", NULL);
          imap_accept_ssl_errors = g_key_file_get_boolean (key_file, group, "ImapAcceptSslErrors", NULL);

          smtp_host = g_key_file_get_string (key_file, group, "SmtpHost", NULL);
          smtp_use_auth = g_key_file_get_boolean (key_file, group, "SmtpUseAuth", NULL);
          if (smtp_use_auth)
            {
              smtp_username = g_key_file_get_string (key_file, group, "SmtpUserName", NULL);
              smtp_auth_login = g_key_file_get_boolean (key_file, group, "SmtpAuthLogin", NULL);
              smtp_auth_plain = g_key_file_get_boolean (key_file, group, "SmtpAuthPlain", NULL);
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

  /* Calendar */
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  uri_caldav = g_key_file_get_string (key_file, group, "CalDavUri", NULL);
  if (uri_caldav != NULL)
    {
      g_autofree char *username = NULL;
      g_autofree char *uri_encoded = NULL;

      username = g_key_file_get_string (key_file, group, "CalDavUsername", NULL);
      uri_encoded = uri_encode_identity (uri_caldav, username ? username : identity, FALSE);
      goa_object_skeleton_attach_calendar (object, uri_encoded, calendar_enabled, accept_ssl_errors);
      g_clear_pointer (&uri_encoded, g_free);
    }

  /* Contacts */
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  uri_carddav = g_key_file_get_string (key_file, group, "CardDavUri", NULL);
  if (uri_carddav != NULL)
    {
      g_autofree char *username = NULL;
      g_autofree char *uri_encoded = NULL;

      username = g_key_file_get_string (key_file, group, "CardDavUsername", NULL);
      uri_encoded = uri_encode_identity (uri_carddav, username ? username : identity, FALSE);
      goa_object_skeleton_attach_contacts (object, uri_encoded, contacts_enabled, accept_ssl_errors);
    }

  /* Files */
  files_enabled = g_key_file_get_boolean (key_file, group, "FilesEnabled", NULL);
  uri_vfs = g_key_file_get_string (key_file, group, "FilesUri", NULL);
  if (uri_vfs != NULL)
    {
      g_autofree char *username = NULL;
      g_autofree char *uri_encoded = NULL;

      username = g_key_file_get_string (key_file, group, "FilesUsername", NULL);
      uri_encoded = uri_encode_identity (uri_vfs, username ? username : identity, FALSE);
      goa_object_skeleton_attach_files (object, uri_encoded, files_enabled, accept_ssl_errors);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);
      goa_account_set_files_disabled (account, !files_enabled);

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
      g_signal_connect (account,
                        "notify::files-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "FilesEnabled");
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static struct
{
  const char *service;
  const char *key;
} keyfile_endpoints[] = {
  { GOA_SERVICE_TYPE_CALDAV, "CalDavUri" },
  { GOA_SERVICE_TYPE_CARDDAV, "CardDavUri" },
  { GOA_SERVICE_TYPE_WEBDAV, "FilesUri" },
};

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  g_autoptr (GoaDavClient) dav_client = NULL;
  g_autofree char *username = NULL;
  g_autofree char *password = NULL;
  gboolean accept_ssl_errors;
  gboolean ret = FALSE;

  if (!goa_utils_get_credentials (provider, object, "password", &username, &password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }

      return ret;
    }

  /* Find a URI to confirm the account */
  dav_client = goa_dav_client_new ();
  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");

  for (size_t i = 0; i < G_N_ELEMENTS (keyfile_endpoints); i++)
    {
      g_autofree char *uri = NULL;

      uri = goa_util_lookup_keyfile_string (object, keyfile_endpoints[i].key);
      if (uri != NULL && *uri != '\0')
        {
          g_autoptr(GoaDavConfig) config = NULL;

          config = goa_dav_config_new (keyfile_endpoints[i].service, uri, username);
          ret = goa_dav_client_check_sync (dav_client,
                                           config,
                                           password,
                                           accept_ssl_errors,
                                           cancellable,
                                           error);
          break;
        }
    }

  if (!ret)
    {
      if (error != NULL && g_error_matches (*error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
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
        }

      return ret;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static GtkWidget *
goa_entry_status_new (void)
{
  GtkWidget *status;
  GtkWidget *spinner;
  GtkWidget *icon;

  status = g_object_new (GTK_TYPE_BOX,
                         "margin-start",   8,
                         "margin-end",     2,
                         "width-request",  24,
                         "height-request", 24,
                         NULL);

  spinner = gtk_spinner_new ();
  gtk_box_append (GTK_BOX (status), spinner);

  icon = gtk_image_new_from_icon_name ("emblem-default-symbolic");
  gtk_widget_add_css_class (icon, "success");
  gtk_box_append (GTK_BOX (status), icon);

  g_object_bind_property (spinner, "visible",
                          icon,    "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  g_object_bind_property (spinner, "visible",
                          spinner, "spinning",
                          G_BINDING_SYNC_CREATE);

  return status;
}

static void
goa_entry_status_update (GtkWidget  *status,
                         const char *state)
{
  GtkWidget *spinner = NULL;

  g_return_if_fail (GTK_IS_WIDGET (status));

  spinner = gtk_widget_get_first_child (status);
  g_return_if_fail (GTK_IS_SPINNER (spinner));

  if (g_strcmp0 (state, "active") == 0)
    {
      gtk_widget_set_visible (status, TRUE);
      gtk_widget_set_visible (spinner, TRUE);
    }
  else if (g_strcmp0 (state, "success") == 0)
    {
      gtk_widget_set_visible (status, TRUE);
      gtk_widget_set_visible (spinner, FALSE);
    }
  else
    {
      gtk_widget_set_visible (status, FALSE);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  gboolean accept_ssl_errors;

  char *check_uri;
  char *presentation_identity;
  gboolean is_template;

  GtkWidget *email_address;
  GtkWidget *password;

  GtkWidget *discovery_spinner;
  GCancellable *discovery;
  size_t pending;

  GPtrArray *mail_services;
  GoaMailConfig *imap_config;
  GoaMailConfig *smtp_config;
  GoaDavConfig *caldav;
  GoaDavConfig *carddav;
  GoaDavConfig *webdav;

  GtkWidget *mail_row;
  GtkWidget *calendar_row;
  GtkWidget *contacts_row;
  GtkWidget *files_row;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_pointer (&data->mail_services, g_ptr_array_unref);
  g_clear_object (&data->caldav);
  g_clear_object (&data->carddav);
  g_clear_object (&data->webdav);
  g_clear_pointer (&data->presentation_identity, g_free);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gpointer
services_find_preferred (GPtrArray  *services,
                         const char *name)
{
  for (unsigned int i = 0; i < services->len; i++)
    {
      GoaServiceConfig *config = g_ptr_array_index (services, i);

      if (g_str_equal (goa_service_config_get_service (config), name))
        return config;
    }

  return NULL;
}

static void feature_row_set_status (GtkWidget  *row,
                                    const char *status);

static void
feature_row_activated (GtkWidget      *row,
                       AddAccountData *data)
{
  GtkWidget *group = NULL;

  /* Keep in sync with GoaTlsType */
  static const char * const encryption_types[] = {\
    N_("None"),                      // GOA_TLS_TYPE_NONE
    N_("STARTTLS after connecting"), // GOA_TLS_TYPE_STARTTLS
    N_("SSL on a dedicated port"),   // GOA_TLS_TYPE_SSL
    NULL,
  };

  goa_provider_dialog_add_page (data->dialog,
                                adw_preferences_row_get_title (ADW_PREFERENCES_ROW (row)),
                                NULL);

  if (row == data->mail_row)
    {
      GtkWidget *username, *password, *server, *encryption;

      group = goa_provider_dialog_add_group (data->dialog, _("IMAP Settings"));
      server = goa_provider_dialog_add_entry (data->dialog, group, _("IMAP Server"));
      username = goa_provider_dialog_add_entry (data->dialog, group, _("Username"));
      password = goa_provider_dialog_add_password_entry (data->dialog, group, _("Password"));
      encryption = goa_provider_dialog_add_combo (data->dialog,
                                                  group,
                                                  _("Encryption"),
                                                  (GStrv)encryption_types);
      g_object_set (encryption, "selected", GOA_TLS_TYPE_SSL, NULL);

      if (data->imap_config != NULL)
        {
          gtk_editable_set_text (GTK_EDITABLE (server), goa_mail_config_get_hostname (data->imap_config));
          gtk_editable_set_text (GTK_EDITABLE (username), goa_mail_config_get_username (data->imap_config));
          gtk_editable_set_text (GTK_EDITABLE (password), "");
        }

      group = goa_provider_dialog_add_group (data->dialog, _("SMTP Settings"));
      server = goa_provider_dialog_add_entry (data->dialog, group, _("SMTP Server"));
      username = goa_provider_dialog_add_entry (data->dialog, group, _("Username"));
      password = goa_provider_dialog_add_password_entry (data->dialog, group, _("Password"));
      encryption = goa_provider_dialog_add_combo (data->dialog,
                                                  group,
                                                  _("Encryption"),
                                                  (GStrv)encryption_types);
      g_object_set (encryption, "selected", GOA_TLS_TYPE_SSL, NULL);

      if (data->mail_services != NULL)
        {
          gtk_editable_set_text (GTK_EDITABLE (server), goa_mail_config_get_hostname (data->smtp_config));
          gtk_editable_set_text (GTK_EDITABLE (username), goa_mail_config_get_username (data->smtp_config));
          gtk_editable_set_text (GTK_EDITABLE (password), "");
        }
    }
  else
    {
      GoaDavConfig *config = NULL;
      GtkWidget *server, *username, *password;
      const char *dav_username;
      const char *dav_server;
      const char *dav_password;

      group = goa_provider_dialog_add_group (data->dialog, _("Server Settings"));
      server = goa_provider_dialog_add_entry (data->dialog, group, _("Server"));
      username = goa_provider_dialog_add_entry (data->dialog, group, _("Username"));
      password = goa_provider_dialog_add_password_entry (data->dialog, group, _("Password"));

      dav_server = "";
      dav_username = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
      dav_password = gtk_editable_get_text (GTK_EDITABLE (data->password));

      if (row == data->calendar_row)
        config = data->caldav;
      else if (row == data->contacts_row)
        config = data->carddav;
      else if (row == data->files_row)
        config = data->webdav;

      dav_server = config ? goa_dav_config_get_uri (config) : "";
      dav_username = config ? goa_dav_config_get_username (config) : "";
      dav_password = gtk_editable_get_text (GTK_EDITABLE (data->password));

      gtk_editable_set_text (GTK_EDITABLE (server), dav_server);
      gtk_editable_set_text (GTK_EDITABLE (username), dav_username);
      gtk_editable_set_text (GTK_EDITABLE (password), dav_password);
    }
}

static GtkWidget *
feature_row_new (GoaProviderFeatures  feature,
                 AddAccountData      *data)
{

  GtkWidget *row = NULL;
  GtkWidget *status = NULL;
  GtkWidget *status_label = NULL;
  GtkWidget *status_icon = NULL;
  GtkWidget *arrow = NULL;

  static struct
  {
    GoaProviderFeatures feature;
    const char *title;
    const char *icon_name;
  } feature_info[] = {
    {
      .feature = GOA_PROVIDER_FEATURE_MAIL,
      .title = N_("Mail"),
      .icon_name = "mail-unread-symbolic",
    },
    {
      .feature = GOA_PROVIDER_FEATURE_CALENDAR,
      .title = N_("Calendar"),
      .icon_name = "x-office-calendar-symbolic",
    },
    {
      .feature = GOA_PROVIDER_FEATURE_CONTACTS,
      .title = N_("Contacts"),
      .icon_name = "x-office-address-book-symbolic",
    },
    {
      .feature = GOA_PROVIDER_FEATURE_FILES,
      .title = N_("Files"),
      .icon_name = "folder-remote-symbolic",
    },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (feature_info); i++)
    {
      if (feature == feature_info[i].feature)
        {
          GtkWidget *image;

          row = g_object_new (ADW_TYPE_ACTION_ROW,
                              "activatable",   TRUE,
                              "title",         feature_info[i].title,
                              "use-underline", TRUE,
                              NULL);
          image = gtk_image_new_from_icon_name (feature_info[i].icon_name);
          adw_action_row_add_prefix (ADW_ACTION_ROW (row), image);
          break;
        }
    }
  g_return_val_if_fail (row != NULL, NULL);

  status = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), status);
  g_object_set_data (G_OBJECT (row), "goa-status", status);

  status_label = gtk_label_new (NULL);
  gtk_box_append (GTK_BOX (status), status_label);
  g_object_set_data (G_OBJECT (row), "goa-status-label", status_label);

  status_icon = gtk_image_new ();
  gtk_box_append (GTK_BOX (status), status_icon);
  g_object_set_data (G_OBJECT (row), "goa-status-icon", status_icon);

  arrow = g_object_new (GTK_TYPE_IMAGE,
                        "css-classes", (const char *[]){ "dim-label", NULL },
                        "icon-name",   "go-next-symbolic",
                        "valign",      GTK_ALIGN_CENTER,
                        NULL);
  adw_action_row_add_suffix (ADW_ACTION_ROW (row), arrow);
  g_signal_connect (row, "activated", G_CALLBACK (feature_row_activated), data);

  feature_row_set_status (row, NULL);
  return row;
}

static void
feature_row_set_status (GtkWidget  *row,
                        const char *status)
{
  GtkWidget *box, *label, *icon;

  g_assert (ADW_IS_ACTION_ROW (row));

  box = g_object_get_data (G_OBJECT (row), "goa-status");
  label = g_object_get_data (G_OBJECT (row), "goa-status-label");
  icon = g_object_get_data (G_OBJECT (row), "goa-status-icon");

  if (g_strcmp0 (status, "auth-accepted") == 0)
    {
      gtk_widget_set_css_classes (box, (const char *[]){ "success", NULL });
      gtk_label_set_label (GTK_LABEL (label), NULL);
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "emblem-default-symbolic");
    }
  else if (g_strcmp0 (status, "auth-required") == 0)
    {
      gtk_widget_set_css_classes (box, (const char *[]){ "warning", NULL });
      gtk_label_set_label (GTK_LABEL (label), _("Authentication Required"));
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-password-symbolic");
    }
  else if (g_strcmp0 (status, "auth-rejected") == 0)
    {
      gtk_widget_set_css_classes (box, (const char *[]){ "warning", NULL });
      gtk_label_set_label (GTK_LABEL (label), _("Authentication Error"));
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "emblem-important-symbolic");
    }
  else
    {
      gtk_widget_set_css_classes (box, (const char *[]){ "dim-label", NULL });
      gtk_label_set_label (GTK_LABEL (label), _("Not Set Up"));
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), NULL);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean collection_check_account_ready (AddAccountData *data);

static const char *
auth_state_to_string (GoaAuthState auth_state)
{
  switch (auth_state)
    {
    case GOA_AUTH_STATE_UNKNOWN:
      return "unknown";
    case GOA_AUTH_STATE_ACCEPTED:
      return "auth-accepted";
    case GOA_AUTH_STATE_REJECTED:
      return "auth-rejected";
    case GOA_AUTH_STATE_REQUIRED:
      return "auth-required";
    case GOA_AUTH_STATE_SSL_ERROR:
      return "auth-rejected"; // TODO
    }

  return NULL;
}

static void
dav_discover_cb (GoaDavClient *client,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  AddAccountData *data = user_data;
  g_autoptr(GPtrArray) services = NULL;
  g_autoptr(GError) error = NULL;
  const char *caldav_status = NULL;
  const char *carddav_status = NULL;
  const char *webdav_status = NULL;

  services = goa_dav_client_discover_finish (client, result, &error);
  if (error != NULL && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        goto out;
      else
        g_warning ("%s(): %s", G_STRFUNC, error->message);
    }

  if (services != NULL)
    {
      g_set_object (&data->caldav, services_find_preferred (services, GOA_SERVICE_TYPE_CALDAV));
      if (data->caldav != NULL)
        {
          GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->caldav);
          GoaAuthState auth_state = goa_service_config_get_auth_state (config);
          caldav_status  = auth_state_to_string (auth_state);
        }

      g_set_object (&data->carddav, services_find_preferred (services, GOA_SERVICE_TYPE_CARDDAV));
      if (data->carddav != NULL)
        {
          GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->carddav);
          GoaAuthState auth_state = goa_service_config_get_auth_state (config);
          carddav_status  = auth_state_to_string (auth_state);
        }

      g_set_object (&data->webdav, services_find_preferred (services, GOA_SERVICE_TYPE_WEBDAV));
      if (data->webdav != NULL)
        {
          GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->webdav);
          GoaAuthState auth_state = goa_service_config_get_auth_state (config);
          webdav_status  = auth_state_to_string (auth_state);
        }
    }

  data->pending -= 1;
  feature_row_set_status (data->calendar_row, caldav_status);
  feature_row_set_status (data->contacts_row, carddav_status);
  feature_row_set_status (data->files_row, webdav_status);

out:
  collection_check_account_ready (data);
}

static void
mail_discover_cb (GoaMailClient *client,
                  GAsyncResult  *result,
                  gpointer       user_data)
{
  AddAccountData *data = user_data;
  g_autoptr(GPtrArray) services = NULL;
  g_autoptr(GError) error = NULL;
  const char *feature_status = "success";

  services = goa_mail_client_discover_finish (client, result, &error);
  if (services != NULL)
    {
      g_clear_pointer (&data->mail_services, g_ptr_array_unref);
      data->mail_services = g_steal_pointer (&services);
      data->imap_config = services_find_preferred (data->mail_services, GOA_SERVICE_TYPE_IMAP);
      data->smtp_config = services_find_preferred (data->mail_services, GOA_SERVICE_TYPE_SMTP);
    }
  else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      goto out;
    }
  else if (g_error_matches (error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
    {
      const char *username = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
      const char *password = gtk_editable_get_text (GTK_EDITABLE (data->password));
      gboolean has_credentials = FALSE;

      has_credentials = (username != NULL && *username != '\0')
          && (password != NULL && *password != '\0');

      feature_status = has_credentials ? "auth-rejected" : "auth-required";
    }
  else
    {
      g_warning ("%s(): %s", G_STRFUNC, error->message);
    }

  data->pending -= 1;
  feature_row_set_status (data->mail_row, feature_status);

out:
  collection_check_account_ready (data);
}

static void
on_email_address_changed (GtkEditable *editable,
                          gpointer     user_data)
{
  AddAccountData *data = user_data;
  const char *email_address = NULL;
  gboolean is_email_address = FALSE;
  g_autofree char *email_localpart = NULL;
  g_autofree char *email_domain = NULL;
  g_autofree char *uri = NULL;

  g_cancellable_cancel (data->discovery);
  g_clear_object (&data->discovery);
  data->pending = 0;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  if (goa_utils_parse_email_address (email_address, &email_localpart, &email_domain))
    {
      is_email_address = TRUE;

      if (uri == NULL)
        uri = g_strdup_printf ("https://%s", email_domain);
    }

  if (is_email_address || uri != NULL)
    data->discovery = g_cancellable_new ();

  if (is_email_address)
    {
      g_autoptr (GoaMailClient) mail_client = NULL;

      mail_client = goa_mail_client_new ();
      goa_mail_client_discover (mail_client,
                                email_address,
                                data->discovery,
                                (GAsyncReadyCallback)mail_discover_cb,
                                data);
      data->pending += 1;
    }

  if (uri != NULL)
    {
      g_autoptr (GoaDavClient) dav_client = NULL;

      dav_client = goa_dav_client_new ();
      goa_dav_client_discover (dav_client,
                               uri,           /* uri */
                               email_address, /* username */
                               NULL,          /* password */
                               FALSE,         /* accept_ssl_errors */
                               data->discovery,
                               (GAsyncReadyCallback)dav_discover_cb,
                               data);
      data->pending += 1;
    }

  if (data->pending > 0)
    goa_entry_status_update (data->discovery_spinner, "active");
}

static void
on_password_changed (GtkEditable *editable,
                     gpointer     user_data)
{
  AddAccountData *data = user_data;
  const char *password = NULL;
  gboolean ready = FALSE;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  if (password != NULL && *password != '\0')
    {
      if (data->imap_config != NULL
          || (data->caldav != NULL || data->carddav != NULL || data->webdav != NULL))
        ready = TRUE;
    }

  goa_provider_dialog_set_state (data->dialog, ready
                                   ? GOA_DIALOG_READY
                                   : GOA_DIALOG_IDLE);
}

static gboolean
collection_check_account_ready (AddAccountData *data)
{
  const char *password = NULL;
  gboolean ready = FALSE;
  gboolean success = FALSE;

  success = data->imap_config != NULL
    || (data->caldav != NULL || data->carddav != NULL || data->webdav != NULL);

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  if (password != NULL && *password != '\0')
    ready = success;

  if (ready)
    {
      goa_provider_dialog_set_state (data->dialog, ready
                                       ? GOA_DIALOG_READY
                                       : GOA_DIALOG_IDLE);
    }

  if (data->pending > 0)
    goa_entry_status_update (data->discovery_spinner, "active");
  else if (success)
    goa_entry_status_update (data->discovery_spinner, "success");
  else
    goa_entry_status_update (data->discovery_spinner, NULL);

  return ready;
}

static void
create_account_details_ui (GoaProvider    *provider,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  goa_provider_dialog_add_page (dialog,
                                _("General Account"),
                                _("Add an account with mail, calendar, contacts and files by entering your email or username and server"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->email_address = goa_provider_dialog_add_entry (dialog, group, _("_Email"));
  data->password = goa_provider_dialog_add_password_entry (dialog, group, _("_Password"));

  data->discovery_spinner = goa_entry_status_new ();
  goa_entry_status_update (data->discovery_spinner, NULL);
  adw_entry_row_add_suffix (ADW_ENTRY_ROW (data->email_address), data->discovery_spinner);

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->mail_row = feature_row_new (GOA_PROVIDER_FEATURE_MAIL, data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->mail_row);
  data->calendar_row = feature_row_new (GOA_PROVIDER_FEATURE_CALENDAR, data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->calendar_row);
  data->contacts_row = feature_row_new (GOA_PROVIDER_FEATURE_CONTACTS, data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->contacts_row);
  data->files_row = feature_row_new (GOA_PROVIDER_FEATURE_FILES, data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->files_row);

  if (data->object != NULL)
    {
      GoaAccount *account = goa_object_peek_account (data->object);
      const char *email_address = goa_account_get_identity (account);

      if (email_address == NULL || email_address[0] == '\0')
        data->is_template = TRUE;

      if (!data->is_template)
        {
          gtk_editable_set_text (GTK_EDITABLE (data->email_address), email_address);
          gtk_editable_set_editable (GTK_EDITABLE (data->email_address), FALSE);
        }
    }

  if (new_account || data->is_template)
    gtk_widget_grab_focus (data->email_address);
  else
    gtk_widget_grab_focus (data->password);

  g_signal_connect (data->email_address, "changed", G_CALLBACK (on_email_address_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_password_changed), data);
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
  g_autoptr(GError) error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  ret = g_dbus_object_manager_get_object (goa_client_get_object_manager (data->client),
                                          object_path);
  goa_provider_task_return_account (task, GOA_OBJECT (ret));
}

static void
add_account_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *password;
  const char *email_address;
  gboolean mail_enabled = FALSE;
  gboolean calendar_enabled = FALSE;
  gboolean contacts_enabled = FALSE;
  gboolean files_enabled = FALSE;
  gboolean smtp_use_auth = TRUE;

  /* Account is confirmed */
  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  g_set_str (&data->presentation_identity, email_address);
  mail_enabled = data->mail_services != NULL;
  calendar_enabled = data->caldav != NULL;
  contacts_enabled = data->carddav != NULL;
  files_enabled = data->webdav != NULL;

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "imap-password", g_variant_new_string (password));
  if (smtp_use_auth)
    g_variant_builder_add (&credentials, "{sv}", "smtp-password", g_variant_new_string (password));
  if (calendar_enabled)
    g_variant_builder_add (&credentials, "{sv}", "caldav-password", g_variant_new_string (password));
  if (contacts_enabled)
    g_variant_builder_add (&credentials, "{sv}", "carddav-password", g_variant_new_string (password));
  if (files_enabled)
    g_variant_builder_add (&credentials, "{sv}", "files-password", g_variant_new_string (password));
  // FIXME: needs changes in GVfs and EDS
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  /* Mail */
  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "MailEnabled", mail_enabled ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "EmailAddress", email_address ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "Name", g_get_real_name ()); // FIXME: name from UI
  g_variant_builder_add (&details, "{ss}", "ImapHost", goa_mail_config_get_hostname (data->imap_config));
  g_variant_builder_add (&details, "{ss}", "ImapUserName", goa_mail_config_get_username (data->imap_config));
  g_variant_builder_add (&details, "{ss}", "ImapUseSsl", (goa_mail_config_get_encryption (data->imap_config) == GOA_TLS_TYPE_SSL) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "ImapUseTls", (goa_mail_config_get_encryption (data->imap_config) == GOA_TLS_TYPE_STARTTLS) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "ImapAcceptSslErrors", "false");
  g_variant_builder_add (&details, "{ss}", "SmtpHost", goa_mail_config_get_hostname (data->smtp_config));
  g_variant_builder_add (&details, "{ss}", "SmtpUseAuth", (smtp_use_auth) ? "true" : "false");
  if (smtp_use_auth)
    {
      g_variant_builder_add (&details, "{ss}", "SmtpUserName", goa_mail_config_get_username (data->smtp_config));
      g_variant_builder_add (&details, "{ss}", "SmtpAuthLogin", "true");
      g_variant_builder_add (&details, "{ss}", "SmtpAuthPlain", "true");
    }
  g_variant_builder_add (&details, "{ss}", "SmtpUseSsl", (goa_mail_config_get_encryption (data->smtp_config) == GOA_TLS_TYPE_SSL) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "SmtpUseTls", (goa_mail_config_get_encryption (data->smtp_config) == GOA_TLS_TYPE_STARTTLS) ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "SmtpAcceptSslErrors", data->accept_ssl_errors ? "true" : "false");

  /* Calendar */
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", calendar_enabled ? "true" : "false");
  if (calendar_enabled)
    {
      g_variant_builder_add (&details, "{ss}", "CalDavUri", goa_dav_config_get_uri (data->caldav));
      g_variant_builder_add (&details, "{ss}", "CalDavUsername", goa_dav_config_get_username (data->caldav));
      g_variant_builder_add (&details, "{ss}", "CalDavAcceptSslErrors", data->accept_ssl_errors ? "true" : "false");
    }

  /* Contacts */
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", contacts_enabled ? "true" : "false");
  if (contacts_enabled)
    {
      g_variant_builder_add (&details, "{ss}", "CardDavUri", goa_dav_config_get_uri (data->carddav));
      g_variant_builder_add (&details, "{ss}", "CardDavUsername", goa_dav_config_get_username (data->carddav));
      g_variant_builder_add (&details, "{ss}", "CardDavAcceptSslErrors", data->accept_ssl_errors ? "true" : "false");
    }

  /* Files */
  g_variant_builder_add (&details, "{ss}", "FilesEnabled", files_enabled ? "true" : "false");
  if (files_enabled)
    {
      g_variant_builder_add (&details, "{ss}", "FilesUri", goa_dav_config_get_uri (data->webdav));
      g_variant_builder_add (&details, "{ss}", "FilesUsername", goa_dav_config_get_username (data->webdav));
      g_variant_builder_add (&details, "{ss}", "FilesAcceptSslErrors", data->accept_ssl_errors ? "true" : "false");
    }

  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (data->accept_ssl_errors) ? "true" : "false");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                email_address,
                                data->presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));
}

static void
add_account_check_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GPtrArray) queued = NULL;

  queued = g_ptr_array_new ();
  if (data->caldav != NULL)
    {
      GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->caldav);

      if (goa_service_config_get_auth_state (config) != GOA_AUTH_STATE_ACCEPTED)
        g_ptr_array_add (queued, config);
    }

  if (data->carddav != NULL)
    {
      GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->carddav);

      if (goa_service_config_get_auth_state (config) != GOA_AUTH_STATE_ACCEPTED)
        g_ptr_array_add (queued, config);
    }

  if (data->webdav != NULL)
    {
      GoaServiceConfig *config = GOA_SERVICE_CONFIG (data->webdav);

      if (goa_service_config_get_auth_state (config) != GOA_AUTH_STATE_ACCEPTED)
        g_ptr_array_add (queued, config);
    }

  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
  // add_account_credentials (task);
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  const char *email_address;
  const char *provider_type;
  g_autoptr(GError) error = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  data->presentation_identity = g_strdup (email_address);

  /* If this is duplicate account we're finished */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (data->client,
                                  email_address,
                                  data->presentation_identity,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  add_account_check_credentials (task);
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
  data->dialog = goa_provider_dialog_new (provider, client, parent);
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
refresh_account_full_cb (GoaManager   *manager,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autofree char *object_path = NULL;
  g_autoptr(GError) error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_steal_pointer (&task));
}

static void
refresh_account_check_cb (GoaDavClient *client,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  const char *email_address;
  const char *password;
  g_autoptr(GError) error = NULL;

  if (!goa_dav_client_check_finish (client, result, &error))
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  /* Account is confirmed */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  if (data->is_template)
    {
      GVariantBuilder details;
      GoaAccount *account;
      const char *id;
      const char *provider_type;

      account = goa_object_peek_account (data->object);
      id = goa_account_get_id (account);

      g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
      g_variant_builder_add (&details, "{ss}", "Id", id);

      provider_type = goa_provider_get_provider_type (provider);
      goa_manager_call_add_account (goa_client_get_manager (data->client),
                                    provider_type,
                                    email_address,
                                    data->presentation_identity,
                                    g_variant_builder_end (&credentials),
                                    g_variant_builder_end (&details),
                                    cancellable,
                                    (GAsyncReadyCallback) refresh_account_full_cb,
                                    g_steal_pointer (&task));
      return;
    }

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
                                       g_steal_pointer (&task));
}

static void
refresh_account_action_cb (GoaProviderDialog *dialog,
                           GParamSpec        *pspec,
                           GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  /* const char *server; */
  const char *password;
  const char *username;
  g_autoptr(GoaDavClient) dav_client = NULL;
  g_autofree char *uri = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  username = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  /* g_free (data->presentation_identity); */
  /* uri = dav_normalize_uri (uri_text, NULL, &server); */
  /* if (strchr (username, '@') != NULL) */
  /*   g_set_str (&data->presentation_identity, username); */
  /* else */
  /*   data->presentation_identity = g_strconcat (username, "@", server, NULL); */

  /* Confirm the account */
  dav_client = goa_dav_client_new ();
  /* goa_dav_client_check (dav_client, */
  /*                       uri, */
  /*                       username, */
  /*                       password, */
  /*                       data->accept_ssl_errors, */
  /*                       cancellable, */
  /*                       (GAsyncReadyCallback) refresh_account_check_cb, */
  /*                       g_object_ref (task)); */
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

  g_assert (GOA_IS_COLLECTION_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WINDOW (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new (provider, client, parent);
  data->client = g_object_ref (client);
  data->object = g_object_ref (object);
  data->accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, refresh_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (refresh_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_collection_provider_init (GoaCollectionProvider *self)
{
}

static void
goa_collection_provider_class_init (GoaCollectionProviderClass *klass)
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
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const char            *id, /* unused */
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  const char *account_id;
  const char *method_name;
  const char *provider_type;
  const char *sender;
  g_autoptr(GoaProvider) provider = NULL;
  g_autofree char *password = NULL;
  GError *error = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  account_id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  sender = g_dbus_method_invocation_get_sender (invocation);

  g_debug ("Handling %s from %s for account (%s, %s)", method_name, sender, provider_type, account_id);

  provider = goa_provider_get_for_provider_type (provider_type);
  if (goa_utils_get_credentials (provider, object, id, NULL, &password, NULL, &error))
    goa_password_based_complete_get_password (interface, invocation, password);
  else
    g_dbus_method_invocation_take_error (invocation, error);

  return TRUE; /* invocation was handled */
}
