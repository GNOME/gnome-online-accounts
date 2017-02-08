/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2015 Damián Nohales
 * Copyright © 2015 – 2017 Red Hat, Inc.
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

#include <webkitdom/webkitdom.h>

#include "goaoauthprovider.h"
#include "goaoauth2provider.h"
#include "goaoauth2provider-web-extension.h"
#include "goaprovider.h"
#include "goawebextension.h"

struct _GoaWebExtension
{
  GObject parent;
  GoaProvider *provider;
  WebKitWebExtension *wk_extension;
  gchar *existing_identity;
  gchar *provider_type;
};

struct _GoaWebExtensionClass
{
  GObjectClass parent;
};

enum
{
  PROP_0,
  PROP_EXISTING_IDENTITY,
  PROP_PROVIDER_TYPE,
  PROP_WK_EXTENSION
};

G_DEFINE_TYPE (GoaWebExtension, goa_web_extension, G_TYPE_OBJECT)

static void
web_extension_dom_node_deny_click_cb (WebKitDOMNode *element, WebKitDOMEvent *event, gpointer user_data)
{
  WebKitDOMDOMWindow *dom_window = WEBKIT_DOM_DOM_WINDOW (user_data);
  webkit_dom_dom_window_webkit_message_handlers_post_message (dom_window, "deny-click", "");
}

static void
web_extension_dom_node_password_submit_cb (WebKitDOMNode *element, WebKitDOMEvent *event, gpointer user_data)
{
  WebKitDOMDOMWindow *dom_window = WEBKIT_DOM_DOM_WINDOW (user_data);
  WebKitDOMHTMLInputElement *password_node;
  gchar *password;

  password_node = WEBKIT_DOM_HTML_INPUT_ELEMENT (g_object_get_data (G_OBJECT (dom_window), "goa-password-node"));
  password = webkit_dom_html_input_element_get_value (password_node);
  webkit_dom_dom_window_webkit_message_handlers_post_message (dom_window, "password-submit", password);
  g_free (password);
}

static void
web_extension_document_loaded_cb (WebKitWebPage *web_page, gpointer user_data)
{
  GoaWebExtension *self = GOA_WEB_EXTENSION (user_data);
  WebKitDOMDocument *document;
  WebKitDOMDOMWindow *dom_window;
  WebKitDOMNodeList *elements;
  gulong element_count;
  gulong i;

  document = webkit_web_page_get_dom_document (web_page);
  elements = webkit_dom_document_get_elements_by_tag_name (document, "*");
  element_count = webkit_dom_node_list_get_length (elements);

  dom_window = webkit_dom_document_get_default_view (document);

  for (i = 0; i < element_count; i++)
    {
      WebKitDOMNode *element = webkit_dom_node_list_item (elements, i);

      if ((GOA_IS_OAUTH_PROVIDER (self->provider)
           && goa_oauth_provider_is_deny_node (GOA_OAUTH_PROVIDER (self->provider), element))
          || (GOA_IS_OAUTH2_PROVIDER (self->provider)
              && goa_oauth2_provider_is_deny_node (GOA_OAUTH2_PROVIDER (self->provider), element)))
        {
          webkit_dom_event_target_add_event_listener (WEBKIT_DOM_EVENT_TARGET (element),
                                                      "click",
                                                      G_CALLBACK (web_extension_dom_node_deny_click_cb),
                                                      FALSE,
                                                      dom_window);
        }
      else if (self->existing_identity != NULL
               && self->existing_identity[0] != '\0'
               && WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (element)
               && ((GOA_IS_OAUTH_PROVIDER (self->provider)
                    && goa_oauth_provider_is_identity_node (GOA_OAUTH_PROVIDER (self->provider),
                                                            WEBKIT_DOM_HTML_INPUT_ELEMENT (element)))
                   || (GOA_IS_OAUTH2_PROVIDER (self->provider)
                       && goa_oauth2_provider_is_identity_node (GOA_OAUTH2_PROVIDER (self->provider),
                                                                WEBKIT_DOM_HTML_INPUT_ELEMENT (element)))))
        {
          webkit_dom_html_input_element_set_value (WEBKIT_DOM_HTML_INPUT_ELEMENT (element),
                                                   self->existing_identity);
          webkit_dom_html_input_element_set_read_only (WEBKIT_DOM_HTML_INPUT_ELEMENT (element), TRUE);
        }
      else if (WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (element)
               && ((GOA_IS_OAUTH_PROVIDER (self->provider)
                   && goa_oauth_provider_is_password_node (GOA_OAUTH_PROVIDER (self->provider),
                                                           WEBKIT_DOM_HTML_INPUT_ELEMENT (element)))
                   || (GOA_IS_OAUTH2_PROVIDER (self->provider)
                       && goa_oauth2_provider_is_password_node (GOA_OAUTH2_PROVIDER (self->provider),
                                                                WEBKIT_DOM_HTML_INPUT_ELEMENT (element)))))
        {
          WebKitDOMHTMLFormElement *form;

          form = webkit_dom_html_input_element_get_form (WEBKIT_DOM_HTML_INPUT_ELEMENT (element));
          if (form != NULL)
            {
              g_object_set_data_full (G_OBJECT (dom_window),
                                      "goa-password-node",
                                      g_object_ref (element),
                                      g_object_unref);
              webkit_dom_event_target_add_event_listener (WEBKIT_DOM_EVENT_TARGET (form),
                                                          "submit",
                                                          G_CALLBACK (web_extension_dom_node_password_submit_cb),
                                                          FALSE,
                                                          dom_window);
            }
        }
    }
}

