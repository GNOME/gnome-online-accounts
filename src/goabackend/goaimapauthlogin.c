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
#include <stdlib.h>

#include "goaprovider.h"
#include "goaimapauth.h"
#include "goaimapauthlogin.h"

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
  GoaImapAuth parent_instance;

  GoaProvider *provider;
  GoaObject *object;
  gchar *user_name;
  gchar *password;
};

typedef struct
{
  GoaImapAuthClass parent_class;

} GoaImapAuthLoginClass;

enum
{
  PROP_0,
  PROP_PROVIDER,
  PROP_OBJECT,
  PROP_USER_NAME,
  PROP_PASSWORD
};

static gboolean goa_imap_auth_login_run_sync (GoaImapAuth         *_auth,
                                              GDataInputStream    *input,
                                              GDataOutputStream   *output,
                                              GCancellable        *cancellable,
                                              GError             **error);

G_DEFINE_TYPE (GoaImapAuthLogin, goa_imap_auth_login, GOA_TYPE_IMAP_AUTH);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_imap_auth_login_finalize (GObject *object)
{
  GoaImapAuthLogin *auth = GOA_IMAP_AUTH_LOGIN (object);

  g_object_unref (auth->provider);
  g_object_unref (auth->object);
  g_free (auth->user_name);
  g_free (auth->password);

  G_OBJECT_CLASS (goa_imap_auth_login_parent_class)->finalize (object);
}

static void
goa_imap_auth_login_get_property (GObject      *object,
                                  guint         prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
  GoaImapAuthLogin *auth = GOA_IMAP_AUTH_LOGIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, auth->provider);
      break;

    case PROP_OBJECT:
      g_value_set_object (value, auth->object);
      break;

    case PROP_USER_NAME:
      g_value_set_string (value, auth->user_name);
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
goa_imap_auth_login_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GoaImapAuthLogin *auth = GOA_IMAP_AUTH_LOGIN (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      auth->provider = g_value_dup_object (value);
      break;

    case PROP_OBJECT:
      auth->object = g_value_dup_object (value);
      break;

    case PROP_USER_NAME:
      auth->user_name = g_value_dup_string (value);
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
goa_imap_auth_login_init (GoaImapAuthLogin *client)
{
}

static void
goa_imap_auth_login_class_init (GoaImapAuthLoginClass *klass)
{
  GObjectClass *gobject_class;
  GoaImapAuthClass *auth_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_imap_auth_login_finalize;
  gobject_class->get_property = goa_imap_auth_login_get_property;
  gobject_class->set_property = goa_imap_auth_login_set_property;

  auth_class = GOA_IMAP_AUTH_CLASS (klass);
  auth_class->run_sync = goa_imap_auth_login_run_sync;

  /**
   * GoaImapAuthLogin:provider:
   *
   * The #GoaProvider object for the account.
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
                                   PROP_USER_NAME,
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
   * The password (TODO: if %NULL, look up via the keyring).
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
 * @provider: A #GoaLoginProvider.
 * @object: An account object.
 * @user_name: The user name to use.
 * @password: The password to use.
 *
 * Creates a new #GoaImapAuth to be used for username/password authentication.
 *
 * Returns: (type GoaImapAuthLogin): A #GoaImapAuthLogin. Free with g_object_unref().
 */
GoaImapAuth *
goa_imap_auth_login_new (GoaProvider       *provider,
                         GoaObject         *object,
                         const gchar       *user_name,
                         const gchar       *password)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object), NULL);
  g_return_val_if_fail (user_name != NULL, NULL);
  return GOA_IMAP_AUTH (g_object_new (GOA_TYPE_IMAP_AUTH_LOGIN,
                                      "provider", provider,
                                      "object", object,
                                      "user-name", user_name,
                                      "password", password,
                                      NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_imap_auth_login_run_sync (GoaImapAuth         *_auth,
                              GDataInputStream    *input,
                              GDataOutputStream   *output,
                              GCancellable        *cancellable,
                              GError             **error)
{
  GoaImapAuthLogin *auth = GOA_IMAP_AUTH_LOGIN (_auth);
  gchar *request;
  gchar *response;
  gboolean ret;

  request = NULL;
  response = NULL;
  ret = FALSE;

  /* TODO: support looking up the password from the keyring */

  request = g_strdup_printf ("A001 LOGIN %s %s\r\n", auth->user_name, auth->password);
  if (!g_data_output_stream_put_string (output, request, cancellable, error))
    goto out;

 again:
  response = g_data_input_stream_read_line (input, NULL, cancellable, error);
  if (response == NULL)
    goto out;
  /* ignore untagged responses */
  if (g_str_has_prefix (response, "*"))
    {
      g_free (response);
      goto again;
    }
  if (!g_str_has_prefix (response, "A001 OK"))
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   "Unexpected response `%s' while doing LOGIN authentication",
                   response);
      goto out;
    }

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  return ret;
}
