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
#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>

#include "goadavconfig.h"
#include "goasouplogger.h"
#include "goautils.h"

#include "goadavclient.h"

#define WELL_KNOWN_CALDAV    "/.well-known/caldav"
#define WELL_KNOWN_CARDDAV   "/.well-known/carddav"
#define WELL_KNOWN_NEXTCLOUD "remote.php/dav"

/* Fastmail
 * See: https://www.fastmail.help/hc/en-us/articles/1500000278342-Server-names-and-ports
 */
#define FASTMAIL_WEBDAV      "https://myfiles.fastmail.com"
#define FASTMAIL_CALDAV      "https://caldav.fastmail.com/.well-known/caldav"
#define FASTMAIL_CARDDAV     "https://carddav.fastmail.com/.well-known/carddav"

/* mailbox.org
 * See: https://kb.mailbox.org/en/private/drive-article/webdav-for-linux/
 */
#define MAILBOX_ORG_HOSTNAME "dav.mailbox.org"
#define MAILBOX_ORG_WEBDAV   "https://dav.mailbox.org/servlet/webdav.infostore"
#define MAILBOX_ORG_CALDAV   "https://dav.mailbox.org/caldav"
#define MAILBOX_ORG_CARDDAV  "https://dav.mailbox.org/carddav"

struct _GoaDavClient
{
  GObject parent_instance;
};

G_DEFINE_TYPE (GoaDavClient, goa_dav_client, G_TYPE_OBJECT)

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_dav_client_init (GoaDavClient *self)
{
}

static void
goa_dav_client_class_init (GoaDavClientClass *klass)
{
}

/* ---------------------------------------------------------------------------------------------------- */

