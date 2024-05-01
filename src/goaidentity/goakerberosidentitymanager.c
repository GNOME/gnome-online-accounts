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

#define _GNU_SOURCE 1

#include "goakerberosidentitymanager.h"
#include "goaidentitymanager.h"
#include "goaidentitymanagererror.h"
#include "goaidentitymanagerprivate.h"
#include "goakerberosidentityinquiry.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include <krb5.h>

#ifdef __linux__
#include <keyutils.h>
#include "goalinuxnotificationstream.h"
#include "goalinuxwatchqueue-priv.h"
#endif /* __linux__ */

struct _GoaKerberosIdentityManager
{
  GObject parent_instance;

  GHashTable *identities;
  GHashTable *expired_identities;
  GHashTable *identities_by_realm;
  GCancellable *scheduler_cancellable;
  GThreadPool *thread_pool;

  krb5_context kerberos_context;
  GFileMonitor *credentials_cache_file_monitor;
  char *credentials_cache_type;

  GMutex scheduler_job_lock;
  GCond scheduler_job_unblocked;
  gboolean is_blocking_scheduler_job;

  volatile int pending_refresh_count;

  guint credentials_cache_keyring_notification_id;
  guint credentials_cache_polling_timeout_id;
};

typedef enum
{
  OPERATION_TYPE_REFRESH,
  OPERATION_TYPE_GET_IDENTITY,
  OPERATION_TYPE_LIST,
  OPERATION_TYPE_RENEW,
  OPERATION_TYPE_SIGN_IN,
  OPERATION_TYPE_SIGN_OUT
} OperationType;

typedef struct
{
  GCancellable *cancellable;
  GCancellable *scheduler_cancellable;
  GoaKerberosIdentityManager *manager;
  OperationType type;
  GMainContext *context;
  GTask *task;

  union
  {
    GoaIdentity *identity;
    struct
    {
      char *identifier;
      gconstpointer initial_password;
      char *preauth_source;
      GoaIdentitySignInFlags sign_in_flags;
      GoaIdentityInquiry *inquiry;
      GoaIdentityInquiryFunc inquiry_func;
      gpointer inquiry_data;
      GMutex inquiry_lock;
      GCond inquiry_finished_condition;
      volatile gboolean is_inquiring;
    };
  };
} Operation;

typedef struct
{
  GoaKerberosIdentityManager *manager;
  GoaIdentity *identity;
} IdentitySignalWork;

static GoaIdentityManager *goa_kerberos_identity_manager_singleton;

static void identity_manager_interface_init (GoaIdentityManagerInterface *
                                             interface);
static void initable_interface_init (GInitableIface *interface);

static void on_identity_expired (GoaIdentity                *identity,
                                 GoaKerberosIdentityManager *self);

G_DEFINE_TYPE_WITH_CODE (GoaKerberosIdentityManager,
                         goa_kerberos_identity_manager,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GOA_TYPE_IDENTITY_MANAGER,
                                                identity_manager_interface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_interface_init));
#define FALLBACK_POLLING_INTERVAL 5

static Operation *
operation_new (GoaKerberosIdentityManager *self,
               GCancellable               *cancellable,
               OperationType               type,
               GTask                      *task)
{
  Operation *operation;

  operation = g_slice_new0 (Operation);

  operation->manager = self;
  operation->scheduler_cancellable = g_object_ref (self->scheduler_cancellable);

  operation->type = type;

  if (cancellable == NULL)
    cancellable = g_cancellable_new ();
  else
    g_object_ref (cancellable);
  operation->cancellable = cancellable;

  operation->context = g_main_context_ref_thread_default ();

  if (task != NULL)
    g_object_ref (task);
  operation->task = task;

  operation->identity = NULL;

  return operation;
}

static void
operation_free (Operation *operation)
{
  g_clear_object (&operation->cancellable);
  g_clear_object (&operation->scheduler_cancellable);
  g_clear_pointer (&operation->context, g_main_context_unref);

  if (operation->type != OPERATION_TYPE_SIGN_IN &&
      operation->type != OPERATION_TYPE_GET_IDENTITY)
    {
      g_clear_object (&operation->identity);
    }
  else
    {
      g_clear_pointer (&operation->identifier, g_free);
      g_clear_pointer (&operation->preauth_source, g_free);
    }
  g_clear_object (&operation->task);

  g_slice_free (Operation, operation);
}

static GSource *
goa_kerberos_identity_manager_keyring_source_new (GError **error)
{
  GSource *ret = NULL;
  gint fds[2] = { -1, -1 };

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

#if !defined(__linux__)
  {
    g_set_error_literal (error, G_UNIX_ERROR, 0, "KEYRING only supported on Linux");
    return NULL;
  }
#elif !defined(HAVE_PIPE2)
  {
    g_set_error_literal (error, G_UNIX_ERROR, 0, "pipe2(2) not supported");
    return NULL;
  }
#else
  {
    g_autoptr (GInputStream) stream = NULL;
    gint error_code;

    error_code = pipe2 (fds, O_NOTIFICATION_PIPE);
    if (error_code == -1)
      {
        const gchar *error_str;

        error_str = g_strerror (errno);
        g_set_error_literal (error, G_UNIX_ERROR, 0, error_str);
        goto out;
      }

    error_code = ioctl (fds[0], IOC_WATCH_QUEUE_SET_SIZE, WATCH_QUEUE_BUFFER_SIZE);
    if (error_code == -1)
      {
        const gchar *error_str;

        error_str = g_strerror (errno);
        g_set_error_literal (error, G_UNIX_ERROR, 0, error_str);
        goto out;
      }

    error_code = ioctl(fds[0], IOC_WATCH_QUEUE_SET_FILTER, &watch_queue_notification_filter);
    if (error_code == -1)
      {
        const gchar *error_str;

        error_str = g_strerror (errno);
        g_set_error_literal (error, G_UNIX_ERROR, 0, error_str);
        goto out;
      }

    error_code = keyctl_watch_key (KEY_SPEC_SESSION_KEYRING, fds[0], 0x01);
    if (error_code == -1)
      {
        const gchar *error_str;

        error_str = g_strerror (errno);
        g_set_error_literal (error, G_UNIX_ERROR, 0, error_str);
        goto out;
      }

    error_code = keyctl_watch_key (KEY_SPEC_USER_KEYRING, fds[0], 0x02);
    if (error_code == -1)
      {
        const gchar *error_str;

        error_str = g_strerror (errno);
        g_set_error_literal (error, G_UNIX_ERROR, 0, error_str);
        goto out;
      }

    stream = goa_linux_notification_stream_new (fds[0], fds[1]);
    ret = g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM (stream), NULL);
  }
#endif

 out:
  if (ret == NULL)
    {
      g_close (fds[0], NULL);
      g_close (fds[1], NULL);
    }

  return ret;
}

