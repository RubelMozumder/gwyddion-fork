/*
 *  $Id: stats-sum.c 26068 2023-12-20 12:25:29Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

/**
 * square_area1:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @q: One fourth of rectangle projected area (x-size * ysize).
 *
 * Calculates approximate area of a one square pixel.
 *
 * Returns: The area.
 **/
static inline gdouble
square_area1(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
             gdouble q)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/4.0;
    z1 -= c;
    z2 -= c;
    z3 -= c;
    z4 -= c;

    return (sqrt(1.0 + 2.0*(z1*z1 + z2*z2)/q) + sqrt(1.0 + 2.0*(z2*z2 + z3*z3)/q)
            + sqrt(1.0 + 2.0*(z3*z3 + z4*z4)/q) + sqrt(1.0 + 2.0*(z4*z4 + z1*z1)/q));
}

/**
 * square_area1w:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @w1: Weight of first corner (0 or 1).
 * @w2: Weight of second corner (0 or 1).
 * @w3: Weight of third corner (0 or 1).
 * @w4: Weight of fourth corner (0 or 1).
 * @q: One fourth of rectangle projected area (x-size * ysize).
 *
 * Calculates approximate area of a one square pixel with some corners possibly missing.
 *
 * Returns: The area.
 **/
static inline gdouble
square_area1w(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
              gint w1, gint w2, gint w3, gint w4,
              gdouble q)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/4.0;
    z1 -= c;
    z2 -= c;
    z3 -= c;
    z4 -= c;

    return ((w1 + w2)*sqrt(1.0 + 2.0*(z1*z1 + z2*z2)/q)
            + (w2 + w3)*sqrt(1.0 + 2.0*(z2*z2 + z3*z3)/q)
            + (w3 + w4)*sqrt(1.0 + 2.0*(z3*z3 + z4*z4)/q)
            + (w4 + w1)*sqrt(1.0 + 2.0*(z4*z4 + z1*z1)/q))/2.0;
}

/**
 * square_area2:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate area of a one general rectangular pixel.
 *
 * Returns: The area.
 **/
static inline gdouble
square_area2(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
             gdouble x, gdouble y)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/2.0;

    return (sqrt(1.0 + (z1 - z2)*(z1 - z2)/x + (z1 + z2 - c)*(z1 + z2 - c)/y)
            + sqrt(1.0 + (z2 - z3)*(z2 - z3)/y + (z2 + z3 - c)*(z2 + z3 - c)/x)
            + sqrt(1.0 + (z3 - z4)*(z3 - z4)/x + (z3 + z4 - c)*(z3 + z4 - c)/y)
            + sqrt(1.0 + (z1 - z4)*(z1 - z4)/y + (z1 + z4 - c)*(z1 + z4 - c)/x));
}

/**
 * square_area2w:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @w1: Weight of first corner (0 or 1).
 * @w2: Weight of second corner (0 or 1).
 * @w3: Weight of third corner (0 or 1).
 * @w4: Weight of fourth corner (0 or 1).
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate area of a one general rectangular pixel with some
 * corners possibly missing.
 *
 * Returns: The area.
 **/
static inline gdouble
square_area2w(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
              gint w1, gint w2, gint w3, gint w4,
              gdouble x, gdouble y)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/2.0;

    return ((w1 + w2)*sqrt(1.0 + (z1 - z2)*(z1 - z2)/x + (z1 + z2 - c)*(z1 + z2 - c)/y)
            + (w2 + w3)*sqrt(1.0 + (z2 - z3)*(z2 - z3)/y + (z2 + z3 - c)*(z2 + z3 - c)/x)
            + (w3 + w4)*sqrt(1.0 + (z3 - z4)*(z3 - z4)/x + (z3 + z4 - c)*(z3 + z4 - c)/y)
            + (w4 + w1)*sqrt(1.0 + (z1 - z4)*(z1 - z4)/y + (z1 + z4 - c)*(z1 + z4 - c)/x))/2.0;
}

/**
 * stripe_area1:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 * @mode: Masking mode.
 * @q: One fourth of rectangle projected area (x-size * ysize).
 *
 * Calculates approximate area of a half-pixel stripe.
 *
 * Returns: The area.
 **/
static gdouble
stripe_area1(gint n,
             gint stride,
             const gdouble *r,
             const gdouble *rr,
             const gdouble *m,
             GwyMaskingType mode,
             gdouble q)
{
    gdouble sum = 0.0;
    gint j;

    if (m && mode != GWY_MASK_IGNORE) {
        if (mode == GWY_MASK_INCLUDE) {
            for (j = 0; j < n-1; j++) {
                sum += square_area1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                     m[j*stride] > 0.0, m[(j + 1)*stride] > 0.0, 0, 0, q);
            }
        }
        else {
            for (j = 0; j < n-1; j++) {
                sum += square_area1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                     m[j*stride] < 1.0, m[(j + 1)*stride] < 1.0, 0, 0, q);
            }
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_area1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                 1, 1, 0, 0, q);
        }
    }

    return sum;
}

/**
 * stripe_area2:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate area of a half-pixel stripe.
 *
 * Returns: The area.
 **/
static gdouble
stripe_area2(gint n,
             gint stride,
             const gdouble *r,
             const gdouble *rr,
             const gdouble *m,
             GwyMaskingType mode,
             gdouble x,
             gdouble y)
{
    gdouble sum = 0.0;
    gint j;

    if (m && mode == GWY_MASK_INCLUDE) {
        for (j = 0; j < n-1; j++) {
            sum += square_area2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                 m[j*stride] > 0.0, m[(j + 1)*stride] > 0.0, 0, 0, x, y);
        }
    }
    else if (m && mode == GWY_MASK_EXCLUDE) {
        for (j = 0; j < n-1; j++) {
            sum += square_area2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                 m[j*stride] < 1.0, m[(j + 1)*stride] < 1.0, 0, 0, x, y);
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_area2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                 1, 1, 0, 0, x, y);
        }
    }

    return sum;
}

