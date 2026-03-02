/*
 *  $Id: straighten_path.c 24626 2022-03-03 12:59:05Z yeti-dn $
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
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correct.h>
#include <libprocess/stats.h>
#include <libprocess/interpolation.h>
#include <libprocess/spline.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    COLUMN_I, COLUMN_X, COLUMN_Y, NCOLUMNS
};

enum {
    PARAM_CLOSED,
    PARAM_INTERP,
    PARAM_ORIENTATION,
    PARAM_SLACKNESS,
    PARAM_THICKNESS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *result_mask;
    GwySelection *selection;
    /* Cached values for input data field. */
    gboolean realsquare;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkWidget *coordlist;
    GtkWidget *view;
    GtkWidget *view_result;
    GwySelection *selection;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register        (void);
static GwyParamDef*     define_module_params   (void);
static void             straighten_path        (GwyContainer *data,
                                                GwyRunType runtype);
static void             execute                (ModuleArgs *args,
                                                GwySelection *selection);
static GwyDialogOutcome run_gui                (ModuleArgs *args,
                                                GwyContainer *data,
                                                gint id);
static void             param_changed          (ModuleGUI *gui,
                                                gint id);
static void             preview                (gpointer user_data);
static void             reset_path             (ModuleGUI *gui);
static void             restore_path           (ModuleGUI *gui);
static void             reverse_path           (ModuleGUI *gui);
static void             set_scaled_thickness   (ModuleGUI *gui);
static void             init_selection         (GwySelection *selection,
                                                ModuleArgs *args);
static void             selection_changed      (ModuleGUI *gui,
                                                gint hint);
static void             fill_coord_list        (ModuleGUI *gui);
static GtkWidget*       create_coord_list      (ModuleGUI *gui);
static void             render_coord_cell      (GtkCellLayout *layout,
                                                GtkCellRenderer *renderer,
                                                GtkTreeModel *model,
                                                GtkTreeIter *iter,
                                                gpointer user_data);
static gboolean         delete_selection_object(GtkTreeView *treeview,
                                                GdkEventKey *event,
                                                ModuleGUI *gui);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extracts a straightened part of image along a curve."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, straighten_path)

