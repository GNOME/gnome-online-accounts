/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat, Inc.
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

#include "goaidentity.h"
#include "goakerberosidentity.h"
#include "goakerberosidentityinquiry.h"
#include "goaalarm.h"

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <string.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

typedef enum
{
  VERIFICATION_LEVEL_UNVERIFIED,
  VERIFICATION_LEVEL_ERROR,
  VERIFICATION_LEVEL_EXISTS,
  VERIFICATION_LEVEL_SIGNED_IN
} VerificationLevel;

struct _GoaKerberosIdentityPrivate
{
  krb5_context kerberos_context;
  krb5_ccache  credentials_cache;

  char *identifier;
  guint identifier_idle_id;

  krb5_timestamp expiration_time;
  guint          expiration_time_idle_id;

  GoaAlarm     *expiration_alarm;
  GoaAlarm     *expiring_alarm;
  GoaAlarm     *renewal_alarm;

  VerificationLevel cached_verification_level;
  guint             is_signed_in_idle_id;
};

enum
{
  EXPIRING,
  EXPIRED,
  UNEXPIRED,
  NEEDS_RENEWAL,
  NEEDS_REFRESH,
  NUMBER_OF_SIGNALS,
};

enum
{
  PROP_0,
  PROP_IDENTIFIER,
  PROP_IS_SIGNED_IN,
  PROP_EXPIRATION_TIMESTAMP
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

static void identity_interface_init (GoaIdentityInterface *interface);
static void initable_interface_init (GInitableIface *interface);
static void reset_alarms (GoaKerberosIdentity *self);
static void clear_alarms (GoaKerberosIdentity *self);
static gboolean goa_kerberos_identity_is_signed_in (GoaIdentity *identity);
static void set_error_from_krb5_error_code (GoaKerberosIdentity  *self,
                                            GError              **error,
                                            gint                  code,
                                            krb5_error_code       error_code,
                                            const char           *format,
                                            ...);

G_LOCK_DEFINE_STATIC (identity_lock);

G_DEFINE_TYPE_WITH_CODE (GoaKerberosIdentity,
                         goa_kerberos_identity,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                initable_interface_init)
                         G_IMPLEMENT_INTERFACE (GOA_TYPE_IDENTITY,
                                                identity_interface_init));
static void
goa_kerberos_identity_dispose (GObject *object)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (object);

  G_LOCK (identity_lock);
  g_clear_object (&self->priv->renewal_alarm);
  g_clear_object (&self->priv->expiring_alarm);
  g_clear_object (&self->priv->expiration_alarm);
  G_UNLOCK (identity_lock);

  G_OBJECT_CLASS (goa_kerberos_identity_parent_class)->dispose (object);

}

static void
goa_kerberos_identity_finalize (GObject *object)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (object);

  g_free (self->priv->identifier);

  if (self->priv->credentials_cache != NULL)
    krb5_cc_close (self->priv->kerberos_context, self->priv->credentials_cache);

  G_OBJECT_CLASS (goa_kerberos_identity_parent_class)->finalize (object);
}

static void
goa_kerberos_identity_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *param_spec)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (object);

  switch (property_id)
    {
    case PROP_IDENTIFIER:
      G_LOCK (identity_lock);
      g_value_set_string (value, self->priv->identifier);
      G_UNLOCK (identity_lock);
      break;
    case PROP_IS_SIGNED_IN:
      g_value_set_boolean (value,
                           goa_kerberos_identity_is_signed_in (GOA_IDENTITY (self)));
      break;
    case PROP_EXPIRATION_TIMESTAMP:
      G_LOCK (identity_lock);
      g_value_set_int64 (value, (gint64) self->priv->expiration_time);
      G_UNLOCK (identity_lock);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, param_spec);
      break;
    }
}

static void
goa_kerberos_identity_class_init (GoaKerberosIdentityClass *klass)
{
  GObjectClass *object_class;

  object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = goa_kerberos_identity_dispose;
  object_class->finalize = goa_kerberos_identity_finalize;
  object_class->get_property = goa_kerberos_identity_get_property;

  g_type_class_add_private (klass, sizeof (GoaKerberosIdentityPrivate));

  signals[EXPIRING] = g_signal_new ("expiring",
                                    G_TYPE_FROM_CLASS (klass),
                                    G_SIGNAL_RUN_LAST,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    G_TYPE_NONE,
                                    0);
  signals[EXPIRED] = g_signal_new ("expired",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   G_TYPE_NONE,
                                   0);
  signals[UNEXPIRED] = g_signal_new ("unexpired",
                                     G_TYPE_FROM_CLASS (klass),
                                     G_SIGNAL_RUN_LAST,
                                     0,
                                     NULL,
                                     NULL,
                                     NULL,
                                     G_TYPE_NONE,
                                     0);
  signals[NEEDS_RENEWAL] = g_signal_new ("needs-renewal",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         0);
  signals[NEEDS_REFRESH] = g_signal_new ("needs-refresh",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         0);

  g_object_class_override_property (object_class, PROP_IDENTIFIER, "identifier");
  g_object_class_override_property (object_class, PROP_IS_SIGNED_IN, "is-signed-in");
  g_object_class_override_property (object_class,
                                    PROP_EXPIRATION_TIMESTAMP,
                                    "expiration-timestamp");

}

