/*
 *  $Id: arithmetic.c 24347 2021-10-12 13:39:24Z rsleza $
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
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/correct.h>
#include <libprocess/arithmetic.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

/* for compatibility checks */
#define EPSILON 5e-6

static gboolean
compatibility_check_common(GwyDataField *result,
                           GwyDataField *operand1,
                           GwyDataField *operand2)
{
    GwyDataCompatibilityFlags flags = GWY_DATA_COMPATIBILITY_RES;
    g_return_val_if_fail(GWY_IS_DATA_FIELD(result), FALSE);
    g_return_val_if_fail(!gwy_data_field_check_compatibility(result, operand1, flags), FALSE);
    g_return_val_if_fail(!gwy_data_field_check_compatibility(result, operand2, flags), FALSE);
    return TRUE;
}

/**
 * gwy_data_field_sum_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Sums two data fields.
 **/
void
gwy_data_field_sum_fields(GwyDataField *result,
                          GwyDataField *operand1,
                          GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint i, n;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = p[i] + q[i];

    if (CTEST(operand1, SUM) && CTEST(operand2, SUM)) {
        result->cached = CBIT(SUM);
        CVAL(result, SUM) = CVAL(operand1, SUM) + CVAL(operand2, SUM);
    }
    else
        gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_subtract_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Subtracts one data field from another.
 **/
void
gwy_data_field_subtract_fields(GwyDataField *result,
                               GwyDataField *operand1,
                               GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = p[i] - q[i];

    if (CTEST(operand1, SUM) && CTEST(operand2, SUM)) {
        result->cached = CBIT(SUM);
        CVAL(result, SUM) = CVAL(operand1, SUM) - CVAL(operand2, SUM);
    }
    else
        gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_multiply_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Multiplies two data fields.
 **/
void
gwy_data_field_multiply_fields(GwyDataField *result,
                               GwyDataField *operand1,
                               GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = p[i]*q[i];

    gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_divide_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Divides one data field with another.
 **/
void
gwy_data_field_divide_fields(GwyDataField *result,
                             GwyDataField *operand1,
                             GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = p[i]/q[i];

    gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_min_of_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Finds point-wise maxima of two data fields.
 **/
void
gwy_data_field_min_of_fields(GwyDataField *result,
                             GwyDataField *operand1,
                             GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = fmin(p[i], q[i]);

    if (CTEST(operand1, MIN) && CTEST(operand2, MIN)) {
        result->cached = CBIT(MIN);
        CVAL(result, MIN) = MIN(CVAL(operand1, MIN), CVAL(operand2, MIN));
    }
    else
        gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_max_of_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Finds point-wise minima of two data fields.
 **/
void
gwy_data_field_max_of_fields(GwyDataField *result,
                             GwyDataField *operand1,
                             GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise. */
    for (i = 0; i < n; i++)
        r[i] = fmax(p[i], q[i]);

    if (CTEST(operand1, MAX) && CTEST(operand2, MAX)) {
        result->cached = CBIT(MAX);
        CVAL(result, MAX) = MAX(CVAL(operand1, MAX), CVAL(operand2, MAX));
    }
    else
        gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_hypot_of_fields:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @operand1: First data field operand.
 * @operand2: Second data field operand.
 *
 * Finds point-wise hypotenuse of two data fields.
 *
 * Since: 2.31
 **/
void
gwy_data_field_hypot_of_fields(GwyDataField *result,
                               GwyDataField *operand1,
                               GwyDataField *operand2)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(r,p,q,n)
#endif
    for (i = 0; i < n; i++)
        r[i] = hypot(p[i], q[i]);

    gwy_data_field_invalidate(result);
}

/**
 * gwy_data_field_linear_combination:
 * @result: A data field to put the result to.  May be one of @operand1, @operand2.
 * @constant: Constant term to add to the result.
 * @operand1: First data field operand.
 * @coeff1: Factor to multiply the first operand with.
 * @operand2: Second data field operand.
 * @coeff2: Factor to multiply the second operand with.
 *
 * Computes point-wise general linear combination of two data fields.
 *
 * Since: 2.59
 **/
void
gwy_data_field_linear_combination(GwyDataField *result,
                                  gdouble coeff1,
                                  GwyDataField *operand1,
                                  gdouble coeff2,
                                  GwyDataField *operand2,
                                  gdouble constant)
{
    gdouble *p, *q, *r;
    gint n, i;

    if (!compatibility_check_common(result, operand1, operand2))
        return;

    r = result->data;
    p = operand1->data;
    q = operand2->data;
    n = result->xres * result->yres;
    /* Too trivial to parallelise? */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(r,p,q,n,constant,coeff1,coeff2)
#endif
    for (i = 0; i < n; i++)
        r[i] = coeff1*p[i] + coeff2*q[i] + constant;

    gwy_data_field_invalidate(result);
}

static void
check_basic_properties(gint res1, gint res2,
                       gdouble real1, gdouble real2,
                       GwyDataCompatibilityFlags check,
                       GwyDataCompatibilityFlags *result)
{
    /* Resolution */
    if ((check & GWY_DATA_COMPATIBILITY_RES) && !(*result & GWY_DATA_COMPATIBILITY_RES)) {
        if (res1 != res2)
            *result |= GWY_DATA_COMPATIBILITY_RES;
    }

    /* Real size */
    if (check & GWY_DATA_COMPATIBILITY_REAL && !(*result & GWY_DATA_COMPATIBILITY_REAL)) {
        /* Keeps the condition in negative form to catch NaNs and odd values
         * as incompatible. */
        if (!(fabs(log(real1/real2)) <= EPSILON))
            *result |= GWY_DATA_COMPATIBILITY_REAL;
    }

    /* Measure */
    if (check & GWY_DATA_COMPATIBILITY_MEASURE && !(*result & GWY_DATA_COMPATIBILITY_MEASURE)) {
        if (!(fabs(log(real1/res1*res2/real2)) <= EPSILON))
            *result |= GWY_DATA_COMPATIBILITY_MEASURE;
    }
}

/* Check if two SI Units are equal, accepting also NULLs and considering them
 * equal to empty units. */
static gboolean
units_are_equal(GwySIUnit *unit1, GwySIUnit *unit2)
{
    if (unit1 == unit2)
        return TRUE;
    if (!unit1)
        return gwy_si_unit_equal_string(unit2, NULL);
    if (!unit2)
        return gwy_si_unit_equal_string(unit1, NULL);
    return gwy_si_unit_equal(unit1, unit2);
}

/**
 * gwy_data_field_check_compatibility:
 * @data_field1: A data field.
 * @data_field2: Another data field.
 * @check: The compatibility tests to perform.
 *
 * Checks whether two data fields are compatible.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if data fields are not
 *          compatible.
 **/
GwyDataCompatibilityFlags
gwy_data_field_check_compatibility(GwyDataField *data_field1,
                                   GwyDataField *data_field2,
                                   GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field1), check);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field2), check);

    check_basic_properties(data_field1->xres, data_field2->xres, data_field1->xreal, data_field2->xreal,
                           check, &result);
    check_basic_properties(data_field1->yres, data_field2->yres, data_field1->yreal, data_field2->yreal,
                           check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(data_field1->si_unit_xy, data_field2->si_unit_xy))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    if ((check & GWY_DATA_COMPATIBILITY_VALUE) && !units_are_equal(data_field1->si_unit_z, data_field2->si_unit_z))
        result |= GWY_DATA_COMPATIBILITY_VALUE;

    return result;
}

/**
 * gwy_data_line_check_compatibility:
 * @data_line1: A data line.
 * @data_line2: Another data line.
 * @check: The compatibility tests to perform.
 *
 * Checks whether two data lines are compatible.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if data lines are not
 *          compatible.
 **/
GwyDataCompatibilityFlags
gwy_data_line_check_compatibility(GwyDataLine *data_line1,
                                  GwyDataLine *data_line2,
                                  GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_DATA_LINE(data_line1), check);
    g_return_val_if_fail(GWY_IS_DATA_LINE(data_line2), check);

    check_basic_properties(data_line1->res, data_line2->res, data_line1->real, data_line2->real, check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(data_line1->si_unit_x, data_line2->si_unit_x))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    if ((check & GWY_DATA_COMPATIBILITY_VALUE) && !units_are_equal(data_line1->si_unit_y, data_line2->si_unit_y))
        result |= GWY_DATA_COMPATIBILITY_VALUE;

    return result;
}

