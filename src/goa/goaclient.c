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
#include <glib/gi18n-lib.h>

#include "goaclient.h"
#include "goaerror.h"

G_LOCK_DEFINE_STATIC (init_lock);

/**
 * GoaClient:
 *
 * [class@Goa.Client] is used for accessing the GNOME Online Accounts service
 * from a client program.
 */
struct _GoaClient
{
  GObject parent_instance;

  gboolean is_initialized;
  GError *initialization_error;

  GDBusObjectManager *object_manager;
};

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
  GoaClient *self = GOA_CLIENT (object);

  if (self->initialization_error != NULL)
    g_error_free (self->initialization_error);

  if (self->object_manager != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->object_manager, G_CALLBACK (on_object_added), self);
      g_signal_handlers_disconnect_by_func (self->object_manager, G_CALLBACK (on_object_removed), self);
      g_signal_handlers_disconnect_by_func (self->object_manager, G_CALLBACK (on_interface_proxy_properties_changed), self);
      g_signal_handlers_disconnect_by_func (self->object_manager, G_CALLBACK (on_interface_added), self);
      g_signal_handlers_disconnect_by_func (self->object_manager, G_CALLBACK (on_interface_removed), self);
      g_object_unref (self->object_manager);
    }

  G_OBJECT_CLASS (goa_client_parent_class)->finalize (object);
}

static void
goa_client_init (GoaClient *self)
{
  /* this will force associating errors in the GOA_ERROR error domain
   * with org.freedesktop.Goa.Error.* errors via g_dbus_error_register_error_domain().
   */
  goa_error_quark ();
}

