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
#include "goaproviderdialog.h"
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
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  GSource *client_source;
  char *identity;

  GtkWidget *username;
  GtkWidget *password;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  if (data->client_source != NULL)
    {
      g_source_destroy (data->client_source);
      g_clear_pointer (&data->client_source, g_source_unref);
    }

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_pointer (&data->identity, g_free);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
parse_principal (const char  *principal,
                 char       **principal_out)
{
  g_autofree char *domain = NULL;
  g_autofree char *realm = NULL;
  g_autofree char *username = NULL;

  if (principal == 0 || *principal == '\0')
    return FALSE;

  if (!goa_utils_parse_email_address (principal, &username, &domain))
    {
      if (strchr (principal, '@') != NULL)
        return FALSE;

      username = g_strdup (principal);
      domain = g_strdup (GOA_FEDORA_REALM);
    }

  realm = g_utf8_strup (domain, -1);
  if (g_strcmp0 (realm, GOA_FEDORA_REALM) != 0)
    return FALSE;

  if (principal_out != NULL)
    *principal_out = g_strconcat (username, "@", realm, NULL);

  return TRUE;
}

static void
on_username_or_password_changed (GtkEditable *editable,
                                 gpointer     user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;
  const char *username;
  const char *password;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  if (password == NULL || *password == '\0')
    {
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
      return;
    }

  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  if (parse_principal (username, NULL))
    goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_READY);
  else
    goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  goa_provider_dialog_add_page (dialog,
                                NULL,
                                _("Access restricted web and network resources for your organization"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->username = goa_provider_dialog_add_entry (dialog, group, _("User_name"));
  data->password = goa_provider_dialog_add_password_entry (dialog, group, _("_Password"));

  gtk_widget_grab_focus (data->username);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_username_or_password_changed), data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
client_account_added_cb (GoaClient *client, GoaObject *object, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  AddAccountData *data = g_task_get_task_data (task);
  GoaAccount *account;
  const gchar *identity;
  const gchar *provider_type;

  account = goa_object_peek_account (object);
  g_return_if_fail (GOA_IS_ACCOUNT (account));

  if (!goa_account_get_is_temporary (account))
    return;

  provider_type = goa_account_get_provider_type (account);
  if (g_strcmp0 (provider_type, GOA_FEDORA_NAME) != 0)
    return;

  identity = goa_account_get_identity (account);
  if (g_strcmp0 (identity, data->identity) != 0)
    return;

  g_return_if_fail (data->object == NULL);
  data->object = g_object_ref (object);
  g_signal_handlers_disconnect_by_func (data->client, client_account_added_cb, task);
}

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

static gboolean
add_account_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GoaAccount *account = NULL;
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *password;
  const char *id;

  /* Still waiting for the object */
  if (data->object == NULL)
    return G_SOURCE_CONTINUE;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  account = goa_object_peek_account (data->object);
  id = goa_account_get_id (account);

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Id", id);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "false");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                data->identity,
                                data->identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                g_task_get_cancellable (task),
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));

  g_signal_handlers_disconnect_by_func (data->client, client_account_added_cb, task);
  g_clear_pointer (&data->client_source, g_source_unref);

  return G_SOURCE_REMOVE;
}

static void
add_account_signin_cb (GoaFedoraProvider *self,
                       GAsyncResult      *result,
                       gpointer           user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  g_autofree char *object_path = NULL;
  g_autoptr(GError) error = NULL;

  object_path = goa_kerberos_provider_sign_in_finish (GOA_KERBEROS_PROVIDER (self),
                                                      result,
                                                      &error);
  if (object_path == NULL)
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  /* Waiting for the temporary account */
  if (data->object == NULL)
    {
      data->client_source = g_idle_source_new ();
      g_task_attach_source (task,
                            data->client_source,
                            G_SOURCE_FUNC (add_account_credentials));
      return;
    }

  /* Proceed to credential storage */
  add_account_credentials (task);
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  const char *username;
  const char *password;
  const char *provider_type;
  g_autoptr(GError) error = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  /* Reset the temporary account watch */
  g_clear_pointer (&data->identity, g_free);
  g_clear_object (&data->object);
  if (data->client_source != NULL)
    {
      g_source_destroy (data->client_source);
      g_source_unref (data->client_source);
      data->client_source = NULL;
    }

  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));
  provider_type = goa_provider_get_provider_type (provider);
  if (!parse_principal (username, &data->identity))
    {
      error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, _("Failed to get principal from user name “%s”"), username);
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* If this is duplicate account we're finished */
  if (!goa_utils_check_duplicate (data->client,
                                  data->identity,
                                  data->identity,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (!goa_utils_check_duplicate (data->client,
                                  data->identity,
                                  data->identity,
                                  GOA_KERBEROS_NAME,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* We currently don't prompt the user to choose a preauthentication
   * source during initial sign in so we assume there's no
   * preauthentication source
   */
  goa_kerberos_provider_sign_in (GOA_KERBEROS_PROVIDER (provider),
                                 data->identity,
                                 password,
                                 NULL, /* preauth_source */
                                 cancellable,
                                 (GAsyncReadyCallback) add_account_signin_cb,
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

  /* We need the ID from the temporary account
   */
  g_signal_connect (client,
                    "account-added",
                    G_CALLBACK (client_account_added_cb),
                    task);

  create_account_details_ui (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (add_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
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
