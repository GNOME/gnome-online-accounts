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
#include "goaoauth2provider-priv.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goasouplogger.h"
#include "goawebdavprovider.h"
#include "goawebdavprovider-priv.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

#include "goaowncloudprovider.h"

#define LOGINFLOW2_PATH          "index.php/login/v2"
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
  GCancellable *cancellable;
  unsigned int cancellable_id;
  GCancellable *req_cancellable;
  GoaAuthFlowFlags flags;
  gboolean accept_ssl_errors;
  char *poll_uri;
  char *poll_data;
  unsigned int poll_id;
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
  g_clear_handle_id (&data->poll_id, g_source_remove);
  g_free (data);
}

static void
loginflow2_cancelled_cb (GCancellable *cancellable,
                         gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);

  /* libsoup will call g_cancellable_cancel() on the caller's cancellable
   */
  soup_session_abort (data->session);
  g_clear_handle_id (&data->poll_id, g_source_remove);
}

static gboolean
loginflow2_accept_certificate (SoupMessage          *msg,
                               GTlsCertificate      *cert,
                               GTlsCertificateFlags  cert_flags,
                               gpointer              user_data)
{
  GTask *task = G_TASK (user_data);
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);

  if (data->accept_ssl_errors || cert_flags == 0)
    return TRUE;

  if (data->error == NULL)
    goa_utils_set_error_ssl (&data->error, cert_flags);

  return FALSE;
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
  SoupStatus status = SOUP_STATUS_NONE;
  g_autoptr(GError) error = NULL;

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  body = soup_session_send_and_read_finish (session, result, &error);
  if (error == NULL && data->error != NULL)
    {
      g_propagate_error (&error, g_steal_pointer (&data->error));
    }

  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  message = soup_session_get_async_result_message (session, result);
  status = soup_message_get_status (message);
  if (status == SOUP_STATUS_NOT_FOUND)
    {
      /* The endpoint returns a 404 until the request is approved.
       */
      data->poll_id = g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                                                  loginflow2_poll_timeout_cb,
                                                  g_object_ref (task));
      return;
    }
  else if (status != SOUP_STATUS_OK)
    {
      goa_utils_set_error_soup (&error, message);
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  parser = json_parser_new ();
  json_data = g_bytes_get_data (body, &json_len);
  if (!json_parser_load_from_data (parser, json_data, json_len, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  response = json_node_get_object (json_parser_get_root (parser));
  server = json_object_get_string_member (response, "server");
  username = json_object_get_string_member (response, "loginName");
  password = json_object_get_string_member (response, "appPassword");
  if (server != NULL && username != NULL && password != NULL)
    {
      g_autoptr(GUri) uri = NULL;
      g_autofree char *ret = NULL;

      uri = g_uri_parse (server, SOUP_HTTP_URI_FLAGS, &error);
      if (uri == NULL)
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }

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
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
    }
}

static void
loginflow2_poll_timeout_cb (gpointer user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  g_autoptr(SoupMessage) message = NULL;

  message = soup_message_new_from_encoded_form (SOUP_METHOD_POST,
                                                data->poll_uri,
                                                g_strdup (data->poll_data));
  g_signal_connect_object (message,
                           "accept-certificate",
                           G_CALLBACK (loginflow2_accept_certificate),
                           task,
                           G_CONNECT_DEFAULT);
  soup_session_send_and_read_async (data->session,
                                    message,
                                    G_PRIORITY_DEFAULT,
                                    data->req_cancellable,
                                    (GAsyncReadyCallback) loginflow2_poll_cb,
                                    g_object_ref (task));
  data->poll_id = 0;
}

static void
loginflow2_launch_uri_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  GError *error = NULL;

  if (!g_app_info_launch_default_for_uri_finish (result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Start polling for a response
   */
  data->poll_id = g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                                              loginflow2_poll_timeout_cb,
                                              g_object_ref (task));
}

/*< private >
 * goa_loginflow2_authenticate:
 * @auth: a `JsonNode`
 * @flags: the authentication flags
 * @accept_ssl_errors: whether to ignore certificate errors
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Perform Login Flow v2 authentication for @auth.
 *
 * Call [method@Goa.loginflow2_authenticate_finish] to get the result.
 *
 * See: https://docs.nextcloud.com/server/latest/developer_manual/client_apis/LoginFlow/
 */
static void
goa_loginflow2_authenticate (JsonNode             *auth,
                             GoaAuthFlowFlags      flags,
                             gboolean              accept_ssl_errors,
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
  endpoint = json_object_get_string_member (poll_data, "endpoint");
  token = json_object_get_string_member (poll_data, "token");

  data = g_new0 (LoginFlow2Data, 1);
  data->flags = flags;
  data->accept_ssl_errors = accept_ssl_errors;
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
      data->poll_id = g_timeout_add_seconds_once (LOGINFLOW2_POLL_INTERVAL,
                                                  loginflow2_poll_timeout_cb,
                                                  g_object_ref (task));
    }
}

