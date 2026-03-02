/*
 *  $Id: slope_dist.c 24550 2021-11-26 17:01:11Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/level.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    SLOPE_DIST_2D_DIST,
    SLOPE_DIST_GRAPH_PHI,
    SLOPE_DIST_GRAPH_THETA,
    SLOPE_DIST_GRAPH_GRADIENT,
    SLOPE_DIST_LAST
} SlopeOutput;

enum {
    PARAM_OUTPUT_TYPE,
    PARAM_SIZE,
    PARAM_LOGSCALE,
    PARAM_FIT_PLANE,
    PARAM_KERNEL_SIZE,
    PARAM_MASKING,
    PARAM_UPDATE,
    PARAM_TARGET_GRAPH,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyGraphModel *gmodel;
    /* Cached input data properties. */
    gboolean same_units;
    GwyDataField *xder;
    GwyDataField *yder;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *dataview;
    GtkWidget *graph;
} ModuleGUI;

static gboolean         module_register           (void);
static GwyParamDef*     define_module_params      (void);
static void             slope_dist                (GwyContainer *data,
                                                   GwyRunType runtype);
static void             execute                   (ModuleArgs *args);
static void             sanitise_params           (ModuleArgs *args);
static GwyDialogOutcome run_gui                   (ModuleArgs *args,
                                                   GwyContainer *data,
                                                   gint id);
static void             param_changed             (ModuleGUI *gui,
                                                   gint id);
static void             preview                   (gpointer user_data);
static GwyDataField*    slope_do_2d               (ModuleArgs *args);
static void             slope_do_graph_phi        (ModuleArgs *args);
static void             slope_do_graph_theta      (ModuleArgs *args);
static void             slope_do_graph_gradient   (ModuleArgs *args);
static void             set_graph_model_properties(GwyGraphModel *gmodel,
                                                   GwyDataField *field,
                                                   SlopeOutput output_type);
static void             compute_slopes            (GwyDataField *field,
                                                   gint kernel_size,
                                                   GwyDataField *xder,
                                                   GwyDataField *yder);
static GwyDataField*    make_datafield            (GwyDataField *old,
                                                   gint res,
                                                   gulong *count,
                                                   gdouble real,
                                                   gboolean logscale);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates one- or two-dimensional distribution of slopes or graph of their angular distribution."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, slope_dist)

static gboolean
module_register(void)
{
    gwy_process_func_register("slope_dist",
                              (GwyProcessFunc)&slope_dist,
                              N_("/_Statistics/_Slope Distribution..."),
                              GWY_STOCK_DISTRIBUTION_SLOPE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate angular slope distribution"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum output_types[] = {
        { N_("_Two-dimensional distribution"), SLOPE_DIST_2D_DIST,        },
        { N_("Directional (φ) _graph"),        SLOPE_DIST_GRAPH_PHI,      },
        { N_("_Inclination (θ) graph"),        SLOPE_DIST_GRAPH_THETA,    },
        { N_("Inclination (gra_dient) graph"), SLOPE_DIST_GRAPH_GRADIENT, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output type"),
                              output_types, G_N_ELEMENTS(output_types), SLOPE_DIST_2D_DIST);
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("Output size"), 1, 1024, 200);
    gwy_param_def_add_boolean(paramdef, PARAM_LOGSCALE, "logscale", _("_Logarithmic value scale"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_FIT_PLANE, "fit_plane", _("Use local plane _fitting"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_KERNEL_SIZE, "kernel_size", _("Plane size"), 2, 16, 5);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
slope_dist(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    gint oldid, newid;
    GwyAppDataId target_graph_id;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(args.field);
    args.same_units = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(args.field),
                                        gwy_data_field_get_si_unit_z(args.field));
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    args.result = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, 1.0, 1.0, TRUE);
    args.gmodel = gwy_graph_model_new();

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    if (gwy_params_get_enum(args.params, PARAM_OUTPUT_TYPE) == SLOPE_DIST_2D_DIST) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                GWY_DATA_ITEM_PALETTE,
                                0);
        gwy_app_set_data_field_title(data, newid, _("Slope distribution"));
        gwy_app_channel_log_add_proc(data, oldid, newid);
    }
    else {
        target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
        gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);
    }

