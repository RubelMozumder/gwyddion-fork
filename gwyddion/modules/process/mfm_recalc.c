/*
 *  $Id: mfm_recalc.c 25108 2022-10-19 12:33:15Z yeti-dn $
 *  Copyright (C) 2018-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwyenum.h>
#include <libprocess/stats.h>
#include <libprocess/inttrans.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"
#include "mfmops.h"

#define MFM_RECALC_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIGNAL,
    PARAM_SPRING_CONSTANT,
    PARAM_QUALITY,
    PARAM_BASE_FREQUENCY,
    PARAM_BASE_AMPLITUDE,
    PARAM_NEW_CHANNEL,
    PARAM_RESULT
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image properties. */
//    gint goodsize;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;


typedef enum  {
    SIGNAL_PHASE_DEG = 0,
    SIGNAL_PHASE_RAD = 1,
    SIGNAL_FREQUENCY = 2,
    SIGNAL_AMPLITUDE_V = 3,
    SIGNAL_AMPLITUDE_M = 4
} MfmRecalcSignal;

/*
typedef struct {
    MfmRecalcSignal signal;
    gdouble spring_constant;
    gdouble quality;
    gdouble base_frequency;
    gdouble base_amplitude;
    gboolean new_channel;
    GwyMFMGradientType result;
} MfmRecalcArgs;

typedef struct {
    MfmRecalcArgs *args;
    GSList *signal;
    GtkObject *spring_constant;
    GtkObject *quality;
    GtkObject *base_frequency;
    GtkObject *base_amplitude;
    GtkWidget *new_channel;
    GtkWidget *result;
} MfmRecalcControls;
*/
static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mfm_recalc          (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
    static GwyDialogOutcome run_gui             (ModuleArgs *args);
/*static gboolean mfm_recalc_dialog              (MfmRecalcArgs *args,
                                                MfmRecalcSignal guess);
static void     mfm_recalc_dialog_update       (MfmRecalcControls *controls);
static void     mfm_recalc_load_args           (GwyContainer *container,
                                                MfmRecalcArgs *args);
static void     mfm_recalc_save_args           (GwyContainer *container,
                                                MfmRecalcArgs *args);
static void     mfm_recalc_sanitize_args       (MfmRecalcArgs *args);
static void     update_sensitivity             (MfmRecalcControls *controls);
static void     signal_changed                 (GtkToggleButton *toggle,
                                                MfmRecalcControls *controls);
static void     new_channel_changed            (GtkToggleButton *check,
                                                MfmRecalcControls *controls);
static void     mfm_recalc_dialog_update_values(MfmRecalcControls *controls,
                                                MfmRecalcArgs *args);*/
/*
static const MfmRecalcArgs mfm_recalc_defaults = {
    SIGNAL_PHASE_DEG, 40, 1000, 150, 0.2, TRUE, GWY_MFM_GRADIENT_MFM,
};
*/
static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts the MFM data to force gradient."),
    "Petr Klapetek <klapetek@gwyddion.net>, Robb Puttock <robb.puttock@npl.co.uk>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

GWY_MODULE_QUERY2(module_info, mfm_recalc)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_recalc",
                              (GwyProcessFunc)&mfm_recalc,
                              N_("/SPM M_odes/_Magnetic/_Recalculate to Force Gradient..."),
                              GWY_STOCK_MFM_CONVERT_TO_FORCE,
                              MFM_RECALC_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
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
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_SIGNAL, "signal", NULL,   //desc?
                              signals, G_N_ELEMENTS(signals), SIGNAL_PHASE_DEG);
    gwy_param_def_add_double(paramdef, PARAM_SPRING_CONSTANT, "spring_constant", _("_Spring constant"),
                             0.01, 1000.0, 40);
    gwy_param_def_add_double(paramdef, PARAM_QUALITY, "quality", _("_Quality factor"),
                             0.01, 10000.0, 1000);
    gwy_param_def_add_double(paramdef, PARAM_BASE_FREQUENCY, "base_frequency", _("_Base frequency"),
                             1, 1000000, 150);
    gwy_param_def_add_double(paramdef, PARAM_BASE_AMPLITUDE, "base_amplitude", _("_Base amplitude"),
                             0.01, 1000, 0.2);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_CHANNEL, "new_channel", _("_Create new image"),
                              TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_RESULT, "result", _("Result _type"),
                              results, G_N_ELEMENTS(results), GWY_MFM_GRADIENT_MFM);

    return paramdef;
}


