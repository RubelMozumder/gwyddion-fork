/*
 *  $Id: gwymathfallback.h 21933 2019-02-27 08:58:37Z yeti-dn $
 *  Copyright (C) 2007-2016 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_MATH_FALLBACK_H__
#define __GWY_MATH_FALLBACK_H__

#include <math.h>
#include <float.h>
#include <glib.h>
#include <gwyconfig.h>

G_BEGIN_DECLS

/* This is necessary to fool gtk-doc that ignores static inline functions */
#define _GWY_STATIC_INLINE static inline

_GWY_STATIC_INLINE double gwy_math_fallback_pow10(double x);
_GWY_STATIC_INLINE double gwy_math_fallback_cbrt (double x);
_GWY_STATIC_INLINE double gwy_math_fallback_hypot(double x, double y);
_GWY_STATIC_INLINE double gwy_math_fallback_acosh(double x);
_GWY_STATIC_INLINE double gwy_math_fallback_asinh(double x);
_GWY_STATIC_INLINE double gwy_math_fallback_atanh(double x);
_GWY_STATIC_INLINE double gwy_math_fallback_powi(double x, int i);
_GWY_STATIC_INLINE int gwy_math_fallback_isinf(double x);
_GWY_STATIC_INLINE int gwy_math_fallback_isnan(double x);

#undef _GWY_STATIC_INLINE

/* The empty comments fix gtk-doc.
 * See http://bugzilla.gnome.org/show_bug.cgi?id=481811
 */
static inline double
gwy_math_fallback_pow10(double x)
{
    return /**/ exp(G_LN10 * x);
}

static inline double
gwy_math_fallback_cbrt(double x)
{
    return /**/ (x == 0.0
                 ? 0.0
                 : ((x < 0.0) ? -pow(-x, 1.0/3.0) : pow(x, 1.0/3.0)));
}

static inline double
gwy_math_fallback_hypot(double x, double y)
{
    return /**/ sqrt(x*x + y*y);
}

static inline double
gwy_math_fallback_acosh(double x)
{
    return /**/ log(x + sqrt(x*x - 1.0));
}

static inline double
gwy_math_fallback_asinh(double x)
{
    return /**/ log(x + sqrt(x*x + 1.0));
}

static inline double
gwy_math_fallback_atanh(double x)
{
    return /**/ log((1.0 + x)/(1.0 - x));
}

static inline int
gwy_math_fallback_isinf(double x)
{
    GDoubleIEEE754 /**/ dbl;
    dbl.v_double = x;
    return /**/ (dbl.mpn.biased_exponent == 0x7ff
                 && !dbl.mpn.mantissa_high && !dbl.mpn.mantissa_low);
}

static inline int
gwy_math_fallback_isnan(double x)
{
    GDoubleIEEE754 /**/ dbl;
    dbl.v_double = x;
    return /**/ (dbl.mpn.biased_exponent == 0x7ff
                 && (dbl.mpn.mantissa_high || dbl.mpn.mantissa_low));
}

#ifndef GWY_MATH_NAMESPACE_CLEAN

#ifndef GWY_HAVE_POW10
#ifdef GWY_HAVE_EXP10
#define pow10 exp10
#else
#define pow10 gwy_math_fallback_pow10
#endif  /* GWY_HAVE_EXP10 */
#endif  /* GWY_HAVE_POW10 */

#ifndef GWY_HAVE_CBRT
#define cbrt gwy_math_fallback_cbrt
#endif  /* GWY_HAVE_CBRT */

#ifndef GWY_HAVE_HYPOT
#define hypot gwy_math_fallback_hypot
#endif  /* GWY_HAVE_HYPOT */

#ifndef GWY_HAVE_ACOSH
#define acosh gwy_math_fallback_acosh
#endif  /* GWY_HAVE_ACOSH */

#ifndef GWY_HAVE_ASINH
#define asinh gwy_math_fallback_asinh
#endif  /* GWY_HAVE_ASINH */

#ifndef GWY_HAVE_ATANH
#define atanh gwy_math_fallback_atanh
#endif  /* GWY_HAVE_ATANH */

#endif /* GWY_MATH_NAMESPACE_CLEAN */

/* GCC's fast math makes isinf no-op.  Must use the fallback function.
 * Gwyddion works with finite arithmetic, but we need to detect INFs upon
 * file import. */
#if (defined(GWY_HAVE_ISINF) && !defined(__FAST_MATH__))
#define gwy_isinf isinf
#else
#define gwy_isinf gwy_math_fallback_isinf
#endif  /* GWY_HAVE_ISINF */

/* GCC's fast math makes isnan no-op.  Must use the fallback function.
 * Gwyddion works with finite arithmetic, but we need to detect NaNs upon
 * file import. */
#if (defined(GWY_HAVE_ISNAN) && !defined(__FAST_MATH__))
#define gwy_isnan isnan
#else
#define gwy_isnan gwy_math_fallback_isnan
#endif  /* GWY_HAVE_ISNAN */

/* This is a compiler thing, not system configuration.  So we cannot even
 * specify in gwyconfig.h whether the function is avilable or not.  All
 * solutions are bad; the cleanest one (do not ever expose the function) does
 * not allow using it in code built on Gwyddion and thus kind of defeats the
 * point... */
#if (!defined(__GNUC__) && !defined(__clang__))
#  define gwy_powi gwy_math_fallback_powi
#else
#  define gwy_powi __builtin_powi
#endif

/* From the precision standpoint this is not nice, but it should be used
 * for small fixed powers where the compiler should reduce it to a couple of
 * multiplications.  Good enough as a fallback. */
static inline double
gwy_math_fallback_powi(double x, int i)
{
    gdouble r = 1.0;
    gboolean negative = FALSE;
    if (!i)
        return 1.0;
    if (i < 0) {
        negative = TRUE;
        i = -i;
    }
    while (TRUE) {
        if (i & 1)
            r *= x;
        if (!(i >>= 1))
            break;
        x *= x;
    }
    return negative ? 1.0/r : r;
}

G_END_DECLS

#endif /* __GWY_MATH_FALLBACK_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
