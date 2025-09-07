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
#include "goadavconfig.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
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
  return g_themed_icon_new_with_default_fallbacks ("x-office-calendar-symbolic");
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
  g_autoptr (GKeyFile) goa_conf = NULL;
  const gchar *provider_type;
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

  provider_type = goa_provider_get_provider_type (provider);
  goa_conf = goa_util_open_goa_conf ();
  account = goa_object_get_account (GOA_OBJECT (object));
  identity = goa_account_get_identity (account);
  accept_ssl_errors = g_key_file_get_boolean (key_file, group, "AcceptSslErrors", NULL);

  /* Calendar */
  calendar_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_CALENDAR) &&
                     g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  uri_caldav = g_key_file_get_string (key_file, group, "CalDavUri", NULL);
  uri_encoded = uri_encode_identity (uri_caldav, identity, FALSE);
  goa_object_skeleton_attach_calendar (object, uri_encoded, calendar_enabled, accept_ssl_errors);
  g_clear_pointer (&uri_encoded, g_free);

  /* Contacts */
  contacts_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_CONTACTS) &&
                     g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  uri_carddav = g_key_file_get_string (key_file, group, "CardDavUri", NULL);
  uri_encoded = uri_encode_identity (uri_carddav, identity, FALSE);
  goa_object_skeleton_attach_contacts (object, uri_encoded, contacts_enabled, accept_ssl_errors);
  g_clear_pointer (&uri_encoded, g_free);

  /* Files */
  files_enabled = goa_util_provider_feature_is_enabled (goa_conf, provider_type, GOA_PROVIDER_FEATURE_FILES) &&
                  g_key_file_get_boolean (key_file, group, "FilesEnabled", NULL);
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

