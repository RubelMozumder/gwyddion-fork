/*
 *  $Id: volume_flattenbase.c 26354 2024-05-21 08:22:32Z yeti-dn $
 *  Copyright (C) 2018-2024 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwynlfitpreset.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/level.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    MAX_DEGREE = 7,
};

enum {
    PARAM_MAX_DEGREE,
    PARAM_INVERTED,
    PARAM_DO_EXTRACT,
    PARAM_MEAN_BG,
};

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register          (void);
static GwyParamDef*     define_module_params     (void);
static void             volume_flattenbase       (GwyContainer *data,
                                                  GwyRunType run);
static GwyDialogOutcome run_gui                  (ModuleArgs *args);
static void             param_changed            (ModuleGUI *gui,
                                                  gint id);
static gboolean         execute                  (ModuleArgs *args,
                                                  GtkWindow *wait_window);
static GwyBrick*        extract_volume_background(ModuleArgs *args);
static GwyDataField*    extract_mean_background  (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Flattens base on all XY planes"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_flattenbase)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_flattenbase",
                             (GwyVolumeFunc)&volume_flattenbase,
                             N_("/_Correct Data/_XY Flatten Base..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Flatten base on all XY planes"));

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
    gwy_param_def_add_int(paramdef, PARAM_MAX_DEGREE, "max_degree", _("_Polynomial degree"), 2, MAX_DEGREE, 5);
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("_Invert height"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_MEAN_BG, "mean_bg", _("_Mean background"), FALSE);
    return paramdef;
}

static void
volume_flattenbase(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyBrick *bg;
    GwyDataField *bgfield;
    const guchar *gradient;
    gint id, newid;

    g_return_if_fail(run & RUN_MODES);

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));
    args.params = gwy_params_new_from_settings(define_module_params());

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    /* FIXME: If we add a full preview, we need to check outcome == GWY_DIALOG_HAVE_RESULT here. */

    args.result = gwy_brick_duplicate(args.brick);
    if (execute(&args, gwy_app_find_window_for_volume(data, id))) {
        newid = gwy_app_data_browser_add_brick(args.result, NULL, data, TRUE);
        gwy_app_set_brick_title(data, newid, _("Base flattened"));
        gwy_app_sync_volume_items(data, data, id, newid, FALSE,
                                  GWY_DATA_ITEM_GRADIENT,
                                  0);
        gwy_app_volume_log_add_volume(data, id, newid);

        if (gwy_params_get_boolean(args.params, PARAM_DO_EXTRACT)) {
            if (gwy_params_get_boolean(args.params, PARAM_MEAN_BG)) {
                bgfield = extract_mean_background(&args);
                newid = gwy_app_data_browser_add_data_field(bgfield, data, TRUE);
                if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
                    gwy_container_set_const_string(data, gwy_app_get_data_palette_key_for_id(newid), gradient);
                gwy_app_set_data_field_title(data, newid, _("Background"));
                gwy_app_volume_log_add_volume(data, -1, newid);
                g_object_unref(bgfield);
            }
            else {
                bg = extract_volume_background(&args);
                newid = gwy_app_data_browser_add_brick(bg, NULL, data, TRUE);
                gwy_app_set_brick_title(data, newid, _("Background"));
                gwy_app_sync_volume_items(data, data, id, newid, FALSE,
                                          GWY_DATA_ITEM_GRADIENT,
                                          0);
                gwy_app_volume_log_add_volume(data, id, newid);
                g_object_unref(bg);
            }
        }
    }

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui; /* We actually don't need the struct until we have a more complex GUI. */

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Summarize Volume Profiles"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_MAX_DEGREE);
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);
    gwy_param_table_append_checkbox(table, PARAM_MEAN_BG);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 4);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_DO_EXTRACT)
        gwy_param_table_set_sensitive(table, PARAM_MEAN_BG, gwy_params_get_boolean(args->params, PARAM_DO_EXTRACT));
}

