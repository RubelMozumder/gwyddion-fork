/*
 *  $Id: graph_peaks.c 24444 2021-11-01 10:14:22Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti).
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/peaks.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

enum {
    NPEAKQUANT = GWY_PEAK_WIDTH + 1
};

enum {
    COLUMN_POSITION,
    COLUMN_HEIGHT,
    COLUMN_AREA,
    COLUMN_WIDTH,
    NCOLUMNS,
};

enum {
    PARAM_CURVE,
    PARAM_BACKGROUND,
    PARAM_ORDER,
    PARAM_INVERTED,
    PARAM_NPEAKS,
    PARAM_REPORT_STYLE,
};

typedef struct {
    gdouble v[NPEAKQUANT];
    gint i;
} Peak;

typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyGraphModel *gmodel;
    GwySelection *selection;
    GtkWidget *dialog;
    GtkWidget *peaklist;
    GwyParamTable *table;
    GwyParamTable *table_peaks;
    GArray *peaks;
    GArray *peaks_sorted;
    GwySIValueFormat *vf[NPEAKQUANT];
    gboolean in_init;
    gboolean peaks_valid;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             graph_peaks         (GwyGraph *graph);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static GtkWidget*       create_peak_list    (ModuleGUI *gui);
static void             update_value_formats(ModuleGUI *gui);
static void             sort_peaks          (GArray *peaks,
                                             GArray *peaks_sorted,
                                             gint npeaks,
                                             GwyPeakOrderType order);
static void             select_peaks        (ModuleGUI *gui);
static gchar*           format_report       (gpointer user_data);
static void             analyse_peaks       (ModuleArgs *args,
                                             GArray *peaks);

static const GwyPeakQuantity quantities[NCOLUMNS] = {
    GWY_PEAK_ABSCISSA, GWY_PEAK_HEIGHT, GWY_PEAK_AREA, GWY_PEAK_WIDTH,
};
static const gchar* column_names[NCOLUMNS] = { "x", "h", "A", "w", };

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Finds peaks on graph curves."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, graph_peaks)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_peaks",
                            (GwyGraphFunc)&graph_peaks,
                            N_("/Measure _Features/Find _Peaks..."),
                            GWY_STOCK_FIND_PEAKS,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Find graph curve peaks"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum orders[] = {
        { N_("Position"),   GWY_PEAK_ORDER_ABSCISSA,   },
        { N_("Prominence"), GWY_PEAK_ORDER_PROMINENCE, },
    };
    static const GwyEnum backgrounds[] = {
        { N_("Zero"),              GWY_PEAK_BACKGROUND_ZERO,   },
        { N_("Bilateral minimum"), GWY_PEAK_BACKGROUND_MMSTEP, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_gwyenum(paramdef, PARAM_BACKGROUND, "background", _("_Background type"),
                              backgrounds, G_N_ELEMENTS(backgrounds), GWY_PEAK_BACKGROUND_MMSTEP);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ORDER, "order", _("Order peaks _by"),
                              orders, G_N_ELEMENTS(orders), GWY_PEAK_ORDER_ABSCISSA);
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("Invert (find valleys)"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_NPEAKS, "npeaks", _("Number of _peaks"), 1, 128, 5);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Peak Parameters"),
                                  GWY_RESULTS_EXPORT_FIXED_FORMAT, GWY_RESULTS_REPORT_TABSEP);
    return paramdef;
}

static void
graph_peaks(GwyGraph *graph)
{
    ModuleArgs args;

    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    run_gui(&args);
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GtkWidget *hbox, *hbox2, *graph, *vbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyGraphArea *area;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    guint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.peaks = g_array_new(FALSE, FALSE, sizeof(Peak));
    gui.peaks_sorted = g_array_new(FALSE, FALSE, sizeof(Peak));
    gui.gmodel = gwy_graph_model_new_alike(args->gmodel);
    g_object_set(gui.gmodel, "label-visible", FALSE, NULL);

    analyse_peaks(args, gui.peaks);
    gui.peaks_valid = gui.in_init = TRUE;

    gui.dialog = gwy_dialog_new(_("Graph Peaks"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(gui.gmodel);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XLINES);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph)));
    gwy_graph_area_set_selection_editable(area, FALSE);
    gui.selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XLINES);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, TRUE, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    gwy_param_table_append_combo(table, PARAM_BACKGROUND);
    gwy_param_table_append_combo(table, PARAM_ORDER);
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    gwy_param_table_append_slider(table, PARAM_NPEAKS);
    gwy_param_table_slider_set_mapping(table, PARAM_NPEAKS, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_restrict_range(table, PARAM_NPEAKS, 1, MAX(gui.peaks->len, 1));
    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    gtk_box_pack_start(GTK_BOX(vbox), create_peak_list(&gui), TRUE, TRUE, 0);

    table = gui.table_peaks = gwy_param_table_new(args->params);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_formatter(table, PARAM_REPORT_STYLE, format_report, &gui, NULL);
    /* XXX: Silly.  Just want to right-align the export controls for consistency. */
    hbox2 = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox2), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(GWY_DIALOG(gui.dialog), table);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    // Not necessary: g_signal_connect_swapped(gui.table_peaks, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_array_free(gui.peaks_sorted, TRUE);
    g_array_free(gui.peaks, TRUE);
    for (i = 0; i < NPEAKQUANT; i++)
        GWY_SI_VALUE_FORMAT_FREE(gui.vf[i]);

    return outcome;
}