static void   //not edited yet
issue_warning(GtkWindow *window)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(window,
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK,
                                    _("Data value units must be "
                                      "deg, rad, m, Hz or V "
                                      "for the recalculation"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
mfm_recalc(GwyContainer *data, GwyRunType runtype)
{
    /*GwyDataField *dfield, *out;
    MfmRecalcArgs args;
    gboolean ok;
    gint newid, oldid;*/
    GwySIUnit *zunit;
    GQuark quark;
    MfmRecalcSignal guess;
    GwyDialogOutcome outcome;
    ModuleArgs args;
    gint id, newid;
    gboolean new_channel;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & MFM_RECALC_RUN_MODES);
    //mfm_recalc_load_args(gwy_app_settings_get(), &args);
    /*gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     GWY_APP_DATA_FIELD_KEY, &dquark,
                                     0);*/
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     0);

    //g_return_if_fail(dfield);

    g_return_if_fail(args.field && quark);  //should && quark be here? it was not, but I thought it is strange

    //guess the units
    zunit = gwy_data_field_get_si_unit_z(args.field);
    if (gwy_si_unit_equal_string(zunit, "deg"))
        guess = SIGNAL_PHASE_DEG;
    else if (gwy_si_unit_equal_string(zunit, "rad"))
        guess = SIGNAL_PHASE_RAD;
    else if (gwy_si_unit_equal_string(zunit, "Hz"))
        guess = SIGNAL_FREQUENCY;
    else if (gwy_si_unit_equal_string(zunit, "V"))
        guess = SIGNAL_AMPLITUDE_V;
    else if (gwy_si_unit_equal_string(zunit, "m"))
        guess = SIGNAL_AMPLITUDE_M;
    else {
        issue_warning(gwy_app_find_window_for_channel(data, id));  //data is not in this version
        return;
    }

    args.params = gwy_params_new_from_settings(define_module_params());

    gwy_params_set_enum(args.params, PARAM_SIGNAL, guess);

    if (runtype == GWY_RUN_INTERACTIVE) {
        /*ok = mfm_recalc_dialog(&args, guess);
        mfm_recalc_save_args(gwy_app_settings_get(), &args);
        if (!ok)
            return;*/
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    new_channel = gwy_params_get_boolean(args.params, PARAM_NEW_CHANNEL);
    gwy_app_undo_qcheckpointv(data, 1, &quark);
    execute(&args);

    if (new_channel) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_set_data_field_title(data, newid, _("Recalculated MFM data"));
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_PALETTE, 0);
        gwy_app_channel_log_add_proc(data, id, newid);
        //g_object_unref(out);
    }
    else {
        gwy_data_field_data_changed(args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }


end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}


static GwyDialogOutcome
run_gui(ModuleArgs *args)   //add handling reset, as in older version? what about updates on parameter change?
{
    gboolean is_phase, is_freq, is_amplitude;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    gint signal;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("MFM Recalculate Data"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_SIGNAL);
    gwy_param_table_append_slider(table, PARAM_SPRING_CONSTANT);
    gwy_param_table_append_slider(table, PARAM_QUALITY);
    gwy_param_table_append_slider(table, PARAM_BASE_FREQUENCY);
    gwy_param_table_append_slider(table, PARAM_BASE_AMPLITUDE);
    gwy_param_table_append_checkbox(table, PARAM_NEW_CHANNEL);
    gwy_param_table_append_combo(table, PARAM_RESULT);

    signal = gwy_params_get_enum(args->params, PARAM_SIGNAL);

    is_phase = (signal == SIGNAL_PHASE_DEG
                || signal == SIGNAL_PHASE_RAD);
    is_freq = (signal == SIGNAL_FREQUENCY);
    is_amplitude = (signal == SIGNAL_AMPLITUDE_V
                    || signal == SIGNAL_AMPLITUDE_M);

    gwy_param_table_set_sensitive(table, PARAM_BASE_FREQUENCY, is_freq);
    gwy_param_table_set_sensitive(table, PARAM_QUALITY, is_phase || is_amplitude);
    gwy_param_table_set_sensitive(table, PARAM_BASE_AMPLITUDE, is_amplitude);
    gwy_param_table_set_sensitive(table, PARAM_SIGNAL, FALSE);
    gwy_param_table_radio_set_sensitive(table, PARAM_SIGNAL, signal, TRUE);

    //should set the displayed units, as previously (mainly nm)?

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    MfmRecalcSignal signal = gwy_params_get_enum(args->params, PARAM_SIGNAL);
    gdouble spring_constant = gwy_params_get_double(args->params, PARAM_SPRING_CONSTANT);
    gdouble quality = gwy_params_get_double(args->params, PARAM_QUALITY);
    gdouble base_frequency = gwy_params_get_double(args->params, PARAM_BASE_FREQUENCY);
    gdouble base_amplitude = gwy_params_get_double(args->params, PARAM_BASE_AMPLITUDE);
    gboolean new_channel = gwy_params_get_boolean(args->params, PARAM_NEW_CHANNEL);
    gint result_type = gwy_params_get_enum(args->params, PARAM_RESULT);  //should be gint?
    GwyDataField *field = args->field;



    if (new_channel)
        args->result = gwy_data_field_duplicate(field);
    else {
        //gwy_app_undo_qcheckpointv(data, 1, &dquark);  //checkpoint moved to mfmrecalc
        args->result = field;
    }

    if (signal == SIGNAL_PHASE_DEG) {
        gwy_data_field_mfm_phase_to_force_gradient(args->result,
                                                   spring_constant,
                                                   quality,
                                                   result_type);
        gwy_data_field_multiply(args->result, M_PI/180.0);
    }
    else if (signal == SIGNAL_PHASE_RAD) {
        gwy_data_field_mfm_phase_to_force_gradient(args->result,
                                                   spring_constant,
                                                   quality,
                                                   result_type);
    }
    else if (signal == SIGNAL_FREQUENCY) {
        gwy_data_field_mfm_frequency_shift_to_force_gradient(args->result,
                                                             spring_constant,
                                                             base_frequency,
                                                             result_type);
    }
    else if (signal == SIGNAL_AMPLITUDE_M) {
        gwy_data_field_mfm_amplitude_shift_to_force_gradient(args->result,
                                                             spring_constant,
                                                             quality,
                                                             base_amplitude*1e-9,
                                                             result_type);
    }
    else {
        g_assert_not_reached();
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
