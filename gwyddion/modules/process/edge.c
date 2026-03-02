/*
 *  $Id: edge.c 25638 2023-09-07 12:23:28Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define EDGE_RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register(void);
static void     edge           (GwyContainer *data,
                                GwyRunType run,
                                const gchar *name);
static void     laplacian_do   (GwyDataField *dfield,
                                GwyDataField *show);
static void     canny_do       (GwyDataField *dfield,
                                GwyDataField *show);
static void     rms_do         (GwyDataField *dfield,
                                GwyDataField *show);
static void     rms_edge_do    (GwyDataField *dfield,
                                GwyDataField *show);
static void     nonlinearity_do(GwyDataField *dfield,
                                GwyDataField *show);
static void     hough_lines_do (GwyDataField *dfield,
                                GwyDataField *show);
static void     harris_do      (GwyDataField *dfield,
                                GwyDataField *show);
static void     inclination_do (GwyDataField *dfield,
                                GwyDataField *show);
static void     step_do        (GwyDataField *dfield,
                                GwyDataField *show);
static void     sobel_do       (GwyDataField *dfield,
                                GwyDataField *show);
static void     prewitt_do     (GwyDataField *dfield,
                                GwyDataField *show);
static void     slope_map      (GwyContainer *data,
                                GwyRunType run);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Several edge detection methods (Laplacian of Gaussian, Canny, and some experimental), creates presentation."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.15",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, edge)

static gboolean
module_register(void)
{
    gwy_process_func_register("edge_laplacian",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Laplacian of Gaussian"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Laplacian of Gaussian step detection "
                                 "presentation"));
    gwy_process_func_register("edge_canny",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Canny"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Canny edge detection presentation"));
    gwy_process_func_register("edge_rms",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_RMS"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Local RMS value based step detection "
                                 "presentation"));
    gwy_process_func_register("edge_rms_edge",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/RMS _Edge"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Local RMS value based step detection with postprocessing"));
    gwy_process_func_register("edge_nonlinearity",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/Local _Nonlinearity"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Local nonlinearity based edge detection "
                                 "presentation"));
    gwy_process_func_register("edge_hough_lines",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Hough Lines"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              /* FIXME */
                              N_("Hough lines presentation"));
    gwy_process_func_register("edge_harris",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Harris Corner"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              /* FIXME */
                              N_("Harris corner presentation"));
    gwy_process_func_register("edge_inclination",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Inclination"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Local inclination visualization presentation"));
    gwy_process_func_register("edge_step",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Step"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Fine step detection presentation"));
    gwy_process_func_register("edge_sobel",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Sobel"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Sobel edge presentation"));
    gwy_process_func_register("edge_prewitt",
                              (GwyProcessFunc)&edge,
                              N_("/_Presentation/_Edge Detection/_Prewitt"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Prewitt edge presentation"));
    gwy_process_func_register("slope_map",
                              (GwyProcessFunc)&slope_map,
                              N_("/_Integral Transforms/Local Slope"),
                              NULL,
                              EDGE_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("First derivative slope transformation"));

    return TRUE;
}

static void
edge(GwyContainer *data, GwyRunType run, const gchar *name)
{
    static const struct {
        const gchar *name;
        void (*func)(GwyDataField *dfield, GwyDataField *show);
    }
    functions[] = {
        { "edge_canny",         canny_do,         },
        { "edge_harris",        harris_do,        },
        { "edge_hough_lines",   hough_lines_do,   },
        { "edge_inclination",   inclination_do,   },
        { "edge_laplacian",     laplacian_do,     },
        { "edge_nonlinearity",  nonlinearity_do,  },
        { "edge_rms",           rms_do,           },
        { "edge_rms_edge",      rms_edge_do,      },
        { "edge_step",          step_do,          },
        { "edge_sobel",         sobel_do,         },
        { "edge_prewitt",       prewitt_do,       },
    };
    GwyDataField *dfield, *showfield;
    GQuark dquark, squark;
    GwySIUnit *siunit;
    gint id;
    guint i;

    g_return_if_fail(run & EDGE_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     GWY_APP_SHOW_FIELD, &showfield,
                                     0);
    g_return_if_fail(dfield && dquark && squark);

    gwy_app_undo_qcheckpointv(data, 1, &squark);
    if (!showfield) {
        showfield = gwy_data_field_new_alike(dfield, FALSE);
        siunit = gwy_si_unit_new(NULL);
        gwy_data_field_set_si_unit_z(showfield, siunit);
        g_object_unref(siunit);
        gwy_container_pass_object(data, squark, showfield);
    }

    for (i = 0; i < G_N_ELEMENTS(functions); i++) {
        if (gwy_strequal(name, functions[i].name)) {
            functions[i].func(dfield, showfield);
            break;
        }
    }
    if (i == G_N_ELEMENTS(functions)) {
        g_warning("edge does not provide function `%s'", name);
        gwy_data_field_copy(dfield, showfield, FALSE);
    }

    gwy_data_field_normalize(showfield);
    gwy_data_field_data_changed(showfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

/* Note this is the limiting case when LoG reduces for discrete data just to Laplacian */
static void
laplacian_do(GwyDataField *dfield, GwyDataField *show)
{
    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_laplacian(show);
}

static void
canny_do(GwyDataField *dfield, GwyDataField *show)
{
    /* Now we use fixed threshold, but in future, there could be API with some setting. We could also do smooting
     * before applying filter.*/
    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_canny(show, 0.1);
}

static void
rms_do(GwyDataField *dfield, GwyDataField *show)
{
    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_rms(show, 5);
}

static void
rms_edge_do(GwyDataField *dfield, GwyDataField *show)
{
    GwyDataField *tmp;
    gint xres, yres, i, j;
    gdouble *d;
    const gdouble *t;
    gdouble s;

    gwy_data_field_copy(dfield, show, FALSE);
    xres = gwy_data_field_get_xres(show);
    yres = gwy_data_field_get_yres(show);

    tmp = gwy_data_field_duplicate(show);
    gwy_data_field_filter_rms(tmp, 5);
    t = gwy_data_field_get_data_const(tmp);
    d = gwy_data_field_get_data(show);
    for (i = 0; i < yres; i++) {
        gint iim = MAX(i-2, 0)*xres;
        gint iip = MIN(i+2, yres-1)*xres;
        gint ii = i*xres;

        for (j = 0; j < xres; j++) {
            gint jm = MAX(j-2, 0);
            gint jp = MIN(j+2, xres-1);

            s = t[ii + jm] + t[ii + jp] + t[iim + j] + t[iip + j]
                + (t[iim + jm] + t[iim + jp] + t[iip + jm] + t[iip + jp])/2.0;
            s /= 6.0;

            d[ii + j] = MAX(t[ii + j] - s, 0);
        }
    }
    g_object_unref(tmp);
}

static gdouble
fit_local_plane_by_pos(gint n,
                       const gint *xp, const gint *yp, const gdouble *z,
                       gdouble *bx, gdouble *by)
{
    gdouble m[12], b[4];
    gint i;

    gwy_clear(m, 6);
    gwy_clear(b, 4);
    for (i = 0; i < n; i++) {
        m[1] += xp[i];
        m[2] += xp[i]*xp[i];
        m[3] += yp[i];
        m[4] += xp[i]*yp[i];
        m[5] += yp[i]*yp[i];
        b[0] += z[i];
        b[1] += xp[i]*z[i];
        b[2] += yp[i]*z[i];
        b[3] += z[i]*z[i];
    }
    m[0] = n;
    gwy_assign(m + 6, m, 6);
    if (gwy_math_choleski_decompose(3, m))
        gwy_math_choleski_solve(3, m, b);
    else
        b[0] = b[1] = b[2] = 0.0;

    *bx = b[1];
    *by = b[2];
    return (b[3] - (b[0]*b[0]*m[6+0] + b[1]*b[1]*m[6+2] + b[2]*b[2]*m[6+5])
            - 2.0*(b[0]*b[1]*m[6+1] + b[0]*b[2]*m[6+3] + b[1]*b[2]*m[6+4]));
}

static void
nonlinearity_do(GwyDataField *dfield, GwyDataField *show)
{
    static const gdouble r = 2.5;
    gint xres, yres, i, j, size;
    gdouble qx, qy;
    gint *xp, *yp;
    gdouble *d, *z;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data(show);
    qx = gwy_data_field_get_dx(dfield);
    qy = gwy_data_field_get_dx(dfield);

    size = gwy_data_field_get_circular_area_size(r);
    z = g_new(gdouble, size);
    xp = g_new(gint, 2*size);
    yp = xp + size;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble bx, by, s0r;
            gint n;

            n = gwy_data_field_circular_area_extract_with_pos(dfield, j, i, r, z, xp, yp);
            s0r = fit_local_plane_by_pos(n, xp, yp, z, &bx, &by);
            bx /= qx;
            by /= qy;
            d[i*xres + j] = sqrt(MAX(s0r, 0.0)/(1.0 + bx*bx + by*by));
        }
    }
    g_free(xp);
    g_free(z);
}

static void
inclination_do(GwyDataField *dfield, GwyDataField *show)
{
    static const gdouble r = 2.5;
    gint xres, yres, i, j, size;
    gdouble qx, qy;
    gint *xp, *yp;
    gdouble *d, *z;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data(show);
    qx = gwy_data_field_get_dx(dfield);
    qy = gwy_data_field_get_dx(dfield);

    size = gwy_data_field_get_circular_area_size(r);
    z = g_new(gdouble, size);
    xp = g_new(gint, 2*size);
    yp = xp + size;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble bx, by;
            gint n;

            n = gwy_data_field_circular_area_extract_with_pos(dfield, j, i, r, z, xp, yp);
            fit_local_plane_by_pos(n, xp, yp, z, &bx, &by);
            bx /= qx;
            by /= qy;
            d[i*xres + j] = atan(hypot(bx, by));
        }
    }
    g_free(xp);
    g_free(z);
}

