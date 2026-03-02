/*
 *  $Id: mask_shift.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2020-2021 David Necas (Yeti).
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
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    MASKSHIFT_EXTERIOR_EMPTY  = 1024,
    MASKSHIFT_EXTERIOR_FILLED = 1025,
};

enum {
    PARAM_EXTERIOR,
    PARAM_HMOVE,
    PARAM_VMOVE,
    PARAM_MASK_COLOR,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    /* Cached input image properties. */
    gint hmove_max;
    gint vmove_max;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mask_shift          (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Shift mask with respect to the image."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2020",
};

GWY_MODULE_QUERY2(module_info, mask_shift)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_shift",
                              (GwyProcessFunc)&mask_shift,
                              N_("/_Mask/_Shift..."),
                              GWY_STOCK_MASK_SHIFT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Shift mask"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum exteriors[] = {
        { N_("exterior|Empty"),    MASKSHIFT_EXTERIOR_EMPTY,   },
        { N_("exterior|Filled"),   MASKSHIFT_EXTERIOR_FILLED,  },
        { N_("exterior|Border"),   GWY_EXTERIOR_BORDER_EXTEND, },
        { N_("exterior|Mirror"),   GWY_EXTERIOR_MIRROR_EXTEND, },
        { N_("exterior|Periodic"), GWY_EXTERIOR_PERIODIC,      },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_EXTERIOR, "exterior", _("_Exterior type"),
                              exteriors, G_N_ELEMENTS(exteriors), GWY_EXTERIOR_BORDER_EXTEND);
    gwy_param_def_add_int(paramdef, PARAM_HMOVE, "hmove", _("_Horizontal shift"), -32768, 32768, 0);
    gwy_param_def_add_int(paramdef, PARAM_VMOVE, "vmove", _("_Vertical shift"), -32768, 32768, 0);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
mask_shift(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark quark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_MASK_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.mask);

    args.result = gwy_data_field_duplicate(args.mask);
    args.hmove_max = (gwy_data_field_get_xres(args.field) + 1)/2;
    args.vmove_max = (gwy_data_field_get_yres(args.field) + 1)/2;
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_container_set_object(data, quark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Shift Mask"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_combo(table, PARAM_EXTERIOR);

    gwy_param_table_append_slider(table, PARAM_HMOVE);
    gwy_param_table_slider_restrict_range(table, PARAM_HMOVE, -args->hmove_max, args->hmove_max);
    gwy_param_table_slider_set_mapping(table, PARAM_HMOVE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_HMOVE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_HMOVE, args->field);

    gwy_param_table_append_slider(table, PARAM_VMOVE);
    gwy_param_table_slider_restrict_range(table, PARAM_VMOVE, -args->vmove_max, args->vmove_max);
    gwy_param_table_slider_set_mapping(table, PARAM_VMOVE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_VMOVE);
    gwy_param_table_alt_set_field_pixel_y(table, PARAM_VMOVE, args->field);

    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);

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
    if (id != PARAM_MASK_COLOR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    guint exterior = gwy_params_get_enum(params, PARAM_EXTERIOR);
    gint hmove = gwy_params_get_int(params, PARAM_HMOVE);
    gint vmove = gwy_params_get_int(params, PARAM_VMOVE);
    GwyDataField *extended, *mask = args->mask;
    gint xres, yres;
    gdouble fillvalue = 0.0;

    if (exterior == MASKSHIFT_EXTERIOR_EMPTY) {
        exterior = GWY_EXTERIOR_FIXED_VALUE;
        fillvalue = 0.0;
    }
    else if (exterior == MASKSHIFT_EXTERIOR_FILLED) {
        exterior = GWY_EXTERIOR_FIXED_VALUE;
        fillvalue = 1.0;
    }

    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    extended = gwy_data_field_extend(mask, MAX(hmove, 0), MAX(-hmove, 0), MAX(vmove, 0), MAX(-vmove, 0),
                                     exterior, fillvalue, FALSE);
    gwy_data_field_area_copy(extended, args->result, MAX(-hmove, 0), MAX(-vmove, 0), xres, yres, 0, 0);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint move;

    move = gwy_params_get_int(params, PARAM_HMOVE);
    gwy_params_set_int(params, PARAM_HMOVE, CLAMP(move, -args->hmove_max, args->hmove_max));
    move = gwy_params_get_int(params, PARAM_VMOVE);
    gwy_params_set_int(params, PARAM_VMOVE, CLAMP(move, -args->vmove_max, args->vmove_max));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
