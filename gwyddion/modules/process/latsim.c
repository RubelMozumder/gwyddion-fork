/*
 *  $Id: latsim.c 24451 2021-11-01 15:34:25Z yeti-dn $
 *  Copyright (C) 2012-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_MU,
    PARAM_ADHESION,
    PARAM_LOAD,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *forward;
    GwyDataField *reverse;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             latsim              (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Lateral force simulator"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2012",
};

GWY_MODULE_QUERY2(module_info, latsim)

static gboolean
module_register(void)
{
    gwy_process_func_register("latsim",
                              (GwyProcessFunc)&latsim,
                              N_("/SPM M_odes/_Force and Indentation/_Lateral Force..."),
                              GWY_STOCK_TIP_LATERAL_FORCE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Simulate topography artifacts in lateral force channels"));

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
    gwy_param_def_add_double(paramdef, PARAM_MU, "mu", _("_Friction coefficient"), 0.01, 20.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ADHESION, "adhesion", _("_Adhesion force"), 0.0, 1e-6, 1e-9);
    gwy_param_def_add_double(paramdef, PARAM_LOAD, "load", _("_Normal force"), 0.0, 1e-6, 1e-9);
    return paramdef;
}

static void
latsim(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(args.field);

    args.forward = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.forward), "N");
    args.reverse = gwy_data_field_new_alike(args.forward, TRUE);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        if (!execute(&args))
            goto end;
    }

    newid = gwy_app_data_browser_add_data_field(args.forward, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Fw lateral force "));
    gwy_app_channel_log_add_proc(data, oldid, newid);

    newid = gwy_app_data_browser_add_data_field(args.reverse, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Rev lateral force"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

end:
    GWY_OBJECT_UNREF(args.forward);
    GWY_OBJECT_UNREF(args.reverse);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Lateral Force Simulation"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_MU);
    gwy_param_table_slider_set_mapping(table, PARAM_MU, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_LOAD);
    gwy_param_table_slider_set_factor(table, PARAM_LOAD, 1e9);
    gwy_param_table_set_unitstr(table, PARAM_LOAD, "nN");
    gwy_param_table_append_slider(table, PARAM_ADHESION);
    gwy_param_table_slider_set_factor(table, PARAM_ADHESION, 1e9);
    gwy_param_table_set_unitstr(table, PARAM_ADHESION, "nN");
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, TRUE, 0);

    return gwy_dialog_run(dialog);
}

static gboolean
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *forward = args->forward, *reverse = args->reverse;
    gdouble mu = gwy_params_get_double(args->params, PARAM_MU);
    gdouble load = gwy_params_get_double(args->params, PARAM_LOAD);
    gdouble adhesion = gwy_params_get_double(args->params, PARAM_ADHESION);
    gint xres, yres, col, row, k;
    gdouble slope, dx, theta;
    gdouble va, vb, vc, vd;
    gdouble *dfw, *drev;
    const gdouble *surface;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    dx = 2.0*gwy_data_field_get_dx(field);
    dfw = gwy_data_field_get_data(forward);
    drev = gwy_data_field_get_data(reverse);
    surface = gwy_data_field_get_data_const(field);

    for (row = 0; row < yres; row++) {
        for (col = 0; col < xres; col++) {
            k = row*xres + col;
            if (col == 0)
                slope = 2.0*(surface[k + 1] - surface[k])/dx;
            else if (col == xres-1)
                slope = 2.0*(surface[k] - surface[k - 1])/dx;
            else
                slope = (surface[k + 1] - surface[k - 1])/dx;

            theta = fabs(atan(slope));
            va = load*sin(theta);
            vc = cos(theta);
            vb = mu*(load*vc + adhesion);
            vd = mu*sin(theta);

            if (slope >= 0.0) {
                dfw[k] = (va + vb)/(vc - vd);
                drev[k] = -(va - vb)/(vc + vd);
            }
            else {
                dfw[k] = -(va - vb)/(vc + vd);
                drev[k] = (va + vb)/(vc - vd);
            }
        }
    }

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
