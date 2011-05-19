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

#include "goalogging.h"
#include "goaimapauth.h"
#include "goaimapclient.h"

/* The timeout used for non-IDLE commands */
#define COMMAND_TIMEOUT_SEC 30

/**
 * GoaImapClient:
 *
 * The #GoaImapClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaImapClient
{
  /*< private >*/
  GObject parent_instance;

  /* The remaining data members are related to the running session
   */
  GSocketClient *sc;
  GSocketConnection *c;
  GDataInputStream *dis;
  GDataOutputStream *dos;

  /* counter used used for generating command tags */
  guint tag;

  GMutex *lock;
};

typedef struct _GoaImapClientClass GoaImapClientClass;

struct _GoaImapClientClass
{
  GObjectClass parent_class;
  void (*untagged_response) (GoaImapClient  *client,
                             const gchar           *response);
};

/**
 * SECTION:goaimapclient
 * @title: GoaImapClient
 * @short_description: A simple IMAP client
 *
 * #GoaImapClient provides a way to talk to
 * <ulink url="http://tools.ietf.org/html/rfc3501">IMAP</ulink>
 * servers.
 */

enum
{
  UNTAGGED_RESPONSE_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

G_DEFINE_TYPE (GoaImapClient, goa_imap_client, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_imap_client_finalize (GObject *object)
{
  GoaImapClient *client = GOA_IMAP_CLIENT (object);

  g_clear_object (&client->sc);
  g_clear_object (&client->c);
  g_clear_object (&client->dis);
  g_clear_object (&client->dos);
  g_mutex_free (client->lock);

  G_OBJECT_CLASS (goa_imap_client_parent_class)->finalize (object);
}

static void
goa_imap_client_init (GoaImapClient *client)
{
  client->lock = g_mutex_new ();
}

static void
goa_imap_client_class_init (GoaImapClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_imap_client_finalize;

  /**
   * GoaImapClient::untagged-response:
   * @client: The #GoaImapClient emitting the signal.
   * @untagged_response: The untagged response.
   *
   * Signal emitted every an <ulink
   * url="http://tools.ietf.org/html/rfc3501#section-2.2.2">untagged
   * response</ulink> has been received.
   *
   * This signal is emitted in the same thread that calls the
   * goa_imap_client_run_command_sync() method.
   */
  signals[UNTAGGED_RESPONSE_SIGNAL] =
    g_signal_new ("untagged-response",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaImapClientClass, untagged_response),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_STRING);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_imap_client_new:
 *
 * Creates a new #GoaImapClient instance.
 *
 * Typical usage includes connecting to the
 * #GoaImapClient::untagged-response signals and then invoking
 * goa_imap_client_connect_sync().
 *
 * You can then use goa_imap_client_connect_sync(),
 * goa_imap_client_idle_sync() and the
 * #GoaImapClient::untagged-response handler to interact with
 * the IMAP server. If the connection fails then the appropriate error
 * e.g. #G_IO_ERROR_TIMED_OUT or %G_IO_ERROR_NETWORK_UNREACHABLE is
 * returned.
 *
 * You can only make a single successful connection with each
 * #GoaImapClient instance - just create a new instance if the
 * connection breaks and you need to reconnect.
 *
 * Returns: (transfer full): A #GoaImapClient that should be freed with g_object_unref().
 */
GoaImapClient *
goa_imap_client_new (void)
{
  return GOA_IMAP_CLIENT (g_object_new (GOA_TYPE_IMAP_CLIENT, NULL));
}


/**
 * goa_imap_client_connect_sync:
 * @client: A #GoaImapClient.
 * @host_and_port: The name and optionally port to connect to.
 * @use_tls: Whether TLS should be used.
 * @auth: Object used for authenticating the connection.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Connects to the IMAP server represented by @host_and_port using
 * @auth to authenticate the connection. The calling thread is blocked
 * while the operation is pending.
 *
 * Returns: %TRUE if the connection was established and authentication
 * worked, %FALSE if @error is set.
 */
gboolean
goa_imap_client_connect_sync (GoaImapClient  *client,
                              const gchar    *host_and_port,
                              gboolean        use_tls,
                              GoaImapAuth    *auth,
                              GCancellable   *cancellable,
                              GError        **error)
{
  gboolean ret;

  g_return_val_if_fail (GOA_IS_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (host_and_port != NULL, FALSE);
  g_return_val_if_fail (GOA_IS_IMAP_AUTH (auth), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  g_mutex_lock (client->lock);

  if (client->sc != NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Already connected");
      goto out;
    }

  client->sc = g_socket_client_new ();
  if (use_tls)
    g_socket_client_set_tls (client->sc, TRUE);

  /* TODO: TLS validation etc etc */

  client->c = g_socket_client_connect_to_host (client->sc,
                                               host_and_port,
                                               use_tls ? 993 : 143,
                                               cancellable,
                                               error);
  if (client->c == NULL)
    goto out;

  /* fail quickly */
  g_socket_set_timeout (g_socket_connection_get_socket (client->c), COMMAND_TIMEOUT_SEC);

  client->dis = g_data_input_stream_new (g_io_stream_get_input_stream (G_IO_STREAM (client->c)));
  client->dos = g_data_output_stream_new (g_io_stream_get_output_stream (G_IO_STREAM (client->c)));
  g_filter_input_stream_set_close_base_stream (G_FILTER_INPUT_STREAM (client->dis), FALSE);
  g_filter_output_stream_set_close_base_stream (G_FILTER_OUTPUT_STREAM (client->dos), FALSE);
  g_data_input_stream_set_newline_type (client->dis, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);

  /* Authenticate via the passed in auth helper */
  if (!goa_imap_auth_run_sync (auth,
                                       client->dis,
                                       client->dos,
                                       cancellable,
                                       error))
    goto out;


  ret = TRUE;

 out:
  if (!ret)
    {
      g_clear_object (&client->sc);
      g_clear_object (&client->c);
      g_clear_object (&client->dis);
      g_clear_object (&client->dos);
    }
  g_mutex_unlock (client->lock);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_imap_client_run_command_sync:
 * @client: A #GoaImapClient.
 * @command: The command to run.
 * @cancellable: A #GCancellable or %NULL.
 * @error: Return location for error.
 *
 * Submits @command to the remote IMAP server and blocks the calling
 * thread until a response is received. Do not include a command tag
 * in @command - this will automatically be appended.
 *
 * If the received response starts with <literal>BAD</literal> (a
 * <ulink url="http://tools.ietf.org/html/rfc3501#section-7.1.3">protocol-level
 * error</ulink>), then @error will be set to %GOA_ERROR_FAILED and
 * %NULL is returned. Otherwise the full response string (excluding
 * the command tag) is returned.
 *
 * If @command is <literal>IDLE</literal> (see
 * <ulink url="http://tools.ietf.org/html/rfc2177">RFC 2177</ulink>)
 * and @cancellable is cancelled, the continuation string
 * <literal>DONE</literal> is written out automatically.
 * While this method <emphasis>can</emphasis> be used for to submit
 * the <literal>IDLE</literal> IMAP command, the
 * goa_imap_client_idle_sync() method should be used instead.
 *
 * The timeout on the underlying socket will be set to 30 seconds
 * except for the the <literal>IDLE</literal> command which never
 * times out.
 *
 * Note that #GoaImapClient::untagged-response signals are
 * emitted in the <emphasis role="strong">same</emphasis> thread that
 * you call this method from - not the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread that @client was constructed in, as one
 * would except.
 *
 * Returns: The response or %NULL if error is set.
 */
gchar *
goa_imap_client_run_command_sync (GoaImapClient  *client,
                                  const gchar    *command,
                                  GCancellable   *cancellable,
                                  GError        **error)
{
  gchar *s;
  gchar *tag;
  gchar *ret;
  GString *response;
  gsize len;
  gboolean is_idle_command;
  gboolean idle_has_sent_done;
  GError *local_error;

  g_return_val_if_fail (GOA_IS_IMAP_CLIENT (client), NULL);
  g_return_val_if_fail (command != NULL, NULL);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  s = NULL;
  tag = NULL;
  response = NULL;
  ret = NULL;
  is_idle_command = FALSE;
  idle_has_sent_done = FALSE;

  g_mutex_lock (client->lock);

  if (client->sc == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Not yet connected");
      goto out;
    }

  if (g_strcmp0 (command, "IDLE") == 0)
    is_idle_command = TRUE;

  /* select a timeout */
  if (is_idle_command)
    {
      /* never time out */
      g_socket_set_timeout (g_socket_connection_get_socket (client->c), 0);
    }
  else
    {
      /* fail quickly */
      g_socket_set_timeout (g_socket_connection_get_socket (client->c), COMMAND_TIMEOUT_SEC);
    }

  tag = g_strdup_printf ("T%05d ", client->tag++);
  s = g_strconcat (tag, command, "\r\n", NULL);
  goa_debug ("Submitting command `%s'", s);
  if (!g_data_output_stream_put_string (client->dos, s, cancellable, error))
    {
      g_prefix_error (error, "Error putting string: ");
      g_free (s);
      goto out;
    }
  g_free (s);

  response = g_string_new (NULL);
 again:
  local_error = NULL;
  s = g_data_input_stream_read_line (client->dis, NULL, cancellable, &local_error);
  goa_debug ("Received response `%s'", s);
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
          goa_debug ("Read literal string of %" G_GSIZE_FORMAT " bytes", lit_len);
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
      g_signal_emit (client, signals[UNTAGGED_RESPONSE_SIGNAL], 0, response->str + 2);
    }
  else
    {
      /* TODO: not yet interesting to handle other unhandled responses..
       * Typically these are command continuation requests, see
       *
       *  http://tools.ietf.org/html/rfc3501#section-7.5
       */
      /* g_debug ("unhandled response `%s'", response->str); */
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
  g_mutex_unlock (client->lock);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaImapClient  *client;
  GCancellable *local_cancellable;
  gboolean timed_out;
} IdleData;

static gboolean
timeout_while_idling_cb (gpointer user_data)
{
  IdleData *data = user_data;
  data->timed_out = TRUE;
  g_cancellable_cancel (data->local_cancellable);
  return FALSE;
}

static gboolean
cancel_in_idle_cb (gpointer user_data)
{
  GCancellable *cancellable = G_CANCELLABLE (user_data);
  g_cancellable_cancel (cancellable);
  return FALSE;
}

static void
cancelled_while_idling_cb (GCancellable *cancellable,
                           gpointer      user_data)
{
  IdleData *data = user_data;

  /* cancel in idle because right now calling g_cancellable_cancel()
   * in a ::cancelled handler may deadlock, see
   *
   * https://bugzilla.gnome.org/show_bug.cgi?id=650252
   */
  g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                   cancel_in_idle_cb,
                   g_object_ref (data->local_cancellable),
                   g_object_unref);
}

/**
 * goa_imap_client_idle_sync:
 * @client: A #GoaImapClient.
 * @max_idle_seconds: Max number of seconds to idle for. This should be no longer than 29 minutes.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Method used to sit and wait until something happens to the selected
 * mailbox. When a change has been detected this method returns %TRUE.
 *
 * Otherwise the method simply blocks for @max_idle_seconds (in which
 * case %TRUE is also returned) or until @cancellable is cancelled -
 * in which case %FALSE is returned and @error is set to
 * %G_IO_ERROR_CANCELLED.
 *
 * If the IMAP server supports the <ulink
 * url="http://tools.ietf.org/html/rfc2177">IDLE</ulink> command then
 * it is used. Otherwise: TODO: handle servers not using IMAP IDLE.
 *
 * Note that #GoaImapClient::untagged-response signals are
 * emitted in the <emphasis role="strong">same</emphasis> thread that
 * you call this method from - not the
 * <link linkend="g-main-context-push-thread-default">thread-default main loop</link>
 * of the thread that @client was constructed in, as one
 * would except.
 *
 * Returns: %TRUE if the request completed, %FALSE if @error is set.
 */
gboolean
goa_imap_client_idle_sync (GoaImapClient  *client,
                           guint           max_idle_seconds,
                           GCancellable   *cancellable,
                           GError        **error)
{
  GError *local_error;
  gboolean ret;
  gchar *response;
  guint timeout_id;
  IdleData data;
  gulong cancelled_id;

  g_return_val_if_fail (GOA_IS_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;
  cancelled_id = 0;
  timeout_id = 0;

  data.client = g_object_ref (client);
  data.local_cancellable = g_cancellable_new ();
  data.timed_out = FALSE;
  if (cancellable != NULL)
    {
      cancelled_id = g_cancellable_connect (cancellable,
                                            G_CALLBACK (cancelled_while_idling_cb),
                                            &data,
                                            NULL);
    }

  /* We use the default GMainContext for the wake-up. This might not
   * be ideal but the only alternative is to create our own thread.
   */
  timeout_id = g_timeout_add_seconds (max_idle_seconds,
                                      timeout_while_idling_cb,
                                      &data);

  /* OK, sit around and wait until the mailbox changes (e.g. new mail
   * arriving)... For we use the IMAP IDLE command if it's available,
   * see http://tools.ietf.org/html/rfc2177
   *
   * (TODO: actually handle IDLE not being available)
   */
  local_error = NULL;
  response = goa_imap_client_run_command_sync (client,
                                                       "IDLE",
                                                       data.local_cancellable,
                                                       &local_error);
  if (response == NULL)
    {
      if ((local_error->domain == G_IO_ERROR && local_error->code == G_IO_ERROR_CANCELLED)
          && data.timed_out)
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
  if (timeout_id > 0)
    g_source_remove (timeout_id);
  if (cancelled_id != 0)
    g_cancellable_disconnect (cancellable, cancelled_id);
  g_object_unref (data.local_cancellable);
  g_object_unref (data.client);
  return ret;
}

/**
 * goa_imap_client_disconnect_sync:
 * @client: A #GoaImapClient.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Closes the connection used by @client, if any. The calling thread
 * is blocked while the operation is pending.
 *
 * Returns: %TRUE if the connection was closed, %FALSE if @error is set.
 */
gboolean
goa_imap_client_disconnect_sync (GoaImapClient  *client,
                                 GCancellable   *cancellable,
                                 GError        **error)
{
  gboolean ret;

  g_return_val_if_fail (GOA_IS_IMAP_CLIENT (client), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mutex_lock (client->lock);
  if (client->c == NULL)
    ret = TRUE;
  else
    ret = g_io_stream_close (G_IO_STREAM (client->c), cancellable, error);
  g_mutex_unlock (client->lock);
  return ret;
}

