/*
 *  $Id: hough.c 24839 2022-05-27 15:09:02Z yeti-dn $
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

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/hough.h>

static void bresenhams_line           (GwyDataField *dfield,
                                       gint x1,
                                       gint x2,
                                       gint y1_,
                                       gint y2_,
                                       gdouble value);
static void bresenhams_line_polar     (GwyDataField *dfield,
                                       gdouble rho,
                                       gdouble theta,
                                       gdouble value);
static void bresenhams_circle         (GwyDataField *dfield,
                                       gdouble r,
                                       gint col,
                                       gint row,
                                       gdouble value);
static void bresenhams_circle_gradient(GwyDataField *dfield,
                                       gdouble r,
                                       gint col,
                                       gint row,
                                       gdouble value,
                                       gdouble angle);

static void
add_point(GwyDataField *result,
          gint rho, gint theta,
          gdouble value, gint hsize)
{
    gint col, row;
    gdouble *rdata, coeff;

    rdata = gwy_data_field_get_data(result);
    for (col = MAX(0, rho-hsize); col < MIN(result->xres, rho+hsize); col++) {
        for (row = MAX(0, theta-hsize); row < MIN(result->yres, theta+hsize); row++) {
            if (hsize)
                coeff = 1 + hypot(col-rho, row-theta);
            else
                coeff = 1;
            rdata[col + row*result->xres] += value/coeff;
        }
    }
}

void
gwy_data_field_hough_line(GwyDataField *dfield,
                          GwyDataField *x_gradient,
                          GwyDataField *y_gradient,
                          GwyDataField *result,
                          gint hwidth,
                          gboolean overlapping)
{
    gint k, col, row, xres, yres, rxres, ryres;
    gdouble rho, theta, rhostep, thetastep, *data, gradangle = 0;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    rxres = gwy_data_field_get_xres(result); /*rho*/
    ryres = gwy_data_field_get_yres(result); /*theta*/

    if ((x_gradient && xres != gwy_data_field_get_xres(x_gradient))
        || (x_gradient && yres != gwy_data_field_get_yres(x_gradient))
        || (y_gradient && xres != gwy_data_field_get_xres(y_gradient))
        || (y_gradient && yres != gwy_data_field_get_yres(y_gradient))) {
        g_warning("Hough: input fields must be of same size (or null).\n");
        return;
    }


    if (overlapping)
        thetastep = 2.0*G_PI/ryres;
    else
        thetastep = G_PI/ryres;
    rhostep = 2.0*hypot(xres, yres)/rxres;

    gwy_data_field_clear(result);
    data = dfield->data;
    for (col = 0; col < xres; col++) {
        for (row = 0; row < yres; row++) {
            if (dfield->data[col + row*xres] > 0.0) {
                if (x_gradient && y_gradient) {
                    gradangle = atan2(y_gradient->data[col + row*xres], x_gradient->data[col + row*xres]);
                    if (gradangle < 0.0)
                        gradangle += G_PI;
                    if (!overlapping)
                        gradangle += G_PI/4;
                }
                for (k = 0; k < result->yres; k++) {
                    theta = k*thetastep;
                    if (!overlapping)
                        theta += G_PI/4;

                    /*
                    threshold = 1.0;
                    if (data[col + row*xres]) printf("%g %g\n", theta, gradangle);
                    if (x_gradient && y_gradient && !(fabs(theta-gradangle)<threshold)) continue;*/

                    rho = col*cos(theta) + row*sin(theta);
                    add_point(result, (gint)((rho/rhostep) + rxres/2.0), k, data[col + row*xres], hwidth);
                }
            }
        }
    }
    gwy_data_field_set_xreal(result, 2.0*hypot(xres, yres));
    if (!overlapping)
        gwy_data_field_set_yreal(result, 2.0*G_PI);
    else
        gwy_data_field_set_yreal(result, G_PI);

    gwy_data_field_invalidate(result);
}

