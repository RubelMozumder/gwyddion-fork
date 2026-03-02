/*
 *  $Id: volumize_layers.c 26179 2024-02-14 10:22:24Z yeti-dn $
 *  Copyright (C) 2015-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/brick.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwylayer-mask.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_XRES,
    PARAM_YRES,
    PARAM_ZRES,
    PARAM_ZREAL,
    PARAM_ZUNIT,
    PARAM_OFFSETS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyBrick *brick;
    gint *ids;
    gint nids;
    gdouble *xdata;
    gdouble *ydata;
    gdouble *zdata;
    gdouble ndata;
    GwyContainer *data;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             volumize_layers     (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static GwyBrick*        execute             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts all datafields to 3D volume data."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volumize_layers)

static gboolean
module_register(void)
{
    gwy_process_func_register("volumize_layers",
                              (GwyProcessFunc)&volumize_layers,
                              N_("/_Basic Operations/Volumize Layers..."),
                              GWY_STOCK_VOLUMIZE_LAYERS,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Convert all datafields to 3D data"));

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
    gwy_param_def_add_int(paramdef, PARAM_XRES, "xres", _("_X resolution"), 1.0, 16384.0, 100);
    gwy_param_def_add_int(paramdef, PARAM_YRES, "yres", _("_Y resolution"), 1.0, 16384.0, 100);
    gwy_param_def_add_int(paramdef, PARAM_ZRES, "zres", _("_Z resolution"), 1.0, 1000.0, 100);
    gwy_param_def_add_double(paramdef, PARAM_ZREAL, "zreal", _("Z _range"), 1e-4, 10000.0, 1e-4);
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("Z _unit"), NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_OFFSETS, "offsets", _("_Extract offsets"), FALSE);
    return paramdef;
}

static void
volumize_layers(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyDataField *preview;
    GtkWidget *dialog;
    gboolean ok = TRUE;
    gint *ids, i, nids, xres, yres, newid;
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & RUN_MODES);

    ids = gwy_app_data_browser_get_data_ids(data);

    args.params = gwy_params_new_from_settings(define_module_params());

    nids = 1;
    args.field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[0]));
    xres = gwy_data_field_get_xres(args.field);
    yres = gwy_data_field_get_yres(args.field);

    i = 0;
    while (ids[i] != -1) {
        args.field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[i]));
        if (xres != gwy_data_field_get_xres(args.field) || yres != gwy_data_field_get_yres(args.field)) {
            ok = FALSE;
            break;
        }

        i++;
        nids++;
    }

    if (!ok) {
        dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, ids[0]),
                                        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                        _("All datafields must have same resolution to make a volume from them."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        goto end;
    }

    gwy_params_set_int(args.params, PARAM_XRES, xres);
    gwy_params_set_int(args.params, PARAM_YRES, yres); //should setters be used?
    gwy_params_set_int(args.params, PARAM_ZRES, nids-1);
    args.nids = nids;
    args.ids = ids;
    args.data = data;
    args.xdata = g_new0(gdouble, nids);
    args.ydata = g_new0(gdouble, nids);
    args.zdata = g_new0(gdouble, nids);
    args.ndata = nids-1;

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    args.brick = execute(&args);

    xres = gwy_params_get_int(args.params, PARAM_XRES);
    yres = gwy_params_get_int(args.params, PARAM_YRES);
    preview = gwy_data_field_new(xres, yres, xres, yres, FALSE);
    gwy_brick_mean_xy_plane(args.brick, preview);
    newid = gwy_app_data_browser_add_brick(args.brick, preview, data, TRUE);
    g_object_unref(args.brick);
    g_object_unref(preview);
    gwy_app_volume_log_add(data, -1, newid, "proc::volumize_layers", NULL);


    if (gwy_params_get_boolean(args.params, PARAM_OFFSETS)) {
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, args.zdata, args.xdata, args.ndata);
        g_object_set(gcmodel, "description", "X offset", NULL);

        gmodel = gwy_graph_model_new();
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        g_object_set(gmodel,
                     "title", _("X offset"),
                     "axis-label-bottom", _("Slice level"),
                     "axis-label-left", _("X offset"),
                     NULL);

        gwy_graph_model_set_units_from_data_field(gmodel, args.field,
                                                  0, 0, 0, 1);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);

        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, args.zdata, args.ydata, args.ndata);
        g_object_set(gcmodel, "description", "Y offset", NULL);

        gmodel = gwy_graph_model_new();
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        g_object_set(gmodel,
                     "title", _("Y offset"),
                     "axis-label-bottom", _("Slice level"),
                     "axis-label-left", _("Y offset"),
                     NULL);

        gwy_graph_model_set_units_from_data_field(gmodel, args.field,
                                                  0, 0, 0, 1);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
     }

end:
    g_free(ids);
    g_free(args.xdata);
    g_free(args.ydata);
    g_free(args.zdata);
    gwy_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Volumize layers"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_ZRES);
    gwy_param_table_set_unitstr(table, PARAM_ZRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_ZREAL);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);
    gwy_param_table_append_checkbox(table, PARAM_OFFSETS);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_ZUNIT) {
        gint power10;
        GwySIUnit *unit = gwy_params_get_unit(params, PARAM_ZUNIT, &power10);
        GwySIValueFormat *vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, NULL);
        gwy_param_table_set_unitstr(gui->table, PARAM_ZREAL, vf->units);
        gwy_si_unit_value_format_free(vf);
    }
}

static GwyBrick*
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field;
    GwyBrick *brick = args->brick;
    GwyContainer *data = args->data;
    gint xres = gwy_params_get_int(args->params, PARAM_XRES);
    gint yres = gwy_params_get_int(args->params, PARAM_YRES);
    gint zres = gwy_params_get_int(args->params, PARAM_ZRES);
    gint nids = args->nids;
    gint *ids = args->ids;
    gint power10, i, row, col;
    gdouble zreal = gwy_params_get_double(args->params, PARAM_ZREAL);
    gdouble *ddata, *bdata;
    GwySIUnit *zunit = gwy_params_get_unit(args->params, PARAM_ZUNIT, &power10);
    gboolean offsets = gwy_params_get_boolean(args->params, PARAM_OFFSETS);

    brick = gwy_brick_new(xres, yres, nids-1,
                          gwy_data_field_get_xreal(field), gwy_data_field_get_yreal(field), zreal * pow10(power10),
                          FALSE);
    bdata = gwy_brick_get_data(brick);
    for (i = 0; i < nids-1; i++) {
        field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[i]));
        ddata = gwy_data_field_get_data(field);

        for (row = 0; row < yres; row++) {
            for (col = 0; col < xres; col++)
                bdata[col + xres*row + xres*yres*i] = ddata[col + xres*row];
        }
    }

    gwy_brick_resample(brick, xres, yres, zres, GWY_INTERPOLATION_ROUND);
    gwy_si_unit_assign(gwy_brick_get_si_unit_x(brick), gwy_data_field_get_si_unit_xy(field));
    gwy_si_unit_assign(gwy_brick_get_si_unit_y(brick), gwy_data_field_get_si_unit_xy(field));
    gwy_si_unit_assign(gwy_brick_get_si_unit_w(brick), gwy_data_field_get_si_unit_z(field));
    gwy_brick_set_si_unit_z(brick, zunit);

    if (offsets) {
        for (i = 0; i < nids-1; i++) {
            field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(ids[i]));
            args->xdata[i] = gwy_data_field_get_xoffset(field);
            args->ydata[i] = gwy_data_field_get_yoffset(field);
            args->zdata[i] = i;
        }
    }

    return brick;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
