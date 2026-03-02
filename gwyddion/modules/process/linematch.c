/*
 *  $Id: linematch.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/level.h>
#include <libprocess/correct.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* Lower symmetric part indexing; i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

enum {
    MAX_DEGREE = 5,
};

typedef enum {
    LINE_MATCH_POLY         = 0,
    LINE_MATCH_MEDIAN       = 1,
    LINE_MATCH_MEDIAN_DIFF  = 2,
    LINE_MATCH_MODUS        = 3,
    LINE_MATCH_MATCH        = 4,
    LINE_MATCH_TRIMMED_MEAN = 5,
    LINE_MATCH_TMEAN_DIFF   = 6,
    LINE_MATCH_FACET_TILT   = 7,
} LineMatchMethod;

enum {
    PARAM_METHOD,
    PARAM_MASKING,
    PARAM_DIRECTION,
    PARAM_MAX_DEGREE,
    PARAM_DO_EXTRACT,
    PARAM_DO_PLOT,
    PARAM_TRIM_FRACTION,
    PARAM_TARGET_GRAPH,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyDataField *bg;
    GwyDataLine *shifts;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyGraphModel *gmodel;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register          (void);
static GwyParamDef*     define_module_params     (void);
static void             linematch                (GwyContainer *data,
                                                  GwyRunType runtype);
static void             execute                  (ModuleArgs *args);
static GwyDialogOutcome run_gui                  (ModuleArgs *args,
                                                  GwyContainer *data,
                                                  gint id);
static void             param_changed            (ModuleGUI *gui,
                                                  gint id);
static void             preview                  (gpointer user_data);
static void             linematch_do_poly        (GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *means,
                                                  GwyMaskingType masking,
                                                  gint degree);
static void             linematch_do_trimmed_mean(GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *shifts,
                                                  GwyMaskingType masking,
                                                  gdouble trim_fraction);
static void             linematch_do_trimmed_diff(GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *shifts,
                                                  GwyMaskingType masking,
                                                  gdouble trim_fraction);
static void             linematch_do_modus       (GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *shifts,
                                                  GwyMaskingType masking);
static void             linematch_do_match       (GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *shifts,
                                                  GwyMaskingType masking);
static void             linematch_do_facet_tilt  (GwyDataField *field,
                                                  GwyDataField *mask,
                                                  GwyDataLine *shifts,
                                                  GwyMaskingType masking);
static void             zero_level_row_shifts    (GwyDataLine *shifts);

static const GwyEnum methods[] = {
    { N_("linematch|Polynomial"),        LINE_MATCH_POLY,         },
    { N_("Median"),                      LINE_MATCH_MEDIAN,       },
    { N_("Median of differences"),       LINE_MATCH_MEDIAN_DIFF,  },
    { N_("Modus"),                       LINE_MATCH_MODUS,        },
    { N_("linematch|Matching"),          LINE_MATCH_MATCH,        },
    { N_("Trimmed mean"),                LINE_MATCH_TRIMMED_MEAN, },
    { N_("Trimmed mean of differences"), LINE_MATCH_TMEAN_DIFF,   },
    { N_("Facet-level tilt"),            LINE_MATCH_FACET_TILT,   },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Aligns rows by various methods."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, linematch)

static gboolean
module_register(void)
{
    gwy_process_func_register("align_rows",
                              (GwyProcessFunc)&linematch,
                              N_("/_Correct Data/_Align Rows..."),
                              GWY_STOCK_LINE_LEVEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Align rows using various methods"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "linematch");
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Method"),
                              methods, G_N_ELEMENTS(methods), LINE_MATCH_MEDIAN);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_enum(paramdef, PARAM_DIRECTION, "direction", NULL, GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_HORIZONTAL);
    gwy_param_def_add_int(paramdef, PARAM_MAX_DEGREE, "max_degree", _("_Polynomial degree"), 0, MAX_DEGREE, 1);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_PLOT, "do_plot", _("Plot background _graph"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_TRIM_FRACTION, "trim_fraction", _("_Trim fraction"), 0.0, 0.5, 0.05);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
linematch(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field, *mask;
    GQuark quark;
    GwyParams *params;
    ModuleArgs args;
    gint id, newid;
    const gchar *methodname;
    gchar *title;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_MASK_FIELD, &mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(field && quark);

    args.field = field;
    args.mask = mask;
    args.bg = gwy_data_field_new_alike(field, FALSE);
    args.shifts = gwy_data_line_new(field->yres, field->yreal, FALSE);
    gwy_data_field_copy_units_to_data_line(field, args.shifts);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_HAVE_RESULT)
            goto end;
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        gwy_data_field_copy(args.result, field, FALSE);
    }
    else {
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        args.result = g_object_ref(field);
        execute(&args);
    }

    gwy_data_field_data_changed(field);
    gwy_app_channel_log_add(data, id, id, "proc::align_rows",
                            "settings-name", "linematch",
                            NULL);

    methodname = gwy_enum_to_string(gwy_params_get_enum(params, PARAM_METHOD), methods, G_N_ELEMENTS(methods));
    methodname = gwy_sgettext(methodname);
    title = g_strdup_printf("%s (%s)", _("Row background"), methodname);

    if (gwy_params_get_boolean(params, PARAM_DO_EXTRACT)) {
        newid = gwy_app_data_browser_add_data_field(args.bg, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
        gwy_app_set_data_field_title(data, newid, title);
        gwy_app_channel_log_add(data, id, newid, "proc::align_rows",
                                "settings-name", "linematch",
                                NULL);
    }

    if (gwy_params_get_boolean(params, PARAM_DO_PLOT)) {
        GwyGraphModel *gmodel = gwy_graph_model_new();
        GwyGraphCurveModel *gcmodel = gwy_graph_curve_model_new();
        GwyAppDataId target_graph_id = gwy_params_get_data_id(params, PARAM_TARGET_GRAPH);

        gwy_graph_curve_model_set_data_from_dataline(gcmodel, args.shifts, 0, 0);
        g_object_set(gcmodel,
                     "description", title,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(0),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        g_object_set(gmodel,
                     "title", _("Row background"),
                     "axis-label-bottom", _("Vertical position"),
                     "axis-label-left", _("Corrected offset"),
                     NULL);
        gwy_graph_model_set_units_from_data_line(gmodel, args.shifts);
        gwy_app_add_graph_or_curves(gmodel, data, &target_graph_id, 1);
        g_object_unref(gmodel);
    }

    g_free(title);

end:
    g_object_unref(params);
    GWY_OBJECT_UNREF(args.result);
    GWY_OBJECT_UNREF(args.shifts);
    GWY_OBJECT_UNREF(args.bg);
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
    /* Create an empty graph model just for compatibility check. */
    gui.gmodel = gwy_graph_model_new();
    gwy_graph_model_set_units_from_data_field(gui.gmodel, args->field, 1, 0, 0, 1);

    args->result = gwy_data_field_duplicate(args->field);

    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            0);

    gui.dialog = gwy_dialog_new(_("Align Rows"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio_header(table, PARAM_METHOD);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MEDIAN);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MEDIAN_DIFF);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MODUS);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MATCH);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_FACET_TILT);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_POLY);
    gwy_param_table_append_slider(table, PARAM_MAX_DEGREE);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_TRIMMED_MEAN);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_TMEAN_DIFF);
    gwy_param_table_append_slider(table, PARAM_TRIM_FRACTION);
    gwy_param_table_slider_set_steps(table, PARAM_TRIM_FRACTION, 0.01, 0.1);
    gwy_param_table_slider_set_factor(table, PARAM_TRIM_FRACTION, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_TRIM_FRACTION, "%");

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);
    gwy_param_table_append_checkbox(table, PARAM_DO_PLOT);

    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, gui.gmodel);

    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);

    return outcome;
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *mask = args->mask;
    GwyDataLine *shifts = args->shifts;
    GwyDataField *myfield, *mymask;
    LineMatchMethod method = gwy_params_get_enum(args->params, PARAM_METHOD);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    GwyOrientation direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    gdouble trim_fraction = gwy_params_get_double(args->params, PARAM_TRIM_FRACTION);
    gint max_degree = gwy_params_get_int(args->params, PARAM_MAX_DEGREE);

    gwy_data_field_copy(field, args->result, TRUE);
    gwy_data_field_copy(field, args->bg, TRUE);

    /* Transpose the fields if necessary. */
    mymask = mask;
    myfield = args->result;
    if (direction == GWY_ORIENTATION_VERTICAL) {
        myfield = gwy_data_field_new_alike(args->result, FALSE);
        gwy_data_field_flip_xy(args->result, myfield, FALSE);
        if (mask) {
            mymask = gwy_data_field_new_alike(mask, FALSE);
            gwy_data_field_flip_xy(mask, mymask, FALSE);
        }
    }

    gwy_data_line_resample(shifts, gwy_data_field_get_yres(myfield), GWY_INTERPOLATION_NONE);
    gwy_data_line_set_real(shifts, gwy_data_field_get_yreal(myfield));

    /* Perform the correction. */
    if (method == LINE_MATCH_POLY) {
        if (max_degree == 0)
            linematch_do_trimmed_mean(myfield, mymask, shifts, masking, 0.0);
        else
            linematch_do_poly(myfield, mymask, shifts, masking, max_degree);
    }
    else if (method == LINE_MATCH_MEDIAN)
        linematch_do_trimmed_mean(myfield, mymask, shifts, masking, 0.5);
    else if (method == LINE_MATCH_MEDIAN_DIFF)
        linematch_do_trimmed_diff(myfield, mymask, shifts, masking, 0.5);
    else if (method == LINE_MATCH_MODUS)
        linematch_do_modus(myfield, mymask, shifts, masking);
    else if (method == LINE_MATCH_MATCH)
        linematch_do_match(myfield, mymask, shifts, masking);
    else if (method == LINE_MATCH_FACET_TILT)
        linematch_do_facet_tilt(myfield, mymask, shifts, masking);
    else if (method == LINE_MATCH_TRIMMED_MEAN)
        linematch_do_trimmed_mean(myfield, mymask, shifts, masking, trim_fraction);
    else if (method == LINE_MATCH_TMEAN_DIFF)
        linematch_do_trimmed_diff(myfield, mymask, shifts, masking, trim_fraction);
    else {
        g_assert_not_reached();
    }

    /* Transpose back if necessary. */
    if (direction == GWY_ORIENTATION_VERTICAL) {
        GWY_OBJECT_UNREF(mymask);
        gwy_data_field_flip_xy(myfield, args->result, FALSE);
        g_object_unref(myfield);
    }
    gwy_data_field_subtract_fields(args->bg, args->bg, args->result);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_METHOD) {
        LineMatchMethod method = gwy_params_get_enum(params, PARAM_METHOD);
        gwy_param_table_set_sensitive(table, PARAM_MAX_DEGREE, method == LINE_MATCH_POLY);
        gwy_param_table_set_sensitive(table, PARAM_TRIM_FRACTION,
                                      method == LINE_MATCH_TRIMMED_MEAN || method == LINE_MATCH_TMEAN_DIFF);
    }
    if (id < 0 || id == PARAM_DO_PLOT) {
        gboolean do_plot = gwy_params_get_boolean(params, PARAM_DO_PLOT);
        gwy_param_table_set_sensitive(table, PARAM_TARGET_GRAPH, do_plot);
    }
    if (id != PARAM_DO_PLOT && id != PARAM_DO_EXTRACT && id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
linematch_do_poly(GwyDataField *field, GwyDataField *mask, GwyDataLine *means,
                  GwyMaskingType masking, gint degree)
{
    gint xres, yres;
    gdouble xc, avg;
    const gdouble *m;
    gdouble *d;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    xc = 0.5*(xres - 1);
    avg = gwy_data_field_get_avg(field);
    d = gwy_data_field_get_data(field);

    m = mask ? gwy_data_field_get_data_const(mask) : NULL;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,xres,yres,masking,avg,degree,xc,means)
#endif
    {
        gdouble *xpowers = g_new(gdouble, 2*degree+1);
        gdouble *zxpowers = g_new(gdouble, degree+1);
        gdouble *matrix = g_new(gdouble, (degree+1)*(degree+2)/2);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j, k;

        for (i = ifrom; i < ito; i++) {
            const gdouble *mrow = m ? m + i*xres : NULL;
            gdouble *drow = d + i*xres;

            gwy_clear(xpowers, 2*degree+1);
            gwy_clear(zxpowers, degree+1);

            for (j = 0; j < xres; j++) {
                gdouble p = 1.0, x = j - xc;

                if ((masking == GWY_MASK_INCLUDE && mrow[j] <= 0.0)
                    || (masking == GWY_MASK_EXCLUDE && mrow[j] >= 1.0))
                    continue;

                for (k = 0; k <= degree; k++) {
                    xpowers[k] += p;
                    zxpowers[k] += p*drow[j];
                    p *= x;
                }
                for (k = degree+1; k <= 2*degree; k++) {
                    xpowers[k] += p;
                    p *= x;
                }
            }

            /* Solve polynomial coefficients. */
            if (xpowers[0] > degree) {
                for (j = 0; j <= degree; j++) {
                    for (k = 0; k <= j; k++)
                        SLi(matrix, j, k) = xpowers[j + k];
                }
                gwy_math_choleski_decompose(degree+1, matrix);
                gwy_math_choleski_solve(degree+1, matrix, zxpowers);
            }
            else
                gwy_clear(zxpowers, degree+1);

            /* Subtract. */
            zxpowers[0] -= avg;
            gwy_data_line_set_val(means, i, zxpowers[0]);
            for (j = 0; j < xres; j++) {
                gdouble p = 1.0, x = j - xc, z = 0.0;

                for (k = 0; k <= degree; k++) {
                    z += p*zxpowers[k];
                    p *= x;
                }

                drow[j] -= z;
            }
        }

        g_free(matrix);
        g_free(zxpowers);
        g_free(xpowers);
    }
}

