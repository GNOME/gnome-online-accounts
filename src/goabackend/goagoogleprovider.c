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

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goaprovider.h"
#include "goaoauthprovider.h"
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
  GoaOAuthProvider parent_instance;
};

typedef struct _GoaGoogleProviderClass GoaGoogleProviderClass;

struct _GoaGoogleProviderClass
{
  GoaOAuthProviderClass parent_class;
};

/**
 * SECTION:goagoogleprovider
 * @title: GoaGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaGoogleProvider, goa_google_provider, GOA_TYPE_OAUTH_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "google";
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Google"));
}

static const gchar *
get_consumer_key (GoaOAuthProvider *provider)
{
  return GOA_GOOGLE_CONSUMER_KEY;
}

static const gchar *
get_consumer_secret (GoaOAuthProvider *provider)
{
  return GOA_GOOGLE_CONSUMER_SECRET;
}

static const gchar *
get_request_uri (GoaOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthGetRequestToken";
}

static gchar **
get_request_uri_params (GoaOAuthProvider *provider)
{
  GPtrArray *p;
  p = g_ptr_array_new ();
  g_ptr_array_add (p, g_strdup ("xoauth_displayname"));
  g_ptr_array_add (p, g_strdup ("GNOME"));

  g_ptr_array_add (p, g_strdup ("scope"));
  g_ptr_array_add (p, g_strdup (
    /* Display email address: cf. https://sites.google.com/site/oauthgoog/Home/emaildisplayscope */
    "https://www.googleapis.com/auth/userinfo#email "
    /* IMAP, SMTP access: http://code.google.com/apis/gmail/oauth/protocol.html */
    "https://mail.google.com/ "
    /* Calendar data API: http://code.google.com/apis/calendar/data/2.0/developers_guide.html */
    "https://www.google.com/calendar/feeds "
    /* Contacts API: http://code.google.com/apis/contacts/docs/3.0/developers_guide.html */
    "https://www.google.com/m8/feeds/"));
  g_ptr_array_add (p, NULL);

  /* NOTE: Increase the number returned in get_crededentials_generation if adding scopes */

  return (gchar **) g_ptr_array_free (p, FALSE);
}

static guint
get_credentials_generation (GoaProvider *provider)
{
  return 1;
}

static const gchar *
get_authorization_uri (GoaOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthAuthorizeToken";
}

static const gchar *
get_token_uri (GoaOAuthProvider *provider)
{
  return "https://www.google.com/accounts/OAuthGetAccessToken";
}

static const gchar *
get_callback_uri (GoaOAuthProvider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth";
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuthProvider  *provider,
                   const gchar       *access_token,
                   const gchar       *access_token_secret,
                   gchar            **out_presentation_identity,
                   GCancellable      *cancellable,
                   GError           **error)
{
  RestProxy *proxy;
  RestProxyCall *call;
  JsonParser *parser;
  JsonObject *json_object;
  JsonObject *json_data_object;
  gchar *ret;
  gchar *email;

  ret = NULL;
  proxy = NULL;
  call = NULL;
  parser = NULL;
  email = NULL;

  /* TODO: cancellable */

  proxy = oauth_proxy_new_with_token (goa_oauth_provider_get_consumer_key (provider),
                                      goa_oauth_provider_get_consumer_secret (provider),
                                      access_token,
                                      access_token_secret,
                                      "https://www.googleapis.com/userinfo/email",
                                      FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_param (call, "alt", "json");

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
  json_data_object = json_object_get_object_member (json_object, "data");
  if (json_data_object == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find data member in JSON data"));
      goto out;
    }

  email = g_strdup (json_object_get_string_member (json_data_object, "email"));
  if (email == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find email member in JSON data"));
      goto out;
    }


  ret = email;
  email = NULL;
  if (out_presentation_identity != NULL)
    *out_presentation_identity = g_strdup (ret); /* for now: use email as presentation identity */

 out:
  g_free (email);
  if (call != NULL)
    g_object_unref (call);
  if (proxy != NULL)
    g_object_unref (proxy);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GError             **error)
{
  GoaAccount *account;
  GoaMail *mail;
  GoaCalendar *calendar;
  GoaContacts *contacts;
  gboolean ret;
  gboolean mail_enabled;
  gboolean calendar_enabled;
  gboolean contacts_enabled;

  account = NULL;
  mail = NULL;
  calendar = NULL;
  contacts = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Email */
  mail = goa_object_get_mail (GOA_OBJECT (object));
  mail_enabled = g_key_file_get_boolean (key_file, group, "MailEnabled", NULL);
  if (mail_enabled)
    {
      if (mail == NULL)
        {
          const gchar *email_address;
          email_address = goa_account_get_identity (account);
          mail = goa_mail_skeleton_new ();
          g_object_set (G_OBJECT (mail),
                        "email-address",   email_address,
                        "imap-supported",  TRUE,
                        "imap-host",       "imap.gmail.com",
                        "imap-user-name",  email_address,
                        "imap-use-tls",    TRUE,
                        "smtp-supported",  TRUE,
                        "smtp-host",       "smtp.gmail.com",
                        "smtp-user-name",  email_address,
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
          calendar = goa_calendar_skeleton_new ();
          goa_object_skeleton_set_calendar (object, calendar);
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
          goa_object_skeleton_set_contacts (object, contacts);
        }
    }
  else
    {
      if (contacts != NULL)
        goa_object_skeleton_set_contacts (object, NULL);
    }


  ret = TRUE;

 out:
  if (contacts != NULL)
    g_object_unref (contacts);
  if (calendar != NULL)
    g_object_unref (calendar);
  if (mail != NULL)
    g_object_unref (mail);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
get_use_external_browser (GoaOAuthProvider *provider)
{
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkTable            *table)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->show_account (provider, client, object, vbox, table);

  goa_util_add_row_editable_label_from_keyfile (table, object, _("Email Address"), "Identity", FALSE);
  goa_util_add_heading (table, _("Use this account for"));
  goa_util_add_row_switch_from_keyfile (table, object, _("Mail"), "MailEnabled");
  goa_util_add_row_switch_from_keyfile (table, object, _("Calendar"), "CalendarEnabled");
  goa_util_add_row_switch_from_keyfile (table, object, _("Contacts"), "ContactsEnabled");
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_key_values (GoaOAuthProvider  *provider,
                        GVariantBuilder   *builder)
{
  g_variant_builder_add (builder, "{ss}", "MailEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "CalendarEnabled", "true");
  g_variant_builder_add (builder, "{ss}", "ContactsEnabled", "true");
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
  GoaOAuthProviderClass *oauth_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;
  provider_class->get_credentials_generation = get_credentials_generation;

  oauth_class = GOA_OAUTH_PROVIDER_CLASS (klass);
  oauth_class->get_identity_sync        = get_identity_sync;
  oauth_class->get_consumer_key         = get_consumer_key;
  oauth_class->get_consumer_secret      = get_consumer_secret;
  oauth_class->get_request_uri          = get_request_uri;
  oauth_class->get_request_uri_params   = get_request_uri_params;
  oauth_class->get_authorization_uri    = get_authorization_uri;
  oauth_class->get_token_uri            = get_token_uri;
  oauth_class->get_callback_uri         = get_callback_uri;
  oauth_class->get_use_external_browser = get_use_external_browser;
  oauth_class->add_account_key_values   = add_account_key_values;
}

/* ---------------------------------------------------------------------------------------------------- */
