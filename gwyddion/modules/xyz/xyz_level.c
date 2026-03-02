/*
 *  $Id: xyz_level.c 25481 2023-06-22 11:54:43Z yeti-dn $
 *  Copyright (C) 2016-2023 David Necas (Yeti).
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
#include <libprocess/surface.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

enum {
    PARAM_METHOD,
    PARAM_UPDATE_ALL,
};

typedef enum {
    XYZ_LEVEL_SUBTRACT = 0,
    XYZ_LEVEL_ROTATE   = 1,
} XYZLevelType;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    /* cached input data properties */
    gboolean same_units;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             execute             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             xyzfixzero          (GwyContainer *data,
                                             GwyRunType runtype);
static void             xyzzeromean         (GwyContainer *data,
                                             GwyRunType runtype);
static void             xyzlevel            (GwyContainer *data,
                                             GwyRunType runtype);
static void             level_rotate_xyz    (GwySurface *surface,
                                             gdouble bx,
                                             gdouble by,
                                             const GwyXYZ *c);
static void             rotate_xyz          (GwySurface *surface,
                                             const GwyXYZ *u,
                                             const GwyXYZ *c,
                                             gdouble phi);
static void             find_plane_coeffs   (GwySurface *surface,
                                             gdouble *a,
                                             gdouble *bx,
                                             gdouble *by,
                                             GwyXYZ *c);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Simple XYZ data leveling."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, xyz_level)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_fix_zero",
                          (GwyXYZFunc)&xyzfixzero,
                          N_("/Fix _Zero"),
                          GWY_STOCK_FIX_ZERO,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Shift minimum data value to zero"));
    gwy_xyz_func_register("xyz_zero_mean",
                          (GwyXYZFunc)&xyzzeromean,
                          N_("/Zero _Mean Value"),
                          GWY_STOCK_ZERO_MEAN,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Shift mean data value to zero"));
    gwy_xyz_func_register("xyz_level",
                          (GwyXYZFunc)&xyzlevel,
                          N_("/Plane _Level..."),
                          GWY_STOCK_LEVEL,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Level data by mean plane correction"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Plane subtraction"), XYZ_LEVEL_SUBTRACT, },
        { N_("Rotation"),          XYZ_LEVEL_ROTATE,   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Method"),
                              methods, G_N_ELEMENTS(methods), XYZ_LEVEL_ROTATE);
    gwy_param_def_add_boolean(paramdef, PARAM_UPDATE_ALL, "update_all", _("Update X and Y of _all compatible data"),
                              TRUE);

    return paramdef;
}

static void
xyzfixzero(GwyContainer *data, G_GNUC_UNUSED GwyRunType runtype)
{
    GwySurface *surface = NULL;
    GQuark quark;
    gint id;
    GwyXYZ *xyz;
    guint k, n;
    gdouble zmin, zmax;

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    quark = gwy_app_get_surface_key_for_id(id);
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    gwy_surface_get_min_max(surface, &zmin, &zmax);
    xyz = gwy_surface_get_data(surface);
    n = gwy_surface_get_npoints(surface);
    for (k = 0; k < n; k++)
        xyz[k].z -= zmin;

    gwy_surface_data_changed(surface);
}

/* FIXME: We should use mean weighted by area.  But that must wait until we can do such thing... */
static void
xyzzeromean(GwyContainer *data, G_GNUC_UNUSED GwyRunType runtype)
{
    GwySurface *surface = NULL;
    GQuark quark;
    gint id;
    GwyXYZ *xyz;
    guint k, n;
    gdouble zmean = 0.0;

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(surface));

    quark = gwy_app_get_surface_key_for_id(id);
    gwy_app_undo_qcheckpointv(data, 1, &quark);

    xyz = gwy_surface_get_data(surface);
    n = gwy_surface_get_npoints(surface);
    for (k = 0; k < n; k++)
        zmean += xyz[k].z;
    zmean /= n;
    for (k = 0; k < n; k++)
        xyz[k].z -= zmean;

    gwy_surface_data_changed(surface);
}

