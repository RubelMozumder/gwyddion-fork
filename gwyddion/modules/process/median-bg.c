/*
 *  $Id: median-bg.c 23797 2021-05-27 14:00:20Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_RADIUS,
    PARAM_DO_EXTRACT,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *bg;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             median_bg           (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Subtracts background using a rank-based algorithm."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, median_bg)

static gboolean
module_register(void)
{
    gwy_process_func_register("median-bg",
                              (GwyProcessFunc)&median_bg,
                              N_("/_Level/_Median Level..."),
                              GWY_STOCK_LEVEL_MEDIAN,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Level data by local median subtraction"));

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
    gwy_param_def_add_int(paramdef, PARAM_RADIUS, "radius", _("_Radius"), 1, 1024, 20);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    return paramdef;
}

static void
median_bg(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;
    GQuark quark;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && quark);

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    args.bg = gwy_data_field_new_alike(args.field, TRUE);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT && !execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_container_set_object(data, gwy_app_get_data_key_for_id(id), args.result);
    gwy_app_channel_log_add_proc(data, id, id);

    if (gwy_params_get_boolean(args.params, PARAM_DO_EXTRACT)) {
        newid = gwy_app_data_browser_add_data_field(args.bg, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, _("Background"));
        gwy_app_channel_log_add(data, id, newid, NULL, NULL);
    }

end:
    g_object_unref(args.bg);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *dataview, *hbox;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Median Level"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_add_alt(table, PARAM_RADIUS);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_RADIUS, args->field);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_DO_EXTRACT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    if (execute(args, GTK_WINDOW(gui->dialog))) {
        gwy_data_field_data_changed(args->result);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyDataField *bg = args->bg, *result = args->result, *kernel;
    gint size = gwy_params_get_int(args->params, PARAM_RADIUS);
    gint xres, yres, kres = 2*size + 1, n;
    gboolean ok;

    gwy_app_wait_start(wait_window, _("Median-leveling..."));

    kernel = gwy_data_field_new(kres, kres, 1.0, 1.0, TRUE);
    n = gwy_data_field_elliptic_area_fill(kernel, 0, 0, kres, kres, 1.0);
    gwy_data_field_copy(args->field, bg, FALSE);
    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    ok = gwy_data_field_area_filter_kth_rank(bg, kernel, 0, 0, xres, yres, n/2, gwy_app_wait_set_fraction);

    gwy_app_wait_finish();

    g_object_unref(kernel);
    if (ok)
        gwy_data_field_subtract_fields(result, args->field, bg);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