typedef struct {
  GSourceFunc func;
  gboolean ret_val;
  gpointer data;
  GDestroyNotify notify;

  GMutex ack_lock;
  GCond ack_condition;
  gboolean ack;
} MainLoopProxy;

static gboolean
mainloop_proxy_func (gpointer data)
{
  MainLoopProxy *proxy = data;

  proxy->ret_val = proxy->func (proxy->data);

  if (proxy->notify)
    proxy->notify (proxy->data);

  g_mutex_lock (&proxy->ack_lock);
  proxy->ack = TRUE;
  g_cond_signal (&proxy->ack_condition);
  g_mutex_unlock (&proxy->ack_lock);

  return FALSE;
}

static void
mainloop_proxy_free (MainLoopProxy *proxy)
{
  g_mutex_clear (&proxy->ack_lock);
  g_cond_clear (&proxy->ack_condition);
  g_free (proxy);
}

static gboolean
goa_kerberos_identify_manager_send_to_context (GMainContext   *context,
                                               GSourceFunc     func,
                                               gpointer        user_data,
                                               GDestroyNotify  notify)
{
  GSource *source;
  MainLoopProxy *proxy;
  gboolean ret_val;

  g_return_val_if_fail (context != NULL, FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  proxy = g_new0 (MainLoopProxy, 1);
  proxy->func = func;
  proxy->data = user_data;
  proxy->notify = notify;
  g_mutex_init (&proxy->ack_lock);
  g_cond_init (&proxy->ack_condition);
  g_mutex_lock (&proxy->ack_lock);

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, mainloop_proxy_func, proxy, NULL);
  g_source_set_name (source, "[goa] mainloop_proxy_func");

  g_source_attach (source, context);
  g_source_unref (source);

  while (!proxy->ack)
    g_cond_wait (&proxy->ack_condition, &proxy->ack_lock);
  g_mutex_unlock (&proxy->ack_lock);

  ret_val = proxy->ret_val;
  mainloop_proxy_free (proxy);

  return ret_val;
}

