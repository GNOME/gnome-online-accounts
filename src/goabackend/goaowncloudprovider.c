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

#include <string.h>

#include <glib/gi18n-lib.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "goaauthflowbutton.h"
#include "goadavclient.h"
#include "goaoauth2provider-priv.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goasouplogger.h"
#include "goawebdavprovider.h"
#include "goawebdavprovider-priv.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

#include "goaowncloudprovider.h"

#define LOGINFLOW2_POLL_INTERVAL (5)

struct _GoaOwncloudProvider
{
  GoaWebDavProvider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaOwncloudProvider, goa_owncloud_provider, GOA_TYPE_WEBDAV_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_OWNCLOUD_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_OWNCLOUD_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider, GoaObject *object)
{
  return g_strdup(_("Nextcloud"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_FILES;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-owncloud");
}

/* ---------------------------------------------------------------------------------------------------- */

static struct
{
  const char *key;
  const char *endpoint;
} migration_table[] = {
    {
      .key = "CalDavUri",
      .endpoint = "remote.php/dav",
    },
    {
      .key = "CardDavUri",
      .endpoint = "remote.php/dav",
    },
    {
      .key = "Uri",
      .endpoint = "remote.php/webdav",
    },
};

static gboolean
migrate_account (GKeyFile    *key_file,
                 const char  *group,
                 GError     **error)
{
  g_autofree char *uri = NULL;
  g_autofree char *base_uri = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GError) warning = NULL;

  /* This shouldn't ever fail, but we check anyways */
  uri = g_key_file_get_string (key_file, group, "Uri", error);
  if (uri == NULL || !g_uri_is_valid (uri, G_URI_FLAGS_PARSE_RELAXED, error))
    return FALSE;

  /* Only URIs missing "remote.php" need migration */
  if (g_strrstr (uri, "remote.php") != NULL)
    return TRUE;

  if (!g_str_has_suffix (uri, "/"))
    base_uri = g_strconcat (uri, "/", NULL);
  else
    base_uri = g_strdup (uri);

  for (size_t i = 0; i < G_N_ELEMENTS (migration_table); i++)
    {
      g_autofree char *value = NULL;

      value = g_strconcat (base_uri, migration_table[i].endpoint, NULL);
      g_key_file_set_string (key_file, group, migration_table[i].key, value);
    }

  path = g_build_filename (g_get_user_config_dir (), "goa-1.0", "accounts.conf", NULL);
  if (!g_key_file_save_to_file (key_file, path, &warning))
    g_warning ("Failed to save account migration: %s", warning->message);

  return TRUE;
}

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const char          *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  if (!migrate_account (key_file, group, error))
    return FALSE;

  /* Chain up to WebDAV provider */
  return GOA_PROVIDER_CLASS (goa_owncloud_provider_parent_class)->build_object (provider,
                                                                                object,
                                                                                key_file,
                                                                                group,
                                                                                connection,
                                                                                just_added,
                                                                                error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  SoupSession *session;
  char *poll_uri;
  char *poll_data;
  GCancellable *cancellable;
  unsigned int cancellable_id;
  GCancellable *req_cancellable;
  GoaAuthFlowFlags flags;
  GError *error;
} LoginFlow2Data;

static void loginflow2_poll_timeout_cb (gpointer user_data);

static void
loginflow2_data_free (gpointer user_data)
{
  LoginFlow2Data *data = (LoginFlow2Data *)user_data;

  if (data->cancellable != NULL)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_clear_object (&data->cancellable);
    }

  if (data->session != NULL)
    {
      soup_session_abort (data->session);
      g_clear_object (&data->session);
    }

  g_clear_object (&data->req_cancellable);
  g_clear_pointer (&data->poll_uri, g_free);
  g_clear_pointer (&data->poll_data, g_free);
  g_free (data);
}

static void
loginflow2_cancelled_cb (GCancellable *cancellable,
                         gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);

  /* libsoup calls g_cancellable_cancel() on the caller's cancellable when
   * aborted; ensure it happens so it can be used as an internal cancellable
   */
  g_cancellable_cancel (data->req_cancellable);
  soup_session_abort (data->session);
}

