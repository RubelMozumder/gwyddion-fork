/*
 *  $Id: cwt.c 24825 2022-05-16 13:35:48Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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

#include <libgwyddion/gwymath.h>
#include <libprocess/inttrans.h>
#include <libprocess/cwt.h>

static void gwy_data_field_mult_wav(GwyDataField *real_field,
                                    GwyDataField *imag_field,
                                    gdouble scale,
                                    Gwy2DCWTWaveletType wtype);

gdouble
gwy_cwt_wfunc_2d(gdouble scale,
                 gdouble mval,
                 gint xres,
                 Gwy2DCWTWaveletType wtype)
{
    gdouble dat2x, cur, scale2, cur2;

    dat2x = 4.0/(gdouble)xres;
    cur = mval*dat2x;
    cur2 = cur*cur;
    scale2 = scale*scale;

    if (wtype == GWY_2DCWT_GAUSS) {
        /* return exp(-(scale2*cur2)/2)*2*G_PI*scale2*2*G_PI*scale;
         * changed only for reasonable normalization*/
        return exp(-(scale2*cur2)/2);
    }
    if (wtype == GWY_2DCWT_HAT) {
        return (scale2*cur2)*exp(-(scale2*cur2)/2)*2*G_PI*scale2;
    }
    g_return_val_if_reached(1.0);
    return 1.0;
}

/*
 * gwy_data_field_mult_wav:
 * @real_field: A data field of real values
 * @imag_field: A data field of imaginary values
 * @scale: wavelet scale
 * @wtype: waveelt type
 *
 * multiply a complex data field (real and imaginary) with complex FT of spoecified wavelet at given scale.
 */
static void
gwy_data_field_mult_wav(GwyDataField *real_field,
                        GwyDataField *imag_field,
                        gdouble scale,
                        Gwy2DCWTWaveletType wtype)
{
    gint xres, yres, xresh, yresh;
    gint i, j;
    gdouble mval, val;

    xres = real_field->xres;
    yres = real_field->yres;
    xresh = xres/2;
    yresh = yres/2;

    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            val = 1;
            if (j < xresh) {
                if (i < yresh)
                    mval = sqrt(j*j + i*i);
                else
                    mval = sqrt(j*j + (yres - i)*(yres - i));
            }
            else {
                if (i < yresh)
                    mval = sqrt((xres - j)*(xres - j) + i*i);
                else
                    mval = sqrt((xres - j)*(xres - j) + (yres - i)*(yres - i));
            }
            val = gwy_cwt_wfunc_2d(scale, mval, xres, wtype);

            real_field->data[j + i*xres] *= val;
            imag_field->data[j + i*xres] *= val;
        }
    }
}

/**
 * gwy_data_field_cwt:
 * @data_field: A data field.
 * @interpolation: Interpolation type. Ignored since 2.8 as no resampling is performed.
 * @scale: Wavelet scale.
 * @wtype: Wavelet type.
 *
 * Computes a continuous wavelet transform (CWT) at given scale and using given wavelet.
 **/
void
gwy_data_field_cwt(GwyDataField *data_field,
                   GwyInterpolationType interpolation,
                   gdouble scale,
                   Gwy2DCWTWaveletType wtype)
{
    GwyDataField *hlp_r, *hlp_i, *imag_field;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    hlp_r = gwy_data_field_new_alike(data_field, FALSE);
    hlp_i = gwy_data_field_new_alike(data_field, FALSE);
    imag_field = gwy_data_field_new_alike(data_field, TRUE);

    gwy_data_field_2dfft(data_field, imag_field, hlp_r, hlp_i,
                         GWY_WINDOWING_RECT, GWY_TRANSFORM_DIRECTION_FORWARD, interpolation,  /* ignored */
                         FALSE, FALSE);
    gwy_data_field_mult_wav(hlp_r, hlp_i, scale, wtype);

    gwy_data_field_2dfft(hlp_r, hlp_i, data_field, imag_field,
                         GWY_WINDOWING_RECT, GWY_TRANSFORM_DIRECTION_BACKWARD, interpolation,  /* ignored */
                         FALSE, FALSE);

    g_object_unref(hlp_r);
    g_object_unref(hlp_i);
    g_object_unref(imag_field);

    gwy_data_field_invalidate(data_field);
}

/************************** Documentation ****************************/

/**
 * SECTION:cwt
 * @title: cwt
 * @short_description: Continuous Wavelet Transform
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
