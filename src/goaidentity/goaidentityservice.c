/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Written by: Ray Strode
 *             Stef Walter
 */

#include "config.h"
#include "goaidentityservice.h"

#include <glib/gi18n-lib.h>
#include <gmodule.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <gcr/gcr.h>

#include "goaidentityenumtypes.h"
#include "goaidentityutils.h"

#include "goakerberosidentitymanager.h"
#include "goalogging.h"

#include "um-realm-manager.h"

struct _GoaIdentityServicePrivate
{
  GDBusConnection          *connection;
  GDBusObjectManagerServer *object_manager_server;
  guint                     bus_id;

  GoaIdentityManager       *identity_manager;

  UmRealmManager           *realm_manager;
  guint                     realmd_watch;
  GCancellable             *cancellable;

  GHashTable               *watched_client_connections;
  GHashTable               *key_holders;

  /* FIXME: This is a little ucky, since the goa service
   * is in process, we should able to use direct calls.
   */
  GoaClient                *client;
  GoaManager               *accounts_manager;
};

static void identity_service_manager_interface_init (GoaIdentityServiceManagerIface *interface);

static void on_realm_joined (UmRealmObject      *realm,
                             GAsyncResult       *result,
                             GSimpleAsyncResult *operation_result);

static void on_realm_looked_up_for_sign_in (GoaIdentityService *self,
                                            GAsyncResult       *result,
                                            GSimpleAsyncResult *operation_result);

static void
look_up_realm (GoaIdentityService  *self,
               const char          *identifier,
               const char          *domain,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data);
static void
sign_in (GoaIdentityService     *self,
         const char             *identifier,
         UmRealmObject          *realm,
         gconstpointer           initial_password,
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

  g_object_bind_property (G_OBJECT (identity),
                          "identifier",
                          G_OBJECT (interface),
                          "identifier",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (G_OBJECT (identity),
                          "expiration-timestamp",
                          G_OBJECT (interface),
                          "expiration-timestamp",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (G_OBJECT (identity),
                          "is-signed-in",
                          G_OBJECT (interface),
                          "is-signed-in",
                          G_BINDING_SYNC_CREATE);

  g_dbus_object_skeleton_add_interface (object, interface);
  g_object_unref (interface);

  g_dbus_object_manager_server_export (self->priv->object_manager_server, object);
  g_object_unref (object);

  return object_path;
}

static void
unexport_identity (GoaIdentityService *self,
                   GoaIdentity        *identity)
{
  char *object_path;

  object_path = get_object_path_for_identity (self, identity);

  g_dbus_object_manager_server_unexport (self->priv->object_manager_server,
                                         object_path);
  g_free (object_path);
}

static char *
get_object_path_for_realm (GoaIdentityService *self,
                           UmRealmObject      *realm)
{
  const char *domain;
  char       *escaped_domain;
  char       *object_path;

  domain = um_realm_kerberos_get_domain_name (um_realm_object_peek_kerberos (realm));
  escaped_domain = goa_identity_utils_escape_object_path (domain,
                                                          strlen (domain));
  object_path = g_strdup_printf ("/org/gnome/Identity/Realms/%s", escaped_domain);

  g_free (escaped_domain);
  return object_path;
}

static void
on_realm_gone (GoaIdentityService *self,
               GAsyncResult       *result,
               char               *object_path)
{
  g_dbus_object_manager_server_unexport (self->priv->object_manager_server,
                                         object_path);
  g_free (object_path);
  g_object_unref (result);
}

static char *
export_realm (GoaIdentityService *self,
              UmRealmObject      *realm)
{
  char *object_path;
  GDBusObjectSkeleton *object;
  GDBusInterfaceSkeleton *interface;
  UmRealmKerberos    *kerberos;
  GSimpleAsyncResult *operation_result;

  kerberos = um_realm_object_peek_kerberos (realm);

  goa_debug ("GoaIdentityService: exporting realm %s",
             um_realm_kerberos_get_domain_name (kerberos));

  object_path = get_object_path_for_realm (self, realm);

  object = G_DBUS_OBJECT_SKELETON (goa_identity_service_object_skeleton_new (object_path));
  interface = G_DBUS_INTERFACE_SKELETON (goa_identity_service_realm_skeleton_new ());

  g_object_bind_property (G_OBJECT (realm),
                          "configured",
                          G_OBJECT (interface),
                          "is-enrolled",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (G_OBJECT (kerberos),
                          "realm-name",
                          G_OBJECT (interface),
                          "realm-name",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (G_OBJECT (kerberos),
                          "domain-name",
                          G_OBJECT (interface),
                          "domain-name",
                          G_BINDING_SYNC_CREATE);
  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                (GAsyncReadyCallback)
                                                on_realm_gone,
                                                g_strdup (object_path),
                                                export_realm);

  g_object_weak_ref (G_OBJECT (self->priv->realm_manager),
                     (GWeakNotify)
                     g_simple_async_result_complete_in_idle,
                     operation_result);

  g_dbus_object_skeleton_add_interface (object, interface);
  g_object_unref (interface);

  g_dbus_object_manager_server_export (self->priv->object_manager_server, object);
  g_object_unref (object);

  return object_path;
}

static void
on_system_enrollment_prompt_answered (GcrPrompt           *prompt,
                                      GAsyncResult        *result,
                                      GSimpleAsyncResult  *operation_result)
{
  GCancellable       *cancellable;
  const char         *answer;
  GError             *error;

  error = NULL;
  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");

  answer = gcr_prompt_password_finish (prompt, result, &error);

  if (answer == NULL)
    {
      if (error != NULL)
        g_simple_async_result_take_error (operation_result, error);
      else
        g_cancellable_cancel (cancellable);

      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  g_simple_async_result_set_op_res_gpointer (operation_result,
                                             (gpointer)
                                             answer,
                                             NULL);
  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);
}

static void
on_system_enrollment_prompt_open (GcrSystemPrompt    *system_prompt,
                                  GAsyncResult       *result,
                                  GSimpleAsyncResult *operation_result)
{
  GCancellable       *cancellable;
  GcrPrompt          *prompt;
  GError             *error;
  const char         *message;

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
  message = g_simple_async_result_get_source_tag (operation_result);

  error = NULL;
  prompt = gcr_system_prompt_open_finish (result, &error);

  if (prompt == NULL)
    {
      if (error != NULL)
        g_simple_async_result_complete_in_idle (operation_result);
      else
        g_cancellable_cancel (cancellable);

      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  g_object_set_data (G_OBJECT (operation_result), "prompt", prompt);

  gcr_prompt_set_title (prompt, _("Domain Administrator Login"));

  message = g_object_get_data (G_OBJECT (operation_result), "message");
  gcr_prompt_set_message (prompt, message);

  /* FIXME: When asking for a username, we show password bullets.
   */
  gcr_prompt_password_async (prompt,
                             cancellable,
                             (GAsyncReadyCallback)
                             on_system_enrollment_prompt_answered,
                             operation_result);
}

static void
open_system_enrollment_prompt (GoaIdentityService  *self,
                               const char          *message,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  GSimpleAsyncResult *operation_result;

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                (gpointer)
                                                message);
  g_simple_async_result_set_check_cancellable (operation_result, cancellable);

  g_object_set_data (G_OBJECT (operation_result),
                     "cancellable",
                     cancellable);

  gcr_system_prompt_open_async (-1,
                                cancellable,
                                (GAsyncReadyCallback)
                                on_system_enrollment_prompt_open,
                                operation_result);
}

static void
on_system_enrollment_password_answered (GoaIdentityService  *self,
                                        GAsyncResult        *result,
                                        GSimpleAsyncResult  *operation_result)
{
  GCancellable       *cancellable;
  GcrPrompt          *prompt;
  UmRealmObject      *realm;
  GBytes             *credentials;
  const char         *username;
  const char         *password;
  GError             *error;

  prompt = g_object_get_data (G_OBJECT (result), "prompt");

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_simple_async_result_take_error (operation_result, error);

      gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, &error);

      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");

  password = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

  realm = g_simple_async_result_get_source_tag (operation_result);
  username = g_object_get_data (G_OBJECT (operation_result), "username");
  credentials = g_object_get_data (G_OBJECT (operation_result), "credentials");

  if (!um_realm_join_as_admin (realm,
                               username,
                               password,
                               credentials,
                               cancellable,
                               (GAsyncReadyCallback)
                               on_realm_joined,
                               operation_result))
    {
       g_simple_async_result_set_error (operation_result,
                                        UM_REALM_ERROR,
                                        UM_REALM_ERROR_GENERIC,
                                        _("Could not find supported credentials"));
       g_simple_async_result_complete_in_idle (operation_result);
       g_object_unref (operation_result);
       return;
    }
  gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, &error);
}

static void
on_system_enrollment_username_answered (GoaIdentityService  *self,
                                        GAsyncResult        *result,
                                        GSimpleAsyncResult  *operation_result)
{
  GCancellable       *cancellable;
  GcrPrompt          *prompt;
  const char         *username;
  GError             *error;

  prompt = g_object_get_data (G_OBJECT (result), "prompt");

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_simple_async_result_take_error (operation_result, error);

      gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, &error);

      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");

  username = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
  g_object_set_data_full (G_OBJECT (operation_result),
                          "username",
                          g_strdup (username),
                          (GDestroyNotify)
                          g_free);
  gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, &error);

  open_system_enrollment_prompt (self,
                                 _("In order to use this enterprise identity, the computer needs to be "
                                   "enrolled in the domain. Please have your network administrator "
                                   "type their domain password here."),
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_system_enrollment_password_answered,
                                 operation_result);
}

