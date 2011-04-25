/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

/* TODO:
 *
 * - Document files, directories and file formats somewhere.
 *   - /etc/goa-1.0/accounts.conf.d/
 *   - $HOME/.config/goa-1.0/accounts.conf.d/
 *   - $HOME/.config/goa-1.0/accounts.conf
 */

#include "config.h"
#include <glib/gi18n.h>

#include "goadaemon.h"
#include "goa/goabackend.h"

struct _GoaDaemon
{
  GObject parent_instance;

  GDBusConnection *connection;

  GFileMonitor *system_conf_dir_monitor;
  GFileMonitor *home_conf_file_monitor;
  GFileMonitor *home_conf_dir_monitor;

  GDBusObjectManagerServer *object_manager;

  GoaManager *manager;
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

static gboolean on_add_account (GoaManager            *object,
                                GDBusMethodInvocation *invocation,
                                const gchar           *type,
                                const gchar           *name,
                                GVariant              *details,
                                gpointer               user_data);

static gboolean on_handle_get_access_token (GoaAccessTokenBased   *object,
                                            GDBusMethodInvocation *invocation,
                                            gpointer               user_data);

static void goa_daemon_reload_configuration (GoaDaemon *daemon);

G_DEFINE_TYPE (GoaDaemon, goa_daemon, G_TYPE_OBJECT);

static void
goa_daemon_finalize (GObject *object)
{
  GoaDaemon *daemon = GOA_DAEMON (object);

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
      /* TODO: syslog */
      g_warning ("Error monitoring %s at %s: %s (%s, %d)",
                 is_dir ? "directory" : "file",
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
  g_object_unref (file);

  return monitor;
}

static void
on_file_monitor_changed (GFileMonitor      *monitor,
                         GFile             *file,
                         GFile             *other_file,
                         GFileMonitorEvent  event_type,
                         gpointer           user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT)
    {
      g_print ("Reloading configuration files\n");
      goa_daemon_reload_configuration (daemon);
    }
}

static void
goa_daemon_init (GoaDaemon *daemon)
{
  static volatile GQuark goa_error_domain = 0;
  GoaObjectSkeleton *object;
  gchar *path;

  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_domain = GOA_ERROR;
  goa_error_domain; /* shut up -Wunused-but-set-variable */

  /* TODO: maybe nicer to pass in a GDBusConnection* construct property */
  daemon->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

  /* Create object manager */
  daemon->object_manager = g_dbus_object_manager_server_new (daemon->connection, "/org/gnome/OnlineAccounts");

  /* Create and export Manager */
  daemon->manager = goa_manager_skeleton_new ();
  g_signal_connect (daemon->manager,
                    "handle-add-account",
                    G_CALLBACK (on_add_account),
                    daemon);
  object = goa_object_skeleton_new ("/org/gnome/OnlineAccounts/Manager");
  goa_object_skeleton_set_manager (object, daemon->manager);
  g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  /* create ~/.config/goa-1.0 and ~/.config/goa-1.0/accounts.conf.d directories */
  path = g_strdup_printf ("%s/goa-1.0/accounts.d", g_get_user_config_dir ());
  if (g_mkdir_with_parents (path, 0755) != 0)
    {
      /* TODO: syslog */
      g_warning ("Error creating directory %s: %m", path);
    }
  g_free (path);

  /* set up file monitoring */
  daemon->system_conf_dir_monitor = create_monitor (PACKAGE_SYSCONF_DIR "/goa-1.0/accounts.conf.d", TRUE);
  if (daemon->system_conf_dir_monitor != NULL)
    g_signal_connect (daemon->system_conf_dir_monitor, "changed", G_CALLBACK (on_file_monitor_changed), daemon);
  path = g_strdup_printf ("%s/goa-1.0/accounts.d", g_get_user_config_dir ());
  daemon->home_conf_dir_monitor = create_monitor (path, TRUE);
  if (daemon->home_conf_dir_monitor != NULL)
    g_signal_connect (daemon->home_conf_dir_monitor, "changed", G_CALLBACK (on_file_monitor_changed), daemon);
  g_free (path);
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  daemon->home_conf_file_monitor = create_monitor (path, FALSE);
  if (daemon->home_conf_file_monitor != NULL)
    g_signal_connect (daemon->home_conf_file_monitor, "changed", G_CALLBACK (on_file_monitor_changed), daemon);
  g_free (path);

  /* prime the list of accounts */
  goa_daemon_reload_configuration (daemon);
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

static void
add_config_file (const gchar   *path,
                 GHashTable    *group_name_to_key_file,
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
          /* TODO: syslog */
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
      gsize num_groups;
      guint n;

      groups = g_key_file_get_groups (key_file, &num_groups);
      for (n = 0; n < num_groups; n++)
        {
          if (g_str_has_prefix (groups[n], "Account "))
            {
              g_hash_table_insert (group_name_to_key_file,
                                   groups[n], /* steals string */
                                   key_file);
            }
          else
            {
              /* TODO: syslog */
              g_warning ("Unexpected group \"%s\" in file %s", groups[n], path);
              g_free (groups[n]);
            }
        }
      g_free (groups);

      *key_files_to_free = g_list_prepend (*key_files_to_free, key_file);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
update_account_object_google (GoaDaemon           *daemon,
                              GoaObjectSkeleton   *object,
                              const gchar         *group,
                              GKeyFile            *key_file,
                              gboolean             just_added)
{
  GoaAccount *account;
  GoaGoogleAccount *google_account;
  gboolean ret;
  gchar *s;

  ret = FALSE;

  account = goa_object_get_account (GOA_OBJECT (object));
  if (just_added)
    {
      google_account = goa_google_account_skeleton_new ();
      goa_object_skeleton_set_google_account (object, google_account);
    }
  else
    {
      google_account = goa_object_get_google_account (GOA_OBJECT (object));
    }

  s = g_key_file_get_string (key_file, group, "EmailAddress", NULL);
  if (s == NULL /* || !is_valid_email_address () */)
    {
      /* TODO: syslog */
      g_warning ("Invalid email address %s for id %s", s, goa_account_get_id (account));
      g_free (s);
      goto out;
    }
  goa_google_account_set_email_address (google_account, s);
  g_free (s);

  ret = TRUE;

 out:
  g_object_unref (google_account);
  g_object_unref (account);
  return ret;
}

/* returns FALSE if object is not (or no longer) valid */
static gboolean
update_account_object (GoaDaemon           *daemon,
                       GoaObjectSkeleton   *object,
                       const gchar         *group,
                       GKeyFile            *key_file,
                       gboolean             just_added)
{
  GoaAccount *account;
  GoaAccessTokenBased *access_token_based;
  gboolean ret;
  gchar *name;
  gchar *type;

  g_return_val_if_fail (GOA_IS_DAEMON (daemon), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT_SKELETON (object), FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);

  ret = FALSE;
  name = NULL;
  type = NULL;
  account = NULL;
  access_token_based = NULL;

  g_debug ("updating %s %d", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), just_added);

  type = g_key_file_get_string (key_file, group, "Type", NULL);
  name = g_key_file_get_string (key_file, group, "Name", NULL);
  if (just_added)
    {
      GoaBackendProvider *provider;

      account = goa_account_skeleton_new ();
      goa_object_skeleton_set_account (object, account);

      provider = goa_backend_provider_get_for_provider_type (type);
      if (provider == NULL)
        {
          /* TODO: syslog */
          g_warning ("Unsupported account type %s for id %s (no provider)", type, goa_account_get_id (account));
          goto out;
        }
      if (goa_backend_provider_get_access_token_supported (provider))
        {
          access_token_based = goa_access_token_based_skeleton_new ();
          g_signal_connect (access_token_based,
                            "handle-get-access-token",
                            G_CALLBACK (on_handle_get_access_token),
                            daemon);
          goa_object_skeleton_set_access_token_based (object, access_token_based);
        }
      g_object_unref (provider);
    }
  else
    {
      account = goa_object_get_account (GOA_OBJECT (object));
      access_token_based = goa_object_get_access_token_based (GOA_OBJECT (object));
    }

  goa_account_set_id (account, g_strrstr (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), "/") + 1);
  goa_account_set_account_type (account, type);
  goa_account_set_name (account, name);

  /* TODO: some kind of GoaAccountProvider subclass stuff */
  if (g_strcmp0 (type, "google") == 0)
    {
      if (!update_account_object_google (daemon, object, group, key_file, just_added))
        {
          goto out;
        }
    }
  else
    {
      /* TODO: syslog */
      g_warning ("Unsupported account type %s for id %s", type, goa_account_get_id (account));
      goto out;
    }

  ret = TRUE;

 out:
  g_object_unref (account);
  if (access_token_based != NULL)
    g_object_unref (access_token_based);
  g_free (type);
  g_free (name);
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
                        GHashTable *group_name_to_key_file)
{
  GHashTableIter iter;
  const gchar *id;
  GKeyFile *key_file;
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
  g_hash_table_iter_init (&iter, group_name_to_key_file);
  while (g_hash_table_iter_next (&iter, (gpointer*) &id, (gpointer*)  &key_file))
    {
      gchar *object_path;

      /* create and validate object path */
      object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id + sizeof "Account " - 1);
      if (strstr (id + sizeof "Account " - 1, "/") != NULL || !g_variant_is_object_path (object_path))
        {
          /* TODO: syslog */
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
      g_debug ("removing %s", object_path);
      g_warn_if_fail (g_dbus_object_manager_server_unexport (daemon->object_manager, object_path));
    }
  for (l = added; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObjectSkeleton *object;
      gchar *group;
      GKeyFile *key_file;

      g_debug ("adding %s", object_path);

      group = object_path_to_group (object_path);
      key_file = g_hash_table_lookup (group_name_to_key_file, group);
      g_warn_if_fail (key_file != NULL);

      object = goa_object_skeleton_new (object_path);
      if (update_account_object (daemon, object, group, key_file, TRUE))
        {
          g_dbus_object_manager_server_export (daemon->object_manager, G_DBUS_OBJECT_SKELETON (object));
        }
      g_object_unref (object);
      g_free (group);
    }
  for (l = unchanged; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GoaObject *object;
      gchar *group;
      GKeyFile *key_file;

      g_debug ("unchanged %s", object_path);

      group = object_path_to_group (object_path);
      key_file = g_hash_table_lookup (group_name_to_key_file, group);
      g_warn_if_fail (key_file != NULL);

      object = GOA_OBJECT (g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager), object_path));
      g_warn_if_fail (object != NULL);
      if (!update_account_object (daemon, GOA_OBJECT_SKELETON (object), group, key_file, FALSE))
        {
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
  GHashTable *group_name_to_key_file;
  GDir *dir;
  gchar *path;
  gchar *dir_path;

  key_files_to_free = NULL;
  group_name_to_key_file = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* First read system config files at /etc/goa-1.0/accounts.conf.d/ */
  dir_path = PACKAGE_SYSCONF_DIR "/goa-1.0/accounts.conf.d";
  dir = g_dir_open (dir_path, 0 /* flags */, NULL);
  if (dir != NULL)
    {
      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          if (g_str_has_suffix (name, ".conf"))
            {
              path = g_strdup_printf ("%s/%s", dir_path, name);
              add_config_file (path, group_name_to_key_file, &key_files_to_free);
              g_free (path);
            }
        }
      g_dir_close (dir);
    }

