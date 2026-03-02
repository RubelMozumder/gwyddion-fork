/*
 *  $Id: curvature.c 25076 2022-10-11 13:48:10Z yeti-dn $
 *  Copyright (C) 2009-2022 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.net.
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyresults.h>
#include <libprocess/level.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    RESULT_X0,
    RESULT_Y0,
    RESULT_A,
    RESULT_R1,
    RESULT_R2,
    RESULT_PHI1,
    RESULT_PHI2,
    RESULT_NRESULTS
} CurvatureParamType;

enum {
    PARAM_MASKING,
    PARAM_SET_SELECTION,
    PARAM_PLOT_GRAPH,
    PARAM_TARGET_GRAPH,
    PARAM_REPORT_STYLE,
    WIDGET_RESULTS,
    LABEL_WARNING,
};

typedef struct {
    gdouble d, t, x, y;
} Intersection;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyGraphModel *gmodel;
    GwySelection *selection;
    gdouble results[RESULT_NRESULTS];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_main;
    GwyParamTable *table_results;
    GwyContainer *data;
    GwyResults *results;
    GwySelection *selection;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             curvature           (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static GwyResults*      create_results      (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates overall curvature."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, curvature)

static gboolean
module_register(void)
{
    gwy_process_func_register("curvature",
                              (GwyProcessFunc)&curvature,
                              N_("/Measure _Features/_Curvature..."),
                              GWY_STOCK_CURVATURE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calculate overall curvature"));

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
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_boolean(paramdef, PARAM_SET_SELECTION, "set_selection", _("_Set selection"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_PLOT_GRAPH, "plot_graph", _("_Plot graph"), FALSE);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", NULL,
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
curvature(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyParams *params;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerLine"));
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    if (!gwy_require_image_same_units(args.field, data, id, _("Curvature")))
        return;

    args.gmodel = gwy_graph_model_new();
    g_object_set(args.gmodel, "title", _("Curvature Sections"), NULL);
    gwy_graph_model_set_units_from_data_field(args.gmodel, args.field, 1, 0, 0, 1);
    args.selection = g_object_new(g_type_from_name("GwySelectionLine"), "max-objects", 1024, NULL);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    if (gwy_params_get_boolean(params, PARAM_SET_SELECTION)) {
        gchar *key = g_strdup_printf("/%d/select/line", id);
        gwy_container_set_object_by_name(data, key, args.selection);
        g_free(key);
        gwy_app_channel_log_add_proc(data, id, id);
    }
    if (gwy_params_get_boolean(params, PARAM_PLOT_GRAPH)) {
        GwyAppDataId target_graph_id = gwy_params_get_data_id(params, PARAM_TARGET_GRAPH);
        gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.selection);
    g_object_unref(args.gmodel);
}

static gboolean
intersect_with_boundary(gdouble x_0, gdouble y_0, gdouble phi, gdouble w, gdouble h,
                        Intersection *i1, Intersection *i2)
{
    enum { NISEC = 4 };
    Intersection isec[NISEC];
    gdouble diag;
    guint i;

    /* With x = 0 */
    isec[0].t = -x_0/cos(phi);
    isec[0].x = 0.0;
    isec[0].y = y_0 - x_0*tan(phi);

    /* With x = w */
    isec[1].t = (w - x_0)/cos(phi);
    isec[1].x = w;
    isec[1].y = y_0 + (w - x_0)*tan(phi);

    /* With y = 0 */
    isec[2].t = -y_0/sin(phi);
    isec[2].x = x_0 - y_0/tan(phi);
    isec[2].y = 0.0;

    /* With y = h */
    isec[3].t = (h - y_0)/sin(phi);
    isec[3].x = x_0 + (h - y_0)/tan(phi);
    isec[3].y = h;

    /* Distance from centre must be at most half the diagonal. */
    diag = 0.5*hypot(w, h);
    for (i = 0; i < NISEC; i++) {
        isec[i].d = hypot(isec[i].x - 0.5*w, isec[i].y - 0.5*h)/diag;
        gwy_debug("isec[%u]: %g", i, isec[i].d);
    }

    qsort(isec, NISEC, sizeof(Intersection), gwy_compare_double);

    for (i = 0; i < NISEC; i++) {
        if (isec[i].d > 1.0)
            break;
    }

    gwy_debug("intersections: %u", i);
    if (i == 1)
        i = 0;
    else if (i == 3)
        i = 2;
    else if (i == 4) {
        i = 2;
        /* Pick the right two intersections if it goes through two opposite
         * corners. */
        if (fabs(isec[0].t - isec[1].t) < fabs(isec[0].t - isec[2].t))
            isec[1] = isec[2];
    }

    if (i) {
        if (isec[0].t <= isec[1].t) {
            *i1 = isec[0];
            *i2 = isec[1];
        }
        else {
            *i1 = isec[1];
            *i2 = isec[0];
        }
        return TRUE;
    }
    return FALSE;
}

