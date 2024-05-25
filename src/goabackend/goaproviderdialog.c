/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
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

#include "goabackendenumtypes-priv.h"
#include "goaprovider.h"
#include "goaproviderdialog.h"
#include "goaprovider-priv.h"
#include "goautils.h"

/**
 * GoaProviderDialog:
 *
 * A dialog for providers to negotiate account setup with the user.
 *
 * Setup for an account may involve non-fatal errors and cycling through states,
 * which the provider is responsible for setting and responding to. Once the
 * state reaches %GOA_DIALOG_DONE any attempt to change the state will fail.
 *
 * Providers should call `goa_provider_task_run_in_dialog() to bind the lifetime
 * of a [vfunc@Goa.Provider.add_account] GTask to the dialog and present it to
 * the user, then call `goa_provider_task_return_account()` or
 * `goa_provider_task_return_error()` to complete it.
 *
 * Note that the state may change from any state to %GOA_DIALOG_DONE and
 * providers are responsible for aborting any ancillary tasks (e.g. another
 * GTask not chained to the primary task).
 *
 * Example Scenario:
 *
 *  1. User provides e-mail and password (IDLE => READY)
 *
 *     Providers should connect to widgets during UI setup and change the state
 *     to %GOA_DIALOG_READY when the account details are well-formed. The dialog
 *     will then mark the [property@Gtk.Widget:default-widget] as sensitive.
 *
 *  2. User clicks "Sign In" (READY => BUSY)
 *
 *     When the user activates the default widget, the dialog will change the
 *     state to %GOA_DIALOG_BUSY and become insensitive. Providers should
 *     respond by submitting the account details to the online service.
 *
 *  3. Service provider responds: bad password (BUSY => ERROR)
 *
 *     Providers may reports errors they consider non-fatal using
 *     [method@Goa.ProviderDialog.report_error] which displays it to user with
 *     a "Try Again" button and sets the state to %GOA_DIALOG_ERROR.
 *
 *  4. User clicks "Try Again" (ERROR => READY)
 *
 *     If the user chooses to retry, the state will return to %GOA_DIALOG_READY
 *     under the assumption the account details are well-formed (despite being
 *     incorrect).
 *
 *  5. User retries with correct information (BUSY => DONE)
 *
 *     When the provider has successfully authenticated and saved the account,
 *     simply complete the GTask and the window will close.
 */
struct _GoaProviderDialog
{
  AdwWindow parent_instance;

  GoaProvider *provider;
  GoaClient *client;
  GoaObject *object;
  GoaDialogState state;
  GCancellable *cancellable;

  GCancellable *task_cancellable;
  unsigned long task_cancellable_id;

  GtkWidget *view;
  GtkWidget *current_page;
  GtkWidget *current_group;
};

G_DEFINE_TYPE (GoaProviderDialog, goa_provider_dialog, ADW_TYPE_WINDOW)

typedef enum
{
  PROP_CLIENT = 1,
  PROP_PROVIDER,
  PROP_STATE,
} GoaProviderDialogProperty;

static GParamSpec *properties[PROP_STATE + 1] = { NULL, };


static void
on_action_activated (GoaProviderDialog *self)
{
  goa_provider_dialog_set_state (self, GOA_DIALOG_BUSY);
}

static void
goa_provider_dialog_default_widget_cb (GtkWindow *window,
                                       gpointer   user_data)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (window);
  AdwNavigationPage *page;
  GtkWidget *actionbar;
  GtkWidget *widget;

  widget = gtk_window_get_default_widget (window);
  if (GTK_IS_BUTTON (widget))
    {
      g_signal_handlers_disconnect_by_func (widget, on_action_activated, window);
      g_signal_connect_object (widget,
                               "clicked",
                               G_CALLBACK (on_action_activated),
                               window,
                               G_CONNECT_SWAPPED);
    }

  /* Hide the action bar if the default widget is not a descendant */
  page = adw_navigation_view_get_visible_page (ADW_NAVIGATION_VIEW (self->view));
  if (page != NULL)
    {
      actionbar = g_object_get_data (G_OBJECT (page), "goa-dialog-actions");
      if (actionbar != NULL)
        {
          if (widget != NULL && gtk_widget_is_ancestor (widget, actionbar))
            gtk_widget_set_visible (actionbar, TRUE);
          else
            gtk_widget_set_visible (actionbar, FALSE);
        }
    }
}

