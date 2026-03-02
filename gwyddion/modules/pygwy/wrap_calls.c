/*
 *  $Id: wrap_calls.c 25260 2023-03-06 11:25:17Z yeti-dn $
 *  Copyright (C) 2008 Jan Horak, 2015-2023 David Necas (Yeti)
 *  E-mail: xhorak@gmail.com, yeti@gwyddion.net.
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
 *
 *  Description: This file contains custom fuctions for automatically generated wrapping by using pygwy-codegen.
 */

#include "wrap_calls.h"
#include <stdlib.h>
#include <string.h>

/* function-helper to short array creation */
static GwyDoubleArray*
create_double_array(const gdouble *data, guint len, gboolean free_data)
{
    GArray *ret = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_append_vals(ret, data, len);
    if (free_data)
        g_free((gpointer)data);
    return ret;
}

static GwyIntArray*
create_int_array(const gint *data, guint len, gboolean free_data)
{
    GArray *ret = g_array_new(FALSE, FALSE, sizeof(gint));
    g_array_append_vals(ret, data, len);
    if (free_data)
        g_free((gpointer)data);
    return ret;
}

static void
free_string_array(GwyStringArray *array)
{
    gsize i, n = array->len;
    gchar *s;

    for (i = 0; i < n; i++) {
        s = g_array_index(array, gchar*, i);
        if (!s)
            break;
        g_free(s);
    }
    g_array_free(array, TRUE);
}

static GwyStringArray*
create_string_array(const gchar **data, guint len, gboolean free_data)
{
   GArray *ret = g_array_new(FALSE, FALSE, sizeof(gchar*));
   g_array_append_vals(ret, data, len);
   if (free_data)
      g_free((gpointer)data);
   return ret;
}

static GwyConstStringArray*
create_const_string_array(const gchar *const *data, guint len,
                          gboolean free_data)
{
   GArray *ret = g_array_new(FALSE, FALSE, sizeof(gchar*));
   g_array_append_vals(ret, data, len);
   if (free_data)
      g_free((gpointer)data);
   return ret;
}

static GwyDataFieldArray*
create_data_field_array(GwyDataField **data, guint len, gboolean free_data)
{
   GArray *ret = g_array_new(FALSE, FALSE, sizeof(GwyDataField*));
   g_array_append_vals(ret, data, len);
   if (free_data)
      g_free(data);
   return ret;
}

gdouble
gwy_math_median_pygwy(GwyDoubleArray *array)
{
    gdouble retval = gwy_math_median(array->len, (gdouble*)array->data);
    g_array_free(array, TRUE);
    return retval;
}

gdouble
gwy_math_kth_rank_pygwy(GwyDoubleArray *array, guint k)
{
    gdouble retval = gwy_math_kth_rank(array->len, (gdouble*)array->data, k);
    g_array_free(array, TRUE);
    return retval;
}

gdouble
gwy_math_trimmed_mean_pygwy(GwyDoubleArray *array,
                            guint nlowest, guint nhighest)
{
    gdouble retval = gwy_math_trimmed_mean(array->len, (gdouble*)array->data, nlowest, nhighest);
    g_array_free(array, TRUE);
    return retval;
}

