/*
 *  $Id: spotremove.c 25562 2023-07-24 12:06:35Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 *  License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 *  later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along with this program; if not, write to the
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/fractals.h>
#include <libprocess/grains.h>
#include <libprocess/elliptic.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwyapp.h>

enum {
    MAX_SIZE = 82,
    SCALE = 5,
    NCOORDS = 4,
};

enum {
    PARAM_METHOD,
    PARAM_SHAPE,
    PARAM_ADAPT_COLOR_RANGE,

    MESSAGE_MISSING,
};

typedef enum {
    GWY_SPOT_REMOVE_HYPER_FLATTEN   = 0,
    GWY_SPOT_REMOVE_PSEUDO_LAPLACE  = 1,
    GWY_SPOT_REMOVE_LAPLACE         = 2,
    GWY_SPOT_REMOVE_FRACTAL         = 3,
    GWY_SPOT_REMOVE_FRACTAL_LAPLACE = 4,
    GWY_SPOT_REMOVE_ZERO            = 5,
} SpotRemoveMethod;

typedef enum {
    GWY_SPOT_REMOVE_RECTANGLE = 0,
    GWY_SPOT_REMOVE_ELLIPSE   = 1,
} SpotRemoveShape;

typedef void (*AreaFillFunc)(GwyDataField *field,
                             gint col, gint row, gint width, gint height,
                             gdouble value);

typedef struct {
    gint from;
    gint to;
    gint dest;
} Range;

typedef struct {
    gdouble z;
    gint i;
    gint j;
} PixelValue;

#define GWY_TYPE_TOOL_SPOT_REMOVER            (gwy_tool_spot_remover_get_type())
#define GWY_TOOL_SPOT_REMOVER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_SPOT_REMOVER, GwyToolSpotRemover))
#define GWY_IS_TOOL_SPOT_REMOVER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_SPOT_REMOVER))
#define GWY_TOOL_SPOT_REMOVER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_SPOT_REMOVER, GwyToolSpotRemoverClass))

typedef struct _GwyToolSpotRemover      GwyToolSpotRemover;
typedef struct _GwyToolSpotRemoverClass GwyToolSpotRemoverClass;

struct _GwyToolSpotRemover {
    GwyPlainTool parent_instance;

    GwyParams *params;

    GwyContainer *data;
    GwyDataField *detail;

    GtkWidget *zoomview;
    GwyParamTable *table;
    GwySelection *zselection;
    gulong zsel_id;

    gulong palette_id;
    gboolean complete;
    Range xr;
    Range yr;
    gint zisel[4];

    GwySIValueFormat *pixel_format;
    GtkWidget *label_real[NCOORDS];
    GtkWidget *label_pix[NCOORDS];

    /* to prevent double-update on data_changed -- badly designed code? */
    gboolean drawn;

    gboolean has_selection;
    gboolean has_zselection;

    /* potential class data */
    GType layer_type_point;
    GType layer_type_rect;
    GType layer_type_ell;
};

struct _GwyToolSpotRemoverClass {
    GwyPlainToolClass parent_class;
};

static gboolean     module_register                        (void);
static GwyParamDef* define_module_params                   (void);
static GType        gwy_tool_spot_remover_get_type         (void)                      G_GNUC_CONST;
static void         gwy_tool_spot_remover_finalize         (GObject *object);
static void         gwy_tool_spot_remover_init_dialog      (GwyToolSpotRemover *tool);
static void         gwy_tool_spot_remover_data_switched    (GwyTool *gwytool,
                                                            GwyDataView *data_view);
static void         gwy_tool_spot_remover_data_changed     (GwyPlainTool *plain_tool);
static void         gwy_tool_spot_remover_palette_changed  (GwyToolSpotRemover *tool);
static void         gwy_tool_spot_remover_response         (GwyTool *gwytool,
                                                            gint response_id);
static void         gwy_tool_spot_remover_apply            (GwyToolSpotRemover *tool);
static void         gwy_tool_spot_remover_selection_changed(GwyPlainTool *plain_tool,
                                                            gint hint);
static void         param_changed                          (GwyToolSpotRemover *tool,
                                                            gint id);
static void         resize_detail                          (GwyToolSpotRemover *tool);
static GtkWidget*   create_selection_info_table            (GwyToolSpotRemover *tool);
static void         setup_zoom_vector_layer                (GwyToolSpotRemover *tool);
static void         zselection_changed                     (GwySelection *selection,
                                                            gint hint,
                                                            GwyToolSpotRemover *tool);
static void         update_selection_info_table            (GwyToolSpotRemover *tool);
static void         draw_zoom                              (GwyToolSpotRemover *tool);
static void         adapt_colour_range                     (GwyToolSpotRemover *tool,
                                                            gboolean make_empty);
