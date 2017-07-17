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
#include "goaprovider-priv.h"
#include "goakerberosprovider.h"
#include "goautils.h"
#include "goaidentity.h"
#include "goaidentitymanagererror.h"

#include <gcr/gcr.h>

#include "org.gnome.Identity.h"

#include "org.freedesktop.realmd.h"

struct _GoaKerberosProvider
{
  GoaProvider parent_instance;
};

typedef struct _GoaKerberosProviderClass GoaKerberosProviderClass;

struct _GoaKerberosProviderClass
{
  GoaProviderClass parent_class;
};

static GoaIdentityServiceManager *identity_manager;
static GMutex identity_manager_mutex;
static GCond identity_manager_condition;

static GDBusObjectManager *object_manager;
static GMutex object_manager_mutex;
static GCond object_manager_condition;

static void ensure_identity_manager (void);
static void ensure_object_manager (void);

static char *sign_in_identity_sync (GoaKerberosProvider  *self,
                                    const char           *identifier,
                                    const char           *password,
                                    const char           *preauth_source,
                                    GCancellable         *cancellable,
                                    GError              **error);
static void sign_in_thread (GTask               *result,
                            GoaKerberosProvider *self,
                            gpointer             task_data,
                            GCancellable        *cancellable);
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
  return g_strdup(_("Enterprise Login (Kerberos)"));
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

typedef struct
{
  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *username;
  GtkWidget *realm_entry;
  GtkWidget *realm_combo_box;
  GtkListStore *realm_store;

  guint      interface_added_id;

  gboolean realm_chosen;

  gchar *account_object_path;

  GError *error;
  GCancellable *cancellable;
  GoaRealmProvider *realm_provider;
  GDBusObjectManager *realm_manager;
} SignInRequest;

static void
translate_error (GError **error)
{
  if (!g_dbus_error_is_remote_error (*error))
    return;

  g_dbus_error_strip_remote_error (*error);
}

static gboolean
query_tooltip (GtkWidget  *widget,
               gint        x,
               gint        y,
               gboolean    keyboard_mode,
               GtkTooltip *tooltip,
               gpointer    user_data)
{
  gchar *tip;

  if (GTK_ENTRY_ICON_SECONDARY == gtk_entry_get_icon_at_pos (GTK_ENTRY (widget), x, y))
    {
      tip = gtk_entry_get_icon_tooltip_text (GTK_ENTRY (widget),
                                             GTK_ENTRY_ICON_SECONDARY);
      gtk_tooltip_set_text (tooltip, tip);
      g_free (tip);
      return TRUE;
    }

  return FALSE;
}

static void
icon_released (GtkEntry             *entry,
               GtkEntryIconPosition  pos,
               GdkEvent             *event,
               gpointer              user_data)
{
  GtkSettings *settings;
  gint timeout;

  settings = gtk_widget_get_settings (GTK_WIDGET (entry));

  g_object_get (settings, "gtk-tooltip-timeout", &timeout, NULL);
  g_object_set (settings, "gtk-tooltip-timeout", 1, NULL);
  gtk_tooltip_trigger_tooltip_query (gtk_widget_get_display (GTK_WIDGET (entry)));
  g_object_set (settings, "gtk-tooltip-timeout", timeout, NULL);
}

static void
set_entry_validation_error (GtkEntry    *entry,
                            GError      *error)
{
  translate_error (&error);
  gtk_entry_set_icon_from_icon_name (entry, GTK_ENTRY_ICON_SECONDARY,
                                     "dialog-error-symbolic");
  gtk_entry_set_icon_activatable (entry, GTK_ENTRY_ICON_SECONDARY, TRUE);
  g_signal_connect (entry, "icon-release", G_CALLBACK (icon_released), FALSE);
  g_signal_connect (entry, "query-tooltip", G_CALLBACK (query_tooltip), NULL);
  g_object_set (entry, "has-tooltip", TRUE, NULL);
  gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
                                   error->message);
}

