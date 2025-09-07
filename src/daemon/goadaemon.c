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

#include <errno.h>
#include <string.h>

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <rest/rest-proxy.h>
#include <libsoup/soup.h>

#include "goadaemon.h"
#include "goa/goa.h"
#include "goabackend/goabackend.h"
#include "goabackend/goaprovider-priv.h"
#include "goabackend/goautils.h"

#define CREDENTIALS_CHECK_TIMEOUT (60)

struct _GoaDaemon
{
  GObject parent_instance;

  GDBusConnection *connection;

  GFileMonitor *home_conf_file_monitor;
  GFileMonitor *template_file_monitor;
  gchar *home_conf_file_path;

  GNetworkMonitor *network_monitor;

  GDBusObjectManagerServer *object_manager;

  GoaManager *manager;

  GQueue *ensure_credentials_queue;
  gboolean ensure_credentials_running;

  guint config_timeout_id;
  guint credentials_timeout_id;

  uint32_t notification_id;
  unsigned int notification_signal_id;
  gboolean accounts_need_notification;
  GPtrArray *accounts_needing_attention;
};

enum
{
  PROP_0,
  PROP_CONNECTION
};

static void on_file_monitor_changed (GFileMonitor     *monitor,
                                     GFile            *file,
                                     GFile            *other_file,
                                     GFileMonitorEvent event_type,
                                     gpointer          user_data);

static gboolean on_manager_handle_add_account (GoaManager            *object,
                                               GDBusMethodInvocation *invocation,
                                               const gchar           *provider_type,
                                               const gchar           *identity,
                                               const gchar           *presentation_identity,
                                               GVariant              *credentials,
                                               GVariant              *details,
                                               gpointer               user_data);

static gboolean
on_manager_handle_is_supported_provider (GoaManager             *manager,
                                         GDBusMethodInvocation  *invocation,
                                         const gchar            *provider_type,
                                         gpointer                user_data);

static gboolean on_account_handle_remove (GoaAccount            *account,
                                          GDBusMethodInvocation *invocation,
                                          gpointer               user_data);

static gboolean on_account_handle_ensure_credentials (GoaAccount            *account,
                                                      GDBusMethodInvocation *invocation,
                                                      gpointer               user_data);

static void ensure_credentials_queue_check (GoaDaemon *self);

static void goa_daemon_check_credentials (GoaDaemon *self);
static void goa_daemon_reload_configuration (GoaDaemon *self);
static void goa_daemon_send_notification (GoaDaemon *self);
static void goa_daemon_withdraw_notification (GoaDaemon *self);
static void goa_daemon_update_notification (GoaDaemon *self);

G_DEFINE_TYPE (GoaDaemon, goa_daemon, G_TYPE_OBJECT);

static void
goa_daemon_constructed (GObject *object)
{
  GoaDaemon *self = GOA_DAEMON (object);

  G_OBJECT_CLASS (goa_daemon_parent_class)->constructed (object);

  /* prime the list of accounts */
  goa_daemon_reload_configuration (self);

  /* Export objects */
  g_dbus_object_manager_server_set_connection (self->object_manager, self->connection);
}

static void
goa_daemon_finalize (GObject *object)
{
  GoaDaemon *self = GOA_DAEMON (object);

  if (self->notification_signal_id != 0)
    {
      g_dbus_connection_signal_unsubscribe (self->connection, self->notification_signal_id);
      goa_daemon_withdraw_notification (self);
    }
  g_clear_pointer (&self->accounts_needing_attention, g_ptr_array_unref);

  if (self->config_timeout_id != 0)
    {
      g_source_remove (self->config_timeout_id);
    }

  if (self->credentials_timeout_id != 0)
    {
      g_source_remove (self->credentials_timeout_id);
    }

  if (self->home_conf_file_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->home_conf_file_monitor, on_file_monitor_changed, self);
      g_object_unref (self->home_conf_file_monitor);
    }

  if (self->template_file_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->template_file_monitor, on_file_monitor_changed, self);
      g_object_unref (self->template_file_monitor);
    }

  g_free (self->home_conf_file_path);
  g_object_unref (self->manager);
  g_object_unref (self->object_manager);
  g_object_unref (self->connection);
  g_queue_free_full (self->ensure_credentials_queue, g_object_unref);

  G_OBJECT_CLASS (goa_daemon_parent_class)->finalize (object);
}

static void
goa_daemon_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GoaDaemon *self = GOA_DAEMON (object);

  switch (prop_id)
    {
    case PROP_CONNECTION:
      self->connection = G_DBUS_CONNECTION (g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GFileMonitor *
create_monitor (const gchar *path, gboolean is_dir)
{
  GFile *file;
  GFileMonitor *monitor;
  GError *error;

  error = NULL;
  file = g_file_new_for_path (path);
  if (is_dir)
    monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, &error);
  else
    monitor = g_file_monitor_file (file, G_FILE_MONITOR_NONE, NULL, &error);

  if (monitor == NULL)
    {
      g_warning ("Error monitoring %s at %s: %s (%s, %d)",
                 is_dir ? "directory" : "file",
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
  g_object_unref (file);

  return monitor;
}

static gboolean
on_config_file_monitor_timeout (gpointer user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);

  self->config_timeout_id = 0;
  g_debug ("Reloading configuration files");
  goa_daemon_reload_configuration (self);

  return G_SOURCE_REMOVE;
}

static void
on_file_monitor_changed (GFileMonitor      *monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         gpointer           user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);

  if (self->config_timeout_id == 0)
    {
      self->config_timeout_id = g_timeout_add (200, on_config_file_monitor_timeout, self);
    }
}

static gboolean
on_check_credentials_timeout (gpointer user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);

  self->credentials_timeout_id = 0;
  g_debug ("Calling EnsureCredentials due to network changes");
  goa_daemon_check_credentials (self);

  return G_SOURCE_REMOVE;
}

static void
queue_check_credentials (GoaDaemon *self)
{
  if (self->credentials_timeout_id != 0)
    {
      g_source_remove (self->credentials_timeout_id);
    }

  self->credentials_timeout_id = g_timeout_add_seconds (1, on_check_credentials_timeout, self);
}

static void
on_network_monitor_network_changed (GoaDaemon *self, gboolean available)
{
  queue_check_credentials (self);
}