static char *
get_identifier (GoaKerberosIdentity  *self,
                GError              **error)
{
  krb5_principal principal;
  krb5_error_code error_code;
  char *unparsed_name;
  char *identifier = NULL;

  if (self->priv->credentials_cache == NULL)
    return NULL;

  error_code = krb5_cc_get_principal (self->priv->kerberos_context,
                                      self->priv->credentials_cache,
                                      &principal);

  if (error_code != 0)
    {
      if (error_code == KRB5_CC_END)
        {
          set_error_from_krb5_error_code (self,
                                          error,
                                          GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,
                                          error_code,
                                          _
                                          ("Could not find identity in credential cache: %k"));
        }
      else
        {
          set_error_from_krb5_error_code (self,
                                          error,
                                          GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS,
                                          error_code,
                                          _
                                          ("Could not find identity in credential cache: %k"));
        }
      return NULL;
    }

  error_code = krb5_unparse_name_flags (self->priv->kerberos_context,
                                        principal,
                                        0,
                                        &unparsed_name);

  if (error_code != 0)
    {
      const char *error_message;

      error_message =
        krb5_get_error_message (self->priv->kerberos_context, error_code);
      g_debug ("GoaKerberosIdentity: Error parsing principal identity name: %s",
               error_message);
      krb5_free_error_message (self->priv->kerberos_context, error_message);
      goto out;
    }

  identifier = g_strdup (unparsed_name);
  krb5_free_unparsed_name (self->priv->kerberos_context, unparsed_name);

out:
  krb5_free_principal (self->priv->kerberos_context, principal);
  return identifier;
}

static void
goa_kerberos_identity_init (GoaKerberosIdentity *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                            GOA_TYPE_KERBEROS_IDENTITY,
                                            GoaKerberosIdentityPrivate);
}

static void
set_error_from_krb5_error_code (GoaKerberosIdentity  *self,
                                GError              **error,
                                gint                  code,
                                krb5_error_code       error_code,
                                const char           *format,
                                ...)
{
  const char *error_message;
  char *literal_message;
  char *expanded_format;
  va_list args;
  char **chunks;

  error_message = krb5_get_error_message (self->priv->kerberos_context, error_code);
  chunks = g_strsplit (format, "%k", -1);
  expanded_format = g_strjoinv (error_message, chunks);
  g_strfreev (chunks);
  krb5_free_error_message (self->priv->kerberos_context, error_message);

  va_start (args, format);
  literal_message = g_strdup_vprintf (expanded_format, args);
  va_end (args);
  g_free (expanded_format);

  g_set_error_literal (error, GOA_IDENTITY_ERROR, code, literal_message);
  g_free (literal_message);
}

char *
goa_kerberos_identity_get_principal_name (GoaKerberosIdentity *self)
{
  krb5_principal principal;
  krb5_error_code error_code;
  char *unparsed_name;
  char *principal_name;
  int flags;

  if (self->priv->identifier == NULL)
    return NULL;

  error_code = krb5_parse_name (self->priv->kerberos_context,
                                self->priv->identifier,
                                &principal);

  if (error_code != 0)
    {
      const char *error_message;
      error_message =
        krb5_get_error_message (self->priv->kerberos_context, error_code);
      g_debug
        ("GoaKerberosIdentity: Error parsing identity %s into kerberos principal: %s",
         self->priv->identifier, error_message);
      krb5_free_error_message (self->priv->kerberos_context, error_message);
      return NULL;
    }

  flags = KRB5_PRINCIPAL_UNPARSE_DISPLAY;
  error_code = krb5_unparse_name_flags (self->priv->kerberos_context,
                                        principal, flags, &unparsed_name);

  if (error_code != 0)
    {
      const char *error_message;

      error_message =
        krb5_get_error_message (self->priv->kerberos_context, error_code);
      g_debug ("GoaKerberosIdentity: Error parsing principal identity name: %s",
               error_message);
      krb5_free_error_message (self->priv->kerberos_context, error_message);
      return NULL;
    }

  principal_name = g_strdup (unparsed_name);
  krb5_free_unparsed_name (self->priv->kerberos_context, unparsed_name);

  return principal_name;
}