static void
clear_entry_validation_error (GtkEntry *entry)
{
  g_object_set (entry, "has-tooltip", FALSE, NULL);
  gtk_entry_set_icon_from_pixbuf (entry, GTK_ENTRY_ICON_SECONDARY, NULL);
}

static void
sign_in_identity (GoaKerberosProvider  *self,
                  const char           *identifier,
                  const char           *password,
                  const char           *preauth_source,
                  GCancellable         *cancellable,
                  GAsyncReadyCallback   callback,
                  gpointer              user_data)
{
  GTask *operation_result;

  operation_result = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (operation_result, (gpointer) identifier);

  g_object_set_data (G_OBJECT (operation_result),
                     "password",
                     (gpointer)
                     password);
  g_object_set_data_full (G_OBJECT (operation_result),
                          "preauthentication-source",
                          g_strdup (preauth_source),
                          g_free);
  g_task_run_in_thread (operation_result, (GTaskThreadFunc) sign_in_thread);

  g_object_unref (operation_result);
}

static gchar *
sign_in_identity_finish (GoaKerberosProvider  *self,
                         GAsyncResult         *result,
                         GError              **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (self), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  task = G_TASK (result);

  return g_task_propagate_pointer (task, error);
}

static void
on_account_signed_in (GoaProvider   *provider,
                      GAsyncResult  *result,
                      SignInRequest *request)
{
  g_task_propagate_boolean (G_TASK (result), &request->error);
  g_main_loop_quit (request->loop);
}

static gboolean
get_ticket_sync (GoaKerberosProvider *self,
                 GoaObject           *object,
                 gboolean             is_interactive,
                 GCancellable        *cancellable,
                 GError             **error)
{
  GVariant            *credentials = NULL;
  GError              *lookup_error;
  GError              *sign_in_error;
  GoaAccount          *account;
  GoaTicketing        *ticketing;
  GVariant            *details;
  const char          *identifier;
  const char          *password;
  const char          *preauth_source;
  char                *object_path = NULL;
  gboolean             ret = FALSE;

  account = goa_object_get_account (object);
  identifier = goa_account_get_identity (account);

  ticketing = goa_object_get_ticketing (object);
  if (ticketing == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Ticketing is disabled for account"));
      goto out;
    }

  details = goa_ticketing_get_details (ticketing);

  preauth_source = NULL;
  g_variant_lookup (details, "preauthentication-source", "&s", &preauth_source);

  password = NULL;

  lookup_error = NULL;
  credentials = goa_utils_lookup_credentials_sync (GOA_PROVIDER (self),
                                                   object,
                                                   cancellable,
                                                   &lookup_error);

  if (credentials == NULL && !is_interactive)
    {
      if (lookup_error != NULL)
          g_propagate_error (error, lookup_error);
      else
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_NOT_AUTHORIZED,
                       _("Could not find saved credentials for principal “%s” in keyring"), identifier);
      goto out;
    }
  else if (credentials != NULL)
    {
      gboolean has_password;

      has_password = g_variant_lookup (credentials, "password", "&s", &password);
      if (!has_password && !is_interactive)
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_NOT_AUTHORIZED,
                       _("Did not find password for principal “%s” in credentials"),
                       identifier);
          goto out;
        }
    }

  sign_in_error = NULL;
  object_path = sign_in_identity_sync (self,
                                       identifier,
                                       password,
                                       preauth_source,
                                       cancellable,
                                       &sign_in_error);

  if (sign_in_error != NULL)
    {
      g_propagate_error (error, sign_in_error);
      goto out;
    }

  ret = TRUE;

 out:
  g_clear_object (&account);
  g_clear_object (&ticketing);
  g_free (object_path);
  g_clear_pointer (&credentials, (GDestroyNotify) g_variant_unref);
  return ret;
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
  got_ticket = get_ticket_sync (GOA_KERBEROS_PROVIDER (provider),
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
                        "TicketingEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&ticketing);

  return ret;
}

static void
add_entry (GtkWidget     *grid,
           gint           row,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 3, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_entry != NULL)
    *out_entry = entry;
}