static void
goa_provider_dialog_constructed (GObject *object)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (object);
  const char *title = NULL;

  G_OBJECT_CLASS (goa_provider_dialog_parent_class)->constructed (object);

  title = gtk_window_get_title (GTK_WINDOW (self));
  if (title == NULL && self->provider != NULL)
    {
      g_autofree char *provider_name = NULL;
      g_autofree char *title = NULL;

      provider_name = goa_provider_get_provider_name (self->provider, NULL);
      /* Translators: this is the title of the "Add Account" and "Refresh
       * Account" dialogs. The %s is the name of the provider. eg.,
       * 'Google'.
       */
      title = g_strdup_printf (_("%s Account"), provider_name);
      gtk_window_set_title (GTK_WINDOW (self), title);
    }
}

static void
goa_provider_dialog_dispose (GObject *object)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (object);

  g_cancellable_cancel (self->cancellable);

  G_OBJECT_CLASS (goa_provider_dialog_parent_class)->dispose (object);
}

static void
goa_provider_dialog_finalize (GObject *object)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (object);

  g_clear_object (&self->task_cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->client);
  g_clear_object (&self->object);
  g_clear_object (&self->provider);

  G_OBJECT_CLASS (goa_provider_dialog_parent_class)->finalize (object);
}

static void
goa_provider_dialog_get_property (GObject      *object,
                                  unsigned int  property_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (object);

  switch ((GoaProviderDialogProperty) property_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, self->client);
      break;

    case PROP_PROVIDER:
      g_value_set_object (value, self->provider);
      break;

    case PROP_STATE:
      g_value_set_enum (value, self->state);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
goa_provider_dialog_set_property (GObject      *object,
                                  unsigned int  prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GoaProviderDialog *self = GOA_PROVIDER_DIALOG (object);

  switch ((GoaProviderDialogProperty) prop_id)
    {
    case PROP_CLIENT:
      g_assert (self->client == NULL);
      self->client = g_value_dup_object (value);
      break;

    case PROP_PROVIDER:
      g_assert (self->provider == NULL);
      self->provider = g_value_dup_object (value);
      break;

    case PROP_STATE:
      goa_provider_dialog_set_state (self, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_provider_dialog_init (GoaProviderDialog *self)
{
  GtkEventController *controller;
  GtkShortcut *shortcut;

  controller = gtk_shortcut_controller_new ();
  gtk_shortcut_controller_set_scope (GTK_SHORTCUT_CONTROLLER (controller),
                                     GTK_SHORTCUT_SCOPE_GLOBAL);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  shortcut = gtk_shortcut_new (gtk_shortcut_trigger_parse_string ("Escape|<Control>w"),
                               gtk_shortcut_action_parse_string ("action(window.close)"));
  gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (controller),
                                        shortcut);

  self->view = g_object_new (ADW_TYPE_NAVIGATION_VIEW, NULL);
  adw_window_set_content (ADW_WINDOW (self), self->view);

  /* Ensure the dialog refresh and remove tasks are cancelled on close */
  self->cancellable = g_cancellable_new ();

  /* The default widget determines the visibility of the action bar */
  g_signal_connect (self,
                    "notify::default-widget",
                    G_CALLBACK (goa_provider_dialog_default_widget_cb),
                    NULL);
}

static void
goa_provider_dialog_class_init (GoaProviderDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = goa_provider_dialog_constructed;
  object_class->dispose = goa_provider_dialog_dispose;
  object_class->finalize = goa_provider_dialog_finalize;
  object_class->get_property = goa_provider_dialog_get_property;
  object_class->set_property = goa_provider_dialog_set_property;

  properties[PROP_CLIENT] =
    g_param_spec_object ("client", NULL, NULL,
                         GOA_TYPE_CLIENT,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  properties[PROP_PROVIDER] =
    g_param_spec_object ("provider", NULL, NULL,
                         GOA_TYPE_PROVIDER,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY));

  properties[PROP_STATE] =
    g_param_spec_enum ("state", NULL, NULL,
                       GOA_TYPE_DIALOG_STATE,
                       GOA_DIALOG_IDLE,
                       (G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS |
                        G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);
}

/**
 * goa_provider_dialog_new:
 * @provider: a `GoaProvider`
 * @client: a `GoaClient`
 * @parent: (nullable): a `GtkWindow`
 *
 * Create a new dialog for @provider.
 *
 * If @parent is given, the dialog will be modal.
 *
 * Returns: a `GoaProviderDialog`
 */
GoaProviderDialog *
goa_provider_dialog_new (GoaProvider *provider,
                         GoaClient   *client,
                         GtkWindow   *parent)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), NULL);

  return goa_provider_dialog_new_full (provider, client, parent, 480, -1);
}

/**
 * goa_provider_dialog_new_full:
 * @provider: a `GoaProvider`
 * @client: a `GoaClient`
 * @parent: (nullable): a `GtkWindow`
 * @default_width: default width, or `-1`
 * @default_height: default height, or `-1`
 *
 * Create a new dialog for @provider.
 *
 * If @parent is given, the dialog will be modal.
 *
 * Returns: a `GoaProviderDialog`
 */
GoaProviderDialog *
goa_provider_dialog_new_full (GoaProvider *provider,
                         GoaClient   *client,
                         GtkWindow   *parent,
                         int          default_width,
                         int          default_height)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), NULL);

  /* In the non-ideal case a provider needs to chain-up to a parent
   * with it's own parent, we want the real root window.
   */
  if (GOA_IS_PROVIDER_DIALOG (parent))
    parent = gtk_window_get_transient_for (parent);

  return g_object_new (GOA_TYPE_PROVIDER_DIALOG,
                       "provider",            provider,
                       "client",              client,
                       "destroy-with-parent", parent != NULL,
                       "modal",               parent != NULL,
                       "transient-for",       parent,
                       "width-request",       360,
                       "default-width",       default_width,
                       "default-height",      default_height,
                       NULL);
}

