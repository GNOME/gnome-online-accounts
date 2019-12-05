/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2015 Felipe Borges
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

#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>

#include "goahttpclient.h"
#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goaoauth2provider.h"
#include "goalastfmprovider.h"
#include "goarestproxy.h"
#include "goautils.h"

struct _GoaLastfmProvider
{
  GoaOAuth2Provider parent_instance;
};

G_DEFINE_TYPE_WITH_CODE (GoaLastfmProvider, goa_lastfm_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_LASTFM_NAME,
                                                         0));

/* ---------------------------------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *_provider)
{
  return GOA_LASTFM_NAME;
}

static gchar *
get_provider_name (GoaProvider *_provider,
                   GoaObject   *object)
{
  return g_strdup (_("Last.fm"));
}

static GoaProviderGroup
get_provider_group (GoaProvider *_provider)
{
  return GOA_PROVIDER_GROUP_BRANDED;
}

static GoaProviderFeatures
get_provider_features (GoaProvider *_provider)
{
  return GOA_PROVIDER_FEATURE_BRANDED | GOA_PROVIDER_FEATURE_MUSIC;
}

static const gchar *
get_request_uri (GoaProvider *provider)
{
  return "https://ws.audioscrobbler.com/2.0/";
}

static const gchar *
get_client_id (GoaOAuth2Provider *provider)
{
    return GOA_LASTFM_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *provider)
{
    return GOA_LASTFM_CLIENT_SECRET;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
build_object (GoaProvider         *provider,
              GoaObjectSkeleton   *object,
              GKeyFile            *key_file,
              const gchar         *group,
              GDBusConnection     *connection,
              gboolean             just_added,
              GError             **error)
{
  GoaAccount *account;
  GoaMusic *music = NULL;
  gboolean music_enabled;
  gboolean ret = FALSE;

  account = NULL;

  /* Chain up */
  if (!GOA_PROVIDER_CLASS (goa_lastfm_provider_parent_class)->build_object (provider,
                                                                            object,
                                                                            key_file,
                                                                            group,
                                                                            connection,
                                                                            just_added,
                                                                            error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));

  /* Music */
  music = goa_object_get_music (GOA_OBJECT (object));
  music_enabled = g_key_file_get_boolean (key_file, group, "MusicEnabled", NULL);
  if (music_enabled)
    {
      if (music == NULL)
        {
          music = goa_music_skeleton_new ();
          goa_object_skeleton_set_music (object, music);
        }
    }
  else
    {
      if (music != NULL)
        goa_object_skeleton_set_music (object, NULL);
    }

  if (just_added)
    {
      goa_account_set_music_disabled (account, !music_enabled);

      g_signal_connect (account,
                        "notify::music-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "MusicEnabled");
    }

  ret = TRUE;

 out:
  g_clear_object (&music);
  g_clear_object (&account);
  return ret;
}