static void
enroll_machine_as_administrator (GoaIdentityService  *self,
                                 GoaIdentity         *identity,
                                 UmRealmObject       *realm,
                                 GBytes              *credentials,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  GSimpleAsyncResult *operation_result;

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                realm);
  g_simple_async_result_set_check_cancellable (operation_result, cancellable);

  g_object_set_data (G_OBJECT (operation_result),
                     "cancellable",
                     cancellable);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "identity",
                          g_object_ref (identity),
                          (GDestroyNotify)
                          g_object_unref);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "credentials",
                          g_bytes_ref (credentials),
                          (GDestroyNotify)
                          g_bytes_unref);
  open_system_enrollment_prompt (self,
                                 _("In order to use this enterprise identity, the computer needs to be "
                                   "enrolled in the domain. Please have your network administrator "
                                   "type their domain username here."),
                                 cancellable,
                                 (GAsyncReadyCallback)
                                 on_system_enrollment_username_answered,
                                 operation_result);

}

static void
on_machine_enrolled (GoaIdentityService *self,
                     GAsyncResult       *result,
                     GSimpleAsyncResult *operation_result)
{
  g_object_unref (operation_result);
}

static void
on_realm_joined (UmRealmObject      *realm,
                 GAsyncResult       *result,
                 GSimpleAsyncResult *operation_result)
{
  GoaIdentityService  *self;
  GoaIdentity         *identity;
  GError              *error;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

  error = NULL;
  if (!um_realm_join_finish (realm, result, &error))
    {
      GCancellable *cancellable;
      GBytes       *credentials;

      if (!g_error_matches (error, UM_REALM_ERROR, UM_REALM_ERROR_BAD_LOGIN) &&
          !g_error_matches (error, UM_REALM_ERROR, UM_REALM_ERROR_BAD_PASSWORD))
        {
          g_simple_async_result_take_error (operation_result, error);
          g_simple_async_result_complete_in_idle (operation_result);
          g_object_unref (operation_result);
          return;
        }

      cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
      identity = g_object_get_data (G_OBJECT (operation_result), "identify");
      credentials = g_object_get_data (G_OBJECT (operation_result), "credentials");

      /* Try again, as an administrator */
      enroll_machine_as_administrator (self,
                                       identity,
                                       realm,
                                       credentials,
                                       cancellable,
                                       (GAsyncReadyCallback)
                                       on_machine_enrolled,
                                       operation_result);

      return;
    }

  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);
}

static void
enroll_machine_as_user (GoaIdentityService  *self,
                        GoaIdentity         *identity,
                        UmRealmObject       *realm,
                        const char          *password,
                        GBytes              *credentials,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  GSimpleAsyncResult *operation_result;

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                realm);
  g_simple_async_result_set_check_cancellable (operation_result, cancellable);

  g_object_set_data (G_OBJECT (operation_result),
                     "cancellable",
                     cancellable);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "identity",
                          g_object_ref (identity),
                          (GDestroyNotify)
                          g_object_unref);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "credentials",
                          g_bytes_ref (credentials),
                          (GDestroyNotify)
                          g_bytes_unref);

  if (!um_realm_join_as_user (realm,
                              goa_identity_get_identifier (identity),
                              password,
                              credentials,
                              cancellable,
                              (GAsyncReadyCallback)
                              on_realm_joined,
                              operation_result))
    {
       g_simple_async_result_set_error (operation_result,
                                        UM_REALM_ERROR,
                                        UM_REALM_ERROR_GENERIC,
                                        _("Could not find supported credentials"));
       g_simple_async_result_complete_in_idle (operation_result);
       g_object_unref (operation_result);
       return;
    }
}

