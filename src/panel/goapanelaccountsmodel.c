/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2008-2011 Red Hat, Inc.
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

#include <glib/gi18n-lib.h>

#include "goapanelaccountsmodel.h"

struct _GoaPanelAccountsModel
{
  GtkListStore parent_instance;

  GoaClient *client;
};

typedef struct
{
  GtkListStoreClass parent_class;
} GoaPanelAccountsModelClass;

enum
{
  PROP_0,
  PROP_CLIENT,
};

static void init_model (GoaPanelAccountsModel *model);

static void on_account_added (GoaClient   *client,
                              GDBusObject *object,
                              gpointer     user_data);

static void on_account_removed (GoaClient   *client,
                                GDBusObject *object,
                                gpointer     user_data);

static void on_account_changed (GoaClient   *client,
                                GDBusObject *object,
                                gpointer     user_data);

G_DEFINE_TYPE (GoaPanelAccountsModel, goa_panel_accounts_model, GTK_TYPE_LIST_STORE);

static void
goa_panel_accounts_model_finalize (GObject *object)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (object);

  g_signal_handlers_disconnect_by_func (model->client, G_CALLBACK (on_account_added), model);
  g_signal_handlers_disconnect_by_func (model->client, G_CALLBACK (on_account_removed), model);
  g_signal_handlers_disconnect_by_func (model->client, G_CALLBACK (on_account_changed), model);
  g_object_unref (model->client);

  G_OBJECT_CLASS (goa_panel_accounts_model_parent_class)->finalize (object);
}

static void
goa_panel_accounts_model_init (GoaPanelAccountsModel *model)
{
}

static void
goa_panel_accounts_model_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      g_value_set_object (value, goa_panel_accounts_model_get_client (model));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
goa_panel_accounts_model_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (object);

  switch (prop_id)
    {
    case PROP_CLIENT:
      model->client = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_panel_accounts_model_constructed (GObject *object)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (object);
  GType types[GOA_PANEL_ACCOUNTS_MODEL_N_COLUMNS];

  types[0] = G_TYPE_STRING;
  types[1] = G_TYPE_STRING;
  types[2] = G_TYPE_DBUS_OBJECT;
  G_STATIC_ASSERT (3 == GOA_PANEL_ACCOUNTS_MODEL_N_COLUMNS);
  gtk_list_store_set_column_types (GTK_LIST_STORE (model),
                                   GOA_PANEL_ACCOUNTS_MODEL_N_COLUMNS,
                                   types);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                        GOA_PANEL_ACCOUNTS_MODEL_COLUMN_SORT_KEY,
                                        GTK_SORT_ASCENDING);

  g_signal_connect (model->client,
                    "account-added",
                    G_CALLBACK (on_account_added),
                    model);
  g_signal_connect (model->client,
                    "account-removed",
                    G_CALLBACK (on_account_removed),
                    model);
  g_signal_connect (model->client,
                    "account-changed",
                    G_CALLBACK (on_account_changed),
                    model);

  init_model (model);

  if (G_OBJECT_CLASS (goa_panel_accounts_model_parent_class)->constructed != NULL)
    G_OBJECT_CLASS (goa_panel_accounts_model_parent_class)->constructed (object);
}