char *
goa_kerberos_identity_get_realm_name (GoaKerberosIdentity *self)
{
  krb5_principal principal;
  krb5_error_code error_code;
  krb5_data *realm;
  char *realm_name;

  if (self->priv->identifier == NULL)
    return NULL;

  error_code = krb5_parse_name (self->priv->kerberos_context,
                                self->priv->identifier, &principal);

  if (error_code != 0)
    {
      const char *error_message;
      error_message =
        krb5_get_error_message (self->priv->kerberos_context, error_code);
      g_debug
        ("GoaKerberosIdentity: Error parsing identity %s into kerberos principal: %s",
         self->priv->identifier, error_message);
      krb5_free_error_message (self->priv->kerberos_context, error_message);
      return NULL;
    }

  realm = krb5_princ_realm (self->priv->kerberos_context, principal);
  realm_name = g_strndup (realm->data, realm->length);
  krb5_free_principal (self->priv->kerberos_context, principal);

  return realm_name;
}

static const char *
goa_kerberos_identity_get_identifier (GoaIdentity *identity)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (identity);

  return self->priv->identifier;
}

static gboolean
credentials_validate_existence (GoaKerberosIdentity *self,
                                krb5_principal principal, krb5_creds * credentials)
{
  /* Checks if default principal associated with the cache has a valid
   * ticket granting ticket in the passed in credentials
   */

  if (krb5_is_config_principal (self->priv->kerberos_context, credentials->server))
    return FALSE;

  /* looking for the krbtgt / REALM pair, so it should be exactly 2 items */
  if (krb5_princ_size (self->priv->kerberos_context, credentials->server) != 2)
    return FALSE;

  if (!krb5_realm_compare (self->priv->kerberos_context,
                           credentials->server, principal))
    {
      /* credentials are from some other realm */
      return FALSE;
    }

  if (strncmp (credentials->server->data[0].data,
               KRB5_TGS_NAME, credentials->server->data[0].length) != 0)
    {
      /* credentials aren't for ticket granting */
      return FALSE;
    }

  if (credentials->server->data[1].length != principal->realm.length ||
      memcmp (credentials->server->data[1].data,
              principal->realm.data, principal->realm.length) != 0)
    {
      /* credentials are for some other realm */
      return FALSE;
    }

  return TRUE;
}

static krb5_timestamp
get_current_time (GoaKerberosIdentity *self)
{
  krb5_timestamp current_time;
  krb5_error_code error_code;

  error_code = krb5_timeofday (self->priv->kerberos_context, &current_time);

  if (error_code != 0)
    {
      const char *error_message;

      error_message =
        krb5_get_error_message (self->priv->kerberos_context, error_code);
      g_debug ("GoaKerberosIdentity: Error getting current time: %s", error_message);
      krb5_free_error_message (self->priv->kerberos_context, error_message);
      return 0;
    }

  return current_time;
}

typedef struct
{
  GoaKerberosIdentity *self;
  guint *idle_id;
  const char *property_name;
} NotifyRequest;

static void
clear_idle_id (NotifyRequest *request)
{
  *request->idle_id = 0;
  g_object_unref (request->self);
  g_slice_free (NotifyRequest, request);
}

static gboolean
on_notify_queued (NotifyRequest *request)
{
  g_object_notify (G_OBJECT (request->self), request->property_name);

  return FALSE;
}

static void
queue_notify (GoaKerberosIdentity *self,
              guint               *idle_id,
              const char          *property_name)
{
  NotifyRequest *request;

  if (*idle_id != 0)
    {
      return;
    }

  request = g_slice_new0 (NotifyRequest);
  request->self = g_object_ref (self);
  request->idle_id = idle_id;
  request->property_name = property_name;

  *idle_id = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                              (GSourceFunc)
                              on_notify_queued,
                              request,
                              (GDestroyNotify)
                              clear_idle_id);
}

static void
set_expiration_time (GoaKerberosIdentity *self,
                     krb5_timestamp       expiration_time)
{
  if (self->priv->expiration_time != expiration_time)
    {
      self->priv->expiration_time = expiration_time;
      queue_notify (self,
                    &self->priv->expiration_time_idle_id,
                    "expiration-timestamp");
    }
}

static gboolean
credentials_are_expired (GoaKerberosIdentity *self,
                         krb5_creds          *credentials)
{
  krb5_timestamp current_time;

  current_time = get_current_time (self);

  set_expiration_time (self, MAX (credentials->times.endtime,
                                  self->priv->expiration_time));

  if (credentials->times.endtime <= current_time)
    {
      return TRUE;
    }

  return FALSE;
}

