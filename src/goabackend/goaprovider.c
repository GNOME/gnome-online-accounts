/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright © 2011 – 2019 Red Hat, Inc.
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
#include "goaproviderdialog.h"
#include "goaprovider-priv.h"
#include "goaexchangeprovider.h"
#include "goagoogleprovider.h"
#include "goaimapsmtpprovider.h"
#include "goaowncloudprovider.h"
#include "goawebdavprovider.h"
#include "goamsgraphprovider.h"

#ifdef GOA_FEDORA_ENABLED
#include "goafedoraprovider.h"
#endif

#ifdef GOA_KERBEROS_ENABLED
#include "goakerberosprovider.h"
#endif

#include "goautils.h"

/**
 * SECTION:goaprovider
 * @title: GoaProvider
 * @short_description: Abstract base class for providers
 *
 * #GoaProvider is the base type for all providers.
 */

enum {
  PROP_0,
  PROP_PRESEED_DATA,
  NUM_PROPERTIES
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

static gboolean goa_provider_ensure_credentials_sync_real (GoaProvider   *self,
                                                           GoaObject     *object,
                                                           gint          *out_expires_in,
                                                           GCancellable  *cancellable,
                                                           GError       **error);

static gboolean goa_provider_build_object_real (GoaProvider         *self,
                                                GoaObjectSkeleton   *object,
                                                GKeyFile            *key_file,
                                                const gchar         *group,
                                                GDBusConnection     *connection,
                                                gboolean             just_added,
                                                GError             **error);

static guint goa_provider_get_credentials_generation_real (GoaProvider *self);

static GIcon *goa_provider_get_provider_icon_real (GoaProvider *self,
                                                   GoaObject   *object);

static GoaObject *goa_provider_real_add_account_finish (GoaProvider   *self,
                                                        GAsyncResult  *result,
                                                        GError       **error);

static gboolean goa_provider_real_refresh_account_finish (GoaProvider   *self,
                                                          GAsyncResult  *result,
                                                          GError       **error);

static void goa_provider_remove_account_real (GoaProvider          *self,
                                              GoaObject            *object,
                                              GCancellable         *cancellable,
                                              GAsyncReadyCallback   callback,
                                              gpointer              user_data);

static gboolean goa_provider_remove_account_finish_real (GoaProvider   *self,
                                                         GAsyncResult  *res,
                                                         GError       **error);

static void goa_provider_real_show_account (GoaProvider         *provider,
                                            GoaClient           *client,
                                            GoaObject           *object,
                                            GtkWidget           *parent,
                                            GCancellable        *cancellable,
                                            GAsyncReadyCallback  callback,
                                            gpointer             user_data);
static gboolean goa_provider_real_show_account_finish (GoaProvider   *self,
                                                       GAsyncResult  *result,
                                                       GError       **error);

G_DEFINE_ABSTRACT_TYPE (GoaProvider, goa_provider, G_TYPE_OBJECT);

static GoaProviderFeaturesInfo provider_features_info[] = {
  /* The order in which the features are listed is
   * important because it affects the order in which they are
   * displayed in the show_account() UI
   */
  {
    .feature = GOA_PROVIDER_FEATURE_MAIL,
    .property = "mail-disabled",
    .blurb = N_("_Mail"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_CALENDAR,
    .property = "calendar-disabled",
    .blurb = N_("Cale_ndar"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_CONTACTS,
    .property = "contacts-disabled",
    .blurb = N_("_Contacts"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_CHAT,
    .property = "chat-disabled",
    .blurb = N_("C_hat"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_DOCUMENTS,
    .property = "documents-disabled",
    .blurb = N_("_Documents"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_MUSIC,
    .property = "music-disabled",
    .blurb = N_("M_usic"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_PHOTOS,
    .property = "photos-disabled",
    .blurb = N_("_Photos"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_FILES,
    .property = "files-disabled",
    .blurb = N_("_Files"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_TICKETING,
    .property = "ticketing-disabled",
    .blurb = N_("Network _Resources"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_READ_LATER,
    .property = "read-later-disabled",
    .blurb = N_("_Read Later"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_PRINTERS,
    .property = "printers-disabled",
    .blurb = N_("Prin_ters"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_MAPS,
    .property = "maps-disabled",
    .blurb = N_("_Maps"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_TODO,
    .property = "todo-disabled",
    .blurb = N_("T_o Do"),
  },
  {
    .feature = GOA_PROVIDER_FEATURE_INVALID,
    .property = NULL,
    .blurb = NULL,
  }
};

static void
goa_provider_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
    switch (property_id) {
    case PROP_PRESEED_DATA:
        g_value_set_variant (value, NULL);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_provider_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
    switch (property_id) {
    case PROP_PRESEED_DATA:
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
goa_provider_init (GoaProvider *self)
{
}

static void
goa_provider_class_init (GoaProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = goa_provider_set_property;
  object_class->get_property = goa_provider_get_property;

  klass->add_account_finish = goa_provider_real_add_account_finish;
  klass->build_object = goa_provider_build_object_real;
  klass->ensure_credentials_sync = goa_provider_ensure_credentials_sync_real;
  klass->get_credentials_generation = goa_provider_get_credentials_generation_real;
  klass->get_provider_icon = goa_provider_get_provider_icon_real;
  klass->refresh_account_finish = goa_provider_real_refresh_account_finish;
  klass->remove_account = goa_provider_remove_account_real;
  klass->remove_account_finish = goa_provider_remove_account_finish_real;
  klass->show_account = goa_provider_real_show_account;
  klass->show_account_finish = goa_provider_real_show_account_finish;

/**
 * GoaProvider:preseed-data
 *
 * An #GVariant of type a{sv} storing any information already collected that
 * can be useful when creating a new account. For instance, this can be useful
 * to reuse the HTTP cookies from an existing browser session to skip the
 * prompt for username and password in the OAuth2-based providers by passing
 * a #GVariant with the following contents:
 *
 * <informalexample>
 * <programlisting>
 * {
 *   "cookies": [
 *     {
 *       "domain": "example.com",
 *       "name": "LSID",
 *       "value": "asdfasdfasdf"
 *     },
 *     {
 *       "domain": "accounts.example.com",
 *       "name": "SSID",
 *       "value": "asdfasdfasdf"
 *     }
 *   ]
 * }
 * </programlisting>
 * </informalexample>
 *
 * Unknown or unsupported keys will be ignored by providers.
 *
 * Deprecated: 3.28: This property does nothing.
 */
  properties[PROP_PRESEED_DATA] =
    g_param_spec_variant ("preseed-data",
        "Collected data to pre-seed account creation",
        "A a{sv} #GVariant containing a provider-type specific set of data that"
        "can be useful during account creation (eg. http cookies from an existing"
        "browser session or the entrypoint url for self-hosted services).",
        G_VARIANT_TYPE_VARDICT,
        NULL,
        G_PARAM_DEPRECATED | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, NUM_PROPERTIES, properties);
}

/**
 * goa_provider_get_provider_type:
 * @self: A #GoaProvider.
 *
 * Gets the type of @self.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer none): A string owned by @self, do not free.
 */
const gchar *
goa_provider_get_provider_type (GoaProvider *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), NULL);
  return GOA_PROVIDER_GET_CLASS (self)->get_provider_type (self);
}

/**
 * goa_provider_get_provider_name:
 * @self: A #GoaProvider.
 * @object: (allow-none): A #GoaObject for an account.
 *
 * Gets a name for @self and @object that is suitable for display
 * in an user interface. The returned value may depend on @object (if
 * it's not %NULL) - for example, hosted accounts might return a
 * different name.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: (transfer full): A string that should be freed with g_free().
 */
gchar *
goa_provider_get_provider_name (GoaProvider *self,
                                GoaObject   *object)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), NULL);
  return GOA_PROVIDER_GET_CLASS (self)->get_provider_name (self, object);
}

/**
 * goa_provider_get_provider_icon:
 * @self: A #GoaProvider.
 * @object: A #GoaObject for an account.
 *
 * Gets an icon for @self and @object that is suitable for display
 * in an user interface. The returned value may depend on @object -
 * for example, hosted accounts might return a different icon.
 *
 * This is a virtual method with a default implementation that returns
 * a #GThemedIcon with fallbacks constructed from the name
 * <literal>goa-account-TYPE</literal> where <literal>TYPE</literal>
 * is the return value of goa_provider_get_provider_type().
 *
 * Returns: (transfer full): An icon that should be freed with g_object_unref().
 */
GIcon *
goa_provider_get_provider_icon (GoaProvider *self,
                                GoaObject   *object)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), NULL);
  return GOA_PROVIDER_GET_CLASS (self)->get_provider_icon (self, object);
}

static GIcon *
goa_provider_get_provider_icon_real (GoaProvider *self,
                                     GoaObject   *object)
{
  GIcon *ret;
  gchar *s;
  s = g_strdup_printf ("goa-account-%s", goa_provider_get_provider_type (self));
  ret = g_themed_icon_new_with_default_fallbacks (s);
  g_free (s);
  return ret;
}

/**
 * goa_provider_get_provider_group:
 * @self: A #GoaProvider.
 *
 * Gets the group to which @self belongs that is suitable for
 * organizing the providers while displaying them in an user
 * interface.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: A #GoaProviderGroup.
 *
 * Since: 3.8
 *
 * Deprecated: 3.10: Use goa_provider_get_provider_features() instead.
 */
GoaProviderGroup
goa_provider_get_provider_group (GoaProvider *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), GOA_PROVIDER_GROUP_INVALID);
  return GOA_PROVIDER_GET_CLASS (self)->get_provider_group (self);
}

static const gchar *
goa_get_feature_alias (GoaProviderFeatures feature)
{
  switch (feature) {
  case GOA_PROVIDER_FEATURE_MAIL:
    return "mail";
  case GOA_PROVIDER_FEATURE_CALENDAR:
    return "calendar";
  case GOA_PROVIDER_FEATURE_CONTACTS:
    return "contacts";
  case GOA_PROVIDER_FEATURE_CHAT:
    return "chat";
  case GOA_PROVIDER_FEATURE_DOCUMENTS:
    return "documents";
  case GOA_PROVIDER_FEATURE_PHOTOS:
    return "photos";
  case GOA_PROVIDER_FEATURE_FILES:
    return "files";
  case GOA_PROVIDER_FEATURE_TICKETING:
    return "ticketing";
  case GOA_PROVIDER_FEATURE_READ_LATER:
    return "read-later";
  case GOA_PROVIDER_FEATURE_PRINTERS:
    return "printers";
  case GOA_PROVIDER_FEATURE_MAPS:
    return "maps";
  case GOA_PROVIDER_FEATURE_MUSIC:
    return "music";
  case GOA_PROVIDER_FEATURE_TODO:
    return "todo";
  case GOA_PROVIDER_FEATURE_BRANDED:
  case GOA_PROVIDER_FEATURE_INVALID:
    break;
  }
  return NULL;
}

/**
 * goa_util_open_goa_conf:
 *
 * Reads goa.conf file from the system config directory and
 * returns it for use for example by goa_util_provider_feature_is_enabled().
 * It returns %NULL, when the file cannot be opened.
 *
 * Free the returned #GKeyFile with g_key_file_free(), when no longer needed.
 *
 * Returns: (nullable) (transfer full): a new #GKeyFile containing goa.conf
 *    file, or %NULL, when it cannot be opened.
 *
 * Since: 3.52
 */
GKeyFile *
goa_util_open_goa_conf (void)
{
  GKeyFile *goa_conf;
  GError *error = NULL;

  goa_conf = g_key_file_new ();
  if (!g_key_file_load_from_file (goa_conf, GOA_CONF_FILENAME, G_KEY_FILE_NONE, &error))
    {
      g_debug ("Failed to load '%s': %s", GOA_CONF_FILENAME, error ? error->message : "Unknown error");
      g_clear_error (&error);
      g_key_file_free (goa_conf);
      goa_conf = NULL;
    }

  return goa_conf;
}

/**
 * goa_util_provider_feature_is_enabled:
 * @goa_conf: (nullable): a #GKeyFile with loaded goa.conf file, or %NULL
 * @provider_type: a provider type string
 * @feature: a feature to check, one of %GoaProviderFeatures
 *
 * Checks in the @goa_conf, whether the @provider_type can use
 * the @feature. The @goa_conf is a %GKeyFile returned by
 * goa_util_open_goa_conf(), it can be %NULL, in which case
 * the @feature is considered enabled.
 *
 * Returns: %TRUE, when the @feature is enabled, %FALSE otherwise
 *
 * Since: 3.52
 */
gboolean
goa_util_provider_feature_is_enabled (GKeyFile            *goa_conf,
                                      const gchar         *provider_type,
                                      GoaProviderFeatures  feature)
{
  GError *error = NULL;
  const gchar *feature_alias;
  gboolean enabled;

  if (!goa_conf)
    return TRUE;

  g_return_val_if_fail (provider_type != NULL, TRUE);

  feature_alias = goa_get_feature_alias (feature);
  g_return_val_if_fail (feature_alias != NULL, TRUE);

  enabled = g_key_file_get_boolean (goa_conf, provider_type, feature_alias, &error);
  if (error)
    {
      g_clear_error (&error);
      enabled = g_key_file_get_boolean (goa_conf, "all", feature_alias, &error);
      if (error)
        {
          g_clear_error (&error);
          enabled = TRUE;
        }
    }

  return enabled;
}

static GoaProviderFeatures
goa_provider_filter_features (GoaProvider         *self,
                              GoaProviderFeatures  features)
{
  GKeyFile *goa_conf;
  const gchar *provider_type;
  guint i;

  goa_conf = goa_util_open_goa_conf ();
  if (!goa_conf)
    return features;

  provider_type = goa_provider_get_provider_type (self);

  for (i = 0; provider_features_info[i].property != NULL; i++)
    {
      GoaProviderFeatures feature = provider_features_info[i].feature;
      if ((features & feature) != 0 &&
          !goa_util_provider_feature_is_enabled (goa_conf, provider_type, feature))
        {
          features = features & (~feature);
        }
    }

  g_key_file_free (goa_conf);

  return features;
}

/**
 * goa_provider_get_provider_features:
 * @self: A #GoaProvider.
 *
 * Get the features bitmask (eg. %GOA_PROVIDER_FEATURE_CHAT|%GOA_PROVIDER_FEATURE_CONTACTS)
 * supported by the provider.
 *
 * Returns: The #GoaProviderFeatures bitmask with the provided features.
 *
 * Since: 3.10
 */
GoaProviderFeatures
goa_provider_get_provider_features (GoaProvider *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), GOA_PROVIDER_FEATURE_INVALID);
  g_return_val_if_fail (GOA_PROVIDER_GET_CLASS (self)->get_provider_features != NULL, GOA_PROVIDER_FEATURE_INVALID);
  return goa_provider_filter_features (self, GOA_PROVIDER_GET_CLASS (self)->get_provider_features (self));
}

/*< private >
 * goa_provider_get_provider_features_infos:
 *
 * Get the list of `GoaProviderFeaturesInfo` structs.
 *
 * The order of features in the array reflects the order that should be used
 * by user interfaces.
 *
 * It ends with an element with `feature` set to `GOA_PROVIDER_FEATURE_INVALID`.
 *
 * Returns: (array) (element-type Goa.ProviderFeatureInfo): an array of structs
 */
GoaProviderFeaturesInfo *
goa_provider_get_provider_features_infos (void)
{
  return provider_features_info;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_add_account: (vfunc add_account)
 * @self: a `GoaProvider`
 * @client: a `GoaClient`
 * @parent: (nullable): a `GtkWidget`
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * This method brings up the user interface necessary to create a new
 * account on @client of the type for @self, interacts with the
 * user to get all information needed and creates the account.
 *
 * Call [method@Goa.Provider.add_account_finish] to get the result.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 */
void
goa_provider_add_account (GoaProvider         *self,
                          GoaClient           *client,
                          GtkWidget           *parent,
                          GCancellable        *cancellable,
                          GAsyncReadyCallback  callback,
                          gpointer             user_data)
{
  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (GOA_IS_CLIENT (client));
  g_return_if_fail (parent == NULL || GTK_IS_WIDGET (parent));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  GOA_PROVIDER_GET_CLASS (self)->add_account (self,
                                              client,
                                              parent,
                                              cancellable,
                                              callback,
                                              user_data);
}

static GoaObject *
goa_provider_real_add_account_finish (GoaProvider   *provider,
                                      GAsyncResult  *result,
                                      GError       **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * goa_provider_add_account_finish: (vfunc add_account_finish)
 * @provider: a `GoaProvider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.Provider.add_account].
 *
 * If an account was successfully created, a #GoaObject for the
 * created account is returned. If @dialog is dismissed, %NULL is
 * returned and @error is set to %GOA_ERROR_DIALOG_DISMISSED. If an
 * account couldn't be created then @error is set. In some cases,
 * for example, when the credentials could not be stored in the
 * keyring, a #GoaObject can be returned even if @error is set.
 *
 * The caller will always show an error dialog if @error is set unless
 * the error is %GOA_ERROR_DIALOG_DISMISSED.
 *
 * Returns: (transfer full): a `GoaObject`, or %NULL with @error set
 */
GoaObject *
goa_provider_add_account_finish (GoaProvider   *provider,
                                 GAsyncResult  *result,
                                 GError       **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), NULL);
  g_return_val_if_fail (g_task_is_valid (result, provider), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return GOA_PROVIDER_GET_CLASS (provider)->add_account_finish (provider,
                                                                result,
                                                                error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_refresh_account:
 * @self: a `GoaProvider`
 * @client: a `GoaClient`
 * @object: A `GoaObject` with a `GoaAccount` interface
 * @parent: (nullable): a `GtkWidget`
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * This method brings up the user interface necessary for refreshing
 * the credentials for the account specified by @object. This
 * typically involves having the user log in to the account again.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 */
void
goa_provider_refresh_account (GoaProvider         *self,
                              GoaClient           *client,
                              GoaObject           *object,
                              GtkWidget           *parent,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (GOA_IS_CLIENT (client));
  g_return_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL);
  g_return_if_fail (parent == NULL || GTK_IS_WIDGET (parent));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  return GOA_PROVIDER_GET_CLASS (self)->refresh_account (self,
                                                         client,
                                                         object,
                                                         parent,
                                                         cancellable,
                                                         callback,
                                                         user_data);
}

static gboolean
goa_provider_real_refresh_account_finish (GoaProvider   *provider,
                                          GAsyncResult  *result,
                                          GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * goa_provider_refresh_account_finish: (vfunc refresh_account_finish)
 * @provider: a `GoaProvider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.Provider.refresh_account].
 *
 * The caller will always show an error dialog if @error is set unless
 * the error is %GOA_ERROR_DIALOG_DISMISSED.
 *
 * Returns: (transfer full): a `GoaObject`, or %NULL with @error set
 */
gboolean
goa_provider_refresh_account_finish (GoaProvider   *provider,
                                     GAsyncResult  *result,
                                     GError       **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, provider), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);


  return GOA_PROVIDER_GET_CLASS (provider)->refresh_account_finish (provider,
                                                                    result,
                                                                    error);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_provider_real_show_account (GoaProvider         *self,
                                GoaClient           *client,
                                GoaObject           *object,
                                GtkWidget           *parent,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
  GoaProviderDialog *dialog;
  g_autoptr(GTask) task = NULL;

  dialog = goa_provider_dialog_new (self, client, parent);
  goa_provider_dialog_push_account (dialog, object, NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_check_cancellable (task, FALSE);
  g_task_set_source_tag (task, goa_provider_real_show_account);
  goa_provider_task_run_in_dialog (task, dialog);
}

/**
 * goa_provider_show_account: (vfunc show_account)
 * @self: a `GoaProvider`
 * @client: a `GoaClient`
 * @object: A `GoaObject` with a `GoaAccount` interface
 * @parent: (nullable): a `GtkWidget`
 * @cancellable: (nullable): a `GCancellable`
 * @callback: (scope async): a `GAsyncReadyCallback`
 * @user_data: (closure): user supplied data
 *
 * Method to display a dialog for an enrolled account.
 *
 * The default implementation creates a dialog with a branded header and
 * toggles for each `GoaProviderFeatures` supported by the provider.
 */
void
goa_provider_show_account (GoaProvider         *self,
                           GoaClient           *client,
                           GoaObject           *object,
                           GtkWidget           *parent,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (GOA_IS_CLIENT (client));
  g_return_if_fail (GOA_IS_OBJECT (object) && goa_object_peek_account (object) != NULL);
  g_return_if_fail (parent == NULL || GTK_IS_WIDGET (parent));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  return GOA_PROVIDER_GET_CLASS (self)->show_account (self,
                                                      client,
                                                      object,
                                                      parent,
                                                      cancellable,
                                                      callback,
                                                      user_data);
}

static gboolean
goa_provider_real_show_account_finish (GoaProvider   *provider,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * goa_provider_show_account_finish: (vfunc show_account_finish)
 * @provider: a `GoaProvider`
 * @result: a `GAsyncResult`
 * @error: (nullable): a `GError`
 *
 * Finish an operation started with [method@Goa.Provider.show_account].
 *
 * Returns: (transfer full): a `GoaObject`, or %NULL with @error set
 */
gboolean
goa_provider_show_account_finish (GoaProvider   *provider,
                                  GAsyncResult  *result,
                                  GError       **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (provider), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, provider), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return GOA_PROVIDER_GET_CLASS (provider)->show_account_finish (provider,
                                                                 result,
                                                                 error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_build_object:
 * @self: A #GoaProvider.
 * @object: The #GoaObjectSkeleton that is being built.
 * @key_file: The #GKeyFile with configuation data.
 * @group: The group in @key_file to get data from.
 * @connection: The #GDBusConnection used by the daemon to connect to the message bus.
 * @just_added: Whether the account was newly created or being updated.
 * @error: Return location for error or %NULL.
 *
 * This method is called when construction account #GoaObject<!-- -->
 * from configuration data - it basically provides a way to add
 * provider-specific information.
 *
 * The passed in @object will have a #GoaAccount interface
 * set. Implementations should validate and use data from @key_file to
 * add more interfaces to @object.
 *
 * Note that this may be called on already exported objects - for
 * example on configuration files reload.
 *
 * This is a pure virtual method - a subclass must provide an
 * implementation.
 *
 * Returns: %TRUE if data was valid, %FALSE if @error is set.
 */
gboolean
goa_provider_build_object (GoaProvider         *self,
                           GoaObjectSkeleton   *object,
                           GKeyFile            *key_file,
                           const gchar         *group,
                           GDBusConnection     *connection,
                           gboolean             just_added,
                           GError             **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT_SKELETON (object) && goa_object_peek_account (GOA_OBJECT (object)) != NULL, FALSE);
  g_return_val_if_fail (key_file != NULL, FALSE);
  g_return_val_if_fail (group != NULL, FALSE);
  g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return GOA_PROVIDER_GET_CLASS (self)->build_object (self,
                                                      object,
                                                      key_file,
                                                      group,
                                                      connection,
                                                      just_added,
                                                      error);
}

/* ---------------------------------------------------------------------------------------------------- */

typedef struct
{
  GoaObject *object;
  gint expires_in;
} EnsureCredentialsData;

static EnsureCredentialsData *
ensure_credentials_data_new (GoaObject *object)
{
  EnsureCredentialsData *data;
  data = g_new0 (EnsureCredentialsData, 1);
  data->object = g_object_ref (object);
  return data;
}

static void
ensure_credentials_data_free (EnsureCredentialsData *data)
{
  g_object_unref (data->object);
  g_free (data);
}

static void
ensure_credentials_in_thread_func (GTask              *task,
                                   gpointer            object,
                                   gpointer            task_data,
                                   GCancellable       *cancellable)
{
  EnsureCredentialsData *data;
  GError *error;

  data = task_data;

  error = NULL;
  if (!goa_provider_ensure_credentials_sync (GOA_PROVIDER (object),
                                             data->object,
                                             &data->expires_in,
                                             cancellable,
                                             &error))
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, TRUE);
}


/**
 * goa_provider_ensure_credentials:
 * @self: A #GoaProvider.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Ensures that credentials for @object are still valid.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this function was called from. You can then call
 * goa_provider_ensure_credentials_finish() to get the result
 * of the operation.
 *
 * This is a virtual method where the default implementation simply
 * throws the %GOA_ERROR_NOT_SUPPORTED error. A subclass may provide
 * another implementation.
 */
void
goa_provider_ensure_credentials (GoaProvider          *self,
                                 GoaObject            *object,
                                 GCancellable         *cancellable,
                                 GAsyncReadyCallback   callback,
                                 gpointer              user_data)
{
  GTask *task;

  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_task_data (task, ensure_credentials_data_new (object), (GDestroyNotify) ensure_credentials_data_free);
  g_task_set_source_tag (task, goa_provider_ensure_credentials);
  g_task_run_in_thread (task, ensure_credentials_in_thread_func);

  g_object_unref (task);
}

/**
 * goa_provider_ensure_credentials_finish:
 * @self: A #GoaProvider.
 * @out_expires_in: (out): Return location for how long the expired credentials are good for (0 if unknown) or %NULL.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_provider_ensure_credentials().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_provider_ensure_credentials().
 *
 * Returns: %TRUE if the credentials for the passed #GoaObject are valid, %FALSE if @error is set.
 */
gboolean
goa_provider_ensure_credentials_finish (GoaProvider         *self,
                                        gint                *out_expires_in,
                                        GAsyncResult        *res,
                                        GError             **error)
{
  GTask *task;
  gboolean had_error;

  gboolean ret;
  EnsureCredentialsData *data;

  ret = FALSE;

  g_return_val_if_fail (GOA_IS_PROVIDER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_provider_ensure_credentials, FALSE);

  /* Workaround for bgo#764163 */
  had_error = g_task_had_error (task);
  ret = g_task_propagate_boolean (task, error);
  if (had_error)
    goto out;

  data = g_task_get_task_data (task);
  if (out_expires_in != NULL)
    *out_expires_in = data->expires_in;

 out:
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_ensure_credentials_sync:
 * @self: A #GoaProvider.
 * @object: A #GoaObject with a #GoaAccount interface.
 * @out_expires_in: (out): Return location for how long the expired credentials are good for (0 if unknown) or %NULL.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Like goa_provider_ensure_credentials() but blocks the
 * calling thread until an answer is received.
 *
 * Returns: %TRUE if the credentials for the passed #GoaObject are valid, %FALSE if @error is set.
 */
gboolean
goa_provider_ensure_credentials_sync (GoaProvider     *self,
                                      GoaObject       *object,
                                      gint            *out_expires_in,
                                      GCancellable    *cancellable,
                                      GError         **error)
{
  GoaAccount *account = NULL;
  GoaProviderFeatures features;
  gboolean disabled = TRUE;
  gboolean ret = FALSE;
  guint i;

  g_return_val_if_fail (GOA_IS_PROVIDER (self), FALSE);
  g_return_val_if_fail (GOA_IS_OBJECT (object), FALSE);
  g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  account = goa_object_get_account (object);
  g_return_val_if_fail (GOA_IS_ACCOUNT (account), FALSE);

  features = goa_provider_get_provider_features (self);

  for (i = 0; provider_features_info[i].property != NULL; i++)
    {
      if ((features & provider_features_info[i].feature) != 0)
        {
          gboolean feature_disabled;

          g_object_get (account, provider_features_info[i].property, &feature_disabled, NULL);
          disabled = disabled && feature_disabled;
          if (!disabled)
            break;
        }
    }

  if (disabled)
    {
      g_set_error_literal (error, GOA_ERROR, GOA_ERROR_NOT_SUPPORTED, _("Account is disabled"));
      goto out;
    }

  ret = GOA_PROVIDER_GET_CLASS (self)->ensure_credentials_sync (self, object, out_expires_in, cancellable, error);

 out:
  if (!ret && error != NULL && *error == NULL)
    {
      const gchar *provider_type;

      provider_type = goa_provider_get_provider_type (self);
      g_critical ("GoaProvider (%s) failed to set error correctly", provider_type);
      g_set_error_literal (error, GOA_ERROR, GOA_ERROR_NOT_AUTHORIZED, _("Unknown error"));
    }

  g_clear_object (&account);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static gboolean
goa_provider_ensure_credentials_sync_real (GoaProvider   *self,
                                           GoaObject     *object,
                                           gint          *out_expires_in,
                                           GCancellable  *cancellable,
                                           GError       **error)
{
  g_set_error (error,
               GOA_ERROR,
               GOA_ERROR_NOT_SUPPORTED,
               _("ensure_credentials_sync is not implemented on type %s"),
               g_type_name (G_TYPE_FROM_INSTANCE (self)));
  return FALSE;
}

static gboolean
goa_provider_build_object_real (GoaProvider         *self,
                                GoaObjectSkeleton   *object,
                                GKeyFile            *key_file,
                                const gchar         *group,
                                GDBusConnection     *connection,
                                gboolean             just_added,
                                GError             **error)
{
  /* does nothing */
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_get_credentials_generation:
 * @self: A #GoaProvider.
 *
 * Gets the generation of credentials being used for the provider.
 *
 * Implementations should bump this number when changes are introduced
 * that may render existing credentials unusable.
 *
 * For example, if an additional scope is requested (e.g. access to
 * contacts data) while obtaining credentials, then this number needs
 * to be bumped since existing credentials are not good for the added
 * scope.
 *
 * This is a virtual method where the default implementation returns
 * 0.
 *
 * Returns: The current generation of credentials.
 */
guint
goa_provider_get_credentials_generation (GoaProvider *self)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), 0);
  return GOA_PROVIDER_GET_CLASS (self)->get_credentials_generation (self);
}

static guint
goa_provider_get_credentials_generation_real (GoaProvider *self)
{
  return 0;
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_provider_ensure_extension_points_registered (void)
{
  static gsize once_init_value = 0;

  if (g_once_init_enter (&once_init_value))
    {
      GIOExtensionPoint *extension_point;

      extension_point = g_io_extension_point_register (GOA_PROVIDER_EXTENSION_POINT_NAME);
      g_io_extension_point_set_required_type (extension_point, GOA_TYPE_PROVIDER);

      g_once_init_leave (&once_init_value, 1);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

static struct
{
  const gchar *name;
  GType (*get_type) (void);
} ordered_builtins_map[] = {
  /* The order in which the providers' types are created is
   * important because it affects the order in which they are
   * returned by goa_provider_get_all.
   */
#ifdef GOA_GOOGLE_ENABLED
  { GOA_GOOGLE_NAME, goa_google_provider_get_type },
#endif
#ifdef GOA_OWNCLOUD_ENABLED
  { GOA_OWNCLOUD_NAME, goa_owncloud_provider_get_type },
#endif
#ifdef GOA_EXCHANGE_ENABLED
  { GOA_EXCHANGE_NAME, goa_exchange_provider_get_type },
#endif
#ifdef GOA_FEDORA_ENABLED
  { GOA_FEDORA_NAME, goa_fedora_provider_get_type },
#endif
#ifdef GOA_IMAP_SMTP_ENABLED
  { GOA_IMAP_SMTP_NAME, goa_imap_smtp_provider_get_type },
#endif
#ifdef GOA_WEBDAV_ENABLED
  { GOA_WEBDAV_NAME, goa_webdav_provider_get_type },
#endif
#ifdef GOA_KERBEROS_ENABLED
  { GOA_KERBEROS_NAME, goa_kerberos_provider_get_type },
#endif
#ifdef GOA_MS_GRAPH_ENABLED
  { GOA_MS_GRAPH_NAME, goa_ms_graph_provider_get_type },
#endif
  { NULL, NULL }
};

void
goa_provider_ensure_builtins_loaded (void)
{
  static gsize once_init_value = 0;

  goa_provider_ensure_extension_points_registered ();

  if (g_once_init_enter (&once_init_value))
    {
      GKeyFile *goa_conf;
      gchar **whitelisted_providers = NULL;
      guint i;
      guint j;
      gboolean all = FALSE;

      goa_conf = goa_util_open_goa_conf ();
      if (goa_conf)
        {
          whitelisted_providers = g_key_file_get_string_list (goa_conf, "providers", "enable", NULL, NULL);
          if (whitelisted_providers && !*whitelisted_providers)
            g_clear_pointer (&whitelisted_providers, g_strfreev);

          g_clear_pointer (&goa_conf, g_key_file_free);
        }

      /* Default to enabling all providers */
      if (whitelisted_providers == NULL)
        {
          whitelisted_providers = g_new0 (gchar *, 2);
          whitelisted_providers[0] = g_strdup ("all");
          whitelisted_providers[1] = NULL;
        }

      /* Enable everything if there is 'all'. */
      for (i = 0; whitelisted_providers[i] != NULL; i++)
        {
          if (g_strcmp0 (whitelisted_providers[i], "all") == 0)
            {
              g_debug ("Loading all providers: ");
              for (j = 0; ordered_builtins_map[j].name != NULL; j++)
                {
                  g_debug (" - %s", ordered_builtins_map[j].name);
                  g_type_ensure ((*ordered_builtins_map[j].get_type) ());
                }

              all = TRUE;
              break;
            }
        }

      if (all)
        goto cleanup;

      /* Otherwise try them one by one. */
      g_debug ("Loading whitelisted providers: ");
      for (i = 0; ordered_builtins_map[i].name != NULL; i++)
        {
          for (j = 0; whitelisted_providers[j] != NULL; j++)
            {
              if (g_strcmp0 (whitelisted_providers[j], ordered_builtins_map[i].name) == 0)
                {
                  g_debug (" - %s", ordered_builtins_map[j].name);
                  g_type_ensure ((*ordered_builtins_map[i].get_type) ());
                  break;
                }
            }
        }

    cleanup:
      g_strfreev (whitelisted_providers);
      g_once_init_leave (&once_init_value, 1);
    }
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_get_for_provider_type:
 * @provider_type: A provider type.
 *
 * Looks up the %GOA_PROVIDER_EXTENSION_POINT_NAME extension
 * point and returns a newly created #GoaProvider for
 * @provider_type, if any.
 *
 * Returns: (transfer full): A #GoaProvider (that must be freed
 * with g_object_unref()) or %NULL if not found.
 */
GoaProvider *
goa_provider_get_for_provider_type (const gchar *provider_type)
{
  GIOExtension *extension;
  GIOExtensionPoint *extension_point;
  GoaProvider *ret;

  g_return_val_if_fail (provider_type != NULL, NULL);

  goa_provider_ensure_builtins_loaded ();

  ret = NULL;

  extension_point = g_io_extension_point_lookup (GOA_PROVIDER_EXTENSION_POINT_NAME);
  extension = g_io_extension_point_get_extension_by_name (extension_point, provider_type);
  if (extension != NULL)
    ret = GOA_PROVIDER (g_object_new (g_io_extension_get_type (extension), NULL));
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

static void
free_list_and_unref (gpointer data)
{
  g_list_free_full (data, g_object_unref);
}

/**
 * goa_provider_get_all:
 * @callback: The function to call when the request is satisfied.
 * @user_data: Pointer to pass to @callback.
 *
 * Creates a list of all the available #GoaProvider instances.
 *
 * When the result is ready, @callback will be called in the the <link
 * linkend="g-main-context-push-thread-default">thread-default main
 * loop</link> this function was called from. You can then call
 * goa_provider_get_all_finish() to get the result of the operation.
 *
 * See goa_provider_get_for_provider_type() for details on how the providers
 * are found.
 */
void
goa_provider_get_all (GAsyncReadyCallback callback,
                      gpointer            user_data)
{
  GList *extensions;
  GList *providers = NULL;
  GList *l;
  GIOExtensionPoint *extension_point;
  GTask *task = NULL;

  goa_provider_ensure_builtins_loaded ();

  task = g_task_new (NULL, NULL, callback, user_data);
  g_task_set_source_tag (task, goa_provider_get_all);

  extension_point = g_io_extension_point_lookup (GOA_PROVIDER_EXTENSION_POINT_NAME);
  extensions = g_io_extension_point_get_extensions (extension_point);
  /* TODO: what if there are two extensions with the same name? */
  for (l = extensions; l != NULL; l = l->next)
    {
      GIOExtension *extension = l->data;
      /* The extensions are loaded in the reverse order we used in
       * goa_provider_ensure_builtins_loaded, so we need to push
       * extension if front of the already loaded ones. */
      providers = g_list_prepend (providers, g_object_new (g_io_extension_get_type (extension), NULL));
    }

  g_task_return_pointer (task, g_steal_pointer (&providers), free_list_and_unref);
  g_object_unref (task);
}

/**
 * goa_provider_get_all_finish:
 * @out_providers: (out) (transfer full) (element-type GoaProvider):
 * Return location for a list of #GoaProvider instances.
 * @res: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to goa_provider_get_all().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with goa_provider_get_all().
 *
 * Returns: %TRUE if the list was successfully retrieved, %FALSE if @error is set.
 */
gboolean
goa_provider_get_all_finish (GList        **out_providers,
                             GAsyncResult  *result,
                             GError       **error)
{
  GTask *task;
  GList *providers;
  gboolean had_error;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
  task = G_TASK (result);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_provider_get_all, FALSE);

  /* Workaround for bgo#764163 */
  had_error = g_task_had_error (task);
  providers = g_task_propagate_pointer (task, error);
  if (had_error)
    return FALSE;

  if (out_providers != NULL)
    {
      *out_providers = providers;
      providers = NULL;
    }

  g_list_free_full (providers, g_object_unref);
  return TRUE;
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_provider_remove_account (GoaProvider          *self,
                             GoaObject            *object,
                             GCancellable         *cancellable,
                             GAsyncReadyCallback   callback,
                             gpointer              user_data)
{
  g_return_if_fail (GOA_IS_PROVIDER (self));
  g_return_if_fail (GOA_IS_OBJECT (object));
  g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

  GOA_PROVIDER_GET_CLASS (self)->remove_account (self, object, cancellable, callback, user_data);
}

gboolean
goa_provider_remove_account_finish (GoaProvider   *self,
                                    GAsyncResult  *res,
                                    GError       **error)
{
  g_return_val_if_fail (GOA_IS_PROVIDER (self), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return GOA_PROVIDER_GET_CLASS (self)->remove_account_finish (self, res, error);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
goa_provider_remove_account_real (GoaProvider          *self,
                                  GoaObject            *object,
                                  GCancellable         *cancellable,
                                  GAsyncReadyCallback   callback,
                                  gpointer              user_data)
{
  GTask *task;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, goa_provider_remove_account_real);
  g_task_return_boolean (task, TRUE);
  g_object_unref (task);
}

static gboolean
goa_provider_remove_account_finish_real (GoaProvider   *self,
                                         GAsyncResult  *res,
                                         GError       **error)
{
  GTask *task;

  g_return_val_if_fail (g_task_is_valid (res, self), FALSE);
  task = G_TASK (res);

  g_return_val_if_fail (g_task_get_source_tag (task) == goa_provider_remove_account_real, FALSE);

  return g_task_propagate_boolean (task, error);
}

/* ---------------------------------------------------------------------------------------------------- */

/**
 * goa_provider_set_preseed_data:
 * @self: The #GoaProvider
 * @preseed_data: A #GVariant of type a{sv}
 *
 * Sets the #GoaProvider:preseed-data property to feed any information already
 * collected that can be useful when creating a new account.
 *
 * If the @preseed_data #GVariant is floating, it is consumed to allow
 * 'inline' use of the g_variant_new() family of functions.
 *
 * Deprecated: 3.28: This function does nothing.
 */
void
goa_provider_set_preseed_data (GoaProvider *self,
                               GVariant    *preseed_data)
{
}

/**
 * goa_provider_get_preseed_data:
 * @self: The #GoaProvider
 *
 * Gets the #GVariant set through the #GoaProvider:preseed-data property.
 *
 * Returns: (transfer none): A #GVariant that is known to be valid until
 *   the property is overridden or the provider freed.
 *
 * Deprecated: 3.28: This function does nothing.
 */
GVariant *
goa_provider_get_preseed_data (GoaProvider *self)
{
  return NULL;
}

/* ---------------------------------------------------------------------------------------------------- */

gchar *
goa_util_lookup_keyfile_string (GoaObject    *object,
                                const gchar  *key)
{
  GoaAccount *account;
  GError *error;
  GKeyFile *key_file;
  gchar *path;
  gchar *group;
  gchar *ret;

  ret = NULL;

  account = goa_object_peek_account (object);
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }
  ret = g_key_file_get_string (key_file,
                               group,
                               key,
                               &error);
  if (ret == NULL)
    {
      /* this is not fatal (think upgrade-path) */
      g_debug ("Error getting value for key %s in group `%s' from keyfile %s: %s (%s, %d)",
               key,
               group,
               path,
               error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_key_file_unref (key_file);
  g_free (group);
  g_free (path);
  return ret;
}

gboolean
goa_util_lookup_keyfile_boolean (GoaObject    *object,
                                 const gchar  *key)
{
  GoaAccount *account;
  GError *error;
  GKeyFile *key_file;
  gchar *path;
  gchar *group;
  gboolean ret;

  ret = FALSE;

  account = goa_object_peek_account (object);
  path = g_strdup_printf ("%s/goa-1.0/accounts.conf", g_get_user_config_dir ());
  group = g_strdup_printf ("Account %s", goa_account_get_id (account));

  key_file = g_key_file_new ();
  error = NULL;
  if (!g_key_file_load_from_file (key_file,
                                  path,
                                  G_KEY_FILE_NONE,
                                  &error))
    {
      g_warning ("Error loading keyfile %s: %s (%s, %d)",
                 path,
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }
  ret = g_key_file_get_boolean (key_file,
                                group,
                                key,
                                &error);
  if (error != NULL)
    {
      /* this is not fatal (think upgrade-path) */
      g_debug ("Error getting boolean value for key %s in group `%s' from keyfile %s: %s (%s, %d)",
               key,
               group,
               path,
               error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
      goto out;
    }

 out:
  g_key_file_unref (key_file);
  g_free (group);
  g_free (path);
  return ret;
}

/* ---------------------------------------------------------------------------------------------------- */

void
goa_util_account_notify_property_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
  GoaAccount *account;
  gboolean value;
  const gchar *key;
  const gchar *name;

  g_return_if_fail (GOA_IS_ACCOUNT (object));

  account = GOA_ACCOUNT (object);
  key = user_data;
  name = g_param_spec_get_name (pspec);

  g_object_get (account, name, &value, NULL);
  goa_utils_keyfile_set_boolean (account, key, !value);
}