static void         update_message                         (GwyToolSpotRemover *tool);
static gboolean     find_subrange                          (gint center,
                                                            gint res,
                                                            gint size,
                                                            Range *r);
static void         blend_fractal_and_laplace              (GwyDataField *field,
                                                            GwyDataField *area,
                                                            GwyDataField *distances,
                                                            gint col,
                                                            gint row);
static void         pseudo_laplace_average                 (GwyDataField *field,
                                                            GwyDataField *mask);
static void         hyperbolic_average                     (GwyDataField *field,
                                                            GwyDataField *mask);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Spot removal tool, interpolates small parts of data (displayed on a zoomed view) using selected algorithm."),
    "Yeti <yeti@gwyddion.net>",
    "4.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, spotremove)

G_DEFINE_TYPE(GwyToolSpotRemover, gwy_tool_spot_remover, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_SPOT_REMOVER);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Hyperbolic flatten"),    GWY_SPOT_REMOVE_HYPER_FLATTEN,   },
        { N_("Pseudo-Laplace"),        GWY_SPOT_REMOVE_PSEUDO_LAPLACE,  },
        { N_("Laplace solver"),        GWY_SPOT_REMOVE_LAPLACE,         },
        { N_("Fractal interpolation"), GWY_SPOT_REMOVE_FRACTAL,         },
        { N_("Fractal-Laplace blend"), GWY_SPOT_REMOVE_FRACTAL_LAPLACE, },
        { N_("Zero"),                  GWY_SPOT_REMOVE_ZERO,            },
    };
    static const GwyEnum shapes[] = {
        { N_("Rectangle"), GWY_SPOT_REMOVE_RECTANGLE, },
        { N_("Ellipse"),   GWY_SPOT_REMOVE_ELLIPSE,   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "spotremove");
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("_Interpolation method"),
                              methods, G_N_ELEMENTS(methods), GWY_SPOT_REMOVE_PSEUDO_LAPLACE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SHAPE, "shape", _("Shape"),
                              shapes, G_N_ELEMENTS(shapes), GWY_SPOT_REMOVE_RECTANGLE);
    gwy_param_def_add_boolean(paramdef, PARAM_ADAPT_COLOR_RANGE, "adapt-color-range",
                              _("Adapt color range to detail"), TRUE);

    return paramdef;
}

static void
gwy_tool_spot_remover_class_init(GwyToolSpotRemoverClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_spot_remover_finalize;

    tool_class->stock_id = GWY_STOCK_SPOT_REMOVE;
    tool_class->title = _("Remove Spots");
    tool_class->tooltip = _("Interpolate small defects, manually selected");
    tool_class->prefix = "/module/spotremover";
    tool_class->data_switched = gwy_tool_spot_remover_data_switched;
    tool_class->response = gwy_tool_spot_remover_response;

    ptool_class->data_changed = gwy_tool_spot_remover_data_changed;
    ptool_class->selection_changed = gwy_tool_spot_remover_selection_changed;
}

static void
gwy_tool_spot_remover_finalize(GObject *object)
{
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(object);

    GWY_SIGNAL_HANDLER_DISCONNECT(GWY_PLAIN_TOOL(object)->container, tool->palette_id);
    gwy_params_save_to_settings(tool->params);
    GWY_OBJECT_UNREF(tool->params);
    GWY_OBJECT_UNREF(tool->data);
    GWY_OBJECT_UNREF(tool->detail);
    GWY_SI_VALUE_FORMAT_FREE(tool->pixel_format);

    G_OBJECT_CLASS(gwy_tool_spot_remover_parent_class)->finalize(object);
}

static void
gwy_tool_spot_remover_init(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->layer_type_point = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerPoint");
    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerRectangle");
    tool->layer_type_ell = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerEllipse");
    if (!tool->layer_type_point || !tool->layer_type_rect || !tool->layer_type_ell)
        return;

    plain_tool->lazy_updates = TRUE;
    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_VFMARKUP;

    tool->params = gwy_params_new_from_settings(define_module_params());
    tool->pixel_format = gwy_si_unit_value_format_new(1.0, 0, _("px"));

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_point, "pointer");

    tool->data = gwy_container_new();
    tool->detail = gwy_data_field_new(MAX_SIZE, MAX_SIZE, MAX_SIZE, MAX_SIZE, TRUE);
    gwy_container_set_object(tool->data, gwy_app_get_data_key_for_id(0), tool->detail);
    adapt_colour_range(tool, TRUE);

    gwy_tool_spot_remover_init_dialog(tool);
}

