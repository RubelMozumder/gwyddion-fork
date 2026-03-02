/*
 *  $Id: correct.c 25308 2023-04-21 13:16:28Z yeti-dn $
 *  Copyright (C) 2004-2018 David Necas (Yeti), Petr Klapetek.
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
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>
#include <libprocess/elliptic.h>
#include <libprocess/linestats.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/filters.h>
#include <libprocess/correct.h>
#include <libprocess/interpolation.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

typedef struct {
    gdouble max;
    gdouble x;
    gdouble y;
    gdouble d;
    gdouble q;
    guint basecount;
} LatticeMaximumInfo;

static void    gwy_data_field_distort_internal(GwyDataField *source,
                                               GwyDataField *dest,
                                               GwyInterpolationType interp,
                                               GwyExteriorType exterior,
                                               gdouble fill_value,
                                               const GwyXY *coords,
                                               GwyCoordTransform2DFunc invtrans,
                                               gpointer user_data);
static gdouble unrotate_refine_correction     (GwyDataLine *derdist,
                                               guint m,
                                               gdouble phi);
static void    compute_fourier_coeffs         (gint nder,
                                               const gdouble *der,
                                               guint symmetry,
                                               gdouble *st,
                                               gdouble *ct);

/**
 * gwy_data_field_mask_outliers:
 * @data_field: A data field.
 * @mask_field: A data field to be filled with mask.
 * @thresh: Threshold value.
 *
 * Creates mask of data that are above or below @thresh*sigma from average height.
 *
 * Sigma denotes root-mean square deviation of heights. This criterium corresponds to the usual Gaussian distribution
 * outliers detection if @thresh is 3.
 **/
void
gwy_data_field_mask_outliers(GwyDataField *data_field,
                             GwyDataField *mask_field,
                             gdouble thresh)
{
    gwy_data_field_mask_outliers2(data_field, mask_field, thresh, thresh);
}

/**
 * gwy_data_field_mask_outliers2:
 * @data_field: A data field.
 * @mask_field: A data field to be filled with mask.
 * @thresh_low: Lower threshold value.
 * @thresh_high: Upper threshold value.
 *
 * Creates mask of data that are above or below multiples of rms from average height.
 *
 * Data that are below @mean-@thresh_low*@sigma or above @mean+@thresh_high*@sigma are marked as outliers, where
 * @sigma denotes the root-mean square deviation of heights.
 *
 * Since: 2.26
 **/
void
gwy_data_field_mask_outliers2(GwyDataField *data_field,
                              GwyDataField *mask_field,
                              gdouble thresh_low,
                              gdouble thresh_high)
{
     gdouble avg, val;
     gdouble criterium_low, criterium_high;
     gint i;

     avg = gwy_data_field_get_avg(data_field);
     criterium_low = -gwy_data_field_get_rms(data_field) * thresh_low;
     criterium_high = gwy_data_field_get_rms(data_field) * thresh_high;

     for (i = 0; i < (data_field->xres * data_field->yres); i++) {
         val = data_field->data[i] - avg;
         mask_field->data[i] = (val < criterium_low || val > criterium_high);
     }

     gwy_data_field_invalidate(mask_field);
}

/**
 * gwy_data_field_correct_average:
 * @data_field: A data field.
 * @mask_field: Mask of places to be corrected.
 *
 * Fills data under mask with the average value.
 *
 * This function simply puts average value of all the @data_field values (both masked and unmasked) into points in
 * @data_field lying under points where @mask_field values are nonzero.
 *
 * In most cases you probably want to use gwy_data_field_correct_average_unmasked() instead.
 **/
void
gwy_data_field_correct_average(GwyDataField *data_field,
                               GwyDataField *mask_field)
{
    gdouble avg;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_DATA_FIELD(mask_field));
    if (!_gwy_data_field_check_mask(data_field, &mask_field, NULL))
        return;
    avg = gwy_data_field_get_avg(data_field);
    if (gwy_isnan(avg) || gwy_isinf(avg)) {
        gwy_data_field_clear(data_field);
        return;
    }
    gwy_data_field_area_fill_mask(data_field, mask_field, GWY_MASK_INCLUDE,
                                  0, 0, data_field->xres, data_field->yres, avg);
}

/**
 * gwy_data_field_correct_average_unmasked:
 * @data_field: A data field.
 * @mask_field: Mask of places to be corrected.
 *
 * Fills data under mask with the average value of unmasked data.
 *
 * This function calculates the average value of all unmasked pixels in @data_field and then fills all the masked
 * pixels with this average value. It is useful as the first rough step of correction of data under the mask.
 *
 * If all data are masked the field is filled with zeroes.
 *
 * Since: 2.44
 **/
void
gwy_data_field_correct_average_unmasked(GwyDataField *data_field,
                                        GwyDataField *mask_field)
{
    gdouble avg;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    if (!_gwy_data_field_check_mask(data_field, &mask_field, NULL))
        return;
    avg = gwy_data_field_area_get_avg_mask(data_field, mask_field, GWY_MASK_INCLUDE,
                                           0, 0, data_field->xres, data_field->yres);
    if (gwy_isnan(avg) || gwy_isinf(avg)) {
        gwy_data_field_clear(data_field);
        return;
    }
    gwy_data_field_area_fill_mask(data_field, mask_field, GWY_MASK_INCLUDE,
                                  0, 0, data_field->xres, data_field->yres, avg);
}

/**
 * gwy_data_field_unrotate_find_corrections:
 * @derdist: Angular derivation distribution (normally obrained from gwy_data_field_slope_distribution()).
 * @correction: Corrections for particular symmetry types will be stored here (indexed by GwyPlaneSymmetry).
 *              @correction[0] contains the most probable correction.  All angles are in radians.
 *
 * Finds rotation corrections.
 *
 * Rotation correction is computed for for all symmetry types.
 * In addition an estimate is made about the prevalent one.
 *
 * Returns: The estimate type of prevalent symmetry.
 **/
GwyPlaneSymmetry
gwy_data_field_unrotate_find_corrections(GwyDataLine *derdist,
                                         gdouble *correction)
{
    static const guint symm[] = { 2, 3, 4, 6 };
    GwyPlaneSymmetry guess, t;
    gint nder;
    guint j, m;
    gdouble avg, max, total, phi;
    const gdouble *der;
    gdouble sint[G_N_ELEMENTS(symm)], cost[G_N_ELEMENTS(symm)];

    nder = gwy_data_line_get_res(derdist);
    der = gwy_data_line_get_data_const(derdist);
    avg = gwy_data_line_get_avg(derdist);
    gwy_data_line_add(derdist, -avg);

    guess = GWY_SYMMETRY_AUTO;
    max = -G_MAXDOUBLE;
    for (j = 0; j < G_N_ELEMENTS(symm); j++) {
        m = symm[j];
        compute_fourier_coeffs(nder, der, m, sint+j, cost+j);
        phi = atan2(-sint[j], cost[j]);
        total = sqrt(sint[j]*sint[j] + cost[j]*cost[j]);

        gwy_debug("sc%d = (%f, %f), total%d = (%f, %f)", m, sint[j], cost[j], m, total, 180.0/G_PI*phi);

        phi /= 2*G_PI*m;
        phi = unrotate_refine_correction(derdist, m, phi);
        t = sizeof("Die, die GCC warning!");
        /*
         *             range from             smallest possible
         *  symmetry   compute_correction()   range                ratio
         *    m        -1/2m .. 1/2m
         *
         *    2        -1/4  .. 1/4           -1/8  .. 1/8         1/2
         *    3        -1/6  .. 1/6           -1/12 .. 1/12        1/2
         *    4        -1/8  .. 1/8           -1/8  .. 1/8 (*)     1
         *    6        -1/12 .. 1/12          -1/12 .. 1/12        1
         *
         *  (*) not counting rhombic
         */
        if (m == 2) {
            t = GWY_SYMMETRY_PARALLEL;
            /* align with any x or y */
            if (phi >= 0.25/m)
                phi -= 0.5/m;
            else if (phi <= -0.25/m)
                phi += 0.5/m;
            correction[t] = phi;
            total /= 1.25;
        }
        else if (m == 3) {
            t = GWY_SYMMETRY_TRIANGULAR;
            /* align with any x or y */
            if (phi >= 0.125/m)
                phi -= 0.25/m;
            else if (phi <= -0.125/m)
                phi += 0.25/m;
            correction[t] = phi;
        }
        else if (m == 4) {
            t = GWY_SYMMETRY_SQUARE;
            correction[t] = phi;
            /* decide square/rhombic */
            phi += 0.5/m;
            if (phi > 0.5/m)
                phi -= 1.0/m;
            t = GWY_SYMMETRY_RHOMBIC;
            correction[t] = phi;
            if (fabs(phi) > fabs(correction[GWY_SYMMETRY_SQUARE]))
                t = GWY_SYMMETRY_SQUARE;
            total /= 1.4;
        }
        else if (m == 6) {
            t = GWY_SYMMETRY_HEXAGONAL;
            correction[t] = phi;
        }
        else {
            g_assert_not_reached();
        }

        if (total > max) {
            max = total;
            guess = t;
        }
    }
    gwy_data_line_add(derdist, avg);
    g_assert(guess != GWY_SYMMETRY_AUTO);
    gwy_debug("SELECTED: %d", guess);
    correction[GWY_SYMMETRY_AUTO] = correction[guess];

    for (j = 0; j < GWY_SYMMETRY_LAST; j++) {
        gwy_debug("FINAL %d: (%f, %f)", j, correction[j], 360*correction[j]);
        correction[j] *= 2.0*G_PI;
    }

    return guess;
}