static void
schedule_refresh (GoaKerberosIdentityManager *self)
{
  Operation *operation;

  g_atomic_int_inc (&self->pending_refresh_count);

  operation = operation_new (self, NULL, OPERATION_TYPE_REFRESH, NULL);
  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static IdentitySignalWork *
identity_signal_work_new (GoaKerberosIdentityManager *self,
                          GoaIdentity                *identity)
{
  IdentitySignalWork *work;

  work = g_slice_new (IdentitySignalWork);
  work->manager = self;
  work->identity = g_object_ref (identity);

  return work;
}

static void
identity_signal_work_free (IdentitySignalWork *work)
{
  g_object_unref (work->identity);
  g_slice_free (IdentitySignalWork, work);
}

static void
do_identity_signal_expired_work (IdentitySignalWork *work)
{
  GoaKerberosIdentityManager *self = work->manager;
  GoaIdentity *identity = work->identity;

  g_debug ("GoaKerberosIdentityManager: identity expired");
  _goa_identity_manager_emit_identity_expired (GOA_IDENTITY_MANAGER (self), identity);
}

static void
on_identity_expired (GoaIdentity                *identity,
                     GoaKerberosIdentityManager *self)
{
  IdentitySignalWork *work;

  work = identity_signal_work_new (self, identity);
  goa_kerberos_identify_manager_send_to_context (g_main_context_default (),
                                                 (GSourceFunc)
                                                 do_identity_signal_expired_work,
                                                 work,
                                                 (GDestroyNotify)
                                                 identity_signal_work_free);
}

static void
on_identity_unexpired (GoaIdentity                *identity,
                       GoaKerberosIdentityManager *self)
{
  g_debug ("GoaKerberosIdentityManager: identity unexpired");
  /* If an identity is now unexpired, that means some sort of weird
   * clock skew happened and we should just do a full refresh, since it's
   * probably affected more than one identity
   */
  schedule_refresh (self);
}

static void
on_identity_expiring (GoaIdentity                *identity,
                      GoaKerberosIdentityManager *self)
{
  g_debug ("GoaKerberosIdentityManager: identity about to expire");
  _goa_identity_manager_emit_identity_expiring (GOA_IDENTITY_MANAGER (self),
                                                identity);
}

static void
on_identity_needs_renewal (GoaIdentity                *identity,
                           GoaKerberosIdentityManager *self)
{
  g_debug ("GoaKerberosIdentityManager: identity needs renewal");
  _goa_identity_manager_emit_identity_needs_renewal (GOA_IDENTITY_MANAGER (self),
                                                     identity);
}

static void
on_identity_needs_refresh (GoaIdentity                *identity,
                           GoaKerberosIdentityManager *self)
{
  g_debug ("GoaKerberosIdentityManager: needs refresh");
  schedule_refresh (self);
}

static void
watch_for_identity_expiration (GoaKerberosIdentityManager *self,
                               GoaIdentity                *identity)
{
  g_signal_handlers_disconnect_by_func (G_OBJECT (identity),
                                        G_CALLBACK (on_identity_expired),
                                        self);
  g_signal_connect (G_OBJECT (identity),
                    "expired",
                    G_CALLBACK (on_identity_expired),
                    self);

  g_signal_handlers_disconnect_by_func (G_OBJECT (identity),
                                        G_CALLBACK (on_identity_unexpired),
                                        self);
  g_signal_connect (G_OBJECT (identity),
                    "unexpired",
                    G_CALLBACK (on_identity_unexpired),
                    self);

  g_signal_handlers_disconnect_by_func (G_OBJECT (identity),
                                        G_CALLBACK (on_identity_expiring),
                                        self);
  g_signal_connect (G_OBJECT (identity),
                    "expiring",
                    G_CALLBACK (on_identity_expiring),
                    self);

  g_signal_handlers_disconnect_by_func (G_OBJECT (identity),
                                        G_CALLBACK (on_identity_needs_renewal),
                                        self);
  g_signal_connect (G_OBJECT (identity),
                    "needs-renewal",
                    G_CALLBACK (on_identity_needs_renewal),
                    self);

  g_signal_handlers_disconnect_by_func (G_OBJECT (identity),
                                        G_CALLBACK (on_identity_needs_refresh),
                                        self);
  g_signal_connect (G_OBJECT (identity),
                    "needs-refresh",
                    G_CALLBACK (on_identity_needs_refresh),
                    self);
}

static void
do_identity_signal_added_work (IdentitySignalWork *work)
{
  GoaKerberosIdentityManager *self = work->manager;
  GoaIdentity *identity = work->identity;

  watch_for_identity_expiration (self, identity);
  _goa_identity_manager_emit_identity_added (GOA_IDENTITY_MANAGER (self), identity);
}

static void
do_identity_signal_removed_work (IdentitySignalWork *work)
{
  GoaKerberosIdentityManager *self = work->manager;
  GoaIdentity *identity = work->identity;

  _goa_identity_manager_emit_identity_removed (GOA_IDENTITY_MANAGER (self),
                                               identity);
}

static void
do_identity_signal_renamed_work (IdentitySignalWork *work)
{
  GoaKerberosIdentityManager *self = work->manager;
  GoaIdentity *identity = work->identity;

  _goa_identity_manager_emit_identity_renamed (GOA_IDENTITY_MANAGER (self),
                                               identity);
}

static void
do_identity_signal_refreshed_work (IdentitySignalWork *work)
{
  GoaKerberosIdentityManager *self = work->manager;
  GoaIdentity *identity = work->identity;

  watch_for_identity_expiration (self, identity);
  _goa_identity_manager_emit_identity_refreshed (GOA_IDENTITY_MANAGER (self),
                                                 identity);
}

static void
remove_identity (GoaKerberosIdentityManager *self,
                 Operation                  *operation,
                 GoaIdentity                *identity)
{
  IdentitySignalWork *work;
  const char *identifier;
  char *name;
  GList *other_identities = NULL;

  identifier = goa_identity_get_identifier (identity);
  name = goa_kerberos_identity_get_realm_name (GOA_KERBEROS_IDENTITY (identity));

  if (name != NULL)
    {
      other_identities = g_hash_table_lookup (self->identities_by_realm, name);
      g_hash_table_remove (self->identities_by_realm, name);

      other_identities = g_list_remove (other_identities, identity);
    }


  if (other_identities != NULL)
    g_hash_table_replace (self->identities_by_realm, g_strdup (name), other_identities);

  g_free (name);

  work = identity_signal_work_new (self, identity);
  g_hash_table_remove (self->expired_identities, identifier);
  g_hash_table_remove (self->identities, identifier);

  goa_kerberos_identify_manager_send_to_context (operation->context,
                                                 (GSourceFunc)
                                                 do_identity_signal_removed_work,
                                                 work,
                                                 (GDestroyNotify) identity_signal_work_free);
  /* If there's only one identity for this realm now, then we can
   * rename that identity to just the realm name
   */
  if (other_identities != NULL && other_identities->next == NULL)
    {
      GoaIdentity *other_identity = other_identities->data;

      work = identity_signal_work_new (self, other_identity);

      goa_kerberos_identify_manager_send_to_context (operation->context,
                                                     (GSourceFunc)
                                                     do_identity_signal_renamed_work,
                                                     work,
                                                     (GDestroyNotify)
                                                     identity_signal_work_free);
    }
}

static void
drop_stale_identities (GoaKerberosIdentityManager *self,
                       Operation                  *operation,
                       GHashTable                 *known_identities)
{
  GList *stale_identity_ids;
  GList *node;

  stale_identity_ids = g_hash_table_get_keys (self->identities);

  node = stale_identity_ids;
  while (node != NULL)
    {
      GoaIdentity *identity;
      const char *identifier = node->data;

      identity = g_hash_table_lookup (known_identities, identifier);
      if (identity == NULL)
        {
          identity = g_hash_table_lookup (self->identities, identifier);

          if (identity != NULL)
            {
              remove_identity (self, operation, identity);
            }
        }
      node = node->next;
    }
  g_list_free (stale_identity_ids);
}

static void
add_identity (GoaKerberosIdentityManager *self,
              Operation                  *operation,
              GoaIdentity                *identity,
              const char                 *identifier)
{
  IdentitySignalWork *work;

  g_hash_table_replace (self->identities, g_strdup (identifier), g_object_ref (identity));

  if (!goa_identity_is_signed_in (identity))
    g_hash_table_replace (self->expired_identities, g_strdup (identifier), identity);

  work = identity_signal_work_new (self, identity);
  goa_kerberos_identify_manager_send_to_context (operation->context,
                                                 (GSourceFunc)
                                                 do_identity_signal_added_work,
                                                 work,
                                                 (GDestroyNotify) identity_signal_work_free);
}

static char *
get_principal_from_cache (GoaKerberosIdentityManager *self,
                          krb5_ccache                 cache)
{
  int error_code;
  krb5_principal principal;
  char *unparsed_name;
  char *principal_name;

  error_code = krb5_cc_get_principal (self->kerberos_context, cache, &principal);

  error_code = krb5_unparse_name_flags (self->kerberos_context, principal, 0, &unparsed_name);
  krb5_free_principal (self->kerberos_context, principal);

  if (error_code != 0)
    return NULL;

  principal_name = g_strdup (unparsed_name);

  krb5_free_unparsed_name (self->kerberos_context, unparsed_name);

  return principal_name;
}

static void
import_credentials_cache (GoaKerberosIdentityManager *self,
                          Operation                  *operation,
                          GHashTable                 *refreshed_identities,
                          krb5_ccache                 cache)
{
  g_autofree char *identifier = NULL;
  GoaIdentity *identity = NULL;

  identifier = get_principal_from_cache (self, cache);

  if (identifier == NULL)
    return;

  identity = g_hash_table_lookup (self->identities, identifier);

  if (identity == NULL)
    {
      g_autoptr(GError) error = NULL;

      g_debug ("GoaKerberosIdentityManager: Adding new identity '%s'", identifier);
      identity = goa_kerberos_identity_new (self->kerberos_context, cache, &error);

      if (error != NULL)
        {
          g_debug ("GoaKerberosIdentityManager: Could not track identity %s: %s",
                   identifier, error->message);
          return;
        }

      add_identity (self, operation, identity, identifier);
    }
  else
    {
      if (!goa_kerberos_identity_has_credentials_cache (GOA_KERBEROS_IDENTITY (identity), cache))
        goa_kerberos_identity_add_credentials_cache (GOA_KERBEROS_IDENTITY (identity), cache);
    }

  /* Track refreshed identities so we can emit refreshes and removals when we're done fully
   * enumerating the collection of credential caches
   */
  g_hash_table_replace (refreshed_identities,
                        g_strdup (identifier),
                        g_object_ref (identity));
}

static gboolean
refresh_identities (GoaKerberosIdentityManager *self,
                    Operation                  *operation)
{
  krb5_error_code error_code;
  krb5_ccache cache;
  krb5_cccol_cursor cursor;
  const char *error_message;
  GHashTable *refreshed_identities;
  GHashTableIter iter;
  const char *name;
  GoaIdentity *identity;

  /* If we have more refreshes queued up, don't bother doing this one
   */
  if (!g_atomic_int_dec_and_test (&self->pending_refresh_count))
    {
      return FALSE;
    }

  g_debug ("GoaKerberosIdentityManager: Refreshing identities");
  refreshed_identities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  error_code = krb5_cccol_cursor_new (self->kerberos_context, &cursor);

  if (error_code != 0)
    {
      error_message = krb5_get_error_message (self->kerberos_context, error_code);
      g_debug ("GoaKerberosIdentityManager: Error looking up available credential caches: %s",
               error_message);
      krb5_free_error_message (self->kerberos_context, error_message);
      goto done;
    }

  error_code = krb5_cccol_cursor_next (self->kerberos_context, cursor, &cache);

  while (error_code == 0 && cache != NULL)
    {
      import_credentials_cache (self, operation, refreshed_identities, cache);

      krb5_cc_close (self->kerberos_context, cache);
      error_code = krb5_cccol_cursor_next (self->kerberos_context, cursor, &cache);
    }

  if (error_code != 0)
    {
      error_message = krb5_get_error_message (self->kerberos_context, error_code);
      g_debug ("GoaKerberosIdentityManager: Error iterating over available credential caches: %s",
               error_message);
      krb5_free_error_message (self->kerberos_context, error_message);
    }

  krb5_cccol_cursor_free (self->kerberos_context, &cursor);

  g_hash_table_iter_init (&iter, self->identities);
  while (g_hash_table_iter_next (&iter, (gpointer *) &name, (gpointer*) &identity))
    {
      goa_kerberos_identity_refresh (GOA_KERBEROS_IDENTITY (identity));

      if (goa_identity_is_signed_in (identity))
        {
          IdentitySignalWork *work;

          /* if it's not expired, send out a refresh signal */
          g_debug ("GoaKerberosIdentityManager: identity '%s' refreshed",
                   goa_identity_get_identifier (identity));

          work = identity_signal_work_new (self, identity);
          goa_kerberos_identify_manager_send_to_context (operation->context,
                                                         (GSourceFunc)
                                                         do_identity_signal_refreshed_work,
                                                         work,
                                                         (GDestroyNotify)
                                                         identity_signal_work_free);
        }
    }

done:
  drop_stale_identities (self, operation, refreshed_identities);
  g_hash_table_unref (refreshed_identities);

  return TRUE;
}

static int
identity_sort_func (GoaIdentity *a,
                    GoaIdentity *b)
{
  return g_strcmp0 (goa_identity_get_identifier (a),
                    goa_identity_get_identifier (b));
}

static void
free_identity_list (GList *list)
{
  g_list_free_full (list, g_object_unref);
}

static void
list_identities (GoaKerberosIdentityManager *self,
                 Operation                  *operation)
{
  GList *identities;

  g_debug ("GoaKerberosIdentityManager: Listing identities");
  identities = g_hash_table_get_values (self->identities);

  identities = g_list_sort (identities, (GCompareFunc) identity_sort_func);

  g_list_foreach (identities, (GFunc) g_object_ref, NULL);
  g_task_return_pointer (operation->task, identities, (GDestroyNotify) free_identity_list);
}

static void
renew_identity (GoaKerberosIdentityManager *self,
                Operation                  *operation)
{
  GError *error;
  gboolean was_renewed;
  char *identity_name;

  identity_name =
    goa_kerberos_identity_get_principal_name (GOA_KERBEROS_IDENTITY
                                              (operation->identity));
  g_debug ("GoaKerberosIdentityManager: renewing identity %s", identity_name);
  g_free (identity_name);

  error = NULL;
  was_renewed =
    goa_kerberos_identity_renew (GOA_KERBEROS_IDENTITY (operation->identity),
                                 &error);

  if (!was_renewed)
    {
      g_debug ("GoaKerberosIdentityManager: could not renew identity: %s",
               error->message);

      g_task_return_error (operation->task, error);
      return;
    }

  g_task_return_boolean (operation->task, was_renewed);
}

static void
do_identity_inquiry (Operation *operation)
{
  if (operation->inquiry_func == NULL)
    {
      return;
    }

  operation->inquiry_func (operation->inquiry,
                           operation->cancellable,
                           operation->inquiry_data);
}

static void
stop_waiting_on_inquiry (Operation *operation)
{
  g_mutex_lock (&operation->inquiry_lock);
  if (operation->is_inquiring)
    {
      operation->is_inquiring = FALSE;
      g_cond_signal (&operation->inquiry_finished_condition);
    }
  g_mutex_unlock (&operation->inquiry_lock);
}

static void
on_kerberos_identity_inquiry_complete (GoaIdentityInquiry *inquiry,
                                       Operation          *operation)
{
  stop_waiting_on_inquiry (operation);
}

static void
start_inquiry (Operation          *operation,
               GoaIdentityInquiry *inquiry)
{
  operation->is_inquiring = TRUE;

  g_signal_connect (G_OBJECT (inquiry),
                    "complete",
                    G_CALLBACK (on_kerberos_identity_inquiry_complete),
                    operation);

  operation->inquiry = inquiry;
  goa_kerberos_identify_manager_send_to_context (operation->context,
                                                 (GSourceFunc)
                                                 do_identity_inquiry,
                                                 operation, NULL);
}

static void
wait_for_inquiry_to_complete (Operation                  *operation,
                              GoaKerberosIdentityInquiry *inquiry)
{
  g_mutex_lock (&operation->inquiry_lock);
  while (operation->is_inquiring)
    g_cond_wait (&operation->inquiry_finished_condition,
                 &operation->inquiry_lock);
  g_mutex_unlock (&operation->inquiry_lock);
}

static void
on_sign_in_operation_cancelled (GCancellable *cancellable,
                                Operation    *operation)
{
  stop_waiting_on_inquiry (operation);
}

static void
on_kerberos_identity_inquiry (GoaKerberosIdentityInquiry *inquiry,
                              GCancellable               *cancellable,
                              Operation                  *operation)
{
  gulong handler_id;

  start_inquiry (operation, GOA_IDENTITY_INQUIRY (inquiry));

  handler_id = g_cancellable_connect (cancellable,
                                      G_CALLBACK (on_sign_in_operation_cancelled),
                                      operation, NULL);

  if ((operation->cancellable == NULL) ||
      !g_cancellable_is_cancelled (operation->cancellable))
    wait_for_inquiry_to_complete (operation, inquiry);

  g_cancellable_disconnect (cancellable, handler_id);
}

static void
get_identity (GoaKerberosIdentityManager *self,
              Operation                  *operation)
{
  GoaIdentity *identity;

  g_debug ("GoaKerberosIdentityManager: get identity %s", operation->identifier);
  identity = g_hash_table_lookup (self->identities, operation->identifier);

  if (identity == NULL)
    {
      g_task_return_new_error (operation->task,
                               GOA_IDENTITY_MANAGER_ERROR,
                               GOA_IDENTITY_MANAGER_ERROR_IDENTITY_NOT_FOUND,
                               _("Could not find identity"));
      return;
    }

  g_task_return_pointer (operation->task, g_object_ref (identity), g_object_unref);
}

static krb5_error_code
get_new_credentials_cache (GoaKerberosIdentityManager *self,
                           krb5_ccache                *credentials_cache)
{
  krb5_error_code error_code;
  gboolean supports_multiple_identities;

  if (g_strcmp0 (self->credentials_cache_type, "FILE") == 0)
    {
      g_debug ("GoaKerberosIdentityManager: credential cache type %s doesn't supports cache collections",
               self->credentials_cache_type);
      supports_multiple_identities = FALSE;
    }
  else if (g_strcmp0 (self->credentials_cache_type, "DIR") == 0
           || g_strcmp0 (self->credentials_cache_type, "KCM") == 0
           || g_strcmp0 (self->credentials_cache_type, "KEYRING") == 0)
    {
      g_debug ("GoaKerberosIdentityManager: credential cache type %s supports cache collections", self->credentials_cache_type);
      supports_multiple_identities = TRUE;
    }
  else
    {
      g_debug ("GoaKerberosIdentityManager: don't know if credential cache type %s supports cache collections, "
               "assuming yes",
               self->credentials_cache_type);
      supports_multiple_identities = TRUE;
    }

  /* If we're configured for FILE based credentials, then we only
   * have one ccache, and we need to use it always.
   *
   * If we're configured for DIR or KEYRING based credentials, then we
   * can have multiple ccache's so we should use the default one first
   * (so it gets selected automatically) and then fallback to unique
   * ccache names for subsequent tickets.
   *
   */
  if (!supports_multiple_identities || g_hash_table_size (self->identities) == 0)
    error_code = krb5_cc_default (self->kerberos_context, credentials_cache);
  else
    error_code = krb5_cc_new_unique (self->kerberos_context, self->credentials_cache_type, NULL, credentials_cache);

  return error_code;
}

static void
sign_in_identity (GoaKerberosIdentityManager *self,
                  Operation                  *operation)
{
  GoaIdentity *identity;
  GError *error;
  krb5_error_code error_code;
  gboolean is_new_identity = FALSE;

  g_debug ("GoaKerberosIdentityManager: signing in identity %s",
           operation->identifier);
  error = NULL;
  identity = g_hash_table_lookup (self->identities, operation->identifier);
  if (identity == NULL)
    {
      krb5_ccache credentials_cache;

      error_code = get_new_credentials_cache (self, &credentials_cache);

      if (error_code != 0)
        {
          const char *error_message;

          error_message = krb5_get_error_message (self->kerberos_context, error_code);
          g_debug ("GoaKerberosIdentityManager:         Error creating new cache for identity credentials: %s",
                   error_message);
          krb5_free_error_message (self->kerberos_context, error_message);

          g_task_return_new_error (operation->task,
                                   GOA_IDENTITY_MANAGER_ERROR,
                                   GOA_IDENTITY_MANAGER_ERROR_CREATING_IDENTITY,
                                   _("Could not create credential cache for identity"));
          return;
        }

      identity = goa_kerberos_identity_new (self->kerberos_context, credentials_cache, &error);
      if (identity == NULL)
        {
          krb5_cc_destroy (self->kerberos_context, credentials_cache);
          g_task_return_error (operation->task, error);
          return;
        }
      krb5_cc_close (self->kerberos_context, credentials_cache);
      is_new_identity = TRUE;
    }
  else
    {
      g_object_ref (identity);
    }

  if (!goa_kerberos_identity_sign_in (GOA_KERBEROS_IDENTITY (identity),
                                      operation->identifier,
                                      operation->initial_password,
                                      operation->preauth_source,
                                      operation->sign_in_flags,
                                      (GoaIdentityInquiryFunc)
                                      on_kerberos_identity_inquiry,
                                      operation,
                                      NULL,
                                      operation->cancellable,
                                      &error))
    {
      if (is_new_identity)
        goa_kerberos_identity_erase (GOA_KERBEROS_IDENTITY (identity), NULL);

      g_task_return_error (operation->task, error);
    }
  else
    {
      g_task_return_pointer (operation->task, g_object_ref (identity), g_object_unref);
      g_hash_table_replace (self->identities, g_strdup (operation->identifier), g_object_ref (identity));
    }

  g_object_unref (identity);
}

static void
sign_out_identity (GoaKerberosIdentityManager *self,
                   Operation                  *operation)
{
  GError *error;
  gboolean was_signed_out;
  char *identity_name;

  identity_name =
    goa_kerberos_identity_get_principal_name (GOA_KERBEROS_IDENTITY
                                              (operation->identity));
  g_debug ("GoaKerberosIdentityManager: signing out identity %s", identity_name);
  g_free (identity_name);

  error = NULL;
  was_signed_out =
    goa_kerberos_identity_erase (GOA_KERBEROS_IDENTITY (operation->identity),
                                 &error);

  if (!was_signed_out)
    {
      g_debug ("GoaKerberosIdentityManager: could not sign out identity: %s",
               error->message);
      g_error_free (error);
    }

  g_task_return_boolean (operation->task, TRUE);
}

static void
block_scheduler_job (GoaKerberosIdentityManager *self)
{
  g_mutex_lock (&self->scheduler_job_lock);
  while (self->is_blocking_scheduler_job)
    g_cond_wait (&self->scheduler_job_unblocked, &self->scheduler_job_lock);
  self->is_blocking_scheduler_job = TRUE;
  g_mutex_unlock (&self->scheduler_job_lock);
}

static void
stop_blocking_scheduler_job (GoaKerberosIdentityManager *self)
{
  g_mutex_lock (&self->scheduler_job_lock);
  self->is_blocking_scheduler_job = FALSE;
  g_cond_signal (&self->scheduler_job_unblocked);
  g_mutex_unlock (&self->scheduler_job_lock);
}

static void
wait_for_scheduler_job_to_become_unblocked (GoaKerberosIdentityManager *self)
{
  g_mutex_lock (&self->scheduler_job_lock);
  while (self->is_blocking_scheduler_job)
    g_cond_wait (&self->scheduler_job_unblocked, &self->scheduler_job_lock);
  g_mutex_unlock (&self->scheduler_job_lock);
}

static void
on_scheduler_cancellable_cancelled (GCancellable               *cancellable,
                                    GoaKerberosIdentityManager *self)
{
  stop_blocking_scheduler_job (self);
}

static void
goa_kerberos_identity_manager_thread_pool_func (gpointer data, gpointer user_data)
{
  Operation *operation = (Operation *) data;
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (operation->manager);
  gboolean processed_operation = FALSE;

  if (operation->task != NULL && g_task_return_error_if_cancelled (operation->task))
    {
      g_clear_object (&operation->task);
      goto out;
    }

  if (g_cancellable_is_cancelled (operation->scheduler_cancellable))
    goto out;

  switch (operation->type)
    {
    case OPERATION_TYPE_REFRESH:
      processed_operation = refresh_identities (operation->manager, operation);
      break;

    case OPERATION_TYPE_GET_IDENTITY:
      get_identity (operation->manager, operation);
      processed_operation = TRUE;
      break;

    case OPERATION_TYPE_LIST:
      list_identities (operation->manager, operation);
      processed_operation = TRUE;

      /* We want to block refreshes (and their associated "added"
       * and "removed" signals) until the caller has had
       * a chance to look at the batch of
       * results we already processed
       */
      g_assert (operation->task != NULL);

      g_debug ("GoaKerberosIdentityManager: Blocking until identities list processed");
      block_scheduler_job (self);
      g_object_weak_ref (G_OBJECT (operation->task), (GWeakNotify) stop_blocking_scheduler_job, self);
      g_debug ("GoaKerberosIdentityManager: Continuing");
      break;

    case OPERATION_TYPE_SIGN_IN:
      sign_in_identity (operation->manager, operation);
      processed_operation = TRUE;
      break;

    case OPERATION_TYPE_SIGN_OUT:
      sign_out_identity (operation->manager, operation);
      processed_operation = TRUE;
      break;

    case OPERATION_TYPE_RENEW:
      renew_identity (operation->manager, operation);
      processed_operation = TRUE;
      break;

    default:
      break;
    }

  g_clear_object (&operation->task);
  wait_for_scheduler_job_to_become_unblocked (self);

  /* Don't bother saying "Waiting for next operation" if this operation
   * was a no-op, since the debug spew probably already says the message
   */
  if (processed_operation)
    g_debug ("GoaKerberosIdentityManager: Waiting for next operation");

 out:
  operation_free (operation);
  return;
}

static void
goa_kerberos_identity_manager_get_identity (GoaIdentityManager   *manager,
                                            const char           *identifier,
                                            GCancellable         *cancellable,
                                            GAsyncReadyCallback   callback,
                                            gpointer              user_data)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  GTask *task;
  Operation *operation;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_identity_manager_get_identity);

  operation = operation_new (self, cancellable, OPERATION_TYPE_GET_IDENTITY, task);
  g_object_unref (task);

  operation->identifier = g_strdup (identifier);

  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static GoaIdentity *
