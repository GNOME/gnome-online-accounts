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

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goamailauth.h"

struct _GoaMailAuthPrivate
{
  GDataInputStream *input;
  GDataOutputStream *output;
};

enum
{
  PROP_0,
  PROP_INPUT,
  PROP_OUTPUT
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GoaMailAuth, goa_mail_auth, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
mail_auth_run_in_thread_func (GTask *task,
                              gpointer object,
                              G_GNUC_UNUSED gpointer task_data,
                              GCancellable *cancellable)
{
  GError *error;

  error = NULL;
  if (!goa_mail_auth_run_sync (GOA_MAIL_AUTH (object), cancellable, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mail_auth_starttls_in_thread_func (GTask *task,
                                   gpointer object,
                                   G_GNUC_UNUSED gpointer task_data,
                                   GCancellable *cancellable)
{
  GError *error;

  error = NULL;
  if (!goa_mail_auth_starttls_sync (GOA_MAIL_AUTH (object), cancellable, &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_auth_dispose (GObject *object)
{
  GoaMailAuth *self = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);

  g_clear_object (&priv->input);
  g_clear_object (&priv->output);

  G_OBJECT_CLASS (goa_mail_auth_parent_class)->dispose (object);
}

static void
goa_mail_auth_get_property (GObject      *object,
                            guint         prop_id,
                            GValue       *value,
                            GParamSpec   *pspec)
{
  GoaMailAuth *self = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_INPUT:
      g_value_set_object (value, priv->input);
      break;

    case PROP_OUTPUT:
      g_value_set_object (value, priv->output);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_mail_auth_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GoaMailAuth *self = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_INPUT:
      priv->input = g_value_dup_object (value);
      break;

    case PROP_OUTPUT:
      priv->output = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_auth_init (GoaMailAuth *self)
{
}

static void
goa_mail_auth_class_init (GoaMailAuthClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose      = goa_mail_auth_dispose;
  gobject_class->get_property = goa_mail_auth_get_property;
  gobject_class->set_property = goa_mail_auth_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_INPUT,
                                   g_param_spec_object ("input",
                                                        "input",
                                                        "input",
                                                        G_TYPE_DATA_INPUT_STREAM,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_OUTPUT,
                                   g_param_spec_object ("output",
                                                        "output",
                                                        "output",
                                                        G_TYPE_DATA_OUTPUT_STREAM,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_is_needed (GoaMailAuth *self)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (self), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (self)->is_needed (self);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_run_sync (GoaMailAuth         *self,
                        GCancellable        *cancellable,
                        GError             **error)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (self), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (self)->run_sync (self, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_mail_auth_run (GoaMailAuth         *self,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (GOA_IS_MAIL_AUTH (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_mail_auth_run);

  g_task_run_in_thread (task, mail_auth_run_in_thread_func);

  g_object_unref (task);
}

gboolean
goa_mail_auth_run_finish (GoaMailAuth         *self,
                          GAsyncResult        *res,
                          GError             **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_MAIL_AUTH (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_mail_auth_run, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_starttls_sync (GoaMailAuth         *self,
                             GCancellable        *cancellable,
                             GError             **error)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (self), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (self)->starttls_sync (self, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_mail_auth_starttls (GoaMailAuth         *self,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (GOA_IS_MAIL_AUTH (self));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_mail_auth_starttls);

  g_task_run_in_thread (task, mail_auth_starttls_in_thread_func);

  g_object_unref (task);
}

gboolean
goa_mail_auth_starttls_finish (GoaMailAuth         *self,
                               GAsyncResult        *res,
                               GError             **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_MAIL_AUTH (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_mail_auth_starttls, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

GDataInputStream *
goa_mail_auth_get_input (GoaMailAuth *self)
{
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);
  return priv->input;
}

void
goa_mail_auth_set_input (GoaMailAuth      *self,
                         GDataInputStream *input)
{
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);

  if (priv->input == input)
    return;

  g_clear_object (&priv->input);
  priv->input = g_object_ref (input);
  g_object_notify (G_OBJECT (self), "input");
}

GDataOutputStream *
goa_mail_auth_get_output (GoaMailAuth  *self)
{
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);
  return priv->output;
}

void
goa_mail_auth_set_output (GoaMailAuth       *self,
                          GDataOutputStream *output)
{
  GoaMailAuthPrivate *priv;

  priv = goa_mail_auth_get_instance_private (self);

  if (priv->output == output)
    return;

  g_clear_object (&priv->output);
  priv->output = g_object_ref (output);
  g_object_notify (G_OBJECT (self), "output");
}