/* Does not include x and y offsets of the data field */
static gboolean
curvature_calculate(GwyDataField *field, GwyDataField *mask, GwyMaskingType masking,
                    gdouble *r, Intersection *i1, Intersection *i2)
{
    enum { DEGREE = 2 };
    enum { A, BX, CXX, BY, CXY, CYY, NTERMS };
    gint term_powers[2*NTERMS];
    gdouble coeffs[NTERMS], ccoeffs[NTERMS];
    gdouble xreal, yreal, qx, qy, q, mx, my;
    gint xres, yres, i, j, k;
    gboolean ok;

    k = 0;
    g_assert(NTERMS == (DEGREE + 1)*(DEGREE + 2)/2);
    for (i = 0; i <= DEGREE; i++) {
        for (j = 0; j <= DEGREE - i; j++) {
            term_powers[k++] = j;
            term_powers[k++] = i;
        }
    }

    gwy_data_field_fit_poly(field, mask, NTERMS, term_powers, masking != GWY_MASK_INCLUDE, coeffs);
    gwy_debug("NORM a=%g, bx=%g, by=%g, cxx=%g, cxy=%g, cyy=%g",
              coeffs[A], coeffs[BX], coeffs[BY], coeffs[CXX], coeffs[CXY], coeffs[CYY]);

    /* Transform coeffs from normalized coordinates to coordinates that are
     * still numerically around 1 but have the right aspect ratio. */
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    xreal = gwy_data_field_get_xreal(field);
    yreal = gwy_data_field_get_yreal(field);
    qx = 2.0/xreal*xres/(xres - 1.0);
    qy = 2.0/yreal*yres/(yres - 1.0);
    q = sqrt(qx*qy);
    mx = sqrt(qx/qy);
    my = sqrt(qy/qx);

    ccoeffs[0] = coeffs[A];
    ccoeffs[1] = mx*coeffs[BX];
    ccoeffs[2] = my*coeffs[BY];
    ccoeffs[3] = mx*mx*coeffs[CXX];
    ccoeffs[4] = coeffs[CXY];
    ccoeffs[5] = my*my*coeffs[CYY];
    gwy_math_curvature_at_apex(ccoeffs,
                               r + RESULT_R1, r + RESULT_R2, r + RESULT_PHI1, r + RESULT_PHI2,
                               r + RESULT_X0, r + RESULT_Y0, r + RESULT_A);
    /* Transform to physical values. */
    /* FIXME: Why we have q*q here? */
    r[RESULT_R1] = 1.0/(q*q*r[RESULT_R1]);
    r[RESULT_R2] = 1.0/(q*q*r[RESULT_R2]);
    r[RESULT_X0] = r[RESULT_X0]/q + 0.5*xreal;
    r[RESULT_Y0] = r[RESULT_Y0]/q + 0.5*yreal;

    ok = TRUE;
    for (i = 0; i < 2; i++)
        ok &= intersect_with_boundary(r[RESULT_X0], r[RESULT_Y0], -r[RESULT_PHI1 + i], xreal, yreal, i1 + i, i2 + i);

    r[RESULT_X0] += gwy_data_field_get_xoffset(field);
    r[RESULT_Y0] += gwy_data_field_get_yoffset(field);

    return ok;
}

