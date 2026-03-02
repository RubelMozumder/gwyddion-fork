/*
 *  $Id: mask_noisify.c 24796 2022-04-28 15:20:59Z yeti-dn $
 *  Copyright (C) 2017-2021 David Necas (Yeti).
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    NOISE_DIRECTION_BOTH = 0,
    NOISE_DIRECTION_UP   = 1,
    NOISE_DIRECTION_DOWN = 2,
} NoiseDirectionType;

enum {
    PARAM_DENSITY,
    PARAM_DIRECTION,
    PARAM_ONLY_BOUNDARIES,
};

typedef struct {
    GwyDataField *mask;
    GwyDataField *result;
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mask_noisify        (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Adds salt and/or pepper noise to mask."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mask_noisify)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_noisify",
                              (GwyProcessFunc)&mask_noisify,
                              N_("/_Mask/_Noisify..."),
                              GWY_STOCK_MASK_NOISIFY,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Add noise to mask"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("S_ymmetrical"),        NOISE_DIRECTION_BOTH, },
        { N_("One-sided _positive"), NOISE_DIRECTION_UP,   },
        { N_("One-sided _negative"), NOISE_DIRECTION_DOWN, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_DENSITY, "density", _("Densi_ty"), 0.0, 1.0, 0.1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", _("Noise type"),
                              directions, G_N_ELEMENTS(directions), NOISE_DIRECTION_BOTH);
    gwy_param_def_add_boolean(paramdef, PARAM_ONLY_BOUNDARIES, "only_boundaries", _("_Alter only boundaries"), FALSE);
    return paramdef;
}

static void
mask_noisify(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GQuark quark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_MASK_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.mask);

    args.result = g_object_ref(args.mask);   /* Change to a copy once we have a preview. */
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }
    gwy_app_undo_qcheckpointv(data, 1, &quark);
    execute(&args);
    gwy_app_channel_log_add_proc(data, id, id);
    gwy_data_field_data_changed(args.mask);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Noisify Mask"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_DIRECTION);
    gwy_param_table_append_slider(table, PARAM_DENSITY);
    gwy_param_table_slider_set_steps(table, PARAM_DENSITY, 0.001, 0.1);
    gwy_param_table_slider_set_digits(table, PARAM_DENSITY, 4);
    gwy_param_table_append_checkbox(table, PARAM_ONLY_BOUNDARIES);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    GwyRandGenSet *rngset = gwy_rand_gen_set_new(1);
    GwyDataField *mask = args->result;
    NoiseDirectionType direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    gboolean only_boundaries = gwy_params_get_boolean(args->params, PARAM_ONLY_BOUNDARIES);
    gdouble density = gwy_params_get_double(args->params, PARAM_DENSITY);
    gboolean on_boundary;
    guint xres, yres, iter, i, j, n, nind, k, have_bits = 0;
    gint is_m, change_to;
    guint *indices;
    gdouble *m;
    guint32 r;

    gwy_data_field_copy(args->mask, mask, FALSE);
    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    n = xres*yres;
    nind = GWY_ROUND(n*density);
    indices = gwy_rand_gen_set_choose_shuffle(rngset, 0, n, nind);
    m = gwy_data_field_get_data(mask);

    for (iter = 0; iter < nind; iter++) {
        k = indices[iter];

        /* No-ops. */
        is_m = (m[k] > 0.0);
        if (direction == NOISE_DIRECTION_UP)
            change_to = 1;
        else if (direction == NOISE_DIRECTION_DOWN)
            change_to = 0;
        else {
            if (!have_bits) {
                r = gwy_rand_gen_set_int(rngset, 0);
                have_bits = 32;
            }
            change_to = (r & 1);
            have_bits--;
        }
        if (!change_to == !is_m)
            continue;

        /* Are we on a boundary?  This cannot be pre-determined because we allow progressive boundary alteration. */
        if (only_boundaries) {
            on_boundary = FALSE;
            i = k/xres;
            j = k % xres;
            if (!on_boundary && i > 0 && !is_m == !(m[k-xres] <= 0.0))
                on_boundary = TRUE;
            if (!on_boundary && j > 0 && !is_m == !(m[k-1] <= 0.0))
                on_boundary = TRUE;
            if (!on_boundary && j < xres-1 && !is_m == !(m[k+1] <= 0.0))
                on_boundary = TRUE;
            if (!on_boundary && i < yres-1 && !is_m == !(m[k+xres] <= 0.0))
                on_boundary = TRUE;
            if (!on_boundary)
                continue;
        }

        m[k] = change_to;
    }

    g_free(indices);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
