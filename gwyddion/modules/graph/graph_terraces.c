/*
 *  $Id: graph_terraces.c 26100 2024-01-04 14:09:03Z yeti-dn $
 *  Copyright (C) 2019-2023 David Necas (Yeti).
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwycheckboxes.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "../process/preview.h"

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

#define terrace_index(a, i) g_array_index(a, TerraceSegment, (i))

#define MAX_BROADEN 128.0
#define pwr 0.65

enum {
    RESPONSE_SURVEY = 1000,
};

enum {
    PARAM_CURVE,
    PARAM_EDGE_KERNEL_SIZE,
    PARAM_EDGE_THRESHOLD,
    PARAM_EDGE_BROADENING,
    PARAM_MIN_AREA_FRAC,
    PARAM_POLY_DEGREE,
    PARAM_INDEPENDENT,
    PARAM_DISPLAY,
    PARAM_USE_SELECTION,
    PARAM_FIT_REPORT_STYLE,
    PARAM_TERRACE_REPORT_STYLE,
    PARAM_OUTPUT,
    PARAM_POLY_DEGREE_MAX,
    PARAM_POLY_DEGREE_MIN,
    PARAM_BROADENING_MAX,
    PARAM_BROADENING_MIN,
    PARAM_SURVEY_POLY,
    PARAM_SURVEY_BROADENING,
    WIDGET_RESULTS,
    LABEL_FIT_RESULT,
    LABEL_SURVEY,
    BUTTON_RUN_SURVEY,
};

enum {
    COLUMN_ID,
    COLUMN_HEIGHT,
    COLUMN_LEVEL,
    COLUMN_AREA,
    COLUMN_ERROR,
    COLUMN_RESIDUUM,
};

enum {
    MAX_DEGREE = 18,
};

typedef enum {
    PREVIEW_DATA_FIT   = 0,
    PREVIEW_DATA_POLY  = 1,
    PREVIEW_RESIDUUM   = 2,
    PREVIEW_TERRACES   = 3,
    PREVIEW_LEVELLED   = 4,
    PREVIEW_BACKGROUND = 5,
    PREVIEW_STEPS      = 6,
    PREVIEW_NTYPES
} PreviewMode;

typedef enum {
    OUTPUT_DATA_FIT   = (1 << 0),
    OUTPUT_DATA_POLY  = (1 << 1),
    OUTPUT_RESIDUUM   = (1 << 2),
    OUTPUT_TERRACES   = (1 << 3),
    OUTPUT_LEVELLED   = (1 << 4),
    OUTPUT_BACKGROUND = (1 << 5),
    OUTPUT_ALL        = (1 << 6) - 1,
} OutputFlags;

typedef struct {
    guint nterrparam;
    guint npowers;
    guint nterraces;
    gdouble msq;
    gdouble deltares;
    gdouble *solution;
    gdouble *invdiag;
} FitResult;

typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
    GwyDataLine *edges;
    GwyDataLine *residuum;
    GwyDataLine *background;
    GArray *terracesegments;
    FitResult *fres;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyGraphModel *gmodel;
    GtkWidget *dialog;
    GtkWidget *graph;
    GwyResults *results;
    GtkWidget *terracelist;
    GdkPixbuf *colourpixbuf;
    GwyParamTable *table_param;
    GwyParamTable *table_terraces;
    GwyParamTable *table_output;
    GwyParamTable *table_survey;
    GwySIValueFormat *vf;
} ModuleGUI;

typedef struct {
    gdouble xfrom;
    gdouble xto;
    gint i;
    gint npixels;
    gint level;
    gdouble height;   /* estimate from free fit */
    gdouble error;    /* difference from free fit estimate */
    gdouble residuum; /* final fit residuum */
} TerraceSegment;

typedef struct {
    gint poly_degree;
    gdouble edge_kernel_size;
    gdouble edge_threshold;
    gdouble edge_broadening;
    gdouble min_area_frac;
    gint fit_ok;
    gint nterraces;
    gdouble step;
    gdouble step_err;
    gdouble msq;
    gdouble discrep;
} TerraceSurveyRow;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             graph_terraces      (GwyGraph *graph);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             graph_xsel_changed  (ModuleGUI *gui);
static void             preview             (gpointer user_data);
static void             fill_preview_graph  (ModuleGUI *gui);
static FitResult*       terrace_do          (const gdouble *xdata,
                                             const gdouble *ydata,
                                             guint ndata,
                                             GwyDataLine *edges,
                                             GwyDataLine *residuum,
                                             GwyDataLine *background,
                                             GArray *terracesegments,
                                             GwySelection *xsel,
                                             GwyParams *params,
                                             const gchar** message);
static void             update_value_formats(ModuleGUI *gui);
static GtkWidget*       parameters_tab_new  (ModuleGUI *gui);
static GtkWidget*       terrace_list_tab_new(ModuleGUI *gui);
static GtkWidget*       output_tab_new      (ModuleGUI *gui);
static GtkWidget*       survey_tab_new      (ModuleGUI *gui);
static GwyResults*      create_results      (GwyContainer *data,
                                             GwyGraphModel *gmodel);
static void             create_output_graphs(ModuleArgs *args,
                                             GwyContainer *data);
static void             free_fit_result     (FitResult *fres);
static gchar*           format_report       (gpointer user_data);
static guint            prepare_survey      (GwyParams *params,
                                             GArray *degrees,
                                             GArray *broadenings);
static void             run_survey          (ModuleGUI *gui);

static const GwyEnum output_flags[] = {
    { N_("Data + fit"),            OUTPUT_DATA_FIT,   },
    { N_("Data + polynomials"),    OUTPUT_DATA_POLY,  },
    { N_("Difference"),            OUTPUT_RESIDUUM,   },
    { N_("Terraces (ideal)"),      OUTPUT_TERRACES,   },
    { N_("Leveled surface"),       OUTPUT_LEVELLED,   },
    { N_("Polynomial background"), OUTPUT_BACKGROUND, },
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Fits terraces with polynomial background."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, graph_terraces)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_terraces",
                            (GwyGraphFunc)&graph_terraces,
                            N_("/Measure _Features/_Terraces..."),
                            GWY_STOCK_GRAPH_TERRACE_MEASURE,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Fit terraces with polynomial background"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("Data + fit"),            PREVIEW_DATA_FIT,   },
        { N_("Data + polynomials"),    PREVIEW_DATA_POLY,  },
        { N_("Difference"),            PREVIEW_RESIDUUM,   },
        { N_("Terraces (ideal)"),      PREVIEW_TERRACES,   },
        { N_("Leveled surface"),       PREVIEW_LEVELLED,   },
        { N_("Polynomial background"), PREVIEW_BACKGROUND, },
        { N_("Step detection"),        PREVIEW_STEPS,      },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_double(paramdef, PARAM_EDGE_KERNEL_SIZE, "edge_kernel_size", _("_Step detection kernel"),
                             1.0, 64.0, 3.5);
    gwy_param_def_add_percentage(paramdef, PARAM_EDGE_THRESHOLD, "edge_threshold", _("Step detection _threshold"),
                                 0.4);
    gwy_param_def_add_double(paramdef, PARAM_EDGE_BROADENING, "edge_broadening", _("Step _broadening"),
                             0.0, 128.0, 6.0);
    gwy_param_def_add_double(paramdef, PARAM_MIN_AREA_FRAC, "min_area_frac", _("Minimum terrace _length"),
                             0.0, 0.4, 0.015);
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE, "poly_degree", _("_Polynomial degree"), 0, MAX_DEGREE, 4);
    gwy_param_def_add_boolean(paramdef, PARAM_INDEPENDENT, "independent", _("_Independent heights"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, NULL, gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), PREVIEW_DATA_FIT);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_SELECTION, "use_selection", _("Select regions _manually"), FALSE);
    gwy_param_def_add_report_type(paramdef, PARAM_FIT_REPORT_STYLE, "fit_report_style", _("Save Fit Report"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    gwy_param_def_add_report_type(paramdef, PARAM_TERRACE_REPORT_STYLE, "terrace_report_style", _("Save Terrace Table"),
                                  GWY_RESULTS_EXPORT_TABULAR_DATA, GWY_RESULTS_REPORT_TABSEP);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output"),
                               output_flags, G_N_ELEMENTS(output_flags), OUTPUT_DATA_POLY);
    gwy_param_def_add_boolean(paramdef, PARAM_SURVEY_POLY, "survey_poly", _("_Polynomial degree"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SURVEY_BROADENING, "survey_broadening", _("Step _broadening"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE_MIN, "poly_degree_min", _("M_inimum polynomial degree"),
                          0, MAX_DEGREE, 0);
    gwy_param_def_add_int(paramdef, PARAM_POLY_DEGREE_MAX, "poly_degree_max", _("_Maximum polynomial degree"),
                          0, MAX_DEGREE, MAX_DEGREE);
    gwy_param_def_add_double(paramdef, PARAM_BROADENING_MIN, "broadening_min", _("Minimum broadening"),
                             0, MAX_BROADEN, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_BROADENING_MAX, "broadening_max", _("Maximum broadening"),
                             0, MAX_BROADEN, MAX_BROADEN);

    return paramdef;
}

static void
graph_terraces(GwyGraph *graph)
{
    GwyContainer *data;
    GwyDialogOutcome outcome;
    GwySIUnit *unit;
    ModuleArgs args;

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);

    args.background = gwy_data_line_new(1, 1.0, TRUE);
    g_object_get(args.gmodel, "si-unit-x", &unit, NULL);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(args.background), unit);
    g_object_unref(unit);
    g_object_get(args.gmodel, "si-unit-y", &unit, NULL);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(args.background), unit);
    args.edges = gwy_data_line_duplicate(args.background);
    args.residuum = gwy_data_line_duplicate(args.background);
    args.terracesegments = g_array_new(FALSE, FALSE, sizeof(TerraceSegment));

    outcome = run_gui(&args, data);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_HAVE_RESULT)
        create_output_graphs(&args, data);

    g_object_unref(args.edges);
    g_object_unref(args.residuum);
    g_object_unref(args.background);
    g_object_unref(args.params);
    g_array_free(args.terracesegments, TRUE);
    free_fit_result(args.fres);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GtkWidget *hbox, *notebook;
    GwyGraphArea *area;
    GwySelection *xsel;
    ModuleGUI gui;
    gint width, height;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(data, args->gmodel);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    gui.colourpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, height, height);

    gui.gmodel = gwy_graph_model_new_alike(args->gmodel);
    g_object_set(gui.gmodel, "label-visible", FALSE, NULL);

    gui.dialog = gwy_dialog_new(_("Fit Terraces"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_CLEAR, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), parameters_tab_new(&gui), gtk_label_new(_("Parameters")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), terrace_list_tab_new(&gui), gtk_label_new(_("Terrace List")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), output_tab_new(&gui), gtk_label_new(_("Output")));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), survey_tab_new(&gui), gtk_label_new(_("Survey")));

    /* Graph */
    gui.graph = gwy_graph_new(gui.gmodel);
    gtk_widget_set_size_request(gui.graph, 480, 300);
    gwy_graph_enable_user_input(GWY_GRAPH(gui.graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), gui.graph, TRUE, TRUE, 0);
    gwy_graph_set_status(GWY_GRAPH(gui.graph), GWY_GRAPH_STATUS_XSEL);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(gui.graph)));
    gwy_graph_area_set_selection_editable(area, TRUE);
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(xsel, 1024);

    g_signal_connect_swapped(xsel, "changed", G_CALLBACK(graph_xsel_changed), &gui);
    g_signal_connect_swapped(gui.table_param, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_terraces, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_survey, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);
    g_object_unref(gui.gmodel);
    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static void
