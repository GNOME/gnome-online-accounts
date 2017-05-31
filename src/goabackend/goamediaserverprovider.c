/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2014 Pranav Kant
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

#include "goaprovider.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"
#include "goaprovider-priv.h"
#include "goamediaserverprovider.h"
#include "goadleynaservermanager.h"
#include "goadleynaservermediadevice.h"
#include "goadlnaservermanager.h"

struct _GoaMediaServerProvider
{
  GoaProvider parent_instance;
  GoaDlnaServerManager *dlna_mngr;
};

typedef struct _GoaMediaServerProviderClass GoaMediaServerProviderClass;

struct _GoaMediaServerProviderClass
{
  GoaProviderClass parent_class;
};

G_DEFINE_TYPE_WITH_CODE (GoaMediaServerProvider, goa_media_server_provider, GOA_TYPE_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_MEDIA_SERVER_NAME,
                                                         0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_MEDIA_SERVER_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_strdup (_("Media Server"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED |
         GOA_PROVIDER_FEATURE_PHOTOS;
}

static GIcon *
get_provider_icon (GoaProvider *provider, GoaObject *object)
{
  return g_themed_icon_new_with_default_fallbacks ("network-server-symbolic");
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
  gboolean ret;
  GoaAccount *account;
  GoaMediaServer *mediaserver;
  const gchar *udn;
  gboolean photos_enabled;

  mediaserver = NULL;

  account = NULL;
  ret = FALSE;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_media_server_provider_parent_class)->build_object (provider,
                                                                                  object,
                                                                                  key_file,
                                                                                  group,
                                                                                  connection,
                                                                                  just_added,
                                                                                  error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  udn = goa_account_get_identity (account);

  /* Photos */
  photos_enabled = g_key_file_get_boolean (key_file, group, "PhotosEnabled", NULL);
  goa_object_skeleton_attach_photos (object, photos_enabled);

  /* Media Server */
  mediaserver = goa_object_get_media_server (GOA_OBJECT (object));
  if (mediaserver == NULL)
    {
      mediaserver = goa_media_server_skeleton_new ();
      g_object_set (G_OBJECT (mediaserver),
                    "dlna-supported", TRUE,
                    "udn", udn,
                    NULL);
      goa_object_skeleton_set_media_server (object, mediaserver);
    }

  if (just_added)
    {
      goa_account_set_photos_disabled (account, !photos_enabled);

      g_signal_connect (account,
                        "notify::photos-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        "PhotosEnabled");
    }

  ret = TRUE;

out:
  g_clear_object (&account);
  g_clear_object (&mediaserver);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
ensure_credentials_sync (GoaProvider         *provider,
                         GoaObject           *object,
                         gint                *out_expires_in,
                         GCancellable        *cancellable,
                         GError             **error)
{
  if (out_expires_in != NULL)
    *out_expires_in = 0;

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_list_box_row (GtkWidget *list_box,
                  DleynaServerMediaDevice *server)
{
  GtkWidget *label;
  GtkWidget *row;
  const gchar *name;

  row = gtk_list_box_row_new ();
  g_object_set_data_full (G_OBJECT (row), "server", g_object_ref (server), g_object_unref);
  gtk_container_add (GTK_CONTAINER (list_box), row);

  name = dleyna_server_media_device_get_friendly_name (server);
  label = gtk_label_new (name);
  gtk_widget_set_margin_start (label, 20);
  gtk_widget_set_margin_end (label, 20);
  gtk_widget_set_margin_top (label, 12);
  gtk_widget_set_margin_bottom (label, 12);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_container_add (GTK_CONTAINER (row), label);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GtkDialog *dialog;
  GMainLoop *loop;
  GoaDlnaServerManager *dlna_mngr;

  gchar *friendly_name;
  gchar *udn;

  gchar *account_object_path;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static void
server_found_cb (GoaDlnaServerManager *dlna_mngr,
                 DleynaServerMediaDevice *server,
                 GtkWidget *list_box)
{
  add_list_box_row (list_box, server);
  gtk_widget_show_all (list_box);
}

static void
server_lost_cb (GoaDlnaServerManager *dlna_mngr,
                DleynaServerMediaDevice *server,
                GtkWidget *list_box)
{
  GList *children;
  GList *l;
  const gchar *udn;

  children = gtk_container_get_children (GTK_CONTAINER (list_box));
  udn = dleyna_server_media_device_get_udn (server);

  for (l = children; l != NULL; l = l->next)
    {
      DleynaServerMediaDevice *tmp_server;
      GtkWidget *row = GTK_WIDGET (l->data);
      const gchar *tmp_udn;

      tmp_server = DLEYNA_SERVER_MEDIA_DEVICE (g_object_get_data (G_OBJECT (row), "server"));
      tmp_udn = dleyna_server_media_device_get_udn (tmp_server);
      if (g_strcmp0 (tmp_udn, udn) == 0)
        {
          gtk_container_remove (GTK_CONTAINER (list_box), row);
          break;
        }
    }

  g_list_free (children);
}

static void
list_box_activate_cb (GtkListBox *list_box,
                      GtkListBoxRow *row,
                      gpointer user_data)
{
  AddAccountData *data = user_data;
  DleynaServerMediaDevice *device;
  const gchar *friendly_name;
  const gchar *udn;

  device = DLEYNA_SERVER_MEDIA_DEVICE (g_object_get_data (G_OBJECT (row), "server"));
  friendly_name = dleyna_server_media_device_get_friendly_name (device);
  udn = dleyna_server_media_device_get_udn (device);

  data->udn = g_strdup (udn);
  data->friendly_name = g_strdup (friendly_name);
  gtk_dialog_response (data->dialog, GTK_RESPONSE_OK);
}

static void
update_header_func (GtkListBoxRow *row,
                    GtkListBoxRow *before,
                    gpointer user_data)
{
  GtkWidget *current;

  if (before == NULL)
    return;

  current = gtk_list_box_row_get_header (row);
  if (current == NULL)
    {
      current = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
      gtk_widget_show (current);
      gtk_list_box_row_set_header (row, current);
    }
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           AddAccountData *data)
{
  GList *l;
  GList *servers;
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *list_box;
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *scrolled_window;
  gchar *markup;
  gint height;

  goa_utils_set_dialog_title (provider, dialog, TRUE);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_widget_set_margin_start (grid0, 36);
  gtk_widget_set_margin_end (grid0, 36);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  label = gtk_label_new (_("Personal content can be added to your applications through a media server account."));
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 40);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_container_add (GTK_CONTAINER (grid0), label);

  grid1 = gtk_grid_new ();
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid1), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 6);
  gtk_container_add (GTK_CONTAINER (grid0), grid1);

  label = gtk_label_new ("");
  markup = g_strdup_printf ("<b>%s</b>", _("Available Media Servers"));
  gtk_label_set_markup (GTK_LABEL (label), markup);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  g_free (markup);
  gtk_container_add (GTK_CONTAINER (grid1), label);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_hexpand (scrolled_window, TRUE);
  gtk_widget_set_vexpand (scrolled_window, TRUE);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (grid1), scrolled_window);

  list_box = gtk_list_box_new ();
  gtk_list_box_set_header_func (GTK_LIST_BOX (list_box), update_header_func, NULL, NULL);
  gtk_container_add (GTK_CONTAINER (scrolled_window), list_box);
  g_signal_connect (list_box, "row-activated", G_CALLBACK (list_box_activate_cb), data);
  g_signal_connect_object (data->dlna_mngr, "server-found", G_CALLBACK (server_found_cb), list_box, 0);
  g_signal_connect_object (data->dlna_mngr, "server-lost", G_CALLBACK (server_lost_cb), list_box, 0);

  servers = goa_dlna_server_manager_dup_servers (data->dlna_mngr);
  for (l = servers; l != NULL; l = l->next)
    {
      DleynaServerMediaDevice *server = DLEYNA_SERVER_MEDIA_DEVICE (l->data);
      add_list_box_row (list_box, server);
    }

  g_list_free_full (servers, g_object_unref);

  label = gtk_label_new (_("No media servers found"));
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, "dim-label");
  gtk_list_box_set_placeholder (GTK_LIST_BOX (list_box), label);
  gtk_widget_show (label);

  gtk_window_get_size (GTK_WINDOW (data->dialog), NULL, &height);
  gtk_window_set_default_size (GTK_WINDOW (data->dialog), -1, height);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_account_cb (GoaManager *manager, GAsyncResult *res, gpointer user_data)
{
  AddAccountData *data = user_data;
  goa_manager_call_add_account_finish (manager,
                                       &data->account_object_path,
                                       res,
                                       &data->error);
  g_main_loop_quit (data->loop);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider  *provider,
             GoaClient    *client,
             GtkDialog    *dialog,
             GtkBox       *vbox,
             GError      **error)
{
  GoaMediaServerProvider *self = GOA_MEDIA_SERVER_PROVIDER (provider);
  AddAccountData data;
  GVariantBuilder details;
  GVariantBuilder credentials;
  GoaObject *ret;
  const gchar *provider_type;
  gint response;

  ret = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  if (self->dlna_mngr == NULL)
    self->dlna_mngr = goa_dlna_server_manager_dup_singleton ();
  data.dlna_mngr = self->dlna_mngr;

  create_account_details_ui (provider, dialog, vbox, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));

  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      goto out;
    }

  /* See if there's already an account of this type with the
   * given identity.
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  data.udn,
                                  data.friendly_name,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_account,
                                  &data.error))
    goto out;

  gtk_widget_hide (GTK_WIDGET (dialog));

  /* No authentication required, so passing an empty variant. */
  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "PhotosEnabled", "true");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                data.udn,
                                data.friendly_name,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL,
                                (GAsyncReadyCallback) add_account_cb,
                                &data);
  g_main_loop_run (data.loop);
  if (data.error != NULL)
    goto out;

  ret = GOA_OBJECT (g_dbus_object_manager_get_object (goa_client_get_object_manager (client),
                                                      data.account_object_path));

 out:
  /* We might have an object even when data.error is set.
   * eg., if we failed to store the credentials in the keyring.
  */
  if (data.error != NULL)
    g_propagate_error (error, data.error);
  else
    g_assert (ret != NULL);

  g_free (data.account_object_path);
  g_free (data.friendly_name);
  g_free (data.udn);
  g_clear_pointer (&data.loop, (GDestroyNotify) g_main_loop_unref);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider  *provider,
                 GoaClient    *client,
                 GoaObject    *object,
                 GtkWindow    *parent,
                 GError      **error)
{
  GoaAccount *account;

  account = goa_object_peek_account (object);
  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_media_server_provider_dispose (GObject *object)
{
  GoaMediaServerProvider *self = GOA_MEDIA_SERVER_PROVIDER (object);

  g_clear_object (&self->dlna_mngr);

  G_OBJECT_CLASS (goa_media_server_provider_parent_class)->dispose (object);
}

static void
goa_media_server_provider_init (GoaMediaServerProvider *self)
{
}

static void
goa_media_server_provider_class_init (GoaMediaServerProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GoaProviderClass *provider_class = GOA_PROVIDER_CLASS (klass);

  object_class->dispose = goa_media_server_provider_dispose;

  provider_class->get_provider_type       = get_provider_type;
  provider_class->get_provider_name       = get_provider_name;
  provider_class->get_provider_group      = get_provider_group;
  provider_class->get_provider_features   = get_provider_features;
  provider_class->get_provider_icon       = get_provider_icon;
  provider_class->add_account             = add_account;
  provider_class->refresh_account         = refresh_account;
  provider_class->build_object            = build_object;
  provider_class->ensure_credentials_sync = ensure_credentials_sync;
}