static gboolean
curvature_set_selection(GwyDataField *field,
                        const Intersection *i1, const Intersection *i2,
                        GwySelection *selection)
{
    gdouble xreal, yreal;
    gdouble xy[4];
    gint xres, yres;
    guint i;

    xreal = gwy_data_field_get_xreal(field);
    yreal = gwy_data_field_get_yreal(field);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    for (i = 0; i < 2; i++) {
        xy[0] = CLAMP(i1[i].x, 0, xreal*(xres - 1)/xres);
        xy[1] = CLAMP(i1[i].y, 0, yreal*(yres - 1)/yres);
        xy[2] = CLAMP(i2[i].x, 0, xreal*(xres - 1)/xres);
        xy[3] = CLAMP(i2[i].y, 0, yreal*(yres - 1)/yres);
        gwy_selection_set_object(selection, i, xy);
    }

    return TRUE;
}

static gboolean
curvature_plot_graph(GwyDataField *field,
                     const Intersection *i1, const Intersection *i2,
                     GwyGraphModel *gmodel)
{
    GwyGraphCurveModel *gcmodel;
    GwyDataLine *dline;
    gint xres, yres;
    gchar *s;
    guint i;

    if (gwy_graph_model_get_n_curves(gmodel) != 2) {
        gwy_graph_model_remove_all_curves(gmodel);
        for (i = 0; i < 2; i++) {
            gcmodel = gwy_graph_curve_model_new();
            s = g_strdup_printf(_("Profile %d"), (gint)i+1);
            g_object_set(gcmodel,
                         "description", s,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "color", gwy_graph_get_preset_color(i),
                         NULL);
            g_free(s);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }
    }

    dline = gwy_data_line_new(1, 1.0, FALSE);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    for (i = 0; i < 2; i++) {
        gint col1 = gwy_data_field_rtoj(field, i1[i].x);
        gint row1 = gwy_data_field_rtoi(field, i1[i].y);
        gint col2 = gwy_data_field_rtoj(field, i2[i].x);
        gint row2 = gwy_data_field_rtoi(field, i2[i].y);

        /* FIXME: We should use gwy_data_field_get_profile_mask() here. */
        gwy_data_field_get_profile(field, dline,
                                   CLAMP(col1, 0, xres-1), CLAMP(row1, 0, yres-1),
                                   CLAMP(col2, 0, xres-1), CLAMP(row2, 0, yres-1),
                                   -1, 1, GWY_INTERPOLATION_BILINEAR);
        gwy_data_line_set_offset(dline, i1[i].t/(i2[i].t - i1[i].t)*gwy_data_line_get_real(dline));
        gcmodel = gwy_graph_model_get_curve(gmodel, i);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
    }
    g_object_unref(dline);

    return TRUE;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox, *vbox, *dataview, *graph;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args, data, id);

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    if (args->mask)
        gwy_container_set_object_by_name(gui.data, "/0/mask", args->mask);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Curvature"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Line", 2, FALSE);
    g_object_ref(gui.selection);
    gwy_selection_assign(gui.selection, args->selection);
    g_object_set(gui.selection, "max-objects", 2, NULL);
    gtk_box_pack_start(GTK_BOX(vbox), dataview, FALSE, FALSE, 0);

    table = gui.table_main = gwy_param_table_new(args->params);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);
    /* XXX: Preserve settings scheme.  Otherwise we would use a flag set. */
    gwy_param_table_append_message(table, -1, _("Output type:"));
    gwy_param_table_append_checkbox(table, PARAM_SET_SELECTION);
    gwy_param_table_append_checkbox(table, PARAM_PLOT_GRAPH);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gwy_param_table_append_message(table, LABEL_WARNING, NULL);
    gwy_param_table_message_set_type(table, LABEL_WARNING, GTK_MESSAGE_ERROR);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    vbox = gwy_vbox_new(4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    graph = gwy_graph_new(args->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, 320, 260);
    gtk_box_pack_start(GTK_BOX(vbox), graph, TRUE, TRUE, 0);

    table = gui.table_results = gwy_param_table_new(args->params);
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui.results,
                                   "x0", "y0", "z0", "r1", "r2", "phi1", "phi2", NULL);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_REPORT_STYLE, gui.results);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table_main, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);
    g_object_unref(gui.selection);
    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table_main;

    if (id < 0 || id == PARAM_PLOT_GRAPH) {
        gboolean plot_graph = gwy_params_get_boolean(params, PARAM_PLOT_GRAPH);
        gwy_param_table_set_sensitive(table, PARAM_TARGET_GRAPH, plot_graph);
    }
    if (id < 0 || id == PARAM_MASKING)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static GwyResults*
