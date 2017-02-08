/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2010 – 2013 Collabora Ltd.
 * Copyright © 2013 Intel Corporation
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

/* This class makes sure we have a GOA account for each Telepathy account
 * configured in the system.
 * Note that this handles only plain Telepathy accounts; the ones with
 * multiple capabilities (e.g. Facebook) are handled differently. */

#include "config.h"

#include <gio/gio.h>
#include <telepathy-glib/telepathy-glib.h>

#include "goatpaccountlinker.h"
#include "goabackend/goautils.h"

#define GOA_TP_ACCOUNT_LINKER_GET_PRIVATE(obj) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOA_TYPE_TP_ACCOUNT_LINKER, \
                                GoaTpAccountLinkerPrivate))

G_DEFINE_TYPE (GoaTpAccountLinker, goa_tp_account_linker, G_TYPE_OBJECT)

struct _GoaTpAccountLinkerPrivate
{
  TpAccountManager *account_manager;
  GoaClient *goa_client;

  GHashTable *tp_accounts; /* owned gchar *id -> reffed TpAccount * */
  GHashTable *goa_accounts; /* owned gchar *id -> reffed GoaObject * */

  GQueue *remove_tp_account_queue;
};

/* The path of the Telepathy account is used as common identifier between
 * GOA and Telepathy */
static const gchar *
get_id_from_tp_account (TpAccount *tp_account)
{
  return tp_proxy_get_object_path (tp_account);
}

static const gchar *
get_id_from_goa_account (GoaAccount *goa_account)
{
  return goa_account_get_identity (goa_account);
}

static gboolean
is_telepathy_account (GoaAccount *goa_account)
{
  const gchar *type = goa_account_get_provider_type (goa_account);
  return g_str_has_prefix (type, "telepathy/");
}

static void
tp_account_removed_by_us_cb (GObject      *object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  TpAccount *tp_account = TP_ACCOUNT (object);
  GError *error = NULL;
  GTask *task = G_TASK (user_data);

  if (!tp_account_remove_finish (tp_account, res, &error))
    {
      g_critical ("Error removing Telepathy account %s: %s (%s, %d)",
          get_id_from_tp_account (tp_account),
          error->message,
          g_quark_to_string (error->domain),
          error->code);
      g_task_return_error (task, error);
      goto out;
    }

  g_task_return_boolean (task, TRUE);

 out:
  g_object_unref (task);
}

static void
remove_tp_account_queue_check (GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GList *l;

  if (priv->goa_client == NULL ||
      priv->account_manager == NULL ||
      !tp_proxy_is_prepared (priv->account_manager, TP_ACCOUNT_MANAGER_FEATURE_CORE))
    {
      /* Not everything is ready yet */
      return;
    }

  if (priv->remove_tp_account_queue->length == 0)
    return;

  for (l = priv->remove_tp_account_queue->head; l != NULL; l = l->next)
    {
      GTask *task = G_TASK (l->data);
      GoaAccount *goa_account;
      GoaObject *goa_object;
      TpAccount *tp_account;
      const gchar *id;

      goa_object = GOA_OBJECT (g_task_get_task_data (task));
      goa_account = goa_object_peek_account (goa_object);

      id = get_id_from_goa_account (goa_account);
      if (!g_hash_table_remove (priv->goa_accounts, id))
        {
          /* 1 - The user removes the Telepathy account (but not the GOA one)
           * 2 - We delete the corresponding GOA account and remove it
           *     from priv->goa_accounts
           * 3 - The Telepathy provider again tries to remove the
           *     corresponding Telepathy account
           */
          g_debug ("Ignoring removal of GOA account we asked to remove "
                   "(%s, Telepathy object path: %s)",
                   goa_account_get_id (goa_account),
                   id);
          g_task_return_boolean (task, TRUE);
          continue;
        }

      g_info ("GOA account %s for Telepathy account %s removed, "
              "removing Telepathy account",
              goa_account_get_id (goa_account), id);

      tp_account = g_hash_table_lookup (priv->tp_accounts, id);
      if (tp_account == NULL)
        {
          g_critical ("There is no Telepathy account for removed GOA "
                      "account %s (Telepathy object path: %s)",
                      goa_account_get_id (goa_account), id);
          g_task_return_boolean (task, TRUE);
          continue;
        }
      tp_account_remove_async (tp_account, tp_account_removed_by_us_cb, g_object_ref (task));
      g_hash_table_remove (priv->tp_accounts, id);
    }

  g_queue_foreach (priv->remove_tp_account_queue, (GFunc) g_object_unref, NULL);
  g_queue_clear (priv->remove_tp_account_queue);
}