GwyArrayFuncStatus
gwy_math_curvature_pygwy(GwyDoubleArray *coeffs, gint *dimen,
                         gdouble *kappa1, gdouble *kappa2,
                         gdouble *phi1, gdouble *phi2,
                         gdouble *xc, gdouble *yc, gdouble *zc)
{
    gboolean ok = (coeffs->len == 6);
    if (ok)
        *dimen = gwy_math_curvature((gdouble*)coeffs->data, kappa1, kappa2, phi1, phi2, xc, yc, zc);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_refine_maximum_pygwy(GwyDoubleArray *z,
                              gdouble *x, gdouble *y, gboolean *refined)
{
    gboolean ok = (z->len == 9);
    if (ok)
        *refined = gwy_math_refine_maximum((gdouble*)z->data, x, y);
    g_array_free(z, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_refine_maximum_2d_pygwy(GwyDoubleArray *z,
                                 gdouble *x, gdouble *y, gboolean *refined)
{
    gboolean ok = (z->len == 9);
    if (ok)
        *refined = gwy_math_refine_maximum_2d((gdouble*)z->data, x, y);
    g_array_free(z, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_refine_maximum_1d_pygwy(GwyDoubleArray *y,
                                 gdouble *x, gboolean *refined)
{
    gboolean ok = (y->len == 3);
    if (ok)
        *refined = gwy_math_refine_maximum_1d((gdouble*)y->data, x);
    g_array_free(y, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_is_in_polygon_pygwy(gdouble x, gdouble y, GwyDoubleArray *poly,
                             gboolean *is_inside)
{
    gboolean ok = !(poly->len % 2);
    if (ok)
        *is_inside = gwy_math_is_in_polygon(x, y, (gdouble*)poly->data, poly->len/2);
    g_array_free(poly, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_find_nearest_line_pygwy(gdouble x, gdouble y,
                                 GwyDoubleArray *coords, GwyDoubleArray *metric,
                                 gint *idx, gdouble *d2min)
{
    gboolean ok = !(coords->len % 4) && (!metric || metric->len == 4);
    if (ok) {
        *idx = gwy_math_find_nearest_line(x, y, d2min, coords->len/2, (gdouble*)coords->data,
                                          metric ? (gdouble*)metric->data : NULL);
    }
    g_array_free(coords, TRUE);
    if (metric)
        g_array_free(metric, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_find_nearest_point_pygwy(gdouble x, gdouble y,
                                  GwyDoubleArray *coords, GwyDoubleArray *metric,
                                  gint *idx, gdouble *d2min)
{
    gboolean ok = !(coords->len % 2) && (!metric || metric->len == 4);
    if (ok) {
        *idx = gwy_math_find_nearest_point(x, y, d2min, coords->len/2, (gdouble*)coords->data,
                                           metric ? (gdouble*)metric->data : NULL);
    }
    g_array_free(coords, TRUE);
    if (metric)
        g_array_free(metric, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_math_fit_polynom_pygwy(GwyDoubleArray *xdata, GwyDoubleArray *ydata,
                           gint n,
                           GwyDoubleArrayOutArg coeffs)
{
    gboolean ok = (ydata->len == xdata->len);
    if (ok) {
        g_array_set_size(coeffs, n+1);
        gwy_math_fit_polynom(xdata->len, (gdouble*)xdata->data, (gdouble*)ydata->data,
                             n, (gdouble*)coeffs->data);
    }
    else
        g_array_free(coeffs, TRUE);
    g_array_free(xdata, TRUE);
    g_array_free(ydata, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_fft_window_pygwy(GwyDoubleArray *data, GwyWindowingType windowing)
{
    gwy_fft_window(data->len, (gdouble*)data->data, windowing);
    /* This is the same as freeing data and returning a new array. */
    return data;
}

GwyDoubleArray*
gwy_interpolation_resolve_coeffs_1d_pygwy(GwyDoubleArray *data,
                                          GwyInterpolationType interpolation)
{
    gwy_interpolation_resolve_coeffs_1d(data->len, (gdouble*)data->data,
                                        interpolation);
    /* This is the same as freeing data and returning a new array. */
    return data;
}

GwyDoubleArray*
gwy_interpolation_resolve_coeffs_2d_pygwy(gint width,
                                          gint height,
                                          gint rowstride,
                                          GwyDoubleArray *data,
                                          GwyInterpolationType interpolation)
{
    g_return_val_if_fail(data->len == height*rowstride, data);
    g_return_val_if_fail(width <= rowstride, data);
    gwy_interpolation_resolve_coeffs_2d(width, height, rowstride, (gdouble*)data->data, interpolation);
    /* This is the same as freeing data and returning a new array. */
    return data;
}

GwyArrayFuncStatus
gwy_interpolation_get_dval_of_equidists_pygwy(gdouble x,
                                              GwyDoubleArray *data,
                                              GwyInterpolationType interpolation,
                                              gdouble *result)
{
    guint suplen = gwy_interpolation_get_support_size(interpolation);
    gboolean ok = (data->len == suplen || suplen == 0);
    if (ok)
        *result = gwy_interpolation_get_dval_of_equidists(x, (gdouble*)data->data, interpolation);
    g_array_free(data, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_interpolation_interpolate_1d_pygwy(gdouble x,
                                       GwyDoubleArray *coeff,
                                       GwyInterpolationType interpolation,
                                       gdouble *result)
{
    guint suplen = gwy_interpolation_get_support_size(interpolation);
    gboolean ok = (coeff->len == suplen || suplen == 0);
    if (ok)
        *result = gwy_interpolation_interpolate_1d(x, (gdouble*)coeff->data, interpolation);
    g_array_free(coeff, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_interpolation_interpolate_2d_pygwy(gdouble x, gdouble y,
                                       gint rowstride,
                                       GwyDoubleArray *coeff,
                                       GwyInterpolationType interpolation,
                                       gdouble *result)
{
    guint suplen = gwy_interpolation_get_support_size(interpolation);
    gboolean ok = (coeff->len == suplen*rowstride || suplen == 0);
    if (ok)
        *result = gwy_interpolation_interpolate_2d(x, y, rowstride, (gdouble*)coeff->data, interpolation);
    g_array_free(coeff, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_interpolation_resample_block_1d_pygwy(GwyDoubleArray *data,
                                          gint newlength,
                                          GwyInterpolationType interpolation)
{
    GwyDoubleArray *ret = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(ret, newlength);
    gwy_interpolation_resample_block_1d(data->len, (gdouble*)data->data,
                                        newlength, (gdouble*)ret->data,
                                        interpolation, FALSE);
    return ret;
}

GwyDoubleArray*
gwy_interpolation_resample_block_2d_pygwy(GwyDoubleArray *data,
                                          gint width,
                                          gint height,
                                          gint rowstride,
                                          gint newwidth,
                                          gint newheight,
                                          gint newrowstride,
                                          GwyInterpolationType interpolation)
{
    GwyDoubleArray *ret = g_array_new(FALSE, TRUE, sizeof(gdouble));
    g_array_set_size(ret, newrowstride*newheight);
    g_return_val_if_fail(data->len == height*rowstride, ret);
    gwy_interpolation_resample_block_2d(width, height, rowstride, (gdouble*)data->data,
                                        newwidth, newheight, newrowstride, (gdouble*)ret->data,
                                        interpolation, FALSE);
    return ret;
}

GwyDoubleArray*
gwy_interpolation_shift_block_1d_pygwy(GwyDoubleArray *data,
                                       gdouble offset,
                                       GwyInterpolationType interpolation,
                                       GwyExteriorType exterior,
                                       gdouble fill_value)
{
    GwyDoubleArray *ret = g_array_new(FALSE, FALSE, sizeof(gdouble));
    gwy_interpolation_shift_block_1d(data->len, (gdouble*)data->data,
                                     offset, (gdouble*)ret->data,
                                     interpolation, exterior, fill_value, FALSE);
    return ret;
}

/**
 * gwy_selection_get_data_pygwy:
 * @selection: A selection.
 *
 * Get selection coordinates as single flat list.
 *
 * Returns: a list of selected data
**/
GwyDoubleArray*
gwy_selection_get_data_pygwy(GwySelection *selection)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    guint n = gwy_selection_get_data(selection, NULL);
    guint objsize = gwy_selection_get_object_size(selection);
    g_array_set_size(array, n*objsize);
    gwy_selection_get_data(selection, (gdouble*)array->data);
    return array;
}

GwyDoubleArray*
gwy_selection_get_object_pygwy(GwySelection *selection, gint i)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    guint objsize = gwy_selection_get_object_size(selection);
    g_array_set_size(array, objsize);
    gwy_selection_get_object(selection, i, (gdouble*)array->data);
    return array;
}

GwyArrayFuncStatus
gwy_selection_set_data_pygwy(GwySelection *selection,
                             GwyDoubleArray *data)
{
    guint n = data->len;
    guint objsize = gwy_selection_get_object_size(selection);
    gboolean ok = !(n % objsize);
    if (ok)
        gwy_selection_set_data(selection, n/objsize, (gdouble*)data->data);
    g_array_free(data, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_selection_set_object_pygwy(GwySelection *selection,
                               gint i,
                               GwyDoubleArray *data)
{
    guint n = data->len;
    guint objsize = gwy_selection_get_object_size(selection);
    gboolean ok = (n == objsize);
    if (ok)
        gwy_selection_set_object(selection, i, (gdouble*)data->data);
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_data_line_get_data_pygwy:
 *
 * Extract the data of a data line.
 *
 * The returned list contains a copy of the data.  Changing its contents does not change the data line's data.
 *
 * Returns: List containing extracted data line data.
 **/
GwyDoubleArray*
gwy_data_line_get_data_pygwy(GwyDataLine *dline)
{
    return create_double_array(dline->data, dline->res, FALSE);
}

/**
 * gwy_data_field_get_data_pygwy:
 *
 * Extract the data of a data field.
 *
 * The returned list contains a copy of the data.  Changing its contents does not change the data field's data.
 *
 * Returns: List containing extracted data field data.
 **/
GwyDoubleArray*
gwy_data_field_get_data_pygwy(GwyDataField *dfield)
{
    return create_double_array(dfield->data, dfield->xres*dfield->yres, FALSE);
}

/**
 * gwy_brick_get_data_pygwy:
 *
 * Extract the data of a data brick.
 *
 * The returned list contains a copy of the data.  Changing its contents does not change the data brick's data.
 *
 * Returns: List containing extracted data brick data.
 **/
GwyDoubleArray*
gwy_brick_get_data_pygwy(GwyBrick *brick)
{
    return create_double_array(brick->data, brick->xres*brick->yres*brick->zres, FALSE);
}

/**
 * gwy_lawn_get_curve_data_pygwy:
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @n: Index of a curve in @lawn.
 *
 * Extract the data of one curve at given position in a curve map.
 *
 * The returned list contains a copy of the data.  Changing its contents does not change the lawn's data.
 *
 * Returns: List containing extracted curve data.
 **/
GwyDoubleArray*
gwy_lawn_get_curve_data_pygwy(GwyLawn *lawn, gint col, gint row, gint n)
{
    gint len = 0;
    const gdouble *data = gwy_lawn_get_curve_data_const(lawn, col, row, n, &len);
    return create_double_array(data, len, FALSE);
}

/**
 * gwy_lawn_get_curves_data_pygwy:
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 *
 * Extract the data of all curves at given position in a curve map.
 *
 * The returned list contains a copy of the data.  Changing its contents does not change the lawn's data.
 *
 * Returns: Flat list containing extracted data of all curves.
 **/
GwyDoubleArray*
gwy_lawn_get_curves_data_pygwy(GwyLawn *lawn, gint col, gint row)
{
    gint len = 0;
    const gdouble *data = gwy_lawn_get_curves_data_const(lawn, col, row, &len);
    return create_double_array(data, len, FALSE);
}

/**
 * gwy_data_line_set_data_pygwy:
 * @data: Sequence of floating point values.
 *
 * Sets the entire contents of a data line.
 *
 * The length of @data must be equal to the number of elements of the data line.
 **/
GwyArrayFuncStatus
gwy_data_line_set_data_pygwy(GwyDataLine *data_line,
                             GwyDoubleArray *data)
{
    guint n = data_line->res;
    gboolean ok = (data->len == n);
    if (ok) {
        gwy_assign(data_line->data, (gdouble*)data->data, n);
        /* gwy_data_line_invalidate(data_line); */
    }
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_data_field_set_data_pygwy:
 * @data: Sequence of floating point values.
 *
 * Sets the entire contents of a data field.
 *
 * The length of @data must be equal to the number of elements of the data field.
 **/
GwyArrayFuncStatus
gwy_data_field_set_data_pygwy(GwyDataField *data_field,
                              GwyDoubleArray *data)
{
    guint n = data_field->xres*data_field->yres;
    gboolean ok = (data->len == n);
    if (ok) {
        gwy_assign(data_field->data, (gdouble*)data->data, n);
        gwy_data_field_invalidate(data_field);
    }
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_brick_set_data_pygwy:
 * @data: Sequence of floating point values.
 *
 * Sets the entire contents of a data brick.
 *
 * The length of @data must be equal to the number of elements of the data brick.
 **/
GwyArrayFuncStatus
gwy_brick_set_data_pygwy(GwyBrick *brick,
                         GwyDoubleArray *data)
{
    guint n = brick->xres*brick->yres*brick->zres;
    gboolean ok = (data->len == n);
    if (ok) {
        gwy_assign(brick->data, (gdouble*)data->data, n);
        /* gwy_brick_invalidate(brick); */
    }
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_lawn_set_curve_data_pygwy:
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @n: Index of a curve in @lawn.
 * @data: Sequence of floating point values.
 *
 * Sets the data of one curve in a curve map.
 *
 * The number of points remains the same since all curves at give pixel must have the same number of points.  If you
 * want to change the number of points set the data of all curves together using gwy_lawn_set_curves().
 **/
GwyArrayFuncStatus
gwy_lawn_set_curve_data_pygwy(GwyLawn *lawn,
                              gint col,
                              gint row,
                              gint n,
                              GwyDoubleArray *data)
{
    gboolean ok;
    gint len = 0;
    gwy_lawn_get_curve_data_const(lawn, col, row, n, &len);
    if ((ok = (data->len == len)))
        gwy_lawn_set_curve_data(lawn, col, row, n, (gdouble*)data->data);
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_data_field_fit_polynom_pygwy:
 * @data_field: A data field.
 * @col_degree: Degree of polynomial to fit column-wise (x-coordinate).
 * @row_degree: Degree of polynomial to fit row-wise (y-coordinate).
 *
 * Fits a two-dimensional polynomial to a data field.
 *
 * Returns: a newly allocated array with coefficients.
 **/
GwyDoubleArray*
gwy_data_field_fit_polynom_pygwy(GwyDataField *data_field,
                                 gint col_degree, gint row_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (col_degree+1)*(row_degree+1));
    gwy_data_field_fit_polynom(data_field, col_degree, row_degree, (gdouble*)coeffs->data);
    return coeffs;
}

/**
 * gwy_data_field_area_fit_polynom_pygwy:
 * @data_field: A data field.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @col_degree: Degree of polynomial to fit column-wise (x-coordinate).
 * @row_degree: Degree of polynomial to fit row-wise (y-coordinate).
 *
 * Fits a two-dimensional polynomial to a rectangular part of a data field.
 *
 * The coefficients are stored by row into @coeffs, like data in a datafield. Row index is y-degree, column index is
 * x-degree.
 *
 * Note naive x^n y^m polynomial fitting is numerically unstable, therefore this method works only up to @col_degree
 * = @row_degree = 6.
 *
 * Returns: a newly allocated array with coefficients.
 *
 **/
GwyDoubleArray*
gwy_data_field_area_fit_polynom_pygwy(GwyDataField *data_field,
                                      gint col, gint row,
                                      gint width, gint height,
                                      gint col_degree, gint row_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (col_degree+1)*(row_degree+1));
    gwy_data_field_area_fit_polynom(data_field, col, row, width, height, col_degree, row_degree,
                                    (gdouble*)coeffs->data);
    return coeffs;
}

GwyArrayFuncStatus
gwy_data_field_subtract_polynom_pygwy(GwyDataField *data_field,
                                      gint col_degree,
                                      gint row_degree,
                                      GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (col_degree+1)*(row_degree+1));
    if (ok)
        gwy_data_field_subtract_polynom(data_field, col_degree, row_degree, (gdouble*)coeffs->data);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_area_subtract_polynom_pygwy(GwyDataField *data_field,
                                           gint col, gint row,
                                           gint width, gint height,
                                           gint col_degree,
                                           gint row_degree,
                                           GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (col_degree+1)*(row_degree+1));
    if (ok) {
        gwy_data_field_area_subtract_polynom(data_field, col, row, width, height,
                                             col_degree, row_degree, (gdouble*)coeffs->data);
    }
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_data_field_fit_legendre_pygwy(GwyDataField *data_field,
                                  gint col_degree,
                                  gint row_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (col_degree+1)*(row_degree+1));
    gwy_data_field_fit_legendre(data_field, col_degree, row_degree, (gdouble*)coeffs->data);
    return coeffs;
}

GwyDoubleArray*
gwy_data_field_area_fit_legendre_pygwy(GwyDataField *data_field,
                                       gint col, gint row,
                                       gint width, gint height,
                                       gint col_degree,
                                       gint row_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (col_degree+1)*(row_degree+1));
    gwy_data_field_area_fit_legendre(data_field, col, row, width, height, col_degree, row_degree,
                                     (gdouble*)coeffs->data);
    return coeffs;
}

GwyArrayFuncStatus
gwy_data_field_subtract_legendre_pygwy(GwyDataField *data_field,
                                       gint col_degree,
                                       gint row_degree,
                                       GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (col_degree+1)*(row_degree+1));
    if (ok)
        gwy_data_field_subtract_legendre(data_field, col_degree, row_degree, (gdouble*)coeffs->data);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_area_subtract_legendre_pygwy(GwyDataField *data_field,
                                            gint col, gint row,
                                            gint width, gint height,
                                            gint col_degree,
                                            gint row_degree,
                                            GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (col_degree+1)*(row_degree+1));
    if (ok) {
        gwy_data_field_area_subtract_legendre(data_field, col, row, width, height, col_degree, row_degree,
                                              (gdouble*)coeffs->data);
    }
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_data_field_fit_poly_max_pygwy(GwyDataField *data_field,
                                  gint max_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (max_degree+1)*(max_degree+2)/2);
    gwy_data_field_fit_poly_max(data_field, max_degree, (gdouble*)coeffs->data);
    return coeffs;
}

GwyDoubleArray*
gwy_data_field_area_fit_poly_max_pygwy(GwyDataField *data_field,
                                       gint col, gint row,
                                       gint width, gint height,
                                       gint max_degree)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, (max_degree+1)*(max_degree+2)/2);
    gwy_data_field_area_fit_poly_max(data_field, col, row, width, height,
                                     max_degree, (gdouble*)coeffs->data);
    return coeffs;
}

GwyArrayFuncStatus
gwy_data_field_subtract_poly_max_pygwy(GwyDataField *data_field,
                                       gint max_degree,
                                       GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (max_degree+1)*(max_degree+2)/2);
    if (ok)
        gwy_data_field_subtract_poly_max(data_field, max_degree, (gdouble*)coeffs->data);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_area_subtract_poly_max_pygwy(GwyDataField *data_field,
                                            gint col, gint row,
                                            gint width, gint height,
                                            gint max_degree,
                                            GwyDoubleArray *coeffs)
{
    gboolean ok = (coeffs->len == (max_degree+1)*(max_degree+2)/2);
    if (ok)
        gwy_data_field_area_subtract_poly_max(data_field, col, row, width, height, max_degree, (gdouble*)coeffs->data);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_fit_poly_pygwy(GwyDataField *data_field,
                              GwyDataField *mask_field,
                              GwyIntArray *term_powers,
                              gboolean exclude,
                              GwyDoubleArrayOutArg coeffs)
{
    gboolean ok = !(term_powers->len % 2);
    if (ok) {
        guint nterms = term_powers->len/2;
        g_array_set_size(coeffs, nterms);
        gwy_data_field_fit_poly(data_field, mask_field,
                                nterms, (gint*)term_powers->data, exclude, (gdouble*)coeffs->data);
    }
    else
        g_array_free(coeffs, TRUE);
    g_array_free(term_powers, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_area_fit_poly_pygwy(GwyDataField *data_field,
                                   GwyDataField *mask_field,
                                   gint col, gint row,
                                   gint width, gint height,
                                   GwyIntArray *term_powers,
                                   gboolean exclude,
                                   GwyDoubleArrayOutArg coeffs)
{
    gboolean ok = !(term_powers->len % 2);
    if (ok) {
        guint nterms = term_powers->len/2;
        g_array_set_size(coeffs, nterms);
        gwy_data_field_area_fit_poly(data_field, mask_field, col, row, width, height,
                                     nterms, (gint*)term_powers->data, exclude, (gdouble*)coeffs->data);
    }
    else
        g_array_free(coeffs, TRUE);
    g_array_free(term_powers, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_subtract_poly_pygwy(GwyDataField *data_field,
                                   GwyIntArray *term_powers,
                                   GwyDoubleArray *coeffs)
{
    guint nterms = coeffs->len;
    gboolean ok = (term_powers->len == 2*nterms);
    if (ok)
        gwy_data_field_subtract_poly(data_field, nterms, (gint*)term_powers->data, (gdouble*)coeffs->data);
    g_array_free(term_powers, TRUE);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_area_subtract_poly_pygwy(GwyDataField *data_field,
                                        gint col, gint row,
                                        gint width, gint height,
                                        GwyIntArray *term_powers,
                                        GwyDoubleArray *coeffs)
{
    guint nterms = coeffs->len;
    gboolean ok = (term_powers->len == 2*nterms);
    if (ok) {
        gwy_data_field_area_subtract_poly(data_field, col, row, width, height,
                                          nterms, (gint*)term_powers->data, (gdouble*)coeffs->data);
    }
    g_array_free(term_powers, TRUE);
    g_array_free(coeffs, TRUE);
    return ok;
}

GwyDataFieldArray*
gwy_data_field_area_fit_local_planes_pygwy(GwyDataField *data_field,
                                           gint size,
                                           gint col, gint row,
                                           gint width, gint height,
                                           GwyIntArray *types)
{
    guint nresults = types->len;
    GwyPlaneFitQuantity *qtypes = &g_array_index(types, GwyPlaneFitQuantity, 0);
    GwyDataField **results = gwy_data_field_area_fit_local_planes(data_field, size, col, row, width, height,
                                                                  nresults, qtypes, NULL);
    return create_data_field_array(results, nresults, TRUE);
}

GwyDataFieldArray*
gwy_data_field_fit_local_planes_pygwy(GwyDataField *data_field,
                                      gint size,
                                      GwyIntArray *types)
{
    guint nresults = types->len;
    GwyPlaneFitQuantity *qtypes = &g_array_index(types, GwyPlaneFitQuantity, 0);
    GwyDataField **results = gwy_data_field_fit_local_planes(data_field, size, nresults, qtypes, NULL);
    return create_data_field_array(results, nresults, TRUE);
}

/**
 * gwy_data_field_elliptic_area_extract_pygwy:
 * @data_field: A data field.
 * @col: Upper-left bounding box column coordinate.
 * @row: Upper-left bounding box row coordinate.
 * @width: Bounding box width (number of columns).
 * @height: Bounding box height (number of rows).
 *
 * Extracts values from an elliptic region of a data field.
 *
 * The elliptic region is defined by its bounding box which must be completely
 * contained in the data field.
 *
 * Returns: The number of extracted values.
 **/
GwyDoubleArray*
gwy_data_field_elliptic_area_extract_pygwy(GwyDataField *data_field,
                                           gint col, gint row,
                                           gint width, gint height)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(array, gwy_data_field_get_elliptic_area_size(width, height));
    gwy_data_field_elliptic_area_extract(data_field, col, row, width, height, (gdouble*)array->data);
    return array;
}

GwyArrayFuncStatus
gwy_data_field_elliptic_area_unextract_pygwy(GwyDataField *data_field,
                                             gint col, gint row,
                                             gint width, gint height,
                                             GwyDoubleArray *data)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    gboolean ok = (data->len == gwy_data_field_get_elliptic_area_size(width, height));
    if (ok) {
        gwy_data_field_elliptic_area_unextract(data_field, col, row, width, height, (gdouble*)array->data);
    }
    g_array_free(data, TRUE);
    return ok;
}

/**
 * gwy_data_field_circular_area_extract_pygwy:
 * @data_field: A data field.
 * @col: Row index of circular area centre.
 * @row: Column index of circular area centre.
 * @radius: Circular area radius (in pixels).  See gwy_data_field_circular_area_extract_with_pos() for caveats.
 *
 * Extracts values from a circular region of a data field.
 *
 * Returns: Array of values.
 **/
GwyDoubleArray*
gwy_data_field_circular_area_extract_pygwy(GwyDataField *data_field,
                                           gint col, gint row,
                                           gdouble radius)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(array, gwy_data_field_get_circular_area_size(radius));
    gwy_data_field_circular_area_extract(data_field, col, row, radius, (gdouble*)array->data);
    return array;
}

GwyArrayFuncStatus
gwy_data_field_circular_area_unextract_pygwy(GwyDataField *data_field,
                                             gint col, gint row,
                                             gdouble radius,
                                             GwyDoubleArray *data)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    gboolean ok = (data->len == gwy_data_field_get_circular_area_size(radius));
    if (ok)
        gwy_data_field_circular_area_unextract(data_field, col, row, radius, (gdouble*)array->data);
    g_array_free(data, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_data_field_circular_area_extract_with_pos_pygwy(GwyDataField *data_field,
                                                    gint col, gint row,
                                                    gdouble radius,
                                                    GwyIntArrayOutArg xpos,
                                                    GwyIntArrayOutArg ypos)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    guint size = gwy_data_field_get_circular_area_size(radius);
    g_array_set_size(array, size);
    g_array_set_size(xpos, size);
    g_array_set_size(ypos, size);
    gwy_data_field_circular_area_extract_with_pos(data_field, col, row, radius,
                                                  (gdouble*)array->data, (gint*)xpos->data, (gint*)ypos->data);
    return array;
}

gboolean
gwy_data_field_local_maximum_pygwy(GwyDataField *dfield,
                                   gdouble x, gdouble y,
                                   gint ax, gint ay,
                                   gdouble *x_out, gdouble *y_out)
{
    *x_out = x;
    *y_out = y;
    return gwy_data_field_local_maximum(dfield, x_out, y_out, ax, ay);
}

GwyArrayFuncStatus
gwy_data_field_affine_pygwy(GwyDataField *data_field,
                            GwyDataField *dest,
                            GwyDoubleArray *affine,
                            GwyInterpolationType interp,
                            GwyExteriorType exterior,
                            gdouble fill_value)
{
    gboolean ok = (affine->len == 6);
    if (ok)
        gwy_data_field_affine(data_field, dest, (gdouble*)affine->data, interp, exterior, fill_value);
    g_array_free(affine, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_affine_prepare_pygwy(GwyDataField *source,
                                    GwyDataField *dest,
                                    GwyDoubleArray *a1a2,
                                    GwyDoubleArray *a1a2_corr,
                                    GwyAffineScalingType scaling,
                                    gboolean prevent_rotation,
                                    gdouble oversampling,
                                    GwyDoubleArrayOutArg a1a2_corr_out,
                                    GwyDoubleArrayOutArg invtrans)
{
    gboolean ok = (a1a2->len == 4 && a1a2_corr->len == 4);
    g_array_set_size(a1a2_corr_out, 4);
    g_array_set_size(invtrans, 6);
    gwy_clear((gdouble*)invtrans->data, 6);
    if (ok) {
        gwy_assign((gdouble*)a1a2_corr_out->data, (gdouble*)a1a2_corr->data, 4);
        gwy_data_field_affine_prepare(source, dest,
                                      (const gdouble*)a1a2->data,
                                      (gdouble*)a1a2_corr_out->data,
                                      (gdouble*)invtrans->data,
                                      scaling, prevent_rotation, oversampling);
    }
    else {
        gwy_clear((gdouble*)a1a2_corr_out->data, 4);
    }
    g_array_free(a1a2, TRUE);
    g_array_free(a1a2_corr, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_measure_lattice_acf_pygwy(GwyDataField *acf2d,
                                         GwyDoubleArray *a1a2,
                                         GwyDoubleArrayOutArg a1a2_out,
                                         gboolean *succeeded)
{
    gboolean ok = (a1a2->len == 4);
    g_array_set_size(a1a2_out, 4);
    gwy_clear((gdouble*)a1a2_out->data, 4);
    *succeeded = FALSE;
    if (ok) {
        gwy_assign((gdouble*)a1a2_out->data, (gdouble*)a1a2->data, 4);
        *succeeded = gwy_data_field_measure_lattice_acf(acf2d, (gdouble*)a1a2_out->data);
    }
    if (!*succeeded || !ok)
        gwy_clear((gdouble*)a1a2_out->data, 4);
    g_array_free(a1a2, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_measure_lattice_psdf_pygwy(GwyDataField *psdf2d,
                                          GwyDoubleArray *a1a2,
                                          GwyDoubleArrayOutArg a1a2_out,
                                          gboolean *succeeded)
{
    gboolean ok = (a1a2->len == 4);
    g_array_set_size(a1a2_out, 4);
    gwy_clear((gdouble*)a1a2_out->data, 4);
    *succeeded = FALSE;
    if (ok) {
        gwy_assign((gdouble*)a1a2_out->data, (gdouble*)a1a2->data, 4);
        *succeeded = gwy_data_field_measure_lattice_psdf(psdf2d, (gdouble*)a1a2_out->data);
    }
    if (!*succeeded || !ok)
        gwy_clear((gdouble*)a1a2_out->data, 4);
    g_array_free(a1a2, TRUE);
    return ok;
}

gint
gwy_data_field_waterpour_pygwy(GwyDataField *data_field,
                               GwyDataField *result,
                               GwyIntArrayOutArg grains)
{
    guint n = data_field->xres*data_field->yres;
    g_array_set_size(grains, n);
    gwy_clear(((gint*)grains->data), n);
    return gwy_data_field_waterpour(data_field, result, (gint*)grains->data);
}

void
gwy_data_field_get_local_maxima_list_pygwy(GwyDataField *dfield,
                                           GwyDoubleArrayOutArg xdata,
                                           GwyDoubleArrayOutArg ydata,
                                           GwyDoubleArrayOutArg zdata,
                                           gint ndata,
                                           gint skip,
                                           gdouble threshold,
                                           gboolean subpixel)
{
    g_array_set_size(xdata, ndata);
    g_array_set_size(ydata, ndata);
    g_array_set_size(zdata, ndata);
    ndata = gwy_data_field_get_local_maxima_list(dfield,
                                                 (gdouble*)xdata->data, (gdouble*)ydata->data, (gdouble*)zdata->data,
                                                 ndata, skip, threshold, subpixel);
    g_array_set_size(xdata, ndata);
    g_array_set_size(ydata, ndata);
    g_array_set_size(zdata, ndata);
}

GwyPlaneSymmetry
gwy_data_field_unrotate_find_corrections_pygwy(GwyDataLine *derdist,
                                               GwyDoubleArrayOutArg correction)
{
    g_array_set_size(correction, GWY_SYMMETRY_LAST);
    return gwy_data_field_unrotate_find_corrections(derdist,
                                                    (gdouble*)correction->data);
}

GwyDoubleArray*
gwy_data_field_get_profile_mask_pygwy(GwyDataField *dfield,
                                      GwyDataField *mask,
                                      GwyMaskingType masking,
                                      gdouble xfrom, gdouble yfrom,
                                      gdouble xto, gdouble yto,
                                      gint res,
                                      gint thickness,
                                      GwyInterpolationType interpolation)
{
    GwyDoubleArray *xyarray;
    gint ndata;
    GwyXY *xy;

    xy = gwy_data_field_get_profile_mask(dfield, &ndata, mask, masking, xfrom, yfrom, xto, yto,
                                         res, thickness, interpolation);
    xyarray = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(xyarray, 2*ndata);
    memcpy(xyarray->data, xy, 2*ndata*sizeof(gdouble));
    g_free(xy);
    return xyarray;
}

GwyDoubleArray*
gwy_data_line_part_fit_polynom_pygwy(GwyDataLine *data_line,
                                     gint n,
                                     gint from,
                                     gint to)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, n+1);
    gwy_data_line_part_fit_polynom(data_line, n, (gdouble*)coeffs->data, from, to);
    return coeffs;
}

GwyDoubleArray*
gwy_data_line_fit_polynom_pygwy(GwyDataLine *data_line,
                                gint n)
{
    GwyDoubleArray *coeffs = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(coeffs, n+1);
    gwy_data_line_fit_polynom(data_line, n, (gdouble*)coeffs->data);
    return coeffs;
}

void
gwy_data_line_part_subtract_polynom_pygwy(GwyDataLine *data_line,
                                          GwyDoubleArray *coeffs,
                                          gint from,
                                          gint to)
{
    gwy_data_line_part_subtract_polynom(data_line, coeffs->len, (gdouble*)coeffs->data, from, to);
    g_array_free(coeffs, TRUE);
}

void
gwy_data_line_subtract_polynom_pygwy(GwyDataLine *data_line,
                                     GwyDoubleArray *coeffs)
{
    gwy_data_line_subtract_polynom(data_line, coeffs->len, (gdouble*)coeffs->data);
    g_array_free(coeffs, TRUE);
}

/**
 * gwy_data_line_get_kth_peaks_pygwy:
 * @data_line: A data line.
 * @m: Number of sampling lengths the line is split into.
 * @rank: Rank of the peak to find.  One means the highest peak, three the third highers, etc.
 * @peaks: %TRUE for peaks, %FALSE for valleys.  If you pass %FALSE, swap the meanings of peaks and valleys in the
 *         description.  Valley depths are positive.
 * @average: Calculate the average of the first @rank peaks instead of the height of @rank-th peak.
 * @pthreshold: Peak height threshold.  Peaks must stick above this threshold.
 * @vthreshold: Valley depth threshold.  Valleys must fall below this threshold.  The depth is a positive value.
 *
 * Calculate k-th largers peaks or valleys in a data line split into given number of sampling lengths.
 *
 * This is a general function that can be used as the base for various standard roughness quantities such as Rp, Rpm,
 * Rv, Rvm or R3z.  It is assumed the line is already levelled, the form removed, etc.
 *
 * See gwy_data_line_count_peaks() for the description what is considered a peak.
 *
 * For larger thresholds and/or short lines some sampling lengths may not contain the requested number of peaks.  If
 * there are any peaks at all, the smallest peak height (even though it is not @rank-th) is used.  If there are no
 * peaks, a large negative value is stored in the corresponding @peakvalues item.
 *
 * Returns: List with @m items containing the peak heights.
 **/
GwyDoubleArray*
gwy_data_line_get_kth_peaks_pygwy(GwyDataLine *data_line,
                                  gint m, gint rank,
                                  gboolean peaks, gboolean average,
                                  gdouble pthreshold, gdouble vthreshold)
{
    GwyDoubleArray *peakvalues = g_array_new(FALSE, FALSE, sizeof(gdouble));
    if (m > 0) {
        g_array_set_size(peakvalues, m);
        gwy_data_line_get_kth_peaks(data_line, m, rank, peaks, average, pthreshold, vthreshold,
                                    &g_array_index(peakvalues, gdouble, 0));
    }
    else {
        g_warning("Negative number of sampling lengths.");
    }
    return peakvalues;
}

/**
 * gwy_key_from_name_pygwy:
 * @name: String representation of key.
 *
 * Convert string representation of key to numerical.
 *
 * Returns: Corresponding numerical key.
 **/
GQuark
gwy_key_from_name_pygwy(const gchar *name)
{
    return g_quark_from_string(name);
}

/**
 * gwy_name_from_key_pygwy:
 * @key: Numerical representation of a key.
 *
 * Convert numerical representation of key to string.
 *
 * The argument may only be identifier actually corresponding to string key, for instance obtained with
 * gwy_key_from_name().  Do not pass random integers to this function.
 *
 * Returns: Corresponding string key.
 **/
const gchar*
gwy_name_from_key_pygwy(GQuark key)
{
    return g_quark_to_string(key);
}

GwyIntArray*
gwy_container_keys_pygwy(GwyContainer *container)
{
    g_assert(sizeof(GQuark) == sizeof(gint));
    return create_int_array((gint*)gwy_container_keys(container), gwy_container_get_n_items(container), TRUE);
}

GwyConstStringArray*
gwy_container_keys_by_name_pygwy(GwyContainer *container)
{
    const gchar **keys = gwy_container_keys_by_name(container);
    return create_const_string_array((const gchar*const*)keys, gwy_container_get_n_items(container), TRUE);
}

GwyContainer*
gwy_container_duplicate_by_prefix_pygwy(GwyContainer *container,
                                        GwyStringArray *keys)
{
    GwyContainer *ret;
    ret = gwy_container_duplicate_by_prefixv(container, keys->len, (const gchar**)keys->data);
    free_string_array(keys);
    return ret;
}

GwyStringArray*
gwy_container_serialize_to_text_pygwy(GwyContainer *container)
{
    GPtrArray *strs = gwy_container_serialize_to_text(container);
    GwyStringArray *array = create_string_array((const gchar**)strs->pdata, strs->len, FALSE);
    g_ptr_array_free(strs, TRUE);
    return array;
}

static GwyIntArray*
create_id_array(gint *ids)
{
    guint n = 0;
    while (ids[n] != -1)
        n++;
    return create_int_array(ids, n, TRUE);
}

/**
 * gwy_app_data_browser_get_data_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all channels in a data container (presumably representing a file).
 *
 * Returns: List of channel ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_data_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_data_ids(container));
}

/**
 * gwy_app_data_browser_get_graph_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all graphs in a data container (presumably representing a file).
 *
 * Returns: List of graph ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_graph_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_graph_ids(container));
}

/**
 * gwy_app_data_browser_get_spectra_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all spectra in a data container (presumably representing a file).
 *
 * Returns: List of spectra ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_spectra_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_spectra_ids(container));
}

/**
 * gwy_app_data_browser_get_volume_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all volume data in a data container (presumably representing a file).
 *
 * Returns: List of volume data ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_volume_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_volume_ids(container));
}

/**
 * gwy_app_data_browser_get_xyz_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all XYZ data in a data container (presumably representing a file).
 *
 * Returns: List of XYZ data ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_xyz_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_xyz_ids(container));
}

/**
 * gwy_app_data_browser_get_curve_map_ids_pygwy:
 * @container: A data container.
 *
 * Gets the list of all curve map data in a data container (presumably representing a file).
 *
 * Returns: List of curve map data ids.
 **/
GwyIntArray*
gwy_app_data_browser_get_curve_map_ids_pygwy(GwyContainer *container)
{
    return create_id_array(gwy_app_data_browser_get_curve_map_ids(container));
}

GwyIntArray*
gwy_app_data_browser_find_data_by_title_pygwy(GwyContainer *data,
                                              const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_data_by_title(data, titleglob));
}

GwyIntArray*
gwy_app_data_browser_find_graphs_by_title_pygwy(GwyContainer *data,
                                                const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_graphs_by_title(data, titleglob));
}

GwyIntArray*
gwy_app_data_browser_find_spectra_by_title_pygwy(GwyContainer *data,
                                                 const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_spectra_by_title(data, titleglob));
}

GwyIntArray*
gwy_app_data_browser_find_volume_by_title_pygwy(GwyContainer *data,
                                                const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_volume_by_title(data, titleglob));
}

GwyIntArray*
gwy_app_data_browser_find_xyz_by_title_pygwy(GwyContainer *data,
                                             const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_xyz_by_title(data, titleglob));
}

GwyIntArray*
gwy_app_data_browser_find_curve_map_by_title_pygwy(GwyContainer *data,
                                                   const gchar *titleglob)
{
    return create_id_array(gwy_app_data_browser_find_curve_map_by_title(data, titleglob));
}

/**
 * gwy_data_field_number_grains_pygwy:
 * @mask_field: A data field representing a mask.
 *
 * Constructs an array with grain numbers from a mask data field.
 *
 * Returns: A list of integers, containing 0 outside grains and the grain number inside a grain.
 **/
GwyIntArray*
gwy_data_field_number_grains_pygwy(GwyDataField *mask_field)
{
    gint xres = gwy_data_field_get_xres(mask_field);
    gint yres = gwy_data_field_get_yres(mask_field);
    GwyIntArray *grains = g_array_new(FALSE, TRUE, sizeof(gint));
    g_array_set_size(grains, xres*yres);
    gwy_data_field_number_grains(mask_field, (gint*)grains->data);
    return grains;
}

/**
 * gwy_data_field_number_grains_periodic_pygwy:
 * @mask_field: A data field representing a mask.
 *
 * Constructs an array with grain numbers from a mask data field treated as periodic.
 *
 * Returns: A list of integers, containing 0 outside grains and the grain number inside a grain.
 **/
GwyIntArray*
gwy_data_field_number_grains_periodic_pygwy(GwyDataField *mask_field)
{
    gint xres = gwy_data_field_get_xres(mask_field);
    gint yres = gwy_data_field_get_yres(mask_field);
    GwyIntArray *grains = g_array_new(FALSE, TRUE, sizeof(gint));
    g_array_set_size(grains, xres*yres);
    gwy_data_field_number_grains_periodic(mask_field, (gint*)grains->data);
    return grains;
}

static gint
find_ngrains(const GwyIntArray *grains)
{
    gint ngrains = 0;
    const gint *g = (const gint*)grains->data;
    guint i, len = grains->len;

    for (i = 0; i < len; i++) {
        if (g[i] > ngrains)
            ngrains = g[i];
    }
    return ngrains;
}

/**
 * gwy_data_field_get_grain_bounding_boxes_pygwy:
 * @data_field: A data field representing a mask.
 * @grains: Array of grain numbers.
 *
 * Finds bounding boxes of all grains in a mask data field.
 *
 * The array @grains must have the same number of elements as @mask_field. Normally it is obtained from a function
 * such as gwy_data_field_number_grains().
 *
 * Returns: An array of quadruples of integers, each representing the bounding box of the corresponding grain (the
 *          zeroth quadrupe does not correspond to any as grain numbers start from 1).
 **/
GwyArrayFuncStatus
gwy_data_field_get_grain_bounding_boxes_pygwy(GwyDataField *data_field,
                                              GwyIntArray *grains,
                                              GwyIntArrayOutArg bboxes)
{
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    gboolean ok = (grains->len == xres*yres);
    if (ok) {
        guint ngrains = find_ngrains(grains);
        g_array_set_size(bboxes, 4*(ngrains+1));
        gwy_data_field_get_grain_bounding_boxes(data_field, ngrains, (gint*)grains->data, (gint*)bboxes->data);
    }
    else
        g_array_free(bboxes, TRUE);
    g_array_free(grains, TRUE);
    return ok;
}

/**
 * gwy_data_field_get_grain_bounding_boxes_periodic_pygwy:
 * @data_field: A data field representing a mask.
 * @grains: Array of grain numbers.
 *
 * Finds bounding boxes of all grains in a mask data field, assuming periodic boundary condition.
 *
 * The array @grains must have the same number of elements as @mask_field. Normally it is obtained from a function
 * such as gwy_data_field_number_grains().
 *
 * Returns: An array of quadruples of integers, each representing the bounding box of the corresponding grain (the
 *          zeroth quadruple does not correspond to any as grain numbers start from 1).
 **/
GwyArrayFuncStatus
gwy_data_field_get_grain_bounding_boxes_periodic_pygwy(GwyDataField *data_field,
                                                       GwyIntArray *grains,
                                                       GwyIntArrayOutArg bboxes)
{
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    gboolean ok = (grains->len == xres*yres);
    if (ok) {
        guint ngrains = find_ngrains(grains);
        g_array_set_size(bboxes, 4*(ngrains+1));
        gwy_data_field_get_grain_bounding_boxes_periodic(data_field, ngrains, (gint*)grains->data, (gint*)bboxes->data);
    }
    else
        g_array_free(bboxes, TRUE);
    g_array_free(grains, TRUE);
    return ok;
}

/**
 * gwy_data_field_get_grain_inscribed_boxes_pygwy:
 * @data_field: A data field representing a mask.
 * @grains: Array of grain numbers.
 *
 * Finds maximum-area inscribed boxes of all grains in a mask data field.
 *
 * The array @grains must have the same number of elements as @mask_field. Normally it is obtained from a function
 * such as gwy_data_field_number_grains().
 *
 * Returns: An array of quadruples of integers, each representing the inscribed box of the corresponding grain (the
 *          zeroth quadruple does not correspond to any as grain numbers start from 1).
 **/
GwyArrayFuncStatus
gwy_data_field_get_grain_inscribed_boxes_pygwy(GwyDataField *data_field,
                                               GwyIntArray *grains,
                                               GwyIntArrayOutArg iboxes)
{
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    gboolean ok = (grains->len == xres*yres);
    if (ok) {
        guint ngrains = find_ngrains(grains);
        g_array_set_size(iboxes, 4*(ngrains+1));
        gwy_data_field_get_grain_inscribed_boxes(data_field, ngrains, (gint*)grains->data, (gint*)iboxes->data);
    }
    else
        g_array_free(iboxes, TRUE);
    g_array_free(grains, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_data_field_get_grain_sizes_pygwy(GwyDataField *data_field,
                                     GwyIntArray *grains,
                                     GwyIntArrayOutArg sizes)
{
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    gboolean ok = (grains->len == xres*yres);
    if (ok) {
        guint ngrains = find_ngrains(grains);
        g_array_set_size(sizes, ngrains+1);
        gwy_data_field_get_grain_sizes(data_field, ngrains, (gint*)grains->data, (gint*)sizes->data);
    }
    else
        g_array_free(sizes, TRUE);
    g_array_free(grains, TRUE);
    return ok;
}

/**
 * gwy_data_field_grains_get_values_pygwy:
 * @data_field: A data field representing a surface.
 * @grains: Array of grain numbers.
 * @quantity: The quantity to calculate, identified by GwyGrainQuantity.
 *
 * Finds a speficied quantity for all grains in a data field.
 *
 * The array @grains must have the same number of elements as @data_field. Normally it is obtained from a function
 * such as gwy_data_field_number_grains() for the corresponding mask.
 *
 * Returns: An array of floating point values, each representing the requested quantity for corresponding grain (the
 *          zeroth item does not correspond to any as grain numbers start from 1).
 **/
GwyArrayFuncStatus
gwy_data_field_grains_get_values_pygwy(GwyDataField *data_field,
                                       GwyIntArray *grains,
                                       GwyGrainQuantity quantity,
                                       GwyDoubleArrayOutArg values)
{
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    gboolean ok = (grains->len == xres*yres);
    if (ok) {
        guint ngrains = find_ngrains(grains);
        g_array_set_size(values, ngrains+1);
        gwy_data_field_grains_get_values(data_field, (gdouble*)values->data, ngrains, (gint*)grains->data, quantity);
    }
    else
        g_array_free(values, TRUE);
    g_array_free(grains, TRUE);
    return ok;
}

/**
 * gwy_data_field_grains_get_distribution_pygwy:
 * @data_field: A data field representing a surface.
 * @grain_field: A data field representing the mask.  It must have the same dimensions as the data field.
 * @grains: Array of grain numbers.
 * @quantity: The quantity to calculate, identified by GwyGrainQuantity.
 * @nstats: The number of bins in the histogram.  Pass a non-positive value to determine the number of bins
 *          automatically.
 *
 * Calculates the distribution of a speficied grain quantity.
 *
 * The array @grains must have the same number of elements as @data_field. Normally it is obtained from a function
 * such as gwy_data_field_number_grains() for the corresponding mask.
 *
 * Returns: The distribution as a data line.
 **/
GwyDataLine*
gwy_data_field_grains_get_distribution_pygwy(GwyDataField *data_field,
                                             GwyDataField *grain_field,
                                             GwyIntArray *grains,
                                             GwyGrainQuantity quantity,
                                             gint nstats)
{
    GwyDataLine *dist;
    guint xres = gwy_data_field_get_xres(data_field);
    guint yres = gwy_data_field_get_yres(data_field);
    guint ngrains;

    g_return_val_if_fail(grains->len == xres*yres, NULL);
    g_return_val_if_fail(grain_field->xres == xres, NULL);
    g_return_val_if_fail(grain_field->yres == yres, NULL);
    ngrains = find_ngrains(grains);
    dist = gwy_data_field_grains_get_distribution(data_field, grain_field,
                                                  NULL, ngrains, (gint*)grains->data, quantity, nstats);
    g_array_free(grains, TRUE);
    return dist;
}

GwyDataField*
gwy_tip_dilation_pygwy(GwyDataField *tip, GwyDataField *surface)
{
    return gwy_tip_dilation(tip, surface, gwy_data_field_new_alike(surface, FALSE), NULL, NULL);
}

GwyDataField*
gwy_tip_erosion_pygwy(GwyDataField *tip, GwyDataField *surface)
{
    return gwy_tip_erosion(tip, surface, gwy_data_field_new_alike(surface, FALSE), NULL, NULL);
}

GwyDataField*
gwy_tip_cmap_pygwy(GwyDataField *tip, GwyDataField *surface)
{
    return gwy_tip_cmap(tip, surface, gwy_data_field_new_alike(surface, FALSE), NULL, NULL);
}

GwyDataField*
gwy_data_field_create_full_mask_pygwy(GwyDataField *d)
{
    GwyDataField *m;
    m = gwy_data_field_new_alike(d, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(m), NULL);
    gwy_data_field_add(m, 1.0);
    return m;
}

gboolean
gwy_get_grain_quantity_needs_same_units_pygwy(GwyGrainQuantity quantity)
{
    return gwy_grain_quantity_needs_same_units(quantity);
}

GwySIUnit*
gwy_construct_grain_quantity_units_pygwy(GwyGrainQuantity quantity,
                                         GwySIUnit *siunitxy,
                                         GwySIUnit *siunitz)
{
    return gwy_grain_quantity_get_units(quantity, siunitxy, siunitz, NULL);
}

void
gwy_surface_set_pygwy(GwySurface *surface, guint pos, const GwyXYZ *point)
{
    GwyXYZ pt = *point;
    gwy_surface_set(surface, pos, pt);
}

GwyArrayFuncStatus
gwy_data_field_average_xyz_pygwy(GwyDataField *dfield,
                                 GwyDataField *densitymap,
                                 GwyDoubleArray *points)
{
    guint n = points->len;
    gboolean ok = (n % 3 == 0);
    if (ok)
        gwy_data_field_average_xyz(dfield, densitymap, (GwyXYZ*)points->data, n);
    g_array_free(points, TRUE);
    return ok;
}

GwyIntArray*
gwy_spectra_find_nearest_pygwy(GwySpectra *spectra,
                               gdouble x, gdouble y, guint n)
{
    GwyIntArray *array = g_array_new(FALSE, FALSE, sizeof(guint));
    g_array_set_size(array, MIN(n, gwy_spectra_get_n_spectra(spectra)));
    gwy_spectra_find_nearest(spectra, x, y, n, (guint*)array->data);
    return array;
}

GwyDoubleArray*
gwy_graph_curve_model_get_xdata_pygwy(GwyGraphCurveModel *gcmodel)
{
    return create_double_array(gwy_graph_curve_model_get_xdata(gcmodel), gwy_graph_curve_model_get_ndata(gcmodel),
                               FALSE);
}

GwyDoubleArray*
gwy_graph_curve_model_get_ydata_pygwy(GwyGraphCurveModel *gcmodel)
{
    return create_double_array(gwy_graph_curve_model_get_ydata(gcmodel), gwy_graph_curve_model_get_ndata(gcmodel),
                               FALSE);
}

GwyArrayFuncStatus
gwy_graph_curve_model_set_data_pygwy(GwyGraphCurveModel *gcmodel,
                                     GwyDoubleArray *xdata,
                                     GwyDoubleArray *ydata)
{
    gboolean ok = (ydata->len == xdata->len);
    if (ok)
        gwy_graph_curve_model_set_data(gcmodel, (gdouble*)xdata->data, (gdouble*)ydata->data, xdata->len);
    g_array_free(xdata, TRUE);
    g_array_free(ydata, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_graph_curve_model_set_data_interleaved_pygwy(GwyGraphCurveModel *gcmodel,
                                                 GwyDoubleArray *xydata)
{
    gboolean ok = !(xydata->len % 2);
    if (ok)
        gwy_graph_curve_model_set_data_interleaved(gcmodel, (gdouble*)xydata->data, xydata->len/2);
    g_array_free(xydata, TRUE);
    return ok;
}

void
gwy_graph_area_set_x_grid_data_pygwy(GwyGraphArea *area,
                                     GwyDoubleArray *grid_data)
{
    gwy_graph_area_set_x_grid_data(area, grid_data->len, (gdouble*)grid_data->data);
    g_array_free(grid_data, TRUE);
}

void
gwy_graph_area_set_y_grid_data_pygwy(GwyGraphArea *area,
                                     GwyDoubleArray *grid_data)
{
    gwy_graph_area_set_y_grid_data(area, grid_data->len, (gdouble*)grid_data->data);
    g_array_free(grid_data, TRUE);
}

GwyDoubleArray*
gwy_graph_area_get_x_grid_data_pygwy(GwyGraphArea *area)
{
    guint n;
    const gdouble *xdata = gwy_graph_area_get_x_grid_data(area, &n);
    return create_double_array(xdata, n, FALSE);
}

GwyDoubleArray*
gwy_graph_area_get_y_grid_data_pygwy(GwyGraphArea *area)
{
    guint n;
    const gdouble *ydata = gwy_graph_area_get_y_grid_data(area, &n);
    return create_double_array(ydata, n, FALSE);
}

GwyDoubleArray*
gwy_draw_data_field_map_adaptive_pygwy(GwyDataField *data_field,
                                       GwyDoubleArray *z)
{
    GwyDoubleArray *mapped = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(mapped, z->len);
    gwy_draw_data_field_map_adaptive(data_field, (gdouble*)z->data, (gdouble*)mapped->data, z->len);
    g_array_free(z, TRUE);
    return mapped;
}

GwyDoubleArray*
gwy_data_view_get_metric_pygwy(GwyDataView *data_view)
{
    GwyDoubleArray *metric = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(metric, 4);
    gwy_data_view_get_metric(data_view, (gdouble*)metric->data);
    return metric;
}

GwyDoubleArray*
gwy_axis_get_major_ticks_pygwy(GwyAxis *axis)
{
    guint n;
    const gdouble *ticks = gwy_axis_get_major_ticks(axis, &n);
    return create_double_array(ticks, n, FALSE);
}

gulong
gwy_undo_qcheckpoint_pygwy(GwyContainer *container, GwyIntArray *keys)
{
    gulong id = 0;
    g_assert(sizeof(GQuark) == sizeof(gint));
    if (keys->len)
        id = gwy_undo_qcheckpointv(container, keys->len, (GQuark*)keys->data);
    g_array_free(keys, TRUE);
    return id;
}

gulong
gwy_undo_checkpoint_pygwy(GwyContainer *container, GwyStringArray *keys)
{
    gulong id = 0;
    if (keys->len)
        id = gwy_undo_checkpointv(container, keys->len, (const gchar**)keys->data);
    free_string_array(keys);
    return id;
}

gulong
gwy_app_undo_qcheckpoint_pygwy(GwyContainer *container, GwyIntArray *keys)
{
    gulong id = 0;
    g_assert(sizeof(GQuark) == sizeof(gint));
    if (keys->len)
        id = gwy_app_undo_qcheckpointv(container, keys->len, (GQuark*)keys->data);
    g_array_free(keys, TRUE);
    return id;
}

gulong
gwy_app_undo_checkpoint_pygwy(GwyContainer *container, GwyStringArray *keys)
{
    gulong id = 0;
    if (keys->len)
        id = gwy_app_undo_checkpointv(container, keys->len, (const gchar**)keys->data);
    free_string_array(keys);
    return id;
}

GObject*
gwy_inventory_get_item_pygwy(GwyInventory *inventory,
                             const gchar *name)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to get object from non-object Inventory");
        return NULL;
    }
    return (GObject*)gwy_inventory_get_item(inventory, name);
}

GObject*
gwy_inventory_get_item_or_default_pygwy(GwyInventory *inventory,
                                        const gchar *name)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to get object from non-object Inventory");
        return NULL;
    }
    return (GObject*)gwy_inventory_get_item_or_default(inventory, name);
}

GObject*
gwy_inventory_get_nth_item_pygwy(GwyInventory *inventory,
                                 guint n)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to get object from non-object Inventory");
        return NULL;
    }
    return (GObject*)gwy_inventory_get_nth_item(inventory, n);
}

GObject*
gwy_inventory_get_default_item_pygwy(GwyInventory *inventory)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to get object from non-object Inventory");
        return NULL;
    }
    return (GObject*)gwy_inventory_get_default_item(inventory);
}

void
gwy_inventory_insert_item_pygwy(GwyInventory *inventory,
                                GObject *object)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(G_OBJECT_TYPE(object), type->type)) {
        g_warning("Attempting to insert object to wrong-typed Inventory");
        g_object_ref(object);
        return;
    }
    gwy_inventory_insert_item(inventory, object);
}

void
gwy_inventory_insert_nth_item_pygwy(GwyInventory *inventory,
                                    GObject *object,
                                    guint n)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(G_OBJECT_TYPE(object), type->type)) {
        g_warning("Attempting to insert object to wrong-typed Inventory");
        g_object_ref(object);
        return;
    }
    gwy_inventory_insert_nth_item(inventory, object, n);
}

void
gwy_inventory_rename_item_pygwy(GwyInventory *inventory,
                                const gchar *name,
                                const gchar *newname)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to rename object in non-object Inventory");
        return;
    }
    if (!type->rename) {
        g_warning("Attempting to rename object in Inventory that does not support renaming.");
        return;
    }
    gwy_inventory_rename_item(inventory, name, newname);
}

GObject*
gwy_inventory_new_item_pygwy(GwyInventory *inventory,
                             const gchar *name,
                             const gchar *newname)
{
    const GwyInventoryItemType *type = gwy_inventory_get_item_type(inventory);
    if (!type->type || !g_type_is_a(type->type, G_TYPE_OBJECT)) {
        g_warning("Attempting to create object in non-object Inventory");
        return NULL;
    }
    if (!type->rename || !type->copy) {
        g_warning("Attempting to rename object in Inventory that does not support copying.");
        return NULL;
    }
    return (GObject*)gwy_inventory_new_item(inventory, name, newname);
}

GwyArrayFuncStatus
gwy_cdline_fit_pygwy(GwyCDLine *cdline,
                     GwyDoubleArray *x, GwyDoubleArray *y,
                     GwyDoubleArrayOutArg params, GwyDoubleArrayOutArg err)
{
    gboolean ok = (x->len == y->len);
    if (ok) {
        guint np = gwy_cdline_get_nparams(cdline);
        g_array_set_size(params, np);
        g_array_set_size(err, np);
        gwy_cdline_fit(cdline, x->len, (gdouble*)x->data, (gdouble*)y->data,
                       np, (gdouble*)params->data, (gdouble*)err->data,
                       NULL, NULL);
    }
    else {
        g_array_free(params, TRUE);
        g_array_free(err, TRUE);
    }
    g_array_free(x, TRUE);
    g_array_free(y, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_cdline_get_value_pygwy(GwyCDLine *cdline,
                           gdouble x, GwyDoubleArray *params,
                           gdouble *value, gboolean *fres)
{
    gboolean ok = (params->len == gwy_cdline_get_nparams(cdline));
    if (ok)
        *value = gwy_cdline_get_value(cdline, x, (gdouble*)params->data, fres);
    g_array_free(params, TRUE);
    return ok;
}

GwyArrayFuncStatus
gwy_peaks_analyze_pygwy(GwyPeaks *peaks,
                        GwyDoubleArray *xdata,
                        GwyDoubleArray *ydata,
                        guint maxpeaks,
                        guint *npeaks)
{
    gboolean ok = (ydata->len == xdata->len);
    if (ok)
        *npeaks = gwy_peaks_analyze(peaks, (gdouble*)xdata->data, (gdouble*)ydata->data, xdata->len, maxpeaks);
    g_array_free(xdata, TRUE);
    g_array_free(ydata, TRUE);
    return ok;
}

GwyDoubleArray*
gwy_peaks_get_quantity_pygwy(GwyPeaks *peaks,
                             GwyPeakQuantity quantity)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(array, gwy_peaks_n_peaks(peaks));
    gwy_peaks_get_quantity(peaks, quantity, (gdouble*)array->data);
    return array;
}

GwyIntArray*
gwy_tip_model_preset_get_params_pygwy(const GwyTipModelPreset *preset)
{
    GwyIntArray *array = g_array_new(FALSE, FALSE, sizeof(guint));
    guint i, n = gwy_tip_model_get_preset_nparams(preset);
    const GwyTipParamType *paramtypes = gwy_tip_model_get_preset_params(preset);
    g_array_set_size(array, n);
    for (i = 0; i < n; i++)
        g_array_index(array, guint, i) = paramtypes[i];
    return array;
}

GwyArrayFuncStatus
gwy_tip_model_preset_create_pygwy(const GwyTipModelPreset *preset,
                                  GwyDataField *tip,
                                  GwyDoubleArray *params)
{
    gboolean ok = (params->len == gwy_tip_model_get_preset_nparams(preset));
    if (ok)
        gwy_tip_model_preset_create(preset, tip, (const gdouble*)params->data);
    return ok;
}

GwyArrayFuncStatus
gwy_tip_model_preset_create_for_zrange_pygwy(const GwyTipModelPreset *preset,
                                             GwyDataField *tip,
                                             gdouble zrange,
                                             gboolean square,
                                             GwyDoubleArray *params)
{
    gboolean ok = (params->len == gwy_tip_model_get_preset_nparams(preset));
    if (ok)
        gwy_tip_model_preset_create_for_zrange(preset, tip, zrange, square, (const gdouble*)params->data);
    return ok;
}

GwySpline*
gwy_spline_new_from_points_pygwy(GwyDoubleArray *xy)
{
    return gwy_spline_new_from_points((GwyXY*)xy->data, xy->len/2);
}

GwyDoubleArray*
gwy_spline_get_points_pygwy(GwySpline *spline)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    guint n = gwy_spline_get_npoints(spline);
    g_array_set_size(array, 2*n);
    gwy_assign((gdouble*)array->data, (const gdouble*)gwy_spline_get_points(spline), 2*n);
    return array;
}

GwyDoubleArray*
gwy_spline_get_tangents_pygwy(GwySpline *spline)
{
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    guint n = gwy_spline_get_npoints(spline);
    g_array_set_size(array, 2*n);
    gwy_assign((gdouble*)array->data, (const gdouble*)gwy_spline_get_tangents(spline), 2*n);
    return array;
}

GwyDoubleArray*
gwy_spline_sample_naturally_pygwy(GwySpline *spline)
{
    guint n;
    const GwyXY *xy = gwy_spline_sample_naturally(spline, &n);
    GwyDoubleArray *array = g_array_new(FALSE, FALSE, sizeof(gdouble));
    g_array_set_size(array, 2*n);
    gwy_assign((gdouble*)array->data, (const gdouble*)xy, 2*n);
    return array;
}

gdouble
gwy_spline_sample_uniformly_pygwy(GwySpline *spline,
                                  GwyDoubleArrayOutArg xy,
                                  GwyDoubleArrayOutArg t,
                                  guint n)
{
    g_array_set_size(xy, 2*n);
    g_array_set_size(t, 2*n);
    return gwy_spline_sample_uniformly(spline, (GwyXY*)xy->data, (GwyXY*)t->data, n);
}

GwyDoubleArray*
gwy_marker_box_get_markers_pygwy(GwyMarkerBox *mbox)
{
    return create_double_array(gwy_marker_box_get_markers(mbox), gwy_marker_box_get_nmarkers(mbox), FALSE);
}

GtkWidget*
gwy_combo_box_metric_unit_new_pygwy(gint from,
                                    gint to,
                                    GwySIUnit *unit,
                                    gint active)
{
    return gwy_combo_box_metric_unit_new(NULL, NULL, from, to, unit, active);
}

GtkWidget*
gwy_combo_box_graph_curve_new_pygwy(GwyGraphModel *gmodel,
                                    gint current)
{
    return gwy_combo_box_graph_curve_new(NULL, NULL, gmodel, current);
}

GtkWidget*
gwy_menu_gradient_pygwy(void)
{
    return gwy_menu_gradient(NULL, NULL);
}

GtkWidget*
gwy_gradient_selection_new_pygwy(const gchar *active)
{
    return gwy_gradient_selection_new(NULL, NULL, active);
}

GtkWidget*
gwy_gradient_tree_view_new_pygwy(const gchar *active)
{
    return gwy_gradient_tree_view_new(NULL, NULL, active);
}

GtkWidget*
gwy_menu_gl_material_pygwy(void)
{
    return gwy_menu_gl_material(NULL, NULL);
}

GtkWidget*
gwy_gl_material_selection_new_pygwy(const gchar *active)
{
    return gwy_gl_material_selection_new(NULL, NULL, active);
}

GtkWidget*
gwy_gl_material_tree_view_new_pygwy(const gchar *active)
{
    return gwy_gl_material_tree_view_new(NULL, NULL, active);
}

void
gwy_marker_box_set_markers_pygwy(GwyMarkerBox *mbox,
                                 GwyDoubleArray *markers)
{
    gwy_marker_box_set_markers(mbox, markers->len, (gdouble*)markers->data);
    g_array_free(markers, TRUE);
}

void
gwy_app_sync_data_items_pygwy(GwyContainer *source, GwyContainer *dest,
                              gint from_id, gint to_id, gboolean delete_too,
                              GwyIntArray *items)
{
    g_assert(sizeof(GwyDataItem) == sizeof(gint));
    gwy_app_sync_data_itemsv(source, dest, from_id, to_id, delete_too, (GwyDataItem*)items->data, items->len);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
