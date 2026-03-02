/*
 *  $Id: pathlevel.c 24935 2022-08-24 15:43:45Z yeti-dn $
 *  Copyright (C) 2007-2022 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/datafield.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwynullstore.h>
#include <app/gwyapp.h>

enum {
    COLUMN_I, COLUMN_X0, COLUMN_Y0, COLUMN_X1, COLUMN_Y1, NCOLUMNS
};

enum {
    PARAM_THICKNESS,
};

/* Line start or end. */
typedef struct {
    gint row;
    gint id;
    gboolean end;
} ChangePoint;

#define GWY_TYPE_TOOL_PATH_LEVEL            (gwy_tool_path_level_get_type())
#define GWY_TOOL_PATH_LEVEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_PATH_LEVEL, GwyToolPathLevel))
#define GWY_IS_TOOL_PATH_LEVEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_PATH_LEVEL))
#define GWY_TOOL_PATH_LEVEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_PATH_LEVEL, GwyToolPathLevelClass))

typedef struct _GwyToolPathLevel      GwyToolPathLevel;
typedef struct _GwyToolPathLevelClass GwyToolPathLevelClass;

struct _GwyToolPathLevel {
    GwyPlainTool parent_instance;

    GwyParams *params;

    GtkTreeView *treeview;
    GtkTreeModel *model;

    GwyParamTable *table;

    /* potential class data */
    GType layer_type_line;
};

struct _GwyToolPathLevelClass {
    GwyPlainToolClass parent_class;
};

static gboolean     module_register                      (void);
static GwyParamDef* define_module_params                 (void);
static GType        gwy_tool_path_level_get_type         (void)                      G_GNUC_CONST;
static void         gwy_tool_path_level_finalize         (GObject *object);
static void         gwy_tool_path_level_init_dialog      (GwyToolPathLevel *tool);
static void         gwy_tool_path_level_data_switched    (GwyTool *gwytool,
                                                          GwyDataView *data_view);
static void         gwy_tool_path_level_response         (GwyTool *gwytool,
                                                          gint response_id);
static void         gwy_tool_path_level_selection_changed(GwyPlainTool *plain_tool,
                                                          gint hint);
static void         gwy_tool_path_level_apply            (GwyToolPathLevel *tool);
static void         param_changed                        (GwyToolPathLevel *tool,
                                                          gint id);
static void         render_cell                          (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void         sel_to_isel                          (GwyToolPathLevel *tool,
                                                          gint i,
                                                          gint *isel);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Path level tool, performs row leveling along on user-set lines."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2007",
};

GWY_MODULE_QUERY2(module_info, pathlevel)

G_DEFINE_TYPE(GwyToolPathLevel, gwy_tool_path_level, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_PATH_LEVEL);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "pathlevel");
    gwy_param_def_add_int(paramdef, PARAM_THICKNESS, "thickness", _("_Thickness"), 1, 128, 1);

    return paramdef;
}

static void
gwy_tool_path_level_class_init(GwyToolPathLevelClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_path_level_finalize;

    tool_class->stock_id = GWY_STOCK_PATH_LEVEL;
    tool_class->title = _("Path Level");
    tool_class->tooltip = _("Level rows using intersections with given lines");
    tool_class->prefix = "/module/pathlevel";
    tool_class->default_height = 240;
    tool_class->data_switched = gwy_tool_path_level_data_switched;
    tool_class->response = gwy_tool_path_level_response;

    ptool_class->selection_changed = gwy_tool_path_level_selection_changed;
}

static void
gwy_tool_path_level_finalize(GObject *object)
{
    GwyToolPathLevel *tool = GWY_TOOL_PATH_LEVEL(object);

    gwy_params_save_to_settings(tool->params);
    GWY_OBJECT_UNREF(tool->params);
    if (tool->model) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        GWY_OBJECT_UNREF(tool->model);
    }

    G_OBJECT_CLASS(gwy_tool_path_level_parent_class)->finalize(object);
}

static void
gwy_tool_path_level_init(GwyToolPathLevel *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->layer_type_line = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerLine");
    if (!tool->layer_type_line)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    tool->params = gwy_params_new_from_settings(define_module_params());

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_line, "line");

    gwy_tool_path_level_init_dialog(tool);
}

