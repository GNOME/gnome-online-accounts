/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
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

#include "config.h"

#include "goaalarm.h"

#ifdef HAVE_TIMERFD
#include <sys/timerfd.h>
#endif /*HAVE_TIMERFD */

#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>

#define MAX_TIMEOUT_INTERVAL (10 *1000)

typedef enum
{
  GOA_ALARM_TYPE_UNSCHEDULED,
  GOA_ALARM_TYPE_TIMER,
  GOA_ALARM_TYPE_TIMEOUT,
} GoaAlarmType;

struct _GoaAlarm
{
  GObject parent;

  GDateTime *time;
  GDateTime *previous_wakeup_time;
  GMainContext *context;
  GSource *immediate_wakeup_source;
  GRecMutex lock;

  GoaAlarmType type;
  GSource *scheduled_wakeup_source;
  GInputStream *stream; /* NULL, unless using timerfd */
};

enum
{
  FIRED,
  REARMED,
  NUMBER_OF_SIGNALS,
};

enum
{
  PROP_0,
  PROP_TIME
};

static void schedule_wakeups (GoaAlarm *self);
static void schedule_wakeups_with_timeout_source (GoaAlarm *self);
static void goa_alarm_set_time (GoaAlarm *self, GDateTime *time);
static void clear_wakeup_source_pointer (GoaAlarm *self);
static guint signals[NUMBER_OF_SIGNALS] = { 0 };

G_DEFINE_TYPE (GoaAlarm, goa_alarm, G_TYPE_OBJECT);

static void
goa_alarm_dispose (GObject *object)
{
  GoaAlarm *self = GOA_ALARM (object);

  g_clear_object (&self->stream);
  g_clear_pointer (&self->immediate_wakeup_source, g_source_destroy);
  g_clear_pointer (&self->scheduled_wakeup_source, g_source_destroy);
  g_clear_pointer (&self->context, g_main_context_unref);
  g_clear_pointer (&self->time, g_date_time_unref);
  g_clear_pointer (&self->previous_wakeup_time, g_date_time_unref);

  G_OBJECT_CLASS (goa_alarm_parent_class)->dispose (object);
}

static void
goa_alarm_finalize (GObject *object)
{
  GoaAlarm *self = GOA_ALARM (object);

  g_rec_mutex_clear (&self->lock);

  G_OBJECT_CLASS (goa_alarm_parent_class)->finalize (object);
}