static void
step_do(GwyDataField *dfield, GwyDataField *show)
{
    static const gdouble r = 2.5;
    gint xres, yres, i, j, size;
    gdouble *d, *z;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data(show);

    size = gwy_data_field_get_circular_area_size(r);
    z = g_new(gdouble, size);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gint n;

            n = gwy_data_field_circular_area_extract(dfield, j, i, r, z);
            gwy_math_sort(n, z);
            d[i*xres + j] = sqrt(z[n-1 - n/3] - z[n/3]);
        }
    }
    g_free(z);
}

static void
hough_lines_do(GwyDataField *dfield, GwyDataField *show)
{
    GwyDataField *x_gradient, *y_gradient;

    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_canny(show, 0.1);

    x_gradient = gwy_data_field_duplicate(dfield);
    gwy_data_field_filter_sobel(x_gradient, GWY_ORIENTATION_HORIZONTAL);
    y_gradient = gwy_data_field_duplicate(dfield);
    gwy_data_field_filter_sobel(y_gradient, GWY_ORIENTATION_VERTICAL);

    gwy_data_field_hough_line_strenghten(show, x_gradient, y_gradient, 1, 0.2);
}
static void
harris_do(GwyDataField *dfield, GwyDataField *show)
{
    GwyDataField *x_gradient, *y_gradient;

    gwy_data_field_copy(dfield, show, FALSE);
    x_gradient = gwy_data_field_duplicate(dfield);
    gwy_data_field_filter_sobel(x_gradient, GWY_ORIENTATION_HORIZONTAL);
    y_gradient = gwy_data_field_duplicate(dfield);
    gwy_data_field_filter_sobel(y_gradient, GWY_ORIENTATION_VERTICAL);

    gwy_data_field_filter_harris(x_gradient, y_gradient, show, 20, 0.1);
}

static void
sobel_do(GwyDataField *dfield, GwyDataField *show)
{
    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_sobel_total(show);
}

static void
prewitt_do(GwyDataField *dfield, GwyDataField *show)
{
    gwy_data_field_copy(dfield, show, FALSE);
    gwy_data_field_filter_prewitt_total(show);
}

static void
slope_map(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *sfield, *buf;
    GwySIUnit *xyunit, *zunit;
    gint oldid, newid;

    g_return_if_fail(run & EDGE_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(dfield);

    sfield = gwy_data_field_new_alike(dfield, FALSE);
    buf = gwy_data_field_new_alike(dfield, FALSE);

    gwy_data_field_filter_slope(dfield, sfield, buf);
    gwy_data_field_hypot_of_fields(sfield, sfield, buf);
    g_object_unref(buf);

    xyunit = gwy_data_field_get_si_unit_xy(sfield);
    zunit = gwy_data_field_get_si_unit_z(sfield);
    gwy_si_unit_divide(zunit, xyunit, zunit);

    newid = gwy_app_data_browser_add_data_field(sfield, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("Slope map"));
    gwy_app_channel_log_add_proc(data, oldid, newid);
    g_object_unref(sfield);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
