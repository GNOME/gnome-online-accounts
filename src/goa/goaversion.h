/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#ifndef __GOA_VERSION_H__
#define __GOA_VERSION_H__

#include <glib.h>
#include <goaconfig.h>

G_BEGIN_DECLS

extern const guint goa_major_version;
extern const guint goa_minor_version;
extern const guint goa_micro_version;

const gchar * goa_check_version (guint required_major,
                                 guint required_minor,
                                 guint required_micro);

#define GOA_CHECK_VERSION(major,minor,micro)    \
    (GOA_MAJOR_VERSION > (major) || \
     (GOA_MAJOR_VERSION == (major) && GOA_MINOR_VERSION > (minor)) || \
     (GOA_MAJOR_VERSION == (major) && GOA_MINOR_VERSION == (minor) && \
      GOA_MICRO_VERSION >= (micro)))

G_END_DECLS

#endif /* __GOA_VERSION_H__ */
