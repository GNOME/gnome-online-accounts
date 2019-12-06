/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2015 Damián Nohales
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

/* Based on code by the Epiphany team.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <jsc/jsc.h>
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>

#include "goawebview.h"
#include "nautilus-floating-bar.h"

struct _GoaWebView
{
  GtkOverlay parent_instance;
  GoaProvider *provider;
  GtkWidget *floating_bar;
  GtkWidget *progress_bar;
  GtkWidget *web_view;
  WebKitUserContentManager *user_content_manager;
  WebKitWebContext *context;
  gchar *existing_identity;
  gulong clear_notify_progress_id;
  gulong notify_load_status_id;
  gulong notify_progress_id;
};

enum
{
  PROP_0,
  PROP_EXISTING_IDENTITY,
  PROP_PROVIDER
};

enum
{
  DENY_CLICK,
  PASSWORD_SUBMIT,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GoaWebView, goa_web_view, GTK_TYPE_OVERLAY)

static gboolean
web_view_clear_notify_progress_cb (gpointer user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);

  gtk_widget_hide (self->progress_bar);
  self->clear_notify_progress_id = 0;
  return FALSE;
}

static char *
web_view_create_loading_title (const gchar *url)
{
  SoupURI *uri;
  const gchar *hostname;
  gchar *title;

  g_return_val_if_fail (url != NULL && url[0] != '\0', NULL);

  uri = soup_uri_new (url);
  hostname = soup_uri_get_host (uri);
  /* translators: %s here is the address of the web page */
  title = g_strdup_printf (_("Loading “%s”…"), hostname);
  soup_uri_free (uri);

  return title;
}

static void
web_view_floating_bar_update (GoaWebView *self, const gchar *text)
{
  nautilus_floating_bar_set_label (NAUTILUS_FLOATING_BAR (self->floating_bar), text);

  if (text == NULL || text[0] == '\0')
    {
      gtk_widget_hide (self->floating_bar);
      gtk_widget_set_halign (self->floating_bar, GTK_ALIGN_START);
    }
  else
    gtk_widget_show (self->floating_bar);
}

static void
web_view_initialize_web_extensions_cb (GoaWebView *self)
{
  GVariant *data;
  const gchar *existing_identity;
  const gchar *provider_type;

  webkit_web_context_set_web_extensions_directory (self->context, PACKAGE_WEB_EXTENSIONS_DIR);

  if (self->provider == NULL)
    return;

  provider_type = goa_provider_get_provider_type (self->provider);
  existing_identity = (self->existing_identity == NULL) ? "" : self->existing_identity;
  data = g_variant_new ("(ss)", provider_type, existing_identity);
  webkit_web_context_set_web_extensions_initialization_user_data (self->context, data);
}

#ifdef GOA_INSPECTOR_ENABLED
static void
web_view_inspector_closed_cb (WebKitWebInspector *inspector)
{
  GtkWidget *window;
  WebKitWebViewBase *inspector_web_view;

  inspector_web_view = webkit_web_inspector_get_web_view (inspector);
  window = gtk_widget_get_toplevel (GTK_WIDGET (inspector_web_view));
  if (gtk_widget_is_toplevel (window))
    gtk_widget_destroy (window);
}

static gboolean
web_view_inspector_open_window_cb (WebKitWebInspector *inspector)
{
  GtkWidget *window;
  GtkWindowGroup *group;
  WebKitWebViewBase *inspector_web_view;

  group = gtk_window_group_new ();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_resize (GTK_WINDOW (window), 800, 600);
  gtk_window_group_add_window (group, GTK_WINDOW (window));
  g_object_unref (group);

  inspector_web_view = webkit_web_inspector_get_web_view (inspector);
  gtk_container_add (GTK_CONTAINER (window), GTK_WIDGET (inspector_web_view));

  gtk_widget_show_all (window);
  gtk_window_present (GTK_WINDOW (window));

  return GDK_EVENT_STOP;
}
#endif /* GOA_INSPECTOR_ENABLED */

static void
web_view_load_changed_cb (WebKitWebView  *web_view,
                          WebKitLoadEvent load_event,
                          gpointer        user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);

  switch (load_event)
    {
    case WEBKIT_LOAD_STARTED:
    case WEBKIT_LOAD_COMMITTED:
      {
        const gchar *uri;
        gchar *title;

        uri = webkit_web_view_get_uri (web_view);
        title = web_view_create_loading_title (uri);

        web_view_floating_bar_update (self, title);
        g_free (title);
        break;
      }

    case WEBKIT_LOAD_REDIRECTED:
      /* TODO: Update the loading uri */
      break;

    case WEBKIT_LOAD_FINISHED:
      web_view_floating_bar_update (self, NULL);
      break;

    default:
      break;
    }
}