static void
on_realm_looked_up_for_enrollment (GoaIdentityService *self,
                                   GAsyncResult       *result,
                                   GSimpleAsyncResult *operation_result)
{
  UmRealmObject *realm;
  GoaIdentity   *identity;
  GError        *error;
  GCancellable  *cancellable;
  GBytes        *credentials;
  gconstpointer  initial_password;

  realm = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             &error))
    {
      goa_debug ("GoaIdentityService: Could not discover realm: %s",
                 error->message);
      g_error_free (error);

      g_object_unref (operation_result);
      return;
    }

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
  initial_password = g_object_get_data (G_OBJECT (operation_result),
                                        "initial-password");
  identity = g_object_get_data (G_OBJECT (operation_result), "identity");
  credentials = goa_identity_get_credentials (identity);

  /* Otherwise, try to enroll the machine with the domain controller
   */
  enroll_machine_as_user (self,
                          identity,
                          realm,
                          initial_password,
                          credentials,
                          cancellable,
                          (GAsyncReadyCallback)
                          on_machine_enrolled,
                          operation_result);
  g_bytes_unref (credentials);
}

static void
on_sign_in_done (GoaIdentityService *self,
                 GAsyncResult       *result,
                 GSimpleAsyncResult *operation_result)
{
  GoaIdentity      *identity;
  char             *object_path;
  char             *domain;
  GCancellable     *cancellable;
  UmRealmObject    *realm;
  GError           *error;

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_simple_async_result_take_error (operation_result, error);
      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  identity = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
  object_path = export_identity (self, identity);
  realm = g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result));

  g_simple_async_result_set_op_res_gpointer (operation_result,
                                             object_path,
                                             (GDestroyNotify)
                                             g_free);

  /* User is signed in, so we're mostly done
   */
  g_simple_async_result_complete_in_idle (operation_result);

  if (realm != NULL && um_realm_is_configured (realm))
    {
      g_object_unref (operation_result);
      return;
    }

  /* Try to enroll the machine at the point, too, if necessary.
   */
  g_object_set_data_full (G_OBJECT (operation_result),
                          "identity",
                          g_object_ref (identity),
                          (GDestroyNotify)
                          g_object_unref);

  domain = g_object_get_data (G_OBJECT (operation_result),
                                        "domain");
  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");

  look_up_realm (self,
                 goa_identity_get_identifier (identity),
                 domain,
                 cancellable,
                 (GAsyncReadyCallback)
                 on_realm_looked_up_for_enrollment,
                 operation_result);
}

static GoaObject *
find_object_with_principal (GoaIdentityService *self,
                            const char         *principal,
                            gboolean            must_be_enabled)
{
  GList      *objects;
  GList      *node;
  GoaObject  *found_object;

  objects = goa_client_get_accounts (self->priv->client);

  found_object = NULL;
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

      if (g_strcmp0 (provider_type, "kerberos") != 0)
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
  g_list_free_full (objects, (GDestroyNotify) g_object_unref);

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
      goa_debug ("GoaIdentityService: could not ensure credentials for account %s: %s",
                 account_identity,
                 error->message);
      g_error_free (error);
      return;
    }

  goa_debug ("GoaIdentityService: credentials for account %s ensured for %d seconds",
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

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
    }
  else
    {
      const char *object_path;

      object_path = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
      goa_identity_service_manager_complete_sign_in (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                     invocation,
                                                     object_path);
    }
}

static void
read_sign_in_details (GoaIdentityServiceManager  *manager,
                      GVariant                   *details,
                      GoaIdentitySignInFlags     *flags,
                      char                      **domain,
                      char                      **secret_key)
{
  GVariantIter  iter;
  char          *key;
  char          *value;

  *flags = GOA_IDENTITY_SIGN_IN_FLAGS_NONE;
  g_variant_iter_init (&iter, details);
  while (g_variant_iter_loop (&iter, "{ss}", &key, &value))
    {
      if (g_strcmp0 (key, "domain") == 0)
        *domain = g_strdup (value);
      else if (g_strcmp0 (key, "initial-password") == 0)
        *secret_key = g_strdup (value);
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

static void
on_realm_looked_up_for_sign_in (GoaIdentityService *self,
                                GAsyncResult       *result,
                                GSimpleAsyncResult *operation_result)
{
  UmRealmObject          *realm;
  GoaIdentitySignInFlags  flags;
  GError                 *error;
  GCancellable           *cancellable;
  const char             *identifier;
  gconstpointer           initial_password;
  char                   *new_identifier;

  identifier = g_simple_async_result_get_source_tag (operation_result);
  new_identifier = NULL;
  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             &error))
    {
      goa_debug ("GoaIdentityService: Could not discover realm: %s",
                 error->message);
      g_error_free (error);

      /* let it slide, we might not need realmd for this deployment
       */
      realm = NULL;
    }
  else
    {
      char *user;
      UmRealmKerberos *kerberos;

      /* Rebuild the identifier using the updated realm
       */

      realm = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

      goa_identity_utils_split_identifier (identifier,
                                           &user,
                                           NULL);

      kerberos = um_realm_object_peek_kerberos (realm);

      if (user != NULL)
        new_identifier = g_strdup_printf ("%s@%s",
                                          user,
                                          um_realm_kerberos_get_realm_name (kerberos));

      identifier = new_identifier;

      g_free (user);
    }

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
  initial_password = g_object_get_data (G_OBJECT (operation_result),
                                        "initial-password");
  flags = (GoaIdentitySignInFlags) g_object_get_data (G_OBJECT (operation_result),
                                                      "flags");

  sign_in (self,
           identifier,
           realm,
           initial_password,
           flags,
           cancellable,
           (GAsyncReadyCallback)
           on_sign_in_done,
           operation_result);

  g_free (new_identifier);
}

static void
on_realm_looked_up (UmRealmManager     *manager,
                    GAsyncResult       *result,
                    GSimpleAsyncResult *operation_result)
{
  GError *error;
  GList *realms;

  error = NULL;
  realms = um_realm_manager_discover_finish (manager, result, &error);
  if (error != NULL)
    {
      g_simple_async_result_take_error (operation_result, error);
      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  if (realms->data == NULL)
    g_simple_async_result_set_op_res_gpointer (operation_result, NULL, NULL);
  else
    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               g_object_ref (realms->data),
                                               (GDestroyNotify)
                                               g_free);
  g_list_free_full (realms,
                    (GDestroyNotify)
                    g_object_unref);
}

