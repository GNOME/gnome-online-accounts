/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
 * Copyright © 2011 Collabora Ltd.
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

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goawindowsliveprovider.h"
#include "goaobjectskeletonutils.h"
#include "goarestproxy.h"

struct _GoaWindowsLiveProvider
{
  GoaOAuth2Provider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaWindowsLiveProvider, goa_windows_live_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_WINDOWS_LIVE_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_WINDOWS_LIVE_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_strdup (_("Microsoft"));
}

static GIcon *
get_provider_icon (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-msn");
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_MAIL |
         GOA_PROVIDER_FEATURE_DOCUMENTS;
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://login.live.com/oauth20_authorize.srf";
}


static const gchar *
get_token_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://login.live.com/oauth20_token.srf";
}


static const gchar *
get_redirect_uri (GoaOAuth2Provider *oauth2_provider)
{
  return "https://login.live.com/oauth20_desktop.srf";
}

static const gchar *
get_scope (GoaOAuth2Provider *oauth2_provider)
{
  return "wl.imap,"
         "wl.offline_access,"
         "wl.skydrive_update,"
         "wl.emails";
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 3;
}

static const gchar *
get_client_id (GoaOAuth2Provider *oauth2_provider)
{
  return GOA_WINDOWS_LIVE_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *oauth2_provider)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *oauth2_provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  GError *identity_error = NULL;
  RestProxy *proxy = NULL;
  RestProxyCall *call = NULL;
  JsonParser *parser = NULL;
  JsonObject *json_object;
  gchar *ret = NULL;
  gchar *id = NULL;
  gchar *presentation_identity = NULL;

  /* TODO: cancellable */

  proxy = goa_rest_proxy_new ("https://apis.live.net/v5.0/me", FALSE);
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
                   _("Expected status 200 when requesting your identity, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   &identity_error))
    {
      g_warning ("json_parser_load_from_data() failed: %s (%s, %d)",
                 identity_error->message,
                 g_quark_to_string (identity_error->domain),
                 identity_error->code);
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  if (!json_object_has_member (json_object, "id"))
    {
      g_warning ("Did not find id in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  if (!json_object_has_member (json_object, "emails"))
    {
      g_warning ("Did not find emails in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  id = g_strdup (json_object_get_string_member (json_object, "id"));

  json_object = json_object_get_object_member (json_object, "emails");
  if (!json_object_has_member (json_object, "account"))
    {
      g_warning ("Did not find emails.account in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  presentation_identity = g_strdup (json_object_get_string_member (json_object, "account"));

  ret = id;
  id = NULL;
  if (out_presentation_identity != NULL)
    {
      *out_presentation_identity = presentation_identity;
      presentation_identity = NULL;
    }

 out:
  g_clear_object (&parser);
  g_clear_error (&identity_error);
  g_clear_object (&call);
  g_clear_object (&proxy);
  g_free (id);
  g_free (presentation_identity);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_identity_node (GoaOAuth2Provider *oauth2_provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret = FALSE;
  gchar *element_type = NULL;
  gchar *name = NULL;

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
  GoaAccount *account = NULL;
  GoaMail *mail = NULL;
  gboolean mail_enabled;
  gboolean documents_enabled;
  gboolean ret = FALSE;
  const gchar *email_address;

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
  email_address = goa_account_get_presentation_identity (account);

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail),
                        "email-address",   email_address,
                        "imap-supported",  TRUE,
                        "imap-host",       "imap-mail.outlook.com",
                        "imap-user-name",  email_address,
                        "imap-use-ssl",    TRUE,
                        "smtp-supported",  TRUE,
                        "smtp-host",       "smtp-mail.outlook.com",
                        "smtp-user-name",  email_address,
                        "smtp-use-auth",   TRUE,
                        "smtp-auth-xoauth2", TRUE,
                        "smtp-use-tls",    TRUE,
                        NULL);
          goa_object_skeleton_set_mail (object, mail);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  /* Documents */
  documents_enabled = g_key_file_get_boolean (key_file, group, "DocumentsEnabled", NULL);
  goa_object_skeleton_attach_documents (object, documents_enabled);

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_documents_disabled (account, !documents_enabled);

      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "MailEnabled");
      g_signal_connect (account,
                        "notify::documents-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "DocumentsEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&mail);
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider *oauth2_provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "DocumentsEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_windows_live_provider_init (GoaWindowsLiveProvider *self)
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
  provider_class->get_provider_icon          = get_provider_icon;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object               = build_object;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authorization_uri    = get_authorization_uri;
  oauth2_class->get_token_uri            = get_token_uri;
  oauth2_class->get_redirect_uri         = get_redirect_uri;
  oauth2_class->get_scope                = get_scope;
  oauth2_class->get_client_id            = get_client_id;
  oauth2_class->get_client_secret        = get_client_secret;
  oauth2_class->get_identity_sync        = get_identity_sync;
  oauth2_class->is_identity_node         = is_identity_node;
  oauth2_class->add_account_key_values   = add_account_key_values;
}
