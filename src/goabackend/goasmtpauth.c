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

#include <glib/gi18n-lib.h>

#include "goasmtpauth.h"
#include "goautils.h"

/*
 * SECTION:goasmtpauth
 * @title: GoaSmtpAuth
 * @short_description: Authentication mechanisms for SMTP
 *
 * #GoaSmtpAuth implements the <ulink
 * url="http://tools.ietf.org/html/rfc4616">PLAIN</ulink> and <ulink
 * url="http://msdn.microsoft.com/en-us/library/cc433484(v=EXCHG.80).aspx">LOGIN</ulink>
 * SASL mechanisms (e.g. using usernames / passwords) for SMTP.
 */

/*
 * GoaSmtpAuth:
 *
 * The #GoaSmtpAuth structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaSmtpAuth
{
  GoaMailAuth parent_instance;

  gboolean auth_supported;
  gboolean greeting_absent;
  gboolean login_supported;
  gboolean plain_supported;
  gchar *domain;
  gchar *username;
  gchar *password;
};

enum
{
  PROP_0,
  PROP_DOMAIN,
  PROP_USERNAME,
  PROP_PASSWORD
};

static gboolean goa_smtp_auth_is_needed (GoaMailAuth        *auth);
static gboolean goa_smtp_auth_run_sync (GoaMailAuth         *auth,
                                        GCancellable        *cancellable,
                                        GError             **error);
static gboolean goa_smtp_auth_starttls_sync (GoaMailAuth    *auth,
                                             GCancellable   *cancellable,
                                             GError        **error);

G_DEFINE_TYPE (GoaSmtpAuth, goa_smtp_auth, GOA_TYPE_MAIL_AUTH);

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
smtp_auth_check_not_220 (const gchar *response, GError **error)
{
  if (!g_str_has_prefix (response, "220"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   "Unexpected response `%s'",
                   response);
      return TRUE;
    }

  return FALSE;
}

static gboolean
smtp_auth_check_not_235 (const gchar *response, GError **error)
{
  if (!g_str_has_prefix (response, "235"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Authentication failed"));
      return TRUE;
    }

  return FALSE;
}

static gboolean
smtp_auth_check_not_250 (const gchar *response, GError **error)
{
  if (!g_str_has_prefix (response, "250") || strlen (response) < 4)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   "Unexpected response `%s'",
                   response);
      return TRUE;
    }

  return FALSE;
}

static gboolean
smtp_auth_check_not_334_login_password (const gchar *response, GError **error)
{
  if (!g_str_has_prefix (response, "334 UGFzc3dvcmQ6"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   "Unexpected response `%s'",
                   response);
      return TRUE;
    }

  return FALSE;
}

static gboolean
smtp_auth_check_421 (const gchar *response, GError **error)
{
  if (g_str_has_prefix (response, "421"))
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
smtp_auth_check_454 (const gchar *response, GError **error)
{
  if (g_str_has_prefix (response, "454"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("TLS not available"));
      return TRUE;
    }

  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
smtp_auth_check_greeting (GDataInputStream *input, GCancellable *cancellable, GError **error)
{
  gboolean ret = FALSE;
  gchar *response = NULL;

 greeting_again:
  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (smtp_auth_check_421 (response, error))
    goto out;
  if (smtp_auth_check_not_220 (response, error))
    goto out;

  if (response[3] == '-')
    {
      g_clear_pointer (&response, g_free);
      goto greeting_again;
    }

  ret = TRUE;

 out:
  g_free (response);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static char *
smtp_auth_escape0 (const char *str,
                   size_t     *length)
{
  GString *ret;

  ret = g_string_new ("");
  for (size_t i = 0; str && str[i]; i++)
    {
      if (str[i] == '\\' || str[i] == '"')
        g_string_append_c (ret, '\\');
      g_string_append_c (ret, str[i]);
    }

  if (length)
    *length = ret->len;

  return g_string_free (ret, FALSE);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_smtp_auth_finalize (GObject *object)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (object);

  g_free (self->domain);
  g_free (self->username);
  g_free (self->password);

  G_OBJECT_CLASS (goa_smtp_auth_parent_class)->finalize (object);
}

static void
goa_smtp_auth_get_property (GObject      *object,
                            guint         prop_id,
                            GValue       *value,
                            GParamSpec   *pspec)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (object);

  switch (prop_id)
    {
    case PROP_DOMAIN:
      g_value_set_string (value, self->domain);
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
goa_smtp_auth_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (object);

  switch (prop_id)
    {
    case PROP_DOMAIN:
      self->domain = g_value_dup_string (value);
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
goa_smtp_auth_init (GoaSmtpAuth *self)
{
}

static void
goa_smtp_auth_class_init (GoaSmtpAuthClass *klass)
{
  GObjectClass *gobject_class;
  GoaMailAuthClass *auth_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = goa_smtp_auth_finalize;
  gobject_class->get_property = goa_smtp_auth_get_property;
  gobject_class->set_property = goa_smtp_auth_set_property;

  auth_class = GOA_MAIL_AUTH_CLASS (klass);
  auth_class->is_needed = goa_smtp_auth_is_needed;
  auth_class->run_sync = goa_smtp_auth_run_sync;
  auth_class->starttls_sync = goa_smtp_auth_starttls_sync;

  /**
   * GoaSmtpAuth:domain:
   *
   * The domain.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_DOMAIN,
                                   g_param_spec_string ("domain",
                                                        "domain",
                                                        "domain",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaSmtpAuth:user-name:
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
   * GoaSmtpAuth:password:
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
 * goa_smtp_auth_new:
 * @domain: The domain to use.
 * @username: (nullable): The user name to use.
 * @password: (nullable): The password to use.
 *
 * Creates a new #GoaMailAuth to be used for username/password
 * authentication using LOGIN or PLAIN over SMTP.
 *
 * If either @username or @password are %NULL, authentication will only
 * succeed if the service supports it.
 *
 * Returns: (type GoaSmtpAuth): A #GoaSmtpAuth. Free with
 * g_object_unref().
 */
