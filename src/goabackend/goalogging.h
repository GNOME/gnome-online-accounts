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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: David Zeuthen <davidz@redhat.com>
 */

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_LOGGING_H__
#define __GOA_LOGGING_H__

#include <glib.h>

#include "goabackendenums-priv.h"

G_BEGIN_DECLS

void goa_log_init (gboolean no_colors);

void goa_log (GoaLogLevel     level,
              const gchar    *function,
              const gchar    *location,
              const gchar    *format,
              ...) G_GNUC_PRINTF (4, 5);

/**
 * goa_debug:
 * @...: printf()-style format string and arguments
 *
 * Logging macro for %GOA_LOG_LEVEL_DEBUG.
 */
#define goa_debug(...)   goa_log(GOA_LOG_LEVEL_DEBUG, G_STRFUNC, G_STRLOC, __VA_ARGS__);

/**
 * goa_info:
 * @...: printf()-style format string and arguments
 *
 * Logging macro for %GOA_LOG_LEVEL_INFO.
 */
#define goa_info(...)    goa_log(GOA_LOG_LEVEL_INFO, G_STRFUNC, G_STRLOC, __VA_ARGS__);

/**
 * goa_notice:
 * @...: printf()-style format string and arguments
 *
 * Logging macro for %GOA_LOG_LEVEL_NOTICE.
 */
#define goa_notice(...)    goa_log(GOA_LOG_LEVEL_NOTICE, G_STRFUNC, G_STRLOC, __VA_ARGS__);

/**
 * goa_warning:
 * @...: printf()-style format string and arguments
 *
 * Logging macro for %GOA_LOG_LEVEL_WARNING.
 */
#define goa_warning(...) goa_log(GOA_LOG_LEVEL_WARNING, G_STRFUNC, G_STRLOC, __VA_ARGS__);

/**
 * goa_error:
 * @...: printf()-style format string and arguments
 *
 * Logging macro for %GOA_LOG_LEVEL_ERROR.
 */
#define goa_error(...)   goa_log(GOA_LOG_LEVEL_ERROR, G_STRFUNC, G_STRLOC, __VA_ARGS__);


G_END_DECLS

#endif /* __GOA_LOGGING_H__ */