static VerificationLevel
verify_identity (GoaKerberosIdentity  *self,
                 GError              **error)
{
  krb5_principal principal;
  krb5_cc_cursor cursor;
  krb5_creds credentials;
  krb5_error_code error_code;
  VerificationLevel verification_level;

  set_expiration_time (self, 0);

  if (self->priv->credentials_cache == NULL)
    return VERIFICATION_LEVEL_UNVERIFIED;

  error_code = krb5_cc_get_principal (self->priv->kerberos_context,
                                      self->priv->credentials_cache,
                                      &principal);

  if (error_code != 0)
    {
      if (error_code == KRB5_CC_END || error_code == KRB5_FCC_NOFILE)
        return VERIFICATION_LEVEL_UNVERIFIED;

      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_NOT_FOUND,
                                      error_code,
                                      _("Could not find identity in "
                                        "credential cache: %k"));
      return VERIFICATION_LEVEL_ERROR;
    }

  error_code = krb5_cc_start_seq_get (self->priv->kerberos_context,
                                      self->priv->credentials_cache, &cursor);
  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,
                                      error_code,
                                      _("Could not find identity "
                                        "credentials in cache: %k"));

      verification_level = VERIFICATION_LEVEL_ERROR;
      goto out;
    }

  verification_level = VERIFICATION_LEVEL_UNVERIFIED;

  error_code = krb5_cc_next_cred (self->priv->kerberos_context,
                                  self->priv->credentials_cache,
                                  &cursor,
                                  &credentials);

  while (error_code == 0)
    {
      if (credentials_validate_existence (self, principal, &credentials))
        {
          if (!credentials_are_expired (self, &credentials))
            verification_level = VERIFICATION_LEVEL_SIGNED_IN;
          else
            verification_level = VERIFICATION_LEVEL_EXISTS;
        }

      krb5_free_cred_contents (self->priv->kerberos_context, &credentials);

      error_code = krb5_cc_next_cred (self->priv->kerberos_context,
                                      self->priv->credentials_cache,
                                      &cursor,
                                      &credentials);
    }

  if (error_code != KRB5_CC_END)
    {
      verification_level = VERIFICATION_LEVEL_ERROR;

      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS,
                                      error_code,
                                      _("Could not sift through identity "
                                        "credentials in cache: %k"));
      goto end_sequence;
    }

 end_sequence:
  error_code = krb5_cc_end_seq_get (self->priv->kerberos_context,
                                    self->priv->credentials_cache,
                                    &cursor);

  if (error_code != 0)
    {
      verification_level = VERIFICATION_LEVEL_ERROR;

      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_ENUMERATING_CREDENTIALS,
                                      error_code,
                                      _("Could not finish up sifting through "
                                        "identity credentials in cache: %k"));
      goto out;
    }
out:
  krb5_free_principal (self->priv->kerberos_context, principal);
  return verification_level;
}

static gboolean
goa_kerberos_identity_is_signed_in (GoaIdentity *identity)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (identity);
  gboolean is_signed_in = FALSE;

  G_LOCK (identity_lock);
  if (self->priv->cached_verification_level == VERIFICATION_LEVEL_SIGNED_IN)
    is_signed_in = TRUE;
  G_UNLOCK (identity_lock);

  return is_signed_in;
}

static void
identity_interface_init (GoaIdentityInterface *interface)
{
  interface->get_identifier = goa_kerberos_identity_get_identifier;
  interface->is_signed_in = goa_kerberos_identity_is_signed_in;
}

static void
on_expiration_alarm_fired (GoaAlarm            *alarm,
                           GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  g_debug ("GoaKerberosIdentity: expiration alarm fired for identity %s",
           goa_identity_get_identifier (GOA_IDENTITY (self)));
  g_signal_emit (G_OBJECT (self), signals[NEEDS_REFRESH], 0);
}

static void
on_expiration_alarm_rearmed (GoaAlarm            *alarm,
                             GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  g_debug ("GoaKerberosIdentity: expiration alarm rearmed");
  g_signal_emit (G_OBJECT (self), signals[NEEDS_REFRESH], 0);
}

static void
on_renewal_alarm_rearmed (GoaAlarm            *alarm,
                          GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  g_debug ("GoaKerberosIdentity: renewal alarm rearmed");
}

static void
on_renewal_alarm_fired (GoaAlarm            *alarm,
                        GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  if (self->priv->cached_verification_level == VERIFICATION_LEVEL_SIGNED_IN)
    {
      g_debug ("GoaKerberosIdentity: renewal alarm fired for signed-in identity");
      g_signal_emit (G_OBJECT (self), signals[NEEDS_RENEWAL], 0);
    }
}

static void
on_expiring_alarm_rearmed (GoaAlarm            *alarm,
                           GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  g_debug ("GoaKerberosIdentity: expiring alarm rearmed");
}

static void
on_expiring_alarm_fired (GoaAlarm            *alarm,
                         GoaKerberosIdentity *self)
{
  g_return_if_fail (GOA_IS_ALARM (alarm));
  g_return_if_fail (GOA_IS_KERBEROS_IDENTITY (self));

  if (self->priv->cached_verification_level == VERIFICATION_LEVEL_SIGNED_IN)
    {
      g_debug ("GoaKerberosIdentity: expiring alarm fired for signed-in identity");
      g_signal_emit (G_OBJECT (self), signals[EXPIRING], 0);
    }
}

