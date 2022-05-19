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

#include "goakerberosidentityinquiry.h"
#include "goaidentityinquiryprivate.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

struct _GoaKerberosIdentityInquiry
{
  GObject parent;
  GoaIdentity *identity;
  char *name;
  char *banner;
  GList *queries;
  int number_of_queries;
  int number_of_unanswered_queries;
  gboolean is_failed;
};

typedef struct
{
  GoaIdentityInquiry *inquiry;
  krb5_prompt *kerberos_prompt;
  gboolean is_answered;
} GoaKerberosIdentityQuery;

static void identity_inquiry_interface_init (GoaIdentityInquiryInterface *
                                             interface);
static void initable_interface_init (GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (GoaKerberosIdentityInquiry,
                         goa_kerberos_identity_inquiry,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_interface_init)
                         G_IMPLEMENT_INTERFACE (GOA_TYPE_IDENTITY_INQUIRY,
                                                identity_inquiry_interface_init));

static gboolean
goa_kerberos_identity_inquiry_initable_init (GInitable * initable,
                                             GCancellable *cancellable,
                                             GError ** error)
{
  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
      return FALSE;
    }

  return TRUE;
}

static void
initable_interface_init (GInitableIface *interface)
{
  interface->init = goa_kerberos_identity_inquiry_initable_init;
}

static GoaKerberosIdentityQuery *
goa_kerberos_identity_query_new (GoaIdentityInquiry * inquiry,
                                 krb5_prompt * kerberos_prompt)
{
  GoaKerberosIdentityQuery *query;

  query = g_slice_new (GoaKerberosIdentityQuery);
  query->inquiry = inquiry;
  query->kerberos_prompt = kerberos_prompt;
  query->is_answered = FALSE;

  return query;
}

static void
goa_kerberos_identity_query_free (GoaKerberosIdentityQuery *query)
{
  g_slice_free (GoaKerberosIdentityQuery, query);
}

static void
goa_kerberos_identity_inquiry_dispose (GObject *object)
{
  GoaKerberosIdentityInquiry *self = GOA_KERBEROS_IDENTITY_INQUIRY (object);

  g_clear_object (&self->identity);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->banner, g_free);

  G_OBJECT_CLASS (goa_kerberos_identity_inquiry_parent_class)->dispose (object);
}

static void
goa_kerberos_identity_inquiry_finalize (GObject *object)
{
  GoaKerberosIdentityInquiry *self = GOA_KERBEROS_IDENTITY_INQUIRY (object);

  g_list_free_full (self->queries, (GDestroyNotify) goa_kerberos_identity_query_free);

  G_OBJECT_CLASS (goa_kerberos_identity_inquiry_parent_class)->finalize (object);
}

static void
goa_kerberos_identity_inquiry_class_init (GoaKerberosIdentityInquiryClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = goa_kerberos_identity_inquiry_dispose;
  object_class->finalize = goa_kerberos_identity_inquiry_finalize;
}

static void
goa_kerberos_identity_inquiry_init (GoaKerberosIdentityInquiry *self)
{
}

GoaIdentityInquiry *
goa_kerberos_identity_inquiry_new (GoaKerberosIdentity * identity,
                                   const char *name,
                                   const char *banner,
                                   krb5_prompt prompts[], int number_of_prompts)
{
  GObject *object;
  GoaIdentityInquiry *inquiry;
  GoaKerberosIdentityInquiry *self;
  GError *error;
  int i;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY (identity), NULL);
  g_return_val_if_fail (number_of_prompts > 0, NULL);

  object = g_object_new (GOA_TYPE_KERBEROS_IDENTITY_INQUIRY, NULL);

  inquiry = GOA_IDENTITY_INQUIRY (object);
  self = GOA_KERBEROS_IDENTITY_INQUIRY (object);

  /* FIXME: make these construct properties */
  self->identity = g_object_ref (GOA_IDENTITY (identity));
  self->name = g_strdup (name);
  self->banner = g_strdup (banner);

  self->number_of_queries = 0;
  for (i = 0; i < number_of_prompts; i++)
    {
      GoaKerberosIdentityQuery *query;

      query = goa_kerberos_identity_query_new (inquiry, &prompts[i]);

      self->queries = g_list_prepend (self->queries, query);
      self->number_of_queries++;
    }
  self->queries = g_list_reverse (self->queries);

  self->number_of_unanswered_queries = self->number_of_queries;

  error = NULL;
  if (!g_initable_init (G_INITABLE (self), NULL, &error))
    {
      g_debug ("%s", error->message);
      g_error_free (error);
      g_object_unref (self);
      return NULL;
    }

  return inquiry;
}

static GoaIdentity *
goa_kerberos_identity_inquiry_get_identity (GoaIdentityInquiry *inquiry)
{
  GoaKerberosIdentityInquiry *self;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry), NULL);

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  return self->identity;
}

static char *
goa_kerberos_identity_inquiry_get_name (GoaIdentityInquiry *inquiry)
{
  GoaKerberosIdentityInquiry *self;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry), NULL);

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  return g_strdup (self->name);
}

static char *
goa_kerberos_identity_inquiry_get_banner (GoaIdentityInquiry *inquiry)
{
  GoaKerberosIdentityInquiry *self;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry), NULL);

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  return g_strdup (self->banner);
}

static gboolean
goa_kerberos_identity_inquiry_is_complete (GoaIdentityInquiry *inquiry)
{
  GoaKerberosIdentityInquiry *self;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry), FALSE);

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  return self->number_of_unanswered_queries == 0 || self->is_failed;
}

