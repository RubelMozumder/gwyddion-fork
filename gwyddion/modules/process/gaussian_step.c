/*
 *  $Id: gaussian_step.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define FWHM2SIGMA (1.0/(2.0*sqrt(2*G_LN2)))

enum {
    PARAM_GAUSSIAN_FWHM,
    PARAM_UPDATE,
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
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             gaussian_step       (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Edge detection using a Gaussian step filter."),
    "Yeti <Yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, gaussian_step)

static gboolean
module_register(void)
{
    gwy_process_func_register("gaussian_step",
                              (GwyProcessFunc)&gaussian_step,
                              N_("/_Presentation/_Edge Detection/_Gaussian Step..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Detect edges using Gaussian step filter"));

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
    gwy_param_def_add_double(paramdef, PARAM_GAUSSIAN_FWHM, "gaussian-fwhm", _("_Gaussian FWHM"), 0.0, 25.0, 3.5);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
gaussian_step(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark squark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);
    g_return_if_fail(args.field && squark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &squark);
    gwy_container_set_object(data, squark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Gaussian Step Detection"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_GAUSSIAN_FWHM);
    gwy_param_table_slider_add_alt(table, PARAM_GAUSSIAN_FWHM);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_GAUSSIAN_FWHM, args->field);
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
    if (id != PARAM_UPDATE)
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
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble gaussian_fwhm = gwy_params_get_double(params, PARAM_GAUSSIAN_FWHM);
    GwyDataField *result = args->result, *field = args->field;

    gwy_data_field_copy(field, result, FALSE);
    gwy_data_field_filter_gauss_step(result, gaussian_fwhm*FWHM2SIGMA);
    gwy_data_field_normalize(result);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
