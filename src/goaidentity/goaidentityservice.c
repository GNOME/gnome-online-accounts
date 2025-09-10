/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2019 Red Hat, Inc.
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
#include "goaidentityservice.h"

#include <glib/gi18n.h>
#include <gmodule.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <gcr/gcr.h>

#include <goa/goa.h>

#include "goaidentityenumtypes.h"
#include "goaidentityerror.h"
#include "goaidentitymanagererror.h"
#include "goaidentityutils.h"

#include "goakerberosidentitymanager.h"

struct _GoaIdentityService
{
  GoaIdentityServiceManagerSkeleton  parent_instance;

  GDBusConnection          *connection;
  GDBusObjectManagerServer *object_manager_server;
  guint                     bus_id;

  GoaIdentityManager       *identity_manager;

  GHashTable               *watched_client_connections;
  GHashTable               *key_holders;
  GHashTable               *pending_temporary_account_results;

  GoaClient                *client;
};

static void identity_service_manager_interface_init (GoaIdentityServiceManagerIface *interface);

static void
sign_in (GoaIdentityService     *self,
         const char             *identifier,
         gconstpointer           initial_password,
         const char             *preauth_source,
         GoaIdentitySignInFlags  flags,
         GCancellable           *cancellable,
         GAsyncReadyCallback     callback,
         gpointer                user_data);

G_DEFINE_TYPE_WITH_CODE (GoaIdentityService,
                         goa_identity_service,
                         GOA_IDENTITY_SERVICE_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (GOA_IDENTITY_SERVICE_TYPE_MANAGER,
                                                identity_service_manager_interface_init));

static char *
get_object_path_for_identity (GoaIdentityService *self,
                              GoaIdentity        *identity)
{
  const char *identifier;
  char       *escaped_identifier;
  char       *object_path;

  identifier = goa_identity_get_identifier (identity);
  escaped_identifier = goa_identity_utils_escape_object_path (identifier,
                                                              strlen (identifier));
  object_path = g_strdup_printf ("/org/gnome/Identity/Identities/%s", escaped_identifier);

  g_free (escaped_identifier);
  return object_path;
}