static gboolean
data_lines_equal(GwyDataLine *data_line1,
                 GwyDataLine *data_line2)
{
    gint res, i;
    const gdouble *data1, *data2;

    if (gwy_data_line_check_compatibility(data_line1, data_line2,
                                          GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_VALUE))
        return FALSE;

    res = data_line1->res;
    data1 = data_line1->data;
    data2 = data_line2->data;
    for (i = 0; i < res; i++) {
        /* FIXME: Add a tolerance here? */
        if (data2[i] != data1[i])
            return FALSE;
    }

    return TRUE;
}

/**
 * gwy_brick_check_compatibility:
 * @brick1: A data brick.
 * @brick2: Another data brick.
 * @check: The compatibility tests to perform.
 *
 * Checks whether two data bricks are compatible.
 *
 * Real dimensions are checked without regard to calibration.  Calibrations are considered compatible if either both
 * exist and are identical or none exists.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if bricks are not
 *          compatible.
 *
 * Since: 2.51
 **/
GwyDataCompatibilityFlags
gwy_brick_check_compatibility(GwyBrick *brick1,
                              GwyBrick *brick2,
                              GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_BRICK(brick1), check);
    g_return_val_if_fail(GWY_IS_BRICK(brick2), check);

    check_basic_properties(brick1->xres, brick2->xres, brick1->xreal, brick2->xreal, check, &result);
    check_basic_properties(brick1->yres, brick2->yres, brick1->yreal, brick2->yreal, check, &result);
    check_basic_properties(brick1->zres, brick2->zres, brick1->zreal, brick2->zreal, check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL)
        && (!units_are_equal(brick1->si_unit_x, brick2->si_unit_x)
            || !units_are_equal(brick1->si_unit_y, brick2->si_unit_y)
            || !units_are_equal(brick1->si_unit_z, brick2->si_unit_z)))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    if (check & GWY_DATA_COMPATIBILITY_VALUE && !units_are_equal(brick1->si_unit_w, brick2->si_unit_w))
        result |= GWY_DATA_COMPATIBILITY_VALUE;

    /* Z-calibration. */
    if (check & GWY_DATA_COMPATIBILITY_AXISCAL) {
        GwyDataLine *zcal1 = gwy_brick_get_zcalibration(brick1);
        GwyDataLine *zcal2 = gwy_brick_get_zcalibration(brick2);
        if ((zcal1 && !zcal2) || (!zcal1 && zcal2))
            result |= GWY_DATA_COMPATIBILITY_AXISCAL;
        else if (zcal1 && zcal2 && !data_lines_equal(zcal1, zcal2))
            result |= GWY_DATA_COMPATIBILITY_AXISCAL;
    }

    return result;
}

