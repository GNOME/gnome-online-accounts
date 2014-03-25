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
 */

#ifndef __GOA_DAEMON_H__
#define __GOA_DAEMON_H__

#include "goadaemontypes.h"
#include <goa/goa.h>

G_BEGIN_DECLS

#define GOA_TYPE_DAEMON  (goa_daemon_get_type ())
#define GOA_DAEMON(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_DAEMON, GoaDaemon))
#define GOA_IS_DAEMON(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_DAEMON))

GType               goa_daemon_get_type           (void) G_GNUC_CONST;
GoaDaemon          *goa_daemon_new                (void);

G_END_DECLS

#endif /* __GOA_DAEMON_H__ */