/*< private >
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
  SoupMessage *message = NULL;
  g_autoptr(GBytes) body = NULL;
  g_autoptr(JsonParser) parser = NULL;
  g_autoptr(JsonNode) response = NULL;
  JsonObject *auth_data, *poll_data;
  const char *json_data;
  size_t json_len;
  g_autoptr(GError) error = NULL;

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  body = soup_session_send_and_read_finish (session, result, &error);
  if (error != NULL)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  message = soup_session_get_async_result_message (session, result);
  if (soup_message_get_status (message) != SOUP_STATUS_OK)
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_SUPPORTED,
                                       _("Not supported"));
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

  response = json_parser_steal_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (response))
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
      return;
    }

  auth_data = json_node_get_object (response);
  poll_data = json_object_get_object_member (auth_data, "poll");
  if (poll_data != NULL
      && json_object_get_string_member (auth_data, "login") != NULL
      && json_object_get_string_member (poll_data, "endpoint") != NULL
      && json_object_get_string_member (poll_data, "token") != NULL)
    {
      g_task_return_pointer (task,
                             g_steal_pointer (&response),
                             (GDestroyNotify)json_node_unref);
    }
  else
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
    }
}

/*< private >
 * goa_loginflow2_query:
 * @uri: a Nextcloud URI
 * @accept_ssl_errors: whether to ignore certificate errors
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
                      gboolean             accept_ssl_errors,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(SoupLogger) logger = NULL;
  g_autoptr(SoupMessage) message = NULL;
  LoginFlow2Data *data;

  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  if (!g_strrstr (uri, LOGINFLOW2_PATH))
    {
      g_autofree char *message_uri = NULL;

      message_uri = g_uri_resolve_relative (uri,
                                            LOGINFLOW2_PATH,
                                            G_URI_FLAGS_PARSE_RELAXED | G_URI_FLAGS_ENCODED,
                                            NULL);
      message = soup_message_new (SOUP_METHOD_POST, message_uri);
    }
  else
    {
      message = soup_message_new (SOUP_METHOD_POST, uri);
    }

  if (message == NULL)
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
  data->accept_ssl_errors = accept_ssl_errors;
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

  g_signal_connect_object (message,
                           "accept-certificate",
                           G_CALLBACK (loginflow2_accept_certificate),
                           task,
                           G_CONNECT_DEFAULT);
  soup_session_send_and_read_async (data->session,
                                    message,
                                    G_PRIORITY_DEFAULT,
                                    data->req_cancellable,
                                    (GAsyncReadyCallback) goa_loginflow2_query_cb,
                                    g_object_ref (task));
}

/*< private >
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
  GCancellable *cancellable;
  char *identity;
  gboolean accept_ssl_errors;
  GoaAuthFlowFlags flags;

  JsonNode *login_info;
  GCancellable *discover;

  GtkWidget *uri;
  GtkWidget *login_button;
  GtkWidget *discovery_status;
  GtkWidget *discovery_spinner;
  unsigned int discovery_id;
} AddAccountData;

static void
add_account_data_free (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_clear_object (&data->cancellable);
  g_clear_pointer (&data->identity, g_free);
  g_clear_pointer (&data->login_info, json_node_unref);
  g_cancellable_cancel (data->discover);
  g_clear_object (&data->discover);
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
      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_READY);
      gtk_widget_set_visible (data->discovery_status, TRUE);
      gtk_widget_set_visible (data->discovery_spinner, FALSE);
      gtk_accessible_update_state (GTK_ACCESSIBLE (data->uri),
                                   GTK_ACCESSIBLE_STATE_BUSY, FALSE,
                                   -1);

      g_clear_pointer (&data->login_info, json_node_unref);
      data->login_info = json_node_ref (response);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
      g_debug ("%s(): %s", G_STRFUNC, error->message);

      goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
      gtk_widget_set_visible (data->discovery_status, FALSE);
      gtk_accessible_update_state (GTK_ACCESSIBLE (data->uri),
                                   GTK_ACCESSIBLE_STATE_BUSY, FALSE,
                                   -1);

      if (g_error_matches (error, GOA_ERROR, GOA_ERROR_SSL))
        {
          data->accept_ssl_errors = TRUE;
          goa_provider_dialog_report_error (data->dialog, g_steal_pointer (&error));
        }
      else
        {
          goa_provider_dialog_add_toast (data->dialog, adw_toast_new (error->message));
        }
    }
}

static void
loginflow2_query (gpointer user_data)
{
  AddAccountData *data = (AddAccountData *)user_data;
  const char *uri;

  uri = gtk_editable_get_text (GTK_EDITABLE (data->uri));
  if (uri != NULL && *uri != '\0')
    {
      g_autofree char *normalized_uri = NULL;

      normalized_uri = goa_utils_normalize_url (uri, NULL, NULL);
      if (normalized_uri != NULL)
        {
          gtk_widget_set_visible (data->discovery_status, TRUE);
          gtk_widget_set_visible (data->discovery_spinner, TRUE);
          gtk_accessible_update_state (GTK_ACCESSIBLE (data->uri),
                                       GTK_ACCESSIBLE_STATE_BUSY, TRUE,
                                       -1);

          data->discover = g_cancellable_new ();
          goa_loginflow2_query (normalized_uri,
                                data->accept_ssl_errors,
                                data->discover,
                                (GAsyncReadyCallback) loginflow2_query_cb,
                                data);
        }
    }

  data->discovery_id = 0;
}

static void
on_entry_changed (GtkEditable    *editable,
                  AddAccountData *data)
{
  goa_provider_dialog_set_state (data->dialog, GOA_DIALOG_IDLE);
  gtk_widget_set_visible (data->discovery_status, FALSE);
  g_cancellable_cancel (data->discover);
  g_clear_object (&data->discover);
  g_clear_handle_id (&data->discovery_id, g_source_remove);

  /* Reset the preference to ignore SSL/TLS errors and start
   * discovery after a short delay.
   */
  data->accept_ssl_errors = FALSE;
  data->discovery_id = g_timeout_add_once (250, loginflow2_query, data);
}

