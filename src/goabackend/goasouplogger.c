/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2017 Red Hat, Inc.
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

#include "config.h"

#include <glib.h>

#include "goasouplogger.h"

static void
goa_soup_logger_printer (SoupLogger         *logger,
                         SoupLoggerLogLevel  level,
                         gchar               direction,
                         const gchar        *data,
                         gpointer            user_data)
{
  gchar *message;

  message = g_strdup_printf ("%c %s", direction, data);
  g_log_default_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, message, NULL);
  g_free (message);
}

SoupLogger *
goa_soup_logger_new (SoupLoggerLogLevel   level,
                     gint                 max_body_size)
{
  SoupLogger *logger;

  logger = soup_logger_new (level);
  if (max_body_size != -1)
    soup_logger_set_max_body_size (logger, max_body_size);
  soup_logger_set_printer (logger, goa_soup_logger_printer, NULL, NULL);

  return logger;
}
