/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2013 Intel Corporation
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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_PROVIDER_FACTORY_H__
#define __GOA_PROVIDER_FACTORY_H__

#include <gio/gio.h>

#include "goaprovider.h"

G_BEGIN_DECLS

#define GOA_TYPE_PROVIDER_FACTORY         (goa_provider_factory_get_type ())
#define GOA_PROVIDER_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_PROVIDER_FACTORY, GoaProviderFactory))
#define GOA_PROVIDER_FACTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_PROVIDER_FACTORY, GoaProviderFactoryClass))
#define GOA_PROVIDER_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_PROVIDER_FACTORY, GoaProviderFactoryClass))
#define GOA_IS_PROVIDER_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_PROVIDER_FACTORY))
#define GOA_IS_PROVIDER_FACTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_PROVIDER_FACTORY))

typedef struct _GoaProviderFactory GoaProviderFactory;
typedef struct _GoaProviderFactoryClass GoaProviderFactoryClass;

/*
 * GoaProviderFactory:
 *
 * The #GoaProviderFactory structure contains only private data and should
 * only be accessed using the provided API.
 */
struct _GoaProviderFactory
{
  /*< private >*/
  GObject parent_instance;
};

/*
 * GoaProviderFactoryClass:
 * @parent_class: The parent class.
 * @get_provider: Virtual function for goa_provider_factory_get_provider().
 * @get_providers: Virtual function for goa_provider_factory_get_providers().
 * @get_providers_finish: Virtual function for goa_provider_factory_get_providers_finish().
 *
 * Class structure for #GoaProviderFactory.
 */
struct _GoaProviderFactoryClass
{
  GObjectClass parent_class;

  /* Mandatory to implement. */
  GoaProvider *(*get_provider)              (GoaProviderFactory    *factory,
                                             const gchar           *provider_name);

  /* The async method is mandatory to implement, but _finish has a default
   * implementation suitable for a GTask. */
  void         (*get_providers)             (GoaProviderFactory    *factory,
                                             GAsyncReadyCallback    callback,
                                             gpointer               user_data);
  gboolean     (*get_providers_finish)      (GoaProviderFactory    *factory,
                                             GList                **out_providers,
                                             GAsyncResult          *result,
                                             GError               **error);
};

GType        goa_provider_factory_get_type                  (void) G_GNUC_CONST;

GoaProvider *goa_provider_factory_get_provider              (GoaProviderFactory     *factory,
                                                             const gchar            *provider_name);

void         goa_provider_factory_get_providers             (GoaProviderFactory     *factory,
                                                             GAsyncReadyCallback     callback,
                                                             gpointer                user_data);
gboolean     goa_provider_factory_get_providers_finish      (GoaProviderFactory     *factory,
                                                             GList                 **out_providers,
                                                             GAsyncResult           *result,
                                                             GError                **error);

G_END_DECLS

#endif /* __GOA_PROVIDER_FACTORY_H__ */
