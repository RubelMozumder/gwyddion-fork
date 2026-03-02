/*
 *  $Id: drift.c 25638 2023-09-07 12:23:28Z yeti-dn $
 *  Copyright (C) 2007-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  ‘Distribute’ and ‘Replace’ options were originally added by Sameer Grover  (sameer.grover.1@gmail.com)
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
#include <libprocess/linestats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/interpolation.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

typedef enum {
    PREVIEW_CORRECTED = 0,
    PREVIEW_MASK      = 1,
} DriftPreviewType;

enum {
    PARAM_INTERP,
    PARAM_RANGE,
    PARAM_DISTRIBUTE,
    PARAM_NEW_IMAGE,
    PARAM_DO_CORRECT,
    PARAM_DO_PLOT,
    PARAM_EXCLUDE_LINEAR,
    PARAM_DISPLAY,
    PARAM_TARGET_GRAPH,
    PARAM_MASK_COLOR,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyDataLine *drift;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyGraphModel *gmodel;
    GwyDataView *dataview;
    GwyPixmapLayer *mlayer;
    GwyPixmapLayer *blayer;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register              (void);
static GwyParamDef*     define_module_params         (void);
static void             compensate_drift             (GwyContainer *data,
                                                      GwyRunType runtype);
static void             gather_quarks_for_one_image  (GwyContainer *data,
                                                      gint id,
                                                      GArray *quarks);
static void             apply_correction_to_one_image(ModuleArgs *args,
                                                      GwyContainer *data,
                                                      gint id);
static void             execute                      (ModuleArgs *args);
static GwyDialogOutcome run_gui                      (ModuleArgs *args,
                                                      GwyContainer *data,
                                                      gint id);
static void             param_changed                (ModuleGUI *gui,
                                                      gint id);
static void             preview                      (gpointer user_data);
static void             mask_process                 (GwyDataField *maskfield,
                                                      GwyDataLine *drift);
static void             gwy_data_field_normalize_rows(GwyDataField *field);
static void             match_line                   (gint res,
                                                      const gdouble *ref,
                                                      const gdouble *cmp,
                                                      gint maxoff,
                                                      gdouble *offset,
                                                      gdouble *score,
                                                      gdouble *d);
static void             calculate_correlation_scores (GwyDataField *field,
                                                      gint range,
                                                      gint maxoffset,
                                                      gdouble supersample,
                                                      GwyInterpolationType interp,
                                                      GwyDataField *scores,
                                                      GwyDataField *offsets);
static void             calculate_drift_very_naive   (GwyDataField *offsets,
                                                      GwyDataField *scores,
                                                      GwyDataLine *drift);
static void             apply_drift                  (GwyDataField *field,
                                                      GwyDataLine *drift,
                                                      GwyInterpolationType interp);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Evaluates and/or correct thermal drift in fast scan axis."),
    "Petr Klapetek <petr@klapetek.cz>, Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2007",
};

GWY_MODULE_QUERY2(module_info, drift)

static gboolean
module_register(void)
{
    gwy_process_func_register("drift",
                              (GwyProcessFunc)&compensate_drift,
                              N_("/_Distortion/Compensate _Drift..."),
                              GWY_STOCK_DRIFT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Evaluate/correct thermal drift in fast scan axis"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum previews[] = {
        { N_("Correc_ted data"), PREVIEW_CORRECTED, },
        { N_("Drift _lines"),    PREVIEW_MASK,      },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_BSPLINE);
    gwy_param_def_add_int(paramdef, PARAM_RANGE, "range", _("_Search range"), 1, 50, 12);
    gwy_param_def_add_boolean(paramdef, PARAM_DISTRIBUTE, "distribute", _("_Apply to all compatible images"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_IMAGE, "new-image", _("Create new image"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_CORRECT, "do-correct", _("Correct _data"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_PLOT, "do-plot", _("Plot drift _graph"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_EXCLUDE_LINEAR, "exclude-linear", _("_Exclude linear skew"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              previews, G_N_ELEMENTS(previews), PREVIEW_MASK);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
compensate_drift(GwyContainer *data, GwyRunType runtype)
{
    enum { compat_flags = GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_REAL | GWY_DATA_COMPATIBILITY_LATERAL };
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyParams *params;
    GwyDataField *field, *mask, *sfield;
    gboolean do_plot, do_correct, new_image, distribute;
    GArray *undo_quarks;
    gint *image_ids;
    gint id, i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mask,
                                     GWY_APP_SHOW_FIELD, &sfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    args.mask = gwy_data_field_new_alike(field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);
    args.result = gwy_data_field_new_alike(field, TRUE);
    args.drift = gwy_data_line_new(gwy_data_field_get_yres(field), gwy_data_field_get_yreal(field), TRUE);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    new_image = gwy_params_get_boolean(params, PARAM_NEW_IMAGE);
    do_plot = gwy_params_get_boolean(params, PARAM_DO_PLOT);
    do_correct = gwy_params_get_boolean(params, PARAM_DO_CORRECT);
    distribute = gwy_params_get_boolean(params, PARAM_DISTRIBUTE);
    if (do_plot) {
        GwyAppDataId target_graph_id = gwy_params_get_data_id(params, PARAM_TARGET_GRAPH);
        GwyGraphModel *gmodel;
        GwyGraphCurveModel *gcmodel;

        gmodel = gwy_graph_model_new();
        gwy_graph_model_set_units_from_data_line(gmodel, args.drift);
        g_object_set(gmodel,
                     "title", _("Drift"),
                     "axis-label-left", _("drift"),
                     "axis-label-bottom", "y",
                     NULL);

        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, args.drift, -1, -1);
        g_object_set(gcmodel, "description", _("x-axis drift"), NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        gwy_app_add_graph_or_curves(gmodel, data, &target_graph_id, 1);
        g_object_unref(gmodel);
    }

    if (!do_correct)
        goto end;

    if (!distribute) {
        if (!new_image) {
            undo_quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));
            gather_quarks_for_one_image(data, id, undo_quarks);
            gwy_app_undo_qcheckpointv(data, undo_quarks->len, (GQuark*)undo_quarks->data);
            g_array_free(undo_quarks, TRUE);
        }
        apply_correction_to_one_image(&args, data, id);
        goto end;
    }

    image_ids = gwy_app_data_browser_get_data_ids(data);
    if (!new_image) {
        undo_quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));
        for (i = 0; image_ids[i] != -1; i++) {
            GwyDataField *otherfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(image_ids[i]));
            if (!gwy_data_field_check_compatibility(field, otherfield, compat_flags))
                gather_quarks_for_one_image(data, image_ids[i], undo_quarks);
        }
        gwy_app_undo_qcheckpointv(data, undo_quarks->len, (GQuark*)undo_quarks->data);
        g_array_free(undo_quarks, TRUE);
    }
    for (i = 0; image_ids[i] != -1; i++) {
        GwyDataField *otherfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(image_ids[i]));
        if (gwy_data_field_check_compatibility(field, otherfield, compat_flags))
            continue;

        apply_correction_to_one_image(&args, data, image_ids[i]);
    }
    g_free(image_ids);

end:
    g_object_unref(params);
    g_object_unref(args.result);
    g_object_unref(args.mask);
    g_object_unref(args.drift);
}

static void
gather_quarks_for_one_image(GwyContainer *data, gint id, GArray *quarks)
{
    GObject *object;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(id);
    object = gwy_container_get_object(data, quark);
    g_assert(GWY_IS_DATA_FIELD(object));
    g_array_append_val(quarks, quark);

    quark = gwy_app_get_mask_key_for_id(id);
    if (gwy_container_gis_object(data, quark, &object) && GWY_IS_DATA_FIELD(object))
        g_array_append_val(quarks, quark);

    quark = gwy_app_get_show_key_for_id(id);
    if (gwy_container_gis_object(data, quark, &object) && GWY_IS_DATA_FIELD(object))
        g_array_append_val(quarks, quark);
}

static void
apply_correction_to_one_image(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyInterpolationType interp = gwy_params_get_enum(args->params, PARAM_INTERP);
    gboolean new_image = gwy_params_get_boolean(args->params, PARAM_NEW_IMAGE);
    gboolean distribute = gwy_params_get_boolean(args->params, PARAM_DISTRIBUTE);
    GwyDataField *field, *mask = NULL, *show = NULL;
    gchar *title, *newtitle;
    gint newid;

    field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    g_assert(GWY_IS_DATA_FIELD(field));
    gwy_container_gis_object(data, gwy_app_get_mask_key_for_id(id), &mask);
    gwy_container_gis_object(data, gwy_app_get_show_key_for_id(id), &show);

    if (!new_image) {
        apply_drift(field, args->drift, interp);
        gwy_data_field_data_changed(field);
        if (mask) {
            apply_drift(mask, args->drift, GWY_INTERPOLATION_ROUND);
            gwy_data_field_data_changed(mask);
        }
        if (show) {
            apply_drift(show, args->drift, interp);
            gwy_data_field_data_changed(show);
        }
        gwy_app_channel_log_add_proc(data, id, id);
        return;
    }

    field = gwy_data_field_duplicate(field);
    apply_drift(field, args->drift, interp);
    newid = gwy_app_data_browser_add_data_field(field, data, !distribute);
    g_object_unref(field);
    title = gwy_app_get_data_field_title(data, id);
    newtitle = g_strdup_printf("%s (%s)", title, _("Drift-corrected"));
    gwy_app_set_data_field_title(data, newid, newtitle);
    g_free(newtitle);
    g_free(title);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_channel_log_add_proc(data, id, newid);

    if (mask) {
        mask = gwy_data_field_duplicate(mask);
        apply_drift(mask, args->drift, GWY_INTERPOLATION_ROUND);
        gwy_container_pass_object(data, gwy_app_get_mask_key_for_id(newid), mask);
    }
    if (show) {
        show = gwy_data_field_duplicate(show);
        apply_drift(show, args->drift, interp);
        gwy_container_pass_object(data, gwy_app_get_show_key_for_id(newid), show);
    }
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *dataview;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    /* Create an empty graph model just for compatibility check. */
    gui.gmodel = gwy_graph_model_new();
    gwy_graph_model_set_units_from_data_field(gui.gmodel, args->field, 1, 0, 1, 0);
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->mask);
    gwy_container_set_object_by_name(gui.data, "/1/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

    gui.dialog = gwy_dialog_new(_("Compensate Drift"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    gui.dataview = GWY_DATA_VIEW(dataview);
    gui.blayer = g_object_ref(gwy_data_view_get_base_layer(gui.dataview));
    gui.mlayer = g_object_ref(gwy_data_view_get_alpha_layer(gui.dataview));

    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), gui.dataview, FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Drift"));
    gwy_param_table_append_slider(table, PARAM_RANGE);
    gwy_param_table_set_unitstr(table, PARAM_RANGE, _("rows"));
    gwy_param_table_append_checkbox(table, PARAM_EXCLUDE_LINEAR);
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, NULL, -1);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_checkbox(table, PARAM_DO_CORRECT);
    gwy_param_table_append_checkbox(table, PARAM_NEW_IMAGE);
    gwy_param_table_append_checkbox(table, PARAM_DISTRIBUTE);
    gwy_param_table_append_checkbox(table, PARAM_DO_PLOT);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, gui.gmodel);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    param_changed(&gui, PARAM_DISPLAY);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.mlayer);
    g_object_unref(gui.blayer);
    g_object_unref(gui.gmodel);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_DO_PLOT) {
        gboolean do_plot = gwy_params_get_boolean(params, PARAM_DO_PLOT);
        gwy_param_table_set_sensitive(table, PARAM_TARGET_GRAPH, do_plot);
    }
    if (id == PARAM_DISPLAY) {
        DriftPreviewType display = gwy_params_get_enum(params, PARAM_DISPLAY);
        if (display == PREVIEW_CORRECTED) {
            gwy_pixmap_layer_set_data_key(gui->blayer, "/1/data");
            gwy_data_view_set_alpha_layer(gui->dataview, NULL);
        }
        else {
            gwy_pixmap_layer_set_data_key(gui->blayer, "/0/data");
            gwy_data_view_set_alpha_layer(gui->dataview, gui->mlayer);
        }
    }
    if (id < 0 || id == PARAM_TARGET_GRAPH || id == PARAM_INTERP || id == PARAM_EXCLUDE_LINEAR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    mask_process(args->mask, args->drift);
    gwy_data_field_data_changed(args->result);
    gwy_data_field_data_changed(args->mask);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
mask_process(GwyDataField *maskfield, GwyDataLine *drift)
{
    gint i, j, k, step, pos, xres, yres, w, from, to;
    gdouble *mdata, *rdata;

    gwy_data_field_clear(maskfield);
    xres = gwy_data_field_get_xres(maskfield);
    yres = gwy_data_field_get_yres(maskfield);

    step = xres/10;
    w = (xres + 3*PREVIEW_SIZE/4)/PREVIEW_SIZE;
    w = MAX(w, 1);
    rdata = gwy_data_line_get_data(drift);
    mdata = gwy_data_field_get_data(maskfield);
    for (i = 0; i < yres; i++) {
        for (j = -2*step - step/2; j <= xres + 2*step + step/2; j += step) {
            pos = j + GWY_ROUND(gwy_data_line_rtoi(drift, rdata[i]));
            from = MAX(0, pos - w/2);
            to = MIN(pos + (w - w/2) - 1, xres-1);
            for (k = from; k <= to; k++)
                mdata[i*xres + k] = 1.0;
        }
    }
}

/**
 * gwy_data_field_normalize_rows:
 * @field: A data field.
 *
 * Normalizes all rows to have mean value 0 and rms 1 (unless they consist of
 * all identical values, then they will have rms 0).
 **/
static void
gwy_data_field_normalize_rows(GwyDataField *field)
{
    gdouble *data, *row;
    gdouble avg, rms;
    gint xres, yres, i, j;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    data = gwy_data_field_get_data(field);

    for (i = 0; i < yres; i++) {
        row = data + i*xres;
        avg = 0.0;
        for (j = 0; j < xres; j++)
            avg += row[j];
        avg /= xres;
        rms = 0.0;
        for (j = 0; j < xres; j++) {
            row[j] -= avg;
            rms += row[j]*row[j];
        }
        if (rms > 0.0) {
            rms = sqrt(rms/xres);
            for (j = 0; j < xres; j++)
                row[j] /= rms;
        }
    }
}

/**
 * match_line:
 * @res: The length of @ref and @cmp.
 * @ref: Reference line, it must be normalized.
 * @cmp: Line to match to @ref, it must be normalized.
 * @maxoff: Maximum offset to try.
 * @offset: Location to store the offset.
 * @score: Location to store the score.
 * @d: Scratch buffer of size 2*@maxoff + 1.
 *
 * Matches a line to a reference line using correlation.
 *
 * The offset if from @ref to @cmp.
 **/
static void
match_line(gint res,
           const gdouble *ref,
           const gdouble *cmp,
           gint maxoff,
           gdouble *offset,
           gdouble *score,
           gdouble *d)
{
    gdouble s, z0, zm, zp;
    gint i, j, from, to;

    for (i = -maxoff; i <= maxoff; i++) {
        s = 0.0;
        from = MAX(0, -i);
        to = res-1 - MAX(0, i);
        for (j = from; j <= to; j++)
            s += ref[j]*cmp[j + i];
        d[i+maxoff] = s/(to - from + 1);
    }

    j = 0;
    for (i = -maxoff; i <= maxoff; i++) {
        if (d[i+maxoff] > d[j+maxoff])
            j = i;
    }

    *score = d[j+maxoff];
    if (ABS(j) == maxoff)
        *offset = j;
    else {
        /* Subpixel correction */
        z0 = d[j+maxoff];
        zm = d[j+maxoff - 1];
        zp = d[j+maxoff + 1];
        *offset = j + (zm - zp)/(zm + zp - 2.0*z0)/2.0;
    }
}

static void
calculate_correlation_scores(GwyDataField *field,
                             gint range,
                             gint maxoffset,
                             gdouble supersample,
                             GwyInterpolationType interp,
                             GwyDataField *scores,
                             GwyDataField *offsets)
{
    GwyDataField *dsuper;
    GwySIUnit *siunit;
    gint xres, yres, rangeres;
    gint maxoff, i, ii;
    const gdouble *ds;
    gdouble *sdata, *odata;
    gdouble dx;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    maxoff = (gint)ceil(supersample*maxoffset);
    xres *= supersample;
    dsuper = gwy_data_field_new_resampled(field, xres, yres, interp);
    gwy_data_field_normalize_rows(dsuper);

    rangeres = 2*range + 1;
    ds = gwy_data_field_get_data_const(dsuper);
    sdata = gwy_data_field_get_data(scores);
    odata = gwy_data_field_get_data(offsets);
    dx = gwy_data_field_get_dx(dsuper);
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            private(i,ii) \
            shared(odata,sdata,dsuper,range,rangeres,xres,yres,maxoff,ds,dx)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gdouble *tmp = g_new(gdouble, 2*maxoff + 1);

        for (i = ifrom; i < ito; i++) {
            odata[i*rangeres + range] = 0.0;
            sdata[i*rangeres + range] = 1.0;
            for (ii = i+1; ii <= i+range; ii++) {
                gdouble offset, score;

                if (ii < yres)
                    match_line(xres, ds + i*xres, ds + ii*xres, maxoff, &offset, &score, tmp);
                else {
                    offset = 0.0;
                    score = -1.0;
                }
                odata[i*rangeres + ii - (i - range)] = offset*dx;
                sdata[i*rangeres + ii - (i - range)] = score;
            }
        }
        g_free(tmp);
    }
    g_object_unref(dsuper);

    /* Fill symmetric correlation scores and offsets:
     * Delta_{i,j} = -Delta_{j,i}
     * w_{i,j} = w_{j,i}
     */
    for (i = 0; i < yres; i++) {
        for (ii = i-range; ii < i; ii++) {
            gdouble offset, score;

            if (ii >= 0) {
                offset = odata[ii*rangeres + i - (ii - range)];
                score = sdata[ii*rangeres + i - (ii - range)];
            }
            else {
                offset = 0.0;
                score = -1.0;
            }
            odata[i*rangeres + ii - (i - range)] = -offset;
            sdata[i*rangeres + ii - (i - range)] = score;
        }
    }

    gwy_data_field_set_yreal(scores, gwy_data_field_get_yreal(field));
    gwy_data_field_set_xreal(scores, gwy_data_field_itor(field, rangeres));
    gwy_data_field_set_xoffset(scores, gwy_data_field_itor(field, -range - 0.5));
    gwy_data_field_set_yreal(offsets, gwy_data_field_get_yreal(field));
    gwy_data_field_set_xreal(offsets, gwy_data_field_itor(field, rangeres));
    gwy_data_field_set_xoffset(offsets, gwy_data_field_itor(field, -range - 0.5));

    siunit = gwy_data_field_get_si_unit_xy(field);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(scores), siunit);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(offsets), siunit);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(offsets), siunit);
}

