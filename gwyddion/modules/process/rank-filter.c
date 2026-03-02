/*
 *  $Id: rank-filter.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
#include <libprocess/datafield.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    MAX_SIZE = 1024,
    MAX_SIZE2 = (2*MAX_SIZE + 1)*(2*MAX_SIZE + 1),
};

enum {
    PARAM_SIZE,
    PARAM_PERCENTILE1,
    PARAM_PERCENTILE2,
    PARAM_RANK1,
    PARAM_RANK2,
    PARAM_BOTH,
    PARAM_DIFFERENCE,
    PARAM_NEW_IMAGE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *result2;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    gdouble percentscale;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             rank_filter         (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             add_new_field       (GwyContainer *data,
                                             gint oldid,
                                             GwyDataField *field,
                                             gdouble fraction);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("General k-th rank filter replacing data with k-th rank values from the neighborhood."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, rank_filter)

static gboolean
module_register(void)
{
    gwy_process_func_register("rank_filter",
                              (GwyProcessFunc)&rank_filter,
                              N_("/_Integral Transforms/_Rank Filter..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("K-th rank filter"));

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
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("_Pixel radius"), 1, MAX_SIZE, 20);
    gwy_param_def_add_percentage(paramdef, PARAM_PERCENTILE1, "percentile1", _("_Percentile"), 0.75);
    gwy_param_def_add_percentage(paramdef, PARAM_PERCENTILE2, "percentile2", _("_Percentile"), 0.25);
    gwy_param_def_add_int(paramdef, PARAM_RANK1, NULL, _("Ra_nk"), 0, MAX_SIZE2, 0);
    gwy_param_def_add_int(paramdef, PARAM_RANK2, NULL, _("Ra_nk"), 0, MAX_SIZE2, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_BOTH, "both", _("_Second filter"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFFERENCE, "difference", _("Calculate _difference"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_IMAGE, "new_image", _("Create new image"), TRUE);
    return paramdef;
}

static void
rank_filter(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id;
    GQuark quark;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;

    if (gwy_params_get_boolean(args.params, PARAM_BOTH)) {
        if (gwy_params_get_boolean(args.params, PARAM_DIFFERENCE)) {
            gwy_data_field_subtract_fields(args.result, args.result, args.result2);
            add_new_field(data, id, args.result, -1.0);
        }
        else {
            add_new_field(data, id, args.result, gwy_params_get_double(args.params, PARAM_PERCENTILE1));
            add_new_field(data, id, args.result2, gwy_params_get_double(args.params, PARAM_PERCENTILE2));
        }
    }
    else {
        if (gwy_params_get_boolean(args.params, PARAM_NEW_IMAGE))
            add_new_field(data, id, args.result, gwy_params_get_double(args.params, PARAM_PERCENTILE1));
        else {
            gwy_app_undo_qcheckpointv(data, 1, &quark);
            gwy_container_set_object(data, quark, args.result);
            gwy_app_channel_log_add_proc(data, id, id);
        }
    }

end:
    GWY_OBJECT_UNREF(args.result2);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static void
add_new_field(GwyContainer *data, gint oldid, GwyDataField *field, gdouble fraction)
{
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    if (fraction < 0.0)
        gwy_app_set_data_field_title(data, newid, _("Rank difference"));
    else {
        gchar *s = g_strdup_printf(_("Rank filtered (%.1f %%)"), 100.0*fraction);
        gwy_app_set_data_field_title(data, newid, s);
        g_free(s);
    }
    gwy_app_channel_log_add(data, oldid, newid, NULL, NULL);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Rank Filter"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Kernel Size"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIZE, args->field);

    gwy_param_table_append_header(table, -1, _("Rank"));
    gwy_param_table_append_slider(table, PARAM_PERCENTILE1);
    gwy_param_table_append_slider(table, PARAM_RANK1);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_BOTH);
    gwy_param_table_append_slider(table, PARAM_PERCENTILE2);
    gwy_param_table_append_slider(table, PARAM_RANK2);
    gwy_param_table_append_checkbox(table, PARAM_DIFFERENCE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_NEW_IMAGE);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    gdouble percentscale = gui->percentscale;
    gint size = gwy_params_get_int(params, PARAM_SIZE);

    if (id == PARAM_RANK1)
        gwy_param_table_set_double(table, PARAM_PERCENTILE1, gwy_params_get_int(params, PARAM_RANK1)*percentscale);
    if (id == PARAM_RANK2)
        gwy_param_table_set_double(table, PARAM_PERCENTILE2, gwy_params_get_int(params, PARAM_RANK2)*percentscale);

    if (id < 0 || id == PARAM_SIZE) {
        gint kres = 2*size + 1;
        gint n = gwy_data_field_get_elliptic_area_size(kres, kres);

        percentscale = gui->percentscale = 1.0/n;
        gwy_param_table_slider_restrict_range(table, PARAM_RANK1, 0.0, n - 1.0);
        gwy_param_table_slider_restrict_range(table, PARAM_RANK2, 0.0, n - 1.0);
    }

    if (id < 0 || id == PARAM_SIZE || id == PARAM_PERCENTILE1)
        gwy_param_table_set_int(table, PARAM_RANK1, gwy_params_get_double(params, PARAM_PERCENTILE1)/percentscale);
    if (id < 0 || id == PARAM_SIZE || id == PARAM_PERCENTILE2)
        gwy_param_table_set_int(table, PARAM_RANK2, gwy_params_get_double(params, PARAM_PERCENTILE2)/percentscale);
    if (id < 0 || id == PARAM_BOTH) {
        gboolean both = gwy_params_get_boolean(params, PARAM_BOTH);
        gwy_param_table_set_sensitive(table, PARAM_NEW_IMAGE, !both);
        gwy_param_table_set_sensitive(table, PARAM_PERCENTILE2, both);
        gwy_param_table_set_sensitive(table, PARAM_RANK2, both);
        gwy_param_table_set_sensitive(table, PARAM_DIFFERENCE, both);
    }
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    gint size = gwy_params_get_int(args->params, PARAM_SIZE);
    gboolean both = gwy_params_get_boolean(args->params, PARAM_BOTH);
    gdouble percentile1 = gwy_params_get_double(args->params, PARAM_PERCENTILE1);
    gdouble percentile2 = gwy_params_get_double(args->params, PARAM_PERCENTILE2);
    gint xres, yres, rank, kres = 2*size + 1;
    gint n = gwy_data_field_get_elliptic_area_size(kres, kres);
    GwyDataField *kernel, *field = args->field, *result = args->result;
    gboolean ok = FALSE;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    gwy_app_wait_start(wait_window, _("Filtering..."));

    kernel = gwy_data_field_new(kres, kres, 1.0, 1.0, TRUE);
    gwy_data_field_elliptic_area_fill(kernel, 0, 0, kres, kres, 1.0);
    gwy_data_field_copy(field, result, FALSE);
    rank = GWY_ROUND(percentile1*(n - 1));
    if (!gwy_data_field_area_filter_kth_rank(result, kernel, 0, 0, xres, yres, rank, gwy_app_wait_set_fraction))
        goto end;

    if (both) {
        args->result2 = result = gwy_data_field_duplicate(field);
        rank = GWY_ROUND(percentile2*(n - 1));
        if (!gwy_data_field_area_filter_kth_rank(result, kernel, 0, 0, xres, yres, rank, gwy_app_wait_set_fraction))
            goto end;
    }

    ok = TRUE;

end:
    gwy_app_wait_finish();
    g_object_unref(kernel);

    return ok;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;

    if (gwy_params_get_boolean(params, PARAM_BOTH))
        gwy_params_set_boolean(params, PARAM_NEW_IMAGE, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
