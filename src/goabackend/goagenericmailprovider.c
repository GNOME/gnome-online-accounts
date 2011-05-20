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

#include "goaprovider.h"
#include "goagenericmailprovider.h"
#include "goaimapauthlogin.h"

#include "goaimapmail.h"

/**
 * GoaGenericMailProvider:
 *
 * The #GoaGenericMailProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaGenericMailProvider
{
  /*< private >*/
  GoaProvider parent_instance;
};

typedef struct _GoaGenericMailProviderClass GoaGenericMailProviderClass;

struct _GoaGenericMailProviderClass
{
  GoaProviderClass parent_class;
};

/**
 * SECTION:goagenericmailprovider
 * @title: GoaGenericMailProvider
 * @short_description: A provider for standards-based mail servers
 *
 * #GoaGenericMailProvider is used to access standards-based Internet
 * mail servers.
 */

G_DEFINE_TYPE_WITH_CODE (GoaGenericMailProvider, goa_generic_mail_provider, GOA_TYPE_PROVIDER,
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
							 g_define_type_id,
							 "generic_mail",
							 0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return "generic_mail";
}

static const gchar *
get_name (GoaProvider *_provider)
{
  return _("Mail Server");
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
  gboolean ret;
  gchar *imap_host_and_port;
  gboolean imap_use_tls;
  gboolean imap_ignore_bad_tls;
  gchar *imap_user_name;
  gchar *imap_password;

  account = NULL;
  mail = NULL;
  imap_host_and_port = NULL;
  imap_user_name = NULL;
  imap_password = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_generic_mail_provider_parent_class)->build_object (provider,
                                                                                  object,
                                                                                  key_file,
                                                                                  group,
                                                                                  error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* mail */
  imap_host_and_port = g_key_file_get_string (key_file, group, "ImapHost", NULL);
  imap_use_tls = g_key_file_get_boolean (key_file, group, "ImapUseTls", NULL);
  imap_ignore_bad_tls = g_key_file_get_boolean (key_file, group, "ImapIgnoreBadTls", NULL);
  imap_user_name = g_key_file_get_string (key_file, group, "ImapUserName", NULL);
  imap_password = g_key_file_get_string (key_file, group, "ImapPassword", NULL);

  mail = goa_object_get_mail (GOA_OBJECT (object));
  if (imap_host_and_port != NULL)
    {
      if (mail == NULL)
        {
          GoaImapAuth *auth;
          if (imap_user_name == NULL)
            imap_user_name = g_strdup (g_get_user_name ());
          auth = goa_imap_auth_login_new (provider, GOA_OBJECT (object), imap_user_name, imap_password);
          mail = goa_imap_mail_new (imap_host_and_port, imap_use_tls, imap_ignore_bad_tls, auth);
          goa_object_skeleton_set_mail (object, mail);
          g_object_unref (auth);
        }
    }
  else
    {
      if (mail != NULL)
        goa_object_skeleton_set_mail (object, NULL);
    }

  ret = TRUE;

 out:
  if (mail != NULL)
    g_object_unref (mail);
  g_free (imap_host_and_port);
  g_free (imap_user_name);
  g_free (imap_password);
  if (account != NULL)
    g_object_unref (account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  /* TODO: we could try and log into the mail server etc. but for now we don't */
  if (out_expires_in != NULL)
    *out_expires_in = 0;
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_generic_mail_provider_init (GoaGenericMailProvider *client)
{
}

static void
goa_generic_mail_provider_class_init (GoaGenericMailProviderClass *klass)
{
  GoaProviderClass *provider_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_name                   = get_name;
  provider_class->build_object               = build_object;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;
}

/* ---------------------------------------------------------------------------------------------------- */
