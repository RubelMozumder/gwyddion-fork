/*
 *  $Id: correlation.c 25413 2023-06-06 09:52:30Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/simplefft.h>
#include <libprocess/inttrans.h>
#include <libprocess/filters.h>
#include <libprocess/correlation.h>
#include "gwyprocessinternal.h"
#include "gwyfftw.h"

/* Correlation iterator */
typedef struct {
    GwyComputationState cs;
    GwyDataField *data_field;
    GwyDataField *kernel_field;
    GwyDataField *score;
    GwyDataField *avg;
    GwyDataField *rms;
    gdouble kavg;
    gdouble krms;
    gint i;
    gint j;
} GwyCorrelationState;

/* Cross-correlation iterator */
typedef struct {
    GwyComputationState cs;
    GwyDataField *data_field1;
    GwyDataField *data_field2;
    GwyDataField *x_dist;
    GwyDataField *y_dist;
    GwyDataField *score;
    GwyDataField *weights;

    /* Aux data to avoid repeated computation of weighted avg and rms. */
    GwyDataField *avg1;
    GwyDataField *avg2;
    GwyDataField *rms1;
    GwyDataField *rms2;
    gint8 *have_aux1;
    gint8 *have_aux2;

    gint search_width;
    gint search_height;
    gint window_width;
    gint window_height;
    gint i;
    gint j;
} GwyCrossCorrelationState;

/**
 * get_raw_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 * @data_avg: Mean value of the effective data field area.
 * @data_rms: Mean value of the effective kernel field area.
 *
 * Calculates a raw correlation score in one point.
 *
 * See gwy_data_field_get_correlation_score() for description.  This function is useful if you know the mean values
 * and rms.
 *
 * To obtain the score, divide the returned value with the product of rms of data field area and rms of the kernel.
 *
 * Returns: Correlation score (normalized to multiple of kernel and data area rms).
 **/
static gdouble
get_raw_correlation_score(GwyDataField *data_field,
                          GwyDataField *kernel_field,
                          gint col,
                          gint row,
                          gint kernel_col,
                          gint kernel_row,
                          gint kernel_width,
                          gint kernel_height,
                          gdouble data_avg,
                          gdouble kernel_avg)
{
    gint xres, yres, kxres, kyres, i, j;
    gdouble sumpoints, score;
    const gdouble *data, *kdata, *drow, *krow;

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -1;

    /* correlation request outside data field */
    if (col < 0 || row < 0 || col + kernel_width > xres || row + kernel_height > yres)
        return -1;
    if (kernel_col < 0 || kernel_row < 0 || kernel_col + kernel_width > kxres || kernel_row + kernel_height > kyres)
        return -1;

    score = 0;
    sumpoints = kernel_width * kernel_height;
    data = data_field->data;
    kdata = kernel_field->data;
    for (i = 0; i < kernel_height; i++) {
        drow = data + col + xres*(i + row);
        krow = kdata + kernel_col + kxres*(i + kernel_row);
        for (j = 0; j < kernel_width; j++)
            score += (drow[j] - data_avg)*(krow[j] - kernel_avg);
    }
    score /= sumpoints;

    return score;
}

/**
 * get_raw_weighted_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 * @data_avg: Mean value of the effective data field area.
 * @data_rms: Mean value of the effective kernel field area.
 *
 * Calculates a raw correlation score in one point.
 *
 * See gwy_data_field_get_correlation_score() for description.  This function is useful if you know the mean values
 * and rms.
 *
 * To obtain the score, divide the returned value with the product of rms of data field area and rms of the kernel.
 *
 * Returns: Correlation score (normalized to multiple of kernel and data area rms).
 **/
static gdouble
get_raw_weighted_correlation_score(GwyDataField *data_field,
                                   GwyDataField *kernel_field,
                                   GwyDataField *weight_field,
                                   gint col,
                                   gint row,
                                   gint kernel_col,
                                   gint kernel_row,
                                   gdouble data_avg,
                                   gdouble kernel_avg,
                                   gdouble weightsum)
{
    gint xres, yres, kxres, kyres, wxres, wyres, i, j;
    const gdouble *data, *kdata, *wdata, *drow, *krow, *wrow;
    gdouble score;

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;
    wxres = weight_field->xres;
    wyres = weight_field->yres;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -1;

    /* correlation request outside data field */
    if (col < 0 || row < 0 || col + wxres > xres || row + wyres > yres)
        return -1;
    if (kernel_col < 0 || kernel_row < 0 || kernel_col + wxres > kxres || kernel_row + wyres > kyres)
        return -1;

    score = 0;
    data = data_field->data;
    kdata = kernel_field->data;
    wdata = weight_field->data;
    for (i = 0; i < wyres; i++) {
        drow = data + col + xres*(i + row);
        krow = kdata + kernel_col + kxres*(i + kernel_row);
        wrow = wdata + wxres*i;
        for (j = 0; j < wxres; j++)
            score += wrow[j] * (drow[j] - data_avg)*(krow[j] - kernel_avg);
    }
    score /= weightsum;

    return score;
}

/**
 * gwy_data_field_get_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 *
 * Calculates a correlation score in one point.
 *
 * Correlation window size is given by @kernel_col, @kernel_row, @kernel_width, @kernel_height, postion of the
 * correlation window on data is given by @col, @row.
 *
 * If anything fails (data too close to boundary, etc.), function returns -1.0 (none correlation)..
 *
 * Returns: Correlation score (between -1.0 and 1.0). Value 1.0 denotes maximum correlation, -1.0 none correlation.
 **/
gdouble
gwy_data_field_get_correlation_score(GwyDataField *data_field,
                                     GwyDataField *kernel_field,
                                     gint col,
                                     gint row,
                                     gint kernel_col,
                                     gint kernel_row,
                                     gint kernel_width,
                                     gint kernel_height)
{
    gint xres, yres, kxres, kyres, i, j;
    gdouble rms1, rms2, avg1, avg2, sumpoints, score;
    const gdouble *data, *kdata, *drow, *krow;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), -1.0);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;

    /* correlation request outside kernel */
    if (kernel_col > kxres || kernel_row > kyres)
        return -1;

    /* correlation request outside data field */
    if (col < 0 || row < 0 || col + kernel_width > xres || row + kernel_height > yres)
        return -1;
    if (kernel_col < 0 || kernel_row < 0 || kernel_col + kernel_width > kxres || kernel_row + kernel_height > kyres)
        return -1;

    avg1 = gwy_data_field_area_get_avg(data_field, NULL, col, row, kernel_width, kernel_height);
    avg2 = gwy_data_field_area_get_avg(kernel_field, NULL, kernel_col, kernel_row, kernel_width, kernel_height);

    rms1 = gwy_data_field_area_get_rms(data_field, NULL, col, row, kernel_width, kernel_height);
    if (rms1 == 0.0)
        return 0.0;
    rms2 = gwy_data_field_area_get_rms(kernel_field, NULL, kernel_col, kernel_row, kernel_width, kernel_height);
    if (rms2 == 0.0)
        return 0.0;

    score = 0;
    sumpoints = kernel_width * kernel_height;
    data = data_field->data;
    kdata = kernel_field->data;
    for (i = 0; i < kernel_height; i++) {   /* row */
        drow = data + col + xres*(i + row);
        krow = kdata + kernel_col + kxres*(i + kernel_row);
        for (j = 0; j < kernel_width; j++) {   /* col */
            score += (drow[j] - avg1) * (krow[j] - avg2);
        }
    }
    score /= rms1 * rms2 * sumpoints;

    return score;
}

