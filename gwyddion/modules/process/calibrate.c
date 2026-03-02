/*
 *  $Id: calibrate.c 26320 2024-05-06 11:27:10Z yeti-dn $
 *  Copyright (C) 2003-2024 David Necas (Yeti), Petr Klapetek.
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
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <gtk/gtk.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* for compatibility checks */
#define EPSILON 1e-6

enum {
    PARAM_DIMS_MODE,
    PARAM_XREAL,
    PARAM_YREAL,
    PARAM_XRATIO,
    PARAM_YRATIO,
    PARAM_SQUARE,
    PARAM_XYUNIT,

    PARAM_OFFSETS_MODE,
    PARAM_XOFFSET,
    PARAM_YOFFSET,

    PARAM_TEMPLATE,

    PARAM_VALUE_MODE,
    PARAM_ZRANGE,
    PARAM_ZMIN,
    PARAM_ZSHIFT,
    PARAM_ZRATIO,
    PARAM_ZUNIT,

    PARAM_NEW_DATA,

    LABEL_DIMS,
    LABEL_VALUES,
    LABEL_OFFSETS,
};

/* This is a mix of values for all of dims/value/offset. */
typedef enum {
    MODE_KEEP         = 0,
    MODE_SET_RANGE    = 1,
    MODE_CALIBRATE    = 2,
    MODE_MATCH        = 3,
    MODE_PROPORTIONAL = 4,
    MODE_CLEAR        = 5,
} CalibrateMode;

typedef struct {
    GwyParams *params;
    /* Exactly one of the following is non-NULL. */
    GwyDataField *field;
    GwyLawn *lawn;
    GwySurface *surface;
    /* Chached input data parameters (pixel-related quantities unused for XYZ). */
    gdouble xreal;
    gdouble yreal;
    gdouble xoffset;
    gdouble yoffset;
    gdouble zmin;
    gdouble zmax;
    gint xres;
    gint yres;
    gint is_square;
    GwySIUnit *xyunit;
    GwySIUnit *zunit;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dims;
    GwyParamTable *table_offsets;
    GwyParamTable *table_value;
    GwySIValueFormat *xyvf;
    GwySIValueFormat *xycalvf;
    GwySIValueFormat *zvf;
    GwySIValueFormat *zcalvf;
} ModuleGUI;

static gboolean          module_register       (void);
static GwyParamDef*      define_image_params   (void);
static GwyParamDef*      define_cmap_params    (void);
static GwyParamDef*      define_xyz_params     (void);
static void              calibrate_image       (GwyContainer *data,
                                                GwyRunType runtype);
static void              calibrate_xyz         (GwyContainer *data,
                                                GwyRunType runtype);
static void              calibrate_cmap        (GwyContainer *data,
                                                GwyRunType runtype);
static GwyDialogOutcome  run_gui               (ModuleArgs *args);
static GwyParamTable*    make_table_dims       (ModuleArgs *args);
static GwyParamTable*    make_table_offsets    (ModuleArgs *args);
static GwyParamTable*    make_table_value      (ModuleArgs *args);
static GwySIValueFormat* get_format_xy         (ModuleArgs *args,
                                                GwySIUnitFormatStyle style,
                                                GwySIValueFormat *vf);
static GwySIValueFormat* get_format_z          (ModuleArgs *args,
                                                GwySIUnitFormatStyle style,
                                                GwySIValueFormat *vf);
static void              param_changed         (ModuleGUI *gui,
                                                gint id);
static void              dialog_response       (GwyDialog *dialog,
                                                gint response,
                                                ModuleGUI *gui);
static gboolean          template_image_filter (GwyContainer *data,
                                                gint id,
                                                gpointer user_data);
static gboolean          template_cmap_filter  (GwyContainer *data,
                                                gint id,
                                                gpointer user_data);
static void              init_xyparams_for_mode(ModuleArgs *args);
static void              init_zparams_for_mode (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Recalibrates scan lateral dimensions or value range."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "4.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, calibrate)