void
gwy_data_field_hough_line_strenghten(GwyDataField *dfield,
                                     GwyDataField *x_gradient,
                                     GwyDataField *y_gradient,
                                     gint hwidth,
                                     gdouble threshold)
{
    GwyDataField *result, *water;
    gdouble hmax, hmin, threshval, zdata[20];
    gint i;
    gdouble xdata[20], ydata[20];
    gdouble dx;

    result = gwy_data_field_new(3*hypot(dfield->xres, dfield->yres), 360, 0, 10, FALSE);

    gwy_data_field_hough_line(dfield, x_gradient, y_gradient, result, hwidth, FALSE);

    water = gwy_data_field_duplicate(result);
    gwy_data_field_grains_splash_water(result, water, 2,
                                       0.005*(gwy_data_field_get_max(result) - gwy_data_field_get_min(result)));

    gwy_debug("%d %d, %g, %g",
              gwy_data_field_get_xres(result),
              gwy_data_field_get_yres(result),
              gwy_data_field_get_min(result),
              gwy_data_field_get_max(result));

    gwy_data_field_get_min_max(water, &hmin, &hmax);

    threshval = hmin + (hmax - hmin)*threshold;
    gwy_data_field_get_local_maxima_list(water, xdata, ydata, zdata, 20, 10, threshval, TRUE);
    dx = gwy_data_field_get_dx(result);
    for (i = 0; i < 20; i++) {
        if (zdata[i] > threshval) {
            gwy_debug("point: %g %g (of %d %d), xreal: %g  yreal: %g\n",
                      xdata[i], ydata[i], result->xres, result->yres, result->xreal, result->yreal);
            bresenhams_line_polar(dfield, xdata[i]*dx - result->xreal/2.0,
                                  ydata[i]*G_PI/result->yres + G_PI/4,
                                  1);
        }
    }
    g_object_unref(water);
}

void
gwy_data_field_hough_circle(GwyDataField *dfield,
                            GwyDataField *x_gradient,
                            GwyDataField *y_gradient,
                            GwyDataField *result,
                            gdouble radius)
{
    gint col, row, xres, yres;
    gdouble angle = 0.0;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);

    if ((x_gradient && xres != gwy_data_field_get_xres(x_gradient))
        || (x_gradient && yres != gwy_data_field_get_yres(x_gradient))
        || (y_gradient && xres != gwy_data_field_get_xres(y_gradient))
        || (y_gradient && yres != gwy_data_field_get_yres(y_gradient))) {
        g_warning("Hough: input fields must be of same size (or null).\n");
        return;
    }

    gwy_data_field_fill(result, 0);
    for (col = 0; col < xres; col++) {
        for (row = 0; row < yres; row++) {
            if (dfield->data[col + row*xres] > 0) {
                if (x_gradient && y_gradient)
                    angle = atan2(y_gradient->data[col + row*xres], x_gradient->data[col + row*xres]);

                if (x_gradient && y_gradient)
                    bresenhams_circle_gradient(result, radius, col, row, 1, angle);
                else
                    bresenhams_circle(result, radius, col, row, 1);
            }
        }
    }

    gwy_data_field_invalidate(result);
}

void
gwy_data_field_hough_circle_strenghten(GwyDataField *dfield,
                                       GwyDataField *x_gradient,
                                       GwyDataField *y_gradient,
                                       gdouble radius,
                                       gdouble threshold)
{
    GwyDataField *result, *buffer;
    gdouble hmax, hmin, threshval, zdata[200];
    gint i;
    gdouble xdata[200], ydata[200];

    result = gwy_data_field_new_alike(dfield, FALSE);

    gwy_data_field_hough_circle(dfield, x_gradient, y_gradient, result, radius);

    gwy_data_field_get_min_max(result, &hmin, &hmax);
    threshval = hmax + (hmax - hmin)*threshold;
    gwy_data_field_get_local_maxima_list(result, xdata, ydata, zdata, 200, 2, threshval, TRUE);

    buffer = gwy_data_field_duplicate(dfield);
    gwy_data_field_fill(buffer, 0);

    for (i = 0; i < 200; i++) {
        if (zdata[i] > threshval)
            bresenhams_circle(buffer, (gint)radius, xdata[i], ydata[i], 1);
    }
    gwy_data_field_threshold(buffer, 1, 0, 2);
    gwy_data_field_sum_fields(dfield, dfield, buffer);
}

static inline gint
signum(gint x)
{
    if (x < 0) {
        return -1;
    }
    if (x > 0) {
        return 1;
    }
    return 0;
}