static void
on_manager_realm_added (UmRealmManager     *manager,
                        UmRealmObject      *realm,
                        GoaIdentityService *self)
{
  export_realm (self, realm);
}

static void
on_realm_manager_ensured (GObject            *source,
                          GAsyncResult       *result,
                          GSimpleAsyncResult *operation_result)
{
  GoaIdentityService *self;
  GError             *error;
  GList              *realms, *node;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

  g_clear_object (&self->priv->realm_manager);

  error = NULL;
  self->priv->realm_manager = um_realm_manager_new_finish (result, &error);
  if (error != NULL)
    {
      g_simple_async_result_take_error (operation_result, error);
    }
  else
    {
      realms = um_realm_manager_get_realms (self->priv->realm_manager);

      for (node = realms; node != NULL; node = node->next)
        export_realm (self, node->data);

      g_list_free (realms);

      g_signal_connect (self->priv->realm_manager,
                        "realm-added",
                        G_CALLBACK (on_manager_realm_added),
                        self);

    }

  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);
}

static void
ensure_realm_manager (GoaIdentityService  *self,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  GSimpleAsyncResult *operation_result;

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                ensure_realm_manager);

  g_simple_async_result_set_check_cancellable (operation_result, cancellable);
  um_realm_manager_new (self->priv->cancellable,
                        (GAsyncReadyCallback)
                        on_realm_manager_ensured,
                        operation_result);
}

static gboolean
foo (GSimpleAsyncResult *operation_result)
{
  GCancellable *cancellable;
  const char   *domain;
  GoaIdentityService *self;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));


  domain = g_object_get_data (G_OBJECT (operation_result), "domain");
  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
  um_realm_manager_discover (self->priv->realm_manager,
                             domain,
                             cancellable,
                             (GAsyncReadyCallback)
                             on_realm_looked_up,
                             operation_result);
  return FALSE;
}

static void
on_realm_manager_ensured_for_look_up (GoaIdentityService *self,
                                      GAsyncResult       *result,
                                      GSimpleAsyncResult *operation_result)
{
  GError       *error;

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_simple_async_result_take_error (operation_result, error);
      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  g_timeout_add_seconds (30, (GSourceFunc) foo, operation_result);
}

static void
look_up_realm (GoaIdentityService  *self,
               const char          *identifier,
               const char          *domain,
               GCancellable        *cancellable,
               GAsyncReadyCallback  callback,
               gpointer             user_data)
{
  GSimpleAsyncResult *operation_result;
  char *domain_to_look_up;

  goa_debug ("GoaIdentityService: looking up realm");

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                look_up_realm);

  g_simple_async_result_set_check_cancellable (operation_result, cancellable);

  if (domain != NULL)
    {
      domain_to_look_up = g_strdup (domain);
    }
  else
    {
      goa_identity_utils_split_identifier (identifier,
                                           NULL,
                                           &domain_to_look_up);
    }

  if (domain_to_look_up == NULL)
    domain_to_look_up = g_strdup ("");

  g_object_set_data_full (G_OBJECT (operation_result),
                          "domain",
                          domain_to_look_up,
                          (GDestroyNotify)
                          g_free);

  ensure_realm_manager (self,
                        cancellable,
                        (GAsyncReadyCallback)
                        on_realm_manager_ensured_for_look_up,
                        operation_result);

}

static gboolean
goa_identity_service_handle_sign_in (GoaIdentityServiceManager *manager,
                                     GDBusMethodInvocation     *invocation,
                                     const char                *identifier,
                                     GVariant                  *details)
{
  GoaIdentityService     *self = GOA_IDENTITY_SERVICE (manager);
  GSimpleAsyncResult     *operation_result;
  GoaIdentitySignInFlags  flags;
  char                   *domain;
  char                   *secret_key;
  gconstpointer           initial_password;
  GCancellable           *cancellable;

  domain = NULL;
  secret_key = NULL;
  initial_password = NULL;

  read_sign_in_details (manager, details, &flags, &domain, &secret_key);

  if (secret_key != NULL)
    {
      GcrSecretExchange *secret_exchange;

      secret_exchange = g_hash_table_lookup (self->priv->key_holders,
                                             g_dbus_method_invocation_get_sender (invocation));

      if (secret_exchange == NULL)
        {
          g_free (secret_key);
          g_dbus_method_invocation_return_error (invocation,
                                                 GOA_IDENTITY_MANAGER_ERROR,
                                                 GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS,
                                                 _("initial secret passed before secret key exchange"));
          return TRUE;
        }

      gcr_secret_exchange_receive (secret_exchange, secret_key);
      g_free (secret_key);

      initial_password = gcr_secret_exchange_get_secret (secret_exchange, NULL);
    }

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                (GAsyncReadyCallback)
                                                on_sign_in_handled,
                                                g_object_ref (invocation),
                                                g_strdup (identifier));
  cancellable = g_cancellable_new ();
  g_object_set_data (G_OBJECT (operation_result),
                     "cancellable",
                     cancellable);
  g_object_set_data (G_OBJECT (operation_result),
                     "initial-password",
                     (gpointer)
                     initial_password);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "domain",
                          domain,
                          (GDestroyNotify)
                          g_free);
  g_object_set_data (G_OBJECT (operation_result),
                     "flags",
                     GINT_TO_POINTER ((int) flags));
  if (domain == NULL)
    sign_in (self,
             identifier,
             NULL,
             initial_password,
             flags,
             cancellable,
             (GAsyncReadyCallback)
             on_sign_in_done,
             operation_result);
  else
    look_up_realm (self,
                   identifier,
                   domain,
                   cancellable,
                   (GAsyncReadyCallback)
                   on_realm_looked_up_for_sign_in,
                   operation_result);

  g_object_unref (cancellable);

  return TRUE;
}

static void
on_sign_out_handled (GoaIdentityService    *self,
                     GAsyncResult          *result,
                     GDBusMethodInvocation *invocation)
{
  GError *error;

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    g_dbus_method_invocation_take_error (invocation, error);
  else
    goa_identity_service_manager_complete_sign_out (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                    invocation);
}

static void
on_identity_signed_out (GoaIdentityManager *manager,
                        GAsyncResult       *result,
                        GSimpleAsyncResult *operation_result)
{
  GoaIdentityService *self;
  GError             *error;
  GoaIdentity        *identity;
  const char         *identifier;
  GoaObject          *object;

  error = NULL;
  goa_identity_manager_sign_identity_out_finish (manager, result, &error);

  if (error != NULL)
    {
      goa_debug ("GoaIdentityService: Identity could not be signed out: %s",
                 error->message);
      g_simple_async_result_take_error (operation_result, error);
    }

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));
  identity = g_object_get_data (G_OBJECT (operation_result), "identity");

  identifier = goa_identity_get_identifier (identity);
  object = find_object_with_principal (self, identifier, FALSE);

  if (object != NULL)
    ensure_account_credentials (self, object);

  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);
}

