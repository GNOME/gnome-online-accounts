/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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
#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>
#include <rest/rest-proxy.h>
#include <libsoup/soup.h>

#include "goadaemon.h"
#include "goabackend/goabackend.h"
#include "goabackend/goautils.h"
#ifdef GOA_KERBEROS_ENABLED
#include "goaidentity/goaidentityservice.h"
#endif

struct _GoaDaemon
{
  GObject parent_instance;

  GDBusConnection *connection;

  GFileMonitor *system_conf_dir_monitor;
  GFileMonitor *home_conf_file_monitor;
  GFileMonitor *home_conf_dir_monitor;

  GDBusObjectManagerServer *object_manager;

  GoaManager *manager;

#ifdef GOA_KERBEROS_ENABLED
  GoaIdentityService *identity_service;
#endif

  guint config_timeout_id;
};

typedef struct
{
  GObjectClass parent_class;
} GoaDaemonClass;

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

static gboolean on_account_handle_remove (GoaAccount            *account,
                                          GDBusMethodInvocation *invocation,
                                          gpointer               user_data);

static gboolean on_account_handle_ensure_credentials (GoaAccount            *account,
                                                      GDBusMethodInvocation *invocation,
                                                      gpointer               user_data);

static void goa_daemon_reload_configuration (GoaDaemon *daemon);

G_DEFINE_TYPE (GoaDaemon, goa_daemon, G_TYPE_OBJECT);

static void
goa_daemon_finalize (GObject *object)
{
  GoaDaemon *daemon = GOA_DAEMON (object);

  if (daemon->config_timeout_id != 0)
    {
      g_source_remove (daemon->config_timeout_id);
    }

  if (daemon->system_conf_dir_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (daemon->system_conf_dir_monitor, on_file_monitor_changed, daemon);
      g_object_unref (daemon->system_conf_dir_monitor);
    }
  if (daemon->home_conf_dir_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (daemon->home_conf_dir_monitor, on_file_monitor_changed, daemon);
      g_object_unref (daemon->home_conf_dir_monitor);
    }
  if (daemon->home_conf_file_monitor != NULL)
    {
      g_signal_handlers_disconnect_by_func (daemon->home_conf_file_monitor, on_file_monitor_changed, daemon);
      g_object_unref (daemon->home_conf_file_monitor);
    }

  g_object_unref (daemon->manager);
  g_object_unref (daemon->object_manager);
  g_object_unref (daemon->connection);

#ifdef GOA_KERBEROS_ENABLED
  g_clear_object (&daemon->identity_service);
#endif

  G_OBJECT_CLASS (goa_daemon_parent_class)->finalize (object);
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
  GoaDaemon *daemon = GOA_DAEMON (user_data);

  daemon->config_timeout_id = 0;
  g_info ("Reloading configuration files\n");
  goa_daemon_reload_configuration (daemon);

  return FALSE;
}

static void
on_file_monitor_changed (GFileMonitor      *monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         gpointer           user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);

  if (daemon->config_timeout_id == 0)
    {
      daemon->config_timeout_id = g_timeout_add (200, on_config_file_monitor_timeout, daemon);
    }
}

static void
goa_daemon_init (GoaDaemon *daemon)
{
  static volatile GQuark goa_error_domain = 0;
  GoaObjectSkeleton *object;
  gchar *path;
#ifdef GOA_KERBEROS_ENABLED
  GError *error = NULL;
#endif

  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_domain = GOA_ERROR;
  goa_error_domain; /* shut up -Wunused-but-set-variable */

  /* TODO: maybe nicer to pass in a GDBusConnection* construct property */
  daemon->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  /* Create object manager */
  daemon->object_manager = g_dbus_object_manager_server_new ("/org/gnome/OnlineAccounts");

  /* Create and export Manager */
  daemon->manager = goa_manager_skeleton_new ();
  g_signal_connect (daemon->manager,
                    "handle-add-account",
                    G_CALLBACK (on_manager_handle_add_account),
                    daemon);
  object = goa_object_skeleton_new ("/org/gnome/OnlineAccounts/Manager");
  goa_object_skeleton_set_manager (object, daemon->manager);
  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  /* create ~/.config/goa-1.0 directory */
  path = g_strdup_printf ("%s/goa-1.0", g_get_user_config_dir ());
  if (g_mkdir_with_parents (path, 0755) != 0)
    {
      g_warning ("Error creating directory %s: %m", path);
    }
  g_free (path);

  /* set up file monitoring */
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  daemon->home_conf_file_monitor = create_monitor (path, FALSE);
  if (daemon->home_conf_file_monitor != NULL)
    g_signal_connect (daemon->home_conf_file_monitor, "changed", G_CALLBACK (on_file_monitor_changed), daemon);
  g_free (path);

  /* prime the list of accounts */
  goa_daemon_reload_configuration (daemon);

  /* Export objects */
  g_dbus_object_manager_server_set_connection (daemon->object_manager, daemon->connection);

#ifdef GOA_KERBEROS_ENABLED
  daemon->identity_service = goa_identity_service_new ();
  if (!goa_identity_service_activate (daemon->identity_service,
                                      &error))
    {
      g_warning ("Error activating identity service: %s", error->message);
      g_error_free (error);
      g_clear_object (&daemon->identity_service);
    }
#endif
}

