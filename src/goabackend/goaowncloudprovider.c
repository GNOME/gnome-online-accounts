/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012, 2013, 2014 Red Hat, Inc.
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

#include <libsoup/soup.h>

#include "goahttpclient.h"
#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaowncloudprovider.h"
#include "goautils.h"

/**
 * GoaOwncloudProvider:
 *
 * The #GoaOwncloudProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaOwncloudProvider
{
  /*< private >*/
  GoaProvider parent_instance;
};

typedef struct _GoaOwncloudProviderClass GoaOwncloudProviderClass;

struct _GoaOwncloudProviderClass
{
  GoaProviderClass parent_class;
};

/**
 * SECTION:goaowncloudprovider
 * @title: GoaOwncloudProvider
 * @short_description: A provider for ownCloud servers
 *
 * #GoaOwncloudProvider is used to access ownCloud servers.
 */

G_DEFINE_TYPE_WITH_CODE (GoaOwncloudProvider, goa_owncloud_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_OWNCLOUD_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *CALDAV_ENDPOINT = "remote.php/caldav/";
static const gchar *CARDDAV_ENDPOINT = "remote.php/carddav/";
static const gchar *WEBDAV_ENDPOINT = "remote.php/webdav/";

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_OWNCLOUD_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("ownCloud"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_DOCUMENTS |
         GOA_PROVIDER_FEATURE_FILES;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean on_handle_get_password (GoaPasswordBased      *interface,
                                        GDBusMethodInvocation *invocation,
                                        const gchar           *id,
                                        gpointer               user_data);

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaCalendar *calendar;
  GoaContacts *contacts;
  GoaDocuments *documents;
  GoaFiles *files;
  GoaPasswordBased *password_based;
  SoupURI *uri;
  gboolean accept_ssl_errors;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean documents_enabled;
  gboolean files_enabled;
  gboolean ret;
  const gchar *identity;
  gchar *uri_string;

  account = NULL;
  calendar = NULL;
  contacts = NULL;
  documents = NULL;
  files = NULL;
  password_based = NULL;
  uri = NULL;
  uri_string = NULL;

  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_owncloud_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

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
  uri_string = g_key_file_get_string (key_file, group, "Uri", NULL);
  uri = soup_uri_new (uri_string);
  if (uri != NULL)
    soup_uri_set_user (uri, identity);

  accept_ssl_errors = g_key_file_get_boolean (key_file, group, "AcceptSslErrors", NULL);

  /* Calendar */
  calendar = goa_object_get_calendar (GOA_OBJECT (object));
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  if (calendar_enabled)
    {
      if (calendar == NULL)
        {
          gchar *uri_caldav;

          uri_caldav = NULL;

          if (uri != NULL)
            {
              gchar *uri_tmp;

              uri_tmp = soup_uri_to_string (uri, FALSE);
              uri_caldav = g_strconcat (uri_tmp, CALDAV_ENDPOINT, NULL);
              g_free (uri_tmp);
            }

          calendar = goa_calendar_skeleton_new ();
          g_object_set (G_OBJECT (calendar),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri_caldav,
                        NULL);
          goa_object_skeleton_set_calendar (object, calendar);
          g_free (uri_caldav);
        }
    }
  else
    {
      if (calendar != NULL)
        goa_object_skeleton_set_calendar (object, NULL);
    }

  /* Contacts */
  contacts = goa_object_get_contacts (GOA_OBJECT (object));
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  if (contacts_enabled)
    {
      if (contacts == NULL)
        {
          gchar *uri_carddav;

          uri_carddav = NULL;

          if (uri != NULL)
            {
              gchar *uri_tmp;

              uri_tmp = soup_uri_to_string (uri, FALSE);
              uri_carddav = g_strconcat (uri_tmp, CARDDAV_ENDPOINT, NULL);
              g_free (uri_tmp);
            }

          contacts = goa_contacts_skeleton_new ();
          g_object_set (G_OBJECT (contacts),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri_carddav,
                        NULL);
          goa_object_skeleton_set_contacts (object, contacts);
          g_free (uri_carddav);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }

  /* Documents */
  documents = goa_object_get_documents (GOA_OBJECT (object));
  documents_enabled = g_key_file_get_boolean (key_file, group, "DocumentsEnabled", NULL);

  if (documents_enabled)
    {
      if (documents == NULL)
        {
          documents = goa_documents_skeleton_new ();
          goa_object_skeleton_set_documents (object, documents);
        }
    }
  else
    {
      if (documents != NULL)
        goa_object_skeleton_set_documents (object, NULL);
    }

  /* Files */
  files = goa_object_get_files (GOA_OBJECT (object));
  files_enabled = g_key_file_get_boolean (key_file, group, "FilesEnabled", NULL);
  if (files_enabled)
    {
      if (files == NULL)
        {
          gchar *uri_webdav;

          uri_webdav = NULL;

          if (uri != NULL)
            {
              const gchar *scheme;
              gchar *uri_tmp;

              scheme = soup_uri_get_scheme (uri);
              if (g_strcmp0 (scheme, SOUP_URI_SCHEME_HTTPS) == 0)
                soup_uri_set_scheme (uri, "davs");
              else
                soup_uri_set_scheme (uri, "dav");

              uri_tmp = soup_uri_to_string (uri, FALSE);
              uri_webdav = g_strconcat (uri_tmp, WEBDAV_ENDPOINT, NULL);
              g_free (uri_tmp);
            }

          files = goa_files_skeleton_new ();
          g_object_set (G_OBJECT (files),
                        "accept-ssl-errors", accept_ssl_errors,
                        "uri", uri_webdav,
                        NULL);
          goa_object_skeleton_set_files (object, files);
          g_free (uri_webdav);
        }
    }
  else
    {
      if (files != NULL)
        goa_object_skeleton_set_files (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);
      goa_account_set_documents_disabled (account, !documents_enabled);
      goa_account_set_files_disabled (account, !files_enabled);

      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ContactsEnabled");
      g_signal_connect (account,
                        "notify::documents-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "DocumentsEnabled");
      g_signal_connect (account,
                        "notify::files-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "FilesEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&calendar);
  g_clear_object (&contacts);
  g_clear_object (&files);
  g_clear_object (&password_based);
  g_clear_pointer (&uri, (GDestroyNotify *) soup_uri_free);
  g_free (uri_string);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  GVariant *credentials;
  GoaAccount *account;
  GoaHttpClient *http_client;
  gboolean accept_ssl_errors;
  gboolean ret;
  const gchar *username;
  gchar *password;
  gchar *uri;
  gchar *uri_webdav;

  credentials = NULL;
  http_client = NULL;
  password = NULL;
  uri = NULL;
  uri_webdav = NULL;

  ret = FALSE;

  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   cancellable,
                                                   error);
  if (credentials == NULL)
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  uri = goa_util_lookup_keyfile_string (object, "Uri");
  uri_webdav = g_strconcat (uri, WEBDAV_ENDPOINT, NULL);

  account = goa_object_peek_account (object);
  username = goa_account_get_identity (account);

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      if (error != NULL)
        {
          *error = g_error_new (GOA_ERROR,
                                GOA_ERROR_NOT_AUTHORIZED,
                                _("Did not find password with identity ‘%s’ in credentials"),
                                username);
        }
      goto out;
    }

  http_client = goa_http_client_new ();
  ret = goa_http_client_check_sync (http_client,
                                    uri_webdav,
                                    username,
                                    password,
                                    accept_ssl_errors,
                                    cancellable,
                                    error);
  if (!ret)
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is the username
                           * (eg., debarshi.ray@gmail.com or rishi), and the
                           * (%s, %d) is the error domain and code.
                           */
                          _("Invalid password with username ‘%s’ (%s, %d): "),
                          username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

 out:
  g_clear_object (&http_client);
  g_free (password);
  g_free (uri);
  g_free (uri_webdav);
  if (credentials != NULL)
    g_variant_unref (credentials);
  return ret;
}

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *uri;
  GtkWidget *username;
  GtkWidget *password;

  gchar *account_object_path;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
normalize_uri (const gchar *address, gchar **server)
{
  SoupURI *uri;
  const gchar *path;
  gchar *scheme;
  gchar *ret;
  gchar *uri_string;
  guint std_port;

  uri = NULL;
  uri_string = NULL;
  scheme = g_uri_parse_scheme (address);
  std_port = 0;

  ret = NULL;

  if (g_strcmp0 (scheme, "http") == 0
      || g_strcmp0 (scheme, "dav") == 0)   /* dav(s) is used by DNS-SD
                                              and gvfs */
    {
      uri_string = g_strdup (address);
      std_port = 80;
    }
  else if (g_strcmp0 (scheme, "https") == 0
           || g_strcmp0 (scheme, "davs") == 0)
    {
      uri_string = g_strdup (address);
      std_port = 443;
    }
  else if (scheme == NULL)
    {
      uri_string = g_strconcat ("https://", address, NULL);
      std_port = 443;
    }
  else
    goto out;

  uri = soup_uri_new (uri_string);
  if (uri == NULL)
    goto out;

  if (g_strcmp0 (scheme, "dav") == 0)
    soup_uri_set_scheme (uri, SOUP_URI_SCHEME_HTTP);
  else if (g_strcmp0 (scheme, "davs") == 0)
    soup_uri_set_scheme (uri, SOUP_URI_SCHEME_HTTPS);

  path = soup_uri_get_path (uri);
  if (!g_str_has_suffix (path, "/"))
    {
      gchar *new_path;

      new_path = g_strconcat (path, "/", NULL);
      soup_uri_set_path (uri, new_path);
      path = soup_uri_get_path (uri);
      g_free (new_path);
    }

  if (server != NULL)
    {
      gchar *port_string;
      gchar *pretty_path;
      guint port;

      port = soup_uri_get_port (uri);
      port_string = g_strdup_printf (":%u", port);

      pretty_path = g_strdup (path);
      pretty_path[strlen(pretty_path) - 1] = '\0';

      *server = g_strconcat (soup_uri_get_host (uri), (port == std_port) ? "" : port_string, pretty_path, NULL);

      g_free (port_string);
      g_free (pretty_path);
    }

  ret = soup_uri_to_string (uri, FALSE);

 out:
  g_clear_pointer (&uri, (GDestroyNotify *) soup_uri_free);
  g_free (scheme);
  g_free (uri_string);
  return ret;
}

static void
on_uri_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add;
  const gchar *address;
  gchar *uri;

  can_add = FALSE;
  uri = NULL;

  address = gtk_entry_get_text (GTK_ENTRY (data->uri));
  uri = normalize_uri (address, NULL);
  if (uri == NULL)
    goto out;

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->username)) != 0
            && gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (uri);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
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
  add_entry (grid1, row++, _("_Server"), &data->uri);
  add_entry (grid1, row++, _("User_name"), &data->username);
  add_entry (grid1, row++, _("_Password"), &data->password);
  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus ((new_account) ? data->uri : data->password);

  g_signal_connect (data->uri, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  data->progress_grid = gtk_grid_new ();
  gtk_widget_set_no_show_all (data->progress_grid, TRUE);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), data->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_widget_show (spinner);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_show (label);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

  if (new_account)
    {
      gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
      gtk_widget_set_size_request (GTK_WIDGET (data->dialog), width, -1);
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
          gtk_widget_set_size_request (GTK_WIDGET (data->dialog), (gint) (0.5 * width), -1);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
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
  GoaHttpClient *client = GOA_HTTP_CLIENT (source_object);
  AddAccountData *data = user_data;

  goa_http_client_check_finish (client, res, &data->error);
  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->connect_button, TRUE);
  gtk_widget_hide (data->progress_grid);
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
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
  GoaHttpClient *http_client;
  GoaObject *ret;
  gboolean accept_ssl_errors;
  const gchar *uri_text;
  const gchar *password;
  const gchar *username;
  const gchar *provider_type;
  gchar *presentation_identity;
  gchar *server;
  gchar *uri;
  gchar *uri_webdav;
  gint response;

  http_client = NULL;
  accept_ssl_errors = FALSE;
  presentation_identity = NULL;
  server = NULL;
  uri = NULL;

  ret = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  http_client = goa_http_client_new ();

 http_again:
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

  uri = normalize_uri (uri_text, &server);
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

  uri_webdav = g_strconcat (uri, WEBDAV_ENDPOINT, NULL);
  g_cancellable_reset (data.cancellable);
  goa_http_client_check (http_client,
                         uri_webdav,
                         username,
                         password,
                         accept_ssl_errors,
                         data.cancellable,
                         check_cb,
                         &data);
  g_free (uri_webdav);

  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
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
      gchar *markup;

      if (data.error->code == GOA_ERROR_SSL)
        {
          gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Ignore"));
          accept_ssl_errors = TRUE;
        }
      else
        {
          gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));
          accept_ssl_errors = FALSE;
        }

      markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                _("Error connecting to ownCloud server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);

      g_clear_pointer (&presentation_identity, g_free);
      g_clear_pointer (&server, g_free);
      g_clear_pointer (&uri, g_free);
      goto http_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "DocumentsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "FilesEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "Uri", uri);
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (accept_ssl_errors) ? "true" : "false");

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

  g_free (presentation_identity);
  g_free (server);
  g_free (uri);
  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  g_clear_object (&data.cancellable);
  g_clear_object (&http_client);
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
  GVariantBuilder builder;
  GoaAccount *account;
  GoaHttpClient *http_client;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean accept_ssl_errors;
  gboolean ret;
  const gchar *password;
  const gchar *username;
  gchar *uri;
  gchar *uri_webdav;
  gint response;

  g_return_val_if_fail (GOA_IS_OWNCLOUD_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  http_client = NULL;
  uri = NULL;
  uri_webdav = NULL;

  ret = FALSE;

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

  create_account_details_ui (provider, GTK_DIALOG (dialog), GTK_BOX (vbox), FALSE, &data);

  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  uri = goa_util_lookup_keyfile_string (object, "Uri");
  gtk_entry_set_text (GTK_ENTRY (data.uri), uri);
  gtk_editable_set_editable (GTK_EDITABLE (data.uri), FALSE);

  account = goa_object_peek_account (object);
  username = goa_account_get_identity (account);
  gtk_entry_set_text (GTK_ENTRY (data.username), username);
  gtk_editable_set_editable (GTK_EDITABLE (data.username), FALSE);

  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  http_client = goa_http_client_new ();
  uri_webdav = g_strconcat (uri, WEBDAV_ENDPOINT, NULL);

 http_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  g_cancellable_reset (data.cancellable);
  goa_http_client_check (http_client,
                         uri_webdav,
                         username,
                         password,
                         accept_ssl_errors,
                         data.cancellable,
                         check_cb,
                         &data);
  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
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
      gchar *markup;

      markup = g_strdup_printf ("<b>%s:</b>\n%s",
                                _("Error connecting to ownCloud server"),
                                data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto http_again;
    }

  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    &data.error))
    goto out;

  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;

 out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);

  gtk_widget_destroy (dialog);
  g_free (uri);
  g_free (uri_webdav);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  g_clear_object (&data.cancellable);
  g_clear_object (&http_client);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *grid,
              G_GNUC_UNUSED GtkGrid *dummy)
{
  gint row;

  row = 0;

  goa_util_add_account_info (grid, row++, object);

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   /* Translators: This is a label for a series of
                                                    * options switches. For example: “Use for Mail”. */
                                                   _("Use for"),
                                                   "calendar-disabled",
                                                   _("Cale_ndar"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "contacts-disabled",
                                                   _("_Contacts"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "documents-disabled",
                                                   _("_Documents"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "files-disabled",
                                                   _("_Files"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_owncloud_provider_init (GoaOwncloudProvider *provider)
{
}

static void
goa_owncloud_provider_class_init (GoaOwncloudProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
on_handle_get_password (GoaPasswordBased      *interface,
                        GDBusMethodInvocation *invocation,
                        const gchar           *id, /* unused */
                        gpointer               user_data)
{
  GoaObject *object;
  GoaAccount *account;
  GoaProvider *provider;
  GError *error;
  GVariant *credentials;
  const gchar *identity;
  gchar *password;

  /* TODO: maybe log what app is requesting access */

  password = NULL;
  credentials = NULL;

  object = GOA_OBJECT (g_dbus_interface_get_object (G_DBUS_INTERFACE (interface)));
  account = goa_object_peek_account (object);
  identity = goa_account_get_identity (account);
  provider = goa_provider_get_for_provider_type (goa_account_get_provider_type (account));

  error = NULL;
  credentials = goa_utils_lookup_credentials_sync (provider,
                                                   object,
                                                   NULL, /* GCancellable* */
                                                   &error);
  if (credentials == NULL)
    {
      g_dbus_method_invocation_take_error (invocation, error);
      goto out;
    }

  if (!g_variant_lookup (credentials, "password", "s", &password))
    {
      g_dbus_method_invocation_return_error (invocation,
                                             GOA_ERROR,
                                             GOA_ERROR_FAILED, /* TODO: more specific */
                                             _("Did not find password with identity ‘%s’ in credentials"),
                                             identity);
      goto out;
    }

  goa_password_based_complete_get_password (interface, invocation, password);

 out:
  g_free (password);
  if (credentials != NULL)
    g_variant_unref (credentials);
  g_object_unref (provider);
  return TRUE; /* invocation was handled */
}
