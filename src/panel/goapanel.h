
#ifndef __GOA_PANEL_H__
#define __GOA_PANEL_H__

#include <libgnome-control-center/cc-panel.h>

G_BEGIN_DECLS

#define GOA_TYPE_PANEL  (goa_panel_get_type ())
#define GOA_PANEL(o)    (G_TYPE_CHECK_INSTANCE_CAST ((o), GOA_TYPE_PANEL, GoaPanel))
#define GOA_IS_PANEL(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), GOA_TYPE_PANEL))

typedef struct _GoaPanel              GoaPanel;

GType      goa_panel_get_type   (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GOA_PANEL_H__ */