static void
render_peak(G_GNUC_UNUSED GtkTreeViewColumn *column,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwyPeakQuantity q = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(renderer), "quantity"));
    const Peak *peak;
    gchar buf[32];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    peak = &g_array_index(gui->peaks_sorted, Peak, i);
    g_snprintf(buf, sizeof(buf), "%.*f", gui->vf[q]->precision, peak->v[q]/gui->vf[q]->magnitude);
    g_object_set(renderer, "text", buf, NULL);
}

static void
add_peak_list_column(GtkTreeView *treeview,
                     ModuleGUI *gui,
                     GwyPeakQuantity quantity)
{
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *label;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);

    label = gtk_label_new(NULL);
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);
    gtk_tree_view_append_column(treeview, column);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    g_object_set_data(G_OBJECT(renderer), "quantity", GUINT_TO_POINTER(quantity));
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_peak, gui, NULL);
}

static GtkWidget*
create_peak_list(ModuleGUI *gui)
{
    GtkWidget *scwin;
    GtkTreeView *treeview;
    GtkTreeSelection *selection;
    GwyNullStore *store;
    guint i;

    store = gwy_null_store_new(0);
    gui->peaklist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    treeview = GTK_TREE_VIEW(gui->peaklist);

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

    for (i = 0; i < NCOLUMNS; i++)
        add_peak_list_column(treeview, gui, quantities[i]);
    update_value_formats(gui);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), gui->peaklist);

    return scwin;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_CURVE) {
        gint curve = gwy_params_get_int(params, PARAM_CURVE);

        gwy_null_store_set_n_rows(GWY_NULL_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gui->peaklist))), 0);
        gwy_graph_model_remove_all_curves(gui->gmodel);
        gwy_graph_model_add_curve(gui->gmodel, gwy_graph_model_get_curve(args->gmodel, curve));
        if (gui->in_init)
            gui->in_init = FALSE;
        else
            gui->peaks_valid = FALSE;
    }
    if (id == PARAM_BACKGROUND || id == PARAM_INVERTED)
        gui->peaks_valid = FALSE;

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    if (!gui->peaks_valid) {
        analyse_peaks(gui->args, gui->peaks);
        gwy_param_table_slider_restrict_range(gui->table, PARAM_NPEAKS, 1, MAX(gui->peaks->len, 1));
        update_value_formats(gui);
        gui->peaks_valid = TRUE;
    }
    select_peaks(gui);
}

