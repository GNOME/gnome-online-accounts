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

#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#include "goaidentitymanager.h"
#include "goaidentitymanagerprivate.h"

enum
{
  IDENTITY_ADDED,
  IDENTITY_REMOVED,
  IDENTITY_RENAMED,
  IDENTITY_REFRESHED,
  IDENTITY_NEEDS_RENEWAL,
  IDENTITY_EXPIRING,
  IDENTITY_EXPIRED,
  NUMBER_OF_SIGNALS,
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

G_DEFINE_INTERFACE (GoaIdentityManager, goa_identity_manager, G_TYPE_OBJECT);

static void
goa_identity_manager_default_init (GoaIdentityManagerInterface *interface)
{
  signals[IDENTITY_ADDED] = g_signal_new ("identity-added",
                                          G_TYPE_FROM_INTERFACE (interface),
                                          G_SIGNAL_RUN_LAST,
                                          G_STRUCT_OFFSET
                                          (GoaIdentityManagerInterface,
                                           identity_added), NULL, NULL, NULL,
                                          G_TYPE_NONE, 1, GOA_TYPE_IDENTITY);
  signals[IDENTITY_REMOVED] = g_signal_new ("identity-removed",
                                            G_TYPE_FROM_INTERFACE (interface),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                             identity_removed),
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_TYPE_NONE,
                                            1,
                                            GOA_TYPE_IDENTITY);
  signals[IDENTITY_REFRESHED] = g_signal_new ("identity-refreshed",
                                              G_TYPE_FROM_INTERFACE (interface),
                                              G_SIGNAL_RUN_LAST,
                                              G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                               identity_refreshed),
                                              NULL,
                                              NULL,
                                              NULL,
                                              G_TYPE_NONE,
                                              1,
                                              GOA_TYPE_IDENTITY);
  signals[IDENTITY_RENAMED] = g_signal_new ("identity-renamed",
                                            G_TYPE_FROM_INTERFACE (interface),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                             identity_renamed),
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_TYPE_NONE,
                                            1,
                                            GOA_TYPE_IDENTITY);
  signals[IDENTITY_NEEDS_RENEWAL] = g_signal_new ("identity-needs-renewal",
                                                  G_TYPE_FROM_INTERFACE (interface),
                                                  G_SIGNAL_RUN_LAST,
                                                  G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                                   identity_needs_renewal),
                                                  NULL,
                                                  NULL,
                                                  NULL,
                                                  G_TYPE_NONE,
                                                  1,
                                                  GOA_TYPE_IDENTITY);
  signals[IDENTITY_EXPIRING] = g_signal_new ("identity-expiring",
                                             G_TYPE_FROM_INTERFACE (interface),
                                             G_SIGNAL_RUN_LAST,
                                             G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                              identity_expiring),
                                             NULL,
                                             NULL,
                                             NULL,
                                             G_TYPE_NONE,
                                             1,
                                             GOA_TYPE_IDENTITY);
  signals[IDENTITY_EXPIRED] = g_signal_new ("identity-expired",
                                            G_TYPE_FROM_INTERFACE (interface),
                                            G_SIGNAL_RUN_LAST,
                                            G_STRUCT_OFFSET (GoaIdentityManagerInterface,
                                                             identity_expired),
                                            NULL,
                                            NULL,
                                            NULL,
                                            G_TYPE_NONE,
                                            1,
                                            GOA_TYPE_IDENTITY);
}

void
goa_identity_manager_get_identity (GoaIdentityManager  *self,
                                   const char          *identifier,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->get_identity (self,
                                                       identifier,
                                                       cancellable,
                                                       callback,
                                                       user_data);
}

GoaIdentity *
goa_identity_manager_get_identity_finish (GoaIdentityManager  *self,
                                          GAsyncResult        *result,
                                          GError             **error)
{
  return GOA_IDENTITY_MANAGER_GET_IFACE (self)->get_identity_finish (self,
                                                                     result,
                                                                     error);
}

