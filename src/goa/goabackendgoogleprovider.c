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

#include <webkit/webkit.h>
#include <json-glib/json-glib.h>
#include <gnome-keyring.h>

#include "goabackendprovider.h"
#include "goabackendgoogleprovider.h"

/* The upstream client_id / client_secret for GNOME are managed by
 * David Zeuthen <zeuthen@gmail.com>. Distributors may change this to
 * their values (or not?). TODO: maybe make these a configure option
 * in case downstream distributors want to change it...
 */
#define GOOGLE_CLIENT_ID     "108305240709-9tncnurl91sh2i0isqnpc7l397sojst2.apps.googleusercontent.com"
#define GOOGLE_CLIENT_SECRET "6RoXohmMzFzAnoFEXc0BNW7g"

static const GnomeKeyringPasswordSchema keyring_password_schema =
{
  GNOME_KEYRING_ITEM_GENERIC_SECRET,
  {
    { "goa-google-email-address", GNOME_KEYRING_ATTRIBUTE_TYPE_STRING },
    { NULL, 0 }
  }
};

/**
 * GoaBackendGoogleProvider:
 *
 * The #GoaBackendGoogleProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendGoogleProvider
{
  /*< private >*/
  GoaBackendProvider parent_instance;
};

typedef struct _GoaBackendGoogleProviderClass GoaBackendGoogleProviderClass;

struct _GoaBackendGoogleProviderClass
{
  GoaBackendProviderClass parent_class;
};

static const gchar *goa_backend_google_provider_get_provider_type (GoaBackendProvider *_provider);
static const gchar *goa_backend_google_provider_get_name          (GoaBackendProvider *_provider);
static GoaObject   *goa_backend_google_provider_add_account       (GoaBackendProvider *_provider,
                                                                   GoaClient          *client,
                                                                   GtkDialog          *dialog,
                                                                   GtkBox             *vbox,
                                                                   GError            **error);

static gboolean     goa_backend_google_provider_get_access_token_supported (GoaBackendProvider   *provider);
static void         goa_backend_google_provider_get_access_token           (GoaBackendProvider   *provider,
                                                                            GoaObject            *object,
                                                                            GCancellable         *cancellable,
                                                                            GAsyncReadyCallback   callback,
                                                                            gpointer              user_data);
static gchar       *goa_backend_google_provider_get_access_token_finish    (GoaBackendProvider   *provider,
                                                                            gint                 *out_expires_in,
                                                                            GAsyncResult         *res,
                                                                            GError              **error);

/**
 * SECTION:goabackendgoogleprovider
 * @title: GoaBackendGoogleProvider
 * @short_description: A provider for Google
 *
 * #GoaBackendGoogleProvider is used for handling Google accounts.
 */

G_DEFINE_TYPE_WITH_CODE (GoaBackendGoogleProvider, goa_backend_google_provider, GOA_TYPE_BACKEND_PROVIDER,
                         g_io_extension_point_implement (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "google",
							 0));

static void
goa_backend_google_provider_init (GoaBackendGoogleProvider *client)
{
}

static void
goa_backend_google_provider_class_init (GoaBackendGoogleProviderClass *klass)
{
  GoaBackendProviderClass *provider_klass;

  provider_klass = GOA_BACKEND_PROVIDER_CLASS (klass);
  provider_klass->get_provider_type          = goa_backend_google_provider_get_provider_type;
  provider_klass->get_name                   = goa_backend_google_provider_get_name;
  provider_klass->add_account                = goa_backend_google_provider_add_account;
  provider_klass->get_access_token_supported = goa_backend_google_provider_get_access_token_supported;
  provider_klass->get_access_token           = goa_backend_google_provider_get_access_token;
  provider_klass->get_access_token_finish    = goa_backend_google_provider_get_access_token_finish;
}

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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkDialog *dialog;
  gchar *authorization_code;
  GError *error;
  GMainLoop *loop;
  gchar *account_object_path;
} AddData;

static void
on_web_view_notify_title (GObject     *object,
                          GParamSpec  *pspec,
                          gpointer     user_data)
{
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  AddData *data = user_data;
  const gchar *title;

  title = webkit_web_view_get_title (web_view);
  if (title != NULL && g_str_has_prefix (title, "Success code="))
    {
      const gchar *code = title + sizeof "Success code=" - 1;
      data->authorization_code = g_strdup (code);
      gtk_dialog_response (data->dialog, GTK_RESPONSE_OK);
    }
}

