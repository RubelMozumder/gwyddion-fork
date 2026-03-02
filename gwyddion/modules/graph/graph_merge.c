/*
 *  $Id: graph_merge.c 25624 2023-09-06 07:55:12Z yeti-dn $
 *  Copyright (C) 2023 David Necas (Yeti)
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
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>

static gboolean module_register(void);
static void     average        (GwyGraph *graph);
static void     merge          (GwyGraph *graph);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Merges and averages graph curves."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, graph_merge)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_merge",
                            (GwyGraphFunc)&merge,
                            N_("/_Correct Data/_Merge Curves"),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Merge data of all curves"));
    gwy_graph_func_register("graph_average",
                            (GwyGraphFunc)&average,
                            N_("/_Correct Data/_Average Curves"),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Average all curves"));

    return TRUE;
}

static GwyGraphCurveModel*
merge_average_do(GwyGraph *graph, gdouble tightness)
{
    GwyContainer *data;
    GwyGraphCurveModel *cmodel;
    GwyGraphModel *gmodel;
    const gdouble *xdata, *ydata;
    GwyXYZ *merged;
    gdouble *newxdata, *newydata;
    gint i, j, k, l, ncurves, ndata, nalldata;
    GQuark quark;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data,
                                     GWY_APP_GRAPH_MODEL_KEY, &quark,
                                     0);
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    gmodel = gwy_graph_get_model(graph);
    ncurves = gwy_graph_model_get_n_curves(gmodel);

    /* Handle the silly case specially. */
    if (ncurves == 1) {
        cmodel = gwy_graph_curve_model_duplicate(gwy_graph_model_get_curve(gmodel, 0));
        goto finish;
    }

    nalldata = 0;
    for (i = 0; i < ncurves; i++)
        nalldata += gwy_graph_curve_model_get_ndata(gwy_graph_model_get_curve(gmodel, i));

    merged = g_new(GwyXYZ, nalldata);
    for (i = k = 0; i < ncurves; i++) {
        cmodel = gwy_graph_model_get_curve(gmodel, i);
        ndata = gwy_graph_curve_model_get_ndata(cmodel);
        xdata = gwy_graph_curve_model_get_xdata(cmodel);
        ydata = gwy_graph_curve_model_get_ydata(cmodel);
        for (j = 0; j < ndata; j++) {
            merged[k].x = xdata[j];
            merged[k].y = ydata[j];
            /* Remember the minimum distance to neighbours. */
            if (ndata == 1)
                merged[k].z = 0.0;
            else if (!j)
                merged[k].z = fabs(xdata[j+1] - xdata[j]);
            else if (j == ndata-1)
                merged[k].z = fabs(xdata[j] - xdata[j-1]);
            else
                merged[k].z = fmin(fabs(xdata[j] - xdata[j-1]), fabs(xdata[j+1] - xdata[j]));

            gwy_debug("delta%d[%d] = %g", i, k, merged[k].z);
            k++;
        }
    }

    qsort(merged, nalldata, sizeof(GwyXYZ), gwy_compare_double);

    newxdata = g_new0(gdouble, nalldata);
    newydata = g_new0(gdouble, nalldata);
    i = k = 0;
    while (i < nalldata) {
        /* Greedy-merge, until we exceed given tightness. */
        for (j = i+1; j < nalldata; j++) {
            if (merged[j].x - merged[j-1].x > tightness*fmin(merged[j].z, merged[j-1].z))
                break;
            /* Never merge neighbours. */
            if (merged[j].x - merged[i].x >= fmin(merged[j].z, merged[j-1].z))
                break;
        }
        /* Fast-path single-point merges. */
        if (j == i+1) {
            gwy_debug("single point %.12g", merged[i].x);
            newxdata[k] = merged[i].x;
            newydata[k] = merged[i].y;
        }
        else {
            gwy_debug("merging %d points %.12g .. %.12g", j-i, merged[i].x, merged[j-1].x);
            for (l = i; l < j; l++) {
                newxdata[k] += merged[l].x;
                newydata[k] += merged[l].y;
            }
            newxdata[k] /= j-i;
            newydata[k] /= j-i;
        }
        k++;
        i = j;
    }
    g_free(merged);

    cmodel = gwy_graph_curve_model_new_alike(gwy_graph_model_get_curve(gmodel, 0));
    gwy_graph_curve_model_set_data(cmodel, newxdata, newydata, k);
    g_free(newxdata);
    g_free(newydata);

finish:
    gwy_graph_model_add_curve(gmodel, cmodel);
    g_object_set(cmodel, "color", gwy_graph_get_preset_color(ncurves), NULL);

    return cmodel;
}

static void
average(GwyGraph *graph)
{
    GwyGraphCurveModel *cmodel;

    cmodel = merge_average_do(graph, 0.2);
    g_object_set(cmodel, "description", _("Average curve"), NULL);
    g_object_unref(cmodel);
}

static void
merge(GwyGraph *graph)
{
    GwyGraphCurveModel *cmodel;

    cmodel = merge_average_do(graph, 1e-9);
    g_object_set(cmodel, "description", _("Merged curves"), NULL);
    g_object_unref(cmodel);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
