/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2012, 2013, 2014 Red Hat, Inc.
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
#include <libsoup/soup.h>
#include <webkit/webkit.h>

#include "goawebview.h"
#include "nautilus-floating-bar.h"

struct _GoaWebViewPrivate
{
  GtkWidget *floating_bar;
  GtkWidget *progress_bar;
  GtkWidget *web_view;
  gboolean status;
  gulong clear_notify_progress_id;
  gulong notify_load_status_id;
  gulong notify_progress_id;
};

#define GOA_WEB_VIEW_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOA_TYPE_WEB_VIEW, GoaWebViewPrivate))

G_DEFINE_TYPE (GoaWebView, goa_web_view, GTK_TYPE_OVERLAY)

static gboolean
web_view_clear_notify_progress_cb (gpointer user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);
  GoaWebViewPrivate *priv = self->priv;

  gtk_widget_hide (priv->progress_bar);
  priv->clear_notify_progress_id = 0;
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
  GoaWebViewPrivate *priv = self->priv;

  nautilus_floating_bar_set_label (NAUTILUS_FLOATING_BAR (priv->floating_bar), text);

  if (text == NULL || text[0] == '\0')
    {
      gtk_widget_hide (priv->floating_bar);
      gtk_widget_set_halign (priv->floating_bar, GTK_ALIGN_START);
    }
  else
    gtk_widget_show (priv->floating_bar);
}

static gboolean
web_view_is_loading (GoaWebView *self)
{
  GoaWebViewPrivate *priv = self->priv;
  WebKitLoadStatus status;

  status = webkit_web_view_get_load_status (WEBKIT_WEB_VIEW (priv->web_view));

  if ((priv->status == WEBKIT_LOAD_FINISHED || priv->status == WEBKIT_LOAD_FAILED)
      && status != WEBKIT_LOAD_PROVISIONAL)
    return FALSE;

  priv->status = status;
  return status != WEBKIT_LOAD_FINISHED && status != WEBKIT_LOAD_FAILED;
}

static void
web_view_log_printer (SoupLogger         *logger,
                      SoupLoggerLogLevel  level,
                      gchar               direction,
                      const gchar        *data,
                      gpointer            user_data)
{
  gchar *message;

  message = g_strdup_printf ("%c %s", direction, data);
  g_log_default_handler ("goa", G_LOG_LEVEL_DEBUG, message, NULL);
  g_free (message);
}

static void
web_view_notify_load_status_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  WebKitLoadStatus status;

  status = webkit_web_view_get_load_status (web_view);
  switch (status)
    {
    case WEBKIT_LOAD_PROVISIONAL:
      {
        WebKitNetworkRequest *request;
        WebKitWebDataSource *source;
        WebKitWebFrame *frame;
        const gchar *uri;
        gchar *title;

        frame = webkit_web_view_get_main_frame (web_view);
        source = webkit_web_frame_get_provisional_data_source (frame);
        request = webkit_web_data_source_get_initial_request (source);
        uri = webkit_network_request_get_uri (request);
        title = web_view_create_loading_title (uri);

        web_view_floating_bar_update (self, title);
        g_free (title);
        break;
      }

    case WEBKIT_LOAD_FAILED:
    case WEBKIT_LOAD_FINISHED:
      web_view_floating_bar_update (self, NULL);
      break;

    default:
      break;
    }
}

static void
web_view_notify_progress_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GoaWebView *self = GOA_WEB_VIEW (user_data);
  GoaWebViewPrivate *priv = self->priv;
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  gboolean loading;
  const gchar *uri;
  gdouble progress;

  if (priv->clear_notify_progress_id != 0)
    {
      g_source_remove (priv->clear_notify_progress_id);
      priv->clear_notify_progress_id = 0;
    }

  uri = webkit_web_view_get_uri (web_view);
  if (!uri || g_str_equal (uri, "about:blank"))
    return;

  progress = webkit_web_view_get_progress (WEBKIT_WEB_VIEW (priv->web_view));
  loading = web_view_is_loading (self);

  if (progress == 1.0 || !loading)
    priv->clear_notify_progress_id = g_timeout_add (500, web_view_clear_notify_progress_cb, self);
  else
    gtk_widget_show (priv->progress_bar);

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progress_bar),
                                 (loading || progress == 1.0) ? progress : 0.0);
}

static void
goa_web_view_dispose (GObject *object)
{
  GoaWebView *self = GOA_WEB_VIEW (object);
  GoaWebViewPrivate *priv = self->priv;

  if (priv->clear_notify_progress_id != 0)
    {
      g_source_remove (priv->clear_notify_progress_id);
      priv->clear_notify_progress_id = 0;
    }

  if (priv->notify_load_status_id != 0)
    {
      g_signal_handler_disconnect (priv->web_view, priv->notify_load_status_id);
      priv->notify_load_status_id = 0;
    }

  if (priv->notify_progress_id != 0)
    {
      g_signal_handler_disconnect (priv->web_view, priv->notify_progress_id);
      priv->notify_progress_id = 0;
    }

  G_OBJECT_CLASS (goa_web_view_parent_class)->dispose (object);
}

#ifdef GOA_INSPECTOR_ENABLED
static WebKitWebView *
web_inspector_inspect_web_view_cb (WebKitWebInspector *inspector,
                                   WebKitWebView      *web_view,
                                   gpointer            user_data)
{
  GtkWidget *inspector_web_view;
  GtkWidget *scrolled_window;
  GtkWidget *window;

  inspector_web_view = webkit_web_view_new ();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_resize (GTK_WINDOW (window), 800, 600);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  gtk_container_add (GTK_CONTAINER (window), scrolled_window);
  gtk_container_add (GTK_CONTAINER (scrolled_window), inspector_web_view);

  g_object_set_data (G_OBJECT (inspector), "window", window);

  return WEBKIT_WEB_VIEW (inspector_web_view);
}