end:
    GWY_OBJECT_UNREF(args.xder);
    GWY_OBJECT_UNREF(args.yder);
    g_object_unref(args.result);
    g_object_unref(args.gmodel);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *vbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    SlopeOutput output_type;
    GwyDialogOutcome outcome;

    output_type = gwy_params_get_enum(args->params, PARAM_OUTPUT_TYPE);

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Slope Distribution"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            0);
    vbox = gwy_vbox_new(0);
    gtk_widget_set_size_request(vbox, PREVIEW_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), gui.dataview, FALSE, FALSE, 0);
    if (output_type != SLOPE_DIST_2D_DIST)
        gtk_widget_set_no_show_all(gui.dataview, TRUE);

    set_graph_model_properties(args->gmodel, args->field, output_type);
    gui.graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(gui.graph, PREVIEW_SIZE, -1);
    gwy_graph_enable_user_input(GWY_GRAPH(gui.graph), FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), gui.graph, TRUE, TRUE, 0);
    if (output_type == SLOPE_DIST_2D_DIST)
        gtk_widget_set_no_show_all(gui.graph, TRUE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_radio_set_sensitive(table, PARAM_OUTPUT_TYPE, SLOPE_DIST_GRAPH_THETA, args->same_units);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_append_checkbox(table, PARAM_LOGSCALE);
    gwy_param_table_append_checkbox(table, PARAM_FIT_PLANE);
    gwy_param_table_append_slider(table, PARAM_KERNEL_SIZE);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_end(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_OUTPUT_TYPE) {
        SlopeOutput output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);
        gboolean is_2d = (output_type == SLOPE_DIST_2D_DIST);

        gtk_widget_set_no_show_all(gui->dataview, !is_2d);
        gtk_widget_set_no_show_all(gui->graph, is_2d);
        if (is_2d) {
            gtk_widget_hide(gui->graph);
            gtk_widget_show(gui->dataview);
        }
        else {
            gtk_widget_hide(gui->dataview);
            gtk_widget_show(gui->graph);
        }
        gwy_param_table_set_sensitive(table, PARAM_LOGSCALE, is_2d);
        gwy_param_table_set_sensitive(table, PARAM_TARGET_GRAPH, !is_2d);
        /* We normally do this in preview, but it breaks target graph filtering when preview is not instant! */
        if (!is_2d) {
            gwy_graph_model_remove_all_curves(gui->args->gmodel);
            set_graph_model_properties(gui->args->gmodel, gui->args->field, output_type);
            gwy_param_table_data_id_refilter(table, PARAM_TARGET_GRAPH);
        }
    }
    if (id < 0 || id == PARAM_FIT_PLANE)
        gwy_param_table_set_sensitive(gui->table, PARAM_KERNEL_SIZE, gwy_params_get_boolean(params, PARAM_FIT_PLANE));

    if (id != PARAM_TARGET_GRAPH && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    SlopeOutput output_type = gwy_params_get_enum(args->params, PARAM_OUTPUT_TYPE);

    execute(args);
    if (output_type == SLOPE_DIST_2D_DIST) {
        gwy_data_field_data_changed(args->result);
        gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
    }
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static inline gboolean
is_counted(GwyDataField *mask, guint k, GwyMaskingType masking)
{
    if (!mask || masking == GWY_MASK_IGNORE)
        return TRUE;

    if (masking == GWY_MASK_INCLUDE)
        return mask->data[k] > 0.0;
    else
        return mask->data[k] <= 0.0;
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    SlopeOutput output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);
    GwyGraphCurveModel *gcmodel;
    GwyDataField *result;

    if (!args->xder)
        args->xder = gwy_data_field_new_alike(args->field, FALSE);
    if (!args->yder)
        args->yder = gwy_data_field_new_alike(args->field, FALSE);

    compute_slopes(args->field, fit_plane ? kernel_size : 0, args->xder, args->yder);

    if (output_type == SLOPE_DIST_2D_DIST) {
        result = slope_do_2d(args);
        gwy_data_field_assign(args->result, result);
        g_object_unref(result);
    }
    else {
        gwy_graph_model_remove_all_curves(args->gmodel);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_model_add_curve(args->gmodel, gcmodel);
        g_object_unref(gcmodel);

        if (output_type == SLOPE_DIST_GRAPH_PHI)
            slope_do_graph_phi(args);
        else if (output_type == SLOPE_DIST_GRAPH_THETA)
            slope_do_graph_theta(args);
        else if (output_type == SLOPE_DIST_GRAPH_GRADIENT)
            slope_do_graph_gradient(args);

        set_graph_model_properties(args->gmodel, args->field, output_type);
    }
}

