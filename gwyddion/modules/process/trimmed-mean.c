/*
 *  $Id: trimmed-mean.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
    MAX_SIZE = 1024,
    MAX_SIZE2 = (2*MAX_SIZE + 1)*(2*MAX_SIZE + 1),
};

enum {
    PARAM_SIZE,
    PARAM_FRACTION_LOWEST,
    PARAM_FRACTION_HIGHEST,
    PARAM_VALUES_LOWEST,
    PARAM_VALUES_HIGHEST,
    PARAM_TRIM_SYMM,
    PARAM_DO_EXTRACT,
    HEADER_HIGHEST,
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

static gboolean         module_register           (void);
static GwyParamDef*     define_module_params      (void);
static void             trimmed_mean              (GwyContainer *data,
                                                   GwyRunType runtype);
static gboolean         execute                   (ModuleArgs *args,
                                                   GtkWindow *wait_window);
static GwyDialogOutcome run_gui                   (ModuleArgs *args);
static void             param_changed             (ModuleGUI *gui,
                                                   gint id);
static void             calculate_nlowest_nhighest(GwyParams *params,
                                                   gint *nlowest,
                                                   gint *nhighest);
static void             sanitise_params           (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Trimmed mean filtering and leveling."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, trimmed_mean)

static gboolean
module_register(void)
{
    gwy_process_func_register("trimmed_mean",
                              (GwyProcessFunc)&trimmed_mean,
                              N_("/_Level/_Trimmed Mean..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Trimmed mean leveling and filter"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "trimmed-mean");  /* Compatibility. */
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("Kernel _size"), 1, MAX_SIZE, 20);
    gwy_param_def_add_percentage(paramdef, PARAM_FRACTION_LOWEST, "fraction_lowest", _("_Percentile"), 0.05);
    gwy_param_def_add_percentage(paramdef, PARAM_FRACTION_HIGHEST, "fraction_highest", _("_Percentile"), 0.05);
    gwy_param_def_add_int(paramdef, PARAM_VALUES_LOWEST, NULL, _("_Number of values"), 0, MAX_SIZE2, 0);
    gwy_param_def_add_int(paramdef, PARAM_VALUES_HIGHEST, NULL, _("_Number of values"), 0, MAX_SIZE2, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_TRIM_SYMM, "trim_symm", _("_Trim symmetrically"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    return paramdef;
}

static void
trimmed_mean(GwyContainer *data, GwyRunType runtype)
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
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_data_field_subtract_fields(args.field, args.field, args.result);
    gwy_data_field_data_changed(args.field);
    gwy_app_channel_log_add_proc(data, id, id);

    if (!gwy_params_get_boolean(args.params, PARAM_DO_EXTRACT))
        goto end;

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Background"));
    gwy_app_channel_log_add(data, id, newid, NULL, NULL);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyDataField *field = args->field, *result = args->result, *kernel;
    gint size = gwy_params_get_int(args->params, PARAM_SIZE);
    gint kres = 2*size + 1;
    gint xres, yres, nlowest, nhighest;
    gboolean ok;

    gwy_app_wait_start(wait_window, _("Filtering..."));

    calculate_nlowest_nhighest(args->params, &nlowest, &nhighest);
    kernel = gwy_data_field_new(kres, kres, 1.0, 1.0, TRUE);
    gwy_data_field_elliptic_area_fill(kernel, 0, 0, kres, kres, 1.0);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    gwy_data_field_copy(field, result, FALSE);
    ok = gwy_data_field_area_filter_trimmed_mean(result, kernel, 0, 0, xres, yres, nlowest, nhighest,
                                                 gwy_app_wait_set_fraction);
    g_object_unref(kernel);

    gwy_app_wait_finish();

    return ok;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Trimmed Mean"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Kernel Size"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIZE, args->field);

    gwy_param_table_append_header(table, -1, _("Trim Lowest"));
    gwy_param_table_append_slider(table, PARAM_FRACTION_LOWEST);
    gwy_param_table_slider_set_mapping(table, PARAM_FRACTION_LOWEST, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_append_slider(table, PARAM_VALUES_LOWEST);
    gwy_param_table_append_checkbox(table, PARAM_TRIM_SYMM);

    gwy_param_table_append_header(table, HEADER_HIGHEST, _("Trim Highest"));
    gwy_param_table_append_slider(table, PARAM_FRACTION_HIGHEST);
    gwy_param_table_slider_set_mapping(table, PARAM_FRACTION_HIGHEST, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_append_slider(table, PARAM_VALUES_HIGHEST);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    gint size = gwy_params_get_int(args->params, PARAM_SIZE);
    gint nlowest, nhighest, kres = 2*size + 1;
    gint n = gwy_data_field_get_elliptic_area_size(kres, kres);
    gboolean trim_symm = gwy_params_get_boolean(params, PARAM_TRIM_SYMM);
    gdouble fraction_lowest = gwy_params_get_double(params, PARAM_FRACTION_LOWEST);
    gdouble fraction_highest = gwy_params_get_double(params, PARAM_FRACTION_HIGHEST);
    gdouble m;

    if (id < 0 || id == PARAM_SIZE || id == PARAM_TRIM_SYMM) {
        m = (trim_symm ? 0.5*(n-1) : n-1);
        gwy_param_table_slider_restrict_range(table, PARAM_VALUES_LOWEST, 0.0, m);
        gwy_param_table_slider_restrict_range(table, PARAM_VALUES_HIGHEST, 0.0, m);
    }
    /* If the integers change pretend the fractions changed instead. */
    if (id == PARAM_VALUES_LOWEST) {
        fraction_lowest = gwy_params_get_int(params, PARAM_VALUES_LOWEST)/(gdouble)n;
        gwy_param_table_set_double(table, PARAM_FRACTION_LOWEST, fraction_lowest);
        id = PARAM_FRACTION_LOWEST;
    }
    else if (id == PARAM_VALUES_HIGHEST) {
        fraction_highest = gwy_params_get_int(params, PARAM_VALUES_HIGHEST)/(gdouble)n;
        gwy_param_table_set_double(table, PARAM_FRACTION_HIGHEST, fraction_highest);
        id = PARAM_FRACTION_HIGHEST;
    }

    if (id < 0 || id == PARAM_TRIM_SYMM) {
        gwy_param_table_set_sensitive(table, PARAM_FRACTION_HIGHEST, !trim_symm);
        gwy_param_table_set_sensitive(table, PARAM_VALUES_HIGHEST, !trim_symm);
        gwy_param_table_set_sensitive(table, HEADER_HIGHEST, !trim_symm);
        if (trim_symm) {
            m = fmin(fraction_highest, fraction_lowest);
            gwy_param_table_set_double(table, PARAM_FRACTION_LOWEST, (fraction_lowest = m));
            gwy_param_table_set_double(table, PARAM_FRACTION_HIGHEST, (fraction_highest = m));
        }
        m = (trim_symm ? 0.5 : 1.0);
        gwy_param_table_slider_restrict_range(table, PARAM_FRACTION_LOWEST, 0.0, m);
        gwy_param_table_slider_restrict_range(table, PARAM_FRACTION_HIGHEST, 0.0, m);
    }

    if (id == PARAM_FRACTION_LOWEST) {
        if (trim_symm)
            gwy_param_table_set_double(table, PARAM_FRACTION_HIGHEST, (fraction_highest = fraction_lowest));
        else if (fraction_lowest + fraction_highest >= 1.0)
            gwy_param_table_set_double(table, PARAM_FRACTION_HIGHEST, (fraction_highest = 1.0 - fraction_lowest));
    }
    if (id == PARAM_FRACTION_HIGHEST) {
        if (trim_symm)
            gwy_param_table_set_double(table, PARAM_FRACTION_LOWEST, (fraction_lowest = fraction_highest));
        else if (fraction_lowest + fraction_highest >= 1.0)
            gwy_param_table_set_double(table, PARAM_FRACTION_LOWEST, (fraction_lowest = 1.0 - fraction_highest));
    }
    calculate_nlowest_nhighest(params, &nlowest, &nhighest);
    gwy_param_table_set_int(table, PARAM_VALUES_LOWEST, nlowest);
    gwy_param_table_set_int(table, PARAM_VALUES_HIGHEST, nhighest);
}

static void
calculate_nlowest_nhighest(GwyParams *params, gint *nlowest, gint *nhighest)
{
    gdouble fraction_lowest = gwy_params_get_double(params, PARAM_FRACTION_LOWEST);
    gdouble fraction_highest = gwy_params_get_double(params, PARAM_FRACTION_HIGHEST);
    gint size = gwy_params_get_int(params, PARAM_SIZE);
    gint kres = 2*size + 1;
    gint n = gwy_data_field_get_elliptic_area_size(kres, kres);

    *nlowest = (gint)floor(fraction_lowest*n + 1e-12);
    *nhighest = (gint)floor(fraction_highest*n + 1e-12);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble fraction_lowest = gwy_params_get_double(params, PARAM_FRACTION_LOWEST);
    gdouble fraction_highest = gwy_params_get_double(params, PARAM_FRACTION_HIGHEST);
    gboolean trim_symm = gwy_params_get_boolean(params, PARAM_TRIM_SYMM);

    if (fraction_highest != fraction_lowest)
        gwy_params_set_boolean(params, PARAM_TRIM_SYMM, (trim_symm = FALSE));
    if (fraction_highest + fraction_lowest >= 0.99) {
        gwy_params_set_double(params, PARAM_FRACTION_LOWEST, (fraction_lowest = 0.495));
        gwy_params_set_double(params, PARAM_FRACTION_HIGHEST, (fraction_highest = 0.495));
    }
    /* The transient integer parameters are recalculated later. */
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