static struct
{
  const char *service;
  const char *key;
} keyfile_endpoints[] = {
  { GOA_SERVICE_TYPE_WEBDAV, "Uri" },
  { GOA_SERVICE_TYPE_CALDAV, "CalDavUri" },
  { GOA_SERVICE_TYPE_CARDDAV, "CardDavUri" },
};

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

  /* Find a URI to confirm the account */
  dav_client = goa_dav_client_new ();
  accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");

  for (size_t i = 0; i < G_N_ELEMENTS (keyfile_endpoints); i++)
    {
      g_autofree char *uri = NULL;

      uri = goa_util_lookup_keyfile_string (object, keyfile_endpoints[i].key);
      if (uri != NULL && g_uri_is_valid (uri, SOUP_HTTP_URI_FLAGS, NULL))
        {
          g_autoptr(GoaDavConfig) config = NULL;

          config = goa_dav_config_new (keyfile_endpoints[i].service, uri, username);
          ret = goa_dav_client_check_sync (dav_client,
                                           config,
                                           password,
                                           accept_ssl_errors,
                                           cancellable,
                                           error);
          break;
        }
    }

  if (!ret)
    {
      if (error != NULL && g_error_matches (*error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED))
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
        }

      return ret;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  gboolean accept_ssl_errors;

  GoaDavConfig *caldav;
  GoaDavConfig *carddav;
  GoaDavConfig *webdav;
  GoaDavConfig *check_config;
  GoaProviderFeatures check_stage;
  char *presentation_identity;
  gboolean is_template;

  GtkWidget *uri;
  GtkWidget *username;
  GtkWidget *password;
  GtkWidget *webdav_uri;
  GtkWidget *caldav_uri;
  GtkWidget *carddav_uri;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_object (&data->caldav);
  g_clear_object (&data->carddav);
  g_clear_object (&data->webdav);
  g_clear_object (&data->check_config);
  g_clear_pointer (&data->presentation_identity, g_free);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_uri_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add = FALSE;
  const char *address;
  g_autofree char *uri = NULL;

  /* Reset the preference to ignore SSL/TLS errors
   */
  data->accept_ssl_errors = FALSE;

  address = gtk_editable_get_text (GTK_EDITABLE (data->uri));
  uri = goa_utils_normalize_url (address, NULL, NULL);

  if (uri != NULL)
    {
      const char *username = gtk_editable_get_text (GTK_EDITABLE (data->username));
      const char *password = gtk_editable_get_text (GTK_EDITABLE (data->password));

      can_add = ((username != NULL && *username != '\0')
        && (password != NULL && *password != '\0'));
    }

  goa_provider_dialog_set_state (data->dialog, can_add
                                   ? GOA_DIALOG_READY
                                   : GOA_DIALOG_IDLE);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  goa_provider_dialog_add_page (dialog,
                                _("Calendar, Contacts and Files"),
                                _("Add a calendar, contacts and files account by entering your WebDAV server and account details"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->uri = goa_provider_dialog_add_entry (dialog, group, _("_Server Address"));
  goa_provider_dialog_add_description (dialog, data->uri, _("Examples: example.com, 192.168.0.82"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->username = goa_provider_dialog_add_entry (dialog, group, _("User_name"));
  data->password = goa_provider_dialog_add_password_entry (dialog, group, _("_Password"));

  if (new_account)
    {
      const char *provider_type;

      group = goa_provider_dialog_add_group (dialog, _("Server Addresses (Optional)"));
      data->webdav_uri = goa_provider_dialog_add_entry (dialog, group, _("Files"));
      data->caldav_uri = goa_provider_dialog_add_entry (dialog, group, _("Calendar (CalDAV)"));
      data->carddav_uri = goa_provider_dialog_add_entry (dialog, group, _("Contacts (CardDAV)"));

      /* The only reason to subclass the WebDAV provider is to brand it, thus we
       * expect the provider to take responsibility for custom endpoints.
       */
      provider_type = goa_provider_get_provider_type (provider);
      if (!g_str_equal (GOA_WEBDAV_NAME, provider_type))
        gtk_widget_set_visible (group, FALSE);
    }

  if (data->object != NULL)
    {
      GoaAccount *account = goa_object_peek_account (data->object);
      const char *username = goa_account_get_identity (account);
      g_autofree char *uri = NULL;

      /* Find a URI to confirm the account */
      for (size_t i = 0; i < G_N_ELEMENTS (keyfile_endpoints); i++)
        {
          uri = goa_util_lookup_keyfile_string (data->object, keyfile_endpoints[i].key);
          if (uri != NULL && g_uri_is_valid (uri, SOUP_HTTP_URI_FLAGS, NULL))
            {
              data->check_config = goa_dav_config_new (keyfile_endpoints[i].service, uri, username);
              break;
            }

          g_clear_pointer (&uri, g_free);
        }

      if (username == NULL || username[0] == '\0')
        data->is_template = TRUE;

      gtk_editable_set_text (GTK_EDITABLE (data->uri), uri);
      gtk_editable_set_editable (GTK_EDITABLE (data->uri), FALSE);

      if (!data->is_template)
        {
          gtk_editable_set_text (GTK_EDITABLE (data->username), username);
          gtk_editable_set_editable (GTK_EDITABLE (data->username), FALSE);
        }
    }

  if (new_account)
    gtk_widget_grab_focus (data->uri);
  else if (data->is_template)
    gtk_widget_grab_focus (data->username);
  else
    gtk_widget_grab_focus (data->password);

  g_signal_connect (data->uri, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->username, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_uri_username_or_password_changed), data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_credentials_cb (GoaManager   *manager,
                            GAsyncResult *res,
                            gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GDBusObject *ret = NULL;
  g_autofree char *object_path = NULL;
  g_autoptr(GError) error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  ret = g_dbus_object_manager_get_object (goa_client_get_object_manager (data->client),
                                          object_path);
  goa_provider_task_return_account (task, GOA_OBJECT (ret));
}

static void
add_account_credentials (GTask *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *password;
  const char *username;

  /* Account is confirmed */
  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Uri", data->webdav ? goa_dav_config_get_uri (data->webdav) : "");
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", data->caldav ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "CalDavUri", data->caldav ? goa_dav_config_get_uri (data->caldav) : "");
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", data->carddav ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "CardDavUri", data->carddav ? goa_dav_config_get_uri (data->carddav) : "");
  g_variant_builder_add (&details, "{ss}", "FilesEnabled", data->webdav ? "true" : "false");
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", (data->accept_ssl_errors) ? "true" : "false");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                username,
                                data->presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));
}

static gpointer
services_find_preferred (GPtrArray  *services,
                         const char *name)
{
  for (unsigned int i = 0; i < services->len; i++)
    {
      GoaServiceConfig *config = g_ptr_array_index (services, i);

      if (g_str_equal (goa_service_config_get_service (config), name))
        return g_object_ref (config);
    }

  return NULL;
}

static gboolean
add_account_handle_response (GTask     *task,
                             GPtrArray *services)
{
  AddAccountData *data = g_task_get_task_data (task);
  GtkEditable *editable = NULL;
  const char *username = NULL;
  const char *base_uri = NULL;
  const char *check_uri = NULL;
  const char *service = NULL;
  g_autofree char *normalized_uri = NULL;
  g_autoptr(GError) error = NULL;

  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  base_uri = gtk_editable_get_text (GTK_EDITABLE (data->uri));

  while (TRUE)
    {
      switch (data->check_stage)
        {
        /* This is the primary discovery stage.
         */
        case GOA_PROVIDER_FEATURE_INVALID:
          if (services != NULL)
            {
              g_clear_object (&data->webdav);
              data->webdav = services_find_preferred (services, GOA_SERVICE_TYPE_WEBDAV);
              g_clear_object (&data->caldav);
              data->caldav = services_find_preferred (services, GOA_SERVICE_TYPE_CALDAV);
              g_clear_object (&data->carddav);
              data->carddav = services_find_preferred (services, GOA_SERVICE_TYPE_CARDDAV);
            }

          data->check_stage = GOA_PROVIDER_FEATURE_FILES;
          service = GOA_SERVICE_TYPE_WEBDAV;
          editable = GTK_EDITABLE (data->webdav_uri);
          check_uri = gtk_editable_get_text (editable);
          break;

        /* WebDAV: discover so we get the redirected URI for GVfs, but don't
         * override previously discovered CalDAV/CardDAV endpoints.
         */
        case GOA_PROVIDER_FEATURE_FILES:
          if (services != NULL)
            {
              g_clear_object (&data->webdav);
              data->webdav = services_find_preferred (services, GOA_SERVICE_TYPE_WEBDAV);
            }

          data->check_stage = GOA_PROVIDER_FEATURE_CALENDAR;
          service = GOA_SERVICE_TYPE_CALDAV;
          editable = GTK_EDITABLE (data->caldav_uri);
          check_uri = gtk_editable_get_text (editable);
          break;

        /* CalDAV/CardDAV: user-defined URIs override discovered services even
         * if the server doesn't claim support.
         */
        case GOA_PROVIDER_FEATURE_CALENDAR:
          if (data->check_config != NULL)
            {
              g_clear_object (&data->caldav);
              data->caldav = g_steal_pointer (&data->check_config);
            }

          data->check_stage = GOA_PROVIDER_FEATURE_CONTACTS;
          service = GOA_SERVICE_TYPE_CARDDAV;
          editable = GTK_EDITABLE (data->carddav_uri);
          check_uri = gtk_editable_get_text (editable);
          break;

        case GOA_PROVIDER_FEATURE_CONTACTS:
          if (data->check_config != NULL)
            {
              g_clear_object (&data->carddav);
              data->carddav = g_steal_pointer (&data->check_config);
            }

          /* Set the next stage to default to add_account_credentials() */
          data->check_stage = GOA_PROVIDER_FEATURE_TICKETING;
          editable = NULL;
          break;

        default:
          add_account_credentials (task);
          return FALSE;
        }

      /* The user entered a URI to be checked */
      if (check_uri != NULL && *check_uri != '\0')
        break;
    }

  /* If the user entered a bunk URI, they probably want to be notified */
  normalized_uri = goa_utils_normalize_url (base_uri, check_uri, NULL);
  if (normalized_uri == NULL)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Invalid URI: %s"),
                   check_uri);
      goa_provider_dialog_report_error (data->dialog, error);
      return FALSE;
    }
  else if (editable != NULL)
    {
      g_signal_handlers_block_by_func (editable, on_uri_username_or_password_changed, data);
      gtk_editable_set_text (editable, normalized_uri);
      g_signal_handlers_unblock_by_func (editable, on_uri_username_or_password_changed, data);
    }

  data->check_config = goa_dav_config_new (service, normalized_uri, username);

  return TRUE;
}

