/*
 *  $Id: volume_calibrate.c 26320 2024-05-06 11:27:10Z yeti-dn $
 *  Copyright (C) 2013-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* for compatibility checks */
#define EPSILON 1e-6

enum {
    PARAM_XY_MODE,
    PARAM_XREAL,
    PARAM_YREAL,
    PARAM_XRATIO,
    PARAM_YRATIO,
    PARAM_SQUARE,
    PARAM_XYUNIT,  /* XXX: Bricks can have different x and y units, but then it gets confusingly complex. */

    PARAM_Z_MODE,
    PARAM_ZREAL,
    PARAM_ZRATIO,
    PARAM_ZUNIT,

    PARAM_OFFSETS_MODE,
    PARAM_XOFFSET,
    PARAM_YOFFSET,
    PARAM_ZOFFSET,

    PARAM_XYTEMPLATE,
    PARAM_ZTEMPLATE,

    PARAM_VALUE_MODE,
    PARAM_WRANGE,
    PARAM_WMIN,
    PARAM_WSHIFT,
    PARAM_WRATIO,
    PARAM_WUNIT,

    PARAM_NEW_DATA,

    LABEL_XY,
    LABEL_Z,
    LABEL_VALUES,
    LABEL_OFFSETS,
};

/* This is a mix of values for all of xy/z/value/offset. */
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
    GwyBrick *brick;
    /* Chached input data parameters. */
    gdouble xreal;
    gdouble yreal;
    gdouble zreal;
    gdouble xoffset;
    gdouble yoffset;
    gdouble zoffset;
    gdouble wmin;
    gdouble wmax;
    gint xres;
    gint yres;
    gint zres;
    gint is_square;
    GwySIUnit *xyunit;
    GwySIUnit *zunit;
    GwySIUnit *wunit;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_xy;
    GwyParamTable *table_z;
    GwyParamTable *table_offsets;
    GwyParamTable *table_value;
    GwySIValueFormat *xyvf;
    GwySIValueFormat *xycalvf;
    GwySIValueFormat *zvf;
    GwySIValueFormat *zcalvf;
    GwySIValueFormat *wvf;
    GwySIValueFormat *wcalvf;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             volcal              (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static GwyParamTable*   make_table_xy       (ModuleArgs *args);
static GwyParamTable*   make_table_z        (ModuleArgs *args);
static GwyParamTable*   make_table_offsets  (ModuleArgs *args);
static GwyParamTable*   make_table_value    (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (GwyDialog *dialog,
                                             gint response,
                                             ModuleGUI *gui);
static gboolean         template_filter     (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             init_xyparams       (ModuleArgs *args);
static void             init_zparams        (ModuleArgs *args);
static void             init_wparams        (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Recalibrate volume data dimensions or value range."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.2",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volume_calibrate)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volcal",
                             (GwyVolumeFunc)&volcal,
                             N_("/_Basic Operations/_Dimensions and Units..."),
                             GWY_STOCK_VOLUME_DIMENSIONS,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Change physical dimensions, units or value scale"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum dims_modes[] = {
        { N_("Do not change"),            MODE_KEEP,      },
        { N_("Match voxel size"),         MODE_MATCH,     },
        { N_("Set dimensions"),           MODE_SET_RANGE, },
        { N_("Correct by factor"),        MODE_CALIBRATE, },
    };
    static const GwyEnum offsets_modes[] = {
        { N_("Do not change"),         MODE_KEEP,         },
        { N_("Scale with dimensions"), MODE_PROPORTIONAL, },
        { N_("Set offsets"),           MODE_SET_RANGE,    },
        { N_("Clear offsets"),         MODE_CLEAR,        },
    };
    static const GwyEnum value_modes[] = {
        { N_("Do not change"),         MODE_KEEP,      },
        { N_("Set range"),             MODE_SET_RANGE, },
        { N_("Correct by factor"),     MODE_CALIBRATE, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());

    gwy_param_def_add_gwyenum(paramdef, PARAM_XY_MODE, "xy_mode", NULL,
                              dims_modes, G_N_ELEMENTS(dims_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_XREAL, "xreal", _("_Width"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YREAL, "yreal", _("_Height"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_XRATIO, "xratio", _("_X correction factor"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YRATIO, "yratio", _("_Y correction factor"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE, "square", _("_Square pixels"), TRUE);
    gwy_param_def_add_unit(paramdef, PARAM_XYUNIT, "xyunit", _("_Dimensions unit"), NULL);

    gwy_param_def_add_gwyenum(paramdef, PARAM_Z_MODE, "z_mode", NULL,
                              dims_modes, G_N_ELEMENTS(dims_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_ZREAL, "zreal", _("_Z range"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ZRATIO, "zratio", _("_Z correction factor"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("Z _unit"), NULL);

    gwy_param_def_add_gwyenum(paramdef, PARAM_OFFSETS_MODE, "offsets_mode", NULL,
                              offsets_modes, G_N_ELEMENTS(offsets_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_XOFFSET, "xoffset", _("X offset"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YOFFSET, "yoffset", _("Y offset"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ZOFFSET, "zoffset", _("Z offset"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);

    gwy_param_def_add_volume_id(paramdef, PARAM_XYTEMPLATE, "xytemplate", _("_Template"));
    gwy_param_def_add_volume_id(paramdef, PARAM_ZTEMPLATE, "ztemplate", _("_Template"));

    gwy_param_def_add_gwyenum(paramdef, PARAM_VALUE_MODE, "value_mode", NULL,
                              value_modes, G_N_ELEMENTS(value_modes), MODE_KEEP);
    gwy_param_def_add_double(paramdef, PARAM_WRANGE, "wrange", _("Value _range"), -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_WMIN, "wmin", _("Value _minimum"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_WSHIFT, "wshift", _("Value shi_ft"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_WRATIO, "wratio", _("_Value correction factor"),
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_unit(paramdef, PARAM_WUNIT, "wunit", _("_Value unit"), NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_DATA, "new_volume", _("Create new volume data"), FALSE);
    return paramdef;
}

static void
calibrate_one_brick(GwyBrick *brick, GwyParams *params)
{
    if (gwy_params_get_enum(params, PARAM_XY_MODE) != MODE_KEEP) {
        gwy_brick_set_xreal(brick, gwy_params_get_double(params, PARAM_XREAL));
        gwy_brick_set_yreal(brick, gwy_params_get_double(params, PARAM_YREAL));
        gwy_si_unit_assign(gwy_brick_get_si_unit_x(brick), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
        gwy_si_unit_assign(gwy_brick_get_si_unit_y(brick), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_Z_MODE) != MODE_KEEP) {
        gwy_brick_set_zreal(brick, gwy_params_get_double(params, PARAM_ZREAL));
        gwy_si_unit_assign(gwy_brick_get_si_unit_z(brick), gwy_params_get_unit(params, PARAM_ZUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP) {
        gwy_brick_set_xoffset(brick, gwy_params_get_double(params, PARAM_XOFFSET));
        gwy_brick_set_yoffset(brick, gwy_params_get_double(params, PARAM_YOFFSET));
        gwy_brick_set_zoffset(brick, gwy_params_get_double(params, PARAM_ZOFFSET));
    }
    if (gwy_params_get_enum(params, PARAM_VALUE_MODE) != MODE_KEEP) {
        gwy_brick_multiply(brick, gwy_params_get_double(params, PARAM_WRATIO));
        gwy_brick_add(brick, gwy_params_get_double(params, PARAM_WSHIFT));
        gwy_si_unit_assign(gwy_brick_get_si_unit_w(brick), gwy_params_get_unit(params, PARAM_WUNIT, NULL));
    }
}

static void
calibrate_one_image(GwyDataField *field, GwyParams *params)
{
    if (gwy_params_get_enum(params, PARAM_XY_MODE) != MODE_KEEP) {
        gwy_data_field_set_xreal(field, gwy_params_get_double(params, PARAM_XREAL));
        gwy_data_field_set_yreal(field, gwy_params_get_double(params, PARAM_YREAL));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), gwy_params_get_unit(params, PARAM_XYUNIT, NULL));
    }
    if (gwy_params_get_enum(params, PARAM_OFFSETS_MODE) != MODE_KEEP) {
        gwy_data_field_set_xoffset(field, gwy_params_get_double(params, PARAM_XOFFSET));
        gwy_data_field_set_yoffset(field, gwy_params_get_double(params, PARAM_YOFFSET));
    }
    if (gwy_params_get_enum(params, PARAM_VALUE_MODE) != MODE_KEEP) {
        gwy_data_field_multiply(field, gwy_params_get_double(params, PARAM_WRATIO));
        gwy_data_field_add(field, gwy_params_get_double(params, PARAM_WSHIFT));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), gwy_params_get_unit(params, PARAM_WUNIT, NULL));
    }
}

static void
volcal(GwyContainer *data, GwyRunType runtype)
{
    GwyBrick *brick;
    GwyDataField *preview;
    GQuark quark;
    gint oldid, newid;
    ModuleArgs args;
    GwyDialogOutcome outcome;
    GwyParams *params;
    gboolean new_channel;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_KEY, &quark,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(brick);

    gwy_clear(&args, 1);
    args.brick = brick;
    args.xres = gwy_brick_get_xres(brick);
    args.yres = gwy_brick_get_yres(brick);
    args.zres = gwy_brick_get_zres(brick);
    args.xreal = gwy_brick_get_xreal(brick);
    args.yreal = gwy_brick_get_yreal(brick);
    args.zreal = gwy_brick_get_zreal(brick);
    args.xoffset = gwy_brick_get_xoffset(brick);
    args.yoffset = gwy_brick_get_yoffset(brick);
    args.zoffset = gwy_brick_get_zoffset(brick);
    args.wmin = gwy_brick_get_min(brick);
    args.wmax = gwy_brick_get_max(brick);
    args.xyunit = gwy_brick_get_si_unit_x(brick);
    args.zunit = gwy_brick_get_si_unit_z(brick);
    args.wunit = gwy_brick_get_si_unit_w(brick);
    args.is_square = (fabs(log(args.yreal/args.yres * args.xres/args.xreal)) <= EPSILON);
    args.params = params = gwy_params_new_from_settings(define_module_params());
    init_xyparams(&args);
    init_zparams(&args);
    init_wparams(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    new_channel = gwy_params_get_boolean(params, PARAM_NEW_DATA);
    if (new_channel)
        brick = gwy_brick_duplicate(brick);
    else
        gwy_app_undo_qcheckpointv(data, 1, &quark);

    calibrate_one_brick(brick, params);

    preview = gwy_container_get_object(data, gwy_app_get_brick_preview_key_for_id(oldid));
    if (new_channel) {
        preview = gwy_data_field_duplicate(preview);
        calibrate_one_image(preview, params);
        newid = gwy_app_data_browser_add_brick(brick, preview, data, TRUE);
        g_object_unref(brick);
        g_object_unref(preview);
        gwy_app_sync_volume_items(data, data, oldid, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
        gwy_app_set_brick_title(data, newid, _("Recalibrated Data"));
        gwy_app_volume_log_add_volume(data, oldid, newid);
    }
    else {
        calibrate_one_image(preview, params);
        gwy_brick_data_changed(brick);
        gwy_data_field_data_changed(preview);
        gwy_app_volume_log_add_volume(data, oldid, oldid);
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
    GwySIValueFormat *xyvf, *zvf, *wvf;
    gchar *buf;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Dimensions and Units"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    table = gui.table_xy = make_table_xy(args);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_z = make_table_z(args);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_value = make_table_value(args);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_offsets = make_table_offsets(args);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_NEW_DATA);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    xyvf = gui.xyvf = gwy_brick_get_value_format_x(args->brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    zvf = gui.zvf = gwy_brick_get_value_format_z(args->brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    wvf = gui.wvf = gwy_brick_get_value_format_w(args->brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    buf = g_strdup_printf("%.*f%s%s × %.*f%s%s",
                          xyvf->precision, args->xreal/xyvf->magnitude, *xyvf->units ? " " : "", xyvf->units,
                          xyvf->precision, args->yreal/xyvf->magnitude, *xyvf->units ? " " : "", xyvf->units);
    gwy_param_table_info_set_valuestr(gui.table_xy, LABEL_XY, buf);
    g_free(buf);

    buf = g_strdup_printf("%.*f%s%s",
                          zvf->precision, args->zreal/zvf->magnitude, *zvf->units ? " " : "", zvf->units);
    gwy_param_table_info_set_valuestr(gui.table_z, LABEL_Z, buf);
    g_free(buf);

    buf = g_strdup_printf("(%.*f%s%s, %.*f%s%s, %.*f%s%s)",
                          xyvf->precision, args->xoffset/xyvf->magnitude, *xyvf->units ? " " : "", xyvf->units,
                          xyvf->precision, args->yoffset/xyvf->magnitude, *xyvf->units ? " " : "", xyvf->units,
                          zvf->precision, args->zoffset/zvf->magnitude, *zvf->units ? " " : "", zvf->units);
    gwy_param_table_info_set_valuestr(gui.table_offsets, LABEL_OFFSETS, buf);
    g_free(buf);

    buf = g_strdup_printf("[%.*f%s%s, %.*f%s%s]",
                          wvf->precision, args->wmin/wvf->magnitude, *wvf->units ? " " : "", wvf->units,
                          wvf->precision, args->wmax/wvf->magnitude, *wvf->units ? " " : "", wvf->units);
    gwy_param_table_info_set_valuestr(gui.table_value, LABEL_VALUES, buf);
    g_free(buf);

    g_signal_connect_swapped(gui.table_xy, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_z, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_value, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_offsets, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    outcome = gwy_dialog_run(dialog);

    GWY_SI_VALUE_FORMAT_FREE(gui.xyvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.xycalvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.zvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.zcalvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.wvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.wcalvf);

    return outcome;
}

static GwyParamTable*
make_table_xy(ModuleArgs *args)
{
    static const gint noreset[] = {
        PARAM_XREAL, PARAM_YREAL, PARAM_XRATIO, PARAM_YRATIO, PARAM_SQUARE, PARAM_XYTEMPLATE, PARAM_XYUNIT
    };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("XY Dimensions"));
    /* TRANSLATORS: Current is an adjective here (as in the current value). */
    gwy_param_table_append_info(table, LABEL_XY, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_XY_MODE, MODE_KEEP);
    gwy_param_table_append_radio_item(table, PARAM_XY_MODE, MODE_MATCH);
    gwy_param_table_append_volume_id(table, PARAM_XYTEMPLATE);
    gwy_param_table_data_id_set_filter(table, PARAM_XYTEMPLATE, template_filter, args->brick, NULL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_XY_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_XREAL);
    gwy_param_table_append_entry(table, PARAM_YREAL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_XY_MODE, MODE_CALIBRATE);
    gwy_param_table_append_entry(table, PARAM_XRATIO);
    gwy_param_table_append_entry(table, PARAM_YRATIO);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_SQUARE);
    gwy_param_table_append_unit_chooser(table, PARAM_XYUNIT);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static GwyParamTable*
make_table_z(ModuleArgs *args)
{
    static const gint noreset[] = { PARAM_ZREAL, PARAM_ZRATIO, PARAM_ZTEMPLATE, PARAM_ZUNIT };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Z Dimension"));
    /* TRANSLATORS: Current is an adjective here (as in the current value). */
    gwy_param_table_append_info(table, LABEL_Z, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_Z_MODE, MODE_KEEP);
    gwy_param_table_append_radio_item(table, PARAM_Z_MODE, MODE_MATCH);
    gwy_param_table_append_volume_id(table, PARAM_ZTEMPLATE);
    gwy_param_table_data_id_set_filter(table, PARAM_ZTEMPLATE, template_filter, args->brick, NULL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_Z_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_ZREAL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_Z_MODE, MODE_CALIBRATE);
    gwy_param_table_append_entry(table, PARAM_ZRATIO);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static GwyParamTable*
make_table_offsets(ModuleArgs *args)
{
    static const gint noreset[] = { PARAM_XOFFSET, PARAM_YOFFSET, PARAM_ZOFFSET };
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
    gwy_param_table_append_entry(table, PARAM_ZOFFSET);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static GwyParamTable*
make_table_value(ModuleArgs *args)
{
    static const gint noreset[] = { PARAM_WRANGE, PARAM_WMIN, PARAM_WSHIFT, PARAM_WRATIO, PARAM_WUNIT };
    GwyParamTable *table;

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Value Range"));
    gwy_param_table_append_info(table, LABEL_VALUES, _("Current"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_KEEP);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_SET_RANGE);
    gwy_param_table_append_entry(table, PARAM_WMIN);
    gwy_param_table_append_entry(table, PARAM_WRANGE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio_item(table, PARAM_VALUE_MODE, MODE_CALIBRATE);
    gwy_param_table_append_entry(table, PARAM_WRATIO);
    gwy_param_table_append_entry(table, PARAM_WSHIFT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_unit_chooser(table, PARAM_WUNIT);
    gwy_param_table_set_no_resetv(table, noreset, G_N_ELEMENTS(noreset), TRUE);
    return table;
}

static void
update_xy_formats(ModuleGUI *gui)
{
    gwy_param_table_entry_set_value_format(gui->table_xy, PARAM_XREAL, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_xy, PARAM_YREAL, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_xy, PARAM_XRATIO, gui->xycalvf);
    gwy_param_table_entry_set_value_format(gui->table_xy, PARAM_YRATIO, gui->xycalvf);
    gwy_param_table_entry_set_value_format(gui->table_offsets, PARAM_XOFFSET, gui->xyvf);
    gwy_param_table_entry_set_value_format(gui->table_offsets, PARAM_YOFFSET, gui->xyvf);
}

static void
update_z_formats(ModuleGUI *gui)
{
    gwy_param_table_entry_set_value_format(gui->table_z, PARAM_ZREAL, gui->zvf);
    gwy_param_table_entry_set_value_format(gui->table_z, PARAM_ZRATIO, gui->zcalvf);
    gwy_param_table_entry_set_value_format(gui->table_offsets, PARAM_ZOFFSET, gui->zvf);
}

static void
update_w_formats(ModuleGUI *gui)
{
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_WRANGE, gui->wvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_WMIN, gui->wvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_WSHIFT, gui->wvf);
    gwy_param_table_entry_set_value_format(gui->table_value, PARAM_WRATIO, gui->wcalvf);
}

static void
param_changed_xy(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table_xy = gui->table_xy, *table_offsets = gui->table_offsets;
    GwySIUnit *unit, *unitcal;
    GwySIValueFormat *vf;
    gdouble xreal, yreal, m;
    gboolean square, xreal_changed = FALSE, yreal_changed = FALSE, use_template = FALSE;
    CalibrateMode mode, offsets_mode;
    gint power10;

    xreal = gwy_params_get_double(params, PARAM_XREAL);
    yreal = gwy_params_get_double(params, PARAM_YREAL);
    mode = gwy_params_get_enum(params, PARAM_XY_MODE);

    if (id < 0) {
        /* XXX: This is a bit weird.  Param table should probably allow checking the filter state earlier. */
        if (!gwy_params_get_volume(params, PARAM_XYTEMPLATE)) {
            gwy_param_table_radio_set_sensitive(table_xy, PARAM_XY_MODE, MODE_MATCH, FALSE);
            if (mode == MODE_MATCH)
                gwy_param_table_set_enum(table_xy, PARAM_XY_MODE, (mode = MODE_KEEP));
        }
    }

    if (id < 0 || id == PARAM_XY_MODE) {
        gwy_param_table_set_sensitive(table_xy, PARAM_XREAL, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_xy, PARAM_YREAL, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_xy, PARAM_SQUARE, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_xy, PARAM_XRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_xy, PARAM_YRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_xy, PARAM_XYTEMPLATE, mode == MODE_MATCH);
        gwy_param_table_set_sensitive(table_xy, PARAM_XYUNIT, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_xy, PARAM_XREAL, (xreal = args->xreal));
            gwy_param_table_set_double(table_xy, PARAM_YREAL, (yreal = args->yreal));
            xreal_changed = yreal_changed = TRUE;
        }
        else if (mode == MODE_MATCH)
            use_template = TRUE;
        /* When switching to other modes, the values should be already consistent. */
    }
    if (use_template || id == PARAM_XYTEMPLATE) {
        GwyBrick *template = gwy_params_get_volume(params, PARAM_XYTEMPLATE);
        gwy_param_table_set_double(table_xy, PARAM_XREAL,
                                   (xreal = gwy_brick_get_dx(template)*gwy_brick_get_xres(args->brick)));
        gwy_param_table_set_double(table_xy, PARAM_YREAL,
                                   (yreal = gwy_brick_get_dx(template)*gwy_brick_get_yres(args->brick)));
        vf = gwy_brick_get_value_format_x(template, GWY_SI_UNIT_FORMAT_PLAIN, NULL);
        gwy_param_table_set_string(table_xy, PARAM_XYUNIT, vf->units);
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
            gwy_param_table_set_double(table_xy, PARAM_YREAL, (yreal = xreal/args->xres * args->yres));
            gwy_param_table_set_double(table_xy, PARAM_YRATIO, yreal/args->yreal);
            yreal_changed = TRUE;
        }
    }

    if (id == PARAM_XREAL || xreal_changed) {
        gwy_param_table_set_double(table_xy, PARAM_XRATIO, xreal/args->xreal);
        xreal_changed = TRUE;
    }
    if (id == PARAM_YREAL || yreal_changed) {
        gwy_param_table_set_double(table_xy, PARAM_YRATIO, yreal/args->yreal);
        yreal_changed = TRUE;
    }
    if (id == PARAM_XRATIO) {
        xreal = args->xreal * gwy_params_get_double(params, PARAM_XRATIO);
        gwy_param_table_set_double(table_xy, PARAM_XREAL, xreal);
        xreal_changed = TRUE;
    }
    if (id == PARAM_YRATIO) {
        yreal = args->yreal * gwy_params_get_double(params, PARAM_YRATIO);
        gwy_param_table_set_double(table_xy, PARAM_YREAL, yreal);
        yreal_changed = TRUE;
    }
    /* This can do some redundant updates but we do not care because they are idempotent. */
    if (square && xreal_changed) {
        gwy_param_table_set_double(table_xy, PARAM_YREAL, (yreal = xreal/args->xres * args->yres));
        gwy_param_table_set_double(table_xy, PARAM_YRATIO, yreal/args->yreal);
        yreal_changed = TRUE;
    }
    if (square && yreal_changed) {
        gwy_param_table_set_double(table_xy, PARAM_XREAL, (xreal = yreal/args->yres * args->xres));
        gwy_param_table_set_double(table_xy, PARAM_XRATIO, xreal/args->xreal);
        xreal_changed = TRUE;
    }
    if (offsets_mode == MODE_PROPORTIONAL && xreal_changed)
        gwy_param_table_set_double(table_offsets, PARAM_XOFFSET, xreal/args->xreal * args->xoffset);
    if (offsets_mode == MODE_PROPORTIONAL && yreal_changed)
        gwy_param_table_set_double(table_offsets, PARAM_YOFFSET, yreal/args->yreal * args->yoffset);

    /* Units are mostly just a presentational aspect.  When the user changes units we do not change any value. */
    if (id < 0 || id == PARAM_XYUNIT || xreal_changed || yreal_changed) {
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
    GwyParamTable *table_z = gui->table_z, *table_offsets = gui->table_offsets;
    GwySIUnit *unit, *unitcal;
    GwySIValueFormat *vf;
    gdouble zreal, m;
    gboolean zreal_changed = FALSE, use_template = FALSE;
    CalibrateMode mode, offsets_mode;
    gint power10;

    zreal = gwy_params_get_double(params, PARAM_ZREAL);
    mode = gwy_params_get_enum(params, PARAM_Z_MODE);

    if (id < 0) {
        /* XXX: This is a bit weird.  Param table should probably allow checking the filter state earlier. */
        if (!gwy_params_get_volume(params, PARAM_ZTEMPLATE)) {
            gwy_param_table_radio_set_sensitive(table_z, PARAM_Z_MODE, MODE_MATCH, FALSE);
            if (mode == MODE_MATCH)
                gwy_param_table_set_enum(table_z, PARAM_Z_MODE, (mode = MODE_KEEP));
        }
    }

    if (id < 0 || id == PARAM_Z_MODE) {
        gwy_param_table_set_sensitive(table_z, PARAM_ZREAL, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_z, PARAM_SQUARE, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_z, PARAM_ZRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_z, PARAM_ZTEMPLATE, mode == MODE_MATCH);
        gwy_param_table_set_sensitive(table_z, PARAM_ZUNIT, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_z, PARAM_ZREAL, (zreal = args->zreal));
            zreal_changed = TRUE;
        }
        else if (mode == MODE_MATCH)
            use_template = TRUE;
        /* When switching to other modes, the values should be already consistent. */
    }
    if (use_template || id == PARAM_ZTEMPLATE) {
        GwyBrick *template = gwy_params_get_volume(params, PARAM_ZTEMPLATE);
        gwy_param_table_set_double(table_z, PARAM_ZREAL,
                                   (zreal = gwy_brick_get_dx(template)*gwy_brick_get_xres(args->brick)));
        vf = gwy_brick_get_value_format_z(template, GWY_SI_UNIT_FORMAT_PLAIN, NULL);
        gwy_param_table_set_string(table_z, PARAM_ZUNIT, vf->units);
        gwy_si_unit_value_format_free(vf);
        zreal_changed = TRUE;
    }

    offsets_mode = gwy_params_get_enum(params, PARAM_OFFSETS_MODE);
    if (id < 0 || id == PARAM_OFFSETS_MODE) {
        mode = offsets_mode;
        gwy_param_table_set_sensitive(table_offsets, PARAM_ZOFFSET, mode == MODE_SET_RANGE);
        if (mode == MODE_KEEP)
            gwy_param_table_set_double(table_offsets, PARAM_ZOFFSET, args->zoffset);
        else if (mode == MODE_CLEAR)
            gwy_param_table_set_double(table_offsets, PARAM_ZOFFSET, 0.0);
        else if (mode == MODE_PROPORTIONAL)
            gwy_param_table_set_double(table_offsets, PARAM_ZOFFSET, zreal/args->zreal * args->zoffset);
        /* When switching to other modes, the values should be already consistent. */
    }

    if (id == PARAM_ZREAL || zreal_changed) {
        gwy_param_table_set_double(table_z, PARAM_ZRATIO, zreal/args->zreal);
        zreal_changed = TRUE;
    }
    if (id == PARAM_ZRATIO) {
        zreal = args->zreal * gwy_params_get_double(params, PARAM_ZRATIO);
        gwy_param_table_set_double(table_z, PARAM_ZREAL, zreal);
        zreal_changed = TRUE;
    }
    if (offsets_mode == MODE_PROPORTIONAL && zreal_changed)
        gwy_param_table_set_double(table_offsets, PARAM_ZOFFSET, zreal/args->zreal * args->zoffset);

    /* Units are mostly just a presentational aspect.  When the user changes units we do not change any value. */
    if (id < 0 || id == PARAM_ZUNIT || zreal_changed) {
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
param_changed_w(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table_value = gui->table_value;
    GwySIUnit *unit, *unitcal;
    gdouble m, wmin, wratio, wshift, wrange;
    CalibrateMode mode;
    gint power10;

    if (id < 0 || id == PARAM_VALUE_MODE) {
        mode = gwy_params_get_enum(params, PARAM_VALUE_MODE);
        gwy_param_table_set_sensitive(table_value, PARAM_WRANGE, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_value, PARAM_WMIN, mode == MODE_SET_RANGE);
        gwy_param_table_set_sensitive(table_value, PARAM_WRATIO, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_value, PARAM_WSHIFT, mode == MODE_CALIBRATE);
        gwy_param_table_set_sensitive(table_value, PARAM_WUNIT, mode == MODE_SET_RANGE || mode == MODE_CALIBRATE);
        if (mode == MODE_KEEP) {
            gwy_param_table_set_double(table_value, PARAM_WRANGE, args->wmax - args->wmin);
            gwy_param_table_set_double(table_value, PARAM_WMIN, args->wmin);
            gwy_param_table_set_double(table_value, PARAM_WRATIO, 1.0);
            gwy_param_table_set_double(table_value, PARAM_WSHIFT, 0.0);
        }
        /* When switching to other modes, the values should be already consistent. */
    }

    if (id == PARAM_WMIN) {
        wmin = gwy_params_get_double(params, PARAM_WMIN);
        wratio = gwy_params_get_double(params, PARAM_WRATIO);
        gwy_param_table_set_double(table_value, PARAM_WSHIFT, wmin - wratio*args->wmin);
    }
    if (id == PARAM_WSHIFT) {
        wshift = gwy_params_get_double(params, PARAM_WSHIFT);
        wratio = gwy_params_get_double(params, PARAM_WRATIO);
        gwy_param_table_set_double(table_value, PARAM_WMIN, wratio*args->wmin + wshift);
    }
    if (id == PARAM_WRATIO) {
        wshift = gwy_params_get_double(params, PARAM_WSHIFT);
        wratio = gwy_params_get_double(params, PARAM_WRATIO);
        gwy_param_table_set_double(table_value, PARAM_WMIN, wratio*args->wmin + wshift);
        gwy_param_table_set_double(table_value, PARAM_WRANGE, wratio*(args->wmax - args->wmin));
    }
    if (id == PARAM_WRANGE) {
        wmin = gwy_params_get_double(params, PARAM_WMIN);
        wrange = gwy_params_get_double(params, PARAM_WRANGE);
        if (args->wmax > args->wmin)
            gwy_param_table_set_double(table_value, PARAM_WRATIO, (wratio = wrange/(args->wmax - args->wmin)));
        else
            gwy_param_table_set_double(table_value, PARAM_WRATIO, (wratio = 1.0));
        gwy_param_table_set_double(table_value, PARAM_WSHIFT, wmin - wratio*args->wmin);
    }

    /* Units are mostly just a presentational aspect.  When the user changes units we do not change any value. */
    if (id < 0 || id == PARAM_WUNIT || id == PARAM_WRANGE || id == PARAM_WRATIO || id == PARAM_VALUE_MODE) {
        unit = gwy_params_get_unit(params, PARAM_WUNIT, &power10);
        unitcal = gwy_si_unit_divide(unit, args->wunit, NULL);
        gui->wvf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, gui->wvf);
        gui->wvf->precision = 4;
        m = 5.0*gwy_params_get_double(params, PARAM_WRATIO);
        gui->wcalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, m, 6, gui->wcalvf);
        gwy_debug("W %g (%d) [%s] for %g", gui->wcalvf->magnitude, gui->wcalvf->precision, gui->wcalvf->units, m);
        g_object_unref(unitcal);
        update_w_formats(gui);
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    param_changed_xy(gui, id);
    param_changed_z(gui, id);
    param_changed_w(gui, id);
}

static void
reset_formats(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    GwySIUnit *unitcal = gwy_si_unit_new(NULL);

    gui->xyvf = gwy_brick_get_value_format_x(brick, GWY_SI_UNIT_FORMAT_PLAIN, gui->xyvf);
    gwy_param_table_set_string(gui->table_xy, PARAM_XYUNIT, gui->xyvf->units);
    gui->xyvf = gwy_brick_get_value_format_x(brick, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->xyvf);
    gui->xyvf->precision += 2;
    gui->xycalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, 10.0, 6, gui->xycalvf);

    gui->zvf = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_PLAIN, gui->zvf);
    gwy_param_table_set_string(gui->table_z, PARAM_ZUNIT, gui->zvf->units);
    gui->zvf = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->zvf);
    gui->zvf->precision += 2;
    gui->zcalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, 10.0, 6, gui->zcalvf);

    gui->wvf = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_PLAIN, gui->wvf);
    gwy_param_table_set_string(gui->table_value, PARAM_WUNIT, gui->wvf->units);
    gui->wvf = gwy_brick_get_value_format_z(brick, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->wvf);
    gui->wvf->precision += 2;
    gui->wcalvf = gwy_si_unit_get_format_with_digits(unitcal, GWY_SI_UNIT_FORMAT_VFMARKUP, 10.0, 6, gui->wcalvf);

    g_object_unref(unitcal);

    update_xy_formats(gui);
    update_z_formats(gui);
    update_w_formats(gui);
    gwy_param_table_set_boolean(gui->table_xy, PARAM_SQUARE, args->is_square);
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        reset_formats(gui);
}

static gboolean
template_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyBrick *otherbrick, *brick = (GwyBrick*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_brick_key_for_id(id), &otherbrick))
        return FALSE;
    return otherbrick != brick;
}

static void
init_xyparams(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwySIValueFormat *vf = NULL;
    GwySIUnit *xyunit;
    gdouble newxreal, newyreal, newxoffset, newyoffset;
    CalibrateMode mode;

    /* Dimensions. */
    mode = gwy_params_get_enum(params, PARAM_XY_MODE);
    xyunit = gwy_params_get_unit(params, PARAM_XYUNIT, NULL);
    if (mode == MODE_MATCH) {
        GwyBrick *template = gwy_params_get_volume(params, PARAM_XYTEMPLATE);
        if (template) {
            newxreal = gwy_brick_get_dx(template)*gwy_brick_get_xres(args->brick);
            newyreal = gwy_brick_get_dy(template)*gwy_brick_get_yres(args->brick);
            vf = gwy_brick_get_value_format_x(template, GWY_SI_UNIT_FORMAT_PLAIN, vf);
            gwy_params_set_unit(params, PARAM_XYUNIT, vf->units);
        }
        else
            mode = MODE_KEEP;
    }
    if (mode == MODE_KEEP) {
        newxreal = args->xreal;
        newyreal = args->yreal;
        vf = gwy_brick_get_value_format_x(args->brick, GWY_SI_UNIT_FORMAT_PLAIN, vf);
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
init_zparams(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwySIValueFormat *vf = NULL;
    GwySIUnit *zunit;
    gdouble newzreal, newzoffset;
    CalibrateMode mode;

    /* Dimensions. */
    mode = gwy_params_get_enum(params, PARAM_Z_MODE);
    zunit = gwy_params_get_unit(params, PARAM_ZUNIT, NULL);
    if (mode == MODE_MATCH) {
        GwyBrick *template = gwy_params_get_volume(params, PARAM_ZTEMPLATE);
        if (template) {
            newzreal = gwy_brick_get_dz(template)*gwy_brick_get_zres(args->brick);
            vf = gwy_brick_get_value_format_z(template, GWY_SI_UNIT_FORMAT_PLAIN, vf);
            gwy_params_set_unit(params, PARAM_ZUNIT, vf->units);
        }
        else
            mode = MODE_KEEP;
    }
    if (mode == MODE_KEEP) {
        newzreal = args->zreal;
        vf = gwy_brick_get_value_format_z(args->brick, GWY_SI_UNIT_FORMAT_PLAIN, vf);
    }
    else if (mode == MODE_SET_RANGE) {
        newzreal = gwy_params_get_double(params, PARAM_ZREAL);
        vf = gwy_si_unit_get_format_with_digits(zunit, GWY_SI_UNIT_FORMAT_PLAIN, newzreal, 6, vf);
    }
    else if (mode == MODE_CALIBRATE) {
        newzreal = args->zreal*gwy_params_get_double(params, PARAM_ZRATIO);
        vf = gwy_si_unit_get_format_with_digits(zunit, GWY_SI_UNIT_FORMAT_PLAIN, newzreal, 6, vf);
    }
    else if (mode == MODE_MATCH) {
    }
    else {
        g_return_if_reached();
    }
    gwy_params_set_unit(params, PARAM_ZUNIT, vf->units);
    gwy_params_set_double(params, PARAM_ZRATIO, newzreal/args->zreal);
    gwy_params_set_double(params, PARAM_ZREAL, newzreal);

    /* Offsets. */
    mode = gwy_params_get_enum(params, PARAM_OFFSETS_MODE);
    if (mode == MODE_KEEP)
        newzoffset = args->zoffset;
    else if (mode == MODE_CLEAR)
        newzoffset = 0.0;
    else if (mode == MODE_SET_RANGE)
        newzoffset = gwy_params_get_double(params, PARAM_ZOFFSET);
    else if (mode == MODE_PROPORTIONAL)
        newzoffset = args->zoffset*(newzreal/args->zreal);
    else {
        g_return_if_reached();
    }
    gwy_params_set_double(params, PARAM_ZOFFSET, newzoffset);

    gwy_si_unit_value_format_free(vf);
}

static void
init_wparams(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwySIValueFormat *vf = NULL;
    GwySIUnit *wunit;
    gdouble newwrange, wshift, wcal, newwmin;
    CalibrateMode mode;

    /* Values. */
    mode = gwy_params_get_enum(params, PARAM_VALUE_MODE);
    wunit = gwy_params_get_unit(params, PARAM_WUNIT, NULL);
    if (mode == MODE_KEEP) {
        newwrange = args->wmax - args->wmin;
        wshift = 0.0;
        wcal = 1.0;
        newwmin = args->wmin;
        vf = gwy_brick_get_value_format_w(args->brick, GWY_SI_UNIT_FORMAT_PLAIN, vf);
    }
    else if (mode == MODE_SET_RANGE) {
        newwrange = gwy_params_get_double(params, PARAM_WRANGE);
        newwmin = gwy_params_get_double(params, PARAM_WMIN);
        wcal = (args->wmax - args->wmin > 0.0 ? newwrange/(args->wmax - args->wmin) : 0.0);
        wshift = newwmin - args->wmin;
        vf = gwy_si_unit_get_format_with_digits(wunit, GWY_SI_UNIT_FORMAT_PLAIN, newwrange, 6, vf);
    }
    else if (mode == MODE_CALIBRATE) {
        wcal = gwy_params_get_double(params, PARAM_WRATIO);
        wshift = gwy_params_get_double(params, PARAM_WSHIFT);
        newwrange = (args->wmax - args->wmin)*wcal;
        newwmin = args->wmin - wshift;
        vf = gwy_si_unit_get_format_with_digits(wunit, GWY_SI_UNIT_FORMAT_PLAIN, newwrange, 6, vf);
    }
    else {
        g_return_if_reached();
    }
    gwy_params_set_unit(params, PARAM_WUNIT, vf->units);
    gwy_params_set_double(params, PARAM_WRANGE, newwrange);
    gwy_params_set_double(params, PARAM_WMIN, newwmin);
    gwy_params_set_double(params, PARAM_WRATIO, wcal);
    gwy_params_set_double(params, PARAM_WSHIFT, wshift);

    gwy_si_unit_value_format_free(vf);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