static void
web_view_notify_estimated_load_progress_cb (GObject    *object,
                                            GParamSpec *pspec,
                                            gpointer    user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  gboolean loading;
  const gchar *uri;
  gdouble progress;

  if (self->clear_notify_progress_id != 0)
    {
      g_source_remove (self->clear_notify_progress_id);
      self->clear_notify_progress_id = 0;
    }

  uri = webkit_web_view_get_uri (web_view);
  if (!uri || g_str_equal (uri, "about:blank"))
    return;

  progress = webkit_web_view_get_estimated_load_progress (web_view);
  loading = webkit_web_view_is_loading (web_view);

  if (progress == 1.0 || !loading)
    self->clear_notify_progress_id = g_timeout_add (500, web_view_clear_notify_progress_cb, self);
  else
    gtk_widget_show (self->progress_bar);

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (self->progress_bar),
                                 (loading || progress == 1.0) ? progress : 0.0);
}

static void
web_view_script_message_received_deny_click_cb (GoaWebView *self)
{
  g_signal_emit (self, signals[DENY_CLICK], 0);
}

static void
web_view_script_message_received_password_submit_cb (GoaWebView *self, WebKitJavascriptResult *js_result)
{
  JSCValue *jsc_value;
  gchar *password = NULL;

  jsc_value = webkit_javascript_result_get_js_value (js_result);
  password = jsc_value_to_string (jsc_value);
  if (password != NULL && password[0] != '\0')
    g_signal_emit (self, signals[PASSWORD_SUBMIT], 0, password);

  g_free (password);
}

static void
goa_web_view_get_preferred_height (GtkWidget *widget, gint *minimum_size, gint *natural_size)
{
  if (minimum_size != NULL)
    *minimum_size = 200;

  if (natural_size != NULL)
    *natural_size = 400;
}

static void
goa_web_view_get_preferred_width (GtkWidget *widget, gint *minimum_size, gint *natural_size)
{
  if (minimum_size != NULL)
    *minimum_size = 300;

  if (natural_size != NULL)
    *natural_size = 500;
}

static GtkSizeRequestMode
goa_web_view_get_request_mode (GtkWidget *widget)
{
  return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
goa_web_view_constructed (GObject *object)
{
  GoaWebView *self = GOA_WEB_VIEW (object);
  const gchar *const *language_names;

  G_OBJECT_CLASS (goa_web_view_parent_class)->constructed (object);

  self->context = webkit_web_context_new ();
  language_names = g_get_language_names ();
  webkit_web_context_set_preferred_languages (self->context, language_names);
  g_signal_connect_swapped (self->context,
                            "initialize-web-extensions",
                            G_CALLBACK (web_view_initialize_web_extensions_cb),
                            self);

  self->user_content_manager = webkit_user_content_manager_new ();
  g_signal_connect_swapped (self->user_content_manager,
                            "script-message-received::deny-click",
                            G_CALLBACK (web_view_script_message_received_deny_click_cb),
                            self);
  g_signal_connect_swapped (self->user_content_manager,
                            "script-message-received::password-submit",
                            G_CALLBACK (web_view_script_message_received_password_submit_cb),
                            self);
  webkit_user_content_manager_register_script_message_handler (self->user_content_manager, "deny-click");
  webkit_user_content_manager_register_script_message_handler (self->user_content_manager, "password-submit");

  self->web_view = GTK_WIDGET (g_object_new (WEBKIT_TYPE_WEB_VIEW,
                                             "user-content-manager", self->user_content_manager,
                                             "web-context", self->context,
                                             NULL));
  gtk_container_add (GTK_CONTAINER (self), self->web_view);

#ifdef GOA_INSPECTOR_ENABLED
  {
    WebKitSettings *settings;
    WebKitWebInspector *inspector;

    /* Setup the inspector */
    settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (self->web_view));
    g_object_set (settings, "enable-developer-extras", TRUE, NULL);

    inspector = webkit_web_view_get_inspector (WEBKIT_WEB_VIEW (self->web_view));
    g_signal_connect (inspector, "closed", G_CALLBACK (web_view_inspector_closed_cb), NULL);
    g_signal_connect (inspector, "open-window", G_CALLBACK (web_view_inspector_open_window_cb), NULL);
  }
#endif /* GOA_INSPECTOR_ENABLED */

  /* statusbar is hidden by default */
  self->floating_bar = nautilus_floating_bar_new (NULL, FALSE);
  gtk_widget_set_halign (self->floating_bar, GTK_ALIGN_START);
  gtk_widget_set_valign (self->floating_bar, GTK_ALIGN_END);
  gtk_widget_set_no_show_all (self->floating_bar, TRUE);
  gtk_overlay_add_overlay (GTK_OVERLAY (self), self->floating_bar);

  self->progress_bar = gtk_progress_bar_new ();
  gtk_style_context_add_class (gtk_widget_get_style_context (self->progress_bar),
                               GTK_STYLE_CLASS_OSD);
  gtk_widget_set_halign (self->progress_bar, GTK_ALIGN_FILL);
  gtk_widget_set_valign (self->progress_bar, GTK_ALIGN_START);
  gtk_overlay_add_overlay (GTK_OVERLAY (self), self->progress_bar);

  self->notify_progress_id = g_signal_connect (self->web_view,
                                               "notify::estimated-load-progress",
                                               G_CALLBACK (web_view_notify_estimated_load_progress_cb),
                                               self);
  self->notify_load_status_id = g_signal_connect (self->web_view,
                                                  "load_changed",
                                                  G_CALLBACK (web_view_load_changed_cb),
                                                  self);
}

