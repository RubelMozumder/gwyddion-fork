/*
 *  $Id: extract_path.c 24450 2021-11-01 14:32:53Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti).
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/spline.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

enum {
    PARAM_X,
    PARAM_Y,
    PARAM_VX,
    PARAM_VY,
    LABEL_NPOINTS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    gboolean realsquare;
    GwySelection *selection;
    GwyGraphModel *gmodel_r;
    GwyGraphModel *gmodel_v;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             extract_path        (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extracts coordinates and tangents along a path selection."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, extract_path)

static gboolean
module_register(void)
{
    gwy_process_func_register("extract_path",
                              (GwyProcessFunc)&extract_path,
                              N_("/_Distortion/Extract _Path Selection..."),
                              GWY_STOCK_EXTRACT_PATH,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Extract path selection data"));

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
    gwy_param_def_add_boolean(paramdef, PARAM_X, "x", _("X position"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_Y, "y", _("Y position"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_VX, "vx", _("X tangent"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_VY, "vy", _("Y tangent"), FALSE);
    return paramdef;
}

static void
extract_path(GwyContainer *data, GwyRunType runtype)
{
    GwyAppDataId nullid = GWY_APP_DATA_ID_NONE;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gchar key[48];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPath"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.params = gwy_params_new_from_settings(define_module_params());

    g_snprintf(key, sizeof(key), "/%d/select/path", id);
    gwy_container_gis_object_by_name(data, key, &args.selection);
    gwy_container_gis_boolean(data, gwy_app_get_data_real_square_key_for_id(id), &args.realsquare);

    if (runtype == GWY_RUN_IMMEDIATE) {
        if (!args.selection)
            goto end;
    }
    else {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    execute(&args);
    if (args.gmodel_r) {
        gwy_app_add_graph_or_curves(args.gmodel_r, data, &nullid, 1);
        g_object_unref(args.gmodel_r);
    }
    if (args.gmodel_v) {
        gwy_app_add_graph_or_curves(args.gmodel_v, data, &nullid, 1);
        g_object_unref(args.gmodel_v);
    }

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;
    gchar buf[16];

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Extract Path Selection"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    if (args->selection) {
        gwy_param_table_append_info(table, LABEL_NPOINTS, _("Number of path points"));
        g_snprintf(buf, sizeof(buf), "%d", gwy_selection_get_data(args->selection, NULL));
        gwy_param_table_info_set_valuestr(table, LABEL_NPOINTS, buf);
    }
    else {
        gwy_param_table_append_message(table, LABEL_NPOINTS, _("There is no path selection."));
        gwy_param_table_message_set_type(table, LABEL_NPOINTS, GTK_MESSAGE_ERROR);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);
    }
    gwy_param_table_append_checkbox(table, PARAM_X);
    gwy_param_table_append_checkbox(table, PARAM_Y);
    gwy_param_table_append_checkbox(table, PARAM_VX);
    gwy_param_table_append_checkbox(table, PARAM_VY);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean make_x = gwy_params_get_boolean(params, PARAM_X);
    gboolean make_y = gwy_params_get_boolean(params, PARAM_Y);
    gboolean make_vx = gwy_params_get_boolean(params, PARAM_VX);
    gboolean make_vy = gwy_params_get_boolean(params, PARAM_VY);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                      (make_x || make_y || make_vx || make_vy) && args->selection);
}

static void
add_graph_curve_model(GwyGraphModel *gmodel,
                      const gdouble *xdata, const gdouble *ydata, guint ndata,
                      const gchar *description)
{
    GwyGraphCurveModel *gcmodel = gwy_graph_curve_model_new();

    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
    g_object_set(gcmodel,
                 "description", description,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(gwy_graph_model_get_n_curves(gmodel)),
                 NULL);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
}

static GwyGraphModel*
create_graph_model(const GwyXY *points, const gdouble *xdata, gdouble *ydata, guint n,
                   const gchar *xlabel, const gchar *ylabel,
                   gboolean x, gboolean y)
{
    GwyGraphModel *gmodel;
    guint i;

    if ((!x && !y) || !n)
        return NULL;

    gmodel = gwy_graph_model_new();
    g_object_set(gmodel, "axis-label-bottom", xlabel, "axis-label-left", ylabel, NULL);

    if (x) {
        for (i = 0; i < n; i++)
            ydata[i] = points[i].x;
        add_graph_curve_model(gmodel, xdata, ydata, n, "X");
    }

    if (y) {
        for (i = 0; i < n; i++)
            ydata[i] = points[i].y;
        add_graph_curve_model(gmodel, xdata, ydata, n, "Y");
    }

    return gmodel;
}

/* XXX: This replicates straighten_path.c */
static GwyXY*
rescale_points(GwySelection *selection, GwyDataField *field, gboolean realsquare,
               gdouble *pdx, gdouble *pdy, gdouble *pqx, gdouble *pqy)
{
    gdouble dx, dy, qx, qy, h;
    GwyXY *points;
    guint n, i;

    dx = gwy_data_field_get_dx(field);
    dy = gwy_data_field_get_dy(field);
    h = MIN(dx, dy);
    if (realsquare) {
        qx = h/dx;
        qy = h/dy;
        dx = dy = h;
    }
    else
        qx = qy = 1.0;

    n = gwy_selection_get_data(selection, NULL);
    points = g_new(GwyXY, n);
    for (i = 0; i < n; i++) {
        gdouble xy[2];

        gwy_selection_get_object(selection, i, xy);
        points[i].x = xy[0]/dx;
        points[i].y = xy[1]/dy;
    }

    *pdx = dx;
    *pdy = dy;
    *pqx = qx;
    *pqy = qy;
    return points;
}

