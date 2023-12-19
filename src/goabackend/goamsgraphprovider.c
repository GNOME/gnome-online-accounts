/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2019 Vilém Hořínek
 * Copyright © 2022-2023 Jan-Michael Brummer <jan-michael.brummer1@volkswagen.de>
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

#include "goaprovider.h"
#include "goaprovider-priv.h"
#include "goamsgraphprovider.h"
#include "goarestproxy.h"
#include "goaobjectskeletonutils.h"
#include "goautils.h"

struct _GoaMsGraphProvider
{
  GoaOAuth2Provider parent_instance;

  gboolean setup_done;
  char *client_id;
  char *redirect_uri;
  char *authorization_uri;
  char *token_uri;
};

G_DEFINE_TYPE_WITH_CODE (GoaMsGraphProvider, goa_ms_graph_provider, GOA_TYPE_OAUTH2_PROVIDER,
                         goa_provider_ensure_extension_points_registered ();
                         g_io_extension_point_implement (GOA_PROVIDER_EXTENSION_POINT_NAME,
                                                         g_define_type_id,
                                                         GOA_MS_GRAPH_NAME, 0));

typedef struct
{
  GCancellable *cancellable;

  GtkDialog *dialog;
  GMainLoop *loop;

  GtkWidget *grid;
  GtkWidget *email_entry;
  GtkWidget *expander;
  GtkWidget *client_id_entry;
  GtkWidget *connect_button;
  GError *error;
} AddAccountData;

/* -------------------------------------------------------------------------- */

static const gchar *
get_provider_type (GoaProvider *provider)
{
  return GOA_MS_GRAPH_NAME;
}

static gchar *
get_provider_name (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_strdup (_("Microsoft 365"));
}

static GIcon *
get_provider_icon (GoaProvider *provider,
                   GoaObject   *object)
{
  return g_themed_icon_new_with_default_fallbacks ("goa-account-msn");
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
         GOA_PROVIDER_FEATURE_FILES;
}

static const gchar *
get_authorization_uri (GoaOAuth2Provider *oauth2_provider)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (oauth2_provider);
  return self->authorization_uri;
}

static const gchar *
get_token_uri (GoaOAuth2Provider *oauth2_provider)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (oauth2_provider);
  return self->token_uri;
}

static const gchar *
get_redirect_uri (GoaOAuth2Provider *oauth2_provider)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (oauth2_provider);
  return self->redirect_uri;
}

static const gchar *
get_scope (GoaOAuth2Provider *oauth2_provider)
{
  return "offline_access files.readwrite files.readwrite.all sites.read.all sites.readwrite.all user.read mail.readwrite contacts.readwrite";
}

static const gchar *
get_client_id (GoaOAuth2Provider *oauth2_provider)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (oauth2_provider);
  return self->client_id ? self->client_id : GOA_MS_GRAPH_CLIENT_ID;
}

static const gchar *
get_client_secret (GoaOAuth2Provider *oauth2_provider)
{
  return "";
}

static gchar *
build_authorization_uri (GoaOAuth2Provider *self,
                         const gchar       *authorization_uri,
                         const gchar       *escaped_redirect_uri,
                         const gchar       *escaped_client_id,
                         const gchar       *escaped_scope)
{
  return g_strdup_printf ("%s"
                          "?client_id=%s"
                          "&response_type=code"
                          "&redirect_uri=%s"
                          "&response_mode=query"
                          "&scope=%s",
                          authorization_uri,
                          escaped_client_id,
                          escaped_redirect_uri,
                          escaped_scope);
}

/* -------------------------------------------------------------------------- */