static void
ensure_avg_and_rms(GwyDataField *field, GwyDataField *weight_field, gdouble weightsum,
                   gint col, gint row,
                   GwyDataField *avg_field, GwyDataField *rms_field,
                   gint8 *have_it)
{
    gint i, j, k, xres = field->xres, kernel_width = weight_field->xres, kernel_height = weight_field->yres;
    const gdouble *data = field->data, *wdata = weight_field->data, *drow, *wrow;
    gdouble avg, rms;

    k = row*xres + col;
    if (have_it[k])
        return;

    avg = 0.0;
    for (i = 0; i < kernel_height; i++) {
        drow = data + col + xres*(i + row);
        wrow = wdata + kernel_width*i;
        for (j = 0; j < kernel_width; j++)
            avg += drow[j] * wrow[j];
    }
    avg /= weightsum;

    rms = 0.0;
    for (i = 0; i < kernel_height; i++) {
        drow = data + col + xres*(i + row);
        wrow = wdata + kernel_width*i;
        for (j = 0; j < kernel_width; j++)
            rms += wrow[j]*(drow[j] - avg)*(drow[j] - avg);
    }
    rms /= weightsum;
    rms = sqrt(rms);

    avg_field->data[k] = avg;
    rms_field->data[k] = rms;
    have_it[k] = TRUE;
}

/**
 * gwy_data_field_get_weighted_correlation_score:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @weight_field: data field of same size as kernel window size
 * @col: Upper-left column position in the data field.
 * @row: Upper-left row position in the data field.
 * @kernel_col: Upper-left column position in kernel field.
 * @kernel_row: Upper-left row position in kernel field.
 * @kernel_width: Width of kernel field area.
 * @kernel_height: Heigh of kernel field area.
 *
 * Calculates a correlation score in one point using weights to center the used information to the center of kernel.
 *
 * Correlation window size is given by @kernel_col, @kernel_row, @kernel_width, @kernel_height, postion of the
 * correlation window on data is given by @col, @row.
 *
 * If anything fails (data too close to boundary, etc.), function returns -1.0 (none correlation)..
 *
 * Returns: Correlation score (between -1.0 and 1.0). Value 1.0 denotes maximum correlation, -1.0 none correlation.
 **/
gdouble
gwy_data_field_get_weighted_correlation_score(GwyDataField *data_field,
                                              GwyDataField *kernel_field,
                                              GwyDataField *weight_field,
                                              gint col,
                                              gint row,
                                              gint kernel_col,
                                              gint kernel_row,
                                              gint kernel_width,
                                              gint kernel_height)
{
    gint xres, yres, kxres, kyres, wxres, i, j;
    gdouble rms1, rms2, avg1, avg2, score, weightsum;
    const gdouble *data, *kdata, *wdata, *drow, *krow, *wrow;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), -1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(weight_field), -1.0);
    g_return_val_if_fail(kernel_width == weight_field->xres && kernel_height == weight_field->yres, -1.0);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;
    wxres = weight_field->xres;

    /* correlation request outside kernel */
    g_return_val_if_fail(kernel_col >= 0 && kernel_row >= 0, -1.0);
    g_return_val_if_fail(kernel_col + kernel_width <= kxres, -1.0);
    g_return_val_if_fail(kernel_row + kernel_height <= kyres, -1.0);

    g_return_val_if_fail(col >= 0 && row >= 0, -1.0);
    g_return_val_if_fail(col + kernel_width <= xres, -1.0);
    g_return_val_if_fail(row + kernel_height <= yres, -1.0);

    data = data_field->data;
    kdata = kernel_field->data;
    wdata = weight_field->data;
    weightsum = gwy_data_field_get_sum(weight_field);

    avg1 = avg2 = 0.0;
    for (i = 0; i < kernel_height; i++) {   /* row */
        drow = data + col + xres*(i + row);
        krow = kdata + kernel_col + kxres*(i + kernel_row);
        wrow = wdata + wxres*i;
        for (j = 0; j < kernel_width; j++) {   /* col */
            avg1 += drow[j] * wrow[j];
            avg2 += krow[j] * wrow[j];
        }
    }
    avg1 /= weightsum;
    avg2 /= weightsum;

    rms1 = rms2 = 0.0;
    for (i = 0; i < kernel_height; i++) {   /* row */
        drow = data + col + xres*(i + row);
        krow = kdata + kernel_col + kxres*(i + kernel_row);
        wrow = wdata + wxres*i;
        for (j = 0; j < kernel_width; j++) {   /* col */
            rms1 += wrow[j]*(drow[j] - avg1)*(drow[j] - avg1);
            rms2 += wrow[j]*(krow[j] - avg2)*(krow[j] - avg2);
        }
    }
    rms1 /= weightsum;
    rms2 /= weightsum;

    rms1 = sqrt(rms1);
    rms2 = sqrt(rms2);

    if (rms1 == 0.0 || rms2 == 0.0)
        return 0.0;

    score = get_raw_weighted_correlation_score(data_field, kernel_field, weight_field,
                                               col, row, kernel_col, kernel_row,
                                               avg1, avg2, weightsum);
    return score/(rms1*rms2);
}

/* Note: rms and avg must be identical and contain a copy of the original data
 * field */
static void
calculate_normalization(GwyDataField *avg,
                        GwyDataField *rms,
                        gint kernel_width,
                        gint kernel_height)
{
    GwyDataField *buffer;
    gint xres, yres, i;

    g_return_if_fail(rms->xres == avg->xres && rms->yres == avg->yres);
    xres = avg->xres;
    yres = avg->yres;

    for (i = 0; i < xres*yres; i++)
        rms->data[i] *= rms->data[i];

    buffer = gwy_data_field_new_alike(avg, FALSE);
    gwy_data_field_area_gather(rms, rms, buffer, kernel_width, kernel_height, TRUE, 0, 0, xres, yres);
    gwy_data_field_area_gather(avg, avg, buffer, kernel_width, kernel_height, TRUE, 0, 0, xres, yres);
    g_object_unref(buffer);

    for (i = 0; i < xres*yres; i++) {
        rms->data[i] -= avg->data[i]*avg->data[i];
        rms->data[i] = sqrt(MAX(rms->data[i], 0.0));
    }
}

/**
 * gwy_data_field_correlate:
 * @data_field: A data field.
 * @kernel_field: Correlation kernel.
 * @score: Data field to store correlation scores to.
 * @method: Correlation score calculation method.
 *
 * Computes correlation score for all positions in a data field.
 *
 * Correlation score is compute for all points in data field @data_field and full size of correlation kernel
 * @kernel_field.
 *
 * The points in @score correspond to centers of kernel.  More precisely, the point ((@kxres-1)/2, (@kyres-1)/2) in
 * @score corresponds to kernel field top left corner coincident with data field top left corner.  Points outside the
 * area where the kernel field fits into the data field completely are set to -1 for %GWY_CORRELATION_NORMAL.
 *
 * This function is mostly made obsolete by gwy_data_field_correlation_search() which offers, beside the plain
 * FFT-based correlation, a method equivalent to %GWY_CORRELATION_NORMAL as well as several others, all computed
 * efficiently using FFT.
 **/
