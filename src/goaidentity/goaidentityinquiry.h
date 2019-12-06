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

#ifndef __GOA_IDENTITY_INQUIRY_H__
#define __GOA_IDENTITY_INQUIRY_H__

#include <stdint.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "goaidentity.h"

G_BEGIN_DECLS

#define GOA_TYPE_IDENTITY_INQUIRY (goa_identity_inquiry_get_type ())
G_DECLARE_INTERFACE (GoaIdentityInquiry, goa_identity_inquiry, GOA, IDENTITY_INQUIRY, GObject);

typedef struct _GoaIdentityInquiryIter GoaIdentityInquiryIter;

typedef struct _GoaIdentityQuery GoaIdentityQuery;

typedef void (* GoaIdentityInquiryFunc) (GoaIdentityInquiry *inquiry,
                                         GCancellable       *cancellable,
                                         gpointer            user_data);

typedef enum
{
  GOA_IDENTITY_QUERY_MODE_INVISIBLE,
  GOA_IDENTITY_QUERY_MODE_VISIBLE
} GoaIdentityQueryMode;

struct _GoaIdentityInquiryIter
{
  gpointer data;
};

struct _GoaIdentityInquiryInterface
{
  GTypeInterface base_interface;

  GoaIdentity * (* get_identity) (GoaIdentityInquiry *inquiry);
  char *        (* get_name)     (GoaIdentityInquiry *inquiry);
  char *        (* get_banner)   (GoaIdentityInquiry *inquiry);

  gboolean      (* is_complete)  (GoaIdentityInquiry *inquiry);
  void          (* answer_query) (GoaIdentityInquiry *inquiry,
                                  GoaIdentityQuery   *query,
                                  const char         *answer);

  void          (* iter_init)    (GoaIdentityInquiryIter *iter,
                                  GoaIdentityInquiry     *inquiry);

  GoaIdentityQuery *   (* iter_next) (GoaIdentityInquiryIter *iter,
                                      GoaIdentityInquiry     *inquiry);

  GoaIdentityQueryMode (* get_mode) (GoaIdentityInquiry *inquiry,
                                     GoaIdentityQuery   *query);
  char *               (* get_prompt) (GoaIdentityInquiry *inquiry,
                                       GoaIdentityQuery   *query);
  gboolean             (* is_answered) (GoaIdentityInquiry *inquiry,
                                        GoaIdentityQuery   *query);
  gboolean             (* is_failed)  (GoaIdentityInquiry *inquiry);
};

GoaIdentity *goa_identity_inquiry_get_identity (GoaIdentityInquiry *inquiry);
char        *goa_identity_inquiry_get_name     (GoaIdentityInquiry *inquiry);
char        *goa_identity_inquiry_get_banner   (GoaIdentityInquiry *inquiry);
gboolean     goa_identity_inquiry_is_complete  (GoaIdentityInquiry *inquiry);
gboolean     goa_identity_inquiry_is_failed    (GoaIdentityInquiry *inquiry);
void         goa_identity_inquiry_answer_query (GoaIdentityInquiry *inquiry,
                                                GoaIdentityQuery   *query,
                                                const char         *answer);

void              goa_identity_inquiry_iter_init (GoaIdentityInquiryIter *iter,
                                                  GoaIdentityInquiry     *inquiry);
GoaIdentityQuery *goa_identity_inquiry_iter_next (GoaIdentityInquiryIter *iter,
                                                  GoaIdentityInquiry     *inquiry);

GoaIdentityQueryMode goa_identity_query_get_mode   (GoaIdentityInquiry *inquiry,
                                                    GoaIdentityQuery   *query);
char *               goa_identity_query_get_prompt (GoaIdentityInquiry *inquiry,
                                                    GoaIdentityQuery   *query);
gboolean             goa_identity_query_is_answered (GoaIdentityInquiry *inquiry,
                                                     GoaIdentityQuery *query);

#endif /* __GOA_IDENTITY_INQUIRY_H__ */
