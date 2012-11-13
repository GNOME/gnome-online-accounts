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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Debarshi Ray <debarshir@gnome.org>
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "goalogging.h"
#include "goaprovider.h"
#include "goaexchangeprovider.h"
#include "goagoogleprovider.h"
#include "goafacebookprovider.h"
#include "goawindowsliveprovider.h"

#ifdef GOA_KERBEROS_ENABLED
#include "goakerberosprovider.h"
#endif

#include "goaeditablelabel.h"
#include "goautils.h"

/**
 * SECTION:goaprovider
 * @title: GoaProvider
 * @short_description: Abstract base class for providers
 *
 * #GoaProvider is the base type for all providers.
 */

static gboolean goa_provider_ensure_credentials_sync_real (GoaProvider   *provider,
                                                           GoaObject     *object,
                                                           gint          *out_expires_in,
                                                           GCancellable  *cancellable,
                                                           GError       **error);

static gboolean goa_provider_build_object_real (GoaProvider         *provider,
                                                GoaObjectSkeleton   *object,
                                                GKeyFile            *key_file,
                                                const gchar         *group,
                                                GDBusConnection     *connection,
                                                gboolean             just_added,
                                                GError             **error);

static void goa_provider_show_account_real (GoaProvider         *provider,
                                            GoaClient           *client,
                                            GoaObject           *object,
                                            GtkBox              *vbox,
                                            GtkGrid             *left,
                                            GtkGrid             *right);

static guint goa_provider_get_credentials_generation_real (GoaProvider *provider);

static GIcon *goa_provider_get_provider_icon_default (GoaProvider *provider,
                                                      GoaObject   *object);

G_DEFINE_ABSTRACT_TYPE (GoaProvider, goa_provider, G_TYPE_OBJECT);

static void
goa_provider_init (GoaProvider *client)
{
}

static void
goa_provider_class_init (GoaProviderClass *klass)
{
  klass->build_object = goa_provider_build_object_real;
  klass->ensure_credentials_sync = goa_provider_ensure_credentials_sync_real;
  klass->show_account = goa_provider_show_account_real;
  klass->get_credentials_generation = goa_provider_get_credentials_generation_real;
  klass->get_provider_icon = goa_provider_get_provider_icon_default;
}

/**
 * goa_provider_get_provider_type:
 * @provider: A #GoaProvider.
 *
 * Gets the type of @provider.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @provider, do not free.
 */
const gchar *
goa_provider_get_provider_type (GoaProvider *provider)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  return GOA_PROVIDER_GET_CLASS (provider)->get_provider_type (provider);
}

/**
 * goa_provider_get_provider_name:
 * @provider: A #GoaProvider.
 * @object: (allow-none): A #GoaObject for an account.
 *
 * Gets a name for @provider and @object that is suitable for display
 * in an user interface. The returned value may depend on @object (if
 * it's not %NULL) - for example, hosted accounts might return a
 * different name.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer full): A string that should be freed with g_free().
 */
gchar *
goa_provider_get_provider_name (GoaProvider *provider,
                                GoaObject   *object)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  return GOA_PROVIDER_GET_CLASS (provider)->get_provider_name (provider, object);
}

/**
 * goa_provider_get_provider_icon:
 * @provider: A #GoaProvider.
 * @object: A #GoaObject for an account.
 *
 * Gets an icon for @provider and @object that is suitable for display
 * in an user interface. The returned value may depend on @object -
 * for example, hosted accounts might return a different icon.
 *
 * This is a virtual method with a default implementation that returns
 * a #GThemedIcon with fallbacks constructed from the name
 * <literal>goa-account-TYPE</literal> where <literal>TYPE</literal>
 * is the return value of goa_provider_get_provider_type().
 *
 * Returns: (transfer full): An icon that should be freed with g_object_unref().
 */
GIcon *
goa_provider_get_provider_icon (GoaProvider *provider,
                                GoaObject   *object)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  return GOA_PROVIDER_GET_CLASS (provider)->get_provider_icon (provider, object);
}