/**
 * goa_provider_dialog_get_client:
 * @self: a `GoaProviderDialog`
 *
 * Get the dialog client.
 *
 * Returns: (transfer none): a `GoaClient`
 */
GoaClient *
goa_provider_dialog_get_client (GoaProviderDialog *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);

  return self->client;
}

/**
 * goa_provider_dialog_get_provider:
 * @self: a `GoaProviderDialog`
 *
 * Get the dialog provider.
 *
 * Returns: (transfer none): a `GoaProvider`
 */
GoaProvider *
goa_provider_dialog_get_provider (GoaProviderDialog *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);

  return self->provider;
}

/**
 * goa_provider_dialog_get_state:
 * @self: a `GoaProviderDialog`
 *
 * Get the dialog state.
 *
 * Returns: a `GoaDialogState`
 */
GoaDialogState
goa_provider_dialog_get_state (GoaProviderDialog *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), GOA_DIALOG_IDLE);

  return self->state;
}

/**
 * goa_provider_dialog_set_state:
 * @self: a `GoaProviderDialog`
 * @state: whether to mark the dialog as busy
 *
 * Set the dialog state.
 *
 * The %GOA_DIALOG_IDLE state is the default and indicates that input is
 * required. Providers should change the state to %GOA_DIALOG_READY to enable
 * the [property@Gtk.Window::default-widget]. When the default widget is
 * activated, the state will change to %GOA_DIALOG_BUSY and the dialog content
 * made insensitive.
 *
 * If an error is encountered, calling [method@Goa.ProviderDialog.report_error]
 * presents the error and changes the state to %GOA_DIALOG_ERROR.
 *
 * Once the state reaches %GOA_DIALOG_DONE, attempting to change it will fail
 * and log a critical.
 */
void
goa_provider_dialog_set_state (GoaProviderDialog *self,
                               GoaDialogState     state)
{
  AdwNavigationPage *page;
  GtkWidget *button;

  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (self));
  g_return_if_fail (self->state != GOA_DIALOG_DONE);

  if (self->state == state)
    return;

  /* Update the default widget */
  button = gtk_window_get_default_widget (GTK_WINDOW (self));
  if (button != NULL)
    gtk_widget_set_sensitive (button, state == GOA_DIALOG_READY);

  page = adw_navigation_view_get_visible_page (ADW_NAVIGATION_VIEW (self->view));
  if (ADW_IS_NAVIGATION_PAGE (page))
    {
      GtkWidget *banner;
      GtkWidget *content;

      banner = g_object_get_data (G_OBJECT (page), "goa-dialog-banner");
      if (banner != NULL)
        adw_banner_set_revealed (ADW_BANNER (banner), state == GOA_DIALOG_ERROR);

      content = g_object_get_data (G_OBJECT (page), "goa-dialog-content");
      if (content != NULL)
        gtk_widget_set_sensitive (content, state != GOA_DIALOG_BUSY);
    }

  self->state = state;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_STATE]);

  // Destroy the window after handlers have run
  if (self->state == GOA_DIALOG_DONE)
    gtk_window_destroy (GTK_WINDOW (self));
}

static GtkWidget *
create_feature_row (GoaAccount *account,
                    const char *property,
                    const char *blurb)
{
  GtkWidget *row;

  row = g_object_new (ADW_TYPE_SWITCH_ROW,
                      "title",         blurb,
                      "use-underline", TRUE,
                      NULL);

  if (goa_account_get_attention_needed (account))
    {
      gtk_widget_set_sensitive (row, FALSE);
      adw_switch_row_set_active (ADW_SWITCH_ROW (row), FALSE);
    }
  else
    {
      g_object_bind_property (account, property,
                              row,     "active",
                              (G_BINDING_SYNC_CREATE |
                               G_BINDING_BIDIRECTIONAL |
                               G_BINDING_INVERT_BOOLEAN));
    }

  return row;
}

