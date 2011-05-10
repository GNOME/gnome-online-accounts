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

#ifndef __GOA_BACKEND_ENUMS_H__
#define __GOA_BACKEND_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * GoaBackendImapMessageFlags:
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_NONE: No flags set.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_SEEN: Corresponds to the <literal>\Seen</literal> flag.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_ANSWERED: Corresponds to the <literal>\Answered</literal> flag.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_FLAGGED: Corresponds to the <literal>\Flagged</literal> flag.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_DELETED: Corresponds to the <literal>\Deleted</literal> flag.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_DRAFT: Corresponds to the <literal>\Draft</literal> flag.
 * @GOA_BACKEND_IMAP_MESSAGE_FLAGS_RECENT: Corresponds to the <literal>\Recent</literal> flag.
 *
 * Flag enumeration corresponding to <ulink url="http://tools.ietf.org/html/rfc3501#section-2.3.2">IMAP flags</ulink>.
 */
typedef enum
{
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_NONE     = 0,
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_SEEN     = (1<<0),
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_ANSWERED = (1<<1),
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_FLAGGED  = (1<<2),
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_DELETED  = (1<<3),
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_DRAFT    = (1<<4),
  GOA_BACKEND_IMAP_MESSAGE_FLAGS_RECENT   = (1<<5)
} GoaBackendImapMessageFlags;

G_END_DECLS

#endif /* __GOA_BACKEND_ENUMS_H__ */
