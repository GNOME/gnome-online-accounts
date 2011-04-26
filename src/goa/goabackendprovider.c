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

#include "goabackendprovider.h"
#include "goabackendgoogleprovider.h"

/**
 * SECTION:goabackendprovider
 * @title: GoaBackendProvider
 * @short_description: Abstract base class for providers
 *
 * #GoaBackendProvider is the base type that all providers implement.
 */

G_DEFINE_ABSTRACT_TYPE (GoaBackendProvider, goa_backend_provider, G_TYPE_OBJECT);

static void
goa_backend_provider_init (GoaBackendProvider *client)
{
}

static void
goa_backend_provider_class_init (GoaBackendProviderClass *klass)
{
}

/**
 * goa_backend_provider_get_provider_type:
 * @provider: A #GoaBackendProvider.
 *
 * Gets the type of @provider.
 *
 * Returns: (transfer none): A string owned by @provider, do not free.
 */
const gchar *
goa_backend_provider_get_provider_type (GoaBackendProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), NULL);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_provider_type (provider);
}

/**
 * goa_backend_provider_get_name:
 * @provider: A #GoaBackendProvider.
 *
 * Gets a localized name for @provider that is suitable for display in
 * an user interface.
 *
 * Returns: (transfer none): A string owned by @provider, do not free.
 */
const gchar *
goa_backend_provider_get_name (GoaBackendProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), NULL);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_name (provider);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_add_account:
 * @provider: A #GoaBackendProvider.
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
 * account couldn't be created then @error is set.
 *
 * The caller will always show an error dialog if @error is set unless
 * the error is %GOA_ERROR_DIALOG_DISMISSED.
 *
 * Implementations should run the <link
 * linkend="g_main_context_default">default main loop</link> while
 * interacting with the user and may do so using e.g. gtk_dialog_run()
 * on @dialog.
 *
 * Returns: The #GoaObject for the created account (must be relased
 *   with g_object_unref()) or %NULL if @error is set.
 */
