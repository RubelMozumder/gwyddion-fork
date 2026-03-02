/*
 *  $Id: volume_scars.c 26354 2024-05-21 08:22:32Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correlation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    MAX_LENGTH = 1024
};

typedef enum {
    FEATURES_POSITIVE = 1 << 0,
    FEATURES_NEGATIVE = 1 << 2,
    FEATURES_BOTH     = (FEATURES_POSITIVE | FEATURES_NEGATIVE),
} FeatureType;

enum {
    PARAM_TYPE,
    PARAM_THRESHOLD_HIGH,
    PARAM_THRESHOLD_LOW,
    PARAM_MIN_LENGTH,
    PARAM_MAX_WIDTH,
    PARAM_UPDATE,
    PARAM_MASK_COLOR,
    PARAM_Z,
};

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
} ModuleGUI;


static gboolean              module_register          (void);
static GwyParamDef*          define_module_params     (void);
static void                  scars                    (GwyContainer *data,
                                                       GwyRunType runtype);
static gboolean              execute                  (ModuleArgs *args,
                                                       GtkWindow *wait_window);
static GwyDialogOutcome      run_gui                  (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void                  param_changed            (ModuleGUI *gui,
                                                       gint id);
static void                  dialog_response          (GwyDialog *dialog,
                                                       gint response,
                                                       ModuleGUI *gui);
static void                  preview                  (gpointer user_data);
static void                  update_image             (ModuleGUI *gui,
                                                       gint z);
static void                  sanitise_params          (ModuleArgs *args);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performs scars removal for all the volume data levels."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_scars)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_scars",
                             (GwyVolumeFunc)&scars,
                             N_("/_Correct Data/_Scars..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Remove scars in all levels"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum feature_types[] = {
        { N_("Positive"), FEATURES_POSITIVE, },
        { N_("Negative"), FEATURES_NEGATIVE, },
        { N_("Both"),     FEATURES_BOTH,     },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "scars");
    gwy_param_def_add_int(paramdef, PARAM_Z, "z", "Preview level", 0, G_MAXINT, 0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("Scars type"),
                              feature_types, G_N_ELEMENTS(feature_types), FEATURES_BOTH);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD_HIGH, "threshold_high", _("_Hard threshold"), 0.0, 2.0, 0.666);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD_LOW, "threshold_low", _("_Soft threshold"), 0.0, 2.0, 0.25);
    gwy_param_def_add_int(paramdef, PARAM_MIN_LENGTH, "min_len", _("Minimum _length"), 1, MAX_LENGTH, 16);
    gwy_param_def_add_int(paramdef, PARAM_MAX_WIDTH, "max_width", _("Maximum _width"), 1, 16, 4);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
scars(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (execute(&args, gwy_app_find_window_for_volume(data, oldid))) {

        newid = gwy_app_data_browser_add_brick(args.result, NULL, data, TRUE);

        gwy_app_set_brick_title(data, newid, _("Scars corrected"));
        gwy_app_sync_volume_items(data, data, oldid, newid, FALSE,
                                  GWY_DATA_ITEM_GRADIENT,
                                  0);

        gwy_app_volume_log_add_volume(data, -1, newid);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    GwyBrick *brick = args->brick;
    GwyDataField *field = gwy_data_field_new(gwy_brick_get_xres(brick),
                                             gwy_brick_get_yres(brick),
                                             gwy_brick_get_xreal(brick),
                                             gwy_brick_get_yreal(brick),
                                             TRUE);
    GwyDataField *mask = gwy_data_field_new_alike(field, TRUE);
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    args->result = gwy_brick_duplicate(brick);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);
    gwy_container_set_object(gui.data, gwy_app_get_mask_key_for_id(0), mask);

    gui.dialog = gwy_dialog_new(_("Scars"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_Z);
    gwy_param_table_slider_restrict_range(table, PARAM_Z, 0, gwy_brick_get_zres(brick)-1);
    gwy_param_table_append_slider(table, PARAM_MAX_WIDTH);
    gwy_param_table_set_unitstr(table, PARAM_MAX_WIDTH, _("px"));
    gwy_param_table_slider_set_mapping(table, PARAM_MAX_WIDTH, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_MIN_LENGTH);
    gwy_param_table_set_unitstr(table, PARAM_MIN_LENGTH, _("px"));

    gwy_param_table_append_slider(table, PARAM_THRESHOLD_HIGH);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD_HIGH, _("RMS"));
    gwy_param_table_append_slider(table, PARAM_THRESHOLD_LOW);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD_LOW, _("RMS"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_TYPE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}


static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table_options;

    if (id == PARAM_THRESHOLD_HIGH || id == PARAM_THRESHOLD_LOW) {
        gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);
        gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
        if (threshold_high < threshold_low) {
            if (id == PARAM_THRESHOLD_HIGH)
                gwy_param_table_set_double(table, PARAM_THRESHOLD_LOW, threshold_high);
            else
                gwy_param_table_set_double(table, PARAM_THRESHOLD_HIGH, threshold_low);
        }
    }
    if (id != PARAM_MASK_COLOR && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        gwy_brick_copy(gui->args->brick, gui->args->result, FALSE);

    preview(gui);
}

static void
mark_scars(GwyDataField *dfield, GwyDataField *mask, gdouble threshold_high, gdouble threshold_low,
           gint min_len, gint max_width, FeatureType type)
{
    GwyDataField *tmp;

    if (type == FEATURES_POSITIVE || type == FEATURES_NEGATIVE) {
        gwy_data_field_mark_scars(dfield, mask, threshold_high, threshold_low,
                                  min_len, max_width, type == FEATURES_NEGATIVE);
    } else {
        gwy_data_field_mark_scars(dfield, mask, threshold_high, threshold_low, min_len, max_width, FALSE);
        tmp = gwy_data_field_new_alike(dfield, FALSE);
        gwy_data_field_mark_scars(dfield, tmp, threshold_high, threshold_low, min_len, max_width, TRUE);
        gwy_data_field_max_of_fields(mask, mask, tmp);
        g_object_unref(tmp);
    }
}

static void
update_image(ModuleGUI *gui, gint z)
{
    GwyDataField *dfield, *mask;
    GwyBrick *brick = gui->args->result;
    GwyParams *params = gui->args->params;
    FeatureType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
    gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);
    gint min_len = gwy_params_get_int(params, PARAM_MIN_LENGTH);
    gint max_width = gwy_params_get_int(params, PARAM_MAX_WIDTH);

    dfield = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
    mask = gwy_container_get_object(gui->data, gwy_app_get_mask_key_for_id(0));

    gwy_brick_extract_xy_plane(brick, dfield, CLAMP(z, 0, brick->zres-1));
    gwy_data_field_data_changed(dfield);

    mark_scars(dfield, mask, threshold_high, threshold_low, min_len, max_width, type);
    gwy_data_field_data_changed(mask);
}


static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gint z = gwy_params_get_int(gui->args->params, PARAM_Z);

    update_image(gui, z);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gint k;
    GwyBrick *original = args->brick;
    GwyBrick *brick = args->result;
    GwyDataField *dfield, *mask;
    FeatureType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
    gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);
    gint min_len = gwy_params_get_int(params, PARAM_MIN_LENGTH);
    gint max_width = gwy_params_get_int(params, PARAM_MAX_WIDTH);
    gboolean cancelled = FALSE;

    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint zres = gwy_brick_get_zres(brick);

    dfield = gwy_data_field_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);
    mask = gwy_data_field_new_alike(dfield, FALSE);

    gwy_app_wait_start(wait_window, _("Removing scars..."));

    for (k = 0; k < zres; k++) {
        gwy_brick_extract_xy_plane(original, dfield, k);
        mark_scars(dfield, mask, threshold_high, threshold_low, min_len, max_width, type);
        gwy_data_field_laplace_solve(dfield, mask, -1, 1.0);
        gwy_brick_set_xy_plane(brick, dfield, k);

        if (!gwy_app_wait_set_fraction((gdouble)k/zres)) {
            cancelled = TRUE;
            break;
        }
    }
    gwy_app_wait_finish();

    g_object_unref(dfield);
    g_object_unref(mask);

    return !cancelled;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble threshold_high = gwy_params_get_double(params, PARAM_THRESHOLD_HIGH);
    gdouble threshold_low = gwy_params_get_double(params, PARAM_THRESHOLD_LOW);

    if (threshold_low > threshold_high)
        gwy_params_set_double(params, PARAM_THRESHOLD_HIGH, threshold_low);
}


/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