void
gwy_data_field_hough_polar_line_to_datafield(GwyDataField *dfield,
                                             gdouble rho, gdouble theta,
                                             gint *px1, gint *px2,
                                             gint *py1, gint *py2)
{
     gint x_top, x_bottom, y_left, y_right;
     gboolean x1set = FALSE;

     x_top = (gint)(rho/cos(theta));
     x_bottom = (gint)((rho - dfield->yres*sin(theta))/cos(theta));
     y_left = (gint)(rho/sin(theta));
     y_right = (gint)((rho - dfield->xres*cos(theta))/sin(theta));

     if (x_top >= 0 && x_top < dfield->xres) {
         *px1 = x_top;
         *py1 = 0;
         x1set = TRUE;
     }
     if (x_bottom >= 0 && x_bottom < dfield->xres) {
         if (x1set) {
             *px2 = x_bottom;
             *py2 = dfield->yres - 1;
         }
         else {
             *px1 = x_bottom;
             *py1 = dfield->yres - 1;
             x1set = TRUE;
         }
     }
     if (y_left >= 0 && y_left < dfield->yres) {
         if (x1set) {
             *px2 = 0;
             *py2 = y_left;
         }
         else {
             *px1 = 0;
             *py1 = y_left;
             x1set = TRUE;
         }
     }
     if (y_right >= 0 && y_right < dfield->yres) {
         *px2 = dfield->xres - 1;
         *py2 = y_right;
     }
     if (!x1set) {
         g_warning("line does not intersect image");
         return;
     }
}

static void
bresenhams_line_polar(GwyDataField *dfield,
                      gdouble rho, gdouble theta, gdouble value)
{
     gint px1, px2, py1, py2;

     gwy_data_field_hough_polar_line_to_datafield(dfield, rho, theta, &px1, &px2, &py1, &py2);
     bresenhams_line(dfield, px1, px2, py1, py2, value);
}

static void
bresenhams_line(GwyDataField *dfield,
                gint x1, gint x2,
                gint y1_, gint y2_,
                gdouble value)
{
     gint i, dx, dy, sdx, sdy, dxabs, dyabs;
     gint x, y, px, py;

     dx = x2 - x1;
     dy = y2_ - y1_;
     dxabs = ABS(dx);
     dyabs = ABS(dy);
     sdx = signum(dx);
     sdy = signum(dy);
     x = dyabs>>1;
     y = dxabs>>1;
     px = x1;
     py = y1_;

     dfield->data[px + py*dfield->xres] = value;

     if (dxabs >= dyabs) {
         for (i = 0; i < dxabs; i++) {
             y += dyabs;
             if (y >= dxabs) {
                 y -= dxabs;
                 py += sdy;
             }
             px += sdx;
             dfield->data[px + py*dfield->xres] = value;
         }
     }
     else {
         for (i = 0; i < dyabs; i++) {
             x += dxabs;
             if (x >= dyabs) {
                 x -= dyabs;
                 px += sdx;
             }
             py += sdy;
             dfield->data[px + py*dfield->xres] = value;
         }
     }
}

static inline void
plot_pixel_safe(GwyDataField *dfield, gint idx, gdouble value)
{
    if (idx > 0 && idx < dfield->xres*dfield->yres)
        dfield->data[idx] += value;
}

static void
bresenhams_circle(GwyDataField *dfield,
                  gdouble r,
                  gint col, gint row,
                  gdouble value)
{
    gdouble n = 0.0, invradius = 1.0/r;
    gint dx = 0, dy = r-1;
    gint dxoffset, dyoffset;
    gint offset = col + row*dfield->xres;

    while (dx <= dy) {
         dxoffset = dfield->xres*dx;
         dyoffset = dfield->xres*dy;
         plot_pixel_safe(dfield, offset + dy - dxoffset, value);
         plot_pixel_safe(dfield, offset + dx - dyoffset, value);
         plot_pixel_safe(dfield, offset - dx - dyoffset, value);
         plot_pixel_safe(dfield, offset - dy - dxoffset, value);
         plot_pixel_safe(dfield, offset - dy + dxoffset, value);
         plot_pixel_safe(dfield, offset - dx + dyoffset, value);
         plot_pixel_safe(dfield, offset + dx + dyoffset, value);
         plot_pixel_safe(dfield, offset + dy + dxoffset, value);
         dx++;
         n += invradius;
         dy = r * sin(acos(n));
     }
}

