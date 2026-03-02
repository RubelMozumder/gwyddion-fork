/*
 *  $Id: threshold.c 23854 2021-06-14 16:36:54Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti)
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

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    RESPONSE_FULL_RANGE = 1000
};

typedef enum {
    THRESHOLD_RANGE_USER,
    THRESHOLD_RANGE_DISPLAY,
    THRESHOLD_RANGE_OUTLIERS,
    THRESHOLD_RANGE_PERCENTILE,
    THRESHOLD_RANGE_NMODES
} ThresholdRangeMode;

enum {
    PARAM_METHOD,
    PARAM_LOWER,
    PARAM_UPPER,
    PARAM_SIGMA,
    PARAM_LOWER_P,
    PARAM_UPPER_P,
    BUTTON_FULL_RANGE,
    INFO_DISPLAY_RANGE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached values for input data field. */
    gdouble min, max;
    gdouble disp_min, disp_max;
    gdouble avg, rms;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GwyDataField *percentfield;
} ModuleGUI;

static gboolean         module_register       (void);
static GwyParamDef*     define_module_params  (void);
static void             threshold             (GwyContainer *data,
                                               GwyRunType runtype);
static void             execute               (ModuleArgs *args,
                                               GwyDataField *percentfield);
static GwyDialogOutcome run_gui               (ModuleArgs *args,
                                               GwyContainer *data,
                                               gint id);
static void             dialog_response       (ModuleGUI *gui,
                                               gint response);
static void             param_changed         (ModuleGUI *gui,
                                               gint id);
static void             preview               (gpointer user_data);
static void             find_out_display_range(GwyContainer *container,
                                               gint id,
                                               GwyDataField *data_field,
                                               gdouble *disp_min,
                                               gdouble *disp_max);
static void             sanitise_params       (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Limit the data range using a lower/upper threshold."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2010",
};

GWY_MODULE_QUERY2(module_info, threshold)

