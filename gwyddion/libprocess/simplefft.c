/*
 *  $Id: simplefft.c 24831 2022-05-19 13:33:34Z yeti-dn $
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

#include <fftw3.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/simplefft.h>
#include <libprocess/inttrans.h>
#include "gwyfftw.h"

static guint smooth_upper_bound(guint n);

typedef gdouble (*GwyFFTWindowingFunc)(gint i, gint n);

static gdouble gwy_fft_window_hann     (gint i, gint n);
static gdouble gwy_fft_window_hamming  (gint i, gint n);
static gdouble gwy_fft_window_blackman (gint i, gint n);
static gdouble gwy_fft_window_lanczos  (gint i, gint n);
static gdouble gwy_fft_window_welch    (gint i, gint n);
static gdouble gwy_fft_window_rect     (gint i, gint n);
static gdouble gwy_fft_window_nuttall  (gint i, gint n);
static gdouble gwy_fft_window_flat_top (gint i, gint n);
static gdouble gwy_fft_window_kaiser25 (gint i, gint n);

static GRWLock gwy_fftw_lock;

/* The order must match GwyWindowingType enum */
static const GwyFFTWindowingFunc windowings[] = {
    NULL,  /* none */
    &gwy_fft_window_hann,
    &gwy_fft_window_hamming,
    &gwy_fft_window_blackman,
    &gwy_fft_window_lanczos,
    &gwy_fft_window_welch,
    &gwy_fft_window_rect,
    &gwy_fft_window_nuttall,
    &gwy_fft_window_flat_top,
    &gwy_fft_window_kaiser25,
};

/**
 * gwy_fft_find_nice_size:
 * @size: Transform size.
 *
 * Finds a nice-for-FFT array size.
 *
 * Here ‘nice’ means three properties are guaranteed: it is greater than or equal to @size; it can be directly used
 * with current FFT backend without scaling (since 2.8 this is true for any size); and the transform is fast, i.e. the
 * number is highly factorable.
 *
 * To be compatible with Gwyddion <= 2.7 one has to pass only data fields and lines with sizes returned by this
 * function to raw integral transforms. Otherwise this function is mainly useful if you extend and pad the input data
 * for other reasons and thus have the freedom to choose a convenient transform size.
 *
 * Returns: A nice FFT array size.
 **/
gint
gwy_fft_find_nice_size(gint size)
{
    if (size <= 16)
        return size;

    size = smooth_upper_bound(size);
    /* Ensure the result is even.  This helps with a number of FFTW routines. When size is odd then size+1 is even,
     * i.e. it has factor 2.  Since smooth_upper_bound() preserves all the small factors, we know we will get an even
     * number. */
    if (size % 2)
        size = smooth_upper_bound(size+1);

    return size;
}

/**
 * smooth_upper_bound:
 * @n: A number.
 *
 * Finds a smooth (highly factorable) number larger or equal to @n.
 *
 * Returns: A smooth number larger or equal to @n.
 **/
static guint
smooth_upper_bound(guint n)
{
    static const guint primes[] = { 2, 3, 5, 7 };

    guint j, p, r;

    for (r = 1; ; ) {
        /* the factorable part */
        for (j = 0; j < G_N_ELEMENTS(primes); j++) {
            p = primes[j];
            while (n % p == 0) {
                n /= p;
                r *= p;
            }
        }

        if (n == 1)
            return r;

        /* gosh... make it factorable again */
        n++;
    }
}

/**
 * gwy_fft_simple:
 * @dir: Transformation direction.
 * @n: Number of data points. Note only certain transform sizes are implemented.  If gwy_fft_simple() is the current
 *     FFT backend, then gwy_fft_find_nice_size() can provide accepted transform sizes. If gwy_fft_simple() is not the
 *     current FFT backend, you should not use it.
 * @istride: Input data stride.
 * @in_re: Real part of input data.
 * @in_im: Imaginary part of input data.
 * @ostride: Output data stride.
 * @out_re: Real part of output data.
 * @out_im: Imaginary part of output data.
 *
 * Performs a DFT algorithm.
 *
 * This is a low-level function that used to be employed by other FFT functions when no better backend was available.
 * Since version 2.49 it just calls the corresponding FFTW routine.
 *
 * Strides are distances between samples in input and output arrays.  Use 1 for normal `dense' arrays.  To use
 * gwy_fft_simple() with interleaved arrays, that is with alternating real and imaginary data, call it with
 * @istride=2, @in_re=@complex_array, @in_im=@complex_array+1 (and similarly for output arrays).
 *
 * The output is symmetrically normalized by square root of @n for both transform directions.  By performing forward
 * and then backward transform, you will obtain the original array (up to rounding errors).
 **/
