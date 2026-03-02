/*
 *  $Id: spectro.c 24961 2022-08-29 14:22:53Z yeti-dn $
 *  Copyright (C) 2003-2022 Owain Davies, David Necas (Yeti), Petr Klapetek.
 *  E-mail: owain.davies@blueyonder.co.uk, yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <glib-object.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libprocess/linestats.h>
#include <libprocess/spectra.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwymodule/gwymodule-tool.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

enum {
    MIN_RESOLUTION = 4,
    MAX_RESOLUTION = 16384
};

enum {
    COLUMN_I, COLUMN_X, COLUMN_Y, NCOLUMNS
};

enum {
    PARAM_SEPARATE,
    PARAM_AVERAGE,
    PARAM_TARGET_GRAPH,
    PARAM_OPTIONS_VISIBLE,
};

#define GWY_TYPE_TOOL_SPECTRO            (gwy_tool_spectro_get_type())
#define GWY_TOOL_SPECTRO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_SPECTRO, GwyToolSpectro))
#define GWY_IS_TOOL_SPECTRO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_SPECTRO))
#define GWY_TOOL_SPECTRO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_SPECTRO, GwyToolSpectroClass))

typedef struct _GwyToolSpectro      GwyToolSpectro;
typedef struct _GwyToolSpectroClass GwyToolSpectroClass;

struct _GwyToolSpectro {
    GwyPlainTool parent_instance;

    GwyParams *params;

    GtkTreeView *treeview;
    GtkTreeModel *model;
    GwyParamTable *table;

    GwyDataLine *line;
    GwyDataLine *weights;
    GwyGraphModel *gmodel;
    GwySpectra *spectra;

    GdkPixbuf *colorpixbuf;
    gulong layer_object_chosen_id;
    gboolean ignore_tree_selection;

    /* potential class data */
    GType layer_type;
};

struct _GwyToolSpectroClass {
    GwyPlainToolClass parent_class;
};

static gboolean     module_register                  (void);
static GwyParamDef* define_module_params             (void);
static GType        gwy_tool_spectro_get_type        (void)                           G_GNUC_CONST;
static void         gwy_tool_spectro_finalize        (GObject *object);
static void         gwy_tool_spectro_init_dialog     (GwyToolSpectro *tool);
static void         gwy_tool_spectro_data_switched   (GwyTool *gwytool,
                                                      GwyDataView *data_view);
static void         gwy_tool_spectro_spectra_switched(GwyTool *gwytool,
                                                      GwySpectra *spectra);
static void         gwy_tool_spectro_response        (GwyTool *tool,
                                                      gint response_id);
static void         gwy_tool_spectro_apply           (GwyToolSpectro *tool);
static void         param_changed                    (GwyToolSpectro *tool,
                                                      gint id);
static void         fill_locations                   (GwyToolSpectro *tool);
static void         tree_selection_changed           (GtkTreeSelection *selection,
                                                      gpointer user_data);
static void         object_chosen                    (GwyVectorLayer *gwyvectorlayer,
                                                      gint i,
                                                      gpointer *data);
static void         show_curve                       (GwyToolSpectro *tool,
                                                      gint i);
static void         gather_curve                     (GwyToolSpectro *tool,
                                                      gint i);
static void         accumulate_lines                 (GwyDataLine *accum,
                                                      GwyDataLine *dline,
                                                      GwyDataLine *weights);
static void         show_averaged                    (GwyToolSpectro *tool);
static void         update_header                    (GwyToolSpectro *tool,
                                                      guint col,
                                                      GString *str,
                                                      const gchar *title,
                                                      GwySIValueFormat *vf);
static void         render_cell                      (GtkCellLayout *layout,
                                                      GtkCellRenderer *renderer,
                                                      GtkTreeModel *model,
                                                      GtkTreeIter *iter,
                                                      gpointer user_data);
static void         render_colour                    (GtkCellLayout *layout,
                                                      GtkCellRenderer *renderer,
                                                      GtkTreeModel *model,
                                                      GtkTreeIter *iter,
                                                      gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Point Spectrum, extracts point spectra to a graph."),
    "Owain Davies <owain.davies@blueyonder.co.uk>",
    "0.20",
    "Owain Davies, David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, spectro)

G_DEFINE_TYPE(GwyToolSpectro, gwy_tool_spectro, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_SPECTRO);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "spectro");
    gwy_param_def_add_boolean(paramdef, PARAM_SEPARATE, "separate", _("_Separate spectra"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_AVERAGE, "average", _("_Average spectra"), FALSE);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, NULL, NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_OPTIONS_VISIBLE, "options_visible", NULL, FALSE);

    return paramdef;
}

