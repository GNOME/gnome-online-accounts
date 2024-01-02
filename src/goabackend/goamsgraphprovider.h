/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2019 Vilém Hořínek
 * Copyright © 2022-2023 Jan-Michael Brummer <jan-michael.brummer1@volkswagen.de>
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

#ifndef __GOA_MS_GRAPH_PROVIDER_H__
#define __GOA_MS_GRAPH_PROVIDER_H__

#include <glib-object.h>

#include "goaoauth2provider.h"
#include "goaoauth2provider-priv.h"

G_BEGIN_DECLS

#define GOA_TYPE_MS_GRAPH_PROVIDER (goa_ms_graph_provider_get_type ())
G_DECLARE_FINAL_TYPE (GoaMsGraphProvider,
                      goa_ms_graph_provider,
                      GOA,
                      MS_GRAPH_PROVIDER,
                      GoaOAuth2Provider);

G_END_DECLS

#endif /* __GOA_MS_GRAPH_PROVIDER_H__ */
