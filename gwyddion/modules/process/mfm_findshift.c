/*
 *  $Id: mfm_findshift.c 25583 2023-07-31 12:07:31Z yeti-dn $
 *  Copyright (C) 2017 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "mfmops.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_START,
    PARAM_STOP,
    PARAM_OP1,
    PARAM_OP2,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field1;
    GwyDataField *field2;
    GwyDataField *result;
    gdouble minshift;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register          (void);
static GwyParamDef*     define_module_params     (void);
static void             mfm_findshift            (GwyContainer *data,
                                                  GwyRunType runtype);
static GwyDialogOutcome run_gui                  (ModuleArgs *args);
static void             execute                  (ModuleArgs *args);
static gboolean         mfm_findshift_data_filter(GwyContainer *data,
                                                  gint id,
                                                  gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Lift height difference estimation from data blur"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.3",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_findshift)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_findshift",
                              (GwyProcessFunc)&mfm_findshift,
                              N_("/SPM M_odes/_Magnetic/_Estimate Shift in Z..."),
                              GWY_STOCK_MFM_FIELD_FIND_SHIFT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Estimate lift height difference in MFM data"));

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
    gwy_param_def_add_double(paramdef, PARAM_START, "start", _("Search _from"), -1000, 1000, 10);
    gwy_param_def_add_double(paramdef, PARAM_STOP, "stop", _("Search _to"), -1000, 1000, 20);
    gwy_param_def_add_image_id(paramdef, PARAM_OP1, "op1", NULL); //maybe should be another id type
    gwy_param_def_add_image_id(paramdef, PARAM_OP2, "op2", _("Data to compare"));
    return paramdef;
}

static void
mfm_findshift(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GtkWidget *dialog;
    ModuleArgs args;
    gint newid;
    GwyAppDataId op1;
    GwyAppDataId op2;
    GQuark quark;
    GwyContainer *mydata;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field1,
                                     GWY_APP_DATA_FIELD_ID, &op1.id,
                                     GWY_APP_CONTAINER_ID, &op1.datano,
                                     0);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    op2 = gwy_params_get_data_id(args.params, PARAM_OP2);

    mydata = gwy_app_data_browser_get(op2.datano);
    quark = gwy_app_get_data_key_for_id(op2.id);
    args.field2 = GWY_DATA_FIELD(gwy_container_get_object(mydata, quark));

    execute(&args);

    dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, op1.id),
                                    GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
                                    "%s %g nm",
                                    _("Estimated shift:"), -args.minshift/1e-9);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, op1.id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

    gwy_app_set_data_field_title(data, newid, _("Shifted field difference"));
    gwy_app_channel_log_add_proc(data, op1.id, newid);

end:
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

    gui.dialog = gwy_dialog_new(_("Estimate Lift Height Shift"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OP2);
    gwy_param_table_data_id_set_filter(table, PARAM_OP2, mfm_findshift_data_filter, args->field1, NULL);
    gwy_param_table_append_slider(table, PARAM_START);
    gwy_param_table_append_slider(table, PARAM_STOP);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field1 = args->field1, *field2 = args->field2, *result;
    gdouble start = 1e-9*gwy_params_get_double(args->params, PARAM_START);
    gdouble stop = 1e-9*gwy_params_get_double(args->params, PARAM_STOP);
    gdouble minshift;

    minshift = args->minshift = gwy_data_field_mfm_find_shift_z(field1, field2, start, stop);
    result = args->result = gwy_data_field_new_alike(field1, FALSE);
    gwy_data_field_mfm_shift_z(field1, result, minshift);
    gwy_data_field_subtract_fields(result, field2, result);
}

static gboolean
mfm_findshift_data_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherfield, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
