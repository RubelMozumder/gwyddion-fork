/*
 *  $Id: volume_kmedians.c 25520 2023-06-30 08:53:16Z yeti-dn $
 *  Copyright (C) 2003-2023 David Necas (Yeti), Petr Klapetek,
 *  Daniil Bratashov, Evgeniy Ryabov.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net,
 *  dn2010@gmail.com, k1u2r3ka@mail.ru.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libprocess/datafield.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_K,
    PARAM_MAX_ITERATIONS,
    PARAM_EPSILON,
    PARAM_LOG_EPSILON,
    PARAM_NORMALIZE,
};

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyDataField *cluster;
    GwyDataField *errormap;
    GwyDataField *intmap;
    GwyGraphModel *gmodel;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             volume_kmedians     (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         execute             (ModuleArgs *args,
                                             GwyContainer *container,
                                             gint id);
static GwyBrick*        normalize_brick     (GwyBrick *brick,
                                             GwyDataField *intfield);
static GwyGraphModel*   create_graph        (GwyBrick *brick,
                                             const gdouble *centers,
                                             gint k);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates K-medians clustering on volume data."),
    "Daniil Bratashov <dn2010@gmail.com> & Evgeniy Ryabov <k1u2r3ka@mail.ru>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov & Evgeniy Ryabov",
    "2014",
};

GWY_MODULE_QUERY2(module_info, volume_kmedians)

static gboolean
module_register(void)
{
    gwy_volume_func_register("kmedians",
                             (GwyVolumeFunc)&volume_kmedians,
                             N_("/_Statistics/_K-Medians Clustering..."),
                             GWY_STOCK_VOLUME_KMEDIANS,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Calculate K-medians clustering on volume data"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_int(paramdef, PARAM_K, "k", _("_Number of clusters"), 2, 100, 10);
    gwy_param_def_add_int(paramdef, PARAM_MAX_ITERATIONS, "max_iterations", _("_Max. iterations"), 1, 10000, 100);
    /* The first is backward-compatible true value saved to settings; the second is for GUI. */
    gwy_param_def_add_double(paramdef, PARAM_EPSILON, "epsilon", NULL, 1e-20, 0.1, 1e-12);
    gwy_param_def_add_double(paramdef, PARAM_LOG_EPSILON, NULL, _("Convergence _precision digits"), 1.0, 20.0, 12.0);
    gwy_param_def_add_boolean(paramdef, PARAM_NORMALIZE, "normalize", _("_Normalize"), FALSE);
    return paramdef;
}

static void
volume_kmedians(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gchar *description;
    gint id, newid;

    g_return_if_fail(run & RUN_MODES);
    g_return_if_fail(data);

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));

    args.params = gwy_params_new_from_settings(define_module_params());
    /* sanitise_params(), but do not bother for one line of code. */
    gwy_params_set_double(args.params, PARAM_LOG_EPSILON, -log10(gwy_params_get_double(args.params, PARAM_EPSILON)));
    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (!execute(&args, data, id))
        goto end;

    description = gwy_app_get_brick_title(data, id);

    newid = gwy_app_data_browser_add_data_field(args.cluster, data, TRUE);
    gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(newid),
                             g_strdup_printf(_("K-medians cluster of %s"), description));
    gwy_app_channel_log_add(data, -1, newid, "volume::kmedians", NULL);

    newid = gwy_app_data_browser_add_data_field(args.errormap, data, TRUE);
    gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(newid),
                             g_strdup_printf(_("K-medians error of %s"), description));
    gwy_app_channel_log_add(data, -1, newid, "volume::kmedians", NULL);

    if (args.intmap) {
        newid = gwy_app_data_browser_add_data_field(args.intmap, data, TRUE);
        gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(newid),
                                 g_strdup_printf(_("Pre-normalized intensity of %s"), description));
        gwy_app_channel_log_add(data, -1, newid, "volume::kmedians", NULL);
    }
    gwy_app_data_browser_add_graph_model(args.gmodel, data, TRUE);

    g_free(description);

end:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.cluster);
    GWY_OBJECT_UNREF(args.errormap);
    GWY_OBJECT_UNREF(args.intmap);
    GWY_OBJECT_UNREF(args.gmodel);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("K-medians"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_K);
    gwy_param_table_append_slider(table, PARAM_LOG_EPSILON);
    gwy_param_table_slider_set_mapping(table, PARAM_LOG_EPSILON, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_MAX_ITERATIONS);
    gwy_param_table_slider_set_mapping(table, PARAM_MAX_ITERATIONS, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_checkbox(table, PARAM_NORMALIZE);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_LOG_EPSILON)
        gwy_params_set_double(params, PARAM_EPSILON, pow10(-gwy_params_get_double(params, PARAM_LOG_EPSILON)));
}