static GwyDataField*
slope_do_2d(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gboolean logscale = gwy_params_get_boolean(params, PARAM_LOGSCALE);
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    const gdouble *xd, *yd;
    gdouble minxd, maxxd, minyd, maxyd, max;
    gint xres, yres, n;
    gint xider, yider, i;
    gulong *count;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    n = fit_plane ? kernel_size : 2;
    n = (xres - n)*(yres - n);

    gwy_data_field_get_min_max(args->xder, &minxd, &maxxd);
    maxxd = MAX(fabs(minxd), fabs(maxxd));
    gwy_data_field_get_min_max(args->yder, &minyd, &maxyd);
    maxyd = MAX(fabs(minyd), fabs(maxyd));
    max = MAX(maxxd, maxyd);
    if (!max)
        max = 1.0;

    count = g_new0(gulong, size*size);
    xd = gwy_data_field_get_data_const(args->xder);
    yd = gwy_data_field_get_data_const(args->yder);
    for (i = 0; i < n; i++) {
        if (is_counted(mask, i, masking)) {
            xider = size*(xd[i]/(2.0*max) + 0.5);
            xider = CLAMP(xider, 0, size-1);
            yider = size*(yd[i]/(2.0*max) + 0.5);
            yider = CLAMP(yider, 0, size-1);

            count[yider*size + xider]++;
        }
    }

    return make_datafield(field, size, count, 2.0*max, logscale);
}

static void
slope_do_graph_phi(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    GwyDataLine *dataline;
    const gdouble *xd, *yd;
    gdouble *data;
    gint xres, yres, n, i;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    n = fit_plane ? kernel_size : 2;
    n = (xres - n)*(yres - n);

    dataline = gwy_data_line_new(size, 360, TRUE);
    data = gwy_data_line_get_data(dataline);
    xd = gwy_data_field_get_data_const(args->xder);
    yd = gwy_data_field_get_data_const(args->yder);
    for (i = 0; i < n; i++) {
        if (is_counted(mask, i, masking)) {
            gdouble phi = gwy_canonicalize_angle(atan2(yd[i], -xd[i]), TRUE, TRUE);
            gdouble d = (xd[i]*xd[i] + yd[i]*yd[i]);
            gint iphi = floor(size*phi/(2.0*G_PI));

            iphi = CLAMP(iphi, 0, size-1);
            data[iphi] += d;
        }
    }

    gwy_graph_curve_model_set_data_from_dataline(gwy_graph_model_get_curve(args->gmodel, 0), dataline, 0, 0);
    g_object_unref(dataline);
}

static void
slope_do_graph_theta(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    GwyDataLine *dataline;
    const gdouble *yd;
    gdouble *xd, *data;
    gint xres, yres, n, i, nc;
    gdouble max;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    n = fit_plane ? kernel_size : 2;
    n = (xres - n)*(yres - n);

    dataline = gwy_data_line_new(size, 90, TRUE);
    data = gwy_data_line_get_data(dataline);
    xd = gwy_data_field_get_data(args->xder);
    yd = gwy_data_field_get_data_const(args->yder);
    for (i = 0; i < n; i++)
        xd[i] = 180.0/G_PI*atan(hypot(xd[i], yd[i]));
    gwy_data_field_area_get_min_max_mask(args->xder, mask, masking, 0, 0, xres, yres, NULL, &max);
    gwy_data_line_set_real(dataline, max);
    nc = 0;
    for (i = 0; i < n; i++) {
        if (is_counted(mask, i, masking)) {
            gint itheta = floor(size*xd[i]/max);

            itheta = CLAMP(itheta, 0, size-1);
            data[itheta]++;
            nc++;
        }
    }

    if (nc && max)
        gwy_data_line_multiply(dataline, size/(nc*max));

    gwy_graph_curve_model_set_data_from_dataline(gwy_graph_model_get_curve(args->gmodel, 0), dataline, 0, 0);
    g_object_unref(dataline);
}

static void
slope_do_graph_gradient(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    GwyDataLine *dataline;
    const gdouble *yd;
    gdouble *xd, *data;
    gint xres, yres, n, i, nc;
    gdouble max;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    n = fit_plane ? kernel_size : 2;
    n = (xres - n)*(yres - n);

    xd = gwy_data_field_get_data(args->xder);
    yd = gwy_data_field_get_data_const(args->yder);
    for (i = 0; i < n; i++)
        xd[i] = hypot(xd[i], yd[i]);
    gwy_data_field_area_get_min_max_mask(args->xder, mask, masking, 0, 0, xres, yres, NULL, &max);

    dataline = gwy_data_line_new(size, max, TRUE);
    data = gwy_data_line_get_data(dataline);
    nc = 0;
    for (i = 0; i < n; i++) {
        if (is_counted(mask, i, masking)) {
            gint ider = floor(size*xd[i]/max);

            ider = CLAMP(ider, 0, size-1);
            data[ider]++;
            nc++;
        }
    }

    if (nc && max)
        gwy_data_line_multiply(dataline, size/(nc*max));

    gwy_graph_curve_model_set_data_from_dataline(gwy_graph_model_get_curve(args->gmodel, 0), dataline, 0, 0);
    g_object_unref(dataline);
}

