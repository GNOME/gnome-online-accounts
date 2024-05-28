/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2024 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#include <linux/types.h>

#ifndef __GOA_LINUX_WATCH_QUEUE_PRIVATE_H__
#define __GOA_LINUX_WATCH_QUEUE_PRIVATE_H__


/* The following definitions are vendored from Linux' headers to avoid
 * conflicts with libc's headers, specifically <fcntl.h>.
 *
 * See: https://www.kernel.org/doc/Documentation/watch_queue.rst
 */
#define O_NOTIFICATION_PIPE        O_EXCL         /* Parameter to pipe2() selecting notification pipe */
#define IOC_WATCH_QUEUE_SET_SIZE   _IO('W', 0x60) /* Set the size in pages */
#define IOC_WATCH_QUEUE_SET_FILTER _IO('W', 0x61) /* Set the filter */

enum watch_notification_type {
  WATCH_TYPE_META       = 0,  /* Special record */
  WATCH_TYPE_KEY_NOTIFY = 1,  /* Key change event notification */
  WATCH_TYPE__NR        = 2
};

struct watch_notification_type_filter {
  __u32  type;              /* Type to apply filter to */
  __u32  info_filter;       /* Filter on watch_notification::info */
  __u32  info_mask;         /* Mask of relevant bits in info_filter */
  __u32  subtype_filter[8]; /* Bitmask of subtypes to filter on */
};

struct watch_notification_filter {
  __u32  nr_filters;         /* Number of filters */
  __u32  __reserved;         /* Must be 0 */
  struct watch_notification_type_filter filters[];
};


/* Kerberos KEYRING watch queue definition
 */
enum
{
  WATCH_QUEUE_BUFFER_SIZE = 256
};

static const struct watch_notification_filter watch_queue_notification_filter =
{
  .nr_filters = 1,
  .filters = { [0] = { .type = WATCH_TYPE_KEY_NOTIFY, .info_filter = 0, .info_mask = 0, .subtype_filter[0] = G_MAXUINT } }
};

#endif /* __GOA_LINUX_WATCH_QUEUE_PRIVATE_H__ */