static void
linematch_do_trimmed_mean(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                          GwyMaskingType masking, gdouble trimfrac)
{
    GwyDataLine *myshifts;

    myshifts = gwy_data_field_find_row_shifts_trimmed_mean(field, mask, masking, trimfrac, 0);
    gwy_data_field_subtract_row_shifts(field, myshifts);
    gwy_data_line_assign(shifts, myshifts);
    g_object_unref(myshifts);
}

static void
linematch_do_trimmed_diff(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                          GwyMaskingType masking, gdouble trimfrac)
{
    GwyDataLine *myshifts;

    myshifts = gwy_data_field_find_row_shifts_trimmed_diff(field, mask, masking, trimfrac, 0);
    gwy_data_field_subtract_row_shifts(field, myshifts);
    gwy_data_line_assign(shifts, myshifts);
    g_object_unref(myshifts);
}

static void
linematch_do_modus(GwyDataField *field, GwyDataField *mask, GwyDataLine *modi,
                   GwyMaskingType masking)
{
    gint xres, yres;
    const gdouble *d, *m;
    gdouble total_median;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    total_median = gwy_data_field_area_get_median_mask(field, mask, masking, 0, 0, xres, yres);

    d = gwy_data_field_get_data(field);
    m = mask ? gwy_data_field_get_data_const(mask) : NULL;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,modi,xres,yres,masking,total_median)