static void
set_graph_model_properties(GwyGraphModel *gmodel,
                           GwyDataField *field,
                           SlopeOutput output_type)
{
    GwySIUnit *siunitx, *siunity;
    GwyGraphCurveModel *gcmodel;
    const gchar *xlabel, *ylabel, *title, *desc;

    if (output_type == SLOPE_DIST_GRAPH_PHI) {
        siunitx = gwy_si_unit_new("deg");
        siunity = gwy_si_unit_divide(gwy_data_field_get_si_unit_z(field), gwy_data_field_get_si_unit_xy(field), NULL);
        gwy_si_unit_power(siunity, 2, siunity);
        xlabel = "φ";
        ylabel = "w";
        title = _("Angular Slope Distribution");
        desc = _("Slopes");
    }
    else if (output_type == SLOPE_DIST_GRAPH_THETA) {
        siunitx = gwy_si_unit_new("deg");
        siunity = gwy_si_unit_power(siunitx, -1, NULL);
        xlabel = "θ";
        ylabel = "ρ";
        title = _("Inclination Distribution");
        desc = _("Inclinations");
    }
    else if (output_type == SLOPE_DIST_GRAPH_GRADIENT) {
        siunitx = gwy_si_unit_divide(gwy_data_field_get_si_unit_z(field), gwy_data_field_get_si_unit_xy(field), NULL);
        siunity = gwy_si_unit_power(siunitx, -1, NULL);
        xlabel = "η";
        ylabel = "ρ";
        title = _("Inclination Distribution");
        desc = _("Inclinations");
    }
    else {
        g_assert(output_type == SLOPE_DIST_2D_DIST);
        return;
    }

    g_object_set(gmodel,
                 "si-unit-x", siunitx,
                 "si-unit-y", siunity,
                 "axis-label-bottom", xlabel,
                 "axis-label-left", ylabel,
                 "title", title,
                 NULL);
    g_object_unref(siunity);
    g_object_unref(siunitx);

    if (gwy_graph_model_get_n_curves(gmodel)) {
        gcmodel = gwy_graph_model_get_curve(gmodel, 0);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", desc,
                     NULL);
    }
}

static void
compute_slopes(GwyDataField *field,
               gint kernel_size,
               GwyDataField *xder,
               GwyDataField *yder)
{
    GwyPlaneFitQuantity quantites[2] = { GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY };
    GwyDataField *fields[2] = { xder, yder };

    if (!kernel_size) {
        gwy_data_field_filter_slope(field, xder, yder);
        return;
    }

    gwy_data_field_fit_local_planes(field, kernel_size, 2, quantites, fields);
    gwy_data_field_multiply(xder, 1.0/gwy_data_field_get_dx(field));
    gwy_data_field_multiply(yder, 1.0/gwy_data_field_get_dy(field));
}

static GwyDataField*
make_datafield(GwyDataField *old,
               gint res, gulong *count,
               gdouble real, gboolean logscale)
{
    GwyDataField *field;
    GwySIUnit *unit;
    gdouble *d;
    gint i;

    field = gwy_data_field_new(res, res, real, real, FALSE);
    gwy_data_field_set_xoffset(field, -real/2);
    gwy_data_field_set_yoffset(field, -real/2);

    unit = gwy_si_unit_new(NULL);
    gwy_data_field_set_si_unit_z(field, unit);
    g_object_unref(unit);

    unit = gwy_si_unit_divide(gwy_data_field_get_si_unit_z(old), gwy_data_field_get_si_unit_xy(old), NULL);
    gwy_data_field_set_si_unit_xy(field, unit);
    g_object_unref(unit);

    d = gwy_data_field_get_data(field);
    if (logscale) {
        for (i = 0; i < res*res; i++)
            d[i] = count[i] ? log((gdouble)count[i]) + 1.0 : 0.0;
    }
    else {
        for (i = 0; i < res*res; i++)
            d[i] = count[i];
    }
    g_free(count);

    return field;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    SlopeOutput output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);

    if (!args->same_units && output_type == SLOPE_DIST_GRAPH_THETA)
        gwy_params_set_enum(params, PARAM_OUTPUT_TYPE, SLOPE_DIST_GRAPH_GRADIENT);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
