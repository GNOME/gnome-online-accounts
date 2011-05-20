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

#include "goaimapmail.h"
#include "goaimapauthoauth.h"

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

static const gchar *
get_name (GoaProvider *_provider)
{
  return _("Google Account");
}

static const gchar *
get_consumer_key (GoaOAuthProvider *provider)
{
  return "anonymous";
}

static const gchar *
get_consumer_secret (GoaOAuthProvider *provider)
{
  return "anonymous";
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
    "https://www.google.com/calendar/feeds"));
  g_ptr_array_add (p, NULL);
  return (gchar **) g_ptr_array_free (p, FALSE);
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
                   gchar            **out_name,
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
  if (out_name != NULL)
    *out_name = g_strdup (ret); /* for now: use email as name */

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
  GoaGoogleAccount *google_account;
  GoaMail *mail;
  gboolean ret;
  gchar *email_address;

  email_address = NULL;
  account = NULL;
  google_account = NULL;
  mail = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  google_account = goa_object_get_google_account (GOA_OBJECT (object));
  if (google_account == NULL)
    {
      google_account = goa_google_account_skeleton_new ();
      goa_object_skeleton_set_google_account (object, google_account);
    }

  email_address = g_key_file_get_string (key_file, group, "Identity", NULL);
  if (email_address == NULL /* || !is_valid_email_address () */)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Invalid identity %s for id %s",
                   email_address,
                   goa_account_get_id (account));
      goto out;
    }

  goa_google_account_set_email_address (google_account, email_address);

  mail = goa_object_get_mail (GOA_OBJECT (object));
  if (mail == NULL)
    {
      GoaImapAuth *auth;
      gchar *request_uri;
      request_uri = g_strdup_printf ("https://mail.google.com/mail/b/%s/imap/", email_address);
      auth = goa_imap_auth_oauth_new (GOA_OAUTH_PROVIDER (provider),
                                      GOA_OBJECT (object),
                                      request_uri);
      mail = goa_imap_mail_new ("imap.gmail.com",
                                TRUE,  /* use_tls */
                                FALSE, /* ignore_bad_tls */
                                auth);
      goa_object_skeleton_set_mail (object, mail);
      g_object_unref (auth);
      g_free (request_uri);

      goa_mail_set_email_address (mail, email_address);
    }

  ret = TRUE;

 out:
  g_free (email_address);
  if (mail != NULL)
    g_object_unref (mail);
  if (google_account != NULL)
    g_object_unref (google_account);
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
  GoaGoogleAccount *gaccount;

  /* Chain up */
  GOA_PROVIDER_CLASS (goa_google_provider_parent_class)->show_account (provider, client, object, vbox, table);

  gaccount = goa_object_get_google_account (object);
  goa_util_add_row (table,
                    _("Email Address"),
                    goa_google_account_get_email_address (gaccount));
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
  provider_class->get_name                   = get_name;
  provider_class->build_object               = build_object;
  provider_class->show_account               = show_account;

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
}

/* ---------------------------------------------------------------------------------------------------- */
