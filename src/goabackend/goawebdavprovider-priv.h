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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_WEBDAV_PROVIDER_PRIV_H__
#define __GOA_WEBDAV_PROVIDER_PRIV_H__

#include <gio/gio.h>
#include <goabackend/goaprovider-priv.h>
#include <goabackend/goawebdavprovider.h>

G_BEGIN_DECLS

/**
 * GoaWebDavProvider:
 *
 * The #GoaWebDavProvider structure contains only private data and should
 * only be accessed using the provided API.
 */

/**
 * GoaWebDavProviderClass:
 * @parent_class: The parent class.
 *
 * Class structure for #GoaWebDavProvider.
 */
struct _GoaWebDavProviderClass
{
  GoaProviderClass parent_class;
};

G_END_DECLS

#endif /* __GOA_WEBDAV_PROVIDER_PRIV_H__ */