static void
add_combo_box (GtkWidget     *grid,
               gint           row,
               const gchar   *text,
               const gchar   *placeholder,
               GtkListStore  *model,
               GtkWidget    **out_combo_box,
               GtkWidget    **out_entry)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *combo_box;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  combo_box = gtk_combo_box_new_with_model_and_entry (GTK_TREE_MODEL (model));
  gtk_widget_set_hexpand (combo_box, TRUE);
  gtk_widget_show (combo_box);

  entry = gtk_bin_get_child (GTK_BIN (combo_box));
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), placeholder);

  gtk_grid_attach (GTK_GRID (grid), combo_box, 1, row, 3, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_combo_box != NULL)
    *out_combo_box = combo_box;

  if (out_entry != NULL)
    *out_entry = entry;
}

static void
on_realm_added (GDBusObjectManager       *manager,
                GoaRealmObject           *object,
                SignInRequest            *request)
{
  GoaRealmKerberos *kerberos;
  GoaRealmCommon *common;
  GtkTreeIter iter;
  const gchar *configured;

  kerberos = goa_realm_object_peek_kerberos (object);
  if (!kerberos)
    return;


  gtk_list_store_append (request->realm_store, &iter);
  gtk_list_store_set (request->realm_store,
                      &iter,
                      0, goa_realm_kerberos_get_domain_name (kerberos),
                     -1);

  if (!request->realm_chosen)
    {
      common = goa_realm_object_peek_common (object);
      g_return_if_fail (common != NULL);

      configured = goa_realm_common_get_configured (common);
      if (configured && !g_str_equal (configured, ""))
        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (request->realm_combo_box), &iter);
    }
}

static void
on_populate_provider_new (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
  GoaRealmProvider *provider;
  GError *error = NULL;
  SignInRequest *request;
  GVariant *options;

  provider = goa_realm_provider_proxy_new_for_bus_finish (result, &error);
  if (error != NULL)
    {
      g_warning ("Couldn't get realmd provider: %s", error->message);
      g_error_free (error);
      return;
    }

  /* We only know request is valid if no error */
  request = user_data;
  request->realm_provider = provider;

  options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
  goa_realm_provider_call_discover (provider, "", options, NULL, NULL, NULL);
}

static void
on_populate_manager_new (GObject *source,
                         GAsyncResult *result,
                         gpointer user_data)
{
  SignInRequest *request;
  GDBusObjectManager *manager;
  GError *error = NULL;
  GList *objects, *l;

  manager = goa_realm_object_manager_client_new_for_bus_finish (result, &error);
  if (error != NULL)
    {
      g_warning ("Couldn't get realmd object manager: %s", error->message);
      g_error_free (error);
      return;
    }

  /* We only know request is valid if no error */
  request = user_data;
  request->realm_manager = manager;

  objects = g_dbus_object_manager_get_objects (manager);
  for (l = objects; l != NULL; l = l->next)
    on_realm_added (manager, l->data, request);
  g_list_free_full (objects, g_object_unref);

  request->interface_added_id = g_signal_connect (manager, "object-added",
                                                  G_CALLBACK (on_realm_added), request);
}

static void
populate_realms_for_request (SignInRequest *request)
{
  goa_realm_provider_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                        G_DBUS_PROXY_FLAGS_NONE,
                                        "org.freedesktop.realmd",
                                        "/org/freedesktop/realmd",
                                        request->cancellable,
                                        on_populate_provider_new,
                                        request);

  goa_realm_object_manager_client_new_for_bus (G_BUS_TYPE_SYSTEM,
                                               G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                               "org.freedesktop.realmd",
                                               "/org/freedesktop/realmd",
                                               request->cancellable,
                                               on_populate_manager_new,
                                               request);
}