static void
goa_provider_refresh_account_cb (GoaProvider       *provider,
                                GAsyncResult      *result,
                                GoaProviderDialog *self)
{
  g_autoptr(GError) error = NULL;

  if (!goa_provider_refresh_account_finish (provider, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        goa_provider_dialog_report_error (self, error);

      return;
    }

  gtk_window_destroy (GTK_WINDOW (self));
}

static void
on_refresh_activated (GoaProviderDialog *self)
{
  goa_provider_refresh_account (self->provider,
                                self->client,
                                self->object,
                                GTK_WINDOW (self),
                                self->cancellable,
                                (GAsyncReadyCallback) goa_provider_refresh_account_cb,
                                self);
  goa_provider_dialog_set_state (self, GOA_DIALOG_BUSY);
}

static void
goa_account_call_remove_cb (GoaAccount   *account,
                            GAsyncResult *result,
                            gpointer      user_data)
{
  g_autoptr(GoaProviderDialog) self = GOA_PROVIDER_DIALOG (user_data);
  g_autoptr(GError) error = NULL;

  if (!goa_account_call_remove_finish (account, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        goa_provider_dialog_report_error (self, error);

      return;
    }

  g_debug ("Removed %s account %s",
           goa_account_get_provider_name (account),
           goa_account_get_presentation_identity (account));

  gtk_window_destroy (GTK_WINDOW (self));
}

static void
adw_message_dialog_choose_cb (AdwMessageDialog *dialog,
                              GAsyncResult     *result,
                              gpointer          user_data)
{
  g_autoptr(GoaProviderDialog) self = GOA_PROVIDER_DIALOG (user_data);
  const char *response = NULL;

  response = adw_message_dialog_choose_finish (dialog, result);
  if (g_strcmp0 (response, "remove") == 0)
    {
      goa_account_call_remove (goa_object_peek_account (self->object),
                               self->cancellable,
                               (GAsyncReadyCallback) goa_account_call_remove_cb,
                               g_object_ref (self));
      return;
    }

  goa_provider_dialog_set_state (self, GOA_DIALOG_READY);
}

static void
on_remove_activated (GoaProviderDialog *self)
{
  GtkWidget *dialog;

  dialog = adw_message_dialog_new (GTK_WINDOW (self),
                                   _("Remove this Account?"),
                                   _("If you remove this Online Account you will have to connect to it again to use it with apps and services."));
  adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
                                    "cancel", _("_Cancel"),
                                    "remove", _("_Remove"),
                                    NULL);
  adw_message_dialog_set_response_appearance (ADW_MESSAGE_DIALOG (dialog),
                                              "remove",
                                              ADW_RESPONSE_DESTRUCTIVE);
  adw_message_dialog_choose (ADW_MESSAGE_DIALOG (dialog),
                             self->cancellable,
                             (GAsyncReadyCallback) adw_message_dialog_choose_cb,
                             g_object_ref (self));
  goa_provider_dialog_set_state (self, GOA_DIALOG_BUSY);
}

/**
 * goa_provider_dialog_push_account:
 * @self: a `GoaProviderDialog`
 * @object: (nullable): a `GoaObject`
 * @content: (nullable): a content widget
 *
 * Push an account management page for @object.
 *
 * If @content is given, it will be placed below the features list, above
 * the remove button.
 */
void
goa_provider_dialog_push_account (GoaProviderDialog *self,
                                  GoaObject         *object,
                                  GtkWidget         *content)
{
  GoaAccount *account;
  const char *account_name;
  g_autoptr(GIcon) provider_icon = NULL;
  g_autofree char *provider_name = NULL;
  GdkDisplay *display;
  GtkIconPaintable *paintable;
  GtkWidget *page;
  GtkWidget *view;
  GtkWidget *headerbar;
  GtkWidget *banner;
  GtkWidget *clamp;
  GtkWidget *status;
  GtkWidget *box;
  GtkWidget *group;
  GtkWidget *action_button;
  GoaProviderFeatures account_features;
  GoaProviderFeaturesInfo *feature_infos = NULL;

  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (self));
  g_return_if_fail (object == NULL || GOA_IS_OBJECT (object));
  g_return_if_fail (content == NULL || GTK_IS_WIDGET (content));

  if (!g_set_object (&self->object, object) || object == NULL)
    return;

  account = goa_object_peek_account (object);
  account_name = goa_account_get_presentation_identity (account);
  account_features = goa_provider_get_provider_features (self->provider);
  provider_icon = goa_provider_get_provider_icon (self->provider, NULL);
  provider_name = goa_provider_get_provider_name (self->provider, NULL);
  display = gtk_widget_get_display (GTK_WIDGET (self));
  paintable = gtk_icon_theme_lookup_by_gicon (gtk_icon_theme_get_for_display (display),
                                              provider_icon,
                                              128,
                                              1,
                                              GTK_TEXT_DIR_NONE,
                                              GTK_ICON_LOOKUP_PRELOAD);

  page = g_object_new (ADW_TYPE_NAVIGATION_PAGE,
                       "title", account_name,
                       "tag",   "account",
                       NULL);
  view = adw_toolbar_view_new ();
  adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (page), view);

  /* Top bars */
  headerbar = adw_header_bar_new ();
  adw_header_bar_set_show_title (ADW_HEADER_BAR (headerbar), FALSE);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), headerbar);

  banner = adw_banner_new (_("Sign in to reconnect to this account"));
  adw_banner_set_button_label (ADW_BANNER (banner), _("_Sign In…"));
  adw_banner_set_use_markup (ADW_BANNER (banner), TRUE);
  g_object_bind_property (account, "attention-needed",
                          banner,  "revealed",
                          G_BINDING_SYNC_CREATE);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (view), banner);

  /* Content */
  clamp = adw_clamp_new ();
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (view), clamp);

  status = g_object_new (ADW_TYPE_STATUS_PAGE,
                         "title",       provider_name,
                         "description", account_name,
                         "paintable",   paintable,
                         NULL);
  gtk_widget_add_css_class (status, "compact");
  gtk_widget_add_css_class (status, "icon-dropshadow");
  adw_clamp_set_child (ADW_CLAMP (clamp), status);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
  adw_status_page_set_child (ADW_STATUS_PAGE (status), box);

  /* Features */
  group = adw_preferences_group_new ();
  g_object_bind_property (account, "attention-needed",
                          group,   "sensitive",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  gtk_box_append (GTK_BOX (box), group);

  feature_infos = goa_provider_get_provider_features_infos ();
  for (unsigned int i = 0; feature_infos[i].property != NULL; i++)
    {
      if ((account_features & feature_infos[i].feature) != 0)
        {
          GtkWidget *row;

          row = create_feature_row (account,
                                    feature_infos[i].property,
                                    _(feature_infos[i].blurb));
          adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
        }
    }

  /* (Optional) Content */
  if (content != NULL)
    gtk_box_append (GTK_BOX (box), content);

  /* Remove */
  action_button = gtk_button_new_with_mnemonic (_("_Remove…"));
  gtk_widget_set_halign (action_button, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (action_button, "pill");
  gtk_box_append (GTK_BOX (box), action_button);

  g_object_set_data (G_OBJECT (page), "goa-dialog-banner", banner);

  g_signal_connect_object (action_button,
                           "clicked",
                           G_CALLBACK (on_remove_activated),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (banner,
                           "button-clicked",
                           G_CALLBACK (on_refresh_activated),
                           self,
                           G_CONNECT_SWAPPED);

  adw_navigation_view_push (ADW_NAVIGATION_VIEW (self->view),
                            ADW_NAVIGATION_PAGE (page));
}

/**
 * goa_provider_dialog_push_content:
 * @self: a `GoaProviderDialog`
 * @title: (nullable): a page title
 * @content: (nullable): a content widget
 *
 * Add a page to the dialog for @content.
 *
 * If @content is a [class@Adw.StatusPage] and the title is empty, the window
 * title will be displayed in the status page instead of the header bar.
 *
 * Returns: (transfer none): the @content widget
 */
GtkWidget *
goa_provider_dialog_push_content (GoaProviderDialog *self,
                                  const char        *title,
                                  GtkWidget         *content)
{
  GtkWidget *page;
  GtkWidget *child;
  GtkWidget *headerbar;
  GtkWidget *banner;
  GtkWidget *actionbar;
  GtkWidget *button;

  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);

  if (title == NULL || *title == '\0')
    title = gtk_window_get_title (GTK_WINDOW (self));

  page = g_object_new (ADW_TYPE_NAVIGATION_PAGE,
                       "title", title,
                       NULL);

  child = adw_toolbar_view_new ();
  adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (page), child);

  headerbar = adw_header_bar_new ();
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (child), headerbar);

  banner = adw_banner_new ("");
  g_signal_connect_object (banner,
                           "button-clicked",
                           G_CALLBACK (on_action_activated),
                           self,
                           G_CONNECT_SWAPPED);
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (child), banner);
  g_object_set_data (G_OBJECT (page), "goa-dialog-banner", banner);

  if (GTK_IS_WIDGET (content))
    {
      adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (child), content);
      g_object_set_data (G_OBJECT (page), "goa-dialog-content", content);

      /* Special handling for AdwStatusPage */
      if (ADW_IS_STATUS_PAGE (content))
        {
          const char *status = NULL;

          status = adw_status_page_get_title (ADW_STATUS_PAGE (content));
          if (status == NULL || *status == '\0')
            {
              adw_header_bar_set_show_title (ADW_HEADER_BAR (headerbar), FALSE);
              adw_status_page_set_title (ADW_STATUS_PAGE (content), title);
            }
        }
    }

  actionbar = gtk_action_bar_new ();
  gtk_widget_add_css_class (actionbar, "toolbar");
  adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (child), actionbar);
  g_object_set_data (G_OBJECT (page), "goa-dialog-actions", actionbar);

  button = gtk_button_new_with_mnemonic (_("_Sign In"));
  gtk_widget_set_sensitive (button, self->state == GOA_DIALOG_READY);
  gtk_widget_add_css_class (button, "suggested-action");
  gtk_widget_add_css_class (button, "pill");
  gtk_action_bar_set_center_widget (GTK_ACTION_BAR (actionbar), button);

  /* Set the default widget after it's a child of the window */
  adw_navigation_view_push (ADW_NAVIGATION_VIEW (self->view),
                            ADW_NAVIGATION_PAGE (page));
  gtk_window_set_default_widget (GTK_WINDOW (self), button);

  return content;
}