#endif
    {
        GwyDataLine *line = gwy_data_line_new(xres, 1.0, FALSE);
        gdouble *buf = gwy_data_line_get_data(line);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i;

        for (i = ifrom; i < ito; i++) {
            const gdouble *row = d + i*xres;
            const gdouble *mrow = m ? m + i*xres : NULL;
            gdouble modus;
            gint count = 0, j;

            for (j = 0; j < xres; j++) {
                if ((masking == GWY_MASK_INCLUDE && mrow[j] <= 0.0)
                    || (masking == GWY_MASK_EXCLUDE && mrow[j] >= 1.0))
                    continue;

                buf[count++] = row[j];
            }

            if (!count)
                modus = total_median;
            else if (count < 9)
                modus = gwy_math_median(count, buf);
            else {
                gint seglen = GWY_ROUND(sqrt(count)), bestj = 0;
                gdouble diff, bestdiff = G_MAXDOUBLE;

                gwy_math_sort(count, buf);
                for (j = 0; j + seglen-1 < count; j++) {
                    diff = buf[j + seglen-1] - buf[j];
                    if (diff < bestdiff) {
                        bestdiff = diff;
                        bestj = j;
                    }
                }
                modus = 0.0;
                count = 0;
                for (j = seglen/3; j < seglen - seglen/3; j++, count++)
                    modus += buf[bestj + j];
                modus /= count;
            }

            gwy_data_line_set_val(modi, i, modus);
        }

        g_object_unref(line);
    }

    zero_level_row_shifts(modi);
    gwy_data_field_subtract_row_shifts(field, modi);
}

