/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2017 Red Hat, Inc.
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

#include "goarestproxy.h"
#include "goasouplogger.h"

struct _GoaRestProxy
{
  RestProxy parent_instance;
};

G_DEFINE_TYPE (GoaRestProxy, goa_rest_proxy, REST_TYPE_PROXY);

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_rest_proxy_constructed (GObject *object)
{
  GoaRestProxy *self = GOA_REST_PROXY (object);
  SoupLogger *logger = NULL;

  G_OBJECT_CLASS (goa_rest_proxy_parent_class)->constructed (object);

  logger = goa_soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  rest_proxy_add_soup_feature (REST_PROXY (self), SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);
}

static void
goa_rest_proxy_init (GoaRestProxy *self)
{
}

static void
goa_rest_proxy_class_init (GoaRestProxyClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->constructed = goa_rest_proxy_constructed;
}

/* ---------------------------------------------------------------------------------------------------- */

RestProxy *
goa_rest_proxy_new (const gchar  *url_format,
                    gboolean      binding_required)
{
  return g_object_new (GOA_TYPE_REST_PROXY, "url-format", url_format, "binding-required", binding_required, NULL);
}
