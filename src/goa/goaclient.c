
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
} GoaClientClass;

enum
{
  PROP_0,
  PROP_OBJECT_MANAGER
};

static void initable_iface_init       (GInitableIface      *initable_iface);
static void async_initable_iface_init (GAsyncInitableIface *async_initable_iface);

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

  g_object_unref (client->object_manager);

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
