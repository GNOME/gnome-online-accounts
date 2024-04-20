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

#if !defined (__GOA_INSIDE_GOA_H__) && !defined (GOA_COMPILATION)
#error "Only <goa/goa.h> can be included directly."
#endif

#ifndef __GOA_ERROR_H__
#define __GOA_ERROR_H__

#include <glib.h>

G_BEGIN_DECLS

/**
 * GOA_ERROR:
 *
 * Error domain for Goa.
 * 
 * Errors in this domain will be from the [type@Goa.Error]
 * enumeration. See [struct@GLib.Error] for more information on error domains.
 */
#define GOA_ERROR (goa_error_quark ())

GQuark goa_error_quark (void);

G_END_DECLS

#endif /* __GOA_ERROR_H__ */
