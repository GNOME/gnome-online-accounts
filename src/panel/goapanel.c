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

#include <string.h>
#include <glib/gi18n-lib.h>

#include <goa/goa.h>
#include <goa/goabackend.h>

#include "goapanel.h"
#include "goapanelaccountsmodel.h"

typedef struct _GoaPanelClass GoaPanelClass;

struct _GoaPanel
{
  CcPanel parent_instance;

  GtkBuilder *builder;

  GoaClient *client;

  GoaPanelAccountsModel *accounts_model;

  GtkWidget *toolbar;
  GtkWidget *toolbar_add_button;
  GtkWidget *accounts_treeview;
  GtkWidget *accounts_vbox;
};

struct _GoaPanelClass
{
  CcPanelClass parent_class;
};

static void on_tree_view_selection_changed (GtkTreeSelection *selection,
                                            gpointer          user_data);

static void on_toolbar_add_button_clicked (GtkToolButton *button,
                                           gpointer       user_data);

G_DEFINE_DYNAMIC_TYPE (GoaPanel, goa_panel, CC_TYPE_PANEL);

static void
goa_panel_finalize (GObject *object)
{
  GoaPanel *panel = GOA_PANEL (object);

  g_signal_handlers_disconnect_by_func (gtk_tree_view_get_selection (GTK_TREE_VIEW (panel->accounts_treeview)),
                                        G_CALLBACK (on_tree_view_selection_changed),
                                        panel);
  if (panel->accounts_model != NULL)
    g_object_unref (panel->accounts_model);
  if (panel->client != NULL)
    g_object_unref (panel->client);
  g_object_unref (panel->builder);

  G_OBJECT_CLASS (goa_panel_parent_class)->finalize (object);
}

static GtkWidget *
add_switch (GoaPanel    *panel,
            const gchar *hbox_id)
{
  GtkWidget *ret;
  ret = gtk_switch_new ();
  gtk_switch_set_active (GTK_SWITCH (ret), TRUE);
  gtk_box_pack_start (GTK_BOX (gtk_builder_get_object (panel->builder, hbox_id)),
                      ret, FALSE, TRUE, 0);
  return ret;
}

static void
goa_panel_init (GoaPanel *panel)
{
  GtkWidget *w;
  GError *error;
  GtkStyleContext *context;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;

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

  panel->toolbar = GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-toolbar"));
  panel->toolbar_add_button = GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-toolbutton-add"));
  g_signal_connect (panel->toolbar_add_button,
                    "clicked",
                    G_CALLBACK (on_toolbar_add_button_clicked),
                    panel);

  context = gtk_widget_get_style_context (GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-scrolledwindow")));
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
  context = gtk_widget_get_style_context (panel->toolbar);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_INLINE_TOOLBAR);
  gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (gtk_builder_get_object (panel->builder, "accounts-notebook")), FALSE);

  panel->accounts_treeview = GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-tree-treeview"));
  g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (panel->accounts_treeview)),
                    "changed",
                    G_CALLBACK (on_tree_view_selection_changed),
                    panel);

  /* Google Account */
  panel->accounts_vbox = GTK_WIDGET (gtk_builder_get_object (panel->builder, "accounts-vbox"));

  /* TODO: probably want to avoid _sync() ... */
  error = NULL;
  panel->client = goa_client_new_sync (NULL /* GCancellable */, &error);
  if (panel->client == NULL)
    {
      g_warning ("Error getting a GoaClient: %s (%s, %d)",
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  panel->accounts_model = goa_panel_accounts_model_new (panel->client);
  gtk_tree_view_set_model (GTK_TREE_VIEW (panel->accounts_treeview), GTK_TREE_MODEL (panel->accounts_model));

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (panel->accounts_treeview), column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, renderer, FALSE);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "markup", GOA_PANEL_ACCOUNTS_MODEL_COLUMN_NAME,
                                       NULL);

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_end (column, renderer, FALSE);
  g_object_set (G_OBJECT (renderer),
                "icon-name", "dialog-warning-symbolic",
                NULL);
  gtk_tree_view_column_set_attributes (column,
                                       renderer,
                                       "visible", GOA_PANEL_ACCOUNTS_MODEL_COLUMN_ATTENTION_NEEDED,
                                       NULL);

 out:
  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-top-widget"));
  /* TODO: not sure this is quite the right way to force minimum size */
  gtk_widget_set_size_request (w, 0, 400);
  gtk_widget_reparent (w, GTK_WIDGET (panel));
  gtk_widget_show_all (w);
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

/* ---------------------------------------------------------------------------------------------------- */