static gdouble
get_peak_max(GArray *peaks, GwyPeakQuantity quantity)
{
    gint i, n = peaks->len;
    gdouble max = 0.0;

    for (i = 0; i < n; i++) {
        const Peak *peak = &g_array_index(peaks, Peak, i);
        if (peak->v[quantity] > max)
            max = peak->v[quantity];
    }

    return max;
}

static void
update_value_formats(ModuleGUI *gui)
{
    gint curve = gwy_params_get_int(gui->args->params, PARAM_CURVE);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->args->gmodel, curve);
    GwySIUnit *xunit, *yunit, *areaunit;
    GtkTreeView *treeview;
    GtkTreeViewColumn *column;
    GtkLabel *label;
    gdouble min, max, xrange, yrange;
    GArray *peaks = gui->peaks;
    gchar *title;
    guint i;

    g_object_get(gui->gmodel,
                 "si-unit-x", &xunit,
                 "si-unit-y", &yunit,
                 NULL);
    areaunit = gwy_si_unit_multiply(xunit, yunit, NULL);

    gwy_graph_curve_model_get_x_range(gcmodel, &min, &max);
    xrange = max - min;
    gui->vf[GWY_PEAK_ABSCISSA] = gwy_si_unit_get_format_with_digits(xunit, GWY_SI_UNIT_FORMAT_MARKUP, xrange, 4,
                                                                    gui->vf[GWY_PEAK_ABSCISSA]);

    max = get_peak_max(peaks, GWY_PEAK_HEIGHT);
    if (!(max > 0.0)) {
        gwy_graph_curve_model_get_y_range(gcmodel, &min, &max);
        max = 0.4*(max - min);
    }
    yrange = max;
    gui->vf[GWY_PEAK_HEIGHT] = gwy_si_unit_get_format_with_digits(yunit, GWY_SI_UNIT_FORMAT_MARKUP, yrange, 4,
                                                                  gui->vf[GWY_PEAK_HEIGHT]);

    max = 0.0;
    max = get_peak_max(peaks, GWY_PEAK_AREA);
    if (!(max > 0.0))
        max = 0.1*xrange*yrange;
    gui->vf[GWY_PEAK_AREA] = gwy_si_unit_get_format_with_digits(areaunit, GWY_SI_UNIT_FORMAT_MARKUP, 0.5*max, 4,
                                                                gui->vf[GWY_PEAK_AREA]);

    max = 0.0;
    max = get_peak_max(peaks, GWY_PEAK_WIDTH);
    if (!(max > 0.0))
        max = 0.05*xrange;
    gui->vf[GWY_PEAK_WIDTH] = gwy_si_unit_get_format_with_digits(xunit, GWY_SI_UNIT_FORMAT_MARKUP, max, 3,
                                                                 gui->vf[GWY_PEAK_WIDTH]);

    g_object_unref(areaunit);
    g_object_unref(yunit);
    g_object_unref(xunit);

    treeview = GTK_TREE_VIEW(gui->peaklist);
    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_get_column(treeview, i);
        title = g_strdup_printf("<b>%s</b> [%s]", column_names[i], gui->vf[quantities[i]]->units);
        label = GTK_LABEL(gtk_tree_view_column_get_widget(column));
        gtk_label_set_markup(label, title);
        g_free(title);
    }
}

static gint
compare_peak_abscissa(gconstpointer a, gconstpointer b)
{
    const gdouble xa = ((const Peak*)a)->v[GWY_PEAK_ABSCISSA];
    const gdouble xb = ((const Peak*)b)->v[GWY_PEAK_ABSCISSA];

    if (xa < xb)
        return -1;
    if (xa > xb)
        return 1;
    return 0;
}

static void
sort_peaks(GArray *peaks, GArray *peaks_sorted, gint npeaks, GwyPeakOrderType order)
{
    g_array_set_size(peaks_sorted, 0);
    g_array_append_vals(peaks_sorted, peaks->data, npeaks);
    if (order == GWY_PEAK_ORDER_ABSCISSA)
        g_array_sort(peaks_sorted, compare_peak_abscissa);
}