static void
gwy_tool_spot_remover_init_dialog(GwyToolSpotRemover *tool)
{
    static const GwyEnum shapes[] = {
        { GWY_STOCK_MASK,        GWY_SPOT_REMOVE_RECTANGLE, },
        { GWY_STOCK_MASK_CIRCLE, GWY_SPOT_REMOVE_ELLIPSE,   },
    };

    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    GtkWidget *hbox, *vbox;
    GwyParamTable *table;
    GwyPixmapLayer *layer;

    hbox = gwy_hbox_new(8);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Zoom view */
    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    tool->zoomview = gwy_data_view_new(tool->data);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(tool->zoomview), (gdouble)SCALE);
    gtk_box_pack_start(GTK_BOX(vbox), tool->zoomview, FALSE, FALSE, 0);

    layer = gwy_layer_basic_new();
    gwy_pixmap_layer_set_data_key(layer, "/0/data");
    gwy_layer_basic_set_gradient_key(GWY_LAYER_BASIC(layer), "/0/base/palette");
    gwy_layer_basic_set_range_type_key(GWY_LAYER_BASIC(layer), "/0/base/range-type");
    gwy_layer_basic_set_min_max_key(GWY_LAYER_BASIC(layer), "/0/base");
    gwy_data_view_set_base_layer(GWY_DATA_VIEW(tool->zoomview), layer);

    setup_zoom_vector_layer(tool);

    /* Right pane */
    vbox = gwy_vbox_new(4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), create_selection_info_table(tool), FALSE, FALSE, 0);

    /* Options */
    table = tool->table = gwy_param_table_new(tool->params);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_radio_buttons(table, PARAM_SHAPE, shapes);
    gwy_param_table_append_checkbox(table, PARAM_ADAPT_COLOR_RANGE);
    gwy_param_table_append_message(table, MESSAGE_MISSING, NULL);
    gwy_param_table_message_set_type(table, MESSAGE_MISSING, GTK_MESSAGE_WARNING);
    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_plain_tool_add_param_table(GWY_PLAIN_TOOL(tool), table);

    gtk_dialog_add_button(dialog, GTK_STOCK_CLEAR, GWY_TOOL_RESPONSE_CLEAR);
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    gtk_dialog_add_button(dialog, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gtk_dialog_set_response_sensitive(dialog, GWY_RESPONSE_CLEAR, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    resize_detail(tool);

    g_signal_connect_swapped(tool->table, "param-changed", G_CALLBACK(param_changed), tool);

    gtk_widget_show_all(dialog->vbox);
}

static GtkWidget*
create_selection_info_table(GwyToolSpotRemover *tool)
{
    GtkTable *table;
    GtkWidget *label;
    gint i, row = 0;

    table = GTK_TABLE(gtk_table_new(6, 3, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(table, 8);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_row_spacing(table, 2, 8);

    label = gwy_label_new_header(_("Origin"));
    gtk_table_attach(table, label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    label = gtk_label_new("X");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new("Y");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gwy_label_new_header(_("Size"));
    gtk_table_attach(table, label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Width"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Height"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    for (i = 0; i < NCOORDS; i++) {
        row = 1 + i + i/2;

        tool->label_real[i] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        tool->label_pix[i] = label = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 2, 3, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }

    return GTK_WIDGET(table);
}

static void
gwy_tool_spot_remover_data_switched(GwyTool *gwytool,
                                    GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(gwytool);
    gboolean ignore = (data_view == plain_tool->data_view);
    GwyPixmapLayer *layer;
    const gchar *key;
    gchar *sigdetail;

    if (!ignore)
        GWY_SIGNAL_HANDLER_DISCONNECT(plain_tool->container, tool->palette_id);

    GWY_TOOL_CLASS(gwy_tool_spot_remover_parent_class)->data_switched(gwytool, data_view);

    if (ignore || plain_tool->init_failed)
        return;

    tool->xr.from = tool->yr.from = tool->xr.to = tool->yr.to = -1;
    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type_point,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        resize_detail(tool);

        layer = gwy_data_view_get_base_layer(data_view);
        g_return_if_fail(GWY_IS_LAYER_BASIC(layer));
        key = gwy_layer_basic_get_gradient_key(GWY_LAYER_BASIC(layer));
        if (key) {
            /* FIXME: We may want to respond also to colour mapping keys in case something else than icolorange changes
             * them (icolorange can't because it is another tool). */
            sigdetail = g_strconcat("item-changed::", key, NULL);
            tool->palette_id = g_signal_connect_swapped(plain_tool->container, sigdetail,
                                                        G_CALLBACK(gwy_tool_spot_remover_palette_changed), tool);
            g_free(sigdetail);
        }
        adapt_colour_range(tool, FALSE);
        gwy_tool_spot_remover_palette_changed(tool);
        gwy_tool_spot_remover_selection_changed(plain_tool, -1);
    }
    else {
        tool->has_selection = FALSE;
        tool->has_zselection = FALSE;
        adapt_colour_range(tool, TRUE);
        update_selection_info_table(tool);
    }
}

static void
gwy_tool_spot_remover_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(plain_tool);

    tool->drawn = FALSE;
    resize_detail(tool);
    gwy_tool_spot_remover_selection_changed(plain_tool, -1);
    adapt_colour_range(tool, FALSE);
    if (!tool->drawn)
        draw_zoom(tool);
}

static void
gwy_tool_spot_remover_palette_changed(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    gwy_app_sync_data_items(plain_tool->container, tool->data, plain_tool->id, 0, TRUE,
                            GWY_DATA_ITEM_GRADIENT,
                            0);
}

static void
gwy_tool_spot_remover_response(GwyTool *gwytool,
                               gint response_id)
{
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(gwytool);

    GWY_TOOL_CLASS(gwy_tool_spot_remover_parent_class)->response(gwytool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_spot_remover_apply(tool);
    else if (response_id == GWY_TOOL_RESPONSE_CLEAR)
        gwy_selection_clear(tool->zselection);
}

static void
resize_detail(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gint xres, yres, dxres, dyres, minres, maxres, newdxres, newdyres;
    gdouble newzoom;

    if (!plain_tool->data_field)
        return;

    xres = gwy_data_field_get_xres(plain_tool->data_field);
    yres = gwy_data_field_get_yres(plain_tool->data_field);
    dxres = gwy_data_field_get_xres(tool->detail);
    dyres = gwy_data_field_get_yres(tool->detail);
    gwy_debug("image %dx%d, detail %dx%d", xres, yres, dxres, dyres);

    /* Max determines the displayed region. */
    maxres = MIN(MAX(xres, yres), MAX_SIZE);
    /* Min determines posible cut in orthogonal direction. */
    minres = MIN(MIN(xres, yres), maxres);
    gwy_debug("minres %d, maxres %d", minres, maxres);

    newdxres = (xres == minres) ? minres : maxres;
    newdyres = (yres == minres) ? minres : maxres;
    gwy_debug("detail should be %dx%d", newdxres, newdyres);

    if (newdxres == dxres && newdyres == dyres)
        return;

    gwy_data_field_resample(tool->detail, newdxres, newdyres, GWY_INTERPOLATION_NONE);
    gwy_data_field_set_xreal(tool->detail, newdxres);
    gwy_data_field_set_yreal(tool->detail, newdyres);
    gwy_data_field_clear(tool->detail);

    newzoom = (gdouble)SCALE/MAX(newdxres, newdyres)*MAX_SIZE;
    gwy_debug("updating zoom to %g", newzoom);
    gwy_data_view_set_zoom(GWY_DATA_VIEW(tool->zoomview), newzoom);
    gwy_data_field_data_changed(tool->detail);
    gwy_selection_clear(tool->zselection);
}

static void
gwy_tool_spot_remover_selection_changed(GwyPlainTool *plain_tool,
                                        gint hint)
{
    GwyToolSpotRemover *tool = GWY_TOOL_SPOT_REMOVER(plain_tool);
    Range xr, yr;
    gboolean has_selection, complete;
    gint xres, yres, dxres, dyres;
    gdouble sel[2];
    gint isel[2];

    g_return_if_fail(hint <= 0);

    has_selection = FALSE;
    if (plain_tool->selection)
        has_selection = gwy_selection_get_object(plain_tool->selection, 0, sel);

    complete = TRUE;
    if (has_selection) {
        dxres = gwy_data_field_get_xres(tool->detail);
        dyres = gwy_data_field_get_yres(tool->detail);
        isel[0] = floor(gwy_data_field_rtoj(plain_tool->data_field, sel[0]));
        isel[1] = floor(gwy_data_field_rtoi(plain_tool->data_field, sel[1]));
        xres = gwy_data_field_get_xres(plain_tool->data_field);
        yres = gwy_data_field_get_yres(plain_tool->data_field);
        complete &= find_subrange(isel[0], xres, dxres, &xr);
        complete &= find_subrange(isel[1], yres, dyres, &yr);
    }
    else
        xr.from = yr.from = xr.to = yr.to = -1;

    tool->has_selection = has_selection;
    if (tool->xr.from == xr.from && tool->yr.from == yr.from && tool->xr.to == xr.to && tool->yr.to == yr.to) {
        update_message(tool);
        return;
    }

    tool->xr = xr;
    tool->yr = yr;
    tool->complete = complete;
    zselection_changed(tool->zselection, -1, tool);
    draw_zoom(tool);
    tool->drawn = TRUE;
}

static void
setup_zoom_vector_layer(GwyToolSpotRemover *tool)
{
    SpotRemoveShape shape = gwy_params_get_enum(tool->params, PARAM_SHAPE);
    GwyVectorLayer *vlayer;

    if (tool->zsel_id) {
        g_signal_handler_disconnect(tool->zselection, tool->zsel_id);
        tool->zsel_id = 0;
    }

    if (shape == GWY_SPOT_REMOVE_RECTANGLE) {
        vlayer = GWY_VECTOR_LAYER(g_object_new(tool->layer_type_rect, NULL));
        gwy_vector_layer_set_selection_key(vlayer, "/0/select/rect");
    }
    else if (shape == GWY_SPOT_REMOVE_ELLIPSE) {
        vlayer = GWY_VECTOR_LAYER(g_object_new(tool->layer_type_ell, NULL));
        gwy_vector_layer_set_selection_key(vlayer, "/0/select/ell");
    }
    else {
        g_return_if_reached();
    }

    gwy_data_view_set_top_layer(GWY_DATA_VIEW(tool->zoomview), vlayer);
    tool->zselection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(tool->zselection, 1);
    tool->zsel_id = g_signal_connect(tool->zselection, "changed", G_CALLBACK(zselection_changed), tool);
}

static void
zselection_changed(GwySelection *selection,
                   gint hint,
                   GwyToolSpotRemover *tool)
{
    GwyDataField *data_field = GWY_PLAIN_TOOL(tool)->data_field;
    gdouble sel[4];
    gboolean is_ok = FALSE;

    g_return_if_fail(hint <= 0);

    if (!data_field) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GTK_RESPONSE_APPLY, FALSE);
        return;
    }

    if (tool->xr.from >= 0 && tool->yr.from >= 0 && gwy_selection_get_object(selection, 0, sel)) {
        GWY_ORDER(gdouble, sel[0], sel[2]);
        GWY_ORDER(gdouble, sel[1], sel[3]);
        /* `real' dimensions on the zoom are actually pixel dimensions on the data field */
        tool->zisel[0] = (gint)floor(sel[0]) + tool->xr.from - tool->xr.dest;
        tool->zisel[1] = (gint)floor(sel[1]) + tool->yr.from - tool->yr.dest;
        tool->zisel[2] = (gint)ceil(sel[2]) + tool->xr.from - tool->xr.dest;
        tool->zisel[3] = (gint)ceil(sel[3]) + tool->yr.from - tool->yr.dest;
        is_ok = (tool->zisel[0] > 0
                 && tool->zisel[1] > 0
                 && tool->zisel[2] < gwy_data_field_get_xres(data_field)
                 && tool->zisel[3] < gwy_data_field_get_yres(data_field));
        gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GWY_RESPONSE_CLEAR, TRUE);
    }
    else
        gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GWY_RESPONSE_CLEAR, FALSE);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GTK_RESPONSE_APPLY, is_ok);

    tool->has_zselection = gwy_selection_get_data(selection, NULL);
    update_message(tool);
    update_selection_info_table(tool);
}