/**
 * gwy_lawn_check_compatibility:
 * @lawn1: A data lawn.
 * @lawn2: Another data lawn.
 * @check: The compatibility tests to perform.
 *
 * Checks whether two data lawns are compatible.
 *
 * Dimensions are checked only in the plane.  To check if the curve lengths match, use the
 * %GWY_DATA_COMPATIBILITY_CURVELEN flag.  Use %GWY_DATA_COMPATIBILITY_NCURVES to check if the two lawns have the
 * same number of curves.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if lawns are not
 *          compatible.
 *
 * Since: 2.60
 **/
GwyDataCompatibilityFlags
gwy_lawn_check_compatibility(GwyLawn *lawn1,
                             GwyLawn *lawn2,
                             GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_LAWN(lawn1), check);
    g_return_val_if_fail(GWY_IS_LAWN(lawn2), check);

    check_basic_properties(lawn1->xres, lawn2->xres, lawn1->xreal, lawn2->xreal, check, &result);
    check_basic_properties(lawn1->yres, lawn2->yres, lawn1->yreal, lawn2->yreal, check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(lawn1->si_unit_xy, lawn2->si_unit_xy))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    if (check & GWY_DATA_COMPATIBILITY_NCURVES && gwy_lawn_get_n_curves(lawn1) != gwy_lawn_get_n_curves(lawn2))
        result |= GWY_DATA_COMPATIBILITY_NCURVES;

    if (check & GWY_DATA_COMPATIBILITY_VALUE) {
        if (gwy_lawn_get_n_curves(lawn1) != gwy_lawn_get_n_curves(lawn2))
            result |= GWY_DATA_COMPATIBILITY_VALUE;
        else {
            gint i, n = gwy_lawn_get_n_curves(lawn1);

            for (i = 0; i < n; i++) {
                if (!units_are_equal(gwy_lawn_get_si_unit_curve(lawn1, i), gwy_lawn_get_si_unit_curve(lawn2, i))) {
                    result |= GWY_DATA_COMPATIBILITY_VALUE;
                    break;
                }
            }
        }
    }

    if (check & GWY_DATA_COMPATIBILITY_CURVELEN) {
        if (lawn1->xres != lawn2->xres || lawn1->yres != lawn2->yres)
            result |= GWY_DATA_COMPATIBILITY_CURVELEN;
        else {
            gint i, xres = lawn1->xres, yres = lawn1->yres;
            for (i = 0; i < xres*yres; i++) {
                if (gwy_lawn_get_curve_length(lawn1, i % xres, i/xres)
                    != gwy_lawn_get_curve_length(lawn2, i % xres, i/xres)) {
                    result |= GWY_DATA_COMPATIBILITY_CURVELEN;
                    break;
                }
            }
        }
    }

    return result;
}