static gboolean
unref_alarm (GoaAlarm *alarm)
{
  g_object_unref (G_OBJECT (alarm));
  return G_SOURCE_REMOVE;
}

static void
clear_alarm_and_unref_on_idle (GoaKerberosIdentity  *self,
                     GoaAlarm            **alarm)
{
  if (!*alarm)
    return;

  g_idle_add ((GSourceFunc) unref_alarm, *alarm);
  *alarm = NULL;
}

static void
reset_alarm (GoaKerberosIdentity  *self,
             GoaAlarm            **alarm,
             GDateTime            *alarm_time)
{
  GDateTime *old_alarm_time = NULL;

  G_LOCK (identity_lock);
  if (*alarm)
    old_alarm_time = goa_alarm_get_time (*alarm);
  if (old_alarm_time == NULL || !g_date_time_equal (alarm_time, old_alarm_time))
    {
      clear_alarm_and_unref_on_idle (self, alarm);
      *alarm = goa_alarm_new (alarm_time);
    }
  G_UNLOCK (identity_lock);

}

static void
disconnect_alarm_signals (GoaKerberosIdentity *self)
{
  if (self->priv->renewal_alarm)
    {
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->renewal_alarm),
                                            G_CALLBACK (on_renewal_alarm_fired),
                                            self);
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->renewal_alarm),
                                            G_CALLBACK (on_renewal_alarm_rearmed),
                                            self);
    }

  if (self->priv->expiring_alarm)
    {
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->expiring_alarm),
                                            G_CALLBACK (on_expiring_alarm_fired),
                                            self);
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->expiring_alarm),
                                            G_CALLBACK (on_expiring_alarm_rearmed),
                                            self);
    }

  if (self->priv->expiration_alarm)
    {
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->expiration_alarm),
                                            G_CALLBACK (on_expiration_alarm_rearmed),
                                            self);
      g_signal_handlers_disconnect_by_func (G_OBJECT (self->priv->expiration_alarm),
                                            G_CALLBACK (on_expiration_alarm_fired),
                                            self);
    }
}

static void
connect_alarm_signals (GoaKerberosIdentity *self)
{
  g_signal_connect (G_OBJECT (self->priv->renewal_alarm),
                    "fired",
                    G_CALLBACK (on_renewal_alarm_fired),
                    self);
  g_signal_connect (G_OBJECT (self->priv->renewal_alarm),
                    "rearmed",
                    G_CALLBACK (on_renewal_alarm_rearmed),
                    self);
  g_signal_connect (G_OBJECT (self->priv->expiring_alarm),
                    "fired",
                    G_CALLBACK (on_expiring_alarm_fired),
                    self);
  g_signal_connect (G_OBJECT (self->priv->expiring_alarm),
                    "rearmed",
                    G_CALLBACK (on_expiring_alarm_rearmed),
                    self);
  g_signal_connect (G_OBJECT (self->priv->expiration_alarm),
                    "fired",
                    G_CALLBACK (on_expiration_alarm_fired),
                    self);
  g_signal_connect (G_OBJECT (self->priv->expiration_alarm),
                    "rearmed",
                    G_CALLBACK (on_expiration_alarm_rearmed),
                    self);
}

static void
reset_alarms (GoaKerberosIdentity *self)
{
  GDateTime *now;
  GDateTime *expiration_time;
  GDateTime *expiring_time;
  GDateTime *renewal_time;
  GTimeSpan time_span_until_expiration;

  now = g_date_time_new_now_local ();
  expiration_time = g_date_time_new_from_unix_local (self->priv->expiration_time);
  time_span_until_expiration = g_date_time_difference (expiration_time, now);
  g_date_time_unref (now);

  /* Let the user reauthenticate 10 min before expiration */
  expiring_time = g_date_time_add_minutes (expiration_time, -10);

  /* Try to quietly auto-renew halfway through so in ideal configurations
   * the ticket is never more than halfway to expired
   */
  renewal_time = g_date_time_add (expiration_time,
                                  -(time_span_until_expiration / 2));

  disconnect_alarm_signals (self);

  reset_alarm (self, &self->priv->renewal_alarm, renewal_time);
  reset_alarm (self, &self->priv->expiring_alarm, expiring_time);
  reset_alarm (self, &self->priv->expiration_alarm, expiration_time);

  g_date_time_unref (renewal_time);
  g_date_time_unref (expiring_time);
  g_date_time_unref (expiration_time);
  connect_alarm_signals (self);
}

static void
clear_alarms (GoaKerberosIdentity *self)
{
  disconnect_alarm_signals (self);
  clear_alarm_and_unref_on_idle (self, &self->priv->renewal_alarm);
  clear_alarm_and_unref_on_idle (self, &self->priv->expiring_alarm);
  clear_alarm_and_unref_on_idle (self, &self->priv->expiration_alarm);
}

