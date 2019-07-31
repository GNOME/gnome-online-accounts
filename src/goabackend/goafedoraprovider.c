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
#include "goaprovider-priv.h"
#include "goafedoraprovider.h"
#include "goautils.h"
#include "goaidentity.h"
#include "goaidentitymanagererror.h"

#include <gcr/gcr.h>

#include "org.gnome.Identity.h"

struct _GoaFedoraProvider
{
  GoaProvider parent_instance;
};

static GoaIdentityServiceManager *identity_manager;
static GMutex identity_manager_mutex;
static GCond identity_manager_condition;

static GDBusObjectManager *object_manager;
static GMutex object_manager_mutex;
static GCond object_manager_condition;

static void ensure_identity_manager (void);
static void ensure_object_manager (void);

static char *sign_in_identity_sync (GoaFedoraProvider  *self,
                                    const char         *identifier,
                                    const char         *password,
                                    const char         *preauth_source,
                                    GCancellable       *cancellable,
                                    GError            **error);
static void sign_in_thread (GTask               *result,
                            GoaFedoraProvider   *self,
                            gpointer             task_data,
                            GCancellable        *cancellable);
static GoaIdentityServiceIdentity *get_identity_from_object_manager (GoaFedoraProvider *self,
                                                                     const char        *identifier);
static gboolean dbus_proxy_reload_properties_sync (GDBusProxy    *proxy,
                                                   GCancellable  *cancellable);

static void goa_fedora_provider_module_init (void);
static void create_object_manager (void);
static void create_identity_manager (void);

G_DEFINE_TYPE_WITH_CODE (GoaFedoraProvider, goa_fedora_provider, GOA_TYPE_PROVIDER,
                         goa_fedora_provider_module_init ();
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_FEDORA_NAME,
                                                         0));

static void
goa_fedora_provider_module_init (void)
{
  create_object_manager ();
  create_identity_manager ();
  g_debug ("activated Fedora provider");
}

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  gchar *identifier;
  gchar *password;
  gchar *preauth_source;
} SignInIdentityData;

static SignInIdentityData *
sign_in_identity_data_new (const gchar *identifier, const gchar *password, const gchar *preauth_source)
{
  SignInIdentityData *data;

  data = g_slice_new0 (SignInIdentityData);
  data->identifier = g_strdup (identifier);
  data->password = g_strdup (password);
  data->preauth_source = g_strdup (preauth_source);

  return data;
}

static void
sign_in_identity_data_free (SignInIdentityData *data)
{
  g_free (data->identifier);
  g_free (data->password);
  g_free (data->preauth_source);

  g_slice_free (SignInIdentityData, data);
}

static void
sign_in_identity (GoaFedoraProvider    *self,
                  const char           *identifier,
                  const char           *password,
                  const char           *preauth_source,
                  GCancellable         *cancellable,
                  GAsyncReadyCallback   callback,
                  gpointer              user_data)
{
  GTask *task;
  SignInIdentityData *data = NULL;

  data = sign_in_identity_data_new (identifier, password, preauth_source);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, data, (GDestroyNotify) sign_in_identity_data_free);
  g_task_run_in_thread (task, (GTaskThreadFunc) sign_in_thread);

  g_object_unref (task);
}