/**
 * gwy_data_field_check_compatibility_with_brick_xy:
 * @data_field: A two-dimensional data field.
 * @brick: A three-dimensional data brick.
 * @check: The compatibility tests to perform.
 *
 * Checks whether a data field is compatible with brick XY-planes.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if the data objects are
 *          not compatible.
 *
 * Since: 2.51
 **/
GwyDataCompatibilityFlags
gwy_data_field_check_compatibility_with_brick_xy(GwyDataField *data_field,
                                                 GwyBrick *brick,
                                                 GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), check);
    g_return_val_if_fail(GWY_IS_BRICK(brick), check);

    check_basic_properties(data_field->xres, brick->xres, data_field->xreal, brick->xreal, check, &result);
    check_basic_properties(data_field->yres, brick->yres, data_field->yreal, brick->yreal, check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL)
        && (!units_are_equal(data_field->si_unit_xy, brick->si_unit_x)
            || !units_are_equal(data_field->si_unit_xy, brick->si_unit_y)))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    if (check & GWY_DATA_COMPATIBILITY_VALUE
        && !units_are_equal(data_field->si_unit_z, brick->si_unit_w))
        result |= GWY_DATA_COMPATIBILITY_VALUE;

    return result;
}

/**
 * gwy_data_line_check_compatibility_with_brick_z:
 * @data_line: A one-dimensional data line.
 * @brick: A three-dimensional data brick.
 * @check: The compatibility tests to perform.
 *
 * Checks whether a data line is compatible with brick Z-profiles.
 *
 * If @check includes %GWY_DATA_COMPATIBILITY_REAL or %GWY_DATA_COMPATIBILITY_LATERAL but not
 * %GWY_DATA_COMPATIBILITY_AXISCAL, @data_line is simply compared to @brick in the Z direction.
 *
 * If you include %GWY_DATA_COMPATIBILITY_AXISCAL and @brick has a Z-calibration data line, then the value range and
 * units of this data line are compared to @data_line.  This may not be very useful.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if the data objects are
 *          not compatible.
 *
 * Since: 2.51
 **/
GwyDataCompatibilityFlags
gwy_data_line_check_compatibility_with_brick_z(GwyDataLine *data_line,
                                               GwyBrick *brick,
                                               GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;
    GwyDataLine *zcal;

    g_return_val_if_fail(GWY_IS_DATA_LINE(data_line), check);
    g_return_val_if_fail(GWY_IS_BRICK(brick), check);

    if (check & GWY_DATA_COMPATIBILITY_VALUE && !units_are_equal(data_line->si_unit_y, brick->si_unit_w))
        result |= GWY_DATA_COMPATIBILITY_VALUE;

    /* Without zcalibration compare directly to the brick. */
    zcal = gwy_brick_get_zcalibration(brick);
    if (!zcal || !(check & GWY_DATA_COMPATIBILITY_AXISCAL)) {
        check_basic_properties(data_line->res, brick->xres, data_line->real, brick->xreal, check, &result);

        if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(data_line->si_unit_x, brick->si_unit_z))
            result |= GWY_DATA_COMPATIBILITY_LATERAL;

        return result;
    }

    /* With Z-calibration we compare to @zcal.  The *values* of @zcal are the same thing as *coordinates* of
     * @data_line.  So this is a mess and possibly not useful at all. */
    g_assert(zcal->res == brick->zres);
    check_basic_properties(data_line->res, zcal->res, data_line->real, zcal->data[zcal->res-1] - zcal->data[0],
                           check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(data_line->si_unit_x, zcal->si_unit_y))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    return result;
}

