/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2010-2013 Collabora Ltd.
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Marco Barisione <marco.barisione@collabora.co.uk>
 */

/* This class makes sure we have a GOA account for each Telepathy account
 * configured in the system.
 * Note that this handles only plain Telepathy accounts; the ones with
 * multiple capabilities (e.g. Facebook) are handled differently. */

#include "config.h"

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

#include "goatpaccountlinker.h"
#include "goa/goa.h"
#include "goabackend/goalogging.h"

#define GOA_TP_ACCOUNT_LINKER_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOA_TYPE_TP_ACCOUNT_LINKER, \
                                GoaTpAccountLinkerPrivate))

G_DEFINE_TYPE (GoaTpAccountLinker, goa_tp_account_linker, G_TYPE_OBJECT)

struct _GoaTpAccountLinkerPrivate
{
  TpAccountManager *account_manager;
  GoaClient *goa_client;
};

static void
start_if_ready (GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;

  if (priv->goa_client == NULL ||
      priv->account_manager == NULL ||
      !tp_proxy_is_prepared (priv->account_manager,
        TP_ACCOUNT_MANAGER_FEATURE_CORE))
    {
      /* Not everything is ready yet */
      return;
    }

  goa_debug ("Both GOA and Tp are ready, starting tracking of accounts");
}

static void
account_manager_prepared_cb (GObject      *object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  GoaTpAccountLinker *self = user_data;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (object, res, &error))
    {
      goa_error ("Error preparing AM: %s", error->message);
      g_clear_error (&error);
      return;
    }

  goa_debug("Telepathy account manager prepared");
  start_if_ready (self);
}

static void
goa_client_new_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  GoaTpAccountLinker *self = user_data;
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GError *error = NULL;

  priv->goa_client = goa_client_new_finish (result, &error);
  if (priv->goa_client == NULL)
    {
      goa_error ("Error connecting to GOA: %s", error->message);
      g_clear_error (&error);
      return;
    }

  goa_debug("GOA client ready");
  start_if_ready (self);
}

static void
goa_tp_account_linker_dispose (GObject *object)
{
  GoaTpAccountLinker *self = GOA_TP_ACCOUNT_LINKER (object);
  GoaTpAccountLinkerPrivate *priv = self->priv;

  g_clear_object (&priv->account_manager);
  g_clear_object (&priv->goa_client);

  G_OBJECT_CLASS (goa_tp_account_linker_parent_class)->dispose (object);
}

static void
goa_tp_account_linker_init (GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv;

  goa_debug ("Starting GOA <-> Telepathy account linker");

  self->priv = GOA_TP_ACCOUNT_LINKER_GET_PRIVATE (self);
  priv = self->priv;

  priv->account_manager = tp_account_manager_dup ();
  tp_proxy_prepare_async (priv->account_manager, NULL,
      account_manager_prepared_cb, self);

  goa_client_new (NULL, goa_client_new_cb, self);
}

static void
goa_tp_account_linker_class_init (GoaTpAccountLinkerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (gobject_class,
      sizeof (GoaTpAccountLinkerPrivate));

  gobject_class->dispose = goa_tp_account_linker_dispose;
}

GoaTpAccountLinker *
goa_tp_account_linker_new (void)
{
  return g_object_new (GOA_TYPE_TP_ACCOUNT_LINKER, NULL);
}