static void
goa_panel_accounts_model_class_init (GoaPanelAccountsModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = goa_panel_accounts_model_finalize;
  gobject_class->constructed  = goa_panel_accounts_model_constructed;
  gobject_class->get_property = goa_panel_accounts_model_get_property;
  gobject_class->set_property = goa_panel_accounts_model_set_property;

  /**
   * GoaPanelAccountsModel:client:
   *
   * The #GoaClient used by the #GoaPanelAccountsModel instance.
   */
  g_object_class_install_property (gobject_class,
                                   PROP_CLIENT,
                                   g_param_spec_object ("client",
                                                        "Client",
                                                        "The client used by the tree model",
                                                        GOA_TYPE_CLIENT,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
}

/**
 * goa_panel_accounts_model_new:
 * @client: A #GoaClient.
 *
 * Creates a new #GoaPanelAccountsModel for viewing the accounts known
 * by @client.
 *
 * Returns: A #GoaPanelAccountsModel. Free with g_object_unref().
 */
GoaPanelAccountsModel *
goa_panel_accounts_model_new (GoaClient  *client)
{
  return GOA_PANEL_ACCOUNTS_MODEL (g_object_new (GOA_TYPE_PANEL_ACCOUNTS_MODEL,
                                                 "client", client,
                                                 NULL));
}

/**
 * goa_panel_accounts_model_get_client:
 * @model: A #GoaPanelAccountsModel.
 *
 * Gets the #GoaClient used by @model.
 *
 * Returns: (transfer none): A #GoaClient. Do not free, the object
 *   belongs to @model.
 */
GoaClient *
goa_panel_accounts_model_get_client (GoaPanelAccountsModel *model)
{
  g_return_val_if_fail (GOA_IS_PANEL_ACCOUNTS_MODEL (model), NULL);
  return model->client;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GDBusObject *object;
  GtkTreeIter iter;
  gboolean found;
} FindIterData;

static gboolean
find_iter_for_object_cb (GtkTreeModel  *model,
                         GtkTreePath   *path,
                         GtkTreeIter   *iter,
                         gpointer       user_data)
{
  FindIterData *data = user_data;
  GDBusObject *iter_object;

  iter_object = NULL;

  gtk_tree_model_get (model,
                      iter,
                      GOA_PANEL_ACCOUNTS_MODEL_COLUMN_DBUS_OBJECT, &iter_object,
                      -1);
  if (iter_object == NULL)
    goto out;

  if (iter_object == data->object)
    {
      data->iter = *iter;
      data->found = TRUE;
      goto out;
    }

 out:
  if (iter_object != NULL)
    g_object_unref (iter_object);
  return data->found;
}

static gboolean
find_iter_for_object (GoaPanelAccountsModel *model,
                      GDBusObject           *object,
                      GtkTreeIter           *out_iter)
{
  FindIterData data;
  memset (&data, 0, sizeof (data));
  data.object = object;
  data.found = FALSE;
  gtk_tree_model_foreach (GTK_TREE_MODEL (model),
                          find_iter_for_object_cb,
                          &data);
  if (data.found)
    {
      if (out_iter != NULL)
        *out_iter = data.iter;
    }
  return data.found;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
set_values (GoaPanelAccountsModel  *model,
            GDBusObject            *object,
            GtkTreeIter            *iter)
{
  GoaAccount *account;
  account = GOA_PEEK_ACCOUNT (object);
  gtk_list_store_set (GTK_LIST_STORE (model),
                      iter,
                      GOA_PANEL_ACCOUNTS_MODEL_COLUMN_NAME, goa_account_get_name (account),
                      GOA_PANEL_ACCOUNTS_MODEL_COLUMN_SORT_KEY, goa_account_get_id (account),
                      GOA_PANEL_ACCOUNTS_MODEL_COLUMN_DBUS_OBJECT, object,
                      -1);
}

static void
add_account (GoaPanelAccountsModel  *model,
             GDBusObject            *object)
{
  GtkTreeIter iter;
  gtk_list_store_insert (GTK_LIST_STORE (model),
                         &iter,
                         G_MAXINT); /* position */
  set_values (model, object, &iter);
}

static void
remove_account (GoaPanelAccountsModel  *model,
                GDBusObject            *object)
{
  GtkTreeIter iter;
  if (!find_iter_for_object (model, object, &iter))
    {
      g_warning ("Error removing object %s - not in tree", g_dbus_object_get_object_path (object));
    }
  else
    {
      gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
    }
}

static void
update_account (GoaPanelAccountsModel  *model,
                GDBusObject            *object)
{
  GtkTreeIter iter;
  if (!find_iter_for_object (model, object, &iter))
    {
      g_warning ("Error updating object %s - not in tree", g_dbus_object_get_object_path (object));
    }
  else
    {
      set_values (model, object, &iter);
    }
}

static void
init_model (GoaPanelAccountsModel *model)
{
  GList *accounts;
  GList *l;

  accounts = goa_client_get_accounts (model->client);
  for (l = accounts; l != NULL; l = l->next)
    {
      GDBusObject *object = G_DBUS_OBJECT (l->data);
      add_account (model, object);
    }
  g_list_foreach (accounts, (GFunc) g_object_unref, NULL);
  g_list_free (accounts);
}

static void
on_account_added (GoaClient   *client,
                  GDBusObject *object,
                  gpointer     user_data)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (user_data);
  add_account (model, object);
}

static void
on_account_removed (GoaClient   *client,
                    GDBusObject *object,
                    gpointer     user_data)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (user_data);
  remove_account (model, object);
}

static void
on_account_changed (GoaClient   *client,
                    GDBusObject *object,
                    gpointer     user_data)
{
  GoaPanelAccountsModel *model = GOA_PANEL_ACCOUNTS_MODEL (user_data);
  update_account (model, object);
}