static void
goa_account_chat_disabled_changed_cb (GoaAccount         *goa_account,
                                      GParamSpec         *spec,
                                      GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  const gchar *id;
  TpAccount *tp_account;
  gboolean tp_enabled;
  gboolean goa_enabled;

  id = get_id_from_goa_account (goa_account);
  tp_account = g_hash_table_lookup (priv->tp_accounts, id);
  if (tp_account == NULL)
    return;

  goa_enabled = !goa_account_get_chat_disabled (goa_account);
  tp_enabled = tp_account_is_enabled (tp_account);
  if (tp_enabled != goa_enabled)
    {
      g_info ("The GOA account %s (Telepathy object path: %s) has been %s, "
          "propagating to Telepathy",
          goa_account_get_id (goa_account), id,
          goa_enabled ? "enabled" : "disabled");
      tp_account_set_enabled_async (tp_account, goa_enabled, NULL, NULL);
    }
}

static void
tp_account_chat_enabled_changed_cb (TpAccount          *tp_account,
                                    GParamSpec         *spec,
                                    GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  const gchar *id;
  GoaObject *goa_object;
  GoaAccount *goa_account;
  gboolean tp_enabled;
  gboolean goa_enabled;

  id = get_id_from_tp_account (tp_account);
  goa_object = g_hash_table_lookup (priv->goa_accounts, id);
  if (goa_object == NULL)
    return;

  goa_account = goa_object_peek_account (goa_object);
  goa_enabled = !goa_account_get_chat_disabled (goa_account);
  tp_enabled = tp_account_is_enabled (tp_account);
  if (tp_enabled != goa_enabled)
    {
      g_info ("The Telepathy account %s has been %s, propagating to GOA",
          id, tp_enabled ? "enabled" : "disabled");
      /* When we set this property, the autogenerated code emits a notify
       * signal immediately even if the property hasn't changed, so
       * goa_account_chat_disabled_changed_cb() thinks that the property
       * changed back to the old value and a cycle starts.
       * The right notify signal will be emitted later when the property is
       * actually changed. */
      g_signal_handlers_block_by_func (goa_account,
          goa_account_chat_disabled_changed_cb, self);
      goa_account_set_chat_disabled (goa_account, !tp_enabled);
      g_signal_handlers_unblock_by_func (goa_account,
          goa_account_chat_disabled_changed_cb, self);
    }
}

static void
goa_account_created_cb (GoaManager   *manager,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  TpAccount *tp_account = user_data;
  gchar *goa_account_object_path = NULL;
  GError *error = NULL;

  if (!goa_manager_call_add_account_finish (manager,
        &goa_account_object_path, res, &error))
    {
      g_critical ("Failed to create a GOA account for %s: %s (%s, %d)",
          get_id_from_tp_account (tp_account),
          error->message,
          g_quark_to_string (error->domain),
          error->code);
      g_error_free (error);
      goto out;
    }

  g_info ("Created new %s GOA account for Telepathy account %s",
      goa_account_object_path, get_id_from_tp_account (tp_account));

 out:
  g_object_unref (tp_account);
}