static void
goa_web_view_dispose (GObject *object)
{
  GoaWebView *self = GOA_WEB_VIEW (object);

  g_clear_object (&self->user_content_manager);
  g_clear_object (&self->context);

  if (self->clear_notify_progress_id != 0)
    {
      g_source_remove (self->clear_notify_progress_id);
      self->clear_notify_progress_id = 0;
    }

  if (self->notify_load_status_id != 0)
    {
      g_signal_handler_disconnect (self->web_view, self->notify_load_status_id);
      self->notify_load_status_id = 0;
    }

  if (self->notify_progress_id != 0)
    {
      g_signal_handler_disconnect (self->web_view, self->notify_progress_id);
      self->notify_progress_id = 0;
    }

  G_OBJECT_CLASS (goa_web_view_parent_class)->dispose (object);
}

static void
goa_web_view_finalize (GObject *object)
{
  GoaWebView *self = GOA_WEB_VIEW (object);

  g_free (self->existing_identity);

  if (self->provider != NULL)
    g_object_remove_weak_pointer (G_OBJECT (self->provider), (gpointer *) &self->provider);

  G_OBJECT_CLASS (goa_web_view_parent_class)->finalize (object);
}

static void
goa_web_view_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GoaWebView *self = GOA_WEB_VIEW (object);

  switch (prop_id)
    {
    case PROP_EXISTING_IDENTITY:
      self->existing_identity = g_value_dup_string (value);
      break;

    case PROP_PROVIDER:
      self->provider = GOA_PROVIDER (g_value_get_object (value));
      if (self->provider != NULL)
        g_object_add_weak_pointer (G_OBJECT (self->provider), (gpointer *) &self->provider);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_web_view_init (GoaWebView *self)
{
}

static void
goa_web_view_class_init (GoaWebViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = goa_web_view_constructed;
  object_class->dispose = goa_web_view_dispose;
  object_class->finalize = goa_web_view_finalize;
  object_class->set_property = goa_web_view_set_property;

  widget_class->get_preferred_height = goa_web_view_get_preferred_height;
  widget_class->get_preferred_width = goa_web_view_get_preferred_width;
  widget_class->get_request_mode = goa_web_view_get_request_mode;

  g_object_class_install_property (object_class,
                                   PROP_EXISTING_IDENTITY,
                                   g_param_spec_string ("existing-identity",
                                                        "A GoaAccount identity",
                                                        "The user name with which we want to prefill the form",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
                                   PROP_PROVIDER,
                                   g_param_spec_object ("provider",
                                                        "A GoaProvider",
                                                        "The provider that is represented by this view",
                                                        GOA_TYPE_PROVIDER,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  signals[DENY_CLICK] = g_signal_new ("deny-click",
                                      G_TYPE_FROM_CLASS (klass),
                                      G_SIGNAL_RUN_LAST,
                                      0,
                                      NULL,
                                      NULL,
                                      g_cclosure_marshal_VOID__VOID,
                                      G_TYPE_NONE,
                                      0);

  signals[PASSWORD_SUBMIT] = g_signal_new ("password-submit",
                                           G_TYPE_FROM_CLASS (klass),
                                           G_SIGNAL_RUN_LAST,
                                           0,
                                           NULL,
                                           NULL,
                                           g_cclosure_marshal_VOID__STRING,
                                           G_TYPE_NONE,
                                           1,
                                           G_TYPE_STRING);
}

GtkWidget *
goa_web_view_new (GoaProvider *provider, const gchar *existing_identity)
{
  return g_object_new (GOA_TYPE_WEB_VIEW, "provider", provider, "existing-identity", existing_identity, NULL);
}

GtkWidget *
goa_web_view_get_view (GoaWebView *self)
{
  return self->web_view;
}

void
goa_web_view_fake_mobile (GoaWebView *self)
{
  WebKitSettings *settings;

  settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (self->web_view));

  /* This is based on the HTC Wildfire's user agent. Some
   * providers, like Google, refuse to provide the mobile
   * version of their authentication pages otherwise. eg.,
   * in Google's case, passing btmpl=mobile does not help.
   *
   * The actual user agent used by a HTC Wildfire is:
   * Mozilla/5.0 (Linux; U; Android 2.2.1; en-us; HTC Wildfire
   * Build/FRG83D) AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0
   * Mobile Safari/533.1
   *
   * Also note that the user agents of some mobile browsers may
   * not work. eg., Nokia N9.
   */
  webkit_settings_set_user_agent (settings,
                                  "Mozilla/5.0 (GNOME; not Android) "
                                  "AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile");
}
