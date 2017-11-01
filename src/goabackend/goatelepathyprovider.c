/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2012 – 2017 Red Hat, Inc.
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

#include "config.h"
#include <glib/gi18n-lib.h>
#include <tp-account-widgets/tpaw-account-widget.h>
#include <tp-account-widgets/tpaw-user-info.h>
#include <tp-account-widgets/tpaw-utils.h>

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goatelepathyprovider.h"
#include "goatpaccountlinker.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

typedef struct _GoaTelepathyProviderPrivate GoaTelepathyProviderPrivate;

struct _GoaTelepathyProviderPrivate
{
  TpawProtocol *protocol;
  gchar *protocol_name;
  gchar *provider_type;
};

enum {
  PROP_0,
  PROP_PROTOCOL,
  PROP_PROTOCOL_NAME,
  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

struct _GoaTelepathyProvider
{
  GoaProvider parent_instance;
  GoaTelepathyProviderPrivate *priv;
};

typedef struct _GoaTelepathyProviderClass GoaTelepathyProviderClass;

struct _GoaTelepathyProviderClass
{
  GoaProviderClass parent_class;
};

G_DEFINE_TYPE (GoaTelepathyProvider, goa_telepathy_provider, GOA_TYPE_PROVIDER);

/* ---------------------------------------------------------------------------------------------------- */

static GoaTpAccountLinker *tp_linker = NULL;
static guint name_watcher_id = 0;

/* ---------------------------------------------------------------------------------------------------- */

/* Telepathy / telepathy-account widgets utility functions. */

static void
account_settings_ready_cb (TpawAccountSettings *settings,
                           GParamSpec          *pspec,
                           gpointer             user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);
}

static void
wait_for_account_settings_ready (TpawAccountSettings *settings,
                                 GMainLoop           *loop)
{
  if (!tpaw_account_settings_is_ready (settings))
    {
      g_signal_connect (settings, "notify::ready",
          G_CALLBACK (account_settings_ready_cb), loop);
      g_main_loop_run (loop);
    }
}

typedef struct
{
  GMainLoop *loop;
  GError *error;
  gboolean ret;
} PrepareTpProxyData;

static void
proxy_prepared_cb (GObject      *object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  PrepareTpProxyData *data = user_data;

  data->ret = tp_proxy_prepare_finish (object, res, &data->error);
  g_main_loop_quit (data->loop);
}

static gboolean
prepare_tp_proxy (gpointer       proxy,
                  const GQuark  *features,
                  GMainLoop     *loop,
                  GError       **error)
{
  PrepareTpProxyData data = { NULL, };

  data.loop = loop;

  tp_proxy_prepare_async (proxy, features, proxy_prepared_cb, &data);
  g_main_loop_run (data.loop);

  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
      g_clear_error (&data.error);
    }

  return data.ret;
}

static TpAccount *
find_tp_account (GoaObject  *goa_object,
                 GMainLoop  *loop,
                 GError    **out_error)
{
  GoaAccount *goa_account = NULL;
  const gchar *id = NULL;
  TpAccountManager *account_manager;
  GList *tp_accounts = NULL;
  GList *l = NULL;
  TpAccount *tp_account = NULL;
  GError *error = NULL;

  goa_account = goa_object_peek_account (goa_object);
  id = goa_account_get_identity (goa_account);

  account_manager = tp_account_manager_dup ();
  if (!prepare_tp_proxy (account_manager, NULL, loop, &error))
    goto out;

  tp_accounts = tp_account_manager_dup_valid_accounts (account_manager);
  for (l = tp_accounts; l != NULL; l = l->next)
    {
      if (g_strcmp0 (tp_proxy_get_object_path (l->data), id) == 0)
        {
          tp_account = g_object_ref (l->data);
          break;
        }
    }

  if (tp_account == NULL)
    {
      g_set_error (&error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Telepathy chat account not found"));
      goto out;
    }

out:
  if (error != NULL)
    g_propagate_error (out_error, error);

  g_clear_error (&error);
  g_clear_object (&account_manager);
  g_list_free_full (tp_accounts, g_object_unref);

  return tp_account;
}

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (provider)->priv;
  return priv->provider_type;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (provider)->priv;

  return g_strdup (tpaw_protocol_name_to_display_name (priv->protocol_name));
}

