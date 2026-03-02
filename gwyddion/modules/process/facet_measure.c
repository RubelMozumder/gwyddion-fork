/*
 *  $Id: facet_measure.c 25638 2023-09-07 12:23:28Z yeti-dn $
 *  Copyright (C) 2019-2022 David Necas (Yeti).
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

/**
 * Facet (angle) view uses a zoomed area-preserving projection of north hemisphere normal.  Coordinates on hemisphere
 * are labeled (theta, phi), coordinates on the projection (x, y)
 **/

#include "config.h"
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/level.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/elliptic.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

#define FVIEW_GRADIENT "DFit"

enum {
    MAX_PLANE_SIZE = 7,  /* this is actually half */
    FACETVIEW_SIZE = PREVIEW_HALF_SIZE | 1,
    IMAGEVIEW_SIZE = (PREVIEW_SIZE + PREVIEW_SMALL_SIZE)/2,
};

enum {
    RESPONSE_MARK = 1000,
    RESPONSE_MEASURE = 1001,
};

enum {
    PARAM_KERNEL_SIZE,
    PARAM_TOLERANCE,
    PARAM_PHI0,
    PARAM_THETA0,
    PARAM_UPDATE,
    PARAM_REPORT_STYLE,
    PARAM_COMBINE,
    PARAM_COMBINE_TYPE,
    PARAM_MASK_COLOR,
    BUTTON_REFINE,
    BUTTON_MARK,
    BUTTON_MEASURE,
    INFO_THETA,
    INFO_PHI,
};

enum {
    FACET_COLUMN_N,
    FACET_COLUMN_NPOINTS,
    FACET_COLUMN_TOL,
    FACET_COLUMN_THETA,
    FACET_COLUMN_PHI,
    FACET_COLUMN_X,
    FACET_COLUMN_Y,
    FACET_COLUMN_Z,
    FACET_COLUMN_ERROR,
};

typedef struct {
    gdouble tolerance;
    gdouble theta;
    gdouble phi;
    GwyXYZ v;
    gdouble error;
    guint npoints;
} FacetMeasurement;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *theta;
    GwyDataField *phi;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyContainer *args_data;
    GtkWidget *dialog;
    GtkWidget *mark_button;
    GtkWidget *delete;
    GtkWidget *theta_min_label;
    GtkWidget *theta_0_label;
    GtkWidget *theta_max_label;
    GwyParamTable *table;
    GwyContainer *data;
    GwyContainer *fdata;
    GwyDataField *dist;
    GwyDataField *mask;
    GwyNullStore *store;
    GwySelection *fselection;
    GwySelection *iselection;
    GArray *measured_data;
    gchar *selkey;
    gdouble q;
    gint selid;
    gboolean did_init;
} ModuleGUI;

static gboolean         module_register                  (void);
static GwyParamDef*     define_module_params             (void);
static void             facet_measure                    (GwyContainer *data,
                                                          GwyRunType runtype);
static void             execute                          (ModuleArgs *args);
static GwyDialogOutcome run_gui                          (ModuleArgs *args,
                                                          GwyContainer *data,
                                                          gint id);
static void             param_changed                    (ModuleGUI *gui,
                                                          gint id);
static void             dialog_response                  (ModuleGUI *gui,
                                                          gint response);
static void             save_facet_selection             (ModuleGUI *gui);
static GtkWidget*       create_theta_box                 (ModuleGUI *gui);
static GtkWidget*       create_point_list                (ModuleGUI *gui);
static void             point_list_selection_changed     (GtkTreeSelection *treesel,
                                                          ModuleGUI *gui);