static gboolean
lastfm_login_sync (GoaProvider                  *provider,
                   const gchar                  *username,
                   const gchar                  *password,
                   GError                       **error)
{
  JsonParser *parser;
  JsonObject *json_obj;
  JsonObject *session_obj;
  JsonNode *root;
  RestProxyCall *call;
  const gchar *payload;
  gchar *sig;
  gchar *sig_md5;
  gboolean ret;

  call = NULL;
  parser = NULL;
  ret = FALSE;

  sig = g_strdup_printf ("api_key%s"
                         "methodauth.getMobileSession"
                         "password%s"
                         "username%s"
                         "%s",
                         GOA_LASTFM_CLIENT_ID,
                         password,
                         username,
                         GOA_LASTFM_CLIENT_SECRET);
  sig_md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig, -1);

  call = rest_proxy_new_call (goa_rest_proxy_new (get_request_uri (provider), FALSE));

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "method", "auth.getMobileSession");
  rest_proxy_call_add_param (call, "api_key", GOA_LASTFM_CLIENT_ID);
  rest_proxy_call_add_param (call, "username", username);
  rest_proxy_call_add_param (call, "password", password);
  rest_proxy_call_add_param (call, "api_sig", sig_md5);
  rest_proxy_call_add_param (call, "format", "json");

  if (!rest_proxy_call_sync (call, error))
    goto out;

  parser = json_parser_new ();
  payload = rest_proxy_call_get_payload (call);
  if (payload == NULL)
    {
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  if (!json_parser_load_from_data (parser,
                                   payload,
                                   rest_proxy_call_get_payload_length (call),
                                   NULL))
    {
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  root = json_parser_get_root (parser);
  json_obj = json_node_get_object (root);
  if (!json_object_has_member (json_obj, "session"))
    {
      g_warning ("Did not find session in JSON data");
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  session_obj = json_node_get_object (json_object_get_member (json_obj, "session"));
  if (!json_object_has_member (session_obj, "name"))
    {
      g_warning ("Did not find session.name in JSON data");
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }
  if (!json_object_has_member (session_obj, "key"))
    {
      g_warning ("Did not find session.key in JSON data");
      g_set_error (error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  ret = TRUE;

 out:
  g_clear_object (&parser);
  g_clear_object (&call);
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
  gchar *username = NULL;
  gchar *password = NULL;
  gboolean ret = FALSE;

  if (!goa_utils_get_credentials (provider, object, "password", &username, &password, cancellable, error))
    {
      if (error != NULL)
        {
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  if (!lastfm_login_sync (provider, username, password, error))
    {
      if (error != NULL)
        {
          g_prefix_error (error,
                          /* Translators: the first %s is the username
                           * (eg., debarshi.ray@gmail.com or rishi), and the
                           * (%s, %d) is the error domain and code.
                           */
                          _("Invalid password with username “%s” (%s, %d): "),
                          username,
                          g_quark_to_string ((*error)->domain),
                          (*error)->code);
          (*error)->domain = GOA_ERROR;
          (*error)->code = GOA_ERROR_NOT_AUTHORIZED;
        }
      goto out;
    }

  if (out_expires_in != NULL)
    *out_expires_in = 0;

  ret = TRUE;

 out:
  g_free (username);
  g_free (password);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
add_entry (GtkWidget     *grid,
           gint           row,
           const gchar   *text,
           GtkWidget    **out_entry)
{
  GtkStyleContext *context;
  GtkWidget *label;
  GtkWidget *entry;

  label = gtk_label_new_with_mnemonic (text);
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);

  entry = gtk_entry_new ();
  gtk_widget_set_hexpand (entry, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  gtk_grid_attach (GTK_GRID (grid), entry, 1, row, 3, 1);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  if (out_entry != NULL)
    *out_entry = entry;
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *cluebar;
  GtkWidget *cluebar_label;
  GtkWidget *connect_button;
  GtkWidget *progress_grid;

  GtkWidget *username;
  GtkWidget *password;

  gchar *account_object_path;
  gchar *access_token;

  GError *error;
} AddAccountData;

/* ---------------------------------------------------------------------------------------------------- */

static void
on_username_or_password_changed (GtkEditable *editable, gpointer user_data)
{
  AddAccountData *data = user_data;
  gboolean can_add;
  gchar *username;
  gchar *password;

  can_add = FALSE;
  username = NULL;
  password = NULL;

  username = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->username)));
  password = g_strdup (gtk_entry_get_text (GTK_ENTRY (data->password)));
  if ((username == NULL) || (password == NULL))
    goto out;

  can_add = gtk_entry_get_text_length (GTK_ENTRY (data->username)) != 0
            && gtk_entry_get_text_length (GTK_ENTRY (data->password)) != 0;

 out:
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, can_add);
  g_free (username);
  g_free (password);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           gboolean        new_account,
                           AddAccountData *data)
{
  GtkWidget *grid0;
  GtkWidget *grid1;
  GtkWidget *label;
  GtkWidget *spinner;
  gint row;
  gint width;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  grid0 = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (grid0), 5);
  gtk_widget_set_margin_bottom (grid0, 6);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (grid0), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_row_spacing (GTK_GRID (grid0), 12);
  gtk_container_add (GTK_CONTAINER (vbox), grid0);

  data->cluebar = gtk_info_bar_new ();
  gtk_info_bar_set_message_type (GTK_INFO_BAR (data->cluebar), GTK_MESSAGE_ERROR);
  gtk_widget_set_hexpand (data->cluebar, TRUE);
  gtk_widget_set_no_show_all (data->cluebar, TRUE);
  gtk_container_add (GTK_CONTAINER (grid0), data->cluebar);

  data->cluebar_label = gtk_label_new ("");
  gtk_label_set_line_wrap (GTK_LABEL (data->cluebar_label), TRUE);
  gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (data->cluebar))),
                     data->cluebar_label);

  grid1 = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid1), 12);
  gtk_grid_set_row_spacing (GTK_GRID (grid1), 12);
  gtk_container_add (GTK_CONTAINER (grid0), grid1);

  row = 0;
  add_entry (grid1, row++, _("User_name"), &data->username);
  add_entry (grid1, row++, _("_Password"), &data->password);
  gtk_entry_set_visibility (GTK_ENTRY (data->password), FALSE);

  gtk_widget_grab_focus ((new_account) ? data->username : data->password);

  g_signal_connect (data->username, "changed", G_CALLBACK (on_username_or_password_changed), data);
  g_signal_connect (data->password, "changed", G_CALLBACK (on_username_or_password_changed), data);

  gtk_dialog_add_button (data->dialog, _("_Cancel"), GTK_RESPONSE_CANCEL);
  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  data->progress_grid = gtk_grid_new ();
  gtk_widget_set_no_show_all (data->progress_grid, TRUE);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->progress_grid), GTK_ORIENTATION_HORIZONTAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->progress_grid), 3);
  gtk_container_add (GTK_CONTAINER (grid0), data->progress_grid);

  spinner = gtk_spinner_new ();
  gtk_widget_set_size_request (spinner, 20, 20);
  gtk_widget_show (spinner);
  gtk_spinner_start (GTK_SPINNER (spinner));
  gtk_container_add (GTK_CONTAINER (data->progress_grid), spinner);

  label = gtk_label_new (_("Connecting…"));
  gtk_widget_show (label);
  gtk_container_add (GTK_CONTAINER (data->progress_grid), label);

  if (new_account)
    {
      gtk_window_get_size (GTK_WINDOW (data->dialog), &width, NULL);
      gtk_window_set_default_size (GTK_WINDOW (data->dialog), width, -1);
    }
  else
    {
      GtkWindow *parent;

      /* Keep in sync with GoaPanelAddAccountDialog in
       * gnome-control-center.
       */
      parent = gtk_window_get_transient_for (GTK_WINDOW (data->dialog));
      if (parent != NULL)
        {
          gtk_window_get_size (parent, &width, NULL);
          gtk_window_set_default_size (GTK_WINDOW (data->dialog), (gint) (0.5 * width), -1);
        }
    }
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

