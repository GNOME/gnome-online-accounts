/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009-2011 Red Hat, Inc.
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
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Based on panels/user-accounts/um-editable-entry.c from
 * gnome-control-center by Matthias Clasen <mclasen@redhat.com>.
 * Relicensed to LGPLv2 after obtaining permission.
 */

#include <gdk/gdkkeysyms.h>
#include "goaeditablelabel.h"

#define EMPTY_TEXT "\xe2\x80\x94"

struct _GoaEditableLabelPrivate {
        GtkNotebook *notebook;
        GtkLabel    *label;
        GtkButton   *button;
        GtkEntry    *entry;

        gchar *text;
        gboolean editable;
        gint weight;
        gboolean weight_set;
        gdouble scale;
        gboolean scale_set;
};

#define GOA_EDITABLE_LABEL_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GOA_TYPE_EDITABLE_LABEL, GoaEditableLabelPrivate))

enum {
        PROP_0,
        PROP_TEXT,
        PROP_EDITABLE,
        PROP_SCALE,
        PROP_SCALE_SET,
        PROP_WEIGHT,
        PROP_WEIGHT_SET
};

enum {
        EDITING_DONE,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GoaEditableLabel, goa_editable_label, GTK_TYPE_ALIGNMENT);

void
goa_editable_label_set_text (GoaEditableLabel *e,
                             const gchar    *text)
{
        GoaEditableLabelPrivate *priv;
        gchar *tmp;
        const gchar *label_text;
        GtkWidget *label;

        priv = e->priv;

        tmp = g_strdup (text);
        g_free (priv->text);
        priv->text = tmp;

        gtk_entry_set_text (priv->entry, tmp);

        label_text = tmp;
        if (label_text == NULL || label_text[0] == '\0')
                label_text = EMPTY_TEXT;

        gtk_label_set_text (priv->label, label_text);
        label = gtk_bin_get_child (GTK_BIN (priv->button));
        gtk_label_set_text (GTK_LABEL (label), label_text);

        g_object_notify (G_OBJECT (e), "text");
}

const gchar *
goa_editable_label_get_text (GoaEditableLabel *e)
{
        return e->priv->text;
}

void
goa_editable_label_set_editable (GoaEditableLabel *e,
                                 gboolean        editable)
{
        GoaEditableLabelPrivate *priv;

        priv = e->priv;

        if (priv->editable != editable) {
                priv->editable = editable;

                gtk_notebook_set_current_page (priv->notebook, editable ? 1 : 0);

                g_object_notify (G_OBJECT (e), "editable");
        }
}

gboolean
goa_editable_label_get_editable (GoaEditableLabel *e)
{
        return e->priv->editable;
}

static void
update_entry_font (GtkWidget        *widget,
                   GoaEditableLabel *e)
{
        GoaEditableLabelPrivate *priv = e->priv;
        PangoFontDescription *desc;
        GtkStyleContext *style;
        gint size;

        if (!priv->weight_set && !priv->scale_set)
                return;

        g_signal_handlers_block_by_func (widget, update_entry_font, e);

        gtk_widget_override_font (widget, NULL);        

        style = gtk_widget_get_style_context (widget);
        desc = pango_font_description_copy 
                (gtk_style_context_get_font (style, gtk_widget_get_state_flags (widget)));

        if (priv->weight_set)
                pango_font_description_set_weight (desc, priv->weight);
        if (priv->scale_set) {
                size = pango_font_description_get_size (desc);
                pango_font_description_set_size (desc, priv->scale * size);
        }
        gtk_widget_override_font (widget, desc);

        pango_font_description_free (desc);

        g_signal_handlers_unblock_by_func (widget, update_entry_font, e);
}

static void
update_fonts (GoaEditableLabel *e)
{
        PangoAttrList *attrs;
        PangoAttribute *attr;
        GtkWidget *label;

        GoaEditableLabelPrivate *priv = e->priv;

        attrs = pango_attr_list_new ();
        if (priv->scale_set) {
                attr = pango_attr_scale_new (priv->scale);
                pango_attr_list_insert (attrs, attr);
        }
        if (priv->weight_set) {
                attr = pango_attr_weight_new (priv->weight);
                pango_attr_list_insert (attrs, attr);
        }

        gtk_label_set_attributes (priv->label, attrs);

        label = gtk_bin_get_child (GTK_BIN (priv->button));
        gtk_label_set_attributes (GTK_LABEL (label), attrs);

        pango_attr_list_unref (attrs);

        update_entry_font ((GtkWidget *)priv->entry, e);
}

