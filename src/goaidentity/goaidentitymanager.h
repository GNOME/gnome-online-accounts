/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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

#ifndef __GOA_IDENTITY_MANAGER_H__
#define __GOA_IDENTITY_MANAGER_H__

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "goaidentity.h"
#include "goaidentityinquiry.h"

G_BEGIN_DECLS
#define GOA_TYPE_IDENTITY_MANAGER             (goa_identity_manager_get_type ())
#define GOA_IDENTITY_MANAGER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GOA_TYPE_IDENTITY_MANAGER, GoaIdentityManager))
#define GOA_IDENTITY_MANAGER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GOA_TYPE_IDENTITY_MANAGER, GoaIdentityManagerInterface))
#define GOA_IS_IDENTITY_MANAGER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GOA_TYPE_IDENTITY_MANAGER))
#define GOA_IDENTITY_MANAGER_GET_IFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE((obj), GOA_TYPE_IDENTITY_MANAGER, GoaIdentityManagerInterface))
#define GOA_IDENTITY_MANAGER_ERROR            (goa_identity_manager_error_quark ())
typedef struct _GoaIdentityManager GoaIdentityManager;
typedef struct _GoaIdentityManagerInterface GoaIdentityManagerInterface;

struct _GoaIdentityManagerInterface
{
  GTypeInterface base_interface;

  /* Signals */
  void (* identity_added) (GoaIdentityManager *identity_manager,
                           GoaIdentity        *identity);

  void (* identity_removed) (GoaIdentityManager *identity_manager,
                             GoaIdentity        *identity);
  void (* identity_renamed) (GoaIdentityManager *identity_manager,
                             GoaIdentity        *identity);
  void (* identity_refreshed) (GoaIdentityManager *identity_manager,
                               GoaIdentity        *identity);
  void (* identity_needs_renewal) (GoaIdentityManager *identity_manager,
                                   GoaIdentity        *identity);
  void (* identity_expiring) (GoaIdentityManager *identity_manager,
                              GoaIdentity        *identity);
  void (* identity_expired) (GoaIdentityManager *identity_manager,
                             GoaIdentity        *identity);

  /* Virtual Functions */
  void (* get_identity) (GoaIdentityManager *identity_manager,
                         const char         *identifier,
                         GCancellable       *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer            user_data);
  GoaIdentity * (* get_identity_finish) (GoaIdentityManager  *identity_manager,
                                         GAsyncResult        *result,
                                         GError             **error);
  void (* list_identities) (GoaIdentityManager  *identity_manager,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data);
  GList * (* list_identities_finish) (GoaIdentityManager  *identity_manager,
                                      GAsyncResult        *result,
                                      GError             **error);

  void (* sign_identity_in) (GoaIdentityManager     *identity_manager,
                             const char             *identifier,
                             gconstpointer           initial_password,
                             const char             *preauth_source,
                             GoaIdentitySignInFlags  flags,
                             GoaIdentityInquiryFunc  inquiry_func,
                             gpointer                inquiry_data,
                             GCancellable           *cancellable,
                             GAsyncReadyCallback     callback,
                             gpointer                user_data);
  GoaIdentity * (* sign_identity_in_finish) (GoaIdentityManager  *identity_manager,
                                             GAsyncResult        *result,
                                             GError             **error);

  void (* sign_identity_out) (GoaIdentityManager  *identity_manager,
                              GoaIdentity         *identity,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data);
  void (* sign_identity_out_finish) (GoaIdentityManager  *identity_manager,
                                     GAsyncResult        *result,
                                     GError             **error);

  void (* renew_identity) (GoaIdentityManager *identity_manager,
                           GoaIdentity        *identity,
                           GCancellable       *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer            user_data);
  void (* renew_identity_finish) (GoaIdentityManager  *identity_manager,
                                  GAsyncResult        *result,
                                  GError             **error);

  char * (* name_identity) (GoaIdentityManager *identity_manager,
                            GoaIdentity        *identity);
};

typedef enum
{
  GOA_IDENTITY_MANAGER_ERROR_INITIALIZING,
  GOA_IDENTITY_MANAGER_ERROR_IDENTITY_NOT_FOUND,
  GOA_IDENTITY_MANAGER_ERROR_CREATING_IDENTITY,
  GOA_IDENTITY_MANAGER_ERROR_ACCESSING_CREDENTIALS,
  GOA_IDENTITY_MANAGER_ERROR_UNSUPPORTED_CREDENTIALS
} GoaIdentityManagerError;

GType  goa_identity_manager_get_type    (void);
GQuark goa_identity_manager_error_quark (void);

void goa_identity_manager_get_identity (GoaIdentityManager  *identity_manager,
                                        const char          *identifier,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data);
GoaIdentity *goa_identity_manager_get_identity_finish (GoaIdentityManager  *identity_manager,
                                                       GAsyncResult        *result,
                                                       GError             **error);
void goa_identity_manager_list_identities (GoaIdentityManager *identity_manager,
                                           GCancellable       *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer            user_data);
GList *goa_identity_manager_list_identities_finish (GoaIdentityManager  *identity_manager,
                                                    GAsyncResult        *result,
                                                    GError             **error);

void goa_identity_manager_sign_identity_in (GoaIdentityManager     *identity_manager,
                                            const char             *identifier,
                                            gconstpointer           initial_password,
                                            const char             *preauth_source,
                                            GoaIdentitySignInFlags  flags,
                                            GoaIdentityInquiryFunc  inquiry_func,
                                            gpointer                inquiry_data,
                                            GCancellable           *cancellable,
                                            GAsyncReadyCallback     callback,
                                            gpointer                user_data);
GoaIdentity *goa_identity_manager_sign_identity_in_finish (GoaIdentityManager  *identity_manager,
                                                           GAsyncResult        *result,
                                                           GError             **error);

void goa_identity_manager_sign_identity_out (GoaIdentityManager *identity_manager,
                                             GoaIdentity        *identity,
                                             GCancellable       *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer            user_data);
void goa_identity_manager_sign_identity_out_finish (GoaIdentityManager  *identity_manager,
                                                    GAsyncResult        *result,
                                                    GError             **error);

void goa_identity_manager_renew_identity (GoaIdentityManager  *identity_manager,
                                          GoaIdentity         *identity,
                                          GCancellable        *cancellable,
                                          GAsyncReadyCallback  callback,
                                          gpointer             user_data);
void goa_identity_manager_renew_identity_finish (GoaIdentityManager  *identity_manager,
                                                 GAsyncResult        *result,
                                                 GError             **error);

char *goa_identity_manager_name_identity (GoaIdentityManager *identity_manager,
                                          GoaIdentity        *identity);

G_END_DECLS
#endif /* __GOA_IDENTITY_MANAGER_H__ */
