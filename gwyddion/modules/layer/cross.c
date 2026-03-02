/*
 *  $Id: cross.c 24732 2022-03-23 20:01:21Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti).
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

#include "config.h"
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-layer.h>

#include "layer.h"

#define GWY_TYPE_LAYER_CROSS            (gwy_layer_cross_get_type())
#define GWY_LAYER_CROSS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_LAYER_CROSS, GwyLayerCross))
#define GWY_IS_LAYER_CROSS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_LAYER_CROSS))
#define GWY_LAYER_CROSS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_LAYER_CROSS, GwyLayerCrossClass))

#define GWY_TYPE_SELECTION_CROSS            (gwy_selection_cross_get_type())
#define GWY_SELECTION_CROSS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_SELECTION_CROSS, GwySelectionCross))
#define GWY_IS_SELECTION_CROSS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_SELECTION_CROSS))
#define GWY_SELECTION_CROSS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_SELECTION_CROSS, GwySelectionCrossClass))

enum {
    OBJECT_SIZE = 2
};

enum {
    PROP_0,
    PROP_THICKNESS,
    PROP_DRAW_VERTICAL,
    PROP_DRAW_HORIZONTAL,
};

typedef enum {
    MOVEMENT_NONE       = 0,
    MOVEMENT_HORIZONTAL = (1 << 0),
    MOVEMENT_VERTICAL   = (1 << 1),
    MOVEMENT_BOTH       = MOVEMENT_HORIZONTAL | MOVEMENT_VERTICAL,
} MovementType;

typedef struct _GwyLayerCross          GwyLayerCross;
typedef struct _GwyLayerCrossClass     GwyLayerCrossClass;
typedef struct _GwySelectionCross      GwySelectionCross;
typedef struct _GwySelectionCrossClass GwySelectionCrossClass;

struct _GwyLayerCross {
    GwyVectorLayer parent_instance;

    GdkCursor *near_cursor;
    GdkCursor *move_cursor;
    GdkCursor *hnear_cursor;
    GdkCursor *vnear_cursor;

    /* Dynamic state */
    MovementType movement;
    gdouble origxy[OBJECT_SIZE];

    /* Properties */
    gboolean draw_horizontal;
    gboolean draw_vertical;
    guint thickness;
};

struct _GwyLayerCrossClass {
    GwyVectorLayerClass parent_class;
};

struct _GwySelectionCross {
    GwySelection parent_instance;
};

struct _GwySelectionCrossClass {
    GwySelectionClass parent_class;
};

static gboolean module_register                    (void);
static GType    gwy_layer_cross_get_type           (void)                       G_GNUC_CONST;
static GType    gwy_selection_cross_get_type       (void)                       G_GNUC_CONST;
static gboolean gwy_selection_cross_crop_object    (GwySelection *selection,
                                                    gint i,
                                                    gpointer user_data);
static void     gwy_selection_cross_crop           (GwySelection *selection,
                                                    gdouble xmin,
                                                    gdouble ymin,
                                                    gdouble xmax,
                                                    gdouble ymax);
static void     gwy_selection_cross_move           (GwySelection *selection,
                                                    gdouble vx,
                                                    gdouble vy);
static void     gwy_layer_cross_set_property       (GObject *object,
                                                    guint prop_id,
                                                    const GValue *value,
                                                    GParamSpec *pspec);
static void     gwy_layer_cross_get_property       (GObject*object,
                                                    guint prop_id,
                                                    GValue *value,
                                                    GParamSpec *pspec);
static void     gwy_layer_cross_draw               (GwyVectorLayer *layer,
                                                    GdkDrawable *drawable,
                                                    GwyRenderingTarget target);
static void     gwy_layer_cross_draw_object        (GwyVectorLayer *layer,
                                                    GdkDrawable *drawable,
                                                    GwyRenderingTarget target,
                                                    gint i);
static gboolean gwy_layer_cross_motion_notify      (GwyVectorLayer *layer,
                                                    GdkEventMotion *event);
static gboolean gwy_layer_cross_button_pressed     (GwyVectorLayer *layer,
                                                    GdkEventButton *event);
static gboolean gwy_layer_cross_button_released    (GwyVectorLayer *layer,
                                                    GdkEventButton *event);
static gboolean gwy_layer_cross_key_pressed        (GwyVectorLayer *layer,
                                                    GdkEventKey *event);