update_value_formats(ModuleGUI *gui)
{
    gint curve = gwy_params_get_int(gui->args->params, PARAM_CURVE);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->args->gmodel, curve);
    GtkTreeView *treeview;
    GList *columns, *l;
    GwySIUnit *yunit;
    gdouble min, max, yrange;

    g_object_get(gui->gmodel, "si-unit-y", &yunit, NULL);

    gwy_graph_curve_model_get_y_range(gcmodel, &min, &max);
    yrange = max - min;
    gui->vf = gwy_si_unit_get_format_with_digits(yunit, GWY_SI_UNIT_FORMAT_MARKUP, yrange, 4, gui->vf);

    g_object_unref(yunit);

    treeview = GTK_TREE_VIEW(gui->terracelist);
    columns = gtk_tree_view_get_columns(treeview);
    for (l = columns; l; l = g_list_next(l)) {
        GtkTreeViewColumn *column = GTK_TREE_VIEW_COLUMN(l->data);
        gboolean is_z = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "is_z"));
        const gchar *title = g_object_get_data(G_OBJECT(column), "title");
        GtkWidget *label = gtk_tree_view_column_get_widget(column);
        gchar *s;

        if (is_z && *gui->vf->units)
            s = g_strdup_printf("<b>%s</b> [%s]", title, gui->vf->units);
        else
            s = g_strdup_printf("<b>%s</b>", title);
        gtk_label_set_markup(GTK_LABEL(label), s);
        g_free(s);
    }
    g_list_free(columns);
}

static GtkWidget*
parameters_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_param = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    gwy_param_table_append_slider(table, PARAM_EDGE_KERNEL_SIZE);
    /* FIXME: Use alts for real sizes?  Pixels are a bit shady anyway. */
    gwy_param_table_set_unitstr(table, PARAM_EDGE_KERNEL_SIZE, _("px"));
    gwy_param_table_append_slider(table, PARAM_EDGE_THRESHOLD);
    gwy_param_table_append_slider(table, PARAM_EDGE_BROADENING);
    gwy_param_table_slider_set_steps(table, PARAM_EDGE_BROADENING, 0.1, 1.0);
    gwy_param_table_set_unitstr(table, PARAM_EDGE_BROADENING, _("px"));
    gwy_param_table_slider_set_digits(table, PARAM_EDGE_BROADENING, 1);
    gwy_param_table_append_slider(table, PARAM_MIN_AREA_FRAC);
    gwy_param_table_slider_set_factor(table, PARAM_MIN_AREA_FRAC, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_MIN_AREA_FRAC, "%");
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_INDEPENDENT);
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    gwy_param_table_append_checkbox(table, PARAM_USE_SELECTION);

    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui->results, "step", "resid", "discrep", "nterraces", NULL);
    gwy_param_table_append_message(table, LABEL_FIT_RESULT, NULL);
    gwy_param_table_message_set_type(table, LABEL_FIT_RESULT, GTK_MESSAGE_ERROR);
    gwy_param_table_append_report(table, PARAM_FIT_REPORT_STYLE);
    gwy_param_table_report_set_results(table, PARAM_FIT_REPORT_STYLE, gui->results);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
render_text_column(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer user_data)
{
    guint column_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "column-id"));
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwySIValueFormat *vf = gui->vf;
    TerraceSegment *seg;
    gchar buf[32];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    seg = &terrace_index(gui->args->terracesegments, i);
    if (column_id == COLUMN_ID)
        g_snprintf(buf, sizeof(buf), "%u", i+1);
    else if (column_id == COLUMN_AREA)
        g_snprintf(buf, sizeof(buf), "%u", seg->npixels);
    else if (column_id == COLUMN_HEIGHT)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, seg->height/vf->magnitude);
    else if (column_id == COLUMN_LEVEL)
        g_snprintf(buf, sizeof(buf), "%d", seg->level);
    else if (column_id == COLUMN_ERROR)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, seg->error/vf->magnitude);
    else if (column_id == COLUMN_RESIDUUM)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, seg->residuum/vf->magnitude);
    else {
        g_assert_not_reached();
    }
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_colour(G_GNUC_UNUSED GtkTreeViewColumn *column, G_GNUC_UNUSED GtkCellRenderer *renderer,
              GtkTreeModel *model, GtkTreeIter *iter,
              gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    guint i, pixel;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    pixel = 0xff | gwy_rgba_to_pixbuf_pixel(gwy_graph_get_preset_color(i+1));
    gdk_pixbuf_fill(gui->colourpixbuf, pixel);
}

static GtkTreeViewColumn*
append_text_column(ModuleGUI *gui, guint column_id, const gchar *title, gboolean is_z)
{
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *label;

    column = gtk_tree_view_column_new();
    g_object_set_data(G_OBJECT(column), "column-id", GUINT_TO_POINTER(column_id));
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_text_column, gui, NULL);

    label = gtk_label_new(NULL);
    g_object_set_data(G_OBJECT(column), "title", (gpointer)title);
    g_object_set_data(G_OBJECT(column), "is_z", GINT_TO_POINTER(is_z));
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);

    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->terracelist), column);

    return column;
}