static void
create_account_details_ui (GoaKerberosProvider *self,
                           GtkDialog           *dialog,
                           GtkWidget           *vbox,
                           gboolean             new_account,
                           SignInRequest       *request)
{
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *label;
  GtkWidget *spinner;
  gint row;
  gint width;

  goa_utils_set_dialog_title (GOA_PROVIDER (self), dialog, new_account);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  request->cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (request->cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_hexpand (request->cluebar, TRUE);
  gtk_widget_set_no_show_all (request->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (grid0), request->cluebar);

  request->cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (request->cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (request->cluebar))),
                     request->cluebar_label);

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_container_add (GTK_CONTAINER (grid0), grid1);

  row = 0;

  request->realm_store = gtk_list_store_new (1, G_TYPE_STRING);
  add_combo_box (grid1,
                 row++,
                 _("_Domain"),
                 _("Enterprise domain or realm name"),
                 request->realm_store,
                 &request->realm_combo_box,
                 &request->realm_entry);
  g_signal_connect (request->realm_entry, "changed", G_CALLBACK (clear_entry_validation_error), NULL);
  gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (request->realm_combo_box), 0);

  add_entry (grid1, row++, _("User_name"), &request->username);

  populate_realms_for_request (request);
  gtk_widget_grab_focus (request->realm_combo_box);

  gtk_dialog_add_button (request->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  request->connect_button = gtk_dialog_add_button (request->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (request->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (request->dialog, GTK_RESPONSE_OK, TRUE);

  request->progress_grid = gtk_grid_new ();
  gtk_widget_set_no_show_all (request->progress_grid, TRUE);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (request->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (request->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), request->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_widget_show (spinner);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (request->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_show (label);
  gtk_container_add (GTK_CONTAINER (request->progress_grid), label);

  gtk_window_get_size (GTK_WINDOW (request->dialog), &width, NULL);
  gtk_window_set_default_size (GTK_WINDOW (request->dialog), width, -1);
}

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *result,
                gpointer      user_data)
{
  SignInRequest *request = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &request->account_object_path,
                                       result,
                                       &request->error);
  if (request->error != NULL)
    translate_error (&request->error);
  g_main_loop_quit (request->loop);
  gtk_widget_set_sensitive (request->connect_button, TRUE);
  gtk_widget_hide (request->progress_grid);
}

static void
remove_account_cb (GoaAccount   *account,
                   GAsyncResult *result,
                   GMainLoop    *loop)
{
  goa_account_call_remove_finish (account, result, NULL);
  g_main_loop_quit (loop);
}

static gboolean
refresh_account (GoaProvider    *provider,
                 GoaClient      *client,
                 GoaObject      *object,
                 GtkWindow      *parent,
                 GError        **error)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (provider);
  gboolean             got_ticket;
  GError              *ticket_error = NULL;

  g_return_val_if_fail (GOA_IS_KERBEROS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  got_ticket = get_ticket_sync (self,
                                object,
                                TRUE /* Allow interaction */,
                                NULL,
                                &ticket_error);

  if (ticket_error != NULL)
    {
      translate_error (&ticket_error);
      g_propagate_error (error, ticket_error);
    }

  return got_ticket;
}

static void
on_initial_sign_in_done (GoaKerberosProvider *self,
                         GAsyncResult        *result,
                         GTask               *operation_result)
{
  GError     *error;
  gboolean    remember_password;
  GoaObject  *object;
  char       *object_path;

  object = g_task_get_source_tag (operation_result);

  remember_password = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (operation_result),
                                                          "remember-password"));

  error = NULL;
  object_path = sign_in_identity_finish (self, result, &error);
  if (error != NULL)
    {
      g_task_return_error (operation_result, error);
      goto out;
    }

  if (remember_password)
    {
      GVariantBuilder  builder;

      if (object_path != NULL && object != NULL)
        {
          GcrSecretExchange *secret_exchange;
          const char        *password;

          secret_exchange = g_object_get_data (G_OBJECT (operation_result), "secret-exchange");
          password = gcr_secret_exchange_get_secret (secret_exchange, NULL);

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
                                                       object,
                                                       g_variant_builder_end (&builder),
                                                       NULL,
                                                       NULL);
        }
    }

  g_task_return_boolean (operation_result, TRUE);

 out:
  g_free (object_path);
  g_object_unref (operation_result);
}