static void
goa_daemon_init (GoaDaemon *self)
{
  GoaObjectSkeleton *object;
  gchar *path;

  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_quark ();

  goa_provider_ensure_builtins_loaded ();

  /* Create object manager */
  self->object_manager = g_dbus_object_manager_server_new ("/org/gnome/OnlineAccounts");

  /* Create and export Manager */
  self->manager = goa_manager_skeleton_new ();
  g_signal_connect (self->manager,
                    "handle-add-account",
                    G_CALLBACK (on_manager_handle_add_account),
                    self);
  g_signal_connect (self->manager, "handle-is-supported-provider", G_CALLBACK (on_manager_handle_is_supported_provider), NULL);
  object = goa_object_skeleton_new ("/org/gnome/OnlineAccounts/Manager");
  goa_object_skeleton_set_manager (object, self->manager);
  g_dbus_object_manager_server_export (self->object_manager, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  self->home_conf_file_path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());

  /* create ~/.config/goa-1.0 directory */
  path = g_path_get_dirname (self->home_conf_file_path);
  if (g_mkdir_with_parents (path, 0755) != 0)
    {
      g_warning ("Error creating directory %s: %s", path, strerror (errno));
    }
  g_free (path);

  /* set up file monitoring */
  self->home_conf_file_monitor = create_monitor (self->home_conf_file_path, FALSE);
  if (self->home_conf_file_monitor != NULL)
    g_signal_connect (self->home_conf_file_monitor, "changed", G_CALLBACK (on_file_monitor_changed), self);

  if (GOA_TEMPLATE_FILE != NULL && GOA_TEMPLATE_FILE[0] != '\0')
    {
      self->template_file_monitor = create_monitor (GOA_TEMPLATE_FILE, FALSE);
      if (self->template_file_monitor != NULL)
        g_signal_connect (self->template_file_monitor, "changed", G_CALLBACK (on_file_monitor_changed), self);
    }

  self->network_monitor = g_network_monitor_get_default ();
  g_signal_connect_object (self->network_monitor,
                           "network-changed",
                           G_CALLBACK (on_network_monitor_network_changed),
                           self,
                           G_CONNECT_SWAPPED);

  /* Notifications */
  self->accounts_needing_attention = g_ptr_array_new_with_free_func (g_object_unref);

  self->ensure_credentials_queue = g_queue_new ();
  queue_check_credentials (self);
}

static void
goa_daemon_class_init (GoaDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed  = goa_daemon_constructed;
  gobject_class->finalize     = goa_daemon_finalize;
  gobject_class->set_property = goa_daemon_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_CONNECTION,
                                   g_param_spec_object ("connection",
                                                        "GDBusConnection object",
                                                        "A connection to a message bus",
                                                        G_TYPE_DBUS_CONNECTION,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS |
                                                        G_PARAM_WRITABLE));
}

GoaDaemon *
goa_daemon_new (GDBusConnection *connection)
{
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
  return GOA_DAEMON (g_object_new (GOA_TYPE_DAEMON, "connection", connection, NULL));
}