static void
compute_fourier_coeffs(gint nder, const gdouble *der,
                       guint symmetry,
                       gdouble *st, gdouble *ct)
{
    guint i;
    gdouble q, sint, cost;

    q = 2*G_PI/nder*symmetry;
    sint = cost = 0.0;
    for (i = 0; i < nder; i++) {
        sint += sin(q*(i + 0.5))*der[i];
        cost += cos(q*(i + 0.5))*der[i];
    }

    *st = sint;
    *ct = cost;
}

/**
 * unrotate_refine_correction:
 * @derdist: Angular derivation distribution (as in Slope dist. graph).
 * @m: Symmetry.
 * @phi: Initial correction guess (in the range 0..1!).
 *
 * Compute correction assuming symmetry @m and initial guess @phi.
 *
 * Returns: The correction (again in the range 0..1!).
 **/
static gdouble
unrotate_refine_correction(GwyDataLine *derdist,
                           guint m, gdouble phi)
{
    gdouble sum, wsum;
    const gdouble *der;
    guint i, j, nder;

    nder = gwy_data_line_get_res(derdist);
    der = gwy_data_line_get_data_const(derdist);

    phi -= floor(phi) + 1.0;
    sum = wsum = 0.0;
    for (j = 0; j < m; j++) {
        gdouble low = (j + 5.0/6.0)/m - phi;
        gdouble high = (j + 7.0/6.0)/m - phi;
        gdouble s, w;
        guint ilow, ihigh;

        ilow = (guint)floor(low*nder);
        ihigh = (guint)floor(high*nder);
        gwy_debug("[%u] peak %u low = %f, high = %f, %u, %u", m, j, low, high, ilow, ihigh);
        s = w = 0.0;
        for (i = ilow; i <= ihigh; i++) {
            s += (i + 0.5)*der[i % nder];
            w += der[i % nder];
        }

        s /= nder*w;
        gwy_debug("[%u] peak %u center: %f", m, j, 360*s);
        sum += (s - (gdouble)j/m)*w*w;
        wsum += w*w;
    }
    phi = sum/wsum;
    gwy_debug("[%u] FITTED phi = %f (%f)", m, phi, 360*phi);
    phi = fmod(phi + 1.0, 1.0/m);
    if (phi > 0.5/m)
        phi -= 1.0/m;
    gwy_debug("[%u] MINIMIZED phi = %f (%f)", m, phi, 360*phi);

    return phi;
}

/**
 * gwy_data_field_sample_distorted:
 * @source: Source data field.
 * @dest: Destination data field.
 * @coords: Array of @source coordinates with the same number of items as @dest, ordered as data field data. See
 *          gwy_data_field_distort() for coordinate convention discussion.
 * @interp: Interpolation type to use.
 * @exterior: Exterior pixels handling.
 * @fill_value: The value to use with @GWY_EXTERIOR_FIXED_VALUE.
 *
 * Resamples a data field in an arbitrarily distorted manner.
 *
 * Each item in @coords corresponds to one pixel in @dest and gives the coordinates in @source defining the value to
 * set in this pixel.
 *
 * The %GWY_EXTERIOR_LAPLACE exterior type cannot be used with this function.
 *
 * Since: 2.45
 **/
void
gwy_data_field_sample_distorted(GwyDataField *source,
                                GwyDataField *dest,
                                const GwyXY *coords,
                                GwyInterpolationType interp,
                                GwyExteriorType exterior,
                                gdouble fill_value)
{
    gwy_data_field_distort_internal(source, dest, interp, exterior, fill_value, coords, NULL, NULL);
}

/**
 * gwy_data_field_distort:
 * @source: Source data field.
 * @dest: Destination data field.
 * @invtrans: Inverse transform function, that is the transformation from new coordinates to old coordinates.   It
 *            gets (@j+0.5, @i+0.5), where @i and @j are the new row and column indices, passed as the input
 *            coordinates.  The output coordinates should follow the same convention.  Unless a special exterior
 *            handling is required, the transform function does not need to concern itself with coordinates being
 *            outside of the data.
 * @user_data: Pointer passed as @user_data to @invtrans.
 * @interp: Interpolation type to use.
 * @exterior: Exterior pixels handling.
 * @fill_value: The value to use with @GWY_EXTERIOR_FIXED_VALUE.
 *
 * Distorts a data field in the horizontal plane.
 *
 * Note the transform function @invtrans is the inverse transform, in other words it calculates the old coordinates
 * from the new coordinates (the transform would not be uniquely defined the other way round).
 *
 * The %GWY_EXTERIOR_LAPLACE exterior type cannot be used with this function.
 *
 * Since: 2.5
 **/
void
gwy_data_field_distort(GwyDataField *source,
                       GwyDataField *dest,
                       GwyCoordTransform2DFunc invtrans,
                       gpointer user_data,
                       GwyInterpolationType interp,
                       GwyExteriorType exterior,
                       gdouble fill_value)
{
    gwy_data_field_distort_internal(source, dest, interp, exterior, fill_value, NULL, invtrans, user_data);
}

