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

#ifndef __GOA_TELEPATHY_FACTORY_H__
#define __GOA_TELEPATHY_FACTORY_H__

#include "goaproviderfactory.h"

G_BEGIN_DECLS

#define GOA_TYPE_TELEPATHY_FACTORY         (goa_telepathy_factory_get_type ())
#define GOA_TELEPATHY_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_TELEPATHY_FACTORY, GoaTelepathyFactory))
#define GOA_TELEPATHY_FACTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), GOA_TYPE_TELEPATHY_FACTORY, GoaTelepathyFactoryClass))
#define GOA_TELEPATHY_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GOA_TYPE_TELEPATHY_FACTORY, GoaTelepathyFactoryClass))
#define GOA_IS_TELEPATHY_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_TELEPATHY_FACTORY))
#define GOA_IS_TELEPATHY_FACTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GOA_TYPE_TELEPATHY_FACTORY))

typedef struct _GoaTelepathyFactory GoaTelepathyFactory;
typedef struct _GoaTelepathyFactoryClass GoaTelepathyFactoryClass;

struct _GoaTelepathyFactory
{
  /*< private >*/
  GoaProviderFactory parent_instance;
};

struct _GoaTelepathyFactoryClass
{
  GoaProviderFactoryClass parent_class;
};

GType        goa_telepathy_factory_get_type                 (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GOA_TELEPATHY_FACTORY_H__ */
