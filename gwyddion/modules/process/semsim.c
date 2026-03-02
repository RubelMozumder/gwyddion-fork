/*
 *  $Id: semsim.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti).
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
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/arithmetic.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    ERF_TABLE_SIZE = 16384
};

typedef enum {
    SEMSIM_METHOD_MONTECARLO = 0,
    SEMSIM_METHOD_INTEGRATION = 1,
} SEMSimMethod;

enum {
    PARAM_METHOD,
    PARAM_QUALITY,
    PARAM_SIGMA,
};

typedef struct {
    gdouble w;
    gint k;
} WeightItem;

typedef struct {
    gdouble dx;
    gdouble dy;
    gdouble dz;
    gdouble *erftable;
    gint extv;
    gint exth;
    gint extxres;
    gint extyres;
    GwyDataField *extfield;
} SEMSimCommon;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             semsim               (GwyContainer *data,
                                              GwyRunType runtype);
static gboolean         execute              (ModuleArgs *args,
                                              GtkWindow *wait_window);
static GwyDialogOutcome run_gui              (ModuleArgs *args);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static gboolean         semsim_do_integration(SEMSimCommon *common,
                                              ModuleArgs *args);
static gboolean         semsim_do_montecarlo (SEMSimCommon *common,
                                              ModuleArgs *args);
static gdouble*         create_erf_table     (GwyDataField *field,
                                              gdouble sigma,
                                              gdouble *zstep);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simple SEM image simulation from topography."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, semsim)

static gboolean
module_register(void)
{
    gwy_process_func_register("semsim",
                              (GwyProcessFunc)&semsim,
                              N_("/_Presentation/_SEM Image..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Simple SEM simulation from topography"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Integration"), SEMSIM_METHOD_INTEGRATION },
        { N_("Monte Carlo"), SEMSIM_METHOD_MONTECARLO  },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Method"),
                              methods, G_N_ELEMENTS(methods), SEMSIM_METHOD_MONTECARLO);
    gwy_param_def_add_double(paramdef, PARAM_QUALITY, "quality", _("_Quality"), 1.0, 7.0, 3.0);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Integration radius"), 0.5, 200.0, 10.0);
    return paramdef;
}

static void
semsim(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark squark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);
    g_return_if_fail(args.field && squark);

    if (!gwy_require_image_same_units(args.field, data, id, _("SEM Image")))
        return;

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (execute(&args, gwy_app_find_window_for_channel(data, id))) {
        gwy_app_undo_qcheckpointv(data, 1, &squark);
        gwy_container_set_object(data, squark, args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("SEM Image"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_add_alt(table, PARAM_SIGMA);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIGMA, args->field);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_METHOD);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_QUALITY);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_METHOD) {
        SEMSimMethod method = gwy_params_get_enum(params, PARAM_METHOD);
        gwy_param_table_set_sensitive(table, PARAM_QUALITY, method == SEMSIM_METHOD_MONTECARLO);
    }
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    GwyDataField *field = args->field;
    SEMSimMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    SEMSimCommon common;
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gdouble sigma;
    gboolean ok;

    gwy_app_wait_start(wait_window, _("SEM image simulation..."));

    common.dx = gwy_data_field_get_dx(field);
    common.dy = gwy_data_field_get_dy(field);
    sigma = gwy_params_get_double(params, PARAM_SIGMA) * common.dx;
    common.exth = (gint)ceil(5.5*sigma/common.dx);
    common.extv = (gint)ceil(5.5*sigma/common.dy);
    common.extxres = xres + 2*common.exth;
    common.extyres = yres + 2*common.extv;
    common.extfield = gwy_data_field_extend(args->field,
                                            common.exth, common.exth, common.extv, common.extv,
                                            GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);
    common.erftable = create_erf_table(field, sigma, &common.dz);

    if (method == SEMSIM_METHOD_INTEGRATION)
        ok = semsim_do_integration(&common, args);
    else
        ok = semsim_do_montecarlo(&common, args);

    gwy_app_wait_finish();

    if (ok)
        gwy_data_field_normalize(args->result);

    g_free(common.erftable);
    g_object_unref(common.extfield);

    return ok;
}

static gboolean
semsim_do_integration(SEMSimCommon *common, ModuleArgs *args)
{
    GwyDataField *show = args->result;
    gint xres = gwy_data_field_get_xres(show), yres = gwy_data_field_get_yres(show);
    gint i, j, k;
    gdouble dx = common->dx, dy = common->dy, dz = common->dz;
    gdouble sigma_r2 = G_SQRT2*gwy_params_get_double(args->params, PARAM_SIGMA) * dx;
    const gdouble *d = gwy_data_field_get_data_const(common->extfield);
    gdouble *s = gwy_data_field_get_data(show);
    gdouble *erftable = common->erftable;
    WeightItem *weight_table;
    gint exth = common->exth, extv = common->extv, extxres = common->extxres;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    gint nw;

    weight_table = g_new(WeightItem, (2*extv + 1)*(2*exth + 1));
    nw = 0;
    for (i = -extv; i <= extv; i++) {
        gdouble x = i*dy/sigma_r2;
        for (j = -exth; j <= exth; j++) {
            gdouble y = j*dx/sigma_r2;
            gdouble w = exp(-(x*x + y*y));
            if (w >= 1e-6) {
                weight_table[nw].w = w;
                weight_table[nw].k = (i + extv)*extxres + (j + exth);
                nw++;
            }
        }
    }

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            private(i,j,k) \
            shared(d,s,erftable,weight_table,nw,extv,exth,extxres,xres,yres,dz,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < xres; j++) {
                gdouble sum = 0.0, z0 = d[(i + extv)*extxres + (j + exth)];
                for (k = 0; k < nw; k++) {
                    gdouble z = d[i*extxres + j + weight_table[k].k];
                    gdouble w = weight_table[k].w;
                    if (z >= z0)
                        sum -= w*erftable[GWY_ROUND((z - z0)/dz)];
                    else
                        sum += w*erftable[GWY_ROUND((z0 - z)/dz)];
                }
                s[i*xres + j] = sum;
            }
            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, i, ifrom, ito, pcancelled))
                break;
        }
    }

    g_free(weight_table);
    return !cancelled;
}

static gboolean
semsim_do_montecarlo(SEMSimCommon *common, ModuleArgs *args)
{
    GwyDataField *show = args->result;
    gint xres = gwy_data_field_get_xres(show), yres = gwy_data_field_get_yres(show);
    gdouble dx = common->dx, dy = common->dy, dz = common->dz;
    gdouble quality = gwy_params_get_double(args->params, PARAM_QUALITY);
    gdouble sigma_r2 = G_SQRT2*gwy_params_get_double(args->params, PARAM_SIGMA) * dx;
    const gdouble *d = gwy_data_field_get_data_const(common->extfield);
    gdouble *s = gwy_data_field_get_data(show);
    gdouble *erftable = common->erftable;
    gint exth = common->exth, extv = common->extv;
    gint extxres = common->extxres, extyres = common->extyres;
    gdouble noise_limit = pow10(-quality);
    gint miniter = (gint)ceil(10*quality);
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,s,erftable,extv,exth,extxres,extyres,xres,yres,dx,dy,dz,sigma_r2,miniter,noise_limit,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        GRand *rng = g_rand_new();
        gint i, j, k;

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < xres; j++) {
                gdouble sum = 0.0, sum2 = 0.0, ss;
                gdouble z0 = d[(i + extv)*extxres + (j + exth)];

                k = 1;
                while (TRUE) {
                    gdouble r = sigma_r2*sqrt(-log(1.0 - g_rand_double(rng)));
                    gdouble phi = 2.0*G_PI*g_rand_double(rng);
                    gdouble x = r*cos(phi), y = r*sin(phi), z;
                    gint ii, jj;

                    jj = j + exth + GWY_ROUND(x/dx);
                    if (jj < 0 || jj > extxres-1)
                        continue;

                    ii = i + extv + GWY_ROUND(y/dy);
                    if (ii < 0 || ii > extyres-1)
                        continue;

                    z = d[ii*extxres + jj];
                    if (z >= z0)
                        ss = -erftable[GWY_ROUND((z - z0)/dz)];
                    else
                        ss = erftable[GWY_ROUND((z0 - z)/dz)];

                    sum += ss;
                    sum2 += ss*ss;
                    if (k - miniter >= 0 && (k - miniter) % 5 == 0) {
                        gdouble mean = sum/k;
                        gdouble disp = sum2/k - mean*mean;

                        mean = 0.5*(1.0 + mean);
                        disp /= 2.0*k;
                        if (disp < noise_limit*mean*(1.0 - mean))
                            break;
                    }
                    k++;
                }

                s[i*xres + j] = sum/k;
            }
            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, i, ifrom, ito, pcancelled))
                break;
        }

        g_rand_free(rng);
    }

    return !cancelled;
}

static gdouble*
create_erf_table(GwyDataField *field, gdouble sigma, gdouble *zstep)
{
    gdouble min, max, dz;
    gdouble *table;
    gint i;

    gwy_data_field_get_min_max(field, &min, &max);
    dz = (max - min)/(ERF_TABLE_SIZE - 1);
    table = g_new(gdouble, ERF_TABLE_SIZE + 1);
    for (i = 0; i <= ERF_TABLE_SIZE; i++)
        table[i] = erf(i*dz/(G_SQRT2*sigma));

    *zstep = dz;
    return table;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