static gchar *
get_access_and_refresh_token_sync (SoupSession  *session,
                                   const gchar  *authorization_code,
                                   gchar       **out_refresh_token,
                                   GError      **error)
{
  SoupMessage *message;
  gint code;
  gchar *body;
  gchar *ret;
  SoupBuffer *buffer;
  JsonParser *parser;
  JsonObject *json_object;
  const gchar *refresh_token;

  g_return_val_if_fail (authorization_code != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  buffer = NULL;
  parser = NULL;

  message = soup_message_new ("POST", "https://accounts.google.com/o/oauth2/token");
  soup_message_headers_append (message->request_headers, "Content-Type", "application/x-www-form-urlencoded");
  body = g_strdup_printf ("client_id=%s&"
                          "client_secret=%s&"
                          "code=%s&"
                          "redirect_uri=urn:ietf:wg:oauth:2.0:oob&"
                          "grant_type=authorization_code",
                          GOOGLE_CLIENT_ID,
                          GOOGLE_CLIENT_SECRET,
                          authorization_code);
  soup_message_body_append (message->request_body, SOUP_MEMORY_TAKE, body, strlen (body));
  code = soup_session_send_message (session, message);

  if (code != SOUP_STATUS_OK)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting access token, instead got status %d"),
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
  refresh_token = json_object_get_string_member (json_object, "refresh_token");
  if (refresh_token == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find refresh_token in JSON data"));
      goto out;
    }
  ret = g_strdup (json_object_get_string_member (json_object, "access_token"));
  if (ret == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Didn't find access_token in JSON data"));
      goto out;
    }

  if (out_refresh_token != NULL)
    *out_refresh_token = g_strdup (refresh_token);

 out:
  if (parser != NULL)
    g_object_unref (parser);
  if (buffer != NULL)
    soup_buffer_free (buffer);
  g_object_unref (message);
  return ret;
}

static gchar *
get_email_address_sync (SoupSession  *session,
                        const gchar  *access_token,
                        GError      **error)
{
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
  return ret;
}

static void
add_account_cb (GoaManager   *manager,
                GAsyncResult *res,
                gpointer      user_data)
{
  AddData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

static void
store_password_cb (GnomeKeyringResult result,
                   gpointer           user_data)
{
  AddData *data = user_data;
  if (result != GNOME_KEYRING_RESULT_OK)
    {
      g_set_error (&data->error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Failed to store the result in the keyring: %s"),
                   gnome_keyring_result_to_message (result));
    }
  g_main_loop_quit (data->loop);
}

static GoaObject *
goa_backend_google_provider_add_account (GoaBackendProvider *_provider,
                                         GoaClient          *client,
                                         GtkDialog          *dialog,
                                         GtkBox             *vbox,
                                         GError            **error)
{
  GoaBackendGoogleProvider *provider = GOA_BACKEND_GOOGLE_PROVIDER (_provider);
  GoaObject *ret;
  SoupSession *webkit_soup_session;
  SoupSession *session;
  SoupCookieJar *cookie_jar;
  GtkWidget *scrolled_window;
  GtkWidget *web_view;
  const gchar *scope;
  gchar *escaped_scope;
  gchar *url;
  gint response;
  AddData data;
  gchar *access_token;
  gchar *refresh_token;
  gchar *email_address;
  GList *accounts;
  GList *l;
  gchar *password_description;

  g_return_val_if_fail (GOA_IS_BACKEND_GOOGLE_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (GTK_IS_BOX (vbox), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = NULL;
  session = NULL;
  access_token = NULL;
  refresh_token = NULL;
  email_address = NULL;
  accounts = NULL;
  password_description = NULL;

  /* TODO: check with NM whether we're online, if not - return error */

  memset (&data, '\0', sizeof (AddData));
  data.loop = g_main_loop_new (NULL, FALSE);

  scope =
    "https://www.googleapis.com/auth/userinfo#email " /* view email address */
    "https://mail.google.com/mail/feed/atom "         /* email */
    "https://www.google.com/m8/feeds "                /* contacts */
    "https://www.google.com/calendar/feeds "          /* calendar */
    "https://picasaweb.google.com/data "              /* picassa */
    ;
  // Use Lattitude? "https://www.googleapis.com/auth/latitude"

  escaped_scope = g_uri_escape_string (scope, NULL, TRUE);
  url = g_strdup_printf ("https://accounts.google.com/o/oauth2/auth"
                         "?response_type=code"
                         "&redirect_uri=urn:ietf:wg:oauth:2.0:oob"
                         "&client_id=%s"
                         "&scope=%s",
                         GOOGLE_CLIENT_ID,
                         escaped_scope);

  /* Ensure we use an empty non-persistent cookie to avoid login
   * credentials being reused...
   */
  webkit_soup_session = webkit_get_default_session ();
  soup_session_remove_feature_by_type (webkit_soup_session, SOUP_TYPE_COOKIE_JAR);
  cookie_jar = soup_cookie_jar_new ();
  soup_session_add_feature (webkit_soup_session, SOUP_SESSION_FEATURE (cookie_jar));

  /* TODO: we might need to add some more web browser UI to make this
   * work...
   */
  web_view = webkit_web_view_new ();
  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), url);
  g_signal_connect (web_view,
                    "notify::title",
                    G_CALLBACK (on_web_view_notify_title),
                    &data);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (scrolled_window, 500, 400);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_window), web_view);
  gtk_container_add (GTK_CONTAINER (vbox), scrolled_window);
  gtk_widget_show_all (scrolled_window);

  data.dialog = dialog;

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      if (data.error == NULL)
        {
          g_set_error (&data.error,
                       GOA_ERROR,
                       GOA_ERROR_DIALOG_DISMISSED,
                       _("Dialog was dismissed"));
        }
      goto out;
    }
  g_assert (data.authorization_code != NULL);

  /* OK, we now have the authorization code... now we need to get the
   * email address (to e.g. check if the account already exists on
   * @client).. for that we need to get a (short-lived) access token
   * and a refresh_token
   */
  session = soup_session_sync_new ();
  access_token = get_access_and_refresh_token_sync (session, data.authorization_code, &refresh_token, error);
  if (access_token == NULL)
    goto out;

  /* Use this to get the email address */
  email_address = get_email_address_sync (session, access_token, error);
  if (email_address == NULL)
    goto out;

  g_debug ("Wee email_address=%s", email_address);

  /* OK, got the email address... see if there's already an account
   * with this email address
   */
  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      GoaGoogleAccount *gaccount;

      gaccount = goa_object_peek_google_account (object);
      if (gaccount == NULL)
        continue;

      if (g_strcmp0 (goa_google_account_get_email_address (gaccount), email_address) == 0)
        {
          g_set_error (&data.error,
                       GOA_ERROR,
                       GOA_ERROR_ACCOUNT_EXISTS,
                       _("There is already a Google account with the email address %s"),
                       email_address);
          goto out;
        }
    }

  goa_manager_call_add_account (goa_client_get_manager (client),
                                "google",
                                email_address, /* Name */
                                g_variant_new_parsed ("{'EmailAddress': %s}",
                                                      email_address),
                                NULL, /* GCancellable* */
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  password_description = g_strdup_printf (_("Google OAuth2 refresh code for %s"), email_address);
  gnome_keyring_store_password (&keyring_password_schema,
                                NULL, /* default keyring */
                                password_description,
                                refresh_token,
                                store_password_cb,
                                &data,
                                NULL, /* GDestroyNotify */
                                "goa-google-email-address", email_address,
                                NULL);

  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  g_free (password_description);
  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
  g_free (email_address);
  if (session != NULL)
    g_object_unref (session);
  g_object_unref (cookie_jar);
  g_free (escaped_scope);
  g_free (url);

  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      g_assert (ret == NULL);
    }
  else
    {
      g_assert (ret != NULL);
    }

  g_free (data.authorization_code);
  g_free (data.account_object_path);
  if (data.loop != NULL)
    g_main_loop_unref (data.loop);
  g_free (access_token);
  g_free (refresh_token);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_google_provider_get_access_token_supported (GoaBackendProvider   *provider)
{
  return TRUE;
}