static gboolean
goa_kerberos_identity_inquiry_is_failed (GoaIdentityInquiry *inquiry)
{
  GoaKerberosIdentityInquiry *self;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry), FALSE);

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  return self->is_failed;
}

static void
goa_kerberos_identity_inquiry_mark_query_answered (GoaKerberosIdentityInquiry * self,
                                                   GoaKerberosIdentityQuery * query)
{
  if (query->is_answered)
    {
      return;
    }

  query->is_answered = TRUE;
  self->number_of_unanswered_queries--;

  if (self->number_of_unanswered_queries == 0)
    {
      _goa_identity_inquiry_emit_complete (GOA_IDENTITY_INQUIRY (self));
    }
}

static void
goa_kerberos_identity_inquiry_fail (GoaKerberosIdentityInquiry *self)
{
  self->is_failed = TRUE;
  _goa_identity_inquiry_emit_complete (GOA_IDENTITY_INQUIRY (self));
}

static void
goa_kerberos_identity_inquiry_answer_query (GoaIdentityInquiry * inquiry,
                                            GoaIdentityQuery *query,
                                            const char *answer)
{
  GoaKerberosIdentityInquiry *self;
  GoaKerberosIdentityQuery *kerberos_query = (GoaKerberosIdentityQuery *) query;
  size_t answer_length;

  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry));
  g_return_if_fail (inquiry == kerberos_query->inquiry);
  g_return_if_fail (!goa_kerberos_identity_inquiry_is_complete (inquiry));

  self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);

  answer_length = strlen (answer);

  if (kerberos_query->kerberos_prompt->reply->length < answer_length)
    {
      goa_kerberos_identity_inquiry_fail (self);
    }
  else
    {
      strncpy (kerberos_query->kerberos_prompt->reply->data,
               answer, kerberos_query->kerberos_prompt->reply->length);
      kerberos_query->kerberos_prompt->reply->length = (unsigned int) answer_length;

      goa_kerberos_identity_inquiry_mark_query_answered (self, kerberos_query);
    }
}

static void
goa_kerberos_identity_inquiry_iter_init (GoaIdentityInquiryIter * iter,
                                         GoaIdentityInquiry * inquiry)
{
  GoaKerberosIdentityInquiry *self = GOA_KERBEROS_IDENTITY_INQUIRY (inquiry);
  iter->data = self->queries;
}

static GoaIdentityQuery *
goa_kerberos_identity_inquiry_iter_next (GoaIdentityInquiryIter * iter,
                                         GoaIdentityInquiry * inquiry)
{
  GoaIdentityQuery *query;
  GList *node;

  node = iter->data;

  if (node == NULL)
    {
      return NULL;
    }

  query = (GoaIdentityQuery *) node->data;

  node = node->next;

  iter->data = node;

  return query;
}

static GoaIdentityQueryMode
goa_kerberos_identity_query_get_mode (GoaIdentityInquiry * inquiry,
                                      GoaIdentityQuery * query)
{
  GoaKerberosIdentityQuery *kerberos_query = (GoaKerberosIdentityQuery *) query;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry),
                        GOA_IDENTITY_QUERY_MODE_INVISIBLE);
  g_return_val_if_fail (inquiry == kerberos_query->inquiry,
                        GOA_IDENTITY_QUERY_MODE_INVISIBLE);

  if (kerberos_query->kerberos_prompt->hidden)
    {
      return GOA_IDENTITY_QUERY_MODE_INVISIBLE;
    }
  else
    {
      return GOA_IDENTITY_QUERY_MODE_VISIBLE;
    }
}

static char *
goa_kerberos_identity_query_get_prompt (GoaIdentityInquiry * inquiry,
                                        GoaIdentityQuery * query)
{
  GoaKerberosIdentityQuery *kerberos_query = (GoaKerberosIdentityQuery *) query;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry),
                        GOA_IDENTITY_QUERY_MODE_INVISIBLE);
  g_return_val_if_fail (inquiry == kerberos_query->inquiry, NULL);

  return g_strdup (kerberos_query->kerberos_prompt->prompt);
}

static gboolean
goa_kerberos_identity_query_is_answered (GoaIdentityInquiry * inquiry,
                                         GoaIdentityQuery * query)
{
  GoaKerberosIdentityQuery *kerberos_query = (GoaKerberosIdentityQuery *) query;

  g_return_val_if_fail (GOA_IS_KERBEROS_IDENTITY_INQUIRY (inquiry),
                        GOA_IDENTITY_QUERY_MODE_INVISIBLE);
  g_return_val_if_fail (inquiry == kerberos_query->inquiry, FALSE);

  return kerberos_query->is_answered;
}

static void
identity_inquiry_interface_init (GoaIdentityInquiryInterface *interface)
{
  interface->get_identity = goa_kerberos_identity_inquiry_get_identity;
  interface->get_name = goa_kerberos_identity_inquiry_get_name;
  interface->get_banner = goa_kerberos_identity_inquiry_get_banner;
  interface->is_complete = goa_kerberos_identity_inquiry_is_complete;
  interface->is_failed = goa_kerberos_identity_inquiry_is_failed;
  interface->answer_query = goa_kerberos_identity_inquiry_answer_query;
  interface->iter_init = goa_kerberos_identity_inquiry_iter_init;
  interface->iter_next = goa_kerberos_identity_inquiry_iter_next;
  interface->get_mode = goa_kerberos_identity_query_get_mode;
  interface->get_prompt = goa_kerberos_identity_query_get_prompt;
  interface->is_answered = goa_kerberos_identity_query_is_answered;
}