static void
loginflow2_poll_cb (SoupSession  *session,
                    GAsyncResult *result,
                    gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  SoupMessage *message = NULL;
  g_autoptr(GBytes) body = NULL;
  g_autoptr(JsonParser) parser = NULL;
  const char *json_data;
  size_t json_len;
  JsonObject *response;
  const char *server;
  const char *username;
  const char *password;
  SoupStatus status = 0;
  g_autoptr(GError) error = NULL;

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  body = soup_session_send_and_read_finish (session, result, &error);
  if (error != NULL && data->error == NULL)
    g_propagate_error (&data->error, g_steal_pointer (&error));

  if (data->error != NULL)
    goto out;

  message = soup_session_get_async_result_message (session, result);
  status = soup_message_get_status (message);
  if (status == SOUP_STATUS_NOT_FOUND)
    {
      /* The endpoint returns a 404 until the request is approved.
       */
      g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                                  loginflow2_poll_timeout_cb,
                                  g_object_ref (task));
      return;
    }
  else if (status != SOUP_STATUS_OK)
    {
      goa_utils_set_error_soup (&data->error, message);
      goto out;
    }

  parser = json_parser_new ();
  json_data = g_bytes_get_data (body, &json_len);
  if (!json_parser_load_from_data (parser, json_data, json_len, &data->error))
    goto out;

  response = json_node_get_object (json_parser_get_root (parser));
  server = json_object_get_string_member (response, "server");
  username = json_object_get_string_member (response, "loginName");
  password = json_object_get_string_member (response, "appPassword");
  if (server != NULL && username != NULL && password != NULL)
    {
      g_autoptr(GUri) uri = NULL;
      g_autofree char *ret = NULL;

      uri = g_uri_parse (server, SOUP_HTTP_URI_FLAGS, &data->error);
      if (uri == NULL)
        goto out;

      ret = g_uri_join_with_user (G_URI_FLAGS_HAS_PASSWORD,
                                  g_uri_get_scheme (uri),
                                  username,
                                  password,
                                  NULL,
                                  g_uri_get_host (uri),
                                  g_uri_get_port (uri),
                                  g_uri_get_path (uri),
                                  g_uri_get_query (uri),
                                  g_uri_get_fragment (uri));

      g_task_return_pointer (task, g_steal_pointer (&ret), g_free);
    }
  else
    {
      g_set_error_literal (&data->error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           _("Could not parse response"));
    }

out:
  if (data->error != NULL)
    g_task_return_error (task, g_steal_pointer (&data->error));
}

static void
loginflow2_poll_timeout_cb (gpointer user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GCancellable *cancellable = g_task_get_cancellable (task);
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);

  if (!g_cancellable_is_cancelled (cancellable))
    {
      g_autoptr(SoupMessage) message = NULL;

      message = soup_message_new_from_encoded_form (SOUP_METHOD_POST,
                                                    data->poll_uri,
                                                    g_strdup (data->poll_data));
      soup_session_send_and_read_async (data->session,
                                        message,
                                        G_PRIORITY_DEFAULT,
                                        data->req_cancellable,
                                        (GAsyncReadyCallback) loginflow2_poll_cb,
                                        g_object_ref (task));
    }
}

static void
loginflow2_launch_uri_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!g_app_info_launch_default_for_uri_finish (result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Start polling for a response
   */
  g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                              loginflow2_poll_timeout_cb,
                              g_object_ref (task));
}

/**
 * goa_loginflow2_authenticate:
 * @auth: a `JsonNode`
 * @auth_uri_out: (out) (nullable) (transfer full): the authentication URI
 * @flags: the authentication flags
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Perform Login Flow v2 authentication for @auth.
 *
 * If @auth_uri is not %NULL, it will be set to the
 *
 * Call [method@Goa.loginflow2_authenticate_finish] to get the result.
 *
 * See: https://docs.nextcloud.com/server/latest/developer_manual/client_apis/LoginFlow/
 */
