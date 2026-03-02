/*
 *  $Id: tilt.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2008-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <gtk/gtk.h>
#include <libprocess/level.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_DX,
    PARAM_DY,
    PARAM_THETA,
    PARAM_PHI,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image properties. */
    gboolean units_equal;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             tilt                (GwyContainer *data,
                                             GwyRunType run);
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
    N_("Tilts image by specified amount."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2008",
};

GWY_MODULE_QUERY2(module_info, tilt)

static gboolean
module_register(void)
{
    gwy_process_func_register("tilt",
                              (GwyProcessFunc)&tilt,
                              N_("/_Basic Operations/_Tilt..."),
                              GWY_STOCK_TILT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Tilt by specified amount"));

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
    gwy_param_def_add_double(paramdef, PARAM_DX, "dx", _("_X"), -100.0, 100.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_DY, "dy", _("_Y"), -100.0, 100.0, 0.0);
    gwy_param_def_add_angle(paramdef, PARAM_THETA, NULL, _("θ"), TRUE, 4, 0.0);
    gwy_param_def_add_angle(paramdef, PARAM_PHI, NULL, _("φ"), FALSE, 1, 0.0);
    return paramdef;
}

static void
tilt(GwyContainer *data, GwyRunType run)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark quark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.result = gwy_data_field_duplicate(args.field);
    args.units_equal = gwy_si_unit_equal(gwy_data_field_get_si_unit_z(args.field),
                                         gwy_data_field_get_si_unit_xy(args.field));

    args.params = gwy_params_new_from_settings(define_module_params());
    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_container_set_object(data, quark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const gint slope_params[] = { PARAM_DX, PARAM_DY };
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *dataview;
    GwySIUnit *unit;
    gchar *unitstr;
    gint i;

    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    unit = gwy_si_unit_new(NULL);
    gwy_si_unit_divide(gwy_data_field_get_si_unit_z(args->field), gwy_data_field_get_si_unit_xy(args->field), unit);
    unitstr = gwy_si_unit_get_string(unit, GWY_SI_UNIT_FORMAT_VFMARKUP);
    g_object_unref(unit);

    gui.dialog = gwy_dialog_new(_("Tilt"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Slopes"));
    for (i = 0; i < G_N_ELEMENTS(slope_params); i++) {
        gwy_param_table_append_slider(table, slope_params[i]);
        gwy_param_table_slider_set_steps(table, slope_params[i], 1e-4, 1e-2);
        gwy_param_table_slider_set_digits(table, slope_params[i], 6);
        gwy_param_table_set_unitstr(table, slope_params[i], unitstr);
    }

    gwy_param_table_append_header(table, -1, _("Angles"));
    if (args->units_equal) {
        gwy_param_table_append_slider(table, PARAM_THETA);
        gwy_param_table_slider_set_mapping(table, PARAM_THETA, GWY_SCALE_MAPPING_SQRT);
        gwy_param_table_slider_restrict_range(table, PARAM_THETA, 0.0, atan(G_SQRT2*100.0));
        gwy_param_table_slider_set_steps(table, PARAM_THETA, 0.01 * G_PI/180.0, 1.0 * G_PI/180.0);
        gwy_param_table_slider_set_digits(table, PARAM_THETA, 4);
    }
    gwy_param_table_append_slider(table, PARAM_PHI);
    gwy_param_table_slider_set_steps(table, PARAM_PHI, 0.01 * G_PI/180.0, 1.0 * G_PI/180.0);
    gwy_param_table_slider_set_digits(table, PARAM_PHI, 4);

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
    GwyParamTable *table = gui->table;
    GwyParams *params = gui->args->params;
    gdouble phi = gwy_params_get_double(params, PARAM_PHI);
    gdouble dx = gwy_params_get_double(params, PARAM_DX);
    gdouble dy = gwy_params_get_double(params, PARAM_DY);

    if (id < 0 || id == PARAM_DX || id == PARAM_DY) {
        gwy_param_table_set_double(table, PARAM_PHI, atan2(dy, dx));
        if (gwy_param_table_exists(table, PARAM_THETA))
            gwy_param_table_set_double(table, PARAM_THETA, atan(hypot(dx, dy)));
    }
    if (id == PARAM_PHI) {
        gwy_param_table_set_double(table, PARAM_DX, hypot(dx, dy)*cos(phi));
        gwy_param_table_set_double(table, PARAM_DY, hypot(dx, dy)*sin(phi));
    }
    if (id == PARAM_THETA) {
        gdouble tan_theta = tan(gwy_params_get_double(params, PARAM_THETA));
        gwy_param_table_set_double(table, PARAM_DX, tan_theta*cos(phi));
        gwy_param_table_set_double(table, PARAM_DY, tan_theta*sin(phi));
    }

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *result = args->result;
    GwyParams *params = args->params;
    gdouble bx, by, c;

    /* Use negative values since the module says ‘Tilt’, not ‘Remove tilt’. */
    bx = -gwy_params_get_double(params, PARAM_DX) * gwy_data_field_get_dx(field);
    by = -gwy_params_get_double(params, PARAM_DY) * gwy_data_field_get_dy(field);
    c = -0.5*(bx*gwy_data_field_get_xres(field) + by*gwy_data_field_get_yres(field));

    gwy_data_field_assign(result, field);
    gwy_data_field_plane_level(result, c, bx, by);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