static void
create_goa_account (GoaTpAccountLinker *self,
                    TpAccount          *tp_account)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GVariantBuilder credentials;
  GVariantBuilder details;
  gchar *provider;

  g_info ("Creating new GOA account for Telepathy account %s",
      get_id_from_tp_account (tp_account));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "ChatEnabled",
      tp_account_is_enabled (tp_account) ? "true" : "false");

  provider = g_strdup_printf ("telepathy/%s",
      tp_account_get_protocol_name (tp_account));

  goa_manager_call_add_account (goa_client_get_manager (priv->goa_client),
      provider,
      get_id_from_tp_account (tp_account),
      tp_account_get_display_name (tp_account),
      g_variant_builder_end (&credentials),
      g_variant_builder_end (&details),
      NULL, /* GCancellable* */
      (GAsyncReadyCallback) goa_account_created_cb,
      g_object_ref (tp_account));

  g_free (provider);
}

static gboolean
is_account_filtered (TpAccount *tp_account)
{
  const gchar *env;
  const gchar *id;

  env = g_getenv ("GOA_TELEPATHY_DEBUG_ACCOUNT_FILTER");
  if (env == NULL || env[0] == '\0')
    return FALSE;

  id = get_id_from_tp_account (tp_account);
  if (g_strstr_len (id, -1, env) != NULL)
    return FALSE; /* "env" is contained in "id" */
  else
    return TRUE;
}

static void
tp_account_added (GoaTpAccountLinker *self,
                  TpAccount          *tp_account)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  const gchar *id = get_id_from_tp_account (tp_account);
  GoaObject *goa_object = NULL;

  if (g_strcmp0 (tp_account_get_storage_provider (tp_account),
        "org.gnome.OnlineAccounts") == 0)
    {
      g_debug ("Skipping Telepathy account %s as it's handled directly by GOA", id);
      return;
    }

  if (is_account_filtered (tp_account))
    {
      g_debug ("The account %s is ignored for debugging reasons", id);
      return;
    }

  g_debug ("Telepathy account found: %s", id);

  g_hash_table_replace (priv->tp_accounts, g_strdup (id),
      g_object_ref (tp_account));

  g_signal_connect_object (tp_account, "notify::enabled",
      G_CALLBACK (tp_account_chat_enabled_changed_cb),
      self, 0);

  goa_object = g_hash_table_lookup (priv->goa_accounts, id);
  if (goa_object == NULL)
    {
      g_debug ("Found a Telepathy account with no corresponding "
          "GOA account: %s", id);
      create_goa_account (self, tp_account);
    }
  else
    {
      g_debug ("Found a Telepathy account with a matching "
          "GOA account: %s", id);
      /* Make sure the initial state is synced. */
      tp_account_chat_enabled_changed_cb (tp_account, NULL, self);
    }
}

static void
goa_account_removed_by_us_cb (GObject      *object,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  /* This callback is only used for debugging */
  GoaAccount *goa_account = GOA_ACCOUNT (object);
  GError *error = NULL;

  if (!goa_account_call_remove_finish (goa_account, res, &error))
    {
      g_critical ("Error removing GOA account %s (Telepathy object path: %s): "
          "%s (%s, %d)",
          goa_account_get_id (goa_account),
          get_id_from_goa_account (goa_account),
          error->message,
          g_quark_to_string (error->domain),
          error->code);
      g_error_free (error);
    }
}

static void
tp_account_removed_cb (TpAccountManager *manager,
                       TpAccount        *tp_account,
                       gpointer          user_data)
{
  GoaTpAccountLinker *self = user_data;
  GoaTpAccountLinkerPrivate *priv = self->priv;
  const gchar *id = get_id_from_tp_account (tp_account);
  GoaObject *goa_object = NULL;

  if (!g_hash_table_remove (priv->tp_accounts, id))
    {
      /* 1 - The user removes the GOA account
       * 2 - We delete the corresponding Telepathy account and remove it
       *     from priv->tp_accounts
       * 3 - "account-removed" is emitted by the account manager
       * 4 - tp_account_removed_cb is called for an unknown account
       */
      g_debug ("Ignoring removal of Telepathy account we asked to "
          "remove (%s)", id);
      return;
    }

  g_info ("Telepathy account %s removed, removing corresponding "
      "GOA account", id);

  goa_object = g_hash_table_lookup (priv->goa_accounts, id);
  if (goa_object == NULL)
    {
      g_critical ("There is no GOA account for removed Telepathy "
          "account %s", id);
      return;
    }
  goa_account_call_remove (goa_object_peek_account (goa_object),
      NULL, /* cancellable */
      goa_account_removed_by_us_cb, NULL);
  g_hash_table_remove (priv->goa_accounts, id);
}