static void
gwy_tool_path_level_init_dialog(GwyToolPathLevel *tool)
{
    static const gchar *lcolumns[] = {
        "<b>n</b>", "<b>x<sub>1</sub></b>", "<b>y<sub>1</sub></b>", "<b>x<sub>2</sub></b>", "<b>y<sub>2</sub></b>",
    };
    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *scwin, *label;
    GwyParamTable *table;
    GwyNullStore *store;
    guint i;

    store = gwy_null_store_new(0);
    tool->model = GTK_TREE_MODEL(store);
    tool->treeview = GTK_TREE_VIEW(gtk_tree_view_new_with_model(tool->model));
    gwy_plain_tool_enable_object_deletion(GWY_PLAIN_TOOL(tool), tool->treeview);

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, render_cell, tool, NULL);
        label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), lcolumns[i]);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(tool->treeview, column);
    }

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), GTK_WIDGET(tool->treeview));
    gtk_box_pack_start(GTK_BOX(dialog->vbox), scwin, TRUE, TRUE, 0);

    table = tool->table = gwy_param_table_new(tool->params);
    gwy_param_table_append_slider(table, PARAM_THICKNESS);
    gwy_param_table_set_unitstr(table, PARAM_THICKNESS, _("px"));
    gwy_plain_tool_add_param_table(GWY_PLAIN_TOOL(tool), table);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), TRUE);
    gtk_dialog_add_button(dialog, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_NO_BUTTON);

    g_signal_connect_swapped(tool->table, "param-changed", G_CALLBACK(param_changed), tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_path_level_data_switched(GwyTool *gwytool,
                                  GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyToolPathLevel *tool = GWY_TOOL_PATH_LEVEL(gwytool);
    gboolean ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_path_level_parent_class)->data_switched(gwytool, data_view);

    if (ignore || plain_tool->init_failed)
        return;

    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type_line,
                                "thickness", gwy_params_get_int(tool->params, PARAM_THICKNESS),
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1024);
    }
}

static void
gwy_tool_path_level_response(GwyTool *gwytool,
                             gint response_id)
{
    GwyToolPathLevel *tool = GWY_TOOL_PATH_LEVEL(gwytool);

    GWY_TOOL_CLASS(gwy_tool_path_level_parent_class)->response(gwytool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_path_level_apply(tool);
}

static void
gwy_tool_path_level_selection_changed(GwyPlainTool *plain_tool,
                                      gint hint)
{
    GwyToolPathLevel *tool = GWY_TOOL_PATH_LEVEL(plain_tool);
    GwyNullStore *store = GWY_NULL_STORE(tool->model);
    gint n = gwy_null_store_get_n_rows(store);

    g_return_if_fail(hint <= n);

    if (hint < 0) {
        gtk_tree_view_set_model(tool->treeview, NULL);
        n = (plain_tool->selection ? gwy_selection_get_data(plain_tool->selection, NULL) : 0);
        gwy_null_store_set_n_rows(store, n);
        gtk_tree_view_set_model(tool->treeview, tool->model);
    }
    else {
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);

        gtk_tree_model_iter_nth_child(tool->model, &iter, NULL, hint);
        path = gtk_tree_model_get_path(tool->model, &iter);
        selection = gtk_tree_view_get_selection(tool->treeview);
        gtk_tree_selection_select_iter(selection, &iter);
        gtk_tree_view_scroll_to_cell(tool->treeview, path, NULL, FALSE, 0.0, 0.0);
    }

    gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GTK_RESPONSE_APPLY,
                                      !!gwy_null_store_get_n_rows(store));
}

static void
param_changed(GwyToolPathLevel *tool, gint id)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    if (id < 0 || id == PARAM_THICKNESS) {
        if (plain_tool->layer)
            g_object_set(plain_tool->layer, "thickness", gwy_params_get_int(tool->params, PARAM_THICKNESS), NULL);
    }
}

static void
render_cell(GtkCellLayout *layout,
            GtkCellRenderer *renderer,
            GtkTreeModel *model,
            GtkTreeIter *iter,
            gpointer user_data)
{
    GwyToolPathLevel *tool = (GwyToolPathLevel*)user_data;
    gint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gchar buf[16];
    gint isel[4];
    guint idx;

    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (id == COLUMN_I) {
        g_snprintf(buf, sizeof(buf), "%d", idx + 1);
        g_object_set(renderer, "text", buf, NULL);
        return;
    }
    g_return_if_fail(id >= COLUMN_X0 && id < NCOLUMNS);

    sel_to_isel(tool, idx, isel);
    g_snprintf(buf, sizeof(buf), "%d", isel[id-COLUMN_X0]);
    g_object_set(renderer, "text", buf, NULL);
}

static gint
change_point_compare(gconstpointer a, gconstpointer b)
{
    const ChangePoint *pa = (const ChangePoint*)a;
    const ChangePoint *pb = (const ChangePoint*)b;

    if (pa->row < pb->row)
        return -1;
    if (pa->row > pb->row)
        return 1;

    if (pa->end < pb->end)
        return -1;
    if (pa->end > pb->end)
        return 1;

    if (pa->id < pb->id)
        return -1;
    if (pa->id > pb->id)
        return 1;

    g_return_val_if_reached(0);
}