static void
show_page (GoaPanel *panel,
           gint page_num)
{
  GtkNotebook *notebook;
  notebook = GTK_NOTEBOOK (gtk_builder_get_object (panel->builder, "accounts-notebook"));
  gtk_notebook_set_current_page (notebook, page_num);
}

static void
show_page_nothing_selected (GoaPanel *panel)
{
  show_page (panel, 0);
}

static void
add_row (GtkWidget    *table,
         guint         row,
         const gchar  *key,
         const gchar  *value)
{
  GtkWidget *label;

  label = gtk_label_new (key);
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label,
                    0, 1,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
  label = gtk_label_new (value);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label,
                    1, 2,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
}

static void
on_info_bar_response (GtkInfoBar *info_bar,
                      gint        response_id,
                      gpointer    user_data)
{
  GoaPanel *panel = GOA_PANEL (user_data);
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (panel->accounts_treeview)),
                                       NULL,
                                       &iter))
    {
      GoaBackendProvider *provider;
      const gchar *provider_type;
      GoaAccount *account;
      GoaObject *object;
      GtkWindow *parent;
      GError *error;

      gtk_tree_model_get (GTK_TREE_MODEL (panel->accounts_model),
                          &iter,
                          GOA_PANEL_ACCOUNTS_MODEL_COLUMN_OBJECT, &object,
                          -1);

      account = goa_object_peek_account (object);
      provider_type = goa_account_get_account_type (account);
      provider = goa_backend_provider_get_for_provider_type (provider_type);

      parent = GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel))));

      error = NULL;
      if (!goa_backend_provider_refresh_account (provider,
                                                 panel->client,
                                                 object,
                                                 parent,
                                                 &error))
        {
          if (!(error->domain == GOA_ERROR && error->code == GOA_ERROR_DIALOG_DISMISSED))
            {
              GtkWidget *dialog;
              dialog = gtk_message_dialog_new (parent,
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_ERROR,
                                               GTK_BUTTONS_CLOSE,
                                               _("Error logging into the account"));
              gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                        "%s",
                                                        error->message);
              gtk_widget_show_all (dialog);
              gtk_dialog_run (GTK_DIALOG (dialog));
              gtk_widget_destroy (dialog);
            }
          g_error_free (error);
        }
      g_object_unref (provider);
      g_object_unref (object);
    }
}

static void
show_page_account (GoaPanel  *panel,
                   GoaObject *object)
{
  GList *children;
  GList *l;
  GtkWidget *table;
  GtkWidget *bar;
  GtkWidget *label;
  guint row;
  GoaBackendProvider *provider;
  GoaAccount *account;
  GoaGoogleAccount *gaccount;
  const gchar *provider_type;
  gchar *s;

  row = 0;
  provider = NULL;

  show_page (panel, 1);

  /* Out with the old */
  children = gtk_container_get_children (GTK_CONTAINER (panel->accounts_vbox));
  for (l = children; l != NULL; l = l->next)
    gtk_container_remove (GTK_CONTAINER (panel->accounts_vbox), GTK_WIDGET (l->data));
  g_list_free (children);

  account = goa_object_peek_account (object);
  gaccount = goa_object_peek_google_account (object);
  provider_type = goa_account_get_account_type (account);
  provider = goa_backend_provider_get_for_provider_type (provider_type);

  /* And in with the new */
  if (goa_account_get_attention_needed (account))
    {
      bar = gtk_info_bar_new ();
      label = gtk_label_new (_("The connection to this account has expired. Please log in again."));
      gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (bar))), label);
      if (provider != NULL)
        gtk_info_bar_add_button (GTK_INFO_BAR (bar), _("_Log In"), GTK_RESPONSE_OK);
      gtk_box_pack_start (GTK_BOX (panel->accounts_vbox), bar, FALSE, TRUE, 0);
      g_signal_connect (bar, "response", G_CALLBACK (on_info_bar_response), panel);
    }

  table = gtk_table_new (3, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 10);
  gtk_table_set_col_spacings (GTK_TABLE (table), 10);
  gtk_box_pack_start (GTK_BOX (panel->accounts_vbox), table, FALSE, TRUE, 0);

  label = gtk_label_new (NULL);
  s = g_strdup_printf ("<big><b>%s</b></big>", goa_account_get_name (account));
  gtk_label_set_markup (GTK_LABEL (label), s);
  g_free (s);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label,
                    1, 2,
                    row, row + 1,
                    GTK_FILL, GTK_FILL, 0, 0);
  row++;

  if (provider != NULL)
    {
      s = g_strdup (goa_backend_provider_get_name (provider));
    }
  else
    {
      /* Translators: Shown as "Account Type" when the UI doesn't support the provider type.
       * The %s is the provider type (e.g. 'google').
       */
      s = g_strdup_printf (_("%s (Unsupported)"), provider_type);
    }
  add_row (table, row++, _("Account Type"), s);
  g_free (s);

  if (gaccount != NULL)
    {
      add_row (table, row++, _("Email Address"), goa_google_account_get_email_address (gaccount));
    }

  gtk_widget_show_all (panel->accounts_vbox);

  if (provider != NULL)
    g_object_unref (provider);
}