static void
web_extension_page_created_cb (GoaWebExtension *self, WebKitWebPage *web_page)
{
  g_signal_connect_object (web_page, "document-loaded", G_CALLBACK (web_extension_document_loaded_cb), self, 0);
}

static void
goa_web_extension_constructed (GObject *object)
{
  GoaWebExtension *self = GOA_WEB_EXTENSION (object);

  G_OBJECT_CLASS (goa_web_extension_parent_class)->constructed (object);

  self->provider = goa_provider_get_for_provider_type (self->provider_type);

  g_signal_connect_object (self->wk_extension,
                           "page-created",
                           G_CALLBACK (web_extension_page_created_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

static void
goa_web_extension_dispose (GObject *object)
{
  GoaWebExtension *self = GOA_WEB_EXTENSION (object);

  g_clear_object (&self->provider);
  g_clear_object (&self->wk_extension);

  G_OBJECT_CLASS (goa_web_extension_parent_class)->dispose (object);
}

static void
goa_web_extension_finalize (GObject *object)
{
  GoaWebExtension *self = GOA_WEB_EXTENSION (object);

  g_free (self->existing_identity);
  g_free (self->provider_type);

  G_OBJECT_CLASS (goa_web_extension_parent_class)->finalize (object);
}

static void
goa_web_extension_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GoaWebExtension *self = GOA_WEB_EXTENSION (object);

  switch (prop_id)
    {
    case PROP_EXISTING_IDENTITY:
      self->existing_identity = g_value_dup_string (value);
      break;

    case PROP_PROVIDER_TYPE:
      self->provider_type = g_value_dup_string (value);
      break;

    case PROP_WK_EXTENSION:
      self->wk_extension = WEBKIT_WEB_EXTENSION (g_value_dup_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_web_extension_class_init (GoaWebExtensionClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = goa_web_extension_constructed;
  object_class->dispose = goa_web_extension_dispose;
  object_class->finalize = goa_web_extension_finalize;
  object_class->set_property = goa_web_extension_set_property;

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
                                   PROP_PROVIDER_TYPE,
                                   g_param_spec_string ("provider-type",
                                                        "A GoaProvider type",
                                                        "The provider type that is represented by this view",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
                                   PROP_WK_EXTENSION,
                                   g_param_spec_object ("wk-extension",
                                                        "A WebKitWebExtension",
                                                        "The associated WebKitWebExtension",
                                                        WEBKIT_TYPE_WEB_EXTENSION,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
goa_web_extension_init (GoaWebExtension *self)
{
}

GoaWebExtension *
goa_web_extension_new (WebKitWebExtension *wk_extension,
                       const gchar *provider_type,
                       const gchar *existing_identity)
{
  return g_object_new (GOA_TYPE_WEB_EXTENSION,
                       "existing-identity", existing_identity,
                       "provider-type", provider_type,
                       "wk-extension", wk_extension,
                       NULL);
}
