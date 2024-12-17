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

#include "goadavclient.h"
#include "goaprovider.h"
#include "goasouplogger.h"
#include "goawebdavprovider.h"
#include "goawebdavprovider-priv.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

#include "goaowncloudprovider.h"

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
  char *uri;
  char *poll_uri;
  char *poll_data;
  GCancellable *cancellable;
  unsigned int cancellable_id;
  unsigned int complete : 1;
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

  g_clear_pointer (&data->uri, g_free);
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
  g_autoptr (GBytes) body = NULL;
  g_autoptr (JsonParser) parser = NULL;
  const char *json_data;
  size_t json_len;
  JsonObject *response;
  const char *server;
  const char *username;
  const char *password;
  SoupStatus status = 0;
  g_autoptr (GError) error = NULL;

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
      g_timeout_add_seconds_once (5, loginflow2_poll_timeout_cb, g_object_ref (task));
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
  if (response == NULL)
    {
      /* HACK: We treat valid JSON with an unexpected root type like the
       *       expected 404 response, because sometimes Nextcloud seems to
       *       return an empty array instead.
       */
      g_timeout_add_seconds_once (5, loginflow2_poll_timeout_cb, g_object_ref (task));
      return;
    }

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

      data->complete = TRUE;
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
  data->complete = TRUE;
  if (data->error != NULL)
    g_task_return_error (task, g_steal_pointer (&data->error));
}

static void
loginflow2_poll_timeout_cb (gpointer user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GCancellable *cancellable = g_task_get_cancellable (task);
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);

  if (!data->complete && !g_cancellable_is_cancelled (cancellable))
    {
      g_autoptr(SoupMessage) message = NULL;

      message = soup_message_new_from_encoded_form (SOUP_METHOD_POST,
                                                    data->poll_uri,
                                                    g_strdup (data->poll_data));
      soup_session_send_and_read_async (data->session,
                                        message,
                                        G_PRIORITY_DEFAULT,
                                        data->cancellable,
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
  g_timeout_add_seconds_once (5,
                              loginflow2_poll_timeout_cb,
                              g_object_ref (task));
}

static void
loginflow2_initiate_cb (SoupSession  *session,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  SoupMessage *message = NULL;
  g_autoptr (GBytes) body = NULL;
  g_autoptr (JsonParser) parser = NULL;
  const char *json_data;
  size_t json_len;
  JsonObject *response, *poll_data;
  const char *endpoint;
  const char *token;
  const char *login_uri;
  g_autoptr (GError) error = NULL;

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  body = soup_session_send_and_read_finish (session, result, &error);
  if (error != NULL && data->error == NULL)
    g_propagate_error (&data->error, g_steal_pointer (&error));

  if (data->error != NULL)
    goto fail;

  message = soup_session_get_async_result_message (session, result);
  if (soup_message_get_status (message) != SOUP_STATUS_OK)
    {
      goa_utils_set_error_soup (&data->error, message);
      goto fail;
    }

  parser = json_parser_new ();
  json_data = g_bytes_get_data (body, &json_len);
  if (!json_parser_load_from_data (parser, json_data, json_len, &data->error))
    goto fail;

  response = json_node_get_object (json_parser_get_root (parser));
  if (response == NULL)
    goto fail;

  login_uri = json_object_get_string_member (response, "login");
  if (login_uri == NULL || !g_uri_is_valid (login_uri, SOUP_HTTP_URI_FLAGS, &data->error))
    goto fail;

  poll_data = json_object_get_object_member (response, "poll");
  if (poll_data == NULL)
    goto fail;

  endpoint = json_object_get_string_member (poll_data, "endpoint");
  token = json_object_get_string_member (poll_data, "token");
  if (token == NULL || endpoint == NULL || !g_uri_is_valid (endpoint, SOUP_HTTP_URI_FLAGS, &data->error))
    goto fail;

  data->poll_uri = g_strdup (endpoint);
  data->poll_data = g_strdup_printf ("token=%s", token);

  /* Start the web browser flow
   */
  g_app_info_launch_default_for_uri_async (login_uri,
                                           NULL,
                                           data->cancellable,
                                           (GAsyncReadyCallback) loginflow2_launch_uri_cb,
                                           g_object_ref (task));
  return;

fail:
  if (data->error == NULL)
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Could not parse response"));
    }
  else
    {
      g_task_return_error (task, g_steal_pointer (&data->error));
    }
}

