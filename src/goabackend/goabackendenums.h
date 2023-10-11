/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_ENUMS_H__
#define __GOA_BACKEND_ENUMS_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * GoaProviderGroup:
 * @GOA_PROVIDER_GROUP_BRANDED: Providers with a well-known brand. For
 *   example, Google.
 * @GOA_PROVIDER_GROUP_CONTACTS: Providers that offer address book services.
 *   For example, CardDAV.
 * @GOA_PROVIDER_GROUP_MAIL: Providers that offer email-like messaging
 *   services. For example, IMAP and SMTP.
 * @GOA_PROVIDER_GROUP_TICKETING: Providers with ticketing
 *   capabilities. For example, Kerberos.
 * @GOA_PROVIDER_GROUP_CHAT: Providers that offer chat-like messaging
 *   capabilities. For example, XMPP, IRC.
 * @GOA_PROVIDER_GROUP_INVALID: Used for error handling. No provider
 *   should belong to this group.
 *
 * An enum for specifying which group a provider belongs to. This is
 * can be used to organize the providers while displaying them in an
 * user interface.
 */
typedef enum
{
  GOA_PROVIDER_GROUP_BRANDED,
  GOA_PROVIDER_GROUP_CONTACTS,
  GOA_PROVIDER_GROUP_MAIL,
  GOA_PROVIDER_GROUP_TICKETING,
  GOA_PROVIDER_GROUP_CHAT,
  GOA_PROVIDER_GROUP_INVALID
} GoaProviderGroup;

/**
 * GoaProviderFeatures:
 * @GOA_PROVIDER_FEATURE_BRANDED: Common providers to be highlighted (ie. Google, OwnCloud).
 * @GOA_PROVIDER_FEATURE_MAIL: Mail services (ie. SMTP, IMAP).
 * @GOA_PROVIDER_FEATURE_CALENDAR: Calendaring services (ie. CalDAV).
 * @GOA_PROVIDER_FEATURE_CONTACTS: Addressbook services (ie. CardDAV).
 * @GOA_PROVIDER_FEATURE_CHAT: Instant messaging services (ie. XMPP, IRC).
 * @GOA_PROVIDER_FEATURE_DOCUMENTS: Deprecated; currently unused.
 * @GOA_PROVIDER_FEATURE_PHOTOS: Photos storage services (ie. Flickr).
 * @GOA_PROVIDER_FEATURE_FILES: Files storage services (ie. WebDAV).
 * @GOA_PROVIDER_FEATURE_TICKETING: Ticketing services (ie. Kerberos).
 * @GOA_PROVIDER_FEATURE_READ_LATER: Deprecated; currently unused.
 * @GOA_PROVIDER_FEATURE_PRINTERS: Deprecated; currently unused.
 * @GOA_PROVIDER_FEATURE_MAPS: Deprecated; currently unused.
 * @GOA_PROVIDER_FEATURE_MUSIC: Music related services (e.g. Vkontakte).
 * @GOA_PROVIDER_FEATURE_TODO: Deprecated; currently unused.
 * @GOA_PROVIDER_FEATURE_INVALID: Used for error handling. No provider
 *   should provide this feature.
 *
 * These flags specify the features exported by each provider. They can be
 * expecially useful to restrict the list of available providers when
 * requesting the creation of an account for a specific purpose (eg. from a
 * chat program).
 *
 * Since: 3.10
 */
typedef enum /*< flags >*/
{
  GOA_PROVIDER_FEATURE_BRANDED   = 1 << 1,
  GOA_PROVIDER_FEATURE_MAIL      = 1 << 2,
  GOA_PROVIDER_FEATURE_CALENDAR  = 1 << 3,
  GOA_PROVIDER_FEATURE_CONTACTS  = 1 << 4,
  GOA_PROVIDER_FEATURE_CHAT      = 1 << 5,
  GOA_PROVIDER_FEATURE_DOCUMENTS = 1 << 6,
  GOA_PROVIDER_FEATURE_PHOTOS    = 1 << 7,
  GOA_PROVIDER_FEATURE_FILES     = 1 << 8,
  GOA_PROVIDER_FEATURE_TICKETING = 1 << 9,
  GOA_PROVIDER_FEATURE_READ_LATER= 1 << 10,
  GOA_PROVIDER_FEATURE_PRINTERS  = 1 << 11,
  GOA_PROVIDER_FEATURE_MAPS      = 1 << 12,
  GOA_PROVIDER_FEATURE_MUSIC     = 1 << 13,
  GOA_PROVIDER_FEATURE_TODO      = 1 << 14,
  GOA_PROVIDER_FEATURE_INVALID   = 0
} GoaProviderFeatures;

G_END_DECLS

#endif /* __GOA_BACKEND_ENUMS_H__ */