static gboolean
module_register(void)
{
    gwy_process_func_register("calibrate",
                              (GwyProcessFunc)&calibrate_image,
                              N_("/_Basic Operations/_Dimensions and Units..."),
                              GWY_STOCK_DATA_MEASURE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Change physical dimensions, units or value scale"));
    gwy_xyz_func_register("xyz_calibrate",
                          (GwyCurveMapFunc)&calibrate_xyz,
                          N_("/_Dimensions and Units..."),
                          GWY_STOCK_DATA_MEASURE,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Change physical dimensions or units"));
    gwy_curve_map_func_register("cmap_calibrate",
                                (GwyCurveMapFunc)&calibrate_cmap,
                                N_("/_Basic Operations/_Dimensions and Units..."),
                                GWY_STOCK_DATA_MEASURE,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Change physical dimensions or units"));

    return TRUE;
}

static void
define_xy_params(GwyParamDef *paramdef, gboolean have_match_mode)
{
    static const GwyEnum dims_modes_match[] = {
        { N_("Do not change"),            MODE_KEEP,      },
        { N_("Match pixel size"),         MODE_MATCH,     },
        { N_("Set dimensions"),           MODE_SET_RANGE, },
        { N_("Correct by factor"),        MODE_CALIBRATE, },
    };
    static const GwyEnum dims_modes_nomatch[] = {
        { N_("Do not change"),            MODE_KEEP,      },
        { N_("Set dimensions"),           MODE_SET_RANGE, },
        { N_("Correct by factor"),        MODE_CALIBRATE, },
    };
    static const GwyEnum offsets_modes[] = {
        { N_("Do not change"),         MODE_KEEP,         },
        { N_("Scale with dimensions"), MODE_PROPORTIONAL, },
        { N_("Set offsets"),           MODE_SET_RANGE,    },
        { N_("Clear offsets"),         MODE_CLEAR,        },
    };

    if (have_match_mode) {
        gwy_param_def_add_gwyenum(paramdef, PARAM_DIMS_MODE, "dims_mode", NULL,
                                  dims_modes_match, G_N_ELEMENTS(dims_modes_match), MODE_KEEP);
    }
    else {
        gwy_param_def_add_gwyenum(paramdef, PARAM_DIMS_MODE, "dims_mode", NULL,
                                  dims_modes_nomatch, G_N_ELEMENTS(dims_modes_nomatch), MODE_KEEP);
    }
    gwy_param_def_add_double(paramdef, PARAM_XREAL, "xreal", _("_Width"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YREAL, "yreal", _("_Height"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_XRATIO, "xratio", _("_X correction factor"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YRATIO, "yratio", _("_Y correction factor"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_unit(paramdef, PARAM_XYUNIT, "xyunit", _("_Dimensions unit"), NULL);

    gwy_param_def_add_gwyenum(paramdef, PARAM_OFFSETS_MODE, "offsets_mode", NULL,
                              offsets_modes, G_N_ELEMENTS(offsets_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_XOFFSET, "xoffset", _("X offset"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YOFFSET, "yoffset", _("Y offset"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
}

static void
define_z_params(GwyParamDef *paramdef)
{
    static const GwyEnum value_modes[] = {
        { N_("Do not change"),         MODE_KEEP,      },
        { N_("Set range"),             MODE_SET_RANGE, },
        { N_("Correct by factor"),     MODE_CALIBRATE, },
    };

    gwy_param_def_add_gwyenum(paramdef, PARAM_VALUE_MODE, "value_mode", NULL,
                              value_modes, G_N_ELEMENTS(value_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_ZRANGE, "zrange", _("Z _range"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ZMIN, "zmin", _("Z _minimum"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ZSHIFT, "zshift", _("Z shi_ft"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ZRATIO, "zratio", _("_Z correction factor"),
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("_Value unit"), NULL);
}

static GwyParamDef*
define_image_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    define_xy_params(paramdef, TRUE);
    define_z_params(paramdef);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE, "square", _("_Square pixels"), TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_TEMPLATE, "template", _("_Template"));
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_DATA, "new_image", _("Create new image"), FALSE);
    return paramdef;
}

static GwyParamDef*
define_xyz_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    define_xy_params(paramdef, FALSE);
    define_z_params(paramdef);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE, "square", _("_Equal XY factors"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_DATA, "new_image", _("Create new XYZ data"), FALSE);
    return paramdef;
}

static GwyParamDef*
define_cmap_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    define_xy_params(paramdef, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE, "square", _("_Square pixels"), TRUE);
    gwy_param_def_add_curve_map_id(paramdef, PARAM_TEMPLATE, "template", _("_Template"));
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_DATA, "new_cmap", _("Create new curve map"), FALSE);
    return paramdef;
}

static void
calibrate_one_image(GwyDataField *field, GwyParams *params, gboolean calibrate_z)
{
    if (gwy_params_get_enum(params, PARAM_DIMS_MODE) != MODE_KEEP) {
        gwy_data_field_set_xreal(field, gwy_params_get_double(params, PARAM_XREAL));
        gwy_data_field_set_yreal(field, gwy_params_get_double(params, PARAM_YREAL));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP) {
        gwy_data_field_set_xoffset(field, gwy_params_get_double(params, PARAM_XOFFSET));
        gwy_data_field_set_yoffset(field, gwy_params_get_double(params, PARAM_YOFFSET));
    }
    if (calibrate_z && gwy_params_get_enum(params, PARAM_VALUE_MODE) != MODE_KEEP) {
        gwy_data_field_multiply(field, gwy_params_get_double(params, PARAM_ZRATIO));
        gwy_data_field_add(field, gwy_params_get_double(params, PARAM_ZSHIFT));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), gwy_params_get_unit(params, PARAM_ZUNIT, NULL));
    }
}

static void
calibrate_image(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *fields[3];
    GQuark quarks[3];
    gint n, i, oldid, newid;
    ModuleArgs args;
    GwyDialogOutcome outcome;
    GwyParams *params;
    gboolean new_channel;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_KEY, quarks + 0,
                                     GWY_APP_MASK_FIELD_KEY, quarks + 1,
                                     GWY_APP_SHOW_FIELD_KEY, quarks + 2,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(fields[0]);

    gwy_clear(&args, 1);
    args.field = fields[0];
    args.xres = gwy_data_field_get_xres(args.field);
    args.yres = gwy_data_field_get_yres(args.field);
    args.xreal = gwy_data_field_get_xreal(args.field);
    args.yreal = gwy_data_field_get_yreal(args.field);
    args.xoffset = gwy_data_field_get_xoffset(args.field);
    args.yoffset = gwy_data_field_get_yoffset(args.field);
    gwy_data_field_get_min_max(args.field, &args.zmin, &args.zmax);
    args.xyunit = gwy_data_field_get_si_unit_xy(args.field);
    args.zunit = gwy_data_field_get_si_unit_z(args.field);
    args.is_square = (fabs(log(args.yreal/args.yres * args.xres/args.xreal)) <= EPSILON);
    args.params = params = gwy_params_new_from_settings(define_image_params());
    init_xyparams_for_mode(&args);
    init_zparams_for_mode(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    new_channel = gwy_params_get_boolean(params, PARAM_NEW_DATA);
    if (new_channel) {
        for (i = 0; i < 3; i++) {
            if (fields[i])
                fields[i] = gwy_data_field_duplicate(fields[i]);
        }
    }
    else {
        for (i = n = 0; i < 3; i++) {
            if (fields[i])
                quarks[n++] = quarks[i];
        }
        gwy_app_undo_qcheckpointv(data, n, quarks);
    }

    for (i = 0; i < 3; i++) {
        if (fields[i])
            calibrate_one_image(fields[i], params, i == 0);
    }

    if (new_channel) {
        newid = gwy_app_data_browser_add_data_field(fields[0], data, TRUE);
        gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_RANGE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                0);
        if (fields[1])
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), fields[1]);
        if (fields[2])
            gwy_container_set_object(data, gwy_app_get_show_key_for_id(newid), fields[2]);
        for (i = 0; i < 3; i++)
            GWY_OBJECT_UNREF(fields[i]);
        gwy_app_set_data_field_title(data, newid, _("Recalibrated Data"));
        gwy_app_channel_log_add_proc(data, oldid, newid);
    }
    else {
        for (i = 0; i < 3; i++) {
            if (fields[i])
                gwy_data_field_data_changed(fields[i]);
        }
        if (gwy_params_get_enum(params, PARAM_DIMS_MODE) != MODE_KEEP
            || gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP)
            gwy_app_data_clear_selections(data, oldid);

        gwy_app_channel_log_add_proc(data, oldid, oldid);
    }

end:
    g_object_unref(params);
}

static void
calibrate_one_xyz(GwySurface *surface, const ModuleArgs *args)
{
    GwyParams *params = args->params;

    if (gwy_params_get_enum(params, PARAM_DIMS_MODE) != MODE_KEEP
        || gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP) {
        gdouble newxreal = gwy_params_get_double(params, PARAM_XREAL);
        gdouble newyreal = gwy_params_get_double(params, PARAM_YREAL);
        gdouble newxoffset = gwy_params_get_double(params, PARAM_XOFFSET);
        gdouble newyoffset = gwy_params_get_double(params, PARAM_YOFFSET);
        gdouble xfactor, yfactor, xoffset, yoffset;
        xfactor = newxreal/args->xreal;
        yfactor = newyreal/args->yreal;
        xoffset = newxoffset - xfactor*args->xoffset;
        yoffset = newyoffset - yfactor*args->yoffset;
        gwy_surface_scale_xy(surface, xfactor, yfactor, xoffset, yoffset);
        gwy_si_unit_assign(gwy_surface_get_si_unit_xy(surface), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_VALUE_MODE) != MODE_KEEP) {
        gwy_surface_multiply(surface, gwy_params_get_double(params, PARAM_ZRATIO));
        gwy_surface_add(surface, gwy_params_get_double(params, PARAM_ZSHIFT));
        gwy_si_unit_assign(gwy_surface_get_si_unit_z(surface), gwy_params_get_unit(params, PARAM_ZUNIT, NULL));
    }
}

static void
calibrate_xyz(GwyContainer *data, GwyRunType runtype)
{
    GwySurface *surface;
    GQuark quark;
    gint oldid, newid;
    ModuleArgs args;
    GwyDialogOutcome outcome;
    GwyParams *params;
    gboolean new_channel;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_KEY, &quark,
                                     GWY_APP_SURFACE_ID, &oldid,
                                     0);
    g_return_if_fail(surface);

    gwy_clear(&args, 1);
    args.surface = surface;
    gwy_surface_get_xrange(surface, &args.xoffset, &args.xreal);
    args.xreal -= args.xoffset;
    gwy_surface_get_yrange(surface, &args.yoffset, &args.yreal);
    args.yreal -= args.yoffset;
    gwy_surface_get_min_max(surface, &args.zmin, &args.zmax);
    args.xyunit = gwy_surface_get_si_unit_xy(surface);
    args.zunit = gwy_surface_get_si_unit_z(surface);
    args.params = params = gwy_params_new_from_settings(define_xyz_params());
    init_xyparams_for_mode(&args);
    init_zparams_for_mode(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    new_channel = gwy_params_get_boolean(params, PARAM_NEW_DATA);
    if (new_channel)
        surface = gwy_surface_duplicate(surface);
    else
        gwy_app_undo_qcheckpointv(data, 1, &quark);

    calibrate_one_xyz(surface, &args);

    if (new_channel) {
        newid = gwy_app_data_browser_add_surface(surface, data, TRUE);
        g_object_unref(surface);
        gwy_app_sync_xyz_items(data, data, oldid, newid, FALSE,
                               GWY_DATA_ITEM_GRADIENT,
                               0);
        gwy_app_set_surface_title(data, newid, _("Recalibrated Data"));
        gwy_app_xyz_log_add_xyz(data, oldid, newid);
    }
    else {
        /* This relies on lateral parameter ids being the same for all data types! */
        gwy_surface_data_changed(surface);
        gwy_app_xyz_log_add_xyz(data, oldid, oldid);
    }

end:
    g_object_unref(params);
}

static void
calibrate_one_cmap(GwyLawn *lawn, GwyParams *params)
{
    if (gwy_params_get_enum(params, PARAM_DIMS_MODE) != MODE_KEEP) {
        gwy_lawn_set_xreal(lawn, gwy_params_get_double(params, PARAM_XREAL));
        gwy_lawn_set_yreal(lawn, gwy_params_get_double(params, PARAM_YREAL));
        gwy_si_unit_assign(gwy_lawn_get_si_unit_xy(lawn), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP) {
        gwy_lawn_set_xoffset(lawn, gwy_params_get_double(params, PARAM_XOFFSET));
        gwy_lawn_set_yoffset(lawn, gwy_params_get_double(params, PARAM_YOFFSET));
    }
}

static void
calibrate_cmap(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quark;
    gint oldid, newid;
    ModuleArgs args;
    GwyDialogOutcome outcome;
    GwyParams *params;
    gboolean new_channel;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_KEY, &quark,
                                     GWY_APP_LAWN_ID, &oldid,
                                     0);
    g_return_if_fail(lawn);

    gwy_clear(&args, 1);
    args.lawn = lawn;
    args.xres = gwy_lawn_get_xres(lawn);
    args.yres = gwy_lawn_get_yres(lawn);
    args.xreal = gwy_lawn_get_xreal(lawn);
    args.yreal = gwy_lawn_get_yreal(lawn);
    args.xoffset = gwy_lawn_get_xoffset(lawn);
    args.yoffset = gwy_lawn_get_yoffset(lawn);
    args.xyunit = gwy_lawn_get_si_unit_xy(lawn);
    args.is_square = (fabs(log(args.yreal/args.yres * args.xres/args.xreal)) <= EPSILON);
    args.params = params = gwy_params_new_from_settings(define_cmap_params());
    init_xyparams_for_mode(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    new_channel = gwy_params_get_boolean(params, PARAM_NEW_DATA);
    if (new_channel)
        lawn = gwy_lawn_duplicate(lawn);
    else
        gwy_app_undo_qcheckpointv(data, 1, &quark);

    calibrate_one_cmap(lawn, params);

    preview = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(oldid));
    if (new_channel) {
        preview = gwy_data_field_duplicate(preview);
        /* This relies on lateral parameter ids being the same for all data types! */
        calibrate_one_image(preview, params, FALSE);
        newid = gwy_app_data_browser_add_lawn(lawn, preview, data, TRUE);
        g_object_unref(lawn);
        g_object_unref(preview);
        gwy_app_sync_curve_map_items(data, data, oldid, newid, FALSE,
                                     GWY_DATA_ITEM_GRADIENT,
                                     0);
        gwy_app_set_lawn_title(data, newid, _("Recalibrated Data"));
        gwy_app_curve_map_log_add_curve_map(data, oldid, newid);
    }
    else {
        /* This relies on lateral parameter ids being the same for all data types! */
        calibrate_one_image(preview, params, FALSE);
        gwy_lawn_data_changed(lawn);
        gwy_data_field_data_changed(preview);
        gwy_app_curve_map_log_add_curve_map(data, oldid, oldid);
    }

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox;
    ModuleGUI gui;
    GwySIValueFormat *vf = NULL;
    gchar *buf;

    g_return_val_if_fail(!!args->lawn + !!args->surface + !!args->field == 1, GWY_DIALOG_CANCEL);

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Dimensions and Units"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    table = gui.table_dims = make_table_dims(args);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    if (args->field || args->surface) {
        table = gui.table_value = make_table_value(args);
        gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
        gwy_dialog_add_param_table(dialog, table);
    }

    table = gui.table_offsets = make_table_offsets(args);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_NEW_DATA);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    if (args->lawn)
        vf = gui.xyvf = gwy_lawn_get_value_format_xy(args->lawn, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    else if (args->surface)
        vf = gui.xyvf = gwy_surface_get_value_format_xy(args->surface, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    else
        vf = gui.xyvf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    buf = g_strdup_printf("%.*f%s%s × %.*f%s%s",
                          vf->precision, args->xreal/vf->magnitude, *vf->units ? " " : "", vf->units,
                          vf->precision, args->yreal/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_param_table_info_set_valuestr(gui.table_dims, LABEL_DIMS, buf);
    g_free(buf);

    buf = g_strdup_printf("(%.*f%s%s, %.*f%s%s)",
                          vf->precision, args->xoffset/vf->magnitude, *vf->units ? " " : "", vf->units,
                          vf->precision, args->yoffset/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_param_table_info_set_valuestr(gui.table_offsets, LABEL_OFFSETS, buf);
    g_free(buf);

    if (gui.table_value) {
        if (args->surface)
            vf = gui.zvf = gwy_surface_get_value_format_z(args->surface, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
        else
            vf = gui.zvf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
        buf = g_strdup_printf("[%.*f%s%s, %.*f%s%s]",
                              vf->precision, args->zmin/vf->magnitude, *vf->units ? " " : "", vf->units,
                              vf->precision, args->zmax/vf->magnitude, *vf->units ? " " : "", vf->units);
        gwy_param_table_info_set_valuestr(gui.table_value, LABEL_VALUES, buf);
        g_free(buf);
    }

    g_signal_connect_swapped(gui.table_dims, "param-changed", G_CALLBACK(param_changed), &gui);
    if (gui.table_value)
        g_signal_connect_swapped(gui.table_value, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_offsets, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    GWY_SI_VALUE_FORMAT_FREE(gui.xyvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.xycalvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.zvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.zcalvf);

    return outcome;
}

static GwyParamTable*
make_table_dims(ModuleArgs *args)
{
    static const gint noreset[] = {
        PARAM_XREAL, PARAM_YREAL, PARAM_XRATIO, PARAM_YRATIO, PARAM_SQUARE, PARAM_TEMPLATE, PARAM_XYUNIT
    };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Dimensions"));
    /* TODO: Give adjective context to Current. */
    /* TRANSLATORS: Current is an adjective here (as in the current value). */
    gwy_param_table_append_info(table, LABEL_DIMS, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_DIMS_MODE, MODE_KEEP);
    if (!args->surface)
        gwy_param_table_append_radio_item(table, PARAM_DIMS_MODE, MODE_MATCH);
    if (args->lawn) {
        gwy_param_table_append_curve_map_id(table, PARAM_TEMPLATE);
        gwy_param_table_data_id_set_filter(table, PARAM_TEMPLATE, template_cmap_filter, args->lawn, NULL);
    }
    else if (args->surface) {
        /* We do not have templates for GwySurface. */
    }
    else {
        gwy_param_table_append_image_id(table, PARAM_TEMPLATE);
        gwy_param_table_data_id_set_filter(table, PARAM_TEMPLATE, template_image_filter, args->field, NULL);
    }
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_DIMS_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_XREAL);
    gwy_param_table_append_entry(table, PARAM_YREAL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_DIMS_MODE, MODE_CALIBRATE);
    gwy_param_table_append_entry(table, PARAM_XRATIO);
    gwy_param_table_append_entry(table, PARAM_YRATIO);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_SQUARE);
    gwy_param_table_append_unit_chooser(table, PARAM_XYUNIT);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static GwyParamTable*
make_table_offsets(ModuleArgs *args)
{
    static const gint noreset[] = { PARAM_XOFFSET, PARAM_YOFFSET };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Offsets"));
    gwy_param_table_append_info(table, LABEL_OFFSETS, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_OFFSETS_MODE, MODE_KEEP);
    gwy_param_table_append_radio_item(table, PARAM_OFFSETS_MODE, MODE_CLEAR);
    gwy_param_table_append_radio_item(table, PARAM_OFFSETS_MODE, MODE_PROPORTIONAL);
    gwy_param_table_append_radio_item(table, PARAM_OFFSETS_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_XOFFSET);
    gwy_param_table_append_entry(table, PARAM_YOFFSET);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static GwyParamTable*
make_table_value(ModuleArgs *args)
{
    static const gint noreset[] = { PARAM_ZRANGE, PARAM_ZMIN, PARAM_ZSHIFT, PARAM_ZRATIO, PARAM_ZUNIT };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Value Range"));
    gwy_param_table_append_info(table, LABEL_VALUES, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_KEEP);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_ZMIN);
    gwy_param_table_append_entry(table, PARAM_ZRANGE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_CALIBRATE);
    gwy_param_table_append_entry(table, PARAM_ZRATIO);
    gwy_param_table_append_entry(table, PARAM_ZSHIFT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static void
update_xy_formats(ModuleGUI *gui)
{
    gwy_param_table_entry_set_value_format(gui->table_dims, PARAM_XREAL, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_dims, PARAM_YREAL, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_dims, PARAM_XRATIO, gui->xycalvf);
    gwy_param_table_entry_set_value_format(gui->table_dims, PARAM_YRATIO, gui->xycalvf);
    gwy_param_table_entry_set_value_format(gui->table_offsets, PARAM_XOFFSET, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_offsets, PARAM_YOFFSET, gui->xyvf);
}

static void
update_z_formats(ModuleGUI *gui)
{
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_ZRANGE, gui->zvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_ZMIN, gui->zvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_ZSHIFT, gui->zvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_ZRATIO, gui->zcalvf);
}

static void
param_changed_xy(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table_dims = gui->table_dims, *table_offsets = gui->table_offsets;
    GwySIUnit *unit, *unitcal;
    GwySIValueFormat *vf;
    gdouble xreal, yreal, m;
    gboolean square, xreal_changed = FALSE, yreal_changed = FALSE, use_template = FALSE, have_template = FALSE;
    CalibrateMode mode, offsets_mode;
    gint power10;

    xreal = gwy_params_get_double(params, PARAM_XREAL);
    yreal = gwy_params_get_double(params, PARAM_YREAL);
    mode = gwy_params_get_enum(params, PARAM_DIMS_MODE);

    if (id < 0) {
        /* XXX: This is a bit weird.  Param table should probably allow checking the filter state earlier. */
        if (args->lawn)
            have_template = !!gwy_params_get_curve_map(params, PARAM_TEMPLATE);
        else if (args->field)
            have_template = !!gwy_params_get_image(params, PARAM_TEMPLATE);

        if (!have_template) {
            if (!args->surface)
                gwy_param_table_radio_set_sensitive(table_dims, PARAM_DIMS_MODE, MODE_MATCH, FALSE);
            if (mode == MODE_MATCH)
                gwy_param_table_set_enum(table_dims, PARAM_DIMS_MODE, (mode = MODE_KEEP));
        }
    }

    if (id < 0 || id == PARAM_DIMS_MODE) {
        gwy_param_table_set_sensitive(table_dims, PARAM_XREAL, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_dims, PARAM_YREAL, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_dims, PARAM_SQUARE, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_dims, PARAM_XRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_dims, PARAM_YRATIO, mode == MODE_CALIBRATE);
        if (args->lawn || args->field)
            gwy_param_table_set_sensitive(table_dims, PARAM_TEMPLATE, mode == MODE_MATCH);
        gwy_param_table_set_sensitive(table_dims, PARAM_XYUNIT, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_dims, PARAM_XREAL, (xreal = args->xreal));
            gwy_param_table_set_double(table_dims, PARAM_YREAL, (yreal = args->yreal));
            xreal_changed = yreal_changed = TRUE;
        }
        else if (mode == MODE_MATCH)
            use_template = TRUE;
        /* When switching to other modes, the values should be already consistent. */
    }
    if (use_template || id == PARAM_TEMPLATE) {
        /* We should only get here with fields and lawns. */
        if (args->lawn) {
            GwyLawn *template = gwy_params_get_curve_map(params, PARAM_TEMPLATE);
            gwy_param_table_set_double(table_dims, PARAM_XREAL,
                                       (xreal = gwy_lawn_get_dx(template)*gwy_lawn_get_xres(args->lawn)));
            gwy_param_table_set_double(table_dims, PARAM_YREAL,
                                       (yreal = gwy_lawn_get_dy(template)*gwy_lawn_get_yres(args->lawn)));
            vf = gwy_lawn_get_value_format_xy(template, GWY_SI_UNIT_FORMAT_PLAIN, NULL);
        }
        else {
            GwyDataField *template = gwy_params_get_image(params, PARAM_TEMPLATE);
            gwy_param_table_set_double(table_dims, PARAM_XREAL,
                                       (xreal = gwy_data_field_get_dx(template)*gwy_data_field_get_xres(args->field)));
            gwy_param_table_set_double(table_dims, PARAM_YREAL,
                                       (yreal = gwy_data_field_get_dy(template)*gwy_data_field_get_yres(args->field)));
            vf = gwy_data_field_get_value_format_xy(template, GWY_SI_UNIT_FORMAT_PLAIN, NULL);
        }
        gwy_param_table_set_string(table_dims, PARAM_XYUNIT, vf->units);
        gwy_si_unit_value_format_free(vf);
        xreal_changed = yreal_changed = TRUE;
    }

    offsets_mode = gwy_params_get_enum(params, PARAM_OFFSETS_MODE);
    if (id < 0 || id == PARAM_OFFSETS_MODE) {
        mode = offsets_mode;
        gwy_param_table_set_sensitive(table_offsets, PARAM_XOFFSET, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_offsets, PARAM_YOFFSET, mode == MODE_SET_RANGE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_offsets, PARAM_XOFFSET, args->xoffset);
            gwy_param_table_set_double(table_offsets, PARAM_YOFFSET, args->yoffset);
        }
        else if (mode == MODE_CLEAR) {
            gwy_param_table_set_double(table_offsets, PARAM_XOFFSET, 0.0);
            gwy_param_table_set_double(table_offsets, PARAM_YOFFSET, 0.0);
        }
        else if (mode == MODE_PROPORTIONAL) {
            gwy_param_table_set_double(table_offsets, PARAM_XOFFSET, xreal/args->xreal * args->xoffset);
            gwy_param_table_set_double(table_offsets, PARAM_YOFFSET, yreal/args->yreal * args->yoffset);
        }
        /* When switching to other modes, the values should be already consistent. */
    }

    /* Do not need to consider id = -1 because we have an explicit reset handler. */
    square = gwy_params_get_boolean(params, PARAM_SQUARE);
    if (id == PARAM_SQUARE) {
        if (square) {
            if (args->surface) {
                /* For Surface square means something different: the same x and y ratios. */
                gwy_param_table_set_double(table_dims, PARAM_YRATIO, xreal/args->xreal);
                gwy_param_table_set_double(table_dims, PARAM_YREAL, (yreal = xreal/args->xreal * args->yreal));
            }
            else {
                gwy_param_table_set_double(table_dims, PARAM_YREAL, (yreal = xreal/args->xres * args->yres));
                gwy_param_table_set_double(table_dims, PARAM_YRATIO, yreal/args->yreal);
            }
            yreal_changed = TRUE;
        }
    }

    if (id == PARAM_XREAL || xreal_changed) {
        gwy_param_table_set_double(table_dims, PARAM_XRATIO, xreal/args->xreal);
        xreal_changed = TRUE;
    }
    if (id == PARAM_YREAL || yreal_changed) {
        gwy_param_table_set_double(table_dims, PARAM_YRATIO, yreal/args->yreal);
        yreal_changed = TRUE;
    }
    if (id == PARAM_XRATIO) {
        xreal = args->xreal * gwy_params_get_double(params, PARAM_XRATIO);
        gwy_param_table_set_double(table_dims, PARAM_XREAL, xreal);
        xreal_changed = TRUE;
    }
    if (id == PARAM_YRATIO) {
        yreal = args->yreal * gwy_params_get_double(params, PARAM_YRATIO);
        gwy_param_table_set_double(table_dims, PARAM_YREAL, yreal);
        yreal_changed = TRUE;
    }
    /* This can do some redundant updates but we do not care because they are idempotent. */
    if (args->surface) {
        /* For Surface square means something different: the same x and y ratios. */
        if (square && xreal_changed) {
            gwy_param_table_set_double(table_dims, PARAM_YRATIO, xreal/args->xreal);
            gwy_param_table_set_double(table_dims, PARAM_YREAL, (yreal = xreal/args->xreal * args->yreal));
            yreal_changed = TRUE;
        }
        if (square && yreal_changed) {
            gwy_param_table_set_double(table_dims, PARAM_XRATIO, yreal/args->yreal);
            gwy_param_table_set_double(table_dims, PARAM_YREAL, (xreal = yreal/args->yreal * args->xreal));
            xreal_changed = TRUE;
        }
    }
    else {
        if (square && xreal_changed) {
            gwy_param_table_set_double(table_dims, PARAM_YREAL, (yreal = xreal/args->xres * args->yres));
            gwy_param_table_set_double(table_dims, PARAM_YRATIO, yreal/args->yreal);
            yreal_changed = TRUE;
        }
        if (square && yreal_changed) {
            gwy_param_table_set_double(table_dims, PARAM_XREAL, (xreal = yreal/args->yres * args->xres));
            gwy_param_table_set_double(table_dims, PARAM_XRATIO, xreal/args->xreal);
            xreal_changed = TRUE;
        }
    }
    if (offsets_mode == MODE_PROPORTIONAL && xreal_changed)
        gwy_param_table_set_double(table_offsets, PARAM_XOFFSET, xreal/args->xreal * args->xoffset);
    if (offsets_mode == MODE_PROPORTIONAL && yreal_changed)
        gwy_param_table_set_double(table_offsets, PARAM_YOFFSET, yreal/args->yreal * args->yoffset);

    /* Units are mostly just a presentational aspect.  When the user changes units we do not change any value. */
    if (id < 0 || id == PARAM_XYUNIT || xreal_changed || yreal_changed) {
        gwy_debug("changing units (id=%d, xreal_changed=%d, yreal_changed=%d)", id, xreal_changed, yreal_changed);
        unit = gwy_params_get_unit(params, PARAM_XYUNIT, &power10);
        unitcal = gwy_si_unit_divide(unit, args->xyunit, NULL);
        gui->xyvf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, gui->xyvf);
        gui->xyvf->precision = 4;
        m = 5.0*gwy_params_get_double(params, PARAM_XRATIO);
        gui->xycalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, m, 6, gui->xycalvf);
        gwy_debug("XY %g (%d) [%s] for %g", gui->xycalvf->magnitude, gui->xycalvf->precision, gui->xycalvf->units, m);
        g_object_unref(unitcal);
        update_xy_formats(gui);
    }
}

static void
param_changed_z(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table_value = gui->table_value;
    GwySIUnit *unit, *unitcal;
    gdouble m, zmin, zratio, zshift, zrange;
    CalibrateMode mode;
    gint power10;

    if (id < 0 || id == PARAM_VALUE_MODE) {
        mode = gwy_params_get_enum(params, PARAM_VALUE_MODE);
        gwy_param_table_set_sensitive(table_value, PARAM_ZRANGE, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_value, PARAM_ZMIN, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_value, PARAM_ZRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_value, PARAM_ZSHIFT, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_value, PARAM_ZUNIT, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_value, PARAM_ZRANGE, args->zmax - args->zmin);
            gwy_param_table_set_double(table_value, PARAM_ZMIN, args->zmin);
            gwy_param_table_set_double(table_value, PARAM_ZRATIO, 1.0);
            gwy_param_table_set_double(table_value, PARAM_ZSHIFT, 0.0);
        }
        /* When switching to other modes, the values should be already consistent. */
    }

    if (id == PARAM_ZMIN) {
        zmin = gwy_params_get_double(params, PARAM_ZMIN);
        zratio = gwy_params_get_double(params, PARAM_ZRATIO);
        gwy_param_table_set_double(table_value, PARAM_ZSHIFT, zmin - zratio*args->zmin);
    }
    if (id == PARAM_ZSHIFT) {
        zshift = gwy_params_get_double(params, PARAM_ZSHIFT);
        zratio = gwy_params_get_double(params, PARAM_ZRATIO);
        gwy_param_table_set_double(table_value, PARAM_ZMIN, zratio*args->zmin + zshift);
    }
    if (id == PARAM_ZRATIO) {
        zshift = gwy_params_get_double(params, PARAM_ZSHIFT);
        zratio = gwy_params_get_double(params, PARAM_ZRATIO);
        gwy_param_table_set_double(table_value, PARAM_ZMIN, zratio*args->zmin + zshift);
        gwy_param_table_set_double(table_value, PARAM_ZRANGE, zratio*(args->zmax - args->zmin));
    }
    if (id == PARAM_ZRANGE) {
        zmin = gwy_params_get_double(params, PARAM_ZMIN);
        zrange = gwy_params_get_double(params, PARAM_ZRANGE);
        if (args->zmax > args->zmin)
            gwy_param_table_set_double(table_value, PARAM_ZRATIO, (zratio = zrange/(args->zmax - args->zmin)));
        else
            gwy_param_table_set_double(table_value, PARAM_ZRATIO, (zratio = 1.0));
        gwy_param_table_set_double(table_value, PARAM_ZSHIFT, zmin - zratio*args->zmin);
    }

    /* Units are mostly just a presentational aspect.  When the user changes units we do not change any value. */
    if (id < 0 || id == PARAM_ZUNIT || id == PARAM_ZRANGE || id == PARAM_ZRATIO || id == PARAM_VALUE_MODE) {
        unit = gwy_params_get_unit(params, PARAM_ZUNIT, &power10);
        unitcal = gwy_si_unit_divide(unit, args->zunit, NULL);
        gui->zvf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, gui->zvf);
        gui->zvf->precision = 4;
        m = 5.0*gwy_params_get_double(params, PARAM_ZRATIO);
        gui->zcalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, m, 6, gui->zcalvf);
        gwy_debug("Z %g (%d) [%s] for %g", gui->zcalvf->magnitude, gui->zcalvf->precision, gui->zcalvf->units, m);
        g_object_unref(unitcal);
        update_z_formats(gui);
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    param_changed_xy(gui, id);
    if (gui->table_value)
        param_changed_z(gui, id);
}

static GwySIValueFormat*
get_format_xy(ModuleArgs *args, GwySIUnitFormatStyle style, GwySIValueFormat *vf)
{
    if (args->lawn)
        return gwy_lawn_get_value_format_xy(args->lawn, style, vf);
    if (args->surface)
        return gwy_surface_get_value_format_xy(args->surface, style, vf);
    return gwy_data_field_get_value_format_xy(args->field, style, vf);
}

static GwySIValueFormat*
get_format_z(ModuleArgs *args, GwySIUnitFormatStyle style, GwySIValueFormat *vf)
{
    if (args->surface)
        return gwy_surface_get_value_format_z(args->surface, style, vf);
    return gwy_data_field_get_value_format_z(args->field, style, vf);
}

static void
reset_formats(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwySIUnit *unitcal;

    unitcal = gwy_si_unit_new(NULL);
    gui->xyvf = get_format_xy(args, GWY_SI_UNIT_FORMAT_PLAIN, gui->xyvf);
    gwy_param_table_set_string(gui->table_dims, PARAM_XYUNIT, gui->xyvf->units);
    gui->xyvf = get_format_xy(args, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->xyvf);
    gui->xyvf->precision += 2;
    gui->xycalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, 10.0, 6, gui->xycalvf);

    if (gui->table_value) {
        gui->zvf = get_format_z(args, GWY_SI_UNIT_FORMAT_PLAIN, gui->zvf);
        gwy_param_table_set_string(gui->table_value, PARAM_ZUNIT, gui->zvf->units);
        gui->zvf = get_format_z(args, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->zvf);
        gui->zvf->precision += 2;
        gui->zcalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, 10.0, 6, gui->zcalvf);
    }
    g_object_unref(unitcal);

    update_xy_formats(gui);
    if (gui->table_value)
        update_z_formats(gui);
    gwy_param_table_set_boolean(gui->table_dims, PARAM_SQUARE, args->is_square);
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        reset_formats(gui);
}

static gboolean
template_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherimage, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherimage))
        return FALSE;
    return otherimage != field;
}

