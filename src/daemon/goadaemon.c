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
  GDBusObjectSkeleton *object;
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
  /* TODO: connect to signals for handling AddAccount()/RemoveAccount() methods */
  object = g_dbus_object_skeleton_new ("/org/gnome/OnlineAccounts/Manager");
  g_dbus_object_skeleton_add_interface (object, G_DBUS_INTERFACE_SKELETON (daemon->manager));
  g_dbus_object_manager_server_export (daemon->object_manager, object);
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
                              GDBusObjectSkeleton *object,
                              const gchar         *group,
                              GKeyFile            *key_file,
                              gboolean             just_added)
{
  GoaAccount *account;
  GoaGoogleAccount *google_account;
  gboolean ret;
  gchar *s;

  ret = FALSE;

  account = GOA_GET_ACCOUNT (object);
  if (just_added)
    {
      google_account = goa_google_account_skeleton_new ();
      g_dbus_object_skeleton_add_interface (object, G_DBUS_INTERFACE_SKELETON (google_account));
    }
  else
    {
      google_account = GOA_GET_GOOGLE_ACCOUNT (object);
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
                       GDBusObjectSkeleton *object,
                       const gchar         *group,
                       GKeyFile            *key_file,
                       gboolean             just_added)
{
  GoaAccount *account;
  gboolean ret;
  gchar *s;

  g_return_val_if_fail (GOA_IS_DAEMON (daemon), FALSE);
  g_return_val_if_fail (G_IS_DBUS_OBJECT_SKELETON (object), FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);

  ret = FALSE;

  g_debug ("updating %s %d", g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), just_added);

  if (just_added)
    {
      account = goa_account_skeleton_new ();
      g_dbus_object_skeleton_add_interface (object, G_DBUS_INTERFACE_SKELETON (account));
    }
  else
    {
      account = GOA_GET_ACCOUNT (object);
    }

  goa_account_set_id (account, g_strrstr (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), "/") + 1);
  s = g_key_file_get_string (key_file, group, "Name", NULL);
  goa_account_set_name (account, s);
  g_free (s);

  s = g_key_file_get_string (key_file, group, "Type", NULL);
  goa_account_set_account_type (account, s);
  /* TODO: some kind of GoaAccountProvider subclass stuff */
  if (g_strcmp0 (s, "google") == 0)
    {
      if (!update_account_object_google (daemon, object, group, key_file, just_added))
        {
          g_free (s);
          goto out;
        }
    }
  else
    {
      /* TODO: syslog */
      g_warning ("Unsupported account type %s for id %s", s, goa_account_get_id (account));
      g_free (s);
      goto out;
    }
  g_free (s);

  ret = TRUE;

 out:
  g_object_unref (account);
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
        GDBusObject *object = G_DBUS_OBJECT (l->data);
        const gchar *object_path;
        object_path = g_dbus_object_get_object_path (object);
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
      GDBusObjectSkeleton *object;
      gchar *group;
      GKeyFile *key_file;

      g_debug ("adding %s", object_path);

      group = object_path_to_group (object_path);
      key_file = g_hash_table_lookup (group_name_to_key_file, group);
      g_warn_if_fail (key_file != NULL);

      object = g_dbus_object_skeleton_new (object_path);
      if (update_account_object (daemon, object, group, key_file, TRUE))
        {
          g_dbus_object_manager_server_export (daemon->object_manager, object);
        }
      g_object_unref (object);
      g_free (group);
    }
  for (l = unchanged; l != NULL; l = l->next)
    {
      const gchar *object_path = l->data;
      GDBusObject *object;
      gchar *group;
      GKeyFile *key_file;

      g_debug ("unchanged %s", object_path);

      group = object_path_to_group (object_path);
      key_file = g_hash_table_lookup (group_name_to_key_file, group);
      g_warn_if_fail (key_file != NULL);

      object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (daemon->object_manager), object_path);
      g_warn_if_fail (object != NULL);
      if (!update_account_object (daemon, G_DBUS_OBJECT_SKELETON (object), group, key_file, FALSE))
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

