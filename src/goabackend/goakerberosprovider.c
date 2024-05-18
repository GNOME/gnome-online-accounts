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

#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goakerberosprovider.h"
#include "goakerberosprovider-priv.h"
#include "goautils.h"
#include "goaidentity.h"
#include "goaidentitymanagererror.h"

#include <gcr/gcr.h>

#include "org.gnome.Identity.h"


static GoaIdentityServiceManager *identity_manager;
static GMutex identity_manager_mutex;
static GCond identity_manager_condition;

static GDBusObjectManager *object_manager;
static GMutex object_manager_mutex;
static GCond object_manager_condition;

static void ensure_identity_manager (void);
static void ensure_object_manager (void);

static GoaIdentityServiceIdentity *get_identity_from_object_manager (GoaKerberosProvider *self,
                                                                     const char          *identifier);
static gboolean dbus_proxy_reload_properties_sync (GDBusProxy    *proxy,
                                                   GCancellable  *cancellable);

static void goa_kerberos_provider_module_init (void);
static void create_object_manager (void);
static void create_identity_manager (void);

G_DEFINE_TYPE_WITH_CODE (GoaKerberosProvider, goa_kerberos_provider, GOA_TYPE_PROVIDER,
                         goa_kerberos_provider_module_init ();
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_KERBEROS_NAME,
                                                         0));

static void
goa_kerberos_provider_module_init (void)
{
  create_object_manager ();
  create_identity_manager ();
  g_debug ("activated kerberos provider");
}

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_KERBEROS_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Kerberos"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_TICKETING;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_TICKETING;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("dialog-password-symbolic");
}

static void
notify_is_temporary_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GoaAccount *account;
  gboolean is_temporary;

  account = GOA_ACCOUNT (object);
  is_temporary = goa_account_get_is_temporary (account);

  /* Toggle IsTemporary */
  goa_utils_keyfile_set_boolean (account, "IsTemporary", is_temporary);

  /* Set/unset SessionId */
  if (is_temporary)
    {
      GDBusConnection *connection;
      const gchar *guid;

      connection = G_DBUS_CONNECTION (user_data);
      guid = g_dbus_connection_get_guid (connection);
      goa_utils_keyfile_set_string (account, "SessionId", guid);
    }
  else
    goa_utils_keyfile_remove_key (account, "SessionId");
}

