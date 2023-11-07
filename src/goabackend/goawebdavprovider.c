/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#include <string.h>

#include <glib/gi18n-lib.h>

#include "goadavclient.h"
#include "goaprovider.h"
#include "goawebdavprovider.h"
#include "goawebdavprovider-priv.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

G_DEFINE_TYPE_WITH_CODE (GoaWebDavProvider, goa_webdav_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_WEBDAV_NAME,
							 0))


/* ---------------------------------------------------------------------------------------------------- */

static const char *
get_provider_type (GoaProvider *provider)
{
  return GOA_WEBDAV_NAME;
}

static char *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup ("WebDAV");
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_FILES;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("network-server-symbolic");
}

/* ---------------------------------------------------------------------------------------------------- */

static char *
uri_encode_identity (const char *uri_string,
                     const char *identity,
                     gboolean    for_vfs)
{
  g_autoptr (GUri) uri = NULL;
  const char *scheme = NULL;

  scheme = g_uri_peek_scheme (uri_string);
  if (scheme == NULL)
    return NULL;

  if (for_vfs)
    {
      if (g_str_equal (scheme, "davs") || g_str_equal (scheme, "https"))
        scheme = "davs";
      else if (g_str_equal (scheme, "dav") || g_str_equal (scheme, "http"))
        scheme = "dav";
      else
        return NULL;
    }

  uri = g_uri_parse (uri_string, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
  if (uri != NULL)
    {
      g_autoptr (GUri) uri_tmp = NULL;
      g_autofree char *encoded_identity = NULL;

      encoded_identity = g_uri_escape_string (identity, NULL, FALSE);
      uri_tmp = g_uri_build_with_user (g_uri_get_flags (uri),
                                       scheme,
                                       encoded_identity,
                                       g_uri_get_password (uri),
                                       g_uri_get_auth_params (uri),
                                       g_uri_get_host (uri),
                                       g_uri_get_port (uri),
                                       g_uri_get_path (uri),
                                       g_uri_get_query (uri),
                                       g_uri_get_fragment (uri));

      if (uri_tmp != NULL)
        return g_uri_to_string (uri_tmp);
    }

  return NULL;
}

/*< private >
 * dav_normalize_uri:
 * @base_uri: a URI
 * @uri_ref: (nullable): an absolute or relative URI
 * @server: (nullable) (out): location for server name
 *
 * Normalize a URI to http(s) with a trailing `/`.
 *
 * If @uri_ref is given it will be resolved relative to @base_uri, before
 * the trailing `/` is applied.
 *
 * If @server is not %NULL, it will be set to a presentable name.
 *
 * Returns: (transfer full): a new URI, or %NULL
 */
static char *
dav_normalize_uri (const char  *base_uri,
                   const char  *uri_ref,
                   char       **server)
{
  g_autoptr (GUri) uri = NULL;
  g_autoptr (GUri) uri_out = NULL;
  const char *scheme;
  const char *path;
  g_autofree char *new_path = NULL;
  g_autofree char *uri_string = NULL;
  int std_port = 0;

  /* dav(s) is used by DNS-SD and gvfs */
  scheme = g_uri_peek_scheme (base_uri);
  if (scheme == NULL)
    {
      uri_string = g_strconcat ("https://", base_uri, NULL);
      scheme = "https";
      std_port = 443;
    }
  else if (g_str_equal (scheme, "https")
           || g_str_equal (scheme, "davs"))
    {
      uri_string = g_strdup (base_uri);
      scheme = "https";
      std_port = 443;
    }
  else if (g_str_equal (scheme, "http")
           || g_str_equal (scheme, "dav"))
    {
      uri_string = g_strdup (base_uri);
      scheme = "http";
      std_port = 80;
    }
  else
    {
      g_critical ("Unsupported URI scheme \"%s\"", scheme);
      return NULL;
    }

  uri = g_uri_parse (uri_string, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
  if (uri == NULL)
    return NULL;

  if (uri_ref != NULL)
    {
      uri_out = g_uri_parse_relative (uri, uri_ref, G_URI_FLAGS_ENCODED | G_URI_FLAGS_PARSE_RELAXED, NULL);
      if (uri_out == NULL)
        return NULL;

      g_uri_unref (uri);
      uri = g_steal_pointer (&uri_out);
    }

  path = g_uri_get_path (uri);
  if (!g_str_has_suffix (path, "/"))
    new_path = g_strconcat (path, "/", NULL);

  uri_out = g_uri_build (g_uri_get_flags (uri),
                         scheme,
                         g_uri_get_userinfo (uri),
                         g_uri_get_host (uri),
                         g_uri_get_port (uri),
                         new_path ? new_path : path,
                         g_uri_get_query (uri),
                         g_uri_get_fragment (uri));

  if (server != NULL)
    {
      g_autofree char *port_string = NULL;
      g_autofree char *pretty_path = NULL;
      int port;

      port = g_uri_get_port (uri_out);
      port_string = g_strdup_printf (":%d", port);

      path = g_uri_get_path (uri_out);
      pretty_path = g_strdup (path);
      pretty_path[strlen(pretty_path) - 1] = '\0';

      *server = g_strconcat (g_uri_get_host (uri), (port == std_port || port == -1) ? "" : port_string, pretty_path, NULL);
    }

  return g_uri_to_string (uri_out);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const char            *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const char          *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account = NULL;
  g_autoptr (GoaPasswordBased) password_based = NULL;
  g_autofree char *uri_encoded = NULL;
  g_autofree char *uri_caldav = NULL;
  g_autofree char *uri_carddav = NULL;
  g_autofree char *uri_vfs = NULL;
  const char *identity;
  gboolean accept_ssl_errors;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean files_enabled;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_webdav_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    return FALSE;

  password_based = goa_object_get_password_based (GOA_OBJECT (object));
  if (password_based == NULL)
    {
      password_based = goa_password_based_skeleton_new ();
      /* Ensure D-Bus method invocations run in their own thread */
      g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (password_based),
                                           G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
      goa_object_skeleton_set_password_based (object, password_based);
      g_signal_connect (password_based,
                        "handle-get-password",
                        G_CALLBACK (on_handle_get_password),
                        NULL);
    }

  account = goa_object_get_account (GOA_OBJECT (object));
  identity = goa_account_get_identity (account);
  accept_ssl_errors = g_key_file_get_boolean (key_file, group, "AcceptSslErrors", NULL);

  /* Calendar */
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  uri_caldav = g_key_file_get_string (key_file, group, "CalDavUri", NULL);
  uri_encoded = uri_encode_identity (uri_caldav, identity, FALSE);
  goa_object_skeleton_attach_calendar (object, uri_encoded, calendar_enabled, accept_ssl_errors);
  g_clear_pointer (&uri_encoded, g_free);

  /* Contacts */
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  uri_carddav = g_key_file_get_string (key_file, group, "CardDavUri", NULL);
  uri_encoded = uri_encode_identity (uri_carddav, identity, FALSE);
  goa_object_skeleton_attach_contacts (object, uri_encoded, contacts_enabled, accept_ssl_errors);
  g_clear_pointer (&uri_encoded, g_free);

  /* Files */
  files_enabled = g_key_file_get_boolean (key_file, group, "FilesEnabled", NULL);
  uri_vfs = g_key_file_get_string (key_file, group, "Uri", NULL);
  uri_encoded = uri_encode_identity (uri_vfs, identity, TRUE);
  goa_object_skeleton_attach_files (object, uri_encoded, files_enabled, accept_ssl_errors);
  g_clear_pointer (&uri_encoded, g_free);

  if (just_added)
    {
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);
      goa_account_set_files_disabled (account, !files_enabled);

      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "ContactsEnabled");
      g_signal_connect (account,
                        "notify::files-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "FilesEnabled");
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  g_autoptr (GoaDavClient) dav_client = NULL;
  g_autofree char *username = NULL;
  g_autofree char *password = NULL;
  g_autofree char *uri = NULL;
  gboolean accept_ssl_errors;
  gboolean ret = FALSE;

  if (!goa_utils_get_credentials (provider, object, "password", &username, &password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }

      return ret;
    }

  /* All WebDAV servers support some file access, so check that URI */
  uri = goa_util_lookup_keyfile_string (object, "Uri");
  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");

  dav_client = goa_dav_client_new ();
  ret = goa_dav_client_check_sync (dav_client,
                                   uri,
                                   username,
                                   password,
                                   accept_ssl_errors,
                                   cancellable,
                                   error);

  if (!ret && error != NULL && *error != NULL)
    {
      g_prefix_error (error,
                      /* Translators: the first %s is the username
                       * (eg., debarshi.ray@gmail.com or rishi), and the
                       * (%s, %d) is the error domain and code.
                       */
                      _("Invalid password with username “%s” (%s, %d): "),
                      username,
                      g_quark_to_string ((*error)->domain),
                      (*error)->code);
      (*error)->domain = GOA_ERROR;
      (*error)->code = GOA_ERROR_NOT_AUTHORIZED;

      return ret;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *account_details;
  GtkWidget *advanced_details;
  GtkWidget *progress_grid;

  GtkWidget *uri;
  GtkWidget *username;
  GtkWidget *password;
  GtkWidget *webdav_uri;
  GtkWidget *caldav_uri;
  GtkWidget *carddav_uri;
  gboolean accept_ssl_errors;

  char *account_object_path;

  GoaDavConfiguration *config;
  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *grid,
           int            row,
           const char    *text,
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
on_uri_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add = FALSE;
  const char *address;
  g_autofree char *uri = NULL;

  address = gtk_entry_get_text (GTK_ENTRY (data->uri));
  uri = dav_normalize_uri (address, NULL, NULL);

  if (uri != NULL)
    {
      can_add = gtk_entry_get_text_length (GTK_ENTRY (data->username)) != 0
                && gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;
    }

  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           gboolean        new_account,
                           gboolean        is_template,
                           AddAccountData *data)
{
  GtkWidget *grid0;
  GtkWidget *expander;
  GtkWidget *label;
  GtkWidget *spinner;
  int row;
  int width;

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

  data->account_details = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (data->account_details), 12);
  gtk_grid_set_row_spacing (GTK_GRID (data->account_details), 12);
  gtk_container_add (GTK_CONTAINER (grid0), data->account_details);

  row = 0;
  add_entry (data->account_details, row++, _("_URL"), &data->uri);
  add_entry (data->account_details, row++, _("User_name"), &data->username);
  add_entry (data->account_details, row++, _("_Password"), &data->password);
  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  expander = gtk_expander_new (_("Advanced"));
  gtk_container_add (GTK_CONTAINER (grid0), expander);

  data->advanced_details = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (data->advanced_details), 12);
  gtk_grid_set_row_spacing (GTK_GRID (data->advanced_details), 12);
  gtk_container_add (GTK_CONTAINER (expander), data->advanced_details);

  row = 0;
  add_entry (data->advanced_details, row++, _("Files Endpoint"), &data->webdav_uri);
  add_entry (data->advanced_details, row++, _("CalDAV Endpoint"), &data->caldav_uri);
  add_entry (data->advanced_details, row++, _("CardDAV Endpoint"), &data->carddav_uri);

  if (new_account)
    gtk_widget_grab_focus (data->uri);
  else if (is_template)
    gtk_widget_grab_focus (data->username);
  else
    gtk_widget_grab_focus (data->password);

  g_signal_connect (data->uri, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);

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

  if (new_account)
    {
      gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
      gtk_window_set_default_size (GTK_WINDOW (data->dialog), width, -1);
    }
  else
    {
      GtkWindow *parent;

      /* Keep in sync with GoaPanelAddAccountDialog in
       * gnome-control-center.
       */
      parent = gtk_window_get_transient_for (GTK_WINDOW (data->dialog));
      if (parent != NULL)
        {
          gtk_window_get_size (parent, &width, NULL);
          gtk_window_set_default_size (GTK_WINDOW (data->dialog), (int) (0.5 * width), -1);
        }
    }
}

static void
show_progress_ui (AddAccountData *data,
                  gboolean        active)
{
  GList *children;
  GList *l;

  children = gtk_container_get_children (GTK_CONTAINER (data->progress_grid));
  for (l = children; l != NULL; l = l->next)
    {
      GtkWidget *widget = GTK_WIDGET (l->data);

      gtk_widget_set_opacity (widget, active ? 1.0 : 0.0);
    }

  g_list_free (children);
  gtk_widget_set_sensitive (data->connect_button, !active);
  gtk_widget_set_sensitive (data->account_details, !active);
  gtk_widget_set_sensitive (data->advanced_details, !active);
}

static gboolean
show_error_ui (AddAccountData *data,
               const char     *summary,
               int            *err_out)
{
  /* We may have been cancelled with or without an error */
  if (g_cancellable_is_cancelled (data->cancellable))
    {
      g_prefix_error (&data->error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data->error->domain),
                      data->error->code);
      data->error->domain = GOA_ERROR;
      data->error->code = GOA_ERROR_DIALOG_DISMISSED;
    }
  else if (data->error != NULL)
    {
      g_autofree char *markup = NULL;

      if (data->error->code == GOA_ERROR_SSL)
        {
          gtk_button_set_label (GTK_BUTTON (data->connect_button), _("_Ignore"));
          data->accept_ssl_errors = TRUE;
        }
      else
        {
          gtk_button_set_label (GTK_BUTTON (data->connect_button), _("_Try Again"));
          data->accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s:</b>\n%s", summary, data->error->message);
      gtk_label_set_markup (GTK_LABEL (data->cluebar_label), markup);

      gtk_widget_set_no_show_all (data->cluebar, FALSE);
      gtk_widget_show_all (data->cluebar);
    }

  /* Stop indicating progress and report the error code */
  if (data->error != NULL)
    {
      show_progress_ui (data, FALSE);

      if (err_out)
        *err_out = data->error->code;

      return TRUE;
    }

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *res,
                gpointer      user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

static void
check_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GoaDavClient *client = GOA_DAV_CLIENT (source_object);
  AddAccountData *data = user_data;

  goa_dav_client_check_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
}

static void
discover_cb (GObject      *source_object,
             GAsyncResult *res,
             gpointer      user_data)
{
  GoaDavClient *client = GOA_DAV_CLIENT (source_object);
  AddAccountData *data = user_data;

  g_clear_pointer (&data->config, goa_dav_configuration_free);
  data->config = goa_dav_client_discover_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
}

static void
dialog_response_cb (GtkDialog *dialog,
                    gint       response_id,
                    gpointer   user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaObject *ret = NULL;
  const char *uri_text;
  const char *password;
  const char *username;
  const char *provider_type;
  const char *caldav_text;
  const char *carddav_text;
  const char *webdav_text;
  g_autoptr (GoaDavClient) dav_client = NULL;
  g_autofree char *presentation_identity = NULL;
  g_autofree char *server = NULL;
  g_autofree char *uri = NULL;
  int response;
  int err = 0;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, FALSE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  dav_client = goa_dav_client_new ();

check_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  uri_text = gtk_entry_get_text (GTK_ENTRY (data.uri));
  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));

  g_clear_pointer (&uri, g_free);
  g_clear_pointer (&server, g_free);
  g_clear_pointer (&presentation_identity, g_free);

  uri = dav_normalize_uri (uri_text, NULL, &server);
  if (strchr (username, '@') != NULL)
    presentation_identity = g_strdup (username);
  else
    presentation_identity = g_strconcat (username, "@", server, NULL);

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  username,
                                  presentation_identity,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &data.error))
    goto out;

  g_clear_object (&data.cancellable);
  data.cancellable = g_cancellable_new ();

  g_clear_pointer (&data.config, goa_dav_configuration_free);
  goa_dav_client_discover (dav_client,
                           uri,
                           username,
                           password,
                           data.accept_ssl_errors,
                           data.cancellable,
                           discover_cb,
                           &data);
  show_progress_ui (&data, TRUE);
  g_main_loop_run (data.loop);

  if (show_error_ui (&data, _("Error connecting to server"), &err))
    {
      if (err == GOA_ERROR_DIALOG_DISMISSED)
        goto out;

      g_clear_error (&data.error);
      goto check_again;
    }

  /* WebDAV: discover so we get the redirected URI for GVfs, but don't
   * override previously discovered CalDAV/CardDAV endpoints.
   */
  webdav_text = gtk_entry_get_text (GTK_ENTRY (data.webdav_uri));
  if (webdav_text != NULL && *webdav_text != '\0')
    {
      GoaDavConfiguration *config = g_steal_pointer (&data.config);
      g_autofree char *check_uri = NULL;

      check_uri = dav_normalize_uri (uri, webdav_text, NULL);
      goa_dav_client_discover (dav_client,
                               check_uri,
                               username,
                               password,
                               data.accept_ssl_errors,
                               data.cancellable,
                               discover_cb,
                               &data);
      g_main_loop_run (data.loop);

      if (show_error_ui (&data, _("Error connecting to Files endpoint"), &err))
        {
          if (err == GOA_ERROR_DIALOG_DISMISSED)
            goto out;

          g_clear_error (&data.error);
          goto check_again;
        }

      g_set_str (&config->webdav_uri, data.config->webdav_uri);
      g_clear_pointer (&data.config, goa_dav_configuration_free);
      data.config = g_steal_pointer (&config);
    }

  /* CalDAV/CardDAV: user-defined URIs override discovered services even if
   * the server doesn't claim support. They may be absolute or relative to
   * the server URI.
   */
  caldav_text = gtk_entry_get_text (GTK_ENTRY (data.caldav_uri));
  if (caldav_text != NULL && *caldav_text != '\0')
    {
      g_autofree char *check_uri = NULL;

      check_uri = dav_normalize_uri (uri, caldav_text, NULL);
      goa_dav_client_check (dav_client,
                            check_uri,
                            username,
                            password,
                            data.accept_ssl_errors,
                            data.cancellable,
                            check_cb,
                            &data);
      g_main_loop_run (data.loop);

      if (show_error_ui (&data, _("Error connecting to CalDAV endpoint"), &err))
        {
          if (err == GOA_ERROR_DIALOG_DISMISSED)
            goto out;

          g_clear_error (&data.error);
          goto check_again;
        }

      g_set_str (&data.config->caldav_uri, check_uri);
    }

  carddav_text = gtk_entry_get_text (GTK_ENTRY (data.carddav_uri));
  if (carddav_text != NULL && *carddav_text != '\0')
    {
      g_autofree char *check_uri = NULL;

      check_uri = dav_normalize_uri (uri, carddav_text, NULL);
      goa_dav_client_check (dav_client,
                            check_uri,
                            username,
                            password,
                            data.accept_ssl_errors,
                            data.cancellable,
                            check_cb,
                            &data);
      g_main_loop_run (data.loop);

      if (show_error_ui (&data, _("Error connecting to CardDAV endpoint"), &err))
        {
          if (err == GOA_ERROR_DIALOG_DISMISSED)
            goto out;

          g_clear_error (&data.error);
          goto check_again;
        }

      g_set_str (&data.config->carddav_uri, check_uri);
    }

  g_debug ("%s(): configured \"%s\" (WebDAV %s, CalDAV %s, CardDAV %s)",
           G_STRFUNC, server,
           data.config->webdav_uri,
           data.config->caldav_uri,
           data.config->carddav_uri);

  show_progress_ui (&data, FALSE);
  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Uri", data.config->webdav_uri);
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", data.config->caldav_uri ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "CalDavUri", data.config->caldav_uri ? data.config->caldav_uri : "");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", data.config->carddav_uri ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "CardDavUri", data.config->carddav_uri ? data.config->carddav_uri : "");
  g_variant_builder_add (&details, "{ss}", "FilesEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (data.accept_ssl_errors) ? "true" : "false");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                username,
                                presentation_identity,
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

  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, &data);

  g_clear_pointer (&data.account_object_path, g_free);
  g_clear_pointer (&data.config, goa_dav_configuration_free);
  g_clear_pointer (&data.loop, g_main_loop_unref);
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
  AddAccountData data;
  GVariantBuilder credentials;
  GoaAccount *account;
  g_autoptr (GoaDavClient) dav_client = NULL;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean is_template = FALSE;
  gboolean ret = FALSE;
  const char *password;
  const char *username;
  g_autofree char *uri = NULL;
  int err = 0;
  int response;

  g_return_val_if_fail (GOA_IS_WEBDAV_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL
                                        | GTK_DIALOG_DESTROY_WITH_PARENT
                                        | GTK_DIALOG_USE_HEADER_BAR,
                                        NULL,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (vbox), 12);

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = GTK_DIALOG (dialog);
  data.error = NULL;

  account = goa_object_peek_account (object);
  username = goa_account_get_identity (account);
  if (username == NULL || username[0] == '\0')
    is_template = TRUE;

  create_account_details_ui (provider, GTK_DIALOG (dialog), GTK_BOX (vbox), FALSE, is_template, &data);

  data.accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  uri = goa_util_lookup_keyfile_string (object, "Uri");
  gtk_entry_set_text (GTK_ENTRY (data.uri), uri);
  gtk_editable_set_editable (GTK_EDITABLE (data.uri), FALSE);

  if (!is_template)
    {
      gtk_entry_set_text (GTK_ENTRY (data.username), username);
      gtk_editable_set_editable (GTK_EDITABLE (data.username), FALSE);
    }

  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  dav_client = goa_dav_client_new ();