static gdouble
calculate_surface_area(GwyDataField *dfield,
                       GwyDataField *mask,
                       GwyMaskingType mode,
                       gint col, gint row,
                       gint width, gint height)
{
    const gdouble *dataul, *maskul, *r, *m;
    gint i, j, xres, yres, s;
    gdouble x, y, q, sum = 0.0;

    /* special cases */
    if (!width || !height)
        return sum;

    xres = dfield->xres;
    yres = dfield->yres;
    x = dfield->xreal/dfield->xres;
    y = dfield->yreal/dfield->yres;
    q = x*y;
    x = x*x;
    y = y*y;
    dataul = dfield->data + xres*row + col;

    if (mask && mode != GWY_MASK_IGNORE) {
        maskul = mask->data + xres*row + col;
        if (fabs(log(x/y)) < 1e-7) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,i,j) \
            shared(dataul,maskul,xres,height,width,mode,q)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                if (mode == GWY_MASK_INCLUDE) {
                    for (j = 0; j < width-1; j++) {
                        sum += square_area1w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                             m[j] > 0.0, m[j+1] > 0.0, m[j+xres+1] > 0.0, m[j+xres] > 0.0, q);
                    }
                }
                else {
                    for (j = 0; j < width-1; j++) {
                        sum += square_area1w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                             m[j] < 1.0, m[j+1] < 1.0, m[j+xres+1] < 1.0, m[j+xres] < 1.0, q);
                    }
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_area1(width, 1, dataul, dataul - s*xres, maskul, mode, q);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_area1(width, 1,
                                dataul + xres*(height-1),
                                dataul + xres*(height-1 + s),
                                maskul + xres*(height-1), mode, q);

            /* Left column */
            s = !(col == 0);
            sum += stripe_area1(height, xres, dataul, dataul - s, maskul, mode, q);
            /* Right column */
            s = !(col + width == xres);
            sum += stripe_area1(height, xres, dataul + width-1, dataul + width-1 + s, maskul + width-1, mode, q);
        }
        else {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,i,j) \
            shared(dataul,maskul,xres,height,width,mode,x,y)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                if (mode == GWY_MASK_INCLUDE) {
                    for (j = 0; j < width-1; j++) {
                        sum += square_area2w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                             m[j] > 0.0, m[j+1] > 0.0, m[j+xres+1] > 0.0, m[j+xres] > 0.0, x, y);
                    }
                }
                else {
                    for (j = 0; j < width-1; j++) {
                        sum += square_area2w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                             m[j] < 1.0, m[j+1] < 1.0, m[j+xres+1] < 1.0, m[j+xres] < 1.0, x, y);
                    }
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_area2(width, 1, dataul, dataul - s*xres, maskul, mode, x, y);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_area2(width, 1,
                                dataul + xres*(height-1),
                                dataul + xres*(height-1 + s),
                                maskul + xres*(height-1),
                                mode, x, y);

            /* Left column */
            s = !(col == 0);
            sum += stripe_area2(height, xres, dataul, dataul - s, maskul, mode, y, x);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_area2(height, xres, dataul + width-1, dataul + width-1 + s, maskul + width-1, mode, y, x);
        }

        /* Just take the four corner quater-pixels as flat.  */
        if (mode == GWY_MASK_INCLUDE) {
            sum += ((maskul[0] > 0.0) + + (maskul[width-1] > 0.0)
                    + (maskul[xres*(height-1)] > 0.0) + (maskul[xres*(height-1) + width-1] > 0.0));
        }
        else {
            sum += ((maskul[0] < 1.0) + (maskul[width-1] < 1.0)
                    + (maskul[xres*(height-1)] < 1.0) + (maskul[xres*(height-1) + width-1] < 1.0));
        }
    }
    else {
        if (fabs(log(x/y)) < 1e-7) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,i,j) \
            shared(dataul,xres,width,height,q)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                for (j = 0; j < width-1; j++)
                    sum += square_area1(r[j], r[j+1], r[j+xres+1], r[j+xres], q);
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_area1(width, 1, dataul, dataul - s*xres, NULL, GWY_MASK_IGNORE, q);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_area1(width, 1, dataul + xres*(height-1), dataul + xres*(height-1 + s),
                                NULL, GWY_MASK_IGNORE, q);

            /* Left column */
            s = !(col == 0);
            sum += stripe_area1(height, xres, dataul, dataul - s, NULL, GWY_MASK_IGNORE, q);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_area1(height, xres, dataul + width-1, dataul + width-1 + s, NULL, GWY_MASK_IGNORE, q);
        }
        else {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,i,j) \
            shared(dataul,xres,width,height,x,y)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                for (j = 0; j < width-1; j++)
                    sum += square_area2(r[j], r[j+1], r[j+xres+1], r[j+xres], x, y);
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_area2(width, 1, dataul, dataul - s*xres, NULL, GWY_MASK_IGNORE, x, y);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_area2(width, 1, dataul + xres*(height-1), dataul + xres*(height-1 + s), NULL,
                                GWY_MASK_IGNORE, x, y);

            /* Left column */
            s = !(col == 0);
            sum += stripe_area2(height, xres, dataul, dataul - s, NULL, GWY_MASK_IGNORE, y, x);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_area2(height, xres, dataul + width-1, dataul + width-1 + s, NULL, GWY_MASK_IGNORE, y, x);
        }

        /* Just take the four corner quater-pixels as flat.  */
        sum += 4.0;
    }

    return sum*q/4;
}

/**
 * gwy_data_field_get_surface_area:
 * @data_field: A data field.
 *
 * Computes surface area of a data field.
 *
 * This quantity is cached.
 *
 * Returns: The surface area.
 **/
