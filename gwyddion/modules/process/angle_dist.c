/*
 *  $Id: angle_dist.c 24549 2021-11-26 17:00:39Z yeti-dn $
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
#include <libgwymodule/gwymodule-process.h>
#include <libprocess/level.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIZE,
    PARAM_STEPS,
    PARAM_LOGSCALE,
    PARAM_FIT_PLANE,
    PARAM_KERNEL_SIZE,
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
static void             angle_dist          (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             compute_slopes      (GwyDataField *field,
                                             gint kernel_size,
                                             GwyDataField *xder,
                                             GwyDataField *yder);
static gdouble          compute_max_der2    (GwyDataField *xder,
                                             GwyDataField *yder);
static gboolean         count_angles        (GwyDataField *xderfield,
                                             GwyDataField *yderfield,
                                             gint size,
                                             gulong *count,
                                             gint steps);
static GwyDataField*    make_datafield      (GwyDataField *old,
                                             gint res,
                                             gulong *count,
                                             gdouble real,
                                             gboolean logscale);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates two-dimensional distribution of angles, that is projections of slopes to all directions."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, angle_dist)

static gboolean
module_register(void)
{
    gwy_process_func_register("angle_dist",
                              (GwyProcessFunc)&angle_dist,
                              N_("/_Statistics/An_gle Distribution..."),
                              GWY_STOCK_DISTRIBUTION_ANGLE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate two-dimensional angle distribution"));

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
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("Output size"), 1, 1024, 200);
    gwy_param_def_add_int(paramdef, PARAM_STEPS, "steps", _("Number of steps"), 1, 65536, 360);
    gwy_param_def_add_boolean(paramdef, PARAM_LOGSCALE, "logscale", _("_Logarithmic value scale"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_FIT_PLANE, "fit_plane", _("Use local plane _fitting"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_KERNEL_SIZE, "kernel_size", _("Plane size"), 2, 16, 5);
    return paramdef;
}

static void
angle_dist(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    gint oldid, newid;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     NULL);
    g_return_if_fail(args.field);
    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        if (!execute(&args, gwy_app_find_window_for_channel(data, oldid)))
            goto end;
    }

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Angle distribution"));
    gwy_app_channel_log_add_proc(data, oldid, newid);
    g_object_unref(args.result);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Angle Distribution"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_append_slider(table, PARAM_STEPS);
    gwy_param_table_append_checkbox(table, PARAM_LOGSCALE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_FIT_PLANE);
    gwy_param_table_append_slider(table, PARAM_KERNEL_SIZE);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_FIT_PLANE)
        gwy_param_table_set_sensitive(gui->table, PARAM_KERNEL_SIZE, gwy_params_get_boolean(params, PARAM_FIT_PLANE));
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gint nsteps = gwy_params_get_int(params, PARAM_STEPS);
    gint kernel_size = gwy_params_get_int(params, PARAM_KERNEL_SIZE);
    gboolean fit_plane = gwy_params_get_boolean(params, PARAM_FIT_PLANE);
    gboolean logscale = gwy_params_get_boolean(params, PARAM_LOGSCALE);
    GwyDataField *field = args->field, *xder, *yder;
    gint xres, yres, n;
    gulong *count;

    gwy_app_wait_start(wait_window, _("Computing angle distribution..."));

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    n = fit_plane ? kernel_size : 2;
    n = (xres - n)*(yres - n);
    xder = gwy_data_field_new_alike(field, FALSE);
    yder = gwy_data_field_new_alike(field, FALSE);
    compute_slopes(field, fit_plane ? kernel_size : 0, xder, yder);
    count = g_new0(gulong, size*size);
    if (count_angles(xder, yder, size, count, nsteps))
        args->result = make_datafield(field, size, count, 2.0*G_PI, logscale);

    g_free(count);
    g_object_unref(yder);
    g_object_unref(xder);

    gwy_app_wait_finish();

    return !!args->result;
}

static gdouble
compute_max_der2(GwyDataField *xder, GwyDataField *yder)
{
    gint i, n = gwy_data_field_get_xres(xder)*gwy_data_field_get_yres(xder);
    const gdouble *xd = gwy_data_field_get_data_const(xder);
    const gdouble *yd = gwy_data_field_get_data_const(yder);
    gdouble m = 0.0;

    for (i = 0; i < n; i++)
        m = fmax(m, xd[i]*xd[i] + yd[i]*yd[i]);

    return m;
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

static gboolean
count_angles(GwyDataField *xderfield, GwyDataField *yderfield,
             gint size, gulong *count,
             gint steps)
{
    const gdouble *xder, *yder;
    gint xider, yider, i, j, n;
    gdouble *ct, *st;
    gdouble d, phi, max;
    gboolean ok = TRUE;

    max = compute_max_der2(xderfield, yderfield);
    max = atan(sqrt(max));
    gwy_debug("max = %g", max);

    ct = g_new(gdouble, steps);
    st = g_new(gdouble, steps);
    for (j = 0; j < steps; j++) {
        gdouble theta = 2.0*G_PI*j/steps;

        ct[j] = cos(theta);
        st[j] = sin(theta);
    }

    xder = gwy_data_field_get_data_const(xderfield);
    yder = gwy_data_field_get_data_const(yderfield);
    n = gwy_data_field_get_xres(xderfield)*gwy_data_field_get_yres(xderfield);
    for (i = 0; i < n; i++) {
        gdouble xd = xder[i], yd = yder[i];

        d = atan(hypot(xd, yd));
        phi = atan2(yd, xd);
        for (j = 0; j < steps; j++) {
            gdouble v = d*cos(2.0*G_PI*j/steps - phi);

            xider = size*(v*ct[j]/(2.0*max) + 0.5);
            xider = CLAMP(xider, 0, size-1);
            yider = size*(v*st[j]/(2.0*max) + 0.5);
            yider = CLAMP(yider, 0, size-1);

            count[yider*size + xider]++;
        }
        if (!gwy_app_wait_set_fraction((gdouble)i/n)) {
            ok = FALSE;
            break;
        }
    }

    g_free(ct);
    g_free(st);

    return ok;
}

static GwyDataField*
make_datafield(G_GNUC_UNUSED GwyDataField *old,
               gint res, gulong *count,
               gdouble real, gboolean logscale)
{
    GwyDataField *field;
    GwySIUnit *unit;
    gdouble *d;
    gint i;

    field = gwy_data_field_new(res, res, real, real, FALSE);
    gwy_data_field_set_xoffset(field, -gwy_data_field_get_xreal(field)/2);
    gwy_data_field_set_yoffset(field, -gwy_data_field_get_yreal(field)/2);

    unit = gwy_si_unit_new(NULL);
    gwy_data_field_set_si_unit_z(field, unit);
    g_object_unref(unit);
    unit = gwy_si_unit_new(NULL);
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

    return field;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