static void
gwy_tool_spectro_class_init(GwyToolSpectroClass *klass)
{
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_spectro_finalize;

    tool_class->stock_id = GWY_STOCK_SPECTRUM;
    tool_class->title = _("Point Spectroscopy");
    tool_class->tooltip = _("Extract and view point spectroscopy data");
    tool_class->prefix = "/module/spectro";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_spectro_data_switched;
    tool_class->spectra_switched = gwy_tool_spectro_spectra_switched;
    tool_class->response = gwy_tool_spectro_response;
}

static void
gwy_tool_spectro_finalize(GObject *object)
{
    GwyToolSpectro *tool = GWY_TOOL_SPECTRO(object);
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(object);

    /* Prevent bad things in the selection callback */
    tool->ignore_tree_selection = TRUE;

    GWY_SIGNAL_HANDLER_DISCONNECT(plain_tool->layer, tool->layer_object_chosen_id);
    gtk_tree_view_set_model(tool->treeview, NULL);
    gwy_params_save_to_settings(tool->params);
    GWY_OBJECT_UNREF(tool->params);
    GWY_OBJECT_UNREF(tool->colorpixbuf);
    GWY_OBJECT_UNREF(tool->model);
    GWY_OBJECT_UNREF(tool->spectra);
    GWY_OBJECT_UNREF(tool->gmodel);

    G_OBJECT_CLASS(gwy_tool_spectro_parent_class)->finalize(object);
}

static void
gwy_tool_spectro_init(GwyToolSpectro *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gint width, height;

    tool->layer_type = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerPoint");
    if (!tool->layer_type)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    tool->params = gwy_params_new_from_settings(define_module_params());

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
    height |= 1;
    tool->colorpixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, height, height);

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type, "spec");

    gwy_tool_spectro_init_dialog(tool);
}

