/*
 *  $Id: cmap_align.c 24456 2021-11-03 13:32:40Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/lawn.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

typedef enum {
    OUTPUT_ALIGN      = 0,
    OUTPUT_TOPOGRAPHY = 1,
    OUTPUT_PREVIEW    = 2,
} AlignOutput;

typedef enum {
    METHOD_MINIMUM    = 0,
    METHOD_MAXIMUM    = 1,
    METHOD_FIRST_ZERO = 2,
    METHOD_LAST_ZERO  = 3,
} AlignMethod;

enum {
    PARAM_METHOD,
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_SEGMENT,
    PARAM_ENABLE_SEGMENT,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_OUTPUT,
};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyLawn *result;
    GwyDataField *field;
    GwyDataField *mask;
    /* Cached input data properties. */
    gint nsegments;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwySelection *selection;
    GwySelection *graph_selection;
    GwyGraphModel *gmodel;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             align                   (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             set_selection           (ModuleGUI *gui);
static void             point_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static gboolean         locate_in_one_curve     (GwyLawn *lawn,
                                                 gint col,
                                                 gint row,
                                                 gint abscissa,
                                                 gint ordinate,
                                                 gint segment,
                                                 AlignMethod method,
                                                 gdouble *result);
static void             extract_one_curve       (GwyLawn *lawn,
                                                 GwyGraphCurveModel *gcmodel,
                                                 gint col,
                                                 gint row,
                                                 gint segment,
                                                 GwyParams *params);
static void             update_graph_model_props(GwyGraphModel *gmodel,
                                                 ModuleArgs *args);
static void             sanitise_params         (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Aligns curves in a curve map by shifting the values."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_align)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_align",
                                (GwyCurveMapFunc)&align,
                                N_("/_Align..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Align curves and extract topography"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Minimum"),    METHOD_MINIMUM,    },
        { N_("Maximum"),    METHOD_MAXIMUM,    },
        { N_("First zero"), METHOD_FIRST_ZERO, },
        { N_("Last zero"),  METHOD_LAST_ZERO,  },
    };
    static const GwyEnum outputs[] = {
        { N_("Align curves"),       (1 << OUTPUT_ALIGN),      },
        { N_("Extract topography"), (1 << OUTPUT_TOPOGRAPHY), },
        { N_("Set preview"),        (1 << OUTPUT_PREVIEW),    },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Method"),
                              methods, G_N_ELEMENTS(methods), METHOD_MAXIMUM);
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Abscissa"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Ordinate"));
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT, "segment", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_SEGMENT, "enable_segment", NULL, FALSE);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output _type"),
                               outputs, G_N_ELEMENTS(outputs), (1 << OUTPUT_TOPOGRAPHY) | (1 << OUTPUT_ALIGN));
    return paramdef;
}

static void
align(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    AlignOutput output;
    GwyDataField *field;
    gint id, newid;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    args.field = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                     gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), TRUE);
    gwy_data_field_set_xoffset(args.field, gwy_lawn_get_xoffset(lawn));
    gwy_data_field_set_yoffset(args.field, gwy_lawn_get_yoffset(lawn));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.field), gwy_lawn_get_si_unit_xy(lawn));
    args.mask = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    output = gwy_params_get_flags(args.params, PARAM_OUTPUT);
    if (output & (1 << OUTPUT_PREVIEW)) {
        field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
        gwy_data_field_assign(field, args.field);
        gwy_data_field_data_changed(field);
    }
    if (output & (1 << OUTPUT_TOPOGRAPHY)) {
        newid = gwy_app_data_browser_add_data_field(args.field, data, TRUE);
        gwy_app_set_data_field_title(data, newid, _("Topography"));
        if (gwy_data_field_get_max(args.mask) > 0.0)
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), args.mask);

        if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient)) {
            gwy_container_set_const_string(data,
                                           gwy_app_get_data_palette_key_for_id(newid), gradient);
        }

        gwy_app_channel_log_add(data, -1, newid, "cmap::cmap_align", NULL);
    }
    if (output & (1 << OUTPUT_ALIGN)) {
        gwy_lawn_data_changed(lawn);
        gwy_app_curve_map_log_add_curve_map(data, id, id);
    }

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.mask);
    g_object_unref(args.field);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align, *area;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyGraphCurveModel *gcmodel;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Align Map Curves"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    vlayer = g_object_new(g_type_from_name("GwyLayerPoint"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(dataview), vlayer);
    gui.selection = gwy_vector_layer_ensure_selection(vlayer);
    set_selection(&gui);

    gui.gmodel = gwy_graph_model_new();
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    gwy_graph_area_set_selection_editable(GWY_GRAPH_AREA(area), FALSE);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT, args->lawn);
        gwy_param_table_add_enabler(table, PARAM_ENABLE_SEGMENT, PARAM_SEGMENT);
    }
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_OUTPUT) {
        AlignOutput output = gwy_params_get_flags(params, PARAM_OUTPUT);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !!output);
    }
    if (id != PARAM_OUTPUT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
set_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = (col + 0.5)*gwy_lawn_get_dx(args->lawn);
    xy[1] = (row + 0.5)*gwy_lawn_get_dy(args->lawn);
    gwy_selection_set_object(gui->selection, 0, xy);
}

