/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2025 GNOME Foundation Inc.
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

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <adwaita.h>

#include "goaauthflowwidget.h"

/**
 * GoaAuthflowWidget:
 *
 * A composite widget for authflow links that can be launched,
 * or copied to the clipboard.
 */
struct _GoaAuthflowWidget
{
  GtkBox parent_instance;

  /* widgets */
  GtkWidget *copy_button;
  GtkWidget *link_button;
};

G_DEFINE_TYPE (GoaAuthflowWidget, goa_authflow_widget, GTK_TYPE_BOX)

enum {
  COPY_ACTIVATED,
  LINK_ACTIVATED,
  N_SIGNALS,
};

static unsigned int signals[N_SIGNALS] = { 0, };

static void
on_button_activated (GoaAuthflowWidget *self,
                     GtkWidget         *button)
{
  g_assert (GOA_IS_AUTHFLOW_WIDGET (self));

  if (self->copy_button == button)
    g_signal_emit (G_OBJECT (self), signals[COPY_ACTIVATED], 0);
  else if (self->link_button == button)
    g_signal_emit (G_OBJECT (self), signals[LINK_ACTIVATED], 0);
}

static void
goa_authflow_widget_init (GoaAuthflowWidget *self)
{
  GtkWidget *buttons;
  GtkWidget *copy_desc;

  g_object_set (self,
                "orientation", GTK_ORIENTATION_VERTICAL,
                "spacing", 18,
                NULL);

  buttons = gtk_list_box_new ();
  gtk_widget_add_css_class (buttons, "boxed-list-separate");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (buttons), GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (self), buttons);

  /* The standard link button; emits link-activated
   */
  self->link_button = g_object_new (ADW_TYPE_BUTTON_ROW,
                                    "title", _("_Sign In…"),
                                    "use-underline", TRUE,
                                    NULL);
  gtk_widget_add_css_class (self->link_button, "suggested-action");
  g_signal_connect_object (self->link_button,
                           "activated",
                           G_CALLBACK (on_button_activated),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_list_box_append (GTK_LIST_BOX (buttons), self->link_button);

  /* The copy-link button; emits copy-activated
   */
  self->copy_button = g_object_new (ADW_TYPE_BUTTON_ROW,
                                    "title", _("_Copy Link"),
                                    "use-underline", TRUE,
                                    "start-icon-name", "edit-copy-symbolic",
                                    NULL);
  g_signal_connect_object (self->copy_button,
                           "activated",
                           G_CALLBACK (on_button_activated),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_list_box_append (GTK_LIST_BOX (buttons), self->copy_button);

  copy_desc = gtk_label_new (_("Copy the authorization URL to continue with a specific web browser."));
  gtk_label_set_justify (GTK_LABEL (copy_desc), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (copy_desc), TRUE);
  gtk_widget_set_halign (copy_desc, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (copy_desc, "caption");
  gtk_accessible_update_relation (GTK_ACCESSIBLE (self->copy_button),
                                  GTK_ACCESSIBLE_RELATION_DESCRIBED_BY, copy_desc, NULL,
                                  -1);
  gtk_box_append (GTK_BOX (self), copy_desc);
}

static void
goa_authflow_widget_class_init (GoaAuthflowWidgetClass *klass)
{
  signals[COPY_ACTIVATED] =
    g_signal_new ("copy-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  signals[LINK_ACTIVATED] =
    g_signal_new ("link-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

/**
 * goa_authflow_widget_new:
 * @provider: a `GoaProvider`
 * @config: (nullable): a service config
 *
 * Create a link button widget.
 *
 * Returns: a `GtkWidget`
 */
GtkWidget *
goa_authflow_widget_new (void)
{
  return g_object_new (GOA_TYPE_AUTHFLOW_WIDGET, NULL);
}

