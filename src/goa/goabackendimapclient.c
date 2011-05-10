/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>
#include <stdlib.h>

#include "goabackendimapauth.h"
#include "goabackendimapclient.h"
#include "goabackendimapmessage.h"
#include "goabackendimapprivate.h"

typedef enum
{
  CAPABILITY_FLAGS_NONE        = 0,
  CAPABILITY_FLAGS_IMAP4REV1   = (1<<0),
  CAPABILITY_FLAGS_IDLE        = (1<<1),
  CAPABILITY_FLAGS_X_GM_EXT_1  = (1<<2),
} CapabilityFlags;

static CapabilityFlags
capability_flags_from_strv (const gchar *const *strings)
{
  CapabilityFlags ret;
  guint n;

  g_return_val_if_fail (strings != NULL, CAPABILITY_FLAGS_NONE);

  ret = CAPABILITY_FLAGS_NONE;
  for (n = 0; strings[n] != NULL; n++)
    {
      if (g_strcmp0 (strings[n], "IMAP4rev1") == 0)
        ret |= CAPABILITY_FLAGS_IMAP4REV1;
      else if (g_strcmp0 (strings[n], "IDLE") == 0)
        ret |= CAPABILITY_FLAGS_IDLE;
      else if (g_strcmp0 (strings[n], "X-GM-EXT-1") == 0)
        ret |= CAPABILITY_FLAGS_X_GM_EXT_1;
    }
  return ret;
}

/**
 * GoaBackendImapClient:
 *
 * The #GoaBackendImapClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendImapClient
{
  /*< private >*/
  GObject parent_instance;

  /* set at init / object construction */
  GMainContext *context;
  gboolean closed;
  GThread *worker_thread;
  GCancellable *worker_cancellable;

  /* set at object construction */
  GoaBackendImapAuth *auth;
  gchar *host_and_port;
  guint use_tls;
  gchar *criteria;
  guint query_offset;
  guint query_size;

  /* The remaining data members are already related to the running
   * session
   */
  gboolean is_running;
  CapabilityFlags caps;
  GSocketClient *sc;
  GSocketConnection *c;
  GDataInputStream *dis;
  GDataOutputStream *dos;
  gint num_search_result;
  gint *search_result;

  /* used for generating command tags */
  guint tag;

  /* the number of messages in the selected mailbox */
  gint exists;

  /* the number of unseen messages in the selected mailbox */
  gint unseen;

  /* the 32-bit uidvalidity */
  gint uidvalidity;

  /* The number of times we've fetched data */
  gint num_completed_fetches;
  GCond *num_completed_fetches_cond;
  GMutex *num_completed_fetches_mutex;

  /* A list of results of the query */
  GList *messages;
  /* A list currently being built */
  GList *messages_buildup;
};

typedef struct _GoaBackendImapClientClass GoaBackendImapClientClass;

struct _GoaBackendImapClientClass
{
  GObjectClass parent_class;
  void (*updated) (GoaBackendImapClient *client);
  void (*closed) (GoaBackendImapClient  *client,
                  const GError         **error);
};

/**
 * SECTION:goabackendimapclient
 * @title: GoaBackendImapClient
 * @short_description: A simple IMAP client
 *
 * #GoaBackendImapClient is a type used for obtaining information via the
 * <ulink url="http://tools.ietf.org/html/rfc3501">IMAP</ulink>
 * protocol.
 */

enum
{
  UPDATED_SIGNAL,
  CLOSED_SIGNAL,
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_HOST_AND_PORT,
  PROP_USE_TLS,
  PROP_CRITERIA,
  PROP_QUERY_OFFSET,
  PROP_QUERY_SIZE,
  PROP_AUTH,
  PROP_CLOSED
};

static guint signals[LAST_SIGNAL] = {0};

static void goa_backend_imap_client__g_initable_iface_init (GInitableIface *iface);

static void goa_backend_imap_client__g_async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GoaBackendImapClient, goa_backend_imap_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, goa_backend_imap_client__g_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, goa_backend_imap_client__g_async_initable_iface_init));

G_LOCK_DEFINE_STATIC (messages_lock);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_client_finalize (GObject *object)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (object);

  g_free (client->search_result);
  if (client->sc != NULL)
    g_object_unref (client->sc);
  if (client->c != NULL)
    g_object_unref (client->c);
  if (client->dis != NULL)
    g_object_unref (client->dis);
  if (client->dos != NULL)
    g_object_unref (client->dos);

  g_free (client->host_and_port);
  g_free (client->criteria);
  g_object_unref (client->auth);

  g_mutex_free (client->num_completed_fetches_mutex);
  g_cond_free (client->num_completed_fetches_cond);

  g_object_unref (client->worker_cancellable);
  if (client->context != NULL)
    g_main_context_unref (client->context);

  G_OBJECT_CLASS (goa_backend_imap_client_parent_class)->finalize (object);
}

static void
goa_backend_imap_client_get_property (GObject      *object,
                                      guint         prop_id,
                                      GValue       *value,
                                      GParamSpec   *pspec)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      g_value_set_string (value, client->host_and_port);
      break;

    case PROP_USE_TLS:
      g_value_set_boolean (value, client->use_tls);
      break;

    case PROP_CRITERIA:
      g_value_set_string (value, client->criteria);
      break;

    case PROP_QUERY_OFFSET:
      g_value_set_uint (value, client->query_offset);
      break;

    case PROP_QUERY_SIZE:
      g_value_set_uint (value, client->query_size);
      break;

    case PROP_AUTH:
      g_value_set_object (value, client->auth);
      break;

    case PROP_CLOSED:
      g_value_set_boolean (value, client->closed);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_client_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      client->host_and_port = g_value_dup_string (value);
      break;

    case PROP_USE_TLS:
      client->use_tls = g_value_get_boolean (value);
      break;

    case PROP_CRITERIA:
      client->criteria = g_value_dup_string (value);
      break;

    case PROP_QUERY_OFFSET:
      client->query_offset = g_value_get_uint (value);
      break;

    case PROP_QUERY_SIZE:
      client->query_size = g_value_get_uint (value);
      break;

    case PROP_AUTH:
      client->auth = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_client_init (GoaBackendImapClient *client)
{
  client->num_completed_fetches_cond = g_cond_new ();
  client->num_completed_fetches_mutex = g_mutex_new ();

  client->context = g_main_context_get_thread_default ();
  if (client->context != NULL)
    g_main_context_ref (client->context);
  client->worker_cancellable = g_cancellable_new ();
}