/**
 * gwy_data_field_check_compatibility_with_lawn_xy:
 * @data_field: A two-dimensional data field.
 * @lawn: A lawn curve map object.
 * @check: The compatibility tests to perform.
 *
 * Checks whether a data field is compatible with lawn in the XY-plane.
 *
 * Returns: Zero if all tested properties are compatible.  Flags corresponding to failed tests if the data objects are
 *          not compatible.
 *
 * Since: 2.60
 **/
GwyDataCompatibilityFlags
gwy_data_field_check_compatibility_with_lawn(GwyDataField *data_field,
                                             GwyLawn *lawn,
                                             GwyDataCompatibilityFlags check)
{
    GwyDataCompatibilityFlags result = 0;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), check);
    g_return_val_if_fail(GWY_IS_LAWN(lawn), check);

    check_basic_properties(data_field->xres, lawn->xres, data_field->xreal, lawn->xreal, check, &result);
    check_basic_properties(data_field->yres, lawn->yres, data_field->yreal, lawn->yreal, check, &result);

    if ((check & GWY_DATA_COMPATIBILITY_LATERAL) && !units_are_equal(data_field->si_unit_xy, lawn->si_unit_xy))
        result |= GWY_DATA_COMPATIBILITY_LATERAL;

    return result;
}

static inline void
fill_block(gdouble *data, guint len, gdouble value)
{
    while (len--)
        *(data++) = value;
}

static inline void
row_extend_base(const gdouble *in, gdouble *out,
                guint *pos, guint *width, guint res,
                guint *extend_left, guint *extend_right)
{
    guint e2r, e2l;

    // Expand the ROI to the right as far as possible
    e2r = MIN(*extend_right, res - (*pos + *width));
    *width += e2r;
    *extend_right -= e2r;

    // Expand the ROI to the left as far as possible
    e2l = MIN(*extend_left, *pos);
    *width += e2l;
    *extend_left -= e2l;
    *pos -= e2l;

    // Direct copy of the ROI
    gwy_assign(out + *extend_left, in + *pos, *width);
}

static void
row_extend_mirror(const gdouble *in, gdouble *out,
                  guint pos, guint width, guint res,
                  guint extend_left, guint extend_right,
                  G_GNUC_UNUSED gdouble value)
{
    guint res2 = 2*res, k0, j;
    gdouble *out2;
    row_extend_base(in, out, &pos, &width, res, &extend_left, &extend_right);
    // Forward-extend
    out2 = out + extend_left + width;
    for (j = 0; j < extend_right; j++, out2++) {
        guint k = (pos + width + j) % res2;
        *out2 = (k < res) ? in[k] : in[res2-1 - k];
    }
    // Backward-extend
    k0 = (extend_left/res2 + 1)*res2;
    out2 = out + extend_left-1;
    for (j = 1; j <= extend_left; j++, out2--) {
        guint k = (k0 + pos - j) % res2;
        *out2 = (k < res) ? in[k] : in[res2-1 - k];
    }
}

static void
row_extend_periodic(const gdouble *in, gdouble *out,
                    guint pos, guint width, guint res,
                    guint extend_left, guint extend_right,
                    G_GNUC_UNUSED gdouble value)
{
    guint k0, j;
    gdouble *out2;
    row_extend_base(in, out, &pos, &width, res, &extend_left, &extend_right);
    // Forward-extend
    out2 = out + extend_left + width;
    for (j = 0; j < extend_right; j++, out2++) {
        guint k = (pos + width + j) % res;
        *out2 = in[k];
    }
    // Backward-extend
    k0 = (extend_left/res + 1)*res;
    out2 = out + extend_left-1;
    for (j = 1; j <= extend_left; j++, out2--) {
        guint k = (k0 + pos - j) % res;
        *out2 = in[k];
    }
}

static void
row_extend_border(const gdouble *in, gdouble *out,
                  guint pos, guint width, guint res,
                  guint extend_left, guint extend_right,
                  G_GNUC_UNUSED gdouble value)
{
    row_extend_base(in, out, &pos, &width, res, &extend_left, &extend_right);
    // Forward-extend
    fill_block(out + extend_left + width, extend_right, in[res-1]);
    // Backward-extend
    fill_block(out, extend_left, in[0]);
}

static void
row_extend_fill(const gdouble *in, gdouble *out,
                guint pos, guint width, guint res,
                guint extend_left, guint extend_right,
                gdouble value)
{
    row_extend_base(in, out, &pos, &width, res, &extend_left, &extend_right);
    // Forward-extend
    fill_block(out + extend_left + width, extend_right, value);
    // Backward-extend
    fill_block(out, extend_left, value);
}