/* XXX: Duplicate with volume_kmeans.c */
static GwyBrick*
normalize_brick(GwyBrick *brick, GwyDataField *intfield)
{
    GwyBrick *result;
    gdouble wmin, dataval, dataval2, integral;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint i, j, l, k;
    gint len = 25;   /* FIXME: Kind of arbitrary. */
    const gdouble *olddata;
    gdouble *newdata, *intdata;

    result = gwy_brick_new_alike(brick, TRUE);
    wmin = gwy_brick_get_min(brick);
    olddata = gwy_brick_get_data_const(brick);
    newdata = gwy_brick_get_data(result);
    intdata = gwy_data_field_get_data(intfield);

    for (i = 0; i < xres; i++) {
        for (j = 0; j < yres; j++) {
            integral = 0;
            for (l = 0; l < zres; l++) {
                dataval = olddata[l*xres*yres + j*xres + i];
                wmin = dataval;
                for (k = -len; k < len; k++) {
                    if (l + k < 0) {
                        k = -l;
                        continue;
                    }
                    if (l + k >= zres)
                        break;
                    dataval2 = olddata[(l + k)*xres*yres + j*xres + i];
                    if (dataval2 < wmin)
                        wmin = dataval2;
                }
                integral += dataval - wmin;
            }
            for (l = 0; l < zres; l++) {
                dataval = olddata[l*xres*yres + j*xres + i];
                wmin = dataval;
                for (k = -len; k < len; k++) {
                    if (l + k < 0) {
                        k = -l;
                        continue;
                    }
                    if (l + k >= zres)
                        break;
                    dataval2 = olddata[(l + k)* xres*yres + j*xres + i];
                    if (dataval2 < wmin)
                        wmin = dataval2;
                }
                if (integral != 0.0) {
                    newdata[l*xres*yres + j*xres + i] = (dataval - wmin)*zres/integral;
                }
            }
            intdata[j*xres + i] = integral / zres;
        }
    }

    return result;
}

static gboolean
execute(ModuleArgs *args, GwyContainer *container, gint id)
{
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick, *normalized = NULL;
    gdouble epsilon = gwy_params_get_double(params, PARAM_EPSILON);
    gboolean normalize = gwy_params_get_boolean(params, PARAM_NORMALIZE);
    gint k = gwy_params_get_int(params, PARAM_K);
    gint max_iterations = gwy_params_get_int(params, PARAM_MAX_ITERATIONS);
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gdouble xreal = gwy_brick_get_xreal(brick), yreal = gwy_brick_get_yreal(brick);
    gdouble xoffset = gwy_brick_get_xoffset(brick);
    gdouble yoffset = gwy_brick_get_yoffset(brick);
    GwyDataField *dfield = NULL, *errormap = NULL, *intmap = NULL;
    GRand *rand;
    const gdouble *data;
    gdouble *centers, *oldcenters, *plane, *data1, *errordata;
    gdouble min, dist;
    gint i, j, l, c;
    gint *npix;
    gint iterations = 0;
    gboolean converged = FALSE, cancelled = FALSE, ok = FALSE;

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, TRUE);
    gwy_data_field_set_xoffset(dfield, xoffset);
    gwy_data_field_set_yoffset(dfield, yoffset);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), gwy_brick_get_si_unit_x(brick));

    intmap = gwy_data_field_new_alike(dfield, TRUE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(intmap), gwy_brick_get_si_unit_w(brick));

    gwy_app_wait_start(gwy_app_find_window_for_volume(container, id), _("Initializing..."));

    if (normalize) {
        normalized = normalize_brick(brick, intmap);
        data = gwy_brick_get_data_const(normalized);
    }
    else {
        data = gwy_brick_get_data_const(brick);
    }

    centers = g_new(gdouble, zres*k);
    oldcenters = g_new(gdouble, zres*k);
    plane = g_new(gdouble, xres*yres*k);
    npix = g_new(gint, k);
    data1 = gwy_data_field_get_data(dfield);

    /* FIXME: This makes the output non-deterministic even when it actually produces the same clusters again as they
     * can be reordered. Which is confusing. */
    rand = g_rand_new();
    for (c = 0; c < k; c++) {
        i = g_rand_int_range(rand, 0, xres);
        j = g_rand_int_range(rand, 0, yres);
        for (l = 0; l < zres; l++)
            centers[c*zres + l] = data[l*xres*yres + j*xres + i];
    }
    g_rand_free(rand);

    if (!gwy_app_wait_set_message(_("K-medians iteration...")))
        cancelled = TRUE;

    while (!converged && !cancelled) {
        if (!gwy_app_wait_set_fraction((gdouble)iterations/max_iterations)) {
            cancelled = TRUE;
            break;
        }

        /* pixels belong to cluster with min distance */
        for (j = 0; j < yres; j++)
            for (i = 0; i < xres; i++) {
                data1[j*xres + i] = 0;
                min = G_MAXDOUBLE;
                for (c = 0; c < k; c++) {
                    dist = 0;
                    for (l = 0; l < zres; l++) {
                        gdouble d = data[l*xres*yres + j*xres + i] - centers[c*zres + l];
                        oldcenters[c*zres + l] = centers[c*zres + l];
                        dist += d*d;
                    }
                    if (dist < min) {
                        min = dist;
                        data1[j*xres + i] = c;
                    }
                }
            }

        /* We're calculating median per one coordinate of all pixels that belongs to same cluster and use it as this
         * coordinate position for cluster center */
        for (l = 0; l < zres; l++) {
            gwy_clear(npix, k);
            for (j = 0; j < yres; j++) {
                for (i = 0; i < xres; i++) {
                    c = (gint)(data1[j*xres + i]);
                    npix[c]++;
                    plane[c*xres*yres + npix[c] - 1] = data[l*xres*yres + j*xres + i];
                }
            }
            for (c = 0; c < k; c++) {
                gwy_math_sort(npix[c], plane + c*xres*yres);
                centers[c*zres + l] = plane[c*xres*yres + npix[c]/2];
            }
        }

        converged = TRUE;
        for (c = 0; c < k; c++) {
            for (l = 0; l < zres; l++)
                if (fabs(oldcenters[c*zres + l] - centers[c*zres + l]) > epsilon) {
                    converged = FALSE;
                    break;
                }
        }
        if (iterations == max_iterations) {
            converged = TRUE;
        }
        iterations++;
    }

    gwy_app_wait_finish();
    if (cancelled)
        goto fail;

    errormap = gwy_data_field_new_alike(dfield, TRUE);
    if (!normalize)
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(errormap), gwy_brick_get_si_unit_w(brick));
    errordata = gwy_data_field_get_data(errormap);

    for (i = 0; i < xres; i++) {
        for (j = 0; j < yres; j++) {
            dist = 0.0;
            c = (gint)(data1[j*xres + i]);
            for (l = 0; l < zres; l++) {
                gdouble d = data[l*xres*yres + j*xres + i] - centers[c*zres + l];
                dist += d*d;
            }
            errordata[j*xres + i] = sqrt(dist);
        }
    }

    gwy_data_field_add(dfield, 1.0);
    args->cluster = g_object_ref(dfield);
    args->errormap = g_object_ref(errormap);
    if (normalize)
        args->intmap = g_object_ref(intmap);
    args->gmodel = create_graph(brick, centers, k);
    ok = TRUE;