static gboolean
template_cmap_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyLawn *othercmap, *lawn = (GwyLawn*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_lawn_key_for_id(id), &othercmap))
        return FALSE;
    return othercmap != lawn;
}

static void
init_xyparams_for_mode(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwySIValueFormat *vf = NULL;
    GwySIUnit *xyunit;
    gdouble newxreal, newyreal, newxoffset, newyoffset;
    CalibrateMode mode;

    /* Dimensions. */
    mode = gwy_params_get_enum(params, PARAM_DIMS_MODE);
    xyunit = gwy_params_get_unit(params, PARAM_XYUNIT, NULL);
    /* FIXME: When it is called the template is normally not set yet. */
    if (mode == MODE_MATCH) {
        if (args->lawn) {
            GwyLawn *template = gwy_params_get_curve_map(params, PARAM_TEMPLATE);
            if (template) {
                newxreal = gwy_lawn_get_dx(template)*gwy_lawn_get_xres(args->lawn);
                newyreal = gwy_lawn_get_dy(template)*gwy_lawn_get_yres(args->lawn);
                vf = gwy_lawn_get_value_format_xy(template, GWY_SI_UNIT_FORMAT_PLAIN, vf);
                gwy_params_set_unit(params, PARAM_XYUNIT, vf->units);
            }
            else
                mode = MODE_KEEP;
        }
        else {
            GwyDataField *template = gwy_params_get_image(params, PARAM_TEMPLATE);
            if (template) {
                newxreal = gwy_data_field_get_dx(template)*gwy_data_field_get_xres(args->field);
                newyreal = gwy_data_field_get_dy(template)*gwy_data_field_get_yres(args->field);
                vf = gwy_data_field_get_value_format_xy(template, GWY_SI_UNIT_FORMAT_PLAIN, vf);
                gwy_params_set_unit(params, PARAM_XYUNIT, vf->units);
            }
            else
                mode = MODE_KEEP;
        }
    }
    if (mode == MODE_KEEP) {
        newxreal = args->xreal;
        newyreal = args->yreal;
        vf = get_format_xy(args, GWY_SI_UNIT_FORMAT_PLAIN, vf);
    }
    else if (mode == MODE_SET_RANGE) {
        newxreal = gwy_params_get_double(params, PARAM_XREAL);
        newyreal = gwy_params_get_double(params, PARAM_YREAL);
        vf = gwy_si_unit_get_format_with_digits(xyunit, GWY_SI_UNIT_FORMAT_PLAIN, newxreal, 6, vf);
    }
    else if (mode == MODE_CALIBRATE) {
        newxreal = args->xreal*gwy_params_get_double(params, PARAM_XRATIO);
        newyreal = args->yreal*gwy_params_get_double(params, PARAM_YRATIO);
        vf = gwy_si_unit_get_format_with_digits(xyunit, GWY_SI_UNIT_FORMAT_PLAIN, newxreal, 6, vf);
    }
    else if (mode == MODE_MATCH) {
    }
    else {
        g_return_if_reached();
    }
    gwy_params_set_unit(params, PARAM_XYUNIT, vf->units);
    gwy_params_set_double(params, PARAM_XRATIO, newxreal/args->xreal);
    gwy_params_set_double(params, PARAM_YRATIO, newyreal/args->yreal);
    gwy_params_set_double(params, PARAM_XREAL, newxreal);
    gwy_params_set_double(params, PARAM_YREAL, newyreal);
    if (args->surface) {
        /* FIXME: What to do here? */
    }
    else
        gwy_params_set_boolean(params, PARAM_SQUARE, fabs(log(newyreal/args->yres * args->xres/newxreal)) <= EPSILON);

    /* Offsets. */
    mode = gwy_params_get_enum(params, PARAM_OFFSETS_MODE);
    if (mode == MODE_KEEP) {
        newxoffset = args->xoffset;
        newyoffset = args->yoffset;
    }
    else if (mode == MODE_CLEAR)
        newxoffset = newyoffset = 0.0;
    else if (mode == MODE_SET_RANGE) {
        newxoffset = gwy_params_get_double(params, PARAM_XOFFSET);
        newyoffset = gwy_params_get_double(params, PARAM_YOFFSET);
    }
    else if (mode == MODE_PROPORTIONAL) {
        newxoffset = args->xoffset*(newxreal/args->xreal);
        newyoffset = args->yoffset*(newyreal/args->yreal);
    }
    else {
        g_return_if_reached();
    }
    gwy_params_set_double(params, PARAM_XOFFSET, newxoffset);
    gwy_params_set_double(params, PARAM_YOFFSET, newyoffset);

    gwy_si_unit_value_format_free(vf);
}

