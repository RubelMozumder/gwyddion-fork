/*
 *  $Id: binning.c 23766 2021-05-24 15:48:55Z yeti-dn $
 *  Copyright (C) 2017-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_BINH,
    PARAM_BINW,
    PARAM_SQUARE_BIN,
    PARAM_TRIM_HIGHEST,
    PARAM_TRIM_LOWEST,
    PARAM_TRIM_SYMM,
    PARAM_XOFF,
    PARAM_YOFF,
    PARAM_IS_SUM,
    INFO_NEWDIM,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    /* Cached values for input data field size. */
    gint xres;
    gint yres;
    gint max_binw;
    gint max_binh;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *view;
    GwyParamTable *table;
    GwyContainer *data;
    GwyDataField *binned;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             binning             (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates a smaller image using binning."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, binning)

static gboolean
module_register(void)
{
    gwy_process_func_register("binning",
                              (GwyProcessFunc)&binning,
                              N_("/_Basic Operations/_Binning..."),
                              GWY_STOCK_BINNING,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Reduce size by binning"));

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
    gwy_param_def_add_int(paramdef, PARAM_BINW, "binw", _("_Width"), 1, 32768, 3);
    gwy_param_def_add_int(paramdef, PARAM_BINH, "binh", _("_Height"), 1, 32768, 3);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE_BIN, "square_bin", _("_Square bin"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_TRIM_LOWEST, "trim_lowest", _("Trim _lowest"), 0, G_MAXINT, 0);
    gwy_param_def_add_int(paramdef, PARAM_TRIM_HIGHEST, "trim_highest", _("Trim hi_ghest"), 0, G_MAXINT, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_TRIM_SYMM, "trim_symm", _("_Trim symmetrically"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_XOFF, "xoff", _("_X offset"), 0, 32768, 0);
    gwy_param_def_add_int(paramdef, PARAM_YOFF, "yoff", _("_Y offset"), 0, 32768, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_IS_SUM, "is_sum", _("_Sum instead of averaging"), FALSE);
    return paramdef;
}

static void
binning(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *fields[3];
    GwyParams *params;
    ModuleArgs args;
    gint id, newid, binw, binh, trim_lowest, trim_highest, xoff, yoff, i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(fields[0]);
    args.field = fields[0];
    args.xres = gwy_data_field_get_xres(args.field);
    args.yres = gwy_data_field_get_yres(args.field);
    args.max_binw = (args.xres-1)/2 + 1;
    args.max_binh = (args.yres-1)/2 + 1;

    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    binw = gwy_params_get_int(params, PARAM_BINW);
    binh = gwy_params_get_int(params, PARAM_BINH);
    trim_lowest = gwy_params_get_int(params, PARAM_TRIM_LOWEST);
    trim_highest = gwy_params_get_int(params, PARAM_TRIM_HIGHEST);
    xoff = gwy_params_get_int(params, PARAM_XOFF);
    yoff = gwy_params_get_int(params, PARAM_YOFF);

    fields[0] = gwy_data_field_new_binned(fields[0], binw, binh, xoff, yoff, trim_lowest, trim_highest);
    /* Only apply is_sum to data.  We do not want to sum mask, and it is pointless to do it with presentation. */
    if (gwy_params_get_boolean(params, PARAM_IS_SUM))
        gwy_data_field_multiply(fields[0], binw*binh);
    if (fields[1]) {
        fields[1] = gwy_data_field_new_binned(fields[1], binw, binh, xoff, yoff, trim_lowest, trim_highest);
        gwy_data_field_threshold(fields[1], 0.5, 0.0, 1.0);
    }
    if (fields[2])
        fields[2] = gwy_data_field_new_binned(fields[2], binw, binh, xoff, yoff, trim_lowest, trim_highest);

    newid = gwy_app_data_browser_add_data_field(fields[0], data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    if (fields[1])
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), fields[1]);
    if (fields[2])
        gwy_container_set_object(data, gwy_app_get_show_key_for_id(newid), fields[2]);

    gwy_app_set_data_field_title(data, newid, _("Binned Data"));
    gwy_app_channel_log_add_proc(data, id, newid);

    for (i = 0; i < 3; i++)
        GWY_OBJECT_UNREF(fields[i]);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyDialogOutcome outcome;
    GtkWidget *hbox;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gui.binned = gwy_data_field_duplicate(args->field);

    gwy_container_set_object_by_name(gui.data, "/0/data", gui.binned);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Binning"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Bin Dimensions"));
    gwy_param_table_append_slider(table, PARAM_BINW);
    gwy_param_table_slider_restrict_range(table, PARAM_BINW, 1, args->max_binw);
    gwy_param_table_slider_add_alt(table, PARAM_BINW);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_BINW, args->field);
    gwy_param_table_append_slider(table, PARAM_BINH);
    gwy_param_table_slider_restrict_range(table, PARAM_BINH, 1, args->max_binh);
    gwy_param_table_slider_add_alt(table, PARAM_BINH);
    gwy_param_table_alt_set_field_pixel_y(table, PARAM_BINH, args->field);
    gwy_param_table_append_checkbox(table, PARAM_SQUARE_BIN);
    gwy_param_table_append_info(table, INFO_NEWDIM, _("New dimensions"));
    gwy_param_table_set_unitstr(table, INFO_NEWDIM, _("px"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_XOFF);
    gwy_param_table_set_unitstr(table, PARAM_XOFF, _("px"));
    gwy_param_table_append_slider(table, PARAM_YOFF);
    gwy_param_table_set_unitstr(table, PARAM_YOFF, _("px"));

    gwy_param_table_append_header(table, -1, _("Options"));

    gwy_param_table_append_slider(table, PARAM_TRIM_LOWEST);
    gwy_param_table_append_slider(table, PARAM_TRIM_HIGHEST);
    gwy_param_table_append_checkbox(table, PARAM_TRIM_SYMM);
    gwy_param_table_append_checkbox(table, PARAM_IS_SUM);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.binned);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint binw = gwy_params_get_int(params, PARAM_BINW);
    gint binh = gwy_params_get_int(params, PARAM_BINH);
    gint xoff = gwy_params_get_int(params, PARAM_XOFF);
    gint yoff = gwy_params_get_int(params, PARAM_YOFF);
    gint trim_lowest = gwy_params_get_int(params, PARAM_TRIM_LOWEST);
    gint trim_highest = gwy_params_get_int(params, PARAM_TRIM_HIGHEST);
    gboolean trim_symm = gwy_params_get_boolean(params, PARAM_TRIM_SYMM);
    gboolean square_bin = gwy_params_get_boolean(params, PARAM_SQUARE_BIN);
    gint binsize, m;
    gchar *s;

    /* Parameter constraints are pretty involved. */
    if (id < 0 || id == PARAM_SQUARE_BIN) {
        if (square_bin) {
            binw = binh = MIN(binh, binw);
            gwy_param_table_set_int(table, PARAM_BINW, binw);
            gwy_param_table_set_int(table, PARAM_BINH, binh);
            gwy_param_table_slider_restrict_range(table, PARAM_BINW, 1, MIN(args->max_binw, args->max_binh));
            gwy_param_table_slider_restrict_range(table, PARAM_BINH, 1, MIN(args->max_binw, args->max_binh));
            id = -1;
        }
        else {
            gwy_param_table_slider_restrict_range(table, PARAM_BINW, 1, args->max_binw);
            gwy_param_table_slider_restrict_range(table, PARAM_BINH, 1, args->max_binh);
        }
    }
    if ((id < 0 || id == PARAM_BINW) && square_bin)
        gwy_param_table_set_int(table, PARAM_BINH, (binh = binw));
    if ((id < 0 || id == PARAM_BINH) && square_bin)
        gwy_param_table_set_int(table, PARAM_BINW, (binw = binh));

    if (id < 0 || id == PARAM_BINW || (square_bin && id == PARAM_BINH))
        gwy_param_table_slider_restrict_range(table, PARAM_XOFF, 0, binw-1);
    if (id < 0 || id == PARAM_BINH || (square_bin && id == PARAM_BINW))
        gwy_param_table_slider_restrict_range(table, PARAM_YOFF, 0, binh-1);

    binsize = binw*binh;
    m = trim_symm ? (binsize-1)/2 : binsize-1;
    if (id < 0 || id == PARAM_BINW || id == PARAM_BINH || id == PARAM_TRIM_SYMM) {
        gwy_param_table_slider_restrict_range(table, PARAM_TRIM_LOWEST, 0, m);
        gwy_param_table_slider_restrict_range(table, PARAM_TRIM_HIGHEST, 0, m);
        if (trim_symm) {
            trim_lowest = trim_highest = MIN(trim_lowest, trim_highest);
            gwy_param_table_set_int(table, PARAM_TRIM_LOWEST, trim_lowest);
            gwy_param_table_set_int(table, PARAM_TRIM_HIGHEST, trim_highest);
        }
    }
    if (trim_symm) {
        if (id == PARAM_TRIM_LOWEST)
            gwy_param_table_set_int(table, PARAM_TRIM_HIGHEST, trim_lowest);
        if (id == PARAM_TRIM_HIGHEST)
            gwy_param_table_set_int(table, PARAM_TRIM_LOWEST, trim_highest);
    }
    else {
        if (id == PARAM_TRIM_LOWEST)
            gwy_param_table_set_int(table, PARAM_TRIM_HIGHEST, MIN(trim_highest, binsize-1 - trim_lowest));
        if (id == PARAM_TRIM_HIGHEST)
            gwy_param_table_set_int(table, PARAM_TRIM_LOWEST, MIN(trim_lowest, binsize-1 - trim_highest));
    }

    s = g_strdup_printf(_("%d × %d"), (args->xres - xoff)/binw, (args->yres - yoff)/binh);
    gwy_param_table_info_set_valuestr(table, INFO_NEWDIM, s);
    g_free(s);

    if (id != PARAM_IS_SUM)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint binw = gwy_params_get_int(params, PARAM_BINW);
    gint binh = gwy_params_get_int(params, PARAM_BINH);
    gint trim_lowest = gwy_params_get_int(params, PARAM_TRIM_LOWEST);
    gint trim_highest = gwy_params_get_int(params, PARAM_TRIM_HIGHEST);
    gint xoff = gwy_params_get_int(params, PARAM_XOFF);
    gint yoff = gwy_params_get_int(params, PARAM_YOFF);

    gwy_data_field_bin(args->field, gui->binned, binw, binh, xoff, yoff, trim_lowest, trim_highest);
    gwy_data_field_data_changed(gui->binned);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view), PREVIEW_SIZE);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint binw, binh, trim_lowest, trim_highest, xoff, yoff;

    /* Parameter constraints are pretty involved. */
    if ((binw = gwy_params_get_int(params, PARAM_BINW)) > args->max_binw)
        gwy_params_set_int(params, PARAM_BINW, (binw = args->max_binw));
    if ((binh = gwy_params_get_int(params, PARAM_BINH)) > args->max_binh)
        gwy_params_set_int(params, PARAM_BINH, (binh = args->max_binh));
    if (binh != binw)
        gwy_params_set_boolean(params, PARAM_SQUARE_BIN, FALSE);

    if ((xoff = gwy_params_get_int(params, PARAM_XOFF)) >= binw)
        gwy_params_set_int(params, PARAM_XOFF, (xoff = binw-1));
    if ((yoff = gwy_params_get_int(params, PARAM_YOFF)) >= binh)
        gwy_params_set_int(params, PARAM_YOFF, (yoff = binh-1));

    trim_lowest = gwy_params_get_int(params, PARAM_TRIM_LOWEST);
    trim_highest = gwy_params_get_int(params, PARAM_TRIM_HIGHEST);
    if (trim_lowest + trim_highest >= binw*binh) {
        gwy_params_set_int(params, PARAM_TRIM_LOWEST, (trim_lowest = 0));
        gwy_params_set_int(params, PARAM_TRIM_HIGHEST, (trim_highest = 0));
    }
    if (trim_highest != trim_lowest)
        gwy_params_set_boolean(params, PARAM_TRIM_SYMM, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
