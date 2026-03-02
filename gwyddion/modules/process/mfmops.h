/*
 *  $Id: mfmops.h 24657 2022-03-10 13:16:25Z yeti-dn $
 *  Copyright (C) 2017-2018 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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

#ifndef __GWY_MFMOPS_H__
#define __GWY_MFMOPS_H__

#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libprocess/mfm.h>

#define MU_0 1.256637061435917295e-6

#define MFM_DIMENSION_ARGS_INIT \
    { 256, 256, 5.0, (gpointer)"m", NULL, -9, 0, FALSE, FALSE }



G_GNUC_UNUSED
static gdouble
mfm_factor(GwyMFMGradientType type, gdouble dx, gdouble dy)
{
    if (type == GWY_MFM_GRADIENT_MFM) 
        return 1.0/MU_0;

    if (type == GWY_MFM_GRADIENT_MFM_AREA)
        return 1.0/(MU_0*dx*dy);

    return 1.0;
}

G_GNUC_UNUSED
static gchar*
mfm_unit(GwyMFMGradientType type)
{
    if (type == GWY_MFM_GRADIENT_MFM) 
        return g_strdup("A^2/m");

    if (type == GWY_MFM_GRADIENT_MFM_AREA)
        return g_strdup("A^2/m^3");

    return g_strdup("N/m");
}


G_GNUC_UNUSED
static void
gwy_data_field_mfm_phase_to_force_gradient(GwyDataField *dfield,
                                           gdouble spring_constant,
                                           gdouble quality,
                                           GwyMFMGradientType type)
{
    gdouble dx = gwy_data_field_get_dx(dfield);
    gdouble dy = gwy_data_field_get_dy(dfield);
    gdouble factor = spring_constant/(quality);

    gwy_data_field_multiply(dfield, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), mfm_unit(type));
}

G_GNUC_UNUSED
static void
gwy_data_field_mfm_frequency_shift_to_force_gradient(GwyDataField *dfield,
                                                     gdouble spring_constant,
                                                     gdouble base_frequency,
                                                     GwyMFMGradientType type)
{
    gdouble dx = gwy_data_field_get_dx(dfield);
    gdouble dy = gwy_data_field_get_dy(dfield);
    gdouble factor = 2*spring_constant/(base_frequency);

    gwy_data_field_multiply(dfield, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), mfm_unit(type));
}

G_GNUC_UNUSED
static void
gwy_data_field_mfm_amplitude_shift_to_force_gradient(GwyDataField *dfield,
                                                     gdouble spring_constant,
                                                     gdouble quality,
                                                     gdouble base_amplitude,
                                                     GwyMFMGradientType type)
{
    gdouble dx = gwy_data_field_get_dx(dfield);
    gdouble dy = gwy_data_field_get_dy(dfield);
    gdouble factor = 3*sqrt(3)*spring_constant/(2*base_amplitude*quality);

    gwy_data_field_multiply(dfield, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), mfm_unit(type));
}

// the same code for volume data, this should not be here repeated twice !!!

G_GNUC_UNUSED
static void
gwy_brick_mfm_phase_to_force_gradient(GwyBrick *brick,
                                      gdouble spring_constant,
                                      gdouble quality,
                                      GwyMFMGradientType type)
{
    gdouble dx = gwy_brick_get_xreal(brick)/gwy_brick_get_xres(brick);
    gdouble dy = gwy_brick_get_yreal(brick)/gwy_brick_get_yres(brick);
    gdouble factor = spring_constant/(quality);

    gwy_brick_multiply(brick, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(brick), mfm_unit(type));
}

G_GNUC_UNUSED
static void
gwy_brick_mfm_frequency_shift_to_force_gradient(GwyBrick *brick,
                                                gdouble spring_constant,
                                                gdouble base_frequency,
                                                GwyMFMGradientType type)
{
    gdouble dx = gwy_brick_get_xreal(brick)/gwy_brick_get_xres(brick);
    gdouble dy = gwy_brick_get_yreal(brick)/gwy_brick_get_yres(brick);
    gdouble factor = 2*spring_constant/(base_frequency);

    gwy_brick_multiply(brick, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(brick), mfm_unit(type));
}

G_GNUC_UNUSED
static void
gwy_brick_mfm_amplitude_shift_to_force_gradient(GwyBrick *brick,
                                                gdouble spring_constant,
                                                gdouble quality,
                                                gdouble base_amplitude,
                                                GwyMFMGradientType type)
{
    gdouble dx = gwy_brick_get_xreal(brick)/gwy_brick_get_xres(brick);
    gdouble dy = gwy_brick_get_yreal(brick)/gwy_brick_get_yres(brick);
    gdouble factor = 3*sqrt(3)*spring_constant/(2*base_amplitude*quality);

    gwy_brick_multiply(brick, factor*mfm_factor(type, dx, dy));
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(brick), mfm_unit(type));
}

#endif

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