static GtkWidget*
terrace_list_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GtkWidget *vbox, *hbox, *scwin;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GwyNullStore *store;
    GwyParamTable *table;

    vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    store = gwy_null_store_new(0);
    gui->terracelist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_container_add(GTK_CONTAINER(scwin), gui->terracelist);

    column = append_text_column(gui, COLUMN_ID, "n", FALSE);
    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, FALSE);
    g_object_set(renderer, "pixbuf", gui->colourpixbuf, NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_colour, gui, NULL);
    append_text_column(gui, COLUMN_HEIGHT, "h", TRUE);
    append_text_column(gui, COLUMN_LEVEL, "k", FALSE);
    append_text_column(gui, COLUMN_AREA, "N<sub>px</sub>", FALSE);
    append_text_column(gui, COLUMN_ERROR, "Δ", TRUE);
    append_text_column(gui, COLUMN_RESIDUUM, "r", TRUE);

    table = gui->table_terraces = gwy_param_table_new(args->params);
    gwy_param_table_append_report(table, PARAM_TERRACE_REPORT_STYLE);
    gwy_param_table_report_set_formatter(table, PARAM_TERRACE_REPORT_STYLE, format_report, gui, NULL);
    /* XXX: Silly.  Just want to right-align the export controls for consistency. */
    hbox = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return vbox;
}

