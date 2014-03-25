/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2011, 2012 Red Hat, Inc.
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
#include <glib/gi18n-lib.h>

#include "goaclient.h"
#include "goaerror.h"
#include "goa-generated.h"

G_LOCK_DEFINE_STATIC (init_lock);

/**
 * SECTION:goaclient
 * @title: GoaClient
 * @short_description: Object for accessing account information
 *
 * #GoaClient is used for accessing the GNOME Online Accounts service
 * from a client program.
 */

/**
 * GoaClient:
 *
 * The #GoaClient structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaClient
{
  GObject parent_instance;

  gboolean is_initialized;
  GError *initialization_error;

  GDBusObjectManager *object_manager;
};

typedef struct
{
  GObjectClass parent_class;

  void (*account_added) (GoaClient *client, GDBusObject *object);
  void (*account_removed) (GoaClient *client, GDBusObject *object);
  void (*account_changed) (GoaClient *client, GDBusObject *object);
} GoaClientClass;

enum
{
  PROP_0,
  PROP_OBJECT_MANAGER
};

enum
{
  ACCOUNT_ADDED_SIGNAL,
  ACCOUNT_REMOVED_SIGNAL,
  ACCOUNT_CHANGED_SIGNAL,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void initable_iface_init       (GInitableIface      *initable_iface);
static void async_initable_iface_init (GAsyncInitableIface *async_initable_iface);

static void on_object_added (GDBusObjectManager   *manager,
                             GDBusObject          *object,
                             gpointer              user_data);
static void on_object_removed (GDBusObjectManager   *manager,
                               GDBusObject          *object,
                               gpointer              user_data);
static void on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                                   GDBusObjectProxy           *object_proxy,
                                                   GDBusProxy                 *interface_proxy,
                                                   GVariant                   *changed_properties,
                                                   const gchar* const         *invalidated_properties,
                                                   gpointer                    user_data);
static void on_interface_added (GDBusObjectManager   *manager,
                                GDBusObject          *object,
                                GDBusInterface       *interface,
                                gpointer              user_data);
static void on_interface_removed (GDBusObjectManager   *manager,
                                  GDBusObject          *object,
                                  GDBusInterface       *interface,
                                  gpointer              user_data);

G_DEFINE_TYPE_WITH_CODE (GoaClient, goa_client, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, initable_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init)
                         );

static void
goa_client_finalize (GObject *object)
{
  GoaClient *client = GOA_CLIENT (object);

  if (client->initialization_error != NULL)
    g_error_free (client->initialization_error);

  if (client->object_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (client->object_manager, G_CALLBACK (on_object_added), client);
      g_signal_handlers_disconnect_by_func (client->object_manager, G_CALLBACK (on_object_removed), client);
      g_signal_handlers_disconnect_by_func (client->object_manager, G_CALLBACK (on_interface_proxy_properties_changed), client);
      g_signal_handlers_disconnect_by_func (client->object_manager, G_CALLBACK (on_interface_added), client);
      g_signal_handlers_disconnect_by_func (client->object_manager, G_CALLBACK (on_interface_removed), client);
      g_object_unref (client->object_manager);
    }

  G_OBJECT_CLASS (goa_client_parent_class)->finalize (object);
}

static void
goa_client_init (GoaClient *client)
{
  static volatile GQuark goa_error_domain = 0;
  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_domain = GOA_ERROR;
  goa_error_domain; /* shut up -Wunused-but-set-variable */
}