static void             render_id                        (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void             render_npoints                   (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void             render_facet_angle               (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void             render_facet_coordinate          (GtkCellLayout *layout,
                                                          GtkCellRenderer *renderer,
                                                          GtkTreeModel *model,
                                                          GtkTreeIter *iter,
                                                          gpointer user_data);
static void             clear_measurements               (ModuleGUI *gui);
static void             delete_measurement               (ModuleGUI *gui);
static void             refine_facet                     (ModuleGUI *gui);
static void             mark_facet                       (gpointer user_data);
static void             measure_facet                    (ModuleGUI *gui);
static gchar*           format_facet_table               (gpointer user_data);
static gboolean         point_list_key_pressed           (ModuleGUI *gui,
                                                          GdkEventKey *event);
static void             recalculate_distribution         (ModuleGUI *gui);
static void             update_theta_range               (ModuleGUI *gui);
static void             facet_view_select_angle          (ModuleGUI *gui,
                                                          gdouble theta,
                                                          gdouble phi);
static void             facet_view_selection_updated     (GwySelection *selection,
                                                          gint hint,
                                                          ModuleGUI *gui);
static void             preview_selection_updated        (GwySelection *selection,
                                                          gint id,
                                                          ModuleGUI *gui);
static void             gwy_data_field_mark_facets       (GwyDataField *dtheta,
                                                          GwyDataField *dphi,
                                                          gdouble theta0,
                                                          gdouble phi0,
                                                          gdouble tolerance,
                                                          GwyDataField *mask);
static void             calculate_average_angle          (GwyDataField *dtheta,
                                                          GwyDataField *dphi,
                                                          gdouble theta0,
                                                          gdouble phi0,
                                                          gdouble tolerance,
                                                          FacetMeasurement *fmeas);
static gdouble          gwy_data_field_facet_distribution(GwyDataField *field,
                                                          GwyDataField *dtheta,
                                                          GwyDataField *dphi,
                                                          GwyDataField *dist,
                                                          gint half_size);
static void             compute_slopes                   (GwyDataField *field,
                                                          gint kernel_size,
                                                          GwyDataField *xder,
                                                          GwyDataField *yder);
static void             mark_fdata                       (GwyDataField *mask,
                                                          gdouble q,
                                                          gdouble theta0,
                                                          gdouble phi0,
                                                          gdouble tolerance);
static GwyDataField*    make_fdist_field                 (GwyDataField *field);
static void             sanitise_params                  (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Visualizes, marks and measures facet orientation."),
    "Yeti <yeti@gwyddion.net>",
    "2.2",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, facet_measure)

static gboolean
module_register(void)
{
    gwy_process_func_register("facet_measure",
                              (GwyProcessFunc)&facet_measure,
                              N_("/Measure _Features/Facet _Measurement..."),
                              GWY_STOCK_FACET_MEASURE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Measure facet angles"));

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
    gwy_param_def_add_int(paramdef, PARAM_KERNEL_SIZE, "kernel-size", _("_Facet plane size"), 0, MAX_PLANE_SIZE, 3);
    gwy_param_def_add_double(paramdef, PARAM_TOLERANCE, "tolerance", _("_Tolerance"), 0.0, G_PI/6.0, 3.0*G_PI/180.0);
    gwy_param_def_add_angle(paramdef, PARAM_PHI0, "phi0", _("Selected φ"), FALSE, 1, 0.0);
    /* The real folding is 4, not 2, but the facet map contains regions outside the possible angles. */
    gwy_param_def_add_angle(paramdef, PARAM_THETA0, "theta0", _("Selected ϑ"), TRUE, 2, 0.0);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", _("I_nstant facet marking"), FALSE);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Facet Vectors"),
                                  GWY_RESULTS_EXPORT_TABULAR_DATA, GWY_RESULTS_REPORT_TABSEP);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
facet_measure(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && mquark);

    if (!gwy_require_image_same_units(args.field, data, id, _("Measure Facets")))
        return;

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.theta = gwy_data_field_new_alike(args.result, FALSE);
    args.phi = gwy_data_field_new_alike(args.result, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.theta);
    g_object_unref(args.phi);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const GwyRGBA facet_mask_color = { 0.56, 0.39, 0.07, 0.5 };

    GtkWidget *hbox, *vbox, *auxbox, *scwin, *button, *dataview, *alignment, *pointlist, *widget;
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table, *extable;
    ModuleGUI gui;
    GtkSizeGroup *sizegroup;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.selid = -1;
    gui.args_data = data;
    gui.selkey = g_strdup_printf("/%d/select/_facets", id);

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.fdata = gwy_container_new();
    gui.dist = make_fdist_field(args->field);
    gui.mask = gwy_data_field_new_alike(gui.dist, TRUE);
    gwy_container_set_object_by_name(gui.fdata, "/0/data", gui.dist);
    gwy_container_set_object_by_name(gui.fdata, "/0/mask", gui.mask);
    gwy_container_set_const_string_by_name(gui.fdata, "/0/base/palette", FVIEW_GRADIENT);
    gwy_rgba_store_to_container(&facet_mask_color, gui.fdata, "/0/mask");

    gui.dialog = gwy_dialog_new(_("Measure Facets"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    /* First row: Image + Options */
    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, IMAGEVIEW_SIZE, TRUE);
    alignment = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(alignment), dataview);
    gtk_box_pack_start(GTK_BOX(hbox), alignment, FALSE, FALSE, 0);

    gui.iselection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Point", 1, TRUE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_KERNEL_SIZE);
    gwy_param_table_set_unitstr(table, PARAM_KERNEL_SIZE, _("px"));
    gwy_param_table_append_slider(table, PARAM_TOLERANCE);
    gwy_param_table_slider_set_factor(table, PARAM_TOLERANCE, 180.0/G_PI);
    gwy_param_table_slider_set_digits(table, PARAM_TOLERANCE, 3);
    gwy_param_table_set_unitstr(table, PARAM_TOLERANCE, _("deg"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_info(table, INFO_THETA, _("Selected ϑ"));
    gwy_param_table_set_unitstr(table, INFO_THETA, _("deg"));
    gwy_param_table_append_info(table, INFO_PHI, _("Selected φ"));
    gwy_param_table_set_unitstr(table, INFO_PHI, _("deg"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_REFINE, -1, RESPONSE_REFINE, _("_Refine"));
    gwy_param_table_append_button(table, BUTTON_MARK, BUTTON_REFINE, RESPONSE_MARK, _("_Mark"));
    gwy_param_table_append_button(table, BUTTON_MEASURE, BUTTON_MARK, RESPONSE_MEASURE, _("Mea_sure"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    /* Second row: Facet view + Facet list */
    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.fdata, 0, FACETVIEW_SIZE, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), dataview, FALSE, FALSE, 0);
    gui.fselection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Point", 1, TRUE);
    g_object_ref(gui.fselection);

    gtk_box_pack_start(GTK_BOX(vbox), create_theta_box(&gui), FALSE, FALSE, 0);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    pointlist = create_point_list(&gui);
    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scwin), pointlist);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    auxbox = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(vbox), auxbox, FALSE, FALSE, 0);

    extable = gwy_param_table_new(args->params);
    gwy_param_table_append_report(extable, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_formatter(extable, PARAM_REPORT_STYLE, format_facet_table, &gui, NULL);
    widget = gwy_param_table_widget(extable);
    /* XXX: Dirty. */
    if (GTK_IS_CONTAINER(widget))
        gtk_container_set_border_width(GTK_CONTAINER(widget), 0);
    gtk_box_pack_end(GTK_BOX(auxbox), widget, FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, extable);

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    button = gwy_stock_like_button_new(_("_Clear"), GTK_STOCK_CLEAR);
    gtk_box_pack_start(GTK_BOX(auxbox), button, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sizegroup, button);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(clear_measurements), &gui);

    button = gwy_stock_like_button_new(_("_Delete"), GTK_STOCK_DELETE);
    gui.delete = button;
    gtk_box_pack_start(GTK_BOX(auxbox), button, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sizegroup, button);
    gtk_widget_set_sensitive(button, FALSE);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(delete_measurement), &gui);
    g_object_unref(sizegroup);

    g_signal_connect(gui.iselection, "changed", G_CALLBACK(preview_selection_updated), &gui);
    g_signal_connect(gui.fselection, "changed", G_CALLBACK(facet_view_selection_updated), &gui);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, mark_facet, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    save_facet_selection(&gui);
    g_object_unref(gui.fselection);
    g_object_unref(gui.data);
    g_object_unref(gui.fdata);
    g_object_unref(gui.dist);
    g_object_unref(gui.mask);
    g_array_free(gui.measured_data, TRUE);
    g_free(gui.selkey);

    return outcome;
}

static GtkWidget*
create_theta_box(ModuleGUI *gui)
{
    GtkWidget *thetabox = gwy_hbox_new(0);

    gui->theta_min_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->theta_min_label), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), gui->theta_min_label, TRUE, TRUE, 0);

    gui->theta_0_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->theta_0_label), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), gui->theta_0_label, TRUE, TRUE, 0);

    gui->theta_max_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->theta_max_label), 1.0, 0.5);
    gtk_box_pack_start(GTK_BOX(thetabox), gui->theta_max_label, TRUE, TRUE, 0);

    return thetabox;
}

