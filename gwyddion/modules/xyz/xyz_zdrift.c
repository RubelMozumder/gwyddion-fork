/*
 *  $Id: xyz_zdrift.c 26355 2024-05-21 08:23:55Z yeti-dn $
 *  Copyright (C) 2016-2023 David Necas (Yeti).
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
#include <libprocess/surface.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    MAXORDER = 10,
    MAXN = 1000
};

enum {
    RESPONSE_APPLY = 5,
    RESPONSE_DRIFT = 6,
};

enum {
    PARAM_METHOD,
    PARAM_FROM,
    PARAM_TO,
    PARAM_DIST,
    PARAM_ORDER,
};

typedef enum {
    METHOD_FULL      = 0,
    METHOD_NEIGHBORS = 1,
} ZDriftMethodType;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    GwySurface *result;
    gint tmax;
    gdouble fitcoeffs[MAXORDER + 1];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwySelection *graph_selection;
    GwyDataField *dfield;
    gboolean drift_done;
    gint ndiv;
 } ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             graph_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static void             dialog_response         (GwyDialog *dialog,
                                                 gint response,
                                                 ModuleGUI *gui);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             update_drift_curve      (ModuleGUI *gui);
static void             update_fit_curve        (ModuleGUI *gui);
static void             xyzzdrift               (GwyContainer *data,
                                                 GwyRunType runtype);
static void             preview                 (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("XYZ data z-drift correction."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2023",
};

GWY_MODULE_QUERY2(module_info, xyz_zdrift)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_zdrift",
                          (GwyXYZFunc)&xyzzdrift,
                          N_("/Correct Z _Drift..."),
                          NULL,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Correct z drift"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Full data"),    METHOD_FULL,      },
        { N_("XY neighbors"), METHOD_NEIGHBORS, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "graph", _("Graph"),
                              methods, G_N_ELEMENTS(methods), METHOD_FULL);
    gwy_param_def_add_int(paramdef, PARAM_FROM, "from", _("From index"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_TO, "to", _("To index"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_ORDER, "order", _("Polynomial degree"), 0, MAXORDER, 1);
    gwy_param_def_add_double(paramdef, PARAM_DIST, "dist", _("Neighbor distance"), 0, 10, 0.001);

    return paramdef;
}

static void
xyzzdrift(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid, k;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(args.surface));

    args.params = gwy_params_new_from_settings(define_module_params());
    args.tmax = gwy_surface_get_npoints(args.surface);
    args.result = NULL;
    gwy_params_set_int(args.params, PARAM_FROM, -1); //FIXME, trying to prevent confusion in initi
    gwy_params_set_int(args.params, PARAM_TO, -1);

    for (k = 0; k <= MAXORDER; k++)
        args.fitcoeffs[k] = 0;

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome == GWY_DIALOG_PROCEED && !args.result)
        execute(&args);

    newid = gwy_app_data_browser_add_surface(args.result, data, TRUE);
    gwy_app_set_surface_title(data, newid, _("Z drift corrected"));
    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);

    g_object_unref(args.result);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyParamTable *table;
    GtkWidget *graph, *area, *hbox, *dataview;
    GwyGraphCurveModel *gcmodel;
    const guchar *gradient;
    GwyDialogOutcome outcome;

    gui.dialog = gwy_dialog_new(_("Correct z drift"));
    dialog = GWY_DIALOG(gui.dialog);

    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Calculate drift"), RESPONSE_DRIFT);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Apply correction"), RESPONSE_APPLY);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), GWY_RESPONSE_RESET);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.args = args;
    gui.drift_done = FALSE;
    gui.data = gwy_container_new();
    gui.dfield = gwy_data_field_new(10, 10, 10, 10, FALSE);

    gwy_preview_surface_to_datafield(args->surface, gui.dfield, PREVIEW_SIZE, PREVIEW_SIZE, GWY_PREVIEW_SURFACE_FILL);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), gui.dfield);

    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
       gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    gui.gmodel = gwy_graph_model_new();
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, "description", "drift data", NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, "description", "fit", NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    //gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.graph_selection, 1);

    gui.table_options = table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_METHOD);
    gwy_param_table_append_slider(table, PARAM_DIST);
    gwy_param_table_set_unitstr(table, PARAM_DIST, "‰");
    //gwy_param_table_slider_set_mapping(table, PARAM_DIST, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_ORDER);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, TRUE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.graph_selection, "changed", G_CALLBACK(graph_selection_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.dfield);
    g_object_unref(gui.data);

    return outcome;
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET) {
        gwy_selection_clear(gui->graph_selection);
        gwy_preview_surface_to_datafield(gui->args->surface, gui->dfield, PREVIEW_SIZE, PREVIEW_SIZE,
                                         GWY_PREVIEW_SURFACE_FILL);
        gwy_data_field_data_changed(gui->dfield);
    }
    else if (response == RESPONSE_DRIFT) {
        update_drift_curve(gui);
        update_fit_curve(gui);
    }
    else if (response == RESPONSE_APPLY) {
        execute(gui->args);
        gwy_preview_surface_to_datafield(gui->args->result, gui->dfield, PREVIEW_SIZE, PREVIEW_SIZE,
                                         GWY_PREVIEW_SURFACE_FILL);

        gwy_data_field_data_changed(gui->dfield);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    update_fit_curve(gui);
}

static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    update_fit_curve(gui);
}

static void
graph_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    gdouble z1z2[2];
    gint from = -1, to = -1;

    if (gwy_selection_get_object(selection, 0, z1z2)) {
        from = CLAMP(z1z2[0], 0, args->tmax);
        to = CLAMP(z1z2[1], 0, args->tmax);
        GWY_ORDER(gint, from, to);
        if (to - from < 2)
            from = to = -1;
    }

    if (from == -1) {
        from = 0;
        to = args->tmax;
    }
    gwy_params_set_int(args->params, PARAM_FROM, from);
    gwy_params_set_int(args->params, PARAM_TO, to);

    gwy_param_table_param_changed(gui->table_options, PARAM_FROM);
    gwy_param_table_param_changed(gui->table_options, PARAM_TO);
}

static gdouble
polynom(gint degree, gdouble *coeffs, gdouble x)
{
    int i;
    double sum, xpow;

    sum = coeffs[0];
    xpow = 1;
    for (i = 1; i <= degree; i++) {
        xpow *= x;
        sum += coeffs[i]*xpow;
    }
    return sum;
}

static void
fit_polynom(gint degree, const gdouble *z, const gdouble *drift, gdouble *fit,
            gint n, gint from, gint fitn, gdouble *fitcoeffs)
{
    gint k;
    gdouble *coeffs;

    //printf("from %d\n", from);
    coeffs = gwy_math_fit_polynom(fitn, z + from, drift + from, degree, NULL);
    if (coeffs) {
        for (k = 0; k < n; k++)
            fit[k] = polynom(degree, coeffs, z[k]);

        for (k = 0; k < MAXORDER; k++) {
            if (k <= degree)
                fitcoeffs[k] = coeffs[k];
            else
                fitcoeffs[k] = 0;
        }
        g_free(coeffs);
    }
}

static void
update_drift_curve(ModuleGUI *gui)
{
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->gmodel, 0);
    GwySurface *surface = gui->args->surface;
    GwyDataField *sfield, *mfield;
    GwyDataLine *avg, *weights;
    const GwyXYZ *xyz;
    gdouble *linedata, *xlinedata, *sdata, *mdata, *adata;
    gint k, n, m, l, linen, foundn;
    gint method = gwy_params_get_enum(gui->args->params, PARAM_METHOD);
    gdouble dist = gwy_params_get_double(gui->args->params, PARAM_DIST);
    gdouble mindist = gui->args->tmax/100;
    gdouble tolerance, stol, xmin, xmax;
    GRand *rng = NULL;

    gwy_surface_get_xrange(surface, &xmin, &xmax);
    tolerance = 0.001*dist*(xmax - xmin);
    stol = tolerance*tolerance;

    xyz = gwy_surface_get_data_const(surface);
    if (gui->args->tmax > 5000000)
        gui->ndiv = 10000;
    else if (gui->args->tmax > 500000)
        gui->ndiv = 1000;
    else if (gui->args->tmax > 50000)
        gui->ndiv = 100;
    else if (gui->args->tmax > 5000)
        gui->ndiv = 10;
    else
        gui->ndiv = 1;

    linen = gui->args->tmax/gui->ndiv;
    xlinedata = g_new(gdouble, linen);
    linedata = g_new(gdouble, linen);

    if (method == METHOD_FULL) {
         m = 0;
         for (n = 0; n < linen; n++) {
             linedata[n] = 0;
             for (k = 0; k < gui->ndiv; k++) {
                 linedata[n] += xyz[m++].z;
             }
             xlinedata[n] = n*gui->ndiv;
             linedata[n] /= gui->ndiv;
         }
    }
    else {
        printf("linen %d MAXN %d\n", linen, MAXN);
        sfield = gwy_data_field_new(linen, MAXN, linen, MAXN, TRUE);
        mfield = gwy_data_field_new_alike(sfield, TRUE);
        avg = gwy_data_line_new(linen, linen, TRUE);
        weights = gwy_data_line_new(linen, linen, TRUE);

        sdata = gwy_data_field_get_data(sfield);
        mdata = gwy_data_field_get_data(mfield);

        foundn = 0;
        rng = g_rand_new();
        gwy_app_wait_start(GTK_WINDOW(gui->dialog), _("Searching for neighbors..."));
        for (m = 0; m < 100000; m++) {
            k = g_rand_int_range(rng, 0, gui->args->tmax);
            for (n = k + mindist; n < gui->args->tmax - mindist; n++) {
                if (((xyz[k].x-xyz[n].x)*(xyz[k].x-xyz[n].x) + (xyz[k].y-xyz[n].y)*(xyz[k].y-xyz[n].y)) < stol) {
                    for (l = k/gui->ndiv; l < n/gui->ndiv; l++) {
                        sdata[l + linen*foundn] = (xyz[n].z - xyz[k].z)/(n - k)*gui->ndiv;
                        mdata[l + linen*foundn] = 1;
                    }

                    /*printf("%d and %d match together, distance is %g   placing at %g %g\n", k, n,
                      sqrt(((xyz[k].x-xyz[n].x)*(xyz[k].x-xyz[n].x) + (xyz[k].y-xyz[n].y)*(xyz[k].y-xyz[n].y))),
                      xlinedata[foundn], linedata[foundn]);*/
                    if (foundn >= MAXN-1)
                        break;
                    else
                        foundn++;

                    n += mindist; //skip too close answers

                    if (!gwy_app_wait_set_fraction((gdouble)foundn/MAXN))
                        break;
                }
            }
            if (foundn >= MAXN-1)
                break;
        }
        gwy_app_wait_finish();

        if (foundn > 0) {
            gwy_data_field_get_line_stats_mask(sfield, mfield, GWY_MASK_INCLUDE,
                                               avg, weights, 0, 0, linen, foundn,
                                               GWY_LINE_STAT_MEAN,
                                              GWY_ORIENTATION_VERTICAL);
            adata = gwy_data_line_get_data(avg);
            //wdata = gwy_data_line_get_data(weights);

            xlinedata[0] = 0;
            linedata[0] = adata[0];
            for (m = 1; m < linen; m++) {
                xlinedata[m] = m*gui->ndiv;
                linedata[m] = adata[m] + linedata[m-1];
                //        printf("%d %g %g\n", m, xlinedata[m], linedata[m]);
            }
        }

        g_object_unref(sfield);
        g_object_unref(mfield);
        g_object_unref(avg);
        g_object_unref(weights);
    }

    gwy_graph_curve_model_set_data(gcmodel, xlinedata, linedata, linen);

    gui->drift_done = TRUE;

    g_free(linedata);
    g_free(xlinedata);
}