/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList *list1,
                   GList *list2,
                   GCompareFunc compare,
                   GList **out_added,
                   GList **out_removed,
                   GList **out_unchanged)
{
  GList *added = NULL;
  GList *removed = NULL;
  GList *unchanged = NULL;
  gint order;

  while (list1 != NULL && list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          removed = g_list_prepend (removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          added = g_list_prepend (added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          unchanged = g_list_prepend (unchanged, list1->data);
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      removed = g_list_prepend (removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      added = g_list_prepend (added, list2->data);
      list2 = list2->next;
    }

  if (out_added != NULL)
    {
      *out_added = added;
      added = NULL;
    }

  if (out_removed != NULL)
    {
      *out_removed = removed;
      removed = NULL;
    }

  if (out_unchanged != NULL)
    {
      *out_unchanged = unchanged;
      unchanged = NULL;
    }

  g_list_free (added);
  g_list_free (removed);
  g_list_free (unchanged);
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
account_group_to_id (const gchar *group)
{
  g_return_val_if_fail (g_str_has_prefix (group, "Account "), NULL);
  return group + sizeof "Account " - 1;
}

static gchar *
account_object_path_to_group (const gchar *object_path)
{
  g_return_val_if_fail (g_str_has_prefix (object_path, "/org/gnome/OnlineAccounts/Accounts/"), NULL);
  return g_strdup_printf ("Account %s", object_path + sizeof "/org/gnome/OnlineAccounts/Accounts/" - 1);
}

static const gchar *
template_group_to_id (const gchar *group)
{
  g_return_val_if_fail (g_str_has_prefix (group, "Template "), NULL);
  return group + sizeof "Template " - 1;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GKeyFile *key_file;
  gchar *path;
} KeyFileData;

static void
key_file_data_free (KeyFileData *data)
{
  g_key_file_unref (data->key_file);
  g_free (data->path);
  g_slice_free (KeyFileData, data);
}

static KeyFileData *
key_file_data_new (GKeyFile    *key_file,
                   const gchar *path)
{
  KeyFileData *data;
  data = g_slice_new (KeyFileData);
  data->key_file = g_key_file_ref (key_file);
  data->path = g_strdup (path);
  return data;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_config_file (GoaDaemon     *self,
                 const gchar   *path,
                 GHashTable    *group_name_to_key_file_data)
{
  GKeyFile *key_file;
  GError *error;
  gboolean needs_update = FALSE;
  gchar **groups;
  const char *guid;
  gsize num_groups;
  guint n;

  key_file = g_key_file_new ();

  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        {
          g_warning ("Error loading %s: %s (%s, %d)",
                     path,
                     error->message, g_quark_to_string (error->domain), error->code);
        }
      g_error_free (error);
      goto out;
    }

  guid = g_dbus_connection_get_guid (self->connection);
  groups = g_key_file_get_groups (key_file, &num_groups);
  for (n = 0; n < num_groups; n++)
    {
      if (g_str_has_prefix (groups[n], "Account "))
        {
          gboolean is_temporary;
          char *session_id;

          is_temporary = g_key_file_get_boolean (key_file,
                                                 groups[n],
                                                 "IsTemporary",
                                                 NULL);

          if (is_temporary)
            {
              session_id = g_key_file_get_string (key_file,
                                                  groups[n],
                                                  "SessionId",
                                                  NULL);

              /* discard temporary accounts from older sessions */
              if (session_id != NULL &&
                  g_strcmp0 (session_id, guid) != 0)
                {
                  GoaProvider *provider = NULL;
                  const gchar *id;
                  gchar *provider_type = NULL;

                  g_debug ("ignoring account \"%s\" in file %s because it's stale",
                           groups[n], path);

                  id = account_group_to_id (groups[n]);
                  if (id == NULL)
                    {
                      g_warning ("Unable to get account ID from group: %s", groups[n]);
                      goto cleanup_and_continue;
                    }

                  provider_type = g_key_file_get_string (key_file, groups[n], "Provider", NULL);
                  if (provider_type != NULL)
                    provider = goa_provider_get_for_provider_type (provider_type);

                  needs_update = g_key_file_remove_group (key_file, groups[n], NULL) || needs_update;

                  if (provider == NULL)
                    {
                      g_debug ("Unsupported account type %s for ID %s (no provider)", provider_type, id);
                      goto cleanup_and_continue;
                    }

                  error = NULL;
                  if (!goa_utils_delete_credentials_for_id_sync (provider, id, NULL, &error))
                    {
                      g_warning ("Unable to clean-up stale keyring entries: %s", error->message);
                      g_error_free (error);
                      goto cleanup_and_continue;
                    }

                cleanup_and_continue:
                  g_clear_object (&provider);
                  g_free (groups[n]);
                  g_free (provider_type);
                  g_free (session_id);
                  continue;
                }
              g_free (session_id);
            }
          else
            {
              needs_update = g_key_file_remove_key (key_file, groups[n], "SessionId", NULL) || needs_update;
            }

          g_hash_table_insert (group_name_to_key_file_data,
                               groups[n], /* steals string */
                               key_file_data_new (key_file, path));
        }
      else if (g_str_has_prefix (groups[n], "Template "))
        {
          g_hash_table_insert (group_name_to_key_file_data,
                               groups[n], /* steals string */
                               key_file_data_new (key_file, path));
        }
      else
        {
          g_warning ("Unexpected group \"%s\" in file %s", groups[n], path);
          g_free (groups[n]);
        }
    }
  g_free (groups);

  if (needs_update)
    {
      error = NULL;
      if (!g_key_file_save_to_file (key_file, path, &error))
        {
          g_prefix_error (&error, "Error writing key-value-file %s: ", path);
          g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
        }
    }

 out:
  g_key_file_unref (key_file);
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns FALSE if object is not (or no longer) valid */
static gboolean
update_account_object (GoaDaemon           *self,
                       GoaObjectSkeleton   *object,
                       const gchar         *path,
                       const gchar         *group,
                       GKeyFile            *key_file,
                       gboolean             just_added)
{
  GoaAccount *account = NULL;
  GoaProvider *provider = NULL;
  gboolean is_locked;
  gboolean is_temporary;
  gboolean ret = FALSE;
  gchar *identity = NULL;
  gchar *presentation_identity;
  gchar *type = NULL;
  gchar *name = NULL;
  GIcon *icon = NULL;
  gchar *serialized_icon = NULL;
  GError *error;

  g_return_val_if_fail (GOA_IS_DAEMON (self), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT_SKELETON (object), FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);

  g_debug ("updating %s %d", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), just_added);

  type = g_key_file_get_string (key_file, group, "Provider", NULL);
  identity = g_key_file_get_string (key_file, group, "Identity", NULL);
  presentation_identity = g_key_file_get_string (key_file, group, "PresentationIdentity", NULL);
  is_locked = g_key_file_get_boolean (key_file, group, "IsLocked", NULL);
  is_temporary = g_key_file_get_boolean (key_file, group, "IsTemporary", NULL);
  if (just_added)
    {
      account = goa_account_skeleton_new ();
      goa_object_skeleton_set_account (object, account);
    }
  else
    {
      account = goa_object_get_account (GOA_OBJECT (object));
    }

  provider = goa_provider_get_for_provider_type (type);
  if (provider == NULL)
    {
      g_debug ("Unsupported account type %s for identity %s (no provider)", type, identity);
      goto out;
    }

  goa_account_set_id (account, g_strrstr (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), "/") + 1);
  goa_account_set_provider_type (account, type);
  goa_account_set_identity (account, identity);
  goa_account_set_presentation_identity (account, presentation_identity);
  goa_account_set_is_locked (account, is_locked);
  goa_account_set_is_temporary (account, is_temporary);

  error = NULL;
  if (!goa_provider_build_object (provider, object, key_file, group, self->connection, just_added, &error))
    {
      g_warning ("Error parsing account: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  name = goa_provider_get_provider_name (provider, GOA_OBJECT (object));
  goa_account_set_provider_name (account, name);

  icon = goa_provider_get_provider_icon (provider, GOA_OBJECT (object));
  serialized_icon = g_icon_to_string (icon);
  goa_account_set_provider_icon (account, serialized_icon);

  ret = TRUE;

 out:
  g_free (serialized_icon);
  g_clear_object (&icon);
  g_free (name);
  g_clear_object (&provider);
  g_clear_object (&account);
  g_free (type);
  g_free (identity);
  g_free (presentation_identity);
  return ret;
}

static void
process_config_entries (GoaDaemon  *self,
                        GHashTable *group_name_to_key_file_data)
{
  GHashTableIter iter;
  KeyFileData *key_file_data;
  GList *existing_object_paths = NULL;
  GList *config_object_paths = NULL;
  GList *added;
  GList *removed;
  GList *unchanged;
  GList *l;

  {
    GList *existing_objects;
    existing_objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (self->object_manager));
    for (l = existing_objects; l != NULL; l = l->next)
      {
        GoaObject *object = GOA_OBJECT (l->data);
        const gchar *object_path;
        object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
        if (g_str_has_prefix (object_path, "/org/gnome/OnlineAccounts/Accounts/"))
          existing_object_paths = g_list_prepend (existing_object_paths, g_strdup (object_path));
      }
    g_list_free_full (existing_objects, g_object_unref);
  }

  {
    const gchar *group;

    g_hash_table_iter_init (&iter, group_name_to_key_file_data);
    while (g_hash_table_iter_next (&iter, (gpointer*) &group, (gpointer*) &key_file_data))
      {
        const gchar *id;
        gchar *object_path;

        if (!g_str_has_prefix (group, "Account "))
          continue;

        id = account_group_to_id (group);
        g_assert (id != NULL);

        /* create and validate object path */
        object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id);
        if (strstr (id, "/") != NULL || !g_variant_is_object_path (object_path))
          {
            g_warning ("`%s' is not a valid account identifier", group);
            g_free (object_path);
            continue;
          }
        /* steals object_path variable */
        config_object_paths = g_list_prepend (config_object_paths, object_path);
      }
  }

  existing_object_paths = g_list_sort (existing_object_paths, (GCompareFunc) g_strcmp0);
  config_object_paths = g_list_sort (config_object_paths, (GCompareFunc) g_strcmp0);
  diff_sorted_lists (existing_object_paths,
                     config_object_paths,
                     (GCompareFunc) g_strcmp0,
                     &added,
                     &removed,
                     &unchanged);

  for (l = removed; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObject *object;
      object = GOA_OBJECT (g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->object_manager), object_path));
      g_warn_if_fail (object != NULL);
      g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                            G_CALLBACK (on_account_handle_remove),
                                            self);
      g_object_unref (object);
      g_debug ("removing %s", object_path);
      g_warn_if_fail (g_dbus_object_manager_server_unexport (self->object_manager, object_path));
    }
  for (l = added; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObjectSkeleton *object;
      gchar *group;

      g_debug ("adding %s", object_path);

      group = account_object_path_to_group (object_path);
      key_file_data = g_hash_table_lookup (group_name_to_key_file_data, group);
      g_warn_if_fail (key_file_data != NULL);

      object = goa_object_skeleton_new (object_path);
      if (update_account_object (self,
                                 object,
                                 key_file_data->path,
                                 group,
                                 key_file_data->key_file,
                                 TRUE))
        {
          g_dbus_object_manager_server_export (self->object_manager, G_DBUS_OBJECT_SKELETON (object));
          g_signal_connect (goa_object_peek_account (GOA_OBJECT (object)),
                            "handle-remove",
                            G_CALLBACK (on_account_handle_remove),
                            self);
          g_signal_connect (goa_object_peek_account (GOA_OBJECT (object)),
                            "handle-ensure-credentials",
                            G_CALLBACK (on_account_handle_ensure_credentials),
                            self);
        }
      g_object_unref (object);
      g_free (group);
    }
  for (l = unchanged; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObject *object;
      gchar *group;

      g_debug ("unchanged %s", object_path);

      group = account_object_path_to_group (object_path);
      key_file_data = g_hash_table_lookup (group_name_to_key_file_data, group);
      g_warn_if_fail (key_file_data != NULL);

      object = GOA_OBJECT (g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (self->object_manager), object_path));
      g_warn_if_fail (object != NULL);
      if (!update_account_object (self,
                                  GOA_OBJECT_SKELETON (object),
                                  key_file_data->path,
                                  group,
                                  key_file_data->key_file,
                                  FALSE))
        {
          g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                                G_CALLBACK (on_account_handle_remove),
                                                self);
          g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                                G_CALLBACK (on_account_handle_ensure_credentials),
                                                self);
          g_warn_if_fail (g_dbus_object_manager_server_unexport (self->object_manager, object_path));
        }
      g_object_unref (object);
      g_free (group);
    }

  g_list_free (removed);
  g_list_free (added);
  g_list_free (unchanged);
  g_list_free_full (existing_object_paths, g_free);
  g_list_free_full (config_object_paths, g_free);
}

