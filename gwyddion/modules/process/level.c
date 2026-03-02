/*
 *  $Id: level.c 24796 2022-04-28 15:20:59Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/level.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_MASKING,
};

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static GwyDialogOutcome run_gui             (GwyParams *params,
                                             const gchar *funcname);
static void             level_func          (GwyContainer *data,
                                             GwyRunType run,
                                             const gchar *funcname);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Levels data by simple plane subtraction or by rotation, and fixes minimal or mean value to zero."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, level)

static gboolean
module_register(void)
{
    gwy_process_func_register("level",
                              &level_func,
                              N_("/_Level/Plane _Level"),
                              GWY_STOCK_LEVEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Level data by mean plane subtraction"));
    gwy_process_func_register("level_rotate",
                              &level_func,
                              N_("/_Level/Level _Rotate"),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Automatically level data by plane rotation"));
    gwy_process_func_register("fix_zero",
                              &level_func,
                              N_("/_Level/Fix _Zero"),
                              GWY_STOCK_FIX_ZERO,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Shift minimum data value to zero"));
    gwy_process_func_register("zero_mean",
                              &level_func,
                              N_("/_Level/Zero _Mean Value"),
                              GWY_STOCK_ZERO_MEAN,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Shift mean data value to zero"));
    gwy_process_func_register("zero_max",
                              &level_func,
                              N_("/_Level/Zero Ma_ximum Value"),
                              GWY_STOCK_ZERO_MAXIMUM,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Shift maximum data value to zero"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "mode", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_EXCLUDE);
    return paramdef;
}

static void
level_func(GwyContainer *data,
           GwyRunType run,
           const gchar *funcname)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyMaskingType masking;
    GwyParams *params;
    GwyDataField *dfield, *mask;
    gdouble c, bx, by;
    gint xres, yres;
    GQuark quark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD, &mask,
                                     0);
    g_return_if_fail(dfield && quark);

    /* We have multiple functions with the same set of parameters. */
    gwy_param_def_set_function_name(define_module_params(), funcname);
    params = gwy_params_new_from_settings(define_module_params());
    if (run != GWY_RUN_IMMEDIATE && mask) {
        outcome = run_gui(params, funcname);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);

    gwy_app_undo_qcheckpoint(data, quark, NULL);

    if (gwy_stramong(funcname, "level", "level_rotate", NULL)) {
        if (mask) {
            if (masking == GWY_MASK_EXCLUDE) {
                mask = gwy_data_field_duplicate(mask);
                gwy_data_field_grains_invert(mask);
            }
            else
                g_object_ref(mask);
        }

        if (mask)
            gwy_data_field_area_fit_plane(dfield, mask, 0, 0, xres, yres, &c, &bx, &by);
        else
            gwy_data_field_fit_plane(dfield, &c, &bx, &by);

        if (gwy_strequal(funcname, "level_rotate")) {
            bx = gwy_data_field_rtoj(dfield, bx);
            by = gwy_data_field_rtoi(dfield, by);
            gwy_data_field_plane_rotate(dfield, atan2(bx, 1), atan2(by, 1), GWY_INTERPOLATION_LINEAR);
            gwy_debug("b = %g, alpha = %g deg, c = %g, beta = %g deg",
                      bx, 180/G_PI*atan2(bx, 1), by, 180/G_PI*atan2(by, 1));
        }
        else {
            c = -0.5*(bx*gwy_data_field_get_xres(dfield) + by*gwy_data_field_get_yres(dfield));
            gwy_data_field_plane_level(dfield, c, bx, by);
        }
        GWY_OBJECT_UNREF(mask);
    }
    else if (gwy_strequal(funcname, "fix_zero")) {
        if (mask)
            gwy_data_field_area_get_min_max_mask(dfield, mask, masking, 0, 0, xres, yres, &c, NULL);
        else
            c = gwy_data_field_get_min(dfield);
        gwy_data_field_add(dfield, -c);
    }
    else if (gwy_strequal(funcname, "zero_mean")) {
        if (mask)
            c = gwy_data_field_area_get_avg_mask(dfield, mask, masking, 0, 0, xres, yres);
        else
            c = gwy_data_field_get_avg(dfield);
        gwy_data_field_add(dfield, -c);
    }
    else if (gwy_strequal(funcname, "zero_max")) {
        if (mask)
            gwy_data_field_area_get_min_max_mask(dfield, mask, masking, 0, 0, xres, yres, NULL, &c);
        else
            c = gwy_data_field_get_max(dfield);
        gwy_data_field_add(dfield, -c);
    }
    else {
        g_assert_not_reached();
    }

    gwy_app_channel_log_add_proc(data, id, id);
    gwy_data_field_data_changed(dfield);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(GwyParams *params, const gchar *funcname)
{
    const gchar *title = NULL;
    GwyDialog *dialog;
    GwyParamTable *table;

    if (gwy_strequal(funcname, "level"))
        title = _("Plane Level");
    else if (gwy_strequal(funcname, "level_rotate"))
        title = _("Level Rotate");
    else if (gwy_strequal(funcname, "fix_zero"))
        title = _("Fix Zero");
    else if (gwy_strequal(funcname, "zero_mean"))
        title = _("Zero Mean Value");
    else if (gwy_strequal(funcname, "zero_max"))
        title = _("Zero Maximum Value");
    else {
        g_assert_not_reached();
    }

    dialog = GWY_DIALOG(gwy_dialog_new(title));
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    table = gwy_param_table_new(params);
    gwy_param_table_append_radio(table, PARAM_MASKING);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