static void
execute(ModuleArgs *args)
{
    gboolean make_x = gwy_params_get_boolean(args->params, PARAM_X);
    gboolean make_y = gwy_params_get_boolean(args->params, PARAM_Y);
    gboolean make_vx = gwy_params_get_boolean(args->params, PARAM_VX);
    gboolean make_vy = gwy_params_get_boolean(args->params, PARAM_VY);
    GwySelection *selection = args->selection;
    GwyGraphModel *gmodel;
    GwySpline *spline;
    GwyXY *points, *tangents;
    GwySIUnit *xyunit;
    gdouble dx, dy, qx, qy, h, l, length, slackness;
    gdouble *xdata, *ydata;
    guint n, i;
    gboolean closed;

    /* This can only be satisfied in non-interactive use.  Doing nothing is the best option in this case. */
    if (!selection || (n = gwy_selection_get_data(selection, NULL)) < 2)
        return;

    points = rescale_points(selection, args->field, args->realsquare, &dx, &dy, &qx, &qy);
    h = MIN(dx, dy);
    spline = gwy_spline_new_from_points(points, n);
    g_object_get(selection, "slackness", &slackness, "closed", &closed, NULL);
    gwy_spline_set_closed(spline, closed);
    gwy_spline_set_slackness(spline, slackness);
    g_free(points);

    length = gwy_spline_length(spline);

    /* This would give natural sampling for a straight line along some axis. */
    n = GWY_ROUND(length + 1.0);
    points = g_new(GwyXY, n);
    tangents = g_new(GwyXY, n);
    xdata = g_new(gdouble, n);
    ydata = g_new(gdouble, n);
    gwy_spline_sample_uniformly(spline, points, tangents, n);
    qx *= dx;
    qy *= dy;
    for (i = 0; i < n; i++) {
        points[i].x *= qx;
        points[i].y *= qy;
        GWY_SWAP(gdouble, tangents[i].x, tangents[i].y);
        tangents[i].x *= qx;
        tangents[i].y *= -qy;
        l = sqrt(tangents[i].x*tangents[i].x + tangents[i].y*tangents[i].y);
        if (h > 0.0) {
            tangents[i].x /= l;
            tangents[i].y /= l;
        }
        xdata[i] = i/(n - 1.0)*length*h;
    }

    xyunit = gwy_data_field_get_si_unit_xy(args->field);
    if ((gmodel = create_graph_model(points, xdata, ydata, n, _("Distance"), _("Position"), make_x, make_y))) {
        g_object_set(gmodel, "si-unit-x", xyunit, "si-unit-y", xyunit, NULL);
        args->gmodel_r = gmodel;
    }

    if ((gmodel = create_graph_model(tangents, xdata, ydata, n, _("Distance"), _("Position"), make_vx, make_vy))) {
        g_object_set(gmodel, "si-unit-x", xyunit, NULL);
        args->gmodel_v = gmodel;
    }

    g_free(ydata);
    g_free(xdata);
    g_free(points);
    g_free(tangents);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