fail:
    GWY_OBJECT_UNREF(errormap);
    GWY_OBJECT_UNREF(intmap);
    GWY_OBJECT_UNREF(dfield);
    GWY_OBJECT_UNREF(normalized);
    g_free(npix);
    g_free(oldcenters);
    g_free(centers);

    return ok;
}

/* Apart from the curve titles, this is identical to volume_kmeans.c */
static GwyGraphModel*
create_graph(GwyBrick *brick, const gdouble *centers, gint k)
{
    gint zres = gwy_brick_get_zres(brick);
    gdouble zreal = gwy_brick_get_zreal(brick);
    GwyGraphModel *gmodel = gwy_graph_model_new();
    GwyDataLine *calibration = gwy_brick_get_zcalibration(brick);
    gdouble zoffset = gwy_brick_get_zoffset(brick);
    GwyGraphCurveModel *gcmodel;
    gdouble *xdata, *ydata;
    GwySIUnit *siunit;
    gint i, c;

    xdata = g_new(gdouble, zres);
    ydata = g_new(gdouble, zres);
    if (calibration) {
        gwy_assign(xdata, gwy_data_line_get_data(calibration), zres);
        siunit = gwy_data_line_get_si_unit_y(calibration);
    }
    else {
        for (i = 0; i < zres; i++)
            xdata[i] = zreal*i/zres + zoffset;
        siunit = gwy_brick_get_si_unit_z(brick);
    }

    for (c = 0; c < k; c++) {
        gwy_assign(ydata, centers + c*zres, zres);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, zres);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description",
                     g_strdup_printf(_("K-medians center %d"), c + 1),
                     "color", gwy_graph_get_preset_color(c),
                     NULL);
        gwy_graph_curve_model_enforce_order(gcmodel);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    g_free(xdata);
    g_free(ydata);
    g_object_set(gmodel,
                 "si-unit-x", siunit,
                 "si-unit-y", gwy_brick_get_si_unit_w(brick),
                 "axis-label-bottom", "x",
                 "axis-label-left", "y",
                 NULL);

    return gmodel;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
