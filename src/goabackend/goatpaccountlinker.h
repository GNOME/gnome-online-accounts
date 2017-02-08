/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2010 – 2013 Collabora Ltd.
 * Copyright © 2013 Intel Corporation
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

#ifndef __GOA_TP_ACCOUNT_LINKER_H__
#define __GOA_TP_ACCOUNT_LINKER_H__

#include <glib-object.h>

#include "goa/goa.h"

G_BEGIN_DECLS

#define GOA_TYPE_TP_ACCOUNT_LINKER           (goa_tp_account_linker_get_type ())
#define GOA_TP_ACCOUNT_LINKER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_TP_ACCOUNT_LINKER, GoaTpAccountLinker))
#define GOA_TP_ACCOUNT_LINKER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), GOA_TYPE_TP_ACCOUNT_LINKER, GoaTpAccountLinkerClass))
#define GOA_IS_TP_ACCOUNT_LINKER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_TP_ACCOUNT_LINKER))
#define GOA_IS_TP_ACCOUNT_LINKER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), GOA_TYPE_TP_ACCOUNT_LINKER))
#define GOA_TP_ACCOUNT_LINKER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GOA_TYPE_TP_ACCOUNT_LINKER, GoaTpAccountLinkerClass))

typedef struct _GoaTpAccountLinker        GoaTpAccountLinker;
typedef struct _GoaTpAccountLinkerClass   GoaTpAccountLinkerClass;
typedef struct _GoaTpAccountLinkerPrivate GoaTpAccountLinkerPrivate;

struct _GoaTpAccountLinker
{
  GObject parent_instance;
  GoaTpAccountLinkerPrivate *priv;
};

struct _GoaTpAccountLinkerClass
{
  GObjectClass parent_class;
};

GType               goa_tp_account_linker_get_type      (void) G_GNUC_CONST;
GoaTpAccountLinker *goa_tp_account_linker_new           (void);
void                goa_tp_account_linker_remove_tp_account        (GoaTpAccountLinker   *self,
                                                                    GoaObject            *object,
                                                                    GCancellable         *cancellable,
                                                                    GAsyncReadyCallback   callback,
                                                                    gpointer              user_data);
gboolean            goa_tp_account_linker_remove_tp_account_finish (GoaTpAccountLinker   *self,
                                                                    GAsyncResult         *res,
                                                                    GError              **error);

G_END_DECLS

#endif
