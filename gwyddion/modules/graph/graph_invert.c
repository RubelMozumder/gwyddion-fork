/*
 *  $Id: graph_invert.c 24487 2021-11-07 21:05:57Z yeti-dn $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>

static gboolean module_register(void);
static void     invert          (GwyGraph *graph);
static void     invert_do       (gdouble *y,
                                 gint n);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Invert graph."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, graph_invert)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_invert",
                            (GwyGraphFunc)&invert,
                            N_("/_Basic Operations/_Invert"),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Invert graph curves"));

    return TRUE;
}

static void
invert(GwyGraph *graph)
{
    GwyContainer *data;
    GwyGraphCurveModel *cmodel;
    const gdouble *xdata, *ydata;
    GArray *newydata;
    gint i, ncurves, ndata;
    GQuark quark;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data,
                                     GWY_APP_GRAPH_MODEL_KEY, &quark,
                                     0);
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    ncurves = gwy_graph_model_get_n_curves(gwy_graph_get_model(graph));
    newydata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    for (i = 0; i < ncurves; i++) {
        cmodel = gwy_graph_model_get_curve(gwy_graph_get_model(graph), i);
        xdata = gwy_graph_curve_model_get_xdata(cmodel);
        ydata = gwy_graph_curve_model_get_ydata(cmodel);
        ndata = gwy_graph_curve_model_get_ndata(cmodel);
        g_array_set_size(newydata, 0);
        g_array_append_vals(newydata, ydata, ndata);
        invert_do((gdouble*)newydata->data, ndata);
        gwy_graph_curve_model_set_data(cmodel, xdata, (gdouble*)newydata->data,
                                       ndata);
    }
    for (i = 0; i < ncurves; i++) {
        cmodel = gwy_graph_model_get_curve(gwy_graph_get_model(graph), i);
        g_signal_emit_by_name(cmodel, "data-changed");
    }
    g_array_free(newydata, TRUE);
}

static void
invert_do(gdouble *y, gint n)
{
    gint i;

    for (i = 0; i < n; i++)
        y[i] = -y[i];
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