static void
goa_daemon_class_init (GoaDaemonClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_daemon_finalize;
}

GoaDaemon *
goa_daemon_new (void)
{
  return GOA_DAEMON (g_object_new (GOA_TYPE_DAEMON, NULL));
}


/* ---------------------------------------------------------------------------------------------------- */

static void
diff_sorted_lists (GList *list1,
                   GList *list2,
                   GCompareFunc compare,
                   GList **added,
                   GList **removed,
                   GList **unchanged)
{
  gint order;

  *added = *removed = *unchanged = NULL;

  while (list1 != NULL && list2 != NULL)
    {
      order = (*compare) (list1->data, list2->data);
      if (order < 0)
        {
          *removed = g_list_prepend (*removed, list1->data);
          list1 = list1->next;
        }
      else if (order > 0)
        {
          *added = g_list_prepend (*added, list2->data);
          list2 = list2->next;
        }
      else
        { /* same item */
          *unchanged = g_list_prepend (*unchanged, list1->data);
          list1 = list1->next;
          list2 = list2->next;
        }
    }

  while (list1 != NULL)
    {
      *removed = g_list_prepend (*removed, list1->data);
      list1 = list1->next;
    }
  while (list2 != NULL)
    {
      *added = g_list_prepend (*added, list2->data);
      list2 = list2->next;
    }
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
  /* the key_file member is freed elsewhere */
  g_free (data->path);
  g_slice_free (KeyFileData, data);
}

static KeyFileData *
key_file_data_new (GKeyFile    *key_file,
                   const gchar *path)
{
  KeyFileData *data;
  data = g_slice_new (KeyFileData);
  data->key_file = key_file;
  data->path = g_strdup (path);
  return data;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_config_file (GoaDaemon     *daemon,
                 const gchar   *path,
                 GHashTable    *group_name_to_key_file_data,
                 GList        **key_files_to_free)
{
  GKeyFile *key_file;
  GError *error;

  key_file = g_key_file_new ();

  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      if (!(error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT))
        {
          g_warning ("Error loading %s: %s (%s, %d)",
                     path,
                     error->message, g_quark_to_string (error->domain), error->code);
        }
      g_error_free (error);
      g_key_file_free (key_file);
    }
  else
    {
      gchar **groups;
      const char *guid;
      gsize num_groups;
      guint n;

      guid = g_dbus_connection_get_guid (daemon->connection);
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
                      g_debug ("ignoring account \"%s\" in file %s because it's stale",
                               groups[n], path);
                      g_free (groups[n]);
                      g_free (session_id);
                      continue;
                    }
                  g_free (session_id);
                }
              else
                {
                  g_key_file_remove_key (key_file, groups[n], "SessionId", NULL);
                }

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

      *key_files_to_free = g_list_prepend (*key_files_to_free, key_file);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/* returns FALSE if object is not (or no longer) valid */
static gboolean
update_account_object (GoaDaemon           *daemon,
                       GoaObjectSkeleton   *object,
                       const gchar         *path,
                       const gchar         *group,
                       GKeyFile            *key_file,
                       gboolean             just_added)
{
  GoaAccount *account;
  GoaProvider *provider;
  gboolean ret;
  gchar *identity;
  gchar *presentation_identity;
  gchar *type;
  gchar *name;
  GIcon *icon;
  gchar *serialized_icon;
  GError *error;

  g_return_val_if_fail (GOA_IS_DAEMON (daemon), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT_SKELETON (object), FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);

  ret = FALSE;
  identity = NULL;
  type = NULL;
  account = NULL;
  name = NULL;
  icon = NULL;
  serialized_icon = NULL;

  g_debug ("updating %s %d", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), just_added);

  type = g_key_file_get_string (key_file, group, "Provider", NULL);
  identity = g_key_file_get_string (key_file, group, "Identity", NULL);
  presentation_identity = g_key_file_get_string (key_file, group, "PresentationIdentity", NULL);
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
      g_warning ("Unsupported account type %s for identity %s (no provider)", type, identity);
      goto out;
    }

  goa_account_set_id (account, g_strrstr (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), "/") + 1);
  goa_account_set_provider_type (account, type);
  goa_account_set_identity (account, identity);
  goa_account_set_presentation_identity (account, presentation_identity);

  error = NULL;
  if (!goa_provider_build_object (provider, object, key_file, group, daemon->connection, just_added, &error))
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
  if (icon != NULL)
    g_object_unref (icon);
  g_free (name);
  if (provider != NULL)
    g_object_unref (provider);
  g_object_unref (account);
  g_free (type);
  g_free (identity);
  g_free (presentation_identity);
  return ret;
}