static void
check_cb (RestProxyCall *call,
          const GError *error,
          GObject *weak_object,
          gpointer user_data)
{
  AddAccountData *data = user_data;
  JsonNode *session;
  JsonParser *parser;
  JsonObject *json_obj;
  JsonObject *session_obj;
  const gchar *payload;

  parser = NULL;

  parser = json_parser_new ();
  payload = rest_proxy_call_get_payload (call);

  if (payload == NULL)
    {
      g_set_error (&data->error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  if (!json_parser_load_from_data (parser,
                                   payload,
                                   rest_proxy_call_get_payload_length (call),
                                   &data->error))
    {
      g_set_error (&data->error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  json_obj = json_node_get_object (json_parser_get_root (parser));
  if (!json_object_has_member (json_obj, "session"))
    {
      g_warning ("Did not find session in JSON data");
      g_set_error (&data->error, GOA_ERROR, GOA_ERROR_FAILED, _("Authentication failed"));
      goto out;
    }

  session = json_object_get_member (json_obj, "session");
  session_obj = json_node_get_object (session);
  if (!json_object_has_member (session_obj, "name"))
    {
      g_warning ("Did not find session.name in JSON data");
      g_set_error (&data->error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }
  if (!json_object_has_member (session_obj, "key"))
    {
      g_warning ("Did not find session.key in JSON data");
      g_set_error (&data->error, GOA_ERROR, GOA_ERROR_FAILED, _("Could not parse response"));
      goto out;
    }

  data->access_token = g_strdup (json_object_get_string_member (session_obj, "key"));

 out:
  g_main_loop_quit (data->loop);
  gtk_widget_set_sensitive (data->connect_button, TRUE);
  gtk_widget_hide (data->progress_grid);
  g_clear_object (&parser);
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL)
    g_cancellable_cancel (data->cancellable);
}

static void
on_rest_proxy_call_cancelled_cb (GCancellable *cancellable, RestProxyCall *call)
{
  rest_proxy_call_cancel (call);
}

static void
lastfm_login (GoaProvider                  *provider,
              const gchar                  *username,
              const gchar                  *password,
              GCancellable                 *cancellable,
              RestProxyCallAsyncCallback   callback,
              gpointer                     user_data)
{
  AddAccountData *data = user_data;
  RestProxyCall *call;
  gchar *sig;
  gchar *sig_md5;

  call = NULL;

  sig = g_strdup_printf ("api_key%s"
                         "methodauth.getMobileSession"
                         "password%s"
                         "username%s"
                         "%s",
                         GOA_LASTFM_CLIENT_ID,
                         password,
                         username,
                         GOA_LASTFM_CLIENT_SECRET);
  sig_md5 = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig, -1);

  call = rest_proxy_new_call (goa_rest_proxy_new (get_request_uri (provider), FALSE));

  rest_proxy_call_set_method (call, "POST");
  rest_proxy_call_add_header (call, "Content-Type", "application/x-www-form-urlencoded");
  rest_proxy_call_add_param (call, "method", "auth.getMobileSession");
  rest_proxy_call_add_param (call, "api_key", GOA_LASTFM_CLIENT_ID);
  rest_proxy_call_add_param (call, "username", username);
  rest_proxy_call_add_param (call, "password", password);
  rest_proxy_call_add_param (call, "api_sig", sig_md5);
  rest_proxy_call_add_param (call, "format", "json");

  rest_proxy_call_async (call, callback, NULL, data, &data->error);

  g_signal_connect (cancellable, "cancelled", G_CALLBACK (on_rest_proxy_call_cancelled_cb), call);

  g_free (sig_md5);
  g_free (sig);
  g_object_unref (call);
}

/* ---------------------------------------------------------------------------------------------------- */

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  AddAccountData data;
  GVariantBuilder credentials;
  GVariantBuilder details;
  GoaObject *ret;
  const gchar *password;
  const gchar *username;
  const gchar *provider_type;
  gint response;

  ret = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

 login_again:
  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED, _("Dialog was dismissed"));
      goto out;
    }

  username = gtk_entry_get_text (GTK_ENTRY (data.username));
  password = gtk_entry_get_text (GTK_ENTRY (data.password));

  /* See if there's already an account of this type with the
   * given identity
   */
  provider_type = goa_provider_get_provider_type (provider);
  if (!goa_utils_check_duplicate (client,
                                  username,
                                  username,
                                  provider_type,
                                  (GoaPeekInterfaceFunc) goa_object_peek_oauth2_based,
                                  &data.error))
    goto out;

  g_cancellable_reset (data.cancellable);
  lastfm_login (provider,
                username,
                password,
                data.cancellable,
                (RestProxyCallAsyncCallback) check_cb,
                &data);

  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
  g_main_loop_run (data.loop);

  if (g_cancellable_is_cancelled (data.cancellable))
    {
      g_prefix_error (&data.error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data.error->domain),
                      data.error->code);
      data.error->domain = GOA_ERROR;
      data.error->code = GOA_ERROR_DIALOG_DISMISSED;
      g_message ("%s", data.error->message);
      goto out;
    }
  else if (data.error != NULL)
    {
      gchar *markup;

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));

      markup = g_strdup_printf ("<b>%s:</b>\n%s", _("Error connecting to Last.fm"), data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto login_again;
    }

  gtk_widget_hide (GTK_WIDGET (dialog));

  g_variant_builder_init (&credentials, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&credentials, "{sv}", "password", g_variant_new_string (password));
  g_variant_builder_add (&credentials, "{sv}", "access_token", g_variant_new_string (data.access_token));

  g_variant_builder_init (&details, G_VARIANT_TYPE ("a{ss}"));
  g_variant_builder_add (&details, "{ss}", "MusicEnabled", "true");

  /* OK, everything is dandy, add the account */
  /* we want the GoaClient to update before this method returns (so it
   * can create a proxy for the new object) so run the mainloop while
   * waiting for this to complete
   */
  goa_manager_call_add_account (goa_client_get_manager (client),
                                goa_provider_get_provider_type (provider),
                                username,
                                username,
                                g_variant_builder_end (&credentials),
                                g_variant_builder_end (&details),
                                NULL, /* GCancellable* */
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

  g_signal_handlers_disconnect_by_func (dialog, dialog_response_cb, &data);

  g_free (data.access_token);
  g_free (data.account_object_path);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_clear_object (&data.cancellable);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
refresh_account (GoaProvider    *provider,
                 GoaClient      *client,
                 GoaObject      *object,
                 GtkWindow      *parent,
                 GError        **error)
{
  AddAccountData data;
  GVariantBuilder builder;
  GoaAccount *account;
  GtkWidget *dialog;
  GtkWidget *vbox;
  gboolean ret;
  const gchar *password;
  const gchar *username;
  gint response;

  g_return_val_if_fail (GOA_IS_LASTFM_PROVIDER (provider), FALSE);
  g_return_val_if_fail (GOA_IS_CLIENT (client), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (parent == NULL || GTK_IS_WINDOW (parent), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ret = FALSE;

  dialog = gtk_dialog_new_with_buttons (NULL,
                                        parent,
                                        GTK_DIALOG_MODAL
                                        | GTK_DIALOG_DESTROY_WITH_PARENT
                                        | GTK_DIALOG_USE_HEADER_BAR,
                                        NULL,
                                        NULL);
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 12);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
  gtk_box_set_spacing (GTK_BOX (vbox), 12);

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = GTK_DIALOG (dialog);
  data.error = NULL;

  create_account_details_ui (provider, GTK_DIALOG (dialog), GTK_BOX (vbox), FALSE, &data);

  account = goa_object_peek_account (object);
  username = goa_account_get_identity (account);
  gtk_entry_set_text (GTK_ENTRY (data.username), username);
  gtk_editable_set_editable (GTK_EDITABLE (data.username), FALSE);

  gtk_widget_show_all (dialog);
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

 login_again:
  response = gtk_dialog_run (GTK_DIALOG (dialog));
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error, GOA_ERROR, GOA_ERROR_DIALOG_DISMISSED, _("Dialog was dismissed"));
      goto out;
    }

  password = gtk_entry_get_text (GTK_ENTRY (data.password));
  g_cancellable_reset (data.cancellable);
  lastfm_login (provider,
                username,
                password,
                data.cancellable,
                (RestProxyCallAsyncCallback) check_cb,
                &data);
  gtk_widget_set_sensitive (data.connect_button, FALSE);
  gtk_widget_show (data.progress_grid);
  g_main_loop_run (data.loop);

  if (g_cancellable_is_cancelled (data.cancellable))
    {
      g_prefix_error (&data.error,
                      _("Dialog was dismissed (%s, %d): "),
                      g_quark_to_string (data.error->domain),
                      data.error->code);
      data.error->domain = GOA_ERROR;
      data.error->code = GOA_ERROR_DIALOG_DISMISSED;
      goto out;
    }
  else if (data.error != NULL)
    {
      gchar *markup;

      markup = g_strdup_printf ("<b>%s:</b>\n%s", _("Error connecting to Last.fm"), data.error->message);
      g_clear_error (&data.error);

      gtk_label_set_markup (GTK_LABEL (data.cluebar_label), markup);
      g_free (markup);

      gtk_button_set_label (GTK_BUTTON (data.connect_button), _("_Try Again"));
      gtk_widget_set_no_show_all (data.cluebar, FALSE);
      gtk_widget_show_all (data.cluebar);
      goto login_again;
    }

  /* TODO: run in worker thread */
  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add (&builder, "{sv}", "password", g_variant_new_string (password));
  g_variant_builder_add (&builder, "{sv}", "access_token", g_variant_new_string (data.access_token));

  if (!goa_utils_store_credentials_for_object_sync (provider,
                                                    object,
                                                    g_variant_builder_end (&builder),
                                                    NULL, /* GCancellable */
                                                    &data.error))
    goto out;

  goa_account_call_ensure_credentials (account,
                                       NULL, /* GCancellable */
                                       NULL, NULL); /* callback, user_data */

  ret = TRUE;
 out:
  if (data.error != NULL)
    g_propagate_error (error, data.error);

  gtk_widget_destroy (dialog);
  g_clear_pointer (&data.loop, g_main_loop_unref);
  g_free (data.access_token);
  g_clear_object (&data.cancellable);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_lastfm_provider_init (GoaLastfmProvider *provider)
{
}

static void
goa_lastfm_provider_class_init (GoaLastfmProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type          = get_provider_type;
  provider_class->get_provider_name          = get_provider_name;
  provider_class->get_provider_group         = get_provider_group;
  provider_class->get_provider_features      = get_provider_features;
  provider_class->add_account                = add_account;
  provider_class->refresh_account            = refresh_account;
  provider_class->build_object               = build_object;
  provider_class->ensure_credentials_sync    = ensure_credentials_sync;

  oauth2_class->get_client_id              = get_client_id;
  oauth2_class->get_client_secret          = get_client_secret;
}