/* ---------------------------------------------------------------------------------------------------- */

static gint
compare_account_and_template_groups (const gchar *account_group, const gchar *template_group)
{
  const gchar *account_id;
  const gchar *template_id;

  g_return_val_if_fail (g_str_has_prefix (account_group, "Account "), 0);
  g_return_val_if_fail (g_str_has_prefix (template_group, "Template "), 0);

  account_id = account_group + sizeof "Account " - 1;
  template_id = template_group + sizeof "Template " - 1;

  return g_strcmp0 (account_id, template_id);
}

static void
process_template_entries (GoaDaemon  *self,
                          GHashTable *group_name_to_key_file_data)
{
  GError *error;
  GHashTable *key_files_to_update = NULL;
  GHashTableIter iter;
  GKeyFile *home_conf_key_file = NULL;
  GKeyFile *key_file;
  KeyFileData *key_file_data;
  const gchar *group;
  const gchar *key_file_path;
  GList *config_object_groups = NULL;
  GList *config_template_groups = NULL;
  GList *added;
  GList *unchanged;
  GList *l;

  key_files_to_update = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_key_file_unref);

  g_hash_table_iter_init (&iter, group_name_to_key_file_data);
  while (g_hash_table_iter_next (&iter, (gpointer *) &group, (gpointer *) &key_file_data))
    {
      if (home_conf_key_file == NULL && g_strcmp0 (key_file_data->path, self->home_conf_file_path) == 0)
        home_conf_key_file = g_key_file_ref (key_file_data->key_file);

      if (g_str_has_prefix (group, "Account "))
        config_object_groups = g_list_prepend (config_object_groups, g_strdup (group));
      else if (g_str_has_prefix (group, "Template "))
        config_template_groups = g_list_prepend (config_template_groups, g_strdup (group));
    }

  if (home_conf_key_file == NULL)
    home_conf_key_file = g_key_file_new ();

  config_object_groups = g_list_sort (config_object_groups, (GCompareFunc) g_strcmp0);
  config_template_groups = g_list_sort (config_template_groups, (GCompareFunc) g_strcmp0);
  diff_sorted_lists (config_object_groups,
                     config_template_groups,
                     (GCompareFunc) compare_account_and_template_groups,
                     &added,
                     NULL,
                     &unchanged);

  for (l = added; l != NULL; l = l->next)
    {
      gboolean needs_update;
      const gchar *id;
      const gchar *template_group = l->data;
      gchar *object_group = NULL;

      key_file_data = g_hash_table_lookup (group_name_to_key_file_data, template_group);
      g_assert_nonnull (key_file_data);

      if (goa_utils_keyfile_get_boolean (key_file_data->key_file, template_group, "ForceRemove"))
        continue;

      g_debug ("Adding from template %s", template_group);

      id = template_group_to_id (template_group);
      object_group = g_strdup_printf ("Account %s", id);
      g_warn_if_fail (!g_key_file_has_group (home_conf_key_file, object_group));

      needs_update = goa_utils_keyfile_copy_group (key_file_data->key_file,
                                                   template_group,
                                                   home_conf_key_file,
                                                   object_group);

      if (needs_update)
        {
          g_key_file_set_boolean (home_conf_key_file, object_group, "IsLocked", TRUE);
          g_hash_table_insert (key_files_to_update,
                               g_strdup (self->home_conf_file_path),
                               g_key_file_ref (home_conf_key_file));
        }

      g_free (object_group);
    }

  for (l = unchanged; l != NULL; l = l->next)
    {
      KeyFileData *object_key_file_data;
      KeyFileData *template_key_file_data;
      gboolean needs_update;
      const gchar *id;
      const gchar *object_group = l->data;
      gchar *template_group = NULL;

      object_key_file_data = g_hash_table_lookup (group_name_to_key_file_data, object_group);
      g_assert_nonnull (object_key_file_data);

      g_warn_if_fail (g_key_file_has_group (object_key_file_data->key_file, object_group));

      id = account_group_to_id (object_group);
      template_group = g_strdup_printf ("Template %s", id);

      template_key_file_data = g_hash_table_lookup (group_name_to_key_file_data, template_group);
      g_assert_nonnull (template_key_file_data);
      g_assert_true (g_key_file_has_group (template_key_file_data->key_file, template_group));

      if (goa_utils_keyfile_get_boolean (template_key_file_data->key_file, template_group, "ForceRemove"))
        {
          gboolean removed;

          g_debug ("Template %s specifies ForceRemove, removing %s", template_group, object_group);

          error = NULL;
          needs_update = g_key_file_remove_group (object_key_file_data->key_file, object_group, &error);
          if (error != NULL)
            {
              g_warning ("Error removing group %s from %s: %s (%s, %d)",
                         object_group,
                         key_file_data->path,
                         error->message,
                         g_quark_to_string (error->domain),
                         error->code);
              g_error_free (error);
            }

          if (needs_update)
            {
              g_hash_table_insert (key_files_to_update,
                                   g_strdup (object_key_file_data->path),
                                   g_key_file_ref (object_key_file_data->key_file));
            }

          removed = g_hash_table_remove (group_name_to_key_file_data, object_group);
          g_warn_if_fail (removed);
        }
      else
        {
          g_debug ("Updating %s from template %s", object_group, template_group);

          needs_update = goa_utils_keyfile_copy_group (template_key_file_data->key_file,
                                                       template_group,
                                                       object_key_file_data->key_file,
                                                       object_group);

          if (needs_update)
            {
              g_key_file_set_boolean (home_conf_key_file, object_group, "IsLocked", TRUE);
              g_hash_table_insert (key_files_to_update,
                                   g_strdup (object_key_file_data->path),
                                   g_key_file_ref (object_key_file_data->key_file));
            }
        }

      g_free (template_group);
    }

  g_hash_table_iter_init (&iter, key_files_to_update);
  while (g_hash_table_iter_next (&iter, (gpointer *) &key_file_path, (gpointer *) &key_file))
    {
      error = NULL;
      if (!g_key_file_save_to_file (key_file, key_file_path, &error))
        {
          g_prefix_error (&error, "Error writing key-value-file %s: ", key_file_path);
          g_warning ("%s (%s, %d)", error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
        }
    }

  g_hash_table_unref (key_files_to_update);
  g_key_file_unref (home_conf_key_file);
  g_list_free (added);
  g_list_free (unchanged);
  g_list_free_full (config_object_groups, g_free);
  g_list_free_full (config_template_groups, g_free);
}

