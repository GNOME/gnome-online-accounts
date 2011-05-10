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

#include "config.h"
#include <glib/gi18n.h>
#include <stdlib.h>

#include "goa/goa.h"

static gchar *
fix_up_excerpt (const gchar *s,
                gsize        max_len)
{
  GString *str;
  guint n;

  str = g_string_new (NULL);
  for (n = 0; s[n] != '\0'; n++)
    {
      gint c = s[n];
      if (c == '\r')
        g_string_append_c (str, ' ');
      else if (c == '\n')
        g_string_append_c (str, ' ');
      else
        g_string_append_c (str, c);
      if (str->len > max_len)
        break;
    }
  if (s[n] != '\0')
    g_string_append (str, "...");

  return g_string_free (str, FALSE);
}

static void
print_result (GoaMailQuery *query)
{
  GVariantIter iter;
  GVariant *result;
  const gchar *guid;
  gint64 date;
  const gchar *from;
  const gchar *subject;
  const gchar *excerpt;
  gint flags;
  guint n;

  result = goa_mail_query_get_result (query);
  if (result != NULL)
    {
      g_print ("Mailbox: NumMessages=%d NumUnread=%d Connected=%d\n"
               "Query:   NumResults=%d MaxSize=%d\n"
               "===========================================\n",
               goa_mail_query_get_num_messages (query),
               goa_mail_query_get_num_unread (query),
               goa_mail_query_get_connected (query),
               (gint) g_variant_n_children (result),
               goa_mail_query_get_max_size (query));
      if (g_variant_n_children (result) == 0)
        g_print ("\n");

      n = 0;
      g_variant_iter_init (&iter, result);
      while (g_variant_iter_next (&iter,
                                  "(st&s&s&si@a{sv})",
                                  &guid, &date, &from, &subject, &excerpt, &flags, NULL))
        {
          gchar *excerpt_fixed_up;
          excerpt_fixed_up = fix_up_excerpt (excerpt, 50);
          g_print ("Message %d: guid %s, flags %d\n"
                   " Date:    %" G_GINT64_FORMAT "\n"
                   " From:    %s\n"
                   " Subject: %s\n"
                   " Excerpt: %s\n"
                   "\n",
                   n, guid, flags,
                   date,
                   from,
                   subject,
                   excerpt_fixed_up);
          g_free (excerpt_fixed_up);
          n++;
        }
    }
  else
    {
      g_print ("result is NULL so proxy must be stale\n"
               "=======================================\n"
               "\n");
    }
}

static void
on_notify (GObject    *object,
           GParamSpec *pspec,
           gpointer    user_data)
{
  GoaMailQuery *query = GOA_MAIL_QUERY (object);
  print_result (query);
}

int
main (int   argc,
      char *argv[])
{
  gint ret;
  GMainLoop *loop;
  GError *error;
  GoaClient *client;
  GList *accounts;
  GList *l;
  GoaMailQuery *query;
  gchar *opt_account;
  gchar *opt_query;
  gint opt_size;
  GOptionEntry opt_entries[] =
    {
      { "account", 'a', 0, G_OPTION_ARG_STRING, &opt_account, "The account to do a query for", NULL },
      { "query", 'q', 0, G_OPTION_ARG_STRING, &opt_query, "The query to perform (blank if not set)", NULL },
      { "size", 's', 0, G_OPTION_ARG_INT, &opt_size, "The size of the query (5 if not set)", NULL },
      { NULL}
    };
  GOptionContext *opt_context;
  gchar *query_object_path;

  ret = 1;
  client = NULL;
  accounts = NULL;
  query = NULL;
  opt_account = NULL;
  opt_query = NULL;
  opt_size = 5;
  query_object_path = NULL;

  g_type_init ();

  opt_context = g_option_context_new ("goa mail query example");
  error = NULL;
  g_option_context_add_main_entries (opt_context, opt_entries, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &error))
    {
      g_printerr ("Error parsing options: %s\n", error->message);
      g_error_free (error);
      goto out;
    }
  if (opt_account == NULL)
    {
      g_printerr ("Incorrect usage, try --help.\n");
      goto out;
    }

  if (opt_query == NULL)
    opt_query = g_strdup("");

  loop = g_main_loop_new (NULL, FALSE);

  error = NULL;
  client = goa_client_new_sync (NULL, /* GCancellable */
                                &error);
  if (client == NULL)
    {
      g_printerr ("Error creating a GOA client: %s (%s, %d)\n",
                  error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

  accounts = goa_client_get_accounts (client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GoaObject *object = GOA_OBJECT (l->data);
      GoaAccount *account;
      GoaMail *mail;

      account = goa_object_peek_account (object);
      if (account == NULL)
        continue;

      if (!(g_strcmp0 (goa_account_get_id (account), opt_account) == 0 ||
            g_strcmp0 (g_dbus_object_get_object_path (G_DBUS_OBJECT (object)), opt_account) == 0))
        continue;

      mail = goa_object_peek_mail (object);
      if (mail == NULL)
        {
          g_printerr ("Given account does not implement the Mail interface\n");
          goto out;
        }

      /* Start querying account */
      g_print ("Querying mail for %s with query string \"%s\"\n",
               goa_account_get_id (account),
               opt_query);
      error = NULL;
      if (!goa_mail_call_create_query_sync (mail,
                                            opt_query, /* query string */
                                            opt_size,  /* return no more than N messages */
                                            &query_object_path,
                                            NULL, /* GCancellable */
                                            &error))
        {
          g_printerr ("Error creating mail query: %s (%s, %d)\n",
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          goto out;
        }

      error = NULL;
      query = goa_mail_query_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                     G_DBUS_PROXY_FLAGS_NONE,
                                                     "org.gnome.OnlineAccounts",
                                                     query_object_path,
                                                     NULL, /* GCancellable */
                                                     &error);
      if (query == NULL)
        {
          g_printerr ("Error creating query proxy: %s (%s, %d)\n",
                      error->message, g_quark_to_string (error->domain), error->code);
          g_error_free (error);
          goto out;
        }

      print_result (query);
      g_signal_connect (query,
                        "notify",
                        G_CALLBACK (on_notify),
                        NULL);
      break;
    }

  if (query_object_path == NULL)
    {
      g_printerr ("Didn't find requested account.\n");
      goto out;
    }

  g_main_loop_run (loop);

  ret = 0;

 out:
  if (query != NULL)
    g_object_unref (query);
  if (client != NULL)
    g_object_unref (client);
  g_free (query_object_path);
  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
  g_free (opt_account);
  g_free (opt_query);
  g_option_context_free (opt_context);
  return ret;
}