typedef struct
{
  volatile gint ref_count;

  GoaObject *object;
  GSimpleAsyncResult *simple;
  SoupSession *session;
  SoupMessage *message;

  gchar *access_token;
  gint expires_in;
} GetAccessTokenData;

static void
get_access_token_data_unref (GetAccessTokenData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      g_object_unref (data->object);
      g_object_unref (data->simple);
      g_object_unref (data->session);
      g_object_unref (data->message);
      g_free (data->access_token);
      g_slice_free (GetAccessTokenData, data);
    }
}

static GetAccessTokenData *
get_access_token_data_ref (GetAccessTokenData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}

static void
get_access_token_cb (SoupSession *session,
                     SoupMessage *message,
                     gpointer     user_data)
{
  GetAccessTokenData *data = user_data;
  SoupBuffer *buffer;
  JsonParser *parser;
  JsonObject *json_object;
  GError *error;

  buffer = NULL;
  parser = NULL;

  if (message->status_code != SOUP_STATUS_OK)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Expected status 200 when requesting access token, instead got status %d (%s)"),
                                       message->status_code,
                                       message->reason_phrase);
      goto out;
    }

  buffer = soup_message_body_flatten (message->response_body);
  parser = json_parser_new ();
  error = NULL;
  if (!json_parser_load_from_data (parser, buffer->data, buffer->length, &error))
    {
      g_prefix_error (&error, _("Error parsing response as JSON: "));
      g_simple_async_result_take_error (data->simple, error);
      goto out;
    }
  json_object = json_node_get_object (json_parser_get_root (parser));
  data->access_token = g_strdup (json_object_get_string_member (json_object, "access_token"));
  if (data->access_token == NULL)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Didn't find access_token in JSON data"));
      goto out;
    }

  data->expires_in = 0;
  if (json_object_has_member (json_object, "expires_in"))
    data->expires_in = json_object_get_int_member (json_object, "expires_in");

  g_simple_async_result_set_op_res_gpointer (data->simple,
                                             get_access_token_data_ref (data),
                                             (GDestroyNotify) get_access_token_data_unref);

 out:
  if (parser != NULL)
    g_object_unref (parser);
  if (buffer != NULL)
    soup_buffer_free (buffer);
  g_simple_async_result_complete_in_idle (data->simple);
  get_access_token_data_unref (data);
}