static void
linematch_do_match(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                   GwyMaskingType masking)
{
    gint xres, yres, k;
    const gdouble *d, *m;
    gdouble *s;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data(field);
    m = mask ? gwy_data_field_get_data_const(mask) : NULL;
    s = gwy_data_line_get_data(shifts);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,s,xres,yres,masking)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres-1) + 1;
        gint ito = gwy_omp_chunk_end(yres-1) + 1;
        gdouble *w = g_new(gdouble, xres-1);
        const gdouble *a, *b, *ma, *mb;
        gdouble q, wsum, lambda, x;
        gint i, j;

        for (i = ifrom; i < ito; i++) {
            a = d + xres*(i - 1);
            b = d + xres*i;
            ma = m + xres*(i - 1);
            mb = m + xres*i;

            /* Diffnorm */
            wsum = 0.0;
            for (j = 0; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0)))
                    continue;

                x = a[j+1] - a[j] - b[j+1] + b[j];
                wsum += fabs(x);
            }
            if (wsum == 0) {
                s[i] = 0.0;
                continue;
            }
            q = wsum/(xres-1);

            /* Weights */
            wsum = 0.0;
            for (j = 0; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0))) {
                    w[j] = 0.0;
                    continue;
                }

                x = a[j+1] - a[j] - b[j+1] + b[j];
                w[j] = exp(-(x*x/(2.0*q)));
                wsum += w[j];
            }

            /* Correction */
            lambda = (a[0] - b[0])*w[0];
            for (j = 1; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0)))
                    continue;

                lambda += (a[j] - b[j])*(w[j-1] + w[j]);
            }
            lambda += (a[xres-1] - b[xres-1])*w[xres-2];
            lambda /= 2.0*wsum;

            gwy_debug("%g %g %g", q, wsum, lambda);

            s[i] = -lambda;
        }

        g_free(w);
    }

    s[0] = 0.0;
    for (k = 1; k < yres; k++)
        s[k] += s[k-1];

    zero_level_row_shifts(shifts);
    gwy_data_field_subtract_row_shifts(field, shifts);
}