static void
goa_loginflow2_authenticate (JsonNode             *auth,
                             char                **auth_uri_out,
                             GoaAuthFlowFlags      flags,
                             GCancellable         *cancellable,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GUri) login = NULL;
  g_autoptr(SoupLogger) logger = NULL;
  JsonObject *auth_data, *poll_data;
  const char *login_uri = NULL;
  const char *endpoint = NULL;
  const char *token = NULL;
  LoginFlow2Data *data;

  g_return_if_fail (auth == NULL || JSON_NODE_HOLDS_OBJECT (auth));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  /* Extract the login and poll endpoints
   */
  auth_data = json_node_get_object (auth);
  login_uri = json_object_get_string_member (auth_data, "login");
  poll_data = json_object_get_object_member (auth_data, "poll");
  if (poll_data != NULL)
    {
      endpoint = json_object_get_string_member (poll_data, "endpoint");
      token = json_object_get_string_member (poll_data, "token");
    }

  if (login_uri == NULL || token == NULL || endpoint == NULL)
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
      return;
    }

  data = g_new0 (LoginFlow2Data, 1);
  data->flags = flags;
  data->poll_uri = g_strdup (endpoint);
  data->poll_data = g_strdup_printf ("token=%s", token);
  data->session = soup_session_new ();
  data->req_cancellable = g_cancellable_new ();

  soup_session_set_user_agent (data->session, "gnome-online-accounts/" PACKAGE_VERSION " ");
  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_loginflow2_authenticate);
  g_task_set_task_data (task, data, loginflow2_data_free);

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (loginflow2_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  /* Start the web browser flow
   */
  if (auth_uri_out != NULL)
    {
      *auth_uri_out = g_strdup (login_uri);
    }

  if ((data->flags & GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI) == 0)
    {
      g_app_info_launch_default_for_uri_async (login_uri,
                                               NULL,
                                               data->req_cancellable,
                                               (GAsyncReadyCallback) loginflow2_launch_uri_cb,
                                               g_object_ref (task));
    }
  else
    {
      g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                                  loginflow2_poll_timeout_cb,
                                  g_object_ref (task));
    }
}

/**
 * goa_loginflow2_authenticate_finish:
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Get the result of an operation started with [func@Goa.loginflow2_authenticate].
 *
 * Returns: (transfer full): the server URI with encoded credentials,
 *   or %NULL with @error set
 */
static char *
goa_loginflow2_authenticate_finish (GAsyncResult  *result,
                                    GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == goa_loginflow2_authenticate, NULL);

  return g_task_propagate_pointer (G_TASK (result), error);
}

static void
goa_loginflow2_query_cb (SoupSession  *session,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  SoupMessage *message = NULL;
  g_autoptr(GBytes) body = NULL;
  g_autoptr(JsonParser) parser = NULL;
  const char *json_data;
  size_t json_len;
  g_autoptr(GError) error = NULL;

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  body = soup_session_send_and_read_finish (session, result, &error);
  if (error != NULL && data->error == NULL)
    g_propagate_error (&data->error, g_steal_pointer (&error));

  if (data->error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&data->error));
      return;
    }

  message = soup_session_get_async_result_message (session, result);
  if (soup_message_get_status (message) != SOUP_STATUS_OK)
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_SUPPORTED,
                                       "Login Flow v2 not supported");
      return;
    }

  parser = json_parser_new ();
  json_data = g_bytes_get_data (body, &json_len);
  if (!json_parser_load_from_data (parser, json_data, json_len, &error))
    {
      g_debug ("%s(): %s", G_STRFUNC, error->message);
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
      return;
    }

  g_task_return_pointer (task,
                         json_parser_steal_root (parser),
                         (GDestroyNotify)json_node_unref);
}

/**
 * goa_loginflow2_query:
 * @uri: a Nextcloud URI
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Retrieve a LoginFlow2 token and endpoint for @uri.
 *
 * Call [method@Goa.loginflow2_query_finish] to get the result.
 *
 * See: https://docs.nextcloud.com/server/latest/developer_manual/client_apis/LoginFlow/
 */
static void
goa_loginflow2_query (const char          *uri,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GUri) query_uri = NULL;
  g_autoptr(SoupLogger) logger = NULL;
  g_autoptr(SoupMessage) message = NULL;
  g_autofree char *message_uri = NULL;
  LoginFlow2Data *data;

  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  query_uri = g_uri_parse (uri, SOUP_HTTP_URI_FLAGS, NULL);
  if (query_uri != NULL)
    {
      g_autoptr(GUri) tmp = g_steal_pointer (&query_uri);
      const char *path = g_uri_get_path (tmp);
      g_autofree char *new_path = NULL;

      if (!g_strrstr (path, "/index.php/login/v2"))
        new_path = g_build_path (path, "/index.php/login/v2", NULL);
      else
        new_path = g_strdup (path);

      query_uri = g_uri_build (g_uri_get_flags (tmp),
                               g_uri_get_scheme (tmp),
                               g_uri_get_userinfo (tmp),
                               g_uri_get_host (tmp),
                               g_uri_get_port (tmp),
                               new_path,
                               g_uri_get_query (tmp),
                               g_uri_get_fragment (tmp));
    }
  else
    {
      g_task_report_new_error (NULL, callback, user_data,
                               goa_loginflow2_query,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Invalid URI: %s"),
                               uri);
      return;
    }

  data = g_new0 (LoginFlow2Data, 1);
  data->session = soup_session_new ();
  data->req_cancellable = g_cancellable_new ();

  soup_session_set_user_agent (data->session, "gnome-online-accounts/" PACKAGE_VERSION " ");
  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_loginflow2_query);
  g_task_set_task_data (task, data, loginflow2_data_free);

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (loginflow2_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  message = soup_message_new_from_uri (SOUP_METHOD_POST, query_uri);
  if (message == NULL)
    {
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Invalid URI: %s"),
                               uri);
      return;
    }

  soup_session_send_and_read_async (data->session,
                                    message,
                                    G_PRIORITY_DEFAULT,
                                    data->req_cancellable,
                                    (GAsyncReadyCallback) goa_loginflow2_query_cb,
                                    g_object_ref (task));
}

