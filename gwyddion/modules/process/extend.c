/*
 *  $Id: extend.c 23985 2021-08-16 12:27:37Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    EXTEND_MAX = 2048
};

enum {
    PARAM_UP,
    PARAM_DOWN,
    PARAM_LEFT,
    PARAM_RIGHT,
    PARAM_SYMMETRIC,
    PARAM_EXTERIOR,
    PARAM_KEEP_OFFSETS,
    PARAM_NEW_CHANNEL,
    PARAM_UPDATE,
    INFO_NEWDIM,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached values for dimensions display. */
    gint xres;
    gint yres;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *view;
    gint last_active;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             extend              (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extends image by adding borders."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2012",
};

GWY_MODULE_QUERY2(module_info, extend)

static gboolean
module_register(void)
{
    gwy_process_func_register("extend",
                              (GwyProcessFunc)&extend,
                              N_("/_Basic Operations/E_xtend..."),
                              GWY_STOCK_EXTEND,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Extend by adding borders"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum exteriors[] = {
        { N_("Mean"),              GWY_EXTERIOR_FIXED_VALUE,   },
        { N_("exterior|Border"),   GWY_EXTERIOR_BORDER_EXTEND, },
        { N_("exterior|Mirror"),   GWY_EXTERIOR_MIRROR_EXTEND, },
        { N_("exterior|Periodic"), GWY_EXTERIOR_PERIODIC,      },
        { N_("exterior|Laplace"),  GWY_EXTERIOR_LAPLACE,       },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_int(paramdef, PARAM_UP, "up", _("_Up"), 0, EXTEND_MAX, 0);
    gwy_param_def_add_int(paramdef, PARAM_DOWN, "down", _("_Down"), 0, EXTEND_MAX, 0);
    gwy_param_def_add_int(paramdef, PARAM_LEFT, "left", _("_Left"), 0, EXTEND_MAX, 0);
    gwy_param_def_add_int(paramdef, PARAM_RIGHT, "right", _("_Right"), 0, EXTEND_MAX, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_SYMMETRIC, "symmetric", _("Extend _symmetrically"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_EXTERIOR, "exterior", _("_Exterior type"),
                              exteriors, G_N_ELEMENTS(exteriors), GWY_EXTERIOR_MIRROR_EXTEND);
    gwy_param_def_add_boolean(paramdef, PARAM_KEEP_OFFSETS, "keep_offsets", _("Keep lateral offsets"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_CHANNEL, "new_channel", _("Create new image"), FALSE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    return paramdef;
}

static void
extend(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *fields[3], *result;
    GQuark quarks[3], quark;
    GwyParams *params;
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint n, id, newid, up, down, left, right, exterior;
    gboolean new_channel, keep_offsets;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_KEY, quarks + 0,
                                     GWY_APP_MASK_FIELD_KEY, quarks + 1,
                                     GWY_APP_SHOW_FIELD_KEY, quarks + 2,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(fields[0]);

    args.field = fields[0];
    args.xres = gwy_data_field_get_xres(args.field);
    args.yres = gwy_data_field_get_yres(args.field);
    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        args.result = gwy_data_field_duplicate(args.field);
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    up = gwy_params_get_int(params, PARAM_UP);
    down = gwy_params_get_int(params, PARAM_DOWN);
    left = gwy_params_get_int(params, PARAM_LEFT);
    right = gwy_params_get_int(params, PARAM_RIGHT);
    exterior = gwy_params_get_int(params, PARAM_EXTERIOR);
    new_channel = gwy_params_get_boolean(params, PARAM_NEW_CHANNEL);
    keep_offsets = gwy_params_get_boolean(params, PARAM_KEEP_OFFSETS);

    if (!new_channel) {
        n = 1;
        if (fields[1]) {
            n++;
            if (fields[2])
                n++;
        }
        else if (fields[2]) {
            quarks[1] = quarks[2];
            quarks[2] = 0;
            n++;
        }
        gwy_app_undo_qcheckpointv(data, n, quarks);
    }

    if (outcome == GWY_DIALOG_HAVE_RESULT) {
        result = args.result;
        args.result = NULL;
    }
    else {
        result = gwy_data_field_extend(fields[0], left, right, up, down, exterior, gwy_data_field_get_avg(fields[0]),
                                       keep_offsets);
    }

    if (!new_channel) {
        gwy_data_field_assign(fields[0], result);
        gwy_data_field_data_changed(fields[0]);
        gwy_app_channel_log_add_proc(data, id, id);
    }
    else {
        newid = gwy_app_data_browser_add_data_field(result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_RANGE,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                GWY_DATA_ITEM_SELECTIONS,
                                0);
        gwy_app_set_data_field_title(data, newid, _("Extended"));
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    g_object_unref(result);

    if (fields[1]) {
        /* Do not apply Laplace interpolation to masks, it makes no sense. */
        GwyExteriorType mask_exterior = (exterior == GWY_EXTERIOR_LAPLACE ? GWY_EXTERIOR_FIXED_VALUE : exterior);
        gdouble fill_value = 0.0;

        /* Set fill_value to 1 for fields that are mostly masked, 0 for fields that are mostly unmasked. */
        if (mask_exterior == GWY_EXTERIOR_FIXED_VALUE) {
            guint n_unmasked, xres, yres;

            xres = gwy_data_field_get_xres(fields[1]);
            yres = gwy_data_field_get_yres(fields[1]);
            gwy_data_field_area_count_in_range(fields[1], NULL, 0, 0, xres, yres, 0.0, 0.0, &n_unmasked, NULL);
            fill_value = (n_unmasked > 0.5*xres*yres);
        }
        result = gwy_data_field_extend(fields[1], left, right, up, down, mask_exterior, fill_value, keep_offsets);
        if (!new_channel) {
            gwy_data_field_assign(fields[1], result);
            gwy_data_field_data_changed(fields[1]);
        }
        else {
            quark = gwy_app_get_mask_key_for_id(newid);
            gwy_container_set_object(data, quark, result);
        }
        g_object_unref(result);
    }

    if (fields[2]) {
        result = gwy_data_field_extend(fields[2], left, right, up, down, exterior, gwy_data_field_get_avg(fields[2]),
                                       keep_offsets);
        if (!new_channel) {
            gwy_data_field_assign(fields[2], result);
            gwy_data_field_data_changed(fields[2]);
        }
        else {
            quark = gwy_app_get_show_key_for_id(newid);
            gwy_container_set_object(data, quark, result);
        }
        g_object_unref(result);
    }

end:
    g_object_unref(params);
    GWY_OBJECT_UNREF(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.last_active = PARAM_UP;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Extend"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Borders"));
    gwy_param_table_append_slider(table, PARAM_UP);
    gwy_param_table_slider_add_alt(table, PARAM_UP);
    gwy_param_table_alt_set_field_pixel_y(table, PARAM_UP, args->field);
    gwy_param_table_append_slider(table, PARAM_DOWN);
    gwy_param_table_slider_add_alt(table, PARAM_DOWN);
    gwy_param_table_alt_set_field_pixel_y(table, PARAM_DOWN, args->field);
    gwy_param_table_append_slider(table, PARAM_LEFT);
    gwy_param_table_slider_add_alt(table, PARAM_LEFT);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_LEFT, args->field);
    gwy_param_table_append_slider(table, PARAM_RIGHT);
    gwy_param_table_slider_add_alt(table, PARAM_RIGHT);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_RIGHT, args->field);
    gwy_param_table_append_checkbox(table, PARAM_SYMMETRIC);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_info(table, INFO_NEWDIM, _("New dimensions"));
    gwy_param_table_set_unitstr(table, INFO_NEWDIM, _("px"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_EXTERIOR);
    gwy_param_table_append_checkbox(table, PARAM_KEEP_OFFSETS);
    gwy_param_table_append_checkbox(table, PARAM_NEW_CHANNEL);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean symmetric = gwy_params_get_boolean(params, PARAM_SYMMETRIC);
    gint extend_by = 0;
    gchar *s;

    if ((id < 0 || id == PARAM_SYMMETRIC) && symmetric)
        extend_by = gwy_params_get_int(params, gui->last_active);

    if (id == PARAM_UP || id == PARAM_DOWN || id == PARAM_LEFT || id == PARAM_RIGHT) {
        extend_by = gwy_params_get_int(params, id);
        gui->last_active = id;
    }

    if ((id < 0 || id == PARAM_SYMMETRIC || id == PARAM_UP || id == PARAM_DOWN || id == PARAM_LEFT || id == PARAM_RIGHT)
        && symmetric) {
        gwy_param_table_set_int(table, PARAM_UP, extend_by);
        gwy_param_table_set_int(table, PARAM_DOWN, extend_by);
        gwy_param_table_set_int(table, PARAM_LEFT, extend_by);
        gwy_param_table_set_int(table, PARAM_RIGHT, extend_by);
    }

    if (id < 0 || id == PARAM_UP || id == PARAM_DOWN || id == PARAM_LEFT || id == PARAM_RIGHT) {
        gint up = gwy_params_get_int(params, PARAM_UP);
        gint down = gwy_params_get_int(params, PARAM_DOWN);
        gint left = gwy_params_get_int(params, PARAM_LEFT);
        gint right = gwy_params_get_int(params, PARAM_RIGHT);
        s = g_strdup_printf(_("%d × %d"), args->xres + left + right, args->yres + up + down);
        gwy_param_table_info_set_valuestr(table, INFO_NEWDIM, s);
        g_free(s);
    }

    if (id != PARAM_NEW_CHANNEL && id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *field = args->field, *result;
    gint up = gwy_params_get_int(params, PARAM_UP);
    gint down = gwy_params_get_int(params, PARAM_DOWN);
    gint left = gwy_params_get_int(params, PARAM_LEFT);
    gint right = gwy_params_get_int(params, PARAM_RIGHT);
    GwyExteriorType exterior = gwy_params_get_int(params, PARAM_EXTERIOR);
    gboolean keep_offsets = gwy_params_get_boolean(params, PARAM_KEEP_OFFSETS);

    result = gwy_data_field_extend(field, left, right, up, down, exterior, gwy_data_field_get_avg(field), keep_offsets);
    gwy_data_field_assign(args->result, result);
    g_object_unref(result);
    gwy_data_field_data_changed(args->result);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view), PREVIEW_SIZE);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;

    if (gwy_params_get_int(params, PARAM_UP) != gwy_params_get_int(params, PARAM_DOWN)
        || gwy_params_get_int(params, PARAM_LEFT) != gwy_params_get_int(params, PARAM_RIGHT)
        || gwy_params_get_int(params, PARAM_LEFT) != gwy_params_get_int(params, PARAM_UP))
        gwy_params_set_boolean(params, PARAM_SYMMETRIC, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