GoaMailAuth *
goa_smtp_auth_new (const gchar       *domain,
                   const gchar       *username,
                   const gchar       *password)
{
  g_return_val_if_fail (domain != NULL && domain[0] != '\0', NULL);
  g_return_val_if_fail (username != NULL || (username == NULL && password == NULL), NULL);
  g_return_val_if_fail (password == NULL || password[0] != '\0', NULL);

  return GOA_MAIL_AUTH (g_object_new (GOA_TYPE_SMTP_AUTH,
                                      "domain", domain,
                                      "user-name", username,
                                      "password", password,
                                      NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

gboolean
goa_smtp_auth_is_login (GoaSmtpAuth *self)
{
  return self->login_supported;
}

gboolean
goa_smtp_auth_is_plain (GoaSmtpAuth *self)
{
  return self->plain_supported;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_smtp_auth_is_needed (GoaMailAuth *auth)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (auth);
  return self->auth_supported;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_smtp_auth_run_sync (GoaMailAuth         *auth,
                        GCancellable        *cancellable,
                        GError             **error)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (auth);
  GDataInputStream *input;
  GDataOutputStream *output;
  gboolean ret = FALSE;
  gchar *auth_arg_base64 = NULL;
  gchar *auth_arg_plain = NULL;
  gchar *request = NULL;
  gchar *response = NULL;
  gsize auth_arg_plain_len;

  input = goa_mail_auth_get_input (auth);
  output = goa_mail_auth_get_output (auth);

  /* Check the greeting, if there is one */

  if (!self->greeting_absent)
    {
      if (!smtp_auth_check_greeting (input, cancellable, error))
        goto out;
    }

  /* Send EHLO */

  request = g_strdup_printf ("EHLO %s\r\n", self->domain);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Check which SASL mechanisms are supported */

 ehlo_again:
  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (smtp_auth_check_421 (response, error))
    goto out;
  if (smtp_auth_check_not_250 (response, error))
    goto out;

  if (g_str_has_prefix (response + 4, "AUTH"))
    {
      self->auth_supported = TRUE;
      if (strstr (response, "PLAIN") != NULL)
        self->plain_supported = TRUE;
      else if (strstr (response, "LOGIN") != NULL)
        self->login_supported = TRUE;
    }

  if (response[3] == '-')
    {
      g_free (response);
      goto ehlo_again;
    }
  else if (!self->auth_supported)
    {
      ret = TRUE;
      goto out;
    }
  else if (self->username == NULL && self->password == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_AUTHORIZED,
                   _("Authentication failed"));
      goto out;
    }
  else if (!self->login_supported && !self->plain_supported)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Unknown authentication mechanism"));
      goto out;
    }
  g_clear_pointer (&response, g_free);

  /* Try different SASL mechanisms */
  if (self->plain_supported)
    {
      g_autofree char *username_esc = NULL;
      g_autofree char *password_esc = NULL;
      size_t username_len, password_len;

      /* AUTH PLAIN */

      username_esc = smtp_auth_escape0 (self->username, &username_len);
      password_esc = smtp_auth_escape0 (self->password, &password_len);
      auth_arg_plain = g_strdup_printf ("%s%c%s%c%s", username_esc, '\0', username_esc, '\0', password_esc);
      auth_arg_plain_len = 2 * username_len + 2 + password_len;
      auth_arg_base64 = g_base64_encode ((guchar *) auth_arg_plain, auth_arg_plain_len);

      request = g_strdup_printf ("AUTH PLAIN %s\r\n", auth_arg_base64);
      g_debug ("> AUTH PLAIN ********************");
      if (!g_data_output_stream_put_string (output, request, cancellable, error))
        goto out;
      g_clear_pointer (&request, g_free);
    }
  else
    {
      /* AUTH LOGIN */

      auth_arg_plain = smtp_auth_escape0 (self->username, &auth_arg_plain_len);
      auth_arg_base64 = g_base64_encode ((guchar *) auth_arg_plain, auth_arg_plain_len);

      request = g_strdup_printf ("AUTH LOGIN %s\r\n", auth_arg_base64);
      g_debug ("> AUTH LOGIN ********************");
      if (!g_data_output_stream_put_string (output, request, cancellable, error))
        goto out;
      g_clear_pointer (&request, g_free);

      response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
      if (response == NULL)
        goto out;
      g_debug ("< %s", response);
      if (smtp_auth_check_not_334_login_password (response, error))
        goto out;

      g_free (auth_arg_plain);
      g_free (auth_arg_base64);

      auth_arg_plain = smtp_auth_escape0 (self->password, &auth_arg_plain_len);
      auth_arg_base64 = g_base64_encode ((guchar *) auth_arg_plain, auth_arg_plain_len);

      request = g_strdup_printf ("%s\r\n", auth_arg_base64);
      g_debug ("> ********************");
      if (!g_data_output_stream_put_string (output, request, cancellable, error))
        goto out;
      g_clear_pointer (&request, g_free);
    }

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (smtp_auth_check_not_235 (response, error))
    goto out;
  g_clear_pointer (&response, g_free);

  ret = TRUE;

 out:
  g_free (auth_arg_base64);
  g_free (auth_arg_plain);
  g_free (response);
  g_free (request);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_smtp_auth_starttls_sync (GoaMailAuth         *auth,
                             GCancellable        *cancellable,
                             GError             **error)
{
  GoaSmtpAuth *self = GOA_SMTP_AUTH (auth);
  GDataInputStream *input;
  GDataOutputStream *output;
  gboolean ret = FALSE;
  gboolean starttls_supported = FALSE;
  gchar *request = NULL;
  gchar *response = NULL;

  input = goa_mail_auth_get_input (auth);
  output = goa_mail_auth_get_output (auth);

  /* Check the greeting */

  if (!smtp_auth_check_greeting (input, cancellable, error))
    goto out;

  /* Send EHLO */

  request = g_strdup_printf ("EHLO %s\r\n", self->domain);
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Check if STARTTLS is supported or not */

 ehlo_again:
  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (smtp_auth_check_421 (response, error))
    goto out;
  if (smtp_auth_check_not_250 (response, error))
    goto out;

  if (g_str_has_prefix (response + 4, "STARTTLS"))
    starttls_supported = TRUE;

  if (response[3] == '-')
    {
      g_free (response);
      goto ehlo_again;
    }
  else if (!starttls_supported)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Server does not support STARTTLS"));
      goto out;
    }
  g_clear_pointer (&response, g_free);

  /* Send STARTTLS */

  request = g_strdup ("STARTTLS\r\n");
  g_debug ("> %s", request);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  response = goa_utils_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  g_debug ("< %s", response);
  if (smtp_auth_check_454 (response, error))
    goto out;
  if (smtp_auth_check_not_220 (response, error))
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
