/*
 *  $Id: psdf_logphi.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/inttrans.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_WINDOW,
    PARAM_SIGMA,
};

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

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             psdflp              (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Two-dimensional FFT (Fast Fourier Transform) transformed to coordinates (log-frequency, angle)."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, psdf_logphi)

static gboolean
module_register(void)
{
    gwy_process_func_register("psdf_logphi",
                              (GwyProcessFunc)&psdflp,
                              N_("/_Statistics/_Log-Phi PSDF..."),
                              GWY_STOCK_PSDF_LOG_PHI,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute PSDF in Log-Phi coordinates"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma",  _("Gaussian _smoothing"), 0.0, 40.0, 0.0);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOW, "window", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_HANN);
    return paramdef;
}

static void
psdflp(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, "Log-phi PSDF");
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Log-Phi PSDF"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_WINDOW);
    gwy_param_table_append_slider(table, PARAM_SIGMA);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    enum { N = 4 };

    GwyDataField *field = args->field, *lpsdf = args->result, *reout, *imout;
    gint pxres, pyres, fxres, fyres, i, j;
    gdouble *ldata, *redata, *imdata, *cosphi, *sinphi;
    gdouble xreal, yreal, f0, f_max, b;
    gdouble sigma = gwy_params_get_double(args->params, PARAM_SIGMA);
    GwyWindowingType window = gwy_params_get_enum(args->params, PARAM_WINDOW);

    reout = gwy_data_field_new_alike(field, FALSE);
    imout = gwy_data_field_new_alike(field, FALSE);
    gwy_data_field_2dfft(field, NULL, reout, imout,
                         window, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_ROUND, TRUE, 1);

    pxres = gwy_data_field_get_xres(reout);
    pyres = gwy_data_field_get_yres(reout);
    redata = gwy_data_field_get_data(reout);
    imdata = gwy_data_field_get_data(imout);
    for (i = 0; i < pxres*pyres; i++)
        redata[i] = redata[i]*redata[i] + imdata[i]*imdata[i];
    gwy_data_field_2dfft_humanize(reout);
    gwy_data_field_filter_gaussian(reout, sigma);
    redata = gwy_data_field_get_data(reout);
    for (i = 0; i < pxres*pyres; i++)
        redata[i] = sqrt(redata[i]);

    fxres = pxres/2;
    fyres = pyres/2;
    gwy_data_field_resample(lpsdf, fxres, fyres, GWY_INTERPOLATION_NONE);
    ldata = gwy_data_field_get_data(lpsdf);

    xreal = gwy_data_field_get_xreal(field);
    yreal = gwy_data_field_get_yreal(field);
    f0 = 2.0/MIN(xreal, yreal);
    f_max = 0.5*MIN(pxres/xreal, pyres/yreal);
    if (f_max <= f0) {
        g_warning("Minimum frequency is not smaller than maximum frequency.");
    }
    b = log(f_max/f0)/fyres;

    /* Incorporate some prefactors to sinphi[] and cosphi[], knowing that
     * cosine is only ever used for x and sine for y frequencies. */
    cosphi = g_new(gdouble, (N+1)*fxres);
    sinphi = g_new(gdouble, (N+1)*fxres);
    for (j = 0; j < fxres; j++) {
        gdouble phi_from = 2.0*G_PI*j/fxres;
        gdouble phi_to = 2.0*G_PI*(j + 1.0)/fxres;
        gint pi;

        for (pi = 0; pi <= N; pi++) {
            gdouble phi = ((pi + 0.5)*phi_from + (N - 0.5 - pi)*phi_to)/N;
            cosphi[j*(N+1) + pi] = cos(phi)*xreal;
            sinphi[j*(N+1) + pi] = sin(phi)*yreal;
        }
    }

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(fxres,fyres,pxres,pyres,cosphi,sinphi,f0,b,reout,ldata) \
            private(i,j)
#endif
    for (i = 0; i < fyres; i++) {
        gdouble f_from = f0*exp(b*i);
        gdouble f_to = f0*exp(b*(i + 1.0));

        for (j = 0; j < fxres; j++) {
            const gdouble *cosphi_j = cosphi + j*(N+1);
            const gdouble *sinphi_j = sinphi + j*(N+1);
            guint n = 0;
            gdouble s = 0.0;
            gint fi, pi;

            for (fi = 0; fi <= N; fi++) {
                gdouble f = ((fi + 0.5)*f_from + (N - 0.5 - fi)*f_to)/N;
                for (pi = 0; pi <= N; pi++) {
                    gdouble x = f*cosphi_j[pi] + pxres/2.0;
                    gdouble y = f*sinphi_j[pi] + pyres/2.0;
                    gdouble p;

                    if (G_UNLIKELY(x < 0.5 || y < 0.5 || x > pxres - 1.5 || y > pyres - 1.5))
                        continue;

                    p = gwy_data_field_get_dval(reout, x, y, GWY_INTERPOLATION_SCHAUM);
                    s += p;
                    n++;
                }
            }

            ldata[i*fxres + j] = 2.0*G_PI/fxres * s/MAX(n, 1)*(f_to - f_from);
        }
    }

    g_object_unref(imout);
    g_object_unref(reout);

    gwy_data_field_set_xreal(lpsdf, 2.0*G_PI);
    gwy_data_field_set_xoffset(lpsdf, 0.0);
    gwy_data_field_set_yreal(lpsdf, log(f_max/f0));
    gwy_data_field_set_yoffset(lpsdf, log(f0));
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(lpsdf), NULL);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(lpsdf), NULL);
    gwy_data_field_normalize(lpsdf);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