static void
goa_client_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  GoaClient *self = GOA_CLIENT (object);

  switch (prop_id)
    {
    case PROP_OBJECT_MANAGER:
      g_value_set_object (value, goa_client_get_object_manager (self));
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
   * GoaClient:object-manager: (getter get_object_manager)
   *
   * The [iface@Gio.DBusObjectManager] used by the [class@Goa.Client] instance.
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
   * @client: The `GoaClient` object emitting the signal
   * @object: The `GoaObject` for the added account
   *
   * Emitted when @object has been added. See [method@Goa.Client.get_accounts]
   * for information about how to use this object.
   */
  signals[ACCOUNT_ADDED_SIGNAL] =
    g_signal_new ("account-added",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

  /**
   * GoaClient::account-removed:
   * @client: The `GoaClient` object emitting the signal
   * @object: The `GoaObject` for the removed account
   *
   * Emitted when @object has been removed.
   */
  signals[ACCOUNT_REMOVED_SIGNAL] =
    g_signal_new ("account-removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

  /**
   * GoaClient::account-changed:
   * @client: The `GoaClient` object emitting the signal
   * @object: The `GoaObject` for the account with changes
   *
   * Emitted when something on @object changes.
   */
  signals[ACCOUNT_CHANGED_SIGNAL] =
    g_signal_new ("account-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE,
                  1,
                  GOA_TYPE_OBJECT);

}

/**
 * goa_client_new: (finish-func new_finish)
 * @cancellable: (nullable): A `GCancellable`
 * @callback: A callback to call when the operation is complete
 * @user_data: Data to pass to @callback
 *
 * Asynchronously gets a [class@Goa.Client].
 *
 * This is a failable asynchronous constructor - when the client is ready,
 * @callback will be invoked and you can use [ctor@Goa.Client.new_finish] to
 * get the result.
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
 * @res: A `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finishes an operation started with [func@Goa.Client.new].
 *
 * Returns: (transfer full): A `GoaClient`, or %NULL with @error set
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
 * @cancellable: (nullable): A `GCancellable`
 * @error: (nullable): A `GError`
 *
 * Synchronously gets a [class@Goa.Client].
 *
 * Returns: (transfer full): A `GoaClient`, or %NULL with @error set
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
  GoaClient *self = GOA_CLIENT (initable);
  gboolean ret = FALSE;

  /* This method needs to be idempotent to work with the singleton
   * pattern. See the docs for g_initable_init(). We implement this by
   * locking.
   */
  G_LOCK (init_lock);
  if (self->is_initialized)
    {
      if (self->object_manager != NULL)
        ret = TRUE;
      else
        g_assert (self->initialization_error != NULL);
      goto out;
    }
  g_assert (self->initialization_error == NULL);

  self->object_manager = goa_object_manager_client_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                     G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                                     "org.gnome.OnlineAccounts",
                                                                     "/org/gnome/OnlineAccounts",
                                                                     cancellable,
                                                                     &self->initialization_error);
  if (self->object_manager == NULL)
    goto out;
  g_signal_connect (self->object_manager,
                    "object-added",
                    G_CALLBACK (on_object_added),
                    self);
  g_signal_connect (self->object_manager,
                    "object-removed",
                    G_CALLBACK (on_object_removed),
                    self);
  g_signal_connect (self->object_manager,
                    "interface-proxy-properties-changed",
                    G_CALLBACK (on_interface_proxy_properties_changed),
                    self);
  g_signal_connect (self->object_manager,
                    "interface-added",
                    G_CALLBACK (on_interface_added),
                    self);
  g_signal_connect (self->object_manager,
                    "interface-removed",
                    G_CALLBACK (on_interface_removed),
                    self);

  ret = TRUE;

out:
  self->is_initialized = TRUE;
  if (!ret)
    {
      g_assert (self->initialization_error != NULL);
      g_propagate_error (error, g_error_copy (self->initialization_error));
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
 * goa_client_get_object_manager: (get-property object-manager)
 * @self: A `GoaClient`
 *
 * Gets the [iface@Gio.DBusObjectManager] used by @self.
 *
 * Returns: (transfer none): A `GDBusObjectManager`
 */
GDBusObjectManager *
goa_client_get_object_manager (GoaClient        *self)
{
  g_return_val_if_fail (GOA_IS_CLIENT (self), NULL);
  return self->object_manager;
}

/**
 * goa_client_get_manager:
 * @self: A `GoaClient`
 *
 * Gets the [iface@Goa.Manager] for @self, if any.
 *
 * Returns: (nullable) (transfer none): A `GoaManager` or %NULL
 */
GoaManager *
goa_client_get_manager (GoaClient *self)
{
  GDBusObject *object;
  GoaManager *manager = NULL;

  object = g_dbus_object_manager_get_object (self->object_manager, "/org/gnome/OnlineAccounts/Manager");
  if (object == NULL)
    goto out;

  manager = goa_object_peek_manager (GOA_OBJECT (object));

 out:
  g_clear_object (&object);
  return manager;
}

/**
 * goa_client_get_accounts:
 * @self: A `GoaClient`
 *
 * Gets all accounts that @self knows about.
 *
 * The result is a list of [iface@Goa.Object] instances where each object at
 * least has an [iface@Goa.Account] interface (that can be obtained via the
 * [method@Goa.Object.get_account] method) but may also implement other
 * interfaces such as [iface@Goa.Mail] or [iface@Goa.Files].
 *
 * Returns: (transfer full) (element-type Goa.Object): A list of `GoaObject`
 *   instances
 */
GList *
goa_client_get_accounts (GoaClient *self)
{
  GList *ret = NULL;
  GList *objects;
  GList *l;

  g_return_val_if_fail (GOA_IS_CLIENT (self), NULL);

  objects = g_dbus_object_manager_get_objects (self->object_manager);
  for (l = objects; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);

      if (goa_object_peek_account (object) != NULL)
        ret = g_list_prepend (ret, g_object_ref (object));
    }
  g_list_free_full (objects, g_object_unref);

  return ret;
}

/**
 * goa_client_lookup_by_id:
 * @self: A `GoaClient`
 * @id: The ID to look for
 *
 * Finds and returns the [iface@Goa.Object] instance whose
 * [property@Goa.Account:id] D-Bus property matches @id.
 *
 * Returns: (transfer full): the object for the given id
 *
 * Since: 3.6
 */
GoaObject *
goa_client_lookup_by_id (GoaClient           *self,
                         const gchar         *id)
{
  GList *accounts;
  GList *l;
  GoaObject *ret = NULL;

  accounts = goa_client_get_accounts (self);
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
  GoaClient *self = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (self, signals[ACCOUNT_ADDED_SIGNAL], 0, object);
}

static void
on_object_removed (GDBusObjectManager   *manager,
                   GDBusObject          *object,
                   gpointer              user_data)
{
  GoaClient *self = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (self, signals[ACCOUNT_REMOVED_SIGNAL], 0, object);
}

static void
on_interface_proxy_properties_changed (GDBusObjectManagerClient   *manager,
                                       GDBusObjectProxy           *object_proxy,
                                       GDBusProxy                 *interface_proxy,
                                       GVariant                   *changed_properties,
                                       const gchar* const         *invalidated_properties,
                                       gpointer                    user_data)
{
  GoaClient *self = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object_proxy)) != NULL)
    g_signal_emit (self, signals[ACCOUNT_CHANGED_SIGNAL], 0, object_proxy);
}

static void
on_interface_added (GDBusObjectManager   *manager,
                    GDBusObject          *object,
                    GDBusInterface       *interface,
                    gpointer              user_data)
{
  GoaClient *self = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (self, signals[ACCOUNT_CHANGED_SIGNAL], 0, object);
}

static void
on_interface_removed (GDBusObjectManager   *manager,
                      GDBusObject          *object,
                      GDBusInterface       *interface,
                      gpointer              user_data)
{
  GoaClient *self = GOA_CLIENT (user_data);
  if (goa_object_peek_account (GOA_OBJECT (object)) != NULL)
    g_signal_emit (self, signals[ACCOUNT_CHANGED_SIGNAL], 0, object);
}
