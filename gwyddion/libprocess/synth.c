/*
 *  $Id: synth.c 23460 2021-04-11 17:02:45Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>
#include <libprocess/filters.h>
#include <libprocess/synth.h>
#include "libgwyddion/gwyomp.h"

/* Iterating through square in a spiral fashion from the origin to preserve the centre conrer if it's randomly
 * generated.  Field @k holds the current index in the two-dimensional array.  */
typedef struct {
    gint n;
    gint i, j, k;
    gint istep, jstep;
    gint s, segmentend, ntotalstep;
} GrowingIter;

static inline void
growing_iter_init(GrowingIter *giter, guint n)
{
    giter->n = n;
    giter->j = giter->i = 0;
    giter->istep = 0;
    giter->jstep = -1;
    giter->ntotalstep = n*n;
    giter->segmentend = MIN(1, n*n);
    giter->s = 0;
    giter->k = (n/2 - giter->i)*n + (giter->j + n/2);
}

static inline gboolean
growing_iter_next(GrowingIter *giter)
{
    giter->i += giter->istep;
    giter->j += giter->jstep;
    giter->k = (giter->n/2 - giter->i)*giter->n + (giter->j + giter->n/2);
    giter->s++;
    if (giter->s == giter->segmentend) {
        if (giter->s == giter->ntotalstep)
            return FALSE;

        if (giter->i == giter->j + 1) {
            giter->istep = 1;
            giter->jstep = 0;
            giter->segmentend = 1 - 2*giter->i;
        }
        else if (giter->i == giter->j) {
            giter->istep = -1;
            giter->jstep = 0;
            giter->segmentend = 2*giter->i;
        }
        else if (giter->j > 0) {
            giter->istep = 0;
            giter->jstep = -1;
            giter->segmentend = 2*giter->j + 1;
        }
        else {
            giter->istep = 0;
            giter->jstep = 1;
            giter->segmentend = 2*giter->i;
        }
        giter->segmentend += giter->s;
        giter->segmentend = MIN(giter->segmentend, giter->ntotalstep);
    }
    return TRUE;
}

/* Fill a data field with uncorrelated random numbers in a growing fashion to
 * preserve the character of the noise even if the dimensions change */
static void
fill_displacement_map(GwyDataField *dfield, gdouble q, GRand *rng)
{
    GrowingIter giter;
    guint xres, yres;
    gdouble *data;

    xres = dfield->xres;
    yres = dfield->yres;
    data = dfield->data;
    g_return_if_fail(xres == yres);
    growing_iter_init(&giter, xres);

    do {
        data[giter.k] = q*(g_rand_double(rng) - 0.5);
    } while (growing_iter_next(&giter));
}

/**
 * gwy_data_field_synth_gaussian_displacement:
 * @data_field: A data field.
 * @sigma: Amplitude of displacement (in units of 1, usually meaning pixels).
 * @tau: Lateral scale of displacement variation (in pixels).
 * @rng: Random number generator.
 *
 * Fills a data field with correlated Gaussian noise, suitable as displacement field.
 *
 * The number of calls to @rng is unspecified; it will not be left in any defined state.  However, the noise is filled
 * from the data field centre.  Meaning that changing @tau will zoom in or out the Gaussian noise, preserving the
 * centr (whereas changing @sigma simply changes the amplitude).
 *
 * Since: 2.59
 **/
void
gwy_data_field_synth_gaussian_displacement(GwyDataField *data_field,
                                           gdouble sigma, gdouble tau,
                                           GRand *rng)
{
    GwyDataField *grid;
    guint gn, n, xres, yres;
    gdouble q, r;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(rng);

    xres = data_field->xres;
    yres = data_field->yres;
    n = MAX(xres, yres);
    q = 2.0*sigma*tau;
    if (!q) {
        gwy_data_field_clear(data_field);
        return;
    }

    if (tau <= 1.0) {
        if (xres == yres) {
            fill_displacement_map(data_field, q, rng);
            gwy_data_field_filter_gaussian(data_field, tau);
            return;
        }

        grid = gwy_data_field_new(n, n, 1.0, 1.0, FALSE);
        fill_displacement_map(grid, q, rng);
        gwy_data_field_filter_gaussian(grid, tau);
        gwy_data_field_area_copy(grid, data_field, (n - xres)/2, (n - yres)/2, xres, yres, 0, 0);
        g_object_unref(grid);
        return;
    }

    gn = GWY_ROUND(1.0/tau*n);
    gn = MAX(gn, 2);
    r = (gdouble)gn/n;
    grid = gwy_data_field_new(gn, gn, 1.0, 1.0, FALSE);
    fill_displacement_map(grid, q*r, rng);
    gwy_data_field_filter_gaussian(grid, r*tau);
    gwy_data_field_resample(grid, n, n, GWY_INTERPOLATION_KEY);
    gwy_data_field_area_copy(grid, data_field, (n - xres)/2, (n - yres)/2, xres, yres, 0, 0);
    g_object_unref(grid);
}

/************************** Documentation ****************************/

/**
 * SECTION:synth
 * @title: synth
 * @short_description: Synthetic data generation
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
