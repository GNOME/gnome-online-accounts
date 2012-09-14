/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Author: Debarshi Ray <debarshir@gnome.org>
 */

#ifndef __GOA_SPINNER_BUTTON_H__
#define __GOA_SPINNER_BUTTON_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GOA_TYPE_SPINNER_BUTTON         (goa_spinner_button_get_type ())
#define GOA_SPINNER_BUTTON(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_SPINNER_BUTTON, GoaSpinnerButton))
#define GOA_SPINNER_BUTTON_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_SPINNER_BUTTON, GoaSpinnerButtonClass))
#define GOA_IS_SPINNER_BUTTON(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_SPINNER_BUTTON))
#define GOA_IS_SPINNER_BUTTON_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_SPINNER_BUTTON))
#define GOA_SPINNER_BUTTON_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_SPINNER_BUTTON, GoaSpinnerButtonClass))

typedef struct _GoaSpinnerButton        GoaSpinnerButton;
typedef struct _GoaSpinnerButtonClass   GoaSpinnerButtonClass;
typedef struct _GoaSpinnerButtonPrivate GoaSpinnerButtonPrivate;

struct _GoaSpinnerButton
{
  /*<private>*/
  GtkButton parent_instance;
  GoaSpinnerButtonPrivate *priv;
};

struct _GoaSpinnerButtonClass
{
  GtkButtonClass parent_class;
};

GType        goa_spinner_button_get_type              (void) G_GNUC_CONST;
GtkWidget   *goa_spinner_button_new_from_stock        (const gchar *stock_id);
void         goa_spinner_button_set_label             (GoaSpinnerButton *self, const gchar *label);
void         goa_spinner_button_start                 (GoaSpinnerButton *self);
void         goa_spinner_button_stop                  (GoaSpinnerButton *self);

G_END_DECLS

#endif /* _GOA_SPINNER_BUTTON_H_ */
