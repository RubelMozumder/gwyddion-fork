/*
 *  @(#) $Id: threshold-example.c 24821 2022-05-13 11:51:42Z yeti-dn $
 *  Copyright (C) 2003,2004,2008,2014,2022 David Necas (Yeti)
 *  E-mail: yeti@gwyddion.net
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

#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyversion.h>
#include <libgwymodule/gwymodule.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include "config.h"

#define MOD_NAME PACKAGE_NAME

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* List of parameter ids for GwyParams.  The ids just need to be nonnegative and different. */
enum {
    PARAM_FRACTILE,
    PARAM_ABSOLUTE,
    PARAM_MODE,
};

typedef enum {
    CHANGE_DATA         = 0,
    CHANGE_MASK         = 1,
    CHANGE_PRESENTATION = 2,
} ThresholdMode;

/* Settings and data for this function. */
typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached/precomputed input data properties. */
    gdouble *sorted;
    gint n;
} ModuleArgs;

/* Objects and other data for the GUI. */
typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             threshold           (GwyContainer *data,
                                             GwyRunType run,
                                             const gchar *name);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gdouble          fractile_to_absolute(ModuleArgs *args,
                                             gdouble f);
static gdouble          absolute_to_fractile(ModuleArgs *args,
                                             gdouble value);

/* The module info. */
static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    "This is an example module.  Splits the values to only two distinct ones using either an absolute threshold "
        "or a fractile value.",
    "Yeti <yeti@gwyddion.net>",
    PACKAGE_VERSION,
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

/* This is the ONLY exported symbol.  The argument is the module info.  NO semicolon after. */
GWY_MODULE_QUERY(module_info)

/*
 * Module registeration function.
 *
 * It is called at Gwyddion startup and registers one or more functions.
 */
static gboolean
module_register(void)
{
    gwy_process_func_register(MOD_NAME,
                              &threshold,
                              N_("/_Test/_Threshold..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Threshold data."));

    return TRUE;
}

/*
 * Parameter definition using the GwyParams framework.
 *
 * When first called it creates a static GwyParamDef object returned on all future calls.
 *
 * GwyParamDef holds parameter keys for settings, labels for GUI, allowed values and defaults.
 */
static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("_Data"),         CHANGE_DATA,         },
        { N_("_Mask"),         CHANGE_MASK,         },
        { N_("_Presentation"), CHANGE_PRESENTATION, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, MOD_NAME);
    gwy_param_def_add_percentage(paramdef, PARAM_FRACTILE, "fractile", _("_Fractile"), 0.5);
    /* ABSOLUTE is not saved because on different data the fractile and absolute threshold would no longer agree.  So
     * it has NULL key. The value range is the full range of double; we restrict it on the fly. */
    gwy_param_def_add_double(paramdef, PARAM_ABSOLUTE, NULL, _("Absolute _threshold"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Modify"), modes, G_N_ELEMENTS(modes), CHANGE_DATA);
    return paramdef;
}

/*
 * The main function.
 *
 * This is what gwy_process_func_register() registers and what the program calls when the user invokes the function
 * from the menu or toolbox.
 */
static void
threshold(GwyContainer *data, GwyRunType run, G_GNUC_UNUSED const gchar *name)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark quarks[3];
    ModuleArgs args;
    ThresholdMode mode;
    gint id;

    g_return_if_fail(run & RUN_MODES);

    /* Obtain the current data field and keys.  This function can optinally set either a mask or presentation, so we
     * need to get more things than typical. */
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, quarks + CHANGE_DATA,
                                     GWY_APP_MASK_FIELD_KEY, quarks + CHANGE_MASK,
                                     GWY_APP_SHOW_FIELD_KEY, quarks + CHANGE_PRESENTATION,
                                     0);
    args.result = gwy_data_field_new_alike(args.field, FALSE);

    /* Set up parameters from settings. */
    args.params = gwy_params_new_from_settings(define_module_params());

    /* Precompute sorted data for fractile ↔ value transformation. */
    args.n = gwy_data_field_get_xres(args.field)*gwy_data_field_get_yres(args.field);
    args.sorted = g_memdup(gwy_data_field_get_data_const(args.field), args.n*sizeof(gdouble));
    gwy_math_sort(args.n, args.sorted);

    /* Possibly present the GUI.  Save settings even if the user cancels the dialogue. */
    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    /* Run the computation if the result is not already computed.  In this case the GUI never computes it.  But it is
     * nicer to keep this idiom. */
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    mode = gwy_params_get_enum(args.params, PARAM_MODE);
    /* Save current data field (of the type we are going to modify) for undo */
    gwy_app_undo_qcheckpointv(data, 1, quarks + mode);
    /* Replace it. */
    gwy_container_set_object(data, quarks[mode], args.result);
    /* Log the data processing operation (the source and target channel is the same). */
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_free(args.sorted);
    g_object_unref(args.params);
    g_object_unref(args.result);
}