static void
xyzlevel(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(args.surface));

    args.params = gwy_params_new_from_settings(define_module_params());
    args.same_units = gwy_si_unit_equal(gwy_surface_get_si_unit_xy(args.surface),
                                        gwy_surface_get_si_unit_z(args.surface));
    /* sanitise_params(), but do not bother with a single param. */
    if (!args.same_units) {
        gwy_params_set_enum(args.params, PARAM_METHOD, XYZ_LEVEL_SUBTRACT);
        gwy_params_set_enum(args.params, PARAM_UPDATE_ALL, FALSE);
    }
    if (runtype == GWY_RUN_INTERACTIVE && args.same_units) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome == GWY_DIALOG_PROCEED)
        execute(&args, data, id);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Level XYZ Data"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_METHOD);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE_ALL);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, TRUE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id < 0 || id == PARAM_METHOD) {
        gwy_param_table_set_sensitive(gui->table, PARAM_UPDATE_ALL,
                                      gwy_params_get_enum(gui->args->params, PARAM_METHOD) == XYZ_LEVEL_ROTATE);
    }
}

static gboolean
accelerate_convergence(const gdouble *seq, gdouble *x)
{
    gdouble q, qdiff;

    /* If we are converged do not prevent convergence in other components. */
    if (seq[2] == 0.0) {
        *x = 0.0;
        return TRUE;
    }

    if (seq[0]*seq[1] <= 0.0 || seq[1]*seq[2] <= 0.0)
        return FALSE;

    q = seq[2]/seq[1];
    if (q > 0.5)
        return FALSE;

    qdiff = fabs(log(q*seq[0]/seq[1]));
    gwy_debug("q %g (diff %g)", q, qdiff);
    if (qdiff > 0.5*q)
        return FALSE;

    *x = seq[2]/(1.0 - q);
    return TRUE;
}

static void
execute(ModuleArgs *args,
        GwyContainer *data,
        gint id)
{
    XYZLevelType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    gboolean update_all = gwy_params_get_boolean(args->params, PARAM_UPDATE_ALL);
    GQuark otherquark, quark = gwy_app_get_surface_key_for_id(id);
    GQuark *allquarks = NULL;
    GwySurface *surface = args->surface, *othersurface;
    GwyXYZ *xyz, c;
    const GwyXYZ *newxyz;
    guint k, n, kq, ns, nq = 0;
    gdouble a, bx, by, bxseq[3], byseq[3], bx_acc, by_acc;
    gint *ids = NULL;

    if (method == XYZ_LEVEL_SUBTRACT) {
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        find_plane_coeffs(surface, &a, &bx, &by, &c);
        gwy_debug("b %g %g", bx, by);
        xyz = gwy_surface_get_data(surface);
        n = gwy_surface_get_npoints(surface);
        for (k = 0; k < n; k++)
            xyz[k].z -= a + bx*xyz[k].x + by*xyz[k].y;
        gwy_surface_data_changed(surface);

        return;
    }

    if (update_all) {
        ids = gwy_app_data_browser_get_xyz_ids(data);
        for (kq = 0; ids[kq] > -1; kq++) {
            if (ids[kq] == id) {
                ids[nq++] = ids[kq];
            }
            else {
                otherquark = gwy_app_get_surface_key_for_id(ids[kq]);
                othersurface = gwy_container_get_object(data, otherquark);
                if (gwy_surface_xy_is_compatible(surface, othersurface))
                    ids[nq++] = ids[kq];
            }
        }
        ids[nq] = -1;

        g_assert(nq);
        allquarks = g_new(GQuark, nq);
        for (kq = 0; kq < nq; kq++)
            allquarks[kq] = gwy_app_get_surface_key_for_id(ids[kq]);
        gwy_app_undo_qcheckpointv(data, nq, allquarks);
        g_free(allquarks);
    }
    else
        gwy_app_undo_qcheckpointv(data, 1, &quark);

    /* find_plane_coeffs() calculates the mean plane in ordinary least-squares sense.  But this is not self-consistent
     * with rotation that should use total least squares.  Perform a few iterations. */
    ns = 0;
    for (k = 0; k < 20; k++) {
        find_plane_coeffs(surface, &a, &bx, &by, &c);
        gwy_debug("[%u] b %g %g", k, bx, by);
        if (ns < 3) {
            bxseq[ns] = bx;
            byseq[ns] = by;
            ns++;
        }
        else {
            bxseq[0] = bxseq[1];
            bxseq[1] = bxseq[2];
            bxseq[2] = bx;
            byseq[0] = byseq[1];
            byseq[1] = byseq[2];
            byseq[2] = by;
        }
        if (ns == 3) {
            if (accelerate_convergence(bxseq, &bx_acc)
                && accelerate_convergence(byseq, &by_acc)) {
                bx = bx_acc;
                by = by_acc;
                ns = 0;
                gwy_debug("acceleratied b %g %g", bx, by);
            }
        }

        level_rotate_xyz(surface, bx, by, &c);
        if (k > 1 && sqrt(bx*bx + by*by) < 1e-15)
            break;
    }

    gwy_surface_data_changed(surface);
    if (!update_all) {
        g_free(ids);
        return;
    }

    newxyz = gwy_surface_get_data_const(surface);
    n = gwy_surface_get_npoints(surface);
    for (kq = 0; kq < nq; kq++) {
        if (ids[kq] == id)
            continue;

        otherquark = gwy_app_get_surface_key_for_id(ids[kq]);
        othersurface = gwy_container_get_object(data, otherquark);
        xyz = gwy_surface_get_data(othersurface);
        for (k = 0; k < n; k++) {
            xyz[k].x = newxyz[k].x;
            xyz[k].y = newxyz[k].y;
        }
        gwy_surface_data_changed(othersurface);
    }
    g_free(ids);
}

