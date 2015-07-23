/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012, 2013, 2014 Red Hat, Inc.
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

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauth2provider.h"
#include "goagoogleprovider.h"

/**
 * GoaGoogleProvider:
 *
 * The #GoaGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaGoogleProvider
{
  /*< private >*/
  GoaOAuth2Provider parent_instance;
};

typedef struct _GoaGoogleProviderClass GoaGoogleProviderClass;

struct _GoaGoogleProviderClass
{
  GoaOAuth2ProviderClass parent_class;
};

/**
 * SECTION:goagoogleprovider
 * @title: GoaGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaGoogleProvider, goa_google_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 GOA_GOOGLE_NAME,
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return GOA_GOOGLE_NAME;
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Google"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_MAIL |
         GOA_PROVIDER_FEATURE_CALENDAR |
         GOA_PROVIDER_FEATURE_CONTACTS |
         GOA_PROVIDER_FEATURE_CHAT |
         GOA_PROVIDER_FEATURE_DOCUMENTS |
         GOA_PROVIDER_FEATURE_PHOTOS |
         GOA_PROVIDER_FEATURE_PRINTERS;
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/auth";
}

static const gchar *
get_token_uri (GoaOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/token";
}

static const gchar *
get_redirect_uri (GoaOAuth2Provider *provider)
{
  return "http://localhost";
}

static const gchar *
get_scope (GoaOAuth2Provider *provider)
{
  return /* Read-only access to the user's email address */
         "https://www.googleapis.com/auth/userinfo.email "

         /* Name and picture */
         "https://www.googleapis.com/auth/userinfo.profile "

         /* Google Calendar API (CalDAV and GData) */
         "https://www.googleapis.com/auth/calendar "

         /* Google Contacts API (GData) */
         "https://www.google.com/m8/feeds/ "

         /* Google Contacts API (CardDAV) - undocumented */
         "https://www.googleapis.com/auth/carddav "

         /* Google Documents List Data API */
         "https://docs.google.com/feeds/ "
         "https://docs.googleusercontent.com/ "
         "https://spreadsheets.google.com/feeds/ "

         /* Google PicasaWeb API (GData) */
         "https://picasaweb.google.com/data/ "

         /* GMail IMAP and SMTP access */
         "https://mail.google.com/ "

         /* Google Cloud Print */
         "https://www.googleapis.com/auth/cloudprint "

         /* Google Talk */
         "https://www.googleapis.com/auth/googletalk "

         /* Google Tasks - undocumented */
         "https://www.googleapis.com/auth/tasks";
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 9;
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
  return GOA_GOOGLE_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
  return GOA_GOOGLE_CLIENT_SECRET;
}