static gchar *
sign_in_identity_finish (GoaFedoraProvider    *self,
                         GAsyncResult         *result,
                         GError              **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_FEDORA_PROVIDER (self), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  task = G_TASK (result);

  return g_task_propagate_pointer (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_ticket_sync (GoaFedoraProvider   *self,
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
  g_clear_pointer (&credentials, g_variant_unref);
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
  got_ticket = get_ticket_sync (GOA_FEDORA_PROVIDER (provider),
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

  if (!GOA_PROVIDER_CLASS (goa_fedora_provider_parent_class)->build_object (provider,
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

typedef struct
{
  GCancellable *cancellable;
  GMainLoop *loop;

  GoaObject *object_temporary;

  GtkDialog *dialog;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *username;
  GtkWidget *password;

  gboolean waiting_for_temporary_account;

  gchar *account_object_path;
  gchar *identity;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

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

static gchar *
normalize_principal (const gchar *principal)
{
  gchar *domain = NULL;
  gchar *realm = NULL;
  gchar *ret = NULL;
  gchar *username = NULL;

  if (principal == 0 || principal[0] == '\0')
    goto out;

  if (!goa_utils_parse_email_address (principal, &username, &domain))
    {
      gchar *at;

      at = strchr (principal, '@');
      if (at != NULL)
        goto out;

      username = g_strdup (principal);
      domain = g_strdup (GOA_FEDORA_REALM);
    }

  realm = g_utf8_strup (domain, -1);
  if (g_strcmp0 (realm, GOA_FEDORA_REALM) != 0)
    goto out;

  ret = g_strconcat (username, "@", realm, NULL);

 out:
  g_free (domain);
  g_free (realm);
  g_free (username);
  return ret;
}

static void
on_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;
  gboolean can_add = FALSE;
  const gchar *username;
  gchar *principal = NULL;

  username = gtk_entry_get_text (GTK_ENTRY (data->username));
  principal = normalize_principal (username);
  if (principal == NULL)
    goto out;

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (principal);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkWidget      *vbox,
                           gboolean        new_account,
                           AddAccountData *data)
{
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *label;
  GtkWidget *spinner;
  gint row;
  gint width;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  data->cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_hexpand (data->cluebar, TRUE);
  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (grid0), data->cluebar);

  data->cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (data->cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_container_add (GTK_CONTAINER (grid0), grid1);

  row = 0;
  add_entry (grid1, row++, _("User_name"), &data->username);
  add_entry (grid1, row++, _("_Password"), &data->password);
  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus (data->username);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_username_or_password_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  data->progress_grid = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), data->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_opacity (spinner, 0.0);
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_set_opacity (label, 0.0);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

  gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
  gtk_window_set_default_size (GTK_WINDOW (data->dialog), width, -1);
}

static void
show_progress_ui (GtkContainer *container, gboolean progress)
{
  GList *children;
  GList *l;

  children = gtk_container_get_children (container);
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *widget = GTK_WIDGET (l->data);
      gdouble opacity;

      opacity = progress ? 1.0 : 0.0;
      gtk_widget_set_opacity (widget, opacity);
    }

  g_list_free (children);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;

  goa_manager_call_add_account_finish (manager, &data->account_object_path, res, &data->error);
  if (data->error != NULL)
    g_dbus_error_strip_remote_error (data->error);

  g_main_loop_quit (data->loop);
}

static void
client_account_added_cb (GoaClient *client, GoaObject *object, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;
  GoaAccount *account;
  const gchar *identity;
  const gchar *provider_type;

  account = goa_object_peek_account (object);
  g_return_if_fail (GOA_IS_ACCOUNT (account));

  if (!goa_account_get_is_temporary (account))
    goto out;

  provider_type = goa_account_get_provider_type (account);
  g_message ("client_account_added_cb: %s", provider_type);
  if (g_strcmp0 (provider_type, GOA_FEDORA_NAME) != 0)
    goto out;

  identity = goa_account_get_identity (account);
  g_message ("client_account_added_cb: %s", identity);
  if (g_strcmp0 (identity, data->identity) != 0)
    goto out;

  g_return_if_fail (data->object_temporary == NULL);
  data->object_temporary = g_object_ref (object);

  if (data->waiting_for_temporary_account)
    {
      g_main_loop_quit (data->loop);
      gtk_widget_set_sensitive (data->connect_button, TRUE);
      show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);

      data->waiting_for_temporary_account = FALSE;
    }

 out:
  return;
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = (AddAccountData *) user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

static void
sign_in_identity_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (source_object);
  AddAccountData *data = (AddAccountData *) user_data;
  gchar *object_path = NULL;

  object_path = sign_in_identity_finish (self, res, &data->error);
  if (data->error != NULL)
    g_dbus_error_strip_remote_error (data->error);

  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->connect_button, TRUE);
  show_progress_ui (GTK_CONTAINER (data->progress_grid), FALSE);

  g_free (object_path);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (provider);
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaAccount *account_temporary;
  GoaObject *ret = NULL;
  const gchar *id;
  const gchar *password;
  const gchar *provider_type;
  const gchar *username;
  gchar *principal = NULL;
  gint response;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  /* Needs data.identity to be set before signing in the principal
   * further below.
   */
  g_signal_connect (client, "account-added", G_CALLBACK (client_account_added_cb), &data);

  create_account_details_ui (provider, dialog, GTK_WIDGET (vbox), TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

 sign_in_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED, _("Dialog was dismissed"));
      goto out;
    }

  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));

  principal = normalize_principal (username);
  provider_type = goa_provider_get_provider_type (provider);

  if (!goa_utils_check_duplicate (client,
                                  principal,
                                  principal,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &data.error))
    goto out;

  if (!goa_utils_check_duplicate (client,
                                  principal,
                                  principal,
                                  GOA_KERBEROS_NAME,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &data.error))
    goto out;

  g_clear_object (&data.cancellable);
  data.cancellable = g_cancellable_new ();

  /* For the GoaClient::account-added handler. */
  g_free (data.identity);
  data.identity = g_strdup (principal);

  /* We currently don't prompt the user to choose a preauthentication
   * source during initial sign in so we assume there's no
   * preauthentication source
   */
  sign_in_identity (self, principal, password, NULL, data.cancellable, sign_in_identity_cb, &data);

  gtk_widget_set_sensitive (data.connect_button, FALSE);
  show_progress_ui (GTK_CONTAINER (data.progress_grid), TRUE);
  g_main_loop_run (data.loop);

  if (g_cancellable_is_cancelled (data.cancellable))
    {
      g_prefix_error (&data.error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data.error->domain),
                      data.error->code);
      data.error->domain = GOA_ERROR;
      data.error->code = GOA_ERROR_DIALOG_DISMISSED;
      goto out;
    }
  else if (data.error != NULL)
    {
      gchar *markup = NULL;

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));

      markup = g_strdup_printf ("<b>%s:</b>\n%s", _("Error connecting to Fedora"), data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_pointer (&principal, g_free);
      goto sign_in_again;
    }

  if (data.object_temporary == NULL)
    {
      data.waiting_for_temporary_account = TRUE;

      gtk_widget_set_sensitive (data.connect_button, FALSE);
      show_progress_ui (GTK_CONTAINER (data.progress_grid), TRUE);
      g_main_loop_run (data.loop);
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  account_temporary = goa_object_peek_account (data.object_temporary);
  id = goa_account_get_id (account_temporary);

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Id", id);
  g_variant_builder_add (&details, "{ss}", "IsTemporary", "false");
  g_variant_builder_add (&details, "{ss}", "TicketingEnabled", "true");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                provider_type,
                                principal,
                                principal,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  /* We might have an object even when data.error is set.
   * eg., if we failed to store the credentials in the keyring.
   */
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (ret != NULL);

  g_signal_handlers_disconnect_by_func (client, client_account_added_cb, &data);
  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, &data);

  g_free (data.account_object_path);
  g_free (data.identity);
  g_free (principal);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.object_temporary);
  g_clear_object (&data.cancellable);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider    *provider,
                 GoaClient      *client,
                 GoaObject      *object,
                 GtkWindow      *parent,
                 GError        **error)
{
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (provider);
  gboolean           got_ticket;
  GError            *ticket_error = NULL;

  g_return_val_if_fail (GOA_IS_FEDORA_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  got_ticket = get_ticket_sync (self, object, TRUE, NULL, &ticket_error);
  if (ticket_error != NULL)
    {
      g_dbus_error_strip_remote_error (ticket_error);
      g_propagate_error (error, ticket_error);
    }

  return got_ticket;
}

/* ---------------------------------------------------------------------------------------------------- */

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
  identity = get_identity_from_object_manager (GOA_FEDORA_PROVIDER (provider), identifier);
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
      ticket_synced = get_ticket_sync (GOA_FEDORA_PROVIDER (provider),
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
        identity = get_identity_from_object_manager (GOA_FEDORA_PROVIDER (provider), identifier);
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

  g_debug ("Fedora account %s removed and should now be signed out", identifier);

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
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (provider);
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
  GoaFedoraProvider *self = GOA_FEDORA_PROVIDER (provider);
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == remove_account, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaIdentityServiceIdentity *
get_identity_from_object_manager (GoaFedoraProvider *self,
                                  const char        *identifier)
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
sign_in_identity_sync (GoaFedoraProvider  *self,
                       const char         *identifier,
                       const char         *password,
                       const char         *preauth_source,
                       GCancellable       *cancellable,
                       GError            **error)
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
                GoaFedoraProvider   *self,
                gpointer             task_data,
                GCancellable        *cancellable)
{
  SignInIdentityData *data = (SignInIdentityData *) task_data;
  char *object_path;
  GError *error;

  error = NULL;
  object_path = sign_in_identity_sync (self,
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
      g_warning ("GoaFedoraProvider: Could not connect to identity service: %s", error->message);
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
      g_warning ("GoaFedoraProvider: Could not connect to identity service manager: %s", error->message);
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
goa_fedora_provider_init (GoaFedoraProvider *provider)
{
}

static void
goa_fedora_provider_class_init (GoaFedoraProviderClass *fedora_class)
{
  static volatile GQuark goa_identity_manager_error_domain = 0;
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (fedora_class);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
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
