/*
 *  $Id: projective.c 24731 2022-03-23 20:01:03Z yeti-dn $
 *  Copyright (C) 2021-2022 David Necas (Yeti).
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-layer.h>

#include "layer.h"

#define GWY_TYPE_LAYER_PROJECTIVE            (gwy_layer_projective_get_type())
#define GWY_LAYER_PROJECTIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_LAYER_PROJECTIVE, GwyLayerProjective))
#define GWY_IS_LAYER_PROJECTIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_LAYER_PROJECTIVE))
#define GWY_LAYER_PROJECTIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_LAYER_PROJECTIVE, GwyLayerProjectiveClass))

#define GWY_TYPE_SELECTION_PROJECTIVE            (gwy_selection_projective_get_type())
#define GWY_SELECTION_PROJECTIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_SELECTION_PROJECTIVE, GwySelectionProjective))
#define GWY_IS_SELECTION_PROJECTIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_SELECTION_PROJECTIVE))
#define GWY_SELECTION_PROJECTIVE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_SELECTION_PROJECTIVE, GwySelectionProjectiveClass))

/* The 4 points are ordered clockwise. */
enum {
    OBJECT_SIZE = 8
};

enum {
    PROP_0,
    PROP_N_LINES,
    PROP_CONVEX,
};

typedef struct _GwyLayerProjective          GwyLayerProjective;
typedef struct _GwyLayerProjectiveClass     GwyLayerProjectiveClass;
typedef struct _GwySelectionProjective      GwySelectionProjective;
typedef struct _GwySelectionProjectiveClass GwySelectionProjectiveClass;

struct _GwyLayerProjective {
    GwyVectorLayer parent_instance;

    GdkCursor *near_cursor;
    GdkCursor *move_cursor;

    /* Properties */
    guint n_lines;
    gboolean convex;

    /* Dynamic state */
    gint endpoint;
    gdouble from_unit_square[9];
};

struct _GwyLayerProjectiveClass {
    GwyVectorLayerClass parent_class;
};

struct _GwySelectionProjective {
    GwySelection parent_instance;
};

struct _GwySelectionProjectiveClass {
    GwySelectionClass parent_class;
};

static gboolean module_register                     (void);
static GType    gwy_layer_projective_get_type       (void)                       G_GNUC_CONST;
static GType    gwy_selection_projective_get_type   (void)                       G_GNUC_CONST;
static gboolean gwy_selection_projective_crop_object(GwySelection *selection,
                                                     gint i,
                                                     gpointer user_data);
static void     gwy_selection_projective_crop       (GwySelection *selection,
                                                     gdouble xmin,
                                                     gdouble ymin,
                                                     gdouble xmax,
                                                     gdouble ymax);
static void     gwy_selection_projective_move       (GwySelection *selection,
                                                     gdouble vx,
                                                     gdouble vy);
static void     gwy_layer_projective_set_property   (GObject *object,
                                                     guint prop_id,
                                                     const GValue *value,
                                                     GParamSpec *pspec);
static void     gwy_layer_projective_get_property   (GObject*object,
                                                     guint prop_id,
                                                     GValue *value,
                                                     GParamSpec *pspec);
static void     gwy_layer_projective_draw           (GwyVectorLayer *layer,
                                                     GdkDrawable *drawable,
                                                     GwyRenderingTarget target);
static void     gwy_layer_projective_draw_object    (GwyVectorLayer *layer,
                                                     GdkDrawable *drawable,
                                                     GwyRenderingTarget target,
                                                     gint id);
static gboolean gwy_layer_projective_motion_notify  (GwyVectorLayer *layer,
                                                     GdkEventMotion *event);
static gboolean gwy_layer_projective_button_pressed (GwyVectorLayer *layer,
                                                     GdkEventButton *event);
static gboolean gwy_layer_projective_button_released(GwyVectorLayer *layer,
                                                     GdkEventButton *event);
static void     gwy_layer_projective_set_n_lines    (GwyLayerProjective *layer,
                                                     guint nlines);
static void     gwy_layer_projective_set_convex     (GwyLayerProjective *layer,
                                                     gboolean setting);