create_results(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Curvature"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_value_yesno(results, "masking", N_("Mask in use"));
    gwy_results_add_separator(results);

    gwy_results_add_value(results, "x0", N_("Center x position"), "power-x", 1, "symbol", "x<sub>0</sub>", NULL);
    gwy_results_add_value(results, "y0", N_("Center y position"), "power-y", 1, "symbol", "y<sub>0</sub>", NULL);
    gwy_results_add_value(results, "z0", N_("Center value"), "power-z", 1, "symbol", "z<sub>0</sub>", NULL);
    /* The units must be all the same anyway... */
    gwy_results_add_value(results, "r1", N_("Curvature radius 1"), "power-x", 1, "symbol", "r<sub>1</sub>", NULL);
    gwy_results_add_value(results, "r2", N_("Curvature radius 2"), "power-x", 1, "symbol", "r<sub>2</sub>", NULL);
    gwy_results_add_value(results, "phi1", N_("Direction 1"), "is-angle", TRUE, "symbol", "φ<sub>1</sub>", NULL);
    gwy_results_add_value(results, "phi2", N_("Direction 2"), "is-angle", TRUE, "symbol", "φ<sub>2</sub>", NULL);

    gwy_results_bind_formats(results, "x0", "y0", NULL);
    gwy_results_bind_formats(results, "r1", "r2", NULL);

    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(args->field));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(args->field));

    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static gboolean
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    Intersection i1[2], i2[2];

    if (curvature_calculate(field, mask, masking, args->results, i1, i2)) {
        curvature_set_selection(field, i1, i2, args->selection);
        curvature_plot_graph(field, i1, i2, args->gmodel);
        return TRUE;
    }

    gwy_selection_clear(args->selection);
    gwy_graph_model_remove_all_curves(args->gmodel);
    return FALSE;
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    GwyResults *results = gui->results;
    gdouble *r = args->results;

    if (execute(args)) {
        gwy_selection_assign(gui->selection, args->selection);
        gwy_results_fill_values(results, "masking", masking != GWY_MASK_IGNORE, NULL);
        gwy_results_fill_values(results,
                                "x0", r[RESULT_X0], "y0", r[RESULT_Y0], "z0", r[RESULT_A],
                                "r1", r[RESULT_R1], "r2", r[RESULT_R2],
                                "phi1", r[RESULT_PHI1], "phi2", r[RESULT_PHI2],
                                NULL);
        gwy_param_table_results_fill(gui->table_results, WIDGET_RESULTS);
        gwy_param_table_set_label(gui->table_main, LABEL_WARNING, "");
        gwy_param_table_set_sensitive(gui->table_results, PARAM_REPORT_STYLE, TRUE);
    }
    else {
        gwy_param_table_set_label(gui->table_main, LABEL_WARNING, _("Axes are outside the image."));
        gwy_param_table_results_clear(gui->table_results, WIDGET_RESULTS);
        gwy_param_table_set_sensitive(gui->table_results, PARAM_REPORT_STYLE, FALSE);
        gwy_selection_clear(gui->selection);
    }
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
