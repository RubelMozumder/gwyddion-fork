/*
 *  $Id: level_grains.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2011-2021 David Necas (Yeti).
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
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/gwygrainvalue.h>
#include <libprocess/correct.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

enum {
    PARAM_BASE,
    PARAM_DO_EXTRACT,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyDataField *bg;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             level_grains        (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Levels individual grains, interpolating the shifts between using Laplacian interpolation."),
    "David Nečas <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2011",
};

GWY_MODULE_QUERY2(module_info, level_grains)

static gboolean
module_register(void)
{
    gwy_process_func_register("level_grains",
                              (GwyProcessFunc)&level_grains,
                              N_("/_Grains/_Level Grains..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Level individual grains, interpolating the shifts between using Laplacian "
                                 "interpolation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum bases[] = {
        { NULL, GWY_GRAIN_VALUE_MINIMUM,          },
        { NULL, GWY_GRAIN_VALUE_MAXIMUM,          },
        { NULL, GWY_GRAIN_VALUE_MEAN,             },
        { NULL, GWY_GRAIN_VALUE_MEDIAN,           },
        { NULL, GWY_GRAIN_VALUE_BOUNDARY_MINIMUM, },
        { NULL, GWY_GRAIN_VALUE_BOUNDARY_MAXIMUM, },
    };
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    for (i = 0; i < G_N_ELEMENTS(bases); i++) {
        GwyGrainValue *value = gwy_grain_values_get_builtin_grain_value(bases[i].value);
        bases[i].name = gwy_resource_get_name(GWY_RESOURCE(value));
    }

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_BASE, "base", _("Quantity to level"),
                              bases, G_N_ELEMENTS(bases), GWY_GRAIN_VALUE_MINIMUM);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    return paramdef;
}

static void
level_grains(GwyContainer *data, GwyRunType runtype)
{
    GwyParams *params;
    ModuleArgs args;
    GQuark quark;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(args.field && quark);

    args.result = g_object_ref(args.field);   /* Change to a copy once we have a preview. */
    args.bg = gwy_data_field_new_alike(args.field, FALSE);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }
    gwy_app_undo_qcheckpointv(data, 1, &quark);
    execute(&args);
    gwy_app_channel_log_add_proc(data, id, id);
    gwy_data_field_data_changed(args.field);

    if (gwy_params_get_boolean(params, PARAM_DO_EXTRACT)) {
        newid = gwy_app_data_browser_add_data_field(args.bg, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
        gwy_app_set_data_field_title(data, newid, _("Background"));
        gwy_app_channel_log_add_proc(data, id, newid);
    }

end:
    g_object_unref(args.bg);
    g_object_unref(args.result);
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Level Grains"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_BASE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *invmask, *background = args->bg, *mask = args->mask;
    GwyGrainQuantity base = gwy_params_get_enum(args->params, PARAM_BASE);
    gdouble *heights, *bgdata;
    gint *grains;
    gint i, xres, yres, ngrains;

    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    grains = g_new0(gint, xres*yres);
    ngrains = gwy_data_field_number_grains(mask, grains);
    if (!ngrains) {
        g_free(grains);
        return;
    }

    heights = g_new(gdouble, ngrains+1);
    gwy_data_field_grains_get_values(args->field, heights, ngrains, grains, base);
    heights[0] = 0.0;

    bgdata = gwy_data_field_get_data(background);
    for (i = 0; i < xres*yres; i++)
        bgdata[i] = -heights[grains[i]];
    g_free(grains);
    g_free(heights);

    invmask = gwy_data_field_duplicate(mask);
    gwy_data_field_grains_invert(invmask);
    gwy_data_field_laplace_solve(background, invmask, -1, 0.8);
    g_object_unref(invmask);

    gwy_data_field_invert(background, FALSE, FALSE, TRUE);
    gwy_data_field_subtract_fields(args->result, args->field, background);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
