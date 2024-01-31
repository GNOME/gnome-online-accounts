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
#include "goaproviderdialog.h"
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

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaProviderDialog *dialog;
  GoaClient *client;
  GoaObject *object;

  GtkWidget *email_entry;
  GtkWidget *client_id_entry;
} AccountData;

static void
account_data_free (gpointer user_data)
{
  AccountData *data = (AccountData *)user_data;

  g_clear_object (&data->client);
  g_clear_object (&data->object);
  g_free (data);
}

static void
on_email_entry_changed (GtkEditable *editable,
                        AccountData *data)
{
  GoaDialogState state = GOA_DIALOG_IDLE;
  const char *email;
  const char *client_id;

  email = gtk_editable_get_text (GTK_EDITABLE (data->email_entry));
  if (goa_utils_parse_email_address (email, NULL, NULL))
    state = GOA_DIALOG_READY;

  /* Ensure there is client ID, since there is no build-system default */
  client_id = gtk_editable_get_text (GTK_EDITABLE (data->client_id_entry));
  if ((client_id == NULL || *client_id == '\0')
      && (GOA_MS_GRAPH_CLIENT_ID == NULL || *GOA_MS_GRAPH_CLIENT_ID == '\0'))
    state = GOA_DIALOG_IDLE;

  goa_provider_dialog_set_state (data->dialog, state);
}

static void
create_account_details_ui (GoaProvider *provider,
                           AccountData *data,
                           gboolean     new_account)
{
  GoaProviderDialog *dialog = GOA_PROVIDER_DIALOG (data->dialog);
  GtkWidget *group;

  group = goa_provider_dialog_add_group (dialog, NULL);
  data->email_entry = goa_provider_dialog_add_entry (dialog, group, _("_E-Mail"));

  if (new_account)
    {
      GtkWidget *subgroup;

      group = goa_provider_dialog_add_group (dialog, NULL);
      subgroup = g_object_new (ADW_TYPE_EXPANDER_ROW,
                               "title", _("_Custom"),
                               "use-underline", TRUE,
                               NULL);
      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), subgroup);
      data->client_id_entry = goa_provider_dialog_add_entry (dialog, subgroup, _("Client ID"));
    }

  g_signal_connect (data->email_entry,
                    "changed",
                    G_CALLBACK (on_email_entry_changed),
                    data);
}

static gboolean
setup_tenant (GoaMsGraphProvider  *self,
              const char          *host,
              GError             **error)
{
  g_autofree char *config_url = NULL;
  SoupSession *session;
  SoupMessage *msg;
  GInputStream *res;
  JsonParser *parser;

  /* The OAuth configuration is re-built for each account being added,
   * so when the provider chains up the parent sees the correct values.
   */
  g_clear_pointer (&self->client_id, g_free);
  g_clear_pointer (&self->redirect_uri, g_free);
  g_clear_pointer (&self->authorization_uri, g_free);
  g_clear_pointer (&self->token_uri, g_free);

  /* First try to read openid configuration and extract known tenant configuration */
  config_url = g_strdup_printf ("https://login.microsoft.com/%s/v2.0/.well-known/openid-configuration", host);

  session = soup_session_new ();
  soup_session_add_feature_by_type (session, SOUP_TYPE_AUTH_NTLM);

  msg = soup_message_new ("GET", config_url);
  res = soup_session_send (session, msg, NULL, error);
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
          g_set_error (error,
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
              g_set_error (error,
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
              g_set_error (error,
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

static void
add_account_parent_cb (GoaProvider  *provider,
                       GAsyncResult *result,
                       gpointer      user_data)
{
  g_autoptr(GTask) task = G_TASK (g_steal_pointer (&user_data));
  GoaObject *object = NULL;
  GError *error = NULL;

  object = goa_provider_add_account_finish (provider, result, &error);
  if (object != NULL)
    goa_provider_task_return_account (task, GOA_OBJECT (object));
  else
    goa_provider_task_return_error (task, error);
}

static void
add_account_action_cb (GoaProviderDialog *dialog,
                       GParamSpec        *pspec,
                       GTask             *task)
{
  GoaMsGraphProvider *self = g_task_get_source_object (task);
  GoaProvider *provider = g_task_get_source_object (task);
  AccountData *data = g_task_get_task_data (task);
  GCancellable *cancellable = g_task_get_cancellable (task);
  const char *email;
  const char *client_id;
  g_autofree char *domain = NULL;
  g_autoptr(GError) error = NULL;

  if (goa_provider_dialog_get_state (data->dialog) != GOA_DIALOG_BUSY)
    return;

  email = gtk_editable_get_text (GTK_EDITABLE (data->email_entry));
  if (!goa_utils_parse_email_address (email, NULL, &domain))
    g_return_if_reached ();

  /* Setup tenant based on host, clearing the existing client configuration */
  if (!setup_tenant (self, domain, &error))
    {
      goa_provider_dialog_report_error (data->dialog, error);
      return;
    }

  /* Check for a custom client ID, then retrieve the effective client ID */
  client_id = gtk_editable_get_text (GTK_EDITABLE (data->client_id_entry));
  if (client_id != NULL && *client_id != '\0')
    g_set_str (&self->client_id, client_id);

  client_id = goa_oauth2_provider_get_client_id (GOA_OAUTH2_PROVIDER (self));
  self->redirect_uri = g_strdup_printf ("goa-oauth2://localhost/%s", client_id);

  /* With the provider configured for the client, we chain-up to authorize */
  GOA_PROVIDER_CLASS (goa_ms_graph_provider_parent_class)->add_account (provider,
                                                                        data->client,
                                                                        GTK_WINDOW (data->dialog),
                                                                        cancellable,
                                                                        (GAsyncReadyCallback)add_account_parent_cb,
                                                                        g_object_ref (task));
}

static void
add_account (GoaProvider         *provider,
             GoaClient           *client,
             GtkWindow           *parent,
             GCancellable        *cancellable,
             GAsyncReadyCallback  callback,
             gpointer             user_data)
{
  AccountData *data;
  g_autoptr(GTask) task = NULL;

  data = g_new0 (AccountData, 1);
  data->dialog = goa_provider_dialog_new (provider, client, parent);
  data->client = g_object_ref (client);

  task = g_task_new (provider, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, add_account);
  g_task_set_task_data (task, data, account_data_free);
  goa_provider_task_bind_window (task, GTK_WINDOW (data->dialog));

  create_account_details_ui (provider, data, TRUE);
  g_signal_connect_object (data->dialog,
                           "notify::state",
                           G_CALLBACK (add_account_action_cb),
                           task,
                           0 /* G_CONNECT_DEFAULT */);
  gtk_window_present (GTK_WINDOW (data->dialog));

  // We chain-up in add_account_parent_cb() once the user input is confirmed
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