/**
 * goa_provider_dialog_report_error:
 * @self: a `GoaProviderDialog`
 * @error: (nullable): a `GError`
 *
 * Reveal a message bar, using @error for the text.
 *
 * If @error is not %NULL, the message bar will be revealed with actions
 * appropriate for the error and [property@Goa.ProviderDialog:state] will be
 * set to %GOA_DIALOG_ERROR. Otherwise the message bar will be hidden and the
 * state left unchanged.
 */
void
goa_provider_dialog_report_error (GoaProviderDialog *self,
                                  const GError      *error)
{
  AdwNavigationPage *page;
  GtkWidget *banner;
  const char *title = NULL;
  const char *button_label = NULL;
  g_autoptr(GError) user_error = NULL;

  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (self));

  page = adw_navigation_view_get_visible_page (ADW_NAVIGATION_VIEW (self->view));
  g_return_if_fail (ADW_IS_NAVIGATION_PAGE (page));

  banner = g_object_get_data (G_OBJECT (page), "goa-dialog-banner");
  g_return_if_fail (ADW_IS_BANNER (banner));

  if (error != NULL)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
          || g_error_matches (error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED))
        {
          title = adw_banner_get_title (ADW_BANNER (banner));
          button_label = adw_banner_get_button_label (ADW_BANNER (banner));
        }

      /* If this was an aborted retry, don't overwrite an existing error */
      if (title == NULL || *title == '\0')
        {
          user_error = g_error_copy (error);

          if (g_dbus_error_is_remote_error (user_error))
            g_dbus_error_strip_remote_error (user_error);

          title = user_error->message;
          button_label = _("_Try Again");

          /* The user may choose to ignore SSL errors */
          if (g_error_matches (user_error, GOA_ERROR, GOA_ERROR_SSL))
            button_label = _("_Ignore");
        }
    }

  adw_banner_set_title (ADW_BANNER (banner), title);
  adw_banner_set_button_label (ADW_BANNER (banner), button_label);

  if (error != NULL)
    goa_provider_dialog_set_state (self, GOA_DIALOG_ERROR);
}