static void
on_got_identity_for_sign_out (GoaIdentityManager *manager,
                              GAsyncResult       *result,
                              GSimpleAsyncResult *operation_result)
{
  GError *error;
  GoaIdentity *identity;

  error = NULL;
  identity = goa_identity_manager_get_identity_finish (manager, result, &error);

  if (error != NULL)
    {
      goa_debug ("GoaIdentityService: Identity could not be signed out: %s",
                 error->message);
      return;
    }

  g_object_set_data_full (G_OBJECT (operation_result),
                          "identity",
                          g_object_ref (identity),
                          (GDestroyNotify)
                          g_object_unref);

  goa_identity_manager_sign_identity_out (manager,
                                          identity,
                                          NULL,
                                          (GAsyncReadyCallback)
                                          on_identity_signed_out,
                                          operation_result);
}

static gboolean
goa_identity_service_handle_sign_out (GoaIdentityServiceManager *manager,
                                      GDBusMethodInvocation     *invocation,
                                      const char                *identifier)
{
  GoaIdentityService *self = GOA_IDENTITY_SERVICE (manager);
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (self),
                                      (GAsyncReadyCallback)
                                      on_sign_out_handled,
                                      g_object_ref (invocation),
                                      goa_identity_service_handle_sign_out);

  goa_identity_manager_get_identity (self->priv->identity_manager,
                                     identifier,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_identity_for_sign_out,
                                     result);
  return TRUE;
}

static void
on_secret_keys_exchanged (GoaIdentityService *self,
                          GAsyncResult       *result)
{
  GDBusMethodInvocation *invocation;
  GError                *error;

  invocation = g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result));

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
    }
  else
    {
      const char *output_key;

      output_key = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
      goa_identity_service_manager_complete_exchange_secret_keys (GOA_IDENTITY_SERVICE_MANAGER (self),
                                                                  invocation,
                                                                  output_key);
    }
}

static void
on_caller_watched (GDBusConnection    *connection,
                   const char         *name,
                   const char         *name_owner,
                   GSimpleAsyncResult *operation_result)
{
  GoaIdentityService    *self;
  GcrSecretExchange     *secret_exchange;
  const char            *input_key;
  char                  *output_key;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));
  input_key = g_object_get_data (G_OBJECT (operation_result), "input-key");

  secret_exchange = gcr_secret_exchange_new (NULL);

  if (!gcr_secret_exchange_receive (secret_exchange,
                                    input_key))
    {
      g_simple_async_result_set_error (operation_result,
                                       GCR_ERROR,
                                       GCR_ERROR_UNRECOGNIZED,
                                       _("Initial secret key is invalid"));
      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  g_hash_table_insert (self->priv->key_holders,
                       g_strdup (name_owner),
                       secret_exchange);

  output_key = gcr_secret_exchange_send (secret_exchange, NULL, 0);

  g_simple_async_result_set_op_res_gpointer (operation_result,
                                             output_key,
                                             (GDestroyNotify)
                                             g_free);
  g_simple_async_result_complete_in_idle (operation_result);
}

static void
on_caller_vanished (GDBusConnection    *connection,
                    const char         *name,
                    GSimpleAsyncResult *operation_result)
{
  GoaIdentityService *self;
  GCancellable       *cancellable;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));

  cancellable = g_object_get_data (G_OBJECT (operation_result), "cancellable");
  g_cancellable_cancel (cancellable);

  g_hash_table_remove (self->priv->watched_client_connections, name);
  g_hash_table_remove (self->priv->key_holders, name);

}

static gboolean
goa_identity_service_handle_exchange_secret_keys (GoaIdentityServiceManager *manager,
                                                  GDBusMethodInvocation     *invocation,
                                                  const char                *input_key)
{
  GoaIdentityService     *self = GOA_IDENTITY_SERVICE (manager);
  GSimpleAsyncResult     *operation_result;
  GCancellable           *cancellable;
  guint                   watch_id;
  const char             *sender;

  cancellable = g_cancellable_new ();
  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                (GAsyncReadyCallback)
                                                on_secret_keys_exchanged,
                                                NULL,
                                                g_object_ref (invocation));
  g_simple_async_result_set_check_cancellable (operation_result, cancellable);
  g_object_set_data (G_OBJECT (operation_result), "cancellable", cancellable);

  g_object_set_data_full (G_OBJECT (operation_result),
                          "input-key",
                          g_strdup (input_key),
                          (GDestroyNotify)
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
                               (GDestroyNotify)
                               g_object_unref);
  g_hash_table_insert (self->priv->watched_client_connections,
                       g_strdup (sender),
                       GUINT_TO_POINTER (watch_id));

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
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                            GOA_TYPE_IDENTITY_SERVICE,
                                            GoaIdentityServicePrivate);

  goa_debug ("GoaIdentityService: initializing");
  self->priv->watched_client_connections = g_hash_table_new_full (g_str_hash,
                                                                  g_str_equal,
                                                                  (GDestroyNotify)
                                                                  g_free,
                                                                  (GDestroyNotify)
                                                                  g_bus_unwatch_name);

  self->priv->key_holders = g_hash_table_new_full (g_str_hash,
                                                   g_str_equal,
                                                   (GDestroyNotify)
                                                   g_free,
                                                   (GDestroyNotify)
                                                   g_object_unref);
}

static void
goa_identity_service_finalize (GObject *object)
{
  GoaIdentityService *self;

  g_return_if_fail (object != NULL);
  g_return_if_fail (GOA_IS_IDENTITY_SERVICE (object));

  goa_debug ("GoaIdentityService: finalizing");

  self = GOA_IDENTITY_SERVICE (object);

  goa_identity_service_deactivate (self);

  g_clear_object (&self->priv->identity_manager);
  g_clear_object (&self->priv->object_manager_server);
  g_clear_object (&self->priv->realm_manager);
  g_clear_object (&self->priv->watched_client_connections);
  g_clear_object (&self->priv->key_holders);

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
      goa_debug ("GoaIdentityService: could not renew identity: %s",
               error->message);
      g_error_free (error);
      return;
    }

  goa_debug ("GoaIdentityService: identity renewed");
}

