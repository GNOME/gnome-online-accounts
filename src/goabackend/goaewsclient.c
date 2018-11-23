/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2018 Red Hat, Inc.
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

/* Based on code by the Evolution team.
 *
 * This was originally written as a part of evolution-ews:
 * evolution-ews/src/server/e-ews-connection.c
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include <libsoup/soup.h>
#include <libxml/xmlIO.h>

#include "goaewsclient.h"
#include "goautils.h"

struct _GoaEwsClient
{
  GObject parent_instance;
};

G_DEFINE_TYPE (GoaEwsClient, goa_ews_client, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_ews_client_init (GoaEwsClient *self)
{
}

static void
goa_ews_client_class_init (GoaEwsClientClass *self)
{
}

/* ---------------------------------------------------------------------------------------------------- */

GoaEwsClient *
goa_ews_client_new (void)
{
  return GOA_EWS_CLIENT (g_object_new (GOA_TYPE_EWS_CLIENT, NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;
  GError *error;
  SoupMessage *msgs[2];
  SoupSession *session;
  gboolean accept_ssl_errors;
  guint pending;
  gulong cancellable_id;
  xmlOutputBuffer *buf;
} AutodiscoverData;

typedef struct
{
  gchar *password;
  gchar *username;
} AutodiscoverAuthData;

static void
ews_client_autodiscover_data_free (gpointer user_data)
{
  AutodiscoverData *data = user_data;

  if (data->cancellable_id > 0)
    {
      g_cancellable_disconnect (data->cancellable, data->cancellable_id);
      g_object_unref (data->cancellable);
    }

  g_clear_error (&data->error);

  /* soup_session_queue_message stole the references to data->msgs */
  xmlOutputBufferClose (data->buf);
  g_object_unref (data->session);
  g_slice_free (AutodiscoverData, data);
}

static void
ews_client_autodiscover_auth_data_free (gpointer data, GClosure *closure)
{
  AutodiscoverAuthData *auth = data;

  g_free (auth->password);
  g_free (auth->username);
  g_slice_free (AutodiscoverAuthData, auth);
}

static gboolean
ews_client_check_node (const xmlNode *node, const gchar *name)
{
  g_return_val_if_fail (node != NULL, FALSE);
  return node->type == XML_ELEMENT_NODE && !g_strcmp0 ((gchar *) node->name, name);
}

static void
ews_client_authenticate (SoupSession *session,
                         SoupMessage *msg,
                         SoupAuth *auth,
                         gboolean retrying,
                         gpointer user_data)
{
  AutodiscoverAuthData *data = user_data;

  if (retrying)
    return;

  soup_auth_authenticate (auth, data->username, data->password);
}

static void
ews_client_request_started (SoupSession *session, SoupMessage *msg, SoupSocket *socket, gpointer user_data)
{
  AutodiscoverData *data;
  GTask *task = G_TASK (user_data);
  GTlsCertificateFlags cert_flags;

  g_debug ("goa_ews_client_autodiscover(): request started (%p)", msg);

  data = (AutodiscoverData *) g_task_get_task_data (task);

  if (!data->accept_ssl_errors
      && soup_message_get_https_status (msg, NULL, &cert_flags)
      && cert_flags != 0
      && data->error == NULL)
    {
      goa_utils_set_error_ssl (&data->error, cert_flags);

      /* The callback will be invoked after we have returned to the
       * main loop.
       */
      soup_session_abort (data->session);
    }
}

static void
ews_client_autodiscover_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
  AutodiscoverData *data;
  GTask *task = G_TASK (user_data);

  g_debug ("goa_ews_client_autodiscover(): cancelled");

  data = (AutodiscoverData *) g_task_get_task_data (task);

  /* The callback will be invoked after we have returned to the main
   * loop.
   */
  soup_session_abort (data->session);
}

static gboolean
ews_client_autodiscover_parse_protocol (xmlNode *node)
{
  gboolean as_url = FALSE;
  gboolean oab_url = FALSE;

  for (node = node->children; node; node = node->next)
    {
      if (ews_client_check_node (node, "ASUrl"))
        as_url = TRUE;
      else if (ews_client_check_node (node, "OABUrl"))
        oab_url = TRUE;

      if (as_url && oab_url)
        break;
    }

  return as_url && oab_url;
}

static void
ews_client_autodiscover_response_cb (SoupSession *session, SoupMessage *msg, gpointer user_data)
{
  GError *error = NULL;
  AutodiscoverData *data;
  GTask *task = G_TASK (user_data);
  gboolean op_res = FALSE;
  guint idx;
  guint status;
  gsize size;
  xmlDoc *doc;
  xmlNode *node;

  g_debug ("goa_ews_client_autodiscover(): response (%p, %u)", msg, msg->status_code);

  data = (AutodiscoverData *) g_task_get_task_data (task);
  size = sizeof (data->msgs) / sizeof (data->msgs[0]);

  for (idx = 0; idx < size; idx++)
    {
      if (data->msgs[idx] == msg)
        break;
    }
  if (idx == size || data->pending == 0)
    return;

  data->msgs[idx] = NULL;
  status = msg->status_code;

  /* status == SOUP_STATUS_CANCELLED, if we are being aborted by the
   * GCancellable, an SSL error or another message that was
   * successful.
   */
  if (status == SOUP_STATUS_CANCELLED)
    {
      /* If we are being aborted by the GCancellable, then the
       * GTask is responsible for setting the GError automatically.
       *
       * If a previous autodiscover attempt for the same GAsyncResult
       * was successful then no additional attempts are required and
       * we should use the result from the earlier attempt.
       */
      goto out;
    }
  else if (status != SOUP_STATUS_OK)
    {
      g_warning ("goa_ews_client_autodiscover() failed: %u — %s", msg->status_code, msg->reason_phrase);
      g_return_if_fail (data->error == NULL);

      goa_utils_set_error_soup (&error, msg);
      goto out;
    }

  soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->response_body));
  g_debug ("The response headers");
  g_debug ("===================");
  g_debug ("%s", SOUP_MESSAGE (msg)->response_body->data);

  doc = xmlReadMemory (msg->response_body->data, msg->response_body->length, "autodiscover.xml", NULL, 0);
  if (doc == NULL)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Failed to parse autodiscover response XML"));
      goto out;
    }

  node = xmlDocGetRootElement (doc);
  if (g_strcmp0 ((gchar *) node->name, "Autodiscover"))
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   /* Translators: the parameter is an XML element name. */
                   _("Failed to find “%s” element"), "Autodiscover");
      goto out;
    }

  for (node = node->children; node; node = node->next)
    {
      if (ews_client_check_node (node, "Response"))
        break;
    }
  if (node == NULL)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   /* Translators: the parameter is an XML element name. */
                   _("Failed to find “%s” element"), "Response");
      goto out;
    }

  for (node = node->children; node; node = node->next)
    {
      if (ews_client_check_node (node, "Account"))
        break;
    }
  if (node == NULL)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   /* Translators: the parameter is an XML element name. */
                   _("Failed to find “%s” element"), "Account");
      goto out;
    }

  for (node = node->children; node; node = node->next)
    {
      if (ews_client_check_node (node, "Protocol"))
        {
          op_res = ews_client_autodiscover_parse_protocol (node);
          /* Since the server may send back multiple <Protocol> nodes
           * don't break unless we found the one we want.
           */
          if (op_res)
            break;
        }
    }
  if (!op_res)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific*/
                   _("Failed to find ASUrl and OABUrl in autodiscover response"));
      goto out;
    }

  /* This autodiscover attempt was successful. Save the result now so
   * that it won't get lost when we hear from another autodiscover
   * attempt for the same GAsyncResult.
   */

  for (idx = 0; idx < size; idx++)
    {
      if (data->msgs[idx] != NULL)
        {
          /* The callback (ie. this function) will be invoked after we
           * have returned to the main loop.
           */
          soup_session_cancel_message (data->session, data->msgs[idx], SOUP_STATUS_CANCELLED);
        }
    }

 out:
  /* op_res == FALSE, if we are being aborted by the GCancellable, an
   * SSL error, another message that was successful or an error
   * encountered while parsing this response.
   */
  if (!op_res)
    {
      /* There's another request outstanding.
       * Hope that it has better luck.
       */
      if (data->pending > 1)
        g_clear_error (&error);

      if (error != NULL && data->error == NULL)
        {
          g_propagate_error (&data->error, error);
          error = NULL;
        }
    }

  data->pending--;
  if (data->pending == 0)
    {
      if (data->error != NULL)
        g_task_return_error (task, g_steal_pointer (&data->error));
      else
        g_task_return_boolean (task, TRUE);
    }

  g_clear_error (&error);
  g_object_unref (task);
}