static void
update_fit_curve(ModuleGUI *gui)
{
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->gmodel, 0);
    GwyGraphCurveModel *fitmodel = gwy_graph_model_get_curve(gui->gmodel, 1);
    gint degree = gwy_params_get_int(gui->args->params, PARAM_ORDER);
    gint from = gwy_params_get_int(gui->args->params, PARAM_FROM);
    gint to = gwy_params_get_int(gui->args->params, PARAM_TO);
    const gdouble *linedata, *xlinedata;
    gdouble *fitdata;
    gint linen, fitn, fitfrom;

    if (!gui->drift_done)
        return;

    linen = gwy_graph_curve_model_get_ndata(gcmodel);
    xlinedata = gwy_graph_curve_model_get_xdata(gcmodel);
    linedata = gwy_graph_curve_model_get_ydata(gcmodel);
    fitdata = g_new(gdouble, linen);

    if (from == -1)
        from = 0;
    if (to == -1)
        to = gui->args->tmax;

    fitn = MIN((to - from)/gui->ndiv, linen);
    fitfrom = from/gui->ndiv;

    //    printf("method %d  from %d to %d fitfrom %d  n %d\n", method, from, to, fitfrom, linen);
    fit_polynom(degree, xlinedata, linedata, fitdata, linen, fitfrom, fitn, gui->args->fitcoeffs);

    gwy_graph_curve_model_set_data(fitmodel, xlinedata, fitdata, linen);

    g_free(fitdata);
}

static void
execute(ModuleArgs *args)
{
    GwySurface *surface = args->surface;
    GwySurface *result;
    const GwyXYZ *xyz;
    GwyXYZ *xyz_result;
    guint k, n;
    gint degree = gwy_params_get_int(args->params, PARAM_ORDER);

    xyz = gwy_surface_get_data_const(surface);
    n = gwy_surface_get_npoints(surface);

    if (args->result)
        g_object_unref(args->result);
    args->result = result = gwy_surface_new_sized(n);
    xyz_result = gwy_surface_get_data(result);
    gwy_surface_copy_units(surface, result);

    for (k = 0; k < n; k++) {
        xyz_result[k].x = xyz[k].x;
        xyz_result[k].y = xyz[k].y;
        xyz_result[k].z = xyz[k].z - polynom(degree, args->fitcoeffs, k);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