static GIcon *
get_provider_icon (GoaProvider *provider,
                   GoaObject   *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (provider)->priv;
  const gchar *icon_names[3];
  gchar *icon_name;
  GIcon *icon;

  /* Use symbolic icons for generic protocols. Use icons for the
   * branded ones if it matches their visual identity. Otherwise do
   * not use an icon.
   */
  if (g_strcmp0 (priv->protocol_name, "irc") == 0
      || g_strcmp0 (priv->protocol_name, "jabber") == 0
      || g_strcmp0 (priv->protocol_name, "local-xmpp") == 0
      || g_strcmp0 (priv->protocol_name, "sip") == 0)
    {
      icon_name = g_strdup ("user-available-symbolic");
    }
  else if (g_strcmp0 (priv->protocol_name, "aim") == 0
           || g_strcmp0 (priv->protocol_name, "gadugadu") == 0
           || g_strcmp0 (priv->protocol_name, "silc") == 0)
    {
      icon_name = tpaw_protocol_icon_name (priv->protocol_name);
    }
  else
    {
      icon_name = g_strdup ("goa-account");
    }

  icon_names[0] = icon_name;
  /* If the icon doesn't exist, just try with the default icon. */
  icon_names[1] = "goa-account";
  icon_names[2] = NULL;
  icon = g_themed_icon_new_from_names ((gchar **) icon_names, -1);

  g_free (icon_name);

  return icon;
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_CHAT;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_CHAT;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data)
{
  tp_linker = goa_tp_account_linker_new ();
  g_bus_unwatch_name (name_watcher_id);
  name_watcher_id = 0;
}

static void
initialize (GoaProvider *provider)
{
  static gsize once_init_value = 0;

  if (g_once_init_enter (&once_init_value))
    {
      name_watcher_id = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                          "org.gnome.OnlineAccounts",
                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                          on_name_acquired,
                                          NULL,
                                          NULL,
                                          NULL);

      g_once_init_leave (&once_init_value, 1);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  GoaObject *ret;
  GError *error;

  GoaTelepathyProvider *provider;
  GtkDialog *dialog;
  GtkBox *vbox;
  gboolean close_received;

  TpAccount *tp_account;

  GoaClient *goa_client;
  guint goa_account_added_id;
} AddAccountData;

static void
quit_main_loop_if_finished (AddAccountData *data)
{
  if (data->ret != NULL && data->close_received)
    g_main_loop_quit (data->loop);
}

static void
run_main_loop_if_needed (AddAccountData *data)
{
  if (data->ret == NULL || !data->close_received)
    g_main_loop_run (data->loop);
}

static gboolean
check_goa_object_match (AddAccountData *data,
                        GoaObject      *goa_object)
{
  GoaTelepathyProviderPrivate *priv = data->provider->priv;
  GoaAccount *goa_account = NULL;
  const gchar *provider_type = NULL;
  const gchar *goa_id = NULL;
  const gchar *tp_id = NULL;

  if (data->tp_account == NULL)
    {
      /* Still waiting for the creation of the TpAccount */
      return FALSE;
    }

  goa_account = goa_object_peek_account (goa_object);
  provider_type = goa_account_get_provider_type (goa_account);
  if (g_strcmp0 (provider_type, priv->provider_type) != 0)
    return FALSE;

  /* The backend-specific identity is set to the object path of the
   * corresponding Telepathy account object. */
  goa_id = goa_account_get_identity (goa_account);
  tp_id = tp_proxy_get_object_path (TP_PROXY (data->tp_account));
  if (g_strcmp0 (goa_id, tp_id) == 0)
    {
      /* Found it! */
      data->ret = g_object_ref (goa_object);
      quit_main_loop_if_finished (data);
      return TRUE;
    }

  return FALSE;
}

