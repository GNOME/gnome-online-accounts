/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011 Red Hat, Inc.
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup-gnome.h>
#include <webkit/webkit.h>

#include "goaprovider.h"
#include "goaoauth2provider.h"
#include "goawindowsliveprovider.h"

/**
 * GoaWindowsLiveProvider:
 *
 * The #GoaWindowsLiveProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaWindowsLiveProvider
{
  /*< private >*/
  GoaOAuth2Provider parent_instance;
};

typedef struct _GoaWindowsLiveProviderClass GoaWindowsLiveProviderClass;

struct _GoaWindowsLiveProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goawindowsliveprovider
 * @title: GoaWindowsLiveProvider
 * @short_description: A provider for Windows Live accounts
 *
 * #GoaWindowsLiveProvider is used for handling Windows Live accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaWindowsLiveProvider, goa_windows_live_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "windows_live",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "windows_live";
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Windows Live"));
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *provider)
{
  return "https://oauth.live.com/authorize";
}


static const gchar *
get_token_uri (GoaOAuth2Provider *provider)
{
  return "https://oauth.live.com/token";
}


static const gchar *
get_redirect_uri (GoaOAuth2Provider *provider)
{
  return "https://oauth.live.com/desktop";
}

static const gchar *
get_scope (GoaOAuth2Provider *provider)
{
  return "wl.messenger,"
         "wl.offline_access,"
         "wl.skydrive_update,"
         "wl.emails";
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 1;
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
  return GOA_WINDOWS_LIVE_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
  return NULL;
}

static const gchar *
get_authentication_cookie (GoaOAuth2Provider *provider)
{
  return "MSNPPAuth";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *id;
  gchar *presentation_identity;

  ret = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  id = NULL;
  presentation_identity = NULL;

  /* TODO: cancellable */

  proxy = rest_proxy_new ("https://apis.live.net/v5.0/me", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "access_token", access_token);

  if (!rest_proxy_call_sync (call, error))
    goto out;
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting guid, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   error))
    {
      g_prefix_error (error, _("Error parsing response as JSON: "));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  id = g_strdup (json_object_get_string_member (json_object, "id"));
  if (id == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find id member in JSON data"));
      goto out;
    }

  json_object = json_object_get_object_member (json_object, "emails");
  presentation_identity = g_strdup (json_object_get_string_member (json_object, "account"));
  if (presentation_identity == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find account email member in JSON data"));
      goto out;
    }

  ret = id;
  id = NULL;
  if (out_presentation_identity != NULL)
    {
      *out_presentation_identity = presentation_identity;
      presentation_identity = NULL;
    }

 out:
  g_free (id);
  g_free (presentation_identity);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuth2Provider *provider, WebKitDOMNode *node)
{
  WebKitDOMHTMLInputElement *input_element;
  gboolean ret;
  gchar *name;

  name = NULL;
  ret = FALSE;

  if (!WEBKIT_DOM_IS_HTML_INPUT_ELEMENT (node))
    goto out;

  input_element = WEBKIT_DOM_HTML_INPUT_ELEMENT (node);
  name = webkit_dom_html_input_element_get_name (input_element);
  if (g_strcmp0 (name, "submitNo") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (name);
  return ret;
}

static gboolean
is_identity_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret;
  gchar *element_type;
  gchar *name;

  element_type = NULL;
  name = NULL;

  ret = FALSE;

  /* FIXME: This does not show up in
   *        webkit_dom_document_get_elements_by_tag_name, but can be
   *        seen in the inspector. Needs further investigation.
   */

  g_object_get (element, "type", &element_type, NULL);
  if (g_strcmp0 (element_type, "email") != 0)
    goto out;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "login") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (element_type);
  g_free (name);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaChat *chat = NULL;
  GoaDocuments *documents;
  gboolean chat_enabled;
  gboolean documents_enabled;
  gboolean ret = FALSE;

  account = NULL;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_windows_live_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Chat */
  chat = goa_object_get_chat (GOA_OBJECT (object));
  chat_enabled = g_key_file_get_boolean (key_file, group, "ChatEnabled", NULL);
  if (chat_enabled)
    {
      if (chat == NULL)
        {
          chat = goa_chat_skeleton_new ();
          goa_object_skeleton_set_chat (object, chat);
        }
    }
  else
    {
      if (chat != NULL)
        goa_object_skeleton_set_chat (object, NULL);
    }

  /* Documents */
  documents = goa_object_get_documents (GOA_OBJECT (object));
  documents_enabled = g_key_file_get_boolean (key_file, group, "DocumentsEnabled", NULL);

  if (documents_enabled)
    {
      if (documents == NULL)
        {
          documents = goa_documents_skeleton_new ();
          goa_object_skeleton_set_documents (object, documents);
        }
    }
  else
    {
      if (documents != NULL)
        goa_object_skeleton_set_documents (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_chat_disabled (account, !chat_enabled);
      goa_account_set_documents_disabled (account, !documents_enabled);

      g_signal_connect (account,
                        "notify::chat-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ChatEnabled");
      g_signal_connect (account,
                        "notify::documents-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "DocumentsEnabled");
    }

  ret = TRUE;

 out:
  if (chat != NULL)
    g_object_unref (chat);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *left,
              GtkGrid             *right)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_windows_live_provider_parent_class)->show_account (provider,
                                                                             client,
                                                                             object,
                                                                             vbox,
                                                                             left,
                                                                             right);

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   _("Use for"),
                                                   "chat-disabled",
                                                   _("C_hat"));

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   NULL,
                                                   "documents-disabled",
                                                   _("_Documents"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "ChatEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "DocumentsEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_windows_live_provider_init (GoaWindowsLiveProvider *client)
{
}

static void
goa_windows_live_provider_class_init (GoaWindowsLiveProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authorization_uri    = get_authorization_uri;
  oauth2_class->get_token_uri            = get_token_uri;
  oauth2_class->get_redirect_uri         = get_redirect_uri;
  oauth2_class->get_scope                = get_scope;
  oauth2_class->get_client_id            = get_client_id;
  oauth2_class->get_client_secret        = get_client_secret;
  oauth2_class->get_authentication_cookie = get_authentication_cookie;
  oauth2_class->get_identity_sync        = get_identity_sync;
  oauth2_class->is_deny_node             = is_deny_node;
  oauth2_class->is_identity_node         = is_identity_node;
  oauth2_class->add_account_key_values   = add_account_key_values;
}
