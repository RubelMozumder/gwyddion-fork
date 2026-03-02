/*
 *  $Id: stats.c 26068 2023-12-20 12:25:29Z yeti-dn $
 *  Copyright (C) 2003-2020 David Necas (Yeti), Petr Klapetek.
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

#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>
#include <libprocess/level.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/grains.h>
#include <libprocess/filters.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

typedef gdouble (*LineStatFunc)(GwyDataLine *dline);

/**
 * gwy_data_field_get_max:
 * @data_field: A data field.
 *
 * Finds the maximum value of a data field.
 *
 * This quantity is cached.
 *
 * Returns: The maximum value.
 **/
gdouble
gwy_data_field_get_max(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -G_MAXDOUBLE);
    gwy_debug("%s", CTEST(data_field, MAX) ? "cache" : "lame");

    if (!CTEST(data_field, MAX)) {
        const gdouble *d = data_field->data;
        gdouble max = d[0];
        gint i, n = data_field->xres * data_field->yres;

        /* Too trivial to parallelise. */
        for (i = 1; i < n; i++)
            max = fmax(max, d[i]);
        CVAL(data_field, MAX) = max;
        data_field->cached |= CBIT(MAX);
    }

    return CVAL(data_field, MAX);
}


/**
 * gwy_data_field_area_get_max:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Finds the maximum value in a rectangular part of a data field.
 *
 * Returns: The maximum value.  When the number of samples to calculate maximum of is zero, -%G_MAXDOUBLE is returned.
 **/
gdouble
gwy_data_field_area_get_max(GwyDataField *dfield,
                            GwyDataField *mask,
                            gint col, gint row,
                            gint width, gint height)
{
    gint i, j, xres;
    gdouble max = -G_MAXDOUBLE;
    const gdouble *datapos, *mpos;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, NULL))
        return max;

    xres = dfield->xres;
    if (mask) {
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(max:max) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            for (j = 0; j < width; j++) {
                if (G_UNLIKELY(max < drow[j]) && mrow[j] > 0.0)
                    max = drow[j];
            }
        }

        return max;
    }

    if (col == 0 && width == xres && row == 0 && height == dfield->yres)
        return gwy_data_field_get_max(dfield);

    datapos = dfield->data + row*xres + col;
    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;

        for (j = 0; j < width; j++)
            max = fmax(max, drow[j]);
    }

    return max;
}

/**
 * gwy_data_field_get_min:
 * @data_field: A data field.
 *
 * Finds the minimum value of a data field.
 *
 * This quantity is cached.
 *
 * Returns: The minimum value.
 **/
gdouble
gwy_data_field_get_min(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), -G_MAXDOUBLE);
    gwy_debug("%s", CTEST(data_field, MIN) ? "cache" : "lame");

    if (!CTEST(data_field, MIN)) {
        const gdouble *d = data_field->data;
        gdouble min = d[0];
        gint i, n = data_field->xres * data_field->yres;

        /* Too trivial to parallelise. */
        for (i = 1; i < n; i++)
            min = fmin(min, d[i]);
        CVAL(data_field, MIN) = min;
        data_field->cached |= CBIT(MIN);
    }

    return CVAL(data_field, MIN);
}


/**
 * gwy_data_field_area_get_min:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Finds the minimum value in a rectangular part of a data field.
 *
 * Returns: The minimum value.  When the number of samples to calculate minimum of is zero, -%G_MAXDOUBLE is returned.
 **/
gdouble
gwy_data_field_area_get_min(GwyDataField *dfield,
                            GwyDataField *mask,
                            gint col, gint row,
                            gint width, gint height)
{
    gint i, j, xres;
    gdouble min = G_MAXDOUBLE;
    const gdouble *datapos, *mpos;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, NULL))
        return min;

    xres = dfield->xres;
    if (mask) {
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(min:min) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            for (j = 0; j < width; j++) {
                if (min > drow[j] && mrow[j] > 0.0)
                    min = drow[j];
            }
        }

        return min;
    }

    if (col == 0 && width == xres && row == 0 && height == dfield->yres)
        return gwy_data_field_get_min(dfield);

    datapos = dfield->data + row*xres + col;
    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;

        for (j = 0; j < width; j++)
            min = fmin(min, drow[j]);
    }

    return min;
}

/**
 * gwy_data_field_get_min_max:
 * @data_field: A data field.
 * @min: Location to store minimum to.
 * @max: Location to store maximum to.
 *
 * Finds minimum and maximum values of a data field.
 **/
void
gwy_data_field_get_min_max(GwyDataField *data_field,
                           gdouble *min,
                           gdouble *max)
{
    gboolean need_min = FALSE, need_max = FALSE;
    gdouble min1, max1;
    const gdouble *d;
    gint i, n;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    if (min) {
        if (CTEST(data_field, MIN))
            *min = CVAL(data_field, MIN);
        else
            need_min = TRUE;
    }
    if (max) {
        if (CTEST(data_field, MAX))
            *max = CVAL(data_field, MAX);
        else
            need_max = TRUE;
    }

    if (!need_min && !need_max)
        return;
    else if (!need_min) {
        *max = gwy_data_field_get_max(data_field);
        return;
    }
    else if (!need_max) {
        *min = gwy_data_field_get_min(data_field);
        return;
    }

    d = data_field->data;
    min1 = d[0];
    max1 = d[0];
    n = data_field->xres*data_field->yres;
    /* Too trivial to parallelise. */
    for (i = 1; i < n; i++) {
        min1 = fmin(min1, d[i]);
        max1 = fmax(max1, d[i]);
    }

    *min = min1;
    *max = max1;
    CVAL(data_field, MIN) = min1;
    CVAL(data_field, MAX) = max1;
    data_field->cached |= CBIT(MIN) | CBIT(MAX);
}

/**
 * gwy_data_field_area_get_min_max:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @min: Location to store minimum to.
 * @max: Location to store maximum to.
 *
 * Finds minimum and maximum values in a rectangular part of a data field.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_min_max_mask() with masking mode %GWY_MASK_INCLUDE.
 **/
void
gwy_data_field_area_get_min_max(GwyDataField *data_field,
                                GwyDataField *mask,
                                gint col, gint row,
                                gint width, gint height,
                                gdouble *min,
                                gdouble *max)
{
    gwy_data_field_area_get_min_max_mask(data_field, mask, GWY_MASK_INCLUDE, col, row, width, height, min, max);
}

/**
 * gwy_data_field_area_get_min_max_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @min: Location to store minimum to.
 * @max: Location to store maximum to.
 *
 * Finds minimum and maximum values in a rectangular part of a data field.
 *
 * Since: 2.18
 **/
void
gwy_data_field_area_get_min_max_mask(GwyDataField *data_field,
                                     GwyDataField *mask,
                                     GwyMaskingType mode,
                                     gint col, gint row,
                                     gint width, gint height,
                                     gdouble *min,
                                     gdouble *max)
{
    gdouble min1 = G_MAXDOUBLE, max1 = -G_MAXDOUBLE;
    const gdouble *datapos, *mpos;
    gint i, j, xres;

    if (!min && !max)
        return;
    if (min)
        *min = min1;
    if (max)
        *max = max1;
    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(data_field, &mask, &mode))
        return;

    xres = data_field->xres;
    if (mask) {
        datapos = data_field->data + row*xres + col;
        mpos = mask->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(min:min1) reduction(max:max1) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,mode)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    if (min1 > drow[j] && mrow[j] > 0.0)
                        min1 = drow[j];
                    if (max1 < drow[j] && mrow[j] > 0.0)
                        max1 = drow[j];
                }
            }
            else {
                for (j = 0; j < width; j++) {
                    if (min1 > drow[j] && mrow[j] < 1.0)
                        min1 = drow[j];
                    if (max1 < drow[j] && mrow[j] < 1.0)
                        max1 = drow[j];
                }
            }
        }

        if (min)
            *min = min1;
        if (max)
            *max = max1;

        return;
    }

    if (col == 0 && width == xres && row == 0 && height == data_field->yres) {
        gwy_data_field_get_min_max(data_field, min, max);
        return;
    }

    /* Static code analysis: sod off.  We ensure just above that at most one pointer is NULL. */
    if (!min) {
        *max = gwy_data_field_area_get_max(data_field, NULL, col, row, width, height);
        return;
    }
    if (!max) {
        *min = gwy_data_field_area_get_min(data_field, NULL, col, row, width, height);
        return;
    }

    datapos = data_field->data + row*xres + col;
    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;

        for (j = 0; j < width; j++) {
            min1 = fmin(min1, drow[j]);
            max1 = fmax(max1, drow[j]);
        }
    }

    *min = min1;
    *max = max1;
}

/**
 * gwy_data_field_get_sum:
 * @data_field: A data field.
 *
 * Sums all values in a data field.
 *
 * This quantity is cached.
 *
 * Returns: The sum of all values.
 **/
gdouble
gwy_data_field_get_sum(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);
    gwy_debug("%s", CTEST(data_field, SUM) ? "cache" : "lame");

    if (!CTEST(data_field, SUM)) {
        const gdouble *d = data_field->data;
        gdouble sum = 0.0;
        gint i, n = data_field->xres * data_field->yres;

        /* Too trivial to parallelise. */
        for (i = 0; i < n; i++)
            sum += d[i];

        CVAL(data_field, SUM) = sum;
        data_field->cached |= CBIT(SUM);
    }

    return CVAL(data_field, SUM);
}

/**
 * gwy_data_field_area_get_sum:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Sums values of a rectangular part of a data field.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_sum_mask() with masking mode %GWY_MASK_INCLUDE.
 *
 * Returns: The sum of all values inside area.
 **/
gdouble
gwy_data_field_area_get_sum(GwyDataField *dfield,
                            GwyDataField *mask,
                            gint col, gint row,
                            gint width, gint height)
{
    return gwy_data_field_area_get_sum_mask(dfield, mask, GWY_MASK_INCLUDE,
                                            col, row, width, height);
}

/**
 * gwy_data_field_area_get_sum_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Sums values of a rectangular part of a data field.
 *
 * Returns: The sum of all values inside area.
 *
 * Since: 2.18
 **/
gdouble
gwy_data_field_area_get_sum_mask(GwyDataField *dfield,
                                 GwyDataField *mask,
                                 GwyMaskingType mode,
                                 gint col, gint row,
                                 gint width, gint height)
{
    gint i, j, xres;
    gdouble sum = 0.0;
    const gdouble *datapos, *mpos;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return sum;

    xres = dfield->xres;
    if (mask) {
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,mode)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++)
                    sum += (mrow[j] > 0.0)*drow[j];
            }
            else {
                for (j = 0; j < width; j++)
                    sum += (mrow[j] <= 0.0)*drow[j];
            }
        }

        return sum;
    }

    if (col == 0 && width == xres
        && row == 0 && height == dfield->yres)
        return gwy_data_field_get_sum(dfield);

    datapos = dfield->data + row*xres + col;
    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;

        for (j = 0; j < width; j++)
            sum += drow[j];
    }

    return sum;
}