  /* Then read user config files at $HOME/.config/goa-1.0/accounts.conf.d/ */
  dir_path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  dir = g_dir_open (dir_path, 0 /* flags */, NULL);
  if (dir != NULL)
    {
      const gchar *name;
      while ((name = g_dir_read_name (dir)) != NULL)
        {
          if (g_str_has_suffix (name, ".conf"))
            {
              path = g_strdup_printf ("%s/%s", dir_path, name);
              add_config_file (path, group_name_to_key_file, &key_files_to_free);
              g_free (path);
            }
        }
      g_dir_close (dir);
    }
  g_free (dir_path);

  /* Finally the main user config file at $HOME/.config/goa-1.0/accounts.conf */
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  add_config_file (path, group_name_to_key_file, &key_files_to_free);
  g_free (path);

  /* now process the group_name_to_key_file hash table */
  process_config_entries (daemon, group_name_to_key_file);

  g_hash_table_unref (group_name_to_key_file);
  g_list_foreach (key_files_to_free, (GFunc) g_key_file_free, NULL);
  g_list_free (key_files_to_free);
}

static gchar *
generate_new_id (GoaDaemon *daemon)
{
  GDateTime *dt;
  gchar *ret;

  dt = g_date_time_new_now_local ();
  ret = g_date_time_format (dt, "account_%s"); /* seconds since Epoch */
  /* TODO: handle collisions */
  g_date_time_unref (dt);

  return ret;
}

