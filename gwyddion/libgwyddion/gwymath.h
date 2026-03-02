/*
 *  $Id: gwymath.h 25074 2022-10-11 13:46:47Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_MATH_H__
#define __GWY_MATH_H__

#include <glib.h>
#include <glib-object.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyddionenums.h>
#include <math.h>

#include <libgwyddion/gwymathfallback.h>

G_BEGIN_DECLS

#ifndef GWY_DISABLE_DEPRECATED
#define ROUND(x) ((gint)floor((x) + 0.5))
#endif

#define GWY_ROUND(x) ((gint)floor((x) + 0.5))

#define GWY_SQRT3 1.73205080756887729352744634150587236694280525381038
#define GWY_SQRT_PI 1.77245385090551602729816748334114518279754945612237

typedef struct {
    gdouble x;
    gdouble y;
} GwyXY;

typedef struct {
    gdouble x;
    gdouble y;
    gdouble z;
} GwyXYZ;

#define GWY_TYPE_XY (gwy_xy_get_type())

GType  gwy_xy_get_type(void)            G_GNUC_CONST;
GwyXY* gwy_xy_new     (gdouble x,
                       gdouble y)       G_GNUC_MALLOC;
GwyXY* gwy_xy_copy    (const GwyXY *xy) G_GNUC_MALLOC;
void   gwy_xy_free    (GwyXY *xy);

#define GWY_TYPE_XYZ (gwy_xyz_get_type())

GType   gwy_xyz_get_type(void)              G_GNUC_CONST;
GwyXYZ* gwy_xyz_new     (gdouble x,
                         gdouble y,
                         gdouble z)         G_GNUC_MALLOC;
GwyXYZ* gwy_xyz_copy    (const GwyXYZ *xyz) G_GNUC_MALLOC;
void    gwy_xyz_free    (GwyXYZ *xyz);

typedef gdouble (*GwyRealFunc)(gdouble x, gpointer user_data);

gdouble  gwy_math_humanize_numbers     (gdouble unit,
                                        gdouble maximum,
                                        gint *precision);
gboolean gwy_math_is_in_polygon        (gdouble x,
                                        gdouble y,
                                        const gdouble* poly,
                                        guint n);
gint     gwy_math_find_nearest_line    (gdouble x,
                                        gdouble y,
                                        gdouble *d2min,
                                        gint n,
                                        const gdouble *coords,
                                        const gdouble *metric);
gint     gwy_math_find_nearest_point   (gdouble x,
                                        gdouble y,
                                        gdouble *d2min,
                                        gint n,
                                        const gdouble *coords,
                                        const gdouble *metric);
gdouble* gwy_math_lin_solve            (gint n,
                                        const gdouble *matrix,
                                        const gdouble *rhs,
                                        gdouble *result);
gdouble* gwy_math_lin_solve_rewrite    (gint n,
                                        gdouble *matrix,
                                        gdouble *rhs,
                                        gdouble *result);
gboolean gwy_math_tridiag_solve_rewrite(gint n,
                                        gdouble *d,
                                        const gdouble *a,
                                        const gdouble *b,
                                        gdouble *rhs);
gdouble* gwy_math_fit_polynom          (gint ndata,
                                        const gdouble *xdata,
                                        const gdouble *ydata,
                                        gint n,
                                        gdouble *coeffs);
gboolean gwy_math_choleski_decompose   (gint n,
                                        gdouble *matrix);
void     gwy_math_choleski_solve       (gint n,
                                        const gdouble *decomp,
                                        gdouble *rhs);
gboolean gwy_math_choleski_invert      (gint n,
                                        gdouble *matrix);
guint    gwy_math_curvature            (const gdouble *coeffs,
                                        gdouble *kappa1,
                                        gdouble *kappa2,
                                        gdouble *phi1,
                                        gdouble *phi2,
                                        gdouble *xc,
                                        gdouble *yc,
                                        gdouble *zc);
guint    gwy_math_curvature_at_apex    (const gdouble *coeffs,
                                        gdouble *kappa1,
                                        gdouble *kappa2,
                                        gdouble *phi1,
                                        gdouble *phi2,
                                        gdouble *xc,
                                        gdouble *yc,
                                        gdouble *zc);
guint    gwy_math_curvature_at_origin  (const gdouble *coeffs,
                                        gdouble *kappa1,
                                        gdouble *kappa2,
                                        gdouble *phi1,
                                        gdouble *phi2);
gboolean gwy_math_refine_maximum_1d    (const gdouble *y,
                                        gdouble *x);
gboolean gwy_math_refine_maximum_2d    (const gdouble *z,
                                        gdouble *x,
                                        gdouble *y);
gboolean gwy_math_refine_maximum       (const gdouble *z,
                                        gdouble *x,
                                        gdouble *y);
gdouble  gwy_math_find_minimum_1d      (GwyRealFunc function,
                                        gdouble a,
                                        gdouble b,
                                        gpointer user_data);
guint*   gwy_check_regular_2d_grid     (const gdouble *coords,
                                        guint stride,
                                        guint n,
                                        gdouble tolerance,
                                        guint *xres,
                                        guint *yres,
                                        GwyXY *xymin,
                                        GwyXY *xystep)                         G_GNUC_MALLOC;
gdouble  gwy_math_median               (gsize n,
                                        gdouble *array);
gdouble  gwy_math_kth_rank             (gsize n,
                                        gdouble *array,
                                        gsize k);
void     gwy_math_kth_ranks            (gsize n,
                                        gdouble *array,
                                        guint nk,
                                        const guint *k,
                                        gdouble *values);
void     gwy_math_percentiles          (gsize n,
                                        gdouble *array,
                                        GwyPercentileInterpolationType interp,
                                        guint np,
                                        const gdouble *p,
                                        gdouble *values);
void     gwy_math_sort                 (gsize n,
                                        gdouble *array);
void     gwy_guint_sort                (gsize n,
                                        guint *array);
void     gwy_math_sort_with_index      (gsize n,
                                        gdouble *array,
                                        guint *index_array);
gdouble  gwy_math_trimmed_mean         (gsize n,
                                        gdouble *array,
                                        guint nlowest,
                                        guint nhighest);
gdouble  gwy_math_median_uncertainty   (gsize n,
                                        gdouble *array,
                                        gdouble *uarray);
gint     gwy_compare_double            (gconstpointer a,
                                        gconstpointer b);
gdouble  gwy_xlnx_int                  (guint x)                               G_GNUC_CONST;
gdouble  gwy_sinc                      (gdouble x)                             G_GNUC_CONST;
gdouble  gwy_canonicalize_angle        (gdouble phi,
                                        gboolean positive,
                                        gboolean oriented)                     G_GNUC_CONST;
guint    gwy_math_histogram            (const gdouble *values,
                                        guint n,
                                        gdouble min,
                                        gdouble max,
                                        guint nbins,
                                        guint *counts);

G_END_DECLS

#endif /* __GWY_MATH_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
