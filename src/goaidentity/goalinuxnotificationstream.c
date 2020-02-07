/*
 * Copyright © 2020 Red Hat, Inc.
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

#include <errno.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "goalinuxnotificationstream.h"

struct _GoaLinuxNotificationStream
{
  GUnixInputStream parent_instance;
  gint fd_write;
};

enum
{
  PROP_O,
  PROP_FD_WRITE
};

G_DEFINE_TYPE (GoaLinuxNotificationStream, goa_linux_notification_stream, G_TYPE_UNIX_INPUT_STREAM);

static gboolean
goa_linux_notification_stream_close (GInputStream *stream, GCancellable *cancellable, GError **error)
{
  GoaLinuxNotificationStream *self = GOA_LINUX_NOTIFICATION_STREAM (stream);
  gboolean ret_val = FALSE;
  gint error_code;

  error_code = close (self->fd_write);
  if (error_code == -1)
    {
      GIOErrorEnum error_enum;
      const gchar *error_str;
      gint errno_saved = errno;

      error_enum = g_io_error_from_errno (errno_saved);
      error_str = g_strerror (errno_saved);
      g_set_error (error, G_IO_ERROR, error_enum, _("Error closing file descriptor: %s"), error_str);
      goto out;
    }

  if (!G_INPUT_STREAM_CLASS (goa_linux_notification_stream_parent_class)->close_fn (stream, cancellable, error))
    goto out;

  ret_val = TRUE;

 out:
  return ret_val;
}

static void
goa_linux_notification_stream_close_async (GInputStream *stream,
                                           gint io_priority,
                                           GCancellable *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
  g_autoptr (GTask) task = NULL;

  task = g_task_new (stream, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_linux_notification_stream_close_async);
  g_task_set_priority (task, io_priority);

  {
    g_autoptr (GError) error = NULL;

    if (!goa_linux_notification_stream_close (stream, cancellable, &error))
      {
        g_task_return_error (task, error);
        goto out;
      }
  }

  g_task_return_boolean (task, TRUE);

 out:
  return;
}

static gboolean
goa_linux_notification_stream_close_finish (GInputStream *stream, GAsyncResult *result, GError **error)
{
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (result, stream), FALSE);
  task = G_TASK (result);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_linux_notification_stream_close_async, FALSE);

  return g_task_propagate_boolean (task, error);
}

static void
goa_linux_notification_stream_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GoaLinuxNotificationStream *self = GOA_LINUX_NOTIFICATION_STREAM (object);

  switch (prop_id)
    {
    case PROP_FD_WRITE:
      self->fd_write = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_linux_notification_stream_init (GoaLinuxNotificationStream *self)
{
}

static void
goa_linux_notification_stream_class_init (GoaLinuxNotificationStreamClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GInputStreamClass *stream_class = G_INPUT_STREAM_CLASS (klass);

  object_class->set_property = goa_linux_notification_stream_set_property;
  stream_class->close_async = goa_linux_notification_stream_close_async;
  stream_class->close_finish = goa_linux_notification_stream_close_finish;
  stream_class->close_fn = goa_linux_notification_stream_close;

  g_object_class_install_property (object_class,
                                   PROP_FD_WRITE,
                                   g_param_spec_int ("fd-write",
                                                     "A file descriptor for writing to the pipe",
                                                     "The write end of Linux's notification pipe",
                                                     G_MININT,
                                                     G_MAXINT,
                                                     -1,
                                                     G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS | G_PARAM_WRITABLE));
}

GInputStream *
goa_linux_notification_stream_new (gint fd_read, gint fd_write)
{
  g_return_val_if_fail (fd_read != -1, NULL);
  g_return_val_if_fail (fd_write != -1, NULL);

  return g_object_new (GOA_TYPE_LINUX_NOTIFICATION_STREAM, "close-fd", TRUE, "fd", fd_read, "fd-write", fd_write, NULL);
}