static gchar *
get_identity_sync (GoaOAuth2Provider  *oauth2_provider,
                   const gchar        *access_token,
                   gchar             **out_presentation_identity,
                   GCancellable       *cancellable,
                   GError            **error)
{
  JsonParser *parser = NULL;
  JsonObject *json_object = NULL;
  RestProxy *proxy = NULL;
  RestProxyCall *call = NULL;
  GError *identity_error = NULL;
  gchar *authorization = NULL;
  gchar *presentation_identity = NULL;
  gchar *id = NULL;
  gchar *ret = NULL;

  authorization = g_strconcat ("Bearer ", access_token, NULL);

  proxy = goa_rest_proxy_new ("https://graph.microsoft.com/v1.0/me/drive", FALSE);
  call = rest_proxy_new_call (proxy);
  rest_proxy_call_set_method (call, "GET");
  rest_proxy_call_add_header (call, "Authorization", authorization);

  if (!rest_proxy_call_sync (call, error))
    {
        goto out;
    }
  if (rest_proxy_call_get_status_code (call) != 200)
    {
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Expected status 200 when requesting your identity, instead got status %d (%s)"),
                   rest_proxy_call_get_status_code (call),
                   rest_proxy_call_get_status_message (call));
      goto out;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_data (parser,
                                   rest_proxy_call_get_payload (call),
                                   rest_proxy_call_get_payload_length (call),
                                   &identity_error))
    {
      g_debug ("json_parser_load_from_data() failed: %s (%s, %d)",
               identity_error->message,
               g_quark_to_string (identity_error->domain),
               identity_error->code);
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  json_object = json_node_get_object (json_parser_get_root (parser));
  if (!json_object_has_member (json_object, "owner"))
    {
      g_debug ("Did not find owner in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }

  json_object = json_object_get_object_member (json_object, "owner");
  if (!json_object_has_member (json_object, "user"))
    {
      g_debug ("Did not find user in JSON data");
      g_set_error (error,
                   GOA_ERROR,
                   GOA_ERROR_FAILED,
                   _("Could not parse response"));
      goto out;
    }
  json_object = json_object_get_object_member (json_object, "user");

  id = g_strdup (json_object_get_string_member (json_object, "id"));

  presentation_identity = g_strdup (json_object_get_string_member (json_object, "displayName"));
  ret = id;
  id = NULL;
  if (out_presentation_identity != NULL)
    {
      *out_presentation_identity = presentation_identity;
      presentation_identity = NULL;
    }

out:
  g_clear_object (&parser);
  g_clear_error (&identity_error);
  g_clear_object (&call);
  g_clear_object (&proxy);
  g_free (authorization);
  g_free (id);
  g_free (presentation_identity);
  return ret;
}

static gboolean
build_object (GoaProvider        *provider,
              GoaObjectSkeleton  *object,
              GKeyFile           *key_file,
              const gchar        *group,
              GDBusConnection    *connection,
              gboolean            just_added,
              GError            **error)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (provider);
  GoaAccount *account = NULL;
  const gchar *email_address = NULL;
  gboolean files_enabled = FALSE;
  gchar *uri_onedrive = NULL;
  gboolean ret = FALSE;

  if (!GOA_PROVIDER_CLASS (goa_ms_graph_provider_parent_class)->build_object (provider,
                                                                              object,
                                                                              key_file,
                                                                              group,
                                                                              connection,
                                                                              just_added,
                                                                              error))
    goto out;

  account = goa_object_get_account (GOA_OBJECT (object));
  email_address = goa_account_get_identity (account);

  /* Files */
  files_enabled = g_key_file_get_boolean (key_file, group, "FilesEnabled", NULL);
  uri_onedrive = g_strconcat ("onedrive://", email_address, "/", NULL);
  goa_object_skeleton_attach_files (object, uri_onedrive, files_enabled, FALSE);
  g_free (uri_onedrive);

  self->client_id = g_key_file_get_string (key_file, group, "client_id", NULL);
  self->redirect_uri = g_key_file_get_string (key_file, group, "redirect_uri", NULL);
  self->authorization_uri = g_key_file_get_string (key_file, group, "authorization_uri", NULL);
  self->token_uri = g_key_file_get_string (key_file, group, "token_uri", NULL);

  if (just_added)
    {
      goa_account_set_files_disabled (account, !files_enabled);

      g_signal_connect (account,
                        "notify::files-disabled",
                        G_CALLBACK (goa_util_account_notify_property_cb),
                        (gpointer) "FilesEnabled");
    }

  ret = TRUE;

out:
  g_clear_object (&account);
  return ret;
}

static void
add_account_key_values (GoaOAuth2Provider *oauth2_provider,
                        GVariantBuilder   *builder)
{
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (oauth2_provider);

  g_variant_builder_add (builder, "{ss}", "FilesEnabled", "true");

  g_variant_builder_add (builder, "{ss}", "OAuth2AuthorizationUri", self->authorization_uri);
  g_variant_builder_add (builder, "{ss}", "OAuth2TokenUri", self->token_uri);
  g_variant_builder_add (builder, "{ss}", "OAuth2ClientId", self->client_id);
  g_variant_builder_add (builder, "{ss}", "OAuth2RedirectUri", self->redirect_uri);
  g_variant_builder_add (builder, "{ss}", "OAuth2ClientSecret", "");

}

static void
on_email_entry_changed (GtkWidget *entry,
                        gpointer   user_data)
{
  AddAccountData *data = user_data;
  const char *email = gtk_entry_get_text (GTK_ENTRY (entry));
  g_auto (GStrv) split = NULL;
  gboolean ret = FALSE;

  split = g_strsplit (email, "@", -1);
  if (g_strv_length (split) == 2 && strlen (split[1]) > 0)
      ret = TRUE;

  gtk_widget_set_sensitive (data->connect_button, ret);
}

static void
create_account_details_ui (GoaProvider    *provider,
                           GtkDialog      *dialog,
                           GtkBox         *vbox,
                           gboolean        new_account,
                           AddAccountData *data)
{
  GtkWidget *label;
  GtkStyleContext *context;
  GtkWidget *expander_grid;

  goa_utils_set_dialog_title (provider, dialog, new_account);

  data->grid = gtk_grid_new ();
  gtk_container_set_border_width (GTK_CONTAINER (data->grid), 12);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (data->grid), GTK_ORIENTATION_VERTICAL);
  gtk_grid_set_column_spacing (GTK_GRID (data->grid), 6);
  gtk_grid_set_row_spacing (GTK_GRID (data->grid), 12);
  gtk_container_add (GTK_CONTAINER (vbox), data->grid);

  label = gtk_label_new_with_mnemonic (_("E-Mail"));
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_grid_attach (GTK_GRID (data->grid), label, 0, 0, 1, 1);

  data->email_entry = gtk_entry_new ();
  g_signal_connect (G_OBJECT (data->email_entry), "changed", G_CALLBACK (on_email_entry_changed), data);
  gtk_widget_set_hexpand (data->email_entry, TRUE);
  gtk_grid_attach (GTK_GRID (data->grid), data->email_entry, 1, 0, 1, 1);

  data->expander = gtk_expander_new_with_mnemonic (_("_Custom"));
  gtk_expander_set_expanded (GTK_EXPANDER (data->expander), FALSE);
  gtk_expander_set_resize_toplevel (GTK_EXPANDER (data->expander), TRUE);
  gtk_grid_attach (GTK_GRID (data->grid), data->expander, 0, 1, 2, 1);

  expander_grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (expander_grid), 6);
  gtk_grid_set_row_spacing (GTK_GRID (expander_grid), 12);
  gtk_container_add (GTK_CONTAINER (data->expander), expander_grid);

  label = gtk_label_new_with_mnemonic ("Client ID");
  context = gtk_widget_get_style_context (label);
  gtk_style_context_add_class (context, GTK_STYLE_CLASS_DIM_LABEL);
  gtk_widget_set_halign (label, GTK_ALIGN_END);
  gtk_grid_attach (GTK_GRID (expander_grid), label, 0, 1, 1, 1);

  data->client_id_entry = gtk_entry_new ();
  gtk_widget_set_hexpand (data->client_id_entry, TRUE);
  gtk_entry_set_activates_default (GTK_ENTRY (data->client_id_entry), TRUE);
  gtk_entry_set_text (GTK_ENTRY (data->client_id_entry), get_client_id (GOA_OAUTH2_PROVIDER (provider)));
  gtk_grid_attach (GTK_GRID (expander_grid), data->client_id_entry, 1, 1, 1, 1);

  data->connect_button = gtk_dialog_add_button (data->dialog, _("C_onnect"), GTK_RESPONSE_OK);
  gtk_dialog_set_default_response (data->dialog, GTK_RESPONSE_OK);
  gtk_dialog_set_response_sensitive (data->dialog, GTK_RESPONSE_OK, FALSE);

  gtk_widget_set_sensitive (data->connect_button, FALSE);
}