/**
 * gwy_data_field_get_avg:
 * @data_field: A data field
 *
 * Computes average value of a data field.
 *
 * This quantity is cached.
 *
 * Returns: The average value.
 **/
gdouble
gwy_data_field_get_avg(GwyDataField *data_field)
{
    return gwy_data_field_get_sum(data_field)/((data_field->xres * data_field->yres));
}

/**
 * gwy_data_field_area_get_avg:
 * @data_field: A data field
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes average value of a rectangular part of a data field.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_avg_mask()
 * with masking mode %GWY_MASK_INCLUDE.
 *
 * Returns: The average value.
 **/
gdouble
gwy_data_field_area_get_avg(GwyDataField *dfield,
                            GwyDataField *mask,
                            gint col, gint row,
                            gint width, gint height)
{
    return gwy_data_field_area_get_avg_mask(dfield, mask, GWY_MASK_INCLUDE,
                                            col, row, width, height);
}

/**
 * gwy_data_field_area_get_avg_mask:
 * @data_field: A data field
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes average value of a rectangular part of a data field.
 *
 * Returns: The average value.
 *
 * Since: 2.18
 **/
gdouble
gwy_data_field_area_get_avg_mask(GwyDataField *dfield,
                                 GwyDataField *mask,
                                 GwyMaskingType mode,
                                 gint col, gint row,
                                 gint width, gint height)
{
    const gdouble *datapos, *mpos;
    gdouble sum = 0.0;
    gint i, j, xres;
    guint nn;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return sum;
    if (!mask)
        return gwy_data_field_area_get_sum_mask(dfield, NULL, GWY_MASK_IGNORE, col, row, width, height)/(width*height);

    xres = dfield->xres;
    datapos = dfield->data + row*xres + col;
    mpos = mask->data + row*xres + col;
    nn = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum,nn) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,mode)
#endif
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;
        const gdouble *mrow = mpos + i*xres;

        if (mode == GWY_MASK_INCLUDE) {
            for (j = 0; j < width; j++) {
                guint c = (mrow[j] > 0.0);
                sum += c*drow[j];
                nn += c;
            }
        }
        else {
            for (j = 0; j < width; j++) {
                guint c = (mrow[j] <= 0.0);
                sum += c*drow[j];
                nn += c;
            }
        }
    }

    return sum/nn;
}

/**
 * gwy_data_field_get_rms:
 * @data_field: A data field.
 *
 * Computes root mean square value of a data field.
 *
 * The root mean square value is calculated with respect to the mean value. See gwy_data_field_get_mean_square() for
 * a similar function which does not subtract the mean value.
 *
 * This quantity is cached.
 *
 * Returns: The root mean square value.
 **/
gdouble
gwy_data_field_get_rms(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);
    gwy_debug("%s", CTEST(data_field, RMS) ? "cache" : "lame");

    if (!CTEST(data_field, RMS)) {
        gdouble avg = gwy_data_field_get_avg(data_field);
        gdouble sum2 = 0.0;
        const gdouble *d = data_field->data;
        gint i, n = data_field->xres * data_field->yres;

        /* Too trivial to parallelise. */
        for (i = 0; i < n; i++) {
            gdouble dd = d[i] - avg;
            sum2 += dd*dd;
        }

        CVAL(data_field, RMS) = sqrt(sum2/n);
        data_field->cached |= CBIT(RMS);
    }

    return CVAL(data_field, RMS);

}

/**
 * gwy_data_field_area_get_rms:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes root mean square value of a rectangular part of a data field.
 *
 * The root mean square value is calculated with respect to the mean value.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_rms_mask() with masking mode %GWY_MASK_INCLUDE.
 *
 * Returns: The root mean square value.
 **/

gdouble
gwy_data_field_area_get_rms(GwyDataField *dfield,
                            GwyDataField *mask,
                            gint col, gint row,
                            gint width, gint height)
{
    return gwy_data_field_area_get_rms_mask(dfield, mask, GWY_MASK_INCLUDE, col, row, width, height);
}

/**
 * gwy_data_field_area_get_rms_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes root mean square value of deviations of a rectangular part of a data field.
 *
 * The root mean square value is calculated with respect to the mean value.
 *
 * Returns: The root mean square value of deviations from the mean value.
 *
 * Since: 2.18
 **/
gdouble
gwy_data_field_area_get_rms_mask(GwyDataField *dfield,
                                 GwyDataField *mask,
                                 GwyMaskingType mode,
                                 gint col, gint row,
                                 gint width, gint height)
{
    gint i, j, xres;
    gdouble avg, sum2 = 0.0;
    const gdouble *datapos, *mpos;
    guint nn;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return 0.0;

    xres = dfield->xres;
    if (mask) {
        avg = gwy_data_field_area_get_avg_mask(dfield, mask, mode, col, row, width, height);
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
        nn = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum2,nn) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,avg,mode)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    guint c = (mrow[j] > 0.0);
                    gdouble dd = drow[j] - avg;
                    sum2 += c*dd*dd;
                    nn += c;
                }
            }
            else {
                for (j = 0; j < width; j++) {
                    guint c = (mrow[j] <= 0.0);
                    gdouble dd = drow[j] - avg;
                    sum2 += c*dd*dd;
                    nn += c;
                }
            }
        }
        return nn ? sqrt(sum2/nn) : 0.0;
    }

    if (col == 0 && width == xres && row == 0 && height == dfield->yres)
        return gwy_data_field_get_rms(dfield);

    avg = gwy_data_field_area_get_avg_mask(dfield, NULL, GWY_MASK_IGNORE, col, row, width, height);
    datapos = dfield->data + row*xres + col;
    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;

        for (j = 0; j < width; j++) {
            gdouble dd = drow[j] - avg;
            sum2 += dd*dd;
        }
    }
    nn = width*height;

    return sqrt(sum2/nn);
}

/**
 * gwy_data_field_area_get_grainwise_rms:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes grain-wise root mean square value of deviations of a rectangular part of a data field.
 *
 * Grain-wise means that the mean value is determined for each grain (i.e. cotinguous part of the mask or inverted
 * mask) separately and the deviations are calculated from these mean values.
 *
 * Returns: The root mean square value of deviations from the mean value.
 *
 * Since: 2.29
 **/
gdouble
gwy_data_field_area_get_grainwise_rms(GwyDataField *dfield,
                                      GwyDataField *mask,
                                      GwyMaskingType mode,
                                      gint col,
                                      gint row,
                                      gint width,
                                      gint height)
{
    GwyDataField *grainmask;
    gint *grains, *size, *g;
    gint i, j, n;
    gint xres, yres, ngrains;
    gdouble *m;
    const gdouble *datapos;
    gdouble rms = 0.0;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return rms;
    xres = dfield->xres;
    yres = dfield->yres;

    if (!mask)
        return gwy_data_field_area_get_rms_mask(dfield, NULL, GWY_MASK_IGNORE, col, row, width, height);

    if (mode == GWY_MASK_INCLUDE) {
        if (col == 0 && row == 0 && width == xres && height == yres)
            grainmask = (GwyDataField*)g_object_ref(mask);
        else
            grainmask = gwy_data_field_area_extract(mask, col, row, width, height);
    }
    else {
        grainmask = gwy_data_field_area_extract(mask, col, row, width, height);
        gwy_data_field_grains_invert(grainmask);
    }

    grains = g_new0(gint, width*height);
    ngrains = gwy_data_field_number_grains(grainmask, grains);
    if (!ngrains) {
        g_free(grains);
        g_object_unref(grainmask);
        return rms;
    }

    m = g_new0(gdouble, ngrains+1);
    size = g_new0(gint, ngrains+1);
    datapos = dfield->data + row*xres + col;
    for (i = 0; i < height; i++) {
        g = grains + i*width;
        for (j = 0; j < width; j++) {
            m[g[j]] += datapos[i*xres + j];
            size[g[j]]++;
        }
    }

    n = 0;
    for (i = 1; i <= ngrains; i++) {
        m[i] /= size[i];
        n += size[i];
    }

    rms = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:rms) \
            private(g,i,j) \
            shared(datapos,m,grains,xres,width,height)
#endif
    for (i = 0; i < height; i++) {
        g = grains + i*width;
        for (j = 0; j < width; j++) {
            if (g[j]) {
                gdouble d = datapos[i*xres + j] - m[g[j]];
                rms += d*d;
            }
        }
    }
    rms = sqrt(rms/n);

    g_free(size);
    g_free(m);
    g_free(grains);
    g_object_unref(grainmask);

    return rms;
}

/**
 * gwy_data_field_get_mean_square:
 * @data_field: A two-dimensional data field.
 *
 * Computes mean square value of a data field.
 *
 * See gwy_data_field_area_get_mean_square() for remarks.
 *
 * Returns: The mean square value.
 *
 * Since: 2.52
 **/
gdouble
gwy_data_field_get_mean_square(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);
    gwy_debug("%s", CTEST(data_field, MSQ) ? "cache" : "lame");

    if (!CTEST(data_field, MSQ)) {
        gdouble msq = 0.0;
        const gdouble *d = data_field->data;
        guint i, n = data_field->xres * data_field->yres;

        /* Too trivial to parallelise. */
        for (i = 0; i < n; i++)
            msq += d[i]*d[i];
        msq /= n;

        CVAL(data_field, MSQ) = msq;
        data_field->cached |= CBIT(MSQ);
    }

    return CVAL(data_field, MSQ);
}

/**
 * gwy_data_field_area_get_mean_square:
 * @data_field: A two-dimensional data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes mean square value of a rectangular part of a data field.
 *
 * Unlike gwy_data_field_get_rms(), this function does <emphasis>not</emphasis> subtract the mean value beforehand.
 * Therefore, it is useful to sum the squared values of data fields which can have the zero level set differently, for
 * instance when the field contains a distribution.
 *
 * Returns: The mean square value.
 *
 * Since: 2.52
 **/
gdouble
gwy_data_field_area_get_mean_square(GwyDataField *dfield,
                                    GwyDataField *mask,
                                    GwyMaskingType mode,
                                    gint col,
                                    gint row,
                                    gint width,
                                    gint height)
{
    gdouble msq = 0.0;
    const gdouble *datapos, *mpos;
    gint i, j, xres;
    guint nn;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return msq;

    xres = dfield->xres;
    datapos = dfield->data + row*xres + col;
    if (mask) {
        mpos = mask->data + row*xres + col;
        nn = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:msq,nn) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,mode)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    if (mrow[j] > 0.0) {
                        msq += drow[j]*drow[j];
                        nn++;
                    }
                }
            }
            else {
                for (j = 0; j < width; j++) {
                    if (mrow[j] < 1.0) {
                        msq += drow[j]*drow[j];
                        nn++;
                    }
                }
            }
        }

        if (nn)
            msq /= nn;

        return msq;
    }

    if (col == 0 && width == xres && row == 0 && height == dfield->yres)
        return gwy_data_field_get_mean_square(dfield);

    /* Too trivial to parallelise. */
    for (i = 0; i < height; i++) {
        const gdouble *drow = datapos + i*xres;
        for (j = 0; j < width; j++)
            msq += drow[j]*drow[j];
    }
    msq /= width*height;

    return msq;
}