static char *
export_identity (GoaIdentityService *self,
                 GoaIdentity        *identity)
{
  char *object_path;
  GDBusObjectSkeleton *object;
  GDBusInterfaceSkeleton *interface;

  object_path = get_object_path_for_identity (self, identity);

  object = G_DBUS_OBJECT_SKELETON (goa_identity_service_object_skeleton_new (object_path));
  interface = G_DBUS_INTERFACE_SKELETON (goa_identity_service_identity_skeleton_new ());

  g_object_bind_property (identity, "identifier", interface, "identifier", G_BINDING_SYNC_CREATE);

  g_object_bind_property (identity,
                          "expiration-timestamp",
                          interface,
                          "expiration-timestamp",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (identity, "is-signed-in", interface, "is-signed-in", G_BINDING_SYNC_CREATE);

  g_dbus_object_skeleton_add_interface (object, interface);
  g_object_unref (interface);

  g_dbus_object_manager_server_export (self->object_manager_server, object);
  g_object_unref (object);

  return object_path;
}

static void
unexport_identity (GoaIdentityService *self,
                   GoaIdentity        *identity)
{
  char *object_path;

  object_path = get_object_path_for_identity (self, identity);

  g_dbus_object_manager_server_unexport (self->object_manager_server, object_path);
  g_free (object_path);
}

static void
on_sign_in_done (GoaIdentityService *self,
                 GAsyncResult       *result,
                 GTask              *operation_result)
{
  GoaIdentity      *identity;
  char             *object_path;
  GError           *error;
  gboolean          had_error;

  error = NULL;
  /* Workaround for bgo#764163 */
  had_error = g_task_had_error (G_TASK (result));
  identity = g_task_propagate_pointer (G_TASK (result), &error);
  if (had_error)
    {
      g_task_return_error (operation_result, error);
      g_object_unref (operation_result);
      return;
    }

  object_path = export_identity (self, identity);

  g_task_return_pointer (operation_result, object_path, g_free);

  /* User is signed in, so we're mostly done
   */
  g_object_unref (identity);
  g_object_unref (operation_result);
}

static GoaObject *
find_object_with_principal (GoaIdentityService *self,
                            const char         *principal,
                            gboolean            must_be_enabled)
{
  GList      *objects;
  GList      *node;
  GoaObject  *found_object = NULL;

  objects = goa_client_get_accounts (self->client);
  for (node = objects; node != NULL; node = node->next)
    {
      GoaObject *object = GOA_OBJECT (node->data);
      GoaAccount *account;
      const char *provider_type;
      const char *account_identity;

      account = goa_object_peek_account (object);

      if (account == NULL)
        continue;

      provider_type = goa_account_get_provider_type (account);

      if (g_strcmp0 (provider_type, GOA_FEDORA_NAME) != 0 && g_strcmp0 (provider_type, GOA_KERBEROS_NAME) != 0)
        continue;

      if (must_be_enabled)
        {
          GoaTicketing *ticketing;

          ticketing = goa_object_peek_ticketing (object);

          if (ticketing == NULL)
              continue;
        }

      account_identity = goa_account_get_identity (account);

      if (g_strcmp0 (account_identity, principal) == 0)
        {
          found_object = g_object_ref (object);
          break;
        }
    }
  g_list_free_full (objects, g_object_unref);

  return found_object;
}

static void
on_credentials_ensured (GoaAccount         *account,
                        GAsyncResult       *result,
                        GoaIdentityService *self)
{
  GError     *error;
  const char *account_identity;
  int         expires_in;

  account_identity = goa_account_get_identity (account);

  error = NULL;
  if (!goa_account_call_ensure_credentials_finish (account,
                                                   &expires_in,
                                                   result,
                                                   &error))
    {
      g_debug ("GoaIdentityService: could not ensure credentials for account %s: %s",
               account_identity,
               error->message);
      g_error_free (error);
      return;
    }

  g_debug ("GoaIdentityService: credentials for account %s ensured for %d seconds",
           account_identity,
           expires_in);
}

static gboolean
should_ignore_object (GoaIdentityService *self,
                      GoaObject          *object)
{
  GoaAccount *account;

  account = goa_object_peek_account (object);

  if (goa_account_get_ticketing_disabled (account))
    return TRUE;

  return FALSE;
}

static void
ensure_account_credentials (GoaIdentityService *self,
                            GoaObject          *object)
{

  GoaAccount *account;

  if (should_ignore_object (self, object))
    return;

  account = goa_object_peek_account (object);
  goa_account_call_ensure_credentials (account,
                                       NULL,
                                       (GAsyncReadyCallback)
                                       on_credentials_ensured,
                                       self);
}

static void
on_sign_in_handled (GoaIdentityService    *self,
                    GAsyncResult          *result,
                    GDBusMethodInvocation *invocation)
{
  GError *error = NULL;
  char *object_path;

  object_path = g_task_propagate_pointer (G_TASK (result), &error);
  if (error != NULL)
    g_dbus_method_invocation_take_error (invocation, error);
  else
    goa_identity_service_manager_complete_sign_in (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                   invocation,
                                                   object_path);

  g_free (object_path);
  g_object_unref (invocation);
}

static void
read_sign_in_details (GoaIdentityServiceManager  *manager,
                      GVariant                   *details,
                      GoaIdentitySignInFlags     *flags,
                      char                      **secret_key,
                      char                      **preauth_source)
{
  GVariantIter  iter;
  char          *key;
  char          *value;

  *flags = GOA_IDENTITY_SIGN_IN_FLAGS_NONE;
  g_variant_iter_init (&iter, details);
  while (g_variant_iter_loop (&iter, "{ss}", &key, &value))
    {
      if (g_strcmp0 (key, "initial-password") == 0)
        *secret_key = g_strdup (value);
      else if (g_strcmp0 (key, "preauthentication-source") == 0)
        *preauth_source = g_strdup (value);
      else if (g_strcmp0 (key, "disallow-renewal") == 0
               && g_strcmp0 (value, "true") == 0)
        *flags |= GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_RENEWAL;
      else if (g_strcmp0 (key, "disallow-forwarding") == 0
               && g_strcmp0 (value, "true") == 0)
        *flags |= GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_FORWARDING;
      else if (g_strcmp0 (key, "disallow-proxying") == 0
               && g_strcmp0 (value, "true") == 0)
        *flags |= GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_PROXYING;
    }
}

static gboolean
goa_identity_service_handle_sign_in (GoaIdentityServiceManager *manager,
                                     GDBusMethodInvocation     *invocation,
                                     const char                *identifier,
                                     GVariant                  *details)
{
  GoaIdentityService     *self = GOA_IDENTITY_SERVICE (manager);
  GTask                  *operation_result;
  GoaIdentitySignInFlags  flags;
  char                   *secret_key = NULL;
  char                   *preauth_source = NULL;
  gconstpointer           initial_password = NULL;
  GCancellable           *cancellable;

  read_sign_in_details (manager, details, &flags, &secret_key, &preauth_source);

  if (secret_key != NULL)
    {
      GcrSecretExchange *secret_exchange;
      gchar *key;

      key = g_strdup_printf ("%s %s",
                             g_dbus_method_invocation_get_sender (invocation),
                             identifier);

      secret_exchange = g_hash_table_lookup (self->key_holders, key);
      g_free (key);

      if (secret_exchange == NULL)
        {
          g_free (secret_key);
          g_dbus_method_invocation_return_error (invocation,
                                                 GOA_IDENTITY_MANAGER_ERROR,
                                                 GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS,
                                                 "initial secret passed before secret key exchange");
          return TRUE;
        }

      gcr_secret_exchange_receive (secret_exchange, secret_key);
      g_free (secret_key);

      initial_password = gcr_secret_exchange_get_secret (secret_exchange, NULL);
    }

  cancellable = g_cancellable_new ();
  operation_result = g_task_new (self,
                                 cancellable,
                                 (GAsyncReadyCallback) on_sign_in_handled,
                                 g_object_ref (invocation));
  g_object_set_data (G_OBJECT (operation_result),
                     "initial-password",
                     (gpointer)
                     initial_password);
  g_object_set_data (G_OBJECT (operation_result),
                     "flags",
                     GINT_TO_POINTER ((int) flags));

  sign_in (self,
           identifier,
           initial_password,
           preauth_source,
           flags,
           cancellable,
           (GAsyncReadyCallback)
           on_sign_in_done,
           g_object_ref (operation_result));

  g_free (preauth_source);
  g_object_unref (cancellable);
  g_object_unref (operation_result);

  return TRUE;
}

static void
on_sign_out_handled (GoaIdentityService    *self,
                     GAsyncResult          *result,
                     GDBusMethodInvocation *invocation)
{
  GError *error;
  gboolean had_error;

  error = NULL;
  /* Workaround for bgo#764163 */
  had_error = g_task_had_error (G_TASK (result));
  g_task_propagate_boolean (G_TASK (result), &error);
  if (had_error)
    g_dbus_method_invocation_take_error (invocation, error);
  else
    goa_identity_service_manager_complete_sign_out (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                    invocation);

  g_object_unref (invocation);
}

static void
on_identity_signed_out (GoaIdentityManager *manager,
                        GAsyncResult       *result,
                        GTask              *operation_result)
{
  GoaIdentityService *self;
  GError             *error;
  GoaIdentity        *identity;
  const char         *identifier;
  GoaObject          *object = NULL;

  self = GOA_IDENTITY_SERVICE (g_task_get_source_object (operation_result));
  identity = g_object_get_data (G_OBJECT (operation_result), "identity");

  identifier = goa_identity_get_identifier (identity);
  object = find_object_with_principal (self, identifier, FALSE);

  if (object != NULL)
    ensure_account_credentials (self, object);

  error = NULL;
  goa_identity_manager_sign_identity_out_finish (manager, result, &error);
  if (error != NULL)
    {
      g_debug ("GoaIdentityService: Identity could not be signed out: %s",
               error->message);
      g_task_return_error (operation_result, error);
      goto out;
    }

  g_task_return_boolean (operation_result, TRUE);

 out:
  g_clear_object (&object);
  g_object_unref (operation_result);
}

static void
on_got_identity_for_sign_out (GoaIdentityManager *manager,
                              GAsyncResult       *result,
                              GTask              *operation_result)
{
  GError *error;
  GoaIdentity *identity = NULL;

  error = NULL;
  identity = goa_identity_manager_get_identity_finish (manager, result, &error);

  if (error != NULL)
    {
      g_debug ("GoaIdentityService: Identity could not be signed out: %s",
               error->message);
      g_task_return_error (operation_result, error);
      goto out;
    }

  g_object_set_data_full (G_OBJECT (operation_result), "identity", g_object_ref (identity), g_object_unref);

  goa_identity_manager_sign_identity_out (manager,
                                          identity,
                                          NULL,
                                          (GAsyncReadyCallback)
                                          on_identity_signed_out,
                                          g_object_ref (operation_result));

 out:
  g_clear_object (&identity);
  g_object_unref (operation_result);
}

static gboolean
goa_identity_service_handle_sign_out (GoaIdentityServiceManager *manager,
                                      GDBusMethodInvocation     *invocation,
                                      const char                *identifier)
{
  GoaIdentityService *self = GOA_IDENTITY_SERVICE (manager);
  GTask *task;

  task = g_task_new (self, NULL, (GAsyncReadyCallback) on_sign_out_handled, g_object_ref (invocation));
  goa_identity_manager_get_identity (self->identity_manager,
                                     identifier,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_identity_for_sign_out,
                                     g_object_ref (task));

  g_object_unref (task);
  return TRUE;
}

static void
on_secret_keys_exchanged (GoaIdentityService *self,
                          GAsyncResult       *result)
{
  GDBusMethodInvocation *invocation;
  GError                *error;
  char                  *output_key = NULL;

  invocation = g_task_get_task_data (G_TASK (result));

  error = NULL;
  output_key = g_task_propagate_pointer (G_TASK (result), &error);
  if (error != NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
    }
  else
    {
      goa_identity_service_manager_complete_exchange_secret_keys (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                                  invocation,
                                                                  output_key);
    }

  g_free (output_key);
}

static void
on_caller_watched (GDBusConnection    *connection,
                   const char         *name,
                   const char         *name_owner,
                   GTask              *operation_result)
{
  GoaIdentityService    *self;
  GcrSecretExchange     *secret_exchange;
  const char            *identifier;
  const char            *input_key;
  char                  *output_key;

  self = GOA_IDENTITY_SERVICE (g_task_get_source_object (operation_result));

  identifier = g_object_get_data (G_OBJECT (operation_result), "identifier");
  input_key = g_object_get_data (G_OBJECT (operation_result), "input-key");

  secret_exchange = gcr_secret_exchange_new (NULL);

  if (!gcr_secret_exchange_receive (secret_exchange,
                                    input_key))
    {
      g_task_return_new_error (operation_result,
                               GCR_DATA_ERROR,
                               GCR_ERROR_UNRECOGNIZED,
                               _("Initial secret key is invalid"));
      return;
    }

  g_hash_table_insert (self->key_holders, g_strdup_printf ("%s %s", name_owner, identifier), secret_exchange);

  output_key = gcr_secret_exchange_send (secret_exchange, NULL, 0);
  g_task_return_pointer (operation_result, output_key, g_free);
}

static void
on_caller_vanished (GDBusConnection    *connection,
                    const char         *name,
                    GTask              *operation_result)
{
  GoaIdentityService *self;
  GCancellable       *cancellable;

  self = GOA_IDENTITY_SERVICE (g_task_get_source_object (operation_result));

  cancellable = g_task_get_cancellable (operation_result);
  g_cancellable_cancel (cancellable);

  g_hash_table_remove (self->watched_client_connections, name);
  g_hash_table_remove (self->key_holders, name);

}

static gboolean
goa_identity_service_handle_exchange_secret_keys (GoaIdentityServiceManager *manager,
                                                  GDBusMethodInvocation     *invocation,
                                                  const char                *identifier,
                                                  const char                *input_key)
{
  GoaIdentityService     *self = GOA_IDENTITY_SERVICE (manager);
  GTask                  *operation_result;
  GCancellable           *cancellable;
  guint                   watch_id;
  const char             *sender;

  cancellable = g_cancellable_new ();
  operation_result = g_task_new (self, cancellable, (GAsyncReadyCallback) on_secret_keys_exchanged, NULL);
  g_task_set_task_data (operation_result, g_object_ref (invocation), g_object_unref);

  g_object_set_data_full (G_OBJECT (operation_result),
                          "identifier",
                          g_strdup (identifier),
                          g_free);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "input-key",
                          g_strdup (input_key),
                          g_free);
  sender = g_dbus_method_invocation_get_sender (invocation);
  watch_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                               sender,
                               G_BUS_NAME_WATCHER_FLAGS_NONE,
                               (GBusNameAppearedCallback)
                               on_caller_watched,
                               (GBusNameVanishedCallback)
                               on_caller_vanished,
                               g_object_ref (operation_result),
                               g_object_unref);
  g_hash_table_insert (self->watched_client_connections, g_strdup (sender), GUINT_TO_POINTER (watch_id));

  g_object_unref (operation_result);
  return TRUE;
}