static void
update_selection_info_table(GwyToolSpotRemover *tool)
{
    GwyDataField *field = GWY_PLAIN_TOOL(tool)->data_field;
    GwySIValueFormat *vf;
    gdouble dx, dy, v;
    gint icoord[4];
    gchar buf[48];
    gint i;

    vf = tool->pixel_format;
    if (!tool->has_zselection) {
        for (i = 0; i < NCOORDS; i++) {
            gtk_label_set_text(GTK_LABEL(tool->label_real[i]), "");
            gtk_label_set_text(GTK_LABEL(tool->label_pix[i]), vf->units);
        }
        return;
    }

    gwy_assign(icoord, tool->zisel, NCOORDS);
    icoord[2] -= icoord[0];
    icoord[3] -= icoord[1];

    for (i = 0; i < NCOORDS; i++) {
        g_snprintf(buf, sizeof(buf), "%.*f %s",
                   vf->precision, icoord[i]/vf->magnitude, vf->units);
        gtk_label_set_markup(GTK_LABEL(tool->label_pix[i]), buf);
    }

    vf = GWY_PLAIN_TOOL(tool)->coord_format;
    g_return_if_fail(field);

    dx = gwy_data_field_get_dx(field);
    dy = gwy_data_field_get_dx(field);
    for (i = 0; i < NCOORDS; i++) {
        v = icoord[i]*(i % 2 ? dy : dx);
        g_snprintf(buf, sizeof(buf), "%.*f%s%s",
                   vf->precision, v/vf->magnitude,
                   vf->units && *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(GTK_LABEL(tool->label_real[i]), buf);
    }
}

static void
draw_zoom(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gdouble min;

    if (tool->xr.from < 0 || tool->yr.from < 0) {
        gwy_data_field_clear(tool->detail);
        adapt_colour_range(tool, TRUE);
    }
    else {
        if (!tool->complete) {
            min = gwy_data_field_area_get_min(plain_tool->data_field, NULL,
                                              tool->xr.from, tool->yr.from,
                                              tool->xr.to - tool->xr.from, tool->yr.to - tool->yr.from);
            gwy_data_field_fill(tool->detail, min);
        }
        gwy_data_field_area_copy(plain_tool->data_field, tool->detail,
                                 tool->xr.from, tool->yr.from,
                                 tool->xr.to - tool->xr.from, tool->yr.to - tool->yr.from,
                                 tool->xr.dest, tool->yr.dest);
    }
    gwy_data_field_data_changed(tool->detail);
}

static void
adapt_colour_range(GwyToolSpotRemover *tool, gboolean make_empty)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyContainer *data = tool->data;
    GwyDataField *field = plain_tool->data_field;
    gboolean do_adapt = gwy_params_get_boolean(tool->params, PARAM_ADAPT_COLOR_RANGE);
    gint id = plain_tool->id;
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    gdouble min = 0.0, max = 0.0;

    if (make_empty || !field || !plain_tool->container) {
        gwy_container_set_enum(data, gwy_app_get_data_range_type_key_for_id(0), GWY_LAYER_BASIC_RANGE_FULL);
        gwy_container_set_double(data, gwy_app_get_data_range_min_key_for_id(0), 0.0);
        gwy_container_set_double(data, gwy_app_get_data_range_max_key_for_id(0), 0.0);
        return;
    }

    if (do_adapt) {
        gwy_container_set_enum(data, gwy_app_get_data_range_type_key_for_id(0), GWY_LAYER_BASIC_RANGE_FULL);
        gwy_container_remove(data, gwy_app_get_data_range_min_key_for_id(0));
        gwy_container_remove(data, gwy_app_get_data_range_max_key_for_id(0));
        return;
    }

    gwy_container_gis_enum(plain_tool->container, gwy_app_get_data_range_type_key_for_id(id), &range_type);
    if (range_type == GWY_LAYER_BASIC_RANGE_AUTO)
        gwy_data_field_get_autorange(field, &min, &max);
    else {
        gwy_data_field_get_min_max(field, &min, &max);
        if (range_type == GWY_LAYER_BASIC_RANGE_FIXED) {
            gwy_container_gis_double(plain_tool->container, gwy_app_get_data_range_min_key_for_id(id), &min);
            gwy_container_gis_double(plain_tool->container, gwy_app_get_data_range_max_key_for_id(id), &max);
        }
        /* FIXME: Adaptive mapping using the distribution of different data field is currently not possible. */
    }
    gwy_container_set_enum(data, gwy_app_get_data_range_type_key_for_id(0), GWY_LAYER_BASIC_RANGE_FIXED);
    gwy_container_set_double(data, gwy_app_get_data_range_min_key_for_id(0), min);
    gwy_container_set_double(data, gwy_app_get_data_range_max_key_for_id(0), max);
}