static void
compute_autorange(const gdouble *values, guint n,
                  gdouble min, gdouble max,
                  gdouble *rmin, gdouble *rmax)
{
    enum { AR_NDH = 512 };

    guint *dh;
    guint i, j;
    gdouble q;

    if (n < 4 || min >= max) {
        *rmin = MIN(min, max);
        *rmax = MAX(min, max);
        return;
    }

    q = AR_NDH/(max - min);
    dh = g_new(guint, AR_NDH);
    n = gwy_math_histogram(values, n, min, max, AR_NDH, dh);

    j = 0;
    for (i = j = 0; i < AR_NDH-1 && dh[i] < 5e-2*n/AR_NDH && j < 2e-2*n; i++)
        j += dh[i];
    *rmin = min + i/q;

    j = 0;
    for (i = AR_NDH-1, j = 0; i > 0 && dh[i] < 5e-2*n/AR_NDH && j < 2e-2*n; i--)
        j += dh[i];
    *rmax = min + (i + 1)/q;

    g_free(dh);

    /* Someone passed us NaNs in data_field? */
    if (!(*rmin < *rmax)) {
        *rmin = min;
        *rmax = max;
    }
}

/**
 * gwy_data_field_area_get_autorange:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @masking: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @from: Location to store range start.
 * @to: Location to store range end.
 *
 * Computes data field area value range with outliers cut-off.
 *
 * See gwy_data_field_get_autorange() for discussion.
 *
 * Since: 2.54
 **/
void
gwy_data_field_area_get_autorange(GwyDataField *data_field,
                                  GwyDataField *mask,
                                  GwyMaskingType masking,
                                  gint col,
                                  gint row,
                                  gint width,
                                  gint height,
                                  gdouble *from,
                                  gdouble *to)
{
    gdouble min, max, rmin, rmax;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &masking))
        return;

    gwy_data_field_area_get_min_max_mask(data_field, mask, masking, col, row, width, height, &min, &max);
    if (!(min < max)) {
        rmin = min;
        rmax = max;
    }
    else {
        const gdouble *d = data_field->data;
        const gdouble *m = mask ? mask->data : NULL;
        gdouble *values = g_new(gdouble, width*height);
        gint n = 0, i, j, xres = data_field->xres;

        for (i = 0; i < height; i++) {
            const gdouble *drow = d + (i + row)*xres + col;
            const gdouble *mrow = m ? m + (i + row)*xres + col : NULL;

            if (masking == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    if (mrow[j] >= 1.0)
                        values[n++] = drow[j];
                }
            }
            else if (masking == GWY_MASK_EXCLUDE) {
                for (j = 0; j < width; j++) {
                    if (mrow[j] <= 0.0)
                        values[n++] = drow[j];
                }
            }
            else {
                gwy_assign(values + n, drow, width);
                n += width;
            }
        }

        compute_autorange(values, n, min, max, &rmin, &rmax);
        g_free(values);
    }

    if (from)
        *from = rmin;
    if (to)
        *to = rmax;
}

/**
 * gwy_data_field_get_autorange:
 * @data_field: A data field.
 * @from: Location to store range start.
 * @to: Location to store range end.
 *
 * Computes data field value range with outliers cut-off.
 *
 * The purpose of this function is to find a range is suitable for false color mapping.  The precise method how it is
 * calculated is unspecified and may be subject to changes.
 *
 * However, it is guaranteed minimum <= @from <= @to <= maximum.
 *
 * This quantity is cached.
 **/
void
gwy_data_field_get_autorange(GwyDataField *data_field,
                             gdouble *from,
                             gdouble *to)
{
    gdouble min, max, rmin, rmax;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    gwy_debug("%s", CTEST(data_field, ARF) ? "cache" : "lame");
    if ((!from || CTEST(data_field, ARF))
        && (!to || CTEST(data_field, ART))) {
        if (from)
            *from = CVAL(data_field, ARF);
        if (to)
            *to = CVAL(data_field, ART);
        return;
    }

    gwy_data_field_get_min_max(data_field, &min, &max);
    compute_autorange(data_field->data, data_field->xres*data_field->yres, min, max, &rmin, &rmax);

    if (from)
        *from = rmin;
    if (to)
        *to = rmax;

    CVAL(data_field, ARF) = rmin;
    CVAL(data_field, ART) = rmax;
    data_field->cached |= CBIT(ARF) | CBIT(ART);
}

/**
 * gwy_data_field_get_stats:
 * @data_field: A data field.
 * @avg: Where average height value of the surface should be stored, or %NULL.
 * @ra: Where average value of irregularities should be stored, or %NULL.
 * @rms: Where root mean square value of irregularities (Rq) should be stored, or %NULL.
 * @skew: Where skew (symmetry of height distribution) should be stored, or %NULL.
 * @kurtosis: Where kurtosis (peakedness of height ditribution) should be stored, or %NULL.
 *
 * Computes basic statistical quantities of a data field.
 *
 * Note the kurtosis returned by this function returns is the excess kurtosis
 * which is zero for the Gaussian distribution (not 3).
 **/
void
gwy_data_field_get_stats(GwyDataField *data_field,
                         gdouble *avg,
                         gdouble *ra,
                         gdouble *rms,
                         gdouble *skew,
                         gdouble *kurtosis)
{
    gint i;
    gdouble c_sz2, c_sz3, c_sz4, c_abs1;
    const gdouble *d = data_field->data;
    guint n = data_field->xres * data_field->yres;
    gdouble myavg, myrms;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    c_sz2 = c_sz3 = c_sz4 = c_abs1 = 0;

    myavg = gwy_data_field_get_avg(data_field);
    if (avg)
        *avg = myavg;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:c_abs1,c_sz2,c_sz3,c_sz4) \
            private(i) \
            shared(d,n,myavg)
#endif
    for (i = 0; i < n; i++) {
        gdouble dif = d[i] - myavg;
        c_abs1 += fabs(dif);
        c_sz2 += dif*dif;
        c_sz3 += dif*dif*dif;
        c_sz4 += dif*dif*dif*dif;
    }

    myrms = c_sz2/n;
    if (ra)
        *ra = c_abs1/n;
    if (skew) {
        if (myrms > 0.0)
            *skew = c_sz3/pow(myrms, 1.5)/n;
        else
            *skew = 0.0;
    }
    if (kurtosis) {
        if (myrms > 0.0)
            *kurtosis = c_sz4/(myrms*myrms)/n - 3;
        else
            *kurtosis = 0.0;
    }
    if (rms)
        *rms = sqrt(myrms);

    if (!CTEST(data_field, RMS)) {
        CVAL(data_field, RMS) = sqrt(myrms);
        data_field->cached |= CBIT(RMS);
    }
}

/**
 * gwy_data_field_area_get_stats:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @avg: Where average height value of the surface should be stored, or %NULL.
 * @ra: Where average value of irregularities should be stored, or %NULL.
 * @rms: Where root mean square value of irregularities (Rq) should be stored, or %NULL.
 * @skew: Where skew (symmetry of height distribution) should be stored, or %NULL.
 * @kurtosis: Where kurtosis (peakedness of height ditribution) should be stored, or %NULL.
 *
 * Computes basic statistical quantities of a rectangular part of a data field.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_stats_mask() with masking mode %GWY_MASK_INCLUDE.
 *
 * Note the kurtosis returned by this function returns is the excess kurtosis which is zero for the Gaussian
 * distribution (not 3).
 **/
void
gwy_data_field_area_get_stats(GwyDataField *dfield,
                              GwyDataField *mask,
                              gint col, gint row,
                              gint width, gint height,
                              gdouble *avg,
                              gdouble *ra,
                              gdouble *rms,
                              gdouble *skew,
                              gdouble *kurtosis)
{
    gwy_data_field_area_get_stats_mask(dfield, mask, GWY_MASK_INCLUDE, col, row, width, height,
                                       avg, ra, rms, skew, kurtosis);
}

/**
 * gwy_data_field_area_get_stats_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @avg: Where average height value of the surface should be stored, or %NULL.
 * @ra: Where average value of irregularities should be stored, or %NULL.
 * @rms: Where root mean square value of irregularities (Rq) should be stored, or %NULL.
 * @skew: Where skew (symmetry of height distribution) should be stored, or %NULL.
 * @kurtosis: Where kurtosis (peakedness of height ditribution) should be stored, or %NULL.
 *
 * Computes basic statistical quantities of a rectangular part of a data field.
 *
 * Note the kurtosis returned by this function returns is the excess kurtosis which is zero for the Gaussian
 * distribution (not 3).
 *
 * Since: 2.18
 **/
void
gwy_data_field_area_get_stats_mask(GwyDataField *dfield,
                                   GwyDataField *mask,
                                   GwyMaskingType mode,
                                   gint col, gint row,
                                   gint width, gint height,
                                   gdouble *avg,
                                   gdouble *ra,
                                   gdouble *rms,
                                   gdouble *skew,
                                   gdouble *kurtosis)
{
    gdouble c_sz2, c_sz3, c_sz4, c_abs1;
    gdouble myavg, myrms;
    const gdouble *datapos, *mpos;
    gint i, j, xres;
    guint n;

    if (!_gwy_data_field_check_area(dfield, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return;

    myavg = gwy_data_field_area_get_avg_mask(dfield, mask, mode, col, row, width, height);
    c_sz2 = c_sz3 = c_sz4 = c_abs1 = 0.0;
    xres = dfield->xres;
    if (mask) {
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
        n = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:c_abs1,c_sz2,c_sz3,c_sz4,n) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,myavg,mode)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    if (mrow[j] > 0.0) {
                        gdouble dif = drow[j] - myavg;
                        c_abs1 += fabs(dif);
                        c_sz2 += dif*dif;
                        c_sz3 += dif*dif*dif;
                        c_sz4 += dif*dif*dif*dif;
                        n++;
                    }
                }
            }
            else {
                for (j = 0; j < width; j++) {
                    if (mrow[j] < 1.0) {
                        gdouble dif = drow[j] - myavg;
                        c_abs1 += fabs(dif);
                        c_sz2 += dif*dif;
                        c_sz3 += dif*dif*dif;
                        c_sz4 += dif*dif*dif*dif;
                        n++;
                    }
                }
            }
        }
    }
    else {
        n = width*height;
        datapos = dfield->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
           reduction(+:c_abs1,c_sz2,c_sz3,c_sz4) \
           private(i,j) \
           shared(datapos,xres,width,height,myavg)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;

            for (j = 0; j < width; j++) {
                gdouble dif = drow[j] - myavg;
                c_abs1 += fabs(dif);
                c_sz2 += dif*dif;
                c_sz3 += dif*dif*dif;
                c_sz4 += dif*dif*dif*dif;
            }
        }
    }

    myrms = c_sz2/n;
    if (avg)
        *avg = myavg;
    if (ra)
        *ra = c_abs1/n;
    if (skew) {
        if (myrms > 0.0)
            *skew = c_sz3/pow(myrms, 1.5)/n;
        else
            *skew = 0.0;
    }
    if (kurtosis) {
        if (myrms > 0.0)
            *kurtosis = c_sz4/(myrms*myrms)/n - 3;
        else
            *kurtosis = 0.0;
    }
    if (rms)
        *rms = sqrt(myrms);
}

