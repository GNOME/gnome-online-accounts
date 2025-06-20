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

#include "goaauthflowbutton.h"

/**
 * GoaAuthflowButton:
 *
 * A composite widget for authflow links (e.g. OAuth) that can be launched,
 * or copied to the clipboard.
 */
struct _GoaAuthflowButton
{
  GtkBox parent_instance;

  unsigned int active : 1;

  /* widgets */
  GtkWidget *stack;
  GtkWidget *copy_button;
  GtkWidget *link_button;
};

G_DEFINE_TYPE (GoaAuthflowButton, goa_authflow_button, GTK_TYPE_BOX)

typedef enum
{
  PROP_ACTIVE = 1,
} GoaAuthflowButtonProperty;

static GParamSpec *properties[PROP_ACTIVE + 1] = { NULL, };

enum {
  COPY_ACTIVATED,
  LINK_ACTIVATED,
  N_SIGNALS,
};

static unsigned int signals[N_SIGNALS] = { 0, };

static void
on_button_activated (GoaAuthflowButton *self,
                     GtkWidget         *button)
{
  g_assert (GOA_IS_AUTHFLOW_BUTTON (self));

  if (self->copy_button == button)
    g_signal_emit (G_OBJECT (self), signals[COPY_ACTIVATED], 0);
  else if (self->link_button == button)
    g_signal_emit (G_OBJECT (self), signals[LINK_ACTIVATED], 0);
}

static void
goa_authflow_button_activate (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *parameter)
{
  g_signal_emit (G_OBJECT (widget), signals[LINK_ACTIVATED], 0);
}

static void
goa_authflow_button_get_property (GObject      *object,
                                  unsigned int  property_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  GoaAuthflowButton *self = GOA_AUTHFLOW_BUTTON (object);

  switch ((GoaAuthflowButtonProperty) property_id)
    {
    case PROP_ACTIVE:
      g_value_set_boolean (value, self->active);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
goa_authflow_button_set_property (GObject      *object,
                                  unsigned int  prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GoaAuthflowButton *self = GOA_AUTHFLOW_BUTTON (object);

  switch ((GoaAuthflowButtonProperty) prop_id)
    {
    case PROP_ACTIVE:
      goa_authflow_button_set_active (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_authflow_button_init (GoaAuthflowButton *self)
{
  GtkWidget *button_box;
  GtkWidget *button_list;
  GtkWidget *copy_desc;
  GtkWidget *spinner;

  self->stack = g_object_new (GTK_TYPE_STACK,
                              "transition-type", GTK_STACK_TRANSITION_TYPE_CROSSFADE,
                              "vhomogeneous", TRUE,
                              NULL);
  gtk_box_append (GTK_BOX (self), self->stack);

  button_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
  gtk_stack_add_named (GTK_STACK (self->stack), button_box, "inactive");

  button_list = gtk_list_box_new ();
  gtk_widget_add_css_class (button_list, "boxed-list-separate");
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (button_list), GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (button_box), button_list);

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
  gtk_list_box_append (GTK_LIST_BOX (button_list), self->link_button);

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
  gtk_list_box_append (GTK_LIST_BOX (button_list), self->copy_button);

  copy_desc = gtk_label_new (_("Copy the authorization URL to continue with a specific web browser."));
  gtk_label_set_justify (GTK_LABEL (copy_desc), GTK_JUSTIFY_CENTER);
  gtk_label_set_wrap (GTK_LABEL (copy_desc), TRUE);
  gtk_widget_set_halign (copy_desc, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (copy_desc, "caption");
  gtk_accessible_update_relation (GTK_ACCESSIBLE (self->copy_button),
                                  GTK_ACCESSIBLE_RELATION_DESCRIBED_BY, copy_desc, NULL,
                                  -1);
  gtk_box_append (GTK_BOX (button_box), copy_desc);

  /* Activity spinner
   */
  spinner = g_object_new (ADW_TYPE_SPINNER,
                          "height-request", 32,
                          "valign", GTK_ALIGN_CENTER,
                          NULL);
  gtk_stack_add_named (GTK_STACK (self->stack), spinner, "active");
}

static void
goa_authflow_button_class_init (GoaAuthflowButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = goa_authflow_button_get_property;
  object_class->set_property = goa_authflow_button_set_property;

  properties[PROP_ACTIVE] =
    g_param_spec_boolean ("active", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_STATIC_STRINGS |
                           G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);

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

  gtk_widget_class_install_action (widget_class,
                                   "default.activate",
                                   NULL,
                                   goa_authflow_button_activate);
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

/**
 * goa_authflow_button_new:
 *
 * Create a link button widget.
 *
 * Returns: a `GtkWidget`
 */
GtkWidget *
goa_authflow_button_new (void)
{
  return g_object_new (GOA_TYPE_AUTHFLOW_BUTTON, NULL);
}

/**
 * goa_authflow_button_get_active:
 * @widget: a `GoaAuthflowButton`
 *
 * Get the active state of @widget.
 *
 * Returns: %TRUE if the spinner is shown, %FALSE otherwise
 */
gboolean
goa_authflow_button_get_active (GoaAuthflowButton *widget)
{
  g_return_val_if_fail (GOA_IS_AUTHFLOW_BUTTON (widget), FALSE);

  return widget->active;
}

/**
 * goa_authflow_button_set_active:
 * @widget: a `GoaAuthflowButton`
 * @active: whether to show the spinner
 *
 * Set the activate state of @widget.
 *
 * If @active is %TRUE, the spinner will be shown, otherwise the link buttons.
 */
void
goa_authflow_button_set_active (GoaAuthflowButton *widget,
                                gboolean           active)
{
  g_return_if_fail (GOA_IS_AUTHFLOW_BUTTON (widget));

  active = !!active;
  if (widget->active != active)
    {
      gtk_stack_set_visible_child_name (GTK_STACK (widget->stack), active ? "active" : "inactive");
      widget->active = active;
      g_object_notify_by_pspec (G_OBJECT (widget), properties[PROP_ACTIVE]);
    }
}