static void
on_identity_needs_renewal (GoaIdentityManager *identity_manager,
                           GoaIdentity        *identity,
                           GoaIdentityService *self)
{
  const char *principal;
  GoaObject  *object;

  principal = goa_identity_get_identifier (identity);

  goa_debug ("GoaIdentityService: identity %s needs renewal", principal);

  object = find_object_with_principal (self, principal, TRUE);

  if (object != NULL)
    {
      should_ignore_object (self, object);
      return;
    }

  goa_identity_manager_renew_identity (GOA_IDENTITY_MANAGER
                                       (self->priv->identity_manager),
                                       identity,
                                       NULL,
                                       (GAsyncReadyCallback)
                                       on_identity_renewed,
                                       self);
}

static void
on_identity_signed_in (GoaIdentityManager *manager,
                       GAsyncResult       *result,
                       GSimpleAsyncResult *operation_result)
{
  GError *error;
  GoaIdentity *identity;

  error = NULL;
  identity = goa_identity_manager_sign_identity_in_finish (manager, result, &error);

  if (error != NULL)
    {
      goa_debug ("GoaIdentityService: could not sign in identity: %s",
                 error->message);
      g_simple_async_result_take_error (operation_result, error);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (operation_result,
                                                 g_object_ref (identity),
                                                 (GDestroyNotify)
                                                 g_object_unref);
    }
  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);

  goa_debug ("GoaIdentityService: identity signed in");
}

static void
on_temporary_account_created_for_identity (GoaIdentityService *self,
                                           GAsyncResult       *result,
                                           GoaIdentity        *identity)
{
  GoaObject   *object;
  GError      *error;

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      const char *identifier;

      identifier = goa_identity_get_identifier (identity);
      goa_debug ("Could not add temporary account for identity %s: %s",
                 identifier,
                 error->message);
      g_error_free (error);
      return;
    }

  object = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));

  if (object != NULL)
    ensure_account_credentials (self, object);
}

static void
on_account_added (GoaManager         *manager,
                  GAsyncResult       *result,
                  GSimpleAsyncResult *operation_result)
{
  GoaIdentityService *self;
  GDBusObjectManager *object_manager;
  char *object_path;
  GoaObject *object;
  GError *error;

  self = GOA_IDENTITY_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (operation_result)));
  object_path = NULL;
  object = NULL;
  error = NULL;

  if (!goa_manager_call_add_account_finish (manager,
                                            &object_path,
                                            result,
                                            &error))
    {
      g_simple_async_result_take_error (operation_result, error);
      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  if (object_path != NULL && object_path[0] != '\0')
    {
      goa_debug ("Created account for identity with object path %s", object_path);

      object_manager = goa_client_get_object_manager (self->priv->client);
      object = GOA_OBJECT (g_dbus_object_manager_get_object (object_manager,
                                                             object_path));
      g_free (object_path);
    }

  if (object == NULL)
    g_simple_async_result_set_op_res_gpointer (operation_result, NULL, NULL);
  else
    g_simple_async_result_set_op_res_gpointer (operation_result,
                                               object,
                                               (GDestroyNotify)
                                               g_object_unref);

  g_simple_async_result_complete_in_idle (operation_result);
  g_object_unref (operation_result);
}

static void
add_temporary_account (GoaIdentityService *self,
                       GoaIdentity        *identity)
{
  char               *realm;
  const char         *principal;
  const char         *principal_for_display;
  GSimpleAsyncResult *operation_result;
  GVariantBuilder     credentials;
  GVariantBuilder     details;

  principal = goa_identity_get_identifier (identity);

  goa_debug ("GoaIdentityService: adding temporary identity %s", principal);

  /* If there's no account for this identity then create a temporary one.
   */
  principal_for_display = goa_identity_manager_name_identity (self->priv->identity_manager,
                                                              identity);

  realm = goa_kerberos_identity_get_realm_name (GOA_KERBEROS_IDENTITY (identity));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Realm", realm);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "true");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");


  goa_debug ("GoaIdentityService: asking to sign back in");

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                (GAsyncReadyCallback)
                                                on_temporary_account_created_for_identity,
                                                identity,
                                                add_temporary_account);

  goa_manager_call_add_account (self->priv->accounts_manager,
                                "kerberos",
                                principal,
                                principal_for_display,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL,
                                (GAsyncReadyCallback)
                                on_account_added,
                                operation_result);
  g_free (realm);
}