static void
goa_backend_imap_client_class_init (GoaBackendImapClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_backend_imap_client_finalize;
  gobject_class->set_property = goa_backend_imap_client_set_property;
  gobject_class->get_property = goa_backend_imap_client_get_property;

  /**
   * GoaBackendImapClient:host-and-port:
   *
   * The host to connect to.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_HOST_AND_PORT,
                                   g_param_spec_string ("host-and-port",
                                                        "host-and-port",
                                                        "host-and-port",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:use-tls:
   *
   * Whether TLS should be used when establishing the connection.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USE_TLS,
                                   g_param_spec_boolean ("use-tls",
                                                         "use-tls",
                                                         "use-tls",
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:criteria:
   *
   * The criteria used for selecting messages. See
   * goa_backend_imap_client_new_sync() for an explanation of what
   * strings are valid.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CRITERIA,
                                   g_param_spec_string ("criteria",
                                                        "criteria",
                                                        "criteria",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:query-offset:
   *
   * Offset into array of matching messages used when returning messages.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_QUERY_OFFSET,
                                   g_param_spec_uint ("query-offset",
                                                      "query-offset",
                                                      "query-offset",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:query-size:
   *
   * Maximum number of messages to return.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_QUERY_SIZE,
                                   g_param_spec_uint ("query-size",
                                                      "query-size",
                                                      "query-size",
                                                      1, G_MAXUINT, 10,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:auth:
   *
   * The #GoaBackendImapAuth object used for authentication the connection.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_AUTH,
                                   g_param_spec_object ("auth",
                                                        "auth",
                                                        "auth",
                                                        GOA_TYPE_BACKEND_IMAP_AUTH,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient:closed:
   *
   * Whether the connection to the IMAP server has been closed.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLOSED,
                                   g_param_spec_boolean ("closed",
                                                         "closed",
                                                         "closed",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapClient::updated:
   * @client: The #GoaBackendImapClient emitting the signal.
   *
   * Signal emitted every time messages has been fetched from the
   * server. This does not neccesarily mean that the messages returned
   * by goa_backend_imap_client_get_messages() has changed.
   */
  signals[UPDATED_SIGNAL] =
    g_signal_new ("updated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaBackendImapClientClass, updated),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);

  /**
   * GoaBackendImapClient::closed:
   * @client: The #GoaBackendImapClient emitting the signal.
   * @error: A #GError or %NULL.
   *
   * Emitted when the connection has been closed. If the connection
   * was closed because of an error condition, @error will be
   * non-%NULL.
   *
   * After this signal has been emitted, the object is no longer useful.
   */
  signals[CLOSED_SIGNAL] =
    g_signal_new ("closed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaBackendImapClientClass, closed),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__BOXED,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_ERROR);
}

static gboolean
emit_updated_in_idle_cb (gpointer user_data)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (user_data);
  g_signal_emit (client, signals[UPDATED_SIGNAL], 0);
  return FALSE;
}

static void
emit_updated_in_idle (GoaBackendImapClient *client)
{
  GSource *source;

  g_return_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client));

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, emit_updated_in_idle_cb, g_object_ref (client), g_object_unref);
  g_source_attach (source, client->context);
  g_source_unref (source);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaBackendImapClient *client;
  GError *error;
} EmitClosedData;

static void
emit_closed_data_free (EmitClosedData *data)
{
  g_object_unref (data->client);
  if (data->error != NULL)
    g_error_free (data->error);
  g_free (data);
}

static gboolean
emit_closed_in_idle_cb (gpointer user_data)
{
  EmitClosedData *data = user_data;
  g_signal_emit (data->client, signals[CLOSED_SIGNAL], 0, data->error);
  g_object_notify (G_OBJECT (data->client), "closed");
  return FALSE;
}

static void
emit_closed_in_idle (GoaBackendImapClient  *client,
                     const GError          *error)
{
  GSource *source;
  EmitClosedData *data;

  g_return_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client));

  data = g_new0 (EmitClosedData, 1);
  data->client = g_object_ref (client);
  data->error = error != NULL ? g_error_copy (error) : NULL;

  source = g_idle_source_new ();
  g_source_set_priority (source, G_PRIORITY_DEFAULT);
  g_source_set_callback (source, emit_closed_in_idle_cb, data, (GDestroyNotify) emit_closed_data_free);
  g_source_attach (source, client->context);
  g_source_unref (source);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_client_new:
 * @host_and_port: The name and optionally port to connect to.
 * @use_tls: Whether TLS should be used.
 * @auth: Object used for authenticating the connection.
 * @criteria: Criteria string used to filter returned messages.
 * @query_offset: Offset of result window in the array of matching messages.
 * @query_size: Size of result window in the array of matching messages.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: Function to call when the request has been satisfied.
 * @user_data: Data to pass to @callback.
 *
 * Async version of goa_backend_imap_client_new_sync().
 *
 * When the result is ready, @callback will be called in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link>. You can then use goa_backend_imap_client_new_finish()
 * to get the result.
 */
void
goa_backend_imap_client_new (const gchar           *host_and_port,
                             gboolean               use_tls,
                             GoaBackendImapAuth    *auth,
                             const gchar           *criteria,
                             guint                  query_offset,
                             guint                  query_size,
                             GCancellable          *cancellable,
                             GAsyncReadyCallback    callback,
                             gpointer               user_data)
{
  g_return_if_fail (GOA_IS_BACKEND_IMAP_AUTH (auth));
  g_return_if_fail (host_and_port != NULL);
  g_return_if_fail (criteria != NULL);
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  g_async_initable_new_async (GOA_TYPE_BACKEND_IMAP_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              "host-and-port", host_and_port,
                              "use-tls", use_tls,
                              "auth", auth,
                              "criteria", criteria,
                              "query-offset", query_offset,
                              "query-size", query_size,
                              NULL);
}