check_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  if (is_template)
    username = gtk_entry_get_text (GTK_ENTRY (data.username));

  password = gtk_entry_get_text (GTK_ENTRY (data.password));

  g_clear_object (&data.cancellable);
  data.cancellable = g_cancellable_new ();

  goa_dav_client_check (dav_client,
                        uri,
                        username,
                        password,
                        data.accept_ssl_errors,
                        data.cancellable,
                        check_cb,
                        &data);
  show_progress_ui (&data, TRUE);
  g_main_loop_run (data.loop);

  if (show_error_ui (&data, _("Error connecting to WebDAV server"), &err))
    {
      if (err == GOA_ERROR_DIALOG_DISMISSED)
        goto out;

      g_clear_error (&data.error);
      goto check_again;
    }

  /* TODO: run in worker thread */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  if (is_template)
    {
      GVariantBuilder details;
      GoaManager *manager;
      const char *id;
      const char *provider_type;
      g_autofree char *dummy = NULL;
      g_autofree char *presentation_identity = NULL;
      g_autofree char *server = NULL;

      manager = goa_client_get_manager (client);
      id = goa_account_get_id (account);
      provider_type = goa_provider_get_provider_type (provider);

      dummy = dav_normalize_uri (uri, NULL, &server);
      presentation_identity = g_strconcat (username, "@", server, NULL);

      g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
      g_variant_builder_add (&details, "{ss}", "Id", id);

      goa_manager_call_add_account (manager,
                                    provider_type,
                                    username,
                                    presentation_identity,
                                    g_variant_builder_end (&credentials),
                                    g_variant_builder_end (&details),
                                    NULL, /* GCancellable* */
                                    (GAsyncReadyCallback) add_account_cb,
                                    &data);

      g_main_loop_run (data.loop);
      if (data.error != NULL)
        goto out;
    }
  else
    {
      if (!goa_utils_store_credentials_for_object_sync (provider,
                                                        object,
                                                        g_variant_builder_end (&credentials),
                                                        NULL, /* GCancellable */
                                                        &data.error))
        goto out;
    }

  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);

  gtk_widget_destroy (dialog);
  g_clear_pointer (&data.account_object_path, g_free);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.cancellable);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_webdav_provider_init (GoaWebDavProvider *self)
{
}

static void
goa_webdav_provider_class_init (GoaWebDavProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const char            *id, /* unused */
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  const char *account_id;
  const char *method_name;
  const char *provider_type;
  const char *sender;
  g_autoptr (GoaProvider) provider = NULL;
  g_autofree char *password = NULL;
  GError *error = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  account_id = goa_account_get_id (account);
  provider_type = goa_account_get_provider_type (account);
  method_name = g_dbus_method_invocation_get_method_name (invocation);
  sender = g_dbus_method_invocation_get_sender (invocation);

  g_debug ("Handling %s from %s for account (%s, %s)", method_name, sender, provider_type, account_id);

  provider = goa_provider_get_for_provider_type (provider_type);
  if (goa_utils_get_credentials (provider, object, "password", NULL, &password, NULL, &error))
    goa_password_based_complete_get_password (interface, invocation, password);
  else
    g_dbus_method_invocation_take_error (invocation, error);

  return TRUE; /* invocation was handled */
}
