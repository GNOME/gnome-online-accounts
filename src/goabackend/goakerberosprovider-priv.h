/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2023 GNOME Foundation Inc.
 * Contributor: Andy Holmes <andyholmes@gnome.org>
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

#ifndef __GOA_KERBEROS_PROVIDER_PRIV_H__
#define __GOA_KERBEROS_PROVIDER_PRIV_H__

#include <gio/gio.h>
#include <goabackend/goaprovider-priv.h>
#include <goabackend/goakerberosprovider.h>
#include <gcr/gcr.h>

G_BEGIN_DECLS

/**
 * GoaKerberosProviderClass:
 * @parent_class: The parent class.
 *
 * Class structure for #GoaKerberosProvider.
 */
struct _GoaKerberosProviderClass
{
  GoaProviderClass parent_class;
};

void                 goa_kerberos_provider_get_ticket        (GoaKerberosProvider  *self,
                                                              GoaObject            *object,
                                                              gboolean              is_interactive,
                                                              GCancellable         *cancellable,
                                                              GAsyncReadyCallback   callback,
                                                              gpointer              user_data);
gboolean             goa_kerberos_provider_get_ticket_finish (GoaKerberosProvider  *self,
                                                              GAsyncResult         *result,
                                                              GError              **error);
gboolean             goa_kerberos_provider_get_ticket_sync   (GoaKerberosProvider  *self,
                                                              GoaObject            *object,
                                                              gboolean              is_interactive,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
void                 goa_kerberos_provider_sign_in           (GoaKerberosProvider  *self,
                                                              const char           *identifier,
                                                              const char           *password,
                                                              const char           *preauth_source,
                                                              GCancellable         *cancellable,
                                                              GAsyncReadyCallback   callback,
                                                              gpointer              user_data);
char                *goa_kerberos_provider_sign_in_finish    (GoaKerberosProvider  *self,
                                                              GAsyncResult         *result,
                                                              GError              **error);
char                *goa_kerberos_provider_sign_in_sync      (GoaKerberosProvider  *self,
                                                              const char           *identifier,
                                                              const char           *password,
                                                              const char           *preauth_source,
                                                              GCancellable         *cancellable,
                                                              GError              **error);
void                 goa_kerberos_provider_prompt_password        (GoaKerberosProvider  *self,
                                                                   GCancellable         *cancellable,
                                                                   GAsyncReadyCallback   callback,
                                                                   gpointer              user_data);
GcrSecretExchange   *goa_kerberos_provider_prompt_password_finish (GoaKerberosProvider  *self,
                                                                   gboolean             *remember_password_out,
                                                                   GAsyncResult         *result,
                                                                   GError              **error);
GcrSecretExchange   *goa_kerberos_provider_prompt_password_sync   (GoaKerberosProvider  *self,
                                                                   gboolean             *remember_password_out,
                                                                   GCancellable         *cancellable,
                                                                   GError              **error);

G_END_DECLS

#endif /* __GOA_KERBEROS_PROVIDER_PRIV_H__ */