static gchar *
object_path_to_group (const gchar *object_path)
{
  g_return_val_if_fail (g_str_has_prefix (object_path, "/org/gnome/OnlineAccounts/Accounts/"), NULL);
  return g_strdup_printf ("Account %s", object_path + sizeof "/org/gnome/OnlineAccounts/Accounts/" - 1);
}

static void
process_config_entries (GoaDaemon  *daemon,
                        GHashTable *group_name_to_key_file_data)
{
  GHashTableIter iter;
  const gchar *id;
  KeyFileData *key_file_data;
  GList *existing_object_paths;
  GList *config_object_paths;
  GList *added;
  GList *removed;
  GList *unchanged;
  GList *l;

  existing_object_paths = NULL;
  {
    GList *existing_objects;
    existing_objects = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (daemon->object_manager));
    for (l = existing_objects; l != NULL; l = l->next)
      {
        GoaObject *object = GOA_OBJECT (l->data);
        const gchar *object_path;
        object_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (object));
        if (g_str_has_prefix (object_path, "/org/gnome/OnlineAccounts/Accounts/"))
          existing_object_paths = g_list_prepend (existing_object_paths, g_strdup (object_path));
      }
    g_list_foreach (existing_objects, (GFunc) g_object_unref, NULL);
    g_list_free (existing_objects);
  }

  config_object_paths = NULL;
  g_hash_table_iter_init (&iter, group_name_to_key_file_data);
  while (g_hash_table_iter_next (&iter, (gpointer*) &id, (gpointer*) &key_file_data))
    {
      gchar *object_path;

      /* create and validate object path */
      object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id + sizeof "Account " - 1);
      if (strstr (id + sizeof "Account " - 1, "/") != NULL || !g_variant_is_object_path (object_path))
        {
          g_warning ("`%s' is not a valid account identifier", id);
          g_free (object_path);
          continue;
        }
      /* steals object_path variable */
      config_object_paths = g_list_prepend (config_object_paths, object_path);
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
      object = GOA_OBJECT (g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager), object_path));
      g_warn_if_fail (object != NULL);
      g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                            G_CALLBACK (on_account_handle_remove),
                                            daemon);
      g_debug ("removing %s", object_path);
      g_warn_if_fail (g_dbus_object_manager_server_unexport (daemon->object_manager, object_path));
    }
  for (l = added; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObjectSkeleton *object;
      gchar *group;

      g_debug ("adding %s", object_path);

      group = object_path_to_group (object_path);
      key_file_data = g_hash_table_lookup (group_name_to_key_file_data, group);
      g_warn_if_fail (key_file_data != NULL);

      object = goa_object_skeleton_new (object_path);
      if (update_account_object (daemon,
                                 object,
                                 key_file_data->path,
                                 group,
                                 key_file_data->key_file,
                                 TRUE))
        {
          g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (object));
          g_signal_connect (goa_object_peek_account (GOA_OBJECT (object)),
                            "handle-remove",
                            G_CALLBACK (on_account_handle_remove),
                            daemon);
          g_signal_connect (goa_object_peek_account (GOA_OBJECT (object)),
                            "handle-ensure-credentials",
                            G_CALLBACK (on_account_handle_ensure_credentials),
                            daemon);
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

      group = object_path_to_group (object_path);
      key_file_data = g_hash_table_lookup (group_name_to_key_file_data, group);
      g_warn_if_fail (key_file_data != NULL);

      object = GOA_OBJECT (g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager), object_path));
      g_warn_if_fail (object != NULL);
      if (!update_account_object (daemon,
                                  GOA_OBJECT_SKELETON (object),
                                  key_file_data->path,
                                  group,
                                  key_file_data->key_file,
                                  FALSE))
        {
          g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                                G_CALLBACK (on_account_handle_remove),
                                                daemon);
          g_signal_handlers_disconnect_by_func (goa_object_peek_account (object),
                                                G_CALLBACK (on_account_handle_ensure_credentials),
                                                daemon);
          g_warn_if_fail (g_dbus_object_manager_server_unexport (daemon->object_manager, object_path));
        }
      g_object_unref (object);
      g_free (group);
    }

  g_list_free (removed);
  g_list_free (added);
  g_list_free (unchanged);
  g_list_foreach (existing_object_paths, (GFunc) g_free, NULL);
  g_list_free (existing_object_paths);
  g_list_foreach (config_object_paths, (GFunc) g_free, NULL);
  g_list_free (config_object_paths);
}