/**
 * gwy_data_field_area_count_in_range:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @below: Upper bound to compare data to.  The number of samples less than or equal to @below is stored in @nbelow.
 * @above: Lower bound to compare data to.  The number of samples greater than or equal to @above is stored in
 *         @nabove.
 * @nbelow: Location to store the number of samples less than or equal to @below, or %NULL.
 * @nabove: Location to store the number of samples greater than or equal to @above, or %NULL.
 *
 * Counts data samples in given range.
 *
 * No assertion is made about the values of @above and @below, in other words @above may be larger than @below.  To
 * count samples in an open interval instead of a closed interval, exchange @below and @above and then subtract the
 * @nabove and @nbelow from @width*@height to get the complementary counts.
 *
 * With this trick the common task of counting positive values can be realized:
 * <informalexample><programlisting>
 * gwy_data_field_area_count_in_range(data_field, NULL,
 *                                    col, row, width, height,
 *                                    0.0, 0.0, &amp;count, NULL);
 * count = width*height - count;
 * </programlisting></informalexample>
 **/
void
gwy_data_field_area_count_in_range(GwyDataField *data_field,
                                   GwyDataField *mask,
                                   gint col, gint row,
                                   gint width, gint height,
                                   gdouble below,
                                   gdouble above,
                                   gint *nbelow,
                                   gint *nabove)
{
    const gdouble *datapos, *mpos;
    gint i, j, na, nb, xres;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, NULL))
        return;

    if (!nabove && !nbelow)
        return;

    na = nb = 0;
    xres = data_field->xres;
    if (mask) {
        datapos = data_field->data + row*xres + col;
        mpos = mask->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:na,nb) \
            private(i,j) \
            shared(datapos,mpos,xres,width,height,above,below)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            for (j = 0; j < width; j++) {
                guint c = mrow[j] > 0.0;
                na += c*(drow[j] >= above);
                nb += c*(drow[j] <= below);
            }
        }
    }
    else {
        datapos = data_field->data + row*xres + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:na,nb) \
            private(i,j) \
            shared(datapos,xres,width,height,above,below)
#endif
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;

            for (j = 0; j < width; j++) {
                na += (drow[j] >= above);
                nb += (drow[j] <= below);
            }
        }
    }

    if (nabove)
        *nabove = na;
    if (nbelow)
        *nbelow = nb;
}

/**
 * gwy_data_field_area_dh:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account, or %NULL.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates distribution of heights in a rectangular part of data field.
 **/
void
gwy_data_field_area_dh(GwyDataField *data_field,
                       GwyDataField *mask,
                       GwyDataLine *target_line,
                       gint col, gint row,
                       gint width, gint height,
                       gint nstats)
{
    GwySIUnit *fieldunit, *lineunit, *rhounit;
    gdouble min, max;
    const gdouble *data, *drow, *mrow;
    gdouble *values;
    gint xres, i, j;
    guint *counts;
    guint nn;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, NULL))
        return;
    g_return_if_fail(GWY_IS_DATA_LINE(target_line));
    xres = data_field->xres;
    data = data_field->data;

    if (mask) {
        nn = 0;
        for (i = 0; i < height; i++) {
            mrow = mask->data + (i + row)*xres + col;
            for (j = 0; j < width; j++)
                nn += (mrow[j] > 0.0);
        }
    }
    else
        nn = width*height;

    if (nstats < 1) {
        nstats = floor(3.49*cbrt(nn) + 0.5);
        nstats = MAX(nstats, 2);
    }

    gwy_data_line_resample(target_line, nstats, GWY_INTERPOLATION_NONE);
    gwy_data_field_area_get_min_max_mask(data_field, nn ? mask : NULL, GWY_MASK_INCLUDE,
                                         col, row, width, height, &min, &max);

    /* Set proper units */
    fieldunit = gwy_data_field_get_si_unit_z(data_field);
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    gwy_si_unit_assign(lineunit, fieldunit);
    rhounit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_power(lineunit, -1, rhounit);

    /* Handle border cases */
    if (!nn) {
        gwy_data_line_clear(target_line);
        gwy_data_line_set_real(target_line, min < max ? max - min : 1.0);
        return;
    }
    if (min == max) {
        gwy_data_line_clear(target_line);
        gwy_data_line_set_real(target_line, min ? fabs(max) : 1.0);
        target_line->data[0] = nstats/gwy_data_line_get_real(target_line);
        return;
    }

    /* Calculate height distribution */
    gwy_data_line_set_real(target_line, max - min);
    gwy_data_line_set_offset(target_line, min);
    counts = g_new(guint, nstats);

    if (mask) {
        values = g_new(gdouble, nn);
        nn = 0;
        for (i = 0; i < height; i++) {
            drow = data + (i + row)*xres + col;
            mrow = mask->data + (i + row)*xres + col;

            for (j = 0; j < width; j++) {
                if (mrow[j] > 0.0)
                    values[nn++] = drow[j];
            }
        }
        nn = gwy_math_histogram(values, nn, min, max, nstats, counts);
        g_free(values);
    }
    else if (width < xres) {
        values = g_new(gdouble, nn);
        for (i = 0; i < height; i++)
            gwy_assign(values + i*width, data + (i + row)*xres + col, width);
        nn = gwy_math_histogram(values, nn, min, max, nstats, counts);
        g_free(values);
    }
    else {
        /* Contiguous block. */
        g_assert(width == xres);
        nn = gwy_math_histogram(data + row*xres, nn, min, max, nstats, counts);
    }

    for (i = 0; i < nstats; i++)
        target_line->data[i] = counts[i];
    g_free(counts);

    /* Normalize integral to 1 */
    gwy_data_line_multiply(target_line, nstats/(max - min)/MAX(nn, 1));
}

/**
 * gwy_data_field_dh:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates distribution of heights in a data field.
 **/
void
gwy_data_field_dh(GwyDataField *data_field,
                  GwyDataLine *target_line,
                  gint nstats)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_dh(data_field, NULL, target_line, 0, 0, data_field->xres, data_field->yres, nstats);
}

/**
 * gwy_data_field_area_cdh:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account, or %NULL.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates cumulative distribution of heights in a rectangular part of data
 * field.
 **/
void
gwy_data_field_area_cdh(GwyDataField *data_field,
                        GwyDataField *mask,
                        GwyDataLine *target_line,
                        gint col, gint row,
                        gint width, gint height,
                        gint nstats)
{
    GwySIUnit *rhounit, *lineunit;

    gwy_data_field_area_dh(data_field, mask, target_line, col, row, width, height, nstats);
    gwy_data_line_cumulate(target_line);
    gwy_data_line_multiply(target_line, gwy_data_line_itor(target_line, 1));
    target_line->data[target_line->res-1] = 1.0;   /* Fix rounding errors. */

    /* Update units after integration */
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    rhounit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_multiply(rhounit, lineunit, rhounit);
}

/**
 * gwy_data_field_cdh:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates cumulative distribution of heights in a data field.
 **/
void
gwy_data_field_cdh(GwyDataField *data_field,
                   GwyDataLine *target_line,
                   gint nstats)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_cdh(data_field, NULL, target_line, 0, 0, data_field->xres, data_field->yres, nstats);
}

/**
 * gwy_data_field_area_da_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account, or %NULL.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates distribution of slopes in a rectangular part of data field, with masking.
 *
 * Since: 2.49
 **/
void
gwy_data_field_area_da_mask(GwyDataField *data_field,
                            GwyDataField *mask,
                            GwyDataLine *target_line,
                            gint col, gint row,
                            gint width, gint height,
                            GwyOrientation orientation,
                            gint nstats)
{
    GwySIUnit *lineunit, *rhounit;
    const gdouble *drow, *mrow;
    gdouble *values;
    guint *counts;
    gdouble min, max, q;
    gint xres, yres, i, j, nn;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, NULL))
        return;
    g_return_if_fail(GWY_IS_DATA_LINE(target_line));
    xres = data_field->xres;
    yres = data_field->yres;

    if (mask) {
        nn = 0;
        if (orientation == GWY_ORIENTATION_VERTICAL) {
            for (i = 0; i < height-1; i++) {
                mrow = mask->data + (i + row)*xres + col;
                for (j = 0; j < width; j++)
                    nn += (mrow[j] > 0.0)*(mrow[j+xres] > 0.0);
            }
        }
        else {
            for (i = 0; i < height; i++) {
                mrow = mask->data + (i + row)*xres + col;
                for (j = 0; j < width-1; j++)
                    nn += (mrow[j] > 0.0)*(mrow[j+1] > 0.0);
            }
        }
    }
    else {
        if (orientation == GWY_ORIENTATION_VERTICAL)
            nn = width*(height - 1);
        else
            nn = (width - 1)*height;
    }

    if (nstats < 1) {
        nstats = floor(3.49*cbrt(nn) + 0.5);
        nstats = MAX(nstats, 2);
    }

    gwy_data_line_resample(target_line, nstats, GWY_INTERPOLATION_NONE);

    if (orientation == GWY_ORIENTATION_VERTICAL)
        q = yres/data_field->yreal;
    else
        q = xres/data_field->xreal;

    values = g_new(gdouble, MAX(nn, 1));
    nn = 0;
    if (mask) {
        if (orientation == GWY_ORIENTATION_VERTICAL) {
            for (i = 0; i < height-1; i++) {
                mrow = mask->data + (i + row)*xres + col;
                drow = data_field->data + (i + row)*xres + col;
                for (j = 0; j < width; j++) {
                    if (mrow[j] > 0.0 && mrow[j+xres] > 0.0)
                        values[nn++] = q*(drow[j+xres] - drow[j]);
                }
            }
        }
        else {
            for (i = 0; i < height; i++) {
                mrow = mask->data + (i + row)*xres + col;
                drow = data_field->data + (i + row)*xres + col;
                for (j = 0; j < width-1; j++) {
                    if (mrow[j] > 0.0 && mrow[j+1] > 0.0)
                        values[nn++] = q*(drow[j+1] - drow[j]);
                }
            }
        }
    }
    else {
        if (orientation == GWY_ORIENTATION_VERTICAL) {
            for (i = 0; i < height-1; i++) {
                drow = data_field->data + (i + row)*xres + col;
                for (j = 0; j < width; j++)
                    values[nn++] = q*(drow[j+xres] - drow[j]);
            }
        }
        else {
            for (i = 0; i < height; i++) {
                drow = data_field->data + (i + row)*xres + col;
                for (j = 0; j < width-1; j++)
                    values[nn++] = q*(drow[j+1] - drow[j]);
            }
        }
    }

    min = max = values[0];
    for (i = 1; i < nn; i++) {
        if (values[i] < min)
            min = values[i];
        if (values[i] > max)
            max = values[i];
    }

    /* Handle border cases */
    if (!nn) {
        gwy_data_line_clear(target_line);
        gwy_data_line_set_real(target_line, 1.0);
        gwy_data_line_set_offset(target_line, -0.5);
        return;
    }
    if (min == max) {
        gwy_data_line_clear(target_line);
        gwy_data_line_set_real(target_line, min ? fabs(max) : 1.0);
        target_line->data[0] = nstats/gwy_data_line_get_real(target_line);
        return;
    }

    counts = g_new(guint, nstats);
    nn = gwy_math_histogram(values, nn, min, max, nstats, counts);
    g_free(values);

    for (i = 0; i < nstats; i++)
        target_line->data[i] = counts[i];
    g_free(counts);

    gwy_data_line_set_real(target_line, max - min);
    gwy_data_line_set_offset(target_line, min);
    gwy_data_line_multiply(target_line, nstats/(max - min)/nn);

    /* Set proper units */
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    gwy_si_unit_divide(gwy_data_field_get_si_unit_z(data_field), gwy_data_field_get_si_unit_xy(data_field), lineunit);
    rhounit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_power(lineunit, -1, rhounit);
}