static void     gwy_layer_projective_realize        (GwyDataViewLayer *dlayer);
static void     gwy_layer_projective_unrealize      (GwyDataViewLayer *dlayer);
static gint     gwy_layer_projective_near_point     (GwyVectorLayer *layer,
                                                     gdouble xreal,
                                                     gdouble yreal);
static gboolean tetragon_is_convex                  (const gdouble *xy);
static gboolean project                             (const gdouble *xyfrom,
                                                     const gdouble *matrix,
                                                     gdouble *xyto);
static gboolean solve_projection_from_unit_square   (const gdouble *xy,
                                                     gdouble *matrix);

/* Allow to express intent. */
#define gwy_layer_projective_undraw        gwy_layer_projective_draw
#define gwy_layer_projective_undraw_object gwy_layer_projective_draw_object

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Layer allowing selection of a projective plane."),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, projective)

G_DEFINE_TYPE(GwySelectionProjective, gwy_selection_projective, GWY_TYPE_SELECTION)
G_DEFINE_TYPE(GwyLayerProjective, gwy_layer_projective, GWY_TYPE_VECTOR_LAYER)

static gboolean
module_register(void)
{
    gwy_layer_func_register(GWY_TYPE_LAYER_PROJECTIVE);
    return TRUE;
}

static void
gwy_selection_projective_class_init(GwySelectionProjectiveClass *klass)
{
    GwySelectionClass *sel_class = GWY_SELECTION_CLASS(klass);

    sel_class->object_size = OBJECT_SIZE;
    sel_class->crop = gwy_selection_projective_crop;
    sel_class->move = gwy_selection_projective_move;
}

