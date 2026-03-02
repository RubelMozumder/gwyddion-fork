/*
 *  $Id: entropy.c 24441 2021-10-29 15:30:17Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/level.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

#define ENTROPY_NORMAL 1.41893853320467274178l
#define ENTROPY_NORMAL_2D 2.144729885849400174l

typedef enum {
    ENTROPY_VALUES = 0,
    ENTROPY_SLOPES = 1,
    ENTROPY_ANGLES = 2,
} EntropyMode;

enum {
    PARAM_MASKING,
    PARAM_MODE,
    PARAM_FIT_PLANE,
    PARAM_KERNEL_SIZE,
    PARAM_ZOOM_IN,
    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    /* Cached input data properties. */
    gboolean same_units;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyGraphModel *gmodel;
    GwyResults *results;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             entropy             (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static GwyResults*      create_results      (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             set_graph_zoom      (ModuleGUI *gui);
static void             preview             (gpointer user_data);
static void             compute_slopes      (GwyDataField *field,
                                             gint kernel_size,
                                             GwyDataField *xder,
                                             GwyDataField *yder);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Visualizes entropy calculation for value and slope distribution."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, entropy)

static gboolean
module_register(void)
{
    gwy_process_func_register("entropy",
                              (GwyProcessFunc)&entropy,
                              N_("/_Statistics/_Entropy..."),
                              GWY_STOCK_ENTROPY,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate entropy of value and slope distributions"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("Value distribution"),            ENTROPY_VALUES, },
        { N_("Slope derivative distribution"), ENTROPY_SLOPES, },
        { N_("Slope angle distribution"),      ENTROPY_ANGLES, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Mode"), modes, G_N_ELEMENTS(modes), ENTROPY_VALUES);
    gwy_param_def_add_boolean(paramdef, PARAM_FIT_PLANE, "fit_plane", _("Use local plane _fitting"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_KERNEL_SIZE, "kernel_size", _("_Plane size"), 2, 16, 3);
    gwy_param_def_add_boolean(paramdef, PARAM_ZOOM_IN, "zoom_in", _("_Zoom graph around estimate"), TRUE);
    return paramdef;
}

static void
entropy(G_GNUC_UNUSED GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field;
    ModuleArgs args;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    args.field = field;
    args.same_units = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field));
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    run_gui(&args);
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GtkWidget *hbox, *graph;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args);
    gui.gmodel = gwy_graph_model_new();

    gui.dialog = gwy_dialog_new(_("Entropy"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_MODE);
    gwy_param_table_radio_set_sensitive(table, PARAM_MODE, ENTROPY_ANGLES, args->same_units);
    gwy_param_table_append_separator(table);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_checkbox(table, PARAM_ZOOM_IN);
    gwy_param_table_append_checkbox(table, PARAM_FIT_PLANE);
    gwy_param_table_append_slider(table, PARAM_KERNEL_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_KERNEL_SIZE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_set_unitstr(table, PARAM_KERNEL_SIZE, _("px"));
    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui.results, "H", "Hdef", NULL);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);
    g_object_unref(gui.gmodel);

    return outcome;
}

static GwyResults*
create_results(G_GNUC_UNUSED ModuleArgs *args)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_value_plain(results, "H", N_("Entropy"));
    gwy_results_add_value_plain(results, "Hdef", N_("Entropy deficit"));

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_ZOOM_IN)
        set_graph_zoom(gui);
    if (id < 0 || id == PARAM_MODE || id == PARAM_FIT_PLANE) {
        EntropyMode mode = gwy_params_get_enum(params, PARAM_MODE);
        gboolean mode_is_2d = (mode == ENTROPY_ANGLES || mode == ENTROPY_SLOPES);
        gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);

        gwy_param_table_set_sensitive(table, PARAM_FIT_PLANE, mode_is_2d);
        gwy_param_table_set_sensitive(table, PARAM_KERNEL_SIZE, mode_is_2d && fit_plane);
    }
    if (id != PARAM_ZOOM_IN)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
