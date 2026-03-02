/*
 *  $Id: xyz_channels.c 24749 2022-03-25 15:06:08Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PARAM_XDATA,
    PARAM_YDATA,
    PARAM_ZDATA,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwySurface *surface;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             xyz_channels        (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         xzdata_image_filter (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static gboolean         ydata_image_filter  (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts three channels to XYZ data."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

GWY_MODULE_QUERY2(module_info, xyz_channels)

static gboolean
module_register(void)
{
    gwy_process_func_register("xyz_channels",
                              (GwyProcessFunc)&xyz_channels,
                              N_("/_Basic Operations/XYZ from C_hannels..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Convert three channels to XYZ data"));

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
    gwy_param_def_add_image_id(paramdef, PARAM_XDATA, "xdata", _("_X data"));
    gwy_param_def_add_image_id(paramdef, PARAM_YDATA, "ydata", _("_Y data"));
    gwy_param_def_add_image_id(paramdef, PARAM_ZDATA, "zdata", _("_Z data"));
    return paramdef;
}

static void
xyz_channels(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.surface = gwy_surface_new();

    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_surface(args.surface, data, TRUE);
    gwy_app_xyz_log_add(data, -1, newid, "proc::xyzize", NULL);

end:
    g_object_unref(args.surface);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("XYZ Channels"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_XDATA);
    gwy_param_table_data_id_set_filter(table, PARAM_XDATA, xzdata_image_filter, args->field, NULL);
    gwy_param_table_append_image_id(table, PARAM_YDATA);
    gwy_param_table_data_id_set_filter(table, PARAM_YDATA, ydata_image_filter, args->params, NULL);
    gwy_param_table_append_image_id(table, PARAM_ZDATA);
    gwy_param_table_data_id_set_filter(table, PARAM_ZDATA, xzdata_image_filter, args->field, NULL);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id == PARAM_XDATA)
        gwy_param_table_data_id_refilter(gui->table, PARAM_YDATA);
}

static gboolean
xzdata_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *coordfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    return !gwy_data_field_check_compatibility(coordfield, field,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL);
}

/* Y cooordinates must have the same units as X. */
static gboolean
ydata_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyParams *params = (GwyParams*)user_data;
    GwyDataField *xfield = gwy_params_get_image(params, PARAM_XDATA);
    GwyDataField *yfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    return !gwy_data_field_check_compatibility(yfield, xfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *xfield = gwy_params_get_image(params, PARAM_XDATA);
    GwyDataField *yfield = gwy_params_get_image(params, PARAM_YDATA);
    GwyDataField *zfield = gwy_params_get_image(params, PARAM_ZDATA);
    GwySurface *surface = args->surface;
    const gdouble *xd, *yd, *zd;
    GwyXYZ *xyz;
    gint n, i;

    n = gwy_data_field_get_xres(xfield)*gwy_data_field_get_yres(xfield);
    gwy_surface_resize(surface, n);
    xyz = gwy_surface_get_data(surface);
    xd = gwy_data_field_get_data_const(xfield);
    yd = gwy_data_field_get_data_const(yfield);
    zd = gwy_data_field_get_data_const(zfield);
    for (i = 0; i < n; i++) {
        xyz[i].x = xd[i];
        xyz[i].y = yd[i];
        xyz[i].z = zd[i];
    }

    gwy_si_unit_assign(gwy_surface_get_si_unit_xy(surface), gwy_data_field_get_si_unit_z(xfield));
    gwy_si_unit_assign(gwy_surface_get_si_unit_z(surface), gwy_data_field_get_si_unit_z(zfield));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