static void     gwy_layer_cross_set_draw_horizontal(GwyLayerCross *layer,
                                                    gboolean draw_horizontal);
static void     gwy_layer_cross_set_draw_vertical  (GwyLayerCross *layer,
                                                    gboolean draw_vertical);
static void     gwy_layer_cross_set_thickness      (GwyLayerCross *layer,
                                                    guint radius);
static void     gwy_layer_cross_realize            (GwyDataViewLayer *dlayer);
static void     gwy_layer_cross_unrealize          (GwyDataViewLayer *dlayer);
static gint     gwy_layer_cross_near_object        (GwyVectorLayer *layer,
                                                    gdouble xreal,
                                                    gdouble yreal,
                                                    MovementType *movement);

/* Allow to express intent. */
#define gwy_layer_cross_undraw        gwy_layer_cross_draw
#define gwy_layer_cross_undraw_object gwy_layer_cross_draw_object

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Layer allowing selection of combined horizontal and vertical lines."),
    "Yeti <yeti@gwyddion.net>",
    "1.2",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, cross)

G_DEFINE_TYPE(GwySelectionCross, gwy_selection_cross, GWY_TYPE_SELECTION)
G_DEFINE_TYPE(GwyLayerCross, gwy_layer_cross, GWY_TYPE_VECTOR_LAYER)

static gboolean
module_register(void)
{
    gwy_layer_func_register(GWY_TYPE_LAYER_CROSS);
    return TRUE;
}

static void
gwy_selection_cross_class_init(GwySelectionCrossClass *klass)
{
    GwySelectionClass *sel_class = GWY_SELECTION_CLASS(klass);

    sel_class->object_size = OBJECT_SIZE;
    sel_class->crop = gwy_selection_cross_crop;
    sel_class->move = gwy_selection_cross_move;
}