GoaObject *
goa_backend_provider_add_account (GoaBackendProvider *provider,
                                  GoaClient          *client,
                                  GtkDialog          *dialog,
                                  GtkBox             *vbox,
                                  GError            **error)
{
  GoaObject *ret;

  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), NULL);
  g_return_val_if_fail (GOA_IS_CLIENT (client), NULL);
  g_return_val_if_fail (GTK_IS_DIALOG (dialog), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  ret = GOA_BACKEND_PROVIDER_GET_CLASS (provider)->add_account (provider, client, dialog, vbox, error);

  g_warn_if_fail ((ret == NULL && (error == NULL || *error != NULL)) || GOA_IS_OBJECT (ret));

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_refresh_account:
 * @provider: A #GoaBackendProvider.
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
 * Returns: %TRUE if the account has been refreshed, %FALSE if @error
 * is set.
 */
gboolean
goa_backend_provider_refresh_account (GoaBackendProvider  *provider,
                                      GoaClient           *client,
                                      GoaObject           *object,
                                      GtkWindow           *parent,
                                      GError             **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL, FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->refresh_account (provider, client, object, parent, error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_build_object:
 * @provider: A #GoaBackendProvider.
 * @object: The #GoaObjectSkeleton that is being built.
 * @key_file: The #GKeyFile with configuation data.
 * @group: The group in @key_file to get data from.
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
 * Returns: %TRUE if data was valid, %FALSE if @error is set.
 */
gboolean
goa_backend_provider_build_object (GoaBackendProvider  *provider,
                                   GoaObjectSkeleton   *object,
                                   GKeyFile            *key_file,
                                   const gchar         *group,
                                   GError             **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT_SKELETON (object) && goa_object_peek_account (GOA_OBJECT (object)) != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->build_object (provider, object, key_file, group, error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_get_access_token_supported:
 * @provider: A #GoaBackendProvider.
 *
 * Function to check if @provider is access token based.
 *
 * Returns: %TRUE if access token based, %FALSE otherwise.
 */
gboolean
goa_backend_provider_get_access_token_supported (GoaBackendProvider *provider)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), FALSE);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_access_token_supported (provider);
}

/**
 * goa_backend_provider_get_access_token:
 * @provider: A #GoaBackendProvider.
 * @object: The #GoaObject to get an access token for.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Starts obtaining an access token for the account identified by @object.
 *
 * When the access token is ready, @callback is invoked in the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this function was called from. You can then call
 * goa_backend_provider_get_access_token_finish() to get the result of
 * the operation.
 */
void
goa_backend_provider_get_access_token (GoaBackendProvider   *provider,
                                       GoaObject            *object,
                                       GCancellable         *cancellable,
                                       GAsyncReadyCallback   callback,
                                       gpointer              user_data)
{
  g_return_if_fail (GOA_IS_BACKEND_PROVIDER (provider));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
  GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_access_token (provider, object, cancellable, callback, user_data);
}

/**
 * goa_backend_provider_get_access_token_finish:
 * @provider: A #GoaBackendProvider.
 * @out_expires_in: (out): Return location for expiration time (in seconds), or %NULL to ignore.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_backend_provider_get_access_token().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_backend_provider_get_access_token().
 *
 * If this fails with the %GOA_ERROR_NOT_AUTHORIZED error, then the
 * accounts #org.gnome.OnlineAccounts.Account:NeedsAttention property
 * will be set to %TRUE so the user can service the account.
 *
 * Returns: (transfer full): The access token or %NULL if @error is set.
 */
gchar *
goa_backend_provider_get_access_token_finish (GoaBackendProvider   *provider,
                                              gint                 *out_expires_in,
                                              GAsyncResult         *res,
                                              GError              **error)
{
  g_return_val_if_fail (GOA_IS_BACKEND_PROVIDER (provider), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  return GOA_BACKEND_PROVIDER_GET_CLASS (provider)->get_access_token_finish (provider, out_expires_in, res, error);
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

      extension_point = g_io_extension_point_register (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (extension_point, GOA_TYPE_BACKEND_PROVIDER);

      type = GOA_TYPE_BACKEND_GOOGLE_PROVIDER;
      type = type; /* for -Wunused-but-set-variable */

      g_once_init_leave (&once_init_value, 1);
    }
}


/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_get_for_provider_type:
 * @provider_type: A provider type.
 *
 * Looks up the %GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME extension
 * point and returns a newly created #GoaBackendProvider for
 * @provider_type, if any.
 *
 * Returns: (transfer full): A #GoaBackendProvider (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaBackendProvider *
goa_backend_provider_get_for_provider_type (const gchar *provider_type)
{
  GIOExtension *extension;
  GIOExtensionPoint *extension_point;
  GoaBackendProvider *ret;

  ensure_ep_and_builtins ();

  ret = NULL;

  extension_point = g_io_extension_point_lookup (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME);
  extension = g_io_extension_point_get_extension_by_name (extension_point, provider_type);
  if (extension != NULL)
    ret = GOA_BACKEND_PROVIDER (g_object_new (g_io_extension_get_type (extension), NULL));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_backend_provider_get_all:
 *
 * Looks up the %GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME extension
 * point and returns a newly created #GoaBackendProvider for each
 * provider type encountered.
 *
 * Returns: (transfer full) (element-type GoaBackendProvider): A list
 *   of element providers that should be freed with g_list_free()
 *   after each element has been freed with g_object_unref().
 */
GList *
goa_backend_provider_get_all (void)
{
  GList *ret;
  GList *extensions;
  GList *l;
  GIOExtensionPoint *extension_point;

  ensure_ep_and_builtins ();

  ret = NULL;
  extension_point = g_io_extension_point_lookup (GOA_BACKEND_PROVIDER_EXTENSION_POINT_NAME);
  extensions = g_io_extension_point_get_extensions (extension_point);
  /* TODO: what if there are two extensions with the same name? */
  for (l = extensions; l != NULL; l = l->next)
    {
      GIOExtension *extension = l->data;
      ret = g_list_prepend (ret, g_object_new (g_io_extension_get_type (extension), NULL));
    }
  ret = g_list_reverse (ret);
  return ret;
}

