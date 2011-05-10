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

#include "goabackendimapauth.h"
#include "goabackendimapauthoauth.h"
#include "goabackendoauthprovider.h"

/**
 * SECTION:goabackendimapauthoauth
 * @title: GoaBackendImapAuthOAuth
 * @short_description: XOAUTH authentication method for IMAP
 *
 * #GoaBackendImapAuthOAuth implements the <ulink
 * url="http://code.google.com/apis/gmail/oauth/protocol.html">XOAUTH</ulink>
 * authentication method for IMAP.
 */

/**
 * GoaBackendImapAuthOAuth:
 *
 * The #GoaBackendImapAuthOAuth structure contains only private data
 * and should only be accessed using the provided API.
 */
struct _GoaBackendImapAuthOAuth
{
  GoaBackendImapAuth parent_instance;

  GoaBackendOAuthProvider *provider;
  GoaObject *object;
  gchar *request_uri;
};

typedef struct
{
  GoaBackendImapAuthClass parent_class;

} GoaBackendImapAuthOAuthClass;

enum
{
  PROP_0,
  PROP_PROVIDER,
  PROP_OBJECT,
  PROP_REQUEST_URI
};

static gboolean goa_backend_imap_auth_oauth_run_sync (GoaBackendImapAuth  *_auth,
                                                      GDataInputStream    *input,
                                                      GDataOutputStream   *output,
                                                      GCancellable        *cancellable,
                                                      GError             **error);

G_DEFINE_TYPE (GoaBackendImapAuthOAuth, goa_backend_imap_auth_oauth, GOA_TYPE_BACKEND_IMAP_AUTH);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_auth_oauth_finalize (GObject *object)
{
  GoaBackendImapAuthOAuth *auth = GOA_BACKEND_IMAP_AUTH_OAUTH (object);

  g_object_unref (auth->provider);
  g_object_unref (auth->object);

  G_OBJECT_CLASS (goa_backend_imap_auth_oauth_parent_class)->finalize (object);
}