/**
 * goa_provider_dialog_add_page:
 * @self: a `GoaProviderDialog`
 * @title: (nullable): a page title
 * @description: (nullable): a page description
 *
 * Add a page to the dialog.
 *
 * Returns: (transfer none): the content widget
 */
GtkWidget *
goa_provider_dialog_add_page (GoaProviderDialog *self,
                              const char        *title,
                              const char        *description)
{
  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);

  if (title == NULL)
    title = gtk_window_get_title (GTK_WINDOW (self));

  self->current_page = g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                     "title",       title,
                                     "description", description,
                                     NULL);

  return goa_provider_dialog_push_content (self, title, self->current_page);
}

/**
 * goa_provider_dialog_add_group:
 * @self: a `GoaProviderDialog`
 * @title: (nullable): a group label
 *
 * Add a group to the current page.
 *
 * If @title is not %NULL, it will be used as a heading label.
 *
 * Returns: (transfer none): the group widget
 */
GtkWidget *
goa_provider_dialog_add_group (GoaProviderDialog *self,
                               const char        *title)
{
  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);

  if (self->current_page == NULL)
    goa_provider_dialog_add_page (self, NULL, NULL);

  self->current_group = g_object_new (ADW_TYPE_PREFERENCES_GROUP,
                                      "title", title,
                                      NULL);
  adw_preferences_page_add (ADW_PREFERENCES_PAGE (self->current_page),
                            ADW_PREFERENCES_GROUP (self->current_group));

  return self->current_group;
}