static void
gwy_data_field_distort_internal(GwyDataField *source,
                                GwyDataField *dest,
                                GwyInterpolationType interp,
                                GwyExteriorType exterior,
                                gdouble fill_value,
                                const GwyXY *coords,
                                GwyCoordTransform2DFunc invtrans,
                                gpointer user_data)
{
    GwyDataField *coeffield;
    GwyXY *my_coords = NULL;
    gdouble *data;
    const gdouble *cdata;
    gint xres, yres, newxres, newyres;
    gint suplen, sf, st;

    g_return_if_fail(GWY_IS_DATA_FIELD(source));
    g_return_if_fail(GWY_IS_DATA_FIELD(dest));
    g_return_if_fail(coords || invtrans);
    g_return_if_fail(!coords || !invtrans);

    suplen = gwy_interpolation_get_support_size(interp);
    g_return_if_fail(suplen > 0);
    sf = -((suplen - 1)/2);
    st = suplen/2;

    if (gwy_enum_sanitize_value(exterior, GWY_TYPE_EXTERIOR_TYPE) != exterior) {
        g_critical("Invalid exterior type.");
        return;
    }
    if (exterior == GWY_EXTERIOR_LAPLACE) {
        g_warning("Laplace exterior cannot be used with distortions.  Using border extension.");
        exterior = GWY_EXTERIOR_BORDER_EXTEND;
    }

    xres = gwy_data_field_get_xres(source);
    yres = gwy_data_field_get_yres(source);
    newxres = gwy_data_field_get_xres(dest);
    newyres = gwy_data_field_get_yres(dest);

    if (gwy_interpolation_has_interpolating_basis(interp))
        coeffield = g_object_ref(source);
    else {
        coeffield = gwy_data_field_duplicate(source);
        gwy_interpolation_resolve_coeffs_2d(xres, yres, xres, gwy_data_field_get_data(coeffield), interp);
    }

    data = gwy_data_field_get_data(dest);
    cdata = gwy_data_field_get_data_const(coeffield);

    if (!coords) {
        gint newi;

        my_coords = g_new(GwyXY, newxres*newyres);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            shared(my_coords,invtrans,user_data,newxres,newyres) \
            private(newi)
#endif
        for (newi = 0; newi < newyres; newi++) {
            gint newj, k = newi*newxres;

            for (newj = 0; newj < newxres; newj++, k++)
                invtrans(newj + 0.5, newi + 0.5, &my_coords[k].x, &my_coords[k].y, user_data);
        }
        coords = my_coords;
    }

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(data,cdata,xres,yres,newxres,newyres,suplen,sf,st,coords,interp,exterior,fill_value)
#endif
    {
        gdouble *coeff = g_new(gdouble, suplen*suplen);
        gint ifrom = gwy_omp_chunk_start(newyres);
        gint ito = gwy_omp_chunk_end(newyres);
        gint newi, newj, oldi, oldj, i, j, ii, jj;
        gdouble x, y, v;
        gboolean vset;

        for (newi = ifrom; newi < ito; newi++) {
            for (newj = 0; newj < newxres; newj++) {
                x = coords[newi*newxres + newj].x - 0.5;
                y = coords[newi*newxres + newj].y - 0.5;
                vset = FALSE;
                if (y > yres || x > xres || y < 0.0 || x < 0.0) {
                    if (exterior == GWY_EXTERIOR_BORDER_EXTEND) {
                        x = CLAMP(x, 0, xres);
                        y = CLAMP(y, 0, yres);
                    }
                    else if (exterior == GWY_EXTERIOR_PERIODIC) {
                        x = (x > 0) ? fmod(x, xres) : fmod(x, xres) + xres;
                        y = (y > 0) ? fmod(y, yres) : fmod(y, yres) + yres;
                    }
                    else if (exterior == GWY_EXTERIOR_FIXED_VALUE) {
                        v = fill_value;
                        vset = TRUE;
                    }
                    else if (exterior == GWY_EXTERIOR_UNDEFINED) {
                        continue;
                    }
                    /* Mirror extension is what the interpolation code does by
                     * default.  Do not need to adjust anything.  */
                }
                if (!vset) {
                    oldi = (gint)floor(y);
                    y -= oldi;
                    oldj = (gint)floor(x);
                    x -= oldj;
                    for (i = sf; i <= st; i++) {
                        ii = (oldi + i + 2*st*yres) % (2*yres);
                        if (G_UNLIKELY(ii >= yres))
                            ii = 2*yres-1 - ii;
                        for (j = sf; j <= st; j++) {
                            jj = (oldj + j + 2*st*xres) % (2*xres);
                            if (G_UNLIKELY(jj >= xres))
                                jj = 2*xres-1 - jj;
                            coeff[(i-sf)*suplen + j-sf] = cdata[ii*xres + jj];
                        }
                    }
                    v = gwy_interpolation_interpolate_2d(x, y, suplen, coeff, interp);
                }
                data[newj + newxres*newi] = v;
            }
        }
        g_free(coeff);
    }

    g_object_unref(coeffield);
    g_free(my_coords);
}

/**
 * gwy_data_field_affine:
 * @source: Source data field.
 * @dest: Destination data field.
 * @invtrans: Inverse transform, that is the transformation from new pixel coordinates to old pixel coordinates,
 *            represented as (@j+0.5, @i+0.5), where @i and @j are the row and column indices.  It is represented as
 *            a six-element array [@axx, @axy, @ayx, @ayy, @bx, @by] where @axy is the coefficient from @x to @y.
 * @interp: Interpolation type to use.
 * @exterior: Exterior pixels handling.
 * @fill_value: The value to use with @GWY_EXTERIOR_FIXED_VALUE.
 *
 * Performs an affine transformation of a data field in the horizontal plane.
 *
 * Note the transform @invtrans is the inverse transform, in other words it calculates the old coordinates from the
 * new coordinates.  This way even degenerate (non-invertible) transforms can be meaningfully used. Also note that the
 * (column, row) coordinate system is left-handed.
 *
 * The %GWY_EXTERIOR_LAPLACE exterior type cannot be used with this function.
 *
 * Since: 2.34
 **/
void
gwy_data_field_affine(GwyDataField *source,
                      GwyDataField *dest,
                      const gdouble *invtrans,
                      GwyInterpolationType interp,
                      GwyExteriorType exterior,
                      gdouble fill_value)
{
    GwyDataField *coeffield;
    gdouble *data;
    const gdouble *cdata;
    gint xres, yres, newxres, newyres;
    gint suplen, sf, st;
    gdouble axx, axy, ayx, ayy, bx, by;

    g_return_if_fail(GWY_IS_DATA_FIELD(source));
    g_return_if_fail(GWY_IS_DATA_FIELD(dest));
    g_return_if_fail(invtrans);

    axx = invtrans[0];
    axy = invtrans[1];
    ayx = invtrans[2];
    ayy = invtrans[3];
    bx = invtrans[4];
    by = invtrans[5];

    suplen = gwy_interpolation_get_support_size(interp);
    g_return_if_fail(suplen > 0);
    sf = -((suplen - 1)/2);
    st = suplen/2;

    if (gwy_enum_sanitize_value(exterior, GWY_TYPE_EXTERIOR_TYPE) != exterior) {
        g_critical("Invalid exterior type.");
        return;
    }
    if (exterior == GWY_EXTERIOR_LAPLACE) {
        g_warning("Laplace exterior cannot be used with distortions.  Using border extension.");
        exterior = GWY_EXTERIOR_BORDER_EXTEND;
    }

    xres = gwy_data_field_get_xres(source);
    yres = gwy_data_field_get_yres(source);
    newxres = gwy_data_field_get_xres(dest);
    newyres = gwy_data_field_get_yres(dest);

    if (gwy_interpolation_has_interpolating_basis(interp))
        coeffield = g_object_ref(source);
    else {
        coeffield = gwy_data_field_duplicate(source);
        gwy_interpolation_resolve_coeffs_2d(xres, yres, xres, gwy_data_field_get_data(coeffield), interp);
    }

    data = gwy_data_field_get_data(dest);
    cdata = gwy_data_field_get_data_const(coeffield);

    /* Incorporate the half-pixel shifts to bx and by */
    bx += 0.5*(axx + axy - 1.0);
    by += 0.5*(ayx + ayy - 1.0);
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(data,cdata,xres,yres,newxres,newyres,suplen,sf,st,axx,axy,ayx,ayy,bx,by,interp,exterior,fill_value)
#endif
    {
        gdouble *coeff = g_new(gdouble, suplen*suplen);
        gint ifrom = gwy_omp_chunk_start(newyres);
        gint ito = gwy_omp_chunk_end(newyres);
        gint newi, newj, oldi, oldj, i, j, ii, jj;
        gdouble x, y, v;
        gboolean vset;

        for (newi = ifrom; newi < ito; newi++) {
            for (newj = 0; newj < newxres; newj++) {
                x = axx*newj + axy*newi + bx;
                y = ayx*newj + ayy*newi + by;
                vset = FALSE;
                if (y > yres || x > xres || y < 0.0 || x < 0.0) {
                    if (exterior == GWY_EXTERIOR_BORDER_EXTEND) {
                        x = CLAMP(x, 0, xres);
                        y = CLAMP(y, 0, yres);
                    }
                    else if (exterior == GWY_EXTERIOR_PERIODIC) {
                        x = (x > 0) ? fmod(x, xres) : fmod(x, xres) + xres;
                        y = (y > 0) ? fmod(y, yres) : fmod(y, yres) + yres;
                    }
                    else if (exterior == GWY_EXTERIOR_FIXED_VALUE) {
                        v = fill_value;
                        vset = TRUE;
                    }
                    else if (exterior == GWY_EXTERIOR_UNDEFINED) {
                        continue;
                    }
                    /* Mirror extension is what the interpolation code does by
                     * default.  Do not need to adjust anything.  */
                }
                if (!vset) {
                    oldi = (gint)floor(y);
                    y -= oldi;
                    oldj = (gint)floor(x);
                    x -= oldj;
                    for (i = sf; i <= st; i++) {
                        ii = (oldi + i + 2*st*yres) % (2*yres);
                        if (G_UNLIKELY(ii >= yres))
                            ii = 2*yres-1 - ii;
                        for (j = sf; j <= st; j++) {
                            jj = (oldj + j + 2*st*xres) % (2*xres);
                            if (G_UNLIKELY(jj >= xres))
                                jj = 2*xres-1 - jj;
                            coeff[(i - sf)*suplen + j - sf] = cdata[ii*xres + jj];
                        }
                    }
                    v = gwy_interpolation_interpolate_2d(x, y, suplen, coeff, interp);
                }
                data[newj + newxres*newi] = v;
            }
        }
        g_free(coeff);
    }

    g_object_unref(coeffield);
}