static void
identity_service_manager_interface_init (GoaIdentityServiceManagerIface *interface)
{
  interface->handle_sign_in = goa_identity_service_handle_sign_in;
  interface->handle_sign_out = goa_identity_service_handle_sign_out;
  interface->handle_exchange_secret_keys = goa_identity_service_handle_exchange_secret_keys;
}

static void
goa_identity_service_init (GoaIdentityService *self)
{
  g_debug ("GoaIdentityService: initializing");
  self->watched_client_connections = g_hash_table_new_full (g_str_hash,
                                                            g_str_equal,
                                                            g_free,
                                                            (GDestroyNotify)
                                                            g_bus_unwatch_name);

  self->key_holders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  self->pending_temporary_account_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
goa_identity_service_finalize (GObject *object)
{
  GoaIdentityService *self;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GOA_IS_IDENTITY_SERVICE (object));

  g_debug ("GoaIdentityService: finalizing");

  self = GOA_IDENTITY_SERVICE (object);

  goa_identity_service_deactivate (self);

  g_clear_object (&self->identity_manager);
  g_clear_object (&self->object_manager_server);
  g_clear_pointer (&self->watched_client_connections, g_hash_table_unref);
  g_clear_pointer (&self->key_holders, g_hash_table_unref);
  g_clear_pointer (&self->pending_temporary_account_results, g_hash_table_unref);

  G_OBJECT_CLASS (goa_identity_service_parent_class)->finalize (object);
}

