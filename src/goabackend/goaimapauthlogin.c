/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2013, 2015 Red Hat, Inc.
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

#include <string.h>

#include <glib/gi18n-lib.h>
#include <stdlib.h>

#include "goaimapauthlogin.h"
#include "goaprovider.h"
#include "goautils.h"

/**
 * SECTION:goaimapauthlogin
 * @title: GoaImapAuthLogin
 * @short_description: LOGIN authentication method for IMAP
 *
 * #GoaImapAuthLogin implements the standard <ulink
 * url="http://tools.ietf.org/html/rfc3501#section-6.2.3">LOGIN</ulink>
 * authentication method (e.g. using usernames / passwords) for IMAP.
 */

/**
 * GoaImapAuthLogin:
 *
 * The #GoaImapAuthLogin structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _GoaImapAuthLogin
{
  GoaMailAuth parent_instance;

  GoaProvider *provider;
  GoaObject *object;
  gboolean greeting_absent;
  gchar *username;
  gchar *password;
};

typedef struct
{
  GoaMailAuthClass parent_class;

} GoaImapAuthLoginClass;

enum
{
  PROP_0,
  PROP_PROVIDER,
  PROP_OBJECT,
  PROP_USERNAME,
  PROP_PASSWORD
};

static gboolean goa_imap_auth_login_is_needed (GoaMailAuth        *auth);
static gboolean goa_imap_auth_login_run_sync (GoaMailAuth         *auth,
                                              GCancellable        *cancellable,
                                              GError             **error);
static gboolean goa_imap_auth_login_starttls_sync (GoaMailAuth    *auth,
                                                   GCancellable   *cancellable,
                                                   GError        **error);

G_DEFINE_TYPE (GoaImapAuthLogin, goa_imap_auth_login, GOA_TYPE_MAIL_AUTH);

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *IMAP_TAG = "A001";

static gboolean
imap_auth_login_check_BYE (const gchar *response, GError **error)
{
  if (g_str_has_prefix (response, "* BYE"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Service not available"));
      return TRUE;
    }

  return FALSE;
}

static gboolean
imap_auth_login_check_NO (const gchar *response, GError **error)
{
  gboolean ret;
  gchar *str;

  ret = FALSE;
  str = g_strdup_printf ("%s NO", IMAP_TAG);

  if (g_str_has_prefix (response, str))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Authentication failed"));
      ret = TRUE;
    }

  g_free (str);
  return ret;
}

static gboolean
imap_auth_login_check_not_CAPABILITY (const gchar *response)
{
  if (!g_str_has_prefix (response, "* CAPABILITY"))
    return TRUE;

  return FALSE;
}

static gboolean
imap_auth_login_check_not_LOGIN (const gchar *response, GError **error)
{
  if (strstr (response, "AUTH=PLAIN") == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Server does not support PLAIN"));
      return TRUE;
    }

  return FALSE;
}

static gboolean
imap_auth_login_check_not_OK (const gchar *response, gboolean tagged, GError **error)
{
  gboolean ret;

  ret = FALSE;

  if (tagged)
    {
      gchar *str;

      str = g_strdup_printf ("%s OK", IMAP_TAG);
      if (!g_str_has_prefix (response, str))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       "Unexpected response `%s' while doing LOGIN authentication",
                       response);
          ret = TRUE;
        }
      g_free (str);
    }
  else
    {
      if (!g_str_has_prefix (response, "* OK"))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       "Unexpected response `%s' while doing LOGIN authentication",
                       response);
          ret = TRUE;
        }
    }

  return ret;
}

static gboolean
imap_auth_login_check_not_STARTTLS (const gchar *response, GError **error)
{
  if (strstr (response, "STARTTLS") == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Server does not support STARTTLS"));
      return TRUE;
    }

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
imap_auth_escape_backslash (const gchar *str)
{
  GString *ret;
  gsize i;
  gsize len;

  ret = g_string_new ("");
  len = strlen (str);

  for (i = 0; i < len; i++)
    {
      if (str[i] == '\\')
        g_string_append_c (ret, '\\');
      g_string_append_c (ret, str[i]);
    }

  return g_string_free (ret, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_imap_auth_login_finalize (GObject *object)
{
  GoaImapAuthLogin *self = GOA_IMAP_AUTH_LOGIN (object);

  g_clear_object (&self->provider);
  g_clear_object (&self->object);
  g_free (self->username);
  g_free (self->password);

  G_OBJECT_CLASS (goa_imap_auth_login_parent_class)->finalize (object);
}

static void
goa_imap_auth_login_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  GoaImapAuthLogin *self = GOA_IMAP_AUTH_LOGIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, self->provider);
      break;

    case PROP_OBJECT:
      g_value_set_object (value, self->object);
      break;

    case PROP_USERNAME:
      g_value_set_string (value, self->username);
      break;

    case PROP_PASSWORD:
      g_value_set_string (value, self->password);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_imap_auth_login_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GoaImapAuthLogin *self = GOA_IMAP_AUTH_LOGIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      self->provider = g_value_dup_object (value);
      break;

    case PROP_OBJECT:
      self->object = g_value_dup_object (value);
      break;

    case PROP_USERNAME:
      self->username = g_value_dup_string (value);
      break;

    case PROP_PASSWORD:
      self->password = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */


static void
goa_imap_auth_login_init (GoaImapAuthLogin *self)
{
}

static void
goa_imap_auth_login_class_init (GoaImapAuthLoginClass *klass)
{
  GObjectClass *gobject_class;
  GoaMailAuthClass *auth_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_imap_auth_login_finalize;
  gobject_class->get_property = goa_imap_auth_login_get_property;
  gobject_class->set_property = goa_imap_auth_login_set_property;

  auth_class = GOA_MAIL_AUTH_CLASS (klass);
  auth_class->is_needed = goa_imap_auth_login_is_needed;
  auth_class->run_sync = goa_imap_auth_login_run_sync;
  auth_class->starttls_sync = goa_imap_auth_login_starttls_sync;

  /**
   * GoaImapAuthLogin:provider:
   *
   * The #GoaProvider object for the account or %NULL.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PROVIDER,
                                   g_param_spec_object ("provider",
                                                        "provider",
                                                        "provider",
                                                        GOA_TYPE_PROVIDER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaImapAuthLogin:object:
   *
   * The #GoaObject object for the account.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT,
                                   g_param_spec_object ("object",
                                                        "object",
                                                        "object",
                                                        GOA_TYPE_OBJECT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaImapAuthLogin:user-name:
   *
   * The user name.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_USERNAME,
                                   g_param_spec_string ("user-name",
                                                        "user-name",
                                                        "user-name",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaImapAuthLogin:password:
   *
   * The password or %NULL.
   *
   * If this is %NULL, the credentials are looked up using
   * goa_utils_lookup_credentials_sync() using the
   * #GoaImapAuthLogin:provider and #GoaImapAuthLogin:object for
   * @provider and @object. The credentials are expected to be a
   * %G_VARIANT_VARDICT and the key <literal>imap-password</literal>
   * is used to look up the password.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PASSWORD,
                                   g_param_spec_string ("password",
                                                        "password",
                                                        "password",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_imap_auth_login_new:
 * @provider: (allow-none): A #GoaLoginProvider or %NULL.
 * @object: (allow-none): An account object or %NULL.
 * @username: The user name to use.
 * @password: (allow-none): The password to use or %NULL to look it up
 * (see the #GoaImapAuthLogin:password property).
 *
 * Creates a new #GoaMailAuth to be used for username/password
 * authentication using LOGIN over IMAP.
 *
 * Returns: (type GoaImapAuthLogin): A #GoaImapAuthLogin. Free with
 * g_object_unref().
 */
GoaMailAuth *
goa_imap_auth_login_new (GoaProvider       *provider,
                         GoaObject         *object,
                         const gchar       *username,
                         const gchar       *password)
{
  g_return_val_if_fail (provider == NULL || GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (object == NULL || GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (username != NULL, NULL);
  return GOA_MAIL_AUTH (g_object_new (GOA_TYPE_IMAP_AUTH_LOGIN,
                                      "provider", provider,
                                      "object", object,
                                      "user-name", username,
                                      "password", password,
                                      NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_imap_auth_login_is_needed (GoaMailAuth *auth)
{
  /* TODO: support authentication-less IMAP servers */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_imap_auth_login_run_sync (GoaMailAuth         *auth,
                              GCancellable        *cancellable,
                              GError             **error)
{
  GoaImapAuthLogin *self = GOA_IMAP_AUTH_LOGIN (auth);
  GDataInputStream *input;
  GDataOutputStream *output;
  gchar *request;
  gchar *response;
  gboolean ret;
  gchar *password;

  request = NULL;
  response = NULL;
  password = NULL;
  ret = FALSE;

  if (self->password != NULL)
    {
      password = imap_auth_escape_backslash (self->password);
    }
  else if (self->provider != NULL && self->object != NULL)
    {
      GVariant *credentials;
      gchar *value;

      credentials = goa_utils_lookup_credentials_sync (self->provider,
                                                       self->object,
                                                       cancellable,
                                                       error);
      if (credentials == NULL)
        {
          g_prefix_error (error, "Error looking up credentials for IMAP LOGIN in keyring: ");
          goto out;
        }
      if (!g_variant_lookup (credentials, "imap-password", "s", &value))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       "Did not find imap-password in credentials");
          g_variant_unref (credentials);
          goto out;
        }

      password = imap_auth_escape_backslash (value);

      g_free (value);
      g_variant_unref (credentials);
    }
  else
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Cannot do IMAP LOGIN without a password");
      goto out;
    }

  input = goa_mail_auth_get_input (auth);
  output = goa_mail_auth_get_output (auth);

  /* Check the greeting, if there is one */

  if (!self->greeting_absent)
    {
      response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
      if (response == NULL)
        goto out;
      g_debug ("< %s", response);
      if (imap_auth_login_check_BYE (response, error))
        goto out;
      if (imap_auth_login_check_not_OK (response, FALSE, error))
        goto out;
      g_clear_pointer (&response, g_free);
    }

  /* Send CAPABILITY */

  request = g_strdup_printf ("%s CAPABILITY\r\n", IMAP_TAG);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Check if LOGIN is supported or not */

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_LOGIN (response, error))
    goto out;
  g_clear_pointer (&response, g_free);

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_OK (response, TRUE, error))
    goto out;
  g_clear_pointer (&response, g_free);

  /* Send LOGIN */

  request = g_strdup_printf ("%s LOGIN \"%s\" \"%s\"\r\n", IMAP_TAG, self->username, password);
  g_debug ("> %s LOGIN \"********************\" \"********************\"", IMAP_TAG);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Skip post-login CAPABILITY, if any */
  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_CAPABILITY (response))
    goto check_login_response;
  g_clear_pointer (&response, g_free);

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
 check_login_response:
  if (imap_auth_login_check_NO (response, error))
    goto out;
  if (imap_auth_login_check_not_OK (response, TRUE, error))
    goto out;
  g_clear_pointer (&response, g_free);

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  g_free (password);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_imap_auth_login_starttls_sync (GoaMailAuth         *auth,
                                   GCancellable        *cancellable,
                                   GError             **error)
{
  GoaImapAuthLogin *self = GOA_IMAP_AUTH_LOGIN (auth);
  GDataInputStream *input;
  GDataOutputStream *output;
  gchar *request;
  gchar *response;
  gboolean ret;

  request = NULL;
  response = NULL;

  ret = FALSE;

  input = goa_mail_auth_get_input (auth);
  output = goa_mail_auth_get_output (auth);

  /* Check the greeting */

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_BYE (response, error))
    goto out;
  if (imap_auth_login_check_not_OK (response, FALSE, error))
    goto out;
  g_clear_pointer (&response, g_free);

  /* Send CAPABILITY */

  request = g_strdup_printf ("%s CAPABILITY\r\n", IMAP_TAG);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Check if STARTTLS is supported or not */

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_STARTTLS (response, error))
    goto out;
  g_clear_pointer (&response, g_free);

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_OK (response, TRUE, error))
    goto out;
  g_clear_pointer (&response, g_free);

  /* Send STARTTLS */

  request = g_strdup_printf ("%s STARTTLS\r\n", IMAP_TAG);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (imap_auth_login_check_not_OK (response, TRUE, error))
    goto out;
  g_clear_pointer (&response, g_free);

  /* There won't be a greeting after this */
  self->greeting_absent = TRUE;

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  return ret;
}