static inline void
rect_extend_base(const gdouble *in, guint inrowstride,
                 gdouble *out, guint outrowstride,
                 guint xpos, guint *ypos,
                 guint width, guint *height,
                 guint xres, guint yres,
                 guint extend_left, guint extend_right,
                 guint *extend_up, guint *extend_down,
                 RowExtendFunc extend_row, gdouble fill_value)
{
    guint e2r, e2l, i, h;

    // Expand the ROI down as far as possible
    e2r = MIN(*extend_down, yres - (*ypos + *height));
    *height += e2r;
    *extend_down -= e2r;

    // Expand the ROI up as far as possible
    e2l = MIN(*extend_up, *ypos);
    *height += e2l;
    *extend_up -= e2l;
    *ypos -= e2l;

    // Row-wise extension within the vertical range of the ROI
    h = *height;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out,h,xpos,ypos,xres,width,inrowstride,outrowstride,extend_up,extend_left,extend_right,fill_value,extend_row)
#endif
    for (i = 0; i < h; i++) {
        extend_row(in + (*ypos + i)*inrowstride,
                   out + (*extend_up + i)*outrowstride,
                   xpos, width, xres, extend_left, extend_right, fill_value);
    }
}

static void
rect_extend_mirror(const gdouble *in, guint inrowstride,
                   gdouble *out, guint outrowstride,
                   guint xpos, guint ypos,
                   guint width, guint height,
                   guint xres, guint yres,
                   guint extend_left, guint extend_right,
                   guint extend_up, guint extend_down,
                   G_GNUC_UNUSED gdouble value)
{
    guint yres2, i, k0;
    gdouble *out2;
    rect_extend_base(in, inrowstride, out, outrowstride,
                     xpos, &ypos, width, &height, xres, yres,
                     extend_left, extend_right, &extend_up, &extend_down,
                     &row_extend_mirror, value);
    // Forward-extend
    yres2 = 2*yres;
    out2 = out + outrowstride*(extend_up + height);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,yres2,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_down,value)
#endif
    for (i = 0; i < extend_down; i++) {
        guint k = (ypos + height + i) % yres2;
        if (k >= yres)
            k = yres2-1 - k;
        row_extend_mirror(in + k*inrowstride, out2 + i*outrowstride,
                          xpos, width, xres, extend_left, extend_right, value);
    }
    // Backward-extend
    k0 = (extend_up/yres2 + 1)*yres2;
    out2 = out + outrowstride*extend_up;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,yres2,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_up,extend_down,k0,value)
#endif
    for (i = 1; i <= extend_up; i++) {
        guint k = (k0 + ypos - i) % yres2;
        if (k >= yres)
            k = yres2-1 - k;
        row_extend_mirror(in + k*inrowstride, out2 - i*outrowstride,
                          xpos, width, xres, extend_left, extend_right, value);
    }
}

static void
rect_extend_periodic(const gdouble *in, guint inrowstride,
                     gdouble *out, guint outrowstride,
                     guint xpos, guint ypos,
                     guint width, guint height,
                     guint xres, guint yres,
                     guint extend_left, guint extend_right,
                     guint extend_up, guint extend_down,
                     G_GNUC_UNUSED gdouble value)
{
    guint i, k0;
    gdouble *out2;
    rect_extend_base(in, inrowstride, out, outrowstride,
                     xpos, &ypos, width, &height, xres, yres,
                     extend_left, extend_right, &extend_up, &extend_down,
                     &row_extend_periodic, value);
    // Forward-extend
    out2 = out + outrowstride*(extend_up + height);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_down,value)
#endif
    for (i = 0; i < extend_down; i++) {
        guint k = (ypos + height + i) % yres;
        row_extend_periodic(in + k*inrowstride, out2 + i*outrowstride,
                            xpos, width, xres, extend_left, extend_right, value);
    }
    // Backward-extend
    k0 = (extend_up/yres + 1)*yres;
    out2 = out + outrowstride*extend_up;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_up,extend_down,k0,value)
#endif
    for (i = 1; i <= extend_up; i++) {
        guint k = (k0 + ypos - i) % yres;
        row_extend_periodic(in + k*inrowstride, out2 - i*outrowstride,
                            xpos, width, xres, extend_left, extend_right, value);
    }
}