static void
goa_backend_imap_auth_oauth_get_property (GObject      *object,
                                          guint         prop_id,
                                          GValue       *value,
                                          GParamSpec   *pspec)
{
  GoaBackendImapAuthOAuth *auth = GOA_BACKEND_IMAP_AUTH_OAUTH (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, auth->provider);
      break;

    case PROP_OBJECT:
      g_value_set_object (value, auth->object);
      break;

    case PROP_REQUEST_URI:
      g_value_set_string (value, auth->request_uri);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_auth_oauth_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  GoaBackendImapAuthOAuth *auth = GOA_BACKEND_IMAP_AUTH_OAUTH (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      auth->provider = g_value_dup_object (value);
      break;

    case PROP_OBJECT:
      auth->object = g_value_dup_object (value);
      break;

    case PROP_REQUEST_URI:
      auth->request_uri = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */


static void
goa_backend_imap_auth_oauth_init (GoaBackendImapAuthOAuth *client)
{
}

static void
goa_backend_imap_auth_oauth_class_init (GoaBackendImapAuthOAuthClass *klass)
{
  GObjectClass *gobject_class;
  GoaBackendImapAuthClass *auth_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_backend_imap_auth_oauth_finalize;
  gobject_class->get_property = goa_backend_imap_auth_oauth_get_property;
  gobject_class->set_property = goa_backend_imap_auth_oauth_set_property;

  auth_class = GOA_BACKEND_IMAP_AUTH_CLASS (klass);
  auth_class->run_sync = goa_backend_imap_auth_oauth_run_sync;

  /**
   * GoaBackendImapAuthOAuth:provider:
   *
   * The #GoaBackendOAuthProvider object to use when calculating the XOAUTH mechanism parameter.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_PROVIDER,
                                   g_param_spec_object ("provider",
                                                        "provider",
                                                        "provider",
                                                        GOA_TYPE_BACKEND_OAUTH_PROVIDER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaBackendImapAuthOAuth:object:
   *
   * The #GoaObject object to use when calculating the XOAUTH mechanism parameter.
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
   * GoaBackendImapAuthOAuth:request-uri:
   *
   * The request URI to use when calculating the XOAUTH mechanism parameter.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_REQUEST_URI,
                                   g_param_spec_string ("request-uri",
                                                        "request-uri",
                                                        "request-uri",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_auth_oauth_new:
 * @provider: A #GoaBackendOAuthProvider.
 * @object: An account object.
 * @request_uri: The request URI to use.
 *
 * Creates a new #GoaBackendImapAuth to be used for XOAUTH authentication.
 *
 * Returns: (type GoaBackendImapAuthOAuth): A #GoaBackendImapAuthOAuth. Free with g_object_unref().
 */
GoaBackendImapAuth *
goa_backend_imap_auth_oauth_new (GoaBackendOAuthProvider  *provider,
                                 GoaObject                *object,
                                 const gchar              *request_uri)
{
  g_return_val_if_fail (GOA_IS_BACKEND_OAUTH_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_OBJECT (object), NULL);
  return GOA_BACKEND_IMAP_AUTH (g_object_new (GOA_TYPE_BACKEND_IMAP_AUTH_OAUTH,
                                              "provider", provider,
                                              "object", object,
                                              "request-uri", request_uri,
                                              NULL));
}

/* ---------------------------------------------------------------------------------------------------- */


#include <libsoup/soup.h>

#define OAUTH_ENCODE_STRING(x_) (x_ ? soup_uri_encode( (x_), "!$&'()*+,;=@") : g_strdup (""))

#define SHA1_BLOCK_SIZE 64
#define SHA1_LENGTH 20

/*
 * hmac_sha1:
 * @key: The key
 * @message: The message
 *
 * Given the key and message, compute the HMAC-SHA1 hash and return the base-64
 * encoding of it.  This is very geared towards OAuth, and as such both key and
 * message must be NULL-terminated strings, and the result is base-64 encoded.
 */
static char *
hmac_sha1 (const char *key, const char *message)
{
  GChecksum *checksum;
  char *real_key;
  guchar ipad[SHA1_BLOCK_SIZE];
  guchar opad[SHA1_BLOCK_SIZE];
  guchar inner[SHA1_LENGTH];
  guchar digest[SHA1_LENGTH];
  gsize key_length, inner_length, digest_length;
  int i;

  g_return_val_if_fail (key, NULL);
  g_return_val_if_fail (message, NULL);

  checksum = g_checksum_new (G_CHECKSUM_SHA1);

  /* If the key is longer than the block size, hash it first */
  if (strlen (key) > SHA1_BLOCK_SIZE) {
    guchar new_key[SHA1_LENGTH];

    key_length = sizeof (new_key);

    g_checksum_update (checksum, (guchar*)key, strlen (key));
    g_checksum_get_digest (checksum, new_key, &key_length);
    g_checksum_reset (checksum);

    real_key = g_memdup (new_key, key_length);
  } else {
    real_key = g_strdup (key);
    key_length = strlen (key);
  }

  /* Sanity check the length */
  g_assert (key_length <= SHA1_BLOCK_SIZE);

  /* Protect against use of the provided key by NULLing it */
  key = NULL;

  /* Stage 1 */
  memset (ipad, 0, sizeof (ipad));
  memset (opad, 0, sizeof (opad));

  memcpy (ipad, real_key, key_length);
  memcpy (opad, real_key, key_length);

  /* Stage 2 and 5 */
  for (i = 0; i < sizeof (ipad); i++) {
    ipad[i] ^= 0x36;
    opad[i] ^= 0x5C;
  }

  /* Stage 3 and 4 */
  g_checksum_update (checksum, ipad, sizeof (ipad));
  g_checksum_update (checksum, (guchar*)message, strlen (message));
  inner_length = sizeof (inner);
  g_checksum_get_digest (checksum, inner, &inner_length);
  g_checksum_reset (checksum);

  /* Stage 6 and 7 */
  g_checksum_update (checksum, opad, sizeof (opad));
  g_checksum_update (checksum, inner, inner_length);

  digest_length = sizeof (digest);
  g_checksum_get_digest (checksum, digest, &digest_length);

  g_checksum_free (checksum);
  g_free (real_key);

  return g_base64_encode (digest, digest_length);
}

static char *
sign_plaintext (const gchar *consumer_secret,
                const gchar *token_secret)
{
  char *cs;
  char *ts;
  char *rv;

  cs = OAUTH_ENCODE_STRING (consumer_secret);
  ts = OAUTH_ENCODE_STRING (token_secret);
  rv = g_strconcat (cs, "&", ts, NULL);

  g_free (cs);
  g_free (ts);

  return rv;
}

static char *
sign_hmac (const gchar *consumer_secret,
           const gchar *token_secret,
           const gchar *http_method,
           const gchar *request_uri,
           const gchar *encoded_params)
{
  GString *text;

  text = g_string_new (NULL);
  g_string_append (text, http_method);
  g_string_append_c (text, '&');
  g_string_append_uri_escaped (text, request_uri, NULL, FALSE);
  g_string_append_c (text, '&');
  g_string_append_uri_escaped (text, encoded_params, NULL, FALSE);

  /* PLAINTEXT signature value is the HMAC-SHA1 key value */
  gchar *key;
  key = sign_plaintext (consumer_secret, token_secret);

  gchar *signature;
  signature = hmac_sha1 (key, text->str);

  g_free (key);
  g_string_free (text, TRUE);

  return signature;
}

static GHashTable *
calculate_xoauth_params (const gchar  *request_uri,
                         const gchar  *consumer_key,
                         const gchar  *consumer_secret,
                         const gchar  *access_token,
                         const gchar  *access_token_secret)
{
  GHashTable *params;
  gchar *nonce;
  gchar *timestamp;
  GList *keys;
  GList *l;
  GString *normalized;

  nonce = g_strdup_printf ("%u", g_random_int ());
  timestamp = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) time (NULL));

  params = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);
  g_hash_table_insert (params, "oauth_consumer_key", g_strdup (consumer_key));
  g_hash_table_insert (params, "oauth_nonce", nonce); /* takes ownership */
  g_hash_table_insert (params, "oauth_timestamp", timestamp);  /* takes ownership */
  g_hash_table_insert (params, "oauth_version", g_strdup ("1.0"));
  g_hash_table_insert (params, "oauth_signature_method", g_strdup ("HMAC-SHA1"));
  g_hash_table_insert (params, "oauth_token", g_strdup (access_token));

  normalized = g_string_new (NULL);
  keys = g_hash_table_get_keys (params);
  keys = g_list_sort (keys, (GCompareFunc) g_strcmp0); /* TODO: locale specific? */
  for (l = keys; l != NULL; l = l->next)
    {
      const gchar *key = l->data;
      const gchar *value;
      gchar *k;
      gchar *v;

      value = g_hash_table_lookup (params, key);
      if (normalized->len > 0)
        g_string_append_c (normalized, '&');

      k = OAUTH_ENCODE_STRING (key);
      v = OAUTH_ENCODE_STRING (value);

      g_string_append_printf (normalized, "%s=%s", k, v);

      g_free (k);
      g_free (v);

      g_print ("key %s=`%s'\n", key, value);
    }
  g_list_free (keys);

  g_print ("normalized: `%s'\n", normalized->str);

  gchar *signature;
  signature = sign_hmac (consumer_secret,
                         access_token_secret,
                         "GET",
                         request_uri,
                         normalized->str);
  g_hash_table_insert (params, "oauth_signature", signature); /* takes ownership */

  g_string_free (normalized, TRUE);
  return params;
}

