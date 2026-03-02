/*
 *  $Id: gwyadjustbar.h 24993 2022-09-02 14:39:52Z yeti-dn $
 *  Copyright (C) 2012-2017 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#ifndef __GWY_ADJUST_BAR_H__
#define __GWY_ADJUST_BAR_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef enum {
    GWY_SCALE_MAPPING_LINEAR = 0,
    GWY_SCALE_MAPPING_SQRT   = 1,
    GWY_SCALE_MAPPING_LOG    = 2,
    GWY_SCALE_MAPPING_LOG1P  = 3,
} GwyScaleMappingType;

#define GWY_TYPE_ADJUST_BAR            (gwy_adjust_bar_get_type())
#define GWY_ADJUST_BAR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_ADJUST_BAR, GwyAdjustBar))
#define GWY_ADJUST_BAR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_ADJUST_BAR, GwyAdjustBarClass))
#define GWY_IS_ADJUST_BAR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_ADJUST_BAR))
#define GWY_IS_ADJUST_BAR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_ADJUST_BAR))
#define GWY_ADJUST_BAR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_ADJUST_BAR, GwyAdjustBarClass))

typedef struct _GwyAdjustBar      GwyAdjustBar;
typedef struct _GwyAdjustBarClass GwyAdjustBarClass;

struct _GwyAdjustBar {
    GtkBin bin;
    struct _GwyAdjustBarPrivate *priv;
};

struct _GwyAdjustBarClass {
    /*<private>*/
    GtkBinClass bin_class;

    void (*change_value)(GwyAdjustBar *adjbar, gdouble value);

    void (*reserved1)(void);
    void (*reserved2)(void);
};

GType               gwy_adjust_bar_get_type            (void)                         G_GNUC_CONST;
GtkWidget*          gwy_adjust_bar_new                 (GtkAdjustment *adjustment,
                                                        const gchar *label);
void                gwy_adjust_bar_set_adjustment      (GwyAdjustBar *adjbar,
                                                        GtkAdjustment *adjustment);
GtkAdjustment*      gwy_adjust_bar_get_adjustment      (GwyAdjustBar *adjbar);
void                gwy_adjust_bar_set_snap_to_ticks   (GwyAdjustBar *adjbar,
                                                        gboolean setting);
gboolean            gwy_adjust_bar_get_snap_to_ticks   (GwyAdjustBar *adjbar);
void                gwy_adjust_bar_set_mapping         (GwyAdjustBar *adjbar,
                                                        GwyScaleMappingType mapping);
GwyScaleMappingType gwy_adjust_bar_get_mapping         (GwyAdjustBar *adjbar);
void                gwy_adjust_bar_set_has_check_button(GwyAdjustBar *adjbar,
                                                        gboolean setting);
gboolean            gwy_adjust_bar_get_has_check_button(GwyAdjustBar *adjbar);
void                gwy_adjust_bar_set_bar_sensitive   (GwyAdjustBar *adjbar,
                                                        gboolean sensitive);
gboolean            gwy_adjust_bar_get_bar_sensitive   (GwyAdjustBar *adjbar);
GtkWidget*          gwy_adjust_bar_get_label           (GwyAdjustBar *adjbar);
GtkWidget*          gwy_adjust_bar_get_check_button    (GwyAdjustBar *adjbar);

G_END_DECLS

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