static gboolean
check_existing_goa_accounts (AddAccountData *data)
{
  GList *goa_accounts = NULL;
  GList *l = NULL;
  gboolean found = FALSE;

  if (data->tp_account == NULL)
    return FALSE;

  goa_accounts = goa_client_get_accounts (data->goa_client);
  for (l = goa_accounts; l != NULL; l = l->next)
    {
      if (check_goa_object_match (data, l->data))
        {
          found = TRUE;
          break;
        }
    }
  g_list_free_full (goa_accounts, g_object_unref);

  return found;
}

static void
tp_account_created_cb (TpawAccountWidget *widget,
                       TpAccount         *tp_account,
                       AddAccountData    *data)
{
  g_assert (data->tp_account == NULL);
  data->tp_account = g_object_ref (tp_account);

  check_existing_goa_accounts (data);
}

static void
goa_account_added_cb (GoaClient *client,
                      GoaObject *goa_object,
                      gpointer   user_data)
{
  AddAccountData *data = user_data;

  check_goa_object_match (data, goa_object);
}

static void
account_widget_close_cb (TpawAccountWidget *widget,
                         GtkResponseType    response,
                         AddAccountData    *data)
{
  data->close_received = TRUE;
  quit_main_loop_if_finished (data);
}

static GoaObject *
add_account (GoaProvider  *provider,
             GoaClient    *client,
             GtkDialog    *dialog,
             GtkBox       *vbox,
             GError      **error)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (provider)->priv;
  AddAccountData data;
  TpawAccountSettings *settings = NULL;
  TpawAccountWidget *account_widget = NULL;
  gint response;
  gint width;

  settings = tpaw_protocol_create_account_settings (priv->protocol);
  if (settings == NULL)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Failed to create a user interface for %s"),
                   priv->protocol != NULL ?
                      tpaw_protocol_get_protocol_name (priv->protocol) :
                      "(null)");
      return NULL;
    }

  memset (&data, 0, sizeof (AddAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);
  data.error = NULL;
  data.provider = GOA_TELEPATHY_PROVIDER (provider);
  data.dialog = dialog;
  data.vbox = vbox;

  data.goa_client = client;
  data.goa_account_added_id = g_signal_connect (data.goa_client,
      "account-added", G_CALLBACK (goa_account_added_cb), &data);

  wait_for_account_settings_ready (settings, data.loop);

  account_widget = tpaw_account_widget_new_for_protocol (settings,
      dialog, TRUE);
  gtk_container_add (GTK_CONTAINER (vbox), GTK_WIDGET (account_widget));
  gtk_widget_show (GTK_WIDGET (account_widget));
  g_signal_connect (account_widget, "account-created",
      G_CALLBACK (tp_account_created_cb), &data);
  g_signal_connect (account_widget, "close",
      G_CALLBACK (account_widget_close_cb), &data);

  /* The dialog now contains a lot of empty space between the account widget
   * and the buttons. We force it's vertical size to be just right to fit the
   * widget. */
  gtk_window_get_size (GTK_WINDOW (dialog), &width, NULL);
  gtk_window_set_default_size (GTK_WINDOW (dialog), width, -1);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK && response != GTK_RESPONSE_APPLY)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  if (data.error != NULL)
    {
      /* An error could have been set by a callback */
      goto out;
    }

  /* We wait for the account to be created */
  run_main_loop_if_needed (&data);

out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (data.ret != NULL);

  if (data.goa_account_added_id)
    g_signal_handler_disconnect (data.goa_client, data.goa_account_added_id);

  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.tp_account);

  return data.ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
account_dialog_widget_cancelled_cb (TpawAccountWidget *account_widget,
                                    gpointer           user_data)
{
  GError **error = user_data;

  g_set_error (error,
               GOA_ERROR,
               GOA_ERROR_DIALOG_DISMISSED,
               _("Dialog was dismissed"));
}