GoaDavClient *
goa_dav_client_new (void)
{
  return g_object_new (GOA_TYPE_DAV_CLIENT, NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  SoupSession *session;
  SoupMessage *msg;
  GoaDavConfig *config;
  char *uri;
  char *username;
  char *password;
  gboolean accept_ssl_errors;
  gboolean well_known_fallback;
  gulong cancellable_id;
  GCancellable *cancellable;
  GError *error;
} CheckData;

static void
dav_client_check_data_free (gpointer user_data)
{
  CheckData *data = user_data;

  if (data->cancellable_id > 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_object_unref (data->cancellable);
    }

  g_free (data->uri);
  g_free (data->username);
  g_free (data->password);
  g_clear_error (&data->error);
  g_clear_object (&data->config);
  g_clear_object (&data->msg);
  g_clear_object (&data->session);
  g_free (data);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  char *password;
  char *username;
} CheckAuthData;

static void
dav_client_check_auth_data_free (gpointer  data,
                                 GClosure *closure)
{
  CheckAuthData *auth = data;

  g_free (auth->password);
  g_free (auth->username);
  g_free (auth);
}

static gboolean
dav_client_accept_certificate (SoupMessage          *msg,
                               GTlsCertificate      *cert,
                               GTlsCertificateFlags  cert_flags,
                               gpointer              user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data = (CheckData *) g_task_get_task_data (task);

  if (data->accept_ssl_errors || cert_flags == 0)
    return TRUE;

  if (data->error == NULL)
    goa_utils_set_error_ssl (&data->error, cert_flags);

  return FALSE;
}

static gboolean
dav_client_authenticate (SoupMessage *msg,
                         SoupAuth    *auth,
                         gboolean     retrying,
                         gpointer     user_data)
{
  CheckAuthData *data = (CheckAuthData *) user_data;

  if (retrying)
    return FALSE;

  soup_auth_authenticate (auth, data->username, data->password);
  return TRUE;
}

static void
dav_client_authenticate_task (GTask *task)
{
  CheckData *data = (CheckData *) g_task_get_task_data (task);
  CheckAuthData *auth;

  auth = g_new0 (CheckAuthData, 1);
  auth->username = g_strdup (data->username);
  auth->password = g_strdup (data->password);
  g_signal_connect_data (data->msg,
                         "authenticate",
                         G_CALLBACK (dav_client_authenticate),
                         auth,
                         dav_client_check_auth_data_free,
                         0);
  g_signal_connect (data->msg,
                    "accept-certificate",
                    G_CALLBACK (dav_client_accept_certificate),
                    task);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dav_client_check_cancelled_cb (GCancellable *cancellable,
                               gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  CheckData *data = (CheckData *) g_task_get_task_data (task);

  g_debug ("goa_dav_client_check(): cancelled");

  /* The callback will be invoked after we have returned to the main
   * loop.
   */
  soup_session_abort (data->session);
}
/* ---------------------------------------------------------------------------------------------------- */

static GoaProviderFeatures
_soup_message_get_dav_features (SoupMessage  *message,
                                GError      **error)
{
  SoupMessageHeaders *headers = NULL;
  const char *dav_header = NULL;
  g_auto (GStrv) dav_features = NULL;
  GoaProviderFeatures ret = 0;

  headers = soup_message_get_response_headers (message);
  dav_header = soup_message_headers_get_list (headers, "DAV");
  if (dav_header == NULL || *dav_header == '\0')
    {
      g_debug ("%s(): no 'DAV' entry in response headers", G_STRFUNC);
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_SUPPORTED,
                           "DAV not supported");
      return ret;
    }

  dav_features = g_strsplit_set (dav_header, ", ", -1);

  /* See: https://datatracker.ietf.org/doc/html/rfc4918#section-18.1
   */
  if (g_strv_contains ((const char * const *)dav_features, "1"))
    ret |= GOA_PROVIDER_FEATURE_FILES;

  /* See: https://datatracker.ietf.org/doc/html/rfc4791#section-5.1
   */
  if (g_strv_contains ((const char * const *)dav_features, "calendar-access"))
    ret |= GOA_PROVIDER_FEATURE_CALENDAR;

  /* See: https://datatracker.ietf.org/doc/html/rfc6352#section-6.1
   */
  if (g_strv_contains ((const char * const *)dav_features, "addressbook"))
    ret |= GOA_PROVIDER_FEATURE_CONTACTS;

  if (ret == 0)
    {
      g_set_error_literal (error,
                           GOA_ERROR,
                           GOA_ERROR_NOT_SUPPORTED,
                           "DAV not supported");
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
dav_client_check_options_cb (SoupSession  *session,
                             GAsyncResult *result,
                             gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GBytes) body = NULL;
  SoupMessage *msg;
  GoaProviderFeatures features = GOA_PROVIDER_FEATURE_INVALID;
  const char *service = NULL;
  CheckData *data;
  guint status;
  g_autoptr (GError) error = NULL;

  msg = soup_session_get_async_result_message (session, result);
  g_debug ("goa_dav_client_check(): response (%p, %u)", msg, soup_message_get_status (msg));

  body = soup_session_send_and_read_finish (session, result, &error);

  data = (CheckData *) g_task_get_task_data (task);

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  if (error != NULL && data->error == NULL)
    g_propagate_error (&data->error, g_steal_pointer (&error));

  if (data->error != NULL)
    goto out;

  status = soup_message_get_status (msg);
  switch (status)
    {
    case SOUP_STATUS_OK:
      break;

    case SOUP_STATUS_NOT_FOUND:
    case SOUP_STATUS_METHOD_NOT_ALLOWED:
    case SOUP_STATUS_INTERNAL_SERVER_ERROR:
    case SOUP_STATUS_NOT_IMPLEMENTED:
      goto fallback;

    default:
      goa_utils_set_error_soup (&data->error, msg);
      goto out;
    }

  features = _soup_message_get_dav_features (msg, &data->error);
  if (data->error != NULL)
    goto fallback;

  service = goa_service_config_get_service (GOA_SERVICE_CONFIG (data->config));
  if (g_strcmp0 (service, GOA_SERVICE_TYPE_CALDAV) == 0)
    {
      if ((features & GOA_PROVIDER_FEATURE_CALENDAR) != GOA_PROVIDER_FEATURE_CALENDAR)
        {
          g_debug ("CalDAV service at \"%s\" is missing \"calendar-access\" in the DAV header",
                   goa_dav_config_get_uri (data->config));
        }

      goto out;
    }

  if (g_strcmp0 (service, GOA_SERVICE_TYPE_CARDDAV) == 0)
    {
      if ((features & GOA_PROVIDER_FEATURE_CONTACTS) != GOA_PROVIDER_FEATURE_CONTACTS)
        {
          g_debug ("CardDAV service at \"%s\" is missing \"addressbook\" in the DAV header",
                   goa_dav_config_get_uri (data->config));
        }

      goto out;
    }

  if (g_strcmp0 (service, GOA_SERVICE_TYPE_WEBDAV) == 0)
    {
      if ((features & GOA_PROVIDER_FEATURE_FILES) != GOA_PROVIDER_FEATURE_FILES)
        {
          g_debug ("WebDAV service at \"%s\" is missing \"1\" in the DAV header",
                   goa_dav_config_get_uri (data->config));
        }

      goto out;
    }

fallback:
  if (!data->well_known_fallback)
    {
      const char *uri = goa_dav_config_get_uri (data->config);
      g_autofree char *fallback_uri = NULL;

      if (g_strcmp0 (service, GOA_SERVICE_TYPE_CALDAV) == 0)
        fallback_uri = g_uri_resolve_relative (uri, WELL_KNOWN_CALDAV, G_URI_FLAGS_NONE, NULL);
      else if (g_strcmp0 (service, GOA_SERVICE_TYPE_CARDDAV) == 0)
        fallback_uri = g_uri_resolve_relative (uri, WELL_KNOWN_CARDDAV, G_URI_FLAGS_NONE, NULL);
      else
        g_debug ("%s(): no well-known path for \"%s\" service", G_STRFUNC, service);

      if (fallback_uri != NULL)
        {
          g_clear_object (&data->msg);

          data->msg = soup_message_new (SOUP_METHOD_OPTIONS, fallback_uri);
          if (data->msg != NULL)
            {
              data->well_known_fallback = TRUE;
              g_clear_error (&data->error);

              dav_client_authenticate_task (task);
              soup_session_send_and_read_async (data->session,
                                                data->msg,
                                                G_PRIORITY_DEFAULT,
                                                data->cancellable,
                                                (GAsyncReadyCallback)dav_client_check_options_cb,
                                                g_object_ref (task));
              return;
            }
        }
    }

out:
  data->well_known_fallback = FALSE;
  if (data->error != NULL)
    g_task_return_error (task, g_steal_pointer (&data->error));
  else
    g_task_return_boolean (task, TRUE);
}

/**
 * goa_dav_client_check:
 * @self: a `GoaDavClient`
 * @config: a `GoaDavConfig`
 * @password: DAV password
 * @accept_ssl_errors: whether to ignore SSL errors
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * Check if @uri is a valid WebDAV endpoint for @username and @password.
 *
 * If @accept_ssl_errors is %TRUE, SSL related errors will be ignored.
 *
 * Call [method@Goa.DavClient] to get the result.
 */
void
goa_dav_client_check (GoaDavClient        *self,
                      GoaDavConfig        *config,
                      const char          *password,
                      gboolean             accept_ssl_errors,
                      GCancellable        *cancellable,
                      GAsyncReadyCallback  callback,
                      gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (SoupLogger) logger = NULL;
  CheckData *data;

  g_return_if_fail (GOA_IS_DAV_CLIENT (self));
  g_return_if_fail (GOA_IS_DAV_CONFIG (config));
  g_return_if_fail (password != NULL && password[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_dav_client_check);

  data = g_new0 (CheckData, 1);
  g_task_set_task_data (task, data, dav_client_check_data_free);

  data->session = soup_session_new ();
  soup_session_set_user_agent (data->session, "gnome-online-accounts/" PACKAGE_VERSION " ");

  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));

  data->config = g_object_ref (config);
  data->msg = soup_message_new (SOUP_METHOD_OPTIONS, goa_dav_config_get_uri (config));
  data->username = g_strdup (goa_dav_config_get_username (config));
  data->password = g_strdup (password);
  data->accept_ssl_errors = accept_ssl_errors;

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (dav_client_check_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  dav_client_authenticate_task (task);
  soup_session_send_and_read_async (data->session,
                                    data->msg,
                                    G_PRIORITY_DEFAULT,
                                    data->cancellable,
                                    (GAsyncReadyCallback)dav_client_check_options_cb,
                                    g_object_ref (task));
}

/**
 * goa_dav_client_check_finish:
 * @self: a `GoaDavClient`
 * @result: (nullable): a `GCancellable`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.DavClient.check].
 *
 * Returns: a bitfield of `GoaProviderFeatures`, or `0` with @error set
 */
gboolean
goa_dav_client_check_finish (GoaDavClient  *self,
                             GAsyncResult  *result,
                             GError       **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_DAV_CLIENT (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  task = G_TASK (result);
  g_return_val_if_fail (g_task_get_source_tag (task) == goa_dav_client_check, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  gboolean success;
  GError **error;
} CheckSyncData;

static void
dav_client_check_sync_cb (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  CheckSyncData *data = user_data;

  data->success = goa_dav_client_check_finish (GOA_DAV_CLIENT (source_object), res, data->error);
  g_main_loop_quit (data->loop);
}

/**
 * goa_dav_client_check_sync:
 * @self: a `GoaDavClient`
 * @config: a `GoaDavConfig`
 * @password: DAV password
 * @accept_ssl_errors: whether to ignore SSL errors
 * @cancellable: (nullable): a `GCancellable`
 * @error: (nullable): a `GError`
 *
 * Check if @uri is a valid WebDAV endpoint for @username and @password.
 *
 * This is a synchronous wrapper for [method@Goa.DavClient.check].
 *
 * Returns: %TRUE, or %FALSE with @error set
 */
gboolean
goa_dav_client_check_sync (GoaDavClient  *self,
                           GoaDavConfig  *config,
                           const char    *password,
                           gboolean       accept_ssl_errors,
                           GCancellable  *cancellable,
                           GError       **error)
{
  CheckSyncData data;
  GMainContext *context = NULL;

  g_return_val_if_fail (GOA_IS_DAV_CLIENT (self), FALSE);
  g_return_val_if_fail (GOA_IS_DAV_CONFIG (config), FALSE);
  g_return_val_if_fail (password != NULL && password[0] != '\0', FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);
  data.error = error;

  goa_dav_client_check (self,
                        config,
                        password,
                        accept_ssl_errors,
                        cancellable,
                        dav_client_check_sync_cb,
                        &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.success;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  CheckData check;

  GPtrArray *services;
  GQueue uris;
  gboolean auth_error;
} DiscoverData;

static void
goa_dav_client_discover_data_free (gpointer task_data)
{
  CheckData *check = task_data;
  DiscoverData *data = (DiscoverData *) check;

  g_clear_pointer (&data->services, g_ptr_array_unref);
  g_queue_clear_full (&data->uris, g_free);
  dav_client_check_data_free (check);
}

static void dav_client_discover_iterate (GTask *task);

static gboolean
dav_client_discover_postconfig_nexcloud (DiscoverData *discover,
                                         SoupMessage  *message)
{
  GUri *uri = soup_message_get_uri (message);
  const char *path = g_uri_get_path (uri);
  const char *server_root = NULL;

  /* Try to infer the server root from common endpoints
   */
  server_root = g_strrstr (path, "/remote.php/dav");
  if (server_root == NULL)
    server_root = g_strrstr (path, "/remote.php/webdav");

  if (server_root != NULL)
    {
      int port = -1;
      const char *scheme = NULL;
      g_autofree char *base_path = NULL;
      g_autofree char *dav_path = NULL;
      g_autofree char *dav_uri = NULL;
      g_autofree char *webdav_path = NULL;
      g_autofree char *webdav_uri = NULL;

      port = g_uri_get_port (uri);
      scheme = g_uri_get_scheme (uri);
      if (g_strcmp0 (scheme, "https") == 0)
        port = port != 443 ? port : -1;
      else if (g_strcmp0 (scheme, "http") == 0)
        port = port != 80 ? port : -1;

      base_path = g_strndup (path, server_root - path);
      dav_path = g_build_path ("/", base_path, "/remote.php/dav", NULL);
      webdav_path = g_build_path ("/", base_path, "/remote.php/webdav", NULL);

      /* TODO: the proper path is `remote.php/dav/files/<username>`
       *
       * See: https://github.com/nextcloud/server/issues/25867
       */
      webdav_uri = g_uri_join_with_user (G_URI_FLAGS_PARSE_RELAXED,
                                         g_uri_get_scheme (uri),
                                         g_uri_get_user (uri),
                                         g_uri_get_password (uri),
                                         g_uri_get_auth_params (uri),
                                         g_uri_get_host (uri),
                                         port,
                                         webdav_path,
                                         g_uri_get_query (uri),
                                         g_uri_get_fragment (uri));
      g_ptr_array_add (discover->services, goa_dav_config_new (GOA_SERVICE_TYPE_WEBDAV,
                                                               webdav_uri,
                                                               ((CheckData *)discover)->username));

      dav_uri = g_uri_join_with_user (G_URI_FLAGS_PARSE_RELAXED,
                                      g_uri_get_scheme (uri),
                                      g_uri_get_user (uri),
                                      g_uri_get_password (uri),
                                      g_uri_get_auth_params (uri),
                                      g_uri_get_host (uri),
                                      port,
                                      dav_path,
                                      g_uri_get_query (uri),
                                      g_uri_get_fragment (uri));
      g_ptr_array_add (discover->services, goa_dav_config_new (GOA_SERVICE_TYPE_CALDAV,
                                                               dav_uri,
                                                               ((CheckData *)discover)->username));
      g_ptr_array_add (discover->services, goa_dav_config_new (GOA_SERVICE_TYPE_CARDDAV,
                                                               dav_uri,
                                                               ((CheckData *)discover)->username));

      return TRUE;
    }

  return FALSE;
}

static void
dav_client_discover_options_cb (SoupSession  *session,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  CheckData *data = g_task_get_task_data (task);
  DiscoverData *discover = (DiscoverData *) data;
  GoaProviderFeatures features = GOA_PROVIDER_FEATURE_INVALID;
  SoupMessage *msg;
  unsigned int status;
  g_autoptr (GBytes) body = NULL;
  g_autoptr (GError) error = NULL;

  msg = soup_session_get_async_result_message (session, result);
  g_debug ("goa_dav_client_discover(): (%p, %u)", msg, soup_message_get_status (msg));

  body = soup_session_send_and_read_finish (session, result, &error);

  /* There may have been error, or we may have been aborted/cancelled
   * from another thread.
   */
  if (error != NULL && data->error == NULL)
    g_propagate_error (&data->error, g_steal_pointer (&error));

  if (data->error != NULL)
    goto out;

  status = soup_message_get_status (msg);
  switch (status)
    {
    case SOUP_STATUS_OK:
      break;

    /* Some responses are non-fatal for discovery */
    case SOUP_STATUS_NOT_FOUND:
    case SOUP_STATUS_METHOD_NOT_ALLOWED:
    case SOUP_STATUS_INTERNAL_SERVER_ERROR:
    case SOUP_STATUS_NOT_IMPLEMENTED:
      goto out;

    /* Defer authentication errors to support content restricted passwords */
    case SOUP_STATUS_UNAUTHORIZED:
    case SOUP_STATUS_FORBIDDEN:
    case SOUP_STATUS_PRECONDITION_FAILED:
      discover->auth_error = TRUE;
      goto out;

    default:
      goa_utils_set_error_soup (&data->error, msg);
      goto out;
    }

  /* Short path for ownCloud/Nextcloud
   */
  if (dav_client_discover_postconfig_nexcloud (discover, msg))
    {
      g_queue_clear_full (&discover->uris, g_free);
      goto out;
    }

  features = _soup_message_get_dav_features (msg, &error);
  if (error != NULL)
    goto out;

  /* TODO: implement PROPFIND behaviour emulating GVfs.
   */
  if (features == GOA_PROVIDER_FEATURE_FILES)
    {
      GUri *uri = NULL;

      /* GVfs won't follow redirects, so return the resolved URI */
      uri = soup_message_get_uri (msg);
      if (uri != NULL)
        {
          GoaDavConfig *config = NULL;
          g_autofree char *webdav_uri = NULL;

          webdav_uri = g_uri_to_string (uri);
          config = goa_dav_config_new (GOA_SERVICE_TYPE_WEBDAV, webdav_uri, data->username);
          g_ptr_array_add (discover->services, g_steal_pointer (&config));
        }
    }

  if ((features & GOA_PROVIDER_FEATURE_CALENDAR) != 0)
    {
      GoaDavConfig *config = NULL;

      config = goa_dav_config_new (GOA_SERVICE_TYPE_CALDAV, data->uri, data->username);
      g_ptr_array_add (discover->services, g_steal_pointer (&config));
    }

  if ((features & GOA_PROVIDER_FEATURE_CONTACTS) != 0)
    {
      GoaDavConfig *config = NULL;

      config = goa_dav_config_new (GOA_SERVICE_TYPE_CARDDAV, data->uri, data->username);
      g_ptr_array_add (discover->services, g_steal_pointer (&config));
    }

out:
  dav_client_discover_iterate (task);
}

/*< private >
 * dav_client_discover_iterate:
 * @task: a discover operation task
 *
 * This function drives the discovery process, recursively testing candidates
 * until exhausted or a fatal error occurs.
 */
static void
dav_client_discover_iterate (GTask *task)
{
  CheckData *data = g_task_get_task_data (task);
  DiscoverData *discover = (DiscoverData *) data;

  if (data->error != NULL)
    {
      /* Only certificate errors and cancellation are immediately fatal */
      if (g_error_matches (data->error, GOA_ERROR, GOA_ERROR_SSL)
          || g_error_matches (data->error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          g_task_return_error (task, g_steal_pointer (&data->error));
          return;
        }

      g_debug ("%s(): %s", G_STRFUNC, data->error->message);
      g_clear_error (&data->error);
    }

  if (!g_queue_is_empty (&discover->uris))
    {
      g_clear_pointer (&data->uri, g_free);
      g_clear_object (&data->msg);

      data->uri = g_queue_pop_head (&discover->uris);
      data->msg = soup_message_new (SOUP_METHOD_OPTIONS, data->uri);

      dav_client_authenticate_task (task);
      soup_session_send_and_read_async (data->session,
                                        data->msg,
                                        G_PRIORITY_DEFAULT,
                                        data->cancellable,
                                        (GAsyncReadyCallback)dav_client_discover_options_cb,
                                        g_object_ref (task));
    }
  else if (discover->services->len == 0)
    {
      if (discover->auth_error)
        {
          g_task_return_new_error (task,
                                   GOA_ERROR,
                                   GOA_ERROR_NOT_AUTHORIZED,
                                   _("Authentication failed"));
        }
      else
        {
          g_task_return_new_error (task,
                                   GOA_ERROR,
                                   GOA_ERROR_NOT_SUPPORTED,
                                   _("Cannot find WebDAV endpoint"));
        }
    }
  else
    {
      g_task_return_pointer (task,
                             g_steal_pointer (&discover->services),
                             g_object_unref);
    }
}

static gboolean
dav_client_discover_preconfig (DiscoverData *discover,
                               const char   *uri)
{
  g_autoptr (GUri) guri = NULL;
  const char *host = NULL;
  const char *base_domain = NULL;

  g_assert (discover != NULL);

  guri = g_uri_parse (uri, G_URI_FLAGS_NONE, NULL);
  if (guri == NULL)
    return FALSE;

  host = g_uri_get_host (guri);
  base_domain = soup_tld_get_base_domain (host, NULL);

  if (g_strcmp0 (host, "fastmail.com") == 0
      || g_strcmp0 (base_domain, "fastmail.com") == 0)
    {
      g_queue_push_tail (&discover->uris, g_strdup (FASTMAIL_WEBDAV));
      g_queue_push_tail (&discover->uris, g_strdup (FASTMAIL_CALDAV));
      g_queue_push_tail (&discover->uris, g_strdup (FASTMAIL_CARDDAV));

      return TRUE;
    }

  if (g_strcmp0 (host, "mailbox.org") == 0
      || g_strcmp0 (base_domain, "mailbox.org") == 0)
    {
      g_queue_push_tail (&discover->uris, g_strdup (MAILBOX_ORG_WEBDAV));
      g_queue_push_tail (&discover->uris, g_strdup (MAILBOX_ORG_CALDAV));
      g_queue_push_tail (&discover->uris, g_strdup (MAILBOX_ORG_CARDDAV));

      return TRUE;
    }

  return FALSE;
}

/**
 * goa_dav_client_discover:
 * @self: a `GoaDavClient`
 * @uri: a WebDAV URI
 * @username: DAV user
 * @password: DAV password
 * @accept_ssl_errors: whether to ignore SSL errors
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (nullable): a `GAsyncReadyCallback`
 * @user_data: user data
 *
 * Check if @uri is a valid WebDAV endpoint for @username and @password.
 *
 * If @accept_ssl_errors is %TRUE, SSL related errors will be ignored.
 *
 * Call [method@Goa.DavClient.discover_finish] to get the result.
 */
void
goa_dav_client_discover (GoaDavClient        *self,
                         const char          *uri,
                         const char          *username,
                         const char          *password,
                         gboolean             accept_ssl_errors,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (SoupLogger) logger = NULL;
  CheckData *data;
  DiscoverData *discover;

  g_return_if_fail (GOA_IS_DAV_CLIENT (self));
  g_return_if_fail (uri != NULL && uri[0] != '\0');
  g_return_if_fail (username != NULL && username[0] != '\0');
  g_return_if_fail (password != NULL && password[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_dav_client_discover);

  discover = g_new0 (DiscoverData, 1);
  data = (CheckData *) discover;
  g_task_set_task_data (task, discover, goa_dav_client_discover_data_free);

  data->session = soup_session_new ();
  soup_session_set_user_agent (data->session, "gnome-online-accounts/" PACKAGE_VERSION " ");

  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (data->session, SOUP_SESSION_FEATURE (logger));

  data->username = g_strdup (username);
  data->password = g_strdup (password);
  data->accept_ssl_errors = accept_ssl_errors;

  discover->services = g_ptr_array_new_with_free_func (g_object_unref);
  g_queue_init (&discover->uris);

  /* Check if the host can be preconfigured, falling back to well-known paths.
   */
  if (!dav_client_discover_preconfig (discover, uri))
    {
      g_queue_push_tail (&discover->uris, g_uri_resolve_relative (uri, WELL_KNOWN_NEXTCLOUD, 0, NULL));
      g_queue_push_tail (&discover->uris, g_uri_resolve_relative (uri, WELL_KNOWN_CALDAV, 0, NULL));
      g_queue_push_tail (&discover->uris, g_uri_resolve_relative (uri, WELL_KNOWN_CARDDAV, 0, NULL));
    }

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (dav_client_check_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  dav_client_discover_iterate (task);
}

/**
 * goa_dav_client_discover_finish:
 * @self: a `GoaDavClient`
 * @res: a `GAsyncResult`
 * @real_uri: (nullable): location for resolved URI
 * @error: (nullable): a `GError`
 *
 * Get the result of an operation started with goa_dav_client_resolve().
 *
 * Returns: (transfer container) (element-type Goa.ServiceConfig) (nullable): a list of services,
 *   or %NULL with @error set
 */
GPtrArray *
goa_dav_client_discover_finish (GoaDavClient  *self,
                                GAsyncResult  *res,
                                GError       **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_DAV_CLIENT (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  task = G_TASK (res);
  g_return_val_if_fail (g_task_get_source_tag (task) == goa_dav_client_discover, FALSE);

  return g_task_propagate_pointer (task, error);
}

