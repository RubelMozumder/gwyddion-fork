/*
 *  $Id: graph_align.c 25076 2022-10-11 13:48:10Z yeti-dn $
 *  Copyright (C) 2015-2017 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>

typedef struct {
    gdouble x;
    gdouble y;
} PointXY;

static gboolean module_register (void);
static void     graph_align     (GwyGraph *graph);
static void     align_two_curves(GwyGraphCurveModel *base,
                                 GwyGraphCurveModel *cmodel);
static gdouble  find_best_offset(const gdouble *a,
                                 gint na,
                                 const gdouble *b,
                                 gint nb,
                                 gint off_from,
                                 gint off_to);
static gdouble* regularise      (const PointXY *xydata,
                                 gint ndata,
                                 gdouble dx,
                                 gint *pn);
static gdouble  difference_score(const gdouble *a,
                                 gint na,
                                 const gdouble *b,
                                 gint nb,
                                 gint boff);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Aligns graph curves."),
    "Yeti <yeti@gwyddion.net>",
    "1.3",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, graph_align)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_align",
                            (GwyGraphFunc)&graph_align,
                            N_("/_Correct Data/_Align"),
                            GWY_STOCK_GRAPH_ALIGN,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Align curves"));

    return TRUE;
}

static void
graph_align(GwyGraph *graph)
{
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *cmodel, *basecmodel = NULL;
    gint i, ncurves, ndata, ndatamax = 0;
    const gdouble *xdata;
    gdouble len, maxlen = 0.0;
    GQuark quark;

    gmodel = gwy_graph_get_model(graph);
    ncurves = gwy_graph_model_get_n_curves(gmodel);
    if (ncurves < 2) {
        gwy_debug("too few curves");
        return;
    }

    for (i = 0; i < ncurves; i++) {
        cmodel = gwy_graph_model_get_curve(gmodel, i);
        ndata = gwy_graph_curve_model_get_ndata(cmodel);
        xdata = gwy_graph_curve_model_get_xdata(cmodel);
        len = xdata[ndata-1] - xdata[0];
        if (len > maxlen) {
            gwy_debug("curve %d selected as the base", i);
            basecmodel = cmodel;
            ndatamax = ndata;
            maxlen = len;
        }
    }
    g_assert(basecmodel);

    if (ndatamax < 6) {
        gwy_debug("base curve has only %d points", ndatamax);
        return;
    }

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data,
                                     GWY_APP_GRAPH_MODEL_KEY, &quark,
                                     0);
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    for (i = 0; i < ncurves; i++) {
        cmodel = gwy_graph_model_get_curve(gmodel, i);
        if (cmodel == basecmodel)
            continue;

        gwy_debug("aligning curve %d to the base", i);
        align_two_curves(basecmodel, cmodel);
        g_signal_emit_by_name(cmodel, "data-changed");
    }
}

static PointXY*
extract_xy_data(GwyGraphCurveModel *gcmodel)
{
    guint i, ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    PointXY *pts = g_new(PointXY, ndata);

    for (i = 0; i < ndata; i++) {
        pts[i].x = xdata[i];
        pts[i].y = ydata[i];
    }
    qsort(pts, ndata, sizeof(PointXY), gwy_compare_double);

    return pts;
}

static void
align_two_curves(GwyGraphCurveModel *base,
                 GwyGraphCurveModel *cmodel)
{
    PointXY *cxydata, *bxydata;
    gdouble *cline, *bline, *newcxdata, *newcydata;
    gint cndata, bndata, cn, bn, i, off_from, off_to;
    gdouble clen, dx, blen, off;
    gboolean sane_dx = TRUE;

    bndata = gwy_graph_curve_model_get_ndata(base);
    bxydata = extract_xy_data(base);

    cndata = gwy_graph_curve_model_get_ndata(cmodel);
    cxydata = extract_xy_data(cmodel);

    if (cndata < 6)
        return;

    blen = bxydata[bndata-1].x - bxydata[0].x;
    clen = cxydata[cndata-1].x - cxydata[0].x;
    /* Check if we are able to resample both curves to a common regular grid
     * without going insane. */
    dx = clen/120.0;
    if ((bxydata[bndata-1].x - bxydata[0].x)/dx > 1e5) {
        sane_dx = FALSE;
        dx = 1e5/blen;
        if (clen/dx < cndata)
            return;
    }

    bline = regularise(bxydata, bndata, dx, &bn);
    cline = regularise(cxydata, cndata, dx, &cn);

    off_from = -((2*cn + 1)/5);
    off_to = bn - (3*cn + 1)/5;
    gwy_debug("regularised base n: %d, curve n: %d", bn, cn);
    off = find_best_offset(bline, bn, cline, cn, off_from, off_to);

    g_free(bline);
    g_free(cline);

    /* Perform a second stage finer search when we have lots of points. */
    if (sane_dx && bndata > 300 && cndata > 300 && bndata + cndata > 800) {
        dx = clen/1200.0;
        if ((bxydata[bndata-1].x - bxydata[0].x)/dx < 1e5) {
            bline = regularise(bxydata, bndata, dx, &bn);
            cline = regularise(cxydata, cndata, dx, &cn);

            off_from = (gint)floor((off - 1)*10 - 1);
            off_to = (gint)ceil((off + 1)*10 + 1);
            off = find_best_offset(bline, bn, cline, cn, off_from, off_to);

            g_free(bline);
            g_free(cline);
        }
    }

    off = dx*off + (bxydata[0].x - cxydata[0].x);
    newcxdata = g_new(gdouble, cndata);
    newcydata = g_new(gdouble, cndata);
    for (i = 0; i < cndata; i++) {
        newcxdata[i] = cxydata[i].x + off;
        newcydata[i] = cxydata[i].y;
    }

    gwy_graph_curve_model_set_data(cmodel, newcxdata, newcydata, cndata);
    g_free(newcydata);
    g_free(newcxdata);
    g_free(cxydata);
    g_free(bxydata);
}