static void
on_system_prompt_answered_for_initial_sign_in (GcrPrompt          *prompt,
                                               GAsyncResult       *result,
                                               GTask              *operation_result)
{
  GoaKerberosProvider *self;
  GCancellable        *cancellable;
  GError              *error;
  const char          *principal;
  const char          *password;
  const char          *preauth_source;
  GcrSecretExchange   *secret_exchange;

  self = GOA_KERBEROS_PROVIDER (g_task_get_source_object (operation_result));
  principal = g_object_get_data (G_OBJECT (operation_result), "principal");
  cancellable = g_task_get_cancellable (operation_result);

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
          g_task_return_error (operation_result, error);
        }
      else
        {
          g_cancellable_cancel (cancellable);
          g_task_return_error_if_cancelled (operation_result);
        }

      g_object_unref (operation_result);
      return;
    }

  secret_exchange = gcr_system_prompt_get_secret_exchange (GCR_SYSTEM_PROMPT (prompt));
  g_object_set_data_full (G_OBJECT (operation_result),
                          "secret-exchange",
                          g_object_ref (secret_exchange),
                          (GDestroyNotify)
                          g_object_unref);

  g_object_set_data (G_OBJECT (operation_result),
                     "remember-password",
                     GINT_TO_POINTER (gcr_prompt_get_choice_chosen (prompt)));

  gcr_system_prompt_close (GCR_SYSTEM_PROMPT (prompt), NULL, NULL);

  sign_in_identity (self,
                    principal,
                    password,
                    preauth_source,
                    cancellable,
                    (GAsyncReadyCallback)
                    on_initial_sign_in_done,
                    operation_result);
}

static void
on_system_prompt_open_for_initial_sign_in (GcrSystemPrompt     *system_prompt,
                                           GAsyncResult        *result,
                                           GTask               *operation_result)
{
  GCancellable *cancellable;
  GcrPrompt    *prompt;
  GError       *error;

  cancellable = g_task_get_cancellable (operation_result);
  error = NULL;
  prompt = gcr_system_prompt_open_finish (result, &error);

  if (prompt == NULL)
    {
      g_task_return_error (operation_result, error);
      g_object_unref (operation_result);

      return;
    }

  gcr_prompt_set_title (prompt, _("Log In to Realm"));
  gcr_prompt_set_description (prompt, _("Please enter your password below."));
  gcr_prompt_set_choice_label (prompt, _("Remember this password"));

  gcr_prompt_password_async (prompt,
                             cancellable,
                             (GAsyncReadyCallback)
                             on_system_prompt_answered_for_initial_sign_in,
                             operation_result);
}

static void
perform_initial_sign_in (GoaKerberosProvider *self,
                         GoaObject           *object,
                         const char          *principal,
                         SignInRequest       *request)
{

  GTask        *operation_result;
  GCancellable *cancellable;

  cancellable = g_cancellable_new ();

  operation_result = g_task_new (self, cancellable, (GAsyncReadyCallback) on_account_signed_in, request);
  g_task_set_source_tag (operation_result, object);

  g_object_set_data (G_OBJECT (operation_result),
                     "principal",
                     (gpointer)
                     principal);

  gcr_system_prompt_open_async (-1,
                                cancellable,
                                (GAsyncReadyCallback)
                                on_system_prompt_open_for_initial_sign_in,
                                operation_result);

  g_object_unref (cancellable);
}

static char *
get_realm (SignInRequest *request)
{
  char *realm;
  GtkTreeIter iter;

  if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (request->realm_combo_box), &iter))
    gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (request->realm_combo_box)),
                        &iter,
                        0, &realm,
                        -1);
  else
    realm = g_strdup (gtk_entry_get_text (GTK_ENTRY (request->realm_entry)));

  return realm;
}

