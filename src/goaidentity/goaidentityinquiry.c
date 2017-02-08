/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#include "goaidentityinquiry.h"
#include "goaidentityinquiryprivate.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

enum
{
  COMPLETE,
  NUMBER_OF_SIGNALS,
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

G_DEFINE_INTERFACE (GoaIdentityInquiry, goa_identity_inquiry, G_TYPE_OBJECT);

static void
goa_identity_inquiry_default_init (GoaIdentityInquiryInterface *interface)
{
  signals[COMPLETE] = g_signal_new ("complete",
                                    G_TYPE_FROM_INTERFACE (interface),
                                    G_SIGNAL_RUN_LAST,
                                    0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

void
_goa_identity_inquiry_emit_complete (GoaIdentityInquiry *self)
{
  g_signal_emit (G_OBJECT (self), signals[COMPLETE], 0);
}

char *
goa_identity_inquiry_get_name (GoaIdentityInquiry *self)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), NULL);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->get_name (self);
}

char *
goa_identity_inquiry_get_banner (GoaIdentityInquiry *self)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), NULL);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->get_banner (self);
}

gboolean
goa_identity_inquiry_is_complete (GoaIdentityInquiry *self)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), TRUE);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->is_complete (self);
}

gboolean
goa_identity_inquiry_is_failed (GoaIdentityInquiry *self)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), TRUE);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->is_failed (self);
}

void
goa_identity_inquiry_iter_init (GoaIdentityInquiryIter *iter,
                                GoaIdentityInquiry     *inquiry)
{
  g_return_if_fail (GOA_IS_IDENTITY_INQUIRY (inquiry));

  GOA_IDENTITY_INQUIRY_GET_IFACE (inquiry)->iter_init (iter, inquiry);
}

GoaIdentityQuery *
goa_identity_inquiry_iter_next (GoaIdentityInquiryIter *iter,
                                GoaIdentityInquiry     *inquiry)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (inquiry), NULL);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (inquiry)->iter_next (iter, inquiry);
}

GoaIdentity *
goa_identity_inquiry_get_identity (GoaIdentityInquiry *self)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), NULL);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->get_identity (self);
}

GoaIdentityQueryMode
goa_identity_query_get_mode (GoaIdentityInquiry *self,
                             GoaIdentityQuery   *query)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self),
                        GOA_IDENTITY_QUERY_MODE_INVISIBLE);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->get_mode (self, query);
}

char *
goa_identity_query_get_prompt (GoaIdentityInquiry *self,
                               GoaIdentityQuery   *query)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), NULL);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->get_prompt (self, query);
}

void
goa_identity_inquiry_answer_query (GoaIdentityInquiry *self,
                                   GoaIdentityQuery   *query,
                                   const char         *answer)
{
  g_return_if_fail (GOA_IS_IDENTITY_INQUIRY (self));

  GOA_IDENTITY_INQUIRY_GET_IFACE (self)->answer_query (self, query, answer);
}

gboolean
goa_identity_query_is_answered (GoaIdentityInquiry *self,
                                GoaIdentityQuery   *query)
{
  g_return_val_if_fail (GOA_IS_IDENTITY_INQUIRY (self), FALSE);

  return GOA_IDENTITY_INQUIRY_GET_IFACE (self)->is_answered (self, query);
}
