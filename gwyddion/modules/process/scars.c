/*
 *  $Id: scars.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define SCARS_MARK_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)
#define SCARS_REMOVE_RUN_MODES GWY_RUN_IMMEDIATE

enum {
    MAX_LENGTH = 1024
};

/* The same as in mark_disconn. */
typedef enum {
    FEATURES_POSITIVE = 1 << 0,
    FEATURES_NEGATIVE = 1 << 2,
    FEATURES_BOTH     = (FEATURES_POSITIVE | FEATURES_NEGATIVE),
} FeatureType;

enum {
    PARAM_TYPE,
    PARAM_THRESHOLD_HIGH,
    PARAM_THRESHOLD_LOW,
    PARAM_MIN_LENGTH,
    PARAM_MAX_WIDTH,
    PARAM_COMBINE_TYPE,
    PARAM_COMBINE,
    PARAM_UPDATE,
    PARAM_MASK_COLOR,
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

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             scars_remove        (GwyContainer *data,
                                             GwyRunType run);
static void             scars_mark          (GwyContainer *data,
                                             GwyRunType run);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             sanitize_params     (GwyParams *params);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Marks and/or removes scars (horizontal linear artifacts)."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, scars)

static gboolean
module_register(void)
{
    gwy_process_func_register("scars_mark",
                              (GwyProcessFunc)&scars_mark,
                              N_("/_Correct Data/M_ark Scars..."),
                              GWY_STOCK_MARK_SCARS,
                              SCARS_MARK_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark horizontal scars (strokes)"));
    gwy_process_func_register("scars_remove",
                              (GwyProcessFunc)&scars_remove,
                              N_("/_Correct Data/Remove _Scars"),
                              GWY_STOCK_SCARS,
                              SCARS_REMOVE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Correct horizontal scars (strokes)"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum feature_types[] = {
        { N_("Positive"), FEATURES_POSITIVE, },
        { N_("Negative"), FEATURES_NEGATIVE, },
        { N_("Both"),     FEATURES_BOTH,     },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "scars");
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("Scars type"),
                              feature_types, G_N_ELEMENTS(feature_types), FEATURES_BOTH);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD_HIGH, "threshold_high", _("_Hard threshold"), 0.0, 2.0, 0.666);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD_LOW, "threshold_low", _("_Soft threshold"), 0.0, 2.0, 0.25);
    gwy_param_def_add_int(paramdef, PARAM_MIN_LENGTH, "min_len", _("Minimum _length"), 1, MAX_LENGTH, 16);
    gwy_param_def_add_int(paramdef, PARAM_MAX_WIDTH, "max_width", _("Maximum _width"), 1, 16, 4);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
mark_scars(GwyDataField *field, GwyDataField *mask,
           GwyParams *params)
{
    FeatureType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
    gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);
    gint min_len = gwy_params_get_int(params, PARAM_MIN_LENGTH);
    gint max_width = gwy_params_get_int(params, PARAM_MAX_WIDTH);
    GwyDataField *tmp;

    if (type == FEATURES_POSITIVE || type == FEATURES_NEGATIVE) {
        gwy_data_field_mark_scars(field, mask, threshold_high, threshold_low,
                                  min_len, max_width, type == FEATURES_NEGATIVE);
        return;
    }

    gwy_data_field_mark_scars(field, mask, threshold_high, threshold_low, min_len, max_width, FALSE);
    tmp = gwy_data_field_new_alike(field, FALSE);
    gwy_data_field_mark_scars(field, tmp, threshold_high, threshold_low, min_len, max_width, TRUE);
    gwy_data_field_max_of_fields(mask, mask, tmp);
    g_object_unref(tmp);
}

static void
scars_remove(GwyContainer *data, GwyRunType run)
{
    GwyParams *params;
    GwyDataField *field, *mask;
    GQuark dquark;
    gint id;

    g_return_if_fail(run & SCARS_REMOVE_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(field && dquark);

    params = gwy_params_new_from_settings(define_module_params());
    sanitize_params(params);
    gwy_app_undo_qcheckpointv(data, 1, &dquark);

    mask = gwy_data_field_new_alike(field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);
    mark_scars(field, mask, params);
    gwy_data_field_laplace_solve(field, mask, -1, 1.0);
    g_object_unref(mask);
    g_object_unref(params);

    gwy_data_field_data_changed(field);
    gwy_app_channel_log_add(data, id, id, "proc::scars_remove",
                            "settings-name", "scars",
                            NULL);
}

static void
scars_mark(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & SCARS_MARK_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitize_params(args.params);
    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add(data, id, id, "proc::scars_remove",
                            "settings-name", "scars",
                            NULL);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static void
execute(ModuleArgs *args)
{
    gboolean combine = gwy_params_get_boolean(args->params, PARAM_COMBINE);
    GwyMergeType combine_type = gwy_params_get_enum(args->params, PARAM_COMBINE_TYPE);

    mark_scars(args->field, args->result, args->params);
    if (args->mask && combine) {
        if (combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(args->result, args->mask);
        else if (combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(args->result, args->mask);
    }
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

    gui.dialog = gwy_dialog_new(_("Mark Scars"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_slider(table, PARAM_MAX_WIDTH);
    gwy_param_table_set_unitstr(table, PARAM_MAX_WIDTH, _("px"));
    gwy_param_table_slider_set_mapping(table, PARAM_MAX_WIDTH, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_MIN_LENGTH);
    gwy_param_table_set_unitstr(table, PARAM_MIN_LENGTH, _("px"));

    gwy_param_table_append_slider(table, PARAM_THRESHOLD_HIGH);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD_HIGH, _("RMS"));
    gwy_param_table_append_slider(table, PARAM_THRESHOLD_LOW);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD_LOW, _("RMS"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_TYPE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }
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
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id == PARAM_THRESHOLD_HIGH || id == PARAM_THRESHOLD_LOW) {
        gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);
        gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
        if (threshold_high < threshold_low) {
            if (id == PARAM_THRESHOLD_HIGH)
                gwy_param_table_set_double(table, PARAM_THRESHOLD_LOW, threshold_high);
            else
                gwy_param_table_set_double(table, PARAM_THRESHOLD_HIGH, threshold_low);
        }
    }
    if (id != PARAM_MASK_COLOR && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
sanitize_params(GwyParams *params)
{
    gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
    gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);

    if (threshold_low > threshold_high)
        gwy_params_set_double(params, PARAM_THRESHOLD_HIGH, threshold_low);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