static void
gwy_layer_cross_class_init(GwyLayerCrossClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GwyDataViewLayerClass *layer_class = GWY_DATA_VIEW_LAYER_CLASS(klass);
    GwyVectorLayerClass *vector_class = GWY_VECTOR_LAYER_CLASS(klass);

    gwy_debug("");

    gobject_class->set_property = gwy_layer_cross_set_property;
    gobject_class->get_property = gwy_layer_cross_get_property;

    layer_class->realize = gwy_layer_cross_realize;
    layer_class->unrealize = gwy_layer_cross_unrealize;

    vector_class->selection_type = GWY_TYPE_SELECTION_CROSS;
    vector_class->draw = gwy_layer_cross_draw;
    vector_class->motion_notify = gwy_layer_cross_motion_notify;
    vector_class->button_press = gwy_layer_cross_button_pressed;
    vector_class->button_release = gwy_layer_cross_button_released;
    vector_class->key_press = gwy_layer_cross_key_pressed;

    g_object_class_install_property
        (gobject_class,
         PROP_DRAW_HORIZONTAL,
         g_param_spec_boolean("draw-horizontal",
                              "Draw horizontal",
                              "Whether to draw the horizontal line",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_DRAW_VERTICAL,
         g_param_spec_boolean("draw-vertical",
                              "Draw vertical",
                              "Whether to draw the vertical line",
                              TRUE,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_THICKNESS,
         g_param_spec_uint("thickness",
                           "Line thickness",
                           "Thickness marked by end-point markers.",
                           0, 1024, 1,
                           G_PARAM_READWRITE));
}

static void
gwy_selection_cross_init(GwySelectionCross *selection)
{
    /* Set max. number of objects to one */
    g_array_set_size(GWY_SELECTION(selection)->objects, OBJECT_SIZE);
}

static gboolean
gwy_selection_cross_crop_object(GwySelection *selection,
                                gint i,
                                gpointer user_data)
{
    const gdouble *minmax = (const gdouble*)user_data;
    gdouble xy[OBJECT_SIZE];

    gwy_selection_get_object(selection, i, xy);
    return (xy[1] >= minmax[1] && xy[1] <= minmax[3]
            && xy[0] >= minmax[0] && xy[0] <= minmax[2]);
}

static void
gwy_selection_cross_crop(GwySelection *selection,
                         gdouble xmin,
                         gdouble ymin,
                         gdouble xmax,
                         gdouble ymax)
{
    gdouble minmax[4] = { xmin, ymin, xmax, ymax };

    gwy_selection_filter(selection, gwy_selection_cross_crop_object, minmax);
}

static void
gwy_selection_cross_move(GwySelection *selection,
                         gdouble vx,
                         gdouble vy)
{
    gdouble *data = (gdouble*)selection->objects->data;
    guint i, n = selection->objects->len/OBJECT_SIZE;

    for (i = 0; i < n; i++) {
        data[OBJECT_SIZE*i + 0] += vx;
        data[OBJECT_SIZE*i + 1] += vy;
    }
}

static void
gwy_layer_cross_init(GwyLayerCross *layer)
{
    layer->draw_horizontal = layer->draw_vertical = TRUE;
    layer->thickness = 0;
}

static void
gwy_layer_cross_set_property(GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
    GwyLayerCross *layer = GWY_LAYER_CROSS(object);

    switch (prop_id) {
        case PROP_DRAW_HORIZONTAL:
        gwy_layer_cross_set_draw_horizontal(layer, g_value_get_boolean(value));
        break;

        case PROP_DRAW_VERTICAL:
        gwy_layer_cross_set_draw_vertical(layer, g_value_get_boolean(value));
        break;

        case PROP_THICKNESS:
        gwy_layer_cross_set_thickness(layer, g_value_get_uint(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_layer_cross_get_property(GObject*object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
    GwyLayerCross *layer = GWY_LAYER_CROSS(object);

    switch (prop_id) {
        case PROP_DRAW_HORIZONTAL:
        g_value_set_boolean(value, layer->draw_horizontal);
        break;

        case PROP_DRAW_VERTICAL:
        g_value_set_boolean(value, layer->draw_vertical);
        break;

        case PROP_THICKNESS:
        g_value_set_uint(value, layer->thickness);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_layer_cross_draw(GwyVectorLayer *layer,
                     GdkDrawable *drawable,
                     GwyRenderingTarget target)
{
    gint i, n;

    if (!layer->selection)
        return;

    g_return_if_fail(GDK_IS_DRAWABLE(drawable));

    n = gwy_vector_layer_n_selected(layer);
    for (i = 0; i < n; i++)
        gwy_layer_cross_draw_object(layer, drawable, target, i);
}

static void
gwy_layer_cross_draw_marker(GwyVectorLayer *layer,
                            GdkDrawable *drawable,
                            GwyDataView *data_view,
                            GwyRenderingTarget target,
                            const gdouble *xy,
                            gboolean draw_hmarker,
                            gboolean draw_vmarker)
{
    gint xc, yc, xmin, xmax, ymin, ymax, size;
    gint dwidth, dheight, xsize, ysize;
    gdouble xreal, yreal, xm, ym;

    if (!draw_hmarker && !draw_vmarker)
        return;

    gdk_drawable_get_size(drawable, &dwidth, &dheight);
    gwy_data_view_get_pixel_data_sizes(data_view, &xsize, &ysize);
    switch (target) {
        case GWY_RENDERING_TARGET_SCREEN:
        xm = dwidth/(xsize*(gwy_data_view_get_hexcess(data_view) + 1.0));
        ym = dheight/(ysize*(gwy_data_view_get_vexcess(data_view) + 1.0));
        gwy_data_view_coords_real_to_xy(data_view, xy[0], xy[1], &xc, &yc);
        xmin = xc - CROSS_SIZE + 1;
        xmax = xc + CROSS_SIZE - 1;
        ymin = yc - CROSS_SIZE + 1;
        ymax = yc + CROSS_SIZE - 1;
        gwy_data_view_coords_xy_clamp(data_view, &xmin, &ymin);
        gwy_data_view_coords_xy_clamp(data_view, &xmax, &ymax);
        break;

        case GWY_RENDERING_TARGET_PIXMAP_IMAGE:
        xm = (gdouble)dwidth/xsize;
        ym = (gdouble)dheight/ysize;
        size = GWY_ROUND(fmax(sqrt(xm*ym)*(CROSS_SIZE - 1), 1.0));
        gwy_data_view_get_real_data_sizes(data_view, &xreal, &yreal);
        xc = floor(xy[0]*dwidth/xreal);
        yc = floor(xy[1]*dheight/yreal);

        xmin = xc - size;
        xmax = xc + size;
        ymin = yc - size;
        ymax = yc + size;
        break;

        default:
        g_return_if_reached();
        break;
    }

    if (draw_hmarker)
        gdk_draw_line(drawable, layer->gc, xmin, yc, xmax, yc);
    if (draw_vmarker)
        gdk_draw_line(drawable, layer->gc, xc, ymin, xc, ymax);
}

static void
gwy_layer_cross_draw_horizontal(GwyVectorLayer *layer,
                                GdkDrawable *drawable,
                                GwyDataView *data_view,
                                GwyRenderingTarget target,
                                const gdouble *xy)
{
    GwyLayerCross *layer_cross;
    gint thickness, coord, width, height, xsize, ysize, xl0, yl0, xl1, yl1;
    gdouble xreal, yreal, ym;

    gwy_data_view_get_real_data_sizes(data_view, &xreal, &yreal);
    gwy_data_view_get_pixel_data_sizes(data_view, &xsize, &ysize);
    gdk_drawable_get_size(drawable, &width, &height);

    gwy_vector_layer_transform_line_to_target(layer, drawable, target,
                                              0.0, xy[1], xreal, xy[1],
                                              &xl0, &yl0, &xl1, &yl1);
    if (target == GWY_RENDERING_TARGET_SCREEN)
        ym = height/(ysize*(gwy_data_view_get_vexcess(data_view) + 1.0));
    else
        ym = (gdouble)height/yreal;

    layer_cross = GWY_LAYER_CROSS(layer);
    thickness = layer_cross->thickness;
    coord = yl0;
    if (thickness > 1) {
        if (width > 2)
            gdk_draw_line(drawable, layer->gc, xl0+1, yl0, xl1-1, yl1);

        yl0 = GWY_ROUND(coord - 0.5*ym*thickness);
        yl1 = GWY_ROUND(coord + 0.5*ym*thickness);
        if (target == GWY_RENDERING_TARGET_SCREEN) {
            gwy_data_view_coords_xy_clamp(data_view, &xl0, &yl0);
            gwy_data_view_coords_xy_clamp(data_view, &xl0, &yl1);
        }
        gdk_draw_line(drawable, layer->gc, xl0, yl0, xl0, yl1);

        yl0 = GWY_ROUND(coord - 0.5*ym*thickness);
        yl1 = GWY_ROUND(coord + 0.5*ym*thickness);
        if (target == GWY_RENDERING_TARGET_SCREEN) {
            gwy_data_view_coords_xy_clamp(data_view, &xl1, &yl0);
            gwy_data_view_coords_xy_clamp(data_view, &xl1, &yl1);
        }
        gdk_draw_line(drawable, layer->gc, xl1, yl0, xl1, yl1);
    }
    else
        gdk_draw_line(drawable, layer->gc, xl0, yl0, xl1, yl1);
}

static void
gwy_layer_cross_draw_vertical(GwyVectorLayer *layer,
                              GdkDrawable *drawable,
                              GwyDataView *data_view,
                              GwyRenderingTarget target,
                              const gdouble *xy)
{
    GwyLayerCross *layer_cross;
    gint thickness, coord, width, height, xsize, ysize, xl0, yl0, xl1, yl1;
    gdouble xreal, yreal, xm;

    gwy_data_view_get_real_data_sizes(data_view, &xreal, &yreal);
    gwy_data_view_get_pixel_data_sizes(data_view, &xsize, &ysize);
    gdk_drawable_get_size(drawable, &width, &height);

    gwy_vector_layer_transform_line_to_target(layer, drawable, target,
                                              xy[0], 0.0, xy[0], yreal,
                                              &xl0, &yl0, &xl1, &yl1);
    if (target == GWY_RENDERING_TARGET_SCREEN)
        xm = width/(xsize*(gwy_data_view_get_hexcess(data_view) + 1.0));
    else
        xm = (gdouble)width/xreal;

    layer_cross = GWY_LAYER_CROSS(layer);
    thickness = layer_cross->thickness;
    coord = xl0;
    if (thickness > 1) {
        if (width > 2)
            gdk_draw_line(drawable, layer->gc, xl0, yl0+1, xl1, yl1-1);

        xl0 = GWY_ROUND(coord - 0.5*xm*thickness);
        xl1 = GWY_ROUND(coord + 0.5*xm*thickness);
        if (target == GWY_RENDERING_TARGET_SCREEN) {
            gwy_data_view_coords_xy_clamp(data_view, &xl0, &yl0);
            gwy_data_view_coords_xy_clamp(data_view, &xl1, &yl0);
        }
        gdk_draw_line(drawable, layer->gc, xl0, yl0, xl1, yl0);

        xl0 = GWY_ROUND(coord - 0.5*xm*thickness);
        xl1 = GWY_ROUND(coord + 0.5*xm*thickness);
        if (target == GWY_RENDERING_TARGET_SCREEN) {
            gwy_data_view_coords_xy_clamp(data_view, &xl0, &yl1);
            gwy_data_view_coords_xy_clamp(data_view, &xl1, &yl1);
        }
        gdk_draw_line(drawable, layer->gc, xl0, yl1, xl1, yl1);
    }
    else
        gdk_draw_line(drawable, layer->gc, xl0, yl0, xl1, yl1);
}

static void
gwy_layer_cross_draw_object(GwyVectorLayer *layer,
                            GdkDrawable *drawable,
                            GwyRenderingTarget target,
                            gint i)
{
    GwyDataView *data_view;
    GwyLayerCross *layer_cross;
    gdouble xy[OBJECT_SIZE];
    gboolean has_object;

    g_return_if_fail(GDK_IS_DRAWABLE(drawable));
    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_if_fail(data_view);

    layer_cross = GWY_LAYER_CROSS(layer);

    has_object = gwy_selection_get_object(layer->selection, i, xy);
    g_return_if_fail(has_object);

    if (layer_cross->draw_horizontal)
        gwy_layer_cross_draw_horizontal(layer, drawable, data_view, target, xy);
    if (layer_cross->draw_vertical)
        gwy_layer_cross_draw_vertical(layer, drawable, data_view, target, xy);

    gwy_layer_cross_draw_marker(layer, drawable, data_view, target, xy,
                                !layer_cross->draw_horizontal,
                                !layer_cross->draw_vertical);
}

static void
gwy_layer_cross_update_cursor(GwyVectorLayer *layer, GdkWindow *window,
                              gdouble xreal, gdouble yreal)
{
    MovementType movement;
    GdkCursor *cursor;
    gint i;

    i = gwy_layer_cross_near_object(layer, xreal, yreal, &movement);
    if (i >= 0 && movement == MOVEMENT_BOTH)
        cursor = GWY_LAYER_CROSS(layer)->near_cursor;
    else if (i >= 0 && movement == MOVEMENT_HORIZONTAL)
        cursor = GWY_LAYER_CROSS(layer)->hnear_cursor;
    else if (i >= 0 && movement == MOVEMENT_VERTICAL)
        cursor = GWY_LAYER_CROSS(layer)->vnear_cursor;
    else
        cursor = NULL;

    gdk_window_set_cursor(window, cursor);
}

static void
gwy_layer_cross_limit_movement(GwyLayerCross *layer_cross,
                               gdouble *xy)
{
    if (!(layer_cross->movement & MOVEMENT_VERTICAL))
        xy[1] = layer_cross->origxy[1];
    if (!(layer_cross->movement & MOVEMENT_HORIZONTAL))
        xy[0] = layer_cross->origxy[0];
}

static gboolean
gwy_layer_cross_motion_notify(GwyVectorLayer *layer,
                              GdkEventMotion *event)
{
    GwyDataView *data_view;
    GdkWindow *window;
    gint x, y, i;
    gdouble xreal, yreal, xy[OBJECT_SIZE];

    if (!layer->selection)
        return FALSE;

    /* FIXME: No cursor change hint -- a bit too crude? */
    if (!layer->editable)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);
    window = GTK_WIDGET(data_view)->window;

    i = layer->selecting;
    if (event->is_hint)
        gdk_window_get_pointer(window, &x, &y, NULL);
    else {
        x = event->x;
        y = event->y;
    }
    gwy_debug("x = %d, y = %d", x, y);
    gwy_data_view_coords_xy_clamp(data_view, &x, &y);
    gwy_data_view_coords_xy_to_real(data_view, x, y, &xreal, &yreal);
    if (i > -1)
        gwy_selection_get_object(layer->selection, i, xy);
    if (i > -1 && xreal == xy[0] && yreal == xy[1])
        return FALSE;

    if (!layer->button) {
        gwy_layer_cross_update_cursor(layer, window, xreal, yreal);
        return FALSE;
    }

    g_assert(layer->selecting != -1);
    gwy_layer_cross_undraw_object(layer, window,
                                  GWY_RENDERING_TARGET_SCREEN, i);
    xy[0] = xreal;
    xy[1] = yreal;
    gwy_layer_cross_limit_movement(GWY_LAYER_CROSS(layer), xy);
    gwy_selection_set_object(layer->selection, i, xy);
    gwy_layer_cross_draw_object(layer, window,
                                GWY_RENDERING_TARGET_SCREEN, i);

    return FALSE;
}

static gboolean
gwy_layer_cross_button_pressed(GwyVectorLayer *layer,
                               GdkEventButton *event)
{
    GwyLayerCross *layer_cross = GWY_LAYER_CROSS(layer);
    GwyDataView *data_view;
    GdkWindow *window;
    gint x, y, i;
    gdouble xreal, yreal, xy[OBJECT_SIZE];
    MovementType movement;

    if (!layer->selection)
        return FALSE;

    if (event->button != 1)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);
    window = GTK_WIDGET(data_view)->window;

    x = event->x;
    y = event->y;
    gwy_data_view_coords_xy_clamp(data_view, &x, &y);
    gwy_debug("[%d,%d]", x, y);
    /* do nothing when we are outside */
    if (x != event->x || y != event->y)
        return FALSE;

    gwy_data_view_coords_xy_to_real(data_view, x, y, &xreal, &yreal);
    xy[0] = layer_cross->origxy[0] = xreal;
    xy[1] = layer_cross->origxy[1] = yreal;

    i = gwy_layer_cross_near_object(layer, xreal, yreal, &movement);
    /* just emit "object-chosen" when selection is not editable */
    if (!layer->editable) {
        if (i >= 0)
            gwy_vector_layer_object_chosen(layer, i);
        return FALSE;
    }
    /* handle existing selection */
    if (i >= 0) {
        layer->selecting = i;
        layer_cross->movement = movement;
        gwy_selection_get_object(layer->selection, i, layer_cross->origxy);
        gwy_layer_cross_undraw_object(layer, window,
                                      GWY_RENDERING_TARGET_SCREEN, i);
    }
    else {
        /* add an object, or do nothing when maximum is reached */
        i = -1;
        if (gwy_selection_is_full(layer->selection)) {
            if (gwy_selection_get_max_objects(layer->selection) > 1)
                return FALSE;
            i = 0;
            gwy_layer_cross_undraw_object(layer, window,
                                          GWY_RENDERING_TARGET_SCREEN, i);
        }
        layer->selecting = 0;    /* avoid "update" signal emission */
        layer->selecting = gwy_selection_set_object(layer->selection, i, xy);
        layer_cross->movement = MOVEMENT_BOTH;
    }
    layer->button = event->button;
    gwy_layer_cross_draw_object(layer, window,
                                GWY_RENDERING_TARGET_SCREEN, layer->selecting);

    gdk_window_set_cursor(window, layer_cross->move_cursor);
    gwy_vector_layer_object_chosen(layer, layer->selecting);

    return FALSE;
}

static gboolean
gwy_layer_cross_button_released(GwyVectorLayer *layer,
                                GdkEventButton *event)
{
    GwyLayerCross *layer_cross = GWY_LAYER_CROSS(layer);
    GwyDataView *data_view;
    GdkWindow *window;
    gint x, y, i;
    gdouble xreal, yreal, xy[OBJECT_SIZE];

    if (!layer->selection)
        return FALSE;

    if (!layer->button)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);
    window = GTK_WIDGET(data_view)->window;

    layer->button = 0;
    x = event->x;
    y = event->y;
    i = layer->selecting;
    gwy_debug("i = %d", i);
    gwy_data_view_coords_xy_clamp(data_view, &x, &y);
    gwy_data_view_coords_xy_to_real(data_view, x, y, &xreal, &yreal);
    gwy_layer_cross_undraw_object(layer, window,
                                  GWY_RENDERING_TARGET_SCREEN, i);
    xy[0] = xreal;
    xy[1] = yreal;
    gwy_layer_cross_limit_movement(layer_cross, xy);
    gwy_selection_set_object(layer->selection, i, xy);
    gwy_layer_cross_draw_object(layer, window, GWY_RENDERING_TARGET_SCREEN, i);

    layer->selecting = -1;
    gwy_layer_cross_update_cursor(layer, window, xreal, yreal);
    gwy_selection_finished(layer->selection);

    return FALSE;
}

static gboolean
gwy_layer_cross_key_pressed(GwyVectorLayer *layer,
                            GdkEventKey *event)
{
    gboolean large_step = (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK));
    GwyDataView *data_view;
    guint keyval = event->keyval;
    gint chosen = layer->chosen, xcurr, ycurr, xnew, ynew, move_distance;
    gdouble xy[2];

    if (chosen < 0
        || chosen >= gwy_selection_get_data(layer->selection, NULL))
        return FALSE;

    if (keyval != GDK_Left && keyval != GDK_Right
        && keyval != GDK_Up && keyval != GDK_Down)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);

    gwy_selection_get_object(layer->selection, chosen, xy);
    gwy_data_view_coords_real_to_xy(data_view, xy[0], xy[1], &xcurr, &ycurr);
    xnew = xcurr;
    ynew = ycurr;
    move_distance = (large_step ? 16 : 1);
    if (keyval == GDK_Left)
        xnew -= move_distance;
    else if (keyval == GDK_Right)
        xnew += move_distance;
    else if (keyval == GDK_Up)
        ynew -= move_distance;
    else if (keyval == GDK_Down)
        ynew += move_distance;
    gwy_data_view_coords_xy_clamp(data_view, &xnew, &ynew);

    if (xnew != xcurr || ynew != ycurr) {
        gwy_data_view_coords_xy_to_real(data_view, xnew, ynew, xy+0, xy+1);
        gwy_selection_set_object(layer->selection, chosen, xy);
    }

    return TRUE;
}

static void
gwy_layer_cross_set_draw_horizontal(GwyLayerCross *layer,
                                    gboolean draw_horizontal)
{
    GwyVectorLayer *vector_layer;
    GtkWidget *parent;

    g_return_if_fail(GWY_IS_LAYER_CROSS(layer));
    vector_layer = GWY_VECTOR_LAYER(layer);
    parent = GWY_DATA_VIEW_LAYER(layer)->parent;

    if (draw_horizontal == layer->draw_horizontal)
        return;

    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_undraw(vector_layer, parent->window,
                               GWY_RENDERING_TARGET_SCREEN);
    layer->draw_horizontal = draw_horizontal;
    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_draw(vector_layer, parent->window,
                             GWY_RENDERING_TARGET_SCREEN);
    g_object_notify(G_OBJECT(layer), "draw-horizontal");
}

static void
gwy_layer_cross_set_draw_vertical(GwyLayerCross *layer,
                                    gboolean draw_vertical)
{
    GwyVectorLayer *vector_layer;
    GtkWidget *parent;

    g_return_if_fail(GWY_IS_LAYER_CROSS(layer));
    vector_layer = GWY_VECTOR_LAYER(layer);
    parent = GWY_DATA_VIEW_LAYER(layer)->parent;

    if (draw_vertical == layer->draw_vertical)
        return;

    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_undraw(vector_layer, parent->window,
                               GWY_RENDERING_TARGET_SCREEN);
    layer->draw_vertical = draw_vertical;
    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_draw(vector_layer, parent->window,
                             GWY_RENDERING_TARGET_SCREEN);
    g_object_notify(G_OBJECT(layer), "draw-vertical");
}

static void
gwy_layer_cross_set_thickness(GwyLayerCross *layer, guint thickness)
{
    GwyVectorLayer *vector_layer;
    GtkWidget *parent;

    g_return_if_fail(GWY_IS_LAYER_CROSS(layer));
    vector_layer = GWY_VECTOR_LAYER(layer);
    parent = GWY_DATA_VIEW_LAYER(layer)->parent;

    if (thickness == layer->thickness)
        return;

    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_undraw(vector_layer, parent->window,
                               GWY_RENDERING_TARGET_SCREEN);
    layer->thickness = thickness;
    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_cross_draw(vector_layer, parent->window,
                             GWY_RENDERING_TARGET_SCREEN);
    g_object_notify(G_OBJECT(layer), "thickness");
}

static void
gwy_layer_cross_realize(GwyDataViewLayer *dlayer)
{
    GwyLayerCross *layer;
    GdkDisplay *display;

    gwy_debug("");

    GWY_DATA_VIEW_LAYER_CLASS(gwy_layer_cross_parent_class)->realize(dlayer);
    layer = GWY_LAYER_CROSS(dlayer);
    display = gtk_widget_get_display(dlayer->parent);
    layer->near_cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
    layer->move_cursor = gdk_cursor_new_for_display(display, GDK_CROSS);
    layer->hnear_cursor = gdk_cursor_new_for_display(display,
                                                     GDK_SB_H_DOUBLE_ARROW);
    layer->vnear_cursor = gdk_cursor_new_for_display(display,
                                                     GDK_SB_V_DOUBLE_ARROW);
}

static void
gwy_layer_cross_unrealize(GwyDataViewLayer *dlayer)
{
    GwyLayerCross *layer;

    gwy_debug("");

    layer = GWY_LAYER_CROSS(dlayer);
    gdk_cursor_unref(layer->near_cursor);
    gdk_cursor_unref(layer->move_cursor);
    gdk_cursor_unref(layer->hnear_cursor);
    gdk_cursor_unref(layer->vnear_cursor);

    GWY_DATA_VIEW_LAYER_CLASS(gwy_layer_cross_parent_class)->unrealize(dlayer);
}

static gint
gwy_layer_cross_near_cross(GwyVectorLayer *layer,
                           gdouble xreal, gdouble yreal,
                           const gdouble *xy,
                           guint n,
                           const gdouble *metric)
{
    gdouble d2min;
    gint i, focus;

    g_return_val_if_fail(n > 0, -1);
    focus = layer->focus;

    if (focus >= 0) {
        i = gwy_math_find_nearest_point(xreal, yreal, &d2min, 1,
                                        xy + OBJECT_SIZE*focus, metric);
    }
    else
        i = gwy_math_find_nearest_point(xreal, yreal, &d2min, n, xy, metric);

    if (d2min > PROXIMITY_DISTANCE*PROXIMITY_DISTANCE)
        return -1;
    return i;
}

static gint
gwy_layer_cross_near_object(GwyVectorLayer *layer,
                            gdouble xreal, gdouble yreal,
                            MovementType *movement)
{
    GwyLayerCross *layer_cross = GWY_LAYER_CROSS(layer);
    gdouble d2min, metric[4], d, d2;
    const gdouble *xy;
    gint i, m, n, ifrom, ito, focus;

    *movement = MOVEMENT_NONE;
    if (!(n = gwy_vector_layer_n_selected(layer)) || layer->focus >= n)
        return -1;

    gwy_data_view_get_metric(GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent),
                             metric);
    xy = gwy_vector_layer_selection_data(layer);

    /* Look for the nearest object depending on what we draw.  When we draw
     * horizontal and/or vertical lines, check how close we are to them.
     * If we draw only a cross, check distance from the cross. */
    if ((i = gwy_layer_cross_near_cross(layer, xreal, yreal,
                                        xy, n, metric)) >= 0) {
        *movement = MOVEMENT_BOTH;
        return i;
    }
    if (!layer_cross->draw_horizontal && !layer_cross->draw_vertical)
        return -1;

    m = -1;
    d2min = G_MAXDOUBLE;
    focus = layer->focus;
    if (focus >= 0) {
        ifrom = focus;
        ito = focus+1;
    }
    else {
        ifrom = 0;
        ito = n;
    }

    for (i = ifrom; i < ito; i++) {
        if (layer_cross->draw_horizontal) {
            d = yreal - xy[2*i + 1];
            d2 = d*d*metric[3];
            if (d2 < d2min) {
                d2min = d2;
                *movement = MOVEMENT_VERTICAL;
                m = i;
            }
        }
        if (layer_cross->draw_vertical) {
            d = xreal - xy[2*i + 0];
            d2 = d*d*metric[0];
            if (d2 < d2min) {
                d2min = d2;
                *movement = MOVEMENT_HORIZONTAL;
                m = i;
            }
        }
    }

    if (d2min > PROXIMITY_DISTANCE*PROXIMITY_DISTANCE) {
        *movement = MOVEMENT_NONE;
        return -1;
    }

    return m;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
