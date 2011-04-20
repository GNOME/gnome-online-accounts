/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 * Author: David Zeuthen <davidz@redhat.com>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goapanel.h"

typedef struct _GoaPanelClass GoaPanelClass;

struct _GoaPanel
{
  CcPanel parent_instance;

  GtkBuilder *builder;
};

struct _GoaPanelClass
{
  CcPanelClass parent_class;
};

G_DEFINE_DYNAMIC_TYPE (GoaPanel, goa_panel, CC_TYPE_PANEL);

static void
goa_panel_finalize (GObject *object)
{
  GoaPanel *panel = GOA_PANEL (object);

  g_object_unref (panel->builder);

  G_OBJECT_CLASS (goa_panel_parent_class)->finalize (object);
}

static void
goa_panel_init (GoaPanel *panel)
{
  GtkWidget *w;
  GError *error;
  GtkStyleContext *context;

  panel->builder = gtk_builder_new ();
  error = NULL;
  if (gtk_builder_add_from_file (panel->builder,
                                 PACKAGE_DATA_DIR "/goa/goapanel.ui",
                                 &error) == 0)
    {
      g_warning ("Error loading UI file: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-scrolledwindow")));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-toolbar")));
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-top-widget"));
  /* TODO: not sure this is quite the right way to force minimum size */
  gtk_widget_set_size_request (w, 0, 400);
  gtk_widget_reparent (w, GTK_WIDGET (panel));
  gtk_widget_show_all (w);

 out:
  ;
}

static void
goa_panel_class_init (GoaPanelClass *klass)
{
}

static void
goa_panel_class_finalize (GoaPanelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = goa_panel_finalize;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_panel_register (GIOModule *module)
{
  goa_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  GOA_TYPE_PANEL,
                                  "goa",
                                  0);
}

void
g_io_module_load (GIOModule *module)
{
  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  goa_panel_register (module);
}

void
g_io_module_unload (GIOModule *module)
{
}