gdouble
gwy_data_field_get_surface_area(GwyDataField *data_field)
{
    gdouble area = 0.0;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), area);

    gwy_debug("%s", CTEST(data_field, ARE) ? "cache" : "lame");
    if (CTEST(data_field, ARE))
        return CVAL(data_field, ARE);

    area = calculate_surface_area(data_field, NULL, GWY_MASK_IGNORE, 0, 0, data_field->xres, data_field->yres);

    CVAL(data_field, ARE) = area;
    data_field->cached |= CBIT(ARE);

    return area;
}

/**
 * gwy_data_field_area_get_surface_area:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes surface area of a rectangular part of a data field.
 *
 * This function is equivalent to calling @gwy_data_field_area_get_surface_area_mask() with masking mode
 * %GWY_MASK_INCLUDE.
 *
 * Returns: The surface area.
 **/
gdouble
gwy_data_field_area_get_surface_area(GwyDataField *data_field,
                                     GwyDataField *mask,
                                     gint col, gint row,
                                     gint width, gint height)
{
    return gwy_data_field_area_get_surface_area_mask(data_field, mask, GWY_MASK_INCLUDE, col, row, width, height);
}

/**
 * gwy_data_field_area_get_surface_area_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes surface area of a rectangular part of a data field.
 *
 * This quantity makes sense only if the lateral dimensions and values of @data_field are the same physical
 * quantities.
 *
 * Returns: The surface area.
 *
 * Since: 2.18
 **/
gdouble
gwy_data_field_area_get_surface_area_mask(GwyDataField *data_field,
                                          GwyDataField *mask,
                                          GwyMaskingType mode,
                                          gint col, gint row,
                                          gint width, gint height)
{
    gdouble area = 0.0;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &mode))
        return area;

    /* The result is the same, but it can be cached. */
    if (!mask && row == 0 && col == 0 && width == data_field->xres && height == data_field->yres)
        return gwy_data_field_get_surface_area(data_field);

    return calculate_surface_area(data_field, mask, mode, col, row, width, height);
}

static gdouble
calculate_rms_surface_slope(GwyDataField *dfield,
                            GwyDataField *mask,
                            GwyMaskingType mode,
                            gint col, gint row,
                            gint width, gint height)
{
    const gdouble *dataul, *maskul, *r, *m;
    gint i, j, xres, n;
    gdouble dx, dy, sumh, sumv, dz;

    /* special cases */
    if (!width || !height)
        return 0.0;

    xres = dfield->xres;
    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    dataul = dfield->data + xres*row + col;
    sumv = sumh = 0.0;
    n = 0;

    /* The gradient has two contributions, from horizontal derivatives and from vertical derivatives.  We could
     * require both to be calculable for masked areas, but we do not.  Each gradient is calculate separately for each
     * pixels where it is defined, regardless whether the orthogonal one can be calculated.  This can give uneven
     * contributions for some odd mask shapes… */
    if (mask && mode != GWY_MASK_IGNORE) {
        maskul = mask->data + xres*row + col;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sumh,sumv,n) \
            private(r,m,i,j,dz) \
            shared(dataul,maskul,xres,height,width,mode)
#endif
        for (i = 0; i < height-1; i++) {
            r = dataul + xres*i;
            m = maskul + xres*i;
            if (mode == GWY_MASK_INCLUDE) {
                /* Inside. */
                for (j = 0; j < width-1; j++) {
                    if (m[j] > 0.0 && m[xres+j] > 0.0) {
                        dz = r[xres+j] - r[j];
                        sumv += dz*dz;
                        n++;
                    }
                    if (m[j] > 0.0 && m[j+1] > 0.0) {
                        dz = r[j+1] - r[j];
                        sumh += dz*dz;
                        n++;
                    }
                }
                /* Last-column vertical gradient. */
                if (m[j] > 0.0 && m[xres+j] > 0.0) {
                    dz = r[xres+j] - r[j];
                    sumv += dz*dz;
                    n++;
                }
            }
            else {
                /* Inside. */
                for (j = 0; j < width-1; j++) {
                    if (m[j] <= 0.0 && m[xres+j] <= 0.0) {
                        dz = r[xres+j] - r[j];
                        sumv += dz*dz;
                        n++;
                    }
                    if (m[j] <= 0.0 && m[j+1] <= 0.0) {
                        dz = r[j+1] - r[j];
                        sumh += dz*dz;
                        n++;
                    }
                }
                /* Last-column vertical gradient. */
                if (m[j] <= 0.0 && m[xres+j] <= 0.0) {
                    dz = r[xres+j] - r[j];
                    sumv += dz*dz;
                    n++;
                }
            }
        }
        /* Last-row horizontal gradient. */
        i = height-1;
        r = dataul + xres*i;
        m = maskul + xres*i;
        if (mode == GWY_MASK_INCLUDE) {
            for (j = 0; j < width-1; j++) {
                if (m[j] > 0.0 && m[j+1] > 0.0) {
                    dz = r[j+1] - r[j];
                    sumh += dz*dz;
                    n++;
                }
            }
        }
        else {
            for (j = 0; j < width-1; j++) {
                if (m[j] <= 0.0 && m[j+1] <= 0.0) {
                    dz = r[j+1] - r[j];
                    sumh += dz*dz;
                    n++;
                }
            }
        }
        if (!n)
            return 0.0;
    }
    else {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sumh,sumv) \
            private(r,i,j,dz) \
            shared(dataul,xres,width,height)
#endif
        for (i = 0; i < height-1; i++) {
            r = dataul + xres*i;
            /* Inside. */
            for (j = 0; j < width-1; j++) {
                dz = r[xres+j] - r[j];
                sumv += dz*dz;
                dz = r[j+1] - r[j];
                sumh += dz*dz;
            }
            /* Last-column vertical gradient. */
            dz = r[xres+j] - r[j];
            sumv += dz*dz;
        }
        /* Last-row horizontal gradient. */
        i = height-1;
        r = dataul + xres*i;
        for (j = 0; j < width-1; j++) {
            dz = r[j+1] - r[j];
            sumh += dz*dz;
        }
        n = (width - 1)*height + width*(height - 1);
    }

    return sqrt(2.0*(sumh/(dx*dx) + sumv/(dy*dy))/n);
}

/**
 * gwy_data_field_area_get_surface_slope_mask:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes root mean square surface slope (Sdq) of a rectangular part of a data field.
 *
 * Returns: The root mean square surface slope.
 *
 * Since: 2.58
 **/
gdouble
gwy_data_field_area_get_surface_slope_mask(GwyDataField *data_field,
                                           GwyDataField *mask,
                                           GwyMaskingType mode,
                                           gint col, gint row,
                                           gint width, gint height)
{
    gdouble Sdq = 0.0;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &mode))
        return Sdq;

    return calculate_rms_surface_slope(data_field, mask, mode, col, row, width, height);
}

/**
 * gwy_data_field_get_surface_slope:
 * @data_field: A data field.
 *
 * Computes root mean square surface slope (Sdq) of a data field.
 *
 * Returns: The root mean square surface slope.
 *
 * Since: 2.58
 **/
gdouble
gwy_data_field_get_surface_slope(GwyDataField *data_field)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);

    return calculate_rms_surface_slope(data_field, NULL, GWY_MASK_IGNORE, 0, 0, data_field->xres, data_field->yres);
}

/**
 * square_var1:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @q: One fourth of rectangle projected var (x-size * ysize).
 *
 * Calculates approximate variation of a one square pixel.
 *
 * Returns: The variation.
 **/
static inline gdouble
square_var1(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
            gdouble q)
{
    gdouble z12 = z1 - z2, z23 = z2 - z3, z34 = z3 - z4, z41 = z4 - z1;

    return (sqrt((z12*z12 + z41*z41)/q) + sqrt((z23*z23 + z12*z12)/q)
            + sqrt((z34*z34 + z23*z23)/q) + sqrt((z41*z41 + z34*z34)/q));
}

/**
 * square_var1w:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @w1: Weight of first corner (0 or 1).
 * @w2: Weight of second corner (0 or 1).
 * @w3: Weight of third corner (0 or 1).
 * @w4: Weight of fourth corner (0 or 1).
 * @q: One fourth of rectangle projected var (x-size * ysize).
 *
 * Calculates approximate variation of a one square pixel with some corners possibly missing.
 *
 * Returns: The variation.
 **/
static inline gdouble
square_var1w(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
             gint w1, gint w2, gint w3, gint w4,
             gdouble q)
{
    gdouble z12 = z1 - z2, z23 = z2 - z3, z34 = z3 - z4, z41 = z4 - z1;

    return (w1*sqrt((z12*z12 + z41*z41)/q) + w2*sqrt((z23*z23 + z12*z12)/q)
            + w3*sqrt((z34*z34 + z23*z23)/q) + w4*sqrt((z41*z41 + z34*z34)/q));
}

/**
 * square_var2:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate variation of a one general rectangular pixel.
 *
 * Returns: The variation.
 **/
static inline gdouble
square_var2(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
            gdouble x, gdouble y)
{
    gdouble z12 = z1 - z2, z23 = z2 - z3, z34 = z3 - z4, z41 = z4 - z1;

    return (sqrt(z12*z12/x + z41*z41/y) + sqrt(z23*z23/y + z12*z12/x)
            + sqrt(z34*z34/x + z23*z23/y) + sqrt(z41*z41/y + z34*z34/x));
}

/**
 * square_var2w:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @w1: Weight of first corner (0 or 1).
 * @w2: Weight of second corner (0 or 1).
 * @w3: Weight of third corner (0 or 1).
 * @w4: Weight of fourth corner (0 or 1).
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate variation of a one general rectangular pixel with some corners possibly missing.
 *
 * Returns: The variation.
 **/
static inline gdouble
square_var2w(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
             gint w1, gint w2, gint w3, gint w4,
             gdouble x, gdouble y)
{
    gdouble z12 = z1 - z2, z23 = z2 - z3, z34 = z3 - z4, z41 = z4 - z1;

    return (w1*sqrt(z12*z12/x + z41*z41/y) + w2*sqrt(z23*z23/y + z12*z12/x)
            + w3*sqrt(z34*z34/x + z23*z23/y) + w4*sqrt(z41*z41/y + z34*z34/x));
}

/**
 * stripe_var1:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 * @mode: Masking mode.
 * @q: One fourth of rectangle projected var (x-size * ysize).
 *
 * Calculates approximate variation of a half-pixel stripe.
 *
 * Returns: The variation.
 **/
static gdouble
stripe_var1(gint n,
            gint stride,
            const gdouble *r,
            const gdouble *rr,
            const gdouble *m,
            GwyMaskingType mode,
            gdouble q)
{
    gdouble sum = 0.0;
    gint j;

    if (m && mode != GWY_MASK_IGNORE) {
        if (mode == GWY_MASK_INCLUDE) {
            for (j = 0; j < n-1; j++) {
                sum += square_var1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                    m[j*stride] > 0.0, m[(j + 1)*stride] > 0.0, 0, 0, q);
            }
        }
        else {
            for (j = 0; j < n-1; j++) {
                sum += square_var1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                    m[j*stride] < 1.0, m[(j + 1)*stride] < 1.0, 0, 0, q);
            }
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_var1w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                1, 1, 0, 0, q);
        }
    }

    return sum;
}

/**
 * stripe_var2:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 * @x: One fourth of square of rectangle width (x-size).
 * @y: One fourth of square of rectangle height (y-size).
 *
 * Calculates approximate variation of a half-pixel stripe.
 *
 * Returns: The variation.
 **/
static gdouble
stripe_var2(gint n,
            gint stride,
            const gdouble *r,
            const gdouble *rr,
            const gdouble *m,
            GwyMaskingType mode,
            gdouble x,
            gdouble y)
{
    gdouble sum = 0.0;
    gint j;

    if (m && mode == GWY_MASK_INCLUDE) {
        for (j = 0; j < n-1; j++) {
            sum += square_var2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                m[j*stride] > 0.0, m[(j + 1)*stride] > 0.0, 0, 0, x, y);
        }
    }
    else if (m && mode == GWY_MASK_EXCLUDE) {
        for (j = 0; j < n-1; j++) {
            sum += square_var2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                m[j*stride] < 1.0, m[(j + 1)*stride] < 1.0, 0, 0, x, y);
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_var2w(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                1, 1, 0, 0, x, y);
        }
    }

    return sum;
}

static gdouble
calculate_variation(GwyDataField *dfield,
                    GwyDataField *mask,
                    GwyMaskingType mode,
                    gint col, gint row,
                    gint width, gint height)
{
    const gdouble *dataul, *maskul, *r, *m;
    gint i, j, xres, yres, s;
    gdouble x, y, q, sum = 0.0;

    /* special cases */
    if (!width || !height)
        return sum;

    xres = dfield->xres;
    yres = dfield->yres;
    x = dfield->xreal/dfield->xres;
    y = dfield->yreal/dfield->yres;
    q = x*y;
    x = x*x;
    y = y*y;
    dataul = dfield->data + xres*row + col;

    if (mask && mode != GWY_MASK_IGNORE) {
        maskul = mask->data + xres*row + col;
        if (fabs(log(x/y)) < 1e-7) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,i,j) \
            shared(dataul,maskul,xres,height,width,mode,q)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                if (mode == GWY_MASK_INCLUDE) {
                    for (j = 0; j < width-1; j++) {
                        sum += square_var1w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                            m[j] > 0.0, m[j+1] > 0.0, m[j+xres+1] > 0.0, m[j+xres] > 0.0, q);
                    }
                }
                else {
                    for (j = 0; j < width-1; j++) {
                        sum += square_var1w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                            m[j] < 1.0, m[j+1] < 1.0, m[j+xres+1] < 1.0, m[j+xres] < 1.0, q);
                    }
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_var1(width, 1, dataul, dataul - s*xres, maskul, mode, q);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_var1(width, 1,
                               dataul + xres*(height-1),
                               dataul + xres*(height-1 + s),
                               maskul + xres*(height-1), mode, q);

            /* Left column */
            s = !(col == 0);
            sum += stripe_var1(height, xres, dataul, dataul - s, maskul, mode, q);
            /* Right column */
            s = !(col + width == xres);
            sum += stripe_var1(height, xres, dataul + width-1, dataul + width-1 + s, maskul + width-1, mode, q);
        }
        else {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,i,j) \
            shared(dataul,maskul,xres,height,width,mode,x,y)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                if (mode == GWY_MASK_INCLUDE) {
                    for (j = 0; j < width-1; j++) {
                        sum += square_var2w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                            m[j] > 0.0, m[j+1] > 0.0, m[j+xres+1] > 0.0, m[j+xres] > 0.0, x, y);
                    }
                }
                else {
                    for (j = 0; j < width-1; j++) {
                        sum += square_var2w(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                            m[j] < 1.0, m[j+1] < 1.0, m[j+xres+1] < 1.0, m[j+xres] < 1.0, x, y);
                    }
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_var2(width, 1, dataul, dataul - s*xres, maskul, mode, x, y);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_var2(width, 1,
                               dataul + xres*(height-1),
                               dataul + xres*(height-1 + s),
                               maskul + xres*(height-1),
                               mode, x, y);

            /* Left column */
            s = !(col == 0);
            sum += stripe_var2(height, xres, dataul, dataul - s, maskul, mode, y, x);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_var2(height, xres, dataul + width-1, dataul + width-1 + s, maskul + width-1, mode, y, x);
        }
    }
    else {
        if (fabs(log(x/y)) < 1e-7) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,i,j) \
            shared(dataul,xres,width,height,q)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                for (j = 0; j < width-1; j++)
                    sum += square_var1(r[j], r[j+1], r[j+xres+1], r[j+xres], q);
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_var1(width, 1, dataul, dataul - s*xres, NULL, GWY_MASK_IGNORE, q);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_var1(width, 1, dataul + xres*(height-1), dataul + xres*(height-1 + s),
                               NULL, GWY_MASK_IGNORE, q);

            /* Left column */
            s = !(col == 0);
            sum += stripe_var1(height, xres, dataul, dataul - s, NULL, GWY_MASK_IGNORE, q);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_var1(height, xres, dataul + width-1, dataul + width-1 + s, NULL, GWY_MASK_IGNORE, q);
        }
        else {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,i,j) \
            shared(dataul,xres,width,height,x,y)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                for (j = 0; j < width-1; j++)
                    sum += square_var2(r[j], r[j+1], r[j+xres+1], r[j+xres], x, y);
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_var2(width, 1, dataul, dataul - s*xres, NULL, GWY_MASK_IGNORE, x, y);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_var2(width, 1, dataul + xres*(height-1), dataul + xres*(height-1 + s), NULL,
                               GWY_MASK_IGNORE, x, y);

            /* Left column */
            s = !(col == 0);
            sum += stripe_var2(height, xres, dataul, dataul - s, NULL, GWY_MASK_IGNORE, y, x);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_var2(height, xres, dataul + width-1, dataul + width-1 + s, NULL, GWY_MASK_IGNORE, y, x);
        }
    }

    return sum*q/4;
}

/**
 * gwy_data_field_get_variation:
 * @data_field: A data field.
 *
 * Computes the total variation of a data field.
 *
 * See gwy_data_field_area_get_variation() for the definition.
 *
 * This quantity is cached.
 *
 * Returns: The variation.
 *
 * Since: 2.38
 **/
gdouble
gwy_data_field_get_variation(GwyDataField *data_field)
{
    gdouble var = 0.0;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), var);

    gwy_debug("%s", CTEST(data_field, VAR) ? "cache" : "lame");
    if (CTEST(data_field, VAR))
        return CVAL(data_field, VAR);

    var = calculate_variation(data_field, NULL, GWY_MASK_IGNORE, 0, 0, data_field->xres, data_field->yres);

    CVAL(data_field, VAR) = var;
    data_field->cached |= CBIT(VAR);

    return var;
}

/**
 * gwy_data_field_area_get_variation:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @mode: Masking mode to use.  See the introduction for description of masking modes.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes the total variation of a rectangular part of a data field.
 *
 * The total variation is estimated as the integral of the absolute value of local gradient.
 *
 * This quantity has the somewhat odd units of value unit times lateral unit. It can be envisioned as follows.  If the
 * surface has just two height levels (upper and lower planes) then the quantity is the length of the boundary between
 * the upper and lower part, multiplied by the step height.  If the surface is piece-wise constant, then the variation
 * is the step height integrated along the boundaries between the constant parts.  Therefore, for non-fractal surfaces
 * it scales with the linear dimension of the image, not with its area, despite being an area integral.
 *
 * Returns: The variation.
 *
 * Since: 2.38
 **/
gdouble
gwy_data_field_area_get_variation(GwyDataField *data_field,
                                  GwyDataField *mask,
                                  GwyMaskingType mode,
                                  gint col, gint row,
                                  gint width, gint height)
{
    gdouble var = 0.0;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &mode))
        return var;

    /* The result is the same, but it can be cached. */
    if (!mask && row == 0 && col == 0 && width == data_field->xres && height == data_field->yres)
        return gwy_data_field_get_variation(data_field);

    return calculate_variation(data_field, mask, mode, col, row, width, height);
}

/**
 * square_volume:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 *
 * Calculates approximate volume of a one square pixel.
 *
 * Returns: The volume.
 **/
static inline gdouble
square_volume(gdouble z1, gdouble z2, gdouble z3, gdouble z4)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/4.0;

    return c;
}

/**
 * square_volumew:
 * @z1: Z-value in first corner.
 * @z2: Z-value in second corner.
 * @z3: Z-value in third corner.
 * @z4: Z-value in fourth corner.
 * @w1: Weight of first corner (0 or 1).
 * @w2: Weight of second corner (0 or 1).
 * @w3: Weight of third corner (0 or 1).
 * @w4: Weight of fourth corner (0 or 1).
 *
 * Calculates approximate volume of a one square pixel with some corners possibly missing.
 *
 * Returns: The volume.
 **/
static inline gdouble
square_volumew(gdouble z1, gdouble z2, gdouble z3, gdouble z4,
               gint w1, gint w2, gint w3, gint w4)
{
    gdouble c;

    c = (z1 + z2 + z3 + z4)/4.0;

    return (w1*(3.0*z1 + z2 + z4 + c) + w2*(3.0*z2 + z1 + z3 + c)
            + w3*(3.0*z3 + z2 + z4 + c) + w4*(3.0*z4 + z3 + z1 + c))/24.0;
}

/**
 * stripe_volume:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 *
 * Calculates approximate volume of a half-pixel stripe.
 *
 * Returns: The volume.
 **/
static gdouble
stripe_volume(gint n,
              gint stride,
              const gdouble *r,
              const gdouble *rr,
              const gdouble *m)
{
    gdouble sum = 0.0;
    gint j;

    if (m) {
        for (j = 0; j < n-1; j++) {
            sum += square_volumew(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                  m[j*stride] > 0.0, m[(j + 1)*stride] > 0.0, 0, 0);
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_volumew(r[j*stride], r[(j + 1)*stride], rr[(j + 1)*stride], rr[j*stride],
                                  1, 1, 0, 0);
        }
    }

    return sum;
}

/**
 * stripe_volumeb:
 * @n: The number of values in @r, @rr, @m.
 * @stride: Stride in @r, @rr, @m.
 * @r: Array of @n z-values of vertices, this row of vertices is considered inside.
 * @rr: Array of @n z-values of vertices, this row of vertices is considered outside.
 * @b: Array of @n z-values of basis, this row of vertices is considered inside.
 * @br: Array of @n z-values of basis, this row of vertices is considered outside.
 * @m: Mask for @r (@rr does not need mask since it has zero weight by definition), or %NULL to sum over all @r
 *     vertices.
 *
 * Calculates approximate volume of a half-pixel stripe, taken from basis.
 *
 * Returns: The volume.
 **/
static gdouble
stripe_volumeb(gint n,
               gint stride,
               const gdouble *r,
               const gdouble *rr,
               const gdouble *b,
               const gdouble *br,
               const gdouble *m)
{
    gdouble sum = 0.0;
    gint j;

    if (m) {
        for (j = 0; j < n-1; j++) {
            sum += square_volumew(r[j*stride] - b[j*stride],
                                  r[(j + 1)*stride] - b[(j + 1)*stride],
                                  rr[(j + 1)*stride] - br[(j + 1)*stride],
                                  rr[j*stride] - br[j*stride],
                                  m[j*stride] > 0.0,
                                  m[(j + 1)*stride] > 0.0,
                                  0, 0);
        }
    }
    else {
        for (j = 0; j < n-1; j++) {
            sum += square_volumew(r[j*stride] - b[j*stride],
                                  r[(j + 1)*stride] - b[(j + 1)*stride],
                                  rr[(j + 1)*stride] - br[(j + 1)*stride],
                                  rr[j*stride] - br[j*stride],
                                  1, 1, 0, 0);
        }
    }

    return sum;
}

static gdouble
calculate_volume(GwyDataField *dfield,
                 GwyDataField *basis,
                 GwyDataField *mask,
                 gint col, gint row,
                 gint width, gint height)
{
    const gdouble *dataul, *maskul, *basisul, *r, *m, *b;
    gint i, j, xres, yres, s;
    gdouble sum = 0.0;

    /* special cases */
    if (!width || !height)
        return sum;

    xres = dfield->xres;
    yres = dfield->yres;
    dataul = dfield->data + xres*row + col;

    if (mask) {
        maskul = mask->data + xres*row + col;
        if (!basis) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,i,j) \
            shared(dataul,maskul,xres,height,width)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                for (j = 0; j < width-1; j++) {
                    sum += square_volumew(r[j], r[j+1], r[j+xres+1], r[j+xres],
                                          m[j] > 0.0, m[j+1] > 0.0, m[j+xres+1] > 0.0, m[j+xres] > 0.0);
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_volume(width, 1, dataul, dataul - s*xres, maskul);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_volume(width, 1, dataul + xres*(height-1),
                                 dataul + xres*(height-1 + s),
                                 maskul + xres*(height-1));

            /* Left column */
            s = !(col == 0);
            sum += stripe_volume(height, xres, dataul, dataul - s, maskul);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_volume(height, xres, dataul + width-1, dataul + width-1 + s, maskul + width-1);

            /* Just take the four corner quater-pixels as flat.  */
            if (maskul[0])
                sum += dataul[0]/4.0;
            if (maskul[width-1])
                sum += dataul[width-1]/4.0;
            if (maskul[xres*(height-1)])
                sum += dataul[xres*(height-1)]/4.0;
            if (maskul[xres*(height-1) + width-1])
                sum += dataul[xres*(height-1) + width-1]/4.0;
        }
        else {
            basisul = basis->data + xres*row + col;

            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,m,b,i,j) \
            shared(dataul,maskul,basisul,xres,height,width)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                m = maskul + xres*i;
                b = basisul + xres*i;
                for (j = 0; j < width-1; j++) {
                    sum += square_volumew(r[j] - b[j],
                                          r[j+1] - b[j+1],
                                          r[j+xres+1] - b[j+xres+1],
                                          r[j+xres] - b[j+xres],
                                          m[j] > 0.0, m[j+1] > 0.0,
                                          m[j+xres+1] > 0.0, m[j+xres] > 0.0);
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_volumeb(width, 1, dataul, dataul - s*xres, basisul, basisul - s*xres, maskul);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_volumeb(width, 1,
                                  dataul + xres*(height-1),
                                  dataul + xres*(height-1 + s),
                                  basisul + xres*(height-1),
                                  basisul + xres*(height-1 + s),
                                  maskul + xres*(height-1));

            /* Left column */
            s = !(col == 0);
            sum += stripe_volumeb(height, xres, dataul, dataul - s, basisul, basisul - s, maskul);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_volumeb(height, xres,
                                  dataul + width-1, dataul + width-1 + s,
                                  basisul + width-1, basisul + width-1 + s,
                                  maskul + width-1);

            /* Just take the four corner quater-pixels as flat.  */
            if (maskul[0])
                sum += (dataul[0] - basisul[0])/4.0;
            if (maskul[width-1])
                sum += (dataul[width-1] - basisul[width-1])/4.0;
            if (maskul[xres*(height-1)])
                sum += (dataul[xres*(height-1)] - basisul[xres*(height-1)])/4.0;
            if (maskul[xres*(height-1) + width-1])
                sum += (dataul[xres*(height-1) + width-1] - basisul[xres*(height-1) + width-1])/4.0;
        }
    }
    else {
        if (!basis) {
            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,i,j) \
            shared(dataul,xres,height,width)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                for (j = 0; j < width-1; j++)
                    sum += square_volume(r[j], r[j+1], r[j+xres+1], r[j+xres]);
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_volume(width, 1, dataul, dataul - s*xres, NULL);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_volume(width, 1, dataul + xres*(height-1), dataul + xres*(height-1 + s), NULL);

            /* Left column */
            s = !(col == 0);
            sum += stripe_volume(height, xres, dataul, dataul - s, NULL);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_volume(height, xres, dataul + width-1, dataul + width-1 + s, NULL);

            /* Just take the four corner quater-pixels as flat.  */
            sum += dataul[0]/4.0;
            sum += dataul[width-1]/4.0;
            sum += dataul[xres*(height-1)]/4.0;
            sum += dataul[xres*(height-1) + width-1]/4.0;
        }
        else {
            basisul = basis->data + xres*row + col;

            /* Inside */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sum) \
            private(r,b,i,j) \
            shared(dataul,basisul,xres,height,width)
#endif
            for (i = 0; i < height-1; i++) {
                r = dataul + xres*i;
                b = basisul + xres*i;
                for (j = 0; j < width-1; j++) {
                    sum += square_volume(r[j] - b[j],
                                         r[j+1] - b[j+1],
                                         r[j+xres+1] - b[j+xres+1],
                                         r[j+xres] - b[j+xres]);
                }
            }

            /* Top row */
            s = !(row == 0);
            sum += stripe_volumeb(width, 1, dataul, dataul - s*xres, basisul, basisul - s*xres, NULL);

            /* Bottom row */
            s = !(row + height == yres);
            sum += stripe_volumeb(width, 1,
                                  dataul + xres*(height-1),
                                  dataul + xres*(height-1 + s),
                                  basisul + xres*(height-1),
                                  basisul + xres*(height-1 + s),
                                  NULL);

            /* Left column */
            s = !(col == 0);
            sum += stripe_volumeb(height, xres, dataul, dataul - s, basisul, basisul - s, NULL);

            /* Right column */
            s = !(col + width == xres);
            sum += stripe_volumeb(height, xres,
                                  dataul + width-1, dataul + width-1 + s,
                                  basisul + width-1, basisul + width-1 + s,
                                  NULL);

            /* Just take the four corner quater-pixels as flat.  */
            sum += (dataul[0] - basisul[0])/4.0;
            sum += (dataul[width-1] - basisul[width-1])/4.0;
            sum += (dataul[xres*(height-1)] - basisul[xres*(height-1)])/4.0;
            sum += (dataul[xres*(height-1) + width-1] - basisul[xres*(height-1) + width-1])/4.0;
        }
    }

    return sum* dfield->xreal/dfield->xres * dfield->yreal/dfield->yres;
}

/* Don't define gwy_data_field_get_volume() without mask and basis, it would
 * just be a complicate way to calculate gwy_data_field_get_sum() */

/**
 * gwy_data_field_area_get_volume:
 * @data_field: A data field.
 * @basis: The basis or background for volume calculation if not %NULL. The height of each vertex is then the
 *         difference between @data_field value and @basis value.  Value %NULL is the same as passing all zeroes for
 *         the basis.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Computes volume of a rectangular part of a data field.
 *
 * Returns: The volume.
 *
 * Since: 2.3
 **/
gdouble
gwy_data_field_area_get_volume(GwyDataField *data_field,
                               GwyDataField *basis,
                               GwyDataField *mask,
                               gint col, gint row,
                               gint width, gint height)
{
    gdouble vol = 0.0;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, NULL))
        return vol;
    g_return_val_if_fail(!basis || (GWY_IS_DATA_FIELD(basis)
                                    && basis->xres == data_field->xres
                                    && basis->yres == data_field->yres), vol);

    return calculate_volume(data_field, basis, mask, col, row, width, height);
}


/**
 * gwy_data_field_area_get_dispersion:
 * @data_field: A data field.
 * @mask: Mask specifying which values to take into account/exclude, or %NULL.
 * @masking: Masking mode to use (has any effect only with non-%NULL @mask).
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @xcenter: Location where to store the horizontal position of centre of mass (in pixel coordinates), or %NULL.
 * @ycenter: Location where to store the vertical position of centre of mass (in pixel coordinates), or %NULL.
 *
 * Calculates the dispersion of a data field area, taking it as a distribution.
 *
 * The function takes @data_field as a distribution, finds the centre of mass in the area and then calculates the mean
 * squared distance from this centre, weighted by @data_field values.  Normally @data_field should contain only
 * non-negative data.
 *
 * The dispersion is measured in real coordinates, so horizontal and vertical pixel sizes play a role and the units
 * are squared lateral units of @data_field.  Note, however, that @xcenter and @ycenter is returned in pixel
 * coordinates since it is usually more convenient.
 *
 * Returns: Dispersion, i.e. estimated average squared distance from centre of mass.
 *
 * Since: 2.52
 **/
gdouble
gwy_data_field_area_get_dispersion(GwyDataField *data_field,
                                   GwyDataField *mask,
                                   GwyMaskingType masking,
                                   gint col,
                                   gint row,
                                   gint width,
                                   gint height,
                                   gdouble *xcenter,
                                   gdouble *ycenter)
{
    gint xres, yres, i, j;
    gdouble dx, dy;
    gdouble sx, sy, s2, sw;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, FALSE)
        || !_gwy_data_field_check_mask(data_field, &mask, &masking))
        return 0.0;

    xres = data_field->xres;
    yres = data_field->yres;
    dx = data_field->xreal/xres;
    dy = data_field->yreal/yres;

    sx = sy = sw = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:sx,sy,sw) \
            private(i,j) \
            shared(data_field,mask,xres,width,height,row,col,masking)
#endif
    for (i = 0; i < height; i++) {
        const gdouble *d = data_field->data + (row + i)*xres + col;

        if (masking == GWY_MASK_INCLUDE) {
            const gdouble *m = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                gdouble w = d[j]*(m[j] > 0.0);
                sw += w;
                sx += j*w;
                sy += i*w;
            }
        }
        else if (masking == GWY_MASK_EXCLUDE) {
            const gdouble *m = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                gdouble w = d[j]*(m[j] <= 0.0);
                sw += w;
                sx += j*w;
                sy += i*w;
            }
        }
        else {
            for (j = 0; j < width; j++) {
                gdouble w = d[j];
                sw += w;
                sx += j*w;
                sy += i*w;
            }
        }
    }

    /* Negative values are silly but do not prevent us from continuing. */
    if (sw == 0.0) {
        if (xcenter)
            *xcenter = col + 0.5*width;
        if (ycenter)
            *ycenter = row + 0.5*height;
        return 0.0;
    }

    sx /= sw;
    sy /= sw;
    if (xcenter)
        *xcenter = sx;
    if (ycenter)
        *ycenter = sy;
    sx *= dx;
    sy *= dy;

    s2 = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:s2) \
            private(i,j) \
            shared(data_field,mask,xres,width,height,row,col,dx,dy,sx,sy,masking)
#endif
    for (i = 0; i < height; i++) {
        const gdouble *d = data_field->data + (row + i)*xres + col;
        gdouble y = i*dy - sy;

        if (masking == GWY_MASK_INCLUDE) {
            const gdouble *m = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                gdouble x = j*dx - sx;
                s2 += (m[j] > 0.0)*(x*x + y*y)*d[j];
            }
        }
        else if (masking == GWY_MASK_EXCLUDE) {
            const gdouble *m = mask->data + (row + i)*xres + col;
            for (j = 0; j < width; j++) {
                gdouble x = j*dx - sx;
                s2 += (m[j] <= 0.0)*(x*x + y*y)*d[j];
            }
        }
        else {
            for (j = 0; j < width; j++) {
                gdouble x = j*dx - sx;
                s2 += (x*x + y*y)*d[j];
            }
        }
    }

    return s2/sw;
}

/**
 * gwy_data_field_get_dispersion:
 * @data_field: A data field.
 * @xcenter: Location where to store the horizontal position of centre of mass (in pixel coordinates), or %NULL.
 * @ycenter: Location where to store the vertical position of centre of mass (in pixel coordinates), or %NULL.
 *
 * Calculates the dispersion of a data field, taking it as a distribution.
 *
 * See gwy_data_field_area_get_dispersion() for discussion.
 *
 * Returns: Dispersion, i.e. estimated average squared distance from centre of mass.
 *
 * Since: 2.52
 **/
gdouble
gwy_data_field_get_dispersion(GwyDataField *data_field,
                              gdouble *xcenter,
                              gdouble *ycenter)
{
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0.0);
    return gwy_data_field_area_get_dispersion(data_field, NULL, GWY_MASK_IGNORE, 0, 0,
                                              data_field->xres, data_field->yres, xcenter, ycenter);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
