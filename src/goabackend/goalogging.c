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

#include "config.h"
#include <glib/gi18n-lib.h>

#include <sys/types.h>
#include <sys/syscall.h>

#include <syslog.h>

#include "goalogging.h"


/**
 * SECTION:goalog
 * @title: Logging
 * @short_description: Logging Routines
 *
 * TODO: explain a bit more what these logging routines do - e.g. that
 * %GOA_LOG_LEVEL_NOTICE and higher goes to the syslog and so on
 * etc. etc.
 */


G_LOCK_DEFINE_STATIC (log_lock);

/* ---------------------------------------------------------------------------------------------------- */

typedef enum
{
  _COLOR_RESET,
  _COLOR_BOLD_ON,
  _COLOR_INVERSE_ON,
  _COLOR_BOLD_OFF,
  _COLOR_FG_BLACK,
  _COLOR_FG_RED,
  _COLOR_FG_GREEN,
  _COLOR_FG_YELLOW,
  _COLOR_FG_BLUE,
  _COLOR_FG_MAGENTA,
  _COLOR_FG_CYAN,
  _COLOR_FG_WHITE,
  _COLOR_BG_RED,
  _COLOR_BG_GREEN,
  _COLOR_BG_YELLOW,
  _COLOR_BG_BLUE,
  _COLOR_BG_MAGENTA,
  _COLOR_BG_CYAN,
  _COLOR_BG_WHITE
} _Color;

static gboolean _color_stdin_is_tty = FALSE;
static gboolean _color_initialized = FALSE;

static void
_color_init (void)
{
  if (_color_initialized)
    return;
  _color_initialized = TRUE;
  _color_stdin_is_tty = (isatty (STDIN_FILENO) != 0 && isatty (STDOUT_FILENO) != 0);
}

static const gchar *
_color_get (_Color color)
{
  const gchar *str;

  _color_init ();

  if (!_color_stdin_is_tty)
    return "";

  str = NULL;
  switch (color)
    {
    case _COLOR_RESET:      str="\x1b[0m"; break;
    case _COLOR_BOLD_ON:    str="\x1b[1m"; break;
    case _COLOR_INVERSE_ON: str="\x1b[7m"; break;
    case _COLOR_BOLD_OFF:   str="\x1b[22m"; break;
    case _COLOR_FG_BLACK:   str="\x1b[30m"; break;
    case _COLOR_FG_RED:     str="\x1b[31m"; break;
    case _COLOR_FG_GREEN:   str="\x1b[32m"; break;
    case _COLOR_FG_YELLOW:  str="\x1b[33m"; break;
    case _COLOR_FG_BLUE:    str="\x1b[34m"; break;
    case _COLOR_FG_MAGENTA: str="\x1b[35m"; break;
    case _COLOR_FG_CYAN:    str="\x1b[36m"; break;
    case _COLOR_FG_WHITE:   str="\x1b[37m"; break;
    case _COLOR_BG_RED:     str="\x1b[41m"; break;
    case _COLOR_BG_GREEN:   str="\x1b[42m"; break;
    case _COLOR_BG_YELLOW:  str="\x1b[43m"; break;
    case _COLOR_BG_BLUE:    str="\x1b[44m"; break;
    case _COLOR_BG_MAGENTA: str="\x1b[45m"; break;
    case _COLOR_BG_CYAN:    str="\x1b[46m"; break;
    case _COLOR_BG_WHITE:   str="\x1b[47m"; break;
    default:
      g_assert_not_reached ();
      break;
    }
  return str;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_log:
 * @level: A #GoaLogLevel.
 * @function: Pass #G_STRFUNC here.
 * @location: Pass #G_STRLOC here.
 * @format: printf()-style format.
 * @...: Arguments for format.
 *
 * Low-level logging function used by goa_debug() and other macros.
 */
void
goa_log (GoaLogLevel     level,
         const gchar    *function,
         const gchar    *location,
         const gchar    *format,
         ...)
{
  va_list var_args;
  gchar *message;
  GTimeVal now;
  time_t now_time;
  struct tm *now_tm;
  gchar time_buf[128];
  const gchar *level_str;
  const gchar *level_color_str;
  gint syslog_priority;
  static gboolean have_called_openlog = FALSE;
  gchar *thread_str;

  va_start (var_args, format);
  message = g_strdup_vprintf (format, var_args);
  va_end (var_args);

  G_LOCK (log_lock);

  if (!have_called_openlog)
    {
      openlog ("goa",
               LOG_CONS|LOG_NDELAY|LOG_PID,
               LOG_DAEMON);
      have_called_openlog = TRUE;
    }

  g_get_current_time (&now);
  now_time = (time_t) now.tv_sec;
  now_tm = localtime (&now_time);
  strftime (time_buf, sizeof time_buf, "%H:%M:%S", now_tm);

  switch (level)
    {
    case GOA_LOG_LEVEL_DEBUG:
      level_str = "[DEBUG]";
      syslog_priority = LOG_DEBUG;
      level_color_str = _color_get (_COLOR_FG_BLUE);
      break;

    case GOA_LOG_LEVEL_INFO:
      level_str = "[INFO]";
      syslog_priority = LOG_INFO;
      level_color_str = _color_get (_COLOR_FG_CYAN);
      break;

    case GOA_LOG_LEVEL_NOTICE:
      level_str = "[NOTICE]";
      syslog_priority = LOG_NOTICE;
      level_color_str = _color_get (_COLOR_FG_CYAN);
      break;

    case GOA_LOG_LEVEL_WARNING:
      level_str = "[WARNING]";
      syslog_priority = LOG_WARNING;
      level_color_str = _color_get (_COLOR_FG_YELLOW);
      break;

    case GOA_LOG_LEVEL_ERROR:
      level_str = "[ERROR]";
      syslog_priority = LOG_ERR;
      level_color_str = _color_get (_COLOR_FG_RED);
      break;

    default:
      g_assert_not_reached ();
      break;
    }

 /* TODO: Need to find a portable way of getting the thread ID (#660177) */
#ifdef SYS_gettid
  thread_str = g_strdup_printf ("%d", (gint) syscall (SYS_gettid));
#else
  thread_str = g_strdup_printf ("%d", (gint) getpid());
#endif /* SYS_gettid */
  g_print ("%s%s%s.%03d:%s%s%s[%s]%s:%s%s%s:%s %s %s[%s, %s()]%s\n",
           _color_get (_COLOR_BOLD_ON), _color_get (_COLOR_FG_YELLOW), time_buf, (gint) now.tv_usec / 1000, _color_get (_COLOR_RESET),
           _color_get (_COLOR_FG_MAGENTA), _color_get (_COLOR_BOLD_ON), thread_str, _color_get (_COLOR_RESET),
           level_color_str, _color_get (_COLOR_BOLD_ON), level_str, _color_get (_COLOR_RESET),
           message,
           _color_get (_COLOR_FG_BLACK), location, function, _color_get (_COLOR_RESET));
  if (level >= GOA_LOG_LEVEL_NOTICE)
    syslog (syslog_priority, "%s [%s, %s()]", message, location, function);
  g_free (message);

  G_UNLOCK (log_lock);
}