static void
on_identity_added (GoaIdentityManager *identity_manager,
                   GoaIdentity        *identity,
                   GoaIdentityService *self)
{
  GoaObject *object;
  const char *identifier;

  export_identity (self, identity);

  identifier = goa_identity_get_identifier (identity);

  look_up_realm (self, identifier, NULL, NULL, NULL, NULL);

  object = find_object_with_principal (self, identifier, FALSE);

  if (object == NULL)
    add_temporary_account (self, identity);
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

  g_signal_handlers_disconnect_by_func (G_OBJECT (manager),
                                        G_CALLBACK (close_system_prompt),
                                        data);
  error = NULL;
  if (!gcr_system_prompt_close (data->prompt, NULL, &error))
    {
      if (error != NULL)
        {
          goa_debug ("GoaIdentityService: could not close system prompt: %s",
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
          goa_debug ("GoaIdentityService: could not get password from user: %s",
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

  close_system_prompt (self->priv->identity_manager, identity, request);
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

  name = goa_identity_manager_name_identity (self->priv->identity_manager, identity);

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

  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-refreshed",
                    G_CALLBACK (close_system_prompt),
                    request);

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
          goa_debug ("GoaIdentityService: could not open system prompt: %s",
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
                GSimpleAsyncResult *operation_result)
{
  GoaIdentity *operation_identity;

  operation_identity = g_simple_async_result_get_source_tag (operation_result);
  if (operation_identity == identity)
    {
      GCancellable *cancellable;

      cancellable = g_object_get_data (G_OBJECT (operation_result),
                                       "cancellable");
      g_cancellable_cancel (cancellable);
    }
}

static void
sign_in (GoaIdentityService     *self,
         const char             *identifier,
         UmRealmObject          *realm,
         gconstpointer           initial_password,
         GoaIdentitySignInFlags  flags,
         GCancellable           *cancellable,
         GAsyncReadyCallback     callback,
         gpointer                user_data)
{
  GSimpleAsyncResult *operation_result;

  goa_debug ("GoaIdentityService: asking to sign in");

  operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                callback,
                                                user_data,
                                                realm);
  g_simple_async_result_set_check_cancellable (operation_result, cancellable);

  g_object_set_data (G_OBJECT (operation_result),
                     "cancellable",
                     cancellable);
  g_signal_connect_object (G_OBJECT (self->priv->identity_manager),
                           "identity-refreshed",
                           G_CALLBACK (cancel_sign_in),
                           operation_result,
                           0);

  goa_identity_manager_sign_identity_in (self->priv->identity_manager,
                                         identifier,
                                         initial_password,
                                         flags,
                                         (GoaIdentityInquiryFunc)
                                         on_identity_inquiry,
                                         self,
                                         cancellable,
                                         (GAsyncReadyCallback)
                                         on_identity_signed_in,
                                         operation_result);
}

static void
on_identity_expiring (GoaIdentityManager *identity_manager,
                      GoaIdentity        *identity,
                      GoaIdentityService *self)
{
  const char *principal;
  GoaObject  *object;

  principal = goa_identity_get_identifier (identity);

  goa_debug ("GoaIdentityService: identity %s expiring", principal);

  object = find_object_with_principal (self, principal, TRUE);

  if (object == NULL)
    return;

  ensure_account_credentials (self, object);
}

static void
on_identity_expired (GoaIdentityManager *identity_manager,
                     GoaIdentity        *identity,
                     GoaIdentityService *self)
{
  const char *principal;
  GoaObject  *object;

  principal = goa_identity_get_identifier (identity);

  goa_debug ("GoaIdentityService: identity %s expired", principal);

  object = find_object_with_principal (self, principal, TRUE);

  if (object == NULL)
    return;

  ensure_account_credentials (self, object);
}

static void
on_sign_out_for_account_change_done (GoaIdentityService *self,
                                     GAsyncResult       *result)
{
  GError *error = NULL;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      goa_debug ("Log out failed: %s", error->message);
      g_error_free (error);
    }
  else
    {
      goa_debug ("Log out complete");
    }
}

static void
on_ticketing_done (GoaIdentityService *self,
                   GAsyncResult       *result)
{
  GoaObject *object;

  object = g_simple_async_result_get_source_tag (G_SIMPLE_ASYNC_RESULT (result));

  ensure_account_credentials (self, object);
}

static void
on_got_ticket (GoaTicketing       *ticketing,
               GAsyncResult       *result,
               GSimpleAsyncResult *operation_result)
{
  GoaObject          *object;
  GoaAccount         *account;
  GError             *error;
  const char         *account_identity;

  object = g_simple_async_result_get_source_tag (operation_result);
  account = goa_object_peek_account (object);
  account_identity = goa_account_get_identity (account);

  error = NULL;
  if (!goa_ticketing_call_get_ticket_finish (ticketing,
                                             result,
                                             &error))
    {
      goa_debug ("GoaIdentityService: could not get ticket for account %s: %s",
                 account_identity,
                 error->message);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (operation_result);
      g_object_unref (operation_result);
      return;
    }

  goa_debug ("GoaIdentityService: got ticket for account %s",
             account_identity);
  g_simple_async_result_complete_in_idle (operation_result);
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

      GSimpleAsyncResult *operation_result;

      operation_result = g_simple_async_result_new (G_OBJECT (self),
                                                    (GAsyncReadyCallback)
                                                    on_ticketing_done,
                                                    NULL,
                                                    object);
      /* Ticketing interface is present, sign in if not already
       * signed in.
       */
      goa_ticketing_call_get_ticket (ticketing,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_ticket,
                                     operation_result);
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
  GSimpleAsyncResult *result;

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

  goa_debug ("Kerberos account %s was disabled and should now be signed out", account_identity);

  result = g_simple_async_result_new (G_OBJECT (self),
                                      (GAsyncReadyCallback)
                                      on_sign_out_for_account_change_done,
                                      NULL,
                                      on_account_interface_removed);

  goa_identity_manager_get_identity (self->priv->identity_manager,
                                     account_identity,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_identity_for_sign_out,
                                     result);
}

static void
on_account_removed (GoaClient          *client,
                    GoaObject          *object,
                    GoaIdentityService *self)
{
  GSimpleAsyncResult *result;
  GoaAccount         *account;
  const char         *provider_type;
  const char         *account_identity;

  account = goa_object_peek_account (object);

  if (account == NULL)
    return;

  provider_type = goa_account_get_provider_type (account);

  if (g_strcmp0 (provider_type, "kerberos") != 0)
    return;

  account_identity = goa_account_get_identity (account);

  goa_debug ("Kerberos account %s removed and should now be signed out", account_identity);

  result = g_simple_async_result_new (G_OBJECT (self),
                                      (GAsyncReadyCallback)
                                      on_sign_out_for_account_change_done,
                                      NULL,
                                      on_account_removed);

  goa_identity_manager_get_identity (self->priv->identity_manager,
                                     account_identity,
                                     NULL,
                                     (GAsyncReadyCallback)
                                     on_got_identity_for_sign_out,
                                     result);
}

static void
on_identities_listed (GoaIdentityManager *manager,
                      GAsyncResult       *result,
                      GoaIdentityService *self)
{
  GError *error = NULL;
  GList *identities, *node;

  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-added",
                    G_CALLBACK (on_identity_added),
                    self);
  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-removed",
                    G_CALLBACK (on_identity_removed),
                    self);
  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-refreshed",
                    G_CALLBACK (on_identity_refreshed),
                    self);
  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-needs-renewal",
                    G_CALLBACK (on_identity_needs_renewal),
                    self);
  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-expiring",
                    G_CALLBACK (on_identity_expiring),
                    self);
  g_signal_connect (G_OBJECT (self->priv->identity_manager),
                    "identity-expired",
                    G_CALLBACK (on_identity_expired),
                    self);

  g_signal_connect (G_OBJECT (self->priv->client),
                    "account-removed",
                    G_CALLBACK (on_account_removed),
                    self);

  identities = goa_identity_manager_list_identities_finish (manager, result, &error);

  if (identities == NULL)
    {
      if (error != NULL)
        {
          goa_warning ("Could not list identities: %s", error->message);
          g_error_free (error);
        }
      return;
    }

  for (node = identities; node != NULL; node = node->next)
    {
      GoaIdentity *identity = node->data;
      const char  *principal;
      GoaObject   *object;

      export_identity (self, identity);

      principal = goa_identity_get_identifier (identity);

      look_up_realm (self, principal, NULL, NULL, NULL, NULL);

      object = find_object_with_principal (self, principal, TRUE);

      if (object == NULL)
        add_temporary_account (self, identity);
      else
        g_object_unref (object);
    }
}