static void
update_message(GwyToolSpotRemover *tool)
{
    const gchar *message_data = _("No point in the image selected.");
    const gchar *message_zoom = _("No area in the zoom selected.");
    const gchar *message = NULL;
    gchar *to_free = NULL;

    if (tool->has_selection)
        message = tool->has_zselection ? NULL : message_zoom;
    else {
        if (tool->has_zselection)
            message = message_data;
        else
            message = to_free = g_strconcat(message_data, "\n", message_zoom, NULL);
    }

    gwy_param_table_set_label(tool->table, MESSAGE_MISSING, message);
    g_free(to_free);
}

static gboolean
find_subrange(gint center, gint res, gint size, Range *r)
{
    /* complete interval always fit in size */
    if (res <= size) {
        r->from = 0;
        r->to = res;
        r->dest = (size - res)/2;
        return FALSE;
    }

    /* try to keep center in center */
    r->dest = 0;
    r->from = center - size/2;
    r->to = center + size/2 + 1;
    /* but move it if not possible */
    if (r->from < 0) {
        r->to -= r->from;
        r->from = 0;
    }
    if (r->to > res) {
        r->from -= (r->to - res);
        r->to = res;
    }
    g_assert(r->from >= 0);
    return TRUE;
}

static void
param_changed(GwyToolSpotRemover *tool, gint id)
{
    if (id < 0 || id == PARAM_SHAPE) {
        GwySelection *zselection = tool->zselection;
        gdouble sel[4];
        gboolean restore_sel = (zselection && gwy_selection_get_object(zselection, 0, sel));

        setup_zoom_vector_layer(tool);
        if (restore_sel)
            gwy_selection_set_data(zselection, 1, sel);
    }
    if (id < 0 || id == PARAM_ADAPT_COLOR_RANGE)
        adapt_colour_range(tool, FALSE);
}