static void
gwy_layer_projective_class_init(GwyLayerProjectiveClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GwyDataViewLayerClass *layer_class = GWY_DATA_VIEW_LAYER_CLASS(klass);
    GwyVectorLayerClass *vector_class = GWY_VECTOR_LAYER_CLASS(klass);

    gwy_debug("");

    gobject_class->set_property = gwy_layer_projective_set_property;
    gobject_class->get_property = gwy_layer_projective_get_property;

    layer_class->realize = gwy_layer_projective_realize;
    layer_class->unrealize = gwy_layer_projective_unrealize;

    vector_class->selection_type = GWY_TYPE_SELECTION_PROJECTIVE;
    vector_class->draw = gwy_layer_projective_draw;
    vector_class->motion_notify = gwy_layer_projective_motion_notify;
    vector_class->button_press = gwy_layer_projective_button_pressed;
    vector_class->button_release = gwy_layer_projective_button_released;

    g_object_class_install_property
        (gobject_class,
         PROP_N_LINES,
         g_param_spec_uint("n-lines",
                           "N lines",
                           "Number of lattice lines to draw beside the "
                           "central ones",
                           0, 1024, 3,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_CONVEX,
         g_param_spec_boolean("convex",
                              "Convex",
                              "Allow only convex tetragons to be drawn",
                              TRUE,
                              G_PARAM_READWRITE));
}

static void
gwy_selection_projective_init(GwySelectionProjective *selection)
{
    /* Set max. number of objects to one */
    g_array_set_size(GWY_SELECTION(selection)->objects, OBJECT_SIZE);
}

static gboolean
gwy_selection_projective_crop_object(GwySelection *selection,
                                     gint i,
                                     gpointer user_data)
{
    const gdouble *minmax = (const gdouble*)user_data;
    gdouble xy[OBJECT_SIZE];

    gwy_selection_get_object(selection, i, xy);
    return (fmin(fmin(xy[0], xy[2]), fmin(xy[4], xy[6])) >= minmax[0]
            && fmin(fmin(xy[1], xy[3]), fmin(xy[5], xy[7])) >= minmax[1]
            && fmax(fmax(xy[0], xy[2]), fmax(xy[4], xy[6])) <= minmax[2]
            && fmax(fmax(xy[1], xy[3]), fmax(xy[5], xy[7])) <= minmax[3]);
}

static void
gwy_selection_projective_crop(GwySelection *selection,
                              gdouble xmin,
                              gdouble ymin,
                              gdouble xmax,
                              gdouble ymax)
{
    gdouble minmax[4] = { xmin, ymin, xmax, ymax };

    gwy_selection_filter(selection, gwy_selection_projective_crop_object, minmax);
}

static void
gwy_selection_projective_move(GwySelection *selection,
                              gdouble vx,
                              gdouble vy)
{
    gdouble *data = (gdouble*)selection->objects->data;
    guint i, n = selection->objects->len/OBJECT_SIZE;

    for (i = 0; i < n; i++) {
        data[OBJECT_SIZE*i + 0] += vx;
        data[OBJECT_SIZE*i + 1] += vy;
        data[OBJECT_SIZE*i + 2] += vx;
        data[OBJECT_SIZE*i + 3] += vy;
        data[OBJECT_SIZE*i + 4] += vx;
        data[OBJECT_SIZE*i + 5] += vy;
        data[OBJECT_SIZE*i + 6] += vx;
        data[OBJECT_SIZE*i + 7] += vy;
    }
}

static void
gwy_layer_projective_init(GwyLayerProjective *layer)
{
    layer->n_lines = 3;
    layer->convex = TRUE;
    layer->endpoint = -1;

    gwy_clear(layer->from_unit_square, G_N_ELEMENTS(layer->from_unit_square));
    layer->from_unit_square[0] = 1.0;
    layer->from_unit_square[4] = 1.0;
    layer->from_unit_square[8] = 1.0;
}

static void
gwy_layer_projective_set_property(GObject *object,
                                  guint prop_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
    GwyLayerProjective *layer = GWY_LAYER_PROJECTIVE(object);

    switch (prop_id) {
        case PROP_N_LINES:
        gwy_layer_projective_set_n_lines(layer, g_value_get_uint(value));
        break;

        case PROP_CONVEX:
        gwy_layer_projective_set_convex(layer, g_value_get_boolean(value));
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_layer_projective_get_property(GObject*object,
                                  guint prop_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
    GwyLayerProjective *layer = GWY_LAYER_PROJECTIVE(object);

    switch (prop_id) {
        case PROP_N_LINES:
        g_value_set_uint(value, layer->n_lines);
        break;

        case PROP_CONVEX:
        g_value_set_boolean(value, layer->convex);
        break;

        default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gwy_layer_projective_draw(GwyVectorLayer *layer,
                          GdkDrawable *drawable,
                          GwyRenderingTarget target)
{
    gint n;

    if (!layer->selection)
        return;

    g_return_if_fail(GDK_IS_DRAWABLE(drawable));

    if (!GWY_LAYER_PROJECTIVE(layer)->n_lines)
        return;

    n = gwy_vector_layer_n_selected(layer);
    if (n)
        gwy_layer_projective_draw_object(layer, drawable, target, 0);
}

static void
gwy_layer_projective_draw_object(GwyVectorLayer *layer,
                                 GdkDrawable *drawable,
                                 GwyRenderingTarget target,
                                 gint id)
{
    GwyLayerProjective *layer_projective;
    GwyDataView *data_view;
    gdouble xy[OBJECT_SIZE];
    gboolean has_object;
    gint xi0, yi0, xi1, yi1, width, height, nlines, i;
    gdouble xsize, ysize;

    g_return_if_fail(GDK_IS_DRAWABLE(drawable));
    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_if_fail(data_view);

    gwy_debug("%d", id);
    has_object = gwy_selection_get_object(layer->selection, id, xy);
    g_return_if_fail(has_object);

    gwy_data_view_get_real_data_sizes(data_view, &xsize, &ysize);
    gdk_drawable_get_size(drawable, &width, &height);

    layer_projective = GWY_LAYER_PROJECTIVE(layer);
    solve_projection_from_unit_square(xy, layer_projective->from_unit_square);

    nlines = GWY_LAYER_PROJECTIVE(layer)->n_lines;

    gdk_gc_set_line_attributes(layer->gc, 1, GDK_LINE_SOLID,
                               GDK_CAP_BUTT, GDK_JOIN_BEVEL);
    for (i = 0; i < OBJECT_SIZE/2; i++) {
        gint ii = (i + 1) % (OBJECT_SIZE/2);
        gwy_vector_layer_transform_line_to_target(layer, drawable, target,
                                                  xy[2*i], xy[2*i+1],
                                                  xy[2*ii], xy[2*ii+1],
                                                  &xi0, &yi0, &xi1, &yi1);
        gdk_draw_line(drawable, layer->gc, xi0, yi0, xi1, yi1);
    }

    gdk_gc_set_line_attributes(layer->gc, 1, GDK_LINE_ON_OFF_DASH,
                               GDK_CAP_BUTT, GDK_JOIN_BEVEL);

    xy[5] = 0.0;
    xy[7] = 1.0;
    for (i = 0; i < nlines; i++) {
        xy[4] = xy[6] = (i + 1.0)/(nlines + 1.0);
        project(xy + 4, layer_projective->from_unit_square, xy + 0);
        project(xy + 6, layer_projective->from_unit_square, xy + 2);
        gwy_vector_layer_transform_line_to_target(layer, drawable, target,
                                                  xy[0], xy[1], xy[2], xy[3],
                                                  &xi0, &yi0, &xi1, &yi1);
        gdk_draw_line(drawable, layer->gc, xi0, yi0, xi1, yi1);
    }

    xy[4] = 0.0;
    xy[6] = 1.0;
    for (i = 0; i < nlines; i++) {
        xy[5] = xy[7] = (i + 1.0)/(nlines + 1.0);
        project(xy + 4, layer_projective->from_unit_square, xy + 0);
        project(xy + 6, layer_projective->from_unit_square, xy + 2);
        gwy_vector_layer_transform_line_to_target(layer, drawable, target,
                                                  xy[0], xy[1], xy[2], xy[3],
                                                  &xi0, &yi0, &xi1, &yi1);
        gdk_draw_line(drawable, layer->gc, xi0, yi0, xi1, yi1);
    }
}

static gboolean
gwy_layer_projective_motion_notify(GwyVectorLayer *layer,
                                   GdkEventMotion *event)
{
    GwyDataView *data_view;
    GwyLayerProjective *layer_projective;
    GdkWindow *window;
    gint x, y, i, ipt;
    gdouble xreal, yreal, xy[OBJECT_SIZE];

    if (!layer->selection)
        return FALSE;

    if (!layer->editable)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);
    window = GTK_WIDGET(data_view)->window;
    layer_projective = GWY_LAYER_PROJECTIVE(layer);

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

    if (!layer->button || layer->selecting == -1) {
        ipt = gwy_layer_projective_near_point(layer, xreal, yreal);
        gwy_debug("near point: %d", ipt);
        gdk_window_set_cursor(window,
                              ipt > -1 ? layer_projective->near_cursor : NULL);
        return FALSE;
    }

    ipt = layer_projective->endpoint;
    if (ipt == -1)
        return FALSE;

    gwy_selection_get_object(layer->selection, i, xy);
    xy[2*ipt+0] = xreal;
    xy[2*ipt+1] = yreal;
    if (layer_projective->convex && !tetragon_is_convex(xy))
        return FALSE;

    g_assert(layer->selecting != -1);
    gwy_layer_projective_undraw_object(layer, window,
                                       GWY_RENDERING_TARGET_SCREEN, i);
    gwy_selection_set_object(layer->selection, i, xy);
    gwy_layer_projective_draw_object(layer, window,
                                     GWY_RENDERING_TARGET_SCREEN, i);

    return FALSE;
}

static gboolean
gwy_layer_projective_button_pressed(GwyVectorLayer *layer,
                                    GdkEventButton *event)
{
    GwyLayerProjective *layer_projective;
    GwyDataView *data_view;
    GdkWindow *window;
    gint x, y, i, ipt;
    gdouble xreal, yreal;

    if (!layer->editable)
        return FALSE;

    if (!layer->selection)
        return FALSE;

    if (!gwy_vector_layer_n_selected(layer))
        return FALSE;

    if (event->button != 1)
        return FALSE;

    data_view = GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent);
    g_return_val_if_fail(data_view, FALSE);
    window = GTK_WIDGET(data_view)->window;
    layer_projective = GWY_LAYER_PROJECTIVE(layer);

    x = event->x;
    y = event->y;
    gwy_data_view_coords_xy_clamp(data_view, &x, &y);
    gwy_debug("[%d,%d]", x, y);
    /* do nothing when we are outside */
    if (x != event->x || y != event->y)
        return FALSE;

    /* FIXME: Now we require the caller to preset some object for the user
     * to edit. */
    gwy_data_view_coords_xy_to_real(data_view, x, y, &xreal, &yreal);
    ipt = gwy_layer_projective_near_point(layer, xreal, yreal);
    if (ipt < 0)
        return FALSE;

    layer_projective->endpoint = ipt % 4;   /* Assume just one object. */
    i = 0;
    layer->selecting = i;
    layer->button = event->button;
    gdk_window_set_cursor(window, GWY_LAYER_PROJECTIVE(layer)->move_cursor);
    gwy_vector_layer_object_chosen(layer, layer->selecting);

    return FALSE;
}

static gboolean
gwy_layer_projective_button_released(GwyVectorLayer *layer,
                                     GdkEventButton *event)
{
    GwyLayerProjective *layer_projective;
    GwyDataView *data_view;
    GdkWindow *window;
    gint x, y, i, ipt;
    gdouble xreal, yreal, xy[OBJECT_SIZE];

    if (!layer->selection)
        return FALSE;

    if (!layer->button)
        return FALSE;

    layer_projective = GWY_LAYER_PROJECTIVE(layer);
    ipt = layer_projective->endpoint;
    if (ipt < 0)
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
    gwy_selection_get_object(layer->selection, i, xy);
    xy[2*ipt+0] = xreal;
    xy[2*ipt+1] = yreal;

    if (!layer_projective->convex || tetragon_is_convex(xy)) {
        gwy_layer_projective_undraw_object(layer, window,
                                           GWY_RENDERING_TARGET_SCREEN, i);
        gwy_selection_set_object(layer->selection, i, xy);
        gwy_layer_projective_draw_object(layer, window,
                                         GWY_RENDERING_TARGET_SCREEN, i);
    }

    layer->selecting = -1;
    layer_projective->endpoint = -1;

    ipt = gwy_layer_projective_near_point(layer, xreal, yreal);
    gdk_window_set_cursor(window,
                          ipt > -1 ? layer_projective->near_cursor : NULL);

    gwy_selection_finished(layer->selection);

    return FALSE;
}

static void
gwy_layer_projective_set_n_lines(GwyLayerProjective *layer, guint nlines)
{
    GwyVectorLayer *vector_layer;
    GtkWidget *parent;

    g_return_if_fail(GWY_IS_LAYER_PROJECTIVE(layer));
    vector_layer = GWY_VECTOR_LAYER(layer);
    parent = GWY_DATA_VIEW_LAYER(layer)->parent;

    if (nlines == layer->n_lines)
        return;

    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_projective_undraw(vector_layer, parent->window,
                                    GWY_RENDERING_TARGET_SCREEN);
    layer->n_lines = nlines;
    if (parent && GTK_WIDGET_REALIZED(parent))
        gwy_layer_projective_draw(vector_layer, parent->window,
                                  GWY_RENDERING_TARGET_SCREEN);
    g_object_notify(G_OBJECT(layer), "n-lines");
}

static void
gwy_layer_projective_set_convex(GwyLayerProjective *layer, gboolean setting)
{
    g_return_if_fail(GWY_IS_LAYER_PROJECTIVE(layer));

    if (!setting == !layer->convex)
        return;

    /* FIXME: We would like to enforce the property if the caller sets
     * convex, but also sets a non-convex selection.  But how exactly? */
    layer->convex = setting;
    g_object_notify(G_OBJECT(layer), "convex");
}

static void
gwy_layer_projective_realize(GwyDataViewLayer *dlayer)
{
    GwyLayerProjective *layer;
    GdkDisplay *display;

    gwy_debug("");

    GWY_DATA_VIEW_LAYER_CLASS(gwy_layer_projective_parent_class)->realize(dlayer);
    layer = GWY_LAYER_PROJECTIVE(dlayer);
    display = gtk_widget_get_display(dlayer->parent);
    layer->move_cursor = gdk_cursor_new_for_display(display, GDK_CROSS);
    layer->near_cursor = gdk_cursor_new_for_display(display, GDK_DOTBOX);
}

static void
gwy_layer_projective_unrealize(GwyDataViewLayer *dlayer)
{
    GwyLayerProjective *layer;

    gwy_debug("");

    layer = GWY_LAYER_PROJECTIVE(dlayer);
    gdk_cursor_unref(layer->move_cursor);
    gdk_cursor_unref(layer->near_cursor);

    GWY_DATA_VIEW_LAYER_CLASS(gwy_layer_projective_parent_class)->unrealize(dlayer);
}

static gint
gwy_layer_projective_near_point(GwyVectorLayer *layer,
                                gdouble xreal, gdouble yreal)
{
    gdouble d2min, metric[4];
    gint i, n;

    if (!(n = gwy_vector_layer_n_selected(layer)) || layer->focus >= n)
        return -1;

    gwy_data_view_get_metric(GWY_DATA_VIEW(GWY_DATA_VIEW_LAYER(layer)->parent),
                             metric);
    if (layer->focus >= 0) {
        gdouble xy[OBJECT_SIZE];

        gwy_selection_get_object(layer->selection, layer->focus, xy);
        i = gwy_math_find_nearest_point(xreal, yreal, &d2min, 4, xy, metric);
    }
    else {
        const gdouble *xy = gwy_vector_layer_selection_data(layer);
        i = gwy_math_find_nearest_point(xreal, yreal, &d2min, 4*n, xy, metric);
    }
    if (i < 0 || d2min > PROXIMITY_DISTANCE*PROXIMITY_DISTANCE)
        return -1;
    return i;
}

static gboolean
tetragon_is_convex(const gdouble *xy)
{
    gdouble cp1, cp2, cpmax, cpmin;
    gdouble v[OBJECT_SIZE];
    gint i, ii;

    /* Check if the two vectors seem to form non-degenerate angles and the
     * sizes are also sufficiently non-degenerate. */
    for (i = 0; i < OBJECT_SIZE/2; i++) {
        ii = (i + 1) % (OBJECT_SIZE/2);

        v[2*i] = xy[2*ii] - xy[2*i];
        v[2*i + 1] = xy[2*ii + 1] - xy[2*i + 1];
    }
    cpmax = 0.0;
    cpmin = G_MAXDOUBLE;
    for (i = 0; i < OBJECT_SIZE/2; i++) {
        ii = (i + 1) % (OBJECT_SIZE/2);
        cp1 = v[2*i]*v[2*ii + 1];
        cp2 = v[2*i + 1]*v[2*ii];
        cpmin = fmin(cp1 - cp2, cpmin);
        cpmax = fmax(cpmax, fmax(fabs(cp1), fabs(cp2)));
        if (cpmin <= 1e-9*cpmax)
            return FALSE;
    }

    return TRUE;
}

static gboolean
project(const gdouble *xyfrom, const gdouble *matrix, gdouble *xyto)
{
    const gdouble *mx = matrix, *my = matrix + 3, *m1 = matrix + 6;
    gdouble x = xyfrom[0], y = xyfrom[1], d;

    d = m1[0]*x + m1[1]*y + m1[2];
    xyto[0] = (mx[0]*x + mx[1]*y + mx[2])/d;
    xyto[1] = (my[0]*x + my[1]*y + my[2])/d;

    if (fabs(d) < 1e-12*(fabs(m1[0]*x) + fabs(m1[1]*y) + fabs(m1[2])))
        return FALSE;

    return TRUE;
}

static gboolean
solve_projection(const gdouble *xyfrom,
                 const gdouble *xyto,
                 gdouble *matrix)
{
    gdouble a[64], rhs[8];
    guint i;

    gwy_clear(a, 64);
    for (i = 0; i < 4; i++) {
        gdouble xf = xyfrom[2*i + 0], yf = xyfrom[2*i + 1];
        gdouble xt = xyto[2*i + 0], yt = xyto[2*i + 1];
        gdouble *axrow = a + 16*i, *ayrow = axrow + 8, *r = rhs + 2*i;

        axrow[0] = ayrow[3] = xf;
        axrow[1] = ayrow[4] = yf;
        axrow[2] = ayrow[5] = 1.0;
        axrow[6] = -xf*xt;
        axrow[7] = -yf*xt;
        ayrow[6] = -xf*yt;
        ayrow[7] = -yf*yt;
        r[0] = xt;
        r[1] = yt;
    }

    if (!gwy_math_lin_solve_rewrite(8, a, rhs, matrix))
        return FALSE;

    matrix[8] = 1.0;
    return TRUE;
}

static gboolean
solve_projection_from_unit_square(const gdouble *xy, gdouble *matrix)
{
    static const gdouble unit_square[OBJECT_SIZE] = {
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0,
    };

    return solve_projection(unit_square, xy, matrix);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