static void
on_identity_renewed (GoaIdentityManager *manager,
                     GAsyncResult       *result,
                     GoaIdentityService *self)
{
  GError *error;

  error = NULL;
  goa_identity_manager_renew_identity_finish (manager, result, &error);

  if (error != NULL)
    {
      g_debug ("GoaIdentityService: could not renew identity: %s",
               error->message);
      g_error_free (error);
      return;
    }

  g_debug ("GoaIdentityService: identity renewed");
}

static void
on_identity_needs_renewal (GoaIdentityManager *identity_manager,
                           GoaIdentity        *identity,
                           GoaIdentityService *self)
{
  const char *principal;
  GoaObject  *object = NULL;

  principal = goa_identity_get_identifier (identity);

  object = find_object_with_principal (self, principal, TRUE);

  if (object != NULL && should_ignore_object (self, object))
    {
      g_debug ("GoaIdentityService: ignoring identity %s that says it needs renewal", principal);
      goto out;
    }

  g_debug ("GoaIdentityService: identity %s needs renewal", principal);

  goa_identity_manager_renew_identity (GOA_IDENTITY_MANAGER (self->identity_manager),
                                       identity,
                                       NULL,
                                       (GAsyncReadyCallback)
                                       on_identity_renewed,
                                       self);

 out:
  g_clear_object (&object);
}

static void
on_identity_signed_in (GoaIdentityManager *manager,
                       GAsyncResult       *result,
                       GTask              *operation_result)
{
  GError *error;
  GoaIdentity *identity;

  error = NULL;
  identity = goa_identity_manager_sign_identity_in_finish (manager, result, &error);

  if (error != NULL)
    {
      g_debug ("GoaIdentityService: could not sign in identity: %s",
               error->message);
      g_task_return_error (operation_result, error);
    }
  else
    {
      g_debug ("GoaIdentityService: identity signed in");
      g_task_return_pointer (operation_result, g_object_ref (identity), g_object_unref);
    }

  g_object_unref (operation_result);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_temporary_account_as_kerberos_add_account (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaIdentityService *self;
  GDBusObjectManager *object_manager;
  GError *error;
  GTask *task = G_TASK (user_data);
  GoaManager *manager = GOA_MANAGER (source_object);
  GoaObject *object = NULL;
  gchar *object_path = NULL;

  self = GOA_IDENTITY_SERVICE (g_task_get_source_object (task));

  error = NULL;
  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      g_task_return_error (task, error);
      goto out;
    }

  g_debug ("Created account for identity with object path %s", object_path);

  object_manager = goa_client_get_object_manager (self->client);
  object = GOA_OBJECT (g_dbus_object_manager_get_object (object_manager, object_path));
  if (object == NULL)
    {
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_FAILED, /* TODO: more specific */
                               _("Couldn't get GoaObject for object path %s"),
                               object_path);
      goto out;
    }

  g_task_return_pointer (task, g_object_ref (object), g_object_unref);

 out:
  g_free (object_path);
  g_clear_object (&object);
  g_object_unref (task);
}

static void
add_temporary_account_as_kerberos (GoaIdentityService *self,
                                   GoaIdentity *identity,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
  GTask *task = NULL;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaManager *manager;
  const char *provider_name;
  const gchar *principal;
  gchar *preauth_source = NULL;
  gchar *realm = NULL;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, add_temporary_account_as_kerberos);
  g_task_set_task_data (task, g_object_ref (identity), g_object_unref);

  principal = goa_identity_get_identifier (identity);

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  realm = goa_kerberos_identity_get_realm_name (GOA_KERBEROS_IDENTITY (identity));
  g_variant_builder_add (&details, "{ss}", "Realm", realm);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "true");

  preauth_source = goa_kerberos_identity_get_preauthentication_source (GOA_KERBEROS_IDENTITY (identity));
  if (preauth_source != NULL)
    g_variant_builder_add (&details, "{ss}", "PreauthenticationSource", preauth_source);

  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  if (g_strcmp0 (realm, GOA_FEDORA_REALM) == 0)
    provider_name = GOA_FEDORA_NAME;
  else
    provider_name = GOA_KERBEROS_NAME;

  manager = goa_client_get_manager (self->client);
  goa_manager_call_add_account (manager,
                                provider_name,
                                principal,
                                principal,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL,
                                add_temporary_account_as_kerberos_add_account,
                                g_object_ref (task));

  g_free (preauth_source);
  g_free (realm);
  g_object_unref (task);
}