/**
 * goa_backend_imap_client_new_finish:
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_imap_client_new().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_imap_client_new().
 *
 * Returns: A #GoaBackendImapClient or %NULL if @error is set.
 */
GoaBackendImapClient *
goa_backend_imap_client_new_finish (GAsyncResult  *res,
                                    GError       **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return GOA_BACKEND_IMAP_CLIENT (ret);
  else
    return NULL;
}

/**
 * goa_backend_imap_client_new_sync:
 * @host_and_port: The name and optionally port to connect to.
 * @use_tls: Whether TLS should be used.
 * @auth: Object used for authenticating the connection.
 * @criteria: Criteria string used to filter returned messages.
 * @query_offset: Offset of result window in the array of matching messages.
 * @query_size: Size of result window in the array of matching messages.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Creates a new IMAP client connecting to the mailbox INBOX at the
 * server represented by @host_and_port using @auth to authenticate
 * the IMAP connection. If authentication fails or messages can't be
 * downloaded, then this constructor returns %NULL and @error will be
 * set.
 *
 * The returned object is used to obtain #GoaBackendImapMessage
 * instances representing objects on the IMAP server. The messages are
 * selected by requesting the IMAP server to match all messages in the
 * mailbox against the @criteria string. Of the matching messages,
 * @query_size messages starting at offset @query_offset will be
 * downloaded. Note that the entire message is not downloaded - only
 * the data represented by #GoaBackendImapMessage (e.g. typically only
 * hundreds of bytes per message). Use
 * goa_backend_imap_client_get_messages() to obtain the matching
 * messages.
 *
 * The connection to the IMAP server is live insofar the <ulink
 * url="http://en.wikipedia.org/wiki/IMAP_IDLE">IMAP IDLE</ulink> (or
 * polling, if IDLE is not available) command is used to get notified
 * of new messages - the #GoaBackendImapClient::updated signal is
 * emitted every time new messages are fetched. Use
 * goa_backend_imap_client_refresh() to force a refresh.
 *
 * Note that the connection can be broken at any time - the
 * #GoaBackendImapClient::closed signal is emitted this happens. The
 * object is no longer useful when this happens. Use
 * goa_backend_imap_client_close() to close the connection manually.
 *
 * Also note that this constructor blocks the calling thread - see
 * goa_backend_imap_client_new() for the async non-blocking version.
 *
 * All signals are emitted in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this constructor from.
 *
 * Valid strings for @criteria includes the empty string (to match all
 * messages) and <literal>unread</literal> (to match all unread
 * messages). More options may be added in the future.
 *
 * Returns: A #GoaBackendImapClient or %NULL if @error is set.
 */
GoaBackendImapClient *
goa_backend_imap_client_new_sync (const gchar           *host_and_port,
                                  gboolean               use_tls,
                                  GoaBackendImapAuth    *auth,
                                  const gchar           *criteria,
                                  guint                  query_offset,
                                  guint                  query_size,
                                  GCancellable          *cancellable,
                                  GError               **error)
{
  GInitable *ret;
  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_AUTH (auth), NULL);
  g_return_val_if_fail (host_and_port != NULL, NULL);
  g_return_val_if_fail (criteria != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  ret = g_initable_new (GOA_TYPE_BACKEND_IMAP_CLIENT,
                        cancellable,
                        error,
                        "host-and-port", host_and_port,
                        "use-tls", use_tls,
                        "auth", auth,
                        "criteria", criteria,
                        "query-offset", query_offset,
                        "query-size", query_size,
                        NULL);
  if (ret != NULL)
    return GOA_BACKEND_IMAP_CLIENT (ret);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
parse_int (const gchar *s,
           gint        *out_result)
{
  gboolean ret;
  gchar *endp;
  gint result;

  g_return_val_if_fail (s != NULL, FALSE);

  ret = FALSE;
  result = strtol (s, &endp, 0);
  if (result == 0 && endp == s)
    goto out;

  if (out_result != NULL)
    *out_result = result;

  ret = TRUE;

 out:
  return ret;
}

static gboolean
fetch_check (const gchar *data,
             guint       *pos,
             const gchar *key)
{
  gsize key_len;
  gboolean ret;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  ret = FALSE;

  key_len = strlen (key);
  if (strncmp (data + *pos, key, key_len) == 0 && data[*pos + key_len] == ' ')
    {
      ret = TRUE;
      *pos += key_len + 1;
      goto out;
    }
 out:
  return ret;
}

static gchar **
fetch_parenthesized_list (const gchar *data,
                          guint       *pos)
{
  gchar **ret;
  gchar *s;
  guint start_pos;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  if (data[*pos] != '(')
    goto out;
  *pos += 1;
  start_pos = *pos;

  while (data[*pos] != ')' && data[*pos] != '\0')
    *pos += 1;
  if (data[*pos] != ')')
    goto out;

  s = g_strndup (data + start_pos, *pos - start_pos);
  ret = g_strsplit (s, " ", -1);
  g_free (s);

  *pos += 1;

 out:
  return ret;
}

static gchar *
fetch_string (const gchar *data,
              guint       *pos)
{
  gchar *ret;
  guint start_pos;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  start_pos = *pos;

  while (data[*pos] != ' ' && data[*pos] != ')' && data[*pos] != '\0')
    *pos += 1;

  ret = g_strndup (data + start_pos, *pos - start_pos);

  return ret;
}

static gchar *
fetch_quoted_string (const gchar *data,
                     guint       *pos)
{
  gchar *ret;
  guint start_pos;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  if (data[*pos] != '"')
    goto out;
  *pos += 1;

  start_pos = *pos;

  while (data[*pos] != '"' && data[*pos] != '\0')
    *pos += 1;

  ret = g_strndup (data + start_pos, *pos - start_pos);

  *pos += 1;

 out:
  return ret;
}

static guint
lookup_month (const gchar *str)
{
  static const gchar *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  guint n;
  for (n = 0; months[n] != NULL; n++)
    if (g_strcmp0 (str, months[n]) == 0)
      return n + 1;
  return 0;
}

static gboolean
fetch_date_time (const gchar *data,
                 guint       *pos,
                 gint64      *out_value)
{
  gchar *str_value;
  GTimeZone *tz;
  GDateTime *dt;
  gchar month[4];
  gint mon;
  gint day, year, hour, min, sec, offset;
  gboolean ret;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);
  g_return_val_if_fail (out_value != NULL, FALSE);

  str_value = NULL;
  tz = NULL;
  dt = NULL;
  ret = FALSE;

  str_value = fetch_quoted_string (data, pos);
  if (str_value == NULL)
    goto out;

  if (sscanf (str_value,
              "%02d-%03s-%04d %02d:%02d:%02d %04d",
              &day, month, &year, &hour, &min, &sec, &offset) != 7)
    goto out;

  tz = g_time_zone_new (str_value + strlen (str_value) - 5);
  if (tz == NULL)
    goto out;
  mon = lookup_month (month);
  if (mon == 0)
    goto out;

  dt = g_date_time_new (tz, year, mon, day, hour, min, (gdouble) sec);
  if (out_value != NULL)
    *out_value = g_date_time_to_unix (dt);

  ret = TRUE;

 out:
  if (dt != NULL)
    g_date_time_unref (dt);
  if (tz != NULL)
    g_time_zone_unref (tz);
  g_free (str_value);
  return ret;
}