static void
gwy_tool_spectro_init_dialog(GwyToolSpectro *tool)
{
    static const gchar *column_titles[] = {
        "<b>n</b>", "<b>x</b>", "<b>y</b>", "<b>visible</b>",
    };
    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *scwin, *label, *hbox, *vbox, *options, *graph;
    GwyParamTable *table;
    GtkTreeSelection *selection;
    guint i;

    tool->gmodel = gwy_graph_model_new();
    g_object_set(tool->gmodel, "label-visible", FALSE, NULL);

    hbox = gwy_hbox_new(4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Left pane */
    vbox = gwy_vbox_new(8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Point coordinates */
    tool->model = GTK_TREE_MODEL(gwy_null_store_new(0));
    tool->treeview = GTK_TREE_VIEW(gtk_tree_view_new_with_model(tool->model));

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, render_cell, tool, NULL);
        if (i == COLUMN_I) {
            renderer = gtk_cell_renderer_pixbuf_new();
            g_object_set(renderer, "pixbuf", tool->colorpixbuf, NULL);
            gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, FALSE);
            gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, render_colour, tool, NULL);
        }
        label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), column_titles[i]);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(tool->treeview, column);
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tool->treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);
    g_signal_connect(G_OBJECT(selection), "changed", G_CALLBACK(tree_selection_changed), tool);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), GTK_WIDGET(tool->treeview));
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    /* Options */
    options = gwy_create_expander_with_param(_("<b>Options</b>"), tool->params, PARAM_OPTIONS_VISIBLE);
    gtk_box_pack_start(GTK_BOX(vbox), options, FALSE, FALSE, 0);

    table = tool->table = gwy_param_table_new(tool->params);
    gwy_param_table_append_checkbox(table, PARAM_SEPARATE);
    gwy_param_table_append_checkbox(table, PARAM_AVERAGE);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, tool->gmodel);
    gwy_plain_tool_add_param_table(GWY_PLAIN_TOOL(tool), table);
    gtk_container_add(GTK_CONTAINER(options), gwy_param_table_widget(table));

    graph = gwy_graph_new(tool->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 2);

    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    gtk_dialog_add_button(dialog, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    g_signal_connect_swapped(tool->table, "param-changed", G_CALLBACK(param_changed), tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_spectro_data_switched(GwyTool *gwytool,
                               GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyToolSpectro *tool = GWY_TOOL_SPECTRO(gwytool);
    gboolean ignore = (data_view == plain_tool->data_view);

    if (plain_tool->init_failed)
        return;

    if (!ignore) {
        gwy_debug("disconect obj-chosen handler: %u", (guint)tool->layer_object_chosen_id);
        GWY_SIGNAL_HANDLER_DISCONNECT(plain_tool->layer, tool->layer_object_chosen_id);
    }

    GWY_TOOL_CLASS(gwy_tool_spectro_parent_class)->data_switched(gwytool, data_view);

    if (ignore)
        return;

    if (plain_tool->layer) {
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type,
                                "editable", FALSE,
                                "point-numbers", TRUE,
                                "focus", -1,
                                NULL);
    }
    if (data_view) {
        tool->layer_object_chosen_id = g_signal_connect(G_OBJECT(plain_tool->layer), "object-chosen",
                                                        G_CALLBACK(object_chosen), tool);
    }

    gwy_graph_model_remove_all_curves(tool->gmodel);
    if (plain_tool->data_field && tool->spectra) {
        gwy_selection_set_max_objects(plain_tool->selection, gwy_spectra_get_n_spectra(tool->spectra));
        fill_locations(tool);
    }
    gwy_param_table_data_id_refilter(tool->table, PARAM_TARGET_GRAPH);
}

static void
gwy_tool_spectro_spectra_switched(GwyTool *gwytool,
                                  GwySpectra *spectra)
{
    GwyToolSpectro *tool = GWY_TOOL_SPECTRO(gwytool);
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyNullStore *store = GWY_NULL_STORE(tool->model);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tool->treeview);
    GString *str;
    const gchar *spec_xlabel, *spec_ylabel;
    guint nspec, i;

    gwy_debug("spectra: %p", spectra);

    if (spectra) {
        if (!plain_tool->data_field)
            g_warning("Spectra made current without any channel?");
        else {
            if (!gwy_si_unit_equal(gwy_spectra_get_si_unit_xy(spectra),
                                   gwy_data_field_get_si_unit_xy(plain_tool->data_field))) {
                gwy_debug("Spectra and channel units do not match");
                spectra = NULL;
            }
        }
    }

    if (!spectra) {
        g_object_set(tool->gmodel,
                     "title", _("Spectroscopy"),
                     "axis-label-bottom", "x",
                     "axis-label-left", "y",
                     NULL);
        tool->ignore_tree_selection = TRUE;
        gwy_null_store_set_n_rows(store, 0);
        tool->ignore_tree_selection = FALSE;
        tree_selection_changed(selection, tool);
        GWY_OBJECT_UNREF(tool->spectra);
        if (plain_tool->selection)
            gwy_selection_clear(plain_tool->selection);
        return;
    }

    g_return_if_fail(GWY_IS_SPECTRA(spectra));
    g_object_ref(spectra);
    GWY_OBJECT_UNREF(tool->spectra);
    tool->spectra = spectra;

    g_object_set(tool->gmodel, "title", gwy_spectra_get_title(tool->spectra), NULL);

    if (!(spec_xlabel = gwy_spectra_get_spectrum_x_label(tool->spectra)))
        spec_xlabel = "x";
    gwy_graph_model_set_axis_label(tool->gmodel, GTK_POS_BOTTOM, spec_xlabel);

    if (!(spec_ylabel = gwy_spectra_get_spectrum_y_label(tool->spectra)))
        spec_ylabel = "y";
    gwy_graph_model_set_axis_label(tool->gmodel, GTK_POS_LEFT, spec_ylabel);

    nspec = gwy_spectra_get_n_spectra(spectra);
    gwy_selection_set_max_objects(plain_tool->selection, nspec);

    /* Prevent treeview selection updates in a for-cycle as the handler fully redraws the graph */
    tool->ignore_tree_selection = TRUE;

    /* Update point layer selection */
    gwy_selection_clear(plain_tool->selection);
    gwy_null_store_set_n_rows(store, 0);
    fill_locations(tool);
    gwy_null_store_set_n_rows(store, nspec);

    /* Update tree view selection */
    gtk_tree_selection_unselect_all(selection);
    for (i = 0; i < nspec; i++) {
        if (gwy_spectra_get_spectrum_selected(tool->spectra, i)) {
            GtkTreeIter iter;

            gtk_tree_model_iter_nth_child(tool->model, &iter, NULL, i);
            gtk_tree_selection_select_iter(selection, &iter);
            gwy_debug("selecting %u", i);
        }
    }

    /* Finally update the selection */
    tool->ignore_tree_selection = FALSE;
    tree_selection_changed(selection, tool);

    str = g_string_new(NULL);
    update_header(tool, COLUMN_X, str, "x", plain_tool->coord_format);
    update_header(tool, COLUMN_Y, str, "y", plain_tool->coord_format);
    g_string_free(str, TRUE);

    gwy_param_table_data_id_refilter(tool->table, PARAM_TARGET_GRAPH);
}