static void
rect_extend_border(const gdouble *in, guint inrowstride,
                   gdouble *out, guint outrowstride,
                   guint xpos, guint ypos,
                   guint width, guint height,
                   guint xres, guint yres,
                   guint extend_left, guint extend_right,
                   guint extend_up, guint extend_down,
                   G_GNUC_UNUSED gdouble value)
{
    guint i;
    gdouble *out2;
    rect_extend_base(in, inrowstride, out, outrowstride,
                     xpos, &ypos, width, &height, xres, yres,
                     extend_left, extend_right, &extend_up, &extend_down,
                     &row_extend_border, value);
    // Forward-extend
    out2 = out + outrowstride*(extend_up + height);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_down,value)
#endif
    for (i = 0; i < extend_down; i++)
        row_extend_border(in + (yres-1)*inrowstride, out2 + i*outrowstride,
                          xpos, width, xres, extend_left, extend_right, value);
    // Backward-extend
    out2 = out + outrowstride*extend_up;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(in,out2,xres,yres,xpos,ypos,width,height,inrowstride,outrowstride,extend_left,extend_right,extend_up,extend_down,value)
#endif
    for (i = 1; i <= extend_up; i++)
        row_extend_border(in, out2 - i*outrowstride,
                          xpos, width, xres, extend_left, extend_right, value);
}

static void
rect_extend_fill(const gdouble *in, guint inrowstride,
                 gdouble *out, guint outrowstride,
                 guint xpos, guint ypos,
                 guint width, guint height,
                 guint xres, guint yres,
                 guint extend_left, guint extend_right,
                 guint extend_up, guint extend_down,
                 gdouble value)
{
    guint i;
    gdouble *out2;
    rect_extend_base(in, inrowstride, out, outrowstride,
                     xpos, &ypos, width, &height, xres, yres,
                     extend_left, extend_right, &extend_up, &extend_down,
                     &row_extend_fill, value);
    // Forward-extend
    out2 = out + outrowstride*(extend_up + height);
    for (i = 0; i < extend_down; i++, out2 += outrowstride)
        fill_block(out2, extend_left + width + extend_right, value);
    // Backward-extend
    out2 = out + outrowstride*(extend_up - 1);
    for (i = 1; i <= extend_up; i++, out2 -= outrowstride)
        fill_block(out2, extend_left + width + extend_right, value);
}

static inline void
rect_extend_laplace(const gdouble *in, guint inrowstride,
                    gdouble *out, guint outrowstride,
                    guint xpos, guint ypos,
                    guint width, guint height,
                    guint xres, guint yres,
                    guint extend_left, guint extend_right,
                    guint extend_up, guint extend_down,
                    G_GNUC_UNUSED gdouble value)
{
    GwyDataField *mask, *workspace;
    guint e2u, e2d, e2r, e2l, i, extxres, extyres;

    /* Expand the ROI down as far as possible */
    e2d = MIN(extend_down, yres - (ypos + height));
    height += e2d;
    extend_down -= e2d;

    /* Expand the ROI up as far as possible */
    e2u = MIN(extend_up, ypos);
    height += e2u;
    extend_up -= e2u;
    ypos -= e2u;

    /* Expand the ROI to the right as far as possible */
    e2r = MIN(extend_right, xres - (xpos + width));
    width += e2r;
    extend_right -= e2r;

    /* Expand the ROI to the left as far as possible */
    e2l = MIN(extend_left, xpos);
    width += e2l;
    extend_left -= e2l;
    xpos -= e2l;

    if (extend_down + extend_up + extend_right + extend_left == 0) {
        /* Direct copy of the ROI */
        for (i = 0; i < height; i++)
            gwy_assign(out + (extend_up + i)*outrowstride + extend_left, in + (ypos + i)*inrowstride + xpos, width);
        return;
    }

    extxres = width + extend_left + extend_right;
    extyres = height + extend_up + extend_down;
    mask = gwy_data_field_new(extxres, extyres, 1.0, 1.0, FALSE);
    gwy_data_field_fill(mask, 1.0);
    gwy_data_field_area_clear(mask, extend_left, extend_up, width, height);

    /* NB: We cannot recycle out in any manner because it has different
     * rowstride than extxres! */
    workspace = gwy_data_field_new(extxres, extyres, 1.0, 1.0, FALSE);

    for (i = 0; i < height; i++)
        gwy_assign(workspace->data + (extend_up + i)*extxres + extend_left, in + (ypos + i)*inrowstride + xpos, width);
    gwy_data_field_laplace_solve(workspace, mask, -1, 0.5);
    for (i = 0; i < extyres; i++)
        gwy_assign(out + i*outrowstride, workspace->data + i*extxres, extxres);

    g_object_unref(workspace);
    g_object_unref(mask);
}

