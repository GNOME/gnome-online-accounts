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

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include "goabackendprovider.h"
#include "goabackendoauth2provider.h"
#include "goabackendgoogleprovider.h"

/**
 * GoaBackendGoogleProvider:
 *
 * The #GoaBackendGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendGoogleProvider
{
  /*< private >*/
  GoaBackendOAuth2Provider parent_instance;
};

typedef struct _GoaBackendGoogleProviderClass GoaBackendGoogleProviderClass;

struct _GoaBackendGoogleProviderClass
{
  GoaBackendOAuth2ProviderClass parent_class;
};

static const gchar *goa_backend_google_provider_get_provider_type (GoaBackendProvider *_provider);
static const gchar *goa_backend_google_provider_get_name          (GoaBackendProvider *_provider);

/**
 * SECTION:goabackendgoogleprovider
 * @title: GoaBackendGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaBackendGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendGoogleProvider, goa_backend_google_provider, GOA_TYPE_BACKEND_OAUTH2_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
goa_backend_google_provider_get_provider_type (GoaBackendProvider *_provider)
{
  return "google";
}

static const gchar *
goa_backend_google_provider_get_name (GoaBackendProvider *_provider)
{
  return _("Google Account");
}

static const gchar *
goa_backend_google_provider_get_dialog_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/auth";
}


static const gchar *
goa_backend_google_provider_get_authorization_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://accounts.google.com/o/oauth2/token";
}


static const gchar *
goa_backend_google_provider_get_redirect_uri (GoaBackendOAuth2Provider *provider)
{
  return "https://www.gnome.org/goa-1.0/oauth2";
}

static const gchar *
goa_backend_google_provider_get_scope (GoaBackendOAuth2Provider *provider)
{
  return
    "https://www.googleapis.com/auth/userinfo#email " /* view email address */
    "https://mail.google.com/mail/feed/atom "         /* email */
    "https://www.google.com/m8/feeds "                /* contacts */
    "https://www.google.com/calendar/feeds "          /* calendar */
    "https://picasaweb.google.com/data "              /* picassa */
    ;
}

static const gchar *
goa_backend_google_provider_get_client_id (GoaBackendOAuth2Provider *provider)
{
  return "108305240709.apps.googleusercontent.com";
}

static const gchar *
goa_backend_google_provider_get_client_secret (GoaBackendOAuth2Provider *provider)
{
  return "_xDuWutH-QcwiVI079hRrIfE";
}

static gchar *
goa_backend_google_provider_get_identity_sync (GoaBackendOAuth2Provider  *provider,
                                               const gchar               *access_token,
                                               GCancellable              *cancellable,
                                               GError                   **error)
{
  SoupSession *session;
  SoupMessage *message;
  gint code;
  gchar *ret;
  SoupBuffer *buffer;
  JsonParser *parser;
  JsonObject *json_object;
  JsonObject *json_data_object;
  gchar *s;

  g_return_val_if_fail (access_token != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  buffer = NULL;
  parser = NULL;
  session = NULL;

  session = soup_session_sync_new ();
  message = soup_message_new ("GET", "https://www.googleapis.com/userinfo/email?alt=json");
  s = g_strdup_printf ("OAuth %s", access_token);
  soup_message_headers_append (message->request_headers, "Authorization", s);
  g_free (s);
  code = soup_session_send_message (session, message);
  if (code != SOUP_STATUS_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting email address, instead got status %d"),
                   code);
      goto out;
    }

  buffer = soup_message_body_flatten (message->response_body);
  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser, buffer->data, buffer->length, error))
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
  ret = g_strdup (json_object_get_string_member (json_data_object, "email"));
  if (ret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find email member in JSON data"));
      goto out;
    }

 out:
  if (parser != NULL)
    g_object_unref (parser);
  if (buffer != NULL)
    soup_buffer_free (buffer);
  g_object_unref (message);
  if (session != NULL)
    g_object_unref (session);
  return ret;
}

static const gchar *
goa_backend_google_provider_get_identity_from_object (GoaBackendOAuth2Provider *provider,
                                                      GoaObject                *object)
{
  GoaGoogleAccount *gaccount;
  gaccount = goa_object_peek_google_account (object);
  if (gaccount == NULL)
    return NULL;
  return goa_google_account_get_email_address (gaccount);
}


static gboolean
goa_backend_google_provider_build_object (GoaBackendProvider  *provider,
                                          GoaObjectSkeleton   *object,
                                          GKeyFile            *key_file,
                                          const gchar         *group,
                                          GError             **error)
{
  GoaAccount *account;
  GoaGoogleAccount *google_account;
  gboolean ret;
  gchar *email_address;

  ret = FALSE;

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

  ret = TRUE;

 out:
  g_free (email_address);
  g_object_unref (google_account);
  g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_google_provider_init (GoaBackendGoogleProvider *client)
{
}

static void
goa_backend_google_provider_class_init (GoaBackendGoogleProviderClass *klass)
{
  GoaBackendProviderClass *provider_class;
  GoaBackendOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = goa_backend_google_provider_get_provider_type;
  provider_class->get_name                   = goa_backend_google_provider_get_name;
  provider_class->build_object               = goa_backend_google_provider_build_object;

  oauth2_class = GOA_BACKEND_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_dialog_uri           = goa_backend_google_provider_get_dialog_uri;
  oauth2_class->get_authorization_uri    = goa_backend_google_provider_get_authorization_uri;
  oauth2_class->get_redirect_uri         = goa_backend_google_provider_get_redirect_uri;
  oauth2_class->get_scope                = goa_backend_google_provider_get_scope;
  oauth2_class->get_client_id            = goa_backend_google_provider_get_client_id;
  oauth2_class->get_client_secret        = goa_backend_google_provider_get_client_secret;
  oauth2_class->get_identity_sync        = goa_backend_google_provider_get_identity_sync;
  oauth2_class->get_identity_from_object = goa_backend_google_provider_get_identity_from_object;
}
