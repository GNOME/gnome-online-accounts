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

#include <rest/oauth-proxy.h>
#include <json-glib/json-glib.h>

#include "goabackendimapauth.h"
#include "goabackendimapclient.h"
#include "goabackendimapmessage.h"
#include "goabackendimapmail.h"

/**
 * GoaBackendImapMail:
 *
 * The #GoaBackendImapMail structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaBackendImapMail
{
  /*< private >*/
  GoaMailSkeleton parent_instance;

  gchar *host_and_port;
  gboolean use_tls;
  GoaBackendImapAuth *auth;
};

typedef struct _GoaBackendImapMailClass GoaBackendImapMailClass;

struct _GoaBackendImapMailClass
{
  GoaMailSkeletonClass parent_class;
};

enum
{
  PROP_0,
  PROP_HOST_AND_PORT,
  PROP_USE_TLS,
  PROP_AUTH
};

/**
 * SECTION:goabackendimapmail
 * @title: GoaBackendImapMail
 * @short_description: Implementation of the #GoaMail interface for IMAP servers
 *
 * #GoaBackendImapMail is an implementation of the #GoaMail D-Bus
 * interface that uses a #GoaBackendImapClient instance to speak to a
 * remote IMAP server.
 */

static void goa_backend_imap_mail__goa_mail_iface_init (GoaMailIface *iface);

G_DEFINE_TYPE_WITH_CODE (GoaBackendImapMail, goa_backend_imap_mail, GOA_TYPE_MAIL_SKELETON,
                         G_IMPLEMENT_INTERFACE (GOA_TYPE_MAIL, goa_backend_imap_mail__goa_mail_iface_init));

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_mail_finalize (GObject *object)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  g_free (mail->host_and_port);
  g_object_unref (mail->auth);

  G_OBJECT_CLASS (goa_backend_imap_mail_parent_class)->finalize (object);
}