/*
 * The computation.
 *
 * Compute data field args.result from args.field according to parameters.
 */
static void
execute(ModuleArgs *args)
{
    gdouble threshold = fractile_to_absolute(args, gwy_params_get_double(args->params, PARAM_FRACTILE));
    ThresholdMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    gdouble lower = 0.0, upper = 1.0;

    /* Different modes have slightly different output ranges and units. */
    if (mode == CHANGE_DATA)
        gwy_data_field_get_min_max(args->field, &lower, &upper);
    else
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args->result), NULL);

    gwy_data_field_copy(args->field, args->result, FALSE);
    gwy_data_field_threshold(args->result, threshold, lower, upper);
}

/*
 * The GUI.
 *
 * It shows the dialogue with parameter settings and returns the final user's decision.
 */
static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwySIValueFormat *vf;

    gui.args = args;

    /* Create the dialogue. */
    gui.dialog = gwy_dialog_new(_("Threshold"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    /* Create the parameter controls. */
    table = gui.table = gwy_param_table_new(args->params);

    /* The threshold sliders. */
    gwy_param_table_append_slider(table, PARAM_FRACTILE);
    gwy_param_table_slider_set_mapping(table, PARAM_FRACTILE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_ABSOLUTE);
    gwy_param_table_slider_set_mapping(table, PARAM_ABSOLUTE, GWY_SCALE_MAPPING_LINEAR);
    /* Set the correct range and units for the absolute threshold slider. */
    vf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    gwy_param_table_set_unitstr(table, PARAM_ABSOLUTE, vf->units);
    gwy_param_table_slider_set_factor(table, PARAM_ABSOLUTE, 1.0/vf->magnitude);
    gwy_param_table_slider_restrict_range(table, PARAM_ABSOLUTE, args->sorted[0], args->sorted[args->n-1]);
    gwy_si_unit_value_format_free(vf);

    /* The mode. */
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_MODE);

    /* Add the parameter table to the dialogue and handle its "param-changed" signal. */
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

/* Handler for "param-changed" signal of the parameter table, invoked whenever any parameter changes.  If id < 0 it
 * means a global update, like Reset button or dialog initialisation.
 *
 * GwyDialog prevents recursion. So we do not have to care about infinite recursion caused by cross-updates. But for
 * the same reason we have to update everything that depends on the changed parameter. */
static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;

    /* Ensure cross-updates between FRACTILE and ABSOLUTE. */
    if (id < 0 || id == PARAM_FRACTILE) {
        gwy_param_table_set_double(table, PARAM_ABSOLUTE,
                                   fractile_to_absolute(args, gwy_params_get_double(params, PARAM_FRACTILE)));
    }
    if (id == PARAM_ABSOLUTE) {
        gwy_param_table_set_double(table, PARAM_FRACTILE,
                                   absolute_to_fractile(args, gwy_params_get_double(params, PARAM_ABSOLUTE)));
    }
}

/* Convert fractile to data value. */
static gdouble
fractile_to_absolute(ModuleArgs *args, gdouble f)
{
    const gdouble *sorted = args->sorted;
    gint n = args->n;
    gdouble x = f*(n - 1);
    gint i = (gint)floor(x);
    gdouble t = x - i;

    if (i >= n-1)
        return sorted[n-1];
    return t*sorted[i+1] + (1.0-t)*sorted[i];
}

/* Convert data value to fractile (using bisection). */
static gdouble
absolute_to_fractile(ModuleArgs *args, gdouble value)
{
    const gdouble *sorted = args->sorted;
    gint mid, low, high, ilow, ihigh, n = args->n;

    low = 0;
    high = n-1;
    do {
        mid = (low + high)/2;
        if (sorted[mid] > value)
            high = mid;
        else
            low = mid;
    } while (high - low > 1);
    ilow = (sorted[low] == value ? low : high);

    low = 0;
    high = n-1;
    do {
        mid = (low + high)/2;
        if (sorted[mid] < value)
            low = mid;
        else
            high = mid;
    } while (high - low > 1);
    ihigh = (sorted[high] == value ? high : low);

    return 0.5*(ihigh + ilow)/(n - 1.0);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