static const gchar *
get_authentication_cookie (GoaOAuth2Provider *provider)
{
  return "LSID";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  GError *identity_error;
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  gchar *ret;
  gchar *email;

  ret = NULL;

  identity_error = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  email = NULL;

  /* TODO: cancellable */

  proxy = rest_proxy_new ("https://www.googleapis.com/oauth2/v2/userinfo", FALSE);
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
  email = g_strdup (json_object_get_string_member (json_object, "email"));
  if (email == NULL)
    {
      g_warning ("Did not find email in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }


  ret = email;
  email = NULL;
  if (out_presentation_identity != NULL)
    *out_presentation_identity = g_strdup (ret); /* for now: use email as presentation identity */

 out:
  g_clear_object (&parser);
  g_clear_error (&identity_error);
  g_clear_object (&call);
  g_clear_object (&proxy);
  g_free (email);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
is_deny_node (GoaOAuth2Provider *provider, WebKitDOMNode *node)
{
  return FALSE;
}

static gboolean
is_identity_node (GoaOAuth2Provider *provider, WebKitDOMHTMLInputElement *element)
{
  gboolean ret;
  gchar *element_type;
  gchar *id;
  gchar *name;

  element_type = NULL;
  id = NULL;
  name = NULL;

  ret = FALSE;

  g_object_get (element, "type", &element_type, NULL);
  if (g_strcmp0 (element_type, "email") != 0)
    goto out;

  id = webkit_dom_element_get_id (WEBKIT_DOM_ELEMENT (element));
  if (g_strcmp0 (id, "Email") != 0)
    goto out;

  name = webkit_dom_html_input_element_get_name (element);
  if (g_strcmp0 (name, "Email") != 0)
    goto out;

  ret = TRUE;

 out:
  g_free (element_type);
  g_free (id);
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
  GoaMail *mail;
  GoaCalendar *calendar;
  GoaContacts *contacts;
  GoaChat *chat;
  GoaDocuments *documents;
  GoaPhotos *photos;
  GoaPrinters *printers;
  gboolean ret;
  gboolean mail_enabled;
  gboolean calendar_enabled;
  gboolean contacts_enabled;
  gboolean chat_enabled;
  gboolean documents_enabled;
  gboolean photos_enabled;
  gboolean printers_enabled;
  const gchar *email_address;

  account = NULL;
  mail = NULL;
  calendar = NULL;
  contacts = NULL;
  chat = NULL;
  documents = NULL;
  photos = NULL;
  printers = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  email_address = goa_account_get_identity (account);

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
                        "imap-host",       "imap.gmail.com",
                        "imap-user-name",  email_address,
                        "imap-use-ssl",    TRUE,
                        "smtp-supported",  TRUE,
                        "smtp-host",       "smtp.gmail.com",
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

  /* Calendar */
  calendar = goa_object_get_calendar (GOA_OBJECT (object));
  calendar_enabled = g_key_file_get_boolean (key_file, group, "CalendarEnabled", NULL);
  if (calendar_enabled)
    {
      if (calendar == NULL)
        {
          gchar *uri_caldav;

          uri_caldav = g_strconcat ("https://apidata.googleusercontent.com/caldav/v2/",
                                    email_address,
                                    "/user",
                                    NULL);

          calendar = goa_calendar_skeleton_new ();
          g_object_set (G_OBJECT (calendar),
                        "uri", uri_caldav,
                        NULL);
          goa_object_skeleton_set_calendar (object, calendar);
          g_free (uri_caldav);
        }
    }
  else
    {
      if (calendar != NULL)
        goa_object_skeleton_set_calendar (object, NULL);
    }

  /* Contacts */
  contacts = goa_object_get_contacts (GOA_OBJECT (object));
  contacts_enabled = g_key_file_get_boolean (key_file, group, "ContactsEnabled", NULL);
  if (contacts_enabled)
    {
      if (contacts == NULL)
        {
          contacts = goa_contacts_skeleton_new ();
          g_object_set (G_OBJECT (contacts),
                        "uri", "https://www.googleapis.com/.well-known/carddav",
                        NULL);
          goa_object_skeleton_set_contacts (object, contacts);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }

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

  /* Photos */
  photos = goa_object_get_photos (GOA_OBJECT (object));
  photos_enabled = g_key_file_get_boolean (key_file, group, "PhotosEnabled", NULL);

  if (photos_enabled)
    {
      if (photos == NULL)
        {
          photos = goa_photos_skeleton_new ();
          goa_object_skeleton_set_photos (object, photos);
        }
    }
  else
    {
      if (photos != NULL)
        goa_object_skeleton_set_photos (object, NULL);
    }

  /* Printers */
  printers = goa_object_get_printers (GOA_OBJECT (object));
  printers_enabled = g_key_file_get_boolean (key_file, group, "PrintersEnabled", NULL);

  if (printers_enabled)
    {
      if (printers == NULL)
        {
          printers = goa_printers_skeleton_new ();
          goa_object_skeleton_set_printers (object, printers);
        }
    }
  else
    {
      if (printers != NULL)
        goa_object_skeleton_set_printers (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_mail_disabled (account, !mail_enabled);
      goa_account_set_calendar_disabled (account, !calendar_enabled);
      goa_account_set_contacts_disabled (account, !contacts_enabled);
      goa_account_set_chat_disabled (account, !chat_enabled);
      goa_account_set_documents_disabled (account, !documents_enabled);
      goa_account_set_photos_disabled (account, !photos_enabled);
      goa_account_set_printers_disabled (account, !printers_enabled);

      g_signal_connect (account,
                        "notify::mail-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "MailEnabled");
      g_signal_connect (account,
                        "notify::calendar-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "CalendarEnabled");
      g_signal_connect (account,
                        "notify::contacts-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ContactsEnabled");
      g_signal_connect (account,
                        "notify::chat-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ChatEnabled");
      g_signal_connect (account,
                        "notify::documents-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "DocumentsEnabled");
      g_signal_connect (account,
                        "notify::photos-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "PhotosEnabled");
      g_signal_connect (account,
                        "notify::printers-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "PrintersEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&printers);
  g_clear_object (&photos);
  g_clear_object (&documents);
  g_clear_object (&chat);
  g_clear_object (&contacts);
  g_clear_object (&calendar);
  g_clear_object (&mail);
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_mobile_browser (GoaOAuth2Provider *provider)
{
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *grid,
              G_GNUC_UNUSED GtkGrid *dummy)
{
  gint row;

  row = 0;

  goa_util_add_account_info (grid, row++, object);

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   /* Translators: This is a label for a series of
                                                    * options switches. For example: “Use for Mail”. */
                                                   _("Use for"),
                                                   "mail-disabled",
                                                   _("_Mail"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "calendar-disabled",
                                                   _("Cale_ndar"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "contacts-disabled",
                                                   _("_Contacts"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "chat-disabled",
                                                   _("C_hat"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "documents-disabled",
                                                   _("_Documents"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "photos-disabled",
                                                   _("_Photos"));

  goa_util_add_row_switch_from_keyfile_with_blurb (grid, row++, object,
                                                   NULL,
                                                   "printers-disabled",
                                                   _("Prin_ters"));
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuth2Provider  *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "ContactsEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "ChatEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "DocumentsEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "PhotosEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "PrintersEnabled", "true");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_google_provider_init (GoaGoogleProvider *client)
{
}

static void
goa_google_provider_class_init (GoaGoogleProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authentication_cookie = get_authentication_cookie;
  oauth2_class->get_authorization_uri     = get_authorization_uri;
  oauth2_class->get_client_id             = get_client_id;
  oauth2_class->get_client_secret         = get_client_secret;
  oauth2_class->get_identity_sync         = get_identity_sync;
  oauth2_class->get_redirect_uri          = get_redirect_uri;
  oauth2_class->get_scope                 = get_scope;
  oauth2_class->is_deny_node              = is_deny_node;
  oauth2_class->is_identity_node          = is_identity_node;
  oauth2_class->get_token_uri             = get_token_uri;
  oauth2_class->get_use_mobile_browser    = get_use_mobile_browser;
  oauth2_class->add_account_key_values    = add_account_key_values;
}