static gboolean
find_base_peak(GwyDataField *dfield,
               GwyNLFitter *fitter, GwyDataLine *dh,
               GArray *xydata,
               gdouble *mean, gdouble *rms)
{
    gdouble *d, *xdata, *ydata;
    gdouble real, off, dhmax = -G_MAXDOUBLE;
    gdouble param[4];
    gint i, res, from, to, ndata, m = 0;
    gboolean retval;

    gwy_data_field_dh(dfield, dh, 0);
    d = gwy_data_line_get_data(dh);
    res = gwy_data_line_get_res(dh);
    real = gwy_data_line_get_real(dh);
    off = gwy_data_line_get_offset(dh);
    for (i = 0; i < res; i++) {
        if (d[i] > dhmax) {
            dhmax = d[i];
            m = i;
        }
    }

    for (from = m; from > 0; from--) {
        if (d[from] < 0.3*dhmax)
            break;
    }
    for (to = m; to < res-1; to++) {
        if (d[to] < 0.3*dhmax)
            break;
    }

    ndata = to+1 - from;
    while (ndata < 7) {
        if (from)
            from--;
        if (to < res-1)
            to++;
        ndata = to+1 - from;
    }

    g_array_set_size(xydata, 2*ndata);
    xdata = &g_array_index(xydata, gdouble, 0);
    ydata = &g_array_index(xydata, gdouble, ndata);
    for (i = 0; i < ndata; i++) {
        xdata[i] = (i + from + 0.5)*real/res + off;
        ydata[i] = d[i + from];
    }

    /* x0, y0, a, b */
    param[0] = (m + 0.5)*real/res + off;
    param[1] = 0.0;
    param[2] = dhmax;
    param[3] = 0.3*ndata * real/res;

    gwy_math_nlfit_fit(fitter, ndata, xdata, ydata, G_N_ELEMENTS(param), param, NULL);
    retval = !!gwy_math_nlfit_get_covar(fitter);
    *mean = param[0];
    *rms = param[3]/G_SQRT2;

    return retval;
}

static void
grow_mask(GwyDataField *mask, gint amount)
{
    gint xres = mask->xres, yres = mask->yres;
    gdouble *buffer = g_new(gdouble, xres);
    gdouble *prow = g_new(gdouble, xres);
    gdouble *data = gwy_data_field_get_data(mask);
    gint iter, rowstride, i, j;
    gdouble min, max, q1, q2;

    gwy_debug("grow amount %d", amount);

    if (amount > 1)
        max = gwy_data_field_get_max(mask);
    else
        max = 1.0;

    for (iter = 0; iter < amount; iter++) {
        rowstride = xres;
        min = G_MAXDOUBLE;
        for (j = 0; j < xres; j++)
            prow[j] = -G_MAXDOUBLE;
        gwy_assign(buffer, data, xres);
        for (i = 0; i < yres; i++) {
            gdouble *row = data + i*xres;

            if (i == yres-1)
                rowstride = 0;

            j = 0;
            q2 = MAX(buffer[j], buffer[j+1]);
            q1 = MAX(prow[j], row[j+rowstride]);
            row[j] = MAX(q1, q2);
            min = MIN(min, row[j]);
            for (j = 1; j < xres-1; j++) {
                q1 = MAX(prow[j], buffer[j-1]);
                q2 = MAX(buffer[j], buffer[j+1]);
                q2 = MAX(q2, row[j+rowstride]);
                row[j] = MAX(q1, q2);
                min = MIN(min, row[j]);
            }
            j = xres-1;
            q2 = MAX(buffer[j-1], buffer[j]);
            q1 = MAX(prow[j], row[j+rowstride]);
            row[j] = MAX(q1, q2);
            min = MIN(min, row[j]);

            GWY_SWAP(gdouble*, prow, buffer);
            if (i < yres-1)
                gwy_assign(buffer, data + (i+1)*xres, xres);
        }
        if (min == max)
            break;
    }

    g_free(buffer);
    g_free(prow);
}

static gboolean
polylevel_with_mask(GwyDataField *dfield, GwyDataField *mask,
                    gint max_degree,
                    gdouble mean, gdouble rms)
{
    gint nterms = (max_degree + 1)*(max_degree + 2)/2;
    gint *term_powers = g_new(gint, 2*nterms);
    gint i, j, k;
    gdouble min, max, threshold, threshval;
    gdouble *coeffs;

    gwy_data_field_get_min_max(dfield, &min, &max);
    if (max <= min)
        return FALSE;

    threshold = mean + 3*rms;
    threshval = 100.0*(threshold - min)/(max - min);
    gwy_debug("min %g, max %g, threshold %g => threshval %g",
              min, max, threshold, threshval);
    gwy_data_field_grains_mark_height(dfield, mask, threshval, FALSE);
    grow_mask(mask, 1 + max_degree/2);

    k = 0;
    for (i = 0; i <= max_degree; i++) {
        for (j = 0; j <= max_degree - i; j++) {
            term_powers[k++] = i;
            term_powers[k++] = j;
        }
    }

    coeffs = gwy_data_field_fit_poly(dfield, mask, nterms, term_powers, TRUE, NULL);
    gwy_data_field_subtract_poly(dfield, nterms, term_powers, coeffs);
    g_free(coeffs);

    return TRUE;
}