static void
init_zparams_for_mode(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwySIValueFormat *vf = NULL;
    GwySIUnit *zunit;
    gdouble newzrange, zshift, zcal, newzmin;
    CalibrateMode mode;

    /* Values. */
    mode = gwy_params_get_enum(params, PARAM_VALUE_MODE);
    zunit = gwy_params_get_unit(params, PARAM_ZUNIT, NULL);
    if (mode == MODE_KEEP) {
        newzrange = args->zmax - args->zmin;
        zshift = 0.0;
        zcal = 1.0;
        newzmin = args->zmin;
        vf = get_format_z(args, GWY_SI_UNIT_FORMAT_PLAIN, vf);
    }
    else if (mode == MODE_SET_RANGE) {
        newzrange = gwy_params_get_double(params, PARAM_ZRANGE);
        newzmin = gwy_params_get_double(params, PARAM_ZMIN);
        zcal = (args->zmax - args->zmin > 0.0 ? newzrange/(args->zmax - args->zmin) : 0.0);
        zshift = newzmin - args->zmin;
        vf = gwy_si_unit_get_format_with_digits(zunit, GWY_SI_UNIT_FORMAT_PLAIN, newzrange, 6, vf);
    }
    else if (mode == MODE_CALIBRATE) {
        zcal = gwy_params_get_double(params, PARAM_ZRATIO);
        zshift = gwy_params_get_double(params, PARAM_ZSHIFT);
        newzrange = (args->zmax - args->zmin)*zcal;
        newzmin = args->zmin - zshift;
        vf = gwy_si_unit_get_format_with_digits(zunit, GWY_SI_UNIT_FORMAT_PLAIN, newzrange, 6, vf);
    }
    else {
        g_return_if_reached();
    }
    gwy_params_set_unit(params, PARAM_ZUNIT, vf->units);
    gwy_params_set_double(params, PARAM_ZRANGE, newzrange);
    gwy_params_set_double(params, PARAM_ZMIN, newzmin);
    gwy_params_set_double(params, PARAM_ZRATIO, zcal);
    gwy_params_set_double(params, PARAM_ZSHIFT, zshift);

    gwy_si_unit_value_format_free(vf);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
