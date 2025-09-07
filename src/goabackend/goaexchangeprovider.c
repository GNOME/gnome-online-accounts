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
#include "goaproviderdialog.h"
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
  GKeyFile *goa_conf;
  const gchar *provider_type;
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

  provider_type = goa_provider_get_provider_type (provider);
  goa_conf = goa_util_open_goa_conf ();
  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_MAIL) &&
                 g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
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
  calendar_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_CALENDAR) &&
                     g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  goa_object_skeleton_attach_calendar (object, NULL, calendar_enabled, FALSE);

  /* Contacts */
  contacts_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_CONTACTS) &&
                     g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  goa_object_skeleton_attach_contacts (object, NULL, contacts_enabled, FALSE);

  g_clear_pointer (&goa_conf, g_key_file_free);

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
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  gboolean accept_ssl_errors;

  GtkWidget *email_address;
  GtkWidget *password;
  GtkWidget *username;
  GtkWidget *server;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_username_or_server_changed (GtkEditable    *editable,
                               AddAccountData *data)
{
  GoaDialogState state = GOA_DIALOG_IDLE;
  const char *email = NULL;
  const char *password = NULL;
  const char *server = NULL;
  const char *username = NULL;

  /* Reset the preference to ignore SSL/TLS errors
   */
  data->accept_ssl_errors = FALSE;

  email = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  if (!goa_utils_parse_email_address (email, NULL, NULL))
    goto out;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  server = gtk_editable_get_text (GTK_EDITABLE (data->server));
  if (*password != '\0' && *username != '\0' && *server != '\0')
    state = GOA_DIALOG_READY;

out:
  goa_provider_dialog_set_state (data->dialog, state);
}

static void
on_email_or_password_changed (GtkEditable    *editable,
                              AddAccountData *data)
{
  GoaDialogState state = GOA_DIALOG_IDLE;
  const char *email = NULL;
  const char *password = NULL;
  g_autofree char *email_localpart = NULL;
  g_autofree char *email_domain = NULL;

  /* Reset the preference to ignore SSL/TLS errors
   */
  data->accept_ssl_errors = FALSE;

  email = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  if (!goa_utils_parse_email_address (email, &email_localpart, &email_domain))
    goto out;

  /* These fields are only present for a new account */
  if (data->username != NULL && data->server != NULL)
    {
      const char *username = NULL;
      const char *server = NULL;

      /* If the email changed, update the username/server and defer to its handler */
      if (GTK_WIDGET (editable) == data->email_address)
        {
          gtk_editable_set_text (GTK_EDITABLE (data->username), email_localpart);
          gtk_editable_set_text (GTK_EDITABLE (data->server), email_domain);
          return;
        }

      username = gtk_editable_get_text (GTK_EDITABLE (data->username));
      server = gtk_editable_get_text (GTK_EDITABLE (data->server));
      if (*username == '\0' || *server == '\0')
        goto out;
    }

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  if (password != NULL && *password != '\0')
    state = GOA_DIALOG_READY;

out:
  goa_provider_dialog_set_state (data->dialog, state);
}

static void
create_setup_page (GoaProvider    *provider,
                   AddAccountData *data,
                   gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  goa_provider_dialog_add_page (dialog,
                                NULL, // provider name
                                _("Connect to a Microsoft Exchange provider to access calendars, contacts and files"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->email_address = goa_provider_dialog_add_entry (dialog, group, _("_Email"));
  data->password = goa_provider_dialog_add_password_entry (dialog, group, _("_Password"));
  goa_provider_dialog_add_description (dialog, NULL, _("Exchange account details will be auto-detected from your email address when possible"));

  g_signal_connect (data->email_address,
                    "changed",
                    G_CALLBACK (on_email_or_password_changed),
                    data);
  g_signal_connect (data->password,
                    "changed",
                    G_CALLBACK (on_email_or_password_changed),
                    data);

  group = goa_provider_dialog_add_group (dialog, _("Account Details"));
  data->username = goa_provider_dialog_add_entry (dialog, group, _("User_name"));
  data->server = goa_provider_dialog_add_entry (dialog, group, _("_Domain"));
  goa_provider_dialog_add_description (dialog, data->server, _("Example domain: example.com"));

  g_signal_connect (data->username,
                    "changed",
                    G_CALLBACK (on_username_or_server_changed),
                    data);
  g_signal_connect (data->server,
                    "changed",
                    G_CALLBACK (on_username_or_server_changed),
                    data);

  gtk_widget_grab_focus ((new_account) ? data->email_address : data->password);
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
add_account_autodiscover_cb (GoaEwsClient *client,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  const gchar *email_address;
  const gchar *server;
  const gchar *password;
  const gchar *username;
  g_autoptr(GError) error = NULL;

  if (!goa_ews_client_autodiscover_finish (client, res, &error))
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  /* Account is confirmed */
  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  server = gtk_editable_get_text (GTK_EDITABLE (data->server));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "Host", server);
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (data->accept_ssl_errors) ? "true" : "false");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                username,
                                email_address,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_steal_pointer (&task));
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GoaEwsClient) ews_client = NULL;
  g_autoptr(GError) error = NULL;
  const char *email_address;
  const char *server;
  const char *password;
  const char *username;
  const char *provider_type;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  email_address = gtk_editable_get_text (GTK_EDITABLE (data->email_address));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  server = gtk_editable_get_text (GTK_EDITABLE (data->server));

  /* If this is duplicate account we're finished */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (data->client,
                                  username,
                                  email_address,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Confirm the account */
  ews_client = goa_ews_client_new ();
  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               data->accept_ssl_errors,
                               cancellable,
                               (GAsyncReadyCallback)add_account_autodiscover_cb,
                               g_object_ref (task));
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

  create_setup_page (provider, data, TRUE);
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
refresh_account_autodiscover_cb (GoaEwsClient *client,
                                 GAsyncResult *res,
                                 gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  const gchar *password;
  g_autoptr(GError) error = NULL;

  if (!goa_ews_client_autodiscover_finish (client, res, &error))
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  /* Account is confirmed */
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  // TODO: run in worker thread
  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    data->object,
                                                    g_variant_builder_end (&credentials),
                                                    g_task_get_cancellable (task),
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
  GoaAccount *account;
  GoaExchange *exchange;
  g_autoptr(GoaEwsClient) ews_client = NULL;
  const char *email_address;
  const char *server;
  const char *password;
  const char *username;
  gboolean accept_ssl_errors;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  account = goa_object_peek_account (data->object);
  exchange = goa_object_peek_exchange (data->object);

  email_address = goa_account_get_presentation_identity (account);
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  username = goa_account_get_identity (account);
  server = goa_exchange_get_host (exchange);
  accept_ssl_errors = goa_exchange_get_accept_ssl_errors (exchange);

  gtk_editable_set_editable (GTK_EDITABLE (data->email_address), FALSE);
  gtk_editable_set_text (GTK_EDITABLE (data->email_address), email_address);

  /* Confirm the account */
  ews_client = goa_ews_client_new ();
  goa_ews_client_autodiscover (ews_client,
                               email_address,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               cancellable,
                               (GAsyncReadyCallback) refresh_account_autodiscover_cb,
                               g_steal_pointer (&task));
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

  g_assert (GOA_IS_EXCHANGE_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WIDGET (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new (provider, client, parent);
  data->client = g_object_ref (client);
  data->object = g_object_ref (object);

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, refresh_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_setup_page (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (refresh_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
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