static void
on_tree_view_selection_changed (GtkTreeSelection *selection,
                                gpointer          user_data)
{
  GoaPanel *panel = GOA_PANEL (user_data);
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      GoaObject *object;
      gtk_tree_model_get (GTK_TREE_MODEL (panel->accounts_model),
                          &iter,
                          GOA_PANEL_ACCOUNTS_MODEL_COLUMN_OBJECT, &object,
                          -1);
      show_page_account (panel, object);
      g_object_unref (object);
    }
  else
    {
      show_page_nothing_selected (panel);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_toolbar_add_button_clicked (GtkToolButton *button,
                               gpointer       user_data)
{
  GoaPanel *panel = GOA_PANEL (user_data);
  GtkWidget *cancel_button;
  GtkWidget *add_account_button;
  GtkWidget *dialog;
  GtkWidget *combo_box;
  GtkWidget *w;
  gint response;
  GList *providers;
  GoaBackendProvider *provider;
  GList *children;
  GList *l;
  GoaObject *object;
  GError *error;

  provider = NULL;
  providers = NULL;

  dialog = gtk_dialog_new ();
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_window_set_transient_for (GTK_WINDOW (dialog),
                                GTK_WINDOW (cc_shell_get_toplevel (cc_panel_get_shell (CC_PANEL (panel)))));

  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-top-widget"));
  gtk_widget_reparent (w, gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  /* Start at page 0 */
  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-notebook"));
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (w), FALSE);
  gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 0);

  /* Nuke children added during previous run */
  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-vbox"));
  children = gtk_container_get_children (GTK_CONTAINER (w));
  for (l = children; l != NULL; l = l->next)
    gtk_container_remove (GTK_CONTAINER (w), GTK_WIDGET (l->data));
  g_list_free (children);

  combo_box = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-combobox"));
  gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (combo_box));
  providers = goa_backend_provider_get_all ();
  for (l = providers; l != NULL; l = l->next)
    {
      GoaBackendProvider *provider = GOA_BACKEND_PROVIDER (l->data);
      gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box),
                                 goa_backend_provider_get_provider_type (provider),
                                 goa_backend_provider_get_name (provider));
    }
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

  cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
  add_account_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                              _("_Add account..."), GTK_RESPONSE_OK);

  combo_box = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-combobox"));

  gtk_widget_show_all (dialog);
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      gtk_widget_hide (dialog);
      goto out;
    }

  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-notebook"));
  gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 1);
  gtk_widget_hide (add_account_button);

  provider = goa_backend_provider_get_for_provider_type (gtk_combo_box_get_active_id (GTK_COMBO_BOX (combo_box)));
  g_assert (provider != NULL);

  error = NULL;
  object = goa_backend_provider_add_account (provider,
                                             panel->client,
                                             GTK_DIALOG (dialog),
                                             GTK_BOX (gtk_builder_get_object (panel->builder, "goa-add-account-vbox")),
                                             &error);
  if (object != NULL)
    {
      GtkTreeIter iter;
      /* navigate to newly created object */
      if (goa_panel_accounts_model_get_iter_for_object (panel->accounts_model,
                                                        object,
                                                        &iter))
        {
          gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (panel->accounts_treeview)),
                                          &iter);
        }
      g_object_unref (object);
      gtk_widget_hide (dialog);
    }
  else
    {
      if (!(error->domain == GOA_ERROR && error->code == GOA_ERROR_DIALOG_DISMISSED))
        {
          w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-error-label"));
          gtk_label_set_text (GTK_LABEL (w), error->message);
          w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-notebook"));
          gtk_notebook_set_current_page (GTK_NOTEBOOK (w), 2);
          gtk_widget_hide (cancel_button);
          gtk_dialog_add_button (GTK_DIALOG (dialog),
                                 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
          gtk_dialog_run (GTK_DIALOG (dialog));
        }
      g_error_free (error);
      gtk_widget_hide (dialog);
    }

 out:
  if (provider != NULL)
    g_object_unref (provider);
  g_list_foreach (providers, (GFunc) g_object_unref, NULL);
  g_list_free (providers);
}