static void
level_rotate_xyz(GwySurface *surface, gdouble bx, gdouble by, const GwyXYZ *c)
{
    gdouble b = sqrt(bx*bx + by*by);
    GwyXYZ u;

    if (!b)
        return;

    u.x = -by/b;
    u.y = bx/b;
    u.z = 0.0;
    rotate_xyz(surface, &u, c, atan2(b, 1.0));
}

static void
rotate_xyz(GwySurface *surface, const GwyXYZ *u, const GwyXYZ *c, gdouble phi)
{
    GwyXYZ *xyz = gwy_surface_get_data(surface);
    guint k, n = gwy_surface_get_npoints(surface);
    gdouble cphi = cos(phi), sphi = sin(phi);
    gdouble axx = cphi + u->x*u->x*(1.0 - cphi);
    gdouble axy = u->x*u->y*(1.0 - cphi) - u->z*sphi;
    gdouble axz = u->x*u->z*(1.0 - cphi) + u->y*sphi;
    gdouble ayx = u->y*u->x*(1.0 - cphi) + u->z*sphi;
    gdouble ayy = cphi + u->y*u->y*(1.0 - cphi);
    gdouble ayz = u->y*u->z*(1.0 - cphi) - u->x*sphi;
    gdouble azx = u->z*u->x*(1.0 - cphi) - u->y*sphi;
    gdouble azy = u->z*u->y*(1.0 - cphi) + u->x*sphi;
    gdouble azz = cphi + u->z*u->z*(1.0 - cphi);

    for (k = 0; k < n; k++) {
        gdouble x = xyz[k].x - c->x;
        gdouble y = xyz[k].y - c->y;
        gdouble z = xyz[k].z - c->z;

        xyz[k].x = axx*x + axy*y + axz*z + c->x;
        xyz[k].y = ayx*x + ayy*y + ayz*z + c->y;
        xyz[k].z = azx*x + azy*y + azz*z + c->z;
    }
}

static void
find_plane_coeffs(GwySurface *surface, gdouble *a, gdouble *bx, gdouble *by, GwyXYZ *c)
{
    const GwyXYZ *xyz = gwy_surface_get_data_const(surface);
    guint k, n = gwy_surface_get_npoints(surface);
    gdouble sx, sy, sz, sxx, sxy, syy, sxz, syz, D;

    sx = sy = sz = 0.0;
    for (k = 0; k < n; k++) {
        sx += xyz[k].x;
        sy += xyz[k].y;
        sz += xyz[k].z;
    }
    sx /= n;
    sy /= n;
    sz /= n;

    sxx = sxy = syy = sxz = syz = 0.0;
    for (k = 0; k < n; k++) {
        gdouble x = xyz[k].x - sx;
        gdouble y = xyz[k].y - sy;
        gdouble z = xyz[k].z;

        sxx += x*x;
        syy += y*y;
        sxy += x*y;
        sxz += x*z;
        syz += y*z;
    }

    D = sxx*syy - sxy*sxy;
    *bx = (syy*sxz - sxy*syz)/D;
    *by = (sxx*syz - sxy*sxz)/D;
    *a = -(sx*(*bx) + sy*(*by));
    c->x = sx;
    c->y = sy;
    c->z = sz;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
