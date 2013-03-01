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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goamailauth.h"

G_DEFINE_ABSTRACT_TYPE (GoaMailAuth, goa_mail_auth, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GAsyncReadyCallback callback;
  GDataInputStream *input;
  GDataOutputStream *output;
  gpointer user_data;
} RunData;

static void
mail_auth_run_data_free (RunData *data)
{
  g_object_unref (data->input);
  g_object_unref (data->output);
  g_slice_free (RunData, data);
}

static RunData *
mail_auth_run_data_new (GDataInputStream *input,
                        GDataOutputStream *output,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
  RunData *data;

  data = g_slice_new0 (RunData);
  data->input = g_object_ref (input);
  data->output = g_object_ref (output);
  data->callback = callback;
  data->user_data = user_data;

  return data;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
mail_auth_run_async_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  RunData *data = user_data;

  if (data->callback != NULL)
    data->callback (source_object, res, data->user_data);

  mail_auth_run_data_free (data);
}

static void
mail_auth_run_in_thread_func (GSimpleAsyncResult *res, GObject *object, GCancellable *cancellable)
{
  GError *error;
  RunData *data;
  gboolean op_res;

  data = (RunData *) g_async_result_get_user_data (G_ASYNC_RESULT (res));
  op_res = FALSE;

  error = NULL;
  if (!goa_mail_auth_run_sync (GOA_MAIL_AUTH (object), data->input, data->output, cancellable, &error))
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
goa_mail_auth_init (GoaMailAuth *client)
{
}

static void
goa_mail_auth_class_init (GoaMailAuthClass *klass)
{
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
                        GDataInputStream    *input,
                        GDataOutputStream   *output,
                        GCancellable        *cancellable,
                        GError             **error)
{
  g_return_val_if_fail (GOA_IS_MAIL_AUTH (auth), FALSE);
  g_return_val_if_fail (G_IS_DATA_INPUT_STREAM (input), FALSE);
  g_return_val_if_fail (G_IS_DATA_OUTPUT_STREAM (output), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  return GOA_MAIL_AUTH_GET_CLASS (auth)->run_sync (auth, input, output, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_mail_auth_run (GoaMailAuth         *auth,
                   GDataInputStream    *input,
                   GDataOutputStream   *output,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GSimpleAsyncResult *simple;
  RunData *data;

  g_return_if_fail (GOA_IS_MAIL_AUTH (auth));
  g_return_if_fail (G_IS_DATA_INPUT_STREAM (input));
  g_return_if_fail (G_IS_DATA_OUTPUT_STREAM (output));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  data = mail_auth_run_data_new (input, output, callback, user_data);

  simple = g_simple_async_result_new (G_OBJECT (auth), mail_auth_run_async_cb, data, goa_mail_auth_run);
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