set_graph_zoom(ModuleGUI *gui)
{
    gboolean zoom_in = gwy_params_get_boolean(gui->args->params, PARAM_ZOOM_IN);
    GwyGraphModel *gmodel = gui->gmodel;
    GwyGraphCurveModel *gcmodel;
    const gdouble *xdata, *ydata;
    guint ndata, i;
    gdouble S;

    g_object_set(gmodel, "x-min-set", FALSE, "x-max-set", FALSE, "y-min-set", FALSE, "y-max-set", FALSE, NULL);
    if (!zoom_in || gwy_graph_model_get_n_curves(gmodel) < 2)
        return;

    gcmodel = gwy_graph_model_get_curve(gmodel, 1);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    S = ydata[0];

    gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    if (ndata < 5)
        return;

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    for (i = 1; i+1 < ndata; i++) {
        if (ydata[i] > S - G_LN2) {
            g_object_set(gmodel,
                         "x-min", xdata[i-1],
                         "x-min-set", TRUE,
                         "y-min", ydata[i-1],
                         "y-min-set", TRUE,
                         NULL);
            break;
        }
    }
    for (i = ndata-2; i; i--) {
        if (ydata[i] < S + G_LN2) {
            g_object_set(gmodel,
                         "x-max", xdata[i+1],
                         "x-max-set", TRUE,
                         "y-max", ydata[i+1],
                         "y-max-set", TRUE,
                         NULL);
            break;
        }
    }
}

/* This does not transform to spherical (theta,phi) but to a planar coordinate system with unit |J| so the entropy
 * should be preserved.  It is the same transformation as in facet analysis. */
static void
transform_to_sphere(GwyDataField *xder, GwyDataField *yder)
{
    gdouble *xdata = gwy_data_field_get_data(xder);
    gdouble *ydata = gwy_data_field_get_data(yder);
    guint i, n = gwy_data_field_get_xres(xder)*gwy_data_field_get_yres(xder);

    for (i = 0; i < n; i++) {
        gdouble x = xdata[i], y = ydata[i];
        gdouble r2 = x*x + y*y;

        if (r2 > 0.0) {
            gdouble s_r = G_SQRT2*sqrt((1.0 - 1.0/sqrt(1.0 + r2))/r2);
            xdata[i] *= s_r;
            ydata[i] *= s_r;
        }
    }
}

static gdouble
calculate_sigma2_2d(GwyDataField *xfield, GwyDataField *yfield)
{
    gdouble xc = gwy_data_field_get_avg(xfield);
    gdouble yc = gwy_data_field_get_avg(yfield);
    const gdouble *xdata = gwy_data_field_get_data(xfield);
    const gdouble *ydata = gwy_data_field_get_data(yfield);
    gdouble s2 = 0.0;
    guint n, i;

    n = gwy_data_field_get_xres(xfield)*gwy_data_field_get_yres(xfield);
    for (i = 0; i < n; i++)
        s2 += (xdata[i] - xc)*(xdata[i] - xc) + (ydata[i] - yc)*(ydata[i] - yc);

    return s2/n;
}

