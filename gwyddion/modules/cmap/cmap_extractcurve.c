/*
 *  $Id: cmap_extractcurve.c 24333 2021-10-11 16:28:01Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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
#include <libprocess/lawn.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    COLUMN_I, COLUMN_X, COLUMN_Y, NCOLUMNS
};

enum {
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_ENABLE_ABSCISSA,
    PARAM_SEGMENT,
    PARAM_ENABLE_SEGMENT,
    PARAM_SORT,
    PARAM_MULTISELECT,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_TARGET_GRAPH,
};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyGraphModel *result;
    /* Cached input data properties. */
    gint nsegments;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GwySelection *selection;
    GtkWidget *coordlist;
    gint current_point;
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             extract_curve              (GwyContainer *data,
                                                    GwyRunType runtype);
static void             execute                    (ModuleArgs *args,
                                                    GwySelection *selection);
static GwyDialogOutcome run_gui                    (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static GtkWidget*       create_coordlist           (ModuleGUI *gui);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static void             coordlist_selection_changed(GtkTreeSelection *selection,
                                                    ModuleGUI *gui);
static void             preview                    (gpointer user_data);
static void             set_selection              (ModuleGUI *gui);
static void             point_selection_changed    (ModuleGUI *gui,
                                                    gint id,
                                                    GwySelection *selection);
static void             extract_one_curve          (GwyLawn *lawn,
                                                    GwyGraphCurveModel *gcmodel,
                                                    gint col,
                                                    gint row,
                                                    gint segment,
                                                    GwyParams *params);
static void             update_graph_model_props   (ModuleArgs *args);
static void             sanitise_params            (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extracts individual curves from a curve map."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_extractcurve)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_extractcurve",
                                (GwyCurveMapFunc)&extract_curve,
                                N_("/_Extract Curves..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Extract curves"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Abscissa"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Ordinate"));
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_ABSCISSA, "enable_abscissa", NULL, FALSE);
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT, "segment", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_SEGMENT, "enable_segment", NULL, FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SORT, "sort", _("Reorder by abscissa"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_MULTISELECT, "multiselect", _("Extract _multiple"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
extract_curve(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyAppDataId target_graph_id;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.result = gwy_graph_model_new();
    sanitise_params(&args);
    update_graph_model_props(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args, NULL);

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.result, data, &target_graph_id, 1);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align, *coords;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.current_point = 0;
    gui.data = gwy_container_new();
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Extract Map Curves"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_CLEAR, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    vlayer = g_object_new(g_type_from_name("GwyLayerPoint"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(dataview), vlayer);
    gui.selection = gwy_vector_layer_ensure_selection(vlayer);
    set_selection(&gui);

    g_object_set(args->result, "label-visible", FALSE, NULL);

    graph = gwy_graph_new(args->result);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_add_enabler(table, PARAM_ENABLE_ABSCISSA, PARAM_ABSCISSA);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT, args->lawn);
        gwy_param_table_add_enabler(table, PARAM_ENABLE_SEGMENT, PARAM_SEGMENT);
    }
    gwy_param_table_append_checkbox(table, PARAM_SORT);
    gwy_param_table_append_checkbox(table, PARAM_MULTISELECT);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->result);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    coords = create_coordlist(&gui);
    gtk_box_pack_start(GTK_BOX(hbox), coords, FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_set(args->result, "label-visible", TRUE, NULL);
    g_object_unref(gui.data);

    return outcome;
}

static void
render_coord_cell(GtkCellLayout *layout,
                  GtkCellRenderer *renderer,
                  GtkTreeModel *model,
                  GtkTreeIter *iter,
                  gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwyLawn *lawn = gui->args->lawn;
    gchar buf[32];
    gdouble xy[2];
    guint idx, id, i;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (idx >= gwy_selection_get_data(gui->selection, NULL))
        return;

    if (id == COLUMN_I)
        i = idx+1;
    else {
        gwy_selection_get_object(gui->selection, idx, xy);
        if (id == COLUMN_X)
            i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
        else
            i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    }
    g_snprintf(buf, sizeof(buf), "%d", i);
    g_object_set(renderer, "text", buf, NULL);
}

static GtkWidget*
create_coordlist(ModuleGUI *gui)
{
    static const gchar *titles[NCOLUMNS] = { "n", "x", "y" };
    GtkTreeModel *model;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;
    GtkWidget *label, *scwin;
    GString *str;
    guint i;

    model = GTK_TREE_MODEL(gwy_null_store_new(1));
    gui->coordlist = gtk_tree_view_new_with_model(model);
    g_object_unref(model);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), gui->coordlist);

    str = g_string_new(NULL);
    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, render_coord_cell, gui, NULL);
        label = gtk_label_new(NULL);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(GTK_TREE_VIEW(gui->coordlist), column);

        label = gtk_tree_view_column_get_widget(column);
        g_string_assign(str, "<b>");
        g_string_append(str, titles[i]);
        g_string_append(str, "</b>");
        gtk_label_set_markup(GTK_LABEL(label), str->str);
    }
    g_string_free(str, TRUE);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->coordlist));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect(selection, "changed", G_CALLBACK(coordlist_selection_changed), gui);

    return scwin;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_MULTISELECT) {
        gboolean is_multiselect = gwy_params_get_boolean(params, PARAM_MULTISELECT);
        gwy_selection_set_max_objects(gui->selection, 1 + 1024*!!is_multiselect);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GWY_RESPONSE_CLEAR, is_multiselect);
    }
    if (id < 0 || id == PARAM_ENABLE_ABSCISSA) {
        gboolean abscissa_enabled = gwy_params_get_boolean(params, PARAM_ENABLE_ABSCISSA);
        gwy_param_table_set_sensitive(gui->table, PARAM_SORT, abscissa_enabled);
    }
    if (id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_CLEAR) {
        gdouble xy[2];
        gwy_selection_get_object(gui->selection, gui->current_point, xy);
        gwy_selection_set_data(gui->selection, 1, xy);
    }
}