static void
fill_elliptic_area(GwyDataField *field,
                   gint col, gint row, gint width, gint height,
                   gdouble value)
{
    /* Ignore the return value to match prototype. */
    gwy_data_field_elliptic_area_fill(field, col, row, width, height, value);
}

static void
gwy_tool_spot_remover_apply(GwyToolSpotRemover *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    SpotRemoveMethod method = gwy_params_get_enum(tool->params, PARAM_METHOD);
    SpotRemoveShape shape = gwy_params_get_enum(tool->params, PARAM_SHAPE);
    AreaFillFunc fill_area = (shape == GWY_SPOT_REMOVE_ELLIPSE ? fill_elliptic_area : gwy_data_field_area_fill);
    GwyDataField *field = GWY_PLAIN_TOOL(tool)->data_field;
    GwyDataField *area, *mask = NULL;
    gint xmin, xmax, ymin, ymax, w, h;

    g_return_if_fail(plain_tool->id >= 0 && field);

    gwy_app_undo_qcheckpoint(plain_tool->container, gwy_app_get_data_key_for_id(plain_tool->id), 0);

    xmin = tool->zisel[0];
    ymin = tool->zisel[1];
    xmax = tool->zisel[2];
    ymax = tool->zisel[3];
    w = xmax - xmin;
    h = ymax - ymin;
    if (method == GWY_SPOT_REMOVE_FRACTAL_LAPLACE) {
        /* Fractal interpolation is full-size because it analyses the entire data field. */
        mask = gwy_data_field_new_alike(field, TRUE);
        fill_area(mask, xmin, ymin, w, h, 1.0);
        gwy_data_field_fractal_correction(field, mask, GWY_INTERPOLATION_LINEAR);
        g_object_unref(mask);

        area = gwy_data_field_area_extract(field, xmin-1, ymin-1, w+2, h+2);
        mask = gwy_data_field_new_alike(area, TRUE);
        fill_area(mask, 1, 1, w, h, 1.0);
        gwy_data_field_laplace_solve(area, mask, 1, 1.0);

        gwy_data_field_grain_distance_transform(mask);
        blend_fractal_and_laplace(field, area, mask, xmin-1, ymin-1);
        g_object_unref(area);
    }
    else if (method == GWY_SPOT_REMOVE_FRACTAL) {
        /* Fractal interpolation is full-size because it analyses the entire data field. */
        mask = gwy_data_field_new_alike(field, TRUE);
        fill_area(mask, xmin, ymin, w, h, 1.0);
        gwy_data_field_fractal_correction(field, mask, GWY_INTERPOLATION_LINEAR);
    }
    else if (method == GWY_SPOT_REMOVE_ZERO) {
        fill_area(field, xmin, ymin, w, h, 0.0);
    }
    else {
        area = gwy_data_field_area_extract(field, xmin-1, ymin-1, w+2, h+2);
        mask = gwy_data_field_new_alike(area, TRUE);
        fill_area(mask, 1, 1, w, h, 1.0);

        if (method == GWY_SPOT_REMOVE_LAPLACE)
            gwy_data_field_laplace_solve(area, mask, 1, 2.0);
        else if (method == GWY_SPOT_REMOVE_PSEUDO_LAPLACE)
            pseudo_laplace_average(area, mask);
        else if (method == GWY_SPOT_REMOVE_HYPER_FLATTEN)
            hyperbolic_average(area, mask);
        else {
            g_assert_not_reached();
        }

        gwy_data_field_area_copy(area, field, 1, 1, w, h, xmin, ymin);
        g_object_unref(area);
    }

    GWY_OBJECT_UNREF(mask);
    gwy_data_field_data_changed(field);
    gwy_params_save_to_settings(tool->params);   /* Ensure correct parameters in the log. */
    gwy_plain_tool_log_add(plain_tool);
}