static gboolean
goa_kerberos_identity_initable_init (GInitable     *initable,
                                     GCancellable  *cancellable,
                                     GError       **error)
{
  GoaKerberosIdentity *self = GOA_KERBEROS_IDENTITY (initable);
  GError *verification_error;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  if (self->priv->identifier == NULL)
    {
      self->priv->identifier = get_identifier (self, error);

      if (self->priv->identifier != NULL)
        queue_notify (self, &self->priv->identifier_idle_id, "identifier");
    }

  verification_error = NULL;
  self->priv->cached_verification_level =
    verify_identity (self, &verification_error);

  switch (self->priv->cached_verification_level)
    {
    case VERIFICATION_LEVEL_EXISTS:
    case VERIFICATION_LEVEL_SIGNED_IN:
      reset_alarms (self);

      queue_notify (self, &self->priv->is_signed_in_idle_id, "is-signed-in");
      return TRUE;

    case VERIFICATION_LEVEL_UNVERIFIED:
      return TRUE;

    case VERIFICATION_LEVEL_ERROR:
      if (verification_error != NULL)
        {
          g_propagate_error (error, verification_error);
          return FALSE;
        }
    default:
      g_set_error (error,
                   GOA_IDENTITY_ERROR,
                   GOA_IDENTITY_ERROR_VERIFYING,
                   _("No associated identification found"));
      return FALSE;

    }
}

static void
initable_interface_init (GInitableIface *interface)
{
  interface->init = goa_kerberos_identity_initable_init;
}

typedef struct
{
  GoaKerberosIdentity    *identity;
  GoaIdentityInquiryFunc  inquiry_func;
  gpointer                inquiry_data;
  GDestroyNotify          destroy_notify;
  GCancellable           *cancellable;
} SignInOperation;

static krb5_error_code
on_kerberos_inquiry (krb5_context      kerberos_context,
                     SignInOperation  *operation,
                     const char       *name,
                     const char       *banner,
                     int               number_of_prompts,
                     krb5_prompt       prompts[])
{
  GoaIdentityInquiry *inquiry;
  krb5_error_code error_code;

  inquiry = goa_kerberos_identity_inquiry_new (operation->identity,
                                               name,
                                               banner,
                                               prompts,
                                               number_of_prompts);

  operation->inquiry_func (inquiry,
                           operation->cancellable,
                           operation->inquiry_data);

  if (!goa_identity_inquiry_is_complete (inquiry))
    g_cancellable_cancel (operation->cancellable);

  if (g_cancellable_is_cancelled (operation->cancellable))
    error_code = KRB5_LIBOS_PWDINTR;
  else
    error_code = 0;

  g_object_unref (inquiry);

  return error_code;
}

static gboolean
create_credential_cache (GoaKerberosIdentity  *self,
                         GError              **error)
{
  krb5_ccache      default_cache;
  const char      *cache_type;
  krb5_error_code  error_code;

  error_code = krb5_cc_default (self->priv->kerberos_context, &default_cache);

  if (error_code == 0)
    {
      cache_type = krb5_cc_get_type (self->priv->kerberos_context, default_cache);

      error_code = krb5_cc_new_unique (self->priv->kerberos_context,
                                       cache_type,
                                       NULL,
                                       &self->priv->credentials_cache);
    }

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,
                                      error_code,
                                      _("Could not create credential cache: %k"));

      return FALSE;
    }

  return TRUE;
}

static gboolean
goa_kerberos_identity_update_credentials (GoaKerberosIdentity  *self,
                                          krb5_principal        principal,
                                          krb5_creds           *new_credentials,
                                          GError              **error)
{
  krb5_error_code   error_code;

  if (self->priv->credentials_cache == NULL)
    {
      if (!create_credential_cache (self, error))
        {
          krb5_free_cred_contents (self->priv->kerberos_context, new_credentials);
          goto out;
        }
    }

  error_code = krb5_cc_initialize (self->priv->kerberos_context,
                                   self->priv->credentials_cache,
                                   principal);
  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,
                                      error_code,
                                      _("Could not initialize credentials "
                                        "cache: %k"));

      krb5_free_cred_contents (self->priv->kerberos_context, new_credentials);
      goto out;
    }

  error_code = krb5_cc_store_cred (self->priv->kerberos_context,
                                   self->priv->credentials_cache,
                                   new_credentials);

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_SAVING_CREDENTIALS,
                                      error_code,
                                      _("Could not store new credentials in "
                                        "credentials cache: %k"));

      krb5_free_cred_contents (self->priv->kerberos_context, new_credentials);
      goto out;
    }
  krb5_free_cred_contents (self->priv->kerberos_context, new_credentials);

  return TRUE;
out:
  return FALSE;
}

