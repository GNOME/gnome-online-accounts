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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goa/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_IMAP_MESSAGE_H__
#define __GOA_BACKEND_IMAP_MESSAGE_H__

#include <goa/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_BACKEND_IMAP_MESSAGE (goa_backend_imap_message_get_type)

GType                       goa_backend_imap_message_get_type           (void) G_GNUC_CONST;
GoaBackendImapMessage      *goa_backend_imap_message_ref                (GoaBackendImapMessage *message);
void                        goa_backend_imap_message_unref              (GoaBackendImapMessage *message);
GoaBackendImapMessageFlags  goa_backend_imap_message_get_flags          (GoaBackendImapMessage *message);
guint64                     goa_backend_imap_message_get_uid            (GoaBackendImapMessage *message);
gint64                      goa_backend_imap_message_get_internal_date  (GoaBackendImapMessage *message);
const gchar                *goa_backend_imap_message_get_headers        (GoaBackendImapMessage *message);
const gchar                *goa_backend_imap_message_get_excerpt        (GoaBackendImapMessage *message);
const gchar                *goa_backend_imap_message_lookup_header      (GoaBackendImapMessage *message,
                                                                         const gchar           *header);

G_END_DECLS

#endif /* __GOA_BACKEND_IMAP_MESSAGE_H__ */
