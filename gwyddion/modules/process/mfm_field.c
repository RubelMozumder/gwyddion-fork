/*
 *  $Id: mfm_field.c 25108 2022-10-19 12:33:15Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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
    PARAM_OUT,
    PARAM_PROBE,
    PARAM_WALLS,
    PARAM_HEIGHT,
    PARAM_THICKNESS,
    PARAM_SIGMA,
    PARAM_MTIP,
    PARAM_BX,
    PARAM_BY,
    PARAM_LENGTH,
    PARAM_WALL_A,
    PARAM_WALL_KN,
    PARAM_ANGLE,
    PARAM_UPDATE,
};

typedef enum {
    GWY_MFM_FIELD_OUTPUT_FIELD   = 0,
    GWY_MFM_FIELD_OUTPUT_FORCE   = 1,
    GWY_MFM_FIELD_OUTPUT_FORCE_DX = 2,
    GWY_MFM_FIELD_OUTPUT_FORCE_DDX  = 3,
    GWY_MFM_FIELD_OUTPUT_MEFF  = 4
} GwyMfmFieldOutputType;

typedef struct {
    GwyParams *params;
    GwyDataField *mask;
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
static void             mfm_field           (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             execute             (ModuleArgs *args);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simulation of magnetic field above perpendicular media"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, mfm_field)

static gboolean
module_register(void)
{
    gwy_process_func_register("mfm_field",
                              (GwyProcessFunc)&mfm_field,
                              N_("/SPM M_odes/_Magnetic/_Perpendicular Media Field..."),
                              GWY_STOCK_MFM_PERPENDICULAR,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Compute stray field above perpendicular magnetic medium"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum mfm_field_outputs[] = {
        {
            "M<sub>eff</sub>",
            GWY_MFM_FIELD_OUTPUT_MEFF,
        },
        {
            "H<sub>z</sub>",
            GWY_MFM_FIELD_OUTPUT_FIELD,
        },
        {
            "F<sub>z</sub>",
            GWY_MFM_FIELD_OUTPUT_FORCE,
        },
        {
            "dF<sub>z</sub>/dz",
            GWY_MFM_FIELD_OUTPUT_FORCE_DX,
        },
        {
            "d<sup>2</sup>F<sub>z</sub>/dz<sup>2</sup>",
            GWY_MFM_FIELD_OUTPUT_FORCE_DDX,
        },
    };
    static const GwyEnum mfm_field_probes[] = {
        { N_("Point charge"), GWY_MFM_PROBE_CHARGE, },
        { N_("Bar"),          GWY_MFM_PROBE_BAR,    },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUT, "out", _("Output _type"), mfm_field_outputs,
                              G_N_ELEMENTS(mfm_field_outputs), GWY_MFM_FIELD_OUTPUT_FIELD);
    gwy_param_def_add_gwyenum(paramdef, PARAM_PROBE, "probe", _("_Probe type"), mfm_field_probes,
                              G_N_ELEMENTS(mfm_field_probes), GWY_MFM_PROBE_CHARGE);
    gwy_param_def_add_boolean(paramdef, PARAM_WALLS, "walls", _("Include domain _walls"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Output plane height"), 0, 1000, 100);
    gwy_param_def_add_double(paramdef, PARAM_THICKNESS, "thickness", _("_Film thickness"), 0, 1000, 100);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Magnetic charge"), 1, 1000, 1);
    gwy_param_def_add_double(paramdef, PARAM_MTIP, "mtip", _("Tip _magnetization"), 1, 10000, 1);
    gwy_param_def_add_double(paramdef, PARAM_BX, "bx", _("Bar width _x"), 1, 1000, 10);
    gwy_param_def_add_double(paramdef, PARAM_BY, "by", _("Bar width _y"), 1, 1000, 10);
    gwy_param_def_add_double(paramdef, PARAM_LENGTH, "length", _("Bar length (_z)"), 1, 10000, 1000);
    gwy_param_def_add_double(paramdef, PARAM_WALL_A, "wall_a", _("_Exchange constant"), 1, 1000, 28);
    gwy_param_def_add_double(paramdef, PARAM_WALL_KN, "wall_kn", _("_Uniaxial anisotropy"), 1, 1000, 540);
    gwy_param_def_add_double(paramdef, PARAM_ANGLE, "angle", _("Cantilever _angle"), 0, 20, 0);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
mfm_field(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    gint id, newid, datano, out;
    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);
    g_return_if_fail(args.mask);
    args.result = gwy_data_field_new_alike(args.mask, TRUE);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    execute(&args);

    out = gwy_params_get_enum(args.params, PARAM_OUT);

    if (out == GWY_MFM_FIELD_OUTPUT_MEFF) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Meff");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    if (out == GWY_MFM_FIELD_OUTPUT_FIELD) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Hz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (out == GWY_MFM_FIELD_OUTPUT_FORCE) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "Fz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (out == GWY_MFM_FIELD_OUTPUT_FORCE_DX) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "dFz/dz");
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else if (out == GWY_MFM_FIELD_OUTPUT_FORCE_DDX) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

        gwy_app_set_data_field_title(data, newid, "d²Fz/dz²");
        gwy_app_channel_log_add_proc(data, id, newid);

    }

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Perpendicular Media Stray Field"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_set_unitstr(table, PARAM_HEIGHT, "nm");
    gwy_param_table_append_slider(table, PARAM_THICKNESS);
    gwy_param_table_set_unitstr(table, PARAM_THICKNESS, "nm");
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA, "kA/m");
    gwy_param_table_append_slider(table, PARAM_ANGLE);
    gwy_param_table_set_unitstr(table, PARAM_ANGLE, "deg");
    gwy_param_table_append_checkbox(table, PARAM_WALLS);
    gwy_param_table_append_slider(table, PARAM_WALL_A);
    gwy_param_table_set_unitstr(table, PARAM_WALL_A, "pJ/m");
    gwy_param_table_append_slider(table, PARAM_WALL_KN);
    gwy_param_table_set_unitstr(table, PARAM_WALL_KN, "kJ/m³");
    gwy_param_table_append_combo(table, PARAM_OUT);
    gwy_param_table_append_combo(table, PARAM_PROBE);
    gwy_param_table_append_slider(table, PARAM_MTIP);
    gwy_param_table_set_unitstr(table, PARAM_MTIP, "kA/m");
    gwy_param_table_append_slider(table, PARAM_BX);
    gwy_param_table_set_unitstr(table, PARAM_BX, "nm");
    gwy_param_table_append_slider(table, PARAM_BY);
    gwy_param_table_set_unitstr(table, PARAM_BY, "nm");
    gwy_param_table_append_slider(table, PARAM_LENGTH);
    gwy_param_table_set_unitstr(table, PARAM_LENGTH, "nm");
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    //should add some sensitivity based on PARAM_WALLS?
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
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    GwyMfmFieldOutputType out;
    GwyMFMProbeType probe;
    gboolean probe_sensitive;
    gboolean bar_sensitive;

    if (id<0 || id == PARAM_OUT) {
        out = gwy_params_get_enum(params, PARAM_OUT);
        if (out == GWY_MFM_FIELD_OUTPUT_MEFF || out == GWY_MFM_FIELD_OUTPUT_FIELD)
            probe_sensitive = FALSE;
        else
            probe_sensitive = TRUE;

        gwy_param_table_set_sensitive(table, PARAM_PROBE, probe_sensitive);
    }

    if (id<0 || id == PARAM_PROBE || id == PARAM_OUT) {
        out = gwy_params_get_enum(params, PARAM_OUT);
        probe = gwy_params_get_enum(params, PARAM_PROBE);

        if (out == GWY_MFM_FIELD_OUTPUT_MEFF || out == GWY_MFM_FIELD_OUTPUT_FIELD
            || probe != GWY_MFM_PROBE_BAR)
            bar_sensitive = FALSE;
        else
            bar_sensitive= TRUE;

        gwy_param_table_set_sensitive(table, PARAM_MTIP, bar_sensitive);
        gwy_param_table_set_sensitive(table, PARAM_BX, bar_sensitive);
        gwy_param_table_set_sensitive(table, PARAM_BY, bar_sensitive);
        gwy_param_table_set_sensitive(table, PARAM_LENGTH, bar_sensitive);
   }

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
    GwyDataField  *fz=NULL, *fza, *fzb, *fzc;
    GwyParams *params = args->params;
    gdouble dd = 1.0e-9;
    gdouble wall_delta = G_PI*sqrt(gwy_params_get_double(params, PARAM_WALL_A)*1e-12
                                   /(gwy_params_get_double(params, PARAM_WALL_KN)*1e3));
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT)*1e-9;
    gdouble thickness = gwy_params_get_double(params, PARAM_THICKNESS)*1e-9;
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA)*1e3;
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    gdouble length = gwy_params_get_double(params, PARAM_LENGTH)*1e-9;
    gdouble bx = gwy_params_get_double(params, PARAM_BX)*1e-9;
    gdouble by = gwy_params_get_double(params, PARAM_BY)*1e-9;
    gdouble mtip = gwy_params_get_double(params, PARAM_MTIP)*1e3;
    gboolean walls = gwy_params_get_boolean(params, PARAM_WALLS);
    GwyDataField *field = args->mask, *result = args->result;
    GwyMfmFieldOutputType out = gwy_params_get_enum(params, PARAM_OUT);
    GwyMFMProbeType probe = gwy_params_get_enum(params, PARAM_PROBE);

        /* FIXME: This could be done directly if we had a function equivalent to
     * gwy_data_field_mfm_perpendicular_stray_field() which calculated
     * derivatives by Z. */
    if (out == GWY_MFM_FIELD_OUTPUT_FIELD || out == GWY_MFM_FIELD_OUTPUT_MEFF || out == GWY_MFM_FIELD_OUTPUT_FORCE) {
        gwy_data_field_mfm_perpendicular_stray_field(field,
                                                     result,
                                                     height,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);

        if (angle > 0)
            gwy_data_field_mfm_perpendicular_stray_field_angle_correction(result,
                                                                          angle,
                                                                          GWY_ORIENTATION_HORIZONTAL);

        if (out == GWY_MFM_FIELD_OUTPUT_MEFF)
            gwy_data_field_multiply(result, 2.0);


        if (out == GWY_MFM_FIELD_OUTPUT_FORCE) {
            fz = gwy_data_field_new_alike(result, TRUE);
            gwy_data_field_mfm_perpendicular_medium_force(result, fz,
                                                          probe,
                                                          mtip,
                                                          bx,
                                                          by,
                                                          length);
            gwy_data_field_copy(fz, result, FALSE);
            gwy_object_unref(fz);
        }
    }
    else if (out == GWY_MFM_FIELD_OUTPUT_FORCE_DX) {
        //this is done numerically now
        fza = gwy_data_field_new_alike(result, TRUE);
        fzb = gwy_data_field_new_alike(result, TRUE);
        fz = gwy_data_field_new_alike(result, TRUE);

        gwy_data_field_mfm_perpendicular_stray_field(field, result,
                                                     height-dd,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(result, fza,
                                                      probe,
                                                      mtip,
                                                      bx,
                                                      by,
                                                      length);

        gwy_data_field_mfm_perpendicular_stray_field(field, result,
                                                     height+dd,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(result, fzb,
                                                      probe,
                                                      mtip,
                                                      bx,
                                                      by,
                                                      length);

        gwy_data_field_subtract_fields(fz, fza, fzb);
        gwy_data_field_multiply(fz, 0.5/dd);

        gwy_object_unref(fza);
        gwy_object_unref(fzb);

        gwy_data_field_copy(fz, result, FALSE);
        gwy_object_unref(fz);

    }
    else if (out == GWY_MFM_FIELD_OUTPUT_FORCE_DDX) //this is done numerically now
    {
        fza = gwy_data_field_new_alike(result, TRUE);
        fzb = gwy_data_field_new_alike(result, TRUE);
        fzc = gwy_data_field_new_alike(result, TRUE);
        fz = gwy_data_field_new_alike(result, TRUE);


        gwy_data_field_mfm_perpendicular_stray_field(field, result,
                                                     height-dd,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(result, fza,
                                                      probe,
                                                      mtip,
                                                      bx,
                                                      by,
                                                      length);

        gwy_data_field_mfm_perpendicular_stray_field(field, result,
                                                     height,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(result, fzb,
                                                      probe,
                                                      mtip,
                                                      bx,
                                                      by,
                                                      length);

        gwy_data_field_mfm_perpendicular_stray_field(field, result,
                                                     height+dd,
                                                     thickness,
                                                     sigma,
                                                     walls,
                                                     wall_delta);
        gwy_data_field_mfm_perpendicular_medium_force(result, fzc,
                                                      probe,
                                                      mtip,
                                                      bx,
                                                      by,
                                                      length);

        gwy_data_field_multiply(fzb, 2.0);
        gwy_data_field_sum_fields(fz, fza, fzc);
        gwy_data_field_subtract_fields(fz, fz, fzb);

        gwy_data_field_multiply(fz, 1.0/(dd*dd));

        gwy_object_unref(fza);
        gwy_object_unref(fzb);
        gwy_object_unref(fzc);

        gwy_data_field_copy(fz, result, FALSE);
        gwy_object_unref(fz);

    }

}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