static gdouble
matrix2_det(const gdouble *m)
{
    return m[0]*m[3] - m[1]*m[2];
}

/* Permit dest = src */
static void
matrix2_vector2(gdouble *dest, const gdouble *m, const gdouble *src)
{
    gdouble xy[2];

    xy[0] = m[0]*src[0] + m[1]*src[1];
    xy[1] = m[2]*src[0] + m[3]*src[1];
    dest[0] = xy[0];
    dest[1] = xy[1];
}

/* Permit dest = src */
static void
matrix2_matrix2(gdouble *dest, const gdouble *m, const gdouble *src)
{
    gdouble xy[4];

    xy[0] = m[0]*src[0] + m[1]*src[2];
    xy[1] = m[0]*src[1] + m[1]*src[3];
    xy[2] = m[2]*src[0] + m[3]*src[2];
    xy[3] = m[2]*src[1] + m[3]*src[3];
    dest[0] = xy[0];
    dest[1] = xy[1];
    dest[2] = xy[2];
    dest[3] = xy[3];
}

/* Permit dest = src */
static void
invert_matrix2(gdouble *dest, const gdouble *src)
{
    gdouble D = matrix2_det(src);
    gdouble xy[4];

    gwy_debug("D %g", D);
    xy[0] = src[3]/D;
    xy[1] = -src[1]/D;
    xy[2] = -src[2]/D;
    xy[3] = src[0]/D;
    dest[0] = xy[0];
    dest[1] = xy[1];
    dest[2] = xy[2];
    dest[3] = xy[3];
}

static void
corner_max(gdouble x, gdouble y, const gdouble *m, gdouble *vmax)
{
    gdouble v[2];

    v[0] = x;
    v[1] = y;
    matrix2_vector2(v, m, v);
    vmax[0] = MAX(vmax[0], fabs(v[0]));
    vmax[1] = MAX(vmax[1], fabs(v[1]));
}

static void
solve_transform_real(const gdouble *a1a2, const gdouble *a1a2_corr, gdouble *m)
{
    gdouble tmp[4];
    tmp[0] = a1a2[0];
    tmp[1] = a1a2[2];
    tmp[2] = a1a2[1];
    tmp[3] = a1a2[3];
    invert_matrix2(m, tmp);
    tmp[0] = a1a2_corr[0];
    tmp[1] = a1a2_corr[2];
    tmp[2] = a1a2_corr[1];
    tmp[3] = a1a2_corr[3];
    matrix2_matrix2(m, tmp, m);
}

/**
 * gwy_data_field_affine_prepare:
 * @source: Source data field.
 * @dest: Destination data field.
 * @a1a2: Lattice vectors (or generally base vectors) in @source, as an array of four components: @x1, @y1, @x2 and
 *        @y2.
 * @a1a2_corr: Correct lattice vectors (or generally base vectors) @dest should have after the affine transform, in
 *             the same form as @a1a2.
 * @invtrans: Inverse transform as an array of six values to be filled according to gwy_data_field_affine()
 *            specification.
 * @scaling: How (or if) to scale the correct lattice vectors.
 * @prevent_rotation: %TRUE to prevent rotation of the data by rotating @a1a2_corr as a whole to a direction
 *                    preserving the data orientation.  %FALSE to take @a1a2_corr as given.
 * @oversampling: Oversampling factor.  Values larger than 1 mean smaller pixels (and more of them) in @dest, values
 *                smaller than 1 the opposite.  Pass 1.0 for the default pixel size choice.
 *
 * Resolves an affine transformation of a data field in the horizontal plane.
 *
 * This function calculates suitable arguments for gwy_data_field_affine() from given images and lattice vectors (in
 * real coordinates).
 *
 * Data field @dest will be resized and its real dimensions and units set in anticipation of gwy_data_field_affine().
 * Its contents will be destroyed.
 *
 * Note that @a1a2_corr is an input-output parameter.  In general, the vectors will be modified according to @scaling
 * and @prevent_rotation to the actual vectors in @dest after the transformation.  Only if @prevent_rotation is %FALSE
 * and @scaling is %GWY_AFFINE_SCALING_AS_GIVEN the vectors are preserved.
 *
 * Since: 2.49
 **/