static gboolean
fetch_int (const gchar *data,
           guint       *pos,
           gint        *out_value)
{
  gchar *str_value;
  gboolean ret;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);
  g_return_val_if_fail (out_value != NULL, FALSE);

  str_value = NULL;
  ret = FALSE;

  str_value = fetch_string (data, pos);
  if (str_value == NULL)
    goto out;

  if (!parse_int (str_value, out_value))
    goto out;

  ret = TRUE;

 out:
  g_free (str_value);
  return ret;
}

static gchar *
fetch_literal_string (const gchar *data,
                      guint       *pos,
                      guint       *out_len)
{
  gchar *ret;
  guint start_pos;
  guint len;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  ret = NULL;

  start_pos = *pos;

  if (data[*pos] != '{')
    goto out;
  *pos += 1;
  while (g_ascii_isdigit (data[*pos]))
    *pos += 1;
  if (strncmp (data + *pos, "}\r\n", 3) != 0)
    goto out;
  *pos += 3;

  if (!parse_int (data + start_pos + 1, (gint*) &len))
    goto out;

  ret = g_strndup (data + *pos, len);
  *pos += len;

  if (out_len != NULL)
    *out_len = len;

 out:
  return ret;
}

/* Simple FETCH response parser only handling a subset of FETCH
 * responses, see
 *
 *   http://tools.ietf.org/html/rfc3501#section-7.4.2
 *
 * for more details.
 */
static void
handle_fetch_response (GoaBackendImapClient  *client,
                       guint                  message_seqnum,
                       const gchar           *data)
{
  guint n;
  gchar **flags_strv;
  gboolean parsed;
  gboolean has_uid;
  gint uid;
  gint64 internal_date;
  gchar *thread_id;
  gchar *rfc822_headers;
  guint rfc822_headers_len;
  gchar *excerpt;
  guint excerpt_len;
  GoaBackendImapMessageFlags flags;
  GoaBackendImapMessage *message;

  g_return_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client));
  g_return_if_fail (message_seqnum >= 1);
  g_return_if_fail (data != NULL);

  uid = 0;
  has_uid = FALSE;
  thread_id = NULL;
  flags_strv = NULL;
  excerpt = NULL;
  rfc822_headers = NULL;
  parsed = FALSE;

  if (data[0] != '(')
    goto out;
  n = 1;
  while (data[n] != ')' && data[n] != '\0')
    {
      if (fetch_check (data, &n, "X-GM-THRID"))
        {
          thread_id = fetch_string (data, &n);
          if (thread_id == NULL)
            goto out;

        }
      else if (fetch_check (data, &n, "UID"))
        {
          if (!fetch_int (data, &n, &uid))
            goto out;
          has_uid = TRUE;
        }
      else if (fetch_check (data, &n, "INTERNALDATE"))
        {
          if (!fetch_date_time (data, &n, &internal_date))
            goto out;
        }
      else if (fetch_check (data, &n, "FLAGS"))
        {
          flags_strv = fetch_parenthesized_list (data, &n);
          if (flags_strv == NULL)
            goto out;
        }
      else if (fetch_check (data, &n, "BODY[HEADER.FIELDS (Date From To Cc Subject)]"))
        {
          rfc822_headers = fetch_literal_string (data, &n, &rfc822_headers_len);
          if (rfc822_headers == NULL)
            goto out;
        }
      else if (fetch_check (data, &n, "BODY[TEXT]<0>"))
        {
          excerpt = fetch_literal_string (data, &n, &excerpt_len);
          if (excerpt == NULL)
            goto out;
        }
      else
        {
          /* Don't know how to handle unknown data so fail completely */
          goto out;
        }
      /* advance to next value in FETCH response list, if any */
      while (data[n] == ' ')
        n++;
    }

  /* thread_id is optional (it's a GMail extension) */
  if (!has_uid || flags_strv == NULL || rfc822_headers == NULL || excerpt == NULL)
    goto out;

  flags = goa_backend_imap_message_flags_from_strv ((const gchar* const *) flags_strv);

  /* OK, message is valid */
  parsed = TRUE;

  message = goa_backend_imap_message_new ();
  message->seqnum = message_seqnum;
  message->uid = ((guint64) client->uidvalidity << 32) | ((guint64) uid);
  message->flags = flags;
  message->internal_date = internal_date;
  /* steal the rfc822_headers string */
  message->rfc822_headers = rfc822_headers;
  rfc822_headers = NULL;
  /* steal the excerpt string */
  message->excerpt = excerpt;
  excerpt = NULL;

  client->messages_buildup = g_list_prepend (client->messages_buildup, message);

 out:
  if (!parsed)
    {
      g_debug ("Was unable to parse FETCH response for msg %d with data `%s'", message_seqnum, data);
    }
  g_free (rfc822_headers);
  g_free (excerpt);
  g_free (thread_id);
  g_strfreev (flags_strv);
}


