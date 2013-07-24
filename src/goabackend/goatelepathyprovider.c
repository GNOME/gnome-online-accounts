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

typedef struct _GoaTelepathyProviderPrivate GoaTelepathyProviderPrivate;

struct _GoaTelepathyProviderPrivate
{
  TpawProtocol *protocol;
  gchar *protocol_name;
  gchar *provider_type;
};

enum {
  PROP_0,
  PROP_PROTOCOL,
  PROP_PROTOCOL_NAME,
  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

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
  GoaTelepathyProviderPrivate *priv;
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
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (provider)->priv;
  return priv->provider_type;
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

  goa_util_add_row_switch_from_keyfile_with_blurb (left, right, object,
                                                   _("Use for"),
                                                   "chat-disabled",
                                                   _("C_hat"));
}

/* ---------------------------------------------------------------------------------------------------- */

GoaTelepathyProvider *
goa_telepathy_provider_new_from_protocol_name (const gchar *protocol_name)
{
  g_return_val_if_fail (protocol_name != NULL, NULL);

  return g_object_new (GOA_TYPE_TELEPATHY_PROVIDER,
                       "protocol-name", protocol_name,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

GoaTelepathyProvider *
goa_telepathy_provider_new_from_protocol (TpawProtocol *protocol)
{
  g_return_val_if_fail (TPAW_IS_PROTOCOL (protocol), NULL);

  return g_object_new (GOA_TYPE_TELEPATHY_PROVIDER,
                       "protocol", protocol,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_telepathy_provider_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

    switch (property_id) {
    case PROP_PROTOCOL:
        g_value_set_object (value, priv->protocol);
        break;
    case PROP_PROTOCOL_NAME:
        g_value_set_string (value, priv->protocol_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_telepathy_provider_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

    switch (property_id) {
    case PROP_PROTOCOL:
        priv->protocol = g_value_dup_object (value);
        break;
    case PROP_PROTOCOL_NAME:
        priv->protocol_name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_telepathy_provider_init (GoaTelepathyProvider *provider)
{
  provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
        GOA_TYPE_TELEPATHY_PROVIDER, GoaTelepathyProviderPrivate);
}

static void
goa_telepathy_provider_constructed (GObject *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

  G_OBJECT_CLASS (goa_telepathy_provider_parent_class)->constructed (object);

  if (priv->protocol != NULL)
    {
      if (priv->protocol_name != NULL)
        g_error ("You cannot set \"protocol-name\" if you set \"protocol\"");
      priv->protocol_name = g_strdup (tpaw_protocol_get_protocol_name (priv->protocol));
    }
  else
    {
      if (priv->protocol_name == NULL)
        g_error ("You must set \"protocol-name\" or \"protocol\" on GoaTelepathyProvider");
    }

  priv->provider_type = g_strdup_printf ("%s/%s",
      GOA_TELEPATHY_PROVIDER_BASE_TYPE, priv->protocol_name);
}

static void
goa_telepathy_provider_finalize (GObject *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

  g_clear_object (&priv->protocol);
  g_free (priv->protocol_name);
  g_free (priv->provider_type);

  (G_OBJECT_CLASS (goa_telepathy_provider_parent_class)->finalize) (object);
}

static void
goa_telepathy_provider_class_init (GoaTelepathyProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);

  object_class->constructed  = goa_telepathy_provider_constructed;
  object_class->finalize     = goa_telepathy_provider_finalize;
  object_class->get_property = goa_telepathy_provider_get_property;
  object_class->set_property = goa_telepathy_provider_set_property;

  provider_class->get_provider_type     = get_provider_type;
  provider_class->get_provider_name     = get_provider_name;
  provider_class->get_provider_group    = get_provider_group;
  provider_class->get_provider_features = get_provider_features;
  provider_class->add_account           = add_account;
  provider_class->refresh_account       = refresh_account;
  provider_class->build_object          = build_object;
  provider_class->show_account          = show_account;

  g_type_class_add_private (object_class, sizeof (GoaTelepathyProviderPrivate));

  /**
   * GoaTelepathyProvider:protocol
   *
   * A #TpawProtocol associated to this provider (or NULL).
   */
  properties[PROP_PROTOCOL] =
    g_param_spec_object ("protocol",
        "Protocol",
        "A #TpawProtocol associated to the provider (or NULL)",
        TPAW_TYPE_PROTOCOL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * GoaTelepathyProvider:protocol-name
   *
   * The name of the protocol associated to the provider.
   */
  properties[PROP_PROTOCOL_NAME] =
    g_param_spec_string ("protocol-name",
        "Protocol name",
        "The name of the protocol associated to the provider",
        NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);
}
