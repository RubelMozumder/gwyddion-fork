/*
 *  $Id: gwypixfield.c 22340 2019-07-25 10:23:59Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <gwypixfield.h>
#include "libgwyddion/gwyomp.h"

static gint* calc_cdh(GwyDataField *dfield, gint *cdh_size);

/**
 * gwy_pixbuf_draw_data_field_with_range:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data field to draw.
 * @gradient: A color gradient to draw with.
 * @minimum: The value corresponding to gradient start.
 * @maximum: The value corresponding to gradient end.
 *
 * Paints a data field to a pixbuf with an explicite color gradient range.
 *
 * @minimum and all smaller values are mapped to start of @gradient, @maximum
 * and all greater values to its end, values between are mapped linearly to
 * @gradient.
 **/
void
gwy_pixbuf_draw_data_field_with_range(GdkPixbuf *pixbuf,
                                      GwyDataField *data_field,
                                      GwyGradient *gradient,
                                      gdouble minimum,
                                      gdouble maximum)
{
    int xres, yres, i, palsize, rowstride;
    gdouble cor;
    const guchar *samples;
    const gdouble *data;
    guchar *pixels;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_GRADIENT(gradient));
    if (minimum == maximum)
        maximum = G_MAXDOUBLE;

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    data = gwy_data_field_get_data_const(data_field);

    g_return_if_fail(xres == gdk_pixbuf_get_width(pixbuf));
    g_return_if_fail(yres == gdk_pixbuf_get_height(pixbuf));

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    samples = gwy_gradient_get_samples(gradient, &palsize);
    cor = (palsize - 1.0)/(maximum - minimum);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(pixels,data,samples,xres,yres,rowstride,palsize,minimum,cor)
#endif
    for (i = 0; i < yres; i++) {
        guchar *line = pixels + i*rowstride;
        const gdouble *row = data + i*xres;
        const guchar *s;
        gint j, dval;

        for (j = 0; j < xres; j++) {
            dval = (gint)((*(row++) - minimum)*cor + 0.5);
            dval = GWY_CLAMP(dval, 0, palsize-1);
            /* simply index to the guchar samples, it's faster and no one
             * can tell the difference... */
            s = samples + 4*dval;
            *(line++) = *(s++);
            *(line++) = *(s++);
            *(line++) = *s;
        }
    }
}

/**
 * gwy_pixbuf_draw_data_field:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data field to draw.
 * @gradient: A color gradient to draw with.
 *
 * Paints a data field to a pixbuf with an auto-stretched color gradient.
 *
 * Minimum data value is mapped to start of @gradient, maximum value to its
 * end, values between are mapped linearly to @gradient.
 **/
void
gwy_pixbuf_draw_data_field(GdkPixbuf *pixbuf,
                           GwyDataField *data_field,
                           GwyGradient *gradient)
{
    int xres, yres, i, palsize, rowstride;
    gdouble maximum, minimum, cor;
    const guchar *samples;
    const gdouble *data;
    guchar *pixels;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_GRADIENT(gradient));

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    data = gwy_data_field_get_data_const(data_field);

    g_return_if_fail(xres == gdk_pixbuf_get_width(pixbuf));
    g_return_if_fail(yres == gdk_pixbuf_get_height(pixbuf));

    gwy_data_field_get_min_max(data_field, &minimum, &maximum);
    if (minimum == maximum)
        maximum = G_MAXDOUBLE;

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    samples = gwy_gradient_get_samples(gradient, &palsize);
    cor = (palsize-1.0)/(maximum-minimum);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(pixels,data,samples,xres,yres,rowstride,palsize,minimum,cor)
#endif
    for (i = 0; i < yres; i++) {
        guchar *line = pixels + i*rowstride;
        const gdouble *row = data + i*xres;
        const guchar *s;
        gint j, dval;

        for (j = 0; j < xres; j++) {
            dval = (gint)((*(row++) - minimum)*cor + 0.5);
            /* simply index to the guchar samples, it's faster and no one
             * can tell the difference... */
            dval = GWY_CLAMP(dval, 0, palsize-1);
            s = samples + 4*dval;
            *(line++) = *(s++);
            *(line++) = *(s++);
            *(line++) = *s;
        }
    }
}

/**
 * gwy_pixbuf_draw_data_field_adaptive:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data field to draw.
 * @gradient: A color gradient to draw with.
 *
 * Paints a data field to a pixbuf with a color gradient adaptively.
 *
 * The mapping from data field (minimum, maximum) range to gradient is
 * nonlinear, deformed using inverse function to height density cummulative
 * distribution.
 **/
void
gwy_pixbuf_draw_data_field_adaptive(GdkPixbuf *pixbuf,
                                    GwyDataField *data_field,
                                    GwyGradient *gradient)
{
    gint xres, yres, i, rowstride, palsize, cdh_size;
    gdouble min, max, cor, q, m;
    const guchar *samples;
    const gdouble *data;
    guchar *pixels;
    gint *cdh;

    gwy_data_field_get_min_max(data_field, &min, &max);
    if (min == max) {
        gwy_pixbuf_draw_data_field(pixbuf, data_field, gradient);
        return;
    }

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    g_return_if_fail(xres == gdk_pixbuf_get_width(pixbuf));
    g_return_if_fail(yres == gdk_pixbuf_get_height(pixbuf));

    cdh = calc_cdh(data_field, &cdh_size);
    q = (cdh_size - 1.0)/(max - min);
    data = gwy_data_field_get_data_const(data_field);

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    samples = gwy_gradient_get_samples(gradient, &palsize);
    cor = (palsize - 1.0)/cdh[cdh_size-1];

    m = cdh_size - 1.000001;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(pixels,data,samples,cdh,xres,yres,rowstride,q,min,m,cor)
#endif
    for (i = 0; i < yres; i++) {
        guchar *line = pixels + i*rowstride;
        const gdouble *row = data + i*xres;
        const guchar *s;
        gdouble v;
        gint j, h;

        for (j = 0; j < xres; j++) {
            v = (row[j] - min)*q;
            v = GWY_CLAMP(v, 0.0, m);
            h = (gint)v;
            v -= h;
            h = (gint)((cdh[h]*(1.0 - v) + cdh[h+1]*v)*cor + 0.5);
            s = samples + 4*h;
            *(line++) = *(s++);
            *(line++) = *(s++);
            *(line++) = *s;
        }
    }

    g_free(cdh);
}