static void
handle_expunge_response (GoaBackendImapClient  *client,
                         guint                  message_seqnum)
{
  /* We are currently utterly uninterested in this since we
   * currently re-request everything in the query window
   */
}

static void
handle_status_response (GoaBackendImapClient  *client,
                        const gchar           *mailbox,
                        const gchar           *data)
{
  gchar **items;
  guint n;

  items = NULL;

  n = 0;
  items = fetch_parenthesized_list (data, &n);
  if (items == NULL)
    goto out;

  for (n = 0; items[n] != NULL && items[n+1] != NULL; n += 2)
    {
      gint value;
      if (g_strcmp0 (items[n], "MESSAGES") == 0 && parse_int (items[n+1], &value))
        {
          client->exists = value;
        }
      else if (g_strcmp0 (items[n], "UNSEEN") == 0 && parse_int (items[n+1], &value))
        {
          client->unseen = value;
        }
      else if (g_strcmp0 (items[n], "UIDVALIDITY") == 0 && parse_int (items[n+1], &value))
        {
          client->uidvalidity = value;
        }
    }

 out:
  g_strfreev (items);
}

static gint
sort_by_seqnum_reverse (gint *a, gint *b)
{
  return *b - *a;
}

static void
handle_search_response (GoaBackendImapClient  *client,
                        const gchar           *data)
{
  gchar **tokens;
  GArray *array;
  guint n;

  array = g_array_sized_new (FALSE,
                             FALSE,
                             sizeof (gint),
                             100);

  /* TODO: this could be done more efficiently but I'm pretty lazy */
  tokens = g_strsplit (data, " ", -1);
  for (n = 0; tokens[n] != NULL; n++)
    {
      gint seqnum;
      seqnum = atoi (tokens[n]);
      g_array_append_val (array, seqnum);
    }
  g_strfreev (tokens);

  /* Sort resulting messages by seqnum so the newest ones are first */
  g_array_sort (array, (GCompareFunc) sort_by_seqnum_reverse);

  if (client->search_result != NULL)
    g_free (client->search_result);
  client->num_search_result = array->len;
  client->search_result = (gint *) g_array_free (array, FALSE);
}

static void
handle_untagged_response (GoaBackendImapClient  *client,
                          const gchar           *response)
{
  gint n;
  gchar *s;
  gchar **tokens;

  s = NULL;
  tokens = NULL;

  s = g_strdup (response + 1); /* skip leading asterix */
  g_strchug (s); /* and leading whitespace, if any */

  /* special case */
  if (g_str_has_prefix (s, "CAPABILITY "))
    {
      gchar **strv;
      strv = g_strsplit (s + sizeof "CAPABILITY " - 1, " ", -1);
      client->caps = capability_flags_from_strv ((const gchar *const *) strv);
      g_strfreev (strv);
      goto out;
    }

  tokens = g_strsplit (s, " ", 3);
  if (g_strv_length (tokens) < 2)
    {
      g_debug ("ignoring short response `%s'", response);
      goto out;
    }

  if (g_strcmp0 (tokens[1], "EXISTS") == 0 && parse_int (tokens[0], &n))
    {
      client->exists = n;
    }
  else if (g_strcmp0 (tokens[1], "FETCH") == 0 && parse_int (tokens[0], &n))
    {
      if (tokens[2] != NULL)
        handle_fetch_response (client, n, tokens[2]);
      else
        g_debug ("ignoring fetch response with no data `%s'", response);
    }
  else if (g_strcmp0 (tokens[1], "EXPUNGE") == 0 && parse_int (tokens[0], &n))
    {
      handle_expunge_response (client, n);
    }
  else if (g_strcmp0 (tokens[0], "STATUS") == 0)
    {
      handle_status_response (client, tokens[1], tokens[2]);
    }
  else if (g_strcmp0 (tokens[0], "SEARCH") == 0)
    {
      handle_search_response (client, s + sizeof "SEARCH " - 1);
    }
  else
    {
      g_debug ("TODO: unhandled untagged response `%s'", response);
    }

 out:
  g_strfreev (tokens);
  g_free (s);
}