static xmlDoc *
ews_client_create_autodiscover_xml (const gchar *email)
{
  xmlDoc *doc;
  xmlNode *node;
  xmlNs *ns;

  doc = xmlNewDoc ((xmlChar *) "1.0");

  node = xmlNewDocNode (doc, NULL, (xmlChar *) "Autodiscover", NULL);
  xmlDocSetRootElement (doc, node);
  ns = xmlNewNs (node,
                 (xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/outlook/requestschema/2006",
                 NULL);

  node = xmlNewChild (node, ns, (xmlChar *) "Request", NULL);
  xmlNewChild (node, ns, (xmlChar *) "EMailAddress", (xmlChar *) email);
  xmlNewChild (node,
               ns,
               (xmlChar *) "AcceptableResponseSchema",
               (xmlChar *) "http://schemas.microsoft.com/exchange/autodiscover/outlook/responseschema/2006a");

  return doc;
}

static void
ews_client_post_restarted_cb (SoupMessage *msg, gpointer data)
{
  xmlOutputBuffer *buf = data;

  /* In violation of RFC2616, libsoup will change a POST request to
   * a GET on receiving a 302 redirect.
   */
  g_debug ("Working around libsoup bug with redirect");
  g_object_set (msg, SOUP_MESSAGE_METHOD, "POST", NULL);

  soup_message_set_request(msg,
                           "text/xml; charset=utf-8",
                           SOUP_MEMORY_COPY,
#ifdef LIBXML2_NEW_BUFFER
                           (gchar *) xmlOutputBufferGetContent(buf),
                           xmlOutputBufferGetSize(buf));
#else
                           (gchar *) buf->buffer->content,
                           buf->buffer->use);
#endif
}