goa_kerberos_identity_manager_get_identity_finish (GoaIdentityManager  *self,
                                                   GAsyncResult        *result,
                                                   GError             **error)
{
  GTask *task = G_TASK (result);
  return g_task_propagate_pointer (task, error);
}

static void
goa_kerberos_identity_manager_list_identities (GoaIdentityManager  *manager,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  GTask                      *task;
  Operation                  *operation;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_identity_manager_list_identities);

  operation = operation_new (self, cancellable, OPERATION_TYPE_LIST, task);
  g_object_unref (task);

  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static GList *
goa_kerberos_identity_manager_list_identities_finish (GoaIdentityManager  *manager,
                                                      GAsyncResult        *result,
                                                      GError             **error)
{
  GTask *task = G_TASK (result);
  return g_task_propagate_pointer (task, error);
}

static void
goa_kerberos_identity_manager_renew_identity (GoaIdentityManager  *manager,
                                              GoaIdentity         *identity,
                                              GCancellable        *cancellable,
                                              GAsyncReadyCallback  callback,
                                              gpointer             user_data)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  GTask *task;
  Operation *operation;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_identity_manager_renew_identity);

  operation = operation_new (self, cancellable, OPERATION_TYPE_RENEW, task);
  g_object_unref (task);

  operation->identity = g_object_ref (identity);

  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static void
