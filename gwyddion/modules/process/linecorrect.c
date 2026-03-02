/*
 *  $Id: linecorrect.c 26059 2023-12-18 17:46:59Z yeti-dn $
 *  Copyright (C) 2003-2023 David Necas (Yeti), Petr Klapetek, Luke Somers.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, lsomers@sas.upenn.edu.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correct.h>
#include <libprocess/filters.h>
#include <libprocess/linestats.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_IMMEDIATE

typedef struct {
    gdouble *a;
    gdouble *b;
    guint n;
} MedianLineData;

static gboolean module_register                    (void);
static void     line_correct_step                  (GwyContainer *data,
                                                    GwyRunType run);
static void     mark_inverted_lines                (GwyContainer *data,
                                                    GwyRunType run);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Corrects line defects (mostly experimental algorithms)."),
    "Yeti <yeti@gwyddion.net>, Luke Somers <lsomers@sas.upenn.edu>",
    "1.12",
    "David Nečas (Yeti) & Petr Klapetek & Luke Somers",
    "2004",
};

GWY_MODULE_QUERY2(module_info, linecorrect)

static gboolean
module_register(void)
{
    gwy_process_func_register("line_correct_step",
                              (GwyProcessFunc)&line_correct_step,
                              N_("/_Correct Data/Ste_p Line Correction"),
                              GWY_STOCK_LINE_LEVEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Correct steps in lines"));
    gwy_process_func_register("line_correct_inverted",
                              (GwyProcessFunc)&mark_inverted_lines,
                              N_("/_Correct Data/Mark _Inverted Rows"),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark lines with inverted sign"));

    return TRUE;
}

static void
calculate_segment_correction(const gdouble *drow,
                             gdouble *mrow,
                             gint xres,
                             gint len)
{
    const gint min_len = 4;
    gdouble corr;
    gint j;

    if (len >= min_len) {
        corr = 0.0;
        for (j = 0; j < len; j++)
            corr += (drow[j] + drow[2*xres + j])/2.0 - drow[xres + j];
        corr /= len;
        for (j = 0; j < len; j++)
            mrow[j] = (3*corr + (drow[j] + drow[2*xres + j])/2.0 - drow[xres + j])/4.0;
    }
    else {
        for (j = 0; j < len; j++)
            mrow[j] = 0.0;
    }
}

static void
line_correct_step_iter(GwyDataField *dfield,
                       GwyDataField *mask)
{
    const gdouble threshold = 3.0;
    gint xres, yres, i, j, len;
    gdouble u, v, w;
    const gdouble *d, *drow;
    gdouble *m, *mrow;

    yres = gwy_data_field_get_yres(dfield);
    xres = gwy_data_field_get_xres(dfield);
    d = gwy_data_field_get_data_const(dfield);
    m = gwy_data_field_get_data(mask);

    w = 0.0;
    for (i = 0; i < yres-1; i++) {
        drow = d + i*xres;
        for (j = 0; j < xres; j++) {
            v = drow[j + xres] - drow[j];
            w += v*v;
        }
    }
    w = w/(yres-1)/xres;

    for (i = 0; i < yres-2; i++) {
        drow = d + i*xres;
        mrow = m + (i + 1)*xres;

        for (j = 0; j < xres; j++) {
            u = drow[xres + j];
            v = (u - drow[j])*(u - drow[j + 2*xres]);
            if (G_UNLIKELY(v > threshold*w)) {
                if (2*u - drow[j] - drow[j + 2*xres] > 0)
                    mrow[j] = 1.0;
                else
                    mrow[j] = -1.0;
            }
        }

        len = 1;
        for (j = 1; j < xres; j++) {
            if (mrow[j] == mrow[j-1])
                len++;
            else {
                if (mrow[j-1])
                    calculate_segment_correction(drow + j-len, mrow + j-len, xres, len);
                len = 1;
            }
        }
        if (mrow[j-1])
            calculate_segment_correction(drow + j-len, mrow + j-len, xres, len);
    }

    gwy_data_field_sum_fields(dfield, dfield, mask);
}

static void
line_correct_step(GwyContainer *data,
                  GwyRunType run)
{
    GwyDataField *dfield, *mask;
    GwyDataLine *shifts;
    GQuark dquark;
    gdouble avg;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield && dquark);
    gwy_app_undo_qcheckpointv(data, 1, &dquark);

    avg = gwy_data_field_get_avg(dfield);
    shifts = gwy_data_field_find_row_shifts_trimmed_mean(dfield, NULL, GWY_MASK_IGNORE, 0.5, 0);
    gwy_data_field_subtract_row_shifts(dfield, shifts);
    g_object_unref(shifts);

    mask = gwy_data_field_new_alike(dfield, TRUE);
    line_correct_step_iter(dfield, mask);
    gwy_data_field_clear(mask);
    line_correct_step_iter(dfield, mask);
    g_object_unref(mask);

    gwy_data_field_filter_conservative(dfield, 5);
    gwy_data_field_add(dfield, avg - gwy_data_field_get_avg(dfield));
    gwy_data_field_data_changed(dfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

static gdouble
row_correlation(const gdouble *row1, gdouble avg1, gdouble rms1,
                const gdouble *row2, gdouble avg2, gdouble rms2,
                guint n, gdouble total_rms)
{
    gdouble s = 0.0;
    while (n--) {
        s += (*row1 - avg1)*(*row2 - avg2);
        row1++;
        row2++;
    }
    s /= (rms1*rms2 + total_rms*total_rms);
    return s;
}

static void
mark_inverted_lines(GwyContainer *data,
                    GwyRunType run)
{
    GwyDataField *dfield, *existing_mask, *mask;
    GwyDataLine *avgline, *rmsline;
    GQuark mquark;
    gdouble *weight;
    const gdouble *avg, *rms;
    gint xres, yres, i, from, j, id;
    gdouble total_rms, s, wmax;
    gboolean have_negative = FALSE, inverted;
    const gdouble *d;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_MASK_FIELD, &existing_mask,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield && mquark);

    total_rms = gwy_data_field_get_rms(dfield);
    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    if (total_rms <= 0.0 || yres < 3 || xres < 3)
        return;

    avgline = gwy_data_line_new(yres, yres, FALSE);
    gwy_data_field_get_line_stats(dfield, avgline, GWY_LINE_STAT_MEAN, GWY_ORIENTATION_HORIZONTAL);
    avg = gwy_data_line_get_data(avgline);

    rmsline = gwy_data_line_new(yres, yres, FALSE);
    gwy_data_field_get_line_stats(dfield, rmsline, GWY_LINE_STAT_RMS, GWY_ORIENTATION_HORIZONTAL);
    rms = gwy_data_line_get_data(rmsline);

    d = gwy_data_field_get_data_const(dfield);
    /* Calculate neighbour row correlations. */
    weight = g_new0(gdouble, yres-1);
    for (i = 0; i < yres-1; i++) {
        weight[i] = row_correlation(d + i*xres, avg[i], rms[i],
                                    d + (i+1)*xres, avg[i+1], rms[i+1],
                                    xres, total_rms);
        if (weight[i] < 0.0)
            have_negative = TRUE;
    }
    if (!have_negative) {
        g_object_unref(avgline);
        g_object_unref(rmsline);
        g_free(weight);
        return;
    }

    /* Find the most positively correlated block. */
    from = 0;
    for (i = 0; i < yres-2; i++) {
        if (weight[i]*weight[i+1] < 0.0) {
            s = 0.0;
            for (j = from; j <= i; j++)
                s += weight[j];
            for (j = from; j <= i; j++)
                weight[j] = s;
            from = i+1;
        }
    }
    s = 0.0;
    for (j = from; j < yres-1; j++)
        s += weight[j];
    for (j = from; j < yres-1; j++)
        weight[j] = s;

    wmax = 0.0;
    from = 0;
    for (i = 0; i < yres-1; i++) {
        if (weight[i] > wmax) {
            wmax = weight[i];
            from = i;
        }
    }

    g_object_unref(avgline);
    g_object_unref(rmsline);

    mask = gwy_data_field_new_alike(dfield, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);

    /* Extend the sign of this block downwards.  Note weight[i] represents the sign between rows i and i+1. */
    inverted = FALSE;
    for (i = from; i < yres-1; i++) {
        if (weight[i] < 0.0)
            inverted = !inverted;
        if (inverted)
            gwy_data_field_area_fill(mask, 0, i+1, xres, 1, 1.0);
    }

    /* Extend the sign of this block upwards.  Note weight[i] represents the sign between rows i and i+1. */
    inverted = FALSE;
    for (i = j = from; i >= 0; i--) {
        if (weight[i] < 0.0)
            inverted = !inverted;
        if (inverted)
            gwy_data_field_area_fill(mask, 0, i, xres, 1, 1.0);
    }

    g_free(weight);

    if (!existing_mask && gwy_data_field_get_max(mask) <= 0.0) {
        g_object_unref(mask);
        return;
    }
    gwy_app_undo_qcheckpointv(data, 1, &mquark);

    if (existing_mask) {
        gwy_data_field_copy(mask, existing_mask, FALSE);
        gwy_data_field_data_changed(existing_mask);
    }
    else {
        gwy_container_set_object(data, mquark, mask);
    }
    g_object_unref(mask);

    gwy_app_channel_log_add_proc(data, id, id);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