/* XXX: Common with grainremover.c */
static void
blend_fractal_and_laplace(GwyDataField *field,
                          GwyDataField *area,
                          GwyDataField *distances,
                          gint col, gint row)
{
    gint xres, w, h, i, j, k, kk;
    const gdouble *a, *e;
    gdouble *d;
    gdouble t;

    xres = gwy_data_field_get_xres(field);
    w = gwy_data_field_get_xres(area);
    h = gwy_data_field_get_yres(area);
    a = gwy_data_field_get_data_const(area);
    e = gwy_data_field_get_data_const(distances);
    d = gwy_data_field_get_data(field) + row*xres + col;

    for (i = k = kk = 0; i < h; i++) {
        for (j = 0; j < w; j++, k++, kk++) {
            if (e[k] <= 0.0)
                continue;

            t = exp(0.167*(1.0 - e[k]));
            d[kk] *= (1.0 - t);
            d[kk] += t*a[k];
        }
        kk += xres - w;
    }
}

static void
find_hyperbolic_lines(GwyDataField *field,
                      GwyDataField *mask,
                      gint *itop, gdouble *ztop,
                      gint *jleft, gdouble *zleft,
                      gint *jright, gdouble *zright,
                      gint *ibot, gdouble *zbot)
{
    gint xres, yres, i, j;
    const gdouble *d, *m;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data_const(field);
    m = gwy_data_field_get_data_const(mask);

    for (j = 0; j < xres; j++) {
        itop[j] = G_MAXINT;
        ibot[j] = -1;
    }
    for (i = 0; i < yres; i++) {
        jleft[i] = G_MAXINT;
        jright[i] = -1;
    }

    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            if (m[i*xres + j] <= 0.0)
                continue;

            if (i < itop[j])
                itop[j] = i;
            if (i > ibot[j])
                ibot[j] = i;
            if (j < jleft[i])
                jleft[i] = j;
            if (j > jright[i])
                jright[i] = j;
        }
    }

    for (j = 1; j < xres-1; j++) {
        g_assert(itop[j] < yres);
        itop[j]--;
        ztop[j] = d[itop[j]*xres + j];

        g_assert(ibot[j] > 0);
        ibot[j]++;
        zbot[j] = d[ibot[j]*xres + j];
    }
    for (i = 1; i < yres-1; i++) {
        g_assert(jleft[i] < xres);
        jleft[i]--;
        zleft[i] = d[i*xres + jleft[i]];

        g_assert(jright[i] > 0);
        jright[i]++;
        zright[i] = d[i*xres + jright[i]];
    }
}