static gchar *
calculate_xoauth_param (const gchar  *request_uri,
                        const gchar  *consumer_key,
                        const gchar  *consumer_secret,
                        const gchar  *access_token,
                        const gchar  *access_token_secret,
                        GError      **error)
{
  gchar *ret;
  GString *str;
  GHashTable *params;
  GList *keys;
  GList *l;

  params = calculate_xoauth_params (request_uri,
                                    consumer_key,
                                    consumer_secret,
                                    access_token,
                                    access_token_secret);
  str = g_string_new ("GET ");
  g_string_append (str, request_uri);
  g_string_append_c (str, ' ');
  keys = g_hash_table_get_keys (params);
  keys = g_list_sort (keys, (GCompareFunc) g_strcmp0); /* TODO: locale specific? */
  for (l = keys; l != NULL; l = l->next)
    {
      const gchar *key = l->data;
      const gchar *value;
      gchar *k;
      gchar *v;

      value = g_hash_table_lookup (params, key);
      if (l != keys)
        g_string_append_c (str, ',');

      k = OAUTH_ENCODE_STRING (key);
      v = OAUTH_ENCODE_STRING (value);
      g_string_append_printf (str, "%s=\"%s\"", k, v);
      g_free (k);
      g_free (v);
    }
  g_list_free (keys);

  ret = g_base64_encode ((const guchar *) str->str, str->len);
  g_string_free (str, TRUE);
  g_hash_table_unref (params);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_backend_imap_auth_oauth_run_sync (GoaBackendImapAuth  *_auth,
                                      GDataInputStream    *input,
                                      GDataOutputStream   *output,
                                      GCancellable        *cancellable,
                                      GError             **error)
{
  GoaBackendImapAuthOAuth *auth = GOA_BACKEND_IMAP_AUTH_OAUTH (_auth);
  gchar *access_token;
  gchar *access_token_secret;
  gchar *xoauth_param;
  gchar *request;
  gchar *response;
  gboolean ret;

  access_token = NULL;
  access_token_secret = NULL;
  xoauth_param = NULL;
  request = NULL;
  response = NULL;
  ret = FALSE;

  access_token = goa_backend_oauth_provider_get_access_token_sync (auth->provider,
                                                                   auth->object,
                                                                   FALSE,  /* force_refresh */
                                                                   &access_token_secret,
                                                                   NULL,   /* out_access_token_expires_in */
                                                                   NULL,   /* GCancellable */
                                                                   error); /* GError */
  if (access_token == NULL)
    goto out;

  xoauth_param = calculate_xoauth_param (auth->request_uri,
                                         goa_backend_oauth_provider_get_consumer_key (auth->provider),
                                         goa_backend_oauth_provider_get_consumer_secret (auth->provider),
                                         access_token,
                                         access_token_secret,
                                         error);
  if (xoauth_param == NULL)
    goto out;

  request = g_strdup_printf ("A001 AUTHENTICATE XOAUTH %s\r\n", xoauth_param);
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
                   "Unexpected response `%s' while doing XOAUTH authentication",
                   response);
      goto out;
    }

  ret = TRUE;

 out:
  g_free (response);
  g_free (request);
  g_free (xoauth_param);
  g_free (access_token);
  g_free (access_token_secret);
  return ret;
}
