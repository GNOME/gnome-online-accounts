/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2013 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_PROVIDER_PRIV_H__
#define __GOA_PROVIDER_PRIV_H__

G_BEGIN_DECLS

/**
 * GOA_PROVIDER_EXTENSION_POINT_NAME:
 *
 * Extension point for #GoaProvider implementations.
 */
#define GOA_PROVIDER_EXTENSION_POINT_NAME "goa-backend-provider"

/**
 * GOA_PROVIDER_FACTORY_EXTENSION_POINT_NAME:
 *
 * Extension point for #GoaProviderFactory implementations.
 */
#define GOA_PROVIDER_FACTORY_EXTENSION_POINT_NAME "goa-backend-provider-factory"

void goa_provider_ensure_extension_points_registered (void);

G_END_DECLS

#endif /* __GOA_PROVIDER_PRIV_H__ */