static void
dialog_response_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
  AddAccountData *data = user_data;

  if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT)
    g_cancellable_cancel (data->cancellable);
}

static gboolean
setup_tenant (GoaMsGraphProvider *self,
              const char         *host)
{
  g_autofree char *config_url = NULL;
  SoupSession *session;
  SoupMessage *msg;
  GInputStream *res;
  JsonParser *parser;
  g_autoptr (GError) error = NULL;

  /* First try to read openid configuration and extract known tenant configuration */
  config_url = g_strdup_printf ("https://login.microsoft.com/%s/v2.0/.well-known/openid-configuration", host);

  session = soup_session_new ();
  soup_session_add_feature_by_type (session, SOUP_TYPE_AUTH_NTLM);

  msg = soup_message_new ("GET", config_url);
  res = soup_session_send (session, msg, NULL, &error);
  if (res)
    {
      char buffer[2048];
      gsize len;
      GError *parse_error = NULL;
      JsonObject *json_object;

      memset(buffer, 0, sizeof (buffer));
      g_input_stream_read_all (res, &buffer, sizeof (buffer) - 1, &len, NULL, NULL);

      parser = json_parser_new ();
      if (!json_parser_load_from_data (parser,
                                       buffer,
                                       len,
                                       &parse_error))
        {
          g_debug ("json_parser_load_from_data() failed: %s (%s, %d)",
                   parse_error->message,
                   g_quark_to_string (parse_error->domain),
                   parse_error->code);
          g_set_error (&error,
                       GOA_ERROR,
                       GOA_ERROR_FAILED,
                       _("Could not parse response"));
          return FALSE;
        }

      json_object = json_node_get_object (json_parser_get_root (parser));
      if (!json_object_has_member (json_object, "error"))
        {
          if (!json_object_has_member (json_object, "authorization_endpoint"))
            {
              g_debug ("Did not find authorization_endpoint in JSON data");
              g_set_error (&error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           _("Could not parse response"));
              return FALSE;
            }

          char *url = g_strdup (json_object_get_string_member (json_object, "authorization_endpoint"));
          self->authorization_uri = url;

          json_object = json_node_get_object (json_parser_get_root (parser));
          if (!json_object_has_member (json_object, "token_endpoint"))
            {
              g_debug ("Did not find token_endpoint in JSON data");
              g_set_error (&error,
                           GOA_ERROR,
                           GOA_ERROR_FAILED,
                           _("Could not parse response"));
              return FALSE;
            }

          url = g_strdup (json_object_get_string_member (json_object, "token_endpoint"));
          self->token_uri = url;
        }
      }

    if (!self->authorization_uri)
      {
        g_debug ("No openid configuration, using defaults");
        self->authorization_uri = g_strdup ("https://login.microsoftonline.com/common/oauth2/v2.0/authorize");
        self->token_uri = g_strdup ("https://login.microsoftonline.com/common/oauth2/v2.0/token");
      }
  g_debug ("%s: authorization_uri=%s", __FUNCTION__, self->authorization_uri);
  g_debug ("%s: token_uri=%s", __FUNCTION__, self->token_uri);

  return TRUE;
}

