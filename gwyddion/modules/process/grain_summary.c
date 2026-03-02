/*
 *  $Id: grain_summary.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti), Petr Klapetek, Sven Neumann.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, neumann@jpk.com.
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyresults.h>
#include <libprocess/grains.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_REPORT_STYLE,
    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyResults *results;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             grain_summary       (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GwyResults*      create_results      (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             fill_results        (GwyResults *results,
                                             GwyDataField *field,
                                             GwyDataField *mask);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Displays overall grain statistics."),
    "Petr Klapetek <petr@klapetek.cz>, Sven Neumann <neumann@jpk.com>, Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek & Sven Neumann",
    "2015",
};

GWY_MODULE_QUERY2(module_info, grain_summary)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_summary",
                              (GwyProcessFunc)&grain_summary,
                              N_("/_Grains/S_ummary..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Grain summary information"));

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
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Grain Summary"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
grain_summary(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && args.mask);
    args.params = gwy_params_new_from_settings(define_module_params());
    run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    const gchar *values[] = {
        "ngrains", "density", "area", "relarea", "meanarea", "meansize",
        "vol_0", "vol_min", "vol_laplace", "bound_len",
    };

    GwyDialogOutcome outcome;
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.results = create_results(args, data, id);
    fill_results(gui.results, args->field, args->mask);

    gui.dialog = gwy_dialog_new(_("Grain Summary"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results, values, G_N_ELEMENTS(values));
    gwy_param_table_results_fill(table, WIDGET_RESULTS);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);

    return outcome;
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDataField *field = args->field;
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Grain Summary"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_separator(results);
    gwy_results_add_value_int(results, "ngrains", N_("Number of grains"));
    gwy_results_add_value(results, "density", N_("Density"), "power-x", -1, "power-y", -1, NULL);
    gwy_results_add_value(results, "area", N_("Total projected area (abs.)"),
                          "power-x", 1, "power-y", 1,
                          NULL);
    gwy_results_add_value_percents(results, "relarea", _("Total projected area (rel.)"));
    gwy_results_add_value(results, "meanarea", N_("Mean grain area"),
                          "power-x", 1, "power-y", 1,
                          NULL);
    gwy_results_add_value_x(results, "meansize", N_("Mean grain size"));
    gwy_results_add_value(results, "vol_0", N_("Total grain volume (zero)"),
                          "power-x", 1, "power-y", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value(results, "vol_min", N_("Total grain volume (minimum)"),
                          "power-x", 1, "power-y", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value(results, "vol_laplace", N_("Total grain volume (Laplace)"),
                          "power-x", 1, "power-y", 1, "power-z", 1,
                          NULL);
    gwy_results_add_value_x(results, "bound_len", N_("Total projected boundary length"));

    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(field));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(field));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(field));

    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static gdouble
grains_get_total_value(GwyDataField *field,
                       gint ngrains, const gint *grains,
                       gdouble **values, GwyGrainQuantity quantity)
{
    gint i;
    gdouble sum;

    *values = gwy_data_field_grains_get_values(field, *values, ngrains, grains, quantity);
    sum = 0.0;
    for (i = 1; i <= ngrains; i++)
        sum += (*values)[i];

    return sum;
}

static void
fill_results(GwyResults *results, GwyDataField *field, GwyDataField *mask)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gdouble xreal = gwy_data_field_get_xreal(field), yreal = gwy_data_field_get_yreal(field);
    gdouble area, size, vol_0, vol_min, vol_laplace, bound_len;
    gdouble *values;
    gint *grains;
    gint ngrains;

    grains = g_new0(gint, xres*yres);
    ngrains = gwy_data_field_number_grains(mask, grains);
    values = NULL;
    area = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_PROJECTED_AREA);
    size = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_EQUIV_SQUARE_SIDE);
    vol_0 = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_VOLUME_0);
    vol_min = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_VOLUME_MIN);
    vol_laplace = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_VOLUME_LAPLACE);
    bound_len = grains_get_total_value(field, ngrains, grains, &values, GWY_GRAIN_VALUE_FLAT_BOUNDARY_LENGTH);
    g_free(values);
    g_free(grains);

    gwy_results_fill_values(results,
                            "ngrains", ngrains,
                            "density", ngrains/(xreal*yreal),
                            "area", area,
                            "relarea", area/(xreal*yreal),
                            "meanarea", area/ngrains,
                            "meansize", size/ngrains,
                            "vol_0", vol_0,
                            "vol_min", vol_min,
                            "vol_laplace", vol_laplace,
                            "bound_len", bound_len,
                            NULL);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
