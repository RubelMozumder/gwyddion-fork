/*
 *  $Id: layer.h 23053 2021-01-12 12:20:36Z yeti-dn $
 *  Copyright (C) 2016 David Necas (Yeti).
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
#ifndef __GWY_LAYER_H__
#define __GWY_LAYER_H__ 1

enum {
    PROXIMITY_DISTANCE = 8,
    CROSS_SIZE = 8,
};

G_GNUC_UNUSED
static inline void
gwy_vector_layer_transform_line_to_target(GwyVectorLayer *layer,
                                          GdkDrawable *drawable,
                                          GwyRenderingTarget target,
                                          gdouble xfrom, gdouble yfrom,
                                          gdouble xto, gdouble yto,
                                          gint *xifrom, gint *yifrom,
                                          gint *xito, gint *yito)
{
    GwyDataView *data_view;
    gdouble xreal, yreal;
    gint width, height;

    gdk_drawable_get_size(drawable, &width, &height);
    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_if_fail(data_view);
    gwy_data_view_get_real_data_sizes(data_view, &xreal, &yreal);

    if (target == GWY_RENDERING_TARGET_PIXMAP_IMAGE) {
        *xifrom = floor((xfrom)*width/xreal);
        *yifrom = floor((yfrom)*height/yreal);
        *xito = floor((xto)*width/xreal);
        *yito = floor((yto)*height/yreal);
        return;
    }

    g_return_if_fail(target == GWY_RENDERING_TARGET_SCREEN);
    gwy_data_view_coords_real_to_xy(data_view, xfrom, yfrom, xifrom, yifrom);
    gwy_data_view_coords_real_to_xy(data_view, xto, yto, xito, yito);
    gwy_data_view_coords_xy_cut_line(data_view, xifrom, yifrom, xito, yito);
}

/* GwySelection has no public interface for direct access to coordinates, even
 * for reading, but layers are friends.  Wrap the ugliness in a function... */
G_GNUC_UNUSED
static inline const gdouble*
gwy_vector_layer_selection_data(GwyVectorLayer *layer)
{
    return &g_array_index(layer->selection->objects, gdouble, 0);
}

G_GNUC_UNUSED
static inline guint
gwy_vector_layer_n_selected(GwyVectorLayer *layer)
{
    return gwy_selection_get_data(layer->selection, NULL);
}

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