static void
select_peaks(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    GArray *peaks = gui->peaks;
    gint npeaks = gwy_params_get_int(params, PARAM_NPEAKS);
    GwyPeakOrderType order = gwy_params_get_enum(params, PARAM_ORDER);
    GtkTreeView *treeview;
    GwyNullStore *store;
    gint i;
    gdouble *seldata;

    npeaks = MIN((gint)peaks->len, npeaks);
    gwy_selection_set_max_objects(gui->selection, MAX(npeaks, 1));
    gwy_selection_clear(gui->selection);

    sort_peaks(gui->peaks, gui->peaks_sorted, npeaks, order);

    treeview = GTK_TREE_VIEW(gui->peaklist);
    store = GWY_NULL_STORE(gtk_tree_view_get_model(treeview));
    gwy_null_store_set_n_rows(store, npeaks);

    if (!npeaks)
        return;

    seldata = g_new(gdouble, npeaks);
    for (i = 0; i < npeaks; i++) {
        seldata[i] = g_array_index(peaks, Peak, i).v[GWY_PEAK_ABSCISSA];
        gwy_null_store_row_changed(store, i);
    }
    gwy_selection_set_data(gui->selection, npeaks, seldata);
    g_free(seldata);
}

/* TODO: Support both TAB and CVS formats and machine/human styles.  See terracefit for how to do that using
 * gwy_format_result_table_...() functions. */
static gchar*
format_report(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GString *text = g_string_new(NULL);
    GArray *peaks = gui->peaks_sorted;
    guint i, j;

    for (j = 0; j < NCOLUMNS; j++) {
        g_string_append_printf(text, "%s [%s]%c",
                               column_names[j], gui->vf[quantities[j]]->units, j == NCOLUMNS-1 ? '\n' : '\t');
    }
    for (i = 0; i < peaks->len; i++) {
        const Peak *peak = &g_array_index(peaks, Peak, i);
        for (j = 0; j < NCOLUMNS; j++) {
            GwySIValueFormat *vf = gui->vf[quantities[j]];
            g_string_append_printf(text, "%.*f%c",
                                   vf->precision, peak->v[quantities[j]]/vf->magnitude, j == NCOLUMNS-1 ? '\n' : '\t');
        }
    }

    return g_string_free(text, FALSE);
}

static void
analyse_peaks(ModuleArgs *args, GArray *peaks)
{
    gboolean inverted = gwy_params_get_boolean(args->params, PARAM_INVERTED);
    gint curve = gwy_params_get_int(args->params, PARAM_CURVE);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
    GwyPeaks *analyser;
    const gdouble *xdata, *ydata;
    gdouble *invydata = NULL;
    gint n, i, ndata;

    analyser = gwy_peaks_new();
    gwy_peaks_set_order(analyser, GWY_PEAK_ORDER_PROMINENCE);
    gwy_peaks_set_background(analyser, gwy_params_get_enum(args->params, PARAM_BACKGROUND));
    /* We want all the peaks because we control the number in the GUI ourselves. */
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    if (inverted) {
        invydata = g_new(gdouble, ndata);
        for (i = 0; i < ndata; i++)
            invydata[i] = -ydata[i];
    }
    n = gwy_peaks_analyze(analyser, xdata, inverted ? invydata : ydata, ndata, G_MAXUINT);
    g_array_set_size(peaks, n);
    if (n) {
        Peak *p = &g_array_index(peaks, Peak, 0);
        gdouble *values = g_new(gdouble, n);
        guint q;

        for (q = 0; q < NPEAKQUANT; q++) {
            gwy_peaks_get_quantity(analyser, q, values);
            for (i = 0; i < n; i++)
                p[i].v[q] = values[i];
        }
        g_free(values);
    }

    g_free(invydata);
    gwy_peaks_free(analyser);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
