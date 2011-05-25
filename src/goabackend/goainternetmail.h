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

#if !defined (__GOA_BACKEND_INSIDE_GOA_BACKEND_H__) && !defined (GOA_BACKEND_COMPILATION)
#error "Only <goabackend/goabackend.h> can be included directly."
#endif

#ifndef __GOA_INTERNET_MAIL_H__
#define __GOA_INTERNET_MAIL_H__

#include <goabackend/goabackendtypes.h>

G_BEGIN_DECLS

#define GOA_TYPE_INTERNET_MAIL  (goa_internet_mail_get_type ())
#define GOA_INTERNET_MAIL(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_INTERNET_MAIL, GoaInternetMail))
#define GOA_IS_INTERNET_MAIL(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_INTERNET_MAIL))

GType    goa_internet_mail_get_type (void) G_GNUC_CONST;
GoaMail *goa_internet_mail_new      (const gchar  *imap_host,
                                     const gchar  *imap_user_name,
                                     gboolean      imap_use_tls,
                                     gboolean      imap_ignore_bad_tls,
                                     GoaImapAuth  *imap_auth,
                                     const gchar  *smtp_host,
                                     const gchar  *smtp_user_name,
                                     gboolean      smtp_use_tls,
                                     gboolean      smtp_ignore_bad_tls);


G_END_DECLS

#endif /* __GOA_INTERNET_MAIL_H__ */