void
gwy_data_field_correlate(GwyDataField *data_field, GwyDataField *kernel_field,
                         GwyDataField *score, GwyCorrelationType method)
{
    gint xres, yres, kxres, kyres, i, j, k;
    GwyDataField *data_in_re, *data_out_re, *data_out_im;
    GwyDataField *kernel_in_re, *kernel_out_re, *kernel_out_im;
    gdouble norm;

    g_return_if_fail(data_field != NULL && kernel_field != NULL);

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel_field->xres;
    kyres = kernel_field->yres;

    switch (method) {
        case GWY_CORRELATION_NORMAL:
        gwy_data_field_fill(score, -1);
        /* correlation request outside kernel */
        if (kxres > xres || kyres > yres)
            return;

        {
            GwyDataField *avg, *rms;
            gdouble s, davg, drms, kavg, krms;
            gint xoff, yoff;

            /* The number of pixels the correlation kernel extends to the
             * negative direction */
            xoff = (kxres - 1)/2;
            yoff = (kyres - 1)/2;
            kavg = gwy_data_field_get_avg(kernel_field);
            krms = gwy_data_field_get_rms(kernel_field);
            avg = gwy_data_field_duplicate(data_field);
            rms = gwy_data_field_duplicate(data_field);
            calculate_normalization(avg, rms, kxres, kyres);
            for (i = yoff; i + kyres - yoff <= yres; i++) {
                for (j = xoff; j + kxres - xoff <= xres; j++) {
                    k = i*xres + j;
                    davg = avg->data[k];
                    drms = rms->data[k];
                    if (!krms || !drms) {
                        score->data[k] = 0.0;
                        continue;
                    }
                    s = get_raw_correlation_score(data_field, kernel_field, j - xoff, i - yoff, 0, 0,
                                                  kxres, kyres, davg, kavg);
                    score->data[k] = s/(drms*krms);
                }
            }
            g_object_unref(avg);
            g_object_unref(rms);
        }
        break;

        case GWY_CORRELATION_FFT:
        case GWY_CORRELATION_POC:
        data_in_re = gwy_data_field_duplicate(data_field);
        kernel_in_re = gwy_data_field_new_alike(data_field, TRUE);
        gwy_data_field_area_copy(kernel_field, kernel_in_re, 0, 0, kxres, kyres, xres/2 - kxres/2, yres/2 - kyres/2);
        gwy_data_field_resample(score, xres, yres, GWY_INTERPOLATION_NONE);

        data_out_re = gwy_data_field_new_alike(data_in_re, TRUE);
        data_out_im = gwy_data_field_new_alike(data_in_re, TRUE);
        kernel_out_re = gwy_data_field_new_alike(data_in_re, TRUE);
        kernel_out_im = gwy_data_field_new_alike(data_in_re, TRUE);

        gwy_data_field_2dfft(data_in_re, NULL, data_out_re, data_out_im,
                             GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_BILINEAR,
                             FALSE, FALSE);
        gwy_data_field_2dfft(kernel_in_re, NULL, kernel_out_re, kernel_out_im,
                             GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_FORWARD, GWY_INTERPOLATION_BILINEAR,
                             FALSE, FALSE);

        for (i = 0; i < xres*yres; i++) {
            /*NOTE: now we construct new "complex field" from data and kernel fields, just to save memory*/
            data_in_re->data[i] = (data_out_re->data[i]*kernel_out_re->data[i]
                                   + data_out_im->data[i]*kernel_out_im->data[i]);
            kernel_in_re->data[i] = (-data_out_re->data[i]*kernel_out_im->data[i]
                                     + data_out_im->data[i]*kernel_out_re->data[i]);
            if (method == GWY_CORRELATION_POC) {
                norm = hypot(data_in_re->data[i], kernel_in_re->data[i]);
                data_in_re->data[i] /= norm;
                kernel_in_re->data[i] /= norm;
            }
        }
        gwy_data_field_2dfft(data_in_re, kernel_in_re, score, data_out_im,
                             GWY_WINDOWING_NONE, GWY_TRANSFORM_DIRECTION_BACKWARD, GWY_INTERPOLATION_BILINEAR,
                             FALSE, FALSE);
        gwy_data_field_2dfft_humanize(score);

        /*TODO compute it and put to score field*/
        g_object_unref(data_in_re);
        g_object_unref(data_out_re);
        g_object_unref(data_out_im);
        g_object_unref(kernel_in_re);
        g_object_unref(kernel_out_re);
        g_object_unref(kernel_out_im);
        break;
    }

    gwy_data_field_invalidate(score);
}

/**
 * gwy_data_field_correlate_init:
 * @data_field: A data field.
 * @kernel_field: Kernel to correlate data field with.
 * @score: Data field to store correlation scores to.
 *
 * Creates a new correlation iterator.
 *
 * This iterator reports its state as #GwyComputationStateType.
 *
 * This function is mostly made obsolete by gwy_data_field_correlation_search() which offers, beside the plain
 * FFT-based correlation, a method equivalent to %GWY_CORRELATION_NORMAL as well as several others, all computed
 * efficiently using FFT.
 *
 * Returns: A new correlation iterator.
 **/
GwyComputationState*
gwy_data_field_correlate_init(GwyDataField *data_field,
                              GwyDataField *kernel_field,
                              GwyDataField *score)
{
    GwyCorrelationState *state;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel_field), NULL);
    g_return_val_if_fail(kernel_field->xres <= data_field->xres && kernel_field->yres <= data_field->yres, NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(score), NULL);

    state = g_new0(GwyCorrelationState, 1);
    state->cs.state = GWY_COMPUTATION_STATE_INIT;
    state->cs.fraction = 0.0;
    state->data_field = g_object_ref(data_field);
    state->kernel_field = g_object_ref(kernel_field);
    state->score = g_object_ref(score);

    return (GwyComputationState*)state;
}

/**
 * gwy_data_field_correlate_iteration:
 * @state: Correlation iterator.
 *
 * Performs one iteration of correlation.
 *
 * An iterator can be created with gwy_data_field_correlate_init(). When iteration ends, either by finishing or being
 * aborted, gwy_data_field_correlate_finalize() must be called to release allocated resources.
 **/