static gboolean
module_register(void)
{
    gwy_process_func_register("straighten_path",
                              (GwyProcessFunc)&straighten_path,
                              N_("/_Distortion/Straighten _Path..."),
                              GWY_STOCK_STRAIGHTEN_PATH,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Straighten along a path"));

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
    gwy_param_def_add_boolean(paramdef, PARAM_CLOSED, "closed", _("C_losed curve"), FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_enum(paramdef, PARAM_ORIENTATION, "orientation", _("Out_put orientation"), GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_VERTICAL);
    gwy_param_def_add_double(paramdef, PARAM_SLACKNESS, "slackness", _("_Slackness"), 0.0, G_SQRT2, 1.0/G_SQRT2);
    gwy_param_def_add_int(paramdef, PARAM_THICKNESS, "thickness", _("_Thickness"), 3, 16384, 20);
    return paramdef;
}

static void
straighten_path(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyParams *params;
    GwyDataField *field, *tmp;
    gint id, newid, yres;
    GwySelection *selection;
    gboolean closed;
    gdouble slackness;
    gchar key[40];

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPath"));
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(field);

    gwy_clear(&args, 1);
    args.field = field;
    args.params = params = gwy_params_new_from_settings(define_module_params());
    g_snprintf(key, sizeof(key), "/%d/data/realsquare", id);
    gwy_container_gis_boolean_by_name(data, key, &args.realsquare);
    yres = gwy_data_field_get_yres(field);
    args.result = gwy_data_field_new(5, yres, 5, yres, TRUE);
    args.result_mask = gwy_data_field_new_alike(args.result, TRUE);

    g_snprintf(key, sizeof(key), "/%d/select/path", id);
    if (gwy_container_gis_object_by_name(data, key, &selection)
        && gwy_selection_get_data(GWY_SELECTION(selection), NULL) > 1) {
        gwy_debug("init selection from container");
        args.selection = gwy_selection_duplicate(selection);
        gwy_selection_set_max_objects(args.selection, 1024);
        g_object_get(selection, "slackness", &slackness, "closed", &closed, NULL);
        gwy_params_set_double(params, PARAM_SLACKNESS, slackness);
        gwy_params_set_boolean(params, PARAM_CLOSED, closed);
    }
    else {
        gwy_debug("init selection afresh");
        args.selection = g_object_new(g_type_from_name("GwySelectionPath"), NULL);
        gwy_selection_set_max_objects(args.selection, 1024);
        init_selection(args.selection, &args);
    }

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(params);
    gwy_container_set_object_by_name(data, key, args.selection);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args, args.selection);

    if (gwy_params_get_enum(params, PARAM_ORIENTATION) == GWY_ORIENTATION_HORIZONTAL) {
        tmp = gwy_data_field_new_rotated_90(args.result, FALSE);
        g_object_unref(args.result);
        args.result = tmp;
    }

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("Straightened"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    if (gwy_data_field_get_max(args.result_mask) > 0.0)
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), args.result_mask);
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.selection);
    g_object_unref(args.result);
    g_object_unref(args.result_mask);
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *vbox, *alignment, *buttonbox, *button;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    GwyDataField *field = args->field;
    GwyDialogOutcome outcome;
    gint maxthickness, thickness;

    maxthickness = MAX(gwy_data_field_get_xres(field), gwy_data_field_get_yres(field))/2;
    maxthickness = MAX(maxthickness, 3);
    thickness = gwy_params_get_int(args->params, PARAM_THICKNESS);
    if (thickness > maxthickness)
        gwy_params_set_int(args->params, PARAM_THICKNESS, (thickness = maxthickness));

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(1), args->result);
    gwy_container_set_object(gui.data, gwy_app_get_mask_key_for_id(1), args->result_mask);
    gwy_app_sync_data_items(data, gui.data, id, 1, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);

    gui.dialog = gwy_dialog_new(_("Straighten Path"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), create_coord_list(&gui), TRUE, TRUE, 0);
    buttonbox = gwy_hbox_new(0);
    gtk_box_set_homogeneous(GTK_BOX(buttonbox), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), buttonbox, FALSE, FALSE, 0);

    button = gtk_button_new_with_mnemonic(_("_Reset"));
    gtk_box_pack_start(GTK_BOX(buttonbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(reset_path), &gui);

    button = gtk_button_new_with_mnemonic(_("Res_tore"));
    gtk_box_pack_start(GTK_BOX(buttonbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(restore_path), &gui);

    button = gtk_button_new_with_mnemonic(_("Re_verse"));
    gtk_box_pack_start(GTK_BOX(buttonbox), button, TRUE, TRUE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(reverse_path), &gui);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_append_combo(table, PARAM_ORIENTATION);
    gwy_param_table_append_slider(table, PARAM_THICKNESS);
    gwy_param_table_slider_restrict_range(table, PARAM_THICKNESS, 3, maxthickness);
    gwy_param_table_set_unitstr(table, PARAM_THICKNESS, _("px"));
    gwy_param_table_append_slider(table, PARAM_SLACKNESS);
    gwy_param_table_slider_set_digits(table, PARAM_SLACKNESS, 3);
    gwy_param_table_slider_set_mapping(table, PARAM_SLACKNESS, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_CLOSED);

    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 4);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.view), 0, "Path", 1024, TRUE);
    g_object_ref(gui.selection);
    gwy_selection_assign(gui.selection, args->selection);
    gtk_container_add(GTK_CONTAINER(alignment), gui.view);

    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 4);

    gui.view_result = gwy_create_preview(gui.data, 1, PREVIEW_SIZE, TRUE);
    gtk_container_add(GTK_CONTAINER(alignment), gui.view_result);

    fill_coord_list(&gui);
    /* We do not get the right value before the data view is shown. */
    g_signal_connect_swapped(gui.view, "map", G_CALLBACK(set_scaled_thickness), &gui);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(selection_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    gwy_selection_assign(args->selection, gui.selection);
    g_object_unref(gui.selection);
    g_object_unref(gui.data);

    return outcome;
}