/**
 * goa_provider_dialog_add_combo:
 * @self: a `GoaProviderDialog`
 * @group: (nullable): a `GtkWidget` to hold the entry
 * @label: a user-visible label
 * @strings: a list of entries
 *
 * Add multiple-choice field to @group.
 *
 * The @strings will be added in order, so the selected option can be
 * accessed using the `selected` property of the returned widget.
 *
 * Returns: (transfer none): the combo widget
 */
GtkWidget *
goa_provider_dialog_add_combo (GoaProviderDialog *self,
                               GtkWidget         *group,
                               const char        *label,
                               GStrv              strings)
{
  GtkWidget *child = NULL;
  g_autoptr(GtkStringList) model = NULL;

  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);
  g_return_val_if_fail (strings != NULL && *strings != NULL, NULL);
  g_return_val_if_fail (group == NULL || GTK_IS_WIDGET (group), NULL);

  model = gtk_string_list_new ((const char * const *)strings);
  child = g_object_new (ADW_TYPE_COMBO_ROW,
                        "title",         label,
                        "use-underline", TRUE,
                        "model",         model,
                        NULL);

  if (ADW_IS_PREFERENCES_GROUP (group))
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), child);
  else if (ADW_IS_EXPANDER_ROW (group))
    adw_expander_row_add_row (ADW_EXPANDER_ROW (group), child);

  return child;
}

/**
 * goa_provider_dialog_add_entry:
 * @self: a `GoaProviderDialog`
 * @group: a group
 * @label: a user-visible label
 *
 * Add and entry field to @group.
 *
 * Returns: (transfer none): a `GtkEditable`
 */
GtkWidget *
goa_provider_dialog_add_entry (GoaProviderDialog *self,
                               GtkWidget         *group,
                               const char        *label)
{
  GtkWidget *child;

  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);
  g_return_val_if_fail (group == NULL || GTK_IS_WIDGET (group), NULL);

  child = g_object_new (ADW_TYPE_ENTRY_ROW,
                        "title",             label,
                        "activates-default", TRUE,
                        "use-underline",     TRUE,
                        NULL);

  if (ADW_IS_PREFERENCES_GROUP (group))
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), child);
  else if (ADW_IS_EXPANDER_ROW (group))
    adw_expander_row_add_row (ADW_EXPANDER_ROW (group), child);

  return child;
}

/**
 * goa_provider_dialog_add_password_entry:
 * @self: a `GoaProviderDialog`
 * @group: (nullable): a `GtkWidget` to hold the entry
 * @child: a `GtkWidget`
 *
 * Add a password entry field to @group.
 *
 * If @group is %NULL the entry will be added to the dialog content box.
 *
 * Returns: (transfer none): the password entry widget
 */
GtkWidget *
goa_provider_dialog_add_password_entry (GoaProviderDialog *self,
                                        GtkWidget         *group,
                                        const char        *label)
{
  GtkWidget *child;

  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);
  g_return_val_if_fail (group == NULL || GTK_IS_WIDGET (group), NULL);

  child = g_object_new (ADW_TYPE_PASSWORD_ENTRY_ROW,
                        "title",             label,
                        "use-underline",     TRUE,
                        "activates-default", TRUE,
                        NULL);

  if (ADW_IS_PREFERENCES_GROUP (group))
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), child);
  else if (ADW_IS_EXPANDER_ROW (group))
    adw_expander_row_add_row (ADW_EXPANDER_ROW (group), child);

  return child;
}

/**
 * goa_provider_dialog_add_description:
 * @self: a `GoaProviderDialog`
 * @target: (nullable): a `GtkWidget`
 * @description: (nullable): a description
 *
 * Add a description label to the current group.
 *
 * If @target is given, it's accessible description will be update to refer
 * to the new label.
 *
 * Returns: (transfer none): the new label
 */
GtkWidget *
goa_provider_dialog_add_description (GoaProviderDialog *self,
                                     GtkWidget         *target,
                                     const char        *description)
{
  GtkWidget *child;

  g_return_val_if_fail (GOA_IS_PROVIDER_DIALOG (self), NULL);
  g_return_val_if_fail (target == NULL || GTK_IS_WIDGET (target), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (self->current_group), NULL);

  child = g_object_new (GTK_TYPE_LABEL,
                        "label",       description,
                        "visible",     description != NULL,
                        "css-classes", (const char * const[]){"dim-label", NULL},
                        "xalign",      0.0,
                        "margin-top",  12,
                        "wrap",        TRUE,
                        NULL);

  if (GTK_IS_ACCESSIBLE (target))
    {
      gtk_accessible_update_relation (GTK_ACCESSIBLE (target),
                                      GTK_ACCESSIBLE_RELATION_DESCRIBED_BY,
                                      child, NULL,
                                      -1);
    }

  if (ADW_IS_PREFERENCES_GROUP (self->current_group))
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->current_group), child);
  else if (ADW_IS_EXPANDER_ROW (self->current_group))
    adw_expander_row_add_row (ADW_EXPANDER_ROW (self->current_group), child);

  return child;
}

