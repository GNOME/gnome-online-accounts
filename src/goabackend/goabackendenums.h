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
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_BACKEND_ENUMS_H__
#define __GOA_BACKEND_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * GoaLogLevel:
 * @GOA_LOG_LEVEL_DEBUG: Debug messages.
 * @GOA_LOG_LEVEL_INFO: Informational messages.
 * @GOA_LOG_LEVEL_NOTICE: Messages that the administrator should take notice of.
 * @GOA_LOG_LEVEL_WARNING: Warning messages.
 * @GOA_LOG_LEVEL_ERROR: Error messages.
 *
 * Logging levels.
 */
typedef enum
{
  GOA_LOG_LEVEL_DEBUG,
  GOA_LOG_LEVEL_INFO,
  GOA_LOG_LEVEL_NOTICE,
  GOA_LOG_LEVEL_WARNING,
  GOA_LOG_LEVEL_ERROR
} GoaLogLevel;

G_END_DECLS

#endif /* __GOA_BACKEND_ENUMS_H__ */