static void
create_point_list_column(GtkTreeView *treeview, GtkCellRenderer *renderer,
                         ModuleGUI *gui,
                         const gchar *name, const gchar *units,
                         guint facet_column)
{
    GtkTreeViewColumn *column;
    GtkCellLayoutDataFunc cellfunc;
    GtkWidget *label;
    gchar *s;

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_column_set_alignment(column, 0.5);
    g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(facet_column));

    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    if (facet_column == FACET_COLUMN_N)
        cellfunc = render_id;
    else if (facet_column == FACET_COLUMN_NPOINTS)
        cellfunc = render_npoints;
    else if (facet_column >= FACET_COLUMN_X && facet_column <= FACET_COLUMN_Z)
        cellfunc = render_facet_coordinate;
    else
        cellfunc = render_facet_angle;

    gtk_cell_layout_set_cell_data_func(GTK_CELL_LAYOUT(column), renderer, cellfunc, gui, NULL);

    label = gtk_label_new(NULL);
    if (units && strlen(units))
        s = g_strdup_printf("<b>%s</b> [%s]", name, units);
    else
        s = g_strdup_printf("<b>%s</b>", name);
    gtk_label_set_markup(GTK_LABEL(label), s);
    g_free(s);
    gtk_tree_view_column_set_widget(column, label);
    gtk_widget_show(label);
    gtk_tree_view_append_column(treeview, column);
}

static GtkWidget*
create_point_list(ModuleGUI *gui)
{
    GtkTreeView *treeview;
    GtkCellRenderer *renderer;
    GtkTreeSelection *treesel;
    GtkWidget *pointlist;

    gui->store = gwy_null_store_new(0);
    gui->measured_data = g_array_new(FALSE, FALSE, sizeof(FacetMeasurement));
    pointlist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->store));
    treeview = GTK_TREE_VIEW(pointlist);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);

    create_point_list_column(treeview, renderer, gui, "n", NULL, FACET_COLUMN_N);
    create_point_list_column(treeview, renderer, gui, _("points"), NULL, FACET_COLUMN_NPOINTS);
    create_point_list_column(treeview, renderer, gui, "t", _("deg"), FACET_COLUMN_TOL);
    create_point_list_column(treeview, renderer, gui, "θ", _("deg"), FACET_COLUMN_THETA);
    create_point_list_column(treeview, renderer, gui, "φ", _("deg"), FACET_COLUMN_PHI);
    create_point_list_column(treeview, renderer, gui, "x", NULL, FACET_COLUMN_X);
    create_point_list_column(treeview, renderer, gui, "y", NULL, FACET_COLUMN_Y);
    create_point_list_column(treeview, renderer, gui, "z", NULL, FACET_COLUMN_Z);
    create_point_list_column(treeview, renderer, gui, "δ", _("deg"), FACET_COLUMN_ERROR);

    treesel = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_set_mode(treesel, GTK_SELECTION_BROWSE);
    g_signal_connect(treesel, "changed", G_CALLBACK(point_list_selection_changed), gui);
    g_signal_connect_swapped(treeview, "key-press-event", G_CALLBACK(point_list_key_pressed), gui);

    g_object_unref(gui->store);

    return pointlist;
}