static void
reset_path(ModuleGUI *gui)
{
    init_selection(gui->selection, gui->args);
}

static void
restore_path(ModuleGUI *gui)
{
    gwy_selection_assign(gui->selection, gui->args->selection);
}

static void
reverse_path(ModuleGUI *gui)
{
    guint i, n = gwy_selection_get_data(gui->selection, NULL);
    gdouble *xy = g_new(gdouble, 2*n);

    gwy_selection_get_data(gui->selection, xy);
    for (i = 0; i < n/2; i++) {
        GWY_SWAP(gdouble, xy[2*i], xy[2*(n-1 - i)]);
        GWY_SWAP(gdouble, xy[2*i + 1], xy[2*(n-1 - i) + 1]);
    }
    gwy_selection_set_data(gui->selection, n, xy);
    g_free(xy);
}

static void
set_scaled_thickness(ModuleGUI *gui)
{
    gint thickness = gwy_params_get_int(gui->args->params, PARAM_THICKNESS);
    gdouble zoom = gwy_data_view_get_real_zoom(GWY_DATA_VIEW(gui->view));
    GwyVectorLayer *vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui->view));
    g_object_set(vlayer, "thickness", GWY_ROUND(zoom*thickness), NULL);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args, gui->selection);
    gwy_data_field_data_changed(args->result);
    gwy_data_field_data_changed(args->result_mask);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view_result), PREVIEW_SIZE);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static GtkWidget*
create_coord_list(ModuleGUI *gui)
{
    static const gchar *column_labels[] = { "n", "x", "y" };

    GwyNullStore *store;
    GtkTreeView *treeview;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkWidget *label, *scwin;
    guint i;

    store = gwy_null_store_new(0);
    gui->coordlist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    treeview = GTK_TREE_VIEW(gui->coordlist);
    g_signal_connect(treeview, "key-press-event", G_CALLBACK(delete_selection_object), gui);

    for (i = 0; i < NCOLUMNS; i++) {
        column = gtk_tree_view_column_new();
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_column_set_alignment(column, 0.5);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "xalign", 1.0, NULL);
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
        gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, render_coord_cell, gui, NULL);
        label = gtk_label_new(column_labels[i]);
        gtk_tree_view_column_set_widget(column, label);
        gtk_widget_show(label);
        gtk_tree_view_append_column(treeview, column);
    }

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), gui->coordlist);

    return scwin;
}

static void
render_coord_cell(GtkCellLayout *layout,
                  GtkCellRenderer *renderer,
                  GtkTreeModel *model,
                  GtkTreeIter *iter,
                  gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwyDataField *field = gui->args->field;
    gchar buf[32];
    guint idx, id;
    gint ival;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &idx, -1);
    if (id == COLUMN_I)
        ival = idx+1;
    else {
        gdouble xy[2];

        gwy_selection_get_object(gui->selection, idx, xy);
        if (id == COLUMN_X)
            ival = gwy_data_field_rtoj(field, xy[0]);
        else
            ival = gwy_data_field_rtoi(field, xy[1]);
    }

    g_snprintf(buf, sizeof(buf), "%d", ival);
    g_object_set(renderer, "text", buf, NULL);
}

static gboolean
delete_selection_object(GtkTreeView *treeview,
                        GdkEventKey *event,
                        ModuleGUI *gui)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;
    const gint *indices;

    if (event->keyval != GDK_Delete)
        return FALSE;

    selection = gtk_tree_view_get_selection(treeview);
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return FALSE;

    /* Do not permit reduction to a single point. */
    if (gwy_selection_get_data(gui->selection, NULL) < 3)
        return FALSE;

    path = gtk_tree_model_get_path(model, &iter);
    indices = gtk_tree_path_get_indices(path);
    gwy_selection_delete_object(gui->selection, indices[0]);
    gtk_tree_path_free(path);

    return TRUE;
}