static GtkWidget*
output_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
survey_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_survey = gwy_param_table_new(args->params);
    gwy_param_table_append_checkbox(table, PARAM_SURVEY_POLY);
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE_MIN);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE_MIN, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_POLY_DEGREE_MAX);
    gwy_param_table_slider_set_mapping(table, PARAM_POLY_DEGREE_MAX, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_SURVEY_BROADENING);
    gwy_param_table_append_slider(table, PARAM_BROADENING_MIN);
    gwy_param_table_set_unitstr(table, PARAM_BROADENING_MIN, _("px"));
    gwy_param_table_append_slider(table, PARAM_BROADENING_MAX);
    gwy_param_table_set_unitstr(table, PARAM_BROADENING_MAX, _("px"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_RUN_SURVEY, -1, RESPONSE_SURVEY, _("_Execute"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_message(table, LABEL_SURVEY, NULL);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GwyResults*
create_results(GwyContainer *data,
               GwyGraphModel *gmodel)
{
    GwySIUnit *yunit;
    GwyResults *results = gwy_results_new();

    gwy_results_add_header(results, N_("Fit Results"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "graph", N_("Graph"));
    gwy_results_add_value_str(results, "curve", N_("Curve"));
    gwy_results_add_separator(results);
    /* TODO: We might also want to output the segmentation & fit settings. */
    gwy_results_add_value_z(results, "step", N_("Fitted step height"));
    gwy_results_add_value_z(results, "resid", N_("Mean square difference"));
    gwy_results_add_value_z(results, "discrep", N_("Terrace discrepancy"));
    gwy_results_add_value_int(results, "nterraces", N_("Number of terraces"));
    g_object_get(gmodel, "si-unit-y", &yunit, NULL);
    gwy_results_set_unit(results, "z", yunit);
    g_object_unref(yunit);
    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_graph(results, "graph", gmodel);

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table;
    gboolean survey_changed = (id == PARAM_SURVEY_POLY || id == PARAM_SURVEY_BROADENING);

    table = gui->table_param;
    if (id < 0 || id == PARAM_USE_SELECTION) {
        gboolean use_selection = gwy_params_get_boolean(params, PARAM_USE_SELECTION);
        GwyGraph *graph = GWY_GRAPH(gui->graph);

        gwy_param_table_set_sensitive(table, PARAM_EDGE_KERNEL_SIZE, !use_selection);
        gwy_param_table_set_sensitive(table, PARAM_EDGE_THRESHOLD, !use_selection);
        gwy_param_table_set_sensitive(table, PARAM_EDGE_BROADENING, !use_selection);
        gwy_param_table_set_sensitive(table, PARAM_MIN_AREA_FRAC, !use_selection);
        gwy_graph_enable_user_input(graph, use_selection);
        gwy_graph_set_status(GWY_GRAPH(gui->graph), use_selection ? GWY_GRAPH_STATUS_XSEL : GWY_GRAPH_STATUS_PLAIN);
    }
    if (id < 0 || id == PARAM_CURVE) {
        gint ndata, curve = gwy_params_get_int(params, PARAM_CURVE);
        GwyGraphCurveModel *gcmodel;

        gwy_graph_model_remove_all_curves(gui->gmodel);
        gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
        gwy_graph_model_add_curve(gui->gmodel, gcmodel);
        ndata = gwy_graph_curve_model_get_ndata(gcmodel);
        gwy_data_line_resample(args->edges, ndata, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(args->residuum, ndata, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(args->background, ndata, GWY_INTERPOLATION_NONE);
    }
    if (id == PARAM_DISPLAY)
        fill_preview_graph(gui);

    table = gui->table_survey;
    if (id == PARAM_POLY_DEGREE_MIN || id == PARAM_POLY_DEGREE_MAX) {
        gint min_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MIN);
        gint max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MAX);
        if (min_degree > max_degree) {
            if (id == PARAM_POLY_DEGREE_MAX)
                gwy_param_table_set_int(table, PARAM_POLY_DEGREE_MIN, (min_degree = max_degree));
            else
                gwy_param_table_set_int(table, PARAM_POLY_DEGREE_MAX, (max_degree = min_degree));
        }
        survey_changed = TRUE;
    }
    if (id == PARAM_BROADENING_MIN || id == PARAM_BROADENING_MAX) {
        gdouble min_broadening = gwy_params_get_double(params, PARAM_BROADENING_MIN);
        gdouble max_broadening = gwy_params_get_double(params, PARAM_BROADENING_MAX);
        if (min_broadening > max_broadening) {
            if (id == PARAM_BROADENING_MAX)
                gwy_param_table_set_double(table, PARAM_BROADENING_MIN, (min_broadening = max_broadening));
            else
                gwy_param_table_set_double(table, PARAM_BROADENING_MAX, (max_broadening = min_broadening));
        }
        survey_changed = TRUE;
    }

    if (id < 0 || id == PARAM_INDEPENDENT || survey_changed) {
        gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
        gboolean survey_poly = gwy_params_get_boolean(params, PARAM_SURVEY_POLY);
        gboolean survey_broadening = gwy_params_get_boolean(params, PARAM_SURVEY_BROADENING);
        const gchar *message;
        gchar *s = NULL;

        gwy_param_table_set_sensitive(table, PARAM_SURVEY_POLY, !independent);
        gwy_param_table_set_sensitive(table, PARAM_POLY_DEGREE_MIN, !independent && survey_poly);
        gwy_param_table_set_sensitive(table, PARAM_POLY_DEGREE_MAX, !independent && survey_poly);
        gwy_param_table_set_sensitive(table, PARAM_SURVEY_BROADENING, !independent);
        gwy_param_table_set_sensitive(table, PARAM_BROADENING_MIN, !independent && survey_broadening);
        gwy_param_table_set_sensitive(table, PARAM_BROADENING_MAX, !independent && survey_broadening);
        gwy_param_table_set_sensitive(table, BUTTON_RUN_SURVEY, !independent && (survey_poly || survey_broadening));
        if (independent)
            message = _("Survey cannot be run with independent heights.");
        else if (!survey_poly && !survey_broadening)
            message = _("No free parameters are selected.");
        else
            message = s = g_strdup_printf(_("Number of combinations: %u."), prepare_survey(params, NULL, NULL));

        gwy_param_table_set_label(table, LABEL_SURVEY, message);
        g_free(s);
    }

    if (!survey_changed && id != PARAM_OUTPUT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_SURVEY)
        run_survey(gui);
    else if (response == GWY_RESPONSE_CLEAR) {
        GwyGraphArea *area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(gui->graph)));
        GwySelection *xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
        gwy_selection_clear(xsel);
    }
}

static void
graph_xsel_changed(ModuleGUI *gui)
{
    if (gwy_params_get_boolean(gui->args->params, PARAM_USE_SELECTION))
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
free_fit_result(FitResult *fres)
{
    if (fres) {
        g_free(fres->solution);
        g_free(fres->invdiag);
        g_free(fres);
    }
}

static void
create_segmented_graph_curve(GwyGraphModel *gmodel,
                             GwyGraphCurveModel *gcmodel,
                             GArray *terracesegments,
                             const gdouble *xdata,
                             const gdouble *ydata)
{
    guint g, nterraces = terracesegments->len;
    GwyGraphCurveModel *gcmodel2;
    GString *str = g_string_new(NULL);

    for (g = 0; g < nterraces; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
        g_string_printf(str, _("Segment %u"), g+1);
        g_object_set(gcmodel2, "color", gwy_graph_get_preset_color(g+1), "description", str->str, NULL);
        gwy_graph_curve_model_set_data(gcmodel2, xdata + seg->i, ydata + seg->i, seg->npixels);
        gwy_graph_model_add_curve(gmodel, gcmodel2);
        g_object_unref(gcmodel2);
    }
    g_string_free(str, TRUE);
}

static int
compare_gint(const void *a, const void *b)
{
    const gint ia = *(const gint*)a;
    const gint ib = *(const gint*)b;

    if (ia < ib)
        return -1;
    if (ia > ib)
        return 1;
    return 0;
}

static void
create_one_output_graph(ModuleArgs *args,
                        GwyGraphModel *gmodel,
                        PreviewMode mode,
                        GArray *terracesegments,
                        FitResult *fres,
                        gboolean for_preview)
{
    GwyParams *params = args->params;
    GwyDataLine *edges = args->edges, *residuum = args->residuum, *background = args->background;
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
    gdouble edge_threshold = gwy_params_get_double(params, PARAM_EDGE_THRESHOLD);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
    const gdouble *xdata, *ydata;
    GwyDataLine *dline;
    gdouble *d;
    guint i, j, ndata, nterraces;

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    if (mode == PREVIEW_DATA_FIT || mode == PREVIEW_DATA_POLY) {
        gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
        g_object_set(gcmodel, "color", gwy_graph_get_preset_color(0), NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (!fres && mode != PREVIEW_STEPS)
        return;

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 NULL);
    nterraces = terracesegments->len;

    if (mode == PREVIEW_DATA_FIT) {
        /* Add segmented fit */
        dline = gwy_data_line_duplicate(residuum);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < ndata; i++)
            d[i] = ydata[i] - d[i];
        g_object_set(gcmodel, "line-width", 2, NULL);
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata, d);
        g_object_unref(dline);
    }
    else if (mode == PREVIEW_DATA_POLY) {
        /* Add full polynomial background shifted to all discrete levels. */
        const gdouble *solution = fres->solution;
        GwyGraphCurveModel *gcmodel2;
        GString *str = g_string_new(NULL);

        dline = gwy_data_line_duplicate(background);
        if (independent) {
            for (i = 0; i < nterraces; i++) {
                gwy_data_line_copy(background, dline);
                gwy_data_line_add(dline, solution[i]);

                gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
                g_string_printf(str, _("Segment %u"), i+1);
                g_object_set(gcmodel2, "color", gwy_graph_get_preset_color(i+1), "description", str->str, NULL);
                gwy_graph_curve_model_set_data(gcmodel2, xdata, gwy_data_line_get_data(dline), ndata);
                gwy_graph_model_add_curve(gmodel, gcmodel2);
                g_object_unref(gcmodel2);
            }
        }
        else {
            gint *levels = g_new(gint, nterraces);

            for (i = 0; i < nterraces; i++)
                levels[i] = terrace_index(terracesegments, i).level;

            qsort(levels, nterraces, sizeof(gint), compare_gint);
            for (i = 0; i < nterraces; i++) {
                if (i && levels[i-1] == levels[i])
                    continue;

                gwy_data_line_copy(background, dline);
                gwy_data_line_add(dline, solution[1] + levels[i]*solution[0]);

                gcmodel2 = gwy_graph_curve_model_duplicate(gcmodel);
                g_string_printf(str, _("Level %d"), levels[i]);
                g_object_set(gcmodel2, "description", str->str, NULL);
                gwy_graph_curve_model_set_data(gcmodel2, xdata, gwy_data_line_get_data(dline), ndata);
                gwy_graph_model_add_curve(gmodel, gcmodel2);
                g_object_unref(gcmodel2);
            }
            g_free(levels);
        }
        g_object_unref(dline);
        g_string_free(str, TRUE);
    }
    else if (mode == PREVIEW_RESIDUUM) {
        /* Add segmented residuum. */
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata, gwy_data_line_get_data(residuum));
    }
    else if (mode == PREVIEW_TERRACES) {
        /* Add segmented ideal terraces. */
        const gdouble *solution = fres->solution;

        dline = gwy_data_line_new_alike(background, TRUE);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < nterraces; i++) {
            const TerraceSegment *seg = &terrace_index(terracesegments, i);
            gdouble h = (independent ? solution[i] : solution[1] + seg->level*solution[0]);

            for (j = 0; j < seg->npixels; j++)
                d[seg->i + j] = h;
        }
        create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata, d);
        g_object_unref(dline);
    }
    else if (mode == PREVIEW_LEVELLED) {
        /* Add segmented data minus background. */
        dline = gwy_data_line_duplicate(background);
        d = gwy_data_line_get_data(dline);
        for (i = 0; i < ndata; i++)
            d[i] = ydata[i] - d[i];
        gwy_graph_curve_model_set_data(gcmodel, xdata, d, ndata);
        g_object_set(gcmodel, "color", gwy_graph_get_preset_color(0), "description", _("Leveled surface"), NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        if (for_preview)
            create_segmented_graph_curve(gmodel, gcmodel, terracesegments, xdata, d);
        g_object_unref(dline);
    }
    else if (mode == PREVIEW_BACKGROUND) {
        /* Add full background. */
        gwy_graph_curve_model_set_data(gcmodel, xdata, gwy_data_line_get_data(background), ndata);
        g_object_set(gcmodel, "description", _("Polynomial background"), NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
    }
    else if (mode == PREVIEW_STEPS) {
        gdouble stepxdata[2], stepydata[2];

        /* Add full step filter result. */
        g_object_set(gcmodel, "color", gwy_graph_get_preset_color(0), NULL);
        gwy_graph_curve_model_set_data(gcmodel, xdata, gwy_data_line_get_data(edges), ndata);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        gcmodel = gwy_graph_curve_model_new();
        stepxdata[0] = xdata[0];
        stepxdata[1] = xdata[ndata-1];
        stepydata[0] = stepydata[1] = edge_threshold*gwy_data_line_get_max(edges);
        gwy_graph_curve_model_set_data(gcmodel, stepxdata, stepydata, 2);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "line-style", GDK_LINE_ON_OFF_DASH,
                     "color", gwy_graph_get_preset_color(1),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
    }

    g_object_unref(gcmodel);
}

static void
fill_preview_graph(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    PreviewMode mode = gwy_params_get_enum(args->params, PARAM_DISPLAY);
    gwy_graph_model_remove_all_curves(gui->gmodel);
    create_one_output_graph(args, gui->gmodel, mode, args->terracesegments, args->fres, TRUE);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataLine *edges = args->edges, *residuum = args->residuum, *background = args->background;
    GwyParams *params = args->params;
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
    GwyResults *results = gui->results;
    GwyGraphCurveModel *gcmodel;
    GwyGraphArea *area;
    GtkTreeModel *model;
    FitResult *fres;
    const gchar *message = "";
    GwySelection *xsel;

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, FALSE);
    gwy_app_wait_cursor_start(GTK_WINDOW(gui->dialog));
    update_value_formats(gui);

    free_fit_result(args->fres);
    args->fres = NULL;

    gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
    gwy_results_fill_graph_curve(results, "curve", gcmodel);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->terracelist));
    gwy_null_store_set_n_rows(GWY_NULL_STORE(model), 0);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(gui->graph)));
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);
    fres = terrace_do(gwy_graph_curve_model_get_xdata(gcmodel),
                      gwy_graph_curve_model_get_ydata(gcmodel),
                      gwy_graph_curve_model_get_ndata(gcmodel),
                      edges, residuum, background,
                      args->terracesegments, xsel, args->params, &message);

    gwy_param_table_set_label(gui->table_param, LABEL_FIT_RESULT, message);
    args->fres = fres;

    if (fres) {
        gwy_null_store_set_n_rows(GWY_NULL_STORE(model), args->terracesegments->len);
        gwy_results_fill_values(results,
                                "nterraces", fres->nterraces,
                                "resid", fres->msq,
                                NULL);
        if (independent)
            gwy_results_set_na(results, "step", "discrep", NULL);
        else {
            gwy_results_fill_values_with_errors(results,
                                                "step", fres->solution[0], sqrt(fres->invdiag[0])*fres->msq,
                                                NULL);
            gwy_results_fill_values(results, "discrep", fres->deltares, NULL);
        }
        gwy_param_table_results_fill(gui->table_param, WIDGET_RESULTS);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
    else
        gwy_param_table_results_clear(gui->table_param, WIDGET_RESULTS);

#ifdef DEBUG
    if (fres) {
        printf("%d %g %g %g %g %u\n",
               gwy_params_get_int(params, PARAM_POLY_DEGREE),
               fres->solution[0],
               sqrt(fres->invdiag[0])*fres->msq,
               fres->msq,
               fres->deltares,
               fres->nterraces);
    }
#endif

    fill_preview_graph(gui);
    gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !!fres);
}