static gboolean
edit_connection_parameters (GoaObject  *goa_object,
                            GtkWindow  *parent,
                            GError    **out_error)
{
  GMainLoop *loop = NULL;
  TpAccount *tp_account = NULL;
  TpawAccountSettings *settings = NULL;
  GtkWidget *dialog = NULL;
  TpawAccountWidget *account_widget = NULL;
  GtkWidget *content_area = NULL;
  gboolean ret;
  GError *error = NULL;

  loop = g_main_loop_new (NULL, FALSE);

  tp_account = find_tp_account (goa_object, loop, &error);
  if (tp_account == NULL)
    goto out;

  settings = tpaw_account_settings_new_for_account (tp_account);
  wait_for_account_settings_ready (settings, loop);

  dialog = gtk_dialog_new_with_buttons (_("Connection Settings"),
      parent,
      GTK_DIALOG_MODAL
      | GTK_DIALOG_DESTROY_WITH_PARENT
      | GTK_DIALOG_USE_HEADER_BAR,
      NULL, NULL);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  account_widget = tpaw_account_widget_new_for_protocol (settings,
      GTK_DIALOG (dialog), FALSE);
  gtk_widget_set_margin_end (GTK_WIDGET (account_widget), 6);
  gtk_widget_set_margin_start (GTK_WIDGET (account_widget), 6);
  gtk_widget_set_margin_top (GTK_WIDGET (account_widget), 6);
  g_signal_connect (account_widget, "cancelled",
      G_CALLBACK (account_dialog_widget_cancelled_cb), &error);
  g_signal_connect_swapped (account_widget, "close",
      G_CALLBACK (g_main_loop_quit), loop);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_pack_start (GTK_BOX (content_area), GTK_WIDGET (account_widget),
      TRUE, TRUE, 0);

  gtk_widget_show (GTK_WIDGET (account_widget));
  gtk_widget_show (dialog);

  /* Wait for the dialog to be dismissed */
  g_main_loop_run (loop);

  gtk_widget_destroy (dialog);

out:
  if (error != NULL)
    {
      g_propagate_error (out_error, error);
      ret = FALSE;
    }
  else
    {
      ret = TRUE;
    }

  g_clear_object (&settings);
  g_clear_object (&tp_account);
  g_clear_pointer (&loop, g_main_loop_unref);

  return ret;
}

