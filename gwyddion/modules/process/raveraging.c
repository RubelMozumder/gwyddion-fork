/*
 *  $Id: raveraging.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correct.h>
#include <libprocess/filters.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_ANGLE,
    PARAM_RADIUS,
    PARAM_INTERP,
};

typedef struct {
    gdouble xc;
    gdouble yc;
    gdouble phiscale;
    gdouble phioff;
} TransformData;

typedef struct {
    GwyDataField *field;
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             raveraging          (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             filter_radial       (GwyDataField *field,
                                             gdouble radius,
                                             gdouble angle,
                                             GwyInterpolationType interp);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Smooths images in polar coordinates."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, raveraging)

static gboolean
module_register(void)
{
    gwy_process_func_register("raveraging",
                              (GwyProcessFunc)&raveraging,
                              N_("/_Distortion/_Radial Smoothing..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Smooth image in polar coordinates"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("_Angle"), TRUE, 2, 10.0*G_PI/180.0);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("_Radius"), 0.0, 1000.0, 10.0);
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    return paramdef;
}

static void
raveraging(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field;
    GwyParams *params;
    ModuleArgs args;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    field = gwy_data_field_duplicate(field);
    filter_radial(field,
                  gwy_params_get_double(params, PARAM_RADIUS), gwy_params_get_double(params, PARAM_ANGLE),
                  gwy_params_get_enum(params, PARAM_INTERP));
    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    g_object_unref(field);
    gwy_app_set_data_field_title(data, newid, _("Radially smoothed"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Radial Smoothing"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_set_steps(table, PARAM_RADIUS, 0.1, 10.0);
    gwy_param_table_slider_set_digits(table, PARAM_RADIUS, 2);
    gwy_param_table_slider_add_alt(table, PARAM_RADIUS);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_RADIUS, args->field);

    gwy_param_table_append_slider(table, PARAM_ANGLE);
    gwy_param_table_slider_set_steps(table, PARAM_ANGLE, 0.1*G_PI/180.0, 10.0*G_PI/180.0);
    gwy_param_table_slider_set_digits(table, PARAM_ANGLE, 2);
    gwy_param_table_append_combo(table, PARAM_INTERP);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
raverage_rphi_to_xy(gdouble r, gdouble phi,
                    gdouble *x, gdouble *y,
                    gpointer user_data)
{
    const TransformData *td = (const TransformData*)user_data;

    *x = td->xc + (r - 0.5)*cos((phi - 0.5)*td->phiscale);
    *y = td->yc + (r - 0.5)*sin((phi - 0.5)*td->phiscale);
}

static void
raverage_xy_to_rphi(gdouble x, gdouble y,
                    gdouble *r, gdouble *phi,
                    gpointer user_data)
{
    const TransformData *td = (const TransformData*)user_data;
    gdouble rx = x - td->xc, ry = y - td->yc;

    *r = sqrt(rx*rx + ry*ry);
    *phi = td->phioff + atan2(-ry, -rx)/td->phiscale;
}

static void
filter_radial(GwyDataField *field, gdouble radius, gdouble angle, GwyInterpolationType interp)
{
    GwyDataField *rdfield, *erdfield;
    gint xres, yres, rres, ares;
    TransformData transformdata;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    rres = sqrt(xres*xres + yres*yres)/2;
    ares = (GWY_ROUND(G_PI*MAX(xres, yres)) + 1)/2*2;
    transformdata.phiscale = 2.0*G_PI/ares;
    transformdata.phioff = 1.5*ares + 0.5;
    transformdata.xc = xres/2 + 0.5;
    transformdata.yc = yres/2 + 0.5;

    rdfield = gwy_data_field_new(rres, ares, rres, ares, FALSE);
    gwy_data_field_distort(field, rdfield, raverage_rphi_to_xy, &transformdata,
                           interp, GWY_EXTERIOR_BORDER_EXTEND, 0.0);

    /* Additional 180 to cover large angular sigma. */
    erdfield = gwy_data_field_extend(rdfield, 0, 0, ares, ares, GWY_EXTERIOR_PERIODIC, 0.0, FALSE);
    g_object_unref(rdfield);

    if (radius > 0)
        gwy_data_field_row_gaussian(erdfield, radius);
    if (angle > 0)
        gwy_data_field_column_gaussian(erdfield, angle/(2.0*G_PI)*ares);

    gwy_data_field_distort(erdfield, field, raverage_xy_to_rphi, &transformdata,
                           interp, GWY_EXTERIOR_BORDER_EXTEND, 0.0);
    g_object_unref(erdfield);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