static GIcon *
goa_provider_get_provider_icon_default (GoaProvider *provider,
                                        GoaObject   *object)
{
  GIcon *ret;
  gchar *s;
  s = g_strdup_printf ("goa-account-%s", goa_provider_get_provider_type (provider));
  ret = g_themed_icon_new_with_default_fallbacks (s);
  g_free (s);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_add_account:
 * @provider: A #GoaProvider.
 * @client: A #GoaClient.
 * @dialog: A #GtkDialog.
 * @vbox: A vertically oriented #GtkBox to put content in.
 * @error: Return location for error or %NULL.
 *
 * This method brings up the user interface necessary to create a new
 * account on @client of the type for @provider, interacts with the
 * user to get all information needed and creates the account.
 *
 * The passed in @dialog widget is guaranteed to be visible with @vbox
 * being empty and the only visible widget in @dialog's content
 * area. The dialog has exactly one action widget, a cancel button
 * with response id GTK_RESPONSE_CANCEL. Implementations are free to
 * add additional action widgets, as needed.
 *
 * If an account was successfully created, a #GoaObject for the
 * created account is returned. If @dialog is dismissed, %NULL is
 * returned and @error is set to %GOA_ERROR_DIALOG_DISMISSED. If an
 * account couldn't be created then @error is set. In some cases,
 * for example, when the credentials could not be stored in the
 * keyring, a #GoaObject can be returned even if @error is set.
 *
 * The caller will always show an error dialog if @error is set unless
 * the error is %GOA_ERROR_DIALOG_DISMISSED.
 *
 * Implementations should run the <link
 * linkend="g_main_context_default">default main loop</link> while
 * interacting with the user and may do so using e.g. gtk_dialog_run()
 * on @dialog.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: The #GoaObject for the created account (must be relased
 *   with g_object_unref()) or %NULL if @error is set.
 */