void
goa_editable_label_set_weight (GoaEditableLabel *e,
                               gint            weight)
{
        GoaEditableLabelPrivate *priv = e->priv;

        if (priv->weight == weight && priv->weight_set)
                return;

        priv->weight = weight;
        priv->weight_set = TRUE;

        update_fonts (e);

        g_object_notify (G_OBJECT (e), "weight");
        g_object_notify (G_OBJECT (e), "weight-set");
}

gint
goa_editable_label_get_weight (GoaEditableLabel *e)
{
        return e->priv->weight;
}

void
goa_editable_label_set_scale (GoaEditableLabel *e,
                              gdouble         scale)
{
        GoaEditableLabelPrivate *priv = e->priv;

        if (priv->scale == scale && priv->scale_set)
                return;

        priv->scale = scale;
        priv->scale_set = TRUE;

        update_fonts (e);

        g_object_notify (G_OBJECT (e), "scale");
        g_object_notify (G_OBJECT (e), "scale-set");
}

gdouble
goa_editable_label_get_scale (GoaEditableLabel *e)
{
        return e->priv->scale;
}

static void
goa_editable_label_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        GoaEditableLabel *e = GOA_EDITABLE_LABEL (object);

        switch (prop_id) {
        case PROP_TEXT:
                goa_editable_label_set_text (e, g_value_get_string (value));
                break;
        case PROP_EDITABLE:
                goa_editable_label_set_editable (e, g_value_get_boolean (value));
                break;
        case PROP_WEIGHT:
                goa_editable_label_set_weight (e, g_value_get_int (value));
                break;
        case PROP_WEIGHT_SET:
                e->priv->weight_set = g_value_get_boolean (value);
                break;
        case PROP_SCALE:
                goa_editable_label_set_scale (e, g_value_get_double (value));
                break;
        case PROP_SCALE_SET:
                e->priv->scale_set = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
goa_editable_label_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        GoaEditableLabel *e = GOA_EDITABLE_LABEL (object);

        switch (prop_id) {
        case PROP_TEXT:
                g_value_set_string (value,
                                    goa_editable_label_get_text (e));
                break;
        case PROP_EDITABLE:
                g_value_set_boolean (value,
                                     goa_editable_label_get_editable (e));
                break;
        case PROP_WEIGHT:
                g_value_set_int (value,
                                 goa_editable_label_get_weight (e));
                break;
        case PROP_WEIGHT_SET:
                g_value_set_boolean (value, e->priv->weight_set);
                break;
        case PROP_SCALE:
                g_value_set_double (value,
                                    goa_editable_label_get_scale (e));
                break;
        case PROP_SCALE_SET:
                g_value_set_boolean (value, e->priv->scale_set);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
goa_editable_label_finalize (GObject *object)
{
        GoaEditableLabel *e = (GoaEditableLabel*)object;

        g_free (e->priv->text);

        G_OBJECT_CLASS (goa_editable_label_parent_class)->finalize (object);
}

static void
goa_editable_label_class_init (GoaEditableLabelClass *class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (class);

        object_class->set_property = goa_editable_label_set_property;
        object_class->get_property = goa_editable_label_get_property;
        object_class->finalize = goa_editable_label_finalize;

        signals[EDITING_DONE] =
                g_signal_new ("editing-done",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GoaEditableLabelClass, editing_done),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        g_object_class_install_property (object_class, PROP_TEXT,
                g_param_spec_string ("text",
                                     "Text", "The text of the button",
                                     NULL,
                                     G_PARAM_READWRITE));

        g_object_class_install_property (object_class, PROP_EDITABLE,
                g_param_spec_boolean ("editable",
                                      "Editable", "Whether the text can be edited",
                                      FALSE,
                                      G_PARAM_READWRITE));

        g_object_class_install_property (object_class, PROP_WEIGHT,
                g_param_spec_int ("weight",
                                  "Font Weight", "The font weight to use",
                                  0, G_MAXINT, PANGO_WEIGHT_NORMAL,
                                  G_PARAM_READWRITE));

        g_object_class_install_property (object_class, PROP_WEIGHT_SET,
                g_param_spec_boolean ("weight-set",
                                      "Font Weight Set", "Whether a font weight is set",
                                      FALSE,
                                      G_PARAM_READWRITE));

        g_object_class_install_property (object_class, PROP_SCALE,
                g_param_spec_double ("scale",
                                     "Font Scale", "The font scale to use",
                                     0.0, G_MAXDOUBLE, 1.0,
                                     G_PARAM_READWRITE));

        g_object_class_install_property (object_class, PROP_SCALE_SET,
                g_param_spec_boolean ("scale-set",
                                      "Font Scale Set", "Whether a font scale is set",
                                      FALSE,
                                      G_PARAM_READWRITE));

        g_type_class_add_private (class, sizeof (GoaEditableLabelPrivate));
}

static void
start_editing (GoaEditableLabel *e)
{
        gtk_notebook_set_current_page (e->priv->notebook, 2);
}

static void
stop_editing (GoaEditableLabel *e)
{
        goa_editable_label_set_text (e, gtk_entry_get_text (e->priv->entry));
        gtk_notebook_set_current_page (e->priv->notebook, 1);
        g_signal_emit (e, signals[EDITING_DONE], 0);
}

static void
cancel_editing (GoaEditableLabel *e)
{
        gtk_entry_set_text (e->priv->entry, goa_editable_label_get_text (e));
        gtk_notebook_set_current_page (e->priv->notebook, 1);
}

static void
button_clicked (GtkWidget       *widget,
                GoaEditableLabel *e)
{
        start_editing (e);
}

static void
entry_activated (GtkWidget       *widget,
                 GoaEditableLabel *e)
{
        stop_editing (e);
}

static gboolean
entry_focus_out (GtkWidget       *widget,
                 GdkEventFocus   *event,
                 GoaEditableLabel *e)
{
        stop_editing (e);
        return FALSE;
}

static gboolean
entry_key_press (GtkWidget       *widget,
                 GdkEventKey     *event,
                 GoaEditableLabel *e)
{
        if (event->keyval == GDK_KEY_Escape) {
                cancel_editing (e);
        }
        return FALSE;
}

static void
update_button_padding (GtkWidget       *widget,
                       GtkAllocation   *allocation,
                       GoaEditableLabel *e)
{
        GoaEditableLabelPrivate *priv = e->priv;
        GtkAllocation alloc;
        gint offset;
        gint pad;

        gtk_widget_get_allocation (gtk_widget_get_parent (widget), &alloc);

        offset = allocation->x - alloc.x;

        gtk_misc_get_padding  (GTK_MISC (priv->label), &pad, NULL);
        if (offset != pad)
                gtk_misc_set_padding (GTK_MISC (priv->label), offset, 0);
}

static void
goa_editable_label_init (GoaEditableLabel *e)
{
        GoaEditableLabelPrivate *priv;

        priv = e->priv = GOA_EDITABLE_LABEL_GET_PRIVATE (e);

        priv->weight = PANGO_WEIGHT_NORMAL;
        priv->weight_set = FALSE;
        priv->scale = 1.0;
        priv->scale_set = FALSE;

        priv->notebook = (GtkNotebook*)gtk_notebook_new ();
        gtk_notebook_set_show_tabs (priv->notebook, FALSE);
        gtk_notebook_set_show_border (priv->notebook, FALSE);

        priv->label = (GtkLabel*)gtk_label_new (EMPTY_TEXT);
        gtk_misc_set_alignment (GTK_MISC (priv->label), 0.0, 0.5);
        gtk_notebook_append_page (priv->notebook, (GtkWidget*)priv->label, NULL);

        priv->button = (GtkButton*)gtk_button_new_with_label (EMPTY_TEXT);
        gtk_widget_set_receives_default ((GtkWidget*)priv->button, TRUE);
        gtk_button_set_relief (priv->button, GTK_RELIEF_NONE);
        gtk_button_set_alignment (priv->button, 0.0, 0.5);
        gtk_notebook_append_page (priv->notebook, (GtkWidget*)priv->button, NULL);
        g_signal_connect (priv->button, "clicked", G_CALLBACK (button_clicked), e);

        priv->entry = (GtkEntry*)gtk_entry_new ();
        gtk_notebook_append_page (priv->notebook, (GtkWidget*)priv->entry, NULL);

        g_signal_connect (priv->entry, "activate", G_CALLBACK (entry_activated), e);
        g_signal_connect (priv->entry, "focus-out-event", G_CALLBACK (entry_focus_out), e);
        g_signal_connect (priv->entry, "key-press-event", G_CALLBACK (entry_key_press), e);
        g_signal_connect (priv->entry, "style-updated", G_CALLBACK (update_entry_font), e);
        g_signal_connect (gtk_bin_get_child (GTK_BIN (priv->button)), "size-allocate", G_CALLBACK (update_button_padding), e);

        gtk_container_add (GTK_CONTAINER (e), (GtkWidget*)priv->notebook);

        gtk_widget_show ((GtkWidget*)priv->notebook);
        gtk_widget_show ((GtkWidget*)priv->label);
        gtk_widget_show ((GtkWidget*)priv->button);
        gtk_widget_show ((GtkWidget*)priv->entry);

        gtk_notebook_set_current_page (priv->notebook, 0);
}

GtkWidget *
goa_editable_label_new (void)
{
        return (GtkWidget *) g_object_new (GOA_TYPE_EDITABLE_LABEL, NULL);
}