static inline void
slopes_to_angles(gdouble xder, gdouble yder,
                 gdouble *theta, gdouble *phi)
{
    *phi = atan2(yder, -xder);
    *theta = atan(sqrt(xder*xder + yder*yder));
}

static inline void
angles_to_slopes(gdouble theta, gdouble phi,
                 gdouble *xder, gdouble *yder)
{
    *xder = -tan(theta)*cos(phi);
    *yder = tan(theta)*sin(phi);
}

/* Transforms (ϑ, φ) to Cartesian selection coordinates [0,2q], which is [0,2] for the full range of angles. */
static inline void
angles_to_xy(gdouble theta, gdouble phi, gdouble q,
             gdouble *x, gdouble *y)
{
    gdouble rho = G_SQRT2*sin(theta/2.0);
    gdouble c = cos(phi), s = sin(phi);

    *x = rho*c + q;
    *y = -rho*s + q;
}

static inline void
xy_to_angles(gdouble x, gdouble y, gdouble q,
             gdouble *theta, gdouble *phi)
{
    gdouble s = hypot(x - q, y - q)/G_SQRT2;

    *phi = atan2(q - y, x - q);
    if (s <= 1.0)
        *theta = 2.0*asin(s);
    else
        *theta = G_PI - 2.0*asin(2.0 - s);
}

static inline void
make_unit_vector(GwyXYZ *v, gdouble theta, gdouble phi)
{
    v->x = sin(theta)*cos(phi);
    v->y = sin(theta)*sin(phi);
    v->z = cos(theta);
}

static inline void
vector_angles(const GwyXYZ *v, gdouble *theta, gdouble *phi)
{
    *theta = atan2(sqrt(v->x*v->x + v->y*v->y), v->z);
    *phi = atan2(v->y, v->x);
}

static void
render_id(G_GNUC_UNUSED GtkCellLayout *layout,
          GtkCellRenderer *renderer,
          GtkTreeModel *model,
          GtkTreeIter *iter,
          G_GNUC_UNUSED gpointer user_data)
{
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_snprintf(buf, sizeof(buf), "%d", i + 1);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_npoints(G_GNUC_UNUSED GtkCellLayout *layout,
               GtkCellRenderer *renderer,
               GtkTreeModel *model,
               GtkTreeIter *iter,
               gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    FacetMeasurement *fmeas;
    gchar buf[16];
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_return_if_fail(i < gui->measured_data->len);
    fmeas = &g_array_index(gui->measured_data, FacetMeasurement, i);
    g_snprintf(buf, sizeof(buf), "%u", fmeas->npoints);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_facet_angle(GtkCellLayout *layout,
                   GtkCellRenderer *renderer,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    FacetMeasurement *fmeas;
    gchar buf[16];
    guint i, id;
    gdouble u;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_return_if_fail(i < gui->measured_data->len);
    fmeas = &g_array_index(gui->measured_data, FacetMeasurement, i);

    if (id == FACET_COLUMN_THETA)
        u = fmeas->theta;
    else if (id == FACET_COLUMN_PHI)
        u = fmeas->phi;
    else if (id == FACET_COLUMN_ERROR)
        u = fmeas->error;
    else if (id == FACET_COLUMN_TOL)
        u = fmeas->tolerance;
    else {
        g_assert_not_reached();
    }

    g_snprintf(buf, sizeof(buf), "%.3f", 180.0/G_PI*u);
    g_object_set(renderer, "text", buf, NULL);
}

static void
render_facet_coordinate(GtkCellLayout *layout,
                        GtkCellRenderer *renderer,
                        GtkTreeModel *model,
                        GtkTreeIter *iter,
                        gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    FacetMeasurement *fmeas;
    gchar buf[16];
    guint i, id;
    gdouble u;

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(layout), "id"));
    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_return_if_fail(i < gui->measured_data->len);
    fmeas = &g_array_index(gui->measured_data, FacetMeasurement, i);

    if (id == FACET_COLUMN_X)
        u = fmeas->v.x;
    else if (id == FACET_COLUMN_Y)
        u = fmeas->v.y;
    else if (id == FACET_COLUMN_Z)
        u = fmeas->v.z;
    else {
        g_assert_not_reached();
    }

    g_snprintf(buf, sizeof(buf), "%.3f", u);
    g_object_set(renderer, "text", buf, NULL);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table = gui->table;
    GwyParams *params = args->params;
    gdouble theta = gwy_params_get_double(params, PARAM_THETA0);
    gdouble phi = gwy_params_get_double(params, PARAM_PHI0);
    gchar buf[20];

    if (id < 0 || id == PARAM_KERNEL_SIZE)
        recalculate_distribution(gui);

    /* This requires gui->q already calculated because we set selection on the facet view.  Life would be easier
     * if selections used offset coordinates. */
    if (id < 0 && !gui->did_init) {
        GwyContainer *data = gui->args_data;
        GwySelection *sel;
        gdouble xy[2];

        gui->did_init = TRUE;
        if (gwy_container_gis_object_by_name(data, gui->selkey, &sel) && gwy_selection_get_object(sel, 0, xy) > 0)
            slopes_to_angles(xy[0], xy[1], &theta, &phi);
        /* XXX: recursion? */
        facet_view_select_angle(gui, theta, phi);
    }
    if (id < 0 || id == PARAM_THETA0) {
        g_snprintf(buf, sizeof(buf), "%.2f", 180.0/G_PI*theta);
        gwy_param_table_info_set_valuestr(table, INFO_THETA, buf);
    }
    if (id < 0 || id == PARAM_PHI0) {
        g_snprintf(buf, sizeof(buf), "%.2f", 180.0/G_PI*phi);
        gwy_param_table_info_set_valuestr(table, INFO_PHI, buf);
    }
    if (id < 0 || id == PARAM_UPDATE)
        gwy_param_table_set_sensitive(table, BUTTON_MARK, !gwy_params_get_boolean(params, PARAM_UPDATE));

    if (id != PARAM_REPORT_STYLE && id != PARAM_MASK_COLOR)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_MARK)
        mark_facet(gui);
    else if (response == RESPONSE_REFINE)
        refine_facet(gui);
    else if (response == RESPONSE_MEASURE)
        measure_facet(gui);
}