static GoaObject *
add_temporary_account_as_kerberos_finish (GoaIdentityService *self, GAsyncResult *res, GError **error)
{
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (res, self), NULL);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == add_temporary_account_as_kerberos, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return g_task_propagate_pointer (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_temporary_account_added_as_kerberos (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  GoaIdentityService *self = GOA_IDENTITY_SERVICE (source_object);
  GoaIdentity *identity = GOA_IDENTITY (user_data);
  const char *principal;

  GoaObject *object;
  GError *error;

  error = NULL;

  principal = goa_identity_get_identifier (identity);
  g_hash_table_remove (self->pending_temporary_account_results, principal);

  object = add_temporary_account_as_kerberos_finish (self, result, &error);
  if (error != NULL)
    {
      const char *identifier;

      identifier = goa_identity_get_identifier (identity);
      g_debug ("Could not add temporary Kerberos account for identity %s: %s", identifier, error->message);
      g_error_free (error);
      goto out;
    }

  ensure_account_credentials (self, object);

 out:
  g_clear_object (&object);
  g_object_unref (identity);
}

static void
add_temporary_account (GoaIdentityService *self,
                       GoaIdentity        *identity)
{
  const char *principal;

  principal = goa_identity_get_identifier (identity);
  if (g_hash_table_contains (self->pending_temporary_account_results, principal))
    {
      g_debug ("GoaIdentityService: would add temporary identity %s, but it's already pending", principal);
      return;
    }

  g_debug ("GoaIdentityService: adding temporary identity %s", principal);

  /* If there's no account for this identity then create a temporary one.
   */

  g_debug ("GoaIdentityService: asking to sign back in");

  g_hash_table_insert (self->pending_temporary_account_results, g_strdup (principal), g_object_ref (identity));

  add_temporary_account_as_kerberos (self,
                                     identity,
                                     NULL,
                                     on_temporary_account_added_as_kerberos,
                                     g_object_ref (identity));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_identity_added (GoaIdentityManager *identity_manager,
                   GoaIdentity        *identity,
                   GoaIdentityService *self)
{
  GoaObject *object;
  const char *identifier;

  export_identity (self, identity);

  identifier = goa_identity_get_identifier (identity);

  object = find_object_with_principal (self, identifier, FALSE);

  if (object == NULL && goa_identity_is_signed_in (identity))
    add_temporary_account (self, identity);

  g_clear_object (&object);
}

static void
on_identity_removed (GoaIdentityManager *identity_manager,
                     GoaIdentity        *identity,
                     GoaIdentityService *self)
{
  GoaObject *object;
  const char *identifier;

  identifier = goa_identity_get_identifier (identity);
  object = find_object_with_principal (self, identifier, FALSE);

  if (object != NULL)
    ensure_account_credentials (self, object);

  unexport_identity (self, identity);
  g_clear_object (&object);
}

static void
on_identity_refreshed (GoaIdentityManager *identity_manager,
                       GoaIdentity        *identity,
                       GoaIdentityService *self)
{
  GoaObject *object;
  const char *identifier;

  identifier = goa_identity_get_identifier (identity);
  object = find_object_with_principal (self, identifier, FALSE);

  if (object == NULL)
    add_temporary_account (self, identity);
  else
    ensure_account_credentials (self, object);

  g_clear_object (&object);
}

typedef struct
{
  GoaIdentityService *service;
  GoaIdentity        *identity;
  GoaIdentityInquiry *inquiry;
  GoaIdentityQuery   *query;
  GcrSystemPrompt    *prompt;
  GCancellable       *cancellable;
} SystemPromptRequest;

static SystemPromptRequest *
system_prompt_request_new (GoaIdentityService *service,
                           GcrSystemPrompt    *prompt,
                           GoaIdentity        *identity,
                           GoaIdentityInquiry *inquiry,
                           GoaIdentityQuery   *query,
                           GCancellable       *cancellable)
{
  SystemPromptRequest *data;

  data = g_slice_new0 (SystemPromptRequest);

  data->service = service;
  data->prompt = prompt;
  data->identity = g_object_ref (identity);
  data->inquiry = g_object_ref (inquiry);
  data->query = query;
  data->cancellable = g_object_ref (cancellable);

  return data;
}

static void
system_prompt_request_free (SystemPromptRequest *data)
{
  g_clear_object (&data->identity);
  g_clear_object (&data->inquiry);
  g_clear_object (&data->cancellable);
  g_slice_free (SystemPromptRequest, data);
}

static void
close_system_prompt (GoaIdentityManager  *manager,
                     GoaIdentity         *identity,
                     SystemPromptRequest *data)
{
  GError *error;

  /* Only close the prompt if the identity we're
   * waiting on got refreshed
   */
  if (data->identity != identity)
    return;

  g_signal_handlers_disconnect_by_func (manager, G_CALLBACK (close_system_prompt), data);

  error = NULL;
  if (!gcr_system_prompt_close (data->prompt, NULL, &error))
    {
      if (error != NULL)
        {
          g_debug ("GoaIdentityService: could not close system prompt: %s",
                   error->message);
          g_error_free (error);
        }
    }
}

static void
on_password_system_prompt_answered (GcrPrompt           *prompt,
                                    GAsyncResult        *result,
                                    SystemPromptRequest *request)
{
  GoaIdentityService *self = request->service;
  GoaIdentityInquiry *inquiry = request->inquiry;
  GoaIdentity *identity = request->identity;
  GoaIdentityQuery *query = request->query;
  GCancellable *cancellable = request->cancellable;
  GError *error;
  const char *password;

  error = NULL;
  password = gcr_prompt_password_finish (prompt, result, &error);

  if (password == NULL)
    {
      if (error != NULL)
        {
          g_debug ("GoaIdentityService: could not get password from user: %s",
                   error->message);
          g_error_free (error);
        }
      else
        {
          g_cancellable_cancel (cancellable);
        }
    }
  else if (!g_cancellable_is_cancelled (cancellable))
    {
      goa_identity_inquiry_answer_query (inquiry, query, password);
    }

  close_system_prompt (self->identity_manager, identity, request);
  system_prompt_request_free (request);
}

static void
query_user (GoaIdentityService *self,
            GoaIdentity        *identity,
            GoaIdentityInquiry *inquiry,
            GoaIdentityQuery   *query,
            GcrPrompt          *prompt,
            GCancellable       *cancellable)
{
  SystemPromptRequest *request;
  char *prompt_text;
  GoaIdentityQueryMode query_mode;
  char *description;
  char *name;

  g_assert (GOA_IS_KERBEROS_IDENTITY (identity));

  gcr_prompt_set_title (prompt, _("Log In to Realm"));

  name = goa_identity_manager_name_identity (self->identity_manager, identity);

  description =
    g_strdup_printf (_("The network realm %s needs some information to sign you in."),
                     name);
  g_free (name);

  gcr_prompt_set_description (prompt, description);
  g_free (description);

  prompt_text = goa_identity_query_get_prompt (inquiry, query);
  gcr_prompt_set_message (prompt, prompt_text);
  g_free (prompt_text);

  request = system_prompt_request_new (self,
                                       GCR_SYSTEM_PROMPT (prompt),
                                       identity,
                                       inquiry,
                                       query,
                                       cancellable);

  g_signal_connect (self->identity_manager, "identity-refreshed", G_CALLBACK (close_system_prompt), request);

  query_mode = goa_identity_query_get_mode (inquiry, query);

  switch (query_mode)
    {
    case GOA_IDENTITY_QUERY_MODE_INVISIBLE:
      gcr_prompt_password_async (prompt,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_password_system_prompt_answered,
                                 request);
      break;
    case GOA_IDENTITY_QUERY_MODE_VISIBLE:
      /* FIXME: we need to either
       * 1) add new GCR api to allow for asking questions with visible answers
       * or
       * 2) add a new shell D-Bus api and drop GCR
       */
      gcr_prompt_password_async (prompt,
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_password_system_prompt_answered,
                                 request);
      break;
    default:
      break;
    }
}

typedef struct
{
  GoaIdentityService *service;
  GoaIdentityInquiry *inquiry;
  GCancellable *cancellable;
} SystemPromptOpenRequest;

static SystemPromptOpenRequest *
system_prompt_open_request_new (GoaIdentityService *service,
                                GoaIdentityInquiry *inquiry,
                                GCancellable       *cancellable)
{
  SystemPromptOpenRequest *data;

  data = g_slice_new0 (SystemPromptOpenRequest);

  data->service = service;
  data->inquiry = g_object_ref (inquiry);
  data->cancellable = g_object_ref (cancellable);

  return data;
}

static void
system_prompt_open_request_free (SystemPromptOpenRequest *data)
{
  g_clear_object (&data->inquiry);
  g_clear_object (&data->cancellable);
  g_slice_free (SystemPromptOpenRequest, data);
}

static void
on_system_prompt_open (GcrSystemPrompt         *system_prompt,
                       GAsyncResult            *result,
                       SystemPromptOpenRequest *request)
{
  GoaIdentityService *self = request->service;
  GoaIdentityInquiry *inquiry = request->inquiry;
  GCancellable *cancellable = request->cancellable;
  GoaIdentity *identity;
  GoaIdentityQuery *query;
  GcrPrompt *prompt;
  GError *error;
  GoaIdentityInquiryIter iter;

  error = NULL;
  prompt = gcr_system_prompt_open_finish (result, &error);

  if (prompt == NULL)
    {
      if (error != NULL)
        {
          g_debug ("GoaIdentityService: could not open system prompt: %s",
                   error->message);
          g_error_free (error);
        }
      return;
    }

  identity = goa_identity_inquiry_get_identity (inquiry);
  goa_identity_inquiry_iter_init (&iter, inquiry);
  while ((query = goa_identity_inquiry_iter_next (&iter, inquiry)) != NULL)
    query_user (self, identity, inquiry, query, prompt, cancellable);

  system_prompt_open_request_free (request);
}

static void
on_identity_inquiry (GoaIdentityInquiry *inquiry,
                     GCancellable       *cancellable,
                     GoaIdentityService *self)
{
  SystemPromptOpenRequest *request;

  request = system_prompt_open_request_new (self, inquiry, cancellable);
  gcr_system_prompt_open_async (-1,
                                cancellable,
                                (GAsyncReadyCallback)
                                on_system_prompt_open,
                                request);
}

static void
cancel_sign_in (GoaIdentityManager *identity_manager,
                GoaIdentity        *identity,
                GTask              *operation_result)
{
  GoaIdentity *operation_identity;

  operation_identity = g_task_get_task_data (operation_result);
  if (operation_identity == identity)
    {
      GCancellable *cancellable;

      cancellable = g_task_get_cancellable (operation_result);
      g_cancellable_cancel (cancellable);
    }
}

static void
sign_in (GoaIdentityService     *self,
         const char             *identifier,
         gconstpointer           initial_password,
         const char             *preauth_source,
         GoaIdentitySignInFlags  flags,
         GCancellable           *cancellable,
         GAsyncReadyCallback     callback,
         gpointer                user_data)
{
  GTask *operation_result;

  g_debug ("GoaIdentityService: asking to sign in");

  operation_result = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (operation_result, user_data, NULL);

  g_signal_connect_object (self->identity_manager, "identity-refreshed", G_CALLBACK (cancel_sign_in), operation_result, 0);

  goa_identity_manager_sign_identity_in (self->identity_manager,
                                         identifier,
                                         initial_password,
                                         preauth_source,
                                         flags,
                                         (GoaIdentityInquiryFunc)
                                         on_identity_inquiry,
                                         self,
                                         cancellable,
                                         (GAsyncReadyCallback)
                                         on_identity_signed_in,
                                         g_object_ref (operation_result));

  g_object_unref (operation_result);
}

static void
on_identity_expiring (GoaIdentityManager *identity_manager,
                      GoaIdentity        *identity,
                      GoaIdentityService *self)
{
  const char *principal;
  GoaObject  *object;

  principal = goa_identity_get_identifier (identity);

  g_debug ("GoaIdentityService: identity %s expiring", principal);

  object = find_object_with_principal (self, principal, TRUE);

  if (object == NULL)
    return;

  ensure_account_credentials (self, object);
  g_clear_object (&object);
}
static void
handle_identity_expired (GoaIdentityService *self,
                         GoaIdentity        *identity)
{
  const char *principal;
  GoaObject  *object;

  principal = goa_identity_get_identifier (identity);

  g_debug ("GoaIdentityService: identity %s expired", principal);

  object = find_object_with_principal (self, principal, TRUE);

  if (object == NULL)
    return;

  ensure_account_credentials (self, object);
  g_clear_object (&object);
}

static void
on_identity_expired (GoaIdentityManager *identity_manager,
                     GoaIdentity        *identity,
                     GoaIdentityService *self)
{
  handle_identity_expired (self, identity);
}

static void
on_sign_out_for_account_change_done (GoaIdentityService *self,
                                     GAsyncResult       *result)
{
  GError *error = NULL;
  gboolean had_error;

  /* Workaround for bgo#764163 */
  had_error = g_task_had_error (G_TASK (result));
  g_task_propagate_boolean (G_TASK (result), &error);
  if (had_error)
    {
      g_debug ("Log out failed: %s", error->message);
      g_error_free (error);
    }
  else
    {
      g_debug ("Log out complete");
    }
}

static void
on_ticketing_done (GoaIdentityService *self,
                   GAsyncResult       *result)
{
  GoaObject *object;

  object = g_task_get_task_data (G_TASK (result));
  ensure_account_credentials (self, object);
}

static void
on_got_ticket (GoaTicketing *ticketing,
               GAsyncResult *result,
               GTask        *operation_result)
{
  GoaObject          *object;
  GoaAccount         *account;
  GError             *error;
  const char         *account_identity;

  object = g_task_get_task_data (operation_result);
  account = goa_object_peek_account (object);
  account_identity = goa_account_get_identity (account);

  error = NULL;
  if (!goa_ticketing_call_get_ticket_finish (ticketing,
                                             result,
                                             &error))
    {
      g_debug ("GoaIdentityService: could not get ticket for account %s: %s",
               account_identity,
               error->message);

      g_task_return_error (operation_result, error);
      g_object_unref (operation_result);
      return;
    }

  g_debug ("GoaIdentityService: got ticket for account %s",
           account_identity);
  g_task_return_boolean (operation_result, TRUE);
  g_object_unref (operation_result);
}

static void
on_account_interface_added (GDBusObjectManager *manager,
                            GoaObject          *object,
                            GDBusInterface     *interface,
                            GoaIdentityService *self)
{
  GoaAccount         *account;
  GoaTicketing       *ticketing;
  GDBusInterfaceInfo *info;
  const char         *provider_type;

  account = goa_object_peek_account (object);

  if (account == NULL)
    return;

  provider_type = goa_account_get_provider_type (account);

  if (g_strcmp0 (provider_type, "kerberos") != 0)
    return;

  info = g_dbus_interface_get_info (interface);

  if (g_strcmp0 (info->name, "org.gnome.OnlineAccounts.Ticketing") != 0)
    return;

  ticketing = goa_object_peek_ticketing (object);

  if (ticketing != NULL)
    {
      GTask *operation_result;

      operation_result = g_task_new (self, NULL, (GAsyncReadyCallback) on_ticketing_done, NULL);
      g_task_set_task_data (operation_result, object, NULL);

      /* Ticketing interface is present, sign in if not already
       * signed in.
       */
      goa_ticketing_call_get_ticket (ticketing,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_ticket,
                                     g_object_ref (operation_result));

      g_object_unref (operation_result);
      return;
    }
}

static void
on_account_interface_removed (GDBusObjectManager *manager,
                              GoaObject          *object,
                              GDBusInterface     *interface,
                              GoaIdentityService *self)
{
  GoaAccount         *account;
  GoaTicketing       *ticketing;
  GDBusInterfaceInfo *info;
  const char         *provider_type;
  const char         *account_identity;
  GTask              *task;

  account = goa_object_peek_account (object);

  if (account == NULL)
    return;

  provider_type = goa_account_get_provider_type (account);

  if (g_strcmp0 (provider_type, "kerberos") != 0)
    return;

  info = g_dbus_interface_get_info (interface);

  if (g_strcmp0 (info->name, "org.gnome.OnlineAccounts.Ticketing") != 0)
    return;

  ticketing = goa_object_peek_ticketing (object);

  if (ticketing != NULL)
    return;

  /* Ticketing interface is gone, sign out if not already
   * signed out. Also, since the user is playing around
   * with the account make it permanent.
   */
  goa_account_set_is_temporary (account, FALSE);

  account_identity = goa_account_get_identity (account);

  g_debug ("Kerberos account %s was disabled and should now be signed out", account_identity);

  task = g_task_new (self, NULL, (GAsyncReadyCallback) on_sign_out_for_account_change_done, NULL);
  goa_identity_manager_get_identity (self->identity_manager,
                                     account_identity,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_identity_for_sign_out,
                                     g_object_ref (task));

  g_object_unref (task);
}

static void
on_identities_listed (GoaIdentityManager *manager,
                      GAsyncResult       *result,
                      GoaIdentityService *self)
{
  GError *error = NULL;
  GList *identities, *node;

  g_signal_connect (self->identity_manager, "identity-added", G_CALLBACK (on_identity_added), self);
  g_signal_connect (self->identity_manager, "identity-removed", G_CALLBACK (on_identity_removed), self);
  g_signal_connect (self->identity_manager, "identity-refreshed", G_CALLBACK (on_identity_refreshed), self);
  g_signal_connect (self->identity_manager, "identity-needs-renewal", G_CALLBACK (on_identity_needs_renewal), self);
  g_signal_connect (self->identity_manager, "identity-expiring", G_CALLBACK (on_identity_expiring), self);
  g_signal_connect (self->identity_manager, "identity-expired", G_CALLBACK (on_identity_expired), self);

  identities = goa_identity_manager_list_identities_finish (manager, result, &error);

  if (identities == NULL)
    {
      if (error != NULL)
        {
          g_warning ("Could not list identities: %s", error->message);
          g_error_free (error);
        }
      goto out;
    }

  for (node = identities; node != NULL; node = node->next)
    {
      GoaIdentity *identity = node->data;
      const char  *principal;
      GoaObject   *object;
      char        *object_path;

      object_path = export_identity (self, identity);

      principal = goa_identity_get_identifier (identity);

      object = find_object_with_principal (self, principal, TRUE);

      if (object == NULL)
        add_temporary_account (self, identity);
      else
        g_object_unref (object);

      g_free (object_path);

      /* Treat identities that started out expired as if they just expired, in case
       * the identity service is started long after goa-daemon
       */
      if (!goa_identity_is_signed_in (identity))
        handle_identity_expired (self, identity);
    }

 out:
  g_object_unref (self);
}

static void
ensure_credentials_for_accounts (GoaIdentityService *self)
{
  GDBusObjectManager *object_manager;
  GList      *accounts;
  GList      *node;

  object_manager = goa_client_get_object_manager (self->client);

  g_signal_connect (object_manager, "interface-added", G_CALLBACK (on_account_interface_added), self);
  g_signal_connect (object_manager, "interface-removed", G_CALLBACK (on_account_interface_removed), self);

  accounts = goa_client_get_accounts (self->client);

  for (node = accounts; node != NULL; node = node->next)
    {
      GoaObject *object = GOA_OBJECT (node->data);
      GoaAccount *account;
      GoaTicketing *ticketing;
      const char *provider_type;

      account = goa_object_peek_account (object);

      if (account == NULL)
        continue;

      provider_type = goa_account_get_provider_type (account);

      if (g_strcmp0 (provider_type, "kerberos") != 0)
        continue;

      ticketing = goa_object_peek_ticketing (object);

      if (ticketing == NULL)
        continue;

      ensure_account_credentials (self, object);
    }

  g_list_free_full (accounts, g_object_unref);
}

static void
on_got_client (GoaClient          *client,
               GAsyncResult       *result,
               GoaIdentityService *self)
{
  GError *error;

  error = NULL;

  self->client = goa_client_new_finish (result, &error);

  if (self->client == NULL)
    {
      g_warning ("Could not create client: %s", error->message);
      goto out;
    }

  self->identity_manager = goa_kerberos_identity_manager_new (NULL, &error);

  if (self->identity_manager == NULL)
    {
      g_warning ("Could not create identity manager: %s", error->message);
      goto out;
    }

  goa_identity_manager_list_identities (self->identity_manager,
                                        NULL,
                                        (GAsyncReadyCallback)
                                        on_identities_listed,
                                        g_object_ref (self));

  ensure_credentials_for_accounts (self);

 out:
  g_object_unref (self);
}

static void
on_session_bus_acquired (GDBusConnection    *connection,
                         const char         *unique_name,
                         GoaIdentityService *self)
{
  g_debug ("GoaIdentityService: Connected to session bus");

  if (self->connection == NULL)
  {
    self->connection = g_object_ref (connection);
    g_dbus_object_manager_server_set_connection (self->object_manager_server, self->connection);
    goa_client_new (NULL,
                    (GAsyncReadyCallback)
                    on_got_client,
                    g_object_ref (self));
  }
}

static void
on_name_acquired (GDBusConnection    *connection,
                  const char         *name,
                  GoaIdentityService *self)
{
  if (g_strcmp0 (name, "org.gnome.Identity") == 0)
    g_debug ("GoaIdentityService: Acquired name org.gnome.Identity");
}

static void
on_name_lost (GDBusConnection    *connection,
              const char         *name,
              GoaIdentityService *self)
{
  if (g_strcmp0 (name, "org.gnome.Identity") == 0)
    raise (SIGTERM);
}

gboolean
goa_identity_service_activate (GoaIdentityService   *self,
                               GError              **error)
{
  GoaIdentityServiceObjectSkeleton *object;

  g_return_val_if_fail (GOA_IS_IDENTITY_SERVICE (self), FALSE);

  g_debug ("GoaIdentityService: Activating identity service");

  self->object_manager_server = g_dbus_object_manager_server_new ("/org/gnome/Identity");

  object = goa_identity_service_object_skeleton_new ("/org/gnome/Identity/Manager");
  goa_identity_service_object_skeleton_set_manager (object,
                                                    GOA_IDENTITY_SERVICE_MANAGER (self));

  g_dbus_object_manager_server_export (self->object_manager_server, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  self->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                 "org.gnome.Identity",
                                 G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                 G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                 (GBusAcquiredCallback) on_session_bus_acquired,
                                 (GBusNameAcquiredCallback) on_name_acquired,
                                 (GBusNameVanishedCallback) on_name_lost,
                                 self,
                                 NULL);

  return TRUE;
}

void
goa_identity_service_deactivate (GoaIdentityService *self)
{
  g_debug ("GoaIdentityService: Deactivating identity service");

  if (self->identity_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self, on_identity_needs_renewal, self);
      g_signal_handlers_disconnect_by_func (self, on_identity_expiring, self);
      g_signal_handlers_disconnect_by_func (self, on_identity_expired, self);
      g_clear_object (&self->identity_manager);
    }

  g_clear_object (&self->object_manager_server);
  g_clear_object (&self->connection);
  g_clear_object (&self->client);
}

static void
goa_identity_service_class_init (GoaIdentityServiceClass *service_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (service_class);

  object_class->finalize = goa_identity_service_finalize;

  goa_identity_utils_register_error_domain (GOA_IDENTITY_ERROR, GOA_TYPE_IDENTITY_ERROR);
  goa_identity_utils_register_error_domain (GOA_IDENTITY_MANAGER_ERROR, GOA_TYPE_IDENTITY_MANAGER_ERROR);
}

GoaIdentityService *
goa_identity_service_new (void)
{
  GObject *object;

  object = g_object_new (GOA_TYPE_IDENTITY_SERVICE,
                         NULL);

  return GOA_IDENTITY_SERVICE (object);
}
