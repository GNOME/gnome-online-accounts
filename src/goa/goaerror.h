
#if !defined (__GOA_INSIDE_GOA_H__) && !defined (GOA_COMPILATION)
#error "Only <goa/goa.h> can be included directly."
#endif

#ifndef __GOA_ERROR_H__
#define __GOA_ERROR_H__

#include <goa/goatypes.h>

G_BEGIN_DECLS

/**
 * GOA_ERROR:
 *
 * Error domain for Goa. Errors in this domain will be form the
 * #GoaError enumeration. See #GError for more information on error
 * domains.
 */
#define GOA_ERROR (goa_error_quark ())

GQuark goa_error_quark (void);

G_END_DECLS

#endif /* __GOA_ERROR_H__ */