goa_kerberos_identity_manager_renew_identity_finish (GoaIdentityManager  *self,
                                                     GAsyncResult        *result,
                                                     GError             **error)
{
  GTask *task = G_TASK (result);
  g_task_propagate_pointer (task, error);
}

static void
goa_kerberos_identity_manager_sign_identity_in (GoaIdentityManager     *manager,
                                                const char             *identifier,
                                                gconstpointer           initial_password,
                                                const char             *preauth_source,
                                                GoaIdentitySignInFlags  flags,
                                                GoaIdentityInquiryFunc  inquiry_func,
                                                gpointer                inquiry_data,
                                                GCancellable           *cancellable,
                                                GAsyncReadyCallback     callback,
                                                gpointer                user_data)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  GTask *task;
  Operation *operation;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_identity_manager_sign_identity_in);

  operation = operation_new (self, cancellable, OPERATION_TYPE_SIGN_IN, task);
  g_object_unref (task);

  operation->identifier = g_strdup (identifier);
  /* Not duped. Caller is responsible for ensuring it stays alive
   * for duration of operation
   */
  operation->initial_password = initial_password;
  operation->preauth_source = g_strdup (preauth_source);
  operation->sign_in_flags = flags;
  operation->inquiry_func = inquiry_func;
  operation->inquiry_data = inquiry_data;
  g_mutex_init (&operation->inquiry_lock);
  g_cond_init (&operation->inquiry_finished_condition);
  operation->is_inquiring = FALSE;

  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static GoaIdentity *