static void
gwy_data_field_flatten_base(GwyDataField *dfield, gint max_degree,
                            GwyNLFitter *fitter, GwyDataLine *dh, GArray *xydata)
{
    GwyDataField *mfield = NULL;
    gdouble mean, sigma, min, a, bx, by;
    gboolean found_peak;
    gint i;

    found_peak = find_base_peak(dfield, fitter, dh, xydata, &mean, &sigma);
    gwy_debug("initial peak: %s (mean=%g, rms=%g)",
              found_peak ? "OK" : "NOT FOUND", mean, sigma);

    for (i = 0; i < 5; i++) {
        if (!gwy_data_field_fit_facet_plane(dfield, NULL, GWY_MASK_IGNORE, &a, &bx, &by))
            break;

        gwy_data_field_plane_level(dfield, a, bx, by);
        found_peak = find_base_peak(dfield, fitter, dh, xydata, &mean, &sigma);
        gwy_debug("facet[%d] peak: %s (mean=%g, rms=%g)",
                  i, found_peak ? "OK" : "NOT FOUND", mean, sigma);
        if (!found_peak)
            break;
    }

    mfield = gwy_data_field_new_alike(dfield, FALSE);
    for (i = 2; i <= max_degree; i++) {
        polylevel_with_mask(dfield, mfield, i, mean, sigma);
        found_peak = find_base_peak(dfield, fitter, dh, xydata, &mean, &sigma);
        gwy_debug("poly[%d] peak: %s (mean=%g, rms=%g)",
                  i, found_peak ? "OK" : "NOT FOUND", mean, sigma);
        if (!found_peak)
            break;
    }

    if (found_peak)
        gwy_data_field_add(dfield, -mean);

    if ((min = gwy_data_field_get_min(dfield)) > 0.0)
        gwy_data_field_add(dfield, -min);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyBrick *brick = args->result;
    gboolean inverted = gwy_params_get_boolean(args->params, PARAM_INVERTED);
    gint max_degree = gwy_params_get_int(args->params, PARAM_MAX_DEGREE);
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint zres = gwy_brick_get_zres(brick);
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    GwyNLFitPreset *gaussian = gwy_inventory_get_item(gwy_nlfit_presets(), "Gaussian");

    gwy_app_wait_start(wait_window, _("Flattening bases..."));

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,xres,yres,zres,inverted,max_degree,gaussian,pcancelled)
#endif
    {
        GwyNLFitter *fitter;
        GwyDataField *dfield = gwy_data_field_new(xres, yres, xres, yres, FALSE);
        GwyDataLine *dh = gwy_data_line_new(1, 1.0, FALSE);
        GArray *xydata = g_array_new(FALSE, FALSE, sizeof(gdouble));
        gint kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);
        gint k;

        fitter = gwy_nlfit_preset_create_fitter(gaussian);

        for (k = kfrom; k < kto; k++) {
            gwy_brick_extract_xy_plane(brick, dfield, k);
            if (inverted)
                gwy_data_field_multiply(dfield, -1.0);
            gwy_data_field_flatten_base(dfield, max_degree, fitter, dh, xydata);
            if (inverted)
                gwy_data_field_multiply(dfield, -1.0);
            gwy_brick_set_xy_plane(brick, dfield, k);

            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, k, kfrom, kto, pcancelled))
                break;
        }

        g_object_unref(dfield);
        g_object_unref(dh);
        g_array_free(xydata, TRUE);
        gwy_math_nlfit_free(fitter);
    }

    gwy_app_wait_finish();

    return !cancelled;
}

static GwyBrick*
extract_volume_background(ModuleArgs *args)
{
    GwyBrick *diff = gwy_brick_duplicate(args->brick);
    gdouble *d = gwy_brick_get_data(diff);
    const gdouble *r = gwy_brick_get_data_const(args->result);
    gint xres = gwy_brick_get_xres(diff);
    gint yres = gwy_brick_get_yres(diff);
    gint zres = gwy_brick_get_zres(diff);
    gint n = xres*yres*zres, i;

    for (i = 0; i < n; i++)
        d[i] -= r[i];

    return diff;
}

static GwyDataField*
extract_mean_background(ModuleArgs *args)
{
    GwyDataField *diff = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gdouble *d;
    const gdouble *rplane, *r = gwy_brick_get_data_const(args->result);
    const gdouble *bplane, *b = gwy_brick_get_data_const(args->brick);
    gint xres = gwy_brick_get_xres(args->brick);
    gint yres = gwy_brick_get_yres(args->brick);
    gint zres = gwy_brick_get_zres(args->brick);
    gint n = xres*yres, i, k;

    gwy_brick_extract_xy_plane(args->brick, diff, 0);
    gwy_data_field_clear(diff);
    d = gwy_data_field_get_data(diff);

    for (k = 0; k < zres; k++) {
        rplane = r + n*k;
        bplane = b + n*k;
        for (i = 0; i < n; i++)
            d[i] += bplane[i] - rplane[i];
    }
    gwy_data_field_multiply(diff, 1.0/zres);

    return diff;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