static void
ensure_credentials_for_accounts (GoaIdentityService *self)
{
  GDBusObjectManager *object_manager;
  GList      *accounts;
  GList      *node;

  object_manager = goa_client_get_object_manager (self->priv->client);

  g_signal_connect (G_OBJECT (object_manager),
                    "interface-added",
                    G_CALLBACK (on_account_interface_added),
                    self);
  g_signal_connect (G_OBJECT (object_manager),
                    "interface-removed",
                    G_CALLBACK (on_account_interface_removed),
                    self);

  accounts = goa_client_get_accounts (self->priv->client);

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
}

static void
on_got_client (GoaClient          *client,
               GAsyncResult       *result,
               GoaIdentityService *self)
{
  GError *error;

  error = NULL;

  self->priv->client = goa_client_new_finish (result, &error);

  if (self->priv->client == NULL)
    {
      goa_warning ("Could not create client: %s", error->message);
      return;
    }

  self->priv->accounts_manager = goa_client_get_manager (client);

  self->priv->identity_manager = goa_kerberos_identity_manager_new (NULL, &error);

  if (self->priv->identity_manager == NULL)
    {
      goa_warning ("Could not create identity manager: %s", error->message);
      return;
    }

  goa_identity_manager_list_identities (self->priv->identity_manager,
                                        NULL,
                                        (GAsyncReadyCallback)
                                        on_identities_listed,
                                        self);

  ensure_credentials_for_accounts (self);
}

static void
on_session_bus_acquired (GDBusConnection    *connection,
                         const char         *unique_name,
                         GoaIdentityService *self)
{
  goa_debug ("GoaIdentityService: Connected to session bus");

  if (self->priv->connection == NULL)
  {
    self->priv->connection = g_object_ref (connection);

    g_dbus_object_manager_server_set_connection (self->priv->object_manager_server,
                                                 self->priv->connection);

    goa_client_new (NULL,
                    (GAsyncReadyCallback)
                    on_got_client,
                    self);
  }
}

static void
on_name_acquired (GDBusConnection    *connection,
                  const char         *name,
                  GoaIdentityService *self)
{
  if (g_strcmp0 (name, "org.gnome.Identity") == 0)
    goa_debug ("GoaIdentityService: Acquired name org.gnome.Identity");
}

static void
on_name_lost (GDBusConnection    *connection,
              const char         *name,
              GoaIdentityService *self)
{
  if (g_strcmp0 (name, "org.gnome.Identity") == 0)
    goa_debug ("GoaIdentityService: Lost name org.gnome.Identity");
}

static void
on_realm_manager_ensured_for_auto_discovery (GoaIdentityService *self,
                                             GAsyncResult       *result)
{
  GError *error;

  error = NULL;
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), &error))
    {
      goa_debug ("GoaIdentityService: could not auto-discover available realms: %s",
                 error->message);
      return;
    }

  goa_debug ("GoaIdentityService: auto-discovering available realms");

  /* Trigger realm-added signal for all discovered realms */
  um_realm_manager_discover (self->priv->realm_manager,
                             "",
                             self->priv->cancellable,
                             NULL,
                             NULL);
}

static void
on_realmd_appeared (GDBusConnection    *connection,
                    const gchar        *name,
                    const gchar        *name_owner,
                    GoaIdentityService *self)
{
  ensure_realm_manager (self,
                        NULL,
                        (GAsyncReadyCallback)
                        on_realm_manager_ensured_for_auto_discovery,
                        NULL);
}

static void
on_realmd_disappeared (GDBusConnection    *connection,
                       const gchar        *name,
                       GoaIdentityService *self)
{
  if (self->priv->realm_manager)
    {
      g_signal_handlers_disconnect_by_func (self->priv->realm_manager,
                                            on_manager_realm_added,
                                            self);
      g_clear_object (&self->priv->realm_manager);
    }
}

gboolean
goa_identity_service_activate (GoaIdentityService   *self,
                               GError              **error)
{
  GoaIdentityServiceObjectSkeleton *object;

  g_return_val_if_fail (GOA_IS_IDENTITY_SERVICE (self), FALSE);

  goa_debug ("GoaIdentityService: Activating identity service");

  self->priv->cancellable = g_cancellable_new ();

  self->priv->object_manager_server =
    g_dbus_object_manager_server_new ("/org/gnome/Identity");

  object = goa_identity_service_object_skeleton_new ("/org/gnome/Identity/Manager");
  goa_identity_service_object_skeleton_set_manager (object,
                                                    GOA_IDENTITY_SERVICE_MANAGER (self));

  g_dbus_object_manager_server_export (self->priv->object_manager_server,
                                       G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  self->priv->bus_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       "org.gnome.Identity",
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       (GBusAcquiredCallback) on_session_bus_acquired,
                                       (GBusNameAcquiredCallback) on_name_acquired,
                                       (GBusNameVanishedCallback) on_name_lost,
                                       self,
                                       NULL);

  self->priv->realmd_watch = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                               "org.freedesktop.realmd",
                                               G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
                                               (GBusNameAppearedCallback)
                                               on_realmd_appeared,
                                               (GBusNameVanishedCallback)
                                               on_realmd_disappeared,
                                               self,
                                               NULL);

  return TRUE;
}

void
goa_identity_service_deactivate (GoaIdentityService *self)
{
  goa_debug ("GoaIdentityService: Deactivating identity service");

  if (self->priv->realmd_watch == 0)
    g_bus_unwatch_name (self->priv->realmd_watch);

  if (self->priv->identity_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self, on_identity_needs_renewal, self);
      g_signal_handlers_disconnect_by_func (self, on_identity_expiring, self);
      g_signal_handlers_disconnect_by_func (self, on_identity_expired, self);
      g_clear_object (&self->priv->identity_manager);
    }

  g_clear_object (&self->priv->object_manager_server);
  g_clear_object (&self->priv->connection);
  g_clear_object (&self->priv->client);
  g_clear_object (&self->priv->cancellable);
}

static void
goa_identity_service_class_init (GoaIdentityServiceClass *service_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (service_class);

  object_class->finalize = goa_identity_service_finalize;

  goa_identity_utils_register_error_domain (GOA_IDENTITY_ERROR, GOA_TYPE_IDENTITY_ERROR);
  goa_identity_utils_register_error_domain (GOA_IDENTITY_MANAGER_ERROR, GOA_TYPE_IDENTITY_MANAGER_ERROR);

  g_type_class_add_private (service_class, sizeof (GoaIdentityServicePrivate));
}

GoaIdentityService *
goa_identity_service_new (void)
{
  GObject *object;

  object = g_object_new (GOA_TYPE_IDENTITY_SERVICE,
                         NULL);

  return GOA_IDENTITY_SERVICE (object);
}
