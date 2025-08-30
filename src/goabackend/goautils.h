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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_UTILS_H__
#define __GOA_UTILS_H__

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "goaprovider.h"

G_BEGIN_DECLS

typedef gpointer (*GoaPeekInterfaceFunc)   (GoaObject *);

gboolean         goa_utils_check_duplicate (GoaClient              *client,
                                            const gchar            *identity,
                                            const gchar            *presentation_identity,
                                            const gchar            *provider_type,
                                            GoaPeekInterfaceFunc    func,
                                            GError                **error);

gint             goa_utils_convert_abs_usec_to_duration_sec (gint64 abs_usec);

gint64           goa_utils_convert_duration_sec_to_abs_usec (gint duration_sec);

gchar           *goa_utils_data_input_stream_read_line (GDataInputStream  *stream,
                                                        gsize             *length,
                                                        GCancellable      *cancellable,
                                                        GError           **error);

gboolean         goa_utils_delete_credentials_for_account_sync (GoaProvider    *provider,
                                                                GoaAccount     *account,
                                                                GCancellable   *cancellable,
                                                                GError        **error);

gboolean         goa_utils_delete_credentials_for_id_sync (GoaProvider    *provider,
                                                           const gchar    *id,
                                                           GCancellable   *cancellable,
                                                           GError        **error);

GVariant        *goa_utils_lookup_credentials_sync (GoaProvider    *provider,
                                                    GoaObject      *object,
                                                    GCancellable   *cancellable,
                                                    GError        **error);

gboolean         goa_utils_store_credentials_for_id_sync (GoaProvider    *provider,
                                                          const gchar    *id,
                                                          GVariant       *credentials,
                                                          GCancellable   *cancellable,
                                                          GError        **error);

gboolean         goa_utils_store_credentials_for_object_sync (GoaProvider    *provider,
                                                              GoaObject      *object,
                                                              GVariant       *credentials,
                                                              GCancellable   *cancellable,
                                                              GError        **error);

gboolean         goa_utils_keyfile_copy_group (GKeyFile     *src_key_file,
                                               const gchar  *src_group_name,
                                               GKeyFile     *dest_key_file,
                                               const gchar  *dest_group_name);

gboolean         goa_utils_keyfile_get_boolean (GKeyFile *key_file, const gchar *group_name, const gchar *key);

void             goa_utils_keyfile_remove_key (GoaAccount *account, const gchar *key);

void             goa_utils_keyfile_set_boolean (GoaAccount *account, const gchar *key, gboolean value);

void             goa_utils_keyfile_set_string (GoaAccount *account, const gchar *key, const gchar *value);

gboolean         goa_utils_parse_email_address (const gchar *email, gchar **out_username, gchar **out_domain);

char            *goa_utils_normalize_url (const char  *base_uri,
                                          const char  *uri_ref,
                                          char       **server);

void             goa_utils_set_error_soup (GError **err, SoupMessage *msg);

void             goa_utils_set_error_ssl (GError **err, GTlsCertificateFlags flags);

gboolean         goa_utils_get_credentials (GoaProvider    *provider,
                                            GoaObject      *object,
                                            const gchar    *id,
                                            gchar         **username,
                                            gchar         **password,
                                            GCancellable   *cancellable,
                                            GError        **error);

gchar           *goa_utils_base64_url_encode (const guchar *data,
                                              gsize len);
gchar           *goa_utils_generate_code_verifier (void);
gchar           *goa_utils_generate_code_challenge (const gchar *code_verifier);

G_END_DECLS

#endif /* __GOA_UTILS_H__ */