static gboolean
goa_provider_task_run_in_dialog_cb (GoaProviderDialog *self,
                                    gpointer           user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));

  g_signal_handlers_disconnect_by_func (self, goa_provider_task_run_in_dialog_cb, task);

  if (self->task_cancellable != NULL && self->task_cancellable_id != 0)
    {
      g_cancellable_disconnect (self->task_cancellable, self->task_cancellable_id);
      self->task_cancellable_id = 0;
    }

  if (self->state != GOA_DIALOG_DONE)
    {
      goa_provider_dialog_set_state (self, GOA_DIALOG_DONE);
      g_task_return_new_error (task,
                               GOA_ERROR,
                               GOA_ERROR_DIALOG_DISMISSED,
                               _("Dialog was dismissed"));
    }

  return FALSE;
}

static void
goa_provider_task_cancelled_cb (GCancellable *cancellable,
                                GtkWindow    *window)
{
  gtk_window_destroy (window);
}

/**
 * goa_provider_task_run_in_dialog
 * @task: a `GTask`
 * @dialog: a `GoaProviderDialog`
 *
 * Bind the completion of @task to the lifetime of @dialog and present it.
 *
 * The dialog holds a reference on @task and when it completes @dialog is
 * closed. If @dialog is closed before @task completes, the task will return an
 * error with domain and code set to %GOA_ERROR and %GOA_ERROR_DIALOG_DISMISSED,
 * respectively.
 */
void
goa_provider_task_run_in_dialog (GTask             *task,
                                 GoaProviderDialog *dialog)
{
  GCancellable *cancellable = NULL;

  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (dialog));

  /* The signal handler holds the reference to @task.
   */
  g_signal_connect_object (dialog,
                           "close-request",
                           G_CALLBACK (goa_provider_task_run_in_dialog_cb),
                           g_object_ref (task),
                           0 /* G_CONNECT_DEFAULT */);
  g_object_set_data (G_OBJECT (task), "goa-provider-dialog", dialog);

  /* Ensure the handler runs if the task is cancelled.
   */
  cancellable = g_task_get_cancellable (task);
  if (cancellable != NULL)
    {
      dialog->task_cancellable = g_object_ref (cancellable);
      dialog->task_cancellable_id = g_cancellable_connect (cancellable,
                                                           G_CALLBACK (goa_provider_task_cancelled_cb),
                                                           dialog, NULL);
    }

  gtk_window_present (GTK_WINDOW (dialog));
}

/**
 * goa_provider_task_return_account:
 * @self: a `GoaProviderDialog`
 * @task: a `GTask`
 * @object: (transfer full): a `GoaObject`
 *
 * Sets @task's result to @object, completing the task and changing the
 * dialog state to %GOA_PROVIDER_DONE.
 *
 * This should be used instead of `g_task_return_pointer()` to complete the
 * `GTask` for an [vfunc@Goa.Provider.add_account] implementation.
 */
void
goa_provider_task_return_account (GTask     *task,
                                  GoaObject *object)
{
  GoaProviderDialog *self = NULL;

  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (GOA_IS_OBJECT (object));

  self = g_object_get_data (G_OBJECT (task), "goa-provider-dialog");
  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (self));
  g_return_if_fail (self->state != GOA_DIALOG_DONE);

  goa_provider_dialog_set_state (self, GOA_DIALOG_DONE);
  g_task_return_pointer (task, g_steal_pointer (&object), g_object_unref);
}

/**
 * goa_provider_task_return_error:
 * @task: a `GTask`
 * @error: (transfer full): a `GError`
 *
 * Sets @task's result to @error, completing the task and changing the
 * dialog state to %GOA_PROVIDER_DONE.
 *
 * This should be used instead of `g_task_return_error()` to complete the
 * `GTask` for an [vfunc@Goa.Provider.add_account] implementation.
 */
void
goa_provider_task_return_error (GTask  *task,
                                GError *error)
{
  GoaProviderDialog *self = NULL;

  g_return_if_fail (G_IS_TASK (task));
  g_return_if_fail (error != NULL);

  self = g_object_get_data (G_OBJECT (task), "goa-provider-dialog");
  g_return_if_fail (GOA_IS_PROVIDER_DIALOG (self));
  g_return_if_fail (self->state != GOA_DIALOG_DONE);

  goa_provider_dialog_set_state (self, GOA_DIALOG_DONE);
  g_task_return_error (task, g_steal_pointer (&error));
}
