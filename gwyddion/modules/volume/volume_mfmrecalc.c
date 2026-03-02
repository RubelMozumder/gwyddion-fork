/*
 *  $Id: volume_mfmrecalc.c 25532 2023-06-30 16:57:44Z yeti-dn $
 *  Copyright (C) 2017-2023 David Necas (Yeti).
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
#include <libprocess/brick.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "../process/mfmops.h" //mfmops should go to libprocess

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIGNAL,
    PARAM_RESULT,
    PARAM_SPRING_CONSTANT,
    PARAM_QUALITY,
    PARAM_BASE_FREQUENCY,
    PARAM_BASE_AMPLITUDE,
    PARAM_NEW_CHANNEL,
};

typedef enum  {
    SIGNAL_PHASE_DEG   = 0,
    SIGNAL_PHASE_RAD   = 1,
    SIGNAL_FREQUENCY   = 2,
    SIGNAL_AMPLITUDE_V = 3,
    SIGNAL_AMPLITUDE_M = 4
} MfmRecalcSignal;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyParamTable *table;
    GtkWidget *dialog;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             volume_mfmrecalc    (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             execute             (ModuleArgs *args);
static gboolean         guess_signal        (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts the MFM data to force gradient."),
    "Petr Klapetek <klapetek@gwyddion.net>, Robb Puttock <robb.puttock@npl.co.uk>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_mfmrecalc)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_mfmrecalc",
                             (GwyVolumeFunc)&volume_mfmrecalc,
                             N_("/SPM M_odes/_Magnetic Data to Force Gradient..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Recalculate to force gradient"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum signals[] = {
        { N_("Phase (radians)"),   SIGNAL_PHASE_RAD, },
        { N_("Phase (degrees)"),   SIGNAL_PHASE_DEG, },
        { N_("Frequency shift"),   SIGNAL_FREQUENCY, },
        { N_("Amplitude (V)"),     SIGNAL_AMPLITUDE_V, },
        { N_("Amplitude (m)"),     SIGNAL_AMPLITUDE_M, },
    };
    static const GwyEnum results[] = {
        { N_("Force gradient"),                GWY_MFM_GRADIENT_FORCE, },
        { N_("MFM force gradient"),            GWY_MFM_GRADIENT_MFM, },
        { N_("Pixel area MFM force gradient"), GWY_MFM_GRADIENT_MFM_AREA, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_SIGNAL, "signal", _("Signal"),
                              signals, G_N_ELEMENTS(signals), SIGNAL_PHASE_DEG);
    gwy_param_def_add_gwyenum(paramdef, PARAM_RESULT, "result", _("Result _type"),
                              results, G_N_ELEMENTS(results), GWY_MFM_GRADIENT_MFM);
    gwy_param_def_add_double(paramdef, PARAM_SPRING_CONSTANT, "spring_constant", _("_Spring constant"),
                             0.01, 1000.0, 40.0);
    gwy_param_def_add_double(paramdef, PARAM_QUALITY, "quality", _("_Quality factor"),
                             0.01, 10000.0, 1000.0);
    gwy_param_def_add_double(paramdef, PARAM_BASE_FREQUENCY, "base_frequency", _("_Base frequency"),
                             1.0, 1e6, 150.0);
    gwy_param_def_add_double(paramdef, PARAM_BASE_AMPLITUDE, "base_amplitude", _("_Base amplitude"),
                             0.01, 1000.0, 0.2);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_CHANNEL, "new_channel", _("Create new volume data"), TRUE);
    return paramdef;
}

static void
issue_warning(GtkWindow *window)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(window, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                    _("Data value units must be deg, rad, m, Hz or V for the recalculation"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
volume_mfmrecalc(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDataField *preview = NULL;
    gint oldid, newid, xres, yres;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark quarks[2];

    g_return_if_fail(run & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));
    args.params = gwy_params_new_from_settings(define_module_params());
    if (!guess_signal(&args)) {
        issue_warning(gwy_app_find_window_for_channel(data, oldid));
        goto end;
    }

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    execute(&args);

    xres = gwy_brick_get_xres(args.result);
    yres = gwy_brick_get_yres(args.result);
    preview = gwy_data_field_new(xres, yres, xres, yres, FALSE);
    gwy_brick_mean_xy_plane(args.result, preview);

    if (gwy_params_get_boolean(args.params, PARAM_NEW_CHANNEL)) {
        newid = gwy_app_data_browser_add_brick(args.result, preview, data, TRUE);
        gwy_app_set_brick_title(data, oldid, _("Recalculated Data"));
        gwy_app_volume_log_add_volume(data, oldid, newid);
        gwy_app_sync_volume_items(data, data, oldid, newid, GWY_DATA_ITEM_GRADIENT, 0);
    }
    else {
        quarks[0] = gwy_app_get_brick_key_for_id(oldid);
        quarks[1] = gwy_app_get_brick_preview_key_for_id(oldid);
        gwy_app_undo_qcheckpointv(data, 2, quarks);
        gwy_container_set_object(data, quarks[0], args.result);
        gwy_container_set_object(data, quarks[1], preview);
        gwy_app_volume_log_add_volume(data, oldid, oldid);
    }

end:
    GWY_OBJECT_UNREF(preview);
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("MFM Recalculate Data"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_SIGNAL);
    gwy_param_table_set_no_reset(table, PARAM_SIGNAL, TRUE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_SPRING_CONSTANT);
    gwy_param_table_slider_set_mapping(table, PARAM_SPRING_CONSTANT, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_SPRING_CONSTANT, "N/m");
    gwy_param_table_append_slider(table, PARAM_QUALITY);
    gwy_param_table_slider_set_mapping(table, PARAM_QUALITY, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_BASE_FREQUENCY);
    gwy_param_table_slider_set_mapping(table, PARAM_BASE_FREQUENCY, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_BASE_FREQUENCY, "Hz");
    gwy_param_table_append_slider(table, PARAM_BASE_AMPLITUDE);
    gwy_param_table_slider_set_mapping(table, PARAM_BASE_AMPLITUDE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_combo(table, PARAM_RESULT);
    gwy_param_table_append_checkbox(table, PARAM_NEW_CHANNEL);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    static const MfmRecalcSignal signals[] = {
        SIGNAL_PHASE_DEG, SIGNAL_PHASE_RAD, SIGNAL_FREQUENCY, SIGNAL_AMPLITUDE_V, SIGNAL_AMPLITUDE_M,
    };

    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    MfmRecalcSignal signal = gwy_params_get_enum(params, PARAM_SIGNAL);
    gboolean is_phase = (signal == SIGNAL_PHASE_DEG || signal == SIGNAL_PHASE_RAD);
    gboolean is_freq = (signal == SIGNAL_FREQUENCY);
    gboolean is_amplitude = (signal == SIGNAL_AMPLITUDE_V || signal == SIGNAL_AMPLITUDE_M);
    guint i;

    if (id < 0 || id == PARAM_SIGNAL) {
        gwy_param_table_set_sensitive(table, PARAM_BASE_FREQUENCY, is_freq);
        gwy_param_table_set_sensitive(table, PARAM_QUALITY, is_phase || is_amplitude);
        gwy_param_table_set_sensitive(table, PARAM_BASE_AMPLITUDE, is_amplitude);
        for (i = 0; i < G_N_ELEMENTS(signals); i++)
            gwy_param_table_radio_set_sensitive(table, PARAM_SIGNAL, signals[i], signals[i] == signal);

        /* This is correct, the signal is in [m], but the user enters base amplitude in [nm]. */
        gwy_param_table_set_unitstr(table, PARAM_BASE_AMPLITUDE, signal == SIGNAL_AMPLITUDE_M ? "nm" : "V");
    }
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble spring_constant = gwy_params_get_double(params, PARAM_SPRING_CONSTANT);
    gdouble quality = gwy_params_get_double(params, PARAM_QUALITY);
    gdouble base_frequency = gwy_params_get_double(params, PARAM_BASE_FREQUENCY);
    gdouble base_amplitude = 1e-9 * gwy_params_get_double(params, PARAM_BASE_AMPLITUDE);
    MfmRecalcSignal signal = gwy_params_get_enum(params, PARAM_SIGNAL);
    GwyMFMGradientType result_type = gwy_params_get_enum(params, PARAM_RESULT);
    GwyBrick *result;

    args->result = result = gwy_brick_duplicate(args->brick);
    if (signal == SIGNAL_PHASE_DEG) {
        gwy_brick_multiply(result, M_PI/180);
        gwy_brick_mfm_phase_to_force_gradient(result, spring_constant, quality, result_type);
    }
    else if (signal == SIGNAL_PHASE_RAD)
        gwy_brick_mfm_phase_to_force_gradient(result, spring_constant, quality, result_type);
    else if (signal == SIGNAL_FREQUENCY)
        gwy_brick_mfm_frequency_shift_to_force_gradient(result, spring_constant, base_frequency, result_type);
    else if (signal == SIGNAL_AMPLITUDE_M)
        gwy_brick_mfm_amplitude_shift_to_force_gradient(result, spring_constant, quality, base_amplitude, result_type);
}

static gboolean
guess_signal(ModuleArgs *args)
{
    GwySIUnit *wunit = gwy_brick_get_si_unit_w(args->brick);
    MfmRecalcSignal guess;

    if (gwy_si_unit_equal_string(wunit, "deg"))
        guess = SIGNAL_PHASE_DEG;
    else if (gwy_si_unit_equal_string(wunit, "rad"))
        guess = SIGNAL_PHASE_RAD;
    else if (gwy_si_unit_equal_string(wunit, "Hz"))
        guess = SIGNAL_FREQUENCY;
    else if(gwy_si_unit_equal_string(wunit, "V"))
        guess = SIGNAL_AMPLITUDE_V;
    else if (gwy_si_unit_equal_string(wunit, "m"))
        guess = SIGNAL_AMPLITUDE_M;
    else
        return FALSE;

    gwy_params_set_enum(args->params, PARAM_SIGNAL, guess);
    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