static gboolean
on_add_account (GoaManager             *manager,
                GDBusMethodInvocation  *invocation,
                const gchar            *type,
                const gchar            *name,
                GVariant               *details,
                gpointer                user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  GKeyFile *key_file;
  GError *error;
  gchar *path;
  gchar *id;
  gchar *group;
  gchar *data;
  gsize length;
  gchar *object_path;
  GVariantIter iter;
  const gchar *key;
  const gchar *value;

  /* TODO: could check for @type */

  key_file = NULL;
  path = NULL;
  id = NULL;
  group = NULL;
  data = NULL;
  object_path = NULL;

  key_file = g_key_file_new ();
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  error = NULL;
  if (!g_file_get_contents (path,
                            &data,
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
          g_dbus_method_invocation_return_gerror (invocation, error);
          goto out;
        }
    }
  else
    {
      if (length > 0)
        {
          error = NULL;
          if (!g_key_file_load_from_data (key_file, data, length, G_KEY_FILE_KEEP_COMMENTS, &error))
            {
              g_prefix_error (&error, "Error parsing key-value-file %s: ", path);
              g_dbus_method_invocation_return_gerror (invocation, error);
              goto out;
            }
        }
    }

  id = generate_new_id (daemon);
  group = g_strdup_printf ("Account %s", id);
  g_key_file_set_string (key_file, group, "Type", type);
  g_key_file_set_string (key_file, group, "Name", name);

  g_variant_iter_init (&iter, details);
  while (g_variant_iter_next (&iter, "{&s&s}", &key, &value))
    {
      g_key_file_set_string (key_file, group, key, value);
    }

  g_free (data);
  error = NULL;
  data = g_key_file_to_data (key_file,
                             &length,
                             &error);
  if (data == NULL)
    {
      g_prefix_error (&error, "Error generating key-value-file: ");
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  error = NULL;
  if (!g_file_set_contents (path,
                            data,
                            length,
                            &error))
    {
      g_prefix_error (&error, "Error writing key-value-file %s: ", path);
      g_dbus_method_invocation_return_gerror (invocation, error);
      goto out;
    }

  goa_daemon_reload_configuration (daemon);

  object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/Accounts/%s", id);
  goa_manager_complete_add_account (manager, invocation, object_path);

 out:
  g_free (object_path);
  g_free (data);
  g_free (group);
  g_free (id);
  g_free (path);
  if (key_file != NULL)
    g_key_file_free (key_file);

  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaDaemon *daemon;
  GoaObject *object;
  GDBusMethodInvocation *invocation;
} AccessTokenData;

static void
access_token_data_free (AccessTokenData *data)
{
  g_object_unref (data->daemon);
  g_object_unref (data->object);
  g_free (data);
}

static void
get_access_token_cb (GoaBackendProvider  *provider,
                     GAsyncResult        *res,
                     gpointer             user_data)
{
  AccessTokenData *data = user_data;
  GError *error;
  gchar *access_token;
  gint expires_in;

  error = NULL;
  access_token = goa_backend_provider_get_access_token_finish (provider, &expires_in, res, &error);
  if (access_token == NULL)
    {
      if (error->domain == GOA_ERROR && error->code == GOA_ERROR_NOT_AUTHORIZED)
        {
          GoaAccount *account;
          account = goa_object_peek_account (data->object);
          goa_account_set_needs_attention (account, TRUE);
          g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (account));
          /* TODO: syslog */
          g_print ("Setting NeedsAttention to TRUE because GetAccessToken() failed for %s with: %s (%s, %d)\n",
                   g_dbus_object_get_object_path (G_DBUS_OBJECT (data->object)),
                   error->message, g_quark_to_string (error->domain), error->code);
        }
      g_dbus_method_invocation_return_gerror (data->invocation, error);
      g_error_free (error);
    }
  else
    {
      /* TODO: clear NeedsAttention flag if set? */
      goa_access_token_based_complete_get_access_token (goa_object_peek_access_token_based (data->object),
                                                        data->invocation,
                                                        access_token,
                                                        expires_in);
      g_free (access_token);
    }
  access_token_data_free (data);
}

static gboolean
on_handle_get_access_token (GoaAccessTokenBased   *instance,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
  GoaDaemon *daemon = GOA_DAEMON (user_data);
  GoaObject *object;
  GoaAccount *account;
  GoaBackendProvider *provider;
  AccessTokenData *data;

  /* TODO: log what app is requesting access */

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (instance)));
  account = goa_object_get_account (object);
  provider = goa_backend_provider_get_for_provider_type (goa_account_get_account_type (account));

  data = g_new0 (AccessTokenData, 1);
  data->daemon = g_object_ref (daemon);
  data->object = g_object_ref (object);
  data->invocation = invocation;
  goa_backend_provider_get_access_token (provider,
                                         object,
                                         NULL, /* GCancellable* */
                                         (GAsyncReadyCallback) get_access_token_cb,
                                         data);

  return TRUE; /* invocation was handled */
}