static void
make_segments_from_xsel(GArray *terracesegments,
                        const gdouble *xdata,
                        guint ndata,
                        GwySelection *xsel)
{
    gdouble epsilon = 1e-9*fabs(xdata[ndata-1] - xdata[0]);
    guint i, j, nsel;

    nsel = gwy_selection_get_data(xsel, NULL);
    for (i = 0; i < nsel; i++) {
        TerraceSegment seg;
        gdouble xseg[2];

        gwy_selection_get_object(xsel, i, xseg);
        xseg[0] -= epsilon;
        xseg[1] += epsilon;
        GWY_ORDER(gdouble, xseg[0], xseg[1]);

        /* Find integer index ranges corresponding to the selection.  Be careful to skip empty segments. */
        gwy_clear(&seg, 1);
        seg.xfrom = xseg[0];
        seg.xto = xseg[1];
        for (j = 0; j < ndata; j++) {
            if (xdata[j] >= xseg[0])
                break;
        }
        if (j == ndata)
            continue;

        seg.i = j;
        while (j < ndata && xdata[j] <= xseg[1])
            j++;
        if (j == seg.i)
            continue;

        seg.npixels = j - seg.i;
        g_array_append_val(terracesegments, seg);
    }
}

static inline void
step_gauss_line_integrals(gdouble t, gdouble x1, gdouble x2,
                          gdouble y1, gdouble y2,
                          gdouble *pu,
                          gdouble *s1, gdouble *sx, gdouble *sxx,
                          gdouble *sy, gdouble *sxy)
{
    gdouble u1 = *pu;
    gdouble u2 = exp(-0.5*t*t);
    gdouble h = x2 - x1;

    *s1 += h*(u1 + u2);
    *sx += h*(x1*u1 + x2*u2);
    *sxx += h*(x1*x1*u1 + x2*x2*u2);
    *sy += h*(u1*y1 + u2*y2);
    *sxy += h*(x1*y1*u1 + x2*y2*u2);

    *pu = u2;
}

/* Perform Gaussian step filter analogous to the 2D case, but try trapezoid integration. The result values are in
 * units of height and generally should roughly estimate the edge heights. */
static void
apply_gaussian_step_filter(const gdouble *xdata,
                           const gdouble *ydata,
                           GwyDataLine *filtered,
                           gdouble dx,
                           gdouble sigma)
{
    gint n = gwy_data_line_get_res(filtered);
    gdouble *d = gwy_data_line_get_data(filtered);
    gint i;

    gwy_clear(d, n);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(d,xdata,ydata,n,dx,sigma) \
            private(i)
#endif
    for (i = 2; i < n-2; i++) {
        gdouble t, u, xorigin = xdata[i];
        gdouble x1, x2, y1, y2, zlimfw, zlimback, det;
        gdouble s1, sx, sxx, sy, sxy;
        gint j;

        s1 = sx = sxx = sy = sxy = 0.0;
        u = t = x2 = 0.0;
        y2 = ydata[i];
        for (j = i+1; j < n; j++) {
            x1 = x2;
            y1 = y2;
            x2 = xdata[j] - xorigin;
            y2 = ydata[j];
            t = x2/(sigma*dx);
            step_gauss_line_integrals(t, x1, x2, y1, y2, &u, &s1, &sx, &sxx, &sy, &sxy);
            if (t > 8.0)
                break;
        }
        det = s1*sxx - sx*sx;
        zlimfw = (det > 0.0 ? (sy*sxx - sxy*sx)/det : ydata[i]);

        s1 = sx = sxx = sy = sxy = 0.0;
        u = t = x2 = 0.0;
        y2 = ydata[i];
        for (j = i-1; j >= 0; j--) {
            x1 = x2;
            y1 = y2;
            x2 = xorigin - xdata[j];
            y2 = ydata[j];
            t = x2/(sigma*dx);
            step_gauss_line_integrals(t, x1, x2, y1, y2, &u, &s1, &sx, &sxx, &sy, &sxy);
            if (t > 8.0)
                break;
        }

        det = s1*sxx - sx*sx;
        zlimback = (det > 0.0 ? (sy*sxx - sxy*sx)/det : ydata[i]);

        d[i] = fabs(zlimfw - zlimback);
    }
}

static void
enumerate_line_segments(GwyDataLine *marked, const gdouble *xdata,
                        GArray *terracesegments)
{
    gint n = gwy_data_line_get_res(marked);
    gdouble *md = gwy_data_line_get_data(marked);
    gint i, prevedge;
    TerraceSegment seg;

    g_array_set_size(terracesegments, 0);

    prevedge = 0;
    for (i = 1; i < n; i++) {
        /* An edge? */
        if (md[i-1] != md[i]) {
            if (md[i] == 0.0) {
                /* Downwards edge.  The previous segment is a grain. */
                gwy_clear(&seg, 1);
                seg.xfrom = (prevedge ? 0.5*(xdata[prevedge-1] + xdata[prevedge]) : 1.5*xdata[0] - 0.5*xdata[1]);
                seg.xto = 0.5*(xdata[i-1] + xdata[i]);
                seg.i = prevedge;
                seg.npixels = i-prevedge;
                g_array_append_val(terracesegments, seg);
            }
            /* If it's an upwards edge, just remember its position.  Do that in any case. */
            prevedge = i;
        }
    }

    /* Data ending inside a grain is the same as downwards edge. */
    if (md[n-1]) {
        gwy_clear(&seg, 1);
        seg.xfrom = (prevedge ? 0.5*(xdata[prevedge-1] + xdata[prevedge]) : 1.5*xdata[0] - 0.5*xdata[1]);
        seg.xto = 1.5*xdata[n-1] - 0.5*xdata[n-2];
        seg.i = prevedge;
        seg.npixels = n-prevedge;
        g_array_append_val(terracesegments, seg);
    }
}

/* Shrink grains using real distance (i.e. possibly non-uniform sampling). */
static void
shrink_grains(GwyDataLine *marked, const gdouble *xdata, gdouble distance, GArray *terracesegments)
{
    gint j, n = gwy_data_line_get_res(marked);
    gdouble *md = gwy_data_line_get_data(marked);
    guint g, nseg;

    nseg = terracesegments->len;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);

        /* Extend non-grain forward from the left edge. */
        if (seg->i > 0) {
            for (j = seg->i+1; j < n && md[j]; j++) {
                if (xdata[j] - seg->xfrom <= distance)
                    md[j] = 0.0;
            }
        }
        /* Extend non-grain bacwkard from the right edge. */
        if (seg->i + seg->npixels < n) {
            for (j = seg->i + seg->npixels-1; j >= 0 && md[j]; j--) {
                if (seg->xto - xdata[j] <= distance)
                    md[j] = 0.0;
            }
        }
    }
}

/* Remove grains by size using real distance (i.e. possibly non-uniform sampling). */
static void
remove_grains_by_size(GwyDataLine *marked, gdouble minsize, GArray *terracesegments)
{
    gdouble *md = gwy_data_line_get_data(marked);
    guint g, nseg;

    /* Iterate backwards.  We do not need to care how the array changes beyond the current item and the removals are
     * slightly more efficient. */
    nseg = terracesegments->len;
    for (g = nseg; g; g--) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g-1);

        if (seg->xto - seg->xfrom < minsize) {
            gwy_clear(md+seg->i, seg->npixels);
            g_array_remove_index(terracesegments, g-1);
        }
    }
}