static SoupMessage *
ews_client_create_msg_for_url (const gchar *url, xmlOutputBuffer *buf)
{
  SoupMessage *msg;

  msg = soup_message_new (buf != NULL ? "POST" : "GET", url);
  soup_message_headers_append (msg->request_headers, "User-Agent", "libews/0.1");

  if (buf != NULL)
    {
      soup_message_set_request (msg,
                                "text/xml; charset=utf-8",
                                SOUP_MEMORY_COPY,
#ifdef LIBXML2_NEW_BUFFER
                                (gchar *) xmlOutputBufferGetContent(buf),
                                xmlOutputBufferGetSize(buf));
#else
                                (gchar *) buf->buffer->content,
                                buf->buffer->use);
#endif
      g_signal_connect (msg, "restarted", G_CALLBACK (ews_client_post_restarted_cb), buf);
    }

  soup_buffer_free (soup_message_body_flatten (SOUP_MESSAGE (msg)->request_body));
  g_debug ("The request headers");
  g_debug ("===================");
  g_debug ("%s", SOUP_MESSAGE (msg)->request_body->data);

  return msg;
}

void
goa_ews_client_autodiscover (GoaEwsClient        *self,
                             const gchar         *email,
                             const gchar         *password,
                             const gchar         *username,
                             const gchar         *server,
                             gboolean             accept_ssl_errors,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  AutodiscoverData *data;
  AutodiscoverAuthData *auth;
  GTask *task = NULL;
  gchar *url1;
  gchar *url2;
  xmlDoc *doc;
  xmlOutputBuffer *buf;

  g_return_if_fail (GOA_IS_EWS_CLIENT (self));
  g_return_if_fail (email != NULL && email[0] != '\0');
  g_return_if_fail (password != NULL && password[0] != '\0');
  g_return_if_fail (username != NULL && username[0] != '\0');
  g_return_if_fail (server != NULL && server[0] != '\0');
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_ews_client_autodiscover);

  data = g_slice_new0 (AutodiscoverData);
  g_task_set_task_data (task, data, ews_client_autodiscover_data_free);

  doc = ews_client_create_autodiscover_xml (email);
  buf = xmlAllocOutputBuffer (NULL);
  xmlNodeDumpOutput (buf, doc, xmlDocGetRootElement (doc), 0, 1, NULL);
  xmlOutputBufferFlush (buf);

  url1 = g_strdup_printf ("https://%s/autodiscover/autodiscover.xml", server);
  url2 = g_strdup_printf ("https://autodiscover.%s/autodiscover/autodiscover.xml", server);

  /* http://msdn.microsoft.com/en-us/library/ee332364.aspx says we are
   * supposed to try $domain and then autodiscover.$domain. But some
   * people have broken firewalls on the former which drop packets
   * instead of rejecting connections, and make the request take ages
   * to time out. So run both queries in parallel and let the fastest
   * (successful) one win.
   */

  data->buf = buf;
  data->msgs[0] = ews_client_create_msg_for_url (url1, buf);
  data->msgs[1] = ews_client_create_msg_for_url (url2, buf);
  data->pending = sizeof (data->msgs) / sizeof (data->msgs[0]);
  data->session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, FALSE,
                                                 NULL);
  soup_session_add_feature_by_type (data->session, SOUP_TYPE_AUTH_NTLM);
  data->accept_ssl_errors = accept_ssl_errors;

  if (cancellable != NULL)
    {
      data->cancellable = g_object_ref (cancellable);
      data->cancellable_id = g_cancellable_connect (cancellable,
                                                    G_CALLBACK (ews_client_autodiscover_cancelled_cb),
                                                    task,
                                                    NULL);
    }

  auth = g_slice_new0 (AutodiscoverAuthData);
  auth->username = g_strdup (username);
  auth->password = g_strdup (password);
  g_signal_connect_data (data->session,
                         "authenticate",
                         G_CALLBACK (ews_client_authenticate),
                         auth,
                         ews_client_autodiscover_auth_data_free,
                         0);

  g_signal_connect (data->session, "request-started", G_CALLBACK (ews_client_request_started), task);

  soup_session_queue_message (data->session,
                              data->msgs[0],
                              ews_client_autodiscover_response_cb,
                              g_object_ref (task));
  soup_session_queue_message (data->session,
                              data->msgs[1],
                              ews_client_autodiscover_response_cb,
                              g_object_ref (task));

  g_free (url2);
  g_free (url1);
  g_object_unref (task);
  xmlFreeDoc (doc);
}