/**
 * goa_loginflow2_authenticate:
 * @uri: a Nextcloud URI
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Perform Login Flow v2 authentication for @uri.
 *
 * Call [method@Goa.loginflow2_authenticate_finish] to get the result.
 *
 * See: https://docs.nextcloud.com/server/latest/developer_manual/client_apis/LoginFlow/
 */
static void
goa_loginflow2_authenticate (const char          *uri,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (GUri) login = NULL;
  g_autoptr (SoupLogger) logger = NULL;
  g_autoptr (SoupMessage) message = NULL;
  g_autofree char *message_uri = NULL;
  LoginFlow2Data *data;

  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  login = g_uri_parse (uri, SOUP_HTTP_URI_FLAGS, NULL);
  if (login != NULL)
    {
      g_autoptr(GUri) tmp = g_steal_pointer (&login);
      const char *path = g_uri_get_path (tmp);
      g_autofree char *new_path = NULL;

      if (!g_strrstr (path, "/index.php/login/v2"))
        new_path = g_build_path (path, "/index.php/login/v2", NULL);
      else
        new_path = g_strdup (path);

      login = g_uri_build (g_uri_get_flags (tmp),
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
                               goa_loginflow2_authenticate,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Invalid URI: %s"),
                               uri);
      return;
    }

  data = g_new0 (LoginFlow2Data, 1);
  data->uri = g_uri_to_string (login);
  data->session = soup_session_new ();

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

  message = soup_message_new (SOUP_METHOD_POST, data->uri);
  if (message == NULL)
    {
      g_task_report_new_error (NULL, callback, user_data,
                               goa_loginflow2_authenticate,
                               GOA_ERROR,
                               GOA_ERROR_FAILED,
                               _("Invalid URI: %s"),
                               uri);
      return;
    }

  soup_session_send_and_read_async (data->session,
                                    message,
                                    G_PRIORITY_DEFAULT,
                                    data->cancellable,
                                    (GAsyncReadyCallback) loginflow2_initiate_cb,
                                    g_object_ref (task));
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
loginflow2_query_cb (SoupSession  *session,
                     GAsyncResult *result,
                     gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  LoginFlow2Data *data = (LoginFlow2Data *) g_task_get_task_data (task);
  SoupMessage *message = NULL;
  g_autoptr (GBytes) body = NULL;
  g_autoptr (GError) error = NULL;

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

  /* The method was `GET` instead of `POST` to avoid creating a token, so the
   * expected response is 405. Any other should mean not supported.
   */
  message = soup_session_get_async_result_message (session, result);
  if (soup_message_get_status (message) == SOUP_STATUS_METHOD_NOT_ALLOWED)
    {
      g_task_return_boolean (task, TRUE);
    }
  else
    {
      g_task_return_new_error_literal (task,
                                       GOA_ERROR,
                                       GOA_ERROR_NOT_SUPPORTED,
                                       "Login Flow v2 not supported");
    }
}

/**
 * goa_loginflow2_query:
 * @uri: a Nextcloud URI
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Check if Login Flow v2 is supported by @uri.
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
  g_autoptr (GTask) task = NULL;
  g_autoptr (GUri) login = NULL;
  g_autoptr (SoupLogger) logger = NULL;
  g_autoptr (SoupMessage) message = NULL;
  g_autofree char *message_uri = NULL;
  LoginFlow2Data *data;

  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  login = g_uri_parse (uri, SOUP_HTTP_URI_FLAGS, NULL);
  if (login != NULL)
    {
      g_autoptr(GUri) tmp = g_steal_pointer (&login);
      const char *path = g_uri_get_path (tmp);
      g_autofree char *new_path = NULL;

      if (!g_strrstr (path, "/index.php/login/v2"))
        new_path = g_build_path (path, "/index.php/login/v2", NULL);
      else
        new_path = g_strdup (path);

      login = g_uri_build (g_uri_get_flags (tmp),
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
  data->uri = g_uri_to_string (login);
  data->session = soup_session_new ();

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

  message = soup_message_new (SOUP_METHOD_GET, data->uri);
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

  soup_session_send_and_read_async (data->session,
                                    message,
                                    G_PRIORITY_DEFAULT,
                                    data->cancellable,
                                    (GAsyncReadyCallback) loginflow2_query_cb,
                                    g_object_ref (task));
}

/**
 * goa_loginflow2_query_finish:
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Get the result of an operation started with [func@Goa.loginflow2_query].
 *
 * Returns: %TRUE if Login Flow v2 is supported, or %FALSE with @error set
 */
static gboolean
goa_loginflow2_query_finish (GAsyncResult  *result,
                             GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == goa_loginflow2_query, FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
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
}
