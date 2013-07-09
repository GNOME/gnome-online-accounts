/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012, 2013 Red Hat, Inc.
 * Copyright (C) 2013 Intel Corporation
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
 * Author: Marco Barisione <marco.barisione@collabora.co.uk>
 */

#include "config.h"
#include <glib/gi18n-lib.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goatelepathyprovider.h"


/**
 * GoaTelepathyProvider:
 *
 * The #GoaTelepathyProvider structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaTelepathyProvider
{
  /*< private >*/
  GoaProvider parent_instance;
};

typedef struct _GoaTelepathyProviderClass GoaTelepathyProviderClass;

struct _GoaTelepathyProviderClass
{
  GoaProviderClass parent_class;
};

/**
 * SECTION:goatelepathyprovider
 * @title: GoaTelepathyProvider
 * @short_description: A provider for Telepathy
 *
 * #GoaTelepathyProvider is used for handling Telepathy IM accounts.
 */

G_DEFINE_TYPE (GoaTelepathyProvider, goa_telepathy_provider, GOA_TYPE_PROVIDER);

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_TELEPATHY_PROVIDER_BASE_TYPE;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_strdup (_("Other chat account"));

}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_CHAT;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_CHAT;
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider  *provider,
             GoaClient    *client,
             GtkDialog    *dialog,
             GtkBox       *vbox,
             GError      **error)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider  *provider,
                 GoaClient    *client,
                 GoaObject    *object,
                 GtkWindow    *parent,
                 GError      **error)
{
  return FALSE;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider        *provider,
              GoaObjectSkeleton  *object,
              GKeyFile           *key_file,
              const gchar        *group,
              GDBusConnection    *connection,
              gboolean            just_added,
              GError            **error)
{
  GoaAccount *account;
  GoaChat *chat;
  gboolean chat_enabled;
  gboolean ret;

  account = NULL;
  chat = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_telepathy_provider_parent_class)->build_object (provider,
                                                                               object,
                                                                               key_file,
                                                                               group,
                                                                               connection,
                                                                               just_added,
                                                                               error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Chat */
  chat = goa_object_get_chat (GOA_OBJECT (object));
  chat_enabled = g_key_file_get_boolean (key_file, group, "ChatEnabled", NULL);
  if (chat_enabled)
    {
      if (chat == NULL)
        {
          chat = goa_chat_skeleton_new ();
          goa_object_skeleton_set_chat (object, chat);
        }
    }
  else
    {
      if (chat != NULL)
        goa_object_skeleton_set_chat (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_chat_disabled (account, !chat_enabled);
      g_signal_connect (account,
                        "notify::chat-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "ChatEnabled");
    }

  ret = TRUE;

out:
  g_clear_object (&chat);
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              GtkGrid             *left,
              GtkGrid             *right)
{
  /* Chain up */
  GOA_PROVIDER_CLASS (goa_telepathy_provider_parent_class)->show_account (provider,
                                                                          client,
                                                                          object,
                                                                          vbox,
                                                                          left,
                                                                          right);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_telepathy_provider_init (GoaTelepathyProvider *provider)
{
}

static void
goa_telepathy_provider_finalize (GObject *object)
{
  (G_OBJECT_CLASS (goa_telepathy_provider_parent_class)->finalize) (object);
}

static void
goa_telepathy_provider_class_init (GoaTelepathyProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);

  object_class->finalize = goa_telepathy_provider_finalize;

  provider_class->get_provider_type     = get_provider_type;
  provider_class->get_provider_name     = get_provider_name;
  provider_class->get_provider_group    = get_provider_group;
  provider_class->get_provider_features = get_provider_features;
  provider_class->add_account           = add_account;
  provider_class->refresh_account       = refresh_account;
  provider_class->build_object          = build_object;
  provider_class->show_account          = show_account;
}