/**
 * gwy_data_field_area_da:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates distribution of slopes in a rectangular part of data field.
 **/
void
gwy_data_field_area_da(GwyDataField *data_field,
                       GwyDataLine *target_line,
                       gint col, gint row,
                       gint width, gint height,
                       GwyOrientation orientation,
                       gint nstats)
{
    gwy_data_field_area_da_mask(data_field, NULL, target_line, col, row, width, height, orientation, nstats);
}

/**
 * gwy_data_field_da:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates distribution of slopes in a data field.
 **/
void
gwy_data_field_da(GwyDataField *data_field,
                  GwyDataLine *target_line,
                  GwyOrientation orientation,
                  gint nstats)
{
    gwy_data_field_area_da(data_field, target_line, 0, 0, data_field->xres, data_field->yres, orientation, nstats);
}

/**
 * gwy_data_field_area_cda:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates cumulative distribution of slopes in a rectangular part of data field.
 **/
void
gwy_data_field_area_cda(GwyDataField *data_field,
                        GwyDataLine *target_line,
                        gint col, gint row,
                        gint width, gint height,
                        GwyOrientation orientation,
                        gint nstats)
{
    gwy_data_field_area_cda_mask(data_field, NULL, target_line, col, row, width, height, orientation, nstats);
}

/**
 * gwy_data_field_area_cda_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account, or %NULL.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates cumulative distribution of slopes in a rectangular part of data field, with masking.
 *
 * Since: 2.49
 **/
void
gwy_data_field_area_cda_mask(GwyDataField *data_field,
                             GwyDataField *mask,
                             GwyDataLine *target_line,
                             gint col, gint row,
                             gint width, gint height,
                             GwyOrientation orientation,
                             gint nstats)
{
    GwySIUnit *lineunit, *rhounit;

    gwy_data_field_area_da_mask(data_field, mask, target_line, col, row, width, height, orientation, nstats);
    gwy_data_line_cumulate(target_line);
    gwy_data_line_multiply(target_line, gwy_data_line_itor(target_line, 1));
    target_line->data[target_line->res-1] = 1.0;   /* Fix rounding errors. */

    /* Update units after integration */
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    rhounit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_multiply(rhounit, lineunit, rhounit);
}

/**
 * gwy_data_field_cda:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @orientation: Orientation to compute the slope distribution in.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates cumulative distribution of slopes in a data field.
 **/
void
gwy_data_field_cda(GwyDataField *data_field,
                   GwyDataLine *target_line,
                   GwyOrientation orientation,
                   gint nstats)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_cda(data_field, target_line, 0, 0, data_field->xres, data_field->yres, orientation, nstats);
}

/**
 * gwy_data_field_area_minkowski_volume:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski volume functional of a rectangular part of a data field.
 *
 * Volume functional is calculated as the number of values above each threshold value (,white pixels`) divided by the
 * total number of samples in the area.  Is it's equivalent to 1-CDH.
 **/
void
gwy_data_field_area_minkowski_volume(GwyDataField *data_field,
                                     GwyDataLine *target_line,
                                     gint col, gint row,
                                     gint width, gint height,
                                     gint nstats)
{
    gwy_data_field_area_cdh(data_field, NULL, target_line, col, row, width, height, nstats);
    gwy_data_line_multiply(target_line, -1.0);
    gwy_data_line_add(target_line, 1.0);
}

/**
 * gwy_data_field_minkowski_volume:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski volume functional of a data field.
 *
 * See gwy_data_field_area_minkowski_volume() for details.
 **/
void
gwy_data_field_minkowski_volume(GwyDataField *data_field,
                                GwyDataLine *target_line,
                                gint nstats)
{
    gwy_data_field_cdh(data_field, target_line, nstats);
    gwy_data_line_multiply(target_line, -1.0);
    gwy_data_line_add(target_line, 1.0);
}

/**
 * gwy_data_field_area_minkowski_boundary:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski boundary functional of a rectangular part of a data field.
 *
 * Boundary functional is calculated as the number of boundaries for each threshold value (the number of pixel sides
 * where of neighouring pixels is ,white` and the other ,black`) divided by the total number of samples in the area.
 **/
void
gwy_data_field_area_minkowski_boundary(GwyDataField *data_field,
                                       GwyDataLine *target_line,
                                       gint col, gint row,
                                       gint width, gint height,
                                       gint nstats)
{
    GwySIUnit *fieldunit, *lineunit;
    const gdouble *data;
    gdouble *line;
    gdouble min, max, q;
    gint xres;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE))
        return;
    g_return_if_fail(GWY_IS_DATA_LINE(target_line));

    if (nstats < 1) {
        nstats = floor(3.49*cbrt(width*height) + 0.5);
        nstats = MAX(nstats, 2);
    }

    gwy_data_line_resample(target_line, nstats, GWY_INTERPOLATION_NONE);
    gwy_data_line_clear(target_line);
    gwy_data_field_area_get_min_max_mask(data_field, NULL, GWY_MASK_INCLUDE, col, row, width, height, &min, &max);
    /* There are no boundaries on a totally flat sufrace */
    if (min == max || width == 0 || height == 0)
        return;

    xres = data_field->xres;
    q = nstats/(max - min);
    line = target_line->data;
    data = data_field->data;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(data,line,xres,width,height,row,col,nstats,min,q)
#endif
    {
        gdouble *tline = gwy_omp_if_threads_new0(line, nstats);
        gint ifrom = gwy_omp_chunk_start(height-1);
        gint ito = gwy_omp_chunk_end(height-1);
        gint i, j;

        for (i = ifrom; i < ito; i++) {
            gint kr = (gint)((data[i*xres + col] - min)*q);
            for (j = 0; j < width-1; j++) {
                const gdouble *drow = data + (i + row)*xres + (col + j);
                gint kd, k, kfrom, kto, k0 = kr;

                kr = (gint)((drow[1] - min)*q);
                kfrom = MAX(MIN(k0, kr), 0);
                kto = MIN(MAX(k0, kr), nstats);
                for (k = kfrom; k < kto; k++)
                    tline[k] += 1.0;

                kd = (gint)((drow[xres] - min)*q);
                kfrom = MAX(MIN(k0, kd), 0);
                kto = MIN(MAX(k0, kd), nstats);
                for (k = kfrom; k < kto; k++)
                    tline[k] += 1.0;
            }
        }

        gwy_omp_if_threads_sum_double(line, tline, nstats);
    }

    gwy_data_line_multiply(target_line, 1.0/(width*height));
    gwy_data_line_set_real(target_line, max - min);
    gwy_data_line_set_offset(target_line, min);

    /* Set proper units */
    fieldunit = gwy_data_field_get_si_unit_z(data_field);
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    gwy_si_unit_assign(lineunit, fieldunit);
    lineunit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_set_from_string(lineunit, NULL);
}

/**
 * gwy_data_field_minkowski_boundary:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski boundary functional of a data field.
 *
 * See gwy_data_field_area_minkowski_boundary() for details.
 **/
void
gwy_data_field_minkowski_boundary(GwyDataField *data_field,
                                  GwyDataLine *target_line,
                                  gint nstats)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_minkowski_boundary(data_field, target_line, 0, 0, data_field->xres, data_field->yres, nstats);
}

/**
 * gwy_data_field_area_minkowski_euler:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski connectivity functional (Euler characteristics) of a rectangular part of a data field.
 *
 * Connectivity functional is calculated as the number connected areas of pixels above threhsold (,white`) minus the
 * number of connected areas of pixels below threhsold (,black`) for each threshold value, divided by the total number
 * of samples in the area.
 **/
void
gwy_data_field_area_minkowski_euler(GwyDataField *data_field,
                                    GwyDataLine *target_line,
                                    gint col, gint row,
                                    gint width, gint height,
                                    gint nstats)
{
    GwySIUnit *fieldunit, *lineunit;
    GwyDataLine *tmp_line;
    gint i;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE))
        return;
    g_return_if_fail(GWY_IS_DATA_LINE(target_line));

    if (nstats < 1) {
        nstats = floor(3.49*cbrt(width*height) + 0.5);
        nstats = MAX(nstats, 2);
    }

    gwy_data_line_resample(target_line, nstats, GWY_INTERPOLATION_NONE);
    tmp_line = gwy_data_line_new_alike(target_line, FALSE);

    gwy_data_field_area_grains_tgnd(data_field, target_line, col, row, width, height, FALSE, nstats);
    gwy_data_field_area_grains_tgnd(data_field, tmp_line, col, row, width, height, TRUE, nstats);

    for (i = 0; i < nstats; i++)
        target_line->data[i] -= tmp_line->data[nstats-1 - i];
    g_object_unref(tmp_line);

    gwy_data_line_multiply(target_line, 1.0/(width*height));
    gwy_data_line_invert(target_line, TRUE, FALSE);

    /* Set proper units */
    fieldunit = gwy_data_field_get_si_unit_z(data_field);
    lineunit = gwy_data_line_get_si_unit_x(target_line);
    gwy_si_unit_assign(lineunit, fieldunit);
    lineunit = gwy_data_line_get_si_unit_y(target_line);
    gwy_si_unit_set_from_string(lineunit, NULL);
}

/**
 * gwy_data_field_minkowski_euler:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to requested width.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Calculates Minkowski connectivity functional (Euler characteristics) of a data field.
 *
 * See gwy_data_field_area_minkowski_euler() for details.
 **/
void
gwy_data_field_minkowski_euler(GwyDataField *data_field,
                               GwyDataLine *target_line,
                               gint nstats)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_minkowski_euler(data_field, target_line, 0, 0, data_field->xres, data_field->yres, nstats);
}

/**
 * gwy_data_field_slope_distribution:
 * @data_field: A data field.
 * @derdist: A data line to fill with angular slope distribution. Its resolution determines resolution of the
 *           distribution.
 * @kernel_size: If positive, local plane fitting will be used for slope computation; if nonpositive, plain central
 *               derivations will be used.
 *
 * Computes angular slope distribution.
 **/
