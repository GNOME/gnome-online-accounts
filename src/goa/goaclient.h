
#if !defined (__GOA_INSIDE_GOA_H__) && !defined (GOA_COMPILATION)
#error "Only <goa/goa.h> can be included directly."
#endif

#ifndef __GOA_CLIENT_H__
#define __GOA_CLIENT_H__

#include <goa/goatypes.h>
#include <goa/goa-generated.h>

G_BEGIN_DECLS

#define GOA_TYPE_CLIENT  (goa_client_get_type ())
#define GOA_CLIENT(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_CLIENT, GoaClient))
#define GOA_IS_CLIENT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_CLIENT))

GType               goa_client_get_type           (void) G_GNUC_CONST;
void                goa_client_new                (GCancellable        *cancellable,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);
GoaClient          *goa_client_new_finish         (GAsyncResult        *res,
                                                   GError             **error);
GoaClient          *goa_client_new_sync           (GCancellable        *cancellable,
                                                   GError             **error);
GDBusObjectManager *goa_client_get_object_manager (GoaClient           *client);

G_END_DECLS

#endif /* __GOA_CLIENT_H__ */