static gboolean
find_terrace_segments(GArray *terracesegments,
                      const gdouble *xdata,
                      const gdouble *ydata,
                      guint ndata,
                      GwyParams *params,
                      GwyDataLine *edges,
                      GwyDataLine *marked,
                      GwySelection *xsel,
                      gdouble *pxc,
                      gdouble *pxq)
{
    gdouble edge_kernel_size = gwy_params_get_double(params, PARAM_EDGE_KERNEL_SIZE);
    gdouble edge_threshold = gwy_params_get_double(params, PARAM_EDGE_THRESHOLD);
    gdouble edge_broadening = gwy_params_get_double(params, PARAM_EDGE_BROADENING);
    gdouble min_area_frac = gwy_params_get_double(params, PARAM_MIN_AREA_FRAC);
    gboolean use_selection = gwy_params_get_boolean(params, PARAM_USE_SELECTION);
    gint i, npixels;
    gdouble threshold, xlen, min, max, dx, xc, xq;
    guint g, nseg;

    g_array_set_size(terracesegments, 0);

    if (ndata < 3)
        return FALSE;

    /* Use a common `pixel' size. */
    gwy_assign(gwy_data_line_get_data(edges), xdata, ndata);
    gwy_data_line_get_min_max(edges, &min, &max);
    xlen = max - min;
    dx = xlen/ndata;

    /* Always calculate the Gaussian filter to have something to display even when we do not use the result for
     * fitting. */
    apply_gaussian_step_filter(xdata, ydata, edges, dx, edge_kernel_size);
    gwy_data_line_copy(edges, marked);

    if (use_selection) {
        /* Use provided selection as requested. */
        make_segments_from_xsel(terracesegments, xdata, ndata, xsel);
    }
    else {
        /* Mark flat areas in the profile. */
        threshold = edge_threshold*gwy_data_line_get_max(marked);
        gwy_data_line_threshold(marked, threshold, 1.0, 0.0);
        enumerate_line_segments(marked, xdata, terracesegments);
        shrink_grains(marked, xdata, edge_broadening*dx, terracesegments);

        /* Keep only large areas.  This inherently limits the maximum number of areas too. */
        enumerate_line_segments(marked, xdata, terracesegments);
        remove_grains_by_size(marked, min_area_frac*xlen, terracesegments);
    }

    nseg = terracesegments->len;
    if (!nseg) {
        g_array_set_size(terracesegments, 0);
        return FALSE;
    }

    /* Normalise coordinates to have centre of mass at 0 and effective range in the order of unity (we normalise the
     * integral of x² to 1). */
    xc = 0.0;
    npixels = 0;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gint n = seg->npixels;

        npixels += n;
        for (i = 0; i < n; i++)
            xc += xdata[i + seg->i];
    }
    xc /= npixels;
    *pxc = xc;

    xq = 0.0;
    for (g = 0; g < nseg; g++) {
        const TerraceSegment *seg = &terrace_index(terracesegments, g);
        gint n = seg->npixels;
        for (i = 0; i < n; i++) {
            gdouble t = xdata[i + seg->i];
            xq += t*t;
        }
    }
    xq /= npixels;
    *pxq = (xq > 0.0 ? 1.0/sqrt(xq) : 1.0);

    return TRUE;
}

/* Diagonal power-power matrix block.  Some of the entries could be calculated from the per-terrace averages; the
 * higher powers are only used here though.  This is the slow part.  */
static gdouble*
calculate_power_matrix_block(GArray *terracesegments, const gdouble *xdata,
                             gdouble xc, gdouble xq, gint poly_degree)
{
    guint nterraces, kp, mp;
    gdouble *power_block;

    if (!poly_degree)
        return NULL;

    /* We multiply two powers together so the maximum power in the product is twice the single maximum power.  Also,
     * there would be poly_degree+1 powers, but we omit the 0th power so we have exactly poly_degree powers, starting
     * from 1. */
    nterraces = terracesegments->len;
    power_block = g_new0(gdouble, poly_degree*poly_degree);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(terracesegments,poly_degree,power_block,xdata,xc,xq,nterraces)
#endif
    {
        gdouble *xpowers = g_new(gdouble, 2*poly_degree+1);
        gdouble *tpower_block = gwy_omp_if_threads_new0(power_block, poly_degree*poly_degree);
        guint gfrom = gwy_omp_chunk_start(nterraces);
        guint gto = gwy_omp_chunk_end(nterraces);
        guint m, k, g, i;

        xpowers[0] = 1.0;

        for (g = gfrom; g < gto; g++) {
            TerraceSegment *seg = &terrace_index(terracesegments, g);
            guint ifrom = seg->i, npixels = seg->npixels;

            for (i = 0; i < npixels; i++) {
                gdouble x = xq*(xdata[ifrom + i] - xc);

                for (k = 1; k <= 2*poly_degree; k++)
                    xpowers[k] = xpowers[k-1]*x;

                for (k = 1; k <= poly_degree; k++) {
                    for (m = 1; m <= k; m++)
                        tpower_block[(k-1)*poly_degree + (m-1)] += xpowers[k+m];
                }
            }
        }
        g_free(xpowers);
        gwy_omp_if_threads_sum_double(power_block, tpower_block, poly_degree*poly_degree);
    }

    /* Redundant, but keep for simplicity. */
    for (kp = 0; kp < poly_degree; kp++) {
        for (mp = kp+1; mp < poly_degree; mp++)
            power_block[kp*poly_degree + mp] = power_block[mp*poly_degree + kp];
    }

    return power_block;
}

static void
calculate_residuum(GArray *terracesegments, FitResult *fres,
                   GwyDataLine *residuum,
                   const gdouble *xdata, const gdouble *ydata,
                   gdouble xc, gdouble xq,
                   gint poly_degree,
                   gboolean indep)
{
    const gdouble *solution = fres->solution, *solution_block;
    gdouble *resdata;
    guint g, i, k, nterraces = terracesegments->len, npixels;

    solution_block = solution + (indep ? nterraces : 2);
    gwy_data_line_clear(residuum);
    resdata = gwy_data_line_get_data(residuum);

    fres->msq = fres->deltares = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;
        gdouble z0 = (indep ? solution[g] : ng*solution[0] + solution[1]);
        gdouble ts = 0.0, toff = 0.0;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0, s = z0;

            for (k = 0; k < poly_degree; k++) {
                xp *= x;
                s += xp*solution_block[k];
            }
            s = y - s;
            resdata[ifrom + i] = s;
            ts += s*s;
            toff += s;
        }
        seg->residuum = sqrt(ts/n);
        seg->error = toff/n;
        fres->msq += ts;
        fres->deltares += seg->error*seg->error * n;
        npixels += n;
    }
    fres->msq = sqrt(fres->msq/npixels);
    fres->deltares = sqrt(fres->deltares/npixels);
}

static FitResult*
fit_terraces_arbitrary(GArray *terracesegments,
                       const gdouble *xdata, const gdouble *ydata,
                       gdouble xc, gdouble xq,
                       gint poly_degree,
                       const gdouble *power_block,
                       GwyDataLine *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels;
    gdouble *mixed_block, *matrix, *invmat, *rhs;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterrparam = fres->nterraces = terracesegments->len;
    fres->npowers = poly_degree;
    matn = nterraces + poly_degree;

    /* Calculate the matrix by pieces, the put it together.  The terrace block is identity matrix I so we do not need
     * to compute it. */
    mixed_block = g_new0(gdouble, poly_degree*nterraces);
    rhs = fres->solution = g_new0(gdouble, nterraces + poly_degree);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed off-diagonal power-terrace matrix block (we represent it as the upper right block) and power block on the
     * right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gdouble *mixed_row = mixed_block + g*poly_degree;
        gdouble *rhs_block = rhs + nterraces;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0;

            for (k = 1; k <= poly_degree; k++) {
                xp *= x;
                mixed_row[k-1] += xp;
                rhs_block[k-1] += xp*y;
            }
        }
    }

    /* Terrace block of right hand side. */
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;

        for (i = 0; i < n; i++) {
            gdouble y = ydata[ifrom + i];
            rhs[g] += y;
        }
        npixels += n;
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);
    gwy_debug("matrix (%u)", matn);
    for (i = 0; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (i < nterraces && j < nterraces)
                t = (i == j)*(terrace_index(terracesegments, i).npixels);
            else if (j < nterraces)
                t = mixed_block[j*poly_degree + (i - nterraces)];
            else
                t = power_block[(i - nterraces)*poly_degree + (j - nterraces)];

            SLi(matrix, i, j) = t;
#ifdef DEBUG_MATRIX
            printf("% .2e ", t);
#endif
        }
#ifdef DEBUG_MATRIX
        printf("\n");
#endif
    }
    g_free(mixed_block);

    invmat = g_memdup(matrix, matsize*sizeof(gdouble));
    ok = gwy_math_choleski_decompose(matn, matrix);
    gwy_debug("decomposition: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum) {
        calculate_residuum(terracesegments, fres, residuum, xdata, ydata, xc, xq, poly_degree, TRUE);
    }

    ok = gwy_math_choleski_invert(matn, invmat);
    gwy_debug("inversion: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (i = 0; i < matn; i++)
        fres->invdiag[i] = SLi(invmat, i, i);

finalise:
    g_free(matrix);
    g_free(invmat);

    return fres;
}