static gdouble
row_fit_facet_tilt(const gdouble *drow,
                   const gdouble *mrow,
                   GwyMaskingType masking,
                   guint res, gdouble dx,
                   guint mincount)
{
    const gdouble c = 1.0/200.0;

    gdouble vx, q, sumvx, sumvz, sigma2;
    guint i, n;

    sigma2 = 0.0;
    n = 0;
    if (mrow && masking == GWY_MASK_INCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] >= 1.0 && mrow[i+1] >= 1.0) {
                vx = (drow[i+1] - drow[i])/dx;
                sigma2 += vx*vx;
                n++;
            }
        }
    }
    else if (mrow && masking == GWY_MASK_EXCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] <= 0.0 && mrow[i+1] <= 0.0) {
                vx = (drow[i+1] - drow[i])/dx;
                sigma2 += vx*vx;
                n++;
            }
        }
    }
    else {
        for (i = 0; i < res-1; i++) {
            vx = (drow[i+1] - drow[i])/dx;
            sigma2 += vx*vx;
        }
        n = res-1;
    }
    /* Do not try to level from some random pixel */
    gwy_debug("n=%d", n);
    if (n < mincount)
        return 0.0;

    sigma2 = c*sigma2/n;

    sumvx = sumvz = 0.0;
    if (mrow && masking == GWY_MASK_INCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] >= 1.0 && mrow[i+1] >= 1.0) {
                vx = (drow[i+1] - drow[i])/dx;
                q = exp(vx*vx/sigma2);
                sumvx += vx/q;
                sumvz += 1.0/q;
            }
        }
    }
    else if (mrow && masking == GWY_MASK_EXCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] <= 0.0 && mrow[i+1] <= 0.0) {
                vx = (drow[i+1] - drow[i])/dx;
                q = exp(vx*vx/sigma2);
                sumvx += vx/q;
                sumvz += 1.0/q;
            }
        }
    }
    else {
        for (i = 0; i < res-1; i++) {
            vx = (drow[i+1] - drow[i])/dx;
            q = exp(vx*vx/sigma2);
            sumvx += vx/q;
            sumvz += 1.0/q;
        }
    }

    return sumvx/sumvz * dx;
}

static void
untilt_row(gdouble *drow, guint res, gdouble bx)
{
    gdouble x;
    guint i;

    if (!bx)
        return;

    for (i = 0; i < res; i++) {
        x = i - 0.5*(res - 1);
        drow[i] -= bx*x;
    }
}

static void
linematch_do_facet_tilt(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                        GwyMaskingType masking)
{
    guint xres, yres, i, j, mincount;
    const gdouble *mdata, *mrow;
    gdouble *data, *drow;
    gdouble dx, tilt;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    dx = gwy_data_field_get_dx(field);
    mincount = GWY_ROUND(log(xres) + 1);

    data = gwy_data_field_get_data(field);
    mdata = mask ? gwy_data_field_get_data_const(mask) : NULL;
    for (i = 0; i < yres; i++) {
        drow = data + i*xres;
        mrow = mdata ? mdata + i*xres : NULL;
        for (j = 0; j < 30; j++) {
            tilt = row_fit_facet_tilt(drow, mrow, masking, xres, dx, mincount);
            untilt_row(drow, xres, tilt);
            if (fabs(tilt/dx) < 1e-6)
                break;
        }
    }

    /* FIXME: Should we put the tilts there to confuse the user?  We need to
     * make sure all functions set the units correctly in such case. */
    gwy_data_line_clear(shifts);
}

static void
zero_level_row_shifts(GwyDataLine *shifts)
{
    gwy_data_line_add(shifts, -gwy_data_line_get_avg(shifts));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