static void
bresenhams_circle_gradient(GwyDataField *dfield, gdouble r, gint col, gint row,
                           gdouble value, gdouble gradient)
{
    gdouble n = 0.0, invradius = 1.0/r;
    gint dx = 0, dy = r-1;
    gint dxoffset, dyoffset, i;
    gdouble diff;
    gint offset = col + row*dfield->xres;
    gdouble multoctant[8];

    for (i = 0; i < 8; i++) {
        diff = fabs((G_PI*i/4.0 - G_PI) - gradient);
        if (diff > G_PI)
            diff = 2*G_PI - diff;
        multoctant[i] = G_PI - diff;
    }

    while (dx <= dy) {
         dxoffset = dfield->xres*dx;
         dyoffset = dfield->xres*dy;
         plot_pixel_safe(dfield, offset + dy - dxoffset, value*multoctant[0]);
         plot_pixel_safe(dfield, offset + dx - dyoffset, value*multoctant[1]);
         plot_pixel_safe(dfield, offset - dx - dyoffset, value*multoctant[2]);
         plot_pixel_safe(dfield, offset - dy - dxoffset, value*multoctant[3]);
         plot_pixel_safe(dfield, offset - dy + dxoffset, value*multoctant[4]);
         plot_pixel_safe(dfield, offset - dx + dyoffset, value*multoctant[5]);
         plot_pixel_safe(dfield, offset + dx + dyoffset, value*multoctant[6]);
         plot_pixel_safe(dfield, offset + dy + dxoffset, value*multoctant[7]);
         dx++;
         n += invradius;
         dy = r * sin(acos(n));
     }
}

static gint
replace_existing_maxima(gdouble *xdata, gdouble *ydata, gdouble *zdata,
                        gint j, gint i, gdouble value, gdouble skip2, gint *count, gint ndata)
{
    gint k, n, newpos = -1;

    /* If it cannot replace any existing maximum, skip.  The array is kept sorted so we just check the last item. */
    if (*count == ndata && value <= zdata[ndata-1])
        return -1;

    for (k = n = 0; n < *count; n++) {
        gdouble dx = xdata[n] - j, dy = ydata[n] - i;
        if (dx*dx + dy*dy < skip2) {
            /* If we find a value which is too close and larger, then the new one can be discarded. */
            if (zdata[n] >= value)
                return -1;
            /* If we got here then there is no too-close maximum which is larger.  We remove (skip) all the smaller
             * ones by not incrementing k. */
        }
        else {
            /* Otherwise just copy. */
            xdata[k] = xdata[n];
            ydata[k] = ydata[n];
            zdata[k] = zdata[n];
            /* The first time we see a smaller value make it the insertion position.  Things may still be happening
             * there but data at smaller indices will remain larger. */
            if (newpos == -1 && zdata[k] < value)
                newpos = k;
            k++;
        }
    }
    *count = MIN(k+1, ndata);
    if (newpos == -1)
        newpos = k;
    else {
        /* Insert at position newpos to keep data sorted. */
        n = MIN(*count, ndata-1) - newpos;
        memmove(xdata+newpos+1, xdata+newpos, n*sizeof(gdouble));
        memmove(ydata+newpos+1, ydata+newpos, n*sizeof(gdouble));
        memmove(zdata+newpos+1, zdata+newpos, n*sizeof(gdouble));
    }
    xdata[newpos] = j;
    ydata[newpos] = i;
    zdata[newpos] = value;
    return newpos;
}

static void
refine_local_maximum(GwyDataField *dfield,
                     gint mcol, gint mrow,
                     gdouble *xval, gdouble *yval)
{
    *xval = mcol;
    *yval = mrow;
    gwy_data_field_local_maximum(dfield, xval, yval, 1, 1);
}

/**
 * gwy_data_field_get_local_maxima_list:
 * @dfield: A two-dimensional data field.
 * @xdata: Array of @ndata elements where the columns should be stored.
 * @ydata: Array of @ndata elements where the rows should be stored.
 * @zdata: Array of @ndata elements where the values should be stored.
 * @ndata: Number of items in @xdata, @ydata and @zdata.
 * @skip: Minimum pixel distance between maxima.
 * @threshold: Minimum value to be considered a maximum.
 * @subpixel: %TRUE for subpixel refinement.
 *
 * Locates local maxima in a data field.
 *
 * At most @ndata maxima are located (with the largest values).
 *
 * Returns: The actual number of maxima found.
 **/