/* May be called either when the selection is empty or already filled but of correct size. */
static void
fill_locations(GwyToolSpectro *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    gdouble xoff, yoff;
    gdouble coords[2];
    gint i, nspec;

    g_return_if_fail(field);

    nspec = gwy_spectra_get_n_spectra(tool->spectra);
    xoff = gwy_data_field_get_xoffset(plain_tool->data_field);
    yoff = gwy_data_field_get_yoffset(plain_tool->data_field);
    for (i = 0; i < nspec; i++) {
        gwy_spectra_itoxy(tool->spectra, i, &coords[0], &coords[1]);
        coords[0] -= xoff;
        coords[1] -= yoff;
        gwy_selection_set_object(plain_tool->selection, i, coords);
    }
}

static void
gwy_tool_spectro_response(GwyTool *tool,
                          gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_spectro_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_spectro_apply(GWY_TOOL_SPECTRO(tool));
}

static void
tree_selection_changed(GtkTreeSelection *selection,
                       gpointer user_data)
{
    GwyToolSpectro *tool = (GwyToolSpectro*)user_data;
    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    gboolean average = gwy_params_get_boolean(tool->params, PARAM_AVERAGE);
    GtkTreeIter iter;
    guint i, n, nsel;

    gwy_debug("ignored: %d", tool->ignore_tree_selection);
    if (tool->ignore_tree_selection)
        return;

    /* FIXME: Inefficient */
    gwy_graph_model_remove_all_curves(tool->gmodel);
    n = gwy_null_store_get_n_rows(GWY_NULL_STORE(tool->model));
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, n > 0);
    if (!n)
        return;

    g_assert(tool->spectra);
    gtk_tree_model_get_iter_first(tool->model, &iter);
    for (i = nsel = 0; i < n; i++) {
        gboolean sel = gtk_tree_selection_iter_is_selected(selection, &iter);

        gwy_debug("i: %u selected: %d", i, sel);
        gwy_spectra_set_spectrum_selected(tool->spectra, i, sel);
        if (sel) {
            nsel++;
            if (average)
                gather_curve(tool, i);
            else
                show_curve(tool, i);
        }

        gtk_tree_model_iter_next(tool->model, &iter);
    }

    if (average && nsel)
        show_averaged(tool);
}

static void
object_chosen(G_GNUC_UNUSED GwyVectorLayer *gwyvectorlayer,
              gint i,
              gpointer *data)
{
    GwyToolSpectro *tool = (GwyToolSpectro*)data;
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    if (i < 0)
        return;
    gwy_debug("obj-chosen: %d", i);

    if (gtk_tree_model_iter_nth_child(tool->model, &iter, NULL, i)) {
        selection = gtk_tree_view_get_selection(tool->treeview);
        if (gtk_tree_selection_iter_is_selected(selection, &iter))
            gtk_tree_selection_unselect_iter(selection, &iter);
        else
            gtk_tree_selection_select_iter(selection, &iter);
    }
}

static void
show_curve(GwyToolSpectro *tool,
           gint id)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyGraphCurveModel *gcmodel = NULL;
    gint i, n;

    g_return_if_fail(plain_tool->selection);

    tool->line = gwy_spectra_get_spectrum(tool->spectra, id);
    n = gwy_graph_model_get_n_curves(tool->gmodel);

    /* FIXME: Not sure what this is supposed to do.  The graph model is always
     * cleared before this function is called. */
    for (i = 0; i < n; i++) {
        guint idx;

        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        idx = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(gcmodel), "sid"));
        if (idx == id)
            break;
        else
            gcmodel = NULL;
    }

    if (gcmodel) {
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
    }
    else {
        const GwyRGBA *rgba;
        gchar *desc;

        gcmodel = gwy_graph_curve_model_new();
        g_object_set_data(G_OBJECT(gcmodel), "sid", GUINT_TO_POINTER(id));
        desc = g_strdup_printf("%s %d", gwy_spectra_get_title(tool->spectra), id + 1);
        rgba = gwy_graph_get_preset_color(n);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", desc,
                     "color", rgba,
                     NULL);
        g_free(desc);
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
        gwy_graph_model_add_curve(tool->gmodel, gcmodel);
        g_object_unref(gcmodel);

        if (n == 0)
            gwy_graph_model_set_units_from_data_line(tool->gmodel, tool->line);
    }
    tool->line = NULL;
}