void
gwy_data_field_correlate_iteration(GwyComputationState *cstate)
{
    GwyCorrelationState *state = (GwyCorrelationState*)cstate;
    gint xres, yres, kxres, kyres, k, xoff, yoff;
    gdouble s, davg, drms;

    xres = state->data_field->xres;
    yres = state->data_field->yres;
    kxres = state->kernel_field->xres;
    kyres = state->kernel_field->yres;
    xoff = (kxres - 1)/2;
    yoff = (kyres - 1)/2;

    if (state->cs.state == GWY_COMPUTATION_STATE_INIT) {
        gwy_data_field_fill(state->score, -1);
        state->kavg = gwy_data_field_get_avg(state->kernel_field);
        state->krms = gwy_data_field_get_rms(state->kernel_field);
        state->avg = gwy_data_field_duplicate(state->data_field);
        state->rms = gwy_data_field_duplicate(state->data_field);
        calculate_normalization(state->avg, state->rms, kxres, kyres);
        state->cs.state = GWY_COMPUTATION_STATE_ITERATE;
        state->cs.fraction = 0.0;
        state->i = yoff;
        state->j = xoff;
    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_ITERATE) {
        k = state->i*xres + state->j;
        davg = state->avg->data[k];
        drms = state->rms->data[k];
        if (drms && state->krms) {
            s = get_raw_correlation_score(state->data_field, state->kernel_field,
                                          state->j - xoff, state->i - yoff, 0, 0, kxres, kyres,
                                          davg, state->kavg);
            state->score->data[k] = s/(drms*state->krms);
        }
        else
            state->score->data[k] = 0.0;

        state->j++;
        if (state->j + kxres - xoff > xres) {
            state->j = xoff;
            state->i++;
            if (state->i + kyres - yoff > yres)
                state->cs.state = GWY_COMPUTATION_STATE_FINISHED;
        }
        state->cs.fraction += 1.0/((xres - kxres + 1)*(yres - kyres + 1));
        state->cs.fraction = MIN(state->cs.fraction, 1.0);
    }
    else if (state->cs.state == GWY_COMPUTATION_STATE_FINISHED)
        return;

    gwy_data_field_invalidate(state->score);
}

/**
 * gwy_data_field_correlate_finalize:
 * @state: Correlation iterator.
 *
 * Destroys a correlation iterator, freeing all resources.
 **/
void
gwy_data_field_correlate_finalize(GwyComputationState *cstate)
{
    GwyCorrelationState *state = (GwyCorrelationState*)cstate;

    GWY_OBJECT_UNREF(state->data_field);
    GWY_OBJECT_UNREF(state->kernel_field);
    GWY_OBJECT_UNREF(state->score);
    GWY_OBJECT_UNREF(state->avg);
    GWY_OBJECT_UNREF(state->rms);
    g_free(state);
}

/**
 * gwy_data_field_crosscorrelate:
 * @data_field1: A data field.
 * @data_field2: A data field.
 * @x_dist: A data field to store x-distances to.
 * @y_dist: A data field to store y-distances to.
 * @score: Data field to store correlation scores to.
 * @search_width: Search area width.
 * @search_height: Search area height.
 * @window_width: Correlation window width.  This parameter is not actually used.  Pass zero.
 * @window_height: Correlation window height.  This parameter is not actually used.  Pass zero.
 *
 * Algorithm for matching two different images of the same object under changes.
 *
 * It does not use any special features for matching. It simply searches for all points (with their neighbourhood) of
 * @data_field1 within @data_field2. Parameters @search_width and @search_height determine maimum area where to search
 * for points. The area is cenetered in the @data_field2 at former position of points at @data_field1.
 **/
void
gwy_data_field_crosscorrelate(GwyDataField *data_field1,
                              GwyDataField *data_field2, GwyDataField *x_dist,
                              GwyDataField *y_dist, GwyDataField *score,
                              gint search_width, gint search_height,
                              G_GNUC_UNUSED gint window_width,
                              G_GNUC_UNUSED gint window_height)
{
    gint xres, yres, i, j, m, n;
    gint imax, jmax;
    gdouble cormax, lscore;
    gdouble zm, zp, z0, ipos, jpos;

    g_return_if_fail(data_field1 != NULL && data_field2 != NULL);

    xres = data_field1->xres;
    yres = data_field1->yres;

    g_return_if_fail(xres == data_field2->xres && yres == data_field2->yres);

    gwy_data_field_clear(x_dist);
    gwy_data_field_clear(y_dist);
    gwy_data_field_clear(score);

    /*iterate over all the points */
    for (i = search_width/2; i < xres - search_width/2; i++) {
        for (j = search_height/2; j < yres - search_height/2; j++) {
            /*iterate over search area in the second datafield */
            imax = i;
            jmax = j;
            cormax = -1;
            for (m = (i - search_width); m < i; m++) {
                for (n = (j - search_height); n < j; n++) {
                    lscore = gwy_data_field_get_correlation_score(data_field1, data_field2,
                                                                  i-search_width/2, j-search_height/2, m, n,
                                                                  m + search_width, n + search_height);

                    /* add a little to score at exactly same point - to prevent problems on flat data */
                    if (m == (i - search_width/2) && n == (j - search_height/2))
                        lscore *= 1.0001;

                    if (cormax < lscore) {
                        cormax = lscore;
                        imax = m + search_width/2;
                        jmax = n + search_height/2;
                    }
                }
            }
            score->data[i + xres * j] = cormax;

            z0 = cormax;
            zm = gwy_data_field_get_correlation_score(data_field1, data_field2,
                                                      i-search_width/2, j-search_height/2,
                                                      imax - search_width/2 - 1, jmax - search_height/2,
                                                      imax + search_width/2 - 1, jmax + search_height/2);
            zp = gwy_data_field_get_correlation_score(data_field1, data_field2,
                                                      i-search_width/2, j-search_height/2,
                                                      imax - search_width/2 + 1, jmax - search_height/2,
                                                      imax + search_width/2 + 1, jmax + search_height/2);

            ipos = imax + (zm - zp)/(zm + zp - 2*z0)/2.0;
            x_dist->data[i + xres * j] = (ipos - i)*data_field1->xreal/data_field1->xres;

            zm = gwy_data_field_get_correlation_score(data_field1, data_field2,
                                                      i-search_width/2, j-search_height/2,
                                                      imax - search_width/2, jmax - search_height/2 - 1,
                                                      imax + search_width/2, jmax + search_height/2 - 1);
            zp = gwy_data_field_get_correlation_score(data_field1, data_field2,
                                                      i-search_width/2, j-search_height/2,
                                                      imax - search_width/2, jmax - search_height/2 + 1,
                                                      imax + search_width/2, jmax + search_height/2 + 1);

            jpos = jmax + (zm - zp)/(zm + zp - 2*z0)/2.0;
            y_dist->data[i + xres * j] = (jpos - j)*data_field1->yreal/data_field1->yres;
        }
    }

    gwy_data_field_invalidate(score);
    gwy_data_field_invalidate(x_dist);
    gwy_data_field_invalidate(y_dist);
}

/**
 * gwy_data_field_crosscorrelate_init:
 * @data_field1: A data field.
 * @data_field2: A data field.
 * @x_dist: A data field to store x-distances to, or %NULL.
 * @y_dist: A data field to store y-distances to, or %NULL.
 * @score: Data field to store correlation scores to, or %NULL.
 * @search_width: Search area width.
 * @search_height: Search area height.
 * @window_width: Correlation window width.
 * @window_height: Correlation window height.
 *
 * Initializes a cross-correlation iterator.
 *
 * This iterator reports its state as #GwyComputationStateType.
 *
 * Returns: A new cross-correlation iterator.
 **/