GoaObject *
goa_provider_add_account (GoaProvider  *provider,
                          GoaClient    *client,
                          GtkDialog    *dialog,
                          GtkBox       *vbox,
                          GError      **error)
{
  GoaObject *ret;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = GOA_PROVIDER_GET_CLASS (provider)->add_account (provider, client, dialog, vbox, error);

  g_warn_if_fail ((ret == NULL && (error == NULL || *error != NULL)) || GOA_IS_OBJECT (ret));

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_refresh_account:
 * @provider: A #GoaProvider.
 * @client: A #GoaClient.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @parent: (allow-none): Transient parent of dialogs or %NULL.
 * @error: Return location for error or %NULL.
 *
 * This method brings up the user interface necessary for refreshing
 * the credentials for the account specified by @object. This
 * typically involves having the user log in to the account again.
 *
 * Implementations should use @parent (unless %NULL) as the transient
 * parent of any created windows/dialogs.
 *
 * Implementations should run the <link
 * linkend="g_main_context_default">default main loop</link> while
 * interacting with the user.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if the account has been refreshed, %FALSE if @error
 * is set.
 */
gboolean
goa_provider_refresh_account (GoaProvider  *provider,
                              GoaClient    *client,
                              GoaObject    *object,
                              GtkWindow    *parent,
                              GError      **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return GOA_PROVIDER_GET_CLASS (provider)->refresh_account (provider, client, object, parent, error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_show_account:
 * @provider: A #GoaProvider.
 * @client: A #GoaClient.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @dialog: A #GtkDialog.
 * @vbox: A vertically oriented #GtkBox to put content in.
 * @table: A table which is the first element of vbox.
 *
 * Method used to add widgets in the control panel for the account
 * represented by @object.
 *
 * This is a virtual method with an implementation that (currently)
 * does nothing. A subclass should chain up (before adding their own
 * widgets to @vbox and @table) if they want to expose such
 * functionality.
 */
void
goa_provider_show_account (GoaProvider         *provider,
                           GoaClient           *client,
                           GoaObject           *object,
                           GtkBox              *vbox,
                           GtkGrid             *left,
                           GtkGrid             *right)
{
  g_return_if_fail (GOA_IS_PROVIDER (provider));
  g_return_if_fail (GOA_IS_CLIENT (client));
  g_return_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL);
  g_return_if_fail (GTK_IS_BOX (vbox));
  g_return_if_fail (GTK_IS_GRID (left));
  g_return_if_fail (GTK_IS_GRID (right));

  GOA_PROVIDER_GET_CLASS (provider)->show_account (provider, client, object, vbox, left, right);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_provider_show_account_real (GoaProvider         *provider,
                                GoaClient           *client,
                                GoaObject           *object,
                                GtkBox              *vbox,
                                GtkGrid             *left,
                                GtkGrid             *right)
{
  goa_util_add_account_info (left, right, object);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_build_object:
 * @provider: A #GoaProvider.
 * @object: The #GoaObjectSkeleton that is being built.
 * @key_file: The #GKeyFile with configuation data.
 * @group: The group in @key_file to get data from.
 * @connection: The #GDBusConnection used by the daemon to connect to the message bus.
 * @just_added: Whether the account was newly created or being updated.
 * @error: Return location for error or %NULL.
 *
 * This method is called when construction account #GoaObject<!-- -->
 * from configuration data - it basically provides a way to add
 * provider-specific information.
 *
 * The passed in @object will have a #GoaAccount interface
 * set. Implementations should validate and use data from @key_file to
 * add more interfaces to @object.
 *
 * Note that this may be called on already exported objects - for
 * example on configuration files reload.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if data was valid, %FALSE if @error is set.
 */
gboolean
goa_provider_build_object (GoaProvider         *provider,
                           GoaObjectSkeleton   *object,
                           GKeyFile            *key_file,
                           const gchar         *group,
                           GDBusConnection     *connection,
                           gboolean             just_added,
                           GError             **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT_SKELETON (object) && goa_object_peek_account (GOA_OBJECT (object)) != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return GOA_PROVIDER_GET_CLASS (provider)->build_object (provider,
                                                          object,
                                                          key_file,
                                                          group,
                                                          connection,
                                                          just_added,
                                                          error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaObject *object;
  gint expires_in;
} EnsureCredentialsData;

static EnsureCredentialsData *
ensure_credentials_data_new (GoaObject *object)
{
  EnsureCredentialsData *data;
  data = g_new0 (EnsureCredentialsData, 1);
  data->object = g_object_ref (object);
  return data;
}

static void
ensure_credentials_data_free (EnsureCredentialsData *data)
{
  g_object_unref (data->object);
  g_free (data);
}

static void
ensure_credentials_in_thread_func (GSimpleAsyncResult *simple,
                                   GObject            *object,
                                   GCancellable       *cancellable)
{
  EnsureCredentialsData *data;
  GError *error;

  data = g_simple_async_result_get_op_res_gpointer (simple);

  error = NULL;
  if (!goa_provider_ensure_credentials_sync (GOA_PROVIDER (object),
                                             data->object,
                                             &data->expires_in,
                                             cancellable,
                                             &error))
    g_simple_async_result_take_error (simple, error);
}


/**
 * goa_provider_ensure_credentials:
 * @provider: A #GoaProvider.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Ensures that credentials for @object are still valid.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this function was called from. You can then call
 * goa_provider_ensure_credentials_finish() to get the result
 * of the operation.
 *
 * This is a virtual method where the default implementation simply
 * returns the %GOA_ERROR_NOT_SUPPORTED error. A subclass may provide
 * another implementation.
 */
void
goa_provider_ensure_credentials (GoaProvider          *provider,
                                 GoaObject            *object,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
  GSimpleAsyncResult *simple;

  g_return_if_fail (GOA_IS_PROVIDER (provider));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  simple = g_simple_async_result_new (G_OBJECT (provider),
                                      callback,
                                      user_data,
                                      goa_provider_ensure_credentials);
  g_simple_async_result_set_op_res_gpointer (simple,
                                             ensure_credentials_data_new (object),
                                             (GDestroyNotify) ensure_credentials_data_free);
  g_simple_async_result_run_in_thread (simple,
                                       ensure_credentials_in_thread_func,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
  g_object_unref (simple);
}

/**
 * goa_provider_ensure_credentials_finish:
 * @provider: A #GoaProvider.
 * @out_expires_in: (out): Return location for how long the expired credentials are good for (0 if unknown) or %NULL.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_provider_ensure_credentials().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_provider_ensure_credentials().
 *
 * Returns: %TRUE if the credentials for the passed #GoaObject are valid, %FALSE if @error is set.
 */
gboolean
goa_provider_ensure_credentials_finish (GoaProvider  *provider,
                                                gint                *out_expires_in,
                                                GAsyncResult        *res,
                                                GError             **error)
{
  GSimpleAsyncResult *simple = G_SIMPLE_ASYNC_RESULT (res);
  gboolean ret;
  EnsureCredentialsData *data;

  ret = FALSE;

  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_warn_if_fail (g_simple_async_result_get_source_tag (simple) == goa_provider_ensure_credentials);

  if (g_simple_async_result_propagate_error (simple, error))
    goto out;

  ret = TRUE;
  data = g_simple_async_result_get_op_res_gpointer (simple);
  if (out_expires_in != NULL)
    *out_expires_in = data->expires_in;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_ensure_credentials_sync:
 * @provider: A #GoaProvider.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @out_expires_in: (out): Return location for how long the expired credentials are good for (0 if unknown) or %NULL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like goa_provider_ensure_credentials() but blocks the
 * calling thread until an answer is received.
 *
 * Returns: %TRUE if the credentials for the passed #GoaObject are valid, %FALSE if @error is set.
 */
gboolean
goa_provider_ensure_credentials_sync (GoaProvider     *provider,
                                      GoaObject       *object,
                                      gint            *out_expires_in,
                                      GCancellable    *cancellable,
                                      GError         **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return GOA_PROVIDER_GET_CLASS (provider)->ensure_credentials_sync (provider, object, out_expires_in, cancellable, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_provider_ensure_credentials_sync_real (GoaProvider   *provider,
                                           GoaObject     *object,
                                           gint          *out_expires_in,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  g_set_error (error,
               GOA_ERROR,
               GOA_ERROR_NOT_SUPPORTED,
               _("ensure_credentials_sync is not implemented on type %s"),
               g_type_name (G_TYPE_FROM_INSTANCE (provider)));
  return FALSE;
}

static gboolean
goa_provider_build_object_real (GoaProvider         *provider,
                                GoaObjectSkeleton   *object,
                                GKeyFile            *key_file,
                                const gchar         *group,
                                GDBusConnection     *connection,
                                gboolean             just_added,
                                GError             **error)
{
  /* does nothing */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_get_credentials_generation:
 * @provider: A #GoaProvider.
 *
 * Gets the generation of credentials being used for the provider.
 *
 * Implementations should bump this number when changes are introduced
 * that may render existing credentials unusable.
 *
 * For example, if an additional scope is requested (e.g. access to
 * contacts data) while obtaining credentials, then this number needs
 * to be bumped since existing credentials are not good for the added
 * scope.
 *
 * This is a virtual method where the default implementation returns
 * 0.
 *
 * Returns: The current generation of credentials.
 */
guint
goa_provider_get_credentials_generation (GoaProvider *provider)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), 0);
  return GOA_PROVIDER_GET_CLASS (provider)->get_credentials_generation (provider);
}

static guint
goa_provider_get_credentials_generation_real (GoaProvider *provider)
{
  return 0;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
ensure_ep_and_builtins (void)
{
  static gsize once_init_value = 0;

  if (g_once_init_enter (&once_init_value))
    {
      GIOExtensionPoint *extension_point;
      static volatile GType type = 0;

      extension_point = g_io_extension_point_register (GOA_PROVIDER_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (extension_point, GOA_TYPE_PROVIDER);

      /* The order is in which the providers' types are created is
       * important because it affects the order in which they are
       * returned by goa_provider_get_all.
       */
#ifdef GOA_GOOGLE_ENABLED
      type = GOA_TYPE_GOOGLE_PROVIDER;
#endif
#ifdef GOA_FACEBOOK_ENABLED
      type = GOA_TYPE_FACEBOOK_PROVIDER;
#endif
#ifdef GOA_WINDOWS_LIVE_ENABLED
      type = GOA_TYPE_WINDOWS_LIVE_PROVIDER;
#endif
#ifdef GOA_EXCHANGE_ENABLED
      type = GOA_TYPE_EXCHANGE_PROVIDER;
#endif
#ifdef GOA_KERBEROS_ENABLED
      type = GOA_TYPE_KERBEROS_PROVIDER;
#endif

      type = type; /* silence -Wunused-but-set-variable */

      g_once_init_leave (&once_init_value, 1);
    }
}


/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_get_for_provider_type:
 * @provider_type: A provider type.
 *
 * Looks up the %GOA_PROVIDER_EXTENSION_POINT_NAME extension
 * point and returns a newly created #GoaProvider for
 * @provider_type, if any.
 *
 * Returns: (transfer full): A #GoaProvider (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaProvider *
goa_provider_get_for_provider_type (const gchar *provider_type)
{
  GIOExtension *extension;
  GIOExtensionPoint *extension_point;
  GoaProvider *ret;

  ensure_ep_and_builtins ();

  ret = NULL;

  extension_point = g_io_extension_point_lookup (GOA_PROVIDER_EXTENSION_POINT_NAME);
  extension = g_io_extension_point_get_extension_by_name (extension_point, provider_type);
  if (extension != NULL)
    ret = GOA_PROVIDER (g_object_new (g_io_extension_get_type (extension), NULL));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_get_all:
 *
 * Looks up the %GOA_PROVIDER_EXTENSION_POINT_NAME extension
 * point and returns a newly created #GoaProvider for each
 * provider type encountered.
 *
 * Returns: (transfer full) (element-type GoaProvider): A list
 *   of element providers that should be freed with g_list_free()
 *   after each element has been freed with g_object_unref().
 */
GList *
goa_provider_get_all (void)
{
  GList *ret;
  GList *extensions;
  GList *l;
  GIOExtensionPoint *extension_point;

  ensure_ep_and_builtins ();

  ret = NULL;
  extension_point = g_io_extension_point_lookup (GOA_PROVIDER_EXTENSION_POINT_NAME);
  extensions = g_io_extension_point_get_extensions (extension_point);
  /* TODO: what if there are two extensions with the same name? */
  for (l = extensions; l != NULL; l = l->next)
    {
      GIOExtension *extension = l->data;
      ret = g_list_prepend (ret, g_object_new (g_io_extension_get_type (extension), NULL));
    }
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * SECTION:goautil
 * @title: Utilities
 * @short_description: Various utility routines
 *
 * Various utility routines.
 */

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_util_add_row_widget:
 * @left: A #GtkGrid for the left side.
 * @right: A #GtkGrid for the right side.
 * @label_text: (allow-none): The text to insert on the left side or %NULL for no label.
 * @widget: A widget to insert on the right side.
 *
 * Utility function to add @label_text and @widget to @table.
 *
 * Returns: (transfer none): The #GtkWidget that was inserted (e.g. @widget itself).
 */
GtkWidget *
goa_util_add_row_widget (GtkGrid      *left,
                         GtkGrid      *right,
                         const gchar  *label_text,
                         GtkWidget    *widget)
{
  GtkWidget *label;

  g_return_val_if_fail (GTK_IS_GRID (left), NULL);
  g_return_val_if_fail (GTK_IS_GRID (right), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  if (label_text != NULL)
    {
      GtkStyleContext *context;

      label = gtk_label_new (label_text);
      context = gtk_widget_get_style_context (label);
      gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
      gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
      gtk_container_add (GTK_CONTAINER (left), label);
    }

  gtk_container_add (GTK_CONTAINER (right), widget);
  return widget;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
goa_util_lookup_keyfile_string (GoaObject    *object,
                                const gchar  *key)
{
  GoaAccount *account;
  GError *error;
  GKeyFile *key_file;
  gchar *path;
  gchar *group;
  gchar *ret;

  ret = NULL;

  account = goa_object_peek_account (object);
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      goa_warning ("Error loading keyfile %s: %s (%s, %d)",
                   path,
                   error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }
  ret = g_key_file_get_string (key_file,
                               group,
                               key,
                               &error);
  if (ret == NULL)
    {
      /* this is not fatal (think upgrade-path) */
      goa_debug ("Error getting value for key %s in group `%s' from keyfile %s: %s (%s, %d)",
                 key,
                 group,
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (group);
  g_free (path);
  return ret;
}

gboolean
goa_util_lookup_keyfile_boolean (GoaObject    *object,
                                 const gchar  *key)
{
  GoaAccount *account;
  GError *error;
  GKeyFile *key_file;
  gchar *path;
  gchar *group;
  gboolean ret;

  ret = FALSE;

  account = goa_object_peek_account (object);
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      goa_warning ("Error loading keyfile %s: %s (%s, %d)",
                   path,
                   error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }
  ret = g_key_file_get_boolean (key_file,
                                group,
                                key,
                                &error);
  if (error != NULL)
    {
      /* this is not fatal (think upgrade-path) */
      goa_debug ("Error getting boolean value for key %s in group `%s' from keyfile %s: %s (%s, %d)",
                 key,
                 group,
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_free (group);
  g_free (path);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_util_account_notify_property_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GoaAccount *account;
  gboolean value;
  const gchar *key;
  const gchar *name;

  g_return_if_fail (GOA_IS_ACCOUNT (object));

  account = GOA_ACCOUNT (object);
  key = user_data;
  name = g_param_spec_get_name (pspec);

  g_object_get (account, name, &value, NULL);
  goa_utils_keyfile_set_boolean (account, key, !value);
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_util_add_account_info (GtkGrid *left, GtkGrid *right, GoaObject *object)
{
  GIcon *icon;
  GoaAccount *account;
  GtkWidget *image;
  GtkWidget *label;
  const gchar *icon_str;
  const gchar *identity;
  const gchar *name;
  gchar *markup;

  account = goa_object_peek_account (object);

  icon_str = goa_account_get_provider_icon (account);
  icon = g_icon_new_for_string (icon_str, NULL);
  image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
  g_object_unref (icon);
  gtk_misc_set_alignment (GTK_MISC (image), 1.0, 0.5);
  gtk_widget_set_margin_bottom (image, 12);
  gtk_container_add (GTK_CONTAINER (left), image);

  name = goa_account_get_provider_name (account);
  identity = goa_account_get_presentation_identity (account);
  markup = g_strdup_printf ("<b>%s</b>\n%s",
                            name,
                            (identity == NULL || identity[0] == '\0') ? "\xe2\x80\x94" : identity);
  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 24);
  gtk_label_set_width_chars (GTK_LABEL (label), 24);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_widget_set_margin_bottom (label, 12);
  g_free (markup);
  gtk_container_add (GTK_CONTAINER (right), label);

  return;
}

/* ---------------------------------------------------------------------------------------------------- */

GtkWidget *
goa_util_add_row_switch_from_keyfile_with_blurb (GtkGrid      *left,
                                                 GtkGrid      *right,
                                                 GoaObject    *object,
                                                 const gchar  *label_text,
                                                 const gchar  *property,
                                                 const gchar  *blurb)
{
  GoaAccount *account;
  GtkWidget *hbox;
  GtkWidget *switch_;
  gboolean value;

  account = goa_object_peek_account (object);
  g_object_get (account, property, &value, NULL);
  switch_ = gtk_switch_new ();
  gtk_switch_set_active (GTK_SWITCH (switch_), !value);

  g_object_bind_property (switch_, "active",
                          account, property,
                          G_BINDING_BIDIRECTIONAL | G_BINDING_INVERT_BOOLEAN);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_set_homogeneous (GTK_BOX (hbox), FALSE);

  if (blurb != NULL)
    {
      GtkWidget *label;

      label = gtk_label_new_with_mnemonic (blurb);
      gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
      gtk_label_set_mnemonic_widget (GTK_LABEL (label), switch_);
      gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
    }

  gtk_box_pack_end (GTK_BOX (hbox), switch_, FALSE, FALSE, 0);
  goa_util_add_row_widget (left, right, label_text, hbox);
  return switch_;
}
