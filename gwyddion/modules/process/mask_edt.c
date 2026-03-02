/*
 *  $Id: mask_edt.c 23853 2021-06-14 16:35:59Z yeti-dn $
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
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    MASKEDT_INTERIOR = 0,
    MASKEDT_EXTERIOR = 1,
    MASKEDT_SIGNED   = 2,
} MaskEdtType;

enum {
    PARAM_DIST_TYPE,
    PARAM_OUTPUT,
    PARAM_FROM_BORDER,
    PARAM_UPDATE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register(void);
static GwyParamDef*     define_params  (void);
static void             mask_edt       (GwyContainer *data,
                                        GwyRunType runtype);
static void             execute        (ModuleArgs *args);
static GwyDialogOutcome run_gui        (ModuleArgs *args,
                                        GwyContainer *data,
                                        gint id);
static void             param_changed  (ModuleGUI *gui,
                                        gint id);
static void             preview        (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performs simple and true Euclidean distance transforms of masks."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, mask_edt)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_edt",
                              (GwyProcessFunc)&mask_edt,
                              N_("/_Mask/Distanc_e Transform..."),
                              GWY_STOCK_DISTANCE_TRANSFORM,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Distance transform of mask"));

    return TRUE;
}

static GwyParamDef*
define_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Interior"),  MASKEDT_INTERIOR },
        { N_("Exterior"),  MASKEDT_EXTERIOR },
        { N_("Two-sided"), MASKEDT_SIGNED   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_enum(paramdef, PARAM_DIST_TYPE, "dist_type", _("_Distance type"),
                           GWY_TYPE_DISTANCE_TRANSFORM_TYPE, GWY_DISTANCE_TRANSFORM_EUCLIDEAN);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "mask_type", _("Output type"),
                              outputs, G_N_ELEMENTS(outputs), MASKEDT_INTERIOR);
    gwy_param_def_add_boolean(paramdef, PARAM_FROM_BORDER, "from_border", _("Shrink from _border"), TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
mask_edt(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.mask && args.field);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    args.params = gwy_params_new_from_settings(define_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Distance Transform"));
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;

    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Distance Transform"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_DIST_TYPE);
    gwy_param_table_append_radio(table, PARAM_OUTPUT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_FROM_BORDER);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *mask = args->mask, *field = args->field, *result = args->result;
    GwyDistanceTransformType dtype = gwy_params_get_enum(args->params, PARAM_DIST_TYPE);
    gboolean from_border = gwy_params_get_boolean(args->params, PARAM_FROM_BORDER);
    MaskEdtType output = gwy_params_get_enum(args->params, PARAM_OUTPUT);

    gwy_data_field_copy(mask, result, FALSE);
    if (output == MASKEDT_INTERIOR) {
        gwy_data_field_grain_simple_dist_trans(result, dtype, from_border);
    }
    else if (output == MASKEDT_EXTERIOR) {
        gwy_data_field_grains_invert(result);
        gwy_data_field_grain_simple_dist_trans(result, dtype, from_border);
    }
    else if (output == MASKEDT_SIGNED) {
        GwyDataField *tmp = gwy_data_field_duplicate(result);

        gwy_data_field_grain_simple_dist_trans(result, dtype, from_border);
        gwy_data_field_grains_invert(tmp);
        gwy_data_field_grain_simple_dist_trans(tmp, dtype, from_border);
        gwy_data_field_subtract_fields(result, result, tmp);
        g_object_unref(tmp);
    }

    gwy_data_field_multiply(result, sqrt(gwy_data_field_get_dx(field)*gwy_data_field_get_dy(field)));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(result), gwy_data_field_get_si_unit_xy(field));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