static void
set_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = (col + 0.5)*gwy_lawn_get_dx(args->lawn);
    xy[1] = (row + 0.5)*gwy_lawn_get_dy(args->lawn);
    gwy_selection_set_object(gui->selection, 0, xy);
}

static void
point_selection_changed(ModuleGUI *gui, gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GtkTreeModel *model;
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    /* This occurs only upon clear. */
    if (id < 0) {
        gwy_graph_model_remove_all_curves(gui->args->result);
        id = 0;
    }
    gui->current_point = id;

    gwy_selection_get_object(selection, id, xy);
    i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
    i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));

    gwy_param_table_param_changed(gui->table, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table, PARAM_YPOS);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->coordlist));
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), gwy_selection_get_data(selection, NULL));
    gwy_null_store_row_changed(GWY_NULL_STORE(model), id);
}

static void
coordlist_selection_changed(GtkTreeSelection *selection, ModuleGUI *gui)
{
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        path = gtk_tree_model_get_path(model, &iter);
        gui->current_point = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args, gui->selection);
    gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args, GwySelection *selection)
{
    GwyParams *params = args->params;
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    gboolean multiselect = gwy_params_get_boolean(params, PARAM_MULTISELECT);
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    GwyLawn *lawn = args->lawn;
    GwyGraphModel *gmodel = args->result;
    GwyGraphCurveModel *gcmodel;
    gint *points;
    gint npoints, i, j, ncurves;
    gdouble xy[2];

    if (multiselect && selection && (npoints = gwy_selection_get_data(selection, NULL))) {
        points = g_new(gint, 2*npoints);
        for (j = 0; j < npoints; j++) {
            gwy_selection_get_object(selection, j, xy);
            points[2*j + 0] = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
            points[2*j + 1] = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
        }
    }
    else {
        npoints = 1;
        points = g_new(gint, 2*npoints);
        points[0] = col;
        points[1] = row;
    }

    ncurves = gwy_graph_model_get_n_curves(gmodel);
    for (i = j = 0; j < npoints; j++) {
        /* TODO: Show the entire curve, but with coloured segments (in single selection mode). */
        if (i < ncurves)
            gcmodel = gwy_graph_model_get_curve(gmodel, i);
        else {
            gcmodel = gwy_graph_curve_model_new();
            g_object_set(gcmodel,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "color", gwy_graph_get_preset_color(i),
                         NULL);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }
        extract_one_curve(lawn, gcmodel, points[2*j], points[2*j+1], segment, params);
        i++;
    }
    g_free(points);
    while (i < ncurves) {
        gwy_graph_model_remove_curve(gmodel, i);
        ncurves--;
    }

    update_graph_model_props(args);
}

static void
extract_one_curve(GwyLawn *lawn, GwyGraphCurveModel *gcmodel,
                  gint col, gint row, gint segment,
                  GwyParams *params)
{
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    gboolean abscissa_enabled = gwy_params_get_boolean(params, PARAM_ENABLE_ABSCISSA);
    gboolean force_order = gwy_params_get_boolean(params, PARAM_SORT);
    const gdouble *xdata, *ydata;
    gdouble *samplenodata = NULL;
    gint ndata, i, from, end;
    const gint *segments;
    gchar *s;

    s = g_strdup_printf("x: %d, y: %d", col, row);
    g_object_set(gcmodel, "description", s, NULL);
    g_free(s);

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    if (abscissa_enabled)
        xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);
    else {
        samplenodata = g_new(gdouble, ndata);
        for (i = 0; i < ndata; i++)
            samplenodata[i] = i;
        xdata = samplenodata;
    }

    if (segment >= 0) {
        segments = gwy_lawn_get_segments(lawn, col, row, NULL);
        from = segments[2*segment];
        end = segments[2*segment + 1];
        xdata += from;
        ydata += from;
        ndata = end - from;
    }
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
    g_free(samplenodata);

    if (force_order)
        gwy_graph_curve_model_enforce_order(gcmodel);
}

static void
update_graph_model_props(ModuleArgs *args)
{
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    GwyGraphModel *gmodel = args->result;
    gboolean abscissa_enabled = gwy_params_get_boolean(params, PARAM_ENABLE_ABSCISSA);
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwySIUnit *xunit, *yunit;
    const gchar *xlabel, *ylabel;

    if (abscissa_enabled) {
        xunit = g_object_ref(gwy_lawn_get_si_unit_curve(lawn, abscissa));
        xlabel = gwy_lawn_get_curve_label(lawn, abscissa);
    }
    else {
        xunit = gwy_si_unit_new(NULL);
        xlabel = _("sample");
    }
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    ylabel = gwy_lawn_get_curve_label(lawn, ordinate);

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel ? xlabel : _("Untitled"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 NULL);

    g_object_unref(xunit);
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
    GwyLawn *lawn = args->lawn;

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_lawn_get_xres(lawn)-1, gwy_lawn_get_xres(lawn)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_lawn_get_yres(lawn)-1, gwy_lawn_get_yres(lawn)/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