static void
gwy_tool_path_level_apply(GwyToolPathLevel *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    gint thickness = gwy_params_get_int(tool->params, PARAM_THICKNESS);
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    gint n = gwy_selection_get_data(plain_tool->selection, NULL);
    ChangePoint *cpts;
    GwyDataLine *corr;
    gint row, i, j, nw, tp, tn;
    gboolean *wset;
    gdouble *cd, *d;
    gdouble s;
    gint *isel;

    gwy_app_undo_qcheckpoint(plain_tool->container, gwy_app_get_data_key_for_id(plain_tool->id), 0);

    cpts = g_new(ChangePoint, 2*n);
    isel = g_new(gint, 4*n);
    for (i = 0; i < n; i++) {
        sel_to_isel(tool, i, isel + 4*i);
        cpts[2*i].row = isel[4*i + 1];
        cpts[2*i].id = i;
        cpts[2*i].end = FALSE;
        cpts[2*i + 1].row = isel[4*i + 3];
        cpts[2*i + 1].id = i;
        cpts[2*i + 1].end = TRUE;
    }

    qsort(cpts, 2*n, sizeof(ChangePoint), change_point_compare);
    wset = g_new0(gboolean, n);
    corr = gwy_data_line_new(yres, 1.0, TRUE);
    cd = gwy_data_line_get_data(corr);
    d = gwy_data_field_get_data(field);
    tp = (thickness - 1)/2;
    tn = thickness/2;
    i = 0;
    for (row = 0; row < yres; row++) {
        /* Lines participating on this row leveling are in wset now: they intersect this and the previous row */
        if (row) {
            s = 0.0;
            nw = 0;
            for (j = 0; j < n; j++) {
                if (wset[j]) {
                    gint p = isel[4*j + 2] - isel[4*j + 0];
                    gint q = isel[4*j + 3] - isel[4*j + 1];
                    gint sg = q > 0 ? 1 : -1;
                    gint x = ((2*(row - isel[4*j + 1]) + 1)*p + sg*q)/(2*sg*q) + isel[4*j + 0];
                    gint k;

                    for (k = MAX(0, x - tp); k <= MIN(xres-1, x + tn); k++) {
                        s += d[xres*row + k] - d[xres*(row - 1) + k];
                        nw++;
                    }
                }
            }
            if (nw) {
                s /= nw;
                cd[row] = s;
            }
        }

        /* Update working set.  Sort puts starts before ends, therefore horizontal lines are effectively ignored --
         * which is the correct behaviour */
        while (i < 2*n && row == cpts[i].row) {
            if (cpts[i].end) {
                gwy_debug("row %d, removing %d from wset", row, cpts[i].id);
                g_assert(wset[cpts[i].id]);
                wset[cpts[i].id] = FALSE;
            }
            else {
                gwy_debug("row %d, adding %d to wset", row, cpts[i].id);
                g_assert(!wset[cpts[i].id]);
                wset[cpts[i].id] = TRUE;
            }
            i++;
        }
    }
    g_free(wset);
    g_free(cpts);
    g_free(isel);

    gwy_data_line_cumulate(corr);
    for (row = 0; row < yres; row++) {
        s = cd[row];
        for (j = 0; j < xres; j++)
            d[row*xres + j] -= s;
    }
    g_object_unref(corr);

    gwy_data_field_data_changed(field);
    gwy_params_save_to_settings(tool->params);   /* Ensure correct parameters in the log. */
    gwy_plain_tool_log_add(plain_tool);
}

static void
sel_to_isel(GwyToolPathLevel *tool, gint i, gint *isel)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    gint xres = gwy_data_field_get_xres(field);
    gint yres = gwy_data_field_get_yres(field);
    gdouble sel[4];

    gwy_selection_get_object(plain_tool->selection, i, sel);

    sel[0] = floor(gwy_data_field_rtoj(field, sel[0]));
    sel[1] = floor(gwy_data_field_rtoi(field, sel[1]));
    sel[2] = floor(gwy_data_field_rtoj(field, sel[2]));
    sel[3] = floor(gwy_data_field_rtoi(field, sel[3]));
    if (sel[1] > sel[3]) {
        GWY_SWAP(gdouble, sel[0], sel[2]);
        GWY_SWAP(gdouble, sel[1], sel[3]);
    }
    isel[0] = CLAMP((gint)sel[0], 0, xres-1);
    isel[1] = CLAMP(floor(sel[1]), 0, yres-1);
    isel[2] = CLAMP((gint)sel[2], 0, xres-1);
    isel[3] = CLAMP(ceil(sel[3]), 0, yres-1);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