static void
calculate_drift_very_naive(GwyDataField *offsets,
                           GwyDataField *scores,
                           GwyDataLine *drift)
{
    const gdouble *doff, *dsco;
    gdouble *dd;
    gint range, xres, yres;
    gint i, j;
    gdouble dm;

    yres = gwy_data_field_get_yres(offsets);
    xres = gwy_data_field_get_xres(offsets);
    range = (xres - 1)/2;

    doff = gwy_data_field_get_data_const(offsets);
    dsco = gwy_data_field_get_data_const(scores);
    gwy_data_line_resample(drift, yres, GWY_INTERPOLATION_NONE);
    gwy_data_field_copy_units_to_data_line(offsets, drift);
    gwy_data_line_set_real(drift, gwy_data_field_get_yreal(offsets));
    dd = gwy_data_line_get_data(drift);

    for (i = 0; i < yres; i++) {
        gdouble w, sxx, sxz, q;

        sxx = sxz = w = 0.0;
        for (j = -range; j <= range; j++) {
            q = dsco[i*xres + j + range];
            q -= 0.6;
            q = MAX(q, 0.0);
            w += q;
            sxx += w*j*j;
            sxz += w*j*doff[i*xres + j + range];
        }
        if (!w) {
            g_warning("Cannot fit point %d", i);
            dd[i] = 0.0;
        }
        else
            dd[i] = sxz/sxx;
    }

    dm = dd[0];
    dd[0] = 0.0;
    for (i = 1; i < yres; i++) {
        gdouble d = dd[i];

        dd[i] = (dm + d)/2.0;
        dm = d;
    }

    gwy_data_line_cumulate(drift);
}

