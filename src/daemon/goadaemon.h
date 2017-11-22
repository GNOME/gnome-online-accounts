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

#ifndef __GOA_DAEMON_H__
#define __GOA_DAEMON_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define GOA_TYPE_DAEMON (goa_daemon_get_type ())
G_DECLARE_FINAL_TYPE (GoaDaemon, goa_daemon, GOA, DAEMON, GObject);

GoaDaemon          *goa_daemon_new                (GDBusConnection *connection);

G_END_DECLS

#endif /* __GOA_DAEMON_H__ */