/* ---------------------------------------------------------------------------------------------------- */

/* <internal>
 * goa_daemon_reload_configuration:
 * @self: A #GoaDaemon
 *
 * Updates the accounts_objects member from stored configuration -
 * typically called at startup or when a change on the configuration
 * files has been detected.
 */
static void
goa_daemon_reload_configuration (GoaDaemon *self)
{
  GHashTable *group_name_to_key_file_data;

  group_name_to_key_file_data = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify) key_file_data_free);

  /* Read the main user config file at $HOME/.config/goa-1.0/accounts.conf */
  add_config_file (self, self->home_conf_file_path, group_name_to_key_file_data);

  if (GOA_TEMPLATE_FILE != NULL && GOA_TEMPLATE_FILE[0] != '\0')
    add_config_file (self, GOA_TEMPLATE_FILE, group_name_to_key_file_data);

  process_template_entries (self, group_name_to_key_file_data);

  /* now process the group_name_to_key_file_data hash table */
  process_config_entries (self, group_name_to_key_file_data);

  g_hash_table_unref (group_name_to_key_file_data);
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
generate_new_id (GoaDaemon *self)
{
  static guint counter = 0;
  GDateTime *dt;
  gchar *ret;

  dt = g_date_time_new_now_local ();
  ret = g_strdup_printf ("account_%" G_GINT64_FORMAT "_%u",
                         g_date_time_to_unix (dt), /* seconds since Epoch */
                         counter); /* avoids collisions */
  g_date_time_unref (dt);

  counter++;
  return ret;
}

typedef struct
{
  GoaDaemon *daemon;
  GoaManager *manager;
  GDBusMethodInvocation *invocation;
  gchar *provider_type;
  gchar *identity;
  gchar *presentation_identity;
  GVariant *credentials;
  GVariant *details;
} AddAccountData;

static void
get_all_providers_cb (GObject      *source,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  AddAccountData *data = user_data;
  GoaProvider *provider = NULL;
  GKeyFile *key_file = NULL;
  GError *error;
  GList *providers = NULL;
  GList *l;
  gchar *id = NULL;
  gchar *group = NULL;
  gchar *key_file_data = NULL;
  gsize length;
  gsize n_credentials;
  gchar *object_path = NULL;
  GVariantIter iter;
  const gchar *key;
  const gchar *value;

  /* TODO: could check for @type */

  error = NULL;
  if (!goa_provider_get_all_finish (&providers, res, &error))
    {
      g_prefix_error (&error, "Error getting all providers: ");
      g_dbus_method_invocation_take_error (data->invocation, error);
      goto out;
    }

  for (l = providers; l != NULL; l = l->next)
    {
      GoaProvider *p;
      const gchar *type;

      p = GOA_PROVIDER (l->data);
      type = goa_provider_get_provider_type (p);
      if (g_strcmp0 (type, data->provider_type) == 0)
        {
          provider = p;
          break;
        }
    }

  if (provider == NULL)
    {
      error= NULL;
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to find a provider for: %s"),
                   data->provider_type);
      g_dbus_method_invocation_take_error (data->invocation, error);
      goto out;
    }

  key_file = g_key_file_new ();

  error = NULL;
  if (!g_file_get_contents (data->daemon->home_conf_file_path,
                            &key_file_data,
                            &length,
                            &error))
    {
      if (error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT)
        {
          g_error_free (error);
        }
      else
        {
          g_prefix_error (&error, "Error loading file %s: ", data->daemon->home_conf_file_path);
          g_dbus_method_invocation_take_error (data->invocation, error);
          goto out;
        }
    }
  else
    {
      if (length > 0)
        {
          error = NULL;
          if (!g_key_file_load_from_data (key_file, key_file_data, length, G_KEY_FILE_KEEP_COMMENTS, &error))
            {
              g_prefix_error (&error, "Error parsing key-value-file %s: ", data->daemon->home_conf_file_path);
              g_dbus_method_invocation_take_error (data->invocation, error);
              goto out;
            }
        }
    }

  if (!g_variant_lookup (data->details, "Id", "s", &id))
    id = generate_new_id (data->daemon);

  group = g_strdup_printf ("Account %s", id);
  g_key_file_set_string (key_file, group, "Provider", data->provider_type);
  g_key_file_set_string (key_file, group, "Identity", data->identity);
  g_key_file_set_string (key_file, group, "PresentationIdentity", data->presentation_identity);

  g_variant_iter_init (&iter, data->details);
  while (g_variant_iter_next (&iter, "{&s&s}", &key, &value))
    {
      /* We treat IsTemporary special.  If it's true we add in
       * the current session guid, so it can be ignored after
       * the session is over.
       */
      if (g_strcmp0 (key, "IsTemporary") == 0)
        {
          if (g_strcmp0 (value, "true") == 0)
            {
              const char *guid;

              guid = g_dbus_connection_get_guid (data->daemon->connection);
              g_key_file_set_string (key_file, group, "SessionId", guid);
            }
          else
            {
              error = NULL;
              if (!g_key_file_remove_key (key_file, group, "SessionId", &error))
                {
                  if (!g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
                      && !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND))
                    {
                      g_dbus_method_invocation_take_error (data->invocation, error);
                      goto out;
                    }
                }
            }
        }

      /* Skip Id since we already handled it above. */
      if (g_strcmp0 (key, "Id") == 0)
        continue;

      g_key_file_set_string (key_file, group, key, value);
    }

  error = NULL;
  if (!g_key_file_save_to_file (key_file, data->daemon->home_conf_file_path, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", data->daemon->home_conf_file_path);
      g_dbus_method_invocation_take_error (data->invocation, error);
      goto out;
    }

  n_credentials = g_variant_n_children (data->credentials);
  if (n_credentials > 0)
    {
      /* We don't want to fail AddAccount if we could not store the
       * credentials in the keyring.
       */
      goa_utils_store_credentials_for_id_sync (provider,
                                               id,
                                               data->credentials,
                                               NULL, /* GCancellable */
                                               NULL);
    }

  goa_daemon_reload_configuration (data->daemon);

  object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id);
  goa_manager_complete_add_account (data->manager, data->invocation, object_path);

 out:
  g_free (object_path);
  g_list_free_full (providers, g_object_unref);
  g_free (key_file_data);
  g_free (group);
  g_free (id);
  g_clear_pointer (&key_file, g_key_file_unref);
  g_object_unref (data->daemon);
  g_object_unref (data->manager);
  g_object_unref (data->invocation);
  g_free (data->provider_type);
  g_free (data->identity);
  g_free (data->presentation_identity);
  g_variant_unref (data->credentials);
  g_variant_unref (data->details);
  g_slice_free (AddAccountData, data);
}