static void
save_facet_selection(ModuleGUI *gui)
{
    GwySelection *selection;
    gdouble theta, phi, xy[2];

    if (!gwy_selection_get_data(gui->fselection, NULL)) {
        gwy_container_remove_by_name(gui->args_data, gui->selkey);
        return;
    }

    gwy_selection_get_object(gui->fselection, 0, xy);
    xy_to_angles(xy[0], xy[1], gui->q, &theta, &phi);
    angles_to_slopes(theta, phi, xy+0, xy+1);
    /* Create a new object.  We have signals connected to the old one. */
    selection = g_object_new(g_type_from_name("GwySelectionPoint"), "max-objects", 1, NULL);
    gwy_selection_set_data(selection, 1, xy);
    gwy_container_pass_object_by_name(gui->args_data, gui->selkey, selection);
}

static void
point_list_selection_changed(GtkTreeSelection *treesel, ModuleGUI *gui)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean sens;

    if ((sens = gtk_tree_selection_get_selected(treesel, &model, &iter)))
        gtk_tree_model_get(model, &iter, 0, &gui->selid, -1);
    else
        gui->selid = -1;

    gtk_widget_set_sensitive(gui->delete, sens);
}

static void
clear_measurements(ModuleGUI *gui)
{
    gwy_null_store_set_n_rows(gui->store, 0);
    g_array_set_size(gui->measured_data, 0);
}

static void
delete_measurement(ModuleGUI *gui)
{
    gint selid = gui->selid;
    gint i, n = gui->measured_data->len;

    if (selid < 0 || selid >= n)
        return;

    gwy_null_store_set_n_rows(gui->store, n-1);
    g_array_remove_index(gui->measured_data, selid);
    for (i = selid; i < n-1; i++)
        gwy_null_store_row_changed(gui->store, i);
}

static void
refine_facet(ModuleGUI *gui)
{
    gdouble tolerance = gwy_params_get_double(gui->args->params, PARAM_TOLERANCE);
    GwyDataField *dist = gui->dist;
    gdouble xy[2], theta, phi, x, y, h;
    gint fres, range;

    gwy_selection_get_object(gui->fselection, 0, xy);
    xy_to_angles(xy[0], xy[1], gui->q, &theta, &phi);

    fres = gwy_data_field_get_xres(dist);
    h = gwy_data_field_get_dx(dist);
    range = GWY_ROUND(fres/gui->q * 0.5/G_SQRT2 * cos(0.5*theta) * tolerance);
    x = xy[0]/h;
    y = xy[1]/h;
    gwy_data_field_local_maximum(dist, &x, &y, range, range);
    xy[0] = x*h;
    xy[1] = y*h;
    gwy_selection_set_object(gui->fselection, 0, xy);
}

