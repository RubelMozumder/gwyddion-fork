/*
 *  $Id: xyz_tcut.c 26355 2024-05-21 08:23:55Z yeti-dn $
 *  Copyright (C) 2016-2023 David Necas (Yeti).
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
#include <libprocess/surface.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};


enum {
    PARAM_STAT,
    PARAM_FROM,
    PARAM_TO,
};

typedef enum {
    XYZ_STAT_X = 0,
    XYZ_STAT_Y = 1,
    XYZ_STAT_Z = 2,
} XYZStatType;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    GwySurface *result;
    gint tmax;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwySelection *graph_selection;
} ModuleGUI;



static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args);
//static void             set_graph_selection     (ModuleGUI *gui);
static void             graph_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             update_graph_curve      (ModuleGUI *gui);
static void             xyztcut                 (GwyContainer *data,
                                                 GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("XYZ data crop as time series."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2023",
};

GWY_MODULE_QUERY2(module_info, xyz_tcut)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_tcut",
                          (GwyXYZFunc)&xyztcut,
                          N_("/Crop as _Time Series..."),
                          NULL,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Crop XYZ data as time series"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum stats[] = {
        { N_("X value"), XYZ_STAT_X, },
        { N_("Y value"), XYZ_STAT_Y, },
        { N_("Z value"), XYZ_STAT_Z, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_STAT, "graph", _("Graph"),
                              stats, G_N_ELEMENTS(stats), XYZ_STAT_Z);
    gwy_param_def_add_int(paramdef, PARAM_FROM, "from", _("From index"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_TO, "to", _("To index"), -1, G_MAXINT, -1);


    return paramdef;
}

static void
xyztcut(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(args.surface));

    args.params = gwy_params_new_from_settings(define_module_params());
    args.tmax = gwy_surface_get_npoints(args.surface);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome == GWY_DIALOG_PROCEED)
        execute(&args);

    newid = gwy_app_data_browser_add_surface(args.result, data, TRUE);
    gwy_app_set_surface_title(data, newid, _("Time cropped"));
    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);

    g_object_unref(args.result);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyParamTable *table;
    GtkWidget *graph, *area;
    GwyGraphCurveModel *gcmodel;

    gui.dialog = gwy_dialog_new(_("Crop XYZ Data as Time Series"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.args = args;
    gui.gmodel = gwy_graph_model_new();
    g_object_set(gui.gmodel,
                 "label-visible", FALSE,
                 NULL);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gwy_dialog_add_content(dialog, graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.graph_selection, 1);

    gui.table_options = table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_STAT);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, TRUE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.graph_selection, "changed", G_CALLBACK(graph_selection_changed), &gui);

    //set_graph_selection(&gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
//    ModuleArgs *args = gui->args;
//    GwyParams *params = args->params;

/*    if (id == PARAM_FROM || id == PARAM_TO)
        set_graph_selection(gui);
*/
    if (id < 0 || id == PARAM_STAT)
        update_graph_curve(gui);
}

/*
static void
set_graph_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble z1z2[2];

    z1z2[0] = gwy_params_get_int(args->params, PARAM_FROM);
    z1z2[1] = gwy_params_get_int(args->params, PARAM_TO);

    printf("setting selection %g %g\n", z1z2[0], z1z2[1]);

    if (z1z2[0] <= 0 && z1z2[1] >= args->tmax)
        gwy_selection_clear(gui->graph_selection);
    else
        gwy_selection_set_object(gui->graph_selection, 0, z1z2);
}
*/

static void
graph_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    gdouble z1z2[2];
    gint from = -1, to = -1;

    if (gwy_selection_get_object(selection, 0, z1z2)) {
        from = CLAMP(z1z2[0], 0, args->tmax);
        to = CLAMP(z1z2[1], 0, args->tmax);
        GWY_ORDER(gint, from, to);
        if (to - from < 2)
            from = to = -1;
    }

    if (from == -1) {
        from = 0;
        to = args->tmax;
    }
    gwy_params_set_int(args->params, PARAM_FROM, from);
    gwy_params_set_int(args->params, PARAM_TO, to);

    gwy_param_table_param_changed(gui->table_options, PARAM_FROM);
    gwy_param_table_param_changed(gui->table_options, PARAM_TO);
}

static void
update_graph_curve(ModuleGUI *gui)
{
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->gmodel, 0);
    GwyDataLine *line;
    GwySurface *surface = gui->args->surface;
    const GwyXYZ *xyz;
    gdouble *linedata;
    gint k, n, m, ndiv = 1;
    gint stat = gwy_params_get_enum(gui->args->params, PARAM_STAT);

    if (gui->args->tmax > 5000000)
        ndiv = 10000;
    else if (gui->args->tmax > 500000)
        ndiv = 1000;
    else if (gui->args->tmax > 50000)
        ndiv = 100;
    else if (gui->args->tmax > 5000)
        ndiv = 10;

    line = gwy_data_line_new(gui->args->tmax/ndiv, gui->args->tmax, FALSE);
    linedata = gwy_data_line_get_data(line);
    xyz = gwy_surface_get_data_const(surface);

    m = 0;
    for (n = 0; n < gui->args->tmax/ndiv; n++) {
        linedata[n] = 0;
        if (stat == XYZ_STAT_X)
            for (k = 0; k < ndiv; k++)
                linedata[n] += xyz[m++].x;
        else if (stat == XYZ_STAT_Y)
            for (k = 0; k < ndiv; k++)
                linedata[n] += xyz[m++].y;
        else
            for (k = 0; k < ndiv; k++)
                linedata[n] += xyz[m++].z;

         if (ndiv > 1)
             linedata[n] /= ndiv;
    }

    gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);

    g_object_unref(line);
}

static void
execute(ModuleArgs *args)
{
    GwySurface *surface = args->surface;
    GwySurface *result;
    const GwyXYZ *xyz;
    GwyXYZ *xyz_result;
    guint k, n, nres;
    gint from = gwy_params_get_int(args->params, PARAM_FROM);
    gint to = gwy_params_get_int(args->params, PARAM_TO);

    xyz = gwy_surface_get_data_const(surface);
    n = gwy_surface_get_npoints(surface);
    if (from < 0)
        from = 0;
    if (to > n)
        to = n;
    nres = to - from;

    args->result = result = gwy_surface_new_sized(nres);
    xyz_result = gwy_surface_get_data(result);
    gwy_surface_copy_units(surface, result);

    for (k = from; k < to; k++) {
        xyz_result[k-from].x = xyz[k].x;
        xyz_result[k-from].y = xyz[k].y;
        xyz_result[k-from].z = xyz[k].z;
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