static FitResult*
fit_terraces_same_step(GArray *terracesegments,
                       const gdouble *xdata, const gdouble *ydata,
                       gdouble xc, gdouble xq,
                       gint poly_degree,
                       const gdouble *power_block,
                       GwyDataLine *residuum,
                       const gchar **message)
{
    gint g, k, nterraces, matn, matsize, npixels;
    gdouble *sheight_block, *offset_block, *matrix, *invmat, *rhs;
    gdouble stepstep, stepoff, offoff;
    FitResult *fres;
    guint i, j;
    gboolean ok;

    fres = g_new0(FitResult, 1);
    nterraces = fres->nterraces = terracesegments->len;
    fres->npowers = poly_degree;
    fres->nterrparam = 2;
    matn = 2 + poly_degree;

    /* Calculate the matrix by pieces, the put it together.  */
    sheight_block = g_new0(gdouble, poly_degree);
    offset_block = g_new0(gdouble, poly_degree);
    rhs = fres->solution = g_new0(gdouble, matn);
    fres->invdiag = g_new0(gdouble, matn);

    /* Mixed two first upper right matrix rows and power block of right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;
        gdouble *rhs_block = rhs + 2;

        for (i = 0; i < n; i++) {
            gdouble x = xq*(xdata[ifrom + i] - xc), y = ydata[ifrom + i];
            gdouble xp = 1.0;

            for (k = 1; k <= poly_degree; k++) {
                xp *= x;
                sheight_block[k-1] += xp*ng;
                offset_block[k-1] += xp;
                rhs_block[k-1] += xp*y;
            }
        }
    }

    /* Remaining three independent elements in the top left corner of the matrix. */
    stepstep = stepoff = 0.0;
    npixels = 0;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint n = seg->npixels;
        gint ng = seg->level;

        /* Ensure ng does not converted to unsigned, with disasterous consequences. */
        stepstep += ng*ng*(gdouble)n;
        stepoff += ng*(gdouble)n;
        npixels += n;
    }
    offoff = npixels;

    /* Remaining first two elements of the right hand side. */
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);
        guint ifrom = seg->i, n = seg->npixels;
        gint ng = seg->level;

        for (i = 0; i < n; i++) {
            gdouble y = ydata[ifrom + i];
            rhs[0] += ng*y;
            rhs[1] += y;
        }
    }

    /* Construct the matrix. */
    matsize = (matn + 1)*matn/2;
    matrix = g_new(gdouble, matsize);

    gwy_debug("matrix (%u)", matn);
    SLi(matrix, 0, 0) = stepstep;
#ifdef DEBUG_MATRIX
    printf("% .2e\n", stepstep);
#endif
    SLi(matrix, 1, 0) = stepoff;
    SLi(matrix, 1, 1) = offoff;
#ifdef DEBUG_MATRIX
    printf("% .2e % .2e\n", stepoff, offoff);
#endif

    for (i = 2; i < matn; i++) {
        for (j = 0; j <= i; j++) {
            gdouble t;

            if (j == 0)
                t = sheight_block[i-2];
            else if (j == 1)
                t = offset_block[i-2];
            else
                t = power_block[(i - 2)*poly_degree + (j - 2)];

            SLi(matrix, i, j) = t;
#ifdef DEBUG_MATRIX
            printf("% .2e ", t);
#endif
        }
#ifdef DEBUG_MATRIX
        printf("\n");
#endif
    }
    g_free(sheight_block);
    g_free(offset_block);

    invmat = g_memdup(matrix, matsize*sizeof(gdouble));
    ok = gwy_math_choleski_decompose(matn, matrix);
    gwy_debug("decomposition: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    gwy_math_choleski_solve(matn, matrix, rhs);

    if (residuum) {
        calculate_residuum(terracesegments, fres, residuum, xdata, ydata, xc, xq, poly_degree, FALSE);
    }

    ok = gwy_math_choleski_invert(matn, invmat);
    gwy_debug("inversion: %s", ok ? "OK" : "FAIL");
    if (!ok) {
        *message = _("Fit failed");
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (i = 0; i < matn; i++)
        fres->invdiag[i] = SLi(invmat, i, i);

finalise:
    g_free(invmat);
    g_free(matrix);

    return fres;
}

static gint*
estimate_step_parameters(const gdouble *heights, guint n,
                         gdouble *stepheight, gdouble *offset,
                         const gchar **message)
{
    gdouble *steps;
    gdouble sh, off, p;
    gint *levels;
    gint g, ns, m;

    if (n < 2) {
        *message = _("No suitable terrace steps found");
        return NULL;
    }

    steps = g_memdup(heights, n*sizeof(gdouble));
    ns = n-1;
    for (g = 0; g < ns; g++) {
        steps[g] = fabs(steps[g+1] - steps[g]);
        gwy_debug("step%d: height %g nm", g, steps[g]/1e-9);
    }

    p = 85.0;
    gwy_math_percentiles(ns, steps, GWY_PERCENTILE_INTERPOLATION_LINEAR, 1, &p, &sh);
    gwy_debug("estimated step height %g nm", sh/1e-9);
    g_free(steps);

    *stepheight = sh;

    levels = g_new(gint, n);
    levels[0] = 0;
    m = 0;
    for (g = 1; g < n; g++) {
        levels[g] = levels[g-1] + GWY_ROUND((heights[g] - heights[g-1])/sh);
        m = MIN(m, levels[g]);
    }

    off = 0.0;
    for (g = 0; g < n; g++) {
        levels[g] -= m;
        off += heights[g] - sh*levels[g];
    }
    off /= n;
    *offset = off;
    gwy_debug("estimated base offset %g nm", off/1e-9);

    return levels;
}

/* XXX: The background is generally bogus far outside the fitted region.  This usually means profile ends because they
 * contain too small terrace bits. It is more meaningful to only calculate it for marked area. */
static void
fill_background(GwyDataLine *background,
                const gdouble *xdata, gdouble xc, gdouble xq,
                gint poly_degree,
                const gdouble *coeffs)
{
    gint res, i, k;
    gdouble *d;

    res = gwy_data_line_get_res(background);
    d = gwy_data_line_get_data(background);
    for (i = 0; i < res; i++) {
        gdouble x = xq*(xdata[i] - xc);
        gdouble xp = 1.0, s = 0.0;

        for (k = 1; k <= poly_degree; k++) {
            xp *= x;
            s += xp*coeffs[k-1];
        }
        d[i] = s;
    }
}

static FitResult*
terrace_do(const gdouble *xdata, const gdouble *ydata, guint ndata,
           GwyDataLine *edges, GwyDataLine *residuum, GwyDataLine *background,
           GArray *terracesegments, GwySelection *xsel,
           GwyParams *params,
           const gchar **message)
{
    guint nterraces;
    FitResult *fres;
    gdouble sheight, offset, xc, xq;
    gdouble *power_block;
    gboolean independent = gwy_params_get_boolean(params, PARAM_INDEPENDENT);
    gint g, poly_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE);
    gint *levels;

    /* Use background dataline as a scratch space. */
    if (!find_terrace_segments(terracesegments, xdata, ydata, ndata, params, edges, background, xsel, &xc, &xq)) {
        *message = _("No terraces were found");
        return NULL;
    }

    nterraces = terracesegments->len;
    power_block = calculate_power_matrix_block(terracesegments, xdata, xc, xq, poly_degree);
    if (!(fres = fit_terraces_arbitrary(terracesegments,
                                        xdata, ydata, xc, xq, poly_degree,
                                        power_block,
                                        independent ? residuum : NULL,
                                        message)))
        goto finalise;

    if (!(levels = estimate_step_parameters(fres->solution, nterraces, &sheight, &offset, message))) {
        g_array_set_size(terracesegments, 0);
        free_fit_result(fres);
        fres = NULL;
        goto finalise;
    }
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);

        /* This does not depend on whether we run the second stage fit. */
        seg->level = levels[g];
        seg->height = fres->solution[g];
        /* This will be recalculated in the second stage fit.  Note that error is anyway with respect to the multiple
         * of estimated step height and normally similar in both fit types. */
        seg->error = fres->solution[g] - offset - seg->level*sheight;
    }
    g_free(levels);

    /* Normally also perform the second stage fitting with a single common step height.  But if requested, avoid it,
     * keeping the heights independents. */
    if (!independent) {
        free_fit_result(fres);
        if (!(fres = fit_terraces_same_step(terracesegments, xdata, ydata, xc, xq, poly_degree, power_block,
                                            residuum, message)))
            goto finalise;
    }
    fill_background(background, xdata, xc, xq, poly_degree, fres->solution + (independent ? nterraces : 2));

finalise:
    g_free(power_block);

    return fres;
}

static void
create_output_graphs(ModuleArgs *args, GwyContainer *data)
{
    const guint output_map[2*G_N_ELEMENTS(output_flags)] = {
        PREVIEW_DATA_FIT,   OUTPUT_DATA_FIT,
        PREVIEW_DATA_POLY,  OUTPUT_DATA_POLY,
        PREVIEW_RESIDUUM,   OUTPUT_RESIDUUM,
        PREVIEW_TERRACES,   OUTPUT_TERRACES,
        PREVIEW_LEVELLED,   OUTPUT_LEVELLED,
        PREVIEW_BACKGROUND, OUTPUT_BACKGROUND,
    };
    guint i, oflags = gwy_params_get_flags(args->params, PARAM_OUTPUT);
    GArray *terracesegments = args->terracesegments;
    FitResult *fres = args->fres;
    GwyGraphModel *gmodel;
    const gchar *title;

    for (i = 0; i < G_N_ELEMENTS(output_flags); i++) {
        if (!(output_map[2*i + 1] & oflags))
            continue;

        gmodel = gwy_graph_model_new_alike(args->gmodel);
        create_one_output_graph(args, gmodel, output_map[2*i], terracesegments, fres, FALSE);
        title = gwy_enum_to_string(output_map[2*i + 1], output_flags, G_N_ELEMENTS(output_flags));
        g_object_set(gmodel, "title", _(title), NULL);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
    }
}

