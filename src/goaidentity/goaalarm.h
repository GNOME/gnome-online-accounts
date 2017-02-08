/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#ifndef __GOA_ALARM_H__
#define __GOA_ALARM_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS
#define GOA_TYPE_ALARM             (goa_alarm_get_type ())
#define GOA_ALARM(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_ALARM, GoaAlarm))
#define GOA_ALARM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_ALARM, GoaAlarmClass))
#define GOA_IS_ALARM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_ALARM))
#define GOA_IS_ALARM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GOA_TYPE_ALARM))
#define GOA_ALARM_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GOA_TYPE_ALARM, GoaAlarmClass))
typedef struct _GoaAlarm GoaAlarm;
typedef struct _GoaAlarmClass GoaAlarmClass;
typedef struct _GoaAlarmPrivate GoaAlarmPrivate;

struct _GoaAlarm
{
  GObject parent;

  GoaAlarmPrivate *priv;
};

struct _GoaAlarmClass
{
  GObjectClass parent_class;

  void (* fired)   (GoaAlarm *alarm);
  void (* rearmed) (GoaAlarm *alarm);
};

GType goa_alarm_get_type (void);

GoaAlarm *goa_alarm_new (GDateTime *time);
GDateTime *goa_alarm_get_time (GoaAlarm *alarm);
G_END_DECLS
#endif /* __GOA_ALARM_H__ */