gint
gwy_data_field_get_local_maxima_list(GwyDataField *dfield,
                                     gdouble *xdata,
                                     gdouble *ydata,
                                     gdouble *zdata,
                                     gint ndata,
                                     gint skip,
                                     gdouble threshold,
                                     gboolean subpixel)
{
    gint j, i, k, m, count, xres, yres, skip2;
    gdouble xval, yval, value;
    gdouble *d;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), 0);

    gwy_clear(xdata, ndata);
    gwy_clear(ydata, ndata);
    for (i = 0; i < ndata; i++)
        zdata[i] = -G_MAXDOUBLE;

    count = 0;
    skip2 = skip*skip;
    xres = dfield->xres;
    yres = dfield->yres;
    d = dfield->data;
    for (i = 1; i < yres-1; i++) {
        for (j = 1; j < xres-1; j++, k++) {
            k = i*xres + j;
            if ((value = d[k]) < threshold)
                continue;
            /* Ensure it is actually a local maximum. */
            if (value < d[k-1] || value < d[k+1]
                || value < d[k-xres-1] || value < d[k-xres] || value < d[k-xres+1]
                || value < d[k+xres-1] || value < d[k+xres] || value < d[k+xres+1])
                continue;

            /* If there are other maxima too close we either replace some of them or skip this one.  Or just replace
             * the smallest if the new minimum is far from all others.  */
            m = replace_existing_maxima(xdata, ydata, zdata, j, i, value, skip2, &count, ndata);
            if (m < 0)
                continue;

            if (subpixel) {
                refine_local_maximum(dfield, j, i, &xval, &yval);
                xdata[m] = xval;
                ydata[m] = yval;
            }
        }
    }

    return count;
}

/**
 * gwy_data_field_get_local_minima_list:
 * @dfield: A two-dimensional data field.
 * @xdata: Array of @ndata elements where the columns should be stored.
 * @ydata: Array of @ndata elements where the rows should be stored.
 * @zdata: Array of @ndata elements where the values should be stored.
 * @ndata: Number of items in @xdata, @ydata and @zdata.
 * @skip: Minimum pixel distance between minima.
 * @threshold: Maximum value to be considered a minimum.
 * @subpixel: %TRUE for subpixel refinement.
 *
 * Locates local minima in a data field.
 *
 * At most @ndata minima are located (with the smallest values).
 *
 * Returns: The actual number of minima found.
 *
 * Since: 2.59
 **/
gint
gwy_data_field_get_local_minima_list(GwyDataField *dfield,
                                     gdouble *xdata,
                                     gdouble *ydata,
                                     gdouble *zdata,
                                     gint ndata,
                                     gint skip,
                                     gdouble threshold,
                                     gboolean subpixel)
{
    GwyDataField *inverted;
    gint n, i;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), 0);
    inverted = gwy_data_field_duplicate(dfield);
    gwy_data_field_multiply(inverted, -1.0);
    n = gwy_data_field_get_local_maxima_list(inverted, xdata, ydata, zdata, ndata, skip, -threshold, subpixel);
    g_object_unref(inverted);
    for (i = 0; i < n; i++)
        zdata[i] *= -1.0;

    return n;
}

void
gwy_data_field_hough_datafield_line_to_polar(gint px1,
                                             gint px2,
                                             gint py1,
                                             gint py2,
                                             gdouble *rho,
                                             gdouble *theta)
{
    gdouble k, q;

    k = (py2 - py1)/(gdouble)(px2 - px1);
    q = py1 - (py2 - py1)/(gdouble)(px2 - px1)*px1;

    *rho = q/sqrt(k*k + 1);
    *theta = asin(1/sqrt(k*k + 1));

    /*printf("line: p1 (%d, %d), p2 (%d, %d), k=%g q=%g rho=%g theta=%g\n",
           px1, py1, px2, py2, k, q, *rho, *theta);*/
}


/************************** Documentation ****************************/

/**
 * SECTION:hough
 * @title: hough
 * @short_description: Hough transform
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
