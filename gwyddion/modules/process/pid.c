/*
 *  $Id: pid.c 24365 2021-10-13 12:45:57Z yeti-dn $
 *  Copyright (C) 2012-2021 David Necas (Yeti), Petr Klapetek.
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
#include <math.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwyddion/gwymathfallback.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_PROPORTIONAL,
    PARAM_INTEGRAL,
    PARAM_DERIVATIVE,
    PARAM_RATIO,
    PARAM_TAU,
    PARAM_FSTRENGTH,
    PARAM_FSETPOINT,
    PARAM_DISPLAY,
    PARAM_OUTPUT,
    LABEL_ERROR,
};

enum {
    COMPUTATION_OK,
    COMPUTATION_CANCELLED,
    COMPUTATION_FAILED,
};

typedef enum {
    OUTPUT_RESULT_FWD = 0,
    OUTPUT_FORCE_FWD  = 1,
    OUTPUT_RESULT_REV = 2,
    OUTPUT_FORCE_REV  = 3,
    OUTPUT_NTYPES
} PIDOutput;

enum {
    DISPLAY_DATA = OUTPUT_NTYPES,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result[OUTPUT_NTYPES];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register          (void);
static GwyParamDef*     define_module_params     (void);
static void             pid                      (GwyContainer *data,
                                                  GwyRunType runtype);
static void             warn_computation_diverged(GtkWindow *parent_window);
static gint             execute                  (ModuleArgs *args,
                                                  GtkWindow *wait_window);
static GwyDialogOutcome run_gui                  (ModuleArgs *args,
                                                  GwyContainer *data,
                                                  gint id);
static void             param_changed            (ModuleGUI *gui,
                                                  gint id);
static void             preview                  (gpointer user_data);

static const GwyEnum outputs[] = {
    { N_("PID Fwd result"),     (1 << OUTPUT_RESULT_FWD), },
    { N_("PID Fwd max. force"), (1 << OUTPUT_FORCE_FWD),  },
    { N_("PID Rev result"),     (1 << OUTPUT_RESULT_REV), },
    { N_("PID Rev max. force"), (1 << OUTPUT_FORCE_REV),  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("A simple PID simulator"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2012",
};

GWY_MODULE_QUERY2(module_info, pid)

static gboolean
module_register(void)
{
    gwy_process_func_register("pid",
                              (GwyProcessFunc)&pid,
                              N_("/SPM M_odes/_Force and Indentation/_PID Simulation..."),
                              GWY_STOCK_TIP_PID,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Simulate PID effects on measurement"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("Original _image"),    DISPLAY_DATA,      },
        { N_("PID Fwd result"),     OUTPUT_RESULT_FWD, },
        { N_("PID Fwd max. force"), OUTPUT_FORCE_FWD,  },
        { N_("PID Rev result"),     OUTPUT_RESULT_REV, },
        { N_("PID Rev max. force"), OUTPUT_FORCE_REV,  },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_PROPORTIONAL, "proportional", _("_Proportional"), 0.0, 100.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_INTEGRAL, "integral", _("_Integral"), 0.0, 100.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_DERIVATIVE, "derivative", _("_Derivative"), 0.0, 100.0, 0.0);
    gwy_param_def_add_int(paramdef, PARAM_RATIO, "ratio", _("PID/scan speed _ratio"), 1, 500, 100);
    gwy_param_def_add_int(paramdef, PARAM_TAU, "tau", _("_Integration steps"), 2, 1000, 100);
    gwy_param_def_add_double(paramdef, PARAM_FSTRENGTH, "fstrength", _("Force strength"), 0.0, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_FSETPOINT, "fsetpoint", _("Force setpoint"), 0.0, 1000.0, 10.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), DISPLAY_DATA);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output"),
                               outputs, G_N_ELEMENTS(outputs), (1 << OUTPUT_NTYPES)-1);
    return paramdef;
}

static void
pid(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;
    guint output, i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    for (i = 0; i < OUTPUT_NTYPES; i++) {
        args.result[i] = gwy_data_field_new_alike(args.field, TRUE);
        if (i == OUTPUT_FORCE_FWD || i == OUTPUT_FORCE_REV)
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result[i]), NULL);
    }

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        GtkWindow *window = gwy_app_find_window_for_channel(data, id);
        gint result = execute(&args, window);

        if (result != COMPUTATION_OK) {
            if (result == COMPUTATION_FAILED)
                warn_computation_diverged(window);
            goto end;
        }
    }

    output = gwy_params_get_flags(args.params, PARAM_OUTPUT);
    for (i = 0; i < OUTPUT_NTYPES; i++) {
        if (!((1 << i) & output))
            continue;

        newid = gwy_app_data_browser_add_data_field(args.result[i], data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, gwy_enum_to_string(1 << i, outputs, OUTPUT_NTYPES));
        gwy_app_channel_log_add_proc(data, id, newid);
    }

end:
    g_object_unref(args.params);
    for (i = 0; i < OUTPUT_NTYPES; i++)
        GWY_OBJECT_UNREF(args.result[i]);
}

static void
warn_computation_diverged(GtkWindow *parent_window)
{
    GtkWidget *dialog;

    if (!gwy_app_data_browser_get_gui_enabled() && !gwy_app_wait_get_enabled())
        return;

    dialog = gtk_message_dialog_new(parent_window,
                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                    _("Computation diverged."));
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), _("Try different parameters."));
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_NONE)
        gtk_widget_destroy(dialog);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result[0]);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("PID Simulation"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("PID Simulation"));
    gwy_param_table_append_slider(table, PARAM_PROPORTIONAL);
    gwy_param_table_append_slider(table, PARAM_INTEGRAL);
    gwy_param_table_append_slider(table, PARAM_TAU);
    gwy_param_table_append_slider(table, PARAM_DERIVATIVE);
    gwy_param_table_append_slider(table, PARAM_RATIO);
    gwy_param_table_append_slider(table, PARAM_FSTRENGTH);
    gwy_param_table_append_slider(table, PARAM_FSETPOINT);
    gwy_param_table_append_message(table, LABEL_ERROR, NULL);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);

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
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_DISPLAY) {
        gint i = gwy_params_get_enum(params, PARAM_DISPLAY);
        if (i == DISPLAY_DATA)
            gwy_container_set_object_by_name(gui->data, "/0/data", args->field);
        else
            gwy_container_set_object_by_name(gui->data, "/0/data", args->result[i]);
    }

    if (id != PARAM_DISPLAY && id != PARAM_OUTPUT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gint result = execute(gui->args, GTK_WINDOW(gui->dialog));
    guint i;

    for (i = 0; i < OUTPUT_NTYPES; i++)
        gwy_data_field_data_changed(gui->args->result[i]);

    if (result == COMPUTATION_OK) {
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
        gwy_param_table_set_label(gui->table, LABEL_ERROR, NULL);
    }
    else if (result == COMPUTATION_CANCELLED) {
        gwy_param_table_set_label(gui->table, LABEL_ERROR, _("Computation canceled."));
        gwy_param_table_message_set_type(gui->table, LABEL_ERROR, GTK_MESSAGE_INFO);
    }
    else if (result == COMPUTATION_FAILED) {
        gwy_param_table_set_label(gui->table, LABEL_ERROR, _("Computation diverged."));
        gwy_param_table_message_set_type(gui->table, LABEL_ERROR, GTK_MESSAGE_ERROR);
    }
}

static gint
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyDataField *field = args->field;
    GwyDataField *fw = args->result[OUTPUT_RESULT_FWD], *rev = args->result[OUTPUT_RESULT_REV];
    GwyDataField *ffw = args->result[OUTPUT_FORCE_FWD], *frev = args->result[OUTPUT_FORCE_REV];
    gdouble proportional = gwy_params_get_double(args->params, PARAM_PROPORTIONAL);
    gdouble integral = gwy_params_get_double(args->params, PARAM_INTEGRAL);
    gdouble derivative = gwy_params_get_double(args->params, PARAM_DERIVATIVE);
    gdouble fstrength = gwy_params_get_double(args->params, PARAM_FSTRENGTH);
    gdouble fsetpoint = gwy_params_get_double(args->params, PARAM_FSETPOINT);
    gint tau = gwy_params_get_int(args->params, PARAM_TAU);
    gint ratio = gwy_params_get_int(args->params, PARAM_RATIO);
    gint xres, yres, i, j, jnext, k, col, tcol, row, trow;
    gdouble *dfw, *dffw, *drev, *dfrev, *previous = NULL;
    const gdouble *surface;
    gdouble zpos, zrange, strength, sum, triagsum, accumulator, deltaf, force = 0.0;
    gboolean revdir, outcome = COMPUTATION_FAILED;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    gwy_data_field_clear(fw);
    gwy_data_field_clear(ffw);
    gwy_data_field_clear(rev);
    gwy_data_field_clear(frev);
    dfw = gwy_data_field_get_data(fw);
    dffw = gwy_data_field_get_data(ffw);
    drev = gwy_data_field_get_data(rev);
    dfrev = gwy_data_field_get_data(frev);
    surface = gwy_data_field_get_data_const(field);

    /* Primitive normalisation. */
    zpos = surface[0];
    accumulator = 0;
    zrange = (gwy_data_field_get_max(field) - gwy_data_field_get_min(field));
    strength = fstrength/zrange;

    /* Scan, forming forward and backward data and error signals. */
    if (!gwy_app_wait_set_message(_("Scanning...")))
        goto end;

    /* Use a cyclic buffer for the previous force values (current position indexed by j). */
    previous = g_new0(gdouble, tau+1);
    sum = triagsum = 0.0;
    j = 0;
    /* Start with one complete scan line that is then thrown away. */
    for (trow = -2; trow < 2*yres; trow++) {
        revdir = trow % 2;
        row = MAX(0, trow/2);

        for (tcol = 0; tcol < xres; tcol++) {
            if (revdir)
                col = xres-tcol-1;
            else
                col = tcol;

            k = row*xres + col;
            /* Here comes the ratio between scanning and feedback bandwidth. */
            for (i = 0; i < ratio; i++) {
                force = strength*(surface[k] - zpos);
                deltaf = force - fsetpoint;

                /* We want to calculate accumulator as sum(previous[j]*(tau-j)/tau, j=0..tau-1)/tau.
                 * This can be split into two terms, a normal moving-window sum and a moving triangular sum.
                 * Both can be updated in O(1) time by
                 *     Tnew = T + deltaf - S/tau
                 *     Snew = S + deltaf - toremovef
                 * where toremovef is the oldest force in the buffer, deltaf is the one we are just going to add. */
                jnext = (j + 1) % tau;
                triagsum += deltaf - sum/tau;
                sum += deltaf - previous[jnext];
                accumulator = triagsum/tau;

                previous[jnext] = deltaf;
                zpos += (proportional*deltaf
                         + integral*accumulator
                         + derivative*(previous[jnext] - previous[j])/ratio)*zrange;
                j = jnext;
            }
            if (gwy_isinf(zpos) || gwy_isnan(zpos) || gwy_isinf(force) || gwy_isnan(force))
                goto end;

            if (trow < 0)
                continue;

            if (!revdir) {
                dfw[k] = zpos;
                dffw[k] = force;
            }
            else {
                drev[k] = zpos;
                dfrev[k] = force;
            }
        }
        if (!gwy_app_wait_set_fraction((row + 1.0)/yres)) {
            outcome = COMPUTATION_CANCELLED;
            goto end;
        }
    }
    outcome = COMPUTATION_OK;

end:
    gwy_app_wait_finish();
    g_free(previous);

    if (outcome != COMPUTATION_OK) {
        for (i = 0; i < OUTPUT_NTYPES; i++)
            gwy_data_field_clear(args->result[i]);
    }

    return outcome;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