static void
gather_curve(GwyToolSpectro *tool,
             gint id)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataLine *spectrum = gwy_spectra_get_spectrum(tool->spectra, id);

    g_return_if_fail(plain_tool->selection);
    if (!tool->line) {
        /* Use the first selected line as the template. */
        tool->line = gwy_data_line_duplicate(spectrum);
        tool->weights = gwy_data_line_new_alike(spectrum, TRUE);
        gwy_data_line_fill(tool->weights, 1.0);
    }
    else {
        /* Accumulate the others. */
        g_return_if_fail(tool->line);
        g_return_if_fail(tool->weights);
        accumulate_lines(tool->line, spectrum, tool->weights);
    }
}

static void
accumulate_lines(GwyDataLine *accum, GwyDataLine *dline, GwyDataLine *weights)
{
    gint i, n, n1;
    gdouble *adata, *data, *wdata;
    gdouble real, off, real1, off1;

    n = gwy_data_line_get_res(accum);
    n1 = gwy_data_line_get_res(dline);
    data = gwy_data_line_get_data(dline);
    adata = gwy_data_line_get_data(accum);
    real = gwy_data_line_get_real(accum);
    real1 = gwy_data_line_get_real(dline);
    off = gwy_data_line_get_offset(accum);
    off1 = gwy_data_line_get_offset(dline);

    if (n1 == n
        && fabs(real1 - real) <= 1e-9*(fabs(real1) + fabs(real))
        && fabs(off1 - off) <= 1e-9*(fabs(off1) + fabs(off))) {
        for (i = 0; i < n1; i++)
            adata[i] += data[i];
        gwy_data_line_add(weights, 1.0);
        return;
    }

    if (off1 >= real + off || off >= real1 + off1)
        return;

    /* This is not very good but better than a CRTICIAL message we used to do here... */
    wdata = gwy_data_line_get_data(weights);
    for (i = 0; i < n; i++) {
        gdouble x = (i + 0.5)*real/n + off;
        gint j = floor((x - off1)/real1*n1);
        if (j >= 0 && j+1 < n1) {
            adata[i] += data[j];
            wdata[i] += 1.0;
        }
    }
}

static void
show_averaged(GwyToolSpectro *tool)
{
    GwyGraphCurveModel *gcmodel;
    const GwyRGBA *rgba;
    gdouble *adata, *wdata;
    gint i, n;

    gcmodel = gwy_graph_curve_model_new();
    rgba = gwy_graph_get_preset_color(0);
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", gwy_spectra_get_title(tool->spectra),
                 "color", rgba,
                 NULL);
    n = gwy_data_line_get_res(tool->line);
    adata = gwy_data_line_get_data(tool->line);
    wdata = gwy_data_line_get_data(tool->weights);
    for (i = 0; i < n; i++)
        adata[i] /= wdata[i];
    gwy_graph_curve_model_set_data_from_dataline(gcmodel, tool->line, 0, 0);
    gwy_graph_model_add_curve(tool->gmodel, gcmodel);
    g_object_unref(gcmodel);

    gwy_graph_model_set_units_from_data_line(tool->gmodel, tool->line);
    GWY_OBJECT_UNREF(tool->line);
    GWY_OBJECT_UNREF(tool->weights);
}

static void
update_header(GwyToolSpectro *tool,
              guint col,
              GString *str,
              const gchar *title,
              GwySIValueFormat *vf)
{
    GtkTreeViewColumn *column;
    GtkLabel *label;

    column = gtk_tree_view_get_column(tool->treeview, col);
    label = GTK_LABEL(gtk_tree_view_column_get_widget(column));

    g_string_assign(str, "<b>");
    g_string_append(str, title);
    g_string_append(str, "</b>");
    if (vf)
        g_string_append_printf(str, " [%s]", vf->units);
    gtk_label_set_markup(label, str->str);
}