/**
 * goa_loginflow2_query_finish:
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Get the result of an operation started with [func@Goa.loginflow2_query].
 *
 * Returns: the JSON response if Login Flow v2 is supported,
 *   or %NULL with @error set
 */
static JsonNode *
goa_loginflow2_query_finish (GAsyncResult  *result,
                             GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == goa_loginflow2_query, FALSE);

  return g_task_propagate_pointer (G_TASK (result), error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;
  GCancellable *discover;
  gboolean accept_ssl_errors;
  GoaAuthFlowFlags flags;
  gboolean is_template;

  GtkWidget *uri;
  GtkWidget *login_button;
  GtkWidget *discovery_status;
  GtkWidget *discovery_spinner;
  unsigned int discovery_id;
  JsonNode *auth;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_cancellable_cancel (data->discover);
  g_clear_object (&data->discover);

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_handle_id (&data->discovery_id, g_source_remove);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
loginflow2_query_cb (GObject      *unused,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  AddAccountData *data = user_data;
  g_autoptr(JsonNode) response = NULL;
  g_autoptr(GError) error = NULL;

  response = goa_loginflow2_query_finish (result, &error);
  if (response != NULL)
    {
      gtk_widget_set_visible (data->discovery_status, TRUE);
      gtk_widget_set_visible (data->discovery_spinner, FALSE);
      gtk_widget_set_sensitive (data->login_button, TRUE);

      g_clear_pointer (&data->auth, json_node_unref);
      data->auth = json_node_ref (response);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      gtk_widget_set_visible (data->discovery_status, FALSE);
      gtk_widget_set_sensitive (data->login_button, FALSE);
      goa_provider_dialog_add_toast (data->dialog, adw_toast_new (error->message));
    }
}

static void
entry_changed_cb (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;
  const char *uri;

  data->accept_ssl_errors = FALSE;
  data->discovery_id = 0;

  gtk_widget_set_visible (data->discovery_status, FALSE);
  g_cancellable_cancel (data->discover);
  g_clear_object (&data->discover);

  uri = gtk_editable_get_text (GTK_EDITABLE (data->uri));
  if (uri != NULL && *uri != '\0')
    {
      g_autofree char *normalized_uri = NULL;

      normalized_uri = goa_utils_normalize_url (uri, NULL, NULL);
      if (normalized_uri != NULL)
        {
          data->discover = g_cancellable_new ();
          gtk_widget_set_visible (data->discovery_status, TRUE);
          gtk_widget_set_visible (data->discovery_spinner, TRUE);

          goa_loginflow2_query (normalized_uri,
                                data->discover,
                                (GAsyncReadyCallback) loginflow2_query_cb,
                                data);
        }
    }
}

static void
on_entry_changed (GtkEditable    *editable,
                  AddAccountData *data)
{
  g_clear_handle_id (&data->discovery_id, g_source_remove);
  data->discovery_id = g_timeout_add_once (200, entry_changed_cb, data);
}

static void
on_copy_activated (GoaAuthflowButton *widget,
                   AddAccountData    *data)
{
  data->flags |= GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI;
  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_BUSY);
  goa_authflow_button_set_active (widget, TRUE);
}