static void
apply_drift(GwyDataField *field, GwyDataLine *drift, GwyInterpolationType interp)
{
    gdouble *coeff, *data;
    gint xres, yres, i;
    gdouble corr;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    data = gwy_data_field_get_data(field);
    coeff = g_new(gdouble, xres);

    for (i = 0; i < yres; i++) {
        corr = gwy_data_field_rtoj(field, gwy_data_line_get_val(drift, i));
        gwy_assign(coeff, data + i*xres, xres);
        gwy_interpolation_shift_block_1d(xres, coeff, corr, data + i*xres,
                                         interp, GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);
    }

    g_free(coeff);
}

static void
execute(ModuleArgs *args)
{
    gint range = gwy_params_get_int(args->params, PARAM_RANGE);
    GwyInterpolationType interp = gwy_params_get_enum(args->params, PARAM_INTERP);
    gboolean exclude_linear = gwy_params_get_boolean(args->params, PARAM_EXCLUDE_LINEAR);
    GwyDataField *field = args->field, *result = args->result;
    GwyDataField *offsets, *scores;
    GwyDataLine *drift = args->drift;
    gint yres, maxoffset;

    yres = gwy_data_field_get_yres(field);
    gwy_data_field_copy(field, result, FALSE);

    offsets = gwy_data_field_new(2*range + 1, yres, 1.0, 1.0, FALSE);
    scores = gwy_data_field_new(2*range + 1, yres, 1.0, 1.0, FALSE);
    maxoffset = MAX(1, range/5);
    calculate_correlation_scores(field, range, maxoffset, 4.0, interp, scores, offsets);
    calculate_drift_very_naive(offsets, scores, drift);
    g_object_unref(offsets);
    g_object_unref(scores);

    if (exclude_linear) {
        gdouble a, b;

        gwy_data_line_get_line_coeffs(drift, &a, &b);
        gwy_data_line_line_level(drift, a, b);
    }
    gwy_data_line_add(drift, -gwy_data_line_get_median(drift));

    apply_drift(result, drift, interp);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