static gchar *
goa_backend_imap_client_run_command_sync (GoaBackendImapClient  *client,
                                          const gchar           *command,
                                          GCancellable          *cancellable,
                                          GError               **error)
{
  gchar *s;
  gchar *tag;
  gchar *ret;
  GString *response;
  gsize len;
  gboolean is_idle_command;
  gboolean idle_has_sent_done;
  GError *local_error;

  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), NULL);
  g_return_val_if_fail (command != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  s = NULL;
  tag = NULL;
  response = NULL;
  ret = NULL;
  is_idle_command = FALSE;
  idle_has_sent_done = FALSE;

  if (g_strcmp0 (command, "IDLE") == 0)
    is_idle_command = TRUE;

  tag = g_strdup_printf ("T%05d ", client->tag++);
  s = g_strconcat (tag, command, "\r\n", NULL);
  if (!g_data_output_stream_put_string (client->dos, s, cancellable, error))
    {
      g_prefix_error (error, "Error putting string: ");
      goto out;
    }
  g_free (s);

  response = g_string_new (NULL);
 again:
  local_error = NULL;
  s = g_data_input_stream_read_line (client->dis, NULL, cancellable, &local_error);
  if (s == NULL)
    {
      if (local_error != NULL)
        {
          g_prefix_error (&local_error, "Error reading line: ");
          /* if doing an IDLE that was cancelled, write the continuation string
           * anyway, ignoring the cancellable
           */
          if ((local_error->domain == G_IO_ERROR && local_error->code == G_IO_ERROR_CANCELLED) && is_idle_command)
            {
              if (!g_data_output_stream_put_string (client->dos, "DONE\r\n", NULL, error))
                {
                  /* if this fails, ignore the cancelled error */
                  g_error_free (local_error);
                  g_prefix_error (error, "Error putting IDLE continuation string: ");
                  goto out;
                }
              /* TODO: this way we're ignoring the response to the IDLE command we just
               * fired off.. it's not a problem per se, but it's annoying to see in
               * debug output... we could sit around and wait for the response but there's
               * really no point in doing so
               */
            }
          g_propagate_error (error, local_error);
        }
      else
        {
          g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, "No content to read");
        }
      goto out;
    }
  len = strlen (s);
  /* So far so good */
  g_string_append_len (response, s, len);

  /* Could be it's a literal string */
  if (len >= 3 && s[len-1] == '}')
    {
      gint n;
      n = len - 2;
      while (g_ascii_isdigit (s[n]) && n >= 0)
        n--;
      if (s[n] == '{')
        {
          gsize num_read;
          gsize lit_len;
          gchar *lit;
          lit_len = atoi (s + n + 1);
          /* Don't blindly allocate any big number of bytes */
          if (lit_len > 10*1024*1024)
            {
              g_set_error (error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           "Refusing to read an additional %" G_GSIZE_FORMAT " bytes for literal string",
                           lit_len);
              g_free (s);
              goto out;
            }
          lit = g_malloc0 (lit_len + 1);
          if (!g_input_stream_read_all (G_INPUT_STREAM (client->dis),
                                        lit,
                                        lit_len,
                                        &num_read,
                                        cancellable,
                                        error))
            {
              g_free (lit);
              g_prefix_error (error,
                              "Requested %" G_GSIZE_FORMAT " bytes for literal string "
                              "but only read %" G_GSSIZE_FORMAT ": ",
                              lit_len, num_read);
              g_free (s);
              goto out;
            }
          /* include the original CRLF, then the literal string */
          g_string_append (response, "\r\n");
          g_string_append_len (response, lit, lit_len);
          g_free (lit);
          g_free (s);
          /* then keep reading */
          goto again;
        }
    }

  if (g_str_has_prefix (response->str, tag))
    {
      gint tag_len;
      tag_len = strlen (tag);
      if (g_str_has_prefix (response->str + tag_len, "BAD"))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       "BAD response to `%s': %s",
                       command,
                       response->str + tag_len + 4);
          goto out;
        }
      ret = g_strdup (response->str + tag_len);
      /* TODO: return additional response? */
      goto out;
    }
  else if (g_str_has_prefix (response->str, "*"))
    {
      /* untagged */
      handle_untagged_response (client, response->str);
    }
  else
    {
      /* otherwise other unhandled response */
      g_debug ("unhandled response `%s'", response->str);
    }

  /* If idling, when we receive real data, put the DONE continuation
   * string so the IDLE command will terminate
   */
  if (is_idle_command && !g_str_has_prefix (response->str, "+") && !idle_has_sent_done)
    {
      idle_has_sent_done = TRUE;
      if (!g_data_output_stream_put_string (client->dos, "DONE\r\n", cancellable, error))
        {
          g_prefix_error (error, "Error putting IDLE continuation string: ");
          goto out;
        }
    }

  /* reset */
  g_string_set_size (response, 0);
  goto again;

 out:
  if (response != NULL)
    g_string_free (response, TRUE);
  g_free (tag);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_client_get_num_unread:
 * @client: A #GoaBackendImapClient.
 *
 * The amount of unread messages in the mailbox.
 *
 * Returns: The number of unread messages or -1 if the connection has been closed.
 */
gint
goa_backend_imap_client_get_num_unread (GoaBackendImapClient  *client)
{
  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), -1);
  if (client->closed)
    return -1;
  else
    return client->unseen;
}

/**
 * goa_backend_imap_client_get_num_messages:
 * @client: A #GoaBackendImapClient.
 *
 * The amount of messages in the mailbox.
 *
 * Returns: The number of messages or -1 if the connection has been closed.
 */
gint
goa_backend_imap_client_get_num_messages (GoaBackendImapClient  *client)
{
  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), -1);
  if (client->closed)
    return -1;
  else
    return client->exists;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_client_get_messages:
 * @client: A #GoaBackendImapClient.
 *
 * Gets all messages in the mailbox matching the
 * #GoaBackendImapClient:criteria, #GoaBackendImapClient:query-offset
 * and #GoaBackendImapClient:query-size properties as described in the
 * documentation for goa_backend_imap_client_new_sync().
 *
 * Returns: (transfer full) (element-type GoaBackendImapMessage): A
 * list of messages that should be freed with g_list_free() after each
 * element has been freed with goa_imap_message_unref().
 */
GList *
goa_backend_imap_client_get_messages (GoaBackendImapClient  *client)
{
  GList *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), NULL);

  G_LOCK (messages_lock);
  ret = g_list_copy (client->messages);
  g_list_foreach (client->messages, (GFunc) goa_backend_imap_message_ref, NULL);
  G_UNLOCK (messages_lock);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_client_refresh_sync:
 * @client: A #GoaBackendImapClient.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Forcibly refreshes the messages by doing a server roundtrip. The
 * calling thread is blocked while this is happening.
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
goa_backend_imap_client_refresh_sync (GoaBackendImapClient  *client,
                                      GCancellable          *cancellable,
                                      GError               **error)
{
  gboolean ret;
  gint num_completed_fetches_before;

  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_object_ref (client);

  ret = FALSE;

  if (client->closed)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Client is not running");
      goto out;
    }

  g_mutex_lock (client->num_completed_fetches_mutex);
  num_completed_fetches_before = client->num_completed_fetches;
  g_cancellable_cancel (client->worker_cancellable);
  while (client->num_completed_fetches < num_completed_fetches_before + 1)
    g_cond_wait (client->num_completed_fetches_cond, client->num_completed_fetches_mutex);
  if (client->num_completed_fetches == G_MAXINT)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Client stopped while refreshing");
    }
  else
    {
      ret = TRUE;
    }
  //g_debug ("yay client->num_completed_fetches=%d", client->num_completed_fetches);
  g_mutex_unlock (client->num_completed_fetches_mutex);

 out:
  g_object_unref (client);
  return ret;
}

static void
refresh_in_thread_func (GSimpleAsyncResult *res,
                        GObject            *object,
                        GCancellable       *cancellable)
{
  GError *error;
  error = NULL;
  if (!goa_backend_imap_client_refresh_sync (GOA_BACKEND_IMAP_CLIENT (object),
                                             cancellable,
                                             &error))
    g_simple_async_result_take_error (res, error);
}