static void
point_selection_changed(ModuleGUI *gui, gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    gwy_selection_get_object(selection, id, xy);
    i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
    i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));

    gwy_param_table_param_changed(gui->table, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table, PARAM_YPOS);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    AlignMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    gdouble x;

    extract_one_curve(args->lawn, gwy_graph_model_get_curve(gui->gmodel, 0), col, row, segment, params);
    update_graph_model_props(gui->gmodel, args);

    if (locate_in_one_curve(args->lawn, col, row, abscissa, ordinate, segment, method, &x))
        gwy_selection_set_data(gui->graph_selection, 1, &x);
    else
        gwy_selection_clear(gui->graph_selection);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    AlignMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    AlignOutput output = gwy_params_get_flags(params, PARAM_OUTPUT);
    GwyLawn *lawn = args->lawn;
    GwyDataField *field = args->field, *mask = args->mask;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble *data, *mdata;

    /* First locate the point in all data.  This allows interpolating the failed ones. */
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), gwy_lawn_get_si_unit_curve(lawn, abscissa));
    gwy_data_field_clear(mask);
    data = gwy_data_field_get_data(field);
    mdata = gwy_data_field_get_data(mask);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(lawn,xres,yres,abscissa,ordinate,segment,data,mdata,method)
#endif
    {
        guint kfrom = gwy_omp_chunk_start(xres*yres);
        guint kto = gwy_omp_chunk_end(xres*yres);
        guint k, col, row;
        gdouble x;

        for (k = kfrom; k < kto; k++) {
            col = k % xres;
            row = k/xres;
            if (locate_in_one_curve(lawn, col, row, abscissa, ordinate, segment, method, &x))
                data[k] = x;
            else
                mdata[k] = 1.0;
        }
    }

    if (gwy_data_field_get_max(mask) > 0.0)
        gwy_data_field_laplace_solve(field, mask, -1, 1.0);

    if (output & (1 << OUTPUT_ALIGN)) {
        gint ndata, k, m, col, row;
        gdouble *zcd;
        gdouble x;

        data = gwy_data_field_get_data(field);
        for (k = 0; k < xres*yres; k++) {
            col = k % xres;
            row = k/xres;
            x = data[k];
            zcd = gwy_lawn_get_curve_data(lawn, col, row, abscissa, &ndata);
            for (m = 0; m < ndata; m++)
                zcd[m] -= x;
        }
    }
}

static gboolean
locate_minimum(const gdouble *xdata, const gdouble *ydata, gint ndata, gdouble *result)
{
    gdouble m;
    gint i, im;

    if (!ndata) {
        *result = 0.0;
        return FALSE;
    }

    im = 0;
    m = ydata[im];
    for (i = 1; i < ndata; i++) {
        if (ydata[i] < m) {
            im = i;
            m = ydata[im];
        }
    }
    *result = xdata[im];
    return TRUE;
}

static gboolean
locate_maximum(const gdouble *xdata, const gdouble *ydata, gint ndata, gdouble *result)
{
    gdouble m;
    gint i, im;

    if (!ndata) {
        *result = 0.0;
        return FALSE;
    }

    im = 0;
    m = ydata[im];
    for (i = 1; i < ndata; i++) {
        if (ydata[i] > m) {
            im = i;
            m = ydata[im];
        }
    }
    *result = xdata[im];
    return TRUE;
}