static gboolean
on_manager_handle_add_account (GoaManager             *manager,
                               GDBusMethodInvocation  *invocation,
                               const gchar            *provider_type,
                               const gchar            *identity,
                               const gchar            *presentation_identity,
                               GVariant               *credentials,
                               GVariant               *details,
                               gpointer                user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);
  AddAccountData *data;

  data = g_slice_new0 (AddAccountData);
  data->daemon = g_object_ref (self);
  data->manager = g_object_ref (manager);
  data->invocation = g_object_ref (invocation);
  data->provider_type = g_strdup (provider_type);
  data->identity = g_strdup (identity);
  data->presentation_identity = g_strdup (presentation_identity);
  data->credentials = g_variant_ref (credentials);
  data->details = g_variant_ref (details);

  goa_provider_get_all (get_all_providers_cb, data);

  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_manager_handle_is_supported_provider (GoaManager             *manager,
                                         GDBusMethodInvocation  *invocation,
                                         const gchar            *provider_type,
                                         gpointer                user_data)
{
  GoaProvider *provider = NULL;
  gboolean is_supported;

  provider = goa_provider_get_for_provider_type (provider_type);
  is_supported = provider == NULL ? FALSE : TRUE;

  goa_manager_complete_is_supported_provider (manager, invocation, is_supported);

  g_clear_object (&provider);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaObject *object;
  GList *invocations;
} ObjectInvocationData;

static ObjectInvocationData *
object_invocation_data_new (GoaObject             *object,
                 GDBusMethodInvocation *invocation)
{
  ObjectInvocationData *data;
  data = g_slice_new0 (ObjectInvocationData);
  data->object = g_object_ref (object);
  data->invocations = g_list_prepend (data->invocations, invocation);
  return data;
}

