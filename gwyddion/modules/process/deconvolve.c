/*
 *  $Id: deconvolve.c 24658 2022-03-10 13:16:56Z yeti-dn $
 *  Copyright (C) 2018-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libprocess/simplefft.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

#define field_convolve_default(field, kernel) \
    gwy_data_field_area_ext_convolve((field), \
                                     0, 0, \
                                     gwy_data_field_get_xres(field), \
                                     gwy_data_field_get_yres(field), \
                                     (field), (kernel), \
                                     GWY_EXTERIOR_BORDER_EXTEND, 0.0, TRUE)


enum {
    NSTEPS = 31
};

typedef enum {
    DECONV_DISPLAY_DATA        = 0,
    DECONV_DISPLAY_DECONVOLVED = 1,
    DECONV_DISPLAY_DIFFERENCE  = 2,
} DeconvDisplayType;

typedef enum {
    DECONV_LCURVE_DIFFERENCE  = 0,
    DECONV_LCURVE_RMS         = 1,
    DECONV_LCURVE_CURVATURE   = 2,
    DECONV_LCURVE_LCURVE      = 3,
    DECONV_LCURVE_NCURVES,
} LCurveType;

typedef enum {
    LCURVE_DATA_LOG10SIGMA = 0,
    LCURVE_DATA_DIFFERENCE,
    LCURVE_DATA_LOGDIFFERENCE,
    LCURVE_DATA_RMS,
    LCURVE_DATA_LOGRMS,
    LCURVE_DATA_CURVATURE,
    LCURVE_DATA_NTYPES,
} LCurveDataType;

typedef enum {
    DECONV_OUTPUT_DECONVOLVED = 0,
    DECONV_OUTPUT_DIFFERECE   = 1,
} DeconvOutputType;

enum {
    PARAM_KERNEL,
    PARAM_AS_INTEGRAL,
    PARAM_OUTPUT_TYPE,
    PARAM_DISPLAY,
    PARAM_LCURVE,
    PARAM_SIGMA,
    PARAM_SIGMA_RANGE,

    LABEL_SIGMA,
    LABEL_BEST_SIGMA,
    BUTTON_UPDATE_LCURVE,
    BUTTON_USE_ESTIMATE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *deconvolved;
    GwyDataField *difference;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_param;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwyGraphArea *area;
    GwySelection *selection;
    gdouble best_sigma;
    gint nsteps;
    gdouble *lcurvedata;
} ModuleGUI;

static gboolean         module_register              (void);
static GwyParamDef*     define_module_params         (void);
static void             deconvolve                   (GwyContainer *data,
                                                      GwyRunType runtype);
static void             execute                      (ModuleArgs *args);
static GwyDialogOutcome run_gui                      (ModuleArgs *args,
                                                      GwyContainer *data,
                                                      gint id);
static void             param_changed                (ModuleGUI *gui,
                                                      gint id);
static void             preview                      (gpointer user_data);
static void             dialog_response              (ModuleGUI *gui,
                                                      gint response);
static void             graph_selected               (ModuleGUI *gui);
static void             switch_display               (ModuleGUI *gui);
static void             switch_lcurve                (ModuleGUI *gui);
static void             clear_lcurve                 (ModuleGUI *gui);
static void             calculate_lcurve             (ModuleGUI *gui);
static gboolean         kernel_filter                (GwyContainer *data,
                                                      gint id,
                                                      gpointer user_data);
static void             deconvolve_with_kernel       (GwyDataField *measured,
                                                      GwyDataField *tf,
                                                      GwyDataField *deconv,
                                                      GwyDataField *difference,
                                                      gdouble sigma);
static void             adjust_deconv_to_non_integral(GwyDataField *psf);
static gint             create_output_field          (GwyDataField *field,
                                                      GwyContainer *data,
                                                      gint id,
                                                      const gchar *name);

static const GwyEnum lcurves[] = {
    { N_("Difference"), DECONV_LCURVE_DIFFERENCE,  },
    { N_("RMS"),        DECONV_LCURVE_RMS,         },
    { N_("Curvature"),  DECONV_LCURVE_CURVATURE,   },
    { N_("L-curve"),    DECONV_LCURVE_LCURVE,      },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Regularized image deconvolution."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, deconvolve)

static gboolean
module_register(void)
{
    gwy_process_func_register("deconvolve",
                              (GwyProcessFunc)&deconvolve,
                              N_("/M_ultidata/_Deconvolve..."),
                              GWY_STOCK_DECONVOLVE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Deconvolve image"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Deconvolved"), (1 << DECONV_OUTPUT_DECONVOLVED), },
        { N_("Difference"),  (1 << DECONV_OUTPUT_DIFFERECE),   },
    };
    static const GwyEnum displays[] = {
        { N_("Data"),        DECONV_DISPLAY_DATA,        },
        { N_("Deconvolved"), DECONV_DISPLAY_DECONVOLVED, },
        { N_("Difference"),  DECONV_DISPLAY_DIFFERENCE,  },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_KERNEL, "kernel", _("Convolution _kernel"));
    gwy_param_def_add_boolean(paramdef, PARAM_AS_INTEGRAL, "as_integral", "Normalize as _integral", TRUE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output"),
                               outputs, G_N_ELEMENTS(outputs), (1 << DECONV_OUTPUT_DECONVOLVED));
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|_Display"),
                              displays, G_N_ELEMENTS(displays), DECONV_DISPLAY_DECONVOLVED);
    gwy_param_def_add_gwyenum(paramdef, PARAM_LCURVE, "lcurve", _("_L-curve display"),
                              lcurves, G_N_ELEMENTS(lcurves), DECONV_LCURVE_CURVATURE);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Sigma"), -8.0, 8.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA_RANGE, "sigma_range", _("_Sigma range"), -8.0, 8.0, 1.0);
    return paramdef;
}

static void
deconvolve(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    DeconvOutputType output;
    GwyDialogOutcome outcome;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_DATA_FIELD(args.field));

    args.params = gwy_params_new_from_settings(define_module_params());
    args.deconvolved = gwy_data_field_new_alike(args.field, TRUE);
    args.difference = gwy_data_field_new_alike(args.field, TRUE);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    output = gwy_params_get_flags(args.params, PARAM_OUTPUT_TYPE);
    if (!output || !gwy_params_get_image(args.params, PARAM_KERNEL))
        goto end;

    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    if (output & (1 << DECONV_OUTPUT_DECONVOLVED))
        create_output_field(args.deconvolved, data, id, _("Deconvolved"));
    if (output & (1 << DECONV_OUTPUT_DIFFERECE))
        create_output_field(args.difference, data, id, _("Difference"));

end:
    g_object_unref(args.deconvolved);
    g_object_unref(args.difference);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *vbox, *dataview, *notebook, *graph;
    GwyParamTable *table;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Deconvolve"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);

    vbox = gwy_vbox_new(4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox, gtk_label_new("Parameters"));

    gui.gmodel = gwy_graph_model_new();
    graph = gwy_graph_new(gui.gmodel);
    gtk_widget_set_size_request(graph, -1, PREVIEW_HALF_SIZE);
    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XLINES);
    gtk_box_pack_start(GTK_BOX(vbox), graph, TRUE, TRUE, 0);
    gui.area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph)));
    gui.selection = gwy_graph_area_get_selection(gui.area, GWY_GRAPH_STATUS_XLINES);

    table = gui.table_param = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_KERNEL);
    gwy_param_table_data_id_set_filter(table, PARAM_KERNEL, kernel_filter, args->field, NULL);
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA, "log<sub>10</sub>");
    gwy_param_table_append_info(table, LABEL_SIGMA, _("Sigma"));

    gwy_param_table_append_header(table, -1, _("L-Curve"));
    gwy_param_table_append_combo(table, PARAM_LCURVE);
    gwy_param_table_append_slider(table, PARAM_SIGMA_RANGE);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA_RANGE, "log<sub>10</sub>");
    gwy_param_table_append_info(table, LABEL_BEST_SIGMA, _("Best estimate sigma"));
    gwy_param_table_append_button(table, BUTTON_UPDATE_LCURVE, -1,
                                  RESPONSE_CALCULATE, _("_Update L-Curve"));
    gwy_param_table_append_button(table, BUTTON_USE_ESTIMATE, BUTTON_UPDATE_LCURVE,
                                  RESPONSE_ESTIMATE, _("_Use Estimate"));
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_AS_INTEGRAL);

    gwy_dialog_add_param_table(dialog, table);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("Output"));

    g_signal_connect_swapped(gui.table_param, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(graph_selected), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);
    g_free(gui.lcurvedata);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_DISPLAY)
        switch_display(gui);
    if (id < 0 || id == PARAM_LCURVE)
        switch_lcurve(gui);
    if (id < 0 || id == PARAM_KERNEL)
        clear_lcurve(gui);
    if (id < 0 || id == PARAM_OUTPUT_TYPE) {
        gboolean have_kernel = !gwy_params_data_id_is_none(params, PARAM_KERNEL);
        guint output = gwy_params_get_flags(params, PARAM_OUTPUT_TYPE);

        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, output && have_kernel);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_UPDATE_LCURVE, have_kernel);
        gwy_param_table_set_sensitive(gui->table_output, PARAM_AS_INTEGRAL, output & (1 << DECONV_OUTPUT_DECONVOLVED));
    }
    if (id < 0 || id == PARAM_SIGMA) {
        gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
        gchar *s = g_strdup_printf("%.4g", pow10(sigma));
        gwy_selection_set_data(gui->selection, 1, &sigma);
        gwy_param_table_info_set_valuestr(gui->table_param, LABEL_SIGMA, s);
        g_free(s);
    }
    if (id < 0 || id == PARAM_KERNEL || id == PARAM_SIGMA) {
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_USE_ESTIMATE, !!gui->lcurvedata);
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
    }
}

static void
clear_lcurve(ModuleGUI *gui)
{
    GWY_FREE(gui->lcurvedata);
    gui->nsteps = 0;
    gwy_selection_clear(gui->selection);
    gwy_graph_model_remove_all_curves(gui->gmodel);
    gwy_param_table_info_set_valuestr(gui->table_param, LABEL_BEST_SIGMA, _("unknown"));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_ESTIMATE) {
        if (gui->lcurvedata)
            gwy_param_table_set_double(gui->table_param, PARAM_SIGMA, gui->best_sigma);
    }
    else if (response == RESPONSE_CALCULATE) {
        calculate_lcurve(gui);
        switch_lcurve(gui);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_USE_ESTIMATE, !!gui->lcurvedata);
    }
}

static void
graph_selected(ModuleGUI *gui)
{
    gdouble sigma;

    if (gwy_selection_get_object(gui->selection, 0, &sigma))
        gwy_param_table_set_double(gui->table_param, PARAM_SIGMA, sigma);
}

static void
switch_lcurve(ModuleGUI *gui)
{
    static const LCurveDataType abscissae[] = {
        LCURVE_DATA_LOG10SIGMA, LCURVE_DATA_LOG10SIGMA, LCURVE_DATA_LOG10SIGMA, LCURVE_DATA_LOGRMS,
    };
    static const LCurveDataType ordinates[] = {
        LCURVE_DATA_DIFFERENCE, LCURVE_DATA_RMS, LCURVE_DATA_CURVATURE, LCURVE_DATA_LOGDIFFERENCE,
    };
    LCurveType lcurve = gwy_params_get_enum(gui->args->params, PARAM_LCURVE);
    gdouble lsigma = gwy_params_get_double(gui->args->params, PARAM_SIGMA);
    GwyGraphCurveModel *gcmodel;
    gint nsteps = gui->nsteps, shorten = 0;
    const gdouble *lcurvedata = gui->lcurvedata;

    gwy_graph_model_remove_all_curves(gui->gmodel);
    if (lcurve == DECONV_LCURVE_CURVATURE)
        shorten = 1;

    if (lcurvedata && nsteps > 2*shorten) {
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel,
                                       lcurvedata + nsteps*abscissae[lcurve] + shorten,
                                       lcurvedata + nsteps*ordinates[lcurve] + shorten,
                                       nsteps - 2*shorten);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", gwy_enum_to_string(lcurve, lcurves, G_N_ELEMENTS(lcurves)),
                     NULL);
        gwy_graph_model_add_curve(gui->gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (lcurve == DECONV_LCURVE_LCURVE) {
        gwy_graph_area_set_selection_editable(gui->area, FALSE);
        gwy_selection_clear(gui->selection);
        g_object_set(gui->gmodel,
                     "axis-label-bottom", "log ‖G-FH‖",
                     "axis-label-left", "log ‖F‖",
                     NULL);
    }
    else {
        gwy_graph_area_set_selection_editable(gui->area, TRUE);
        gwy_selection_set_data(gui->selection, 1, &lsigma);
        g_object_set(gui->gmodel,
                     "axis-label-bottom", "log<sub>10</sub>(σ)",
                     "axis-label-left", "",
                     NULL);
    }
}

static void
switch_display(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    DeconvDisplayType display = gwy_params_get_enum(args->params, PARAM_DISPLAY);

    if (display == DECONV_DISPLAY_DATA)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->field);
    else if (display == DECONV_DISPLAY_DECONVOLVED)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->deconvolved);
    else
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->difference);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->deconvolved);
    gwy_data_field_data_changed(args->difference);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static gboolean
kernel_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *kernel = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    if (kernel == field)
        return FALSE;
    if (gwy_data_field_get_xres(kernel) > gwy_data_field_get_xres(field))
        return FALSE;
    if (gwy_data_field_get_yres(kernel) > gwy_data_field_get_yres(field))
        return FALSE;

    return !gwy_data_field_check_compatibility(kernel, field,
                                               GWY_DATA_COMPATIBILITY_MEASURE | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble lsigma = gwy_params_get_double(args->params, PARAM_SIGMA);
    gboolean as_integral = gwy_params_get_boolean(args->params, PARAM_AS_INTEGRAL);
    GwyDataField *kernel = gwy_params_get_image(params, PARAM_KERNEL);
    GwyDataField *field = args->field, *deconv = args->deconvolved, *difference = args->difference;

    if (kernel) {
        deconvolve_with_kernel(field, kernel, deconv, difference, pow10(lsigma));
        if (!as_integral)
            adjust_deconv_to_non_integral(deconv);
    }
}

static void
deconvolve_with_kernel(GwyDataField *measured, GwyDataField *kernel,
                       GwyDataField *deconv, GwyDataField *difference,
                       gdouble sigma)
{
    GwyDataField *xm, *xkernel;
    gint xres, yres, extx, exty, txres, tyres;

    xres = gwy_data_field_get_xres(measured);
    yres = gwy_data_field_get_yres(measured);
    txres = gwy_data_field_get_xres(kernel);
    tyres = gwy_data_field_get_yres(kernel);
    extx = txres/2 + 1;
    exty = tyres/2 + 1;
    xm = gwy_data_field_extend(measured, extx, extx, exty, exty, GWY_EXTERIOR_MIRROR_EXTEND, 0.0, FALSE);
    xkernel = gwy_data_field_new_alike(xm, TRUE);
    gwy_data_field_copy_units(kernel, xkernel);
    gwy_data_field_area_copy(kernel, xkernel,
                             0, 0, txres, tyres,
                             xres/2 + extx - txres/2, yres/2 + exty - tyres/2);
    gwy_data_field_deconvolve_regularized(xm, xkernel, deconv, sigma);
    g_object_unref(xkernel);
    g_object_unref(xm);
    gwy_data_field_resize(deconv, extx, exty, xres + extx, yres + exty);

    gwy_data_field_copy(deconv, difference, TRUE);
    field_convolve_default(difference, kernel);
    gwy_data_field_subtract_fields(difference, measured, difference);
}

static void
get_curvatures(gdouble *xdata, gdouble *ydata, gdouble *curvaturedata, gint nsteps)
{
    gint i;
    gdouble xd, yd, xdd, ydd;

    for (i = 0; i < nsteps; i++) {
        if (i == 0 || i == nsteps-1) {
            xd = yd = xdd = ydd = 0;
            curvaturedata[i] = 0;
        }
        else {
            xd = (xdata[i+1] - xdata[i-1])/2;
            yd = (ydata[i+1] - ydata[i-1])/2;

            xdd = (xdata[i+1] + xdata[i-1] - 2*xdata[i])/4;
            ydd = (ydata[i+1] + ydata[i-1] - 2*ydata[i])/4;
            if (xd*xd + yd*yd)
                curvaturedata[i] = (xd*ydd - yd*xdd)/pow(xd*xd + yd*yd, 1.5);
            else
                curvaturedata[i] = 0;
        }
    }
}

static void
calculate_lcurve(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *lfield, *deconv, *difference;
    GwyDataField *kernel = gwy_params_get_image(params, PARAM_KERNEL);
    gdouble mean_sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble sigma_range = gwy_params_get_double(params, PARAM_SIGMA_RANGE);
    gdouble lsigma, sigma, max, maxsigma, x;
    gdouble *lcurvedata;
    gint i, maxsigmapos, nsteps;
    gchar *s;

    if (!kernel)
        return;

    gwy_app_wait_start(GTK_WINDOW(gui->dialog), _("Computing L-curve data..."));

    gui->nsteps = nsteps = NSTEPS;
    gui->lcurvedata = lcurvedata = g_renew(gdouble, gui->lcurvedata, nsteps*LCURVE_DATA_NTYPES);
    lfield = gwy_data_field_new_alike(field, TRUE);
    deconv = gwy_data_field_new_alike(field, TRUE);
    difference = gwy_data_field_new_alike(field, TRUE);
    gwy_data_field_copy(field, lfield, TRUE);
    gwy_data_field_add(lfield, -gwy_data_field_get_avg(field));

    for (i = 0; i < nsteps; i++) {
        if (!gwy_app_wait_set_fraction((gdouble)i/nsteps)) {
            clear_lcurve(gui);
            goto end;
        }

        lsigma = mean_sigma - sigma_range/2 + i*sigma_range/(nsteps - 1);
        lcurvedata[nsteps*LCURVE_DATA_LOG10SIGMA + i] = lsigma;
        sigma = pow10(lsigma);
        gwy_data_field_fill(deconv, 0);

        deconvolve_with_kernel(lfield, kernel, deconv, difference, sigma);

        lcurvedata[nsteps*LCURVE_DATA_DIFFERENCE + i] = sqrt(gwy_data_field_get_mean_square(difference));
        lcurvedata[nsteps*LCURVE_DATA_RMS + i] = gwy_data_field_get_rms(deconv);
        if (!lcurvedata[nsteps*LCURVE_DATA_RMS + i] || !lcurvedata[nsteps*LCURVE_DATA_DIFFERENCE + i]) {
            clear_lcurve(gui);
            goto end;
        }
        lcurvedata[nsteps*LCURVE_DATA_LOGRMS + i] = log(lcurvedata[nsteps*LCURVE_DATA_RMS + i]);
        lcurvedata[nsteps*LCURVE_DATA_LOGDIFFERENCE + i] = log(lcurvedata[nsteps*LCURVE_DATA_DIFFERENCE + i]);
    }

    get_curvatures(lcurvedata + nsteps*LCURVE_DATA_LOGDIFFERENCE,
                   lcurvedata + nsteps*LCURVE_DATA_LOGRMS,
                   lcurvedata + nsteps*LCURVE_DATA_CURVATURE,
                   nsteps);

    max = -G_MAXDOUBLE;
    maxsigmapos = 0;
    maxsigma = mean_sigma;
    for (i = 1; i < nsteps-1; i++) {
         if (lcurvedata[nsteps*LCURVE_DATA_CURVATURE + i] > max) {
             maxsigmapos = i;
             max = lcurvedata[nsteps*LCURVE_DATA_CURVATURE + i];
             maxsigma = lcurvedata[nsteps*LCURVE_DATA_LOG10SIGMA + i];
         }
    }
    if (maxsigmapos > 1 && maxsigmapos < nsteps-2) {
        gwy_math_refine_maximum_1d(lcurvedata + nsteps*LCURVE_DATA_CURVATURE + maxsigmapos-1, &x);
        maxsigma += x*sigma_range/(nsteps - 1);
    }
    gui->best_sigma = maxsigma;

    s = g_strdup_printf("%.4g (log<sub>10</sub>: %.4f)", pow10(maxsigma), maxsigma);
    gwy_param_table_info_set_valuestr(gui->table_param, LABEL_BEST_SIGMA, s);
    g_free(s);

end:
    gwy_app_wait_finish();
    g_object_unref(lfield);
    g_object_unref(deconv);
    g_object_unref(difference);
}

static gint
create_output_field(GwyDataField *field,
                    GwyContainer *data,
                    gint id,
                    const gchar *name)
{
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, name);
    gwy_app_channel_log_add_proc(data, id, newid);

    return newid;
}

static void
adjust_deconv_to_non_integral(GwyDataField *deconv)
{
    GwySIUnit *xyunit, *zunit;

    xyunit = gwy_data_field_get_si_unit_xy(deconv);
    zunit = gwy_data_field_get_si_unit_z(deconv);
    gwy_si_unit_power_multiply(zunit, 1, xyunit, 2, zunit);

    gwy_data_field_multiply(deconv, gwy_data_field_get_dx(deconv) * gwy_data_field_get_dy(deconv));
    gwy_data_field_data_changed(deconv);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