static SignInOperation *
sign_in_operation_new (GoaKerberosIdentity    *identity,
                       GoaIdentityInquiryFunc  inquiry_func,
                       gpointer                inquiry_data,
                       GDestroyNotify          destroy_notify,
                       GCancellable           *cancellable)
{
  SignInOperation *operation;

  operation = g_slice_new0 (SignInOperation);
  operation->identity = g_object_ref (identity);
  operation->inquiry_func = inquiry_func;
  operation->inquiry_data = inquiry_data;
  operation->destroy_notify = destroy_notify;

  if (cancellable == NULL)
    operation->cancellable = g_cancellable_new ();
  else
    operation->cancellable = g_object_ref (cancellable);

  return operation;
}

static void
sign_in_operation_free (SignInOperation *operation)
{
  g_object_unref (operation->identity);
  g_object_unref (operation->cancellable);

  g_slice_free (SignInOperation, operation);
}

gboolean
goa_kerberos_identity_sign_in (GoaKerberosIdentity     *self,
                               const char              *principal_name,
                               gconstpointer            initial_password,
                               GoaIdentitySignInFlags   flags,
                               GoaIdentityInquiryFunc   inquiry_func,
                               gpointer                 inquiry_data,
                               GDestroyNotify           destroy_notify,
                               GCancellable            *cancellable,
                               GError                 **error)
{
  SignInOperation *operation;
  krb5_principal principal;
  krb5_error_code error_code;
  krb5_creds new_credentials;
  krb5_get_init_creds_opt *options;
  krb5_deltat start_time;
  char *service_name;
  gboolean signed_in;

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  error_code = krb5_get_init_creds_opt_alloc (self->priv->kerberos_context,
                                              &options);
  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_ALLOCATING_CREDENTIALS,
                                      error_code, "%k");
      if (destroy_notify)
        destroy_notify (inquiry_data);
      return FALSE;
    }

  signed_in = FALSE;

  operation = sign_in_operation_new (self,
                                     inquiry_func,
                                     inquiry_data,
                                     destroy_notify,
                                     cancellable);

  if (g_strcmp0 (self->priv->identifier, principal_name) != 0)
    {
      g_free (self->priv->identifier);
      self->priv->identifier = g_strdup (principal_name);
    }

  error_code = krb5_parse_name (self->priv->kerberos_context,
                                principal_name,
                                &principal);

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_PARSING_IDENTIFIER,
                                      error_code,
                                      "%k");
      if (destroy_notify)
        destroy_notify (inquiry_data);
      return FALSE;
    }

  if ((flags & GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_FORWARDING) == 0)
    krb5_get_init_creds_opt_set_forwardable (options, TRUE);

  if ((flags & GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_PROXYING) == 0)
    krb5_get_init_creds_opt_set_proxiable (options, TRUE);

  if ((flags & GOA_IDENTITY_SIGN_IN_FLAGS_DISALLOW_RENEWAL) == 0)
    krb5_get_init_creds_opt_set_renew_life (options, G_MAXINT);

  /* Poke glibc in case the network changed
   */
  res_init ();

  start_time = 0;
  service_name = NULL;
  error_code = krb5_get_init_creds_password (self->priv->kerberos_context,
                                             &new_credentials,
                                             principal,
                                             (char *)
                                             initial_password,
                                             (krb5_prompter_fct)
                                             on_kerberos_inquiry,
                                             operation,
                                             start_time,
                                             service_name,
                                             options);

  if (error_code == KRB5_LIBOS_PWDINTR)
    g_cancellable_cancel (operation->cancellable);

  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    {
      if (destroy_notify)
        destroy_notify (inquiry_data);
      sign_in_operation_free (operation);

      krb5_free_principal (self->priv->kerberos_context, principal);
      goto done;
    }

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_AUTHENTICATION_FAILED,
                                      error_code,
                                      "%k");
      if (destroy_notify)
        destroy_notify (inquiry_data);
      sign_in_operation_free (operation);

      krb5_free_principal (self->priv->kerberos_context, principal);
      goto done;
    }

  if (destroy_notify)
    destroy_notify (inquiry_data);
  sign_in_operation_free (operation);

  if (!goa_kerberos_identity_update_credentials (self,
                                                 principal,
                                                 &new_credentials,
                                                 error))
    {
      krb5_free_principal (self->priv->kerberos_context, principal);
      goto done;
    }
  krb5_free_principal (self->priv->kerberos_context, principal);

  g_debug ("GoaKerberosIdentity: identity signed in");
  signed_in = TRUE;
done:

  return signed_in;
}

static void
update_identifier (GoaKerberosIdentity *self, GoaKerberosIdentity *new_identity)
{
  char *new_identifier;

  new_identifier = get_identifier (self, NULL);
  if (g_strcmp0 (self->priv->identifier, new_identifier) != 0)
    {
      g_free (self->priv->identifier);
      self->priv->identifier = new_identifier;
      queue_notify (self, &self->priv->identifier_idle_id, "identifier");
    }
  else
    {
      g_free (new_identifier);
    }
}