static void
tp_account_validity_changed_cb (TpAccountManager *manager,
                                TpAccount        *tp_account,
                                gboolean          valid,
                                gpointer          user_data)
{
  GoaTpAccountLinker *self = user_data;

  if (valid)
    tp_account_added (self, tp_account);
}

static void
goa_account_added_cb (GoaClient *client,
                      GoaObject *goa_object,
                      gpointer   user_data)
{
  GoaTpAccountLinker *self = user_data;
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GoaAccount *goa_account = goa_object_peek_account (goa_object);
  const gchar *id = NULL;
  TpAccount *tp_account;

  if (!is_telepathy_account (goa_account))
    return;

  id = get_id_from_goa_account (goa_account);
  g_debug ("GOA account %s for Telepathy account %s added",
      goa_account_get_id (goa_account), id);

  g_signal_connect_object (goa_account, "notify::chat-disabled",
      G_CALLBACK (goa_account_chat_disabled_changed_cb), self, 0);

  g_hash_table_insert (priv->goa_accounts, g_strdup (id),
      g_object_ref (goa_object));

  tp_account = g_hash_table_lookup (priv->tp_accounts, id);
  if (tp_account != NULL)
    {
      /* The chat enabled status may have changed during the creation of the
       * GOA account, so we need to make sure it's synced. */
      tp_account_chat_enabled_changed_cb (tp_account, NULL, self);
    }
}

static void
start_if_ready (GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GList *goa_accounts = NULL;
  GList *tp_accounts = NULL;
  GList *l = NULL;
  GHashTableIter iter;
  gpointer key, value;

  if (priv->goa_client == NULL ||
      priv->account_manager == NULL ||
      !tp_proxy_is_prepared (priv->account_manager,
        TP_ACCOUNT_MANAGER_FEATURE_CORE))
    {
      /* Not everything is ready yet */
      return;
    }

  g_debug ("Both GOA and Tp are ready, starting tracking of accounts");

  /* GOA */
  goa_accounts = goa_client_get_accounts (priv->goa_client);
  for (l = goa_accounts; l != NULL; l = l->next)
    goa_account_added_cb (priv->goa_client, l->data, self);
  g_list_free_full (goa_accounts, g_object_unref);

  g_signal_connect_object (priv->goa_client, "account-added",
      G_CALLBACK (goa_account_added_cb), self, 0);

  /* Telepathy */
  tp_accounts = tp_account_manager_dup_valid_accounts (priv->account_manager);
  for (l = tp_accounts; l != NULL; l = l->next)
    tp_account_added (self, l->data);
  g_list_free_full (tp_accounts, g_object_unref);

  g_signal_connect_object (priv->account_manager, "account-validity-changed",
      G_CALLBACK (tp_account_validity_changed_cb), self, 0);
  g_signal_connect_object (priv->account_manager, "account-removed",
      G_CALLBACK (tp_account_removed_cb), self, 0);

  /* Now we check if any Telepathy account was deleted while goa-daemon
   * was not running. */
  g_hash_table_iter_init (&iter, priv->goa_accounts);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const gchar *id = key;
      GoaObject *goa_object = value;

      if (!g_hash_table_lookup (priv->tp_accounts, id))
        {
          g_warning ("The Telepathy account %s was removed while the daemon "
              "was not running, removing the corresponding GOA account", id);
          goa_account_call_remove (goa_object_peek_account (goa_object),
              NULL, /* cancellable */
              goa_account_removed_by_us_cb,
              NULL); /* user data */
        }
    }

  remove_tp_account_queue_check (self);
}