static void
release_realmd (GDBusProxy *proxy)
{
  GoaRealmService *service;
  gchar *unique_name;
  GError *error = NULL;

  /*
   * Since we are running from a long running process and don't want
   * realmd to have to hang around for the entire user session, let
   * realmd go away, by calling Release().
   */

  unique_name = g_dbus_proxy_get_name_owner (proxy);
  if (!unique_name)
    return;

  /* This won't block because we're not loading properties, and using a unique name */
  service = goa_realm_service_proxy_new_sync (g_dbus_proxy_get_connection (proxy),
                                              G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES,
                                              unique_name, "/org/freedesktop/realmd",
                                              NULL, &error);
  g_free (unique_name);

  if (service == NULL)
    {
      g_warning ("Failed to create service proxy: %s", error->message);
      g_error_free (error);
    }
  else
    {
      goa_realm_service_call_release (service, NULL, NULL, NULL);
      g_object_unref (service);
    }
}

static void
on_discover_realm (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
  SignInRequest *request = user_data;
  GError *error = NULL;
  gchar **realm_paths = NULL;
  GDBusObject *object = NULL;
  GoaRealmKerberos *kerberos = NULL;
  gint unused;

  goa_realm_provider_call_discover_finish (request->realm_provider, &unused,
                                           &realm_paths, result, &error);

  if (error == NULL && realm_paths)
    {
      if (realm_paths[0])
        object = g_dbus_object_manager_get_object (request->realm_manager,
                                                   realm_paths[0]);
      if (object)
        {
          kerberos = goa_realm_object_get_kerberos (GOA_REALM_OBJECT (object));
          g_object_unref (object);
        }
      g_strfreev (realm_paths);
    }

  g_clear_error (&request->error);

  if (kerberos)
    {
      gtk_entry_set_text (GTK_ENTRY (request->realm_entry),
                          goa_realm_kerberos_get_realm_name (kerberos));
      g_object_unref (kerberos);
    }
  else if (error)
    {
      request->error = error;
    }
  else
    {
      g_set_error (&request->error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   _("The domain is not valid"));
    }

  g_main_loop_quit (request->loop);
  gtk_widget_set_sensitive (request->connect_button, TRUE);
  gtk_widget_hide (request->progress_grid);
}

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  GoaKerberosProvider *self = GOA_KERBEROS_PROVIDER (provider);
  SignInRequest request;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaObject   *object = NULL;
  GoaAccount  *account;
  char        *realm;
  const char  *username;
  const char *provider_type;
  gchar      *principal = NULL;
  gint        response;
  GVariant   *options;

  memset (&request, 0, sizeof (SignInRequest));
  request.cancellable = g_cancellable_new ();
  request.loop = g_main_loop_new (NULL, FALSE);
  request.dialog = dialog;
  request.error = NULL;

  create_account_details_ui (self, dialog, GTK_WIDGET (vbox), TRUE, &request);
  gtk_widget_show_all (GTK_WIDGET (vbox));