static void
init_selection(GwySelection *selection, ModuleArgs *args)
{
    gdouble xreal = gwy_data_field_get_xreal(args->field);
    gdouble yreal = gwy_data_field_get_yreal(args->field);
    gboolean closed = gwy_params_get_boolean(args->params, PARAM_CLOSED);
    gdouble xy[8];

    if (closed) {
        xy[0] = 0.75*xreal;
        xy[1] = xy[5] = 0.5*yreal;
        xy[2] = xy[6] = 0.5*xreal;
        xy[3] = 0.25*yreal;
        xy[4] = 0.25*xreal;
        xy[7] = 0.75*yreal;
        gwy_selection_set_data(selection, 4, xy);
    }
    else {
        xy[0] = xy[2] = xy[4] = 0.5*xreal;
        xy[1] = 0.2*yreal;
        xy[3] = 0.5*yreal;
        xy[5] = 0.8*yreal;
        gwy_selection_set_data(selection, 3, xy);
    }

    g_object_set(selection,
                 "slackness", gwy_params_get_double(args->params, PARAM_SLACKNESS),
                 "closed", closed,
                 NULL);
}

static void
selection_changed(ModuleGUI *gui, gint hint)
{
    GtkTreeView *treeview = GTK_TREE_VIEW(gui->coordlist);
    GtkTreeModel *model = gtk_tree_view_get_model(treeview);
    GwyNullStore *store = GWY_NULL_STORE(model);

    if (hint < 0)
        fill_coord_list(gui);
    else {
        gint n = gwy_null_store_get_n_rows(store);
        GtkTreeSelection *selection;
        GtkTreePath *path;
        GtkTreeIter iter;

        g_return_if_fail(hint <= n);
        if (hint < n)
            gwy_null_store_row_changed(store, hint);
        else
            gwy_null_store_set_n_rows(store, n+1);

        gtk_tree_model_iter_nth_child(model, &iter, NULL, hint);
        path = gtk_tree_model_get_path(model, &iter);
        selection = gtk_tree_view_get_selection(treeview);
        gtk_tree_selection_select_iter(selection, &iter);
        gtk_tree_view_scroll_to_cell(treeview, path, NULL, FALSE, 0.0, 0.0);
        gtk_tree_path_free(path);
    }
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
fill_coord_list(ModuleGUI *gui)
{
    GtkTreeView *treeview = GTK_TREE_VIEW(gui->coordlist);
    GtkTreeModel *model = gtk_tree_view_get_model(treeview);
    GwyNullStore *store = GWY_NULL_STORE(model);
    gint n;

    g_object_ref(model);
    gtk_tree_view_set_model(treeview, NULL);
    n = gwy_selection_get_data(gui->selection, NULL);
    gwy_null_store_set_n_rows(store, n);
    gtk_tree_view_set_model(treeview, model);
    g_object_unref(model);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_CLOSED)
        g_object_set(gui->selection, "closed", gwy_params_get_boolean(params, PARAM_CLOSED), NULL);
    if (id < 0 || id == PARAM_SLACKNESS)
        g_object_set(gui->selection, "slackness", gwy_params_get_double(params, PARAM_SLACKNESS), NULL);
    if (id < 0 || id == PARAM_THICKNESS)
        set_scaled_thickness(gui);

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

/* XXX: This replicates extract_path.c */
static GwyXY*
rescale_points(GwySelection *selection, GwyDataField *field,
               gboolean realsquare,
               gdouble *pdx, gdouble *pdy, gdouble *pqx, gdouble *pqy)
{
    gdouble dx, dy, qx, qy, h;
    GwyXY *points;
    guint n, i;

    dx = gwy_data_field_get_dx(field);
    dy = gwy_data_field_get_dy(field);
    h = MIN(dx, dy);
    if (realsquare) {
        qx = h/dx;
        qy = h/dy;
        dx = dy = h;
    }
    else
        qx = qy = 1.0;

    n = gwy_selection_get_data(selection, NULL);
    points = g_new(GwyXY, n);
    for (i = 0; i < n; i++) {
        gdouble xy[2];

        gwy_selection_get_object(selection, i, xy);
        points[i].x = xy[0]/dx;
        points[i].y = xy[1]/dy;
    }

    *pdx = dx;
    *pdy = dy;
    *pqx = qx;
    *pqy = qy;
    return points;
}

static void
resize_result_field(GwyDataField *result, gint n, gint thickness, gdouble h)
{
    gwy_data_field_resample(result, thickness, n, GWY_INTERPOLATION_NONE);
    gwy_data_field_set_xreal(result, h*thickness);
    gwy_data_field_set_yreal(result, h*n);
    gwy_data_field_set_xoffset(result, 0.0);
    gwy_data_field_set_yoffset(result, 0.0);
}

static void
execute(ModuleArgs *args, GwySelection *selection)
{
    GwyDataField *mask = args->result_mask, *field = args->field, *result = args->result;
    GwyInterpolationType interp = gwy_params_get_enum(args->params, PARAM_INTERP);
    guint thickness = gwy_params_get_int(args->params, PARAM_THICKNESS);
    GwySpline *spline;
    GwyXY *points, *tangents, *coords;
    gdouble dx, dy, qx, qy, h, length;
    guint n, i, j, k;
    gboolean have_exterior = FALSE;
    gint xres, yres;
    gdouble *m;

    n = gwy_selection_get_data(selection, NULL);
    points = rescale_points(selection, field, args->realsquare, &dx, &dy, &qx, &qy);
    h = MIN(dx, dy);
    spline = gwy_spline_new_from_points(points, n);
    gwy_spline_set_closed(spline, gwy_params_get_boolean(args->params, PARAM_CLOSED));
    gwy_spline_set_slackness(spline, gwy_params_get_double(args->params, PARAM_SLACKNESS));
    g_free(points);

    length = gwy_spline_length(spline);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    /* This would give natural sampling for a straight line along some axis. */
    n = GWY_ROUND(length + 1.0);
    resize_result_field(result, n, thickness, h);
    gwy_data_field_copy_units(field, result);
    resize_result_field(mask, n, thickness, h);
    gwy_data_field_copy_units(field, mask);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);
    gwy_data_field_clear(mask);
    if (n < 2) {
        gwy_data_field_clear(result);
        return;
    }

    points = g_new(GwyXY, n);
    tangents = g_new(GwyXY, n);
    coords = g_new(GwyXY, n*thickness);
    gwy_spline_sample_uniformly(spline, points, tangents, n);
    m = gwy_data_field_get_data(mask);
    for (i = k = 0; i < n; i++) {
        gdouble xc = qx*points[i].x, yc = qy*points[i].y;
        gdouble vx = qx*tangents[i].y, vy = -qy*tangents[i].x;

        /* If the derivative is zero we just fill the entire row with the same value.  I declare it acceptable. */
        for (j = 0; j < thickness; j++, k++) {
            gdouble x = xc + (j + 0.5 - 0.5*thickness)*vx;
            gdouble y = yc + (j + 0.5 - 0.5*thickness)*vy;

            coords[k].x = x;
            coords[k].y = y;
            if (y > yres || x > xres || y < 0.0 || x < 0.0) {
                m[i*thickness + j] = 1.0;
                have_exterior = TRUE;
            }
        }
    }
    /* Pass mirror because we handle exterior ourselves here and mirror is the least code which simultaneously does
     * not produce undefined pixels where we disagree with the function on which pixels are numerically outside. */
    gwy_data_field_sample_distorted(field, result, coords, interp, GWY_EXTERIOR_MIRROR_EXTEND, 0.0);

    g_free(coords);
    g_free(points);
    g_free(tangents);

    if (have_exterior)
        gwy_data_field_correct_average_unmasked(result, mask);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