static void
goa_client_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GoaClient *client = GOA_CLIENT (object);

  switch (prop_id)
    {
    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, goa_client_get_object_manager (client));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_client_class_init (GoaClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_client_finalize;
  gobject_class->get_property = goa_client_get_property;

  /**
   * GoaClient:object-manager:
   *
   * The #GDBusObjectManager used by the #GoaClient instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_MANAGER,
                                   g_param_spec_object ("object-manager",
                                                        "object manager",
                                                        "The GDBusObjectManager used by the GoaClient",
                                                        G_TYPE_DBUS_OBJECT_MANAGER,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /**
   * GoaClient::account-added:
   * @client: The #GoaClient object emitting the signal.
   * @object: The #GoaObject for the added account.
   *
   * Emitted when @object has been added. See
   * goa_client_get_accounts() for information about how to use this
   * object.
   */
  signals[ACCOUNT_ADDED_SIGNAL] =
    g_signal_new ("account-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaClientClass, account_added),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

  /**
   * GoaClient::account-removed:
   * @client: The #GoaClient object emitting the signal.
   * @object: The #GoaObject for the removed account.
   *
   * Emitted when @object has been removed.
   */
  signals[ACCOUNT_REMOVED_SIGNAL] =
    g_signal_new ("account-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaClientClass, account_removed),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

  /**
   * GoaClient::account-changed:
   * @client: The #GoaClient object emitting the signal.
   * @object: The #GoaObject for the account with changes.
   *
   * Emitted when something on @object changes.
   */
  signals[ACCOUNT_CHANGED_SIGNAL] =
    g_signal_new ("account-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GoaClientClass, account_changed),
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

}

/**
 * goa_client_new:
 * @cancellable: A #GCancellable or %NULL.
 * @callback: Function that will be called when the result is ready.
 * @user_data: Data to pass to @callback.
 *
 * Asynchronously gets a #GoaClient. When the operation is
 * finished, @callback will be invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> of the thread you are calling this method from.
 */
void
goa_client_new (GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  g_async_initable_new_async (GOA_TYPE_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              callback,
                              user_data,
                              NULL);
}

/**
 * goa_client_new_finish:
 * @res: A #GAsyncResult.
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_client_new().
 *
 * Returns: A #GoaClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
GoaClient *
goa_client_new_finish (GAsyncResult        *res,
                       GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return GOA_CLIENT (ret);
  else
    return NULL;
}

/**
 * goa_client_new_sync:
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: (allow-none): Return location for error or %NULL.
 *
 * Synchronously gets a #GoaClient for the local system.
 *
 * Returns: A #GoaClient or %NULL if @error is set. Free with
 * g_object_unref() when done with it.
 */
GoaClient *
goa_client_new_sync (GCancellable  *cancellable,
                        GError       **error)
{
  GInitable *ret;
  ret = g_initable_new (GOA_TYPE_CLIENT,
                        cancellable,
                        error,
                        NULL);
  if (ret != NULL)
    return GOA_CLIENT (ret);
  else
    return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
initable_init (GInitable     *initable,
               GCancellable  *cancellable,
               GError       **error)
{
  GoaClient *client = GOA_CLIENT (initable);
  gboolean ret;

  ret = FALSE;

  /* This method needs to be idempotent to work with the singleton
   * pattern. See the docs for g_initable_init(). We implement this by
   * locking.
   */
  G_LOCK (init_lock);
  if (client->is_initialized)
    {
      if (client->object_manager != NULL)
        ret = TRUE;
      else
        g_assert (client->initialization_error != NULL);
      goto out;
    }
  g_assert (client->initialization_error == NULL);

  client->object_manager = goa_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                       G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                       "org.gnome.OnlineAccounts",
                                                                       "/org/gnome/OnlineAccounts",
                                                                       cancellable,
                                                                       &client->initialization_error);
  if (client->object_manager == NULL)
    goto out;
  g_signal_connect (client->object_manager,
                    "object-added",
                    G_CALLBACK (on_object_added),
                    client);
  g_signal_connect (client->object_manager,
                    "object-removed",
                    G_CALLBACK (on_object_removed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-added",
                    G_CALLBACK (on_interface_added),
                    client);
  g_signal_connect (client->object_manager,
                    "interface-removed",
                    G_CALLBACK (on_interface_removed),
                    client);

  ret = TRUE;

out:
  client->is_initialized = TRUE;
  if (!ret)
    {
      g_assert (client->initialization_error != NULL);
      g_propagate_error (error, g_error_copy (client->initialization_error));
    }
  G_UNLOCK (init_lock);
  return ret;
}

static void
initable_iface_init (GInitableIface      *initable_iface)
{
  initable_iface->init = initable_init;
}

static void
async_initable_iface_init (GAsyncInitableIface *async_initable_iface)
{
  /* Use default implementation (e.g. run GInitable code in a thread) */
}

/**
 * goa_client_get_object_manager:
 * @client: A #GoaClient.
 *
 * Gets the #GDBusObjectManager used by @client.
 *
 * Returns: (transfer none): A #GDBusObjectManager. Do not free, the
 * instance is owned by @client.
 */
GDBusObjectManager *
goa_client_get_object_manager (GoaClient        *client)
{
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  return client->object_manager;
}

/**
 * goa_client_get_manager:
 * @client: A #GoaClient.
 *
 * Gets the #GoaManager for @client.
 *
 * Returns: (transfer none): A #GoaManager. Do not free, the returned
 * object belongs to @client.
 */
GoaManager *
goa_client_get_manager (GoaClient *client)
{
  GDBusObject *object;
  GoaManager *manager;

  manager = NULL;
  object = g_dbus_object_manager_get_object (client->object_manager, "/org/gnome/OnlineAccounts/Manager");
  if (object == NULL)
    goto out;

  manager = goa_object_peek_manager (GOA_OBJECT (object));

 out:
  if (object != NULL)
    g_object_unref (object);
  return manager;
}

/**
 * goa_client_get_accounts:
 * @client: A #GoaClient.
 *
 * Gets all accounts that @client knows about. The result is a list of
 * #GoaObject instances where each object at least has an #GoaAccount
 * interface (that can be obtained via the goa_object_get_account()
 * method) but may also implement other interfaces such as
 * #GoaMail or #GoaFiles.
 *
 * Returns: (transfer full) (element-type GoaObject): A list of
 * #GoaObject instances that must be freed with g_list_free() after
 * each element has been freed with g_object_unref().
 */
GList *
goa_client_get_accounts (GoaClient *client)
{
  GList *ret;
  GList *objects;
  GList *l;

  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);

  ret = NULL;
  objects = g_dbus_object_manager_get_objects (client->object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);

      if (goa_object_peek_account (object) != NULL)
        ret = g_list_prepend (ret, g_object_ref (object));
    }
  g_list_foreach (objects, (GFunc) g_object_unref, NULL);
  g_list_free (objects);

  return ret;
}

/**
 * goa_client_lookup_by_id:
 * @client: A #GoaClient.
 * @id: The ID to look for.
 *
 * Finds and returns the #GoaObject instance whose
 * <link
 * linkend="gdbus-property-org-gnome-OnlineAccounts-Account.Id">"Id"</link>
 * D-Bus property matches @id.
 *
 * Returns: (transfer full): A #GoaObject. Free the returned
 * object with g_object_unref().
 *
 * Since: 3.6
 */
GoaObject *
goa_client_lookup_by_id (GoaClient           *client,
                         const gchar         *id)
{
  GList *accounts;
  GList *l;
  GoaObject *ret;

  ret = NULL;

  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      GoaAccount *account;
      GoaObject *object = GOA_OBJECT (l->data);

      account = goa_object_peek_account (object);
      if (account == NULL)
        continue;

      if (g_strcmp0 (goa_account_get_id (account), id) == 0)
        {
          ret = g_object_ref (object);
          break;
        }
    }

  g_list_free_full (accounts, g_object_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_object_added (GDBusObjectManager   *manager,
                 GDBusObject          *object,
                 gpointer              user_data)
{
  GoaClient *client = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (client, signals[ACCOUNT_ADDED_SIGNAL], 0, object);
}

static void
on_object_removed (GDBusObjectManager   *manager,
                   GDBusObject          *object,
                   gpointer              user_data)
{
  GoaClient *client = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (client, signals[ACCOUNT_REMOVED_SIGNAL], 0, object);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar* const         *invalidated_properties,
                                       gpointer                    user_data)
{
  GoaClient *client = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object_proxy)) != NULL)
    g_signal_emit (client, signals[ACCOUNT_CHANGED_SIGNAL], 0, object_proxy);
}

static void
on_interface_added (GDBusObjectManager   *manager,
                    GDBusObject          *object,
                    GDBusInterface       *interface,
                    gpointer              user_data)
{
  GoaClient *client = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (client, signals[ACCOUNT_CHANGED_SIGNAL], 0, object);
}

static void
on_interface_removed (GDBusObjectManager   *manager,
                      GDBusObject          *object,
                      GDBusInterface       *interface,
                      gpointer              user_data)
{
  GoaClient *client = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (client, signals[ACCOUNT_CHANGED_SIGNAL], 0, object);
}
