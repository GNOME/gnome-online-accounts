/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
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

#include "config.h"

#include <glib.h>

#include "goaspinnerbutton.h"

struct _GoaSpinnerButtonPrivate
{
  GtkWidget *grid;
  GtkWidget *label;
  GtkWidget *spinner;
  gboolean use_stock;
  gchar *text;
};

enum
{
  PROP_0,
  PROP_LABEL,
  PROP_USE_STOCK
};

#define GOA_SPINNER_BUTTON_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOA_TYPE_SPINNER_BUTTON, GoaSpinnerButtonPrivate))

G_DEFINE_TYPE (GoaSpinnerButton, goa_spinner_button, GTK_TYPE_BUTTON)

static void
goa_spinner_button_constructed (GObject *object)
{
  GoaSpinnerButton *self = GOA_SPINNER_BUTTON (object);
  GoaSpinnerButtonPrivate *priv = self->priv;
  GtkStockItem item;

  G_OBJECT_CLASS (goa_spinner_button_parent_class)->constructed (object);

  if (priv->use_stock && gtk_stock_lookup (priv->text, &item))
    priv->label = gtk_label_new_with_mnemonic (item.label);
  else
    priv->label = gtk_label_new_with_mnemonic (priv->text);

  gtk_widget_set_hexpand (priv->label, TRUE);
  gtk_widget_set_halign (priv->label, GTK_ALIGN_CENTER);

  gtk_label_set_mnemonic_widget (GTK_LABEL (priv->label), GTK_WIDGET (self));
  gtk_container_add (GTK_CONTAINER (priv->grid), priv->label);
}

static void
goa_spinner_button_finalize (GObject *object)
{
  GoaSpinnerButton *self = GOA_SPINNER_BUTTON (object);
  GoaSpinnerButtonPrivate *priv = self->priv;

  g_free (priv->text);

  G_OBJECT_CLASS (goa_spinner_button_parent_class)->finalize (object);
}

static void
goa_spinner_button_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GoaSpinnerButton *self = GOA_SPINNER_BUTTON (object);
  GoaSpinnerButtonPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_LABEL:
      priv->text = g_value_dup_string (value);
      break;

    case PROP_USE_STOCK:
      priv->use_stock = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_spinner_button_init (GoaSpinnerButton *self)
{
  GoaSpinnerButtonPrivate *priv;

  self->priv = GOA_SPINNER_BUTTON_GET_PRIVATE (self);
  priv = self->priv;

  gtk_widget_set_can_default (GTK_WIDGET (self), TRUE);

  priv->grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (priv->grid), 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (priv->grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add (GTK_CONTAINER (self), priv->grid);

  priv->spinner = gtk_spinner_new ();
  gtk_widget_set_no_show_all (priv->spinner, TRUE);
  gtk_container_add (GTK_CONTAINER (priv->grid), priv->spinner);
}

static void
goa_spinner_button_class_init (GoaSpinnerButtonClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->constructed = goa_spinner_button_constructed;
  object_class->finalize = goa_spinner_button_finalize;
  object_class->set_property = goa_spinner_button_set_property;

  g_object_class_install_property (object_class, PROP_LABEL,
          g_param_spec_string ("label",
                               "Label",
                               "Text of the label widget inside the button, if the button contains a label widget.",
                               NULL,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_object_class_install_property (object_class, PROP_USE_STOCK,
          g_param_spec_boolean ("use-stock",
                                "Use a stock item",
                                "If set, the label is used to pick a stock item instead of being displayed.",
                                FALSE,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));

  g_type_class_add_private (object_class, sizeof (GoaSpinnerButtonPrivate));
}

GtkWidget *
goa_spinner_button_new_from_stock (const gchar *stock_id)
{
  return g_object_new (GOA_TYPE_SPINNER_BUTTON, "label", stock_id, "use-stock", TRUE, NULL);
}

void
goa_spinner_button_set_label (GoaSpinnerButton *self, const gchar *label)
{
  GoaSpinnerButtonPrivate *priv = self->priv;

  priv->use_stock = FALSE;

  g_free (priv->text);
  priv->text = g_strdup (label);
  gtk_label_set_use_underline (GTK_LABEL (priv->label), TRUE);
  gtk_label_set_label (GTK_LABEL (priv->label), priv->text);
}

void
goa_spinner_button_start (GoaSpinnerButton *self)
{
  GoaSpinnerButtonPrivate *priv = self->priv;

  gtk_spinner_start (GTK_SPINNER (priv->spinner));
  gtk_widget_show (priv->spinner);
}

void
goa_spinner_button_stop (GoaSpinnerButton *self)
{
  GoaSpinnerButtonPrivate *priv = self->priv;

  gtk_spinner_stop (GTK_SPINNER (priv->spinner));
  gtk_widget_hide (priv->spinner);
}