static void
add_account_check_cb (GoaDavClient *client,
                      GAsyncResult *result,
                      gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GError) error = NULL;

  if (!goa_dav_client_check_finish (client, result, &error))
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  if (add_account_handle_response (task, NULL))
    {
      const char *password = NULL;

      password = gtk_editable_get_text (GTK_EDITABLE (data->password));
      goa_dav_client_check (client,
                            data->check_config,
                            password,
                            data->accept_ssl_errors,
                            cancellable,
                            (GAsyncReadyCallback) add_account_check_cb,
                            g_steal_pointer (&task));
    }
}

static void
add_account_discover_cb (GoaDavClient *client,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autoptr(GPtrArray) services = NULL;
  const char *password = NULL;
  g_autoptr(GError) error = NULL;

  services = goa_dav_client_discover_finish (client, result, &error);
  if (services == NULL)
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  if (!add_account_handle_response (task, services))
    return;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  /* WebDAV needs discovery to get the redirected URI for GVfs */
  if (data->check_stage == GOA_PROVIDER_FEATURE_FILES)
    {
      goa_dav_client_discover (client,
                               goa_dav_config_get_uri (data->check_config),
                               goa_dav_config_get_username (data->check_config),
                               password,
                               data->accept_ssl_errors,
                               cancellable,
                               (GAsyncReadyCallback) add_account_discover_cb,
                               g_steal_pointer (&task));
    }
  else
    {
      goa_dav_client_check (client,
                            data->check_config,
                            password,
                            data->accept_ssl_errors,
                            cancellable,
                            (GAsyncReadyCallback) add_account_check_cb,
                            g_steal_pointer (&task));
    }
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  const char *uri_text;
  const char *password;
  const char *username;
  const char *provider_type;
  g_autoptr(GoaDavClient) dav_client = NULL;
  g_autofree char *server = NULL;
  g_autofree char *uri = NULL;
  g_autoptr(GError) error = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  uri_text = gtk_editable_get_text (GTK_EDITABLE (data->uri));
  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  g_free (data->presentation_identity);
  uri = goa_utils_normalize_url (uri_text, NULL, &server);
  if (strchr (username, '@') != NULL)
    data->presentation_identity = g_strdup (username);
  else
    data->presentation_identity = g_strconcat (username, "@", server, NULL);

  /* If this is duplicate account we're finished */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (data->client,
                                  username,
                                  data->presentation_identity,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_password_based,
                                  &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Update the entry field with the URL we're actually using.
   */
  g_signal_handlers_block_by_func (data->uri, on_uri_username_or_password_changed, data);
  gtk_editable_set_text (GTK_EDITABLE (data->uri), uri);
  g_signal_handlers_unblock_by_func (data->uri, on_uri_username_or_password_changed, data);

  /* Confirm the account */
  dav_client = goa_dav_client_new ();
  goa_dav_client_discover (dav_client,
                           uri,
                           username,
                           password,
                           data->accept_ssl_errors,
                           cancellable,
                           (GAsyncReadyCallback) add_account_discover_cb,
                           g_object_ref (task));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account (GoaProvider         *provider,
             GoaClient           *client,
             GtkWidget           *parent,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
  AddAccountData *data;
  g_autoptr(GTask) task = NULL;

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new (provider, client, parent);
  data->client = g_object_ref (client);

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, add_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, TRUE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (add_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account_credentials_cb (GoaAccount   *account,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GError *error = NULL;

  if (!goa_account_call_ensure_credentials_finish (account, NULL, res, &error))
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
refresh_account_full_cb (GoaManager   *manager,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autofree char *object_path = NULL;
  g_autoptr(GError) error = NULL;

  if (!goa_manager_call_add_account_finish (manager, &object_path, res, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_steal_pointer (&task));
}

static void
refresh_account_check_cb (GoaDavClient *client,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = g_task_get_source_object (task);
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  const char *username;
  const char *password;
  g_autoptr(GError) error = NULL;

  if (!goa_dav_client_check_finish (client, result, &error))
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  username = gtk_editable_get_text (GTK_EDITABLE (data->username));
  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  /* Account is confirmed */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  if (data->is_template)
    {
      GVariantBuilder details;
      GoaAccount *account;
      const char *id;
      const char *provider_type;

      account = goa_object_peek_account (data->object);
      id = goa_account_get_id (account);

      g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
      g_variant_builder_add (&details, "{ss}", "Id", id);

      provider_type = goa_provider_get_provider_type (provider);
      goa_manager_call_add_account (goa_client_get_manager (data->client),
                                    provider_type,
                                    username,
                                    data->presentation_identity,
                                    g_variant_builder_end (&credentials),
                                    g_variant_builder_end (&details),
                                    cancellable,
                                    (GAsyncReadyCallback) refresh_account_full_cb,
                                    g_steal_pointer (&task));
      return;
    }

  // TODO: run in worker thread
  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    data->object,
                                                    g_variant_builder_end (&credentials),
                                                    cancellable,
                                                    &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_steal_pointer (&task));
}

static void
refresh_account_action_cb (GoaProviderDialog *dialog,
                           GParamSpec        *pspec,
                           GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  const char *password;
  g_autoptr(GoaDavClient) dav_client = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  password = gtk_editable_get_text (GTK_EDITABLE (data->password));

  /* Confirm the account */
  dav_client = goa_dav_client_new ();
  goa_dav_client_check (dav_client,
                        data->check_config,
                        password,
                        data->accept_ssl_errors,
                        cancellable,
                        (GAsyncReadyCallback) refresh_account_check_cb,
                        g_object_ref (task));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
refresh_account (GoaProvider         *provider,
                 GoaClient           *client,
                 GoaObject           *object,
                 GtkWidget           *parent,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
  AddAccountData *data;
  g_autoptr(GTask) task = NULL;

  g_assert (GOA_IS_WEBDAV_PROVIDER (provider));
  g_assert (GOA_IS_CLIENT (client));
  g_assert (GOA_IS_OBJECT (object));
  g_assert (parent == NULL || GTK_IS_WIDGET (parent));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = g_new0 (AddAccountData, 1);
  data->dialog = goa_provider_dialog_new (provider, client, parent);
  data->client = g_object_ref (client);
  data->object = g_object_ref (object);
  data->accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");
  data->presentation_identity = goa_util_lookup_keyfile_string (object, "PresentationIdentity");

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, refresh_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (refresh_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  goa_provider_task_run_in_dialog (task, data->dialog);
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
  g_autoptr(GoaProvider) provider = NULL;
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