/**
 * goa_backend_imap_client_refresh:
 * @client: A #GoaBackendImapClient.
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Function to call when the request has been satisfied.
 * @user_data: Data to pass to @callback.
 *
 * Async version of goa_backend_imap_client_refresh_sync().
 *
 * When the result is ready, @callback will be called in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link>. You can then use goa_backend_imap_client_refresh_finish()
 * to get the result.
 */
void
goa_backend_imap_client_refresh (GoaBackendImapClient  *client,
                                 GCancellable          *cancellable,
                                 GAsyncReadyCallback    callback,
                                 gpointer               user_data)
{
  GSimpleAsyncResult *simple;

  g_return_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  simple = g_simple_async_result_new (G_OBJECT (client),
                                      callback,
                                      user_data,
                                      goa_backend_imap_client_refresh);
  g_simple_async_result_run_in_thread (simple,
                                       refresh_in_thread_func,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
  g_object_unref (simple);
}

/**
 * goa_backend_imap_client_refresh_finish:
 * @client: A #GoaBackendImapClient.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_imap_client_refresh().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_imap_client_refresh().
 *
 * Returns: %TRUE if the operation succeeded, %FALSE if @error is set.
 */
gboolean
goa_backend_imap_client_refresh_finish (GoaBackendImapClient  *client,
                                        GAsyncResult          *res,
                                        GError               **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  gboolean ret;

  ret = FALSE;

  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == goa_backend_imap_client_refresh);

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_imap_client_do_login (GoaBackendImapClient  *client,
                                  GCancellable          *cancellable,
                                  GError               **error)
{
  gboolean ret;
  gchar *response;

  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  client->sc = g_socket_client_new ();
  if (client->use_tls)
    g_socket_client_set_tls (client->sc, TRUE);

  /* TODO: TLS validation etc etc */

  client->c = g_socket_client_connect_to_host (client->sc,
                                               client->host_and_port,
                                               client->use_tls ? 993 : 143,
                                               cancellable,
                                               error);
  if (client->c == NULL)
    goto out;

  /* fail quickly */
  g_socket_set_timeout (g_socket_connection_get_socket (client->c), 30);

  client->dis = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (client->c)));
  client->dos = g_data_output_stream_new (g_io_stream_get_output_stream (G_IO_STREAM (client->c)));
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (client->dis), FALSE);
  g_filter_output_stream_set_close_base_stream (G_FILTER_OUTPUT_STREAM (client->dos), FALSE);
  g_data_input_stream_set_newline_type (client->dis, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

  /* Authenticate via the passed in auth helper */
  if (!goa_backend_imap_auth_run_sync (client->auth,
                                       client->dis,
                                       client->dos,
                                       cancellable,
                                       error))
    goto out;

  /* Finally, select the mailbox
   * TODO: make it possible to select other inboxes
   */
  response = goa_backend_imap_client_run_command_sync (client,
                                                       "SELECT INBOX",
                                                       cancellable,
                                                       error);
  if (response == NULL)
    goto out;
  g_free (response);

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_imap_client_do_refresh (GoaBackendImapClient  *client,
                                    GCancellable          *cancellable,
                                    GError               **error)
{
  gchar *response;
  gboolean ret;
  GString *request_str;
  guint num_fetches;

  ret = FALSE;

  /* fail quickly */
  g_socket_set_timeout (g_socket_connection_get_socket (client->c), 30);

  /* First get an overall status */
  response = goa_backend_imap_client_run_command_sync (client,
                                                       "STATUS INBOX (MESSAGES UNSEEN UIDVALIDITY)",
                                                       cancellable,
                                                       error);
  if (response == NULL)
    goto out;
  g_free (response);

  if (client->exists == 0)
    {
      ret = TRUE; /* that was easy */
      goto out;
    }

  num_fetches = 0;
  /* Calculate FETCH string */
  request_str = g_string_new ("FETCH ");
  if (g_strcmp0 (client->criteria, "") == 0)
    {
      gint message_seqnum_high;
      gint message_seqnum_low;

      /* No search criteria => get latest messages */

      /* TODO: use client->query_offset */
      message_seqnum_high = client->exists;
      message_seqnum_low = client->exists - client->query_size + 1;
      if (message_seqnum_low < 1)
        message_seqnum_low = 1;
      g_string_append_printf (request_str,
                              "%d:%d",
                              message_seqnum_low,
                              message_seqnum_high);
      num_fetches = message_seqnum_high - message_seqnum_low + 1;
    }
  else
    {
      guint n;
      if (client->search_result != NULL)
        {
          client->search_result = NULL;
          g_free (client->search_result);
        }
      response = goa_backend_imap_client_run_command_sync (client,
                                                           "SEARCH UNSEEN",
                                                           cancellable,
                                                           error);
      if (response == NULL)
        goto out;
      g_free (response);
      for (n = client->query_offset; n < client->num_search_result && n < client->query_size; n++)
        {
          /* TODO: maybe compress into ranges - not sure it matters
           * as the query is really small
           */
          if (n > 0)
            g_string_append_c (request_str, ',');
          g_string_append_printf (request_str, "%d", client->search_result[n]);
          num_fetches++;
        }
    }

  /* build up messages */
  g_assert (client->messages_buildup == NULL);
  if (num_fetches > 0)
    {
      g_string_append (request_str,
                       " (FLAGS INTERNALDATE UID X-GM-THRID BODY.PEEK[HEADER.FIELDS (Date From To Cc Subject)] BODY.PEEK[TEXT]<0.200>)");
      response = goa_backend_imap_client_run_command_sync (client,
                                                           request_str->str,
                                                           cancellable,
                                                           error);
      g_string_free (request_str, TRUE);
      if (response == NULL)
        goto out;
      g_free (response);
    }
  else
    {
      g_string_free (request_str, FALSE);
    }

  /* Sort resulting messages by seqnum */
  client->messages_buildup = g_list_sort (client->messages_buildup,
                                          (GCompareFunc) goa_backend_imap_message_compare_seqnum_reverse);
  /* replace messages */
  G_LOCK (messages_lock);
  g_list_foreach (client->messages, (GFunc) goa_backend_imap_message_unref, NULL);
  g_list_free (client->messages);
  client->messages = client->messages_buildup;
  G_UNLOCK (messages_lock);
  client->messages_buildup = NULL;

  ret = TRUE;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/* runs in the thread that runs the default GMainContext */
static gboolean
refresh_while_idling_cb (gpointer user_data)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (user_data);
  goa_backend_imap_client_refresh (client,
                                   NULL,  /* GCancellable */
                                   NULL,  /* GAsyncReadyCallback */
                                   NULL); /* user_data */
  return FALSE;
}

static gboolean
goa_backend_imap_client_do_idle (GoaBackendImapClient  *client,
                                 GError               **error)
{
  GError *local_error;
  gboolean ret;
  gchar *response;
  guint refresh_timeout_id;

  ret = FALSE;

  /* OK, sit around and wait until the mailbox changes (e.g. new mail arriving)... For
   * we use the IMAP IDLE command, see http://tools.ietf.org/html/rfc2177, if available
   *
   * (TODO: handle IDLE not being available)
   *
   * Note that this operation can be interrupted by another thread
   * cancelling
   *
   *   client->worker_cancellable
   *
   * If this happens then we reset the cancellable (so it can be cancelled
   * again) and then our caller can do another loop (e.g. SEARCH, FETCH all
   * messages of interest), increasing client->num_completed_fetches (and waking up
   * waiters) when done.
   */
  local_error = NULL;
  g_cancellable_reset (client->worker_cancellable);

  /* We want a nice long timeout here to conserve battery power so
   * set socket timeout to infinite and schedule our own refresh.
   *
   * Note that IMAP IDLE suggests that we should not sit and wait
   * too long so we pick 25 minutes here.
   */
  g_socket_set_timeout (g_socket_connection_get_socket (client->c), 0);

  /* TODO: hmm.. safe to use the default GMainContext? The only alternative
   * is to create our own thread
   */
  refresh_timeout_id = g_timeout_add (25 * 60 * 1000,
                                      refresh_while_idling_cb,
                                      client);
  response = goa_backend_imap_client_run_command_sync (client,
                                                       "IDLE",
                                                       client->worker_cancellable,
                                                       &local_error);
  g_source_remove (refresh_timeout_id);
  if (response == NULL)
    {
      if ((local_error->domain == G_IO_ERROR && local_error->code == G_IO_ERROR_CANCELLED))
        {
          g_error_free (local_error);
        }
      else if ((local_error->domain == G_IO_ERROR && local_error->code == G_IO_ERROR_TIMED_OUT))
        {
          g_error_free (local_error);
        }
      else
        {
          g_propagate_error (error, local_error);
          goto out;
        }
    }
  else
    {
      g_free (response);
    }

  ret = TRUE;

 out:
  return ret;
}


/* ---------------------------------------------------------------------------------------------------- */

/* worker thread */
static gpointer
goa_backend_imap_client_worker_thread_func (gpointer user_data)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (user_data);
  GError *error;

  /* This is the main loop where we idle, then refresh, then idle,
   * then refresh again and around and around she goes...
   */
  while (TRUE)
    {
      //g_debug ("idling in worker thread");
      error = NULL;
      if (!goa_backend_imap_client_do_idle (client, &error))
        goto out;

      /* exit worker thread if being asked to close */
      if (client->closed)
        {
          error = NULL;
          goto out;
        }

      //g_debug ("refreshing in worker thread");
      error = NULL;
      if (!goa_backend_imap_client_do_refresh (client, NULL, &error))
        goto out;

      /* Done with a loop, notify threads waiting on us (if any) */
      //g_debug ("notifying waiters");
      g_mutex_lock (client->num_completed_fetches_mutex);
      client->num_completed_fetches += 1;
      g_cond_broadcast (client->num_completed_fetches_cond);
      g_mutex_unlock (client->num_completed_fetches_mutex);

      /* let the user know that we completed a loop and we're now idling  */
      //g_debug ("emitting ::updated signal");
      emit_updated_in_idle (client);
    }

 out:
  //g_debug ("worker done");
  emit_closed_in_idle (client, error);
  if (error != NULL)
    g_error_free (error);
  g_object_unref (client);
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_client_get_closed:
 * @client: A #GoaBackendImapClient.
 *
 * Gets if @client is closed.
 *
 * Returns: %TRUE if the connection to the IMAP server has been closed, %FALSE otherwise.
 */
gboolean
goa_backend_imap_client_get_closed (GoaBackendImapClient  *client)
{
  g_return_val_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client), FALSE);
  return client->closed;
}

