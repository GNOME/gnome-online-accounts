/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2017 Red Hat, Inc.
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

#include <gio/gio.h>
#include <glib/gi18n-lib.h>
#include <stdlib.h>

#include "goaimapauthlogin.h"
#include "goautils.h"

/*
 * SECTION:goaimapauthlogin
 * @title: GoaImapAuthLogin
 * @short_description: LOGIN authentication method for IMAP
 *
 * #GoaImapAuthLogin implements the standard <ulink
 * url="http://tools.ietf.org/html/rfc3501#section-6.2.3">LOGIN</ulink>
 * authentication method (e.g. using usernames / passwords) for IMAP.
 */

/*
 * GoaImapAuthLogin:
 *
 * The #GoaImapAuthLogin structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _GoaImapAuthLogin
{
  GoaMailAuth parent_instance;

  gboolean greeting_absent;
  gchar *username;
  gchar *password;
};

enum
{
  PROP_0,
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
  gboolean ret = FALSE;
  gchar *str = NULL;

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
imap_auth_login_check_not_OK (const gchar *response, gboolean tagged, GError **error)
{
  gboolean ret = FALSE;

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

static gboolean
imap_auth_login_read_response (GDataInputStream  *input,
                               GCancellable      *cancellable,
                               GError           **error)
{
  gboolean ret = FALSE;

  g_assert (G_IS_DATA_INPUT_STREAM (input));
  g_assert (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  g_assert (error == NULL || *error == NULL);

  while (TRUE)
    {
      g_autofree char *response = NULL;

      response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
      if (response == NULL)
        break;

      g_debug("< %s", response);
      if (g_str_has_prefix (response, IMAP_TAG))
        {
          if (imap_auth_login_check_NO (response, error))
            break;
          if (imap_auth_login_check_not_OK (response, TRUE, error))
            break;

          ret = TRUE;
          break;
        }
      else if (g_str_has_prefix (response, "* "))
        {
          if (imap_auth_login_check_BYE (response, error))
            break;
        }
      else
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       "Unexpected response `%s' while doing LOGIN authentication",
                       response);
          break;
        }
    }

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gchar *
imap_auth_login_escape0 (const gchar *str)
{
  GString *ret;

  ret = g_string_new ("");
  for (size_t i = 0; str && str[i]; i++)
    {
      if (str[i] == '\\' || str[i] == '"')
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
   * The password.
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
 * @username: The user name to use.
 * @password: The password to use.
 *
 * Creates a new #GoaMailAuth to be used for username/password
 * authentication using LOGIN over IMAP.
 *
 * Returns: (type GoaImapAuthLogin): A #GoaImapAuthLogin. Free with
 * g_object_unref().
 */
GoaMailAuth *
goa_imap_auth_login_new (const gchar       *username,
                         const gchar       *password)
{
  g_return_val_if_fail (username != NULL, NULL);
  g_return_val_if_fail (password != NULL && password[0] != '\0', NULL);

  return GOA_MAIL_AUTH (g_object_new (GOA_TYPE_IMAP_AUTH_LOGIN,
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
  gchar *request = NULL;
  gchar *response = NULL;
  gboolean ret = FALSE;
  gchar *username = NULL;
  gchar *password = NULL;

  username = imap_auth_login_escape0 (self->username);
  password = imap_auth_login_escape0 (self->password);

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

  /* Send LOGIN */

  request = g_strdup_printf ("%s LOGIN \"%s\" \"%s\"\r\n", IMAP_TAG, username, password);
  g_debug ("> %s LOGIN \"********************\" \"********************\"", IMAP_TAG);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  if (!imap_auth_login_read_response (input, cancellable, error))
    goto out;

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  g_free (username);
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
  gchar *request = NULL;
  gchar *response = NULL;
  gboolean ret = FALSE;

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

  if (!imap_auth_login_read_response (input, cancellable, error))
    goto out;

  /* Send STARTTLS */

  request = g_strdup_printf ("%s STARTTLS\r\n", IMAP_TAG);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  if (!imap_auth_login_read_response (input, cancellable, error))
    goto out;

  /* There won't be a greeting after this */
  self->greeting_absent = TRUE;

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  return ret;
}
