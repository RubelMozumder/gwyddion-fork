/*
 *  $Id: volume_removeframe.c 26354 2024-05-21 08:22:32Z yeti-dn $
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
    RESPONSE_REMOVE = 101,
};

enum {
    PARAM_Z,
    PARAM_EXTRACT,
    BUTTON_REMOVE
};

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
    GwyDataField *extracted;
    gint extracted_level;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
} ModuleGUI;


static gboolean              module_register          (void);
static GwyParamDef*          define_module_params     (void);
static void                  remove_frame             (GwyContainer *data,
                                                       GwyRunType runtype);
static void                  execute                  (ModuleArgs *args);
static GwyDialogOutcome      run_gui                  (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void                  param_changed            (ModuleGUI *gui,
                                                       gint id);
static void                  dialog_response          (GwyDialog *dialog,
                                                       gint response,
                                                       ModuleGUI *gui);
static void                  removeit                 (gpointer user_data);
static void                  update_image             (ModuleGUI *gui,
                                                       gint z);
static void                  sanitise_params          (ModuleArgs *args);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Remove one frame from volume data stack"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_removeframe)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_removeframe",
                             (GwyVolumeFunc)&remove_frame,
                             N_("/_Basic Operations/_Remove XY Plane..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Remove single XY plane"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_int(paramdef, PARAM_Z, "z", "Preview level", 0, G_MAXINT, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_EXTRACT, "extract", _("_Extract the plane"), FALSE);
    return paramdef;
}

static void
remove_frame(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid, level;
    gchar key[40];
    gchar title[40];
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());
    args.extracted = NULL;
    args.extracted_level = 123;
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_brick(args.result, NULL, data, TRUE);

    gwy_app_set_brick_title(data, newid, _("Frame removed"));
    gwy_app_sync_volume_items(data, data, oldid, newid, FALSE,
                              GWY_DATA_ITEM_GRADIENT,
                              0);

    if (gwy_params_get_boolean(args.params, PARAM_EXTRACT) && args.extracted)
    {
       newid = gwy_app_data_browser_add_data_field(args.extracted, data, TRUE);
       gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
       g_snprintf(key, sizeof(key), "/brick/%d/preview/palette", oldid);
       if (gwy_container_gis_string_by_name(data, key, &gradient)) {
           g_snprintf(key, sizeof(key), "/%d/base/palette", newid);
           gwy_container_set_const_string_by_name(data, key, gradient);
       }

       g_object_unref(args.extracted);
       level = args.extracted_level;
       g_snprintf(title, sizeof(title), _("Extracted frame (%d)"), level);
       gwy_app_set_data_field_title(data, newid, title);
    }

    gwy_app_volume_log_add_volume(data, -1, newid);

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
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    args->result = gwy_brick_duplicate(brick);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Remove XY Plane"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_Z);
    gwy_param_table_slider_restrict_range(table, PARAM_Z, 0, gwy_brick_get_zres(brick)-1);
    gwy_param_table_append_checkbox(table, PARAM_EXTRACT);

    gwy_param_table_append_button(table, BUTTON_REMOVE, -1, RESPONSE_REMOVE,
                                  _("_Remove Current Level"));

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}


static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    gint z = gwy_params_get_int(gui->args->params, PARAM_Z);
    update_image(gui, z);
 }

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    gint z = gwy_params_get_int(gui->args->params, PARAM_Z);

    if (response == GWY_RESPONSE_RESET) {
        g_object_unref(gui->args->result);
        gui->args->result = gwy_brick_duplicate(gui->args->brick);
        gwy_param_table_slider_restrict_range(gui->table_options, PARAM_Z, 0, 
                                              gwy_brick_get_zres(gui->args->result) - 1);
        update_image(gui, z);
    }
    else if (response == RESPONSE_REMOVE) {
        removeit(gui);
        if (gwy_brick_get_zres(gui->args->result) < 2)  
            gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_REMOVE, FALSE);
    }
}

static void
update_image(ModuleGUI *gui, gint z)
{
    GwyDataField *dfield;
    GwyBrick *brick = gui->args->result;
    dfield = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));

    gwy_brick_extract_xy_plane(brick, dfield, CLAMP(z, 0, brick->zres-1));

    gwy_data_field_data_changed(dfield);
}


static void
removeit(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwyBrick *brick;
    gint z;

    execute(gui->args);
    brick = gui->args->result;
    gwy_param_table_slider_restrict_range(gui->table_options, PARAM_Z, 0, gwy_brick_get_zres(brick) - 1);
    z = CLAMP(gwy_params_get_int(gui->args->params, PARAM_Z), 0, gwy_brick_get_zres(brick) - 1);
    update_image(gui, z);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint k, m;
    GwyBrick *original = args->result;
    GwyBrick *newbrick;
    GwyDataField *dfield;
    gint level = gwy_params_get_int(params, PARAM_Z);
    gint xres = gwy_brick_get_xres(original);
    gint yres = gwy_brick_get_yres(original);
    gint zres = gwy_brick_get_zres(original);
    gdouble xreal = gwy_brick_get_xreal(original);
    gdouble yreal = gwy_brick_get_yreal(original);
    gdouble zreal = gwy_brick_get_zreal(original);
    gint newzres = zres - 1;
    gboolean extract = gwy_params_get_boolean(params, PARAM_EXTRACT);

    newbrick = gwy_brick_new(xres, yres, newzres,
                             xreal, yreal, newzres*zreal/zres,
                             FALSE);

    gwy_brick_copy_units(original, newbrick);

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);

    //printf("extracting plane %d\n", level);
    m = 0;
    for (k = 0; k < zres; k++) {
        if (k == level) {
            if (extract) {
                if (!args->extracted) 
                    args->extracted = gwy_data_field_new_alike(dfield, FALSE);
                gwy_brick_extract_xy_plane(original, args->extracted, k);
            }
            continue;
        }
        gwy_brick_extract_xy_plane(original, dfield, k);
        gwy_brick_set_xy_plane(newbrick, dfield, m++);
    }

    g_object_unref(original);
    args->result = newbrick;
    args->extracted_level = level;

    g_object_unref(dfield);
}

static void
sanitise_one_param(GwyParams *params, gint id, gint min, gint max, gint defval)
{
    gint v;

    v = gwy_params_get_int(params, id);
    if (v >= min && v <= max) {
        gwy_debug("param #%d is %d, i.e. within range [%d..%d]", id, v, min, max);
        return;
    }
    gwy_debug("param #%d is %d, setting it to the default %d", id, v, defval);
    gwy_params_set_int(params, id, defval);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    sanitise_one_param(params, PARAM_Z, 0, gwy_brick_get_zres(args->brick), 0);
}





/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