static void
account_manager_prepared_cb (GObject      *object,
                             GAsyncResult *res,
                             gpointer      user_data)
{
  GoaTpAccountLinker *self = user_data;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (object, res, &error))
    {
      g_critical ("Error preparing AM: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_debug("Telepathy account manager prepared");
  start_if_ready (self);
}

static void
goa_client_new_cb (GObject      *object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  GoaTpAccountLinker *self = user_data;
  GoaTpAccountLinkerPrivate *priv = self->priv;
  GError *error = NULL;

  priv->goa_client = goa_client_new_finish (result, &error);
  if (priv->goa_client == NULL)
    {
      g_critical ("Error connecting to GOA: %s", error->message);
      g_clear_error (&error);
      return;
    }

  g_debug("GOA client ready");
  start_if_ready (self);
}

static void
goa_tp_account_linker_dispose (GObject *object)
{
  GoaTpAccountLinker *self = GOA_TP_ACCOUNT_LINKER (object);
  GoaTpAccountLinkerPrivate *priv = self->priv;

  if (priv->remove_tp_account_queue != NULL)
    {
      g_queue_free_full (priv->remove_tp_account_queue, g_object_unref);
      priv->remove_tp_account_queue = NULL;
    }

  g_clear_object (&priv->account_manager);
  g_clear_object (&priv->goa_client);

  g_clear_pointer (&priv->goa_accounts, g_hash_table_unref);
  g_clear_pointer (&priv->tp_accounts, g_hash_table_unref);

  G_OBJECT_CLASS (goa_tp_account_linker_parent_class)->dispose (object);
}

static void
goa_tp_account_linker_init (GoaTpAccountLinker *self)
{
  GoaTpAccountLinkerPrivate *priv;

  g_debug ("Starting GOA <-> Telepathy account linker");

  self->priv = GOA_TP_ACCOUNT_LINKER_GET_PRIVATE (self);
  priv = self->priv;

  priv->goa_accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);
  priv->tp_accounts = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, g_object_unref);

  priv->remove_tp_account_queue = g_queue_new ();

  priv->account_manager = tp_account_manager_dup ();
  tp_proxy_prepare_async (priv->account_manager, NULL,
      account_manager_prepared_cb, self);

  goa_client_new (NULL, goa_client_new_cb, self);
}

static void
goa_tp_account_linker_class_init (GoaTpAccountLinkerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  goa_utils_initialize_client_factory ();

  g_type_class_add_private (gobject_class,
      sizeof (GoaTpAccountLinkerPrivate));

  gobject_class->dispose = goa_tp_account_linker_dispose;
}

GoaTpAccountLinker *
goa_tp_account_linker_new (void)
{
  return g_object_new (GOA_TYPE_TP_ACCOUNT_LINKER, NULL);
}

void
goa_tp_account_linker_remove_tp_account (GoaTpAccountLinker   *self,
                                         GoaObject            *object,
                                         GCancellable         *cancellable,
                                         GAsyncReadyCallback   callback,
                                         gpointer              user_data)
{
  GoaTpAccountLinkerPrivate *priv;
  GTask *task;

  g_return_if_fail (GOA_IS_TP_ACCOUNT_LINKER (self));
  priv = self->priv;

  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_tp_account_linker_remove_tp_account);
  g_task_set_task_data (task, g_object_ref (object), g_object_unref);
  g_queue_push_tail (priv->remove_tp_account_queue, g_object_ref (task));

  remove_tp_account_queue_check (self);

  g_object_unref (task);
}

gboolean
goa_tp_account_linker_remove_tp_account_finish (GoaTpAccountLinker  *self,
                                                GAsyncResult        *res,
                                                GError             **error)
{
  GTask *task;

  g_return_val_if_fail (GOA_IS_TP_ACCOUNT_LINKER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_tp_account_linker_remove_tp_account, FALSE);

  return g_task_propagate_boolean (task, error);
}