static void
on_copy_activated (GoaAuthflowButton *widget,
                   AddAccountData    *data)
{
  const char *login_uri = NULL;

  login_uri = json_object_get_string_member (json_node_get_object (data->login_info), "login");
  gdk_clipboard_set_text (gtk_widget_get_clipboard (GTK_WIDGET (data->dialog)), login_uri);
  goa_provider_dialog_add_toast (data->dialog, adw_toast_new (_("Copied to clipboard")));

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
                                _("Add a Nextcloud account by providing your server address"));

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->uri = goa_provider_dialog_add_entry (dialog, group, _("_Server Address"));
  goa_provider_dialog_add_description (dialog, data->uri, _("Examples: example.com, 192.168.0.82"));
  g_signal_connect (data->uri,
                    "changed",
                    G_CALLBACK (on_entry_changed),
                    data);

  /* Discovery spinner
   */
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

  /* Action Buttons
   */
  group = goa_provider_dialog_add_group (dialog, NULL);
  data->login_button = goa_authflow_button_new ();
  g_signal_connect (data->login_button,
                    "copy-activated",
                    G_CALLBACK (on_copy_activated),
                    data);
  g_signal_connect (data->login_button,
                    "link-activated",
                    G_CALLBACK (on_link_activated),
                    data);
  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->login_button);
  gtk_box_append (GTK_BOX (data->discovery_status), data->discovery_spinner);

  /* Set the default widget after it's a child of the window */
  adw_dialog_set_default_widget (ADW_DIALOG (dialog), data->login_button);
  gtk_widget_set_sensitive (data->login_button, FALSE);
  gtk_accessible_update_state (GTK_ACCESSIBLE (data->uri),
                               GTK_ACCESSIBLE_STATE_BUSY, FALSE,
                               -1);

  if (!new_account)
    {
      g_autofree char *webdav_uri = NULL;
      g_autofree char *server = NULL;

      webdav_uri = goa_util_lookup_keyfile_string (data->object, "Uri");
      server = g_strndup (webdav_uri, strlen (webdav_uri) - strlen ("/remote.php/webdav"));
      gtk_editable_set_text (GTK_EDITABLE (data->uri), server);
      gtk_editable_set_editable (GTK_EDITABLE (data->uri), FALSE);
    }
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
  GVariantBuilder credentials;
  GVariantBuilder details;
  const char *username;
  const char *password;
  g_autoptr(GUri) uri = NULL;
  g_autofree char *credentials_uri = NULL;
  g_autofree char *base_uri = NULL;
  g_autofree char *webdav_path = NULL;
  g_autofree char *webdav_uri = NULL;
  g_autofree char *dav_path = NULL;
  g_autofree char *dav_uri = NULL;
  g_autofree char *presentation_identity = NULL;
  g_autoptr(GError) error = NULL;

  credentials_uri = goa_loginflow2_authenticate_finish (result, &error);
  if (credentials_uri == NULL)
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  uri = g_uri_parse (credentials_uri, SOUP_HTTP_URI_FLAGS, &error);
  if (uri == NULL)
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
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
  base_uri = g_uri_join (G_URI_FLAGS_PARSE_RELAXED,
                         g_uri_get_scheme (uri),
                         NULL,
                         g_uri_get_host (uri),
                         g_uri_get_port (uri),
                         g_uri_get_path (uri),
                         g_uri_get_query (uri),
                         g_uri_get_fragment (uri));
  webdav_uri = goa_utils_normalize_url (base_uri, "remote.php/webdav", NULL);
  dav_uri = goa_utils_normalize_url (base_uri, "remote.php/dav", NULL);

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
                                data->cancellable,
                                (GAsyncReadyCallback) add_account_credentials_cb,
                                g_object_ref (task));
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_DONE)
    {
      g_cancellable_cancel (data->cancellable);
      return;
    }

  if (goa_provider_dialog_get_state (data->dialog) == GOA_DIALOG_BUSY)
    {
      /* A certificate error was encountered when querying for token
       */
      if (data->login_info == NULL && data->accept_ssl_errors)
        {
          loginflow2_query (data);
          return;
        }

      goa_loginflow2_authenticate (data->login_info,
                                   data->flags,
                                   data->accept_ssl_errors,
                                   data->cancellable,
                                   (GAsyncReadyCallback) add_account_loginflow2_cb,
                                   g_object_ref (task));
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
  data->cancellable = g_cancellable_new ();

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
refresh_account_credentials_cb (GoaAccount   *account,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GError *error = NULL;

  if (!goa_account_call_ensure_credentials_finish (account, NULL, result, &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
refresh_account_loginflow2_cb (GObject      *unused,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaProvider *provider = GOA_PROVIDER (g_task_get_source_object (task));
  AddAccountData *data = g_task_get_task_data (task);
  g_autofree char *credentials_uri = NULL;
  g_autoptr(GUri) uri = NULL;
  GVariantBuilder credentials;
  const char *username;
  const char *password;
  g_autoptr(GError) error = NULL;

  credentials_uri = goa_loginflow2_authenticate_finish (result, &error);
  if (credentials_uri == NULL)
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  uri = g_uri_parse (credentials_uri, G_URI_FLAGS_HAS_PASSWORD, &error);
  if (uri == NULL)
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  username = g_uri_get_user (uri);
  if (g_strcmp0 (data->identity, username) != 0)
    {
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Was asked to log in as %s, but logged in as %s"),
                               data->identity,
                               username);
      return;
    }

  /* Update the credentials
   */
  password = g_uri_get_password (uri);
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    data->object,
                                                    g_variant_builder_end (&credentials),
                                                    data->cancellable,
                                                    &error))
    {
      goa_provider_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  goa_account_call_ensure_credentials (goa_object_peek_account (data->object),
                                       data->cancellable,
                                       (GAsyncReadyCallback) refresh_account_credentials_cb,
                                       g_steal_pointer (&task));
}

static void
refresh_account_action_cb (GoaProviderDialog *dialog,
                           GParamSpec        *pspec,
                           GTask             *task)
{
  AddAccountData *data = g_task_get_task_data (task);

  if (goa_provider_dialog_get_state (dialog) == GOA_DIALOG_DONE)
    {
      g_cancellable_cancel (data->cancellable);
      return;
    }

  if (goa_provider_dialog_get_state (data->dialog) == GOA_DIALOG_BUSY)
    {
      goa_loginflow2_authenticate (data->login_info,
                                   data->flags,
                                   data->accept_ssl_errors,
                                   data->cancellable,
                                   (GAsyncReadyCallback) refresh_account_loginflow2_cb,
                                   g_object_ref (task));
    }
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
  data->cancellable = g_cancellable_new ();
  data->identity = goa_util_lookup_keyfile_string (object, "Identity");
  data->accept_ssl_errors = goa_util_lookup_keyfile_boolean (object, "AcceptSslErrors");

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, refresh_account);
  g_task_set_task_data (task, data, add_account_data_free);

  create_account_details_ui (provider, data, FALSE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (refresh_account_action_cb),
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
  provider_class->refresh_account            = refresh_account;
}

