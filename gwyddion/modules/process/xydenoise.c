/*
 *  $Id: xydenoise.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correlation.h>
#include <libprocess/filters.h>
#include <libprocess/inttrans.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_OTHER_IMAGE,
    PARAM_DO_AVERAGE,
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

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             xydenoise           (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         other_image_filter  (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Denoises measurement on basis of two orthogonal scans."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2012",
};

GWY_MODULE_QUERY2(module_info, xydenoise)

static gboolean
module_register(void)
{
    gwy_process_func_register("xydenoise",
                              (GwyProcessFunc)&xydenoise,
                              N_("/M_ultidata/_XY Denoise..."),
                              GWY_STOCK_XY_DENOISE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Denoises horizontal/vertical measurement."));

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
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Second direction"));
    gwy_param_def_add_boolean(paramdef, PARAM_DO_AVERAGE, "do_average", _("Average denoising directions"), TRUE);
    return paramdef;
}

static void
xydenoise(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(&args);
    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
    gwy_app_set_data_field_title(data, newid, _("Denoised"));
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("XY Denoising"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);
    gwy_param_table_append_checkbox(table, PARAM_DO_AVERAGE);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_OTHER_IMAGE) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                          !gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE));
    }
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherimage, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherimage))
        return FALSE;
    if (otherimage == field)
        return FALSE;

    return !gwy_data_field_check_compatibility(field, otherimage,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *fieldx = args->field, *fieldy = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);
    GwyDataField *rx, *ix, *ry, *iy, *result = args->result, *iresult;
    gboolean do_average = gwy_params_get_boolean(args->params, PARAM_DO_AVERAGE);
    gdouble *rxdata, *rydata, *ixdata, *iydata;
    gint i, n;

    n = gwy_data_field_get_xres(fieldx) * gwy_data_field_get_yres(fieldy);
    iresult = gwy_data_field_new_alike(fieldx, TRUE);
    rx = gwy_data_field_new_alike(fieldx, TRUE);
    ix = gwy_data_field_new_alike(fieldx, TRUE);
    ry = gwy_data_field_new_alike(fieldx, TRUE);
    iy = gwy_data_field_new_alike(fieldx, TRUE);

    gwy_data_field_2dfft(fieldx, NULL, rx, ix,
                         GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_LINEAR, FALSE, 0);
    gwy_data_field_2dfft(fieldy, NULL, ry, iy,
                         GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_LINEAR, FALSE, 0);

    rxdata = gwy_data_field_get_data(rx);
    rydata = gwy_data_field_get_data(ry);
    ixdata = gwy_data_field_get_data(ix);
    iydata = gwy_data_field_get_data(iy);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            shared(n,rxdata,ixdata,rydata,iydata,do_average) \
            private(i)
#endif
    for (i = 0; i < n; i++) {
        gdouble xmodulus = sqrt(rxdata[i]*rxdata[i] + ixdata[i]*ixdata[i]);
        gdouble cosxphase = rxdata[i]/fmax(xmodulus, G_MINDOUBLE);
        gdouble sinxphase = ixdata[i]/fmax(xmodulus, G_MINDOUBLE);
        gdouble ymodulus = sqrt(rydata[i]*rydata[i] + iydata[i]*iydata[i]);
        gdouble modulus = fmin(xmodulus, ymodulus);
        if (do_average) {
            gdouble cosyphase = rydata[i]/fmax(ymodulus, G_MINDOUBLE);
            gdouble sinyphase = iydata[i]/fmax(ymodulus, G_MINDOUBLE);
            cosxphase = 0.5*(cosxphase + cosyphase);
            sinxphase = 0.5*(sinxphase + sinyphase);
        }
        rxdata[i] = modulus*cosxphase;
        ixdata[i] = modulus*sinxphase;
    }

    gwy_data_field_2dfft(rx, ix, result, iresult,
                         GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_BACKWARD, GWY_INTERPOLATION_LINEAR, FALSE, 0);

    g_object_unref(iresult);
    g_object_unref(rx);
    g_object_unref(ix);
    g_object_unref(ry);
    g_object_unref(iy);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