/**
 * goa_backend_imap_client_close:
 * @client: A #GoaBackendImapClient.
 *
 * Closes the connection used by @client.
 */
void
goa_backend_imap_client_close (GoaBackendImapClient  *client)
{
  g_return_if_fail (GOA_IS_BACKEND_IMAP_CLIENT (client));
  client->closed = TRUE;
  g_cancellable_cancel (client->worker_cancellable);
  /* TODO: could join on client->worker_thread ... */
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_imap_client_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
  GoaBackendImapClient *client = GOA_BACKEND_IMAP_CLIENT (initable);
  gboolean ret;

  ret = FALSE;

  if (!goa_backend_imap_client_do_login (client, cancellable, error))
    goto out;

  if (!goa_backend_imap_client_do_refresh (client, cancellable, error))
    goto out;

  /* OK, made it this far - create a worker thread for idling and refreshing */
  client->worker_thread = g_thread_create (goa_backend_imap_client_worker_thread_func,
                                           g_object_ref (client),
                                           TRUE, /* joinable */
                                           error);
  if (client->worker_thread == NULL)
    goto out;

  ret = TRUE;

 out:
  return ret;
}

static void
goa_backend_imap_client__g_initable_iface_init (GInitableIface *iface)
{
  iface->init = goa_backend_imap_client_initable_init;
}

static void
goa_backend_imap_client__g_async_initable_iface_init (GAsyncInitableIface *iface)
{
  /* use default implementation that runs the GInitable code in a thread */
}

/* ---------------------------------------------------------------------------------------------------- */
