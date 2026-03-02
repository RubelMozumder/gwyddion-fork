/*
 *  $Id: graph_simplemech.c 24587 2022-02-05 19:41:51Z klapetek $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>


enum {
    PARAM_CURVE_APPROACH,
    PARAM_GRAPH_APPROACH,
    PARAM_CURVE_RETRACT,
    PARAM_GRAPH_RETRACT,
    PARAM_BASELINE_RANGE,
    PARAM_FIT_UPPER,
    PARAM_FIT_LOWER,
    PARAM_RADIUS,
    PARAM_NU,
    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyGraphModel *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyResults *results;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register              (void);
static GwyParamDef*     define_module_params         (void);
static void             graph_simplemech             (GwyGraph *graph);
static GwyDialogOutcome run_gui                      (ModuleArgs *args,
                                                      GwyContainer *data);
static void             execute                      (ModuleArgs *args,
                                                      GwyResults *results);
static void             param_changed                (ModuleGUI *gui,
                                                      gint id);
static GwyResults*      create_results               (GwySIUnit *xunit,
                                                      GwySIUnit *yunit);

static void             preview                      (gpointer user_data);


static const gchar* fitresults[] = { "modulus", "adhesion", "deformation", "dissipation", "baseline", "peak",};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Get simple mechanical quantities"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, graph_simplemech)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_simplemech",
                            (GwyGraphFunc)&graph_simplemech,
                            N_("/_Force Distance/_Nanomechanical Fit..."),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Evaluate DMT modulus, adhesion, deformation and dissipation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());

    gwy_param_def_add_graph_id(paramdef, PARAM_GRAPH_APPROACH, NULL, "Approach graph");
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE_APPROACH, "curve", _("Approach curve"));

    gwy_param_def_add_graph_id(paramdef, PARAM_GRAPH_RETRACT, NULL, "Retract graph");
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE_RETRACT, "curve", _("Retract curve"));

    gwy_param_def_add_double(paramdef, PARAM_BASELINE_RANGE, "baseline", _("Baseline _range"), 0.0, 0.5, 0.2);
    gwy_param_def_add_double(paramdef, PARAM_FIT_UPPER, "upper", _("Fit _upper limit"), 0.6, 1.0, 0.8);
    gwy_param_def_add_double(paramdef, PARAM_FIT_LOWER, "lower", _("Fit _lower limit"), 0.0, 0.4, 0.2);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("_Tip radius"), 0, 500e-9, 20e-9);
    gwy_param_def_add_double(paramdef, PARAM_NU, "nu", _("_Poisson's ratio"), 0, 1, 0.25);

    return paramdef;
}

static void
graph_simplemech(G_GNUC_UNUSED GwyGraph *graph)
{
    GwyDialogOutcome outcome;
    GwyContainer *data;
    ModuleArgs args;
    GwyAppDataId graph_id;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_app_data_browser_get_current(GWY_APP_CONTAINER_ID, &graph_id.datano,
                                     GWY_APP_GRAPH_MODEL_ID, &graph_id.id,
                                     0);

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.result = gwy_graph_model_new();

    gwy_params_set_graph_id(args.params, PARAM_GRAPH_APPROACH, graph_id);
    gwy_params_set_graph_id(args.params, PARAM_GRAPH_RETRACT, graph_id);

    outcome = run_gui(&args, data);
    gwy_params_save_to_settings(args.params);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args, NULL);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, G_GNUC_UNUSED GwyContainer *data)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwySIUnit *xunit, *yunit;
    ModuleGUI gui;

    /* This is to get the target graph filter right. */
    execute(args, NULL);

    gwy_clear(&gui, 1);
    gui.args = args;
    g_object_set(args->result, "label-visible", FALSE, NULL);

    g_object_get(gwy_params_get_graph(args->params, PARAM_GRAPH_APPROACH),
                           "si-unit-x", &xunit, "si-unit-y", &yunit, NULL);
    gui.results = create_results(xunit, yunit);

    gui.dialog = gwy_dialog_new(_("Nanomechanical Fit"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gwy_dialog_have_result(dialog);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(args->result);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    g_object_set(args->result, "si-unit-x", xunit, "si-unit-y", yunit, NULL);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_id(table, PARAM_GRAPH_APPROACH);
    gwy_param_table_append_graph_curve(table, PARAM_CURVE_APPROACH, gwy_params_get_graph(args->params,
                                                                                PARAM_GRAPH_APPROACH));

    gwy_param_table_append_graph_id(table, PARAM_GRAPH_RETRACT);
    gwy_param_table_append_graph_curve(table, PARAM_CURVE_RETRACT, gwy_params_get_graph(args->params,
                                                                                PARAM_GRAPH_RETRACT));

    gwy_param_table_append_slider(table, PARAM_BASELINE_RANGE);
    gwy_param_table_slider_set_factor(table, PARAM_BASELINE_RANGE, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_BASELINE_RANGE, "%");
    gwy_param_table_append_slider(table, PARAM_FIT_UPPER);
    gwy_param_table_slider_set_factor(table, PARAM_FIT_UPPER, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_FIT_UPPER, "%");
    gwy_param_table_append_slider(table, PARAM_FIT_LOWER);
    gwy_param_table_slider_set_factor(table, PARAM_FIT_LOWER, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_FIT_LOWER, "%");
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_set_factor(table, PARAM_RADIUS, 1e9);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, "nm");
    gwy_param_table_append_slider(table, PARAM_NU);

    gwy_param_table_append_header(table, -1, _("Results"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results,
                                    fitresults, G_N_ELEMENTS(fitresults));

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, TRUE, 0);

    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    param_changed(&gui, PARAM_GRAPH_APPROACH);

    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(args, gui.results);

    g_object_set(args->result, "label-visible", TRUE, NULL);


end:
    g_object_unref(gui.results);
    g_object_unref(xunit);
    g_object_unref(yunit);


    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    gchar *label_bottom, *label_left;

    if (id == PARAM_GRAPH_APPROACH || id == PARAM_GRAPH_RETRACT) {

        gwy_param_table_graph_curve_set_model(table, PARAM_CURVE_APPROACH,
                                              gwy_params_get_graph(params, PARAM_GRAPH_APPROACH));
        gwy_param_table_graph_curve_set_model(table, PARAM_CURVE_RETRACT,
                                              gwy_params_get_graph(params, PARAM_GRAPH_RETRACT));

        g_object_get(gwy_params_get_graph(gui->args->params, PARAM_GRAPH_APPROACH),
                           "axis-label-bottom", &label_bottom,
                           "axis-label-left", &label_left, NULL);
        g_object_set(gui->args->result, "axis-label-bottom", label_bottom,
                     "axis-label-left", label_left, NULL);

    }
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));

}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args, gui->results);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    gwy_param_table_results_fill(gui->table, WIDGET_RESULTS);

}