RowExtendFunc
_gwy_get_row_extend_func(GwyExteriorType exterior)
{
    if (exterior == GWY_EXTERIOR_FIXED_VALUE)
        return &row_extend_fill;
    if (exterior == GWY_EXTERIOR_BORDER_EXTEND)
        return &row_extend_border;
    if (exterior == GWY_EXTERIOR_MIRROR_EXTEND)
        return &row_extend_mirror;
    if (exterior == GWY_EXTERIOR_PERIODIC)
        return &row_extend_periodic;
    g_return_val_if_reached(NULL);
}

RectExtendFunc
_gwy_get_rect_extend_func(GwyExteriorType exterior)
{
    if (exterior == GWY_EXTERIOR_FIXED_VALUE)
        return &rect_extend_fill;
    if (exterior == GWY_EXTERIOR_BORDER_EXTEND)
        return &rect_extend_border;
    if (exterior == GWY_EXTERIOR_MIRROR_EXTEND)
        return &rect_extend_mirror;
    if (exterior == GWY_EXTERIOR_PERIODIC)
        return &rect_extend_periodic;
    if (exterior == GWY_EXTERIOR_LAPLACE)
        return &rect_extend_laplace;
    g_return_val_if_reached(NULL);
}

/**
 * gwy_data_field_extend:
 * @data_field: A two-dimensional data field.
 * @left: Number of pixels to extend to the left (towards lower column indices).
 * @right: Number of pixels to extend to the right (towards higher column indices).
 * @up: Number of pixels to extend up (towards lower row indices).
 * @down: Number of pixels to extend down (towards higher row indices).
 * @exterior: Exterior pixels handling.
 * @fill_value: The value to use with %GWY_EXTERIOR_FIXED_VALUE exterior.
 * @keep_offsets: %TRUE to set the X and Y offsets of the new field using @field offsets.  %FALSE to set offsets of
 *                the new field to zeroes.
 *
 * Creates a new data field by extending another data field using the specified method of exterior handling.
 *
 * Returns: A newly created data field.
 *
 * Since: 2.36
 **/
GwyDataField*
gwy_data_field_extend(GwyDataField *data_field,
                      guint left, guint right,
                      guint up, guint down,
                      GwyExteriorType exterior,
                      gdouble fill_value,
                      gboolean keep_offsets)
{
    GwyDataField *target;
    RectExtendFunc extend_rect;
    guint col = 0, row = 0, width, height;
    gdouble dx, dy;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);

    extend_rect = _gwy_get_rect_extend_func(exterior);
    g_return_val_if_fail(extend_rect, NULL);

    width = data_field->xres;
    height = data_field->yres;
    target = gwy_data_field_new(width + left + right, height + up + down, 1.0, 1.0, FALSE);
    extend_rect(data_field->data, data_field->xres, target->data, target->xres,
                col, row, width, height, data_field->xres, data_field->yres,
                left, right, up, down, fill_value);

    dx = data_field->xreal/data_field->xres;
    dy = data_field->yreal/data_field->yres;
    gwy_data_field_set_xreal(target, (width + left + right)*dx);
    gwy_data_field_set_yreal(target, (height + up + down)*dy);
    if (keep_offsets) {
        gwy_data_field_set_xoffset(target, data_field->xoff + col*dx - left*dx);
        gwy_data_field_set_yoffset(target, data_field->yoff + row*dy - up*dy);
    }
    else {
        gwy_data_field_set_xoffset(target, 0.0);
        gwy_data_field_set_yoffset(target, 0.0);
    }
    gwy_data_field_copy_units(data_field, target);

    return target;
}

/************************** Documentation ****************************/

/**
 * SECTION:arithmetic
 * @title: arithmetic
 * @short_description: Arithmetic opetations on data fields
 *
 * Data arithmetic functions perform simple operations combining several data fields.  Their sizes have to be
 * size-compatible, i.e. gwy_data_field_check_compatibility(operand1, operand2, GWY_DATA_COMPATIBILITY_RES) must pass
 * and the same must hold for the data field to store the result to.
 *
 * Functions gwy_data_field_check_compatibility(), gwy_data_line_check_compatibility() and
 * gwy_brick_check_compatibility() simplify testing compatibility of data fields, lines and bricks, respectively.
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