start_over:
  response = gtk_dialog_run (dialog);

  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&request.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  realm = get_realm (&request);

  /* Would have been set by dialog above, otherwise no realmd */
  if (request.realm_provider && request.realm_manager)
    {
      options = g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
      goa_realm_provider_call_discover (request.realm_provider, realm, options,
                                        request.cancellable, on_discover_realm,
                                        &request);
      g_free (realm);

      gtk_widget_set_sensitive (request.connect_button, FALSE);
      gtk_widget_show (request.progress_grid);
      g_main_loop_run (request.loop);

      if (request.error)
        {
          set_entry_validation_error (GTK_ENTRY (request.realm_entry), request.error);
          g_clear_error (&request.error);
          goto start_over;
        }

      /* The realm was updated by on_discover_realm */
      clear_entry_validation_error (GTK_ENTRY (request.realm_entry));
      realm = get_realm (&request);
    }

  username = gtk_entry_get_text (GTK_ENTRY (request.username));

  g_free (principal);
  principal = g_strdup_printf ("%s@%s", username, realm);

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);

  if (!goa_utils_check_duplicate (client,
                                  principal,
                                  principal,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &request.error))
    goto out;

  /* If there isn't an account, then go ahead and create one
   */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Realm", realm);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "true");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  g_free (realm);

  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                principal,
                                principal,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &request);
  gtk_widget_set_sensitive (request.connect_button, FALSE);
  gtk_widget_show (request.progress_grid);
  g_main_loop_run (request.loop);
  if (request.error != NULL)
    goto out;

  object = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                         request.account_object_path));
  account = goa_object_peek_account (object);

  /* After the account is created, try to sign it in
   */
  perform_initial_sign_in (self, object, principal, &request);

  gtk_widget_set_sensitive (request.connect_button, FALSE);
  gtk_widget_show (request.progress_grid);

  g_main_loop_run (request.loop);

  gtk_widget_set_sensitive (request.connect_button, TRUE);
  gtk_widget_hide (request.progress_grid);

  if (request.error != NULL)
    {
      gchar *markup;

      translate_error (&request.error);

      if (!g_error_matches (request.error,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED))
        {
          markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                    _("Error connecting to enterprise identity server"),
                                    request.error->message);
          gtk_label_set_markup (GTK_LABEL (request.cluebar_label), markup);
          g_free (markup);

          gtk_button_set_label (GTK_BUTTON (request.connect_button), _("_Try Again"));
          gtk_widget_set_no_show_all (request.cluebar, FALSE);
          gtk_widget_show_all (request.cluebar);
        }
      g_clear_error (&request.error);

      /* If it couldn't be signed in, then delete it and start over
       */
      goa_account_call_remove (account,
                               NULL,
                               (GAsyncReadyCallback)
                               remove_account_cb,
                               request.loop);
      g_main_loop_run (request.loop);
      g_clear_object (&object);
      goto start_over;
    }

  goa_account_set_is_temporary (account, FALSE);

  gtk_widget_hide (GTK_WIDGET (dialog));

 out:
  /* We might have an object even when request.error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (request.error != NULL)
    {
      translate_error (&request.error);
      g_propagate_error (error, request.error);
    }
  else
    g_assert (object != NULL);

  if (request.interface_added_id != 0)
    g_signal_handler_disconnect (G_OBJECT (request.realm_manager), request.interface_added_id);

  g_cancellable_cancel (request.cancellable);
  g_object_unref (request.cancellable);

  g_clear_object (&request.realm_manager);

  if (request.realm_provider)
    {
      release_realmd (G_DBUS_PROXY (request.realm_provider));
      g_object_unref (request.realm_provider);
    }

  g_free (request.account_object_path);
  g_free (principal);
  g_clear_pointer (&request.loop, (GDestroyNotify) g_main_loop_unref);
  return object;
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
  g_clear_pointer (&iter, (GDestroyNotify) g_variant_iter_free);
  g_clear_pointer (&result, (GDestroyNotify) g_variant_unref);
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
      ticket_synced = get_ticket_sync (GOA_KERBEROS_PROVIDER (provider),
                                       object,
                                       FALSE /* Don't allow interaction */,
                                       cancellable,
                                       &lookup_error);
      g_mutex_lock (&identity_manager_mutex);

      if (!ticket_synced)
        {
          translate_error (&lookup_error);
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

static char *
sign_in_identity_sync (GoaKerberosProvider  *self,
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
                   GCR_ERROR,
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

static void
sign_in_thread (GTask               *task,
                GoaKerberosProvider *self,
                gpointer             task_data G_GNUC_UNUSED,
                GCancellable        *cancellable)
{
  const char *identifier;
  const char *password;
  const char *preauth_source;
  char *object_path;
  GError *error;

  identifier = g_task_get_source_tag (task);
  password = g_object_get_data (G_OBJECT (task), "password");
  preauth_source = g_object_get_data (G_OBJECT (task), "preauth-source");

  error = NULL;
  object_path = sign_in_identity_sync (self, identifier, password, preauth_source, cancellable, &error);
  if (object_path == NULL)
    g_task_return_error (task, error);
  else
    g_task_return_pointer (task, object_path, NULL);
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
  static volatile GQuark goa_identity_manager_error_domain = 0;
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
  goa_identity_manager_error_domain = GOA_IDENTITY_MANAGER_ERROR;
  goa_identity_manager_error_domain; /* shut up -Wunused-but-set-variable */
}