goa_kerberos_identity_manager_sign_identity_in_finish (GoaIdentityManager  *self,
                                                       GAsyncResult        *result,
                                                       GError             **error)
{
  GTask *task = G_TASK (result);
  return g_task_propagate_pointer (task, error);
}

static void
goa_kerberos_identity_manager_sign_identity_out (GoaIdentityManager  *manager,
                                                 GoaIdentity         *identity,
                                                 GCancellable        *cancellable,
                                                 GAsyncReadyCallback  callback,
                                                 gpointer             user_data)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  GTask *task;
  Operation *operation;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_kerberos_identity_manager_sign_identity_out);

  operation = operation_new (self, cancellable, OPERATION_TYPE_SIGN_OUT, task);
  g_object_unref (task);

  operation->identity = g_object_ref (identity);

  g_thread_pool_push (self->thread_pool, operation, NULL);
}

static void
goa_kerberos_identity_manager_sign_identity_out_finish (GoaIdentityManager  *self,
                                                        GAsyncResult        *result,
                                                        GError             **error)
{
  GTask *task = G_TASK (result);
  g_task_propagate_pointer (task, error);
}

static char *
goa_kerberos_identity_manager_name_identity (GoaIdentityManager *manager,
                                             GoaIdentity        *identity)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (manager);
  char *name;
  GList *other_identities;
  gboolean other_identity_needs_rename = FALSE;

  name = goa_kerberos_identity_get_realm_name (GOA_KERBEROS_IDENTITY (identity));

  if (name == NULL)
    return NULL;

  other_identities = g_hash_table_lookup (self->identities_by_realm, name);

  /* If there was already exactly one identity for this realm before,
   * then it was going by just the realm name, so we need to rename it
   * to use the full principle name
   */
  if (other_identities != NULL &&
      other_identities->next == NULL && other_identities->data != identity)
    other_identity_needs_rename = TRUE;

  other_identities = g_list_remove (other_identities, identity);
  other_identities = g_list_prepend (other_identities, identity);

  g_hash_table_replace (self->identities_by_realm, g_strdup (name), other_identities);

  if (other_identities->next != NULL)
    {
      g_free (name);
      name = goa_kerberos_identity_get_principal_name (GOA_KERBEROS_IDENTITY (identity));

      if (other_identity_needs_rename)
        {
          GoaIdentity *other_identity = other_identities->next->data;

          _goa_identity_manager_emit_identity_renamed (GOA_IDENTITY_MANAGER (self),
                                                       other_identity);
        }
    }

  return name;
}