static gboolean
on_handle_get_ticket (GoaTicketing          *interface,
                      GDBusMethodInvocation *invocation)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  gboolean got_ticket;
  const gchar *id;
  const gchar *method_name;
  const gchar *provider_type;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);

  id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  g_debug ("Handling %s for account (%s, %s)", method_name, provider_type, id);

  provider = goa_provider_get_for_provider_type (provider_type);
  error = NULL;
  got_ticket = goa_kerberos_provider_get_ticket_sync (GOA_KERBEROS_PROVIDER (provider),
                                                      object,
                                                      TRUE /* Allow interaction */,
                                                      NULL,
                                                      &error);

  if (!got_ticket)
    g_dbus_method_invocation_take_error (invocation, error);
  else
    goa_ticketing_complete_get_ticket (interface, invocation);

  g_object_unref (provider);
  return TRUE;
}

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount   *account;
  GoaTicketing *ticketing = NULL;
  gboolean      ticketing_enabled;
  gboolean      ret = FALSE;

  if (!GOA_PROVIDER_CLASS (goa_kerberos_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  ticketing = goa_object_get_ticketing (GOA_OBJECT (object));
  ticketing_enabled = g_key_file_get_boolean (key_file, group, "TicketingEnabled", NULL);

  if (ticketing_enabled)
    {
      if (ticketing == NULL)
        {
          char            *preauthentication_source;
          GVariantBuilder  details;

          ticketing = goa_ticketing_skeleton_new ();

          g_signal_connect (ticketing,
                            "handle-get-ticket",
                            G_CALLBACK (on_handle_get_ticket),
                            NULL);

          goa_object_skeleton_set_ticketing (object, ticketing);

          g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));

	  preauthentication_source = g_key_file_get_string (key_file, group, "PreauthenticationSource", NULL);
          if (preauthentication_source)
            g_variant_builder_add (&details, "{ss}", "preauthentication-source", preauthentication_source);

	  g_object_set (G_OBJECT (ticketing), "details", g_variant_builder_end (&details), NULL);
        }
    }
  else if (ticketing != NULL)
    {
      goa_object_skeleton_set_ticketing (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_ticketing_disabled (account, !ticketing_enabled);

      g_signal_connect (account,
                        "notify::is-temporary",
                        G_CALLBACK (notify_is_temporary_cb),
                        connection);

      g_signal_connect (account,
                        "notify::ticketing-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "TicketingEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&ticketing);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account_ticket_cb (GoaKerberosProvider *self,
                           GAsyncResult        *result,
                           gpointer             user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!goa_kerberos_provider_get_ticket_finish (self, result, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

static void
refresh_account (GoaProvider         *provider,
                 GoaClient           *client,
                 GoaObject           *object,
                 GtkWindow           *parent,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (provider);
  g_autoptr(GTask) task = NULL;

  g_assert (GOA_IS_KERBEROS_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WINDOW (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_source_tag (task, refresh_account);

  goa_kerberos_provider_get_ticket (self,
                                    object,
                                    TRUE, /* Allow interaction */
                                    cancellable,
                                    (GAsyncReadyCallback) refresh_account_ticket_cb,
                                    g_steal_pointer (&task));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GcrSecretExchange *secret_exchange;
  GoaObject *object;
  gboolean remember_password;
  gchar *principal;
} InitialSignInData;

static InitialSignInData *
initial_sign_in_data_new (GoaObject *object, const gchar *principal)
{
  InitialSignInData *data;

  data = g_slice_new0 (InitialSignInData);

  if (object != NULL)
    data->object = g_object_ref (object);

  data->principal = g_strdup (principal);

  return data;
}

static void
initial_sign_in_data_free (InitialSignInData *data)
{
  g_clear_object (&data->secret_exchange);
  g_clear_object (&data->object);
  g_free (data->principal);

  g_slice_free (InitialSignInData, data);
}

static void
on_initial_sign_in_done (GoaKerberosProvider *self,
                         GAsyncResult        *result,
                         GTask               *task)
{
  GError     *error;
  InitialSignInData *data;
  char       *object_path;

  data = (InitialSignInData *) g_task_get_task_data (task);

  error = NULL;
  object_path = goa_kerberos_provider_sign_in_finish (self, result, &error);
  if (error != NULL)
    {
      g_task_return_error (task, error);
      goto out;
    }

  if (data->remember_password)
    {
      GVariantBuilder  builder;

      if (object_path != NULL && data->object != NULL)
        {
          const char *password;

          password = gcr_secret_exchange_get_secret (data->secret_exchange, NULL);

          /* FIXME: we go to great lengths to keep the password in non-pageable memory,
           * and then just duplicate it into a gvariant here
           */
          g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
          g_variant_builder_add (&builder,
                                 "{sv}",
                                 "password",
                                 g_variant_new_string (password));

          error = NULL;
          goa_utils_store_credentials_for_object_sync (GOA_PROVIDER (self),
                                                       data->object,
                                                       g_variant_builder_end (&builder),
                                                       NULL,
                                                       NULL);
        }
    }

  g_task_return_boolean (task, TRUE);

 out:
  g_free (object_path);
  g_object_unref (task);
}

static void
on_system_prompt_answered_for_initial_sign_in (GcrPrompt          *prompt,
                                               GAsyncResult       *result,
                                               GTask              *task)
{
  GoaKerberosProvider *self;
  GCancellable        *cancellable;
  GError              *error;
  InitialSignInData   *data;
  const char          *password;
  const char          *preauth_source;
  GcrSecretExchange   *secret_exchange;

  self = GOA_KERBEROS_PROVIDER (g_task_get_source_object (task));
  cancellable = g_task_get_cancellable (task);
  data = (InitialSignInData *) g_task_get_task_data (task);

  /* We currently don't prompt the user to choose a preauthentication source during initial sign in
   * so we assume there's no preauthentication source
   */
  preauth_source = NULL;

  error = NULL;
  password = gcr_prompt_password_finish (prompt, result, &error);

  if (password == NULL)
    {
      gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, NULL);

      if (error != NULL)
        {
          g_task_return_error (task, error);
        }
      else
        {
          g_task_return_new_error (task,
                                   G_IO_ERROR,
                                   G_IO_ERROR_CANCELLED,
                                   _("Operation was cancelled"));
        }

      g_object_unref (task);
      return;
    }

  secret_exchange = gcr_system_prompt_get_secret_exchange (GCR_SYSTEM_PROMPT (prompt));
  data->secret_exchange = g_object_ref (secret_exchange);

  data->remember_password = gcr_prompt_get_choice_chosen (prompt);

  gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, NULL);

  goa_kerberos_provider_sign_in (self,
                                 data->principal,
                                 password,
                                 preauth_source,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_initial_sign_in_done,
                                 task);
}

static void
on_system_prompt_open_for_initial_sign_in (GcrSystemPrompt     *system_prompt,
                                           GAsyncResult        *result,
                                           GTask               *task)
{
  GCancellable *cancellable;
  GcrPrompt    *prompt = NULL;
  GError       *error;

  cancellable = g_task_get_cancellable (task);
  error = NULL;
  prompt = gcr_system_prompt_open_finish (result, &error);

  if (prompt == NULL)
    {
      g_task_return_error (task, error);
      g_object_unref (task);
      goto out;
    }

  gcr_prompt_set_title (prompt, _("Log In to Realm"));
  gcr_prompt_set_description (prompt, _("Please enter your password below."));
  gcr_prompt_set_choice_label (prompt, _("Remember this password"));

  gcr_prompt_password_async (prompt,
                             cancellable,
                             (GAsyncReadyCallback)
                             on_system_prompt_answered_for_initial_sign_in,
                             task);

 out:
  g_clear_object (&prompt);
}

static void
perform_initial_sign_in (GoaKerberosProvider *self,
                         GoaObject           *object,
                         const char          *principal,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{

  GTask *task;
  InitialSignInData *data;

  data = initial_sign_in_data_new (object, principal);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, data, (GDestroyNotify) initial_sign_in_data_free);

  gcr_system_prompt_open_async (-1,
                                cancellable,
                                (GAsyncReadyCallback)
                                on_system_prompt_open_for_initial_sign_in,
                                task);
}

static gboolean
perform_initial_sign_in_finish (GoaKerberosProvider  *self,
                                GAsyncResult         *result,
                                GError              **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (self), FALSE);

  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  task = G_TASK (result);

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;

  GtkWidget *principal;
  GtkWidget *password;
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

static gchar *
normalize_principal (const gchar *principal, gchar **out_realm)
{
  gchar *domain = NULL;
  gchar *realm = NULL;
  gchar *ret = NULL;
  gchar *username = NULL;

  if (!goa_utils_parse_email_address (principal, &username, &domain))
    goto out;

  realm = g_utf8_strup (domain, -1);
  ret = g_strconcat (username, "@", realm, NULL);

  if (out_realm != NULL)
    {
      *out_realm = realm;
      realm = NULL;
    }

 out:
  g_free (domain);
  g_free (realm);
  g_free (username);
  return ret;
}

static void
on_principal_changed (GtkEditable    *editable,
                      AddAccountData *data)
{
  const char *principal;

  principal = gtk_editable_get_text (GTK_EDITABLE (data->principal));
  if (goa_utils_parse_email_address (principal, NULL, NULL))
    goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_READY);
  else
    goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
}

static void
create_account_details_ui (GoaProvider    *self,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;
  GtkWidget *button;

  goa_provider_dialog_add_page (dialog,
                                NULL,
                                _("Access restricted web and network resources for your organization"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->principal = goa_provider_dialog_add_entry (dialog, group, _("_Principal"));
  goa_provider_dialog_add_description (dialog, data->principal, _("Example principal: user@EXAMPLE.COM"));

  button = gtk_window_get_default_widget (GTK_WINDOW (dialog));
  gtk_button_set_label (GTK_BUTTON (button), _("_Sign in…"));

  gtk_widget_grab_focus (data->principal);
  g_signal_connect (data->principal, "changed", G_CALLBACK (on_principal_changed), data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_remove_cb (GoaAccount   *account,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  g_autoptr(GError) error = NULL;

  /* Failure to remove the temporary account is fatal */
  if (!goa_account_call_remove_finish (account, result, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }
}

static void
add_account_signin_cb (GoaKerberosProvider *self,
                       GAsyncResult        *result,
                       gpointer             user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GoaAccount *account = NULL;
  g_autoptr(GError) error = NULL;

  account = goa_object_peek_account (data->object);

  /* If sign in fails, remove the temporary account before restarting */
  if (!perform_initial_sign_in_finish (self, result, &error))
    {
      goa_provider_dialog_report_error (data->dialog, error);
      goa_account_call_remove (account,
                               NULL, /* Cancellable */
                               (GAsyncReadyCallback) add_account_remove_cb,
                               g_steal_pointer (&task));
      return;
    }

  goa_account_set_is_temporary (account, FALSE);
  goa_provider_task_return_account (task, g_object_ref (data->object));
}

static void
add_account_temporary_cb (GoaManager   *manager,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autofree char *object_path = NULL;
  g_autoptr(GDBusObject) object = NULL;
  const char *principal = NULL;
  g_autoptr(GError) error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  object = g_dbus_object_manager_get_object (goa_client_get_object_manager (data->client),
                                             object_path);
  g_set_object (&data->object, GOA_OBJECT (object));
  principal = gtk_editable_get_text (GTK_EDITABLE (data->principal));

  /* Sign in the temporary account */
  perform_initial_sign_in (GOA_KERBEROS_PROVIDER (provider),
                           GOA_OBJECT (object),
                           principal,
                           cancellable,
                           (GAsyncReadyCallback) add_account_signin_cb,
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
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *principal_text;
  const char *provider_type;
  g_autofree char *principal = NULL;
  g_autofree char *realm = NULL;
  g_autoptr(GError) error = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  g_signal_handlers_block_by_func (data->principal, on_principal_changed, data);
  principal_text = gtk_editable_get_text (GTK_EDITABLE (data->principal));
  principal = normalize_principal (principal_text, &realm);
  gtk_editable_set_text (GTK_EDITABLE (data->principal), principal);
  g_signal_handlers_unblock_by_func (data->principal, on_principal_changed, data);

  /* If this is duplicate account we're finished */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (data->client,
                                  principal,
                                  principal,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  if (!goa_utils_check_duplicate (data->client,
                                  principal,
                                  principal,
                                  GOA_FEDORA_NAME,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Create a temporary account */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Realm", realm);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "true");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                provider_type,
                                principal,
                                principal,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_temporary_cb,
                                g_object_ref (task));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account (GoaProvider         *provider,
             GoaClient           *client,
             GtkWindow           *parent,
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

  create_account_details_ui (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (add_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

static gboolean
dbus_proxy_reload_properties_sync (GDBusProxy    *proxy,
                                   GCancellable  *cancellable)
{
  GVariant *result = NULL;
  char *name;
  char *name_owner = NULL;
  GVariant *value;
  GVariantIter *iter = NULL;
  gboolean ret = FALSE;

  name_owner = g_dbus_proxy_get_name_owner (proxy);
  result = g_dbus_connection_call_sync (g_dbus_proxy_get_connection (proxy),
                                        name_owner,
                                        g_dbus_proxy_get_object_path (proxy),
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll",
                                        g_variant_new ("(s)", g_dbus_proxy_get_interface_name (proxy)),
                                        G_VARIANT_TYPE ("(a{sv})"),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        -1,
                                        cancellable,
                                        NULL);
  if (result == NULL)
    goto out;

  g_variant_get (result, "(a{sv})", &iter);
  while (g_variant_iter_next (iter, "{sv}", &name, &value))
    {
      g_dbus_proxy_set_cached_property (proxy, name, value);

      g_free (name);
      g_variant_unref (value);
    }

  ret = TRUE;

 out:
  g_clear_pointer (&iter, g_variant_iter_free);
  g_clear_pointer (&result, g_variant_unref);
  g_free (name_owner);
  return ret;
}

static gboolean
ensure_credentials_sync (GoaProvider    *provider,
                         GoaObject      *object,
                         gint           *out_expires_in,
                         GCancellable   *cancellable,
                         GError        **error)
{
  GoaIdentityServiceIdentity *identity = NULL;
  GoaAccount                 *account = NULL;
  const char                 *identifier;
  gint64                      timestamp;
  GDateTime                  *now, *expiration_time;
  GTimeSpan                   time_span;
  gboolean                    credentials_ensured = FALSE;

  account = goa_object_get_account (object);
  identifier = goa_account_get_identity (account);

  ensure_identity_manager ();

  g_mutex_lock (&identity_manager_mutex);
  identity = get_identity_from_object_manager (GOA_KERBEROS_PROVIDER (provider),
                                               identifier);

  if (identity != NULL)
    {
      if (!dbus_proxy_reload_properties_sync (G_DBUS_PROXY (identity), cancellable))
        g_clear_object (&identity);
    }

  if (identity == NULL || !goa_identity_service_identity_get_is_signed_in (identity))
    {
      GError *lookup_error;
      gboolean ticket_synced;

      lookup_error = NULL;

      g_mutex_unlock (&identity_manager_mutex);
      ticket_synced = goa_kerberos_provider_get_ticket_sync (GOA_KERBEROS_PROVIDER (provider),
                                                             object,
                                                             FALSE /* Don't allow interaction */,
                                                             cancellable,
                                                             &lookup_error);
      g_mutex_lock (&identity_manager_mutex);

      if (!ticket_synced)
        {
          g_dbus_error_strip_remote_error (lookup_error);
          g_set_error_literal (error,
                               GOA_ERROR,
                               GOA_ERROR_NOT_AUTHORIZED,
                               lookup_error->message);
          g_error_free (lookup_error);
          goto out;
        }

      if (identity == NULL)
        identity = get_identity_from_object_manager (GOA_KERBEROS_PROVIDER (provider),
                                                     identifier);
    }

  if (identity == NULL)
    goto out;

  dbus_proxy_reload_properties_sync (G_DBUS_PROXY (identity), cancellable);

  now = g_date_time_new_now_local ();
  timestamp = goa_identity_service_identity_get_expiration_timestamp (identity);

  expiration_time = g_date_time_new_from_unix_local (timestamp);
  time_span = g_date_time_difference (expiration_time, now);

  time_span /= G_TIME_SPAN_SECOND;

  if (time_span < 0 || time_span > G_MAXINT)
    time_span = 0;

  if (out_expires_in != NULL)
    *out_expires_in = (int) time_span;

  credentials_ensured = TRUE;

  g_date_time_unref (now);
  g_date_time_unref (expiration_time);

 out:
  g_clear_object (&account);
  g_clear_object (&identity);
  g_mutex_unlock (&identity_manager_mutex);
  return credentials_ensured;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_account_in_thread_func (GTask         *task,
                               gpointer       source_object,
                               gpointer       task_data,
                               GCancellable  *cancellable)
{
  GError *error;
  GoaAccount *account = NULL;
  GoaObject *object = GOA_OBJECT (task_data);
  const gchar *identifier;

  ensure_identity_manager ();

  account = goa_object_get_account (object);
  identifier = goa_account_get_identity (account);

  g_debug ("Kerberos account %s removed and should now be signed out", identifier);

  error = NULL;
  if (!goa_identity_service_manager_call_sign_out_sync (identity_manager, identifier, cancellable, &error))
    {
      g_task_return_error (task, error);
      goto out;
    }

  g_task_return_boolean (task, TRUE);

 out:
  g_clear_object (&account);
}

static void
remove_account (GoaProvider          *provider,
                GoaObject            *object,
                GCancellable         *cancellable,
                GAsyncReadyCallback   callback,
                gpointer              user_data)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (provider);
  GTask *task;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, remove_account);

  g_task_set_task_data (task, g_object_ref (object), g_object_unref);
  g_task_run_in_thread (task, remove_account_in_thread_func);

  g_object_unref (task);
}

static gboolean
remove_account_finish (GoaProvider   *provider,
                       GAsyncResult  *res,
                       GError       **error)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (provider);
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == remove_account, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaIdentityServiceIdentity *
get_identity_from_object_manager (GoaKerberosProvider *self,
                                  const char          *identifier)
{
  GoaIdentityServiceIdentity *identity = NULL;
  GList                      *objects, *node;

  ensure_object_manager ();

  g_mutex_lock (&object_manager_mutex);
  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (object_manager));

  for (node = objects; node != NULL; node = node->next)
    {
      GoaIdentityServiceIdentity *candidate_identity;
      const char                 *candidate_identifier;
      GDBusObject                *object;

      object = node->data;

      candidate_identity = GOA_IDENTITY_SERVICE_IDENTITY (g_dbus_object_get_interface (object, "org.gnome.Identity"));

      if (candidate_identity == NULL)
        continue;

      candidate_identifier = goa_identity_service_identity_get_identifier (candidate_identity);

      if (g_strcmp0 (candidate_identifier, identifier) == 0)
        {
          identity = candidate_identity;
          break;
        }

      g_object_unref (candidate_identity);
    }

  g_list_free_full (objects, (GDestroyNotify) g_object_unref);
  g_mutex_unlock (&object_manager_mutex);

  return identity;
}

static void
on_object_manager_created (gpointer             object,
                           GAsyncResult        *result,
                           gpointer             unused G_GNUC_UNUSED)
{
  GDBusObjectManager *manager;
  GError *error;

  error = NULL;
  manager = goa_identity_service_object_manager_client_new_for_bus_finish (result, &error);
  if (manager == NULL)
    {
      g_warning ("GoaKerberosProvider: Could not connect to identity service: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_mutex_lock (&object_manager_mutex);
  object_manager = manager;
  g_cond_signal (&object_manager_condition);
  g_mutex_unlock (&object_manager_mutex);
}

static void
create_object_manager (void)
{
  goa_identity_service_object_manager_client_new_for_bus (G_BUS_TYPE_SESSION,
                                                          G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                          "org.gnome.Identity",
                                                          "/org/gnome/Identity",
                                                          NULL,
                                                          (GAsyncReadyCallback)
                                                          on_object_manager_created,
                                                          NULL);
}

static void
ensure_object_manager (void)
{
  g_mutex_lock (&object_manager_mutex);
  while (object_manager == NULL)
      g_cond_wait (&object_manager_condition, &object_manager_mutex);
  g_mutex_unlock (&object_manager_mutex);
}

static void
on_identity_manager_created (gpointer             identity,
                             GAsyncResult        *result,
                             gpointer             unused G_GNUC_UNUSED)
{
  GoaIdentityServiceManager *manager;
  GError *error;

  error = NULL;
  manager = goa_identity_service_manager_proxy_new_for_bus_finish (result, &error);
  if (manager == NULL)
    {
      g_warning ("GoaKerberosProvider: Could not connect to identity service manager: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_mutex_lock (&identity_manager_mutex);
  identity_manager = manager;
  g_cond_signal (&identity_manager_condition);
  g_mutex_unlock (&identity_manager_mutex);
}

static void
create_identity_manager (void)
{
  goa_identity_service_manager_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                                  G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                  "org.gnome.Identity",
                                                  "/org/gnome/Identity/Manager",
                                                  NULL,
                                                  (GAsyncReadyCallback)
                                                  on_identity_manager_created,
                                                  NULL);
}

static void
ensure_identity_manager (void)
{
  g_mutex_lock (&identity_manager_mutex);
  while (identity_manager == NULL)
      g_cond_wait (&identity_manager_condition, &identity_manager_mutex);
  g_mutex_unlock (&identity_manager_mutex);
}

static void
goa_kerberos_provider_init (GoaKerberosProvider *provider)
{
}

static void
goa_kerberos_provider_class_init (GoaKerberosProviderClass *kerberos_class)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (kerberos_class);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->build_object               = build_object;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
  provider_class->remove_account             = remove_account;
  provider_class->remove_account_finish      = remove_account_finish;

  /* this will force associating errors in the
   * GOA_IDENTITY_MANAGER_ERROR error domain with
   * org.gnome.Identity.Manager.Error.* errors via
   * g_dbus_error_register_error_domain().
   */
  goa_identity_manager_error_quark ();
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaObject *object;
  gboolean is_interactive;
} GetTicketData;

static void
get_ticket_data_free (gpointer user_data)
{
  GetTicketData *data = (GetTicketData *)user_data;

  g_clear_object (&data->object);
  g_free (data);
}

static void
get_ticket_thread (GTask *task,
                   gpointer source_object,
                   gpointer task_data,
                   GCancellable *cancellable)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (source_object);
  GetTicketData *data = (GetTicketData *)task_data;
  GError *error = NULL;

  if (!goa_kerberos_provider_get_ticket_sync (self,
                                              data->object,
                                              data->is_interactive,
                                              cancellable, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

/*< private >
 * goa_kerberos_provider_get_ticket:
 * @self: a `GoaKerberosProvider`
 * @object: a `GoaObject`
 * @is_interactive: whether the authentication is interactive
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * Returns: %TRUE on success, or %FALSE with @error set
 */
void
goa_kerberos_provider_get_ticket (GoaKerberosProvider *self,
                                  GoaObject           *object,
                                  gboolean             is_interactive,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GetTicketData *data;

  g_return_if_fail (GOA_IS_KERBEROS_PROVIDER (self));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (GetTicketData, 1);
  data->object = g_object_ref (object);
  data->is_interactive = is_interactive;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_provider_get_ticket);
  g_task_set_task_data (task, data, get_ticket_data_free);
  g_task_run_in_thread (task, get_ticket_thread);

}

/*< private >
 * goa_kerberos_provider_get_ticket_finish:
 * @self: a `GoaKerberosProvider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.KerberosProvider.get_ticket].
 *
 * Returns: %TRUE on success, or %FALSE with @error set
 */
gboolean
goa_kerberos_provider_get_ticket_finish (GoaKerberosProvider  *self,
                                         GAsyncResult         *result,
                                         GError              **error)
{
  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

/*< private >
 * goa_kerberos_provider_get_ticket_sync:
 * @self: a `GoaKerberosProvider`
 * @object: a `GoaObject`
 * @is_interactive: whether the authentication is interactive
 * @cancellable: (nullable): a `GCancellable`
 * @error: (nullable): a `GError`
 *
 * Returns: %TRUE on success, or %FALSE with @error set
 */
gboolean
goa_kerberos_provider_get_ticket_sync (GoaKerberosProvider  *self,
                                       GoaObject            *object,
                                       gboolean              is_interactive,
                                       GCancellable         *cancellable,
                                       GError              **error)
{
  g_autoptr(GoaAccount) account = NULL;
  g_autoptr(GoaTicketing) ticketing = NULL;
  g_autoptr(GVariant) credentials = NULL;
  GVariant *details = NULL;
  const char *identifier = NULL;
  const char *password = NULL;
  const char *preauth_source = NULL;
  gboolean has_password = FALSE;
  g_autofree char *object_path = NULL;
  g_autoptr(GError) lookup_error = NULL;
  GError *sign_in_error = NULL;

  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (self), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ticketing = goa_object_get_ticketing (object);
  if (ticketing == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Ticketing is disabled for account"));
      return FALSE;
    }

  account = goa_object_get_account (object);
  identifier = goa_account_get_identity (account);

  details = goa_ticketing_get_details (ticketing);
  g_variant_lookup (details, "preauthentication-source", "&s", &preauth_source);

  credentials = goa_utils_lookup_credentials_sync (GOA_PROVIDER (self),
                                                   object,
                                                   cancellable,
                                                   &lookup_error);

  if (credentials == NULL && !is_interactive)
    {
      if (lookup_error != NULL)
        {
          g_propagate_error (error, lookup_error);
          lookup_error = NULL;
        }
      else
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_NOT_AUTHORIZED,
                       _("Could not find saved credentials for principal “%s” in keyring"),
                       identifier);
        }

      return FALSE;
    }

  if (credentials != NULL)
    has_password = g_variant_lookup (credentials, "password", "&s", &password);

  if (!has_password && !is_interactive)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_AUTHORIZED,
                   _("Did not find password for principal “%s” in credentials"),
                   identifier);
      return FALSE;
    }

  object_path = goa_kerberos_provider_sign_in_sync (self,
                                                    identifier,
                                                    password,
                                                    preauth_source,
                                                    cancellable,
                                                    &sign_in_error);

  if (sign_in_error != NULL)
    {
      g_propagate_error (error, sign_in_error);
      return FALSE;
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  char *identifier;
  char *password;
  char *preauth_source;
} SignInData;

static void
sign_in_data_free (gpointer user_data)
{
  SignInData *data = (SignInData *)user_data;

  g_clear_pointer (&data->identifier, g_free);
  g_clear_pointer (&data->password, g_free);
  g_clear_pointer (&data->preauth_source, g_free);
  g_free (data);
}

static void
sign_in_thread (GTask        *task,
                gpointer      source_object,
                gpointer      task_data,
                GCancellable *cancellable)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (source_object);
  SignInData *data = (SignInData *) task_data;
  char *object_path;
  GError *error = NULL;

  object_path = goa_kerberos_provider_sign_in_sync (self,
                                                    data->identifier,
                                                    data->password,
                                                    data->preauth_source,
                                                    cancellable,
                                                    &error);
  if (object_path == NULL)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, object_path, g_free);
}

/*< private >
 * goa_kerberos_provider_sign_in:
 * @self: a `GoaKerberosProvider`
 * @identity: the identity
 * @password: the password
 * @preauth_source: (nullable): a pre-auth source
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * Sign in a kerberos identity.
 *
 * Returns: %TRUE on success, or %FALSE with @error set
 */
void
goa_kerberos_provider_sign_in (GoaKerberosProvider *self,
                               const char          *identifier,
                               const char          *password,
                               const char          *preauth_source,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  SignInData *data;

  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (identifier != NULL && *identifier != '\0');
  g_return_if_fail (password != NULL && *password != '\0');
  g_return_if_fail (preauth_source == NULL || *preauth_source != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (SignInData, 1);
  data->identifier = g_strdup (identifier);
  data->password = g_strdup (password);
  data->preauth_source = g_strdup (preauth_source);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_provider_sign_in);
  g_task_set_task_data (task, data, sign_in_data_free);
  g_task_run_in_thread (task, sign_in_thread);
}

/*< private >
 * goa_kerberos_provider_sign_in_finish:
 * @self: a `GoaKerberosProvider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.KerberosProvider.sign_in].
 *
 * Returns: an object path, or %NULL with @error set
 */
char *
goa_kerberos_provider_sign_in_finish (GoaKerberosProvider  *self,
                                      GAsyncResult         *result,
                                      GError              **error)
{
  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/*< private >
 * goa_kerberos_provider_sign_in_sync:
 * @self: a `GoaKerberosProvider`
 * @identity: the identity
 * @password: the password
 * @preauth_source: (nullable): a pre-auth source
 * @cancellable: (nullable): a `GCancellable`
 * @error: (nullable): a `GError`
 *
 * Sign in a kerberos identity.
 *
 * Returns: %TRUE on success, or %FALSE with @error set
 */
char *
goa_kerberos_provider_sign_in_sync (GoaKerberosProvider  *self,
                                    const char           *identifier,
                                    const char           *password,
                                    const char           *preauth_source,
                                    GCancellable         *cancellable,
                                    GError              **error)
{
  GcrSecretExchange  *secret_exchange;
  char               *secret_key;
  char               *return_key = NULL;
  char               *concealed_secret;
  char               *identity_object_path = NULL;
  gboolean            keys_exchanged;
  GError             *local_error;
  GVariantBuilder     details;

  secret_exchange = gcr_secret_exchange_new (NULL);

  secret_key = gcr_secret_exchange_begin (secret_exchange);
  ensure_identity_manager ();

  g_mutex_lock (&identity_manager_mutex);
  keys_exchanged = goa_identity_service_manager_call_exchange_secret_keys_sync (identity_manager,
                                                                                identifier,
                                                                                secret_key,
                                                                                &return_key,
                                                                                cancellable,
                                                                                error);
  g_mutex_unlock (&identity_manager_mutex);
  g_free (secret_key);

  if (!keys_exchanged)
    goto out;

  if (!gcr_secret_exchange_receive (secret_exchange, return_key))
    {
      g_set_error (error,
                   GCR_DATA_ERROR,
                   GCR_ERROR_UNRECOGNIZED,
                   _("Identity service returned invalid key"));
      goto out;
    }

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));

  concealed_secret = gcr_secret_exchange_send (secret_exchange, password, -1);
  g_variant_builder_add (&details, "{ss}", "initial-password", concealed_secret);
  g_free (concealed_secret);

  if (preauth_source != NULL)
    {
      g_variant_builder_add (&details, "{ss}", "preauthentication-source", preauth_source);
    }

  g_mutex_lock (&identity_manager_mutex);

  local_error = NULL;
  goa_identity_service_manager_call_sign_in_sync (identity_manager,
                                                  identifier,
                                                  g_variant_builder_end (&details),
                                                  &identity_object_path,
                                                  cancellable,
                                                  &local_error);

  if (local_error != NULL)
    {
      if (g_error_matches (local_error,
                           GOA_IDENTITY_MANAGER_ERROR,
                           GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS))
        {
          g_assert_not_reached ();
        }

      g_propagate_error (error, local_error);
    }

  g_mutex_unlock (&identity_manager_mutex);

 out:
  g_free (return_key);
  g_object_unref (secret_exchange);
  return identity_object_path;
}