/* Generally, @a should be the longer (base) curve, @b the shorter (being
 * aligned) curve. */
static gdouble
find_best_offset(const gdouble *a, gint na,
                 const gdouble *b, gint nb,
                 gint off_from, gint off_to)
{
    gdouble scores[3] = { 0.0, 0.0, 0.0 };
    gdouble prev, score = G_MAXDOUBLE, bestscore = G_MAXDOUBLE;
    gint off, bestoff = 0;
    gdouble off0, subpixoff = 0.0;

    g_assert(nb > 4);

    off0 = 0.5*(off_from + off_to);

    gwy_debug("off range [%d, %d]", off_from, off_to);
    for (off = off_from; off <= off_to; off++) {
        gdouble t = 4.0*(off - off0)/(off_to - off_from);
        prev = score;
        score = difference_score(a, na, b, nb, off);
        score *= 1.0 + t*t;
        if (score < bestscore) {
            scores[0] = prev;
            scores[1] = score;
            bestscore = score;
            bestoff = off;
        }
        if (off == bestoff+1) {
            scores[2] = score;
        }
        //g_printerr("%d %g\n", off, score);
    }

    gwy_debug("best offset %d [%g %g %g]",
              bestoff,
              scores[0]/bestscore, scores[1]/bestscore, scores[2]/bestscore);

    if (scores[0] > scores[1] && scores[2] > scores[1]) {
        subpixoff = 0.5*(scores[0] - scores[2])/(scores[0] + scores[2]
                                                 - 2.0*scores[1]);
        gwy_debug("subpix %g", subpixoff);
    }

    return bestoff + subpixoff;
}

/* @boff is how much we move @b.  If @boff < 0 we move it to the left and
 * if @boff > 0 we move it to the right.  If @boff = 0 then @a and @b left
 * edges are aligned. */
static gdouble
difference_score(const gdouble *a, gint na,
                 const gdouble *b, gint nb,
                 gint boff)
{
    gint afrom, bfrom, len, i;
    gdouble s = 0.0;

    if (boff <= 0) {
        afrom = 0;
        bfrom = -boff;
        len = MIN(na, nb + boff);
    }
    else {
        afrom = boff;
        bfrom = 0;
        len = MIN(nb, na - afrom);
    }

    g_assert(len > 0);

    a += afrom;
    b += bfrom;
    for (i = 0; i < len; i++) {
        gdouble d = a[i] - b[i];
        s += d*d;
    }

    return s/len;
}

static gdouble*
regularise(const PointXY *xydata, gint ndata, gdouble dx, gint *pn)
{
    gint n = floor((xydata[ndata-1].x - xydata[0].x)/dx) + 1;
    gint i, ic, nzero;
    GwyDataLine *dline, *wline;
    gdouble *data, *weight;

    dline = gwy_data_line_new(n, n, TRUE);
    wline = gwy_data_line_new(n, n, TRUE);
    data = gwy_data_line_get_data(dline);
    weight = gwy_data_line_get_data(wline);
    *pn = n;

    for (ic = 0; ic < ndata; ic++) {
        gdouble x = xydata[ic].x;
        i = (gint)floor((x - xydata[0].x)/dx);
        i = CLAMP(i, 0, n-1);
        data[i] += xydata[ic].y;
        weight[i] += 1.0;
    }

    nzero = 0;
    for (i = 0; i < n; i++) {
        if (weight[i])
            data[i] /= weight[i];
        else
            nzero++;
    }
    if (!weight[0]) {
        data[0] = xydata[0].y;
        weight[0] = 1.0;
        nzero--;
    }
    if (!weight[n-1]) {
        data[n-1] = xydata[ndata-1].y;
        weight[n-1] = 1.0;
        nzero--;
    }

    if (nzero) {
        for (i = 0; i < n; i++)
            weight[i] = (weight[i] <= 0.0);
        gwy_data_line_correct_laplace(dline, wline);
    }
    data = g_memdup(data, n*sizeof(gdouble));

    g_object_unref(dline);
    g_object_unref(wline);

    return data;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