static GwyDataField*
fake_mask(GwyDataField *field, GwyDataField *mask, GwyMaskingType masking)
{
    GwyDataField *masked;
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    const gdouble *d, *m;
    gdouble *md;
    gint i, n;

    if (!mask || masking == GWY_MASK_IGNORE)
        return field;

    gwy_data_field_area_count_in_range(mask, NULL, 0, 0, xres, yres, G_MAXDOUBLE, 1.0, NULL, &n);
    if (masking == GWY_MASK_EXCLUDE)
        n = xres*yres - n;

    if (n == xres*yres)
        return field;

    masked = gwy_data_field_new(n, 1, n, 1.0, FALSE);
    md = gwy_data_field_get_data(masked);
    d = gwy_data_field_get_data_const(field);
    m = gwy_data_field_get_data_const(mask);
    n = 0;
    for (i = 0; i < xres*yres; i++) {
        gboolean mi = (m[i] >= 1.0);
        if ((mi && masking == GWY_MASK_INCLUDE) || (!mi && masking == GWY_MASK_EXCLUDE))
            md[n++] = d[i];
    }
    g_object_unref(field);

    return masked;
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *field = args->field, *mask = args->mask;
    EntropyMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    gboolean fit_plane = gwy_params_get_boolean(args->params, PARAM_FIT_PLANE);
    gint kernel_size = gwy_params_get_int(args->params, PARAM_KERNEL_SIZE);
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    GwyGraphModel *gmodel = gui->gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyDataLine *ecurve;
    gdouble S, s, Smax = 0.0;

    ecurve = gwy_data_line_new(1, 1.0, FALSE);
    if (mode == ENTROPY_VALUES) {
        S = gwy_data_field_area_get_entropy_at_scales(field, ecurve, mask, masking, 0, 0, xres, yres, 0);
        s = gwy_data_field_area_get_rms_mask(field, mask, masking, 0, 0, xres, yres);
        Smax = ENTROPY_NORMAL + log(s);
    }
    else {
        GwyDataField *xder = gwy_data_field_new_alike(field, FALSE);
        GwyDataField *yder = gwy_data_field_new_alike(field, FALSE);

        compute_slopes(args->field, fit_plane ? kernel_size : 0, xder, yder);
        xder = fake_mask(xder, mask, masking);
        yder = fake_mask(yder, mask, masking);
        if (mode == ENTROPY_ANGLES)
            transform_to_sphere(xder, yder);

        S = gwy_data_field_get_entropy_2d_at_scales(xder, yder, ecurve, 0);
        if (mode == ENTROPY_SLOPES) {
            s = calculate_sigma2_2d(xder, yder);
            Smax = ENTROPY_NORMAL_2D + log(s);
        }

        g_object_unref(xder);
        g_object_unref(yder);
    }

    gwy_results_fill_values(gui->results, "H", S, NULL);
    if (mode == ENTROPY_ANGLES)
        gwy_results_set_na(gui->results, "Hdef", NULL);
    else
        gwy_results_fill_values(gui->results, "Hdef", Smax - S, NULL);
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS);

    gwy_graph_model_remove_all_curves(gmodel);
    g_object_set(gmodel,
                 "axis-label-bottom", "log h",
                 "axis-label-left", "S",
                 "label-position", GWY_GRAPH_LABEL_NORTHWEST,
                 NULL);

    if (gwy_data_line_get_min(ecurve) > -0.5*G_MAXDOUBLE) {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "description", _("Entropy at scales"),
                     "mode", GWY_GRAPH_CURVE_LINE_POINTS,
                     "color", gwy_graph_get_preset_color(0),
                     NULL);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, ecurve, 0, 0);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (S > -0.5*G_MAXDOUBLE) {
        GwyDataLine *best = gwy_data_line_duplicate(ecurve);
        gdouble *ydata = gwy_data_line_get_data(best);
        guint i, res = gwy_data_line_get_res(best);

        for (i = 0; i < res; i++)
            ydata[i] = S;

        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "description", _("Best estimate"),
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(1),
                     NULL);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, best, 0, 0);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_object_unref(best);
    }

    g_object_unref(ecurve);

    set_graph_zoom(gui);
}

static void
compute_slopes(GwyDataField *field, gint kernel_size,
               GwyDataField *xder, GwyDataField *yder)
{
    gint xres, yres;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    if (kernel_size) {
        GwyPlaneFitQuantity quantites[] = { GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY };
        GwyDataField *fields[2] = { xder, yder };

        gwy_data_field_fit_local_planes(field, kernel_size, 2, quantites, fields);
        gwy_data_field_multiply(xder, xres/gwy_data_field_get_xreal(field));
        gwy_data_field_multiply(yder, yres/gwy_data_field_get_yreal(field));
    }
    else
        gwy_data_field_filter_slope(field, xder, yder);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    EntropyMode mode;

    if (!args->same_units && (mode = gwy_params_get_enum(params, PARAM_MODE)) == ENTROPY_ANGLES)
        gwy_params_set_enum(params, PARAM_MODE, ENTROPY_SLOPES);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
