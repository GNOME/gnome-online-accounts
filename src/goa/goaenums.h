
#if !defined (__GOA_INSIDE_GOA_H__) && !defined (GOA_COMPILATION)
#error "Only <goa/goa.h> can be included directly."
#endif

#ifndef __GOA_ENUMS_H__
#define __GOA_ENUMS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * GoaError:
 * @GOA_ERROR_FAILED: The operation failed.
 * @GOA_ERROR_NOT_SUPPORTED: The operation is not supported.
 *
 * Error codes for the #GOA_ERROR error domain and the
 * corresponding D-Bus error names.
 */
typedef enum
{
  GOA_ERROR_FAILED,                     /* org.gnome.OnlineAccounts.Error.Failed */
  GOA_ERROR_NOT_SUPPORTED               /* org.gnome.OnlineAccounts.Error.NotSupported */
} GoaError;

#define GOA_ERROR_NUM_ENTRIES  (GOA_ERROR_NOT_SUPPORTED + 1)

G_END_DECLS

#endif /* __GOA_ENUMS_H__ */