static void
mark_facet(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    gdouble theta0 = gwy_params_get_double(args->params, PARAM_THETA0);
    gdouble phi0 = gwy_params_get_double(args->params, PARAM_PHI0);
    gdouble tolerance = gwy_params_get_double(args->params, PARAM_TOLERANCE);

    execute(args);
    mark_fdata(gui->mask, gui->q, theta0, phi0, tolerance);
    gwy_data_field_data_changed(args->result);
    gwy_data_field_data_changed(gui->mask);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
measure_facet(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyNullStore *store = gui->store;
    gdouble theta0 = gwy_params_get_double(args->params, PARAM_THETA0);
    gdouble phi0 = gwy_params_get_double(args->params, PARAM_PHI0);
    gdouble tolerance = gwy_params_get_double(args->params, PARAM_TOLERANCE);
    FacetMeasurement fmeas;

    calculate_average_angle(args->theta, args->phi, theta0, phi0, tolerance, &fmeas);
    g_array_append_val(gui->measured_data, fmeas);
    gwy_null_store_set_n_rows(store, gwy_null_store_get_n_rows(store) + 1);
}

static gboolean
point_list_key_pressed(ModuleGUI *gui,
                       GdkEventKey *event)
{
    if (event->keyval == GDK_Delete) {
        delete_measurement(gui);
        return TRUE;
    }
    return FALSE;
}

static gchar*
format_facet_table(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GwyResultsReportType report_style = gwy_params_get_report_type(gui->args->params, PARAM_REPORT_STYLE);
    FacetMeasurement *fmeas;
    GString *str;
    gdouble q = 1.0;
    guint n, i;

    if (!(n = gui->measured_data->len))
        return NULL;

    str = g_string_new(NULL);
    if (!(report_style & GWY_RESULTS_REPORT_MACHINE)) {
        gwy_format_result_table_strings(str, report_style, 8,
                                        "N", "t [deg]", "ϑ [deg]", "φ [deg]", "x", "y", "z", "δ");
        q = 180.0/G_PI;
    }
    else {
        gwy_format_result_table_strings(str, report_style, 8,
                                        "N", "t", "ϑ", "φ", "x", "y", "z", "δ");
    }

    for (i = 0; i < n; i++) {
        fmeas = &g_array_index(gui->measured_data, FacetMeasurement, i);
        gwy_format_result_table_row(str, report_style, 8,
                                    1.0*fmeas->npoints, fmeas->tolerance,
                                    q*fmeas->theta, q*fmeas->phi,
                                    fmeas->v.x, fmeas->v.y, fmeas->v.z,
                                    q*fmeas->error);
    }

    return g_string_free(str, FALSE);
}

static void
recalculate_distribution(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    guint i, n, kernel_size = gwy_params_get_int(args->params, PARAM_KERNEL_SIZE);
    GwySelection *selection;
    gdouble *xy;

    selection = gui->fselection;
    n = gwy_selection_get_data(selection, NULL);
    xy = g_new(gdouble, 2*n);
    gwy_selection_get_data(selection, xy);
    for (i = 0; i < n; i++)
        xy_to_angles(xy[2*i], xy[2*i+1], gui->q, xy + 2*i, xy + 2*i+1);

    if (GTK_WIDGET_REALIZED(gui->dialog))
        gwy_app_wait_cursor_start(GTK_WINDOW(gui->dialog));
    gui->q = gwy_data_field_facet_distribution(args->field, args->theta, args->phi, gui->dist, kernel_size);

    gwy_data_field_clear(gui->mask);
    gwy_data_field_data_changed(gui->mask);
    gwy_data_field_data_changed(gui->dist);
    update_theta_range(gui);

    for (i = 0; i < n; i++)
        angles_to_xy(xy[2*i], xy[2*i+1], gui->q, xy + 2*i, xy + 2*i+1);
    gwy_selection_set_data(selection, n, xy);
    g_free(xy);
    if (GTK_WIDGET_REALIZED(gui->dialog))
        gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
}

static void
update_theta_range(ModuleGUI *gui)
{
    gdouble theta, phi;
    gchar buf[32];

    xy_to_angles(gui->q, 0.0, gui->q, &theta, &phi);
    g_snprintf(buf, sizeof(buf), "%.1f %s", -180.0/G_PI*theta, _("deg"));
    gtk_label_set_text(GTK_LABEL(gui->theta_min_label), buf);
    g_snprintf(buf, sizeof(buf), "0 %s", _("deg"));
    gtk_label_set_text(GTK_LABEL(gui->theta_0_label), buf);
    g_snprintf(buf, sizeof(buf), "%.1f %s", 180.0/G_PI*theta, _("deg"));
    gtk_label_set_text(GTK_LABEL(gui->theta_max_label), buf);
}

static void
facet_view_select_angle(ModuleGUI *gui, gdouble theta, gdouble phi)
{
    gdouble xy[2];

    angles_to_xy(theta, phi, gui->q, xy+0, xy+1);
    gwy_selection_set_object(gui->fselection, 0, xy);
}

static void
facet_view_selection_updated(GwySelection *selection,
                             gint hint,
                             ModuleGUI *gui)
{
    gdouble xy[2], theta, phi;

    g_return_if_fail(hint == 0 || hint == -1);
    if (!gwy_selection_get_object(selection, 0, xy))
        return;
    xy_to_angles(xy[0], xy[1], gui->q, &theta, &phi);
    gwy_params_set_double(gui->args->params, PARAM_THETA0, theta);
    gwy_param_table_param_changed(gui->table, PARAM_THETA0);
    gwy_params_set_double(gui->args->params, PARAM_PHI0, phi);
    gwy_param_table_param_changed(gui->table, PARAM_PHI0);
}

static void
preview_selection_updated(GwySelection *selection,
                          gint hint,
                          ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *field = args->field, *dtheta = args->theta, *dphi = args->phi;
    gdouble theta, phi, xy[2];
    gint i, j;

    if (hint != 0)
        return;

    gwy_selection_get_object(selection, 0, xy);
    j = gwy_data_field_rtoj(field, xy[0]);
    i = gwy_data_field_rtoi(field, xy[1]);
    theta = gwy_data_field_get_val(dtheta, j, i);
    phi = gwy_data_field_get_val(dphi, j, i);
    facet_view_select_angle(gui, theta, phi);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble theta0 = gwy_params_get_double(params, PARAM_THETA0);
    gdouble phi0 = gwy_params_get_double(params, PARAM_PHI0);
    gdouble tolerance = gwy_params_get_double(params, PARAM_TOLERANCE);
    GwyMergeType combine_type = gwy_params_get_enum(params, PARAM_COMBINE_TYPE);
    gboolean combine = gwy_params_get_boolean(params, PARAM_COMBINE);
    GwyDataField *result = args->result, *mask = args->mask;

    gwy_data_field_mark_facets(args->theta, args->phi, theta0, phi0, tolerance, result);
    if (mask && combine) {
        if (combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(result, mask);
        else if (combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(result, mask);
    }
}

static void
gwy_data_field_mark_facets(GwyDataField *dtheta, GwyDataField *dphi,
                           gdouble theta0, gdouble phi0, gdouble tolerance,
                           GwyDataField *mask)
{
    gdouble ctol = cos(tolerance), cth0 = cos(theta0), sth0 = sin(theta0);
    const gdouble *td = gwy_data_field_get_data_const(dtheta);
    const gdouble *pd = gwy_data_field_get_data_const(dphi);
    gdouble *md = gwy_data_field_get_data(mask);
    guint i, n = gwy_data_field_get_xres(dtheta)*gwy_data_field_get_yres(dtheta);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(td,pd,md,n,cth0,sth0,phi0,ctol)
#endif
    for (i = 0; i < n; i++) {
        gdouble cro = cth0*cos(td[i]) + sth0*sin(td[i])*cos(pd[i] - phi0);
        md[i] = (cro >= ctol);
    }
}

static gdouble
gwy_data_field_facet_distribution(GwyDataField *field,
                                  GwyDataField *dtheta, GwyDataField *dphi, GwyDataField *dist,
                                  gint half_size)
{
    gdouble *xd, *yd, *data;
    const gdouble *xdc, *ydc;
    gdouble q, x, y;
    gint hres, i, xres, yres, n, fres;

    compute_slopes(field, 2*half_size + 1, dtheta, dphi);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    xd = gwy_data_field_get_data(dtheta);
    yd = gwy_data_field_get_data(dphi);
    n = xres*yres;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xd,yd,n)
#endif
    for (i = 0; i < n; i++) {
        gdouble theta, phi;

        slopes_to_angles(xd[i], yd[i], &theta, &phi);
        xd[i] = theta;
        yd[i] = phi;
    }
    q = gwy_data_field_get_max(dtheta);
    q = MIN(q*1.05, 1.001*G_PI/2.0);
    q = G_SQRT2*sin(q/2.0);

    gwy_data_field_clear(dist);
    gwy_data_field_set_xreal(dist, 2.0*q);
    gwy_data_field_set_yreal(dist, 2.0*q);
    gwy_data_field_set_xoffset(dist, -q);
    gwy_data_field_set_yoffset(dist, -q);

    fres = gwy_data_field_get_xres(dist);
    hres = (fres - 1)/2;

    data = gwy_data_field_get_data(dist);
    xdc = gwy_data_field_get_data_const(dtheta);
    ydc = gwy_data_field_get_data_const(dphi);
    for (i = 0; i < n; i++) {
        gint xx, yy;

        angles_to_xy(xdc[i], ydc[i], q, &x, &y);
        x *= hres/q;
        y *= hres/q;
        xx = (gint)floor(x - 0.5);
        yy = (gint)floor(y - 0.5);

        if (G_UNLIKELY(xx < 0)) {
            xx = 0;
            x = 0.0;
        }
        else if (G_UNLIKELY(xx >= fres-1)) {
            xx = fres-2;
            x = 1.0;
        }
        else
            x = x - (xx + 0.5);

        if (G_UNLIKELY(yy < 0)) {
            yy = 0;
            y = 0.0;
        }
        else if (G_UNLIKELY(yy >= fres-1)) {
            yy = fres-2;
            y = 1.0;
        }
        else
            y = y - (yy + 0.5);

        data[yy*fres + xx] += (1.0 - x)*(1.0 - y);
        data[yy*fres + xx+1] += x*(1.0 - y);
        data[yy*fres+fres + xx] += (1.0 - x)*y;
        data[yy*fres+fres + xx+1] += x*y;
    }

    /* Transform values for visualisation. */
    for (i = fres*fres; i; i--, data++)
        *data = cbrt(*data);

    return q;
}

static void
compute_slopes(GwyDataField *field, gint kernel_size,
               GwyDataField *xder, GwyDataField *yder)
{
    GwyPlaneFitQuantity quantites[] = { GWY_PLANE_FIT_BX, GWY_PLANE_FIT_BY };
    GwyDataField *fields[2];
    gint xres, yres;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    if (kernel_size > 1) {
        fields[0] = xder;
        fields[1] = yder;
        gwy_data_field_fit_local_planes(field, kernel_size, 2, quantites, fields);
        gwy_data_field_multiply(xder, xres/gwy_data_field_get_xreal(field));
        gwy_data_field_multiply(yder, yres/gwy_data_field_get_yreal(field));
    }
    else
        gwy_data_field_filter_slope(field, xder, yder);
}

static void
mark_fdata(GwyDataField *mask, gdouble q, gdouble theta0, gdouble phi0, gdouble tolerance)
{
    gdouble r, r2, cr, cro, cth0, sth0, cphi0, sphi0;
    gint fres, hres, i, j;
    gdouble *m;

    cr = cos(tolerance);
    cth0 = cos(theta0);
    sth0 = sin(theta0);
    cphi0 = cos(phi0);
    sphi0 = sin(phi0);
    fres = gwy_data_field_get_xres(mask);
    g_assert(gwy_data_field_get_yres(mask) == fres);
    hres = (fres - 1)/2;
    m = gwy_data_field_get_data(mask);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j,r2,r,cro) \
            shared(m,fres,hres,cth0,sth0,cphi0,sphi0,q,cr)
#endif
    for (i = 0; i < fres; i++) {
        gdouble y = -q*(i/(gdouble)hres - 1.0);

        for (j = 0; j < fres; j++) {
            gdouble x = q*(j/(gdouble)hres - 1.0);

            /**
             * Orthodromic distance computed directly from x, y:
             * cos(theta) = 1 - r^2
             * sin(theta) = r*sqrt(1 - r^2/2)
             * cos(phi) = x/r
             * sin(phi) = y/r
             * where r = hypot(x, y)
             **/
            r2 = x*x + y*y;
            r = sqrt(r2);
            cro = cth0*(1.0 - r2) + sth0*G_SQRT2*r*sqrt(1.0 - r2/2.0)*(x/r*cphi0 + y/r*sphi0);
            m[i*fres + j] = (cro >= cr);
        }
    }
}

static void
calculate_average_angle(GwyDataField *dtheta, GwyDataField *dphi,
                        gdouble theta0, gdouble phi0, gdouble tolerance,
                        FacetMeasurement *fmeas)
{
    gdouble s2, sx, sy, sz, cth0, sth0, ctol;
    const gdouble *td, *pd;
    gint i, n, count;
    GwyXYZ s;

    gwy_clear(fmeas, 1);
    fmeas->tolerance = tolerance;

    cth0 = cos(theta0);
    sth0 = sin(theta0);
    ctol = cos(tolerance);

    td = gwy_data_field_get_data_const(dtheta);
    pd = gwy_data_field_get_data_const(dphi);
    n = gwy_data_field_get_xres(dtheta)*gwy_data_field_get_yres(dtheta);
    count = 0;
    sx = sy = sz = 0.0;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:count,sx,sy,sz) \
            private(i) \
            shared(td,pd,n,cth0,sth0,phi0,ctol)
#endif
    for (i = 0; i < n; i++) {
        gdouble cro = cth0*cos(td[i]) + sth0*sin(td[i]) * cos(pd[i] - phi0);
        if (cro >= ctol) {
            GwyXYZ v;

            make_unit_vector(&v, td[i], pd[i]);
            sx += v.x;
            sy += v.y;
            sz += v.z;
            count++;
        }
    }
    s.x = sx;
    s.y = sy;
    s.z = sz;
    fmeas->npoints = count;

    if (!count)
        return;

    vector_angles(&s, &fmeas->theta, &fmeas->phi);
    make_unit_vector(&s, fmeas->theta, fmeas->phi);
    fmeas->v = s;
    if (count == 1)
        return;

    /* Since we calculate the mean direction as vector average, not point on sphere with minimum square geodesic
     * distance, do the same for the dispersion estimate. */
    s2 = 0.0;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(+:s2) \
            private(i) \
            shared(td,pd,s,n,cth0,sth0,phi0,ctol)
#endif
    for (i = 0; i < n; i++) {
        gdouble cro = cth0*cos(td[i]) + sth0*sin(td[i]) * cos(pd[i] - phi0);
        if (cro >= ctol) {
            GwyXYZ v;

            make_unit_vector(&v, td[i], pd[i]);
            s2 += ((v.x - s.x)*(v.x - s.x) + (v.y - s.y)*(v.y - s.y) + (v.z - s.z)*(v.z - s.z));
        }
    }

    /* This is already in radians. */
    fmeas->error = sqrt(s2/(count - 1));
}

static GwyDataField*
make_fdist_field(GwyDataField *field)
{
    gint n = gwy_data_field_get_xres(field)*gwy_data_field_get_yres(field);
    gint fres = 2*GWY_ROUND(cbrt(3.49*n)) + 1;
    GwyDataField *fdist;

    fdist = gwy_data_field_new(fres, fres, 1.0, 1.0, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(fdist), NULL);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(fdist), NULL);
    return fdist;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble theta;

    if ((theta = gwy_params_get_double(params, PARAM_THETA0)) >= 0.25*G_PI)
        gwy_params_set_double(params, PARAM_THETA0, (theta = 0.0));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