gboolean
goa_ews_client_autodiscover_finish (GoaEwsClient *self, GAsyncResult *res, GError **error)
{
  GTask *task;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_ews_client_autodiscover, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GError **error;
  GMainLoop *loop;
  gboolean op_res;
} AutodiscoverSyncData;

static void
ews_client_autodiscover_sync_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  AutodiscoverSyncData *data = user_data;

  data->op_res = goa_ews_client_autodiscover_finish (GOA_EWS_CLIENT (source_object), res, data->error);
  g_main_loop_quit (data->loop);
}

gboolean
goa_ews_client_autodiscover_sync (GoaEwsClient        *self,
                                  const gchar         *email,
                                  const gchar         *password,
                                  const gchar         *username,
                                  const gchar         *server,
                                  gboolean             accept_ssl_errors,
                                  GCancellable        *cancellable,
                                  GError             **error)
{
  AutodiscoverSyncData data;
  GMainContext *context = NULL;

  data.error = error;

  context = g_main_context_new ();
  g_main_context_push_thread_default (context);
  data.loop = g_main_loop_new (context, FALSE);

  goa_ews_client_autodiscover (self,
                               email,
                               password,
                               username,
                               server,
                               accept_ssl_errors,
                               cancellable,
                               ews_client_autodiscover_sync_cb,
                               &data);
  g_main_loop_run (data.loop);
  g_main_loop_unref (data.loop);

  g_main_context_pop_thread_default (context);
  g_main_context_unref (context);

  return data.op_res;
}