static void
goa_alarm_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                        GParamSpec   *param_spec)
{
  GoaAlarm *self = GOA_ALARM (object);
  GDateTime *time;

  switch (property_id)
    {
    case PROP_TIME:
      time = (GDateTime *) g_value_get_boxed (value);
      goa_alarm_set_time (self, time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
      break;
    }
}

static void
goa_alarm_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *param_spec)
{
  GoaAlarm *self = GOA_ALARM (object);

  switch (property_id)
    {
    case PROP_TIME:
      g_value_set_boxed (value, self->time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
      break;
    }
}

static void
goa_alarm_class_init (GoaAlarmClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = goa_alarm_dispose;
  object_class->finalize = goa_alarm_finalize;
  object_class->get_property = goa_alarm_get_property;
  object_class->set_property = goa_alarm_set_property;

  signals[FIRED] = g_signal_new ("fired",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  signals[REARMED] = g_signal_new ("rearmed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  g_object_class_install_property (object_class,
                                   PROP_TIME,
                                   g_param_spec_boxed ("time",
                                                       "Time",
                                                       "Time to fire",
                                                       G_TYPE_DATE_TIME,
                                                       G_PARAM_READWRITE));
}

static void
goa_alarm_init (GoaAlarm *self)
{
  g_rec_mutex_init (&self->lock);
}

static void
fire_alarm (GoaAlarm *self)
{
  g_signal_emit (G_OBJECT (self), signals[FIRED], 0);
}

static void
rearm_alarm (GoaAlarm *self)
{
  g_signal_emit (G_OBJECT (self), signals[REARMED], 0);
}

static void
fire_or_rearm_alarm (GoaAlarm *self)
{
  GTimeSpan time_until_fire;
  GTimeSpan previous_time_until_fire;
  GDateTime *now;

  now = g_date_time_new_now_local ();
  time_until_fire = g_date_time_difference (self->time, now);

  if (self->previous_wakeup_time == NULL)
    {
      self->previous_wakeup_time = now;

      /* If, according to the time, we're past when we should have fired,
       * then fire the alarm.
       */
      if (time_until_fire <= 0)
        fire_alarm (self);
    }
  else
    {
      previous_time_until_fire = g_date_time_difference (self->time, self->previous_wakeup_time);

      g_date_time_unref (self->previous_wakeup_time);
      self->previous_wakeup_time = now;

      /* If, according to the time, we're past when we should have fired,
       * and this is the first wakeup where that's been true then fire
       * the alarm. The first check makes sure we don't fire prematurely,
       * and the second check makes sure we don't fire more than once
       */
      if (time_until_fire <= 0 && previous_time_until_fire > 0)
        {
          fire_alarm (self);

          /* If, according to the time, we're before when we should fire,
           * and we previously fired the alarm, then we've jumped back in
           * time and need to rearm the alarm.
           */
        }
      else if (time_until_fire > 0 && previous_time_until_fire <= 0)
        {
          rearm_alarm (self);
        }
    }
}

static gboolean
on_immediate_wakeup_source_ready (GoaAlarm *self)
{
  g_return_val_if_fail (self->type != GOA_ALARM_TYPE_UNSCHEDULED, FALSE);

  g_rec_mutex_lock (&self->lock);
  fire_or_rearm_alarm (self);
  g_rec_mutex_unlock (&self->lock);
  return FALSE;
}

#ifdef HAVE_TIMERFD
static gboolean
on_timer_source_ready (GObject *stream, GoaAlarm *self)
{
  gint64 number_of_fires;
  gssize bytes_read;
  gboolean run_again = FALSE;
  GError *error = NULL;

  g_return_val_if_fail (GOA_IS_ALARM (self), FALSE);

  g_rec_mutex_lock (&self->lock);

  if (self->type != GOA_ALARM_TYPE_TIMER)
    {
      g_warning ("GoaAlarm: timer source ready callback called "
                 "when timer source isn't supposed to be used. "
                 "Current timer type is %u", self->type);
      goto out;
    }

  bytes_read =
    g_pollable_input_stream_read_nonblocking (G_POLLABLE_INPUT_STREAM (stream),
                                              &number_of_fires, sizeof (gint64),
                                              NULL, &error);

  if (bytes_read < 0)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_debug ("GoaAlarm: discontinuity detected from timer fd");
      else
        {
          g_warning ("GoaAlarm: failed to read from timer fd: %s\n",
                     error->message);
          goto out;
        }
    }

  if (bytes_read == sizeof (gint64))
    {
      if (number_of_fires < 0 || number_of_fires > 1)
        {
          g_warning ("GoaAlarm: expected timerfd to report firing once,"
                     "but it reported firing %ld times\n", (long) number_of_fires);
        }
    }

  fire_or_rearm_alarm (self);
  run_again = TRUE;
out:
  g_rec_mutex_unlock (&self->lock);
  g_clear_error (&error);
  return run_again;
}
#endif /*HAVE_TIMERFD */

static gboolean
schedule_wakeups_with_timerfd (GoaAlarm *self)
{
#ifdef HAVE_TIMERFD
  struct itimerspec timer_spec;
  int fd;
  int result;
  GSource *source;
  static gboolean seen_before = FALSE;

  if (!seen_before)
    {
      g_debug ("GoaAlarm: trying to use kernel timer");
      seen_before = TRUE;
    }

  fd = timerfd_create (CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);

  if (fd < 0)
    {
      g_debug ("GoaAlarm: could not create timer fd: %s", strerror (errno));
      return FALSE;
    }

  memset (&timer_spec, 0, sizeof (timer_spec));
  timer_spec.it_value.tv_sec = g_date_time_to_unix (self->time) + 1;

  result = timerfd_settime (fd,
                            TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
                            &timer_spec, NULL);

  if (result < 0)
    {
      g_debug ("GoaAlarm: could not set timer: %s", strerror (errno));
      return FALSE;
    }

  self->type = GOA_ALARM_TYPE_TIMER;
  self->stream = g_unix_input_stream_new (fd, TRUE);

  source = g_pollable_input_stream_create_source (G_POLLABLE_INPUT_STREAM (self->stream), NULL);
  self->scheduled_wakeup_source = source;
  g_source_set_callback (self->scheduled_wakeup_source,
                         (GSourceFunc) on_timer_source_ready, self,
                         (GDestroyNotify) clear_wakeup_source_pointer);
  g_source_attach (self->scheduled_wakeup_source, self->context);
  g_source_unref (source);

  return TRUE;

#endif /*HAVE_TIMERFD */

  return FALSE;
}

static gboolean
on_timeout_source_ready (GoaAlarm *self)
{
  g_return_val_if_fail (GOA_IS_ALARM (self), FALSE);

  g_rec_mutex_lock (&self->lock);

  if (self->type == GOA_ALARM_TYPE_UNSCHEDULED)
    goto out;

  fire_or_rearm_alarm (self);

  schedule_wakeups_with_timeout_source (self);

out:
  g_rec_mutex_unlock (&self->lock);
  return FALSE;
}

static void
clear_wakeup_source_pointer (GoaAlarm *self)
{
  self->scheduled_wakeup_source = NULL;
}

static void
schedule_wakeups_with_timeout_source (GoaAlarm *self)
{
  GDateTime *now;
  GSource   *source;
  GTimeSpan  time_span;
  guint      interval;

  self->type = GOA_ALARM_TYPE_TIMEOUT;

  now = g_date_time_new_now_local ();
  time_span = g_date_time_difference (self->time, now);
  g_date_time_unref (now);

  time_span =
    CLAMP (time_span, 1000 *G_TIME_SPAN_MILLISECOND,
           G_MAXUINT *G_TIME_SPAN_MILLISECOND);
  interval = (guint) time_span / G_TIME_SPAN_MILLISECOND;

  /* We poll every 10 seconds or so because we want to catch time skew
   */
  interval = MIN (interval, MAX_TIMEOUT_INTERVAL);

  source = g_timeout_source_new (interval);

  self->scheduled_wakeup_source = source;
  g_source_set_callback (self->scheduled_wakeup_source,
                         (GSourceFunc)
                         on_timeout_source_ready,
                         self, (GDestroyNotify) clear_wakeup_source_pointer);

  g_source_attach (self->scheduled_wakeup_source, self->context);
  g_source_unref (source);
}

static void
schedule_wakeups (GoaAlarm *self)
{
  gboolean wakeup_scheduled;

  wakeup_scheduled = schedule_wakeups_with_timerfd (self);

  if (!wakeup_scheduled)
    {
      static gboolean seen_before = FALSE;

      if (!seen_before)
        {
          g_debug ("GoaAlarm: falling back to polling timeout");
          seen_before = TRUE;
        }
      schedule_wakeups_with_timeout_source (self);
    }
}

static void
clear_immediate_wakeup_source_pointer (GoaAlarm *self)
{
  self->immediate_wakeup_source = NULL;
}

static void
schedule_immediate_wakeup (GoaAlarm *self)
{
  GSource *source;

  source = g_idle_source_new ();

  self->immediate_wakeup_source = source;
  g_source_set_callback (self->immediate_wakeup_source,
                         (GSourceFunc)
                         on_immediate_wakeup_source_ready,
                         self,
                         (GDestroyNotify) clear_immediate_wakeup_source_pointer);

  g_source_attach (self->immediate_wakeup_source, self->context);
  g_source_unref (source);
}

static void
goa_alarm_set_time (GoaAlarm *self, GDateTime *time)
{
  g_rec_mutex_lock (&self->lock);

  g_date_time_ref (time);
  self->time = time;

  if (self->context == NULL)
    self->context = g_main_context_ref (g_main_context_default ());

  schedule_wakeups (self);

  /* Wake up right away, in case it's already expired leaving the gate */
  schedule_immediate_wakeup (self);
  g_rec_mutex_unlock (&self->lock);
  g_object_notify (G_OBJECT (self), "time");
}

GDateTime *
goa_alarm_get_time (GoaAlarm *self)
{
  return self->time;
}

GoaAlarm *
goa_alarm_new (GDateTime *alarm_time)
{
  GoaAlarm *self;

  self = GOA_ALARM (g_object_new (GOA_TYPE_ALARM, "time", alarm_time, NULL));

  return GOA_ALARM (self);
}