static void
goa_backend_imap_mail_get_property (GObject      *object,
                                    guint         prop_id,
                                    GValue       *value,
                                    GParamSpec   *pspec)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      g_value_set_string (value, mail->host_and_port);
      break;

    case PROP_USE_TLS:
      g_value_set_boolean (value, mail->use_tls);
      break;

    case PROP_AUTH:
      g_value_set_object (value, mail->auth);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_mail_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (object);

  switch (prop_id)
    {
    case PROP_HOST_AND_PORT:
      mail->host_and_port = g_value_dup_string (value);
      break;

    case PROP_USE_TLS:
      mail->use_tls = g_value_get_boolean (value);
      break;

    case PROP_AUTH:
      mail->auth = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_backend_imap_mail_init (GoaBackendImapMail *mail)
{
  /* Ensure D-Bus method invocations run in their own thread */
  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (mail),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
}

static void
goa_backend_imap_mail_class_init (GoaBackendImapMailClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = goa_backend_imap_mail_finalize;
  gobject_class->set_property = goa_backend_imap_mail_set_property;
  gobject_class->get_property = goa_backend_imap_mail_get_property;

  g_object_class_install_property (gobject_class,
                                   PROP_HOST_AND_PORT,
                                   g_param_spec_string ("host-and-port",
                                                        "host-and-port",
                                                        "host-and-port",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_USE_TLS,
                                   g_param_spec_boolean ("use-tls",
                                                         "use-tls",
                                                         "use-tls",
                                                         TRUE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_WRITABLE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
                                   PROP_AUTH,
                                   g_param_spec_object ("auth",
                                                        "auth",
                                                        "auth",
                                                        GOA_TYPE_BACKEND_IMAP_AUTH,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_imap_mail_new:
 * @host_and_port: The name and optionally port to connect to.
 * @use_tls: Whether TLS should be used.
 * @auth: Object used for authenticating the connection.
 *
 * Creates a new #GoaMail object.
 *
 * Returns: (type GoaBackendImapMail): A new #GoaMail instance.
 */
GoaMail *
goa_backend_imap_mail_new (const gchar         *host_and_port,
                           gboolean             use_tls,
                           GoaBackendImapAuth  *auth)
{
  g_return_val_if_fail (host_and_port != NULL, NULL);
  return GOA_MAIL (g_object_new (GOA_TYPE_BACKEND_IMAP_MAIL,
                                 "host-and-port", host_and_port,
                                 "use-tls", use_tls,
                                 "auth", auth,
                                 NULL));
}

/* ---------------------------------------------------------------------------------------------------- */

static void on_imap_client_updated (GoaBackendImapClient *client,
                                    gpointer              user_data);

static void on_imap_client_closed (GoaBackendImapClient *client,
                                   gpointer              user_data);

typedef struct
{
  volatile gint ref_count;

  GoaBackendImapMail *mail;
  GoaMailQuery *query;
  GCancellable *cancellable;
  GoaObject *object;
  GoaBackendImapClient *imap_client;

  guint name_watcher_id;
} QueryData;

#if 0
static QueryData *
query_data_ref (QueryData *data)
{
  g_atomic_int_inc (&data->ref_count);
  return data;
}
#endif

static void
query_data_unref (QueryData *data)
{
  if (g_atomic_int_dec_and_test (&data->ref_count))
    {
      if (data->name_watcher_id)
        g_bus_unwatch_name (data->name_watcher_id);
      g_object_unref (data->mail);
      g_object_unref (data->query);
      g_object_unref (data->cancellable);
      if (data->imap_client != NULL)
        {
          g_signal_handlers_disconnect_by_func (data->imap_client, G_CALLBACK (on_imap_client_updated), data);
          g_signal_handlers_disconnect_by_func (data->imap_client, G_CALLBACK (on_imap_client_closed), data);
          g_object_unref (data->imap_client);
        }
      g_slice_free (QueryData, data);
    }
}

static void
nuke_query (QueryData *data)
{
  /* yippee ki yay motherfucker */

  /* unexport the D-Bus object */
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (data->query));
  /* shutdown the IMAP client */
  if (data->imap_client != NULL)
    goa_backend_imap_client_close (data->imap_client);
  query_data_unref (data);
}

static void
on_query_owner_vanished (GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
  QueryData *data = user_data;
  nuke_query (data);
}

static gboolean
mail_query_create_imap_client_sync (QueryData  *data,
                                    GError      **error);

/* runs in thread dedicated to the method invocation */
static gboolean
mail_query_on_handle_close (GoaMailQuery        *query,
                            GDBusMethodInvocation *invocation,
                            gpointer               user_data)
{
  QueryData *data = user_data;
  nuke_query (data);
  return TRUE;
}

/* runs in thread dedicated to the method invocation */
static gboolean
mail_query_on_handle_refresh (GoaMailQuery        *query,
                              GDBusMethodInvocation *invocation,
                              gpointer               user_data)
{
  QueryData *data = user_data;
  GError *error;

  if (data->imap_client == NULL)
    {
      error = NULL;
      if (!mail_query_create_imap_client_sync (data, &error))
        {
          g_prefix_error (&error, "Error creating IMAP client: ");
          g_dbus_method_invocation_return_gerror (invocation, error);
          g_error_free (error);
          goto out;
        }
      goa_mail_query_complete_refresh (query, invocation);
      goto out;
    }

  error = NULL;
  if (!goa_backend_imap_client_refresh_sync (data->imap_client,
                                             NULL, /* GCancellable */
                                             &error))
    {
      g_dbus_method_invocation_return_gerror (invocation, error);
      g_error_free (error);
    }
  else
    {
      goa_mail_query_complete_refresh (query, invocation);
    }
 out:
  return TRUE;
}


static void
on_imap_client_closed (GoaBackendImapClient *client,
                       gpointer              user_data)
{
  QueryData *data = user_data;

  //g_debug ("##### on_imap_client_closed");

  goa_mail_query_set_connected (data->query, FALSE);
  g_signal_handlers_disconnect_by_func (data->imap_client, G_CALLBACK (on_imap_client_updated), data);
  g_signal_handlers_disconnect_by_func (data->imap_client, G_CALLBACK (on_imap_client_closed), data);
  g_object_unref (data->imap_client);
  data->imap_client = NULL;
}

static void
on_imap_client_updated (GoaBackendImapClient *client,
                        gpointer              user_data)
{
  QueryData *data = user_data;
  GList *messages;
  GList *l;
  GVariantBuilder builder;

  //g_debug ("##### on_imap_client_updated");

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(stsssia{sv})"));

  messages = goa_backend_imap_client_get_messages (data->imap_client);
  for (l = messages; l != NULL; l = l->next)
    {
      GoaBackendImapMessage *message = l->data;
      gchar guid[20];
      const gchar *from;
      const gchar *subject;
      GoaBackendImapMessageFlags imap_flags;
      gint flags;
      GVariantBuilder extras_builder;

      from = goa_backend_imap_message_lookup_header (message, "From");
      subject = goa_backend_imap_message_lookup_header (message, "Subject");
      imap_flags = goa_backend_imap_message_get_flags (message);

      flags = 0;
      if (!(imap_flags & GOA_BACKEND_IMAP_MESSAGE_FLAGS_SEEN))
        flags |= 1;

      g_variant_builder_init (&extras_builder, G_VARIANT_TYPE_VARDICT);

      g_snprintf (guid, sizeof guid, "%" G_GUINT64_FORMAT,
                  goa_backend_imap_message_get_uid (message));

      g_variant_builder_add (&builder, "(stsssia{sv})",
                             guid,
                             goa_backend_imap_message_get_internal_date (message),
                             from != NULL ? from : _("No Sender"),
                             subject != NULL ? subject : _("No Subject"),
                             goa_backend_imap_message_get_excerpt (message),
                             flags,
                             &extras_builder);
    }
  goa_mail_query_set_result (data->query, g_variant_builder_end (&builder));
  goa_mail_query_set_num_unread (data->query, goa_backend_imap_client_get_num_unread (data->imap_client));
  goa_mail_query_set_num_messages (data->query, goa_backend_imap_client_get_num_messages (data->imap_client));
  g_dbus_interface_skeleton_flush (G_DBUS_INTERFACE_SKELETON (data->query));

  g_list_foreach (messages, (GFunc) goa_backend_imap_message_ref, NULL);
  g_list_free (messages);
}

static gboolean
mail_query_create_imap_client_sync (QueryData  *data,
                                    GError    **error)
{
  gboolean ret;

  g_return_val_if_fail (data->imap_client == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  /* gets ourselves an IMAP client - it's not running yet */
  data->imap_client = goa_backend_imap_client_new_sync (data->mail->host_and_port,
                                                        data->mail->use_tls,
                                                        data->mail->auth,
                                                        goa_mail_query_get_criteria (data->query),
                                                        0, /* offset */
                                                        goa_mail_query_get_max_size (data->query),
                                                        NULL, /* GCancellable */
                                                        error);
  if (data->imap_client == NULL)
    goto out;

  /* initial update */
  on_imap_client_updated (data->imap_client, data);
  goa_mail_query_set_connected (data->query, TRUE);

  /* subsequent updates */
  g_signal_connect (data->imap_client,
                    "updated",
                    G_CALLBACK (on_imap_client_updated),
                    data);

  g_signal_connect (data->imap_client,
                    "closed",
                    G_CALLBACK (on_imap_client_closed),
                    data);

  ret = TRUE;

 out:
  return ret;
}

/* runs in thread dedicated to the method invocation */
static gboolean
handle_create_query (GoaMail                *_mail,
                     GDBusMethodInvocation  *invocation,
                     const gchar            *criteria,
                     gint                    max_size)
{
  GoaBackendImapMail *mail = GOA_BACKEND_IMAP_MAIL (_mail);
  gchar *query_object_path;
  GError *error;
  QueryData *data;
  static gint _g_query_count = 0;

  query_object_path = NULL;

  data = g_slice_new0 (QueryData);
  data->ref_count = 1;
  data->mail = g_object_ref (mail);
  data->query = goa_mail_query_skeleton_new ();
  data->cancellable = g_cancellable_new ();

  goa_mail_query_set_criteria (data->query, criteria);
  goa_mail_query_set_max_size (data->query, max_size);

  /* Create the IMAP client - it's not fatal if the client is not
   * working (could be the case if there is no network) since the user
   * can call Refresh() to bring it up again.
   */
  error = NULL;
  if (!mail_query_create_imap_client_sync (data, &error))
    {
      g_error_free (error);
    }

  g_dbus_interface_skeleton_set_flags (G_DBUS_INTERFACE_SKELETON (data->query),
                                       G_DBUS_INTERFACE_SKELETON_FLAGS_HANDLE_METHOD_INVOCATIONS_IN_THREAD);
  g_signal_connect (data->query,
                    "handle-refresh",
                    G_CALLBACK (mail_query_on_handle_refresh),
                    data);
  g_signal_connect (data->query,
                    "handle-close",
                    G_CALLBACK (mail_query_on_handle_close),
                    data);

  query_object_path = g_strdup_printf ("/org/gnome/OnlineAccounts/mail_queries/%d", _g_query_count++);

  error = NULL;
  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (data->query),
                                         g_dbus_method_invocation_get_connection (invocation),
                                         query_object_path,
                                         &error))
    {
      g_prefix_error (&error, "Error exporting mail query: ");
      g_dbus_method_invocation_return_gerror (invocation, error);
      query_data_unref (data);
      goto out;
    }

  data->name_watcher_id = g_bus_watch_name_on_connection (g_dbus_method_invocation_get_connection (invocation),
                                                          g_dbus_method_invocation_get_sender (invocation),
                                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                          NULL, /* name_appeared_handler */
                                                          on_query_owner_vanished,
                                                          data,
                                                          NULL);

  /* TODO: set up things so only caller can access the created object? */

  goa_mail_complete_create_query (GOA_MAIL (mail), invocation, query_object_path);

 out:
  g_free (query_object_path);
  return TRUE; /* invocation was handled */
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_backend_imap_mail__goa_mail_iface_init (GoaMailIface *iface)
{
  iface->handle_create_query = handle_create_query;
}

/* ---------------------------------------------------------------------------------------------------- */
