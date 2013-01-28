/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2013 Red Hat, Inc.
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
 *          Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "goasmtpauthplain.h"
#include "goaprovider.h"
#include "goautils.h"

/**
 * SECTION:goasmtpauthplain
 * @title: GoaSmtpAuthPlain
 * @short_description: PLAIN authentication method for SMTP
 *
 * #GoaSmtpAuthPlain implements the <ulink
 * url="http://tools.ietf.org/html/rfc4616">PLAIN</ulink>
 * SASL mechanism (e.g. using usernames / passwords) for SMTP.
 */

/**
 * GoaSmtpAuthPlain:
 *
 * The #GoaSmtpAuthPlain structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _GoaSmtpAuthPlain
{
  GoaMailAuth parent_instance;

  GoaProvider *provider;
  GoaObject *object;
  gchar *domain;
  gchar *username;
  gchar *password;
};

typedef struct
{
  GoaMailAuthClass parent_class;

} GoaSmtpAuthPlainClass;

enum
{
  PROP_0,
  PROP_PROVIDER,
  PROP_OBJECT,
  PROP_DOMAIN,
  PROP_USERNAME,
  PROP_PASSWORD
};

static gboolean goa_smtp_auth_plain_run_sync (GoaMailAuth         *_auth,
                                              GDataInputStream    *input,
                                              GDataOutputStream   *output,
                                              GCancellable        *cancellable,
                                              GError             **error);

G_DEFINE_TYPE (GoaSmtpAuthPlain, goa_smtp_auth_plain, GOA_TYPE_MAIL_AUTH);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_smtp_auth_plain_finalize (GObject *object)
{
  GoaSmtpAuthPlain *auth = GOA_SMTP_AUTH_PLAIN (object);

  g_clear_object (&auth->provider);
  g_clear_object (&auth->object);
  g_free (auth->domain);
  g_free (auth->username);
  g_free (auth->password);

  G_OBJECT_CLASS (goa_smtp_auth_plain_parent_class)->finalize (object);
}

static void
goa_smtp_auth_plain_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  GoaSmtpAuthPlain *auth = GOA_SMTP_AUTH_PLAIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, auth->provider);
      break;

    case PROP_OBJECT:
      g_value_set_object (value, auth->object);
      break;

    case PROP_DOMAIN:
      g_value_set_string (value, auth->domain);
      break;

    case PROP_USERNAME:
      g_value_set_string (value, auth->username);
      break;

    case PROP_PASSWORD:
      g_value_set_string (value, auth->password);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_smtp_auth_plain_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GoaSmtpAuthPlain *auth = GOA_SMTP_AUTH_PLAIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      auth->provider = g_value_dup_object (value);
      break;

    case PROP_OBJECT:
      auth->object = g_value_dup_object (value);
      break;

    case PROP_DOMAIN:
      auth->domain = g_value_dup_string (value);
      break;

    case PROP_USERNAME:
      auth->username = g_value_dup_string (value);
      break;

    case PROP_PASSWORD:
      auth->password = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */


static void
goa_smtp_auth_plain_init (GoaSmtpAuthPlain *client)
{
}