static gboolean
module_register(void)
{
    gwy_process_func_register("threshold",
                              (GwyProcessFunc)&threshold,
                              N_("/_Basic Operations/Li_mit Range..."),
                              GWY_STOCK_LIMIT_RANGE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Limit data range"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Specify _thresholds"),   THRESHOLD_RANGE_USER,       },
        { N_("Use _display range"),    THRESHOLD_RANGE_DISPLAY,    },
        { N_("Cut off outlier_s"),     THRESHOLD_RANGE_OUTLIERS,   },
        { N_("Limit to _percentiles"), THRESHOLD_RANGE_PERCENTILE, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "mode", _("Method"),
                              methods, G_N_ELEMENTS(methods), THRESHOLD_RANGE_USER);
    gwy_param_def_add_double(paramdef, PARAM_LOWER, "lower", _("_Lower"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_UPPER, "upper", _("_Upper"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("F_arther than"), 1.0, 12.0, 3.0);
    gwy_param_def_add_percentage(paramdef, PARAM_LOWER_P, "lower_p", _("_Lower"), 0.05);
    gwy_param_def_add_percentage(paramdef, PARAM_UPPER_P, "upper_p", _("_Upper"), 0.05);
    return paramdef;
}

static void
threshold(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyDataField *field;
    GQuark quark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    gwy_data_field_get_min_max(field, &args.min, &args.max);
    args.avg = gwy_data_field_get_avg(field);
    args.rms = gwy_data_field_get_rms(field);
    find_out_display_range(data, id, field, &args.disp_min, &args.disp_max);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        args.result = gwy_data_field_new_alike(field, TRUE);
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_HAVE_RESULT)
            goto end;
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        gwy_data_field_copy(args.result, field, FALSE);
    }
    else {
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        args.result = g_object_ref(field);
        execute(&args, NULL);
    }

    gwy_data_field_data_changed(field);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const gint range_sliders[] = { PARAM_LOWER, PARAM_UPPER };
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *dataview, *hbox;
    GwySIValueFormat *vf;
    ModuleGUI gui;
    gdouble range, slider_min, slider_max;
    gchar *s;
    gint i;

    gwy_clear(&gui, 1);
    gui.args = args;

    vf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    vf->precision += 2;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Limit Range"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio_header(table, PARAM_METHOD);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, THRESHOLD_RANGE_USER);
    range = args->max - args->min;
    slider_min = args->min - 0.5*range;
    slider_max = args->max + 0.5*range;
    for (i = 0; i < (gint)G_N_ELEMENTS(range_sliders); i++) {
        gint parid = range_sliders[i];
        gwy_param_table_append_slider(table, parid);
        gwy_param_table_slider_set_mapping(table, parid, GWY_SCALE_MAPPING_LINEAR);
        gwy_param_table_slider_restrict_range(table, parid, slider_min, slider_max);
        gwy_param_table_slider_set_factor(table, parid, 1.0/vf->magnitude);
        gwy_param_table_slider_set_digits(table, parid, vf->precision);
        gwy_param_table_set_unitstr(table, parid, vf->units);
    }
    gwy_param_table_append_button(table, BUTTON_FULL_RANGE, -1, RESPONSE_FULL_RANGE, _("Set to _Full Range"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, THRESHOLD_RANGE_DISPLAY);
    gwy_param_table_append_info(table, INFO_DISPLAY_RANGE, "");
    gwy_param_table_set_unitstr(table, INFO_DISPLAY_RANGE, vf->units);
    /* TRANSLATORS: This is a range: 123 to 456. */
    s = g_strdup_printf(_("%.*f to %.*f"),
                        vf->precision, args->disp_min/vf->magnitude,
                        vf->precision, args->disp_max/vf->magnitude);
    gwy_param_table_info_set_valuestr(table, INFO_DISPLAY_RANGE, s);
    g_free(s);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, THRESHOLD_RANGE_OUTLIERS);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_set_steps(table, PARAM_SIGMA, 0.01, 1.0);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA, _("RMS"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, THRESHOLD_RANGE_PERCENTILE);
    gwy_param_table_append_slider(table, PARAM_LOWER_P);
    gwy_param_table_append_slider(table, PARAM_UPPER_P);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(gui.percentfield);
    gwy_si_unit_value_format_free(vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_METHOD) {
        ThresholdRangeMode method = gwy_params_get_enum(params, PARAM_METHOD);
        gwy_param_table_set_sensitive(table, PARAM_LOWER, method == THRESHOLD_RANGE_USER);
        gwy_param_table_set_sensitive(table, PARAM_UPPER, method == THRESHOLD_RANGE_USER);
        gwy_param_table_set_sensitive(table, BUTTON_FULL_RANGE, method == THRESHOLD_RANGE_USER);
        gwy_param_table_set_sensitive(table, PARAM_SIGMA, method == THRESHOLD_RANGE_OUTLIERS);
        gwy_param_table_set_sensitive(table, PARAM_LOWER_P, method == THRESHOLD_RANGE_PERCENTILE);
        gwy_param_table_set_sensitive(table, PARAM_UPPER_P, method == THRESHOLD_RANGE_PERCENTILE);
    }
    if (id == PARAM_LOWER_P || id == PARAM_UPPER_P) {
        gdouble lower_p = gwy_params_get_double(params, PARAM_LOWER_P);
        gdouble upper_p = gwy_params_get_double(params, PARAM_UPPER_P);
        if (lower_p + upper_p >= 0.9999) {
            if (id == PARAM_LOWER_P)
                gwy_param_table_set_double(table, PARAM_UPPER_P, 0.9999 - 1e-15 - lower_p);
            if (id == PARAM_UPPER_P)
                gwy_param_table_set_double(table, PARAM_LOWER_P, 0.9999 - 1e-15 - upper_p);
        }
    }

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table = gui->table;

    if (response == RESPONSE_FULL_RANGE) {
        gwy_param_table_set_double(table, PARAM_LOWER, args->min);
        gwy_param_table_set_double(table, PARAM_UPPER, args->max);
    }
}