static gboolean
locate_first_zero(const gdouble *xdata, const gdouble *ydata, gint ndata, gdouble *result)
{
    gint i;

    *result = 0.0;
    if (!ndata)
        return FALSE;

    if (ydata[0] > 0.0) {
        for (i = 1; i < ndata; i++) {
            if (ydata[i] <= 0.0)
                break;
        }
    }
    else if (ydata[0] < 0.0) {
        for (i = 1; i < ndata; i++) {
            if (ydata[i] >= 0.0)
                break;
        }
    }
    else
        return TRUE;

    if (i == ndata)
        return FALSE;
    if (xdata[i-1] == xdata[i]) {
        *result = xdata[i];
        return TRUE;
    }

    *result = (xdata[i]*ydata[i-1] - xdata[i-1]*ydata[i])/(ydata[i-1] - ydata[i]);
    return TRUE;
}

static gboolean
locate_last_zero(const gdouble *xdata, const gdouble *ydata, gint ndata, gdouble *result)
{
    gint i;

    *result = 0.0;
    if (!ndata)
        return FALSE;

    if (ydata[ndata-1] > 0.0) {
        for (i = ndata-1; i; i--) {
            if (ydata[i-1] <= 0.0)
                break;
        }
    }
    else if (ydata[ndata-1] < 0.0) {
        for (i = ndata-1; i; i--) {
            if (ydata[i--] >= 0.0)
                break;
        }
    }
    else
        return TRUE;

    if (!i)
        return FALSE;
    if (xdata[i-1] == xdata[i]) {
        *result = xdata[i];
        return TRUE;
    }

    *result = (xdata[i]*ydata[i-1] - xdata[i-1]*ydata[i])/(ydata[i-1] - ydata[i]);
    return TRUE;
}

static gboolean
locate_in_one_curve(GwyLawn *lawn, gint col, gint row,
                    gint abscissa, gint ordinate, gint segment,
                    AlignMethod method, gdouble *result)
{
    const gdouble *xdata, *ydata;
    gint ndata, from, end;
    const gint *segments;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);

    if (segment >= 0) {
        segments = gwy_lawn_get_segments(lawn, col, row, NULL);
        from = segments[2*segment];
        end = segments[2*segment + 1];
        xdata += from;
        ydata += from;
        ndata = end - from;
    }

    if (method == METHOD_MAXIMUM)
        return locate_maximum(xdata, ydata, ndata, result);
    if (method == METHOD_MINIMUM)
        return locate_minimum(xdata, ydata, ndata, result);
    if (method == METHOD_FIRST_ZERO)
        return locate_first_zero(xdata, ydata, ndata, result);
    if (method == METHOD_LAST_ZERO)
        return locate_last_zero(xdata, ydata, ndata, result);

    g_return_val_if_reached(FALSE);
}

static void
extract_one_curve(GwyLawn *lawn, GwyGraphCurveModel *gcmodel,
                  gint col, gint row, gint segment,
                  GwyParams *params)
{
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    const gdouble *xdata, *ydata;
    gint ndata, from, end;
    const gint *segments;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);

    if (segment >= 0) {
        segments = gwy_lawn_get_segments(lawn, col, row, NULL);
        from = segments[2*segment];
        end = segments[2*segment + 1];
        xdata += from;
        ydata += from;
        ndata = end - from;
    }
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
}

static void
update_graph_model_props(GwyGraphModel *gmodel, ModuleArgs *args)
{
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwySIUnit *xunit, *yunit;
    const gchar *xlabel, *ylabel;

    xunit = gwy_lawn_get_si_unit_curve(lawn, abscissa);
    xlabel = gwy_lawn_get_curve_label(lawn, abscissa);
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    ylabel = gwy_lawn_get_curve_label(lawn, ordinate);

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel ? xlabel : _("Untitled"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 "label-visible", FALSE,
                 NULL);
}

static void
sanitise_one_param(GwyParams *params, gint id, gint min, gint max, gint defval)
{
    gint v;

    v = gwy_params_get_int(params, id);
    if (v >= min && v <= max) {
        gwy_debug("param #%d is %d, i.e. within range [%d..%d]", id, v, min, max);
        return;
    }
    gwy_debug("param #%d is %d, setting it to the default %d", id, v, defval);
    gwy_params_set_int(params, id, defval);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLawn *lawn = args->lawn;

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_lawn_get_xres(lawn)-1, gwy_lawn_get_xres(lawn)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_lawn_get_yres(lawn)-1, gwy_lawn_get_yres(lawn)/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