void
gwy_data_field_affine_prepare(GwyDataField *source,
                              GwyDataField *dest,
                              const gdouble *a1a2,
                              gdouble *a1a2_corr,
                              gdouble *invtrans,
                              GwyAffineScalingType scaling,
                              gboolean prevent_rotation,
                              gdouble oversampling)
{
    gdouble dx, dy, sdx, sdy, alpha, q;
    gdouble vmax[2], tmp[4];
    guint xres, yres, i;

    g_return_if_fail(GWY_IS_DATA_FIELD(source));
    g_return_if_fail(GWY_IS_DATA_FIELD(dest));
    g_return_if_fail(a1a2);
    g_return_if_fail(a1a2_corr);
    g_return_if_fail(invtrans);

    if (!(oversampling > 0.0)) {
        g_warning("Oversampling must be positive.");
        oversampling = 1.0;
    }
    sdx = source->xreal/source->xres;
    sdy = source->yreal/source->yres;

    gwy_debug("a1a2 %g %g %g %g", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    gwy_debug("a1a2_corr %g %g %g %g", a1a2_corr[0], a1a2_corr[1], a1a2_corr[2], a1a2_corr[3]);
    /* This is an approximate rotation correction to get the base more or less
     * oriented in the plane as expected and not upside down. */
    if (prevent_rotation) {
        alpha = atan2(-a1a2[1], a1a2[0]);
        tmp[0] = tmp[3] = cos(alpha);
        tmp[1] = sin(alpha);
        tmp[2] = -sin(alpha);
        matrix2_vector2(a1a2_corr, tmp, a1a2_corr);
        matrix2_vector2(a1a2_corr + 2, tmp, a1a2_corr + 2);
    }

    solve_transform_real(a1a2, a1a2_corr, invtrans);
    gwy_debug("invtrans %g %g %g %g", invtrans[0], invtrans[1], invtrans[2], invtrans[3]);

    /* This is the exact rotation correction. */
    if (prevent_rotation) {
        alpha = atan2(invtrans[2], invtrans[0]);
        gwy_debug("alpha %g", alpha);
        tmp[0] = tmp[3] = cos(alpha);
        tmp[1] = sin(alpha);
        tmp[2] = -sin(alpha);
        matrix2_matrix2(invtrans, tmp, invtrans);
    }

    if (scaling == GWY_AFFINE_SCALING_PRESERVE_AREA)
        q = 1.0/sqrt(fabs(matrix2_det(invtrans)));
    else if (scaling == GWY_AFFINE_SCALING_PRESERVE_X)
        q = 1.0/hypot(invtrans[0], invtrans[2]);
    else
        q = 1.0;

    for (i = 0; i < 4; i++) {
        invtrans[i] *= q;
        /* To create the corrected lattice selection on result. */
        a1a2_corr[i] *= q;
    }
    gwy_debug("invtrans %g %g %g %g",
              invtrans[0], invtrans[1], invtrans[2], invtrans[3]);

    vmax[0] = vmax[1] = 0.0;
    corner_max(source->xreal, source->yreal, invtrans, vmax);
    corner_max(-source->xreal, source->yreal, invtrans, vmax);
    corner_max(source->xreal, -source->yreal, invtrans, vmax);
    corner_max(-source->xreal, -source->yreal, invtrans, vmax);

    /* Prevent information loss by using a sufficient resolution to represent
     * original pixels. */
    tmp[0] = sdx;
    tmp[1] = tmp[2] = 0.0;
    tmp[3] = sdy;
    gwy_debug("dxdy %g %g", tmp[0], tmp[3]);
    matrix2_matrix2(tmp, invtrans, tmp);
    gwy_debug("pix_corr %g %g %g %g", tmp[0], tmp[1], tmp[2], tmp[3]);
    dx = hypot(tmp[0]/G_SQRT2, tmp[1]/G_SQRT2);
    dy = hypot(tmp[2]/G_SQRT2, tmp[3]/G_SQRT2);
    dx = dy = MIN(dx, dy)/oversampling;
    xres = GWY_ROUND(vmax[0]/dx);
    yres = GWY_ROUND(vmax[1]/dy);
    gwy_debug("dxdy_corr %g %g", dx, dy);
    gwy_debug("res %u %u", xres, yres);

    gwy_data_field_resample(dest, xres, yres, GWY_INTERPOLATION_NONE);
    dest->xreal = dx*xres;
    dest->yreal = dy*yres;
    /* We could preserve the centre but that would be strange for the typical case when there are no offsets. */
    dest->xoff = dest->yoff = 0.0;
    gwy_data_field_copy_units(source, dest);

    /* So far, we used invtrans as a temporary matrix.  Now really fill it with
     * the inverse transformation. */
    invert_matrix2(invtrans, invtrans);
    gwy_debug("minv %g %g %g %g", invtrans[0], invtrans[1], invtrans[2], invtrans[3]);

    /* Multiply from right by pixel-to-real matrix in the corrected field. */
    tmp[0] = dx;
    tmp[1] = tmp[2] = 0.0;
    tmp[3] = dy;
    matrix2_matrix2(invtrans, invtrans, tmp);
    /* and from left by real-to-pixel matrix in the original field. */
    tmp[0] = 1.0/sdx;
    tmp[1] = tmp[2] = 0.0;
    tmp[3] = 1.0/sdy;
    matrix2_matrix2(invtrans, tmp, invtrans);
    gwy_debug("minvpix %g %g %g %g", invtrans[0], invtrans[1], invtrans[2], invtrans[3]);

    invtrans[4] = 0.5*xres;
    invtrans[5] = 0.5*yres;
    matrix2_vector2(invtrans + 4, invtrans, invtrans + 4);
    invtrans[4] = 0.5*source->xres - invtrans[4];
    invtrans[5] = 0.5*source->yres - invtrans[5];
    gwy_debug("b %g %g", invtrans[4], invtrans[5]);
}

static void
maybe_swap_axes(gdouble *a1a2)
{
    gdouble phi;

    phi = fmod(atan2(a1a2[1], a1a2[0]) + 4.0*G_PI - atan2(a1a2[3], a1a2[2]), 2.0*G_PI);
    if (phi > G_PI) {
        GWY_SWAP(gdouble, a1a2[0], a1a2[2]);
        GWY_SWAP(gdouble, a1a2[1], a1a2[3]);
    }
}

static gboolean
transform_vectors_real_freq(gdouble *xy)
{
    gdouble D = matrix2_det(xy);
    gdouble a = fabs(xy[0]*xy[3]) + fabs(xy[1]*xy[2]);

    if (fabs(D)/a < 1e-9)
        return FALSE;

    invert_matrix2(xy, xy);
    /* Transpose. */
    GWY_SWAP(gdouble, xy[1], xy[2]);
    return TRUE;
}

static gint
compare_maxima(gconstpointer pa, gconstpointer pb)
{
    const LatticeMaximumInfo *a = (const LatticeMaximumInfo*)pa;
    const LatticeMaximumInfo *b = (const LatticeMaximumInfo*)pb;

    if (a->basecount*a->q > b->basecount*b->q)
        return -1;
    if (a->basecount*a->q < b->basecount*b->q)
        return 1;

    if (a->q > b->q)
        return -1;
    if (a->q < b->q)
        return 1;

    /* Ensure comparison stability.  This should play no role in significance
     * sorting. */
    if (a->y < b->y)
        return -1;
    if (a->y > b->y)
        return 1;
    if (a->x < b->x)
        return -1;
    if (a->x > b->x)
        return 1;
    return 0;
}

/* @dfield is ACF or PSDF here. */
static gboolean
guess_lattice(GwyDataField *dfield, gdouble *a1a2, gboolean is_psdf)
{
    enum { nquantities = 3 };
    GwyGrainQuantity quantities[nquantities] = {
        GWY_GRAIN_VALUE_MAXIMUM,
        GWY_GRAIN_VALUE_CENTER_X,
        GWY_GRAIN_VALUE_CENTER_Y,
    };
    GwyDataField *smoothed = NULL, *mask;
    gdouble *values[nquantities];
    LatticeMaximumInfo *maxima;
    gint *grains;
    guint i, j, k, ngrains;
    gdouble dh, cphi, sphi, phi, l1, l2, d, x, y;
    gboolean ok = FALSE;

    /* Mark local maxima. */
    mask = gwy_data_field_new_alike(dfield, FALSE);
    if (is_psdf) {
        smoothed = gwy_data_field_duplicate(dfield);
        gwy_data_field_filter_gaussian(smoothed, 1.2);
        dfield = smoothed;
    }

    gwy_data_field_mark_extrema(dfield, mask, TRUE);
    grains = g_new0(gint, dfield->xres*dfield->yres);
    ngrains = gwy_data_field_number_grains(mask, grains);
    GWY_OBJECT_UNREF(mask);

    /* Find the position and value of each. */
    for (i = 0; i < nquantities; i++)
        values[i] = g_new(gdouble, ngrains+1);

    gwy_data_field_grains_get_quantities(dfield, values, quantities, nquantities, ngrains, grains);
    GWY_OBJECT_UNREF(smoothed);

    maxima = g_new(LatticeMaximumInfo, ngrains);
    dh = hypot(dfield->xreal/dfield->xres, dfield->yreal/dfield->yres);
    for (i = 0; i < ngrains; i++) {
        maxima[i].max = values[0][i+1];
        maxima[i].x = values[1][i+1];
        maxima[i].y = values[2][i+1];
        maxima[i].d = hypot(maxima[i].x, maxima[i].y);
        maxima[i].q = maxima[i].max/(maxima[i].d + 5.0*dh);
        maxima[i].basecount = 0;
    }
    for (i = 0; i < nquantities; i++)
        g_free(values[i]);

    /* Remove the central peak, i.e. anything too close to the centre */
    i = j = 0;
    while (i < ngrains) {
        d = maxima[i].d;
        maxima[j] = maxima[i];
        if (d >= 1.8*dh)
            j++;
        i++;
    }
    ngrains = j;

    if ((is_psdf && ngrains < 4) || (!is_psdf && ngrains < 14)) {
        gwy_debug("Too few maxima (after centre removal): %d.", ngrains);
        g_free(maxima);
        return FALSE;
    }

    qsort(maxima, ngrains, sizeof(LatticeMaximumInfo), compare_maxima);
#ifdef DEBUG
    for (i = 0; i < ngrains; i++) {
        gwy_debug("[%u] (%g, %g) %g :: %g", i, maxima[i].x, maxima[i].y, maxima[i].max, maxima[i].q);
    }
#endif

    /* Remove anything with direction opposite to the first vector.  But we must carefully accept ortohogonal vectors.
     * This is just a half-plane selection though it influences the preferred vectors, of course. */
    gwy_debug("Base-plane selector [%u] (%g, %g) %g", 0, maxima[0].x, maxima[0].y, maxima[0].max);
    cphi = maxima[0].x/maxima[0].d;
    sphi = maxima[0].y/maxima[0].d;
    i = j = 1;
    while (i < ngrains) {
        x = cphi*maxima[i].x + sphi*maxima[i].y;
        y = cphi*maxima[i].y - sphi*maxima[i].x;
        maxima[j] = maxima[i];
        if (x > 1e-9*dh || (x > -1e-9*dh && y > 1e-9*dh))
            j++;
        i++;
    }
    ngrains = j;

    if ((is_psdf && ngrains < 2) || (!is_psdf && ngrains < 7)) {
        gwy_debug("Too few maxima (after half-plane removal): %d.", ngrains);
        g_free(maxima);
        return FALSE;
    }

    /* Locate the most important maxima. */
    ngrains = MIN(ngrains, 12);
    for (i = 0; i < ngrains; i++) {
        for (j = i+1; j < ngrains; j++) {
            x = maxima[i].x + maxima[j].x;
            y = maxima[i].y + maxima[j].y;
            for (k = 0; k < ngrains; k++) {
                if (fabs(maxima[k].x - x) < dh && fabs(maxima[k].y - y) < dh) {
                    maxima[i].basecount++;
                    maxima[j].basecount++;
                }
            }
        }
    }
    qsort(maxima, ngrains, sizeof(LatticeMaximumInfo), compare_maxima);
#ifdef DEBUG
    for (i = 0; i < ngrains; i++) {
        gwy_debug("[%u] (%g, %g) %g #%u", i, maxima[i].x, maxima[i].y, maxima[i].max, maxima[i].basecount);
    }
#endif

    if (!is_psdf && maxima[1].basecount < 3) {
        g_free(maxima);
        return FALSE;
    }

    a1a2[0] = maxima[0].x;
    a1a2[1] = maxima[0].y;
    dh = maxima[0].d;
    /* Exclude maxima that appear to be collinear with the first one,
     * otherwise take the next one with the highest basecount. */
    for (i = 1; i < ngrains; i++) {
        for (k = 2; k < 5; k++) {
            if (fabs(maxima[i].x/k - a1a2[0]) < 0.2*dh && fabs(maxima[i].y/k - a1a2[1]) < 0.2*dh) {
                gwy_debug("Excluding #%u for collinearity (%u).", i, k);
                break;
            }
        }
        if (k == 5) {
            a1a2[2] = maxima[i].x;
            a1a2[3] = maxima[i].y;
            ok = TRUE;
            break;
        }
    }

    g_free(maxima);
    if (!ok)
        return FALSE;

    /* Try to choose some sensible vectors among the equivalent choices. It does not guarantee the choice a human
     * would make but at least make a reasonable one... */
    for (i = 0; i < 4; i++)
        a1a2[i] = -a1a2[i];
    if (is_psdf)
        transform_vectors_real_freq(a1a2);
    maybe_swap_axes(a1a2);
    l1 = hypot(a1a2[0], a1a2[1]);
    l2 = hypot(a1a2[2], a1a2[3]);
    phi = acos((a1a2[0]*a1a2[2] + a1a2[1]*a1a2[3])/(l1*l2));
    if (phi > 0.5*G_PI) {
        if (a1a2[0]*a1a2[3] - a1a2[1]*a1a2[2] > 0.0) {
            a1a2[2] = -a1a2[2];
            a1a2[3] = -a1a2[3];
        }
        else {
            a1a2[0] = -a1a2[0];
            a1a2[1] = -a1a2[1];
        }
    }
    maybe_swap_axes(a1a2);

    return TRUE;
}

static gdouble
refine_from_multiple(GwyDataField *dfield, gdouble *a1a2,
                     gint peakrange, gint dist2limit, gint xwinsize, gint ywinsize)
{
    gint i, j, n, nex, xres, yres, ii, jj;
    gdouble sii, sij, sjj, six, sjx, siy, sjy, xytmp[2];
    gdouble w, D, dx, dy, xoff, yoff;

    xoff = dfield->xoff;
    yoff = dfield->yoff;
    xres = dfield->xres;
    yres = dfield->yres;
    dx = dfield->xreal/xres;
    dy = dfield->yreal/yres;
    n = nex = 0;
    sii = sij = sjj = six = sjx = siy = sjy = 0.0;
    for (i = -peakrange; i <= peakrange; i++) {
        for (j = -peakrange; j <= peakrange; j++) {
            w = i*i + j*j;
            if (w > dist2limit || w == 0)
                continue;

            xytmp[0] = i*a1a2[0] + j*a1a2[2];
            xytmp[1] = i*a1a2[1] + j*a1a2[3];

            xytmp[0] = (xytmp[0] - xoff)/dx;
            xytmp[1] = (xytmp[1] - yoff)/dy;
            /* Do not go outside the datafield. */
            if (xytmp[0] < 1 || xytmp[0] > xres-2
                || xytmp[1] < 1 || xytmp[1] > yres-2)
                continue;

            nex++;
            if (!gwy_data_field_local_maximum(dfield, xytmp + 0, xytmp + 1, xwinsize, ywinsize))
                continue;

            jj = GWY_ROUND(xytmp[0]);
            ii = GWY_ROUND(xytmp[1]);
            if (jj < 0 || jj >= xres || ii < 0 || ii >= yres)
                continue;

            xytmp[0] = (xytmp[0] + 0.5)*dx + xoff;
            xytmp[1] = (xytmp[1] + 0.5)*dy + yoff;

            w = dfield->data[ii*xres + jj]/w;
            sii += i*i*w;
            sij += i*j*w;
            sjj += j*j*w;
            six += i*xytmp[0]*w;
            sjx += j*xytmp[0]*w;
            siy += i*xytmp[1]*w;
            sjy += j*xytmp[1]*w;
            n++;
        }
    }
    gwy_debug("nex=%d, n=%d", nex, n);

    if (!n)
        return 0.0;

    D = sii*sjj - sij*sij;
    a1a2[0] = (six*sjj - sjx*sij)/D;
    a1a2[2] = (sjx*sii - six*sij)/D;
    a1a2[1] = (siy*sjj - sjy*sij)/D;
    a1a2[3] = (sjy*sii - siy*sij)/D;

    return (gdouble)n/nex;
}

static gboolean
refine_lattice(GwyDataField *dfield, gdouble *a1a2, gboolean is_psdf)
{
    gint xwinsize, ywinsize;
    gdouble r, dx, dy;

    dx = dfield->xreal/dfield->xres;
    dy = dfield->yreal/dfield->yres;
    xwinsize = (gint)(0.32*MAX(fabs(a1a2[0]), fabs(a1a2[2]))/dx + 0.5);
    ywinsize = (gint)(0.32*MAX(fabs(a1a2[1]), fabs(a1a2[3]))/dy + 0.5);
    gwy_debug("window size: %dx%d", xwinsize, ywinsize);

    r = refine_from_multiple(dfield, a1a2, 1, 2, xwinsize, ywinsize);
    gwy_debug("refine1(%g): (%g, %g) (%g, %g)", r, a1a2[0], a1a2[1], a1a2[2], a1a2[3]);

    if (is_psdf)
        return r >= 0.75;

    if (r == 1.0) {
        xwinsize = 5*xwinsize/6;
        ywinsize = 5*ywinsize/6;
        r = refine_from_multiple(dfield, a1a2, 3, 25, xwinsize, ywinsize);
        gwy_debug("refine3(%g): (%g, %g) (%g, %g)", r, a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    }

    if (r == 1.0) {
        xwinsize = 7*xwinsize/8;
        ywinsize = 7*ywinsize/8;
        r = refine_from_multiple(dfield, a1a2, 5, 29, xwinsize, ywinsize);
        gwy_debug("refine5(%g): (%g, %g) (%g, %g)", r, a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    }

    return r >= 0.5;
}

/**
 * gwy_data_field_measure_lattice_acf:
 * @acf2d: Data field containing two-dimensional autocorrelation function.
 * @a1a2: Lattice vectors as an array of four components: @x1, @y1, @x2 and @y2 (in real coordinates).
 *
 * Estimates or improves estimate of lattice vectors from a 2D ACF field.
 *
 * Note that the 2D ACF of a data field has to be passed, not the data field itself.  The correlation function can be
 * for instance calculated by gwy_data_field_2dacf(). However, you can calculate and/or process the correlation
 * function in any way you see fit.
 *
 * When the vectors in @a1a2 are zero the function attempts to estimate the lattice from scratch.  But if @a1a2
 * contains two non-zero vectors it takes them as approximate lattice vectors to improve.
 *
 * If the function return %FALSE the array @a1a2 is filled with useless values and must be ignored.
 *
 * Returns: %TRUE if good lattice vectors were found, %FALSE on failure.
 *
 * Since: 2.49
 **/
gboolean
gwy_data_field_measure_lattice_acf(GwyDataField *acf2d, gdouble *a1a2)
{
    gdouble dx, dy;
    guint i;

    if ((a1a2[0] == 0.0 && a1a2[1] == 0.0) || (a1a2[2] == 0.0 && a1a2[3] == 0.0)) {
        if (!guess_lattice(acf2d, a1a2, FALSE))
            return FALSE;
        gwy_debug("guess: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    }

    for (i = 0; i < 4; i++) {
        if (gwy_isnan(a1a2[i]) || gwy_isinf(a1a2[i])) {
            gwy_debug("inf/nan 1");
            return FALSE;
        }
    }

    if (!refine_lattice(acf2d, a1a2, FALSE)) {
        gwy_debug("failed to refine");
        return FALSE;
    }

    for (i = 0; i < 4; i++) {
        if (gwy_isnan(a1a2[i]) || gwy_isinf(a1a2[i])) {
            gwy_debug("inf/nan 2");
            return FALSE;
        }
    }

    /* For very skewed lattices refine() can produce two of the same vector. */
    dx = acf2d->xreal/acf2d->xres;
    dy = acf2d->yreal/acf2d->yres;
    if (hypot(a1a2[0] - a1a2[2], a1a2[1] - a1a2[3]) < 1.8*hypot(dx, dy)) {
        gwy_debug("too skewed");
        return FALSE;
    }

    return TRUE;
}

/**
 * gwy_data_field_measure_lattice_psdf:
 * @psdf2d: Data field containing two-dimensional power spectrum density function (or alternatively Fourier
 *          coefficient modulus).
 * @a1a2: Lattice vectors as an array of four components: @x1, @y1, @x2 and @y2 (in real coordinates).
 *
 * Estimates or improves estimate of lattice vectors from a 2D PSDF field.
 *
 * Note that the 2D PSDF of a data field has to be passed, not the data field itself.  The spectral density can be for
 * instance calculated by gwy_data_field_2dfft() and summing the squares of real and imaginary parts However, you can
 * calculate and/or process the spectral density in any way you see fit.
 *
 * When the vectors in @a1a2 are zero the function attempts to estimate the lattice from scratch.  But if @a1a2
 * contains two non-zero vectors it takes them as approximate lattice vectors to improve.
 *
 * If the function return %FALSE the array @a1a2 is filled with useless values and must be ignored.
 *
 * Returns: %TRUE if good lattice vectors were found, %FALSE on failure.
 *
 * Since: 2.49
 **/
gboolean
gwy_data_field_measure_lattice_psdf(GwyDataField *psdf2d, gdouble *a1a2)
{
    gdouble dx, dy;
    guint i;

    gwy_debug("input: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    if ((a1a2[0] == 0.0 && a1a2[1] == 0.0) || (a1a2[2] == 0.0 && a1a2[3] == 0.0)) {
        if (!guess_lattice(psdf2d, a1a2, TRUE))
            return FALSE;
        gwy_debug("guess: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    }

    transform_vectors_real_freq(a1a2);
    gwy_debug("freq: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    for (i = 0; i < 4; i++) {
        if (gwy_isnan(a1a2[i]) || gwy_isinf(a1a2[i]))
            return FALSE;
    }

    if (!refine_lattice(psdf2d, a1a2, TRUE))
        return FALSE;

    gwy_debug("refined: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    transform_vectors_real_freq(a1a2);
    gwy_debug("real: (%g, %g) (%g, %g)", a1a2[0], a1a2[1], a1a2[2], a1a2[3]);
    for (i = 0; i < 4; i++) {
        if (gwy_isnan(a1a2[i]) || gwy_isinf(a1a2[i]))
            return FALSE;
    }

    /* For very skewed lattices refine() can produce two of the same vector. */
    dx = 1.0/psdf2d->xreal;
    dy = 1.0/psdf2d->yreal;
    if (hypot(a1a2[0] - a1a2[2], a1a2[1] - a1a2[3]) < 1.8*hypot(dx, dy))
        return FALSE;

    return TRUE;
}

/**
 * gwy_data_field_mark_scars:
 * @data_field: A data field to find scars in.
 * @result: A data field to store the result to (it is resized to match @data_field).
 * @threshold_high: Miminum relative step for scar marking, must be positive.
 * @threshold_low: Definite relative step for scar marking, must be at least equal to @threshold_high.
 * @min_scar_len: Minimum length of a scar, shorter ones are discarded (must be at least one).
 * @max_scar_width: Maximum width of a scar, must be at least one.
 * @negative: %TRUE to detect negative scars, %FALSE to positive.
 *
 * Find and marks scars in a data field.
 *
 * Scars are linear horizontal defects, consisting of shifted values. Zero or negative values in @result siginify
 * normal data, positive values siginify samples that are part of a scar.
 *
 * Since: 2.46
 **/
void
gwy_data_field_mark_scars(GwyDataField *data_field,
                          GwyDataField *result,
                          gdouble threshold_high,
                          gdouble threshold_low,
                          gdouble min_scar_len,
                          gdouble max_scar_width,
                          gboolean negative)
{
    gint xres, yres, i, j, k;
    gdouble rms;
    const gdouble *d;
    gdouble *m;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_DATA_FIELD(result));
    xres = data_field->xres;
    yres = data_field->yres;
    d = data_field->data;
    gwy_data_field_resample(result, xres, yres, GWY_INTERPOLATION_NONE);
    gwy_data_field_clear(result);
    m = gwy_data_field_get_data(result);

    min_scar_len = MAX(min_scar_len, 1);
    max_scar_width = MIN(max_scar_width, yres - 2);
    threshold_high = MAX(threshold_high, threshold_low);
    if (min_scar_len > xres || max_scar_width < 1 || threshold_low <= 0.0)
        return;

    /* compute `vertical rms' */
    rms = 0.0;
    for (i = 0; i < yres-1; i++) {
        const gdouble *row = d + i*xres;

        for (j = 0; j < xres; j++) {
            gdouble z = row[j] - row[j + xres];

            rms += z*z;
        }
    }
    rms = sqrt(rms/(xres*yres));
    if (rms == 0.0)
        return;

    /* initial scar search */
    for (i = 0; i < yres - (max_scar_width + 1); i++) {
        for (j = 0; j < xres; j++) {
            gdouble top, bottom;
            const gdouble *row = d + i*xres + j;

            if (negative) {
                top = row[0];
                bottom = row[xres];
                for (k = 1; k <= max_scar_width; k++) {
                    top = MIN(row[0], row[xres*(k + 1)]);
                    bottom = MAX(bottom, row[xres*k]);
                    if (top - bottom >= threshold_low*rms)
                        break;
                }
                if (k <= max_scar_width) {
                    gdouble *mrow = m + i*xres + j;

                    while (k) {
                        mrow[k*xres] = fmax(mrow[k*xres], (top - row[k*xres])/rms);
                        k--;
                    }
                }
            }
            else {
                bottom = row[0];
                top = row[xres];
                for (k = 1; k <= max_scar_width; k++) {
                    bottom = MAX(row[0], row[xres*(k + 1)]);
                    top = MIN(top, row[xres*k]);
                    if (top - bottom >= threshold_low*rms)
                        break;
                }
                if (k <= max_scar_width) {
                    gdouble *mrow = m + i*xres + j;

                    while (k) {
                        mrow[k*xres] = fmax(mrow[k*xres], (row[k*xres] - bottom)/rms);
                        k--;
                    }
                }
            }
        }
    }
    /* expand high threshold to neighbouring low threshold */
    for (i = 0; i < yres; i++) {
        gdouble *mrow = m + i*xres;

        for (j = 1; j < xres; j++) {
            if (mrow[j] >= threshold_low && mrow[j-1] >= threshold_high)
                mrow[j] = threshold_high;
        }
        for (j = xres-1; j > 0; j--) {
            if (mrow[j-1] >= threshold_low && mrow[j] >= threshold_high)
                mrow[j-1] = threshold_high;
        }
    }
    /* kill too short segments, clamping result along the way */
    for (i = 0; i < yres; i++) {
        gdouble *mrow = m + i*xres;

        k = 0;
        for (j = 0; j < xres; j++) {
            if (mrow[j] >= threshold_high) {
                mrow[j] = 1.0;
                k++;
                continue;
            }
            if (k && k < min_scar_len) {
                while (k) {
                    mrow[j-k] = 0.0;
                    k--;
                }
            }
            mrow[j] = 0.0;
            k = 0;
        }
        if (k && k < min_scar_len) {
            while (k) {
                mrow[j-k] = 0.0;
                k--;
            }
        }
    }
}

/**
 * gwy_data_field_subtract_row_shifts:
 * @data_field: A data field.
 * @shifts: Data line containing the row shifts.
 *
 * Shifts entire data field rows as specified by given data line.
 *
 * Data line @shifts must have resolution corresponding to the number of @data_field rows.  Its values are subtracted
 * from individual field rows.
 *
 * Since: 2.52
 **/
void
gwy_data_field_subtract_row_shifts(GwyDataField *data_field,
                                   GwyDataLine *shifts)
{
    gint xres, yres, i, j;
    gdouble z;
    const gdouble *s;
    gdouble *d;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_DATA_LINE(shifts));
    xres = data_field->xres;
    yres = data_field->yres;
    g_return_if_fail(shifts->res == yres);

    s = shifts->data;
    d = data_field->data;

    for (i = 0; i < yres; i++) {
        z = s[i];
        for (j = xres; j; j--, d++)
            *d -= z;
    }

    gwy_data_field_invalidate(data_field);
}

static gdouble
trimmed_mean_or_median(gdouble *array, guint n, gdouble p)
{
    guint nlowest = GWY_ROUND(p*n);
    guint nhighest = GWY_ROUND(p*n);

    if (nlowest + nhighest + 1 >= n)
        return gwy_math_median(n, array);
    return gwy_math_trimmed_mean(n, array, nlowest, nhighest);
}

static void
zero_level_row_shifts(GwyDataLine *shifts)
{
    gwy_data_line_add(shifts, -gwy_data_line_get_avg(shifts));
}

static void
slope_level_row_shifts(GwyDataLine *shifts)
{
    gdouble a, b;

    gwy_data_line_get_line_coeffs(shifts, &a, &b);
    gwy_data_line_line_level(shifts, a, b);
}

/**
 * gwy_data_field_find_row_shifts_trimmed_mean:
 * @data_field: A data field.
 * @mask: Mask of values to take values into account/exclude, or %NULL for full @data_field.
 * @masking: Masking mode to use.  See the introduction for description of masking modes.
 * @trimfrac: Fraction of lowest values and highest values to discard when trimming.
 * @mincount: Minimum number of values in a row necessary for per-row calculation.  Rows which are essentially
 *            completely masked are not shifted with respect to a global value.  Pass a non-positive number to use an
 *            automatic minimum count.
 *
 * Finds row shifts to misaligned row correction using trimmed row means.
 *
 * For zero @trimfrac the function calculates row means.  For @trimfrac of 1/2 or larger it calculates row medians.
 * Values between correspond to trimmed means.
 *
 * Returns: A newly created data line containing the row shifts, for instance row means, medians or trimmed means.
 *
 * Since: 2.52
 **/
GwyDataLine*
gwy_data_field_find_row_shifts_trimmed_mean(GwyDataField *data_field,
                                            GwyDataField *mask,
                                            GwyMaskingType masking,
                                            gdouble trimfrac,
                                            gint mincount)
{
    GwyDataLine *shifts;
    gint xres, yres;
    const gdouble *d, *m;
    gdouble total_median;
    gdouble *sdata;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);
    g_return_val_if_fail(trimfrac >= 0.0, NULL);
    xres = data_field->xres;
    yres = data_field->yres;
    if (masking == GWY_MASK_IGNORE)
        mask = NULL;
    else if (mask) {
        g_return_val_if_fail(GWY_IS_DATA_FIELD(mask), NULL);
        g_return_val_if_fail(mask->xres == xres, NULL);
        g_return_val_if_fail(mask->yres == yres, NULL);
    }
    else
        masking = GWY_MASK_IGNORE;

    if (mincount <= 0)
        mincount = GWY_ROUND(log(xres) + 1);

    shifts = gwy_data_line_new(yres, data_field->yreal, FALSE);
    shifts->off = data_field->yoff;
    gwy_data_field_copy_units_to_data_line(data_field, shifts);
    total_median = gwy_data_field_area_get_median_mask(data_field, mask, masking, 0, 0, xres, yres);

    d = data_field->data;
    m = mask ? mask->data : NULL;
    sdata = shifts->data;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,sdata,xres,yres,masking,shifts,total_median,trimfrac,mincount)
#endif
    {
        gdouble *buf = g_new(gdouble, xres);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j, count;
        const gdouble *row, *mrow;

        for (i = ifrom; i < ito; i++) {
            if (m) {
                row = d + i*xres;
                mrow = m + i*xres;
                count = 0;
                if (masking == GWY_MASK_INCLUDE) {
                    for (j = 0; j < xres; j++) {
                        if (mrow[j] > 0.0)
                            buf[count++] = row[j];
                    }
                }
                else {
                    for (j = 0; j < xres; j++) {
                        if (mrow[j] < 1.0)
                            buf[count++] = row[j];
                    }
                }
                if (count >= mincount)
                    sdata[i] = trimmed_mean_or_median(buf, count, trimfrac);
                else
                    sdata[i] = total_median;
            }
            else {
                gwy_assign(buf, d + i*xres, xres);
                sdata[i] = trimmed_mean_or_median(buf, xres, trimfrac);
            }
        }

        g_free(buf);
    }

    zero_level_row_shifts(shifts);
    return shifts;
}

/**
 * gwy_data_field_find_row_shifts_trimmed_diff:
 * @data_field: A data field.
 * @mask: Mask of values to take values into account/exclude, or %NULL for full @data_field.
 * @masking: Masking mode to use.  See the introduction for description of masking modes.
 * @trimfrac: Fraction of lowest values and highest values to discard when trimming.
 * @mincount: Minimum number of values in a row necessary for per-row calculation.  Rows which are essentially
 *            completely masked are not shifted with respect to a global value.  Pass a non-positive number to use an
 *            automatic minimum count.
 *
 * Finds row shifts to misaligned row correction using trimmed means of row differences.
 *
 * For zero @trimfrac the function calculates row means.  For @trimfrac of 1/2 or larger it calculates row medians.
 * Values between correspond to trimmed means.
 *
 * Returns: A newly created data line containing the row shifts, for instance row means, medians or trimmed means.
 *
 * Since: 2.52
 **/
GwyDataLine*
gwy_data_field_find_row_shifts_trimmed_diff(GwyDataField *data_field,
                                            GwyDataField *mask,
                                            GwyMaskingType masking,
                                            gdouble trimfrac,
                                            gint mincount)
{
    GwyDataLine *shifts;
    gint xres, yres, k;
    const gdouble *d, *m;
    gdouble *sdata;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);
    g_return_val_if_fail(trimfrac >= 0.0, NULL);
    xres = data_field->xres;
    yres = data_field->yres;
    if (masking == GWY_MASK_IGNORE)
        mask = NULL;
    else if (mask) {
        g_return_val_if_fail(GWY_IS_DATA_FIELD(mask), NULL);
        g_return_val_if_fail(mask->xres == xres, NULL);
        g_return_val_if_fail(mask->yres == yres, NULL);
    }
    else
        masking = GWY_MASK_IGNORE;

    if (mincount <= 0)
        mincount = GWY_ROUND(log(xres) + 1);

    shifts = gwy_data_line_new(yres, data_field->yreal, FALSE);
    shifts->off = data_field->yoff;
    gwy_data_field_copy_units_to_data_line(data_field, shifts);

    d = data_field->data;
    m = mask ? mask->data : NULL;
    sdata = shifts->data;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,sdata,xres,yres,masking,shifts,trimfrac,mincount)
#endif
    {
        gdouble *buf = g_new(gdouble, xres);
        gint ifrom = gwy_omp_chunk_start(yres-1);
        gint ito = gwy_omp_chunk_end(yres-1);
        gint i, j, count;
        const gdouble *row, *mrow;

        for (i = ifrom; i < ito; i++) {
            row = d + i*xres;
            count = 0;
            if (masking == GWY_MASK_INCLUDE) {
                mrow = m + i*xres;
                for (j = 0; j < xres; j++) {
                    if (mrow[j] <= 1.0 || mrow[xres + j] <= 1.0)
                        continue;
                    buf[count++] = row[xres + j] - row[j];
                }
            }
            else if (masking == GWY_MASK_EXCLUDE) {
                mrow = m + i*xres;
                for (j = 0; j < xres; j++) {
                    if (mrow[j] >= 1.0 || mrow[xres + j] >= 1.0)
                        continue;
                    buf[count++] = row[xres + j] - row[j];
                }
            }
            else {
                for (j = 0; j < xres; j++)
                    buf[j] = row[xres + j] - row[j];
                count = xres;
            }

            if (count >= mincount)
                sdata[i+1] = trimmed_mean_or_median(buf, count, trimfrac);
            else
                sdata[i+1] = 0.0;
        }
        g_free(buf);
    }
    sdata[0] = 0.0;
    for (k = 1; k < yres; k++)
        sdata[k] += sdata[k-1];

    slope_level_row_shifts(shifts);
    return shifts;
}

/************************** Documentation ****************************/

/**
 * SECTION:correct
 * @title: correct
 * @short_description: Data correction
 **/

/**
 * GwyCoordTransform2DFunc:
 * @x: Old x coordinate.
 * @y: Old y coordinate.
 * @px: Location to store new x coordinate.
 * @py: Location to store new y coordinate.
 * @user_data: User data passed to the caller function.
 *
 * The type of two-dimensional coordinate transform function.
 *
 * Since: 2.5
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
