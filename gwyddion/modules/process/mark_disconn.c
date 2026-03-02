/*
 *  $Id: mark_disconn.c 25745 2023-10-01 18:46:03Z yeti-dn $
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* The same as in scars. */
typedef enum {
    FEATURES_POSITIVE = 1 << 0,
    FEATURES_NEGATIVE = 1 << 2,
    FEATURES_BOTH     = (FEATURES_POSITIVE | FEATURES_NEGATIVE),
} GwyFeaturesType;

enum {
    PARAM_TYPE,
    PARAM_RADIUS,
    PARAM_THRESHOLD,
    PARAM_COMBINE_TYPE,
    PARAM_COMBINE,
    PARAM_MASK_COLOR,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mark_disconn        (GwyContainer *data,
                                             GwyRunType run);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Creates mask of values disconnected to the rest."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, mark_disconn)

static gboolean
module_register(void)
{
    gwy_process_func_register("mark_disconn",
                              (GwyProcessFunc)&mark_disconn,
                              N_("/_Correct Data/Mask of _Disconnected..."),
                              GWY_STOCK_DISCONNECTED,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Mark data disconnected from other values"));

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
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("Defect type"),
                              feature_types, G_N_ELEMENTS(feature_types), FEATURES_BOTH);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("_Threshold"), 0.0, 1.0, 0.1);
    gwy_param_def_add_int(paramdef, PARAM_RADIUS, "radius", _("Defect _radius"), 1, 240, 5);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
mark_disconn(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
            goto end;
    }

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Mark Disconnected"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio(table, PARAM_TYPE);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, _("px"));
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_slider_set_steps(table, PARAM_THRESHOLD, 0.001, 0.1);
    gwy_param_table_slider_set_digits(table, PARAM_THRESHOLD, 4);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_MASK_COLOR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    if (execute(args, GTK_WINDOW(gui->dialog))) {
        gwy_data_field_data_changed(args->result);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
    else
        gwy_data_field_clear(args->result);
}

/* Remove from mask pixels with values that do not belong to the largest
 * contiguous block of values in the height distribution. */
static guint
unmark_disconnected_values(GwyDataField *dfield, GwyDataField *inclmask,
                           guint n, gdouble threshold)
{
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint lineres = (guint)floor(2.5*cbrt(xres*yres - n) + 0.5);
    GwyDataLine *dline = gwy_data_line_new(lineres, lineres, FALSE);
    const gdouble *d;
    gdouble *m;
    guint nn, i, blockstart = 0, bestblockstart = 0, bestblocklen = 0;
    gdouble blocksum = 0.0, bestblocksum = 0.0;
    gdouble real, off, min, max, rho_zero;

    gwy_data_field_area_dh(dfield, inclmask, dline, 0, 0, xres, yres, lineres);
    rho_zero = gwy_data_line_get_max(dline)/sqrt(xres*yres - n)*threshold;
    d = gwy_data_line_get_data_const(dline);
    lineres = gwy_data_line_get_res(dline);

    for (i = 0; i <= lineres; i++) {
        if (i == lineres || (i && d[i] + d[i-1] < rho_zero)) {
            if (blocksum > bestblocksum) {
                bestblocksum = blocksum;
                bestblockstart = blockstart;
                bestblocklen = i - blockstart;
            }
            blockstart = i+1;
            blocksum = 0.0;
        }
        else
            blocksum += d[i];
    }

    if (bestblocklen == lineres) {
        g_object_unref(dline);
        return 0;
    }

    real = gwy_data_line_get_real(dline);
    off = gwy_data_line_get_offset(dline);
    min = off + real/lineres*bestblockstart;
    max = off + real/lineres*(bestblockstart + bestblocklen + 1);
    m = gwy_data_field_get_data(inclmask);
    d = gwy_data_field_get_data_const(dfield);
    nn = 0;
    for (i = 0; i < xres*yres; i++) {
        if (m[i] > 0.0 && (d[i] < min || d[i] > max)) {
            m[i] = 0.0;
            nn++;
        }
    }

    g_object_unref(dline);
    return nn;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean combine = gwy_params_get_boolean(params, PARAM_COMBINE);
    GwyMergeType combine_type = gwy_params_get_enum(params, PARAM_COMBINE_TYPE);
    gint size = 2*gwy_params_get_int(params, PARAM_RADIUS) + 1;
    gdouble threshold = gwy_params_get_double(params, PARAM_THRESHOLD);
    GwyFeaturesType type = gwy_params_get_enum(params, PARAM_TYPE);
    GwyDataField *field = args->field, *mask = args->mask, *result = args->result;
    GwyDataField *difffield = NULL, *kernel = NULL;
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    guint n, nn;
    gboolean ok = FALSE;

    gwy_app_wait_start(wait_window, _("Initializing..."));
    /* Remove the positive, negative (or both) defects using a filter.  This produces a defect-free field. */
    gwy_data_field_copy(field, result, FALSE);
    if (!gwy_app_wait_set_message(_("Filtering...")))
        goto finish;

    kernel = gwy_data_field_new(size, size, size, size, TRUE);
    n = gwy_data_field_elliptic_area_fill(kernel, 0, 0, size, size, 1.0);
    if (type == FEATURES_POSITIVE || type == FEATURES_NEGATIVE) {
        GwyMinMaxFilterType filtertpe = (type == FEATURES_POSITIVE
                                         ? GWY_MIN_MAX_FILTER_OPENING
                                         : GWY_MIN_MAX_FILTER_CLOSING);
        gwy_data_field_area_filter_min_max(result, kernel, filtertpe, 0, 0, xres, yres);
    }
    else {
        if (!gwy_data_field_area_filter_kth_rank(result, kernel, 0, 0, xres, yres, n/2, gwy_app_wait_set_fraction))
            goto finish;
    }

    /* Then find look at the difference and mark any outliers in it because these must be defects. */
    difffield = gwy_data_field_new_alike(field, FALSE);
    gwy_data_field_subtract_fields(difffield, field, result);
    gwy_data_field_fill(result, 1.0);

    if (!gwy_app_wait_set_message(_("Marking outliers...")))
        goto finish;

    n = 0;
    while ((nn = unmark_disconnected_values(difffield, result, n, 4.0*threshold)))
        n += nn;

    gwy_data_field_grains_invert(result);
    if (mask && combine) {
        if (combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(result, mask);
        else if (combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(result, mask);
    }
    ok = TRUE;

finish:
    gwy_app_wait_finish();
    GWY_OBJECT_UNREF(kernel);
    GWY_OBJECT_UNREF(difffield);
    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