void
gwy_fft_simple(GwyTransformDirection dir,
               gint n,
               gint istride,
               const gdouble *in_re,
               const gdouble *in_im,
               gint ostride,
               gdouble *out_re,
               gdouble *out_im)
{
    fftw_complex *cin, *cout;
    fftw_plan plan;
    gdouble q;
    gint i, sign;

    if (G_UNLIKELY(!n))
        return;

    g_return_if_fail(istride > 0);
    g_return_if_fail(ostride > 0);

    /* Planner can overwrite the input arrays, so we must allocate temporary buffers.  Since we do that, we can
     * compactify the data and use basic DFT routines. */
    cin = gwy_fftw_new_complex(n);
    cout = gwy_fftw_new_complex(n);
    /* Yes, the directions are swapped. */
    sign = (dir == GWY_TRANSFORM_DIRECTION_BACKWARD ? FFTW_FORWARD : FFTW_BACKWARD);

    plan = gwy_fftw_plan_dft_1d(n, cin, cout, sign, FFTW_DESTROY_INPUT | FFTW_ESTIMATE);

    for (i = 0; i < n; i++) {
        gwycreal(cin[i]) = *in_re;
        gwycimag(cin[i]) = *in_im;
        in_re += istride;
        in_im += istride;
    }
    gwy_fftw_execute(plan);

    fftw_destroy_plan(plan);
    fftw_free(cin);

    q = 1.0/sqrt(n);
    for (i = 0; i < n; i++) {
        *out_re = q*gwycreal(cout[i]);
        *out_im = q*gwycimag(cout[i]);
        in_re += ostride;
        in_im += ostride;
    }
    fftw_free(cout);
}

static gdouble
gwy_fft_window_hann(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n;

    return 0.5 - 0.5*cos(x);
}

static gdouble
gwy_fft_window_hamming(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n;

    return 0.54 - 0.46*cos(x);
}

static gdouble
gwy_fft_window_blackman(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n;

    return 0.42 - 0.5*cos(x) + 0.08*cos(2*x);
}

static gdouble
gwy_fft_window_lanczos(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n - G_PI;

    return fabs(x) < 1e-8 ? 1.0 : sin(x)/x;
}

static gdouble
gwy_fft_window_welch(gint i, gint n)
{
    gdouble x = 2.0*(i + 0.5)/n - 1.0;

    return 1 - x*x;
}

static gdouble
gwy_fft_window_rect(gint i, gint n)
{
    gdouble par;

    if (i == 0 || i == (n-1))
        par = 0.5;
    else
        par = 1.0;
    return par;
}

static gdouble
gwy_fft_window_nuttall(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n;

    return 0.355768 - 0.487396*cos(x) + 0.144232*cos(2*x) - 0.012604*cos(3*x);
}

static gdouble
gwy_fft_window_flat_top(gint i, gint n)
{
    gdouble x = 2*G_PI*(i + 0.5)/n;

    return (1.0 - 1.93*cos(x) + 1.29*cos(2*x) - 0.388*cos(3*x) + 0.032*cos(4*x))/4;
}

static inline gdouble
bessel_I0(gdouble x)
{
    gdouble t, s;
    gint i = 1;

    t = x = x*x/4;
    s = 1.0;
    do {
        s += t;
        i++;
        t *= x/i/i;
    } while (t > 1e-7*s);

    return s + t;
}

/* General function */
static gdouble
gwy_fft_window_kaiser(gint i, gint n, gdouble alpha)
{
    gdouble x = 2.0*(i + 0.5)/n - 1.0;
    x = 1.0 - x*x;
    x = MAX(x, 0.0);

    return bessel_I0(G_PI*alpha*sqrt(x));
}

static gdouble
gwy_fft_window_kaiser25(gint i, gint n)
{
    return gwy_fft_window_kaiser(i, n, 2.5)/373.0206312536293446480;
}

/**
 * gwy_fft_window:
 * @n: Number of data values.
 * @data: Data values.
 * @windowing: Method used for windowing.
 *
 * Multiplies data by given window.
 **/
void
gwy_fft_window(gint n,
               gdouble *data,
               GwyWindowingType windowing)
{
    GwyFFTWindowingFunc window;
    gint i;

    g_return_if_fail(data);
    g_return_if_fail(windowing <= GWY_WINDOWING_KAISER25);
    window = windowings[windowing];
    if (window) {
        for (i = 0; i < n; i++)
            data[i] *= window(i, n);
    }
}

/**
 * gwy_fft_window_data_field:
 * @dfield: A data field.
 * @orientation: Windowing orientation (the same as corresponding FFT orientation).
 * @windowing: The windowing type to use.
 *
 * Performs windowing of a data field in given direction.
 *
 * This is an old alias for gwy_data_field_fft_window_1d().
 **/
void
gwy_fft_window_data_field(GwyDataField *dfield,
                          GwyOrientation orientation,
                          GwyWindowingType windowing)
{
    gwy_data_field_fft_window_1d(dfield, orientation, windowing);
}

/* We assume when there is no multithreading there is no lock contention and the RW lock operations are cheap. */
static inline void
gwy_fftw_lock_execute(void)
{
    g_rw_lock_reader_lock(&gwy_fftw_lock);
}