static gchar*
format_report(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyResultsReportType report_style = gwy_params_get_report_type(args->params, PARAM_TERRACE_REPORT_STYLE);
    GArray *terracesegments = args->terracesegments;
    GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_UNICODE;
    GwySIValueFormat *vfz;
    GwySIUnit *yunit;
    GString *text;
    const gchar *k_header, *Npx_header;
    gchar *h_header, *Delta_header, *r_header;
    guint g, nterraces;

    text = g_string_new(NULL);
    if (!(report_style & GWY_RESULTS_REPORT_MACHINE))
        vfz = gui->vf;
    else {
        g_object_get(args->gmodel, "si-unit-y", &yunit, NULL);
        vfz = gwy_si_unit_get_format_for_power10(yunit, style, 0, NULL);
        g_object_unref(yunit);
    }

    h_header = g_strdup_printf("h [%s]", vfz->units);
    k_header = "k";
    Npx_header = "Npx";
    Delta_header = g_strdup_printf("Δ [%s]", vfz->units);
    r_header = g_strdup_printf("r [%s]", vfz->units);
    gwy_format_result_table_strings(text, report_style, 5, h_header, k_header, Npx_header, Delta_header, r_header);
    g_free(h_header);
    g_free(Delta_header);
    g_free(r_header);

    nterraces = terracesegments->len;
    for (g = 0; g < nterraces; g++) {
        TerraceSegment *seg = &terrace_index(terracesegments, g);

        gwy_format_result_table_mixed(text, report_style, "viivv",
                                      seg->height/vfz->magnitude, seg->level,
                                      seg->npixels,
                                      seg->error/vfz->magnitude, seg->residuum/vfz->magnitude);
    }

    return g_string_free(text, FALSE);
}

static gdouble
interpolate_broadening(gdouble a, gdouble b, gdouble t)
{
    return pow((1.0 - t)*pow(a, pwr) + t*pow(b, pwr), 1.0/pwr);
}

static guint
prepare_survey(GwyParams *params, GArray *degrees, GArray *broadenings)
{
    gint min_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MIN);
    gint max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE_MAX);
    gdouble min_broadening = gwy_params_get_double(params, PARAM_BROADENING_MIN);
    gdouble max_broadening = gwy_params_get_double(params, PARAM_BROADENING_MAX);
    guint i, ndegrees, nbroadenings;

    if (!gwy_params_get_boolean(params, PARAM_SURVEY_POLY))
        min_degree = max_degree = gwy_params_get_int(params, PARAM_POLY_DEGREE);
    if (!gwy_params_get_boolean(params, PARAM_SURVEY_BROADENING))
        min_broadening = max_broadening = gwy_params_get_double(params, PARAM_EDGE_BROADENING);

    ndegrees = max_degree+1 - min_degree;
    nbroadenings = GWY_ROUND(2.0*(pow(max_broadening, pwr) - pow(min_broadening, pwr))) + 1;

    if (degrees) {
        g_array_set_size(degrees, ndegrees);
        for (i = 0; i < ndegrees; i++)
            g_array_index(degrees, gint, i) = min_degree + i;
    }
    if (broadenings) {
        g_array_set_size(broadenings, nbroadenings);
        for (i = 0; i < nbroadenings; i++) {
            gdouble t = (nbroadenings == 1 ? 0.5 : i/(nbroadenings - 1.0));
            g_array_index(broadenings, gdouble, i) = interpolate_broadening(min_broadening, max_broadening, t);
        }
    }

    return nbroadenings*ndegrees;
}

static void
run_survey(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *surveyparams = gwy_params_duplicate(args->params);
    GwyResultsReportType report_style = gwy_params_get_report_type(args->params, PARAM_TERRACE_REPORT_STYLE);
    gint curve = gwy_params_get_int(args->params, PARAM_CURVE);
    GArray *terracesegments = NULL, *surveyout, *degrees, *broadenings;
    GwyDataLine *edges, *residuum, *background;
    GwyGraphCurveModel *gcmodel;
    GwyGraphArea *area;
    FitResult *fres;
    GwySelection *xsel;
    gint ndata;
    const gdouble *xdata, *ydata;
    const gchar *message;
    GString *str;
    guint i, w, totalwork, ndegrees;

    report_style |= GWY_RESULTS_REPORT_MACHINE;
    gcmodel = gwy_graph_model_get_curve(args->gmodel, curve);
    area = GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(gui->graph)));
    xsel = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XSEL);

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    edges = gwy_data_line_new_alike(args->edges, FALSE);
    residuum = gwy_data_line_new_alike(args->residuum, FALSE);
    background = gwy_data_line_new_alike(args->background, FALSE);
    terracesegments = g_array_new(FALSE, FALSE, sizeof(TerraceSegment));
    surveyout = g_array_new(FALSE, FALSE, sizeof(TerraceSurveyRow));

    degrees = g_array_new(FALSE, FALSE, sizeof(gint));
    broadenings = g_array_new(FALSE, FALSE, sizeof(gdouble));
    totalwork = prepare_survey(args->params, degrees, broadenings);
    ndegrees = degrees->len;

    gwy_app_wait_start(GTK_WINDOW(gui->dialog), _("Fitting in progress..."));

    for (w = 0; w < totalwork; w++) {
        TerraceSurveyRow srow;

        gwy_params_set_int(surveyparams, PARAM_POLY_DEGREE, g_array_index(degrees, gint, w % ndegrees));
        gwy_params_set_double(surveyparams, PARAM_EDGE_BROADENING, g_array_index(broadenings, gdouble, w/ndegrees));
        fres = terrace_do(xdata, ydata, ndata, edges, residuum, background, terracesegments, xsel, surveyparams,
                          &message);

        gwy_clear(&srow, 1);
        srow.poly_degree = gwy_params_get_int(surveyparams, PARAM_POLY_DEGREE);
        srow.edge_kernel_size = gwy_params_get_double(surveyparams, PARAM_EDGE_KERNEL_SIZE);
        srow.edge_threshold = gwy_params_get_double(surveyparams, PARAM_EDGE_THRESHOLD);
        srow.edge_broadening = gwy_params_get_double(surveyparams, PARAM_EDGE_BROADENING);
        srow.min_area_frac = gwy_params_get_double(surveyparams, PARAM_MIN_AREA_FRAC);
        srow.fit_ok = !!fres;
        if (fres) {
            srow.nterraces = fres->nterraces;
            srow.step = fres->solution[0];
            srow.step_err = sqrt(fres->invdiag[0])*fres->msq;
            srow.msq = fres->msq;
            srow.discrep = fres->deltares;
        }
        g_array_append_val(surveyout, srow);

        free_fit_result(fres);

        if (!gwy_app_wait_set_fraction((w + 1.0)/totalwork))
            break;
    }

    gwy_app_wait_finish();

    g_array_free(degrees, TRUE);
    g_array_free(broadenings, TRUE);
    g_array_free(terracesegments, TRUE);
    g_object_unref(edges);
    g_object_unref(residuum);
    g_object_unref(background);

    if (w != totalwork) {
        g_array_free(surveyout, TRUE);
        return;
    }

    str = g_string_new(NULL);
    gwy_format_result_table_strings(str, report_style, 11,
                                    "Poly degree", "Edge kernel size", "Edge threshold", "Edge broadening",
                                    "Min area frac", "Fit OK", "Num terraces", "Step height", "Step height err",
                                    "Msq residual", "Discrepancy");
    for (i = 0; i < surveyout->len; i++) {
        TerraceSurveyRow *srow = &g_array_index(surveyout, TerraceSurveyRow, i);
        gwy_format_result_table_mixed(str, report_style, "ivvvvyivvvv",
                                      srow->poly_degree,
                                      srow->edge_kernel_size, srow->edge_threshold, srow->edge_broadening,
                                      srow->min_area_frac,
                                      srow->fit_ok,
                                      srow->nterraces, srow->step, srow->step_err, srow->msq, srow->discrep);
    }
    g_array_free(surveyout, TRUE);

    gwy_save_auxiliary_data(_("Save Terrace Fit Survey"), GTK_WINDOW(gui->dialog), str->len, str->str);
    g_string_free(str, TRUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