GwyComputationState*
gwy_data_field_crosscorrelate_init(GwyDataField *data_field1,
                                   GwyDataField *data_field2,
                                   GwyDataField *x_dist,
                                   GwyDataField *y_dist,
                                   GwyDataField *score,
                                   gint search_width,
                                   gint search_height,
                                   gint window_width,
                                   gint window_height)
{
    GwyCrossCorrelationState *state;
    gint xres, yres;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field1), NULL);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field2), NULL);
    xres = data_field1->xres;
    yres = data_field1->yres;
    g_return_val_if_fail(data_field2->xres == xres && data_field2->yres == yres, NULL);
    g_return_val_if_fail(!x_dist || GWY_IS_DATA_FIELD(x_dist), NULL);
    g_return_val_if_fail(!y_dist || GWY_IS_DATA_FIELD(y_dist), NULL);
    g_return_val_if_fail(!score || GWY_IS_DATA_FIELD(score), NULL);

    state = g_new0(GwyCrossCorrelationState, 1);

    state->cs.state = GWY_COMPUTATION_STATE_INIT;
    state->cs.fraction = 0.0;
    state->data_field1 = g_object_ref(data_field1);
    state->data_field2 = g_object_ref(data_field2);

    if (x_dist)
        state->x_dist = g_object_ref(x_dist);
    if (y_dist)
        state->y_dist = g_object_ref(y_dist);
    if (score)
        state->score = g_object_ref(score);

    state->avg1 = gwy_data_field_new_alike(data_field1, FALSE);
    state->rms1 = gwy_data_field_new_alike(data_field1, FALSE);
    state->avg2 = gwy_data_field_new_alike(data_field2, FALSE);
    state->rms2 = gwy_data_field_new_alike(data_field2, FALSE);
    state->have_aux1 = g_new0(gint8, xres*yres);
    state->have_aux2 = g_new0(gint8, xres*yres);

    state->search_width = search_width;
    state->search_height = search_height;
    state->window_width = window_width;
    state->window_height = window_height;

    state->weights = gwy_data_field_new(window_width, window_height, window_width, window_height, TRUE);
    gwy_data_field_fill(state->weights, 1.0);

    return (GwyComputationState*)state;
}

/**
 * gwy_data_field_crosscorrelate_set_weights:
 * @state: Cross-correlation iterator.
 * @type: Set windowing type to be set as correlation weight, see #GwyWindowingType for details.
 *
 * Sets the weight function to be used within iterative cross-correlation algorithm.
 *
 * By default (not setting it), rectangular windowing is used. This function should be called before running first
 * iteration to get consistent results.
 **/
void
gwy_data_field_crosscorrelate_set_weights(GwyComputationState *state,
                                          GwyWindowingType type)
{
    GwyCrossCorrelationState *cstate = (GwyCrossCorrelationState*)state;
    g_return_if_fail(cstate->cs.state == GWY_COMPUTATION_STATE_INIT);

    gwy_data_field_fill(cstate->weights, 1.0);
    gwy_data_field_fft_window(cstate->weights, type);
}

/**
 * gwy_data_field_crosscorrelate_iteration:
 * @state: Cross-correlation iterator.
 *
 * Performs one iteration of cross-correlation.
 *
 * Cross-correlation matches two different images of the same object under changes.
 *
 * It does not use any special features for matching. It simply searches for all points (with their neighbourhood) of
 * @data_field1 within @data_field2. Parameters @search_width and @search_height determine maimum area where to search
 * for points. The area is cenetered in the @data_field2 at former position of points at @data_field1.
 *
 * A cross-correlation iterator can be created with gwy_data_field_crosscorrelate_init().  When iteration ends, either
 * by finishing or being aborted, gwy_data_field_crosscorrelate_finalize() must be called to release allocated
 * resources.
 **/
void
gwy_data_field_crosscorrelate_iteration(GwyComputationState *cstate)
{
    GwyCrossCorrelationState *state = (GwyCrossCorrelationState*)cstate;
    GwyDataField *field1 = state->data_field1, *field2 = state->data_field2, *weights = state->weights;
    GwyDataField *avg1 = state->avg1, *rms1 = state->rms1, *avg2 = state->avg2, *rms2 = state->rms2;
    gint xres, yres, m, n, m1, n1, m2, n2, k, col, row, colmax, rowmax;
    gint winwidth = state->window_width, winheight = state->window_height;
    gint searchwidth = state->search_width, searchheight = state->search_height;
    gint mfrom, mend, nfrom, nend;
    gdouble cormax, lscore, s, weightsum;
    gdouble ipos, jpos, scores[9];
    gdouble xmaximum, ymaximum;

    xres = field1->xres;
    yres = field1->yres;

    if (state->cs.state == GWY_COMPUTATION_STATE_INIT) {
        if (state->x_dist)
            gwy_data_field_clear(state->x_dist);
        if (state->y_dist)
            gwy_data_field_clear(state->y_dist);
        if (state->score)
            gwy_data_field_clear(state->score);
        gwy_clear(state->have_aux1, xres*yres);
        gwy_clear(state->have_aux2, xres*yres);
        state->cs.state = GWY_COMPUTATION_STATE_ITERATE;
        state->cs.fraction = 0.0;
        state->i = winwidth/2 + 1;
        state->j = winheight/2 + 1;
        return;
    }
    if (state->cs.state == GWY_COMPUTATION_STATE_FINISHED)
        return;

    g_assert(state->cs.state == GWY_COMPUTATION_STATE_ITERATE);
    //iterate over search area in the second datafield
    col = colmax = state->i;
    row = rowmax = state->j;
    mfrom = MAX(0, row - searchheight/2 - winheight/2);
    mend = MIN(yres - winheight, row + searchheight - searchheight/2 - winheight/2);
    nfrom = MAX(0, col - searchwidth/2 - winwidth/2);
    nend = MIN(xres - winwidth, col + searchwidth - searchwidth/2 - winwidth/2);
    weightsum = gwy_data_field_get_sum(weights);
    cormax = -G_MAXDOUBLE;
    m1 = row - winheight/2;
    n1 = col - winwidth/2;
    for (m = mfrom; m < mend; m++) {
        m2 = m;
        for (n = nfrom; n < nend; n++) {
            n2 = n;
            ensure_avg_and_rms(field1, weights, weightsum, n1, m1, avg1, rms1, state->have_aux1);
            ensure_avg_and_rms(field2, weights, weightsum, n2, m2, avg2, rms2, state->have_aux2);

            if (rms1->data[m1*xres + n1] == 0.0 || rms2->data[m2*xres + n2] == 0.0)
                lscore = 0.0;
            else {
                lscore = get_raw_weighted_correlation_score(field1, field2, weights, n1, m1, n2, m2,
                                                            avg1->data[m1*xres + n1], avg2->data[m2*xres + n2],
                                                            weightsum);
                lscore /= (rms1->data[m1*xres + n1] * rms2->data[m2*xres + n2]);
            }

            // add a little to score at exactly same point - to prevent problems on flat data
            if (m == 0 && n == 0)
                lscore *= 1.0001;

            if (lscore > cormax) {
                cormax = lscore;
                colmax = n + winwidth/2;
                rowmax = m + winheight/2;
            }
        }
    }

    if (state->score)
        state->score->data[col + xres*row] = cormax;

    if (state->x_dist || state->y_dist) {
        k = 0;
        for (m = -1; m <= 1; m++) {
            m2 = CLAMP(rowmax - winheight/2 + m, 0, yres-1);
            for (n = -1; n <= 1; n++) {
                if (m == 0 && n == 0)
                    s = cormax;
                else {
                    n2 = CLAMP(colmax - winwidth/2 + n, 0, xres-1);
                    ensure_avg_and_rms(field1, weights, weightsum, n1, m1, avg1, rms1, state->have_aux1);
                    ensure_avg_and_rms(field2, weights, weightsum, n2, m2, avg2, rms2, state->have_aux2);
                    if (rms1->data[m1*xres + n1] == 0.0 || rms2->data[m2*xres + n2] == 0.0)
                        s = 0.0;
                    else {
                        s = get_raw_weighted_correlation_score(field1, field2, weights, n1, m1, n2, m2,
                                                               avg1->data[m1*xres + n1], avg2->data[m2*xres + n2],
                                                               weightsum);
                        s /= (rms1->data[m1*xres + n1] * rms2->data[m2*xres + n2]);
                    }
                }
                scores[k++] = s;
            }
        }

        if (gwy_math_refine_maximum(scores, &xmaximum, &ymaximum)) {
            ipos = colmax + xmaximum;
            jpos = rowmax + ymaximum;
        }
        else {
            ipos = colmax;
            jpos = rowmax;
        }

        state->x_dist->data[col + xres * row] = (ipos - col)*field1->xreal/xres;
        state->y_dist->data[col + xres * row] = (jpos - row)*field1->yreal/yres;
    }

    state->i++;
    if (state->i == (xres - winwidth/2 - 1)) {
        state->i = winwidth/2 + 1;
        state->j++;
        if (state->j == (yres - winheight/2 - 1))
            state->cs.state = GWY_COMPUTATION_STATE_FINISHED;
    }
    state->cs.fraction += 1.0/(xres - winwidth - 2)/(yres - winheight - 2);
    state->cs.fraction = MIN(state->cs.fraction, 1.0);

    if (state->score)
        gwy_data_field_invalidate(state->score);
    if (state->x_dist)
        gwy_data_field_invalidate(state->x_dist);
    if (state->y_dist)
        gwy_data_field_invalidate(state->y_dist);
}

