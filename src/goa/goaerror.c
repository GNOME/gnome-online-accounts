
#include "config.h"
#include <glib/gi18n-lib.h>

#include "goaerror.h"

/**
 * SECTION:goaerror
 * @title: GoaError
 * @short_description: Error codes
 *
 * Error codes and D-Bus errors.
 */

static const GDBusErrorEntry dbus_error_entries[] =
{
  {GOA_ERROR_FAILED,                       "org.freedesktop.Goa.Error.Failed"},
  {GOA_ERROR_NOT_SUPPORTED,                "org.freedesktop.Goa.Error.NotSupported"}
};

GQuark
goa_error_quark (void)
{
  G_STATIC_ASSERT (G_N_ELEMENTS (dbus_error_entries) == GOA_ERROR_NUM_ENTRIES);
  static volatile gsize quark_volatile = 0;
  g_dbus_error_register_error_domain ("goa-error-quark",
                                      &quark_volatile,
                                      dbus_error_entries,
                                      G_N_ELEMENTS (dbus_error_entries));
  return (GQuark) quark_volatile;
}