//param[0]: F_ad, param[1]: E (modulus), param[2]: R, param[3]: xshift, param[4] nu
static gdouble
func_dmt(gdouble x,
                 G_GNUC_UNUSED gint n_param,
                 const gdouble *param,
                 G_GNUC_UNUSED gpointer user_data,
                 gboolean *fres)
{
    /*xc, F_ad, R, E, nu*/
    double xr = fabs(param[0] - x); //this should be deformation and should be already entered only positive
    *fres = TRUE;
    return 4.0*param[3]/3.0/(1-param[4]*param[4])*sqrt(param[2]*xr*xr*xr) + param[1];
}

static void
execute(ModuleArgs *args, GwyResults *results)
{
    GwyParams *params = args->params;
    GwyGraphModel *result = args->result;
    gdouble baseline_range = gwy_params_get_double(params, PARAM_BASELINE_RANGE);
    gdouble fit_upper = gwy_params_get_double(params, PARAM_FIT_UPPER);
    gdouble fit_lower = gwy_params_get_double(params, PARAM_FIT_LOWER);
    gdouble radius = gwy_params_get_double(params, PARAM_RADIUS);
    gdouble nu = gwy_params_get_double(params, PARAM_NU);

    GwyGraphCurveModel *gcmodel_approach;
    GwyGraphCurveModel *gcmodel_retract;
    GwyGraphCurveModel *gcmodel_points;
    GwyGraphCurveModel *gcmodel_baseline;
    GwyGraphCurveModel *gcmodel_fit;

    GwyNLFitter *fitter;
    gint i, nadata, nrdata, nbaseline;
    const gdouble *xadata, *yadata, *xrdata, *yrdata;
    gdouble xp[4], yp[4];
    gdouble xb[2], yb[2];
    gdouble afrom, ato, rfrom, rto;
    gdouble param[5];
    gint fix[5];
    gdouble *nxdata, *nydata;
    gdouble upperval, lowerval, xupper, xlower;
    gint nfitdata, iupper, ilower, iadhesion;
    gboolean fres, fit_done;
    gdouble adis, rdis;

    gdouble modulus, adhesion, deformation, baseline, peak;
    gdouble xadhesion, xpeak, xzero, yzero;

    gwy_graph_model_remove_all_curves(result);
    gcmodel_approach = gwy_graph_model_get_curve(gwy_params_get_graph(params, PARAM_GRAPH_APPROACH),
                                                       gwy_params_get_int(params, PARAM_CURVE_APPROACH));
    gcmodel_retract = gwy_graph_model_get_curve(gwy_params_get_graph(params, PARAM_GRAPH_RETRACT),
                                                       gwy_params_get_int(params, PARAM_CURVE_RETRACT));


    gwy_graph_model_add_curve(result, gcmodel_approach);
    gwy_graph_model_add_curve(result, gcmodel_retract);

    xadata = gwy_graph_curve_model_get_xdata(gcmodel_approach);
    yadata = gwy_graph_curve_model_get_ydata(gcmodel_approach);
    nadata = gwy_graph_curve_model_get_ndata(gcmodel_approach);

    xrdata = gwy_graph_curve_model_get_xdata(gcmodel_retract);
    yrdata = gwy_graph_curve_model_get_ydata(gcmodel_retract);
    nrdata = gwy_graph_curve_model_get_ndata(gcmodel_retract);

    gcmodel_points = gwy_graph_curve_model_new();
    gcmodel_baseline = gwy_graph_curve_model_new();
    gcmodel_fit = gwy_graph_curve_model_new();

    peak = -G_MAXDOUBLE;
    xpeak = xrdata[0];

    afrom = G_MAXDOUBLE;
    ato = -G_MAXDOUBLE;

    //get peak force, approach curve range and force integral in approach direction
    adis = 0;
    for (i=0; i<nadata; i++) {
        if (peak < yadata[i])
        {
            peak = yadata[i];
            xpeak = xadata[i];
        }
        if (xadata[i]<afrom) afrom = xadata[i];
        if (xadata[i]>ato) ato = xadata[i];

        if (i<(nadata-1)) adis += fabs(xadata[i]-xadata[i+1])*(yadata[i]+yadata[i+1])/2.0;
    }

    //fit baseline - average value on approach curve flat part
    baseline = 0;
    nbaseline = 0;
    for (i=0; i<nadata; i++) {
        if (xadata[i]>(ato - baseline_range*(ato-afrom)))
        {
            baseline += yadata[i];
            nbaseline++;
        }
     }
    baseline /= nbaseline;

    //find zero force - point where we reach baseline force on approach curve when going from peak value
    xzero = xadata[nadata-1]; //safe defaults
    yzero = yadata[nadata-1];
    for (i=(nadata-1); i>1; i--)
    {
        if (yadata[i]>=baseline && yadata[i+1]<baseline)
        {
            xzero = xadata[i];
            yzero = yadata[i];
        }
    }

    //calculate deformation
    deformation = xzero - xpeak;

    //get adhesion as minimum on retract curve and get retract curve range
    adhesion = G_MAXDOUBLE;
    xadhesion = xrdata[0];
    iadhesion = 0;
    rfrom = G_MAXDOUBLE;
    rto = -G_MAXDOUBLE;
    rdis = 0;
    for (i=0; i<nrdata; i++) {
        //printf("yrdata %d %g %g\n", i, xrdata[i], yrdata[i]);
        if (adhesion > yrdata[i])
        {
            adhesion = yrdata[i];
            xadhesion = xrdata[i];
            iadhesion = i;
        }
        if (peak < yrdata[i])
        {
            peak = yrdata[i];
            xpeak = xrdata[i];
        }
        if (xrdata[i]<rfrom) rfrom = xrdata[i];
        if (xrdata[i]>rto) rto = xrdata[i];

        if (i<nrdata-1) rdis += fabs(xrdata[i]-xrdata[i+1])*(yrdata[i]+yrdata[i+1])/2.0;

    }

    //find dmt fit limits as points where we reach the points on retract curve when going from peak value
    xupper = xpeak;     //safe inits
    xlower = xadhesion;
    iupper = 0;
    ilower = iadhesion;
    upperval = adhesion + fit_upper*(peak-adhesion);
    lowerval = adhesion + fit_lower*(peak-adhesion);

    for (i=(nrdata-1); i>1; i--)
    {
        if (yrdata[i]>=upperval && yrdata[i+1]<upperval)
        {
            xupper = xrdata[i];
            iupper = i;
        }
        if (yrdata[i]>=lowerval && yrdata[i+1]<lowerval)
        {
            xlower = xrdata[i];
            ilower = i;
        }
    }
    //printf("fit: lower range (%g %g) i %d   upper range (%g %g), i %d\n", xlower, lowerval, ilower,
    //                                                                      xupper, upperval, iupper);

    fit_done = FALSE;
    nfitdata = 0;
    modulus = 5e7;
    if ((ilower-iupper)>4) //random criterion
    {
       //fit dmt
       fitter = gwy_math_nlfit_new((GwyNLFitFunc)func_dmt, gwy_math_nlfit_diff);

       param[0] = xadhesion; //adhesion comes from direct measurement
       fix[0] = 0;

       param[1] = adhesion; //modulus comes from some guess?
       fix[1] = 1;

       param[2] = radius;  //radius comes from user
       fix[2] = 1;

       param[3] = modulus; //x shift of the curve, where this should be?
       fix[3] = 0;

       param[4] = nu;
       fix[4] = 1;


       if (gwy_math_nlfit_fit_full(fitter, ilower-iupper, xrdata + iupper, yrdata + iupper, NULL,
                                                                   5, param, fix, NULL, NULL) >=0)
       {
          //printf("fit ok: params %g %g %g %g\n", param[0], param[1], param[2], param[3]);

          modulus = param[3];

          //plot the fit result
          gcmodel_fit = gwy_graph_curve_model_new();
          nfitdata = 100;
          nxdata = g_new(gdouble, nfitdata);
          nydata = g_new(gdouble, nfitdata);

          for (i=0; i<nfitdata; i++) {
              nxdata[i] = xupper + (gdouble)i*(xlower-xupper)/(gdouble)nfitdata;
              nydata[i] = func_dmt(nxdata[i], 4, param, NULL, &fres);
          }

          fit_done = TRUE;
       } else printf("fit failed\n");

       gwy_math_nlfit_free(fitter);
    }

    //fill graphs
    xp[0] = xadhesion;
    yp[0] = adhesion;
    xp[1] = xpeak;
    yp[1] = peak;
    xp[2] = xzero;
    yp[2] = yzero;

    xb[0] = ato - baseline_range*(ato-afrom);
    yb[0] = baseline;
    xb[1] = ato;
    yb[1] = baseline;


    gwy_graph_curve_model_set_data(gcmodel_points, xp, yp, 3);
    g_object_set(gcmodel_points, "mode", GWY_GRAPH_CURVE_POINTS, NULL);
    g_object_set(gcmodel_points, "description", g_strdup(_("pick points")), NULL);
    gwy_graph_model_add_curve(result, gcmodel_points);

    gwy_graph_curve_model_set_data(gcmodel_baseline, xb, yb, 2);
    g_object_set(gcmodel_baseline,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(2),
                 "description", g_strdup(_("Baseline fit")),
                 "line-width", 3,
                 NULL);
    gwy_graph_model_add_curve(result, gcmodel_baseline);

    if (nfitdata>0)
    {
       gwy_graph_curve_model_set_data(gcmodel_fit, nxdata, nydata, nfitdata);
       g_object_set(gcmodel_fit,
                    "mode", GWY_GRAPH_CURVE_LINE,
                    "color", gwy_graph_get_preset_color(3),
                    "description", _("DMT fit"),
                    "line-width", 3,
                    NULL);
       gwy_graph_model_add_curve(result, gcmodel_fit);
    }

    if (results) {
        gwy_results_fill_values(results,
                                "adhesion", adhesion-baseline,
                                "baseline", baseline,
                                "peak", peak,
                                "deformation", deformation,
                                "dissipation", (adis-rdis)/1.602176634e-19,
                                NULL);

        if (fit_done) gwy_results_fill_values(results, "modulus", modulus/1e6, NULL);

    }

    if (nfitdata > 0)
    {
       g_free(nxdata);
       g_free(nydata);
    }

 }

static GwyResults*
create_results(GwySIUnit *xunit, GwySIUnit *yunit)
{
    GwyResults *results;

    results = gwy_results_new();
    gwy_results_add_header(results, N_("Results"));
    gwy_results_add_value(results, "modulus", N_("DMT modulus"), "unit-str", "MPa", NULL);
    gwy_results_add_value_z(results, "adhesion", N_("Adhesion"));
    gwy_results_add_value_x(results, "deformation", N_("Deformation"));
    gwy_results_add_value(results, "dissipation", N_("Dissipated work"), "unit-str", "eV", NULL);
    gwy_results_add_value_z(results, "baseline", N_("Baseline force"));
    gwy_results_add_value_z(results, "peak", N_("Maximum force"));

    gwy_results_set_unit(results, "x", xunit);
    gwy_results_set_unit(results, "z", yunit);

    return results;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
