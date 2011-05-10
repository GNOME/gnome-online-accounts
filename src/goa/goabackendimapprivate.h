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

#if !defined (GOA_BACKEND_COMPILATION)
#error "This is a private header."
#endif

#ifndef __GOA_BACKEND_IMAP_PRIVATE_H__
#define __GOA_BACKEND_IMAP_PRIVATE_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

/**
 * GoaBackendImapMessage:
 *
 * The #GoaBackendImapMessage structure contains only private data and
 * should only be accessed using the provided API.
 */
struct _GoaBackendImapMessage
{
  volatile gint ref_count;
  gint seqnum;
  guint64 uid;
  GoaBackendImapMessageFlags flags;
  gint64 internal_date;
  gchar *rfc822_headers;
  gchar *excerpt;

  /* calculated on-demand */
  GHashTable *headers_hash;
};

GoaBackendImapMessage *goa_backend_imap_message_new (void);

gint goa_backend_imap_message_compare_seqnum_reverse (const GoaBackendImapMessage *a,
                                                      const GoaBackendImapMessage *b);

GoaBackendImapMessageFlags goa_backend_imap_message_flags_from_strv (const gchar *const *strings);

G_END_DECLS

#endif /* __GOA_BACKEND_IMAP_PRIVATE_H__ */