static void
hyperbolic_average(GwyDataField *field, GwyDataField *mask)
{
    const gdouble *m;
    gdouble *d, *ztop, *zbot, *zleft, *zright;
    gint *itop, *ibot, *jleft, *jright;
    gint i, j, xres, yres;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    ztop = g_new(gdouble, 2*(xres + yres));
    zleft = ztop + xres;
    zright = zleft + yres;
    zbot = zright + yres;

    itop = g_new(gint, 2*(xres + yres));
    jleft = itop + xres;
    jright = jleft + yres;
    ibot = jright + yres;

    find_hyperbolic_lines(field, mask,
                          itop, ztop, jleft, zleft, jright, zright, ibot, zbot);
    d = gwy_data_field_get_data(field);
    m = gwy_data_field_get_data_const(mask);

    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            gint pos = i*xres + j;

            if (m[pos] > 0.0) {
                gdouble px = zleft[i], qx = zright[i];
                gdouble y = (gdouble)(i - itop[j])/(ibot[j] - itop[j]);
                gdouble wx = 1.0/y + 1.0/(1.0 - y);

                gdouble py = ztop[j], qy = zbot[j];
                gdouble x = (gdouble)(j - jleft[i])/(jright[i] - jleft[i]);
                gdouble wy = 1.0/x + 1.0/(1.0 - x);

                gdouble vy = px/x + qx/(1.0 - x);
                gdouble vx = py/y + qy/(1.0 - y);

                d[pos] = (vx + vy)/(wx + wy);
            }
        }
    }

    g_free(ztop);
    g_free(itop);
}

static void
find_bounary_pixel_values(GwyDataField *field,
                          GwyDataField *mask,
                          GArray *pvals)
{
    gint xres, yres, i, j, k;
    const gdouble *d, *m;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data_const(field);
    m = gwy_data_field_get_data_const(mask);
    g_array_set_size(pvals, 0);

    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            k = i*xres + j;
            if (m[k] > 0.0)
                continue;

            if ((i && m[k-xres] > 0.0) || (j && m[k-1] > 0.0)
                || (j < xres-1 && m[k+1] > 0.0) || (i < yres-1 && m[k+xres] > 0.0)) {
                PixelValue pv = { d[k], i, j };
                g_array_append_val(pvals, pv);
            }
        }
    }
}

static void
pseudo_laplace_average(GwyDataField *field, GwyDataField *mask)
{
    gint i, j, n, xres, yres;
    GArray *boundary;
    PixelValue *pvals;
    const gdouble *m;
    gdouble *d;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    boundary = g_array_new(FALSE, FALSE, sizeof(PixelValue));
    find_bounary_pixel_values(field, mask, boundary);
    n = boundary->len;
    pvals = (PixelValue*)g_array_free(boundary, FALSE);

    d = gwy_data_field_get_data(field);
    m = gwy_data_field_get_data_const(mask);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(d,m,pvals,n,xres,yres)
#endif
    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++) {
            gint k, pos = i*xres + j;
            gdouble s = 0.0, sz = 0.0;

            if (m[pos] <= 0.0)
                continue;

            for (k = 0; k < n; k++) {
                gint dx = pvals[k].j - j, dy = pvals[k].i - i;
                gdouble ss = 1.0/(dx*dx + dy*dy);

                s += ss;
                sz += ss*pvals[k].z;
            }
            d[pos] = sz/s;
        }
    }

    g_free(pvals);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
