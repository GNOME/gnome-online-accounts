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

  GtkWidget *google_account_name;
  GtkWidget *google_email_address;
  GtkWidget *google_mail_switch;
  GtkWidget *google_chat_switch;
  GtkWidget *google_contacts_switch;
  GtkWidget *google_calendar_switch;
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
  panel->google_account_name = GTK_WIDGET (gtk_builder_get_object (panel->builder, "google-account-name"));
  panel->google_email_address = GTK_WIDGET (gtk_builder_get_object (panel->builder, "google-email-address"));
  panel->google_mail_switch = add_switch (panel, "google-mail-hbox");
  panel->google_chat_switch = add_switch (panel, "google-chat-hbox");
  panel->google_contacts_switch = add_switch (panel, "google-contacts-hbox");
  panel->google_calendar_switch = add_switch (panel, "google-calendar-hbox");

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
show_page_no_ui (GoaPanel *panel)
{
  show_page (panel, 1);
}

static void
show_page_google_account (GoaPanel    *panel,
                          GDBusObject *object)
{
  GoaAccount *account;
  GoaGoogleAccount *google_account;
  gchar *s;

  account = GOA_PEEK_ACCOUNT (object);
  google_account = GOA_PEEK_GOOGLE_ACCOUNT (object);

  show_page (panel, 2);
  gtk_label_set_text (GTK_LABEL (panel->google_email_address),
                      goa_google_account_get_email_address (google_account));
  s = g_strdup_printf ("<big><b>%s</b></big>", goa_account_get_name (account));
  gtk_label_set_markup (GTK_LABEL (panel->google_account_name), s);
  g_free (s);

  /* TODO: subscribe to changes etc. */
}

static void
on_tree_view_selection_changed (GtkTreeSelection *selection,
                                gpointer          user_data)
{
  GoaPanel *panel = GOA_PANEL (user_data);
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      GDBusObject *object;

      gtk_tree_model_get (GTK_TREE_MODEL (panel->accounts_model),
                          &iter,
                          GOA_PANEL_ACCOUNTS_MODEL_COLUMN_DBUS_OBJECT, &object,
                          -1);

      if (GOA_PEEK_GOOGLE_ACCOUNT (object))
        {
          show_page_google_account (panel, object);
        }
      else
        {
          show_page_no_ui (panel);
        }
      g_object_unref (object);
    }
  else
    {
      show_page_nothing_selected (panel);
    }

  g_debug ("selection changed");
}

/* ---------------------------------------------------------------------------------------------------- */

#include <webkit/webkit.h>

static void
on_web_view_notify_title (GObject     *object,
                          GParamSpec  *pspec,
                          gpointer     user_data)
{
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  const gchar *title;

  title = webkit_web_view_get_title (web_view);
  if (title != NULL && g_str_has_prefix (title, "Success "))
    {
      const gchar *code = title + sizeof "Success " - 1;
      g_print ("Yay, authz code is `%s'\n", code);
    }
}

static void
add_google_account (GoaPanel   *panel,
                    GtkWidget  *dialog,
                    GtkWidget  *vbox)
{
  SoupSession *session;
  SoupCookieJar *cookie_jar;
  GtkWidget *scrolled_window;
  GtkWidget *web_view;
  const gchar *client_id;
  const gchar *scope;
  gchar *escaped_scope;
  gchar *url;

  /* The upstream client id is for GNOME
   *
   *  108305240709-9tncnurl91sh2i0isqnpc7l397sojst2.apps.googleusercontent.com
   *
   * and is managed by David Zeuthen <zeuthen@gmail.com>. Distributors
   * may change this to their own client id (or not?). TODO: maybe
   * make client_id a configure option in case downstream distributors
   * want to change it.
   */
  client_id = "108305240709-9tncnurl91sh2i0isqnpc7l397sojst2.apps.googleusercontent.com";
  scope =
    "https://www.googleapis.com/auth/userinfo#email " /* view email address */
    "https://mail.google.com/mail/feed/atom "         /* email */
    "https://www.google.com/m8/feeds "                /* contacts */
    "https://www.google.com/calendar/feeds "          /* calendar */
    "https://picasaweb.google.com/data "              /* picassa */
    ;
  // Use Lattitude? "https://www.googleapis.com/auth/latitude"

  escaped_scope = g_uri_escape_string (scope, NULL, TRUE);
  url = g_strdup_printf ("https://accounts.google.com/o/oauth2/auth"
                         "?response_type=code"
                         /* use p.fd.o until https://groups.google.com/group/oauth2-dev/browse_thread/thread/e0016525aa6a2d4f is resolved */
                         "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
                         "&client_id=%s"
                         "&scope=%s",
                         client_id,
                         escaped_scope);

  /* Ensure we use an empty non-persistent cookie to avoid login
   * credentials being reused...
   */
  session = webkit_get_default_session ();
  soup_session_remove_feature_by_type (session, SOUP_TYPE_COOKIE_JAR);
  cookie_jar = soup_cookie_jar_new ();
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (cookie_jar));

  /* TODO: we might need to add some more web browser UI to make this
   * work...
   */
  web_view = webkit_web_view_new ();
  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), url);
  g_signal_connect (web_view,
                    "notify::title",
                    G_CALLBACK (on_web_view_notify_title),
                    /* data */ NULL);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (scrolled_window, 500, 400);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_window), web_view);
  gtk_container_add (GTK_CONTAINER (vbox), scrolled_window);
  gtk_widget_show_all (scrolled_window);

  g_object_unref (cookie_jar);
  g_free (escaped_scope);
  g_free (url);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_toolbar_add_button_clicked (GtkToolButton *button,
                               gpointer       user_data)
{
  GoaPanel *panel = GOA_PANEL (user_data);
  GtkWidget *add_account_button;
  GtkWidget *dialog;
  GtkWidget *combo_box;
  GtkWidget *w;
  gint response;
  GList *children;
  GList *l;

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
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box), "google", _("Google Account"));
  gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (combo_box), "facebook", _("Facebook"));
  gtk_combo_box_set_active (GTK_COMBO_BOX (combo_box), 0);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
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

  /* TODO: check type from combo_box */

  w = GTK_WIDGET (gtk_builder_get_object (panel->builder, "goa-add-account-vbox"));
  add_google_account (panel, dialog, w);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);

  g_print ("Final Response was %d\n", response);

 out:
  ;
}