static void
find_out_display_range(GwyContainer *container, gint id, GwyDataField *data_field,
                       gdouble *disp_min, gdouble *disp_max)
{
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;

    gwy_container_gis_enum(container, gwy_app_get_data_range_type_key_for_id(id), &range_type);
    switch (range_type) {
        case GWY_LAYER_BASIC_RANGE_FULL:
        case GWY_LAYER_BASIC_RANGE_ADAPT:
        gwy_data_field_get_min_max(data_field, disp_min, disp_max);
        break;

        case GWY_LAYER_BASIC_RANGE_FIXED:
        gwy_data_field_get_min_max(data_field, disp_min, disp_max);
        gwy_container_gis_double(container, gwy_app_get_data_range_min_key_for_id(id), disp_min);
        gwy_container_gis_double(container, gwy_app_get_data_range_max_key_for_id(id), disp_max);
        break;

        case GWY_LAYER_BASIC_RANGE_AUTO:
        gwy_data_field_get_autorange(data_field, disp_min, disp_max);
        break;

        default:
        g_assert_not_reached();
        break;
    }
}

static void
sanitise_params(ModuleArgs *args)
{
    static gboolean has_been_run = FALSE;

    GwyParams *params = args->params;
    gdouble lower_p = gwy_params_get_double(params, PARAM_LOWER_P);
    gdouble upper_p = gwy_params_get_double(params, PARAM_UPPER_P);
    gdouble lower = gwy_params_get_double(params, PARAM_LOWER);
    gdouble upper = gwy_params_get_double(params, PARAM_UPPER);
    gdouble range = args->max - args->min;

    if (lower_p + upper_p > 0.9999) {
        gwy_params_set_double(params, PARAM_LOWER_P, (upper_p = 0.4999));
        gwy_params_set_double(params, PARAM_UPPER_P, (lower_p = 0.4999));
    }
    if (upper < lower) {
        GWY_SWAP(gdouble, upper, lower);
        gwy_params_set_double(params, PARAM_LOWER, lower);
        gwy_params_set_double(params, PARAM_UPPER, upper);
    }
    if (!has_been_run || lower >= args->max + 0.5*range || upper <= args->min - 0.5*range) {
        gwy_params_set_double(params, PARAM_LOWER, (lower = args->min));
        gwy_params_set_double(params, PARAM_UPPER, (upper = args->max));
        has_been_run = TRUE;
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    /* Since all the percentile functions only shuffle data but do not modify them otherwise we can keep one copy of
     * the data field values and use it repeatedly.  XXX: Since the user is going to move the slider around it would
     * be more efficient to just sort the data.  Then the percentage computation is instant. */
    if (!gui->percentfield)
        gui->percentfield = gwy_data_field_duplicate(args->field);
    execute(args, gui->percentfield);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args, GwyDataField *percentfield)
{
    GwyParams *params = args->params;
    ThresholdRangeMode method = gwy_params_get_enum(params, PARAM_METHOD);
    GwyDataField *buf = NULL;
    gdouble range[2];

    if (method == THRESHOLD_RANGE_USER) {
        gdouble lower = gwy_params_get_double(params, PARAM_LOWER);
        gdouble upper = gwy_params_get_double(params, PARAM_UPPER);

        range[0] = MIN(lower, upper);
        range[1] = MAX(lower, upper);
    }
    else if (method == THRESHOLD_RANGE_DISPLAY) {
        range[0] = MIN(args->disp_min, args->disp_max);
        range[1] = MAX(args->disp_min, args->disp_max);
    }
    else if (method == THRESHOLD_RANGE_OUTLIERS) {
        gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);

        range[0] = args->avg - sigma*args->rms;
        range[1] = args->avg + sigma*args->rms;
    }
    else if (method == THRESHOLD_RANGE_PERCENTILE) {
        gdouble lower_p = gwy_params_get_double(params, PARAM_LOWER_P);
        gdouble upper_p = gwy_params_get_double(params, PARAM_UPPER_P);
        gdouble *pdata;
        gdouble p[2];

        if (!percentfield)
            percentfield = buf = gwy_data_field_duplicate(args->field);
        pdata = gwy_data_field_get_data(percentfield);
        p[0] = 100.0*lower_p;
        p[1] = 100.0*(1.0 - upper_p);
        gwy_math_percentiles(percentfield->xres*percentfield->yres, pdata,
                             GWY_PERCENTILE_INTERPOLATION_LINEAR, 2, p, range);
        GWY_OBJECT_UNREF(buf);
    }
    else {
        g_return_if_reached();
    }
    gwy_data_field_copy(args->field, args->result, FALSE);
    gwy_data_field_clamp(args->result, range[0], range[1]);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