static gboolean
refresh_account (GoaProvider  *provider,
                 GoaClient    *client,
                 GoaObject    *object,
                 GtkWindow    *parent,
                 GError      **error)
{
  return edit_connection_parameters (object, parent, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GMainLoop *loop;
  GError *error;
} EditPersonalDetailsData;

static void
user_info_apply_cb (GObject      *object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  EditPersonalDetailsData *data = user_data;

  tpaw_user_info_apply_finish (TPAW_USER_INFO (object), res, &data->error);
  g_main_loop_quit (data->loop);
}

static gboolean
personal_details_timeout_cb (gpointer user_data)
{
  EditPersonalDetailsData *data = user_data;

  g_main_loop_quit (data->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
edit_personal_details (GoaObject  *goa_object,
                       GtkWindow  *parent,
                       GError    **error)
{
  EditPersonalDetailsData data;
  TpAccount *tp_account = NULL;
  GtkWidget *dialog = NULL;
  GtkWidget *user_info = NULL;
  GtkWidget *content_area = NULL;
  gint response;
  gboolean ret = FALSE;

  memset (&data, 0, sizeof (EditPersonalDetailsData));
  data.loop = g_main_loop_new (NULL, FALSE);

  tp_account = find_tp_account (goa_object, data.loop, &data.error);
  if (tp_account == NULL)
    goto out;

  dialog = gtk_dialog_new_with_buttons (_("Personal Details"),
      parent,
      GTK_DIALOG_MODAL
      | GTK_DIALOG_DESTROY_WITH_PARENT
      | GTK_DIALOG_USE_HEADER_BAR,
      _("_Cancel"), GTK_RESPONSE_CANCEL,
      _("_OK"), GTK_RESPONSE_OK,
      NULL);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  user_info = tpaw_user_info_new (tp_account);
  gtk_widget_set_margin_end (user_info, 6);
  gtk_widget_set_margin_start (user_info, 6);
  gtk_widget_set_margin_top (user_info, 6);
  gtk_widget_show (user_info);

  content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_pack_start (GTK_BOX (content_area), user_info, TRUE, TRUE, 0);

  g_timeout_add (100, personal_details_timeout_cb, &data);
  g_main_loop_run (data.loop);

  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response == GTK_RESPONSE_OK)
    {
      tpaw_user_info_apply_async (TPAW_USER_INFO (user_info),
          user_info_apply_cb, &data);
      g_main_loop_run (data.loop);
      if (data.error != NULL)
        goto out;
    }
  else
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  ret = TRUE;

out:
  if (data.error != NULL)
    {
      g_propagate_error (error, data.error);
    }

  g_clear_pointer (&dialog, gtk_widget_destroy);
  g_clear_object (&tp_account);
  g_clear_pointer (&data.loop, g_main_loop_unref);

  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider        *provider,
              GoaObjectSkeleton  *object,
              GKeyFile           *key_file,
              const gchar        *group,
              GDBusConnection    *connection,
              gboolean            just_added,
              GError            **error)
{
  GoaAccount *account;
  gboolean chat_enabled;
  gboolean ret;

  account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_telepathy_provider_parent_class)->build_object (provider,
                                                                               object,
                                                                               key_file,
                                                                               group,
                                                                               connection,
                                                                               just_added,
                                                                               error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Chat */
  chat_enabled = g_key_file_get_boolean (key_file, group, "ChatEnabled", NULL);
  goa_object_skeleton_attach_chat (object, chat_enabled);

  if (just_added)
    {
      goa_account_set_chat_disabled (account, !chat_enabled);
      g_signal_connect (account,
                        "notify::chat-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "ChatEnabled");
    }

  ret = TRUE;

out:
  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
remove_account_remove_tp_account_cb (GObject       *source_object,
                                     GAsyncResult  *res,
                                     gpointer       user_data)
{
  GError *error;
  GTask *task = G_TASK (user_data);

  error = NULL;
  if (!goa_tp_account_linker_remove_tp_account_finish (tp_linker, res, &error))
    {
      g_task_return_error (task, error);
      goto out;
    }

  g_task_return_boolean (task, TRUE);

 out:
  g_object_unref (task);
}

static void
remove_account (GoaProvider          *provider,
                GoaObject            *object,
                GCancellable         *cancellable,
                GAsyncReadyCallback   callback,
                gpointer              user_data)
{
  GoaTelepathyProvider *self = GOA_TELEPATHY_PROVIDER (provider);
  GTask *task;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, remove_account);

  goa_tp_account_linker_remove_tp_account (tp_linker,
                                           object,
                                           cancellable,
                                           remove_account_remove_tp_account_cb,
                                           g_object_ref (task));

  g_object_unref (task);
}

static gboolean
remove_account_finish (GoaProvider   *provider,
                       GAsyncResult  *res,
                       GError       **error)
{
  GoaTelepathyProvider *self = GOA_TELEPATHY_PROVIDER (provider);
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == remove_account, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  guint ref_count;
  GoaObject *object;
  GtkWindow *parent;
} EditData;

static EditData *
edit_data_new (GoaObject *object,
               GtkWindow *parent)
{
  EditData *data;

  data = g_slice_new0 (EditData);
  data->ref_count = 1;
  data->object = g_object_ref (object);
  data->parent = parent;

  return data;
}

static void
edit_data_unref (EditData *data)
{
  data->ref_count--;
  if (data->ref_count >= 1)
    return;

  g_object_unref (data->object);
  g_slice_free (EditData, data);
}

static void
edit_button_destroy_cb (GtkWidget *button,
                        gpointer   user_data)
{
  EditData *data = user_data;

  edit_data_unref (data);
}

static void
edit_data_handle_button (EditData  *data,
                         GtkWidget *button,
                         GCallback  cb)
{
  g_return_if_fail (GTK_IS_BUTTON (button));

  g_signal_connect (button, "clicked", cb, data);
  g_signal_connect (button, "destroy", G_CALLBACK (edit_button_destroy_cb), data);

  data->ref_count++;
}

static void
maybe_show_error (GtkWindow   *parent,
                  GError      *error,
                  const gchar *msg)
{
  GtkWidget *dialog;

  if (error->domain == GOA_ERROR && error->code == GOA_ERROR_DIALOG_DISMISSED)
    return;

  dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
      "%s: %s (%s, %d)",
      msg,
      error->message,
      g_quark_to_string (error->domain),
      error->code);
  g_signal_connect (dialog, "response", G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_dialog_run (GTK_DIALOG (dialog));
}

static void
edit_parameters_clicked_cb (GtkButton *button,
                            gpointer   user_data)
{
  EditData *data = user_data;
  GError *error = NULL;

  if (!edit_connection_parameters (data->object, data->parent, &error))
    maybe_show_error (data->parent, error, _("Cannot save the connection parameters"));
  g_clear_error (&error);
}

static void
edit_personal_details_clicked_cb (GtkButton *button,
                                  gpointer   user_data)
{
  EditData *data = user_data;
  GError *error = NULL;

  if (!edit_personal_details (data->object, data->parent, &error))
    maybe_show_error (data->parent, error,
        _("Cannot save your personal information on the server"));
  g_clear_error (&error);
}

static void
show_account (GoaProvider         *provider,
              GoaClient           *client,
              GoaObject           *object,
              GtkBox              *vbox,
              G_GNUC_UNUSED GtkGrid *dummy1,
              G_GNUC_UNUSED GtkGrid *dummy2)
{
  EditData *data = NULL;
  GtkWidget *grid;
  GtkWidget *params_button = NULL;
  GtkWidget *details_button = NULL;
  GtkWidget *button_box = NULL;
  gint row = 0;

  goa_utils_account_add_attention_needed (client, object, provider, vbox);

  grid = gtk_grid_new ();
  gtk_widget_set_halign (grid, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand (grid, TRUE);
  gtk_widget_set_margin_end (grid, 72);
  gtk_widget_set_margin_start (grid, 72);
  gtk_widget_set_margin_top (grid, 24);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
  gtk_container_add (GTK_CONTAINER (vbox), grid);

  goa_utils_account_add_header (object, GTK_GRID (grid), row++);

  goa_util_add_row_switch_from_keyfile_with_blurb (GTK_GRID (grid),
                                                   row++,
                                                   object,
                                                   /* Translators: This is a label for a series of
                                                    * options switches. For example: “Use for Mail”. */
                                                   _("Use for"),
                                                   "chat-disabled",
                                                   _("C_hat"));

  data = edit_data_new (object, tpaw_get_toplevel_window (GTK_WIDGET (vbox)));

  /* Connection Settings button */
  params_button = gtk_button_new_with_mnemonic (_("_Connection Settings"));
  edit_data_handle_button (data, params_button, G_CALLBACK (edit_parameters_clicked_cb));

  /* Edit Personal Information button */
  details_button = gtk_button_new_with_mnemonic (_("_Personal Details"));
  edit_data_handle_button (data, details_button, G_CALLBACK (edit_personal_details_clicked_cb));

  /* Box containing the buttons */
  button_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (button_box), params_button,
      FALSE, FALSE, 12);
  gtk_container_add (GTK_CONTAINER (button_box), details_button);

  goa_util_add_row_widget (GTK_GRID (grid), row++, NULL, button_box);

  edit_data_unref (data);
}

/* ---------------------------------------------------------------------------------------------------- */

GoaTelepathyProvider *
goa_telepathy_provider_new_from_protocol_name (const gchar *protocol_name)
{
  g_return_val_if_fail (protocol_name != NULL, NULL);

  return g_object_new (GOA_TYPE_TELEPATHY_PROVIDER,
                       "protocol-name", protocol_name,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

GoaTelepathyProvider *
goa_telepathy_provider_new_from_protocol (TpawProtocol *protocol)
{
  g_return_val_if_fail (TPAW_IS_PROTOCOL (protocol), NULL);

  return g_object_new (GOA_TYPE_TELEPATHY_PROVIDER,
                       "protocol", protocol,
                       NULL);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_telepathy_provider_get_property (GObject *object,
                                     guint property_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

    switch (property_id) {
    case PROP_PROTOCOL:
        g_value_set_object (value, priv->protocol);
        break;
    case PROP_PROTOCOL_NAME:
        g_value_set_string (value, priv->protocol_name);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_telepathy_provider_set_property (GObject *object,
                                     guint property_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

    switch (property_id) {
    case PROP_PROTOCOL:
        priv->protocol = g_value_dup_object (value);
        break;
    case PROP_PROTOCOL_NAME:
        priv->protocol_name = g_value_dup_string (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_telepathy_provider_init (GoaTelepathyProvider *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        GOA_TYPE_TELEPATHY_PROVIDER, GoaTelepathyProviderPrivate);
}

static void
goa_telepathy_provider_constructed (GObject *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

  G_OBJECT_CLASS (goa_telepathy_provider_parent_class)->constructed (object);

  if (priv->protocol != NULL)
    {
      if (priv->protocol_name != NULL)
        g_error ("You cannot set \"protocol-name\" if you set \"protocol\"");
      priv->protocol_name = g_strdup (tpaw_protocol_get_protocol_name (priv->protocol));
    }
  else
    {
      if (priv->protocol_name == NULL)
        g_error ("You must set \"protocol-name\" or \"protocol\" on GoaTelepathyProvider");
    }

  priv->provider_type = g_strdup_printf ("%s/%s",
      GOA_TELEPATHY_NAME, priv->protocol_name);
}

static void
goa_telepathy_provider_finalize (GObject *object)
{
  GoaTelepathyProviderPrivate *priv = GOA_TELEPATHY_PROVIDER (object)->priv;

  g_clear_object (&priv->protocol);
  g_free (priv->protocol_name);
  g_free (priv->provider_type);

  (G_OBJECT_CLASS (goa_telepathy_provider_parent_class)->finalize) (object);
}

static void
goa_telepathy_provider_class_init (GoaTelepathyProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);

  goa_utils_initialize_client_factory ();

  object_class->constructed  = goa_telepathy_provider_constructed;
  object_class->finalize     = goa_telepathy_provider_finalize;
  object_class->get_property = goa_telepathy_provider_get_property;
  object_class->set_property = goa_telepathy_provider_set_property;

  provider_class->get_provider_type     = get_provider_type;
  provider_class->get_provider_name     = get_provider_name;
  provider_class->get_provider_icon     = get_provider_icon;
  provider_class->get_provider_group    = get_provider_group;
  provider_class->get_provider_features = get_provider_features;
  provider_class->initialize            = initialize;
  provider_class->add_account           = add_account;
  provider_class->refresh_account       = refresh_account;
  provider_class->build_object          = build_object;
  provider_class->remove_account        = remove_account;
  provider_class->remove_account_finish = remove_account_finish;
  provider_class->show_account          = show_account;

  g_type_class_add_private (object_class, sizeof (GoaTelepathyProviderPrivate));

  /**
   * GoaTelepathyProvider:protocol
   *
   * A #TpawProtocol associated to this provider (or NULL).
   */
  properties[PROP_PROTOCOL] =
    g_param_spec_object ("protocol",
        "Protocol",
        "A #TpawProtocol associated to the provider (or NULL)",
        TPAW_TYPE_PROTOCOL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  /**
   * GoaTelepathyProvider:protocol-name
   *
   * The name of the protocol associated to the provider.
   */
  properties[PROP_PROTOCOL_NAME] =
    g_param_spec_string ("protocol-name",
        "Protocol name",
        "The name of the protocol associated to the provider",
        NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);
}