void
goa_identity_manager_list_identities (GoaIdentityManager  *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->list_identities (self,
                                                          cancellable,
                                                          callback,
                                                          user_data);
}

GList *
goa_identity_manager_list_identities_finish (GoaIdentityManager  *self,
                                             GAsyncResult        *result,
                                             GError             **error)
{
  return GOA_IDENTITY_MANAGER_GET_IFACE (self)->list_identities_finish (self,
                                                                        result,
                                                                        error);
}

void
goa_identity_manager_renew_identity (GoaIdentityManager  *self,
                                     GoaIdentity         *identity,
                                     GCancellable        *cancellable,
                                     GAsyncReadyCallback  callback,
                                     gpointer             user_data)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->renew_identity (self,
                                                         identity,
                                                         cancellable,
                                                         callback,
                                                         user_data);
}

void
goa_identity_manager_renew_identity_finish (GoaIdentityManager  *self,
                                            GAsyncResult        *result,
                                            GError             **error)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->renew_identity_finish (self, result, error);
}

void
goa_identity_manager_sign_identity_in (GoaIdentityManager     *self,
                                       const char             *identifier,
                                       gconstpointer           initial_password,
                                       const char             *preauth_source,
                                       GoaIdentitySignInFlags  flags,
                                       GoaIdentityInquiryFunc  inquiry_func,
                                       gpointer                inquiry_data,
                                       GCancellable           *cancellable,
                                       GAsyncReadyCallback     callback,
                                       gpointer                user_data)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->sign_identity_in (self,
                                                           identifier,
                                                           initial_password,
                                                           preauth_source,
                                                           flags,
                                                           inquiry_func,
                                                           inquiry_data,
                                                           cancellable,
                                                           callback,
                                                           user_data);
}

GoaIdentity *
goa_identity_manager_sign_identity_in_finish (GoaIdentityManager  *self,
                                              GAsyncResult        *result,
                                              GError             **error)
{
  return GOA_IDENTITY_MANAGER_GET_IFACE (self)->sign_identity_in_finish (self,
                                                                         result,
                                                                         error);
}

void
goa_identity_manager_sign_identity_out (GoaIdentityManager  *self,
                                        GoaIdentity         *identity,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->sign_identity_out (self,
                                                            identity,
                                                            cancellable,
                                                            callback,
                                                            user_data);
}

void
goa_identity_manager_sign_identity_out_finish (GoaIdentityManager  *self,
                                               GAsyncResult        *result,
                                               GError             **error)
{
  GOA_IDENTITY_MANAGER_GET_IFACE (self)->sign_identity_out_finish (self,
                                                                   result,
                                                                   error);
}

char *
goa_identity_manager_name_identity (GoaIdentityManager *self,
                                    GoaIdentity *identity)
{
  return GOA_IDENTITY_MANAGER_GET_IFACE (self)->name_identity (self, identity);
}

void
_goa_identity_manager_emit_identity_added (GoaIdentityManager *self,
                                           GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_ADDED], 0, identity);
}

void
_goa_identity_manager_emit_identity_removed (GoaIdentityManager *self,
                                             GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_REMOVED], 0, identity);
}

void
_goa_identity_manager_emit_identity_renamed (GoaIdentityManager *self,
                                             GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_RENAMED], 0, identity);
}

void
_goa_identity_manager_emit_identity_refreshed (GoaIdentityManager *self,
                                               GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_REFRESHED], 0, identity);
}

void
_goa_identity_manager_emit_identity_needs_renewal (GoaIdentityManager *self,
                                                   GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_NEEDS_RENEWAL], 0, identity);
}

void
_goa_identity_manager_emit_identity_expiring (GoaIdentityManager *self,
                                              GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_EXPIRING], 0, identity);
}

void
_goa_identity_manager_emit_identity_expired (GoaIdentityManager *self,
                                             GoaIdentity        *identity)
{
  g_signal_emit (G_OBJECT (self), signals[IDENTITY_EXPIRED], 0, identity);
}