static inline void
gwy_fftw_unlock_execute(void)
{
    g_rw_lock_reader_unlock(&gwy_fftw_lock);
}

static inline void
gwy_fftw_lock_planner(void)
{
    g_rw_lock_writer_lock(&gwy_fftw_lock);
}

static inline void
gwy_fftw_unlock_planner(void)
{
    g_rw_lock_writer_unlock(&gwy_fftw_lock);
}

void
gwy_fftw_execute(fftw_plan plan)
{
    gwy_fftw_lock_execute();
    fftw_execute(plan);
    gwy_fftw_unlock_execute();
}

/* This must be called with the planner lock already held. */
void
gwy_fftw_plan_maybe_with_threads(void)
{
#if (defined(_OPENMP) && defined(HAVE_FFTW_WITH_OPENMP))
    guint nthreads = 1;

    if (!omp_get_active_level() && gwy_threads_are_enabled())
        nthreads = gwy_omp_max_threads();

    fftw_plan_with_nthreads(nthreads);
#endif
}

void
gwy_fftw_plan_without_threads(void)
{
#if (defined(_OPENMP) && defined(HAVE_FFTW_WITH_OPENMP))
    fftw_plan_with_nthreads(1);
#endif
}

fftw_plan
gwy_fftw_plan_dft_1d(int n,
                     fftw_complex *in, fftw_complex *out,
                     int sign, unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_without_threads();
    plan = fftw_plan_dft_1d(n, in, out, sign, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_dft_r2c_1d(int n,
                         double *in, fftw_complex *out,
                         unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_without_threads();
    plan = fftw_plan_dft_r2c_1d(n, in, out, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_dft_c2r_1d(int n,
                         fftw_complex *in, double *out,
                         unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_without_threads();
    plan = fftw_plan_dft_c2r_1d(n, in, out, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_r2r_1d(int n,
                     double *in, double *out,
                     fftw_r2r_kind kind, unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_without_threads();
    plan = fftw_plan_r2r_1d(n, in, out, kind, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_dft_2d(int n0, int n1,
                     fftw_complex *in, fftw_complex *out,
                     int sign, unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_maybe_with_threads();
    plan = fftw_plan_dft_2d(n0, n1, in, out, sign, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_dft_r2c_2d(int n0, int n1,
                         double *in, fftw_complex *out,
                         unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_maybe_with_threads();
    plan = fftw_plan_dft_r2c_2d(n0, n1, in, out, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_dft_c2r_2d(int n0, int n1,
                         fftw_complex *in, double *out,
                         unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    gwy_fftw_plan_maybe_with_threads();
    plan = fftw_plan_dft_c2r_2d(n0, n1, in, out, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_guru_split_dft(int rank, const fftw_iodim *dims,
                             int howmany_rank,
                             const fftw_iodim *howmany_dims,
                             double *ri, double *ii, double *ro, double *io,
                             unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    if (rank > 1 || howmany_rank > 1 || (dims[0].n > 1 && howmany_dims[0].n > 1))
        gwy_fftw_plan_maybe_with_threads();
    else
        gwy_fftw_plan_without_threads();

    plan = fftw_plan_guru_split_dft(rank, dims, howmany_rank, howmany_dims, ri, ii, ro, io, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

fftw_plan
gwy_fftw_plan_guru_split_dft_r2c(int rank, const fftw_iodim *dims,
                                 int howmany_rank,
                                 const fftw_iodim *howmany_dims,
                                 double *ri, double *ro, double *io,
                                 unsigned int flags)
{
    fftw_plan plan;

    gwy_fftw_lock_planner();
    if (rank > 1 || howmany_rank > 1 || (dims[0].n > 1 && howmany_dims[0].n > 1))
        gwy_fftw_plan_maybe_with_threads();
    else
        gwy_fftw_plan_without_threads();

    plan = fftw_plan_guru_split_dft_r2c(rank, dims, howmany_rank, howmany_dims, ri, ro, io, flags);
    gwy_fftw_unlock_planner();
    g_assert(plan);

    return plan;
}

/************************** Documentation ****************************/

/**
 * SECTION:simplefft
 * @title: simpleFFT
 * @short_description: Simple FFT algorithm
 * @see_also: <link linkend="libgwyprocess-inttrans">inttrans</link> -- high-level integral transform functions
 *
 * The simple one-dimensional FFT algorithm gwy_fft_simple() used to be employed as a fallback by other functions when
 * a better implementation (FFTW3) was not available.  Since version 2.49 it just calls the corresponding FFTW
 * routine.
 *
 * Generally, you should either use high-level Gwyddion functions such as gwy_data_field_2dfft_raw() or, if they are
 * insufficient, FFTW routines directly.
 *
 * Up to version 2.7 simpleFFT required certain tranform sizes, mostly powers of 2.  Since 2.8 it can handle arbitrary
 * tranform sizes, although sizes with large prime factors can be quite slow (still O(n*log(n)) though).
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
