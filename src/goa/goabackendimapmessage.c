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

#include "goabackendimapprivate.h"
#include "goabackendimapmessage.h"

G_LOCK_DEFINE_STATIC (message_lock);

/**
 * SECTION:goabackendimapmessage
 * @title: GoaBackendImapMessage
 * @short_description: Message stored on an IMAP server
 *
 * The #GoaBackendImapMessage type is a boxed type representing a
 * message stored on an IMAP server. See the #GoaBackendImapClient
 * type for more details.
 */

G_DEFINE_BOXED_TYPE (GoaBackendImapMessage, goa_backend_imap_message,
                     goa_backend_imap_message_ref,
                     goa_backend_imap_message_unref);

/* ---------------------------------------------------------------------------------------------------- */

GoaBackendImapMessageFlags
goa_backend_imap_message_flags_from_strv (const gchar *const *strings)
{
  GoaBackendImapMessageFlags ret;
  guint n;

  g_return_val_if_fail (strings != NULL, GOA_BACKEND_IMAP_MESSAGE_FLAGS_NONE);

  ret = GOA_BACKEND_IMAP_MESSAGE_FLAGS_NONE;
  for (n = 0; strings[n] != NULL; n++)
    {
      if (g_strcmp0 (strings[n], "\\Seen") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_SEEN;
      else if (g_strcmp0 (strings[n], "\\Answered") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_ANSWERED;
      else if (g_strcmp0 (strings[n], "\\Flagged") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_FLAGGED;
      else if (g_strcmp0 (strings[n], "\\Deleted") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_DELETED;
      else if (g_strcmp0 (strings[n], "\\Draft") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_DRAFT;
      else if (g_strcmp0 (strings[n], "\\Recent") == 0)
        ret |= GOA_BACKEND_IMAP_MESSAGE_FLAGS_RECENT;
      else
        g_debug ("TODO: unhandled flag `%s'", strings[n]);
    }
  return ret;
}

gint
goa_backend_imap_message_compare_seqnum_reverse (const GoaBackendImapMessage *a,
                                                 const GoaBackendImapMessage *b)
{
  return b->seqnum - a->seqnum;
}

/**
 * goa_backend_imap_message_get_flags:
 * @message: A #GoaBackendImapMessage.
 *
 * Gets the flags for @message.
 *
 * Returns: Flags from the #GoaBackendImapMessageFlags enumeration.
 */
GoaBackendImapMessageFlags
goa_backend_imap_message_get_flags (GoaBackendImapMessage *message)
{
  return message->flags;
}

/**
 * goa_backend_imap_message_get_uid:
 * @message: A #GoaBackendImapMessage.
 *
 * Gets the unique id for @message.
 *
 * This includes the <ulink
 * url="http://tools.ietf.org/html/rfc3501#section-2.3.1.1">unique
 * identifier validity value</ulink> in the upper 32 bits.
 *
 * Returns: Unique id.
 */
guint64
goa_backend_imap_message_get_uid (GoaBackendImapMessage *message)
{
  return message->uid;
}

/**
 * goa_backend_imap_message_get_internal_date:
 * @message: A #GoaBackendImapMessage.
 *
 * Gets the <ulink
 * url="http://tools.ietf.org/html/rfc3501#section-2.3.3">internal
 * date</ulink> for @message.
 *
 * Returns: The internal date represented as seconds since the Epoch, Jan 1 1970 0:00 UTC.
 */
gint64
goa_backend_imap_message_get_internal_date (GoaBackendImapMessage *message)
{
  return message->internal_date;
}

/**
 * goa_backend_imap_message_get_headers:
 * @message: A #GoaBackendImapMessage.
 *
 * Gets the subset of <ulink
 * url="http://tools.ietf.org/html/rfc2822">RFC 2822</ulink> headers
 * extracted with the message. See also
 * goa_backend_imap_message_lookup_header().
 *
 * Returns: A string representing the headers.
 */
const gchar *
goa_backend_imap_message_get_headers (GoaBackendImapMessage *message)
{
  return message->rfc822_headers;
}

/**
 * goa_backend_imap_message_get_excerpt:
 * @message: A #GoaBackendImapMessage.
 *
 * Gets an excerpt of the body of @message - the excerpt typically
 * doesn't exceed 200 characters.
 *
 * Returns: An excerpt of the message body.
 */
const gchar *
goa_backend_imap_message_get_excerpt (GoaBackendImapMessage *message)
{
  return message->excerpt;
}

static GHashTable *
parse_rfc822_headers (const gchar *rfc822_headers)
{
  GHashTable *ret;
  gchar **lines;
  guint n;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  lines = g_strsplit (rfc822_headers, "\r\n", -1);
  for (n = 0; lines[n] != NULL; n++)
    {
      const gchar *line = lines[n];
      const gchar *s;

      if (line[0] == '\0')
        continue;

      s = strstr (line, ": ");
      if (s != NULL)
        {
          gchar *key;
          gchar *value;
          key = g_strndup (line, s - line);
          value = g_strdup (s + 2);
          g_hash_table_insert (ret, key, value);
        }
      else
        g_debug ("%s: mysterious line `%s'", G_STRFUNC, line);
    }
  g_strfreev (lines);

  return ret;
}

/**
 * goa_backend_imap_message_lookup_header:
 * @message: A #GoaBackendImapMessage.
 * @header: Header to lookup.
 *
 * Convenience function to lookup a header on @message.
 *
 * Returns: The value corresponding to @header or %NULL if not found.
 */
const gchar *
goa_backend_imap_message_lookup_header (GoaBackendImapMessage *message,
                                        const gchar           *header)
{
  g_return_val_if_fail (header != NULL, NULL);

  G_LOCK (message_lock);
  if (message->headers_hash == NULL)
    message->headers_hash = parse_rfc822_headers (message->rfc822_headers);
  G_UNLOCK (message_lock);

  return g_hash_table_lookup (message->headers_hash, header);
}

GoaBackendImapMessage *
goa_backend_imap_message_new (void)
{
  GoaBackendImapMessage *message;
  message = g_slice_new0 (GoaBackendImapMessage);
  message->ref_count = 1;
  return message;
}

/**
 * goa_backend_imap_message_ref:
 * @message: A #GoaBackendImapMessage.
 *
 * Increases the reference count of @message.
 *
 * Returns: @message.
 */
GoaBackendImapMessage *
goa_backend_imap_message_ref (GoaBackendImapMessage *message)
{
  g_atomic_int_inc (&message->ref_count);
  return message;
}

/**
 * goa_backend_imap_message_unref:
 * @message: A #GoaBackendImapMessage.
 *
 * Decreases the reference count of @message. When the reference count
 * hits 0, the resources used by @message are freed.
 */
void
goa_backend_imap_message_unref (GoaBackendImapMessage *message)
{
  if (g_atomic_int_dec_and_test (&message->ref_count))
    {
      g_free (message->excerpt);
      g_free (message->rfc822_headers);
      if (message->headers_hash != NULL)
        g_hash_table_unref (message->headers_hash);
      g_slice_free (GoaBackendImapMessage, message);
    }
}
