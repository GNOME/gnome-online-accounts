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

#ifndef __GOA_LINUX_NOTIFICATION_STREAM_H__
#define __GOA_LINUX_NOTIFICATION_STREAM_H__

#include <gio/gunixinputstream.h>

G_BEGIN_DECLS

#define GOA_TYPE_LINUX_NOTIFICATION_STREAM (goa_linux_notification_stream_get_type ())
G_DECLARE_FINAL_TYPE (GoaLinuxNotificationStream,
                      goa_linux_notification_stream,
                      GOA,
                      LINUX_NOTIFICATION_STREAM,
                      GUnixInputStream);

GInputStream *goa_linux_notification_stream_new (gint fd_read, gint fd_write);

G_END_DECLS

#endif /* __GOA_KERBEROS_IDENTITY_MANAGER_H__ */
