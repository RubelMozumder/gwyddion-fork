/*
 *  $Id: gwyfftw.h 22506 2019-09-20 11:30:31Z yeti-dn $
 *  Copyright (C) 2019 David Necas (Yeti), Petr Klapetek.
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

/*< private_header >*/

#ifndef __GWYPROCESS_FFTW_H__
#define __GWYPROCESS_FFTW_H__

#include <fftw3.h>
#include <string.h>
#include <libprocess/gwyprocessenums.h>
#include <libprocess/datafield.h>
#include "libgwyddion/gwyomp.h"

/* Do not require FFTW 3.3 just for a couple of trivial macros. */
#define gwy_fftw_new_real(n) \
    (gdouble*)fftw_malloc((n)*sizeof(gdouble))
#define gwy_fftw_new_complex(n) \
    (fftw_complex*)fftw_malloc((n)*sizeof(fftw_complex))

#define gwycreal(x) ((x)[0])
#define gwycimag(x) ((x)[1])

G_BEGIN_DECLS

typedef void (*GwyFFTAreaFunc)(fftw_plan plan,
                               GwyDataLine *din,
                               GwyDataLine *dout,
                               GwyDataLine *target_line);

/*
 * Wrappers for FFTW planning functions that handle OpenMP stuff.  Use inline
 * functions instead of macros to avoid the necessity to generate OpenMP
 * pragmas portably in macros.
 *
 * They handle the basic complication of making sure the planner is not
 * executed from multiple threads simultaneously.  We could use
 * fftw_make_planner_thread_safe() for just that, but it
 * (a) requires a relatively new FFTW and
 * (b) is of no help with executing fftw_plan_with_nthreads() and the planner
 *     as one block without interference from other threads.
 *
 * Set the number of threads to the default maximum for >= 2D or many 1D
 * transforms and to 1 for single 1D transforms – we essentially never have
 * huge 1D data, while we might execute multiple 1D FFTs in parallel.
 *
 * FIXME FIXME FIXME FIXME FIXME
 *
 * Unfortunately, we also cannot run the planner in parallel with
 * fftw_execute().  I get segfaults when I try.  This means we must protect
 * also fftw_execute().  But if we protect them using an OpenMP critical
 * section, then we cannot run fftw_execute()s in parallel.
 * XXX: This is what is currently done here as a stop-gap measuse.
 *
 * So the conditions seems to be:
 * Running execute -> must prevent the planner from running.
 * Running planner -> must prevent everything else from running.
 * This is the logic of an RW lock, with executes being readers and planners
 * writers.
 *
 * OpenMP does not provide RW locks.  So we can implement them, take them from
 * GLib (possibly with some subtle threading mismatch).
 */
G_GNUC_INTERNAL void      gwy_fftw_execute                (fftw_plan plan);
G_GNUC_INTERNAL void      gwy_fftw_plan_maybe_with_threads(void);
G_GNUC_INTERNAL void      gwy_fftw_plan_without_threads   (void);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_1d            (int n,
                                                           fftw_complex *in,
                                                           fftw_complex *out,
                                                           int sign,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_r2c_1d        (int n,
                                                           double *in,
                                                           fftw_complex *out,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_c2r_1d        (int n,
                                                           fftw_complex *in,
                                                           double *out,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_r2r_1d            (int n,
                                                           double *in,
                                                           double *out,
                                                           fftw_r2r_kind kind,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_2d            (int n0,
                                                           int n1,
                                                           fftw_complex *in,
                                                           fftw_complex *out,
                                                           int sign,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_r2c_2d        (int n0,
                                                           int n1,
                                                           double *in,
                                                           fftw_complex *out,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_dft_c2r_2d        (int n0,
                                                           int n1,
                                                           fftw_complex *in,
                                                           double *out,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_guru_split_dft    (int rank,
                                                           const fftw_iodim *dims,
                                                           int howmany_rank,
                                                           const fftw_iodim *howmany_dims,
                                                           double *ri,
                                                           double *ii,
                                                           double *ro,
                                                           double *io,
                                                           unsigned int flags);
G_GNUC_INTERNAL fftw_plan gwy_fftw_plan_guru_split_dft_r2c(int rank,
                                                           const fftw_iodim *dims,
                                                           int howmany_rank,
                                                           const fftw_iodim *howmany_dims,
                                                           double *ri,
                                                           double *ro,
                                                           double *io,
                                                           unsigned int flags);

G_GNUC_UNUSED
static inline void
do_fft_acf(fftw_plan plan,
           GwyDataLine *din,
           GwyDataLine *dout,
           GwyDataLine *target_line)
{
    gdouble *in, *out;
    gint j, width, res;

    width = target_line->res;
    res = din->res;
    in = din->data;
    out = dout->data;

    gwy_clear(in + width, res - width);

    gwy_fftw_execute(plan);
    in[0] = out[0]*out[0];
    for (j = 1; j < (res + 1)/2; j++)
        in[j] = in[res-j] = out[j]*out[j] + out[res-j]*out[res-j];
    if (!(res % 2))
        in[res/2] = out[res/2]*out[res/2];

    gwy_fftw_execute(plan);
    for (j = 0; j < width; j++)
        target_line->data[j] += out[j]/(width - j);
}

G_GNUC_UNUSED
static inline void
do_fft_hhcf(fftw_plan plan,
            GwyDataLine *din,
            GwyDataLine *dout,
            GwyDataLine *target_line)
{
    gdouble *in, *out;
    gdouble sum;
    gint j, width, res;

    width = target_line->res;
    res = din->res;
    in = din->data;
    out = dout->data;

    sum = 0.0;
    for (j = 0; j < width; j++) {
        sum += in[j]*in[j] + in[width-1-j]*in[width-1-j];
        target_line->data[width-1-j] += sum*res/(j+1);
    }

    gwy_clear(in + width, res - width);

    gwy_fftw_execute(plan);
    in[0] = out[0]*out[0];
    for (j = 1; j < (res + 1)/2; j++)
        in[j] = in[res-j] = out[j]*out[j] + out[res-j]*out[res-j];
    if (!(res % 2))
        in[res/2] = out[res/2]*out[res/2];

    gwy_fftw_execute(plan);
    for (j = 0; j < width; j++)
        target_line->data[j] -= 2*out[j]/(width - j);
}

G_END_DECLS

#endif /* __GWYPROCESS_FFTW_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
