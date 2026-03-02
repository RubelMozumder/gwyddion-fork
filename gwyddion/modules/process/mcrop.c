/*
 *  $Id: mcrop.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti), Petr Klapetek, Daniil Bratashov.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, dn2010@gmail.com
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
#include <libprocess/arithmetic.h>
#include <libprocess/correlation.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_OTHER_IMAGE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *otherimage;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean        module_register     (void);
static GwyParamDef*    define_module_params(void);
static void            mcrop               (GwyContainer *data,
                                            GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void            param_changed       (ModuleGUI *gui,
                                            gint id);
static gboolean        other_image_filter  (GwyContainer *data,
                                            gint id,
                                            gpointer user_data);
static void            execute             (ModuleArgs *args);
static void            find_score_maximum  (GwyDataField *correlation_score,
                                            gint *max_col,
                                            gint *max_row);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Crops non-intersecting regions of two images."),
    "Daniil Bratashov <dn2010@gmail.com>",
    "0.5",
    "David Nečas (Yeti) & Petr Klapetek & Daniil Bratashov",
    "2010",
};

GWY_MODULE_QUERY2(module_info, mcrop)

static gboolean
module_register(void)
{
    gwy_process_func_register("mcrop",
                              (GwyProcessFunc)&mcrop,
                              N_("/M_ultidata/Mutual C_rop..."),
                              GWY_STOCK_MUTUAL_CROP,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Crop non-intersecting regions of two images"));

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
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Second _image"));
    return paramdef;
}

static void
mcrop(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome result;
    GwyAppDataId dataid;
    GwyContainer *otherdata;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.params = gwy_params_new_from_settings(define_module_params());
    result = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (result == GWY_DIALOG_CANCEL)
        goto end;

    dataid = gwy_params_get_data_id(args.params, PARAM_OTHER_IMAGE);
    otherdata = gwy_app_data_browser_get(dataid.datano);
    /* We may act on two different files.  Undo is a bit complicated. */
    if (otherdata == data)
        gwy_app_undo_qcheckpoint(data, gwy_app_get_data_key_for_id(id), gwy_app_get_data_key_for_id(dataid.id), 0);
    else {
        gwy_app_undo_qcheckpoint(data, gwy_app_get_data_key_for_id(id), 0);
        gwy_app_undo_qcheckpoint(otherdata, gwy_app_get_data_key_for_id(dataid.id), 0);
    }
    execute(&args);
    gwy_data_field_data_changed(args.field);
    gwy_data_field_data_changed(args.otherimage);
    gwy_app_channel_log_add_proc(data, id, id);
    gwy_app_channel_log_add_proc(otherdata, dataid.id, dataid.id);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Mutual Crop"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);

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
        gboolean is_none = gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !is_none);
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
    return !gwy_data_field_check_compatibility(otherimage, field,
                                               GWY_DATA_COMPATIBILITY_MEASURE
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field1, *field2;
    GwyDataField *correlation_data, *correlation_kernel, *correlation_score;
    GdkRectangle cdata, kdata;
    gint max_col, max_row;
    gint x1l, x1r, y1t, y1b, x2l, x2r, y2t, y2b;
    gint xres1, xres2, yres1, yres2;

    field1 = args->field;
    field2 = args->otherimage = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);

    xres1 = gwy_data_field_get_xres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres1 = gwy_data_field_get_yres(field1);
    yres2 = gwy_data_field_get_yres(field2);

    if (xres1*yres1 < xres2*yres2) {
        GWY_SWAP(GwyDataField*, field1, field2);
        GWY_SWAP(gint, xres1, xres2);
        GWY_SWAP(gint, yres1, yres2);
    }

    cdata.x = 0;
    cdata.y = 0;
    cdata.width = xres1;
    cdata.height = yres1;
    kdata.width = MIN(xres2, cdata.width/3);
    kdata.height = MIN(yres2, cdata.height/3);
    kdata.x = MAX(0, xres2/2 - kdata.width/2);
    kdata.y = MAX(0, yres2/2 - kdata.height/2);

    correlation_data = gwy_data_field_area_extract(field1, cdata.x, cdata.y, cdata.width, cdata.height);
    correlation_kernel = gwy_data_field_area_extract(field2, kdata.x, kdata.y, kdata.width, kdata.height);
    correlation_score = gwy_data_field_new_alike(correlation_data, FALSE);

    /* FIXME: Add a method parameter or keep it simple? */
    gwy_data_field_correlation_search(correlation_data, correlation_kernel, NULL, correlation_score,
                                      GWY_CORR_SEARCH_COVARIANCE, 0.1, GWY_EXTERIOR_BORDER_EXTEND, 0.0);

    find_score_maximum(correlation_score, &max_col, &max_row);
    gwy_debug("c: %d %d %dx%d  k: %d %d %dx%d res: %d %d\n",
              cdata.x, cdata.y,
              cdata.width, cdata.height,
              kdata.x, kdata.y,
              kdata.width, kdata.height,
              max_col, max_row);

    x1l = MAX(0, MAX(max_col - xres1/2, max_col - xres2/2));
    y1b = MAX(0, MAX(max_row - yres1/2, max_row - yres2/2));
    x1r = MIN(xres1, MIN(max_col + xres1/2, max_col + xres2/2));
    y1t = MIN(yres1, MIN(max_row + yres1/2, max_row + yres2/2));

    x2l = MAX(0, xres2/2 - max_col);
    x2r = x2l + x1r - x1l;
    y2b = MAX(0, yres2/2 - max_row);
    y2t = y2b + y1t - y1b;

    gwy_debug("%d %d %d %d\n", x1l, y1b, x1r, y1t);
    gwy_debug("%d %d %d %d\n", x2l, y2b, x2r, y2t);

    gwy_data_field_resize(field1, x1l, y1b, x1r, y1t);
    gwy_data_field_resize(field2, x2l, y2b, x2r, y2t);

    g_object_unref(correlation_data);
    g_object_unref(correlation_kernel);
    g_object_unref(correlation_score);
}

static void
find_score_maximum(GwyDataField *correlation_score, gint *max_col, gint *max_row)
{
    gint xres = gwy_data_field_get_xres(correlation_score), yres = gwy_data_field_get_yres(correlation_score);
    gint i, n = xres*yres, maxi = 0;
    gdouble max = -G_MAXDOUBLE;
    const gdouble *data = gwy_data_field_get_data_const(correlation_score);

    for (i = 0; i < n; i++) {
        if (max < data[i]) {
            max = data[i];
            maxi = i;
        }
    }
    *max_row = maxi/xres;
    *max_col = maxi % xres;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