static void
render_cell(GtkCellLayout *layout,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    GwyToolSpectro *tool = (GwyToolSpectro*)user_data;
    const GwySIValueFormat *vf = GWY_PLAIN_TOOL(tool)->coord_format;
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gchar buf[48];
    gdouble val;
    guint idx;

    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (id == COLUMN_I) {
        g_snprintf(buf, sizeof(buf), "%d", idx + 1);
        g_object_set(renderer, "text", buf, NULL);
        return;
    }

    if (id == COLUMN_X)
        gwy_spectra_itoxy(tool->spectra, idx, &val, NULL);
    else if (id == COLUMN_Y)
        gwy_spectra_itoxy(tool->spectra, idx, NULL, &val);
    else {
        g_return_if_reached();
    }

    if (vf)
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, val/vf->magnitude);
    else
        g_snprintf(buf, sizeof(buf), "%.3g", val);

    g_object_set(renderer, "text", buf, NULL);
}

static void
render_colour(G_GNUC_UNUSED GtkCellLayout *layout,
              G_GNUC_UNUSED GtkCellRenderer *renderer,
              GtkTreeModel *model,
              GtkTreeIter *iter,
              gpointer user_data)
{
    GwyToolSpectro *tool = (GwyToolSpectro*)user_data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tool->treeview);
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyRGBA *rgba;
    guint idx, pixel, n, i;

    pixel = 0;
    if (gtk_tree_selection_iter_is_selected(sel, iter)) {
        gmodel = tool->gmodel;
        gtk_tree_model_get(model, iter, 0, &idx, -1);
        n = gwy_graph_model_get_n_curves(gmodel);
        for (i = 0; i < n; i++) {
            gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
            if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(gcmodel), "sid")) == idx) {
                g_object_get(gcmodel, "color", &rgba, NULL);
                pixel = 0xff | gwy_rgba_to_pixbuf_pixel(rgba);
                gwy_rgba_free(rgba);
                break;
            }
        }
    }
    gdk_pixbuf_fill(tool->colorpixbuf, pixel);
}

static void
param_changed(GwyToolSpectro *tool, gint id)
{
    GwyParams *params = tool->params;

    if (id < 0 || id == PARAM_SEPARATE) {
        gboolean separate = gwy_params_get_boolean(params, PARAM_SEPARATE);
        GwyAppDataId dataid = GWY_APP_DATA_ID_NONE;

        gwy_param_table_set_sensitive(tool->table, PARAM_TARGET_GRAPH, !separate);
        if (!separate)
            gwy_param_table_set_data_id(tool->table, PARAM_TARGET_GRAPH, dataid);
    }
    if (id == PARAM_AVERAGE)
        tree_selection_changed(gtk_tree_view_get_selection(tool->treeview), tool);
}

static void
gwy_tool_spectro_apply(GwyToolSpectro *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    gboolean separate = gwy_params_get_boolean(tool->params, PARAM_SEPARATE);
    gboolean average = gwy_params_get_boolean(tool->params, PARAM_AVERAGE);
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    gchar *s;
    gint i, n;

    plain_tool = GWY_PLAIN_TOOL(tool);
    g_return_if_fail(plain_tool->selection);
    n = gwy_graph_model_get_n_curves(tool->gmodel);
    g_return_if_fail(n);

    if ((gmodel = gwy_params_get_graph(tool->params, PARAM_TARGET_GRAPH))) {
        gwy_graph_model_append_curves(gmodel, tool->gmodel, 1);
        return;
    }

    if (average || !separate) {
        gmodel = gwy_graph_model_duplicate(tool->gmodel);
        g_object_set(gmodel, "label-visible", TRUE, NULL);
        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container, TRUE);
        g_object_unref(gmodel);
        return;
    }

    for (i = 0; i < n; i++) {
        gmodel = gwy_graph_model_new_alike(tool->gmodel);
        g_object_set(gmodel, "label-visible", TRUE, NULL);
        gcmodel = gwy_graph_model_get_curve(tool->gmodel, i);
        gcmodel = gwy_graph_curve_model_duplicate(gcmodel);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        g_object_get(gcmodel, "description", &s, NULL);
        g_object_set(gmodel, "title", s, NULL);
        g_free(s);
        gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container, TRUE);
        g_object_unref(gmodel);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