static void
on_link_activated (GoaAuthflowButton *widget,
                   AddAccountData    *data)
{
  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_BUSY);
  goa_authflow_button_set_active (widget, TRUE);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           AddAccountData *data,
                           gboolean        new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;
  GtkWidget *icon;

  goa_provider_dialog_add_page (dialog,
                                _("Nextcloud"),
                                _("Add a Nextcloud account by entering your server"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->uri = goa_provider_dialog_add_entry (dialog, group, _("_Server Address"));
  goa_provider_dialog_add_description (dialog, data->uri, _("Examples: example.com, 192.168.0.82"));
  g_signal_connect (data->uri,
                    "changed",
                    G_CALLBACK (on_entry_changed),
                    data);

  /* Action Buttons
   */
  group = goa_provider_dialog_add_group (dialog, NULL);
  data->login_button = goa_authflow_button_new ();
  gtk_widget_set_sensitive (data->login_button, FALSE);
  g_signal_connect (data->login_button,
                    "copy-activated",
                    G_CALLBACK (on_copy_activated),
                    data);
  g_signal_connect (data->login_button,
                    "link-activated",
                    G_CALLBACK (on_link_activated),
                    data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->login_button);

  /* Discovery */
  data->discovery_status = g_object_new (GTK_TYPE_BOX,
                                         "margin-start",   8,
                                         "margin-end",     2,
                                         "width-request",  24,
                                         "height-request", 24,
                                         "visible",        FALSE,
                                         NULL);
  adw_entry_row_add_suffix (ADW_ENTRY_ROW (data->uri), data->discovery_status);

  icon = gtk_image_new_from_icon_name ("emblem-default-symbolic");
  gtk_widget_add_css_class (GTK_WIDGET (icon), "success");
  gtk_box_append (GTK_BOX (data->discovery_status), GTK_WIDGET (icon));

  data->discovery_spinner = gtk_spinner_new ();
  g_object_bind_property (data->discovery_spinner, "visible",
                          icon,                    "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  g_object_bind_property (data->discovery_spinner, "visible",
                          data->discovery_spinner, "spinning",
                          G_BINDING_SYNC_CREATE);
  gtk_box_append (GTK_BOX (data->discovery_status), data->discovery_spinner);

  /* Set the default widget after it's a child of the window */
  adw_dialog_set_default_widget (ADW_DIALOG (dialog), data->login_button);
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
add_account_loginflow2_cb (GObject      *unused,
                           GAsyncResult *result,
                           gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = GOA_PROVIDER (g_task_get_source_object (task));
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *username;
  const char *password;
  g_autoptr(GUri) uri = NULL;
  g_autofree char *credentials_uri = NULL;
  g_autofree char *webdav_uri = NULL;
  g_autofree char *dav_uri = NULL;
  g_autofree char *presentation_identity = NULL;
  g_autoptr(GError) error = NULL;

  credentials_uri = goa_loginflow2_authenticate_finish (result, &error);
  if (credentials_uri == NULL)
    {
      data->accept_ssl_errors = (error->code == GOA_ERROR_SSL);
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  uri = g_uri_parse (credentials_uri, G_URI_FLAGS_HAS_PASSWORD, &error);
  if (uri == NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  username = g_uri_get_user (uri);
  password = g_uri_get_password (uri);
  if (strchr (username, '@') != NULL)
    presentation_identity = g_strdup (username);
  else
    presentation_identity = g_strconcat (username, "@", g_uri_get_host (uri), NULL);

  /* The credentials are already authenticated; just populate the URIs.
   */
  webdav_uri = g_uri_resolve_relative (credentials_uri,
                                       "remote.php/webdav",
                                       G_URI_FLAGS_PARSE_RELAXED,
                                       NULL);
  dav_uri = g_uri_resolve_relative (credentials_uri,
                                    "remote.php/dav",
                                    G_URI_FLAGS_PARSE_RELAXED,
                                    NULL);

  /* Add the account
   */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "Uri", webdav_uri);
  g_variant_builder_add (&details, "{ss}", "FilesEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CalDavUri", dav_uri);
  g_variant_builder_add (&details, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (&details, "{ss}", "CardDavUri", dav_uri);
  g_variant_builder_add (&details, "{ss}", "AcceptSslErrors", "false");

  goa_manager_call_add_account (goa_client_get_manager (data->client),
                                goa_provider_get_provider_type (provider),
                                username,
                                presentation_identity,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  g_autofree char *auth_uri = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  goa_loginflow2_authenticate (data->auth,
                               &auth_uri,
                               data->flags,
                               cancellable,
                               (GAsyncReadyCallback) add_account_loginflow2_cb,
                               g_object_ref (task));

  if ((data->flags & GOA_AUTH_FLOW_DO_NOT_LAUNCH_URI) != 0 && auth_uri != NULL)
    {
      gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (dialog)), auth_uri);
      goa_provider_dialog_add_toast (dialog, adw_toast_new (_("Copied to clipboard")));
    }
}

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
                           G_CONNECT_DEFAULT);
  goa_provider_task_run_in_dialog (task, data->dialog);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_owncloud_provider_init (GoaOwncloudProvider *self)
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
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->build_object               = build_object;

  provider_class->add_account                = add_account;
}