void
gwy_data_field_slope_distribution(GwyDataField *dfield,
                                  GwyDataLine *derdist,
                                  gint kernel_size)
{
    const GwyPlaneFitQuantity quantities[2] = {
        GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY,
    };
    GwyDataField *bfields[2];
    GwySIUnit *lineunit;
    gdouble *der;
    gint xres, yres, nder;

    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));
    g_return_if_fail(GWY_IS_DATA_LINE(derdist));

    /* Set proper units */
    lineunit = gwy_data_line_get_si_unit_x(derdist);
    gwy_si_unit_set_from_string(lineunit, NULL);
    lineunit = gwy_data_line_get_si_unit_y(derdist);
    gwy_si_unit_divide(gwy_data_field_get_si_unit_z(dfield), gwy_data_field_get_si_unit_xy(dfield), lineunit);

    nder = derdist->res;
    der = derdist->data;
    xres = dfield->xres;
    yres = dfield->yres;
    gwy_clear(der, nder);

    if (kernel_size > xres || kernel_size > yres)
        return;

    bfields[0] = gwy_data_field_new_alike(dfield, FALSE);
    bfields[1] = gwy_data_field_new_alike(dfield, FALSE);
    if (kernel_size > 0) {
        gwy_data_field_fit_local_planes(dfield, kernel_size, 2, quantities, bfields);
        gwy_data_field_multiply(bfields[0], xres/dfield->xreal);
        gwy_data_field_multiply(bfields[1], yres/dfield->yreal);
    }
    else
        gwy_data_field_filter_slope(dfield, bfields[0], bfields[1]);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(bfields,der,xres,yres,kernel_size,nder)
#endif
    {
        gint rowfrom = gwy_omp_chunk_start(yres+1-kernel_size) + kernel_size/2;
        gint rowto = gwy_omp_chunk_end(yres+1-kernel_size) + kernel_size/2;
        gint colfrom = kernel_size/2;
        gint colto = kernel_size/2 + xres+1-kernel_size;
        gint row, col;
        const gdouble *bxdata = bfields[0]->data;
        const gdouble *bydata = bfields[1]->data;
        gdouble *tder = gwy_omp_if_threads_new0(der, nder);

        for (row = rowfrom; row < rowto; row++) {
            for (col = colfrom; col < colto; col++) {
                gdouble bx = bxdata[row*xres + col];
                gdouble by = bydata[row*xres + col];
                gdouble phi = atan2(by, bx);
                gint iphi = (gint)floor(nder*(phi + G_PI)/(2.0*G_PI));
                iphi = CLAMP(iphi, 0, nder-1);
                tder[iphi] += sqrt(bx*bx + by*by);
            }
        }

        gwy_omp_if_threads_sum_double(der, tder, nder);
    }

    g_object_unref(bfields[0]);
    g_object_unref(bfields[1]);
}

/**
 * gwy_data_field_area_get_median:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes median value of a data field area.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_median_mask() with masking mode %GWY_MASK_INCLUDE.
 *
 * Returns: The median value.
 **/
gdouble
gwy_data_field_area_get_median(GwyDataField *dfield,
                               GwyDataField *mask,
                               gint col, gint row,
                               gint width, gint height)
{
    return gwy_data_field_area_get_median_mask(dfield, mask, GWY_MASK_INCLUDE, col, row, width, height);
}

/**
 * gwy_data_field_area_get_median_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes median value of a data field area.
 *
 * Returns: The median value.
 *
 * Since: 2.18
 **/
gdouble
gwy_data_field_area_get_median_mask(GwyDataField *dfield,
                                    GwyDataField *mask,
                                    GwyMaskingType mode,
                                    gint col, gint row,
                                    gint width, gint height)
{
    gdouble median = 0.0;
    const gdouble *datapos, *mpos;
    gdouble *buffer;
    gint i, j, xres;
    guint n;

    /* empty_is_noop=TRUE for compatibility */
    if (!_gwy_data_field_check_area(dfield, col, row, width, height, TRUE)
        || !_gwy_data_field_check_mask(dfield, &mask, &mode))
        return median;

    xres = dfield->xres;
    if (mask) {
        buffer = g_new(gdouble, width*height);
        datapos = dfield->data + row*xres + col;
        mpos = mask->data + row*xres + col;
        n = 0;
        for (i = 0; i < height; i++) {
            const gdouble *drow = datapos + i*xres;
            const gdouble *mrow = mpos + i*xres;

            if (mode == GWY_MASK_INCLUDE) {
                for (j = 0; j < width; j++) {
                    if (mrow[j] > 0.0)
                        buffer[n++] = drow[j];
                }
            }
            else {
                for (j = 0; j < width; j++) {
                    if (mrow[j] <= 0.0)
                        buffer[n++] = drow[j];
                }
            }
        }

        if (n)
            median = gwy_math_median(n, buffer);

        g_free(buffer);

        return median;
    }

    if (col == 0 && width == xres
        && row == 0 && height == dfield->yres)
        return gwy_data_field_get_median(dfield);

    buffer = g_new(gdouble, width*height);
    datapos = dfield->data + row*xres + col;
    if (height == 1 || (col == 0 && width == xres))
        gwy_assign(buffer, datapos, width*height);
    else {
        for (i = 0; i < height; i++)
            gwy_assign(buffer + i*width, datapos + i*xres, width);
    }
    median = gwy_math_median(width*height, buffer);
    g_free(buffer);

    return median;
}

/**
 * gwy_data_field_get_median:
 * @data_field: A data field.
 *
 * Computes median value of a data field.
 *
 * This quantity is cached.
 *
 * Returns: The median value.
 **/
gdouble
gwy_data_field_get_median(GwyDataField *data_field)
{
    gint xres, yres;
    gdouble *buffer;
    gdouble med;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);

    gwy_debug("%s", CTEST(data_field, MED) ? "cache" : "lame");
    if (CTEST(data_field, MED))
        return CVAL(data_field, MED);

    xres = data_field->xres;
    yres = data_field->yres;
    buffer = g_memdup(data_field->data, xres*yres*sizeof(gdouble));
    med = gwy_math_median(xres*yres, buffer);
    g_free(buffer);

    CVAL(data_field, MED) = med;
    data_field->cached |= CBIT(MED);

    return med;
}

/**
 * gwy_data_field_area_get_normal_coeffs_mask:
 * @field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @masking: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nx: Where x-component of average normal vector should be stored, or %NULL.
 * @ny: Where y-component of average normal vector should be stored, or %NULL.
 * @nz: Where z-component of average normal vector should be stored, or %NULL.
 *
 * Computes average normal vector of an area of a data field, with masking.
 *
 * Values outside the area or excluded by masking never enter the computation. This is different from functions like
 * gwy_data_field_area_get_normal_coeffs() which can use outside pixels for two-side derivatives. Two-side
 * derivatives are used for interior pixels, one-sided when two-side are not possible. Normals are not calculated for
 * pixels excluded by masking, even when all their neighbours are included and two-sided derivatives do not actually
 * use the central value.
 *
 * The sign convention is also natural for the Gwyddion coordinate system.
 *
 * Returns: The number of vectors averaged. Zero means failure (typically occuring when masking did not leave any
 *          contiguous patch of pixels for computation of derivatives).
 *
 * Since: 2.64
 **/
gint
gwy_data_field_area_get_normal_coeffs_mask(GwyDataField *field,
                                           GwyDataField *mask, GwyMaskingType masking,
                                           gint col, gint row,
                                           gint width, gint height,
                                           gdouble *nx, gdouble *ny, gdouble *nz)
{
    gint xres, i, j, n;
    gdouble sumdx, sumdy, sumdz;
    gdouble dx, dy;
    const gdouble *d, *m;

    if (!_gwy_data_field_check_area(field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(field, &mask, &masking))
        return 0;

    /* This probably should not be enforced */
    /* g_return_if_fail(gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(a), gwy_data_field_get_si_unit_z(a)), FALSE); */

    xres = field->xres;
    d = gwy_data_field_get_data(field) + row*xres + col;
    m = mask ? gwy_data_field_get_data(mask) + row*xres + col : NULL;
    dx = gwy_data_field_get_dx(field);
    dy = gwy_data_field_get_dy(field);
    sumdx = sumdy = sumdz = 0.0;
    n = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sumdx,sumdy,sumdz,n) \
            private(i,j) \
            shared(d,m,width,height,col,row,xres,masking,dx,dy)
#endif
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            gint k = i*xres + j;
            gboolean has_c = (!m
                              || (masking == GWY_MASK_INCLUDE && m[k] >= 1.0)
                              || (masking == GWY_MASK_EXCLUDE && m[k] <= 0.0));
            gboolean has_ym = (i > 0) && (!m
                                          || (masking == GWY_MASK_INCLUDE && m[k-xres] >= 1.0)
                                          || (masking == GWY_MASK_EXCLUDE && m[k-xres] <= 0.0));
            gboolean has_yp = (i < height-1) && (!m
                                                 || (masking == GWY_MASK_INCLUDE && m[k+xres] >= 1.0)
                                                 || (masking == GWY_MASK_EXCLUDE && m[k+xres] <= 0.0));
            gboolean has_xm = (j > 0) && (!m
                                          || (masking == GWY_MASK_INCLUDE && m[k-1] >= 1.0)
                                          || (masking == GWY_MASK_EXCLUDE && m[k-1] <= 0.0));
            gboolean has_xp = (j < width-1) && (!m
                                                || (masking == GWY_MASK_INCLUDE && m[k+1] >= 1.0)
                                                || (masking == GWY_MASK_EXCLUDE && m[k+1] <= 0.0));
            gdouble dzx, dzy, dcx, dcy, dcz, dd;

            if (!has_c)
                continue;

            if (has_xm)
                dzx = has_xp ? 0.5*(d[k+1] - d[k-1]) : d[k] - d[k-1];
            else if (has_xp)
                dzx = d[k+1] - d[k];
            else
                continue;

            if (has_ym)
                dzy = has_yp ? 0.5*(d[k+xres] - d[k-xres]) : d[k] - d[k-xres];
            else if (has_yp)
                dzy = d[k+xres] - d[k];
            else
                continue;

            /* Cross product = normal vector */
            dcx = -dy*dzx;
            dcy = -dx*dzy;
            dcz = dx*dy;

            /* Normalize and add */
            dd = sqrt(dcx*dcx + dcy*dcy + dcz*dcz);
            sumdx += dcx/dd;
            sumdy += dcy/dd;
            sumdz += dcz/dd;
            n++;
        }
    }

    /* average dimensionless normal vector */
    if (n) {
        gdouble len = sqrt(sumdx*sumdx + sumdy*sumdy + sumdz*sumdz);
        sumdx /= len;
        sumdy /= len;
        sumdz /= len;
    }
    else {
        sumdx = sumdy = 0.0;
        sumdz = 1.0;
    }

    if (nx)
        *nx = sumdx;
    if (ny)
        *ny = sumdy;
    if (nz)
        *nz = sumdz;

    return n;
}

/**
 * gwy_data_field_area_get_normal_coeffs:
 * @data_field: A data field.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nx: Where x-component of average normal vector should be stored, or %NULL.
 * @ny: Where y-component of average normal vector should be stored, or %NULL.
 * @nz: Where z-component of average normal vector should be stored, or %NULL.
 * @normalize1: %TRUE to normalize the normal vector to 1, %FALSE to normalize the vector so that @z-component is 1.
 *
 * Computes average normal vector of an area of a data field.
 *
 * The function can use outside pixels for two-side derivatives. See gwy_data_field_area_get_normal_coeffs_mask() for
 * a function which always honours the boundaries.
 **/
void
gwy_data_field_area_get_normal_coeffs(GwyDataField *data_field,
                                      gint col, gint row,
                                      gint width, gint height,
                                      gdouble *nx, gdouble *ny, gdouble *nz,
                                      gboolean normalize1)
{
    gint i, j, n;
    gdouble sumdx, sumdy, sumdz, sumw;
    gdouble avgdx, avgdy, avgdz;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE))
        return;

    /* This probably should not be enforced */
    /*
    g_return_if_fail(gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(a),
                                       gwy_data_field_get_si_unit_z(a)),
                     FALSE);
                     */

    sumdx = sumdy = sumdz = sumw = 0.0;
    n = 0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sumdx,sumdy,sumdz,sumw,n) \
            private(i,j) \
            shared(data_field,width,height,col,row)
#endif
    for (i = col; i < col + width; i++) {
        for (j = row; j < row + height; j++) {
            gdouble d1x = 1.0;
            gdouble d1y = 0.0;
            gdouble d1z = gwy_data_field_get_xder(data_field, i, j);
            gdouble d2x = 0.0;
            gdouble d2y = 1.0;
            gdouble d2z = gwy_data_field_get_yder(data_field, i, j);
            /* Cross product = normal vector */
            gdouble dcx = d1y*d2z - d1z*d2y;
            gdouble dcy = d1z*d2x - d1x*d2z;
            gdouble dcz = d1x*d2y - d1y*d2x; /* Always 1 */
            /* Normalize and add */
            gdouble dd = sqrt(dcx*dcx + dcy*dcy + dcz*dcz);
            dcx /= dd;
            sumdx += dcx;
            dcy /= dd;
            sumdy += dcy;
            dcz /= dd;
            sumdz += dcz;
            sumw += 1.0/dd;
            n++;
        }
    }
    /* average dimensionless normal vector */
    if (normalize1) {
        /* normalize to 1 */
        avgdx = sumdx/n;
        avgdy = sumdy/n;
        avgdz = sumdz/n;
    }
    else {
        /* normalize for gwy_data_field_plane_level */
        avgdx = sumdx/sumw;
        avgdy = sumdy/sumw;
        avgdz = sumdz/sumw;
    }

    if (nx)
        *nx = avgdx;
    if (ny)
        *ny = avgdy;
    if (nz)
        *nz = avgdz;
}

/**
 * gwy_data_field_get_normal_coeffs:
 * @data_field: A data field.
 * @nx: Where x-component of average normal vector should be stored, or %NULL.
 * @ny: Where y-component of average normal vector should be stored, or %NULL.
 * @nz: Where z-component of average normal vector should be stored, or %NULL.
 * @normalize1: %TRUE to normalize the normal vector to 1, %FALSE to normalize the vector so that @z-component is 1.
 *
 * Computes average normal vector of a data field.
 **/
void
gwy_data_field_get_normal_coeffs(GwyDataField *data_field,
                                 gdouble *nx, gdouble *ny, gdouble *nz,
                                 gboolean normalize1)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_get_normal_coeffs(data_field, 0, 0, data_field->xres, data_field->yres, nx, ny, nz, normalize1);
}

/**
 * gwy_data_field_area_get_inclination:
 * @data_field: A data field.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @theta: Where theta angle (in radians) should be stored, or %NULL.
 * @phi: Where phi angle (in radians) should be stored, or %NULL.
 *
 * Calculates the inclination of the image (polar and azimuth angle).
 **/
void
gwy_data_field_area_get_inclination(GwyDataField *data_field,
                                    gint col, gint row,
                                    gint width, gint height,
                                    gdouble *theta,
                                    gdouble *phi)
{
    gdouble nx, ny, nz, nr;

    gwy_data_field_area_get_normal_coeffs(data_field, col, row, width, height, &nx, &ny, &nz, TRUE);

    nr = hypot(nx, ny);
    if (theta)
        *theta = atan2(nr, nz);
    if (phi)
        *phi = atan2(ny, nx);
}


/**
 * gwy_data_field_get_inclination:
 * @data_field: A data field.
 * @theta: Where theta angle (in radians) should be stored, or %NULL.
 * @phi: Where phi angle (in radians) should be stored, or %NULL.
 *
 * Calculates the inclination of the image (polar and azimuth angle).
 **/
void
gwy_data_field_get_inclination(GwyDataField *data_field,
                               gdouble *theta,
                               gdouble *phi)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_get_inclination(data_field, 0, 0, data_field->xres, data_field->yres, theta, phi);
}

static gint
extract_field_row_masked(GwyDataField *dfield,
                         GwyDataField *mask,
                         GwyMaskingType masking,
                         gdouble *values,
                         gint col, gint row, gint width,
                         gboolean replace_masked,
                         gdouble filler)
{
    gint xres = dfield->xres;
    const gdouble *d = dfield->data + row*xres + col, *m;
    gint i, n = width;

    if (masking == GWY_MASK_INCLUDE) {
        m = mask->data + row*xres + col;
        if (replace_masked) {
            for (i = 0; i < width; i++)
                values[i] = (m[i] > 0.0) ? d[i] : filler;
        }
        else {
            for (i = n = 0; i < width; i++) {
                if (m[i] > 0.0)
                    values[n++] = d[i];
            }
        }
    }
    else if (masking == GWY_MASK_EXCLUDE) {
        m = mask->data + row*xres + col;
        if (replace_masked) {
            for (i = 0; i < width; i++)
                values[i] = (m[i] <= 0.0) ? d[i] : filler;
        }
        else {
            for (i = n = 0; i < width; i++) {
                if (m[i] <= 0.0)
                    values[n++] = d[i];
            }
        }
    }
    else {
        gwy_assign(values, d, n);
    }

    return n;
}

static gint
extract_field_column_masked(GwyDataField *dfield,
                            GwyDataField *mask,
                            GwyMaskingType masking,
                            gdouble *values,
                            gint col, gint row, gint height,
                            gboolean replace_masked,
                            gdouble filler)
{
    gint xres = dfield->xres;
    const gdouble *d = dfield->data + row*xres + col, *m;
    gint i, n = height;

    if (masking == GWY_MASK_INCLUDE) {
        m = mask->data + row*xres + col;
        if (replace_masked) {
            for (i = 0; i < height; i++)
                values[i] = (m[xres*i] > 0.0) ? d[xres*i] : filler;
        }
        else {
            for (i = n = 0; i < height; i++) {
                if (m[xres*i] > 0.0)
                    values[n++] = d[xres*i];
            }
        }
    }
    else if (masking == GWY_MASK_EXCLUDE) {
        m = mask->data + row*xres + col;
        if (replace_masked) {
            for (i = 0; i < height; i++)
                values[i] = (m[xres*i] <= 0.0) ? d[xres*i] : filler;
        }
        else {
            for (i = n = 0; i < height; i++) {
                if (m[xres*i] <= 0.0)
                    values[n++] = d[xres*i];
            }
        }
    }
    else {
        for (i = 0; i < height; i++)
            values[i] = d[xres*i];
    }

    return n;
}

static void
calc_field_row_linestat_masked(GwyDataField *dfield,
                               GwyDataField *mask,
                               GwyMaskingType masking,
                               GwyDataLine *dline,
                               GwyDataLine *weights,
                               LineStatFunc func,
                               gboolean replace_masked,
                               gdouble filler_value,
                               gint col, gint row,
                               gint width, gint height)
{
    gdouble *ldata, *wdata;
    gdouble dx = gwy_data_field_get_dx(dfield);

    gwy_data_line_resample(dline, height, GWY_INTERPOLATION_NONE);
    gwy_data_line_set_real(dline, gwy_data_field_itor(dfield, height));
    gwy_data_line_set_offset(dline, gwy_data_field_itor(dfield, row) + gwy_data_field_get_yoffset(dfield));
    ldata = dline->data;

    if (weights) {
        gwy_data_line_resample(weights, height, GWY_INTERPOLATION_NONE);
        gwy_data_line_set_real(weights, gwy_data_field_itor(dfield, height));
        gwy_data_line_set_offset(weights, gwy_data_field_itor(dfield, row) + gwy_data_field_get_yoffset(dfield));
        gwy_data_line_clear(weights);
        wdata = weights->data;
    }
    else
        wdata = NULL;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(dfield,ldata,wdata,mask,masking,width,height,col,row,dx,func,filler_value,replace_masked)
#endif
    {
        GwyDataLine *buf = gwy_data_line_new(width, width*dx, FALSE);
        gdouble *bufdata = buf->data;
        gint ifrom = gwy_omp_chunk_start(height);
        gint ito = gwy_omp_chunk_end(height);
        gint i, n;

        for (i = ifrom; i < ito; i++) {
            n = extract_field_row_masked(dfield, mask, masking, bufdata, col, row + i, width,
                                         replace_masked, filler_value);
            if (n) {
                /* Temporarily shorten the dataline to avoid reallocations. */
                buf->res = n;
                buf->real = n*dx;
                ldata[i] = func(buf);
                buf->res = width;
                buf->real = width*dx;
                if (wdata)
                    wdata[i] = n;
            }
            else
                ldata[i] = filler_value;
        }

        g_object_unref(buf);
    }
}

static void
calc_field_column_linestat_masked(GwyDataField *dfield,
                                  GwyDataField *mask,
                                  GwyMaskingType masking,
                                  GwyDataLine *dline,
                                  GwyDataLine *weights,
                                  LineStatFunc func,
                                  gboolean replace_masked,
                                  gdouble filler_value,
                                  gint col, gint row,
                                  gint width, gint height)
{
    gdouble *ldata, *wdata;
    gdouble dy = gwy_data_field_get_dy(dfield);

    gwy_data_line_resample(dline, width, GWY_INTERPOLATION_NONE);
    gwy_data_line_set_real(dline, gwy_data_field_jtor(dfield, width));
    gwy_data_line_set_offset(dline, gwy_data_field_jtor(dfield, col) + gwy_data_field_get_xoffset(dfield));
    ldata = dline->data;

    if (weights) {
        gwy_data_line_resample(weights, width, GWY_INTERPOLATION_NONE);
        gwy_data_line_set_real(weights, gwy_data_field_jtor(dfield, width));
        gwy_data_line_set_offset(weights, gwy_data_field_jtor(dfield, col) + gwy_data_field_get_xoffset(dfield));
        gwy_data_line_clear(weights);
        wdata = weights->data;
    }
    else
        wdata = NULL;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(dfield,ldata,wdata,mask,masking,width,height,col,row,dy,func,filler_value,replace_masked)
#endif
    {
        GwyDataLine *buf = buf = gwy_data_line_new(height, height*dy, FALSE);
        gdouble *bufdata = buf->data;
        gint ifrom = gwy_omp_chunk_start(width);
        gint ito = gwy_omp_chunk_end(width);
        gint i, n;

        for (i = ifrom; i < ito; i++) {
            n = extract_field_column_masked(dfield, mask, masking, bufdata, col + i, row, height,
                                            replace_masked, filler_value);
            if (n) {
                /* Temporarily shorten the dataline to avoid reallocations. */
                buf->res = n;
                buf->real = n*dy;
                ldata[i] = func(buf);
                buf->res = height;
                buf->real = height*dy;
                if (wdata)
                    wdata[i] = n;
            }
            else
                ldata[i] = filler_value;
        }

        g_object_unref(buf);
    }
}

static gdouble
gwy_data_line_get_slope(GwyDataLine *dline)
{
    gdouble v;

    gwy_data_line_get_line_coeffs(dline, NULL, &v);
    return v*dline->res/dline->real;
}

static gdouble
gwy_data_line_get_range(GwyDataLine *dline)
{
    gdouble min, max;

    gwy_data_line_get_min_max(dline, &min, &max);
    return max - min;
}

static gdouble
gwy_data_line_get_median_destructive(GwyDataLine *dline)
{
    return gwy_math_median(dline->res, dline->data);
}

static gdouble
gwy_data_line_get_Rt_destructive(GwyDataLine *dline)
{
    gwy_data_line_add(dline, -gwy_data_line_get_avg(dline));
    return gwy_data_line_get_xtm(dline, 1, 1);
}

static gdouble
gwy_data_line_get_Rz_destructive(GwyDataLine *dline)
{
    gwy_data_line_add(dline, -gwy_data_line_get_avg(dline));
    return gwy_data_line_get_xtm(dline, 5, 1);
}

/**
 * gwy_data_field_get_line_stats_mask:
 * @data_field: A data field.
 * @mask: Mask of values to take values into account, or %NULL for full @data_field.
 * @masking: Masking mode to use.  See the introduction for description of masking modes.
 * @target_line: A data line to store the distribution to.  It will be resampled to the number of rows (columns).
 * @weights: A data line to store number of data points contributing to each value in @target_line, or %NULL.  It is
 *           useful when masking is used to possibly exclude values calculated from too few data points.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @quantity: The line quantity to calulate for each row (column).
 * @orientation: Line orientation.  For %GWY_ORIENTATION_HORIZONTAL each @target_line point corresponds to a row of
 *               the area, for %GWY_ORIENTATION_VERTICAL each @target_line point corresponds to a column of the area.
 *
 * Calculates a line quantity for each row or column in a data field area.
 *
 * Since: 2.46
 **/
void
gwy_data_field_get_line_stats_mask(GwyDataField *data_field,
                                   GwyDataField *mask,
                                   GwyMaskingType masking,
                                   GwyDataLine *target_line,
                                   GwyDataLine *weights,
                                   gint col, gint row,
                                   gint width, gint height,
                                   GwyLineStatQuantity quantity,
                                   GwyOrientation orientation)
{
    static const LineStatFunc funcs[] = {
        gwy_data_line_get_avg,
        gwy_data_line_get_median_destructive,
        gwy_data_line_get_min,
        gwy_data_line_get_max,
        gwy_data_line_get_rms,
        gwy_data_line_get_length,
        gwy_data_line_get_slope,
        gwy_data_line_get_tan_beta0,
        gwy_data_line_get_ra,
        gwy_data_line_get_Rz_destructive,
        gwy_data_line_get_Rt_destructive,
        gwy_data_line_get_skew,
        gwy_data_line_get_kurtosis,
        gwy_data_line_get_range,
        gwy_data_line_get_variation,
        gwy_data_line_min_pos_r,
        gwy_data_line_max_pos_r,
    };

    LineStatFunc func;
    GwySIUnit *zunit, *xyunit, *lunit;
    gboolean replace_masked = FALSE;
    gdouble filler_value = 0.0;

    g_return_if_fail(quantity < G_N_ELEMENTS(funcs));
    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &masking))
        return;
    g_return_if_fail(GWY_IS_DATA_LINE(target_line));

    func = funcs[quantity];
    if (quantity == GWY_LINE_STAT_MINPOS) {
        replace_masked = TRUE;
        filler_value = G_MAXDOUBLE;
    }
    else if (quantity == GWY_LINE_STAT_MAXPOS) {
        replace_masked = TRUE;
        filler_value = -G_MAXDOUBLE;
    }

    if (orientation == GWY_ORIENTATION_VERTICAL) {
        calc_field_column_linestat_masked(data_field, mask, masking, target_line, weights, func,
                                          replace_masked, filler_value, col, row, width, height);
    }
    else {
        calc_field_row_linestat_masked(data_field, mask, masking, target_line, weights, func,
                                       replace_masked, filler_value, col, row, width, height);
    }

    xyunit = gwy_data_field_get_si_unit_xy(data_field);
    zunit = gwy_data_field_get_si_unit_z(data_field);

    lunit = gwy_data_line_get_si_unit_x(target_line);
    gwy_si_unit_assign(lunit, xyunit);

    lunit = gwy_data_line_get_si_unit_y(target_line);
    switch (quantity) {
        case GWY_LINE_STAT_LENGTH:
        if (!gwy_si_unit_equal(xyunit, zunit))
            g_warning("Length makes no sense when lateral and value units differ");
        case GWY_LINE_STAT_MEAN:
        case GWY_LINE_STAT_MEDIAN:
        case GWY_LINE_STAT_MINIMUM:
        case GWY_LINE_STAT_MAXIMUM:
        case GWY_LINE_STAT_RMS:
        case GWY_LINE_STAT_RA:
        case GWY_LINE_STAT_RT:
        case GWY_LINE_STAT_RZ:
        case GWY_LINE_STAT_RANGE:
        case GWY_LINE_STAT_VARIATION:
        gwy_si_unit_assign(lunit, zunit);
        break;

        case GWY_LINE_STAT_MINPOS:
        case GWY_LINE_STAT_MAXPOS:
        gwy_si_unit_assign(lunit, xyunit);
        break;

        case GWY_LINE_STAT_SLOPE:
        case GWY_LINE_STAT_TAN_BETA0:
        case GWY_LINE_STAT_SKEW:
        case GWY_LINE_STAT_KURTOSIS:
        gwy_si_unit_divide(zunit, xyunit, lunit);
        break;

        default:
        g_assert_not_reached();
        break;
    }

    if (weights) {
        lunit = gwy_data_line_get_si_unit_x(weights);
        gwy_si_unit_assign(lunit, xyunit);
        lunit = gwy_data_line_get_si_unit_y(weights);
        gwy_si_unit_set_from_string(lunit, NULL);
    }
}

/**
 * gwy_data_field_area_get_line_stats:
 * @data_field: A data field.
 * @mask: Mask of values to take values into account, or %NULL for full @data_field.
 * @target_line: A data line to store the distribution to.  It will be resampled to the number of rows (columns).
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @quantity: The line quantity to calulate for each row (column).
 * @orientation: Line orientation.  For %GWY_ORIENTATION_HORIZONTAL each @target_line point corresponds to a row of
 *               the area, for %GWY_ORIENTATION_VERTICAL each @target_line point corresponds to a column of the area.
 *
 * Calculates a line quantity for each row or column in a data field area.
 *
 * Use gwy_data_field_get_line_stats_mask() for full masking type options.
 *
 * Since: 2.2
 **/
void
gwy_data_field_area_get_line_stats(GwyDataField *data_field,
                                   GwyDataField *mask,
                                   GwyDataLine *target_line,
                                   gint col, gint row,
                                   gint width, gint height,
                                   GwyLineStatQuantity quantity,
                                   GwyOrientation orientation)
{
    gwy_data_field_get_line_stats_mask(data_field, mask, GWY_MASK_INCLUDE, target_line, NULL, col, row, width, height,
                                       quantity, orientation);
}

/**
 * gwy_data_field_get_line_stats:
 * @data_field: A data field.
 * @target_line: A data line to store the distribution to.  It will be resampled to @data_field height (width).
 * @quantity: The line quantity to calulate for each row (column).
 * @orientation: Line orientation.  See gwy_data_field_area_get_line_stats().
 *
 * Calculates a line quantity for each row or column of a data field.
 *
 * Since: 2.2
 **/
void
gwy_data_field_get_line_stats(GwyDataField *data_field,
                              GwyDataLine *target_line,
                              GwyLineStatQuantity quantity,
                              GwyOrientation orientation)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_get_line_stats(data_field, NULL, target_line, 0, 0, data_field->xres, data_field->yres,
                                       quantity, orientation);
}

/**
 * gwy_data_field_count_maxima:
 * @data_field: A data field.
 *
 * Counts the number of regional maxima in a data field.
 *
 * See gwy_data_field_mark_extrema() for the definition of a regional maximum.
 *
 * Returns: The number of regional maxima.
 *
 * Since: 2.38
 **/
guint
gwy_data_field_count_maxima(GwyDataField *data_field)
{
    GwyDataField *mask;
    gint *grains;
    guint ngrains;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);
    mask = gwy_data_field_new_alike(data_field, FALSE);
    gwy_data_field_mark_extrema(data_field, mask, TRUE);
    grains = g_new0(gint, data_field->xres*data_field->yres);
    ngrains = gwy_data_field_number_grains(mask, grains);
    g_free(grains);
    g_object_unref(mask);
    return ngrains;
}

/**
 * gwy_data_field_count_minima:
 * @data_field: A data field
 *
 * Counts the number of regional minima in a data field.
 *
 * See gwy_data_field_mark_extrema() for the definition of a regional minimum.
 *
 * Returns: The number of regional minima.
 *
 * Since: 2.38
 **/
guint
gwy_data_field_count_minima(GwyDataField *data_field)
{
    GwyDataField *mask;
    gint *grains;
    guint ngrains;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);
    mask = gwy_data_field_new_alike(data_field, FALSE);
    gwy_data_field_mark_extrema(data_field, mask, FALSE);
    grains = g_new0(gint, data_field->xres*data_field->yres);
    ngrains = gwy_data_field_number_grains(mask, grains);
    g_free(grains);
    g_object_unref(mask);
    return ngrains;
}

/************************** Documentation ****************************/

/**
 * SECTION:stats
 * @title: stats
 * @short_description: Two-dimensional statistical functions
 *
 * Many statistical functions permit to pass masks that determine which values in the data field to take into account
 * or ignore when calculating the statistical characteristics.  Masking mode %GWY_MASK_INCLUDE means that maks values
 * equal to 0.0 and below cause corresponding data field samples to be ignored, values equal to 1.0 and above cause
 * inclusion of corresponding data field samples.  The behaviour for values inside interval (0.0, 1.0) is undefined.
 * In mode @GWY_MASK_EXCLUDE, the meaning of mask is inverted, as if all mask values x were replaced with 1-x.  The
 * mask field is ignored in mode @GWY_MASK_IGNORE, i.e. the same behaviour occurs as with %NULL mask argument.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