static GoaObject *
add_account (GoaProvider    *provider,
             GoaClient      *client,
             GtkDialog      *dialog,
             GtkBox         *vbox,
             GError        **error)
{
  AddAccountData data;
  GoaObject *ret = NULL;
  int response;
  GoaMsGraphProvider *self = GOA_MS_GRAPH_PROVIDER (provider);
  const char *email;
  g_auto (GStrv) split = NULL;

  memset (&data, 0, sizeof (AddAccountData));
  data.cancellable = g_cancellable_new ();
  data.loop = g_main_loop_new (NULL, FALSE);
  data.dialog = dialog;
  data.error = NULL;

  create_account_details_ui (provider, dialog, vbox, TRUE, &data);
  gtk_widget_show_all (GTK_WIDGET (vbox));
  g_signal_connect (dialog, "response", G_CALLBACK (dialog_response_cb), &data);

  response = gtk_dialog_run (dialog);
  if (response != GTK_RESPONSE_OK)
    {
      g_set_error (&data.error,
                   GOA_ERROR,
                   GOA_ERROR_DIALOG_DISMISSED,
                   _("Dialog was dismissed"));
      return ret;
    }

  email = gtk_entry_get_text (GTK_ENTRY (data.email_entry));
  split = g_strsplit (email, "@", -1);
  if (g_strv_length (split) != 2) {
    return ret;
  }

  /* Setup tenant based on host */
  setup_tenant(self, split[1]);

  self->client_id = g_strdup (gtk_entry_get_text (GTK_ENTRY (data.client_id_entry)));
  self->redirect_uri = g_strdup_printf ("goa-oauth2://localhost/%s", self->client_id);

  gtk_widget_set_visible (data.grid, FALSE);
  gtk_widget_set_no_show_all (data.grid, TRUE);
  gtk_widget_set_sensitive (data.connect_button, FALSE);

  return GOA_PROVIDER_CLASS (goa_ms_graph_provider_parent_class)->add_account (provider, client, dialog, vbox, error);
}

/* -------------------------------------------------------------------------- */

static void
goa_ms_graph_provider_init (GoaMsGraphProvider *self)
{
  self->setup_done = FALSE;
}

static void
goa_ms_graph_provider_class_init (GoaMsGraphProviderClass *klass)
{
  GoaProviderClass *provider_class;
  GoaOAuth2ProviderClass *oauth2_class;

  provider_class = GOA_PROVIDER_CLASS (klass);
  provider_class->get_provider_type = get_provider_type;
  provider_class->get_provider_name = get_provider_name;
  provider_class->get_provider_icon = get_provider_icon;
  provider_class->get_provider_group = get_provider_group;
  provider_class->get_provider_features = get_provider_features;
  provider_class->build_object = build_object;
  provider_class->add_account = add_account;

  oauth2_class = GOA_OAUTH2_PROVIDER_CLASS (klass);
  oauth2_class->get_authorization_uri = get_authorization_uri;
  oauth2_class->get_client_id = get_client_id;
  oauth2_class->get_client_secret = get_client_secret;
  oauth2_class->get_identity_sync = get_identity_sync;
  oauth2_class->get_redirect_uri = get_redirect_uri;
  oauth2_class->get_scope = get_scope;
  oauth2_class->get_token_uri = get_token_uri;
  oauth2_class->add_account_key_values = add_account_key_values;

  oauth2_class->build_authorization_uri = build_authorization_uri;
}