/**
 * gwy_data_field_crosscorrelate_finalize:
 * @state: Cross-correlation iterator.
 *
 * Destroys a cross-correlation iterator, freeing all resources.
 **/
void
gwy_data_field_crosscorrelate_finalize(GwyComputationState *cstate)
{
    GwyCrossCorrelationState *state = (GwyCrossCorrelationState*)cstate;

    GWY_OBJECT_UNREF(state->data_field1);
    GWY_OBJECT_UNREF(state->data_field2);
    GWY_OBJECT_UNREF(state->x_dist);
    GWY_OBJECT_UNREF(state->y_dist);
    GWY_OBJECT_UNREF(state->score);
    GWY_OBJECT_UNREF(state->weights);
    GWY_OBJECT_UNREF(state->avg1);
    GWY_OBJECT_UNREF(state->rms1);
    GWY_OBJECT_UNREF(state->avg2);
    GWY_OBJECT_UNREF(state->rms2);
    g_free(state->have_aux1);
    g_free(state->have_aux2);
    g_free(state);
}

/* Assumption: sum of all weights is 1.0/wq. */
static void
characterise_kernel(GwyDataField *kernel, GwyDataField *weight, gdouble wq,
                    gdouble *pmu, gdouble *ps2, gdouble *psigma2)
{
    gint xres = kernel->xres, yres = kernel->yres, n = xres*yres, i;
    gdouble s = 0.0, s2 = 0.0;
    const gdouble *k = kernel->data, *w = weight->data;

    for (i = 0; i < n; i++)
        s += w[i]*k[i];
    s = s*wq;

    for (i = 0; i < n; i++)
        s2 += w[i]*(k[i] - s)*(k[i] - s);
    s2 = s2*wq;

    *pmu = s;
    *psigma2 = s2;
    *ps2 = s2 + s*s;
}

static void
ensure_defined_exterior(GwyExteriorType *exterior, gdouble *fill_value)
{
    if (*exterior == GWY_EXTERIOR_UNDEFINED) {
        g_warning("Do not use GWY_EXTERIOR_UNDEFINED for convolutions and correlations.  "
                  "Fixing to zero-filled exterior.");
        *exterior = GWY_EXTERIOR_FIXED_VALUE;
        *fill_value = 0.0;
    }
}

static inline void
square_values(gdouble *data, guint n)
{
    while (n--) {
        *data *= *data;
        data++;
    }
}

static void
extract_result(const gdouble *extdata, guint xsize, guint col, guint row,
               gdouble *result, guint xres, guint yres,
               gdouble q)
{
    const gdouble *extrow;
    gdouble *rrow;
    guint i, j;

    for (i = 0; i < yres; i++) {
        extrow = extdata + (row + i)*xsize + col;
        rrow = result + i*xres;
        for (j = 0; j < xres; j++)
            rrow[j] = q*extrow[j];
    }
}

static gdouble
extract_sigma2_result(const gdouble *extdata, guint xsize, guint col, guint row,
                      gdouble *result, guint xres, guint yres,
                      gdouble q)
{
    const gdouble *extrow;
    gdouble *rrow;
    guint i, j;
    gdouble sum2 = 0.0;

    for (i = 0; i < yres; i++) {
        extrow = extdata + (row + i)*xsize + col;
        rrow = result + i*xres;
        for (j = 0; j < xres; j++)
            sum2 += rrow[j] = q*extrow[j] - rrow[j]*rrow[j];
    }

    return sum2/(xres*yres);
}

/* Calculate a* b → a */
static void
complex_conj_multiply_with(fftw_complex *a, const fftw_complex *b, guint n)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble re = a[i][0]*b[i][0] + a[i][1]*b[i][1];
        gdouble im = a[i][0]*b[i][1] - a[i][1]*b[i][0];

        a[i][0] = re;
        a[i][1] = im;
    }
}

/* Calculate a b* → a */
static void
complex_multiply_with_conj(fftw_complex *a, const fftw_complex *b, guint n)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble re = a[i][0]*b[i][0] + a[i][1]*b[i][1];
        gdouble im = -a[i][0]*b[i][1] + a[i][1]*b[i][0];

        a[i][0] = re;
        a[i][1] = im;
    }
}

/* Calculate a* b → a, a → b */
static void
complex_conj_multiply_with_swap(fftw_complex *a, fftw_complex *b, guint n)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble re = a[i][0]*b[i][0] + a[i][1]*b[i][1];
        gdouble im = a[i][0]*b[i][1] - a[i][1]*b[i][0];

        b[i][0] = a[i][0];
        b[i][1] = a[i][1];
        a[i][0] = re;
        a[i][1] = im;
    }
}

static void
normalise_fourier_coeffs(fftw_complex *a, guint n, gdouble regcoeff)
{
    gdouble s2 = 0.0;
    guint i;

    for (i = 0; i < n; i++) {
        gdouble re = a[i][0], im = a[i][1];

        s2 += re*re + im*im;
    }

    s2 = sqrt(s2/n)*regcoeff;

    for (i = 0; i < n; i++) {
        gdouble re = a[i][0], im = a[i][1];
        gdouble h = sqrt(re*re + im*im) + s2;
        a[i][0] /= h;
        a[i][1] /= h;
    }
}

/**
 * gwy_data_field_correlation_search:
 * @dfield: A data field to search.
 * @kernel: Detail to find (kernel).
 * @kernel_weight: Kernel weight, or %NULL.  If given, its dimensions must match @kernel.
 * @target: Data field to fill with the score.  It will be resampled to match @dfield.
 * @method: Method, determining the type of output to put into @target.
 * @regcoeff: Regularisation coefficient, any positive number.  Pass something like 0.1 if unsure.  You can also pass
 *            zero, it means the same as %G_MINDOUBLE.
 * @exterior: Exterior pixels handling.
 * @fill_value: The value to use with %GWY_EXTERIOR_FIXED_VALUE exterior.
 *
 * Performs correlation search of a detail in a larger data field.
 *
 * There are two basic classes of methods: Covariance (products of kernel and data values are summed) and height
 * difference (squared differences between kernel and data values are summed).  For the second class, the sign of the
 * output is inverted.  So in both cases higher values mean better match. All methods are implemented efficiently
 * using FFT.
 *
 * Usually you want to use %GWY_CORR_SEARCH_COVARIANCE or %GWY_CORR_SEARCH_HEIGHT_DIFF, in which the absolute data
 * offsets play no role (only the differences).
 *
 * If the detail can also occur with different height scales, use %GWY_CORR_SEARCH_COVARIANCE_SCORE or
 * %GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE in which the local data variance is normalised.  In this case @dfield regions
 * with very small (or zero) variance can lead to odd results and spurious maxima.  Use @regcoeff to suppress them:
 * Score of image details is suppressed if their variance is @regcoeff times the mean local variance.
 *
 * If @kernel_weight is non-%NULL is allows specify masking/weighting of kernel.  The simplest use is masking when
 * searching for a non-rectangular detail.  Fill @kernel_weight with 1s for important kernel pixels and with 0s for
 * irrelevant pixels.  However, you can use arbitrary non-negative weights.
 *
 * Since: 2.50
 **/
void
gwy_data_field_correlation_search(GwyDataField *dfield,
                                  GwyDataField *kernel,
                                  GwyDataField *kernel_weight,
                                  GwyDataField *target,
                                  GwyCorrSearchType method,
                                  gdouble regcoeff,
                                  GwyExteriorType exterior,
                                  gdouble fill_value)
{
    GwyDataField *kappa;
    guint xres, yres, kxres, kyres, xsize, ysize, cstride;
    guint extend_left, extend_right, extend_up, extend_down;
    guint i;
    gdouble kmu, ks2, ksigma2, wq, S2;
    gboolean is_normal, is_score;
    fftw_complex *cbufA, *cbufB;
    gdouble *extdata, *t, *u = NULL;
    fftw_plan fplan, bplan;
    RectExtendFunc extend_rect;
    GwySIUnit *funit, *kunit;

    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));
    xres = dfield->xres;
    yres = dfield->yres;

    g_return_if_fail(GWY_IS_DATA_FIELD(kernel));
    g_return_if_fail(GWY_IS_DATA_FIELD(target));
    kxres = kernel->xres;
    kyres = kernel->yres;
    if (kernel_weight) {
        g_return_if_fail(GWY_IS_DATA_FIELD(kernel_weight));
        g_return_if_fail(kernel_weight->xres == kxres);
        g_return_if_fail(kernel_weight->yres == kyres);
    }
    ensure_defined_exterior(&exterior, &fill_value);
    if (!(extend_rect = _gwy_get_rect_extend_func(exterior)))
        return;

    regcoeff = MAX(regcoeff, G_MINDOUBLE);
    is_normal = (method == GWY_CORR_SEARCH_COVARIANCE || method == GWY_CORR_SEARCH_HEIGHT_DIFF);
    is_score = (method == GWY_CORR_SEARCH_COVARIANCE_SCORE
                || method == GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE
                || method == GWY_CORR_SEARCH_PHASE_ONLY_SCORE);

    gwy_data_field_resample(target, xres, yres, GWY_INTERPOLATION_NONE);
    target->xreal = dfield->xreal;
    target->yreal = dfield->yreal;
    target->xoff = dfield->xoff;
    target->yoff = dfield->yoff;
    _gwy_copy_si_unit(dfield->si_unit_xy, &target->si_unit_xy);

    /* Scores are always unitless, even for HEIGHT_DIFF, because we normalise
     * by (local) rms of the same field. */
    if (is_score)
        _gwy_copy_si_unit(NULL, &target->si_unit_z);
    else if (method == GWY_CORR_SEARCH_COVARIANCE_RAW || method == GWY_CORR_SEARCH_COVARIANCE) {
        funit = gwy_data_field_get_si_unit_z(dfield);
        kunit = gwy_data_field_get_si_unit_z(kernel);
        gwy_si_unit_multiply(funit, kunit, gwy_data_field_get_si_unit_z(target));
    }
    else if (method == GWY_CORR_SEARCH_HEIGHT_DIFF_RAW || method == GWY_CORR_SEARCH_HEIGHT_DIFF) {
        funit = gwy_data_field_get_si_unit_z(dfield);
        kunit = gwy_data_field_get_si_unit_z(kernel);
        if (!gwy_si_unit_equal(funit, kunit))
            g_warning("Image and kernel should be the same physical quantity for height-difference search.");
        gwy_si_unit_power(funit, 2, gwy_data_field_get_si_unit_z(target));
    }
    else {
        g_assert_not_reached();
    }

    /* Normalise ∑_i w[i] = 1.
     *
     * Calculate kernel characteristics (if necessary):
     * μ  = ∑_i w[i] * k[i]
     * s² = ∑_i w[i] * k²[i]
     * σ² = ∑_i w[i] (k[i] - μ)² = s² - μ²
     *
     * Create modified kernel:
     * κ[i] = w[i] * k[i]          for RAW
     *        w[i] * (k[i] - μ)    for NORMAL or SCORE
     *
     * Define mean local variance:
     * S² = regcoeff/N * ∑_j σ²[j]
     *
     * For COVARIANCE outputs we then have:
     * G[j] = 1/u[j] ∑_i κ[i] z[j+i]
     * where  u²[j] = 1                  for RAW and NORMAL
     *                (σ²[j] + S²) * σ²  for SCORE
     * and
     *        μ[j] = ∑_i w[i] * z[k+j]
     *       s²[j] = ∑_i w[i] * z²[k+j]
     *       σ²[j] = ∑_i w[i] * (z[k+j] - μ[j])² = s²[j] - μ²[j]
     *
     * Therefore, for RAW and NORMAL we only modify kernel from k[i] to κ[i]. For SCORE we need to calculate also
     * s²[j] and μ²[j], i.e. two additional correlations with w[i].  Since the local averages would be calculated as
     * correlations anyway and the modification of k[i] is amortised, we do not save anything substantial by treating
     * NULL kernel_weight specially. Just create one when not given.
     *
     * For HEIGHT_DIFF outputs we then have:
     * H[j] = 2G[j] - s²[j] - s²           for RAW
     *      = 2G[j] - σ²[j] - σ²           for NORMAL
     *      = 2G[j] - 2 + S²/(σ²[j] + S²)  for SCORE
     * where G[j] is always calculated according to the corresponding RAW/NORMAL/SCORE formula above.
     */

    if (kernel_weight) {
        if (gwy_data_field_get_min(kernel_weight) < 0.0) {
            g_warning("Invalid negative values in kernel_weight.");
            gwy_data_field_clear(target);
            return;
        }
        else {
            wq = gwy_data_field_get_sum(kernel_weight);
            if (!wq) {
                gwy_data_field_clear(target);
                return;
            }
            g_object_ref(kernel_weight);
            wq = 1.0/wq;
        }
    }
    else {
        kernel_weight = gwy_data_field_new_alike(kernel, FALSE);
        gwy_data_field_fill(kernel_weight, 1.0/(kxres*kyres));
        wq = 1.0;
    }
    /* Do not physically modify kernel_weight->data, multiply them with wq instead.  */
    characterise_kernel(kernel, kernel_weight, wq, &kmu, &ks2, &ksigma2);
    gwy_debug("kmu %g, ks %g, ksigma %g (wq %g)", kmu, sqrt(ks2), sqrt(ksigma2), wq);
    if (ksigma2 <= 0.0) {
        gwy_data_field_clear(target);
        g_object_unref(kernel_weight);
        return;
    }

    kappa = gwy_data_field_duplicate(kernel);
    if (is_normal || is_score)
        gwy_data_field_add(kappa, -kmu);
    gwy_data_field_multiply_fields(kappa, kappa, kernel_weight);

    xsize = gwy_fft_find_nice_size(xres + kxres - 1);
    ysize = gwy_fft_find_nice_size(yres + kyres - 1);
    make_symmetrical_extension(xres, xsize, &extend_left, &extend_right);
    make_symmetrical_extension(yres, ysize, &extend_up, &extend_down);

    cstride = xsize/2 + 1;
    cbufA = gwy_fftw_new_complex(cstride*ysize);
    cbufB = gwy_fftw_new_complex(cstride*ysize);
    extdata = gwy_fftw_new_real(xsize*ysize);
    /* Ensure invalidate for gwy_data_field_normalize(). */
    t = gwy_data_field_get_data(target);
    fplan = gwy_fftw_plan_dft_r2c_2d(ysize, xsize, extdata, cbufB, FFTW_DESTROY_INPUT | FFTW_ESTIMATE);
    bplan = gwy_fftw_plan_dft_c2r_2d(ysize, xsize, cbufB, extdata, FFTW_DESTROY_INPUT | FFTW_ESTIMATE);

    extend_rect(dfield->data, xres, extdata, xsize,
                0, 0, xres, yres, xres, yres,
                extend_left, extend_right, extend_up, extend_down,
                fill_value);
    gwy_fftw_execute(fplan);
    if (method == GWY_CORR_SEARCH_PHASE_ONLY_SCORE)
        normalise_fourier_coeffs(cbufB, cstride*ysize, regcoeff);
    gwy_assign(cbufA, cbufB, cstride*ysize);
    extend_kernel_rect(kappa->data, kxres, kyres, extdata, xsize, ysize, xsize);
    gwy_fftw_execute(fplan);
    if (method == GWY_CORR_SEARCH_PHASE_ONLY_SCORE)
        normalise_fourier_coeffs(cbufB, cstride*ysize, regcoeff);
    complex_conj_multiply_with(cbufB, cbufA, cstride*ysize);
    gwy_fftw_execute(bplan);
    extract_result(extdata, xsize, extend_left, extend_up, t, xres, yres, wq/(xsize*ysize));

    if (method == GWY_CORR_SEARCH_PHASE_ONLY_SCORE) {
        /* The values scale weird for POC. There are obviously field and kernel size dependent factors, but
         * that does not seem to capture it and give a nice predictable range. */
        gwy_data_field_normalize(target);
    }
    else if (is_score || method == GWY_CORR_SEARCH_HEIGHT_DIFF) {
        /* We need σ²[j] and, therefore, also μ[j]. */
        extend_kernel_rect(kernel_weight->data, kxres, kyres, extdata, xsize, ysize, xsize);
        gwy_fftw_execute(fplan);
        complex_conj_multiply_with_swap(cbufB, cbufA, cstride*ysize);
        gwy_fftw_execute(bplan);
        u = g_new(gdouble, xres*yres);
        extract_result(extdata, xsize, extend_left, extend_up, u, xres, yres, wq/(xsize*ysize));
        extend_rect(dfield->data, xres, extdata, xsize,
                    0, 0, xres, yres, xres, yres,
                    extend_left, extend_right, extend_up, extend_down,
                    fill_value);
        square_values(extdata, xsize*ysize);
        gwy_fftw_execute(fplan);
        complex_multiply_with_conj(cbufB, cbufA, cstride*ysize);
        gwy_fftw_execute(bplan);
        S2 = extract_sigma2_result(extdata, xsize, extend_left, extend_up, u, xres, yres, wq/(xsize*ysize));
        gwy_debug("S %g", sqrt(S2));
        S2 *= regcoeff;
        if (is_score) {
            for (i = 0; i < xres*yres; i++)
                u[i] += S2;
            for (i = 0; i < xres*yres; i++)
                t[i] /= sqrt(u[i]*ksigma2);
        }
        if (method == GWY_CORR_SEARCH_HEIGHT_DIFF) {
            for (i = 0; i < xres*yres; i++)
                t[i] = 2.0*t[i] - u[i] - ksigma2;
        }
        if (method == GWY_CORR_SEARCH_HEIGHT_DIFF_SCORE) {
            for (i = 0; i < xres*yres; i++)
                t[i] = 2.0*t[i] - 2.0 + S2/u[i];
        }
    }

    if (method == GWY_CORR_SEARCH_HEIGHT_DIFF_RAW) {
        /* We only need s²[j]. */
        extend_rect(dfield->data, xres, extdata, xsize,
                    0, 0, xres, yres, xres, yres,
                    extend_left, extend_right, extend_up, extend_down,
                    fill_value);
        square_values(extdata, xsize*ysize);
        gwy_fftw_execute(fplan);
        gwy_assign(cbufA, cbufB, cstride*ysize);
        extend_kernel_rect(kernel_weight->data, kxres, kyres, extdata, xsize, ysize, xsize);
        gwy_fftw_execute(fplan);
        complex_conj_multiply_with(cbufB, cbufA, cstride*ysize);
        gwy_fftw_execute(bplan);
        u = g_new(gdouble, xres*yres);
        extract_result(extdata, xsize, extend_left, extend_up, u, xres, yres, wq/(xsize*ysize));
        for (i = 0; i < xres*yres; i++)
            t[i] = 2.0*t[i] - u[i] - ks2;
    }

    g_free(u);
    fftw_free(cbufB);
    fftw_free(cbufA);
    fftw_free(extdata);
    fftw_destroy_plan(bplan);
    fftw_destroy_plan(fplan);

    g_object_unref(kernel_weight);
    g_object_unref(kappa);
    gwy_data_field_invalidate(target);
}

/************************** Documentation ****************************/

/**
 * SECTION:correlation
 * @title: correlation
 * @short_description: Correlation and crosscorrelation
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