static void
find_password_cb (GnomeKeyringResult  result,
                  const gchar        *refresh_token,
                  gpointer            user_data)
{
  GetAccessTokenData *data = user_data;
  gchar *body;

  if (result != GNOME_KEYRING_RESULT_OK)
    {
      g_simple_async_result_set_error (data->simple,
                                       GOA_ERROR,
                                       GOA_ERROR_FAILED,
                                       _("Failed to retrieve the authorization code from the keyring: %s"),
                                       gnome_keyring_result_to_message (result));
      g_simple_async_result_complete_in_idle (data->simple);
      get_access_token_data_unref (data);
      goto out;
    }

  /* Swell. Now get the access code as per http://code.google.com/apis/accounts/docs/OAuth2.html */
  data->message = soup_message_new ("POST", "https://accounts.google.com/o/oauth2/token");
  soup_message_headers_append (data->message->request_headers, "Content-Type", "application/x-www-form-urlencoded");
  body = g_strdup_printf ("client_id=%s&"
                          "client_secret=%s&"
                          "refresh_token=%s&"
                          "grant_type=refresh_token",
                          GOOGLE_CLIENT_ID,
                          GOOGLE_CLIENT_SECRET,
                          refresh_token);
  g_debug ("body=%s", body);
  soup_message_body_append (data->message->request_body, SOUP_MEMORY_TAKE, body, strlen (body));
  soup_session_queue_message (data->session,
                              g_object_ref (data->message),
                              get_access_token_cb,
                              data);

 out:
  ;
}

static void
goa_backend_google_provider_get_access_token (GoaBackendProvider   *provider,
                                              GoaObject            *object,
                                              GCancellable         *cancellable,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data)
{
  GetAccessTokenData *data;
  const gchar *email_address;

  data = g_slice_new0 (GetAccessTokenData);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->session = soup_session_async_new ();
  data->simple = g_simple_async_result_new (G_OBJECT (provider),
                                            callback,
                                            user_data,
                                            goa_backend_google_provider_get_access_token);

  email_address = goa_google_account_get_email_address (goa_object_peek_google_account (object));

  /* First, get the authorization code from gnome-keyring */
  gnome_keyring_find_password (&keyring_password_schema,
                               find_password_cb,
                               data,
                               NULL, /* GDestroyNotify */
                               "goa-google-email-address", email_address,
                               NULL);
}

static gchar *
goa_backend_google_provider_get_access_token_finish (GoaBackendProvider   *provider,
                                                     gint                 *out_expires_in,
                                                     GAsyncResult         *res,
                                                     GError              **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  GetAccessTokenData *data;
  gchar *ret;

  g_warn_if_fail (g_simple_async_result_is_valid (res, G_OBJECT (provider), goa_backend_google_provider_get_access_token));

  if (g_simple_async_result_propagate_error (simple, error))
    return NULL;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  ret = g_strdup (data->access_token);
  if (out_expires_in != NULL)
    *out_expires_in = data->expires_in;

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */
