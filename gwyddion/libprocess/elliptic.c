/*
 *  $Id: elliptic.c 24651 2022-03-08 14:50:06Z yeti-dn $
 *  Copyright (C) 2005-2022 David Necas (Yeti), Petr Klapetek, Chris Anderson.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, sidewinder.asu@gmail.com.
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
#include <libgwyddion/gwymath.h>
#include <libprocess/elliptic.h>
#include "gwyprocessinternal.h"

typedef enum {
    GWY_ELLIPTIC_COUNT,
    GWY_ELLIPTIC_FILL,
    GWY_ELLIPTIC_EXTRACT,
    GWY_ELLIPTIC_UNEXTRACT,
} GwyEllipticOperation;

static gint
elliptic_area_do(GwyDataField *data_field,
                 gint col, gint row,
                 gint width, gint height,
                 GwyEllipticOperation op, gdouble value, gdouble *buffer)
{
    gint i, j, jfrom, jto, xres, yres, count, ifrom, ito, n;
    gdouble a, b, s;
    gdouble *d;

    if (!width || !height)                    /* Compatibility */
        return 0;
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);

    a = width/2.0;
    b = height/2.0;
    xres = data_field->xres;
    yres = data_field->yres;
    count = 0;

    ifrom = MAX(0, row);
    ito = MIN(row + height-1, yres-1);
    for (i = ifrom; i <= ito; i++) {
        d = data_field->data + i*xres;
        s = (i - row + 0.5)/b;
        s = s*(2.0 - s);
        if (s <= 0.0)
            continue;
        s = sqrt(s);
        jfrom = (gint)ceil(a*(1.0 - s) - 0.5) + col;
        jto = (gint)floor(a*(1.0 + s) - 0.5) + col;
        jfrom = MAX(jfrom, 0);
        jto = MIN(jto, xres-1);
        n = jto - jfrom + 1;
        g_return_val_if_fail(n >= 0, 0);
        d += jfrom;
        if (op == GWY_ELLIPTIC_FILL) {
            for (j = 0; j < n; j++)
                d[j] = value;
        }
        else if (op == GWY_ELLIPTIC_EXTRACT) {
            gwy_assign(buffer + count, d, n);
        }
        else if (op == GWY_ELLIPTIC_UNEXTRACT) {
            gwy_assign(d, buffer + count, n);
        }
        else {
            g_assert(op == GWY_ELLIPTIC_COUNT);
        }
        count += n;
    }

    return count;
}

/**
 * gwy_data_field_elliptic_area_fill:
 * @data_field: A data field.
 * @col: Upper-left bounding box column coordinate.
 * @row: Upper-left bounding box row coordinate.
 * @width: Bounding box width (number of columns).
 * @height: Bounding box height (number of rows).
 * @value: Value to be entered.
 *
 * Fills an elliptic region of a data field with given value.
 *
 * The elliptic region is defined by its bounding box.  In versions prior to 2.59 the bounding box must be completely
 * contained in the data field.  Since version 2.59 the ellipse can intersect the data field in any manner.
 *
 * Returns: The number of filled values.
 **/
gint
gwy_data_field_elliptic_area_fill(GwyDataField *data_field,
                                  gint col, gint row,
                                  gint width, gint height,
                                  gdouble value)
{
    gint count;

    count = elliptic_area_do(data_field, col, row, width, height, GWY_ELLIPTIC_FILL, value, NULL);
    if (count)
        gwy_data_field_invalidate(data_field);

    return count;
}

/**
 * gwy_data_field_elliptic_area_extract:
 * @data_field: A data field.
 * @col: Upper-left bounding box column coordinate.
 * @row: Upper-left bounding box row coordinate.
 * @width: Bounding box width (number of columns).
 * @height: Bounding box height (number of rows).
 * @data: Location to store the extracted values to.  Its size has to be sufficient to contain all the extracted
 *        values.  As a conservative estimate @width*@height can be used, or the size can be calculated with
 *        gwy_data_field_get_elliptic_area_size().
 *
 * Extracts values from an elliptic region of a data field.
 *
 * The elliptic region is defined by its bounding box.  In versions prior to 2.59 the bounding box must be completely
 * contained in the data field.  Since version 2.59 the ellipse can intersect the data field in any manner.
 *
 * Returns: The number of extracted values.
 **/
gint
gwy_data_field_elliptic_area_extract(GwyDataField *data_field,
                                     gint col, gint row,
                                     gint width, gint height,
                                     gdouble *data)
{
    return elliptic_area_do(data_field, col, row, width, height, GWY_ELLIPTIC_EXTRACT, 0.0, data);
}

/**
 * gwy_data_field_elliptic_area_unextract:
 * @data_field: A data field.
 * @col: Upper-left bounding box column coordinate.
 * @row: Upper-left bounding box row coordinate.
 * @width: Bounding box width (number of columns).
 * @height: Bounding box height (number of rows).
 * @data: The values to put back.  It must be the same array as in previous gwy_data_field_elliptic_area_extract().
 *
 * Puts values back to an elliptic region of a data field.
 *
 * The elliptic region is defined by its bounding box.  In versions prior to 2.59 the bounding box must be completely
 * contained in the data field.  Since version 2.59 the ellipse can intersect the data field in any manner.
 *
 * This method does the reverse of gwy_data_field_elliptic_area_extract() allowing to implement pixel-wise filters on
 * elliptic areas.  Values from @data are put back to the same positions gwy_data_field_elliptic_area_extract() took
 * them from.
 **/
void
gwy_data_field_elliptic_area_unextract(GwyDataField *data_field,
                                       gint col, gint row,
                                       gint width, gint height,
                                       const gdouble *data)
{
    gint count;

    count = elliptic_area_do(data_field, col, row, width, height, GWY_ELLIPTIC_UNEXTRACT, 0.0, (gdouble*)data);
    if (count)
        gwy_data_field_invalidate(data_field);
}

/**
 * gwy_data_field_get_elliptic_intersection:
 * @data_field: A data field.
 * @col: Upper-left bounding box column coordinate.
 * @row: Upper-left bounding box row coordinate.
 * @width: Bounding box width.
 * @height: Bounding box height.
 *
 * Calculates an upper bound of the number of samples in an elliptic region intersecting a data field.
 *
 * Returns: The number of pixels in an elliptic region with given rectangular bounds (or its upper bound).
 *
 * Since: 2.59
 **/
gint
gwy_data_field_get_elliptic_intersection(GwyDataField *data_field,
                                         gint col, gint row,
                                         gint width, gint height)
{
    return elliptic_area_do(data_field, col, row, width, height, GWY_ELLIPTIC_COUNT, 0.0, NULL);
}

/**
 * gwy_data_field_get_elliptic_area_size:
 * @width: Bounding box width.
 * @height: Bounding box height.
 *
 * Calculates an upper bound of the number of samples in an elliptic region.
 *
 * This function is useful for elliptic areas more or less contained within the data field.  Otherwise the returned
 * size can be overestimated a lot. Use gwy_data_field_get_elliptic_intersection() for elliptic areas intersecting the
 * data field in arbitrary manner.
 *
 * Returns: The number of pixels in an elliptic region with given rectangular bounds (or its upper bound).
 **/
gint
gwy_data_field_get_elliptic_area_size(gint width, gint height)
{
    gint i, from, to, count;
    gdouble a, b, s;

    if (width <= 0 || height <= 0)
        return 0;

    a = width/2.0;
    b = height/2.0;
    count = 0;

    for (i = 0; i < height; i++) {
        s = (i + 0.5)/b;
        s = s*(2.0 - s);
        if (s <= 0)
            continue;
        s = sqrt(s);
        from = ceil(a*(1.0 - s) - 0.5);
        to = floor(a*(1.0 + s) - 0.5);
        from = MAX(from, 0);
        to = MIN(to, width-1);
        count += MAX(to - from + 1, 0);
    }

    return count;
}

/**
 * gwy_data_field_local_maximum:
 * @dfield: A two-dimensional data field.
 * @x: Approximate maximum @x-location to be improved (in pixels).
 * @y: Approximate maximum @y-location to be improved (in pixels).
 * @ax: Horizontal search radius.
 * @ay: Vertical search radius.
 *
 * Searches an elliptical area in a data field for local maximum.
 *
 * The area may stick outside the data field.
 *
 * The function first finds the maximum within the ellipse, intersected with the data field and then tries subpixel
 * refinement.  The maximum is considered successfully located if it is inside the data field, i.e. not on edge, there
 * is no higher value in its 8-neighbourhood, and the subpixel refinement of its position succeeds (which usually
 * happens when the first two conditions are met, but not always).
 *
 * Even if the function returns %FALSE the values of @x and @y are reasonable, but they may not correspond to an
 * actual maximum.
 *
 * The radii can be zero.  A single pixel is then examined, but if it is indeed a local maximum, its position is
 * refined.
 *
 * Returns: %TRUE if the maximum was successfully located.  %FALSE when the location is problematic and should not be
 *          used.
 *
 * Since: 2.49
 **/
gboolean
gwy_data_field_local_maximum(GwyDataField *dfield,
                             gdouble *x, gdouble *y,
                             gint ax, gint ay)
{
    gint xj, yi, mi, mj, i, j, k;
    gint xres, yres, xfrom, xto, yfrom, yto;
    gdouble xx, yy, v, max;
    const gdouble *d;
    gdouble z[9];
    gboolean ok;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), FALSE);
    g_return_val_if_fail(x, FALSE);
    g_return_val_if_fail(y, FALSE);

    xres = dfield->xres;
    yres = dfield->yres;
    xj = (gint)(*x);
    yi = (gint)(*y);
    ax = ABS(ax);
    ay = ABS(ay);

    gwy_debug("searching around: %g, %g (%d+-%d, %d+-%d)", *x, *y, xj, ax, yi, ay);
    mi = mj = 0;
    max = -G_MAXDOUBLE;
    yfrom = MAX(yi - ay, 0) - yi;
    yto = MIN(yi + ay, yres-1) - yi;
    for (i = yfrom; i <= yto; i++) {
        v = i/(ay + 0.5);
        k = (gint)floor((ax + 0.5)*sqrt(1.0 - v*v));
        xfrom = MAX(xj - k, 0) - xj;
        xto = MIN(xj + k, xres-1) - xj;
        d = dfield->data + (i + yi)*xres + (xj + xfrom);
        for (j = xfrom; j <= xto; j++, d++) {
            if (*d > max) {
                max = *d;
                mi = i;
                mj = j;
            }
        }
    }
    mj += xj;
    mi += yi;
    gwy_debug("pixel maximum at: %d, %d", mj, mi);

    /* No pixels found at all. */
    if (max == -G_MAXDOUBLE)
        return FALSE;

    /* Data field edge. */
    *x = mj;
    *y = mi;
    if (mi == 0 || mi == yres-1 || mj == 0 || mj == xres-1)
        return FALSE;

    d = dfield->data;
    k = mi*xres + mj;
    for (i = -1; i <= 1; i++) {
        for (j = -1; j <= 1; j++) {
            v = d[k + i*xres + j];
            /* Not an actual maximum. */
            if ((i || j) && v > max)
                return FALSE;
            z[3*(i + 1) + (j + 1)] = v;
        }
    }
    ok = gwy_math_refine_maximum_2d(z, &xx, &yy);
    gwy_debug("refinement by (%g, %g)", xx, yy);
    if (!ok)
        return FALSE;

    *x += xx;
    *y += yy;
    return TRUE;
}

/**
 * gwy_data_field_circular_area_fill:
 * @data_field: A data field.
 * @col: Row index of circular area centre.
 * @row: Column index of circular area centre.
 * @radius: Circular area radius (in pixels).  Any value is allowed, although to get areas that do not deviate from
 *          true circles after pixelization too much, half-integer values are recommended, integer values are NOT
 *          recommended.
 * @value: Value to be entered.
 *
 * Fills an elliptic region of a data field with given value.
 *
 * Returns: The number of filled values.
 **/
gint
gwy_data_field_circular_area_fill(GwyDataField *data_field,
                                  gint col, gint row,
                                  gdouble radius,
                                  gdouble value)
{
    gint i, j, r, r2, count, xres;
    gint ifrom, jfrom, ito, jto;
    gdouble *d, *drow;
    gdouble s;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);

    if (radius < 0.0)
        return 0;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    xres = data_field->xres;
    d = data_field->data;
    count = 0;

    /* Clip */
    ifrom = MAX(row - r, 0) - row;
    ito = MIN(row + r, data_field->yres-1) - row;

    for (i = ifrom; i <= ito; i++) {
        s = sqrt(r2 - i*i);
        jfrom = ceil(-s);
        jto = floor(s);
        if (jfrom + col < 0)
            jfrom = -col;
        if (jto + col >= xres)
            jto = xres-1 - col;
        if (jfrom > jto)
            continue;

        drow = d + (row + i)*xres + col + jfrom;
        for (j = jto+1 - jfrom; j; j--, drow++)
            *drow = value;
        count += MAX(jto+1 - jfrom, 0);
    }

    gwy_data_field_invalidate(data_field);

    return count;
}

/**
 * gwy_data_field_circular_area_extract:
 * @data_field: A data field.
 * @col: Row index of circular area centre.
 * @row: Column index of circular area centre.
 * @radius: Circular area radius (in pixels).  See gwy_data_field_circular_area_extract_with_pos() for caveats.
 * @data: Location to store the extracted values to.  See gwy_data_field_circular_area_extract_with_pos().
 *
 * Extracts values from a circular region of a data field.
 *
 * Returns: The number of extracted values.  It can be zero when the inside of the circle does not intersect with the
 *          data field.
 **/
gint
gwy_data_field_circular_area_extract(GwyDataField *data_field,
                                     gint col, gint row,
                                     gdouble radius,
                                     gdouble *data)
{
    gint i, r, r2, count, xres;
    gint ifrom, jfrom, ito, jto;
    const gdouble *d;
    gdouble s;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);
    g_return_val_if_fail(data, 0);

    if (radius < 0.0)
        return 0;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    xres = data_field->xres;
    d = data_field->data;
    count = 0;

    /* Clip */
    ifrom = MAX(row - r, 0) - row;
    ito = MIN(row + r, data_field->yres-1) - row;

    for (i = ifrom; i <= ito; i++) {
        s = sqrt(r2 - i*i);
        jfrom = ceil(-s);
        jto = floor(s);
        if (jfrom + col < 0)
            jfrom = -col;
        if (jto + col >= xres)
            jto = xres-1 - col;
        if (jto >= jfrom) {
            gwy_assign(data + count, d + (row + i)*xres + col + jfrom, jto - jfrom + 1);
            count += jto - jfrom + 1;
        }
    }

    return count;
}

/**
 * gwy_data_field_circular_area_extract_with_pos:
 * @data_field: A data field.
 * @col: Row index of circular area centre.
 * @row: Column index of circular area centre.
 * @radius: Circular area radius (in pixels).  Any value is allowed, although to get areas that do not deviate from
 *          true circles after pixelization too much, half-integer values are recommended, integer radii are NOT
 *          recommended.
 * @data: Location to store the extracted values to.  Its size has to be sufficient to contain all the extracted
 *        values.  As a conservative estimate (2*floor(@radius)+1)^2 can be used, or the size can be calculated with
 *        gwy_data_field_get_circular_area_size().
 * @xpos: Location to store relative column indices of values in @data to, the size requirements are the same as for
 *        @data.
 * @ypos: Location to store relative tow indices of values in @data to, the size requirements are the same as for
 *        @data.
 *
 * Extracts values with positions from a circular region of a data field.
 *
 * The row and column indices stored to @xpos and @ypos are relative to the area centre, i.e. to (@col, @row).  The
 * central pixel will therefore have 0 at the corresponding position in both @xpos and @ypos.
 *
 * Returns: The number of extracted values.  It can be zero when the inside of the circle does not intersect with the
 *          data field.
 *
 * Since: 2.2
 **/
gint
gwy_data_field_circular_area_extract_with_pos(GwyDataField *data_field,
                                              gint col, gint row,
                                              gdouble radius,
                                              gdouble *data,
                                              gint *xpos,
                                              gint *ypos)
{
    gint i, j, r, r2, count, xres;
    gint ifrom, jfrom, ito, jto;
    const gdouble *d;
    gdouble s;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), 0);
    g_return_val_if_fail(data, 0);

    if (radius < 0.0)
        return 0;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    xres = data_field->xres;
    d = data_field->data;
    count = 0;

    /* Clip */
    ifrom = MAX(row - r, 0) - row;
    ito = MIN(row + r, data_field->yres-1) - row;

    for (i = ifrom; i <= ito; i++) {
        s = sqrt(r2 - i*i);
        jfrom = ceil(-s);
        jto = floor(s);
        if (jfrom + col < 0)
            jfrom = -col;
        if (jto + col >= xres)
            jto = xres-1 - col;
        if (jto >= jfrom) {
            gwy_assign(data + count, d + (row + i)*xres + col + jfrom, jto - jfrom + 1);
            for (j = jfrom; j <= jto; j++) {
                xpos[count] = j;
                ypos[count] = i;
                count++;
            }
        }
    }

    return count;
}

/**
 * gwy_data_field_circular_area_unextract:
 * @data_field: A data field.
 * @col: Row index of circular area centre.
 * @row: Column index of circular area centre.
 * @radius: Circular area radius (in pixels).
 * @data: The values to put back.  It must be the same array as in previous gwy_data_field_circular_area_unextract().
 *
 * Puts values back to a circular region of a data field.
 *
 * This method does the reverse of gwy_data_field_circular_area_extract() allowing to implement pixel-wise filters on
 * circular areas.  Values from @data are put back to the same positions gwy_data_field_circular_area_extract() took
 * them from.
 **/
void
gwy_data_field_circular_area_unextract(GwyDataField *data_field,
                                       gint col, gint row,
                                       gdouble radius,
                                       const gdouble *data)
{
    gint i, r, r2, count, xres;
    gint ifrom, jfrom, ito, jto;
    gdouble *d;
    gdouble s;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(data);

    if (radius < 0.0)
        return;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    xres = data_field->xres;
    d = data_field->data;
    count = 0;

    /* Clip */
    ifrom = MAX(row - r, 0) - row;
    ito = MIN(row + r, data_field->yres-1) - row;

    for (i = ifrom; i <= ito; i++) {
        s = sqrt(r2 - i*i);
        jfrom = ceil(-s);
        jto = floor(s);
        if (jfrom + col < 0)
            jfrom = -col;
        if (jto + col >= xres)
            jto = xres-1 - col;
        if (jto >= jfrom) {
            gwy_assign(d + (row + i)*xres + col + jfrom, data + count, jto - jfrom + 1);
            count += jto - jfrom + 1;
        }
    }

    gwy_data_field_invalidate(data_field);
}

/**
 * gwy_data_field_get_circular_area_size:
 * @radius: Circular area radius (in pixels).
 *
 * Calculates an upper bound of the number of samples in a circular region.
 *
 * Returns: The number of pixels in a circular region with given rectangular bounds (or its upper bound).
 **/
gint
gwy_data_field_get_circular_area_size(gdouble radius)
{
    gint i, r, r2, count, jto, jfrom;
    gdouble s;

    if (radius < 0.0)
        return 0;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    count = 0;

    for (i = -r; i <= r; i++) {
        s = sqrt(r2 - i*i);
        jfrom = ceil(-s);
        jto = floor(s);
        count += jto - jfrom + 1;
    }

    return count;
}

/************************** Documentation ****************************/

/**
 * SECTION:elliptic
 * @title: elliptic
 * @short_description: Functions to work with elliptic areas
 *
 * Methods for extraction and putting back data from/to elliptic and circular areas can be used to implement
 * sample-wise operations, that is operations that depend only on sample value not on its position, on these areas:
 *
 * |[gdouble *data;
 * gint n, i;
 *
 * data = g_new(gdouble, width*height);
 * n = gwy_data_field_elliptic_area_extract(data_field,
 *                                          col, row, width, height,
 *                                          data);
 * for (i = 0; i < n; i++) {
 *    ... do something with data[i] ...
 * }
 * gwy_data_field_elliptic_area_unextract(data_field,
 *                                        col, row, width, height,
 *                                        data);]|
 *
 * Another possibility is to use #GwyDataLine methods on the extracted data (in practice one would use the same data
 * line repeatedly, of course):
 *
 * |[GwyDataLine *data_line;
 * gdouble *data;
 * gint n;
 *
 * n = gwy_data_field_get_elliptic_area_size(data_field, width, height);
 * data_line = gwy_data_line_new(n, 1.0, FALSE);
 * data = gwy_data_line_get_data(data_line);
 * gwy_data_field_elliptic_area_extract(data_field,
 *                                      col, row, width, height,
 *                                      data);
 * gwy_data_line_pixelwise_filter(data_line, ...);
 * gwy_data_field_elliptic_area_unextract(data_field,
 *                                        col, row, width, height,
 *                                        data);
 * g_object_unref(data_line);]|
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