/* <internal>
 * goa_daemon_reload_configuration:
 * @daemon: A #GoaDaemon
 *
 * Updates the accounts_objects member from stored configuration -
 * typically called at startup or when a change on the configuration
 * files has been detected.
 */
static void
goa_daemon_reload_configuration (GoaDaemon *daemon)
{
  GList *key_files_to_free;
  GHashTable *group_name_to_key_file_data;
  gchar *path;

  key_files_to_free = NULL;
  group_name_to_key_file_data = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       (GDestroyNotify) key_file_data_free);

  /* Read the main user config file at $HOME/.config/goa-1.0/accounts.conf */
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  add_config_file (daemon, path, group_name_to_key_file_data, &key_files_to_free);
  g_free (path);

  /* now process the group_name_to_key_file_data hash table */
  process_config_entries (daemon, group_name_to_key_file_data);

  g_hash_table_unref (group_name_to_key_file_data);
  g_list_foreach (key_files_to_free, (GFunc) g_key_file_free, NULL);
  g_list_free (key_files_to_free);
}

static gchar *
generate_new_id (GoaDaemon *daemon)
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
  GoaProvider *provider;
  GKeyFile *key_file;
  GError *error;
  GList *providers;
  GList *l;
  gchar *path;
  gchar *id;
  gchar *group;
  gchar *key_file_data;
  gsize length;
  gchar *object_path;
  GVariantIter iter;
  const gchar *key;
  const gchar *value;

  /* TODO: could check for @type */

  provider = NULL;
  key_file = NULL;
  providers = NULL;
  path = NULL;
  id = NULL;
  group = NULL;
  key_file_data = NULL;
  object_path = NULL;

  if (!goa_provider_get_all_finish (&providers, res, NULL))
    goto out;

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
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  error = NULL;
  if (!g_file_get_contents (path,
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
          g_prefix_error (&error, "Error loading file %s: ", path);
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
              g_prefix_error (&error, "Error parsing key-value-file %s: ", path);
              g_dbus_method_invocation_take_error (data->invocation, error);
              goto out;
            }
        }
    }

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
        }

      g_key_file_set_string (key_file, group, key, value);
    }

  g_free (key_file_data);
  error = NULL;
  key_file_data = g_key_file_to_data (key_file,
                                      &length,
                                      &error);
  if (key_file_data == NULL)
    {
      g_prefix_error (&error, "Error generating key-value-file: ");
      g_dbus_method_invocation_take_error (data->invocation, error);
      goto out;
    }

  error = NULL;
  if (!g_file_set_contents (path,
                            key_file_data,
                            length,
                            &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      g_dbus_method_invocation_take_error (data->invocation, error);
      goto out;
    }

  /* We don't want to fail AddAccount if we could not store the
   * credentials in the keyring.
   */
  goa_utils_store_credentials_for_id_sync (provider,
                                           id,
                                           data->credentials,
                                           NULL, /* GCancellable */
                                           NULL);

  goa_daemon_reload_configuration (data->daemon);

  object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id);
  goa_manager_complete_add_account (data->manager, data->invocation, object_path);

 out:
  g_free (object_path);
  if (providers != NULL)
    g_list_free_full (providers, g_object_unref);
  g_free (key_file_data);
  g_free (group);
  g_free (id);
  g_free (path);
  if (key_file != NULL)
    g_key_file_free (key_file);

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
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  AddAccountData *data;

  data = g_slice_new0 (AddAccountData);
  data->daemon = g_object_ref (daemon);
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
on_account_handle_remove (GoaAccount            *account,
                          GDBusMethodInvocation *invocation,
                          gpointer               user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  GoaProvider *provider;
  GKeyFile *key_file;
  const gchar *provider_type;
  gchar *path;
  gchar *group;
  gchar *data;
  gsize length;
  GError *error;

  provider = NULL;
  provider_type = NULL;
  path = NULL;
  group = NULL;
  key_file = NULL;
  data = NULL;

  /* update key-file - right now we only support removing the account
   * if the entry is in ~/.config/goa-1.0/accounts.conf
   */

  key_file = g_key_file_new ();
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());

  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
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
  data = g_key_file_to_data (key_file,
                             &length,
                             &error);
  if (data == NULL)
    {
      g_prefix_error (&error, "Error generating key-value-file: ");
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  error = NULL;
  if (!g_file_set_contents (path,
                            data,
                            length,
                            &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
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
  if (!goa_utils_delete_credentials_sync (provider, account, NULL, &error))
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  goa_daemon_reload_configuration (daemon);

  goa_account_complete_remove (account, invocation);

 out:
  if (provider != NULL)
    g_object_unref (provider);
  g_free (data);
  if (key_file != NULL)
    g_key_file_free (key_file);
  g_free (group);
  g_free (path);
  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaDaemon *daemon;
  GoaObject *object;
  GDBusMethodInvocation *invocation;
} EnsureData;

static EnsureData *
ensure_data_new (GoaDaemon             *daemon,
                             GoaObject             *object,
                             GDBusMethodInvocation *invocation)
{
  EnsureData *data;
  data = g_slice_new0 (EnsureData);
  data->daemon = g_object_ref (daemon);
  data->object = g_object_ref (object);
  data->invocation = invocation;
  return data;
}

static void
ensure_data_unref (EnsureData *data)
{
  g_object_unref (data->daemon);
  g_object_unref (data->object);
  g_slice_free (EnsureData, data);
}

static gboolean
is_authorization_error (GError *error)
{
  gboolean ret;

  g_return_val_if_fail (error != NULL, FALSE);

  ret = FALSE;
  if (error->domain == REST_PROXY_ERROR || error->domain == SOUP_HTTP_ERROR)
    {
      if (SOUP_STATUS_IS_CLIENT_ERROR (error->code))
        ret = TRUE;
    }
  else if (error->domain == GOA_ERROR)
    {
      if (error->code == GOA_ERROR_NOT_AUTHORIZED)
        ret = TRUE;
    }
  return ret;
}

static void
ensure_credentials_cb (GoaProvider   *provider,
                       GAsyncResult  *res,
                       gpointer       user_data)
{
  EnsureData *data = user_data;
  gint expires_in;
  GError *error;

  error= NULL;
  if (!goa_provider_ensure_credentials_finish (provider, &expires_in, res, &error))
    {
      /* Set AttentionNeeded only if the error is an authorization error */
      if (is_authorization_error (error))
        {
          GoaAccount *account;
          account = goa_object_peek_account (data->object);
          if (!goa_account_get_attention_needed (account))
            {
              goa_account_set_attention_needed (account, TRUE);
              g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
              g_message ("%s: Setting AttentionNeeded to TRUE because EnsureCredentials() failed with: %s (%s, %d)",
                         g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)),
                         error->message, g_quark_to_string (error->domain), error->code);
            }
        }
      g_dbus_method_invocation_take_error (data->invocation, error);
    }
  else
    {
      GoaAccount *account;
      account = goa_object_peek_account (data->object);

      /* Clear AttentionNeeded flag if set */
      if (goa_account_get_attention_needed (account))
        {
          goa_account_set_attention_needed (account, FALSE);
          g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
          g_message ("%s: Setting AttentionNeeded to FALSE because EnsureCredentials() succeded\n",
                     g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)));
        }
      goa_account_complete_ensure_credentials (goa_object_peek_account (data->object),
                                               data->invocation,
                                               expires_in);
    }
  ensure_data_unref (data);
}

static gboolean
on_account_handle_ensure_credentials (GoaAccount            *account,
                                      GDBusMethodInvocation *invocation,
                                      gpointer               user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  GoaProvider *provider = NULL;
  GoaObject *object;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (account)));
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));
  if (provider == NULL)
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED,
                                             "Unsupported account type %s for id %s (no provider)",
                                             goa_account_get_provider_type (account),
                                             goa_account_get_id (account));
      goto out;
    }

  goa_provider_ensure_credentials (provider,
                                   object,
                                   NULL, /* GCancellable */
                                   (GAsyncReadyCallback) ensure_credentials_cb,
                                   ensure_data_new (daemon, object, invocation));

 out:
  g_clear_object (&provider);
  return TRUE; /* invocation was handled */
}