void
goa_kerberos_identity_update (GoaKerberosIdentity *self,
                              GoaKerberosIdentity *new_identity)
{
  VerificationLevel verification_level;

  if (self->priv->credentials_cache != NULL)
    krb5_cc_close (self->priv->kerberos_context, self->priv->credentials_cache);

  krb5_cc_dup (new_identity->priv->kerberos_context,
               new_identity->priv->credentials_cache,
               &self->priv->credentials_cache);

  G_LOCK (identity_lock);
  update_identifier (self, new_identity);
  G_UNLOCK (identity_lock);

  verification_level = verify_identity (self, NULL);

  if (verification_level == VERIFICATION_LEVEL_SIGNED_IN)
    reset_alarms (self);
  else
    clear_alarms (self);

  if (verification_level != self->priv->cached_verification_level)
    {
      if (self->priv->cached_verification_level == VERIFICATION_LEVEL_SIGNED_IN &&
          verification_level == VERIFICATION_LEVEL_EXISTS)
        {

          G_LOCK (identity_lock);
          self->priv->cached_verification_level = verification_level;
          G_UNLOCK (identity_lock);

          g_signal_emit (G_OBJECT (self), signals[EXPIRED], 0);
        }
      else if (self->priv->cached_verification_level == VERIFICATION_LEVEL_EXISTS &&
               verification_level == VERIFICATION_LEVEL_SIGNED_IN)
        {

          G_LOCK (identity_lock);
          self->priv->cached_verification_level = verification_level;
          G_UNLOCK (identity_lock);

          g_signal_emit (G_OBJECT (self), signals[UNEXPIRED], 0);
        }
      else
        {
          G_LOCK (identity_lock);
          self->priv->cached_verification_level = verification_level;
          G_UNLOCK (identity_lock);
        }
      queue_notify (self, &self->priv->is_signed_in_idle_id, "is-signed-in");
    }
}

gboolean
goa_kerberos_identity_renew (GoaKerberosIdentity *self, GError **error)
{
  krb5_error_code error_code = 0;
  krb5_principal principal;
  krb5_creds new_credentials;
  gboolean renewed = FALSE;
  char *name = NULL;

  if (self->priv->credentials_cache == NULL)
    {
      g_set_error (error,
                   GOA_IDENTITY_ERROR,
                   GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,
                   _("Could not renew identity: Not signed in"));
      goto out;
    }

  error_code = krb5_cc_get_principal (self->priv->kerberos_context,
                                      self->priv->credentials_cache, &principal);

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_CREDENTIALS_UNAVAILABLE,
                                      error_code, _("Could not renew identity: %k"));
      goto out;
    }

  name = goa_kerberos_identity_get_principal_name (self);

  error_code = krb5_get_renewed_creds (self->priv->kerberos_context,
                                       &new_credentials,
                                       principal,
                                       self->priv->credentials_cache, NULL);
  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_RENEWING,
                                      error_code,
                                      _
                                      ("Could not get new credentials to renew identity %s: %k"),
                                      name);
      goto free_principal;
    }

  if (!goa_kerberos_identity_update_credentials (self,
                                                 principal,
                                                 &new_credentials,
                                                 error))
    {
      goto free_principal;
    }

  g_debug ("GoaKerberosIdentity: identity %s renewed", name);
  renewed = TRUE;

free_principal:
  krb5_free_principal (self->priv->kerberos_context, principal);

out:
  g_free (name);

  return renewed;
}

gboolean
goa_kerberos_identity_erase (GoaKerberosIdentity *self, GError **error)
{
  krb5_error_code error_code = 0;

  if (self->priv->credentials_cache != NULL)
    {
      error_code = krb5_cc_destroy (self->priv->kerberos_context,
                                    self->priv->credentials_cache);
      self->priv->credentials_cache = NULL;
    }

  if (error_code != 0)
    {
      set_error_from_krb5_error_code (self,
                                      error,
                                      GOA_IDENTITY_ERROR_REMOVING_CREDENTIALS,
                                      error_code, _("Could not erase identity: %k"));
      return FALSE;
    }

  return TRUE;
}

GoaIdentity *
goa_kerberos_identity_new (krb5_context context, krb5_ccache cache, GError **error)
{
  GoaKerberosIdentity *self;

  self = GOA_KERBEROS_IDENTITY (g_object_new (GOA_TYPE_KERBEROS_IDENTITY, NULL));

  krb5_cc_dup (context, cache, &self->priv->credentials_cache);
  self->priv->kerberos_context = context;

  error = NULL;
  if (!g_initable_init (G_INITABLE (self), NULL, error))
    {
      g_object_unref (self);
      return NULL;
    }

  return GOA_IDENTITY (self);
}