/**
 * gwy_draw_data_field_map_adaptive:
 * @data_field: A data field to draw.
 * @z: Array of @n data values to map.
 * @mapped: Output array of size @n where mapped @z values are to be stored.
 * @n: Number of elements in @z and @mapped.
 *
 * Maps ordinate values to interval [0,1] as
 * gwy_pixbuf_draw_data_field_adaptive() would do.
 *
 * This is useful to find out which positions in the false colour gradient
 * correspond to specific values.
 *
 * Since: 2.39
 **/
void
gwy_draw_data_field_map_adaptive(GwyDataField *data_field,
                                 const gdouble *z,
                                 gdouble *mapped,
                                 guint n)
{
    gdouble min, max, cor, q, v, m;
    gint i, h, cdh_size;
    gint *cdh;

    gwy_data_field_get_min_max(data_field, &min, &max);
    if (min == max) {
        for (i = 0; i < n; i++)
            mapped[i] = 0.5;
        return;
    }

    cdh = calc_cdh(data_field, &cdh_size);
    q = (cdh_size - 1.0)/(max - min);
    cor = 1.0/cdh[cdh_size-1];

    m = cdh_size - 1.000001;
    for (i = 0; i < n; i++) {
        v = (z[i] - min)*q;
        v = GWY_CLAMP(v, 0.0, m);
        h = (gint)v;
        v -= h;
        v = (cdh[h]*(1.0 - v) + cdh[h+1]*v)*cor;
        mapped[i] = GWY_CLAMP(v, 0.0, 1.0);
    }

    g_free(cdh);
}

static gint*
calc_cdh(GwyDataField *dfield, gint *cdh_size)
{
    gdouble min, max;
    gint i, n, xres, yres;
    gint *cdh;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    gwy_data_field_get_min_max(dfield, &min, &max);

    n = *cdh_size = GWY_ROUND(pow(xres*yres, 2.0/3.0));
    cdh = g_new(guint, n);
    gwy_math_histogram(gwy_data_field_get_data_const(dfield), xres*yres,
                       min, max, n, cdh);
    for (i = 1; i < n; i++)
        cdh[i] += xres*yres/(2*n) + cdh[i-1];
    cdh[0] = 0;

    return cdh;
}

/**
 * gwy_pixbuf_draw_data_field_as_mask:
 * @pixbuf: A Gdk pixbuf to draw to.
 * @data_field: A data field to draw.
 * @color: A color to use.
 *
 * Paints a data field to a pixbuf as a single-color mask with varying opacity.
 *
 * Values equal or smaller to 0.0 are drawn as fully transparent, values
 * greater or equal to 1.0 as fully opaque, values between are linearly
 * mapped to pixel opacity.
 **/
void
gwy_pixbuf_draw_data_field_as_mask(GdkPixbuf *pixbuf,
                                   GwyDataField *data_field,
                                   const GwyRGBA *color)
{
    int xres, yres, i, rowstride;
    guchar *pixels;
    guint32 pixel;
    const gdouble *data;
    guchar a;

    g_return_if_fail(GDK_IS_PIXBUF(pixbuf));
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(color);

    pixel = gwy_rgba_to_pixbuf_pixel(color);
    a = (pixel & 0xff);
    pixel |= 0xff;
    gdk_pixbuf_fill(pixbuf, pixel);
    if (!gdk_pixbuf_get_has_alpha(pixbuf))
        return;

    xres = gwy_data_field_get_xres(data_field);
    yres = gwy_data_field_get_yres(data_field);
    data = gwy_data_field_get_data_const(data_field);

    g_return_if_fail(xres == gdk_pixbuf_get_width(pixbuf));
    g_return_if_fail(yres == gdk_pixbuf_get_height(pixbuf));

    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(pixels,data,xres,yres,rowstride,a)
#endif
    for (i = 0; i < yres; i++) {
        guchar *pixline = pixels + i*rowstride + 3;
        const gdouble *drow = data + i*xres;
        gint j;

        for (j = xres; j; j--, drow++, pixline += 4)
            *pixline = (*drow >= 0.5) ? a : 0;
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:gwypixfield
 * @title: gwypixfield
 * @short_description: Draw GwyDataFields to GdkPixbufs
 *
 * The simpliest method to render a #GwyDataField to a #GdkPixbuf with a
 * false color scale is gwy_pixbuf_draw_data_field() which uniformly stretches
 * the color gradient from value minimum to maximum.  Functions
 * gwy_pixbuf_draw_data_field_with_range() and
 * gwy_pixbuf_draw_data_field_adaptive() offer other false color mapping
 * possibilities.  A bit different is
 * gwy_pixbuf_draw_data_field_as_mask() which represents the values as
 * opacities of a signle color.
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