static void
goa_smtp_auth_plain_class_init (GoaSmtpAuthPlainClass *klass)
{
  GObjectClass *gobject_class;
  GoaMailAuthClass *auth_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_smtp_auth_plain_finalize;
  gobject_class->get_property = goa_smtp_auth_plain_get_property;
  gobject_class->set_property = goa_smtp_auth_plain_set_property;

  auth_class = GOA_MAIL_AUTH_CLASS (klass);
  auth_class->run_sync = goa_smtp_auth_plain_run_sync;

  /**
   * GoaSmtpAuthPlain:provider:
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
   * GoaSmtpAuthPlain:object:
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
   * GoaSmtpAuthPlain:domain:
   *
   * The domail or %NULL.
   *
   * If this is %NULL, the domain is obtained from the
   * email address associated with the #GoaObject.
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
   * GoaSmtpAuthPlain:user-name:
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
   * GoaSmtpAuthPlain:password:
   *
   * The password or %NULL.
   *
   * If this is %NULL, the credentials are looked up using
   * goa_utils_lookup_credentials_sync() using the
   * #GoaSmtpAuthPlain:provider and #GoaSmtpAuthPlain:object for
   * @provider and @object. The credentials are expected to be a
   * %G_VARIANT_VARDICT and the key <literal>smtp-password</literal>
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
 * goa_smtp_auth_plain_new:
 * @provider: (allow-none): A #GoaPlainProvider or %NULL.
 * @object: (allow-none): An account object or %NULL.
 * @domain: (allow-none): The domain to use or %NULL to look it up
 * (see the #GoaSmtpAuthPlain:domain property).
 * @username: The user name to use.
 * @password: (allow-none): The password to use or %NULL to look it up
 * (see the #GoaSmtpAuthPlain:password property).
 *
 * Creates a new #GoaMailAuth to be used for username/password
 * authentication using PLAIN over SMTP.
 *
 * Returns: (type GoaSmtpAuthPlain): A #GoaSmtpAuthPlain. Free with
 * g_object_unref().
 */
GoaMailAuth *
goa_smtp_auth_plain_new (GoaProvider       *provider,
                         GoaObject         *object,
                         const gchar       *domain,
                         const gchar       *username,
                         const gchar       *password)
{
  g_return_val_if_fail (provider == NULL || GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (object == NULL || GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (username != NULL, NULL);
  return GOA_MAIL_AUTH (g_object_new (GOA_TYPE_SMTP_AUTH_PLAIN,
                                      "provider", provider,
                                      "object", object,
                                      "domain", domain,
                                      "user-name", username,
                                      "password", password,
                                      NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_smtp_auth_plain_run_sync (GoaMailAuth         *_auth,
                              GDataInputStream    *input,
                              GDataOutputStream   *output,
                              GCancellable        *cancellable,
                              GError             **error)
{
  GoaSmtpAuthPlain *auth = GOA_SMTP_AUTH_PLAIN (_auth);
  gboolean ret;
  gchar *auth_arg_base64;
  gchar *auth_arg_plain;
  gchar *domain;
  gchar *password;
  gchar *request;
  gchar *response;
  gsize auth_arg_plain_len;

  auth_arg_base64 = NULL;
  auth_arg_plain = NULL;
  domain = NULL;
  password = NULL;
  request = NULL;
  response = NULL;

  ret = FALSE;

  if (auth->password != NULL)
    {
      password = g_strdup (auth->password);
    }
  else if (auth->provider != NULL && auth->object != NULL)
    {
      GVariant *credentials;
      credentials = goa_utils_lookup_credentials_sync (auth->provider,
                                                       auth->object,
                                                       cancellable,
                                                       error);
      if (credentials == NULL)
        {
          g_prefix_error (error, "Error looking up credentials for SMTP PLAIN in keyring: ");
          goto out;
        }
      if (!g_variant_lookup (credentials, "smtp-password", "s", &password))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       _("Did not find smtp-password in credentials"));
          g_variant_unref (credentials);
          goto out;
        }
      g_variant_unref (credentials);
    }
  else
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Cannot do SMTP PLAIN without a password"));
      goto out;
    }

  if (auth->domain != NULL)
    {
      domain = g_strdup (auth->domain);
    }
  else if (auth->object != NULL)
    {
      GoaMail *mail;
      gchar *email_address;

      mail = goa_object_get_mail (auth->object);
      if (mail == NULL)
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       _("org.gnome.OnlineAccounts.Mail is not available"));
          goto out;
        }

      email_address = goa_mail_dup_email_address (mail);
      if (!goa_utils_parse_email_address (email_address, NULL, &domain))
        {
          g_set_error (error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED, /* TODO: more specific */
                       _("Failed to parse email address"));
          goto out;
        }

      g_free (email_address);
      g_object_unref (mail);
    }
  else
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Cannot do SMTP PLAIN without a domain"));
      goto out;
    }

  /* Check the greeting */

  response = g_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  if (g_str_has_prefix (response, "421"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Service not available"));
      goto out;
    }
  if (!g_str_has_prefix (response, "220"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   "Unexpected response `%s' while doing PLAIN authentication",
                   response);
      goto out;
    }
  g_clear_pointer (&response, g_free);

  /* Send EHLO */

  request = g_strdup_printf ("EHLO %s\r\n", domain);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

  /* Check if PLAIN is supported or not */

 ehlo_again:
  response = g_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  if (!g_str_has_prefix (response, "250-AUTH"))
    {
      g_free (response);
      goto ehlo_again;
    }
  if (strstr (response, "PLAIN") == NULL)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_NOT_SUPPORTED,
                   _("Server does not support PLAIN"));
      goto out;
    }
  g_clear_pointer (&response, g_free);

  /* Send AUTH PLAIN */

  auth_arg_plain = g_strdup_printf ("%s%c%s%c%s", auth->username, '\0', auth->username, '\0', password);
  auth_arg_plain_len = 2 * strlen (auth->username) + 2 + strlen (password);
  auth_arg_base64 = g_base64_encode ((guchar *) auth_arg_plain, auth_arg_plain_len);

  request = g_strdup_printf ("AUTH PLAIN %s\r\n", auth_arg_base64);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;
  g_clear_pointer (&request, g_free);

 auth_again:
  response = g_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  if (g_str_has_prefix (response, "250"))
    {
      g_free (response);
      goto auth_again;
    }
  if (!g_str_has_prefix (response, "235"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED, /* TODO: more specific */
                   _("Authentication failed"));
      goto out;
    }
  g_clear_pointer (&response, g_free);

  ret = TRUE;

 out:
  g_free (auth_arg_base64);
  g_free (auth_arg_plain);
  g_free (domain);
  g_free (password);
  g_free (response);
  g_free (request);
  return ret;
}