static void
object_invocation_data_unref (ObjectInvocationData *data)
{
  g_list_free (data->invocations);
  g_object_unref (data->object);
  g_slice_free (ObjectInvocationData, data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_account_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  GoaDaemon *self;
  GDBusMethodInvocation *invocation;
  GError *error;
  GoaAccount *account;
  GoaProvider *provider = GOA_PROVIDER (source_object);
  ObjectInvocationData *data;

  self = GOA_DAEMON (g_task_get_source_object (task));
  data = g_task_get_task_data (task);

  error= NULL;
  if (!goa_provider_remove_account_finish (provider, res, &error))
    {
      g_warning ("goa_provider_remove_account() failed: %s (%s, %d)",
                 error->message,
                 g_quark_to_string (error->domain),
                 error->code);
      g_error_free (error);
    }

  goa_daemon_reload_configuration (self);

  account = goa_object_peek_account (data->object);
  invocation = G_DBUS_METHOD_INVOCATION (data->invocations->data);
  goa_account_complete_remove (account, invocation);

  if (g_ptr_array_remove (self->accounts_needing_attention, account))
    goa_daemon_update_notification (self);

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
on_account_handle_remove (GoaAccount            *account,
                          GDBusMethodInvocation *invocation,
                          gpointer               user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);
  GoaObject *object;
  GoaProvider *provider = NULL;
  GKeyFile *key_file = NULL;
  GTask *task = NULL;
  ObjectInvocationData *data;
  const gchar *provider_type = NULL;
  gchar *group = NULL;
  GError *error;

  if (goa_account_get_is_locked (account))
    {
      error = NULL;
      g_set_error_literal (&error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_SUPPORTED,
                           _("IsLocked property is set for account"));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  /* update key-file - right now we only support removing the account
   * if the entry is in ~/.config/goa-1.0/accounts.conf
   */

  key_file = g_key_file_new ();

  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  self->home_conf_file_path,
                                  G_KEY_FILE_KEEP_COMMENTS,
                                  &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  error = NULL;
  if (!g_key_file_remove_group (key_file, group, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  error = NULL;
  if (!g_key_file_save_to_file (key_file, self->home_conf_file_path, &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", self->home_conf_file_path);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  provider_type = goa_account_get_provider_type (account);
  if (provider_type == NULL)
    {
      error = NULL;
      g_set_error_literal (&error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED, /* TODO: more specific */
                           _("ProviderType property is not set for account"));
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  provider = goa_provider_get_for_provider_type (provider_type);
  if (provider == NULL)
    {
      error = NULL;
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to find a provider for: %s"),
                   provider_type);
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  error = NULL;
  if (!goa_utils_delete_credentials_for_account_sync (provider, account, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (account)));
  data = object_invocation_data_new (object, invocation);

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_task_data (task, data, (GDestroyNotify) object_invocation_data_unref);

  goa_provider_remove_account (provider, object, NULL, remove_account_cb, g_object_ref (task));

 out:
  g_clear_object (&provider);
  g_clear_object (&task);
  g_clear_pointer (&key_file, g_key_file_unref);
  g_free (group);
  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

#define NOTIFICATION_ACTION_FMT "('launch-panel', [<('online-accounts', @av [%s])>], @a{sv} {})"

static void
weak_ref_free (GWeakRef *weak_ref)
{
  g_assert (weak_ref != NULL);

  g_weak_ref_clear (weak_ref);
  g_free (weak_ref);
}

static void
on_notification_signal (GDBusConnection *connection,
                        const char      *sender_name,
                        const char      *object_path,
                        const char      *interface_name,
                        const char      *signal_name,
                        GVariant        *parameters,
                        gpointer         user_data)
{
  g_autoptr(GoaDaemon) self = g_weak_ref_get ((GWeakRef *) user_data);

  if (self == NULL)
    return;

  if (g_strcmp0 (signal_name, "ActionInvoked") == 0)
    {
      uint32_t id;

      g_variant_get (parameters, "(u&s)", &id, NULL);
      if (self->notification_id == id)
        {
          g_autofree char *target = NULL;

          if (self->accounts_needing_attention->len == 1)
            {
              GoaAccount *account = g_ptr_array_index (self->accounts_needing_attention, 0);
              g_autofree char *account_id = NULL;

              account_id = g_strdup_printf ("<'%s'>", goa_account_get_id (account));
              target = g_strdup_printf (NOTIFICATION_ACTION_FMT, account_id);
            }
          else
            {
              target = g_strdup_printf (NOTIFICATION_ACTION_FMT, "");
            }

          g_dbus_connection_call (self->connection,
                                  "org.gnome.Settings",
                                  "/org/gnome/Settings",
                                  "org.freedesktop.Application",
                                  "ActivateAction",
                                  g_variant_new_parsed (target),
                                  NULL,
                                  G_DBUS_CALL_FLAGS_NONE,
                                  -1,
                                  NULL,
                                  NULL,
                                  NULL);
        }
    }
  else if (g_strcmp0 (signal_name, "NotificationClosed") == 0)
    {
      uint32_t id;

      g_variant_get (parameters, "(uu)", &id, NULL);
      if (self->notification_id == id)
        self->notification_id = 0;
    }
}

static void
goa_daemon_send_notification_cb (GDBusConnection *connection,
                                 GAsyncResult    *result,
                                 gpointer         user_data)
{
  g_autoptr(GoaDaemon) self = GOA_DAEMON (g_steal_pointer (&user_data));
  g_autoptr(GVariant) reply = NULL;
  g_autoptr(GError) error = NULL;

  reply = g_dbus_connection_call_finish (connection, result, &error);
  if (reply == NULL)
    {
      g_warning ("Failed to notify of required account action: %s", error->message);
      return;
    }

  g_variant_get (reply, "(u)", &self->notification_id);
}

static void
goa_daemon_send_notification (GoaDaemon *self)
{
  g_autofree char *message = NULL;

  if (self->notification_signal_id == 0)
    {
      GWeakRef *weak_ref;

      weak_ref = g_new0 (GWeakRef, 1);
      g_weak_ref_init (weak_ref, self);

      self->notification_signal_id =
        g_dbus_connection_signal_subscribe (self->connection,
                                            "org.freedesktop.Notifications",
                                            "org.freedesktop.Notifications",
                                            NULL, /* NotificationClosed/ActionInvoked */
                                            "/org/freedesktop/Notifications",
                                            NULL,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            on_notification_signal,
                                            g_steal_pointer (&weak_ref),
                                            (GDestroyNotify) weak_ref_free);
    }

  if (self->accounts_needing_attention->len == 1)
    {
      GoaAccount *account = g_ptr_array_index (self->accounts_needing_attention, 0);

      message = g_strdup_printf (_("Failed to sign in to “%s”"),
                                 goa_account_get_presentation_identity (account));
    }
  else
    {
      message = g_strdup (_("Failed to sign in to multiple accounts"));
    }

  g_dbus_connection_call (self->connection,
                          "org.freedesktop.Notifications",
                          "/org/freedesktop/Notifications",
                          "org.freedesktop.Notifications",
                          "Notify",
                          g_variant_new ("(susss@as@a{sv}i)",
                                         _("Online Accounts"),
                                         self->notification_id,
                                         "dialog-warning",
                                         _("Account Action Required"),
                                         message,
                                         g_variant_new_parsed ("@as ['default', '']"),
                                         g_variant_new_parsed ("@a{sv} {}"),
                                         -1),
                          NULL, /* reply type */
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL, /* cancellable */
                          (GAsyncReadyCallback) goa_daemon_send_notification_cb,
                          g_object_ref (self));
}

static void
goa_daemon_withdraw_notification (GoaDaemon *self)
{
  if (self->notification_id > 0)
    {
      g_dbus_connection_call (self->connection,
                              "org.freedesktop.Notifications",
                              "/org/freedesktop/Notifications",
                              "org.freedesktop.Notifications",
                              "CloseNotification",
                              g_variant_new ("(u)", self->notification_id),
                              NULL,
                              G_DBUS_CALL_FLAGS_NONE,
                              -1,
                              NULL,
                              NULL,
                              NULL);
    }
}

static void
goa_daemon_update_notification (GoaDaemon *self)
{
  if (self->accounts_need_notification)
    goa_daemon_send_notification (self);
  else if (self->accounts_needing_attention->len == 0)
    goa_daemon_withdraw_notification (self);

  self->accounts_need_notification = FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_authorization_error (GError *error)
{
  gboolean ret;

  g_return_val_if_fail (error != NULL, FALSE);

  ret = FALSE;
  if (error->domain == REST_PROXY_ERROR)
    {
      if (SOUP_STATUS_IS_CLIENT_ERROR (error->code))
        ret = TRUE;
    }
  else if (g_error_matches (error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
    {
      ret = TRUE;
    }
  return ret;
}

static void
ensure_credentials_queue_complete (GList *invocations, GoaAccount *account, gint expires_in, GError *error)
{
  GList *l;
  const gchar *id;
  const gchar *provider_type;
  gint64 timestamp;

  for (l = invocations; l != NULL; l = l->next)
    {
      GDBusMethodInvocation *invocation = G_DBUS_METHOD_INVOCATION (l->data);

      if (invocation == NULL)
        continue;

      if (error == NULL)
        goa_account_complete_ensure_credentials (account, invocation, expires_in);
      else
        g_dbus_method_invocation_return_gerror (invocation, error);
    }

  id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  timestamp = g_get_monotonic_time ();
  g_debug ("%" G_GINT64_FORMAT ": Handled EnsureCredentials (%s, %s)", timestamp, provider_type, id);
}

static void
ensure_credentials_queue_collector (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask *task = G_TASK (user_data);
  GTask *task_queued;
  GoaDaemon *self;
  GoaAccount *account;
  GoaProvider *provider = GOA_PROVIDER (source_object);
  GError *error;
  ObjectInvocationData *data;
  gint expires_in;

  self = GOA_DAEMON (g_task_get_source_object (task));

  task_queued = G_TASK (g_queue_pop_head (self->ensure_credentials_queue));
  g_assert (task == task_queued);

  data = g_task_get_task_data (task);
  account = goa_object_peek_account (data->object);

  error= NULL;
  if (!goa_provider_ensure_credentials_finish (provider, &expires_in, res, &error))
    {
      /* Set AttentionNeeded only if the error is an authorization error */
      if (is_authorization_error (error))
        {
          if (!goa_account_get_attention_needed (account))
            {
              goa_account_set_attention_needed (account, TRUE);
              g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
              g_debug ("%s: Setting AttentionNeeded to TRUE because EnsureCredentials() failed with: %s (%s, %d)",
                       g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)),
                       error->message, g_quark_to_string (error->domain), error->code);
              g_ptr_array_add (self->accounts_needing_attention, g_object_ref (account));
              self->accounts_need_notification = TRUE;
            }
        }

      ensure_credentials_queue_complete (data->invocations, account, 0, error);
      g_error_free (error);
    }
  else
    {
      /* Clear AttentionNeeded flag if set */
      if (goa_account_get_attention_needed (account))
        {
          goa_account_set_attention_needed (account, FALSE);
          g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
          g_debug ("%s: Setting AttentionNeeded to FALSE because EnsureCredentials() succeeded",
                   g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)));
          g_ptr_array_remove (self->accounts_needing_attention, account);
        }

      ensure_credentials_queue_complete (data->invocations, account, expires_in, NULL);
    }

  self->ensure_credentials_running = FALSE;
  ensure_credentials_queue_check (self);

  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gint
ensure_credentials_queue_sort (gconstpointer a, gconstpointer b, gpointer user_data)
{
  GTask *task_a = G_TASK (a);
  GTask *task_b = G_TASK (b);
  gint priority_a;
  gint priority_b;

  priority_a = g_task_get_priority (task_a);
  priority_b = g_task_get_priority (task_b);

  return priority_a - priority_b;
}

static gboolean
ensure_credentials_timeout_cb (gpointer user_data)
{
  g_cancellable_cancel (G_CANCELLABLE (user_data));
  return G_SOURCE_REMOVE;
}

static void
ensure_credentials_queue_check (GoaDaemon *self)
{
  GoaAccount *account;
  GoaProvider *provider = NULL;
  GCancellable *cancellable = NULL;
  GTask *task;
  ObjectInvocationData *data;
  const gchar *id;
  const gchar *provider_type;
  gint64 timestamp;

  if (self->ensure_credentials_running)
    goto out;

  if (self->ensure_credentials_queue->length == 0)
    {
      goa_daemon_update_notification (self);
      goto out;
    }

  g_queue_sort (self->ensure_credentials_queue, ensure_credentials_queue_sort, NULL);

  task = G_TASK (g_queue_peek_head (self->ensure_credentials_queue));
  self->ensure_credentials_running = TRUE;

  data = (ObjectInvocationData *) g_task_get_task_data (task);
  account = goa_object_peek_account (data->object);

  id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  timestamp = g_get_monotonic_time ();
  g_debug ("%" G_GINT64_FORMAT ": Handling EnsureCredentials (%s, %s)", timestamp, provider_type, id);

  provider = goa_provider_get_for_provider_type (provider_type);
  g_assert_nonnull (provider);

  /* Ensure credential checks without an implicit timeout don't hang forever
   */
  cancellable = g_cancellable_new ();
  g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                              CREDENTIALS_CHECK_TIMEOUT,
                              ensure_credentials_timeout_cb,
                              g_object_ref (cancellable),
                              g_object_unref);
  goa_provider_ensure_credentials (provider,
                                   data->object,
                                   cancellable,
                                   ensure_credentials_queue_collector,
                                   task);

 out:
  g_clear_object (&cancellable);
  g_clear_object (&provider);
}

static gboolean
ensure_credentials_queue_coalesce (GoaDaemon *self, GoaObject *object, GDBusMethodInvocation *invocation)
{
  GList *l;
  GoaAccount *account;
  const gchar *id;
  gboolean ret = FALSE;
  gint priority;

  account = goa_object_peek_account (object);
  id = goa_account_get_id (account);

  priority = (invocation == NULL) ? G_PRIORITY_LOW : G_PRIORITY_HIGH;

  for (l = self->ensure_credentials_queue->head; l != NULL; l = l->next)
    {
      GoaAccount *account_queued;
      GTask *task = G_TASK (l->data);
      ObjectInvocationData *data;
      const gchar *id_queued;

      data = g_task_get_task_data (task);
      account_queued = goa_object_peek_account (data->object);
      id_queued = goa_account_get_id (account_queued);
      if (g_strcmp0 (id, id_queued) == 0)
        {
          gint priority_queued;

          priority_queued = g_task_get_priority (task);
          if (priority < priority_queued)
            g_task_set_priority (task, priority);

          data->invocations = g_list_prepend (data->invocations, invocation);

          ret = TRUE;
          break;
        }
    }

  return ret;
}

static gboolean
on_account_handle_ensure_credentials (GoaAccount            *account,
                                      GDBusMethodInvocation *invocation,
                                      gpointer               user_data)
{
  GoaDaemon *self = GOA_DAEMON (user_data);
  GoaObject *object;
  GoaProvider *provider = NULL;
  GTask *task = NULL;
  ObjectInvocationData *data;
  const gchar *id;
  const gchar *method_name;
  const gchar *provider_type;
  gint64 timestamp;

  id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  timestamp = g_get_monotonic_time ();
  g_debug ("%" G_GINT64_FORMAT ": Received %s (%s, %s)", timestamp, method_name, provider_type, id);

  provider = goa_provider_get_for_provider_type (provider_type);
  if (provider == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED,
                                             "Unsupported account type %s for id %s (no provider)",
                                             provider_type,
                                             id);
      goto out;
    }

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (account)));
  if (ensure_credentials_queue_coalesce (self, object, invocation))
    {
      timestamp = g_get_monotonic_time ();
      g_debug ("%" G_GINT64_FORMAT ": Coalesced %s (%s, %s)",
               timestamp,
               method_name,
               provider_type,
               id);
      goto out;
    }

  data = object_invocation_data_new (object, invocation);

  task = g_task_new (self, NULL, NULL, NULL);
  g_task_set_priority (task, G_PRIORITY_HIGH);
  g_task_set_task_data (task, data, (GDestroyNotify) object_invocation_data_unref);
  g_queue_push_tail (self->ensure_credentials_queue, g_object_ref (task));

  ensure_credentials_queue_check (self);

 out:
  g_clear_object (&provider);
  g_clear_object (&task);
  return TRUE; /* invocation was handled */
}

/* <internal>
 * goa_daemon_check_credentials:
 * @self: A #GoaDaemon
 *
 * Checks whether credentials are valid and tries to refresh them if
 * not. It also reports whether accounts are usable with the current
 * network.
 */
static void
goa_daemon_check_credentials (GoaDaemon *self)
{
  GList *l;
  GList *objects;

  objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (self->object_manager));
  for (l = objects; l != NULL; l = l->next)
    {
      GoaAccount *account;
      GoaObject *object = GOA_OBJECT (l->data);
      GoaProvider *provider = NULL;
      GTask *task = NULL;
      ObjectInvocationData *data;
      const gchar *id;
      const gchar *provider_type;
      gint64 timestamp;

      account = goa_object_peek_account (object);
      if (account == NULL)
        goto cleanup_and_continue;

      provider_type = goa_account_get_provider_type (account);
      provider = goa_provider_get_for_provider_type (provider_type);
      if (provider == NULL)
        goto cleanup_and_continue;

      id = goa_account_get_id (account);
      provider_type = goa_account_get_provider_type (account);
      timestamp = g_get_monotonic_time ();
      g_debug ("%" G_GINT64_FORMAT ": Calling EnsureCredentials (%s, %s)",
               timestamp,
               provider_type,
               id);

      if (ensure_credentials_queue_coalesce (self, object, NULL))
        {
          timestamp = g_get_monotonic_time ();
          g_debug ("%" G_GINT64_FORMAT ": Coalesced EnsureCredentials (%s, %s)",
                   timestamp,
                   provider_type,
                   id);
          goto cleanup_and_continue;
        }

      data = object_invocation_data_new (object, NULL);

      task = g_task_new (self, NULL, NULL, NULL);
      g_task_set_priority (task, G_PRIORITY_LOW);
      g_task_set_task_data (task, data, (GDestroyNotify) object_invocation_data_unref);
      g_queue_push_tail (self->ensure_credentials_queue, g_object_ref (task));

    cleanup_and_continue:
      g_clear_object (&provider);
      g_clear_object (&task);
    }

  ensure_credentials_queue_check (self);
  g_list_free_full (objects, g_object_unref);
}
