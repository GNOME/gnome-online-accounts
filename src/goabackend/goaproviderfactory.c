/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
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
 * Public License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "goaproviderfactory.h"

/**
 * SECTION:goaproviderfactory
 * @title: GoaProviderFactory
 * @short_description: Abstract base class for provider factories
 *
 * #GoaProviderFactory implementations are used to dynamically create #GoaProvider instances.
 */

G_DEFINE_ABSTRACT_TYPE (GoaProviderFactory, goa_provider_factory, G_TYPE_OBJECT);

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_factory_get_provider:
 * @factory: A #GoaProviderFactory.
 * @provider_name: A provider type identifier (ie. IM protocol names in #GoaTelepathyFactory)
 *
 * Create a dynamic #GoaProvider for the subclass-specific @provider_name.
 *
 * Returns: (transfer full): A #GoaProvider (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaProvider *
goa_provider_factory_get_provider (GoaProviderFactory  *factory,
                                   const gchar         *provider_name)
{
  GoaProviderFactoryClass *klass;

  g_return_val_if_fail (GOA_IS_PROVIDER_FACTORY (factory), NULL);
  g_return_val_if_fail (provider_name != NULL, NULL);

  klass = GOA_PROVIDER_FACTORY_GET_CLASS (factory);
  g_return_val_if_fail (klass->get_provider != NULL, NULL);

  return klass->get_provider (factory, provider_name);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_factory_get_providers:
 * @factory: A #GoaProviderFactory.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Get asynchronously a list of #GoaProvider instances handled by @factory.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this function was called from. You can then call
 * goa_provider_factory_get_providers_finish() to get the result
 * of the operation.
 *
 * This is a virtual method that must be implemented by subclasses.
 */
void
goa_provider_factory_get_providers (GoaProviderFactory  *factory,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
  GoaProviderFactoryClass *klass;

  g_return_if_fail (GOA_IS_PROVIDER_FACTORY (factory));

  klass = GOA_PROVIDER_FACTORY_GET_CLASS (factory);
  g_return_if_fail (klass->get_providers != NULL);

  return klass->get_providers (factory, callback, user_data);
}

/**
 * goa_provider_factory_get_providers_finish:
 * @factory: A #GoaProviderFactory.
 * @out_providers: (out): Return location for a list of #GoaProvider instances handled by @factory.
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_provider_factory_get_providers().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_provider_factory_get_providers()
 *
 * This is a virtual method that subclasses may implement. The default implementation is suitable for
 * #GSimpleAsyncResult an implementation of goa_provider_factory_get_providers() using #GSimpleAsyncResult.
 *
 * Returns: %TRUE if the list was successfully retrieved, %FALSE if @error is set.
 */
gboolean
goa_provider_factory_get_providers_finish (GoaProviderFactory  *factory,
                                           GList              **out_providers,
                                           GAsyncResult        *result,
                                           GError             **error)
{
  GoaProviderFactoryClass *klass;

  g_return_val_if_fail (GOA_IS_PROVIDER_FACTORY (factory), FALSE);

  klass = GOA_PROVIDER_FACTORY_GET_CLASS (factory);
  return klass->get_providers_finish (factory, out_providers, result, error);
}

static gboolean
get_providers_finish_default (GoaProviderFactory  *factory,
                              GList              **out_providers,
                              GAsyncResult        *result,
                              GError             **error)
{

  GSimpleAsyncResult *simple = (GSimpleAsyncResult *) result;
  GList *providers;

  g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (factory), NULL),
      FALSE);

  if (g_simple_async_result_propagate_error (simple, error))
    return FALSE;

  if (out_providers != NULL)
    {
      providers = g_simple_async_result_get_op_res_gpointer (simple);
      *out_providers = g_list_copy_deep (providers, (GCopyFunc) g_object_ref, NULL);
    }

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_provider_factory_init (GoaProviderFactory *provider)
{
}

static void
goa_provider_factory_class_init (GoaProviderFactoryClass *klass)
{
  klass->get_providers_finish = get_providers_finish_default;
}