static void
identity_manager_interface_init (GoaIdentityManagerInterface *interface)
{
  interface->get_identity = goa_kerberos_identity_manager_get_identity;
  interface->get_identity_finish = goa_kerberos_identity_manager_get_identity_finish;
  interface->list_identities = goa_kerberos_identity_manager_list_identities;
  interface->list_identities_finish = goa_kerberos_identity_manager_list_identities_finish;
  interface->sign_identity_in = goa_kerberos_identity_manager_sign_identity_in;
  interface->sign_identity_in_finish = goa_kerberos_identity_manager_sign_identity_in_finish;
  interface->sign_identity_out = goa_kerberos_identity_manager_sign_identity_out;
  interface->sign_identity_out_finish = goa_kerberos_identity_manager_sign_identity_out_finish;
  interface->renew_identity = goa_kerberos_identity_manager_renew_identity;
  interface->renew_identity_finish = goa_kerberos_identity_manager_renew_identity_finish;
  interface->name_identity = goa_kerberos_identity_manager_name_identity;
}

static void
credentials_cache_file_monitor_changed (GFileMonitor               *monitor,
                                        GFile                      *file,
                                        GFile                      *other_file,
                                        GFileMonitorEvent          *event_type,
                                        GoaKerberosIdentityManager *self)
{
  schedule_refresh (self);
}

static gboolean
credentials_cache_keyring_notification (GPollableInputStream *stream, GoaKerberosIdentityManager *self)
{
  gssize bytes_read;
  guchar buffer[433];

  {
    g_autoptr (GError) error = NULL;

    bytes_read = g_pollable_input_stream_read_nonblocking (stream, buffer, sizeof (buffer), NULL, &error);
    if (error != NULL)
      {
        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
          g_debug ("GoaKerberosIdentityManager: Keyring notification source not yet ready");
        else
          g_warning ("GoaKerberosIdentityManager: Could not read keyring notification source: %s", error->message);

        goto out;
      }
  }

  if (bytes_read != (gssize) sizeof (buffer))
    {
      g_warning ("GoaKerberosIdentityManager: Expected to read %" G_GSIZE_FORMAT " bytes, "
                 "but received only %" G_GSSIZE_FORMAT " bytes",
                 sizeof (buffer),
                 bytes_read);
    }

  schedule_refresh (self);

 out:
  return G_SOURCE_CONTINUE;
}

static gboolean
credentials_cache_polling_timeout (GoaKerberosIdentityManager *self)
{
  schedule_refresh (self);

  return G_SOURCE_CONTINUE;
}

static gboolean
monitor_credentials_cache (GoaKerberosIdentityManager  *self,
                           GError                     **error)
{
  krb5_ccache default_cache;
  const char *cache_type;
  const char *cache_path;
  krb5_error_code error_code;
  GError *monitoring_error = NULL;
  gboolean can_monitor = TRUE;

  error_code = krb5_cc_default (self->kerberos_context, &default_cache);

  if (error_code != 0)
    {
      const char *error_message;
      error_message = krb5_get_error_message (self->kerberos_context, error_code);

      g_set_error_literal (error,
                           GOA_IDENTITY_MANAGER_ERROR,
                           GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS,
                           error_message);
      krb5_free_error_message (self->kerberos_context, error_message);

      return FALSE;
    }

  cache_type = krb5_cc_get_type (self->kerberos_context, default_cache);
  g_assert (cache_type != NULL);

  g_free (self->credentials_cache_type);
  self->credentials_cache_type = g_strdup (cache_type);

  /* If we're using a FILE type credential cache, then the
   * default cache file is the only cache we care about,
   * and its path is what we want to monitor.
   *
   * If we're using a DIR type credential cache, then the default
   * cache file is one of many possible cache files, all in the
   * same directory.  We want to monitor that directory.
   */
  cache_path = krb5_cc_get_name (self->kerberos_context, default_cache);

  /* The cache name might have a : in front of it.
   * See goakerberosidentity.c (fetch_raw_credentials) for similar code
   * FIXME: figure out if that behavior is by design, or some
   * odd bug.
   */
  if (cache_path[0] == ':')
    cache_path++;

  if (strcmp (cache_type, "FILE") == 0 || strcmp (cache_type, "DIR") == 0)
    {
      GFile *file = NULL;
      GFileMonitor *monitor = NULL;

      file = g_file_new_for_path (cache_path);

      if (strcmp (cache_type, "FILE") == 0)
        {
          monitoring_error = NULL;
          monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, &monitoring_error);
          if (monitoring_error != NULL)
            {
              g_warning ("GoaKerberosIdentityManager: Could not monitor credentials for %s (type %s), reverting to "
                         "polling: %s",
                         cache_path,
                         cache_type,
                         monitoring_error->message);

              can_monitor = FALSE;
              g_error_free (monitoring_error);
            }
        }
      else if (strcmp (cache_type, "DIR") == 0)
        {
          GFile *directory;

          directory = g_file_get_parent (file);

          monitoring_error = NULL;
          monitor = g_file_monitor_directory (directory, G_FILE_MONITOR_NONE, NULL, &monitoring_error);
          if (monitoring_error != NULL)
            {
              g_warning ("GoaKerberosIdentityManager: Could not monitor credentials for %s (type %s), reverting to "
                         "polling: %s",
                         cache_path,
                         cache_type,
                         monitoring_error->message);

              can_monitor = FALSE;
              g_error_free (monitoring_error);
            }

          g_object_unref (directory);
        }

      if (monitor != NULL)
        {
          g_signal_connect (G_OBJECT (monitor), "changed", G_CALLBACK (credentials_cache_file_monitor_changed), self);
          self->credentials_cache_file_monitor = monitor;
        }

      g_object_unref (file);
    }
  else if (strcmp (cache_type, "KEYRING") == 0)
    {
      GSource *keyring_source = NULL;

      monitoring_error = NULL;
      keyring_source = goa_kerberos_identity_manager_keyring_source_new (&monitoring_error);
      if (monitoring_error != NULL)
        {
          g_warning ("GoaKerberosIdentityManager: Could not monitor credentials for %s (type %s), reverting to polling: %s",
                     cache_path,
                     cache_type,
                     monitoring_error->message);

          can_monitor = FALSE;
          g_error_free (monitoring_error);
        }

      if (keyring_source != NULL)
        {
          g_source_set_callback (keyring_source, (GSourceFunc) credentials_cache_keyring_notification, self, NULL);
          self->credentials_cache_keyring_notification_id = g_source_attach (keyring_source, NULL);
        }

      g_clear_pointer (&keyring_source, g_source_unref);
    }
  else
    {
      can_monitor = FALSE;
    }

  if (!can_monitor)
    {
      g_warning ("GoaKerberosIdentityManager: Using polling for change notification for credential cache type '%s'",
                 cache_type);

      self->credentials_cache_polling_timeout_id = g_timeout_add_seconds (FALLBACK_POLLING_INTERVAL,
                                                                          (GSourceFunc) credentials_cache_polling_timeout,
                                                                          self);
    }

  krb5_cc_close (self->kerberos_context, default_cache);

  return TRUE;
}

