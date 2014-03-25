/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2013 Red Hat, Inc.
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

G_DEFINE_ABSTRACT_TYPE (GoaMailAuth, goa_mail_auth, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

static void
mail_auth_run_in_thread_func (GSimpleAsyncResult *res, GObject *object, GCancellable *cancellable)
{
  GError *error;
  gboolean op_res;

  op_res = FALSE;

  error = NULL;
  if (!goa_mail_auth_run_sync (GOA_MAIL_AUTH (object), cancellable, &error))
    {
      g_simple_async_result_take_error (res, error);
      goto out;
    }

  op_res = TRUE;

 out:
  g_simple_async_result_set_op_res_gboolean (res, op_res);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mail_auth_starttls_in_thread_func (GSimpleAsyncResult *res, GObject *object, GCancellable *cancellable)
{
  GError *error;
  gboolean op_res;

  op_res = FALSE;

  error = NULL;
  if (!goa_mail_auth_starttls_sync (GOA_MAIL_AUTH (object), cancellable, &error))
    {
      g_simple_async_result_take_error (res, error);
      goto out;
    }

  op_res = TRUE;

 out:
  g_simple_async_result_set_op_res_gboolean (res, op_res);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_mail_auth_dispose (GObject *object)
{
  GoaMailAuth *auth = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv = auth->priv;

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
  GoaMailAuth *auth = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv = auth->priv;

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
  GoaMailAuth *auth = GOA_MAIL_AUTH (object);
  GoaMailAuthPrivate *priv = auth->priv;

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
goa_mail_auth_init (GoaMailAuth *client)
{
  client->priv = G_TYPE_INSTANCE_GET_PRIVATE (client, GOA_TYPE_MAIL_AUTH, GoaMailAuthPrivate);
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

  g_type_class_add_private (klass, sizeof (GoaMailAuthPrivate));
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_is_needed (GoaMailAuth *auth)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (auth), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (auth)->is_needed (auth);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_run_sync (GoaMailAuth         *auth,
                        GCancellable        *cancellable,
                        GError             **error)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (auth), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (auth)->run_sync (auth, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_mail_auth_run (GoaMailAuth         *auth,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GSimpleAsyncResult *simple;

  g_return_if_fail (GOA_IS_MAIL_AUTH (auth));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  simple = g_simple_async_result_new (G_OBJECT (auth), callback, user_data, goa_mail_auth_run);
  g_simple_async_result_set_handle_cancellation (simple, TRUE);

  g_simple_async_result_run_in_thread (simple, mail_auth_run_in_thread_func, G_PRIORITY_DEFAULT, cancellable);

  g_object_unref (simple);
}

gboolean
goa_mail_auth_run_finish (GoaMailAuth         *auth,
                          GAsyncResult        *res,
                          GError             **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (auth), goa_mail_auth_run), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (res);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return g_simple_async_result_get_op_res_gboolean (simple);
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_mail_auth_starttls_sync (GoaMailAuth         *auth,
                             GCancellable        *cancellable,
                             GError             **error)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (auth), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (auth)->starttls_sync (auth, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_mail_auth_starttls (GoaMailAuth         *auth,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  GSimpleAsyncResult *simple;

  g_return_if_fail (GOA_IS_MAIL_AUTH (auth));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  simple = g_simple_async_result_new (G_OBJECT (auth), callback, user_data, goa_mail_auth_starttls);
  g_simple_async_result_set_handle_cancellation (simple, TRUE);

  g_simple_async_result_run_in_thread (simple,
                                       mail_auth_starttls_in_thread_func,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);

  g_object_unref (simple);
}

gboolean
goa_mail_auth_starttls_finish (GoaMailAuth         *auth,
                               GAsyncResult        *res,
                               GError             **error)
{
  GSimpleAsyncResult *simple;

  g_return_val_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (auth), goa_mail_auth_starttls), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  simple = G_SIMPLE_ASYNC_RESULT (res);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  return g_simple_async_result_get_op_res_gboolean (simple);
}

/* ---------------------------------------------------------------------------------------------------- */

GDataInputStream *
goa_mail_auth_get_input (GoaMailAuth *auth)
{
  return auth->priv->input;
}

void
goa_mail_auth_set_input (GoaMailAuth      *auth,
                         GDataInputStream *input)
{
  GoaMailAuthPrivate *priv = auth->priv;

  if (priv->input == input)
    return;

  g_clear_object (&priv->input);
  priv->input = g_object_ref (input);
  g_object_notify (G_OBJECT (auth), "input");
}

GDataOutputStream *
goa_mail_auth_get_output (GoaMailAuth  *auth)
{
  return auth->priv->output;
}

void
goa_mail_auth_set_output (GoaMailAuth       *auth,
                          GDataOutputStream *output)
{
  GoaMailAuthPrivate *priv = auth->priv;

  if (priv->output == output)
    return;

  g_clear_object (&priv->output);
  priv->output = g_object_ref (output);
  g_object_notify (G_OBJECT (auth), "output");
}