static gboolean
web_inspector_show_window_cb (WebKitWebInspector *inspector,
                              gpointer            user_data)
{
  GtkWidget *window;
  GtkWindowGroup *group;

  group = gtk_window_group_new ();

  window = g_object_get_data (G_OBJECT (inspector), "window");
  gtk_window_group_add_window (group, GTK_WINDOW (window));
  gtk_widget_show_all (window);
  gtk_window_present (GTK_WINDOW (window));

  g_object_unref (group);
  return GDK_EVENT_STOP;
}
#endif /* GOA_INSPECTOR_ENABLED */

static void
goa_web_view_init (GoaWebView *self)
{
  GoaWebViewPrivate *priv;
  GtkWidget *scrolled_window;
  SoupCookieJar *cookie_jar;
  SoupLogger *logger;
  SoupSession *session;
  WebKitWebSettings *settings;

  self->priv = GOA_WEB_VIEW_GET_PRIVATE (self);
  priv = self->priv;

  session = webkit_get_default_session ();
  g_object_set (session, SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE, SOUP_SESSION_SSL_STRICT, TRUE, NULL);

  soup_session_add_feature_by_type (session, SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
  g_object_set (session, "accept-language-auto", TRUE, NULL);

  soup_session_remove_feature_by_type (session, SOUP_TYPE_COOKIE_JAR);
  cookie_jar = soup_cookie_jar_new ();
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (cookie_jar));
  g_object_unref (cookie_jar);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_logger_set_printer (logger, web_view_log_printer, NULL, NULL);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (scrolled_window, 500, 400);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (self), scrolled_window);

  priv->web_view = webkit_web_view_new ();
  priv->status = WEBKIT_LOAD_PROVISIONAL;
  gtk_container_add (GTK_CONTAINER (scrolled_window), priv->web_view);

  settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (priv->web_view));
  g_object_set (settings, "user-stylesheet-uri", "file://" PACKAGE_DATA_DIR "/goawebview.css", NULL);

#ifdef GOA_INSPECTOR_ENABLED
  {
    WebKitWebInspector *inspector;

    /* Setup the inspector */
    g_object_set (settings, "enable-developer-extras", TRUE, NULL);
    inspector = webkit_web_view_get_inspector (WEBKIT_WEB_VIEW (priv->web_view));
    webkit_web_inspector_show (WEBKIT_WEB_INSPECTOR (inspector));

    g_signal_connect (inspector, "inspect-web-view", G_CALLBACK (web_inspector_inspect_web_view_cb), NULL);
    g_signal_connect (inspector, "show-window", G_CALLBACK (web_inspector_show_window_cb), NULL);
  }
#endif /* GOA_INSPECTOR_ENABLED */

  /* statusbar is hidden by default */
  priv->floating_bar = nautilus_floating_bar_new (NULL, FALSE);
  gtk_widget_set_halign (priv->floating_bar, GTK_ALIGN_START);
  gtk_widget_set_valign (priv->floating_bar, GTK_ALIGN_END);
  gtk_widget_set_no_show_all (priv->floating_bar, TRUE);
  gtk_overlay_add_overlay (GTK_OVERLAY (self), priv->floating_bar);

  priv->progress_bar = gtk_progress_bar_new ();
  gtk_style_context_add_class (gtk_widget_get_style_context (priv->progress_bar),
                               GTK_STYLE_CLASS_OSD);
  gtk_widget_set_halign (priv->progress_bar, GTK_ALIGN_FILL);
  gtk_widget_set_valign (priv->progress_bar, GTK_ALIGN_START);
  gtk_overlay_add_overlay (GTK_OVERLAY (self), priv->progress_bar);

  priv->notify_progress_id = g_signal_connect (priv->web_view,
                                               "notify::progress",
                                               G_CALLBACK (web_view_notify_progress_cb),
                                               self);
  priv->notify_load_status_id = g_signal_connect (priv->web_view,
                                                  "notify::load-status",
                                                  G_CALLBACK (web_view_notify_load_status_cb),
                                                  self);
}

static void
goa_web_view_class_init (GoaWebViewClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);
  object_class->dispose = goa_web_view_dispose;

  g_type_class_add_private (object_class, sizeof (GoaWebViewPrivate));
}

GtkWidget *
goa_web_view_new (void)
{
  return g_object_new (GOA_TYPE_WEB_VIEW, NULL);
}

GtkWidget *
goa_web_view_get_view (GoaWebView *self)
{
  return self->priv->web_view;
}

void
goa_web_view_fake_mobile (GoaWebView *self)
{
  WebKitWebSettings *settings;

  settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (self->priv->web_view));

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
  g_object_set (G_OBJECT (settings),
                "user-agent", "Mozilla/5.0 (GNOME; not Android) "
                              "AppleWebKit/533.1 (KHTML, like Gecko) Version/4.0 Mobile",
                NULL);
}

void
goa_web_view_add_cookies (GoaWebView *self,
                          GSList     *cookies)
{
  SoupCookieJar *cookie_jar;
  SoupSession *session;
  GSList *l;

  session = webkit_get_default_session ();
  cookie_jar = SOUP_COOKIE_JAR (soup_session_get_feature (session, SOUP_TYPE_COOKIE_JAR));

  for (l = cookies; l != NULL; l = l->next)
    {
      SoupCookie *cookie = l->data;
      soup_cookie_jar_add_cookie (cookie_jar, soup_cookie_copy (cookie));
    }
}