static void
stop_watching_credentials_cache (GoaKerberosIdentityManager *self)
{
  if (self->credentials_cache_file_monitor != NULL)
    {
      if (!g_file_monitor_is_cancelled (self->credentials_cache_file_monitor))
        g_file_monitor_cancel (self->credentials_cache_file_monitor);

      g_clear_object (&self->credentials_cache_file_monitor);
    }

  if (self->credentials_cache_keyring_notification_id != 0)
    {
      g_source_remove (self->credentials_cache_keyring_notification_id);
      self->credentials_cache_keyring_notification_id = 0;
    }

  if (self->credentials_cache_polling_timeout_id != 0)
    {
      g_source_remove (self->credentials_cache_polling_timeout_id);
      self->credentials_cache_polling_timeout_id = 0;
    }
}

static gboolean
goa_kerberos_identity_manager_initable_init (GInitable     *initable,
                                             GCancellable  *cancellable,
                                             GError       **error)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (initable);
  krb5_error_code error_code;
  GError *monitoring_error;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  error_code = krb5_init_context (&self->kerberos_context);

  if (error_code != 0)
    {
      const char *error_message;
      error_message = krb5_get_error_message (self->kerberos_context, error_code);

      g_set_error_literal (error,
                           GOA_IDENTITY_MANAGER_ERROR,
                           GOA_IDENTITY_MANAGER_ERROR_INITIALIZING, error_message);
      krb5_free_error_message (self->kerberos_context, error_message);

      return FALSE;
    }

  monitoring_error = NULL;
  if (!monitor_credentials_cache (self, &monitoring_error))
    {
      g_warning ("GoaKerberosIdentityManager: Could not monitor credentials: %s",
                 monitoring_error->message);
      g_error_free (monitoring_error);
    }

  schedule_refresh (self);

  return TRUE;
}

static void
initable_interface_init (GInitableIface *interface)
{
  interface->init = goa_kerberos_identity_manager_initable_init;
}

static void
goa_kerberos_identity_manager_init (GoaKerberosIdentityManager *self)
{
  GError *error;

  self->identities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  self->expired_identities = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->identities_by_realm = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  error = NULL;
  self->thread_pool = g_thread_pool_new (goa_kerberos_identity_manager_thread_pool_func, NULL, 1, FALSE, &error);
  g_assert_no_error (error);

  g_mutex_init (&self->scheduler_job_lock);
  g_cond_init (&self->scheduler_job_unblocked);

  self->scheduler_cancellable = g_cancellable_new ();
  g_cancellable_connect (self->scheduler_cancellable, G_CALLBACK (on_scheduler_cancellable_cancelled), self, NULL);
}

static void
goa_kerberos_identity_manager_dispose (GObject *object)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (object);

  if (self->scheduler_cancellable != NULL)
    {
      if (!g_cancellable_is_cancelled (self->scheduler_cancellable))
        {
          g_cancellable_cancel (self->scheduler_cancellable);
        }

      g_clear_object (&self->scheduler_cancellable);
    }

  if (self->thread_pool != NULL)
    {
      g_thread_pool_free (self->thread_pool, FALSE, TRUE);
      self->thread_pool = NULL;
    }

  if (self->identities_by_realm != NULL)
    {
      g_hash_table_unref (self->identities_by_realm);
      self->identities_by_realm = NULL;
    }

  if (self->expired_identities != NULL)
    {
      g_hash_table_unref (self->expired_identities);
      self->expired_identities = NULL;
    }

  if (self->identities != NULL)
    {
      g_hash_table_unref (self->identities);
      self->identities = NULL;
    }

  stop_watching_credentials_cache (self);

  G_OBJECT_CLASS (goa_kerberos_identity_manager_parent_class)->dispose (object);
}

static void
goa_kerberos_identity_manager_finalize (GObject *object)
{
  GoaKerberosIdentityManager *self = GOA_KERBEROS_IDENTITY_MANAGER (object);

  g_free (self->credentials_cache_type);

  g_cond_clear (&self->scheduler_job_unblocked);
  krb5_free_context (self->kerberos_context);

  G_OBJECT_CLASS (goa_kerberos_identity_manager_parent_class)->finalize (object);
}

static void
goa_kerberos_identity_manager_class_init (GoaKerberosIdentityManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = goa_kerberos_identity_manager_dispose;
  object_class->finalize = goa_kerberos_identity_manager_finalize;
}

GoaIdentityManager *
goa_kerberos_identity_manager_new (GCancellable * cancellable, GError ** error)
{
  if (goa_kerberos_identity_manager_singleton == NULL)
    {
      GObject *object;

      object = g_object_new (GOA_TYPE_KERBEROS_IDENTITY_MANAGER, NULL);

      goa_kerberos_identity_manager_singleton = GOA_IDENTITY_MANAGER (object);
      g_object_add_weak_pointer (object,
                                 (gpointer *) &
                                 goa_kerberos_identity_manager_singleton);

      if (!g_initable_init (G_INITABLE (object), cancellable, error))
        {
          g_object_unref (object);
          return NULL;
        }

    }
  else
    {
      if (g_cancellable_set_error_if_cancelled (cancellable, error))
        return NULL;
      g_object_ref (goa_kerberos_identity_manager_singleton);
    }

  return goa_kerberos_identity_manager_singleton;
}
