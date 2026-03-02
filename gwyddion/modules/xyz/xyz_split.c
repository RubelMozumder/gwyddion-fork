/*
 *  $Id: xyz_split.c 26355 2024-05-21 08:23:55Z yeti-dn $
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
};

typedef enum {
    XYZ_SPLIT_LR = 0,
    XYZ_SPLIT_UD = 1,
} XYZSplitType;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    GwySurface *result_fw;
    GwySurface *result_rev;
} ModuleArgs;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             xyzsplit            (GwyContainer *data,
                                             GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("XYZ data split based on direction."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, xyz_split)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_split",
                          (GwyXYZFunc)&xyzsplit,
                          N_("/Split by Direction..."),
                          NULL,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Split XYZ data based on direction"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Left-right"),      XYZ_SPLIT_LR, },
        { N_("Upward-downward"), XYZ_SPLIT_UD, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Split direction"),
                              methods, G_N_ELEMENTS(methods), XYZ_SPLIT_LR);

    return paramdef;
}

static void
xyzsplit(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;
    XYZSplitType method;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(args.surface));

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome == GWY_DIALOG_PROCEED)
        execute(&args);

    method = gwy_params_get_enum(args.params, PARAM_METHOD);

    newid = gwy_app_data_browser_add_surface(args.result_fw, data, TRUE);
    if (method == XYZ_SPLIT_LR)
        gwy_app_set_surface_title(data, newid, _("Split right direction"));
    else
        gwy_app_set_surface_title(data, newid, _("Split down direction"));
    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);

    g_object_unref(args.result_fw);

    newid = gwy_app_data_browser_add_surface(args.result_rev, data, TRUE);
    if (method == XYZ_SPLIT_LR)
        gwy_app_set_surface_title(data, newid, _("Split left direction"));
    else
        gwy_app_set_surface_title(data, newid, _("Split up direction"));
    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);

    g_object_unref(args.result_rev);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Split XYZ Data")));
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_METHOD);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, TRUE, 0);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    XYZSplitType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    GwySurface *surface = args->surface;
    GwySurface *surf_fw, *surf_rev;
    const GwyXYZ *xyz;
    GwyXYZ *xyz_fw, *xyz_rev;
    guint k, n, nfw, nrev;

    xyz = gwy_surface_get_data_const(surface);
    n = gwy_surface_get_npoints(surface);

    nfw = nrev = 0;
    if (method == XYZ_SPLIT_LR) {
       for (k = 0; k < n-1; k++) {
          if (xyz[k+1].x > xyz[k].x)
              nfw++;
          if (xyz[k+1].x < xyz[k].x)
              nrev++;
       }
    } else {
       for (k = 0; k < n-1; k++) {
          if (xyz[k+1].y > xyz[k].y)
             nfw++;
          if (xyz[k+1].y < xyz[k].y)
              nrev++;
       }
    }

    gwy_debug("forward %d reverse %d", nfw, nrev);
    args->result_fw = surf_fw = gwy_surface_new_sized(nfw);
    xyz_fw = gwy_surface_get_data(surf_fw);
    gwy_surface_copy_units(surface, surf_fw);

    args->result_rev = surf_rev = gwy_surface_new_sized(nrev);
    xyz_rev = gwy_surface_get_data(surf_rev);
    gwy_surface_copy_units(surface, surf_rev);

    nfw = nrev = 0;
    if (method == XYZ_SPLIT_LR) {
       for (k = 0; k < n-1; k++) {
          if (xyz[k+1].x > xyz[k].x)
              xyz_fw[nfw++] = xyz[k];
          if (xyz[k+1].x < xyz[k].x)
              xyz_rev[nrev++] = xyz[k];
       }
    } else {
       for (k = 0; k < n-1; k++) {
          if (xyz[k+1].y > xyz[k].y)
              xyz_fw[nfw++] = xyz[k];
          if (xyz[k+1].y < xyz[k].y)
              xyz_rev[nrev++] = xyz[k];
       }
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
