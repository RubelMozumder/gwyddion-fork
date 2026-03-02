/*
 *  $Id: volume_planestat.c 25639 2023-09-07 12:23:42Z yeti-dn $
 *  Copyright (C) 2015-2022 David Necas (Yeti).
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
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define ENTROPY_NORMAL 1.41893853320467274178l

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    /* These are also used as indices into ModuleGUI.adj[] and ModuleGUI.real_coord[]. */
    PARAM_COL,
    PARAM_ROW,
    PARAM_WIDTH,
    PARAM_HEIGHT,

    PARAM_LEVEL,
    PARAM_SHOW_PLANE,
    PARAM_QUANTITY,
    PARAM_UPDATE,
    PARAM_TARGET_GRAPH,

    INFO_VALUE,
};

typedef enum {
    SELECTION_SOURCE_NOTHING   = 0,
    SELECTION_SOURCE_SELECTION = 1,
    SELECTION_SOURCE_NUMERIC   = 2,
} SelectionSource;

typedef enum {
    GWY_PLANE_STAT_MEAN        = 0,
    GWY_PLANE_STAT_RMS         = 1,
    GWY_PLANE_STAT_MIN         = 2,
    GWY_PLANE_STAT_MAX         = 3,
    GWY_PLANE_STAT_RANGE       = 4,
    GWY_PLANE_STAT_SKEW        = 5,
    GWY_PLANE_STAT_KURTOSIS    = 6,
    GWY_PLANE_STAT_SA          = 7,
    GWY_PLANE_STAT_MEDIAN      = 8,
    GWY_PLANE_STAT_VARIATION   = 9,
    GWY_PLANE_STAT_ENTROPY     = 10,
    GWY_PLANE_STAT_ENTROPY_DEF = 11,
    GWY_PLANE_STAT_NQUANTITIES,
} PlaneStatQuantity;

typedef enum {
    GWY_PLANE_STAT_LATERAL_EQUAL = (1 << 0),
    GWY_PLANE_STAT_ALL_EQUAL     = (1 << 1),
    GWY_PLANE_STAT_FLAGS_ALL     = (1 << 2) - 1,
} PlaneStatFlags;

typedef gdouble (*PlaneStatFunc)(GwyDataField *datafield);

typedef struct {
    PlaneStatQuantity quantity;
    guint flags;
    gint powerx;
    gint powery;
    gint powerw;
    PlaneStatFunc func;
    const gchar *name;
    const gchar *symbol;
} PlaneStatQuantInfo;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyGraphModel *gmodel;
    /* Cached input data properties. */
    gboolean lateral_equal;
    gboolean all_equal;
    GwyDataLine *calibration;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyDataField *orig_preview;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GwySelection *image_selection;
    GwySelection *graph_selection;
    GwySIValueFormat *xvf;
    GwySIValueFormat *yvf;
    GwySIValueFormat *vf;
    GtkAdjustment *adj[4];
    GtkWidget *real_coord[4];
    SelectionSource rectangle_source;
} ModuleGUI;

typedef struct {
    GwyBrick *brick;
    const gdouble *db;
    GwyDataLine *dline;
    gdouble *buf;
    guint npts;
    guint npixels;
    guint k;
} PlaneStatIter;

static gboolean                  module_register          (void);
static GwyParamDef*              define_module_params     (void);
static void                      plane_stat               (GwyContainer *data,
                                                           GwyRunType run);
static GwyDialogOutcome          run_gui                  (ModuleArgs *args,
                                                           GwyContainer *data,
                                                           gint id);
static void                      execute                  (ModuleArgs *args);
static GtkWidget*                construct_selection      (ModuleGUI *gui);
static gboolean                  filter_quantity          (const GwyEnum *enumval,
                                                           gpointer user_data);
static void                      image_selection_changed  (ModuleGUI *gui,
                                                           gint id,
                                                           GwySelection *selection);
static void                      dialog_response          (GwyDialog *dialog,
                                                           gint response,
                                                           ModuleGUI *gui);
static void                      coord_changed            (ModuleGUI *gui,
                                                           GtkAdjustment *adj);
static void                      param_changed            (ModuleGUI *gui,
                                                           gint id);
static void                      graph_selection_changed  (ModuleGUI *gui,
                                                           gint id,
                                                           GwySelection *selection);
static void                      show_extracted_plane     (ModuleGUI *gui);
static void                      preview                  (gpointer user_data);
static void                      update_real_size         (ModuleGUI *gui,
                                                           gint id);
static void                      update_graph_ordinate    (ModuleArgs *args);
static void                      update_image_selection   (ModuleGUI *gui);
static void                      update_graph_selection   (ModuleGUI *gui);
static gdouble                   get_plane_range          (GwyDataField *field);
static gdouble                   get_plane_Sa             (GwyDataField *field);
static gdouble                   get_plane_median         (GwyDataField *field);
static gdouble                   get_plane_skew           (GwyDataField *field);
static gdouble                   get_plane_kurtosis       (GwyDataField *field);
static gdouble                   get_plane_entropy_deficit(GwyDataField *field);
static const PlaneStatQuantInfo* get_quantity_info        (PlaneStatQuantity quantity);
static void                      sanitise_params          (ModuleArgs *args);

static const PlaneStatQuantInfo quantities[] = {
    {
        GWY_PLANE_STAT_MEAN, 0, 0, 0, 1, gwy_data_field_get_avg,
        N_("Mean"), "μ",
    },
    {
        GWY_PLANE_STAT_RMS, 0, 0, 0, 1, gwy_data_field_get_rms,
        N_("RMS"), "σ",
    },
    {
        GWY_PLANE_STAT_MIN, 0, 0, 0, 1, gwy_data_field_get_min,
        N_("Minimum"), "v<sub>min</sub>",
    },
    {
        GWY_PLANE_STAT_MAX, 0, 0, 0, 1, gwy_data_field_get_max,
        N_("Maximum"), "v<sub>min</sub>",
    },
    {
        GWY_PLANE_STAT_RANGE, 0, 0, 0, 1, get_plane_range,
        N_("Range"), "R",
    },
    {
        GWY_PLANE_STAT_SKEW, 0, 0, 0, 0, get_plane_skew,
        N_("Skew"), "γ",
    },
    {
        GWY_PLANE_STAT_KURTOSIS, 0, 0, 0, 0, get_plane_kurtosis,
        N_("Excess kurtosis"), "κ",
    },
    {
        GWY_PLANE_STAT_SA, 0, 0, 0, 1, get_plane_Sa,
        N_("Mean roughness"), "Sa",
    },
    {
        GWY_PLANE_STAT_MEDIAN, 0, 0, 0, 1, get_plane_median,
        N_("Median"), "m",
    },
    {
        GWY_PLANE_STAT_VARIATION, GWY_PLANE_STAT_LATERAL_EQUAL, 1, 0, 1, gwy_data_field_get_variation,
        N_("Variation"), "var",
    },
    {
        GWY_PLANE_STAT_ENTROPY, 0, 0, 0, 0, gwy_data_field_get_entropy,
        N_("Entropy"), "H",
    },
    {
        GWY_PLANE_STAT_ENTROPY_DEF, 0, 0, 0, 0, get_plane_entropy_deficit,
        N_("Entropy deficit"), "H<sub>def</sub>",
    },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Summarizes volume data planes to a graph."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_planestat)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_planestat",
                             (GwyVolumeFunc)&plane_stat,
                             N_("/_Statistics/Summarize P_lanes..."),
                             GWY_STOCK_VOLUME_PLANE_STATS,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Summarize planes"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *functions = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    functions = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(quantities), quantities, sizeof(PlaneStatQuantInfo),
                                          G_STRUCT_OFFSET(PlaneStatQuantInfo, name),
                                          G_STRUCT_OFFSET(PlaneStatQuantInfo, quantity));

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_int(paramdef, PARAM_COL, "col", _("_X"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_ROW, "row", _("_Y"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_WIDTH, "width", _("_Width"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_HEIGHT, "height", _("_Height"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_LEVEL, "level", _("Preview _level"), -1, G_MAXINT, -1);
    gwy_param_def_add_boolean(paramdef, PARAM_SHOW_PLANE, "show_plane", _("Preview _level"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_QUANTITY, "quantity", _("_Quantity"),
                              functions, G_N_ELEMENTS(quantities), GWY_PLANE_STAT_MEAN);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
plane_stat(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwySIUnit *xunit, *yunit, *zunit, *wunit;
    GwyAppDataId target_graph_id;
    GwyDialogOutcome outcome;
    GwyGraphCurveModel *gcmodel;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerRectangle"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));

    args.params = gwy_params_new_from_settings(define_module_params());
    args.calibration = gwy_brick_get_zcalibration(args.brick);
    if (args.calibration && (gwy_brick_get_zres(args.brick) != gwy_data_line_get_res(args.calibration)))
        args.calibration = NULL;

    xunit = gwy_brick_get_si_unit_x(args.brick);
    yunit = gwy_brick_get_si_unit_y(args.brick);
    wunit = gwy_brick_get_si_unit_w(args.brick);
    args.lateral_equal = gwy_si_unit_equal(xunit, yunit);
    args.all_equal = args.lateral_equal && gwy_si_unit_equal(wunit, xunit);
    sanitise_params(&args);

    args.gmodel = gwy_graph_model_new();
    zunit = (args.calibration ? gwy_data_line_get_si_unit_y(args.calibration) : gwy_brick_get_si_unit_z(args.brick));
    g_object_set(args.gmodel, "si-unit-x", zunit, "axis-label-bottom", "z", NULL);
    update_graph_ordinate(&args);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    gwy_graph_model_add_curve(args.gmodel, gcmodel);
    g_object_unref(gcmodel);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);

end:
    g_object_unref(args.params);
    g_object_unref(args.gmodel);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GtkWidget *hbox, *dataview, *graph, *area, *align;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDataField *field;
    GwyDialog *dialog;
    const guchar *gradient;
    gint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.xvf = gwy_brick_get_value_format_x(args->brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    gui.yvf = gwy_brick_get_value_format_y(args->brick, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    gui.data = gwy_container_new();
    gui.orig_preview = gwy_container_get_object(data, gwy_app_get_brick_preview_key_for_id(id));
    field = gwy_data_field_new_alike(gui.orig_preview, TRUE);
    gwy_container_pass_object(gui.data, gwy_app_get_data_key_for_id(0), field);

    gui.dialog = gwy_dialog_new(_("Summarize Volume Planes"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 4);

    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gui.image_selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Rectangle", 1, TRUE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    update_image_selection(&gui);

    g_object_set(args->gmodel, "label-visible", FALSE, NULL);  /* Only here. */
    graph = gwy_graph_new(args->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, 4*PREVIEW_SIZE/3, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_max_objects(gui.graph_selection, 1);
    update_graph_selection(&gui);

    hbox = gwy_hbox_new(24);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(hbox), construct_selection(&gui), FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_QUANTITY);
    gwy_param_table_combo_set_filter(table, PARAM_QUANTITY, filter_quantity, &gui, NULL);
    gwy_param_table_append_info(table, INFO_VALUE, _("Value"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_LEVEL);
    gwy_param_table_slider_restrict_range(table, PARAM_LEVEL, 0, gwy_brick_get_zres(args->brick)-1);
    gwy_param_table_slider_add_alt(table, PARAM_LEVEL);
    if (args->calibration)
        gwy_param_table_alt_set_calibration(table, PARAM_LEVEL, args->calibration);
    else
        gwy_param_table_alt_set_brick_pixel_z(table, PARAM_LEVEL, args->brick);
    gwy_param_table_add_enabler(table, PARAM_SHOW_PLANE, PARAM_LEVEL);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.image_selection, "changed", G_CALLBACK(image_selection_changed), &gui);
    g_signal_connect_swapped(gui.graph_selection, "changed", G_CALLBACK(graph_selection_changed), &gui);
    for (i = PARAM_COL; i <= PARAM_HEIGHT; i++)
        g_signal_connect_swapped(gui.adj[i], "value-changed", G_CALLBACK(coord_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    gwy_si_unit_value_format_free(gui.xvf);
    gwy_si_unit_value_format_free(gui.yvf);
    GWY_SI_VALUE_FORMAT_FREE(gui.vf);

    return outcome;
}

static GtkAdjustment*
attach_coord_row(GtkTable *table, gint row, gint value, gint max, const gchar *name, gint id,
                 GtkWidget **real_label)
{
    GtkWidget *label, *spin;
    GtkAdjustment *adj;

    label = gtk_label_new_with_mnemonic(name);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    adj = GTK_ADJUSTMENT(gtk_adjustment_new(value, 0, max, 1, 10, 0));
    g_object_set_data(G_OBJECT(adj), "id", GINT_TO_POINTER(id));
    spin = gtk_spin_button_new(adj, 0, 0);
    gtk_entry_set_width_chars(GTK_ENTRY(spin), 4);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), spin);
    gtk_table_attach(table, spin, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);

    label = *real_label = gtk_label_new(NULL);
    gtk_label_set_width_chars(GTK_LABEL(label), 12);
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 1, 2, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("px"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 3, 4, row, row+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    return adj;
}

static GtkWidget*
construct_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
    GtkTable *table;

    table = GTK_TABLE(gtk_table_new(6, 4, FALSE));
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_col_spacings(table, 8);
    gtk_table_set_row_spacings(table, 2);

    gtk_table_attach(table, gwy_label_new_header(_("Origin")), 0, 2, 0, 1, GTK_FILL, 0, 0, 0);
    gui->adj[PARAM_COL] = attach_coord_row(table, 1, gwy_params_get_int(params, PARAM_COL), xres, _("_X"),
                                           PARAM_COL, &gui->real_coord[PARAM_COL]);
    gui->adj[PARAM_ROW] = attach_coord_row(table, 2, gwy_params_get_int(params, PARAM_ROW), yres, _("_Y"),
                                           PARAM_ROW, &gui->real_coord[PARAM_ROW]);
    gtk_table_attach(table, gwy_label_new_header(_("Size")), 0, 2, 3, 4, GTK_FILL, 0, 0, 0);
    gui->adj[PARAM_WIDTH] = attach_coord_row(table, 4, gwy_params_get_int(params, PARAM_WIDTH), xres, _("_Width"),
                                             PARAM_WIDTH, &gui->real_coord[PARAM_WIDTH]);
    gui->adj[PARAM_HEIGHT] = attach_coord_row(table, 5, gwy_params_get_int(params, PARAM_HEIGHT), yres, _("_Height"),
                                              PARAM_HEIGHT, &gui->real_coord[PARAM_HEIGHT]);

    return GTK_WIDGET(table);
}

static void
image_selection_changed(ModuleGUI *gui,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
    gint newcol, newrow, newwidth, newheight;
    gdouble xy[4];

    newwidth = newheight = 0;
    if (gwy_selection_get_object(selection, 0, xy)) {
        GWY_ORDER(gdouble, xy[0], xy[2]);
        GWY_ORDER(gdouble, xy[1], xy[3]);

        newcol = CLAMP(gwy_brick_rtoi(brick, xy[0]), 0, xres-1);
        newrow = CLAMP(gwy_brick_rtoi(brick, xy[1]), 0, yres-1);
        newwidth = CLAMP(gwy_brick_rtoi(brick, xy[2])+1, 0, xres) - newcol;
        newheight = CLAMP(gwy_brick_rtoi(brick, xy[3])+1, 0, yres) - newrow;
        gwy_debug("new %d×%d at %d,%d", newwidth, newheight, newcol, newrow);
    }
    if (newwidth < 4 || newheight < 4) {
        newcol = newrow = 0;
        newwidth = xres;
        newheight = yres;
        gwy_debug("newfix %d×%d at %d,%d", newwidth, newheight, newcol, newrow);
    }

    /* NB: This does not change any default -1 to meaningful selection because -1 was already outside of the
     * adjustment range. */
    if (gui->rectangle_source == SELECTION_SOURCE_NOTHING) {
        gui->rectangle_source = SELECTION_SOURCE_SELECTION;
        gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->adj[PARAM_COL]), newcol);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->adj[PARAM_ROW]), newrow);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->adj[PARAM_WIDTH]), newwidth);
        gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->adj[PARAM_HEIGHT]), newheight);
        gui->rectangle_source = SELECTION_SOURCE_NOTHING;
    }
}

static gboolean
filter_quantity(const GwyEnum *enumval, gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    const PlaneStatQuantInfo *info = get_quantity_info(enumval->value);

    g_return_val_if_fail(info, FALSE);
    if (!args->lateral_equal && (info->flags & GWY_PLANE_STAT_LATERAL_EQUAL))
        return FALSE;
    if (!args->all_equal && (info->flags & GWY_PLANE_STAT_ALL_EQUAL))
        return FALSE;
    return TRUE;
}

static void
coord_changed(ModuleGUI *gui, GtkAdjustment *adj)
{
    gint id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(adj), "id"));

    gwy_params_set_int(gui->args->params, id, gwy_adjustment_get_int(adj));
    if (gui->rectangle_source == SELECTION_SOURCE_NOTHING) {
        gui->rectangle_source = SELECTION_SOURCE_NUMERIC;
        gwy_param_table_param_changed(gui->table, id);
        gui->rectangle_source = SELECTION_SOURCE_NOTHING;
    }
    else
        gwy_param_table_param_changed(gui->table, id);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    GwyParams *params = args->params;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
    gint i, v;

    if (id == PARAM_COL) {
        v = gwy_params_get_int(params, PARAM_COL);
        if (v + gwy_params_get_int(params, PARAM_WIDTH) > xres)
            gtk_adjustment_set_value(gui->adj[PARAM_WIDTH], xres - v);
        g_object_set(gui->adj[PARAM_WIDTH], "upper", 1.0*(xres - v), NULL);
    }
    else if (id == PARAM_ROW) {
        v = gwy_params_get_int(params, PARAM_ROW);
        if (v + gwy_params_get_int(params, PARAM_HEIGHT) > yres)
            gtk_adjustment_set_value(gui->adj[PARAM_HEIGHT], yres - v);
        g_object_set(gui->adj[PARAM_HEIGHT], "upper", 1.0*(yres - v), NULL);
    }

    if (id < 0 || id == PARAM_QUANTITY) {
        update_graph_ordinate(args);
        gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    }

    if (id < 0 || id == PARAM_COL || id == PARAM_ROW || id == PARAM_WIDTH || id == PARAM_HEIGHT) {
        if (gui->rectangle_source == SELECTION_SOURCE_NUMERIC)
            update_image_selection(gui);
        for (i = PARAM_COL; i <= PARAM_HEIGHT; i++)
            update_real_size(gui, i);
    }
    if (id < 0 || id == PARAM_LEVEL)
        update_graph_selection(gui);

    if (id < 0 || id == PARAM_LEVEL || id == PARAM_SHOW_PLANE)
        show_extracted_plane(gui);
    if (id != PARAM_UPDATE && id != PARAM_TARGET_GRAPH && id != PARAM_LEVEL)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
update_graph_ordinate(ModuleArgs *args)
{
    const PlaneStatQuantInfo *info = get_quantity_info(gwy_params_get_enum(args->params, PARAM_QUANTITY));
    GwySIUnit *xunit = gwy_brick_get_si_unit_x(args->brick);
    GwySIUnit *yunit = gwy_brick_get_si_unit_y(args->brick);
    GwySIUnit *wunit = gwy_brick_get_si_unit_w(args->brick);
    GwySIUnit *unit = gwy_si_unit_new(NULL);

    gwy_si_unit_power_multiply(xunit, info->powerx, yunit, info->powery, unit);
    gwy_si_unit_power_multiply(unit, 1, wunit, info->powerw, unit);
    g_object_set(args->gmodel, "axis-label-left", info->symbol, "si-unit-y", unit, NULL);
    g_object_unref(unit);
}

static void
update_real_size(ModuleGUI *gui, gint id)
{
    GwyBrick *brick = gui->args->brick;
    gint i = gwy_params_get_int(gui->args->params, id);
    GwySIValueFormat *vf;
    gdouble v;
    gchar *s;

    if (id == PARAM_HEIGHT || id == PARAM_ROW) {
        vf = gui->yvf;
        v = gwy_brick_jtor(brick, i);
    }
    else {
        vf = gui->xvf;
        v = gwy_brick_itor(brick, i);
    }

    s = g_strdup_printf("%.*f%s%s", vf->precision, v/vf->magnitude, vf->units ? " " : "", vf->units);
    gtk_label_set_markup(GTK_LABEL(gui->real_coord[id]), s);
    g_free(s);
}

static void
graph_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint zres, level;
    gdouble z;

    /* XXX: When clicking on a new position graph emits two updates, one with old selected line removed and another
     * with the new selection. It is silly. Just ignore updates with no selected line. */
    if (!gwy_selection_get_object(selection, 0, &z))
        return;

    level = GWY_ROUND(gwy_brick_rtok_cal(brick, z));
    zres = gwy_brick_get_zres(brick);
    level = CLAMP(level, 0, zres-1);
    gwy_param_table_set_int(gui->table, PARAM_LEVEL, level);
}

static void
show_extracted_plane(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *field = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));

    if (gwy_params_get_boolean(args->params, PARAM_SHOW_PLANE))
        gwy_brick_extract_xy_plane(args->brick, field, gwy_params_get_int(args->params, PARAM_LEVEL));
    else
        gwy_data_field_copy(gui->orig_preview, field, TRUE);

    gwy_data_field_data_changed(field);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyGraphCurveModel *gcmodel;
    GwySIValueFormat *vf;
    gint n, zlevel;
    GwySIUnit *unit;
    gdouble v;
    gchar *s;

    execute(args);

    if (!gwy_graph_model_get_n_curves(args->gmodel))
        return;

    gcmodel = gwy_graph_model_get_curve(args->gmodel, 0);
    zlevel = gwy_params_get_int(args->params, PARAM_LEVEL);
    n = gwy_graph_curve_model_get_ndata(gcmodel);
    if (CLAMP(zlevel, 0, n-1) != zlevel)
        return;

    v = gwy_graph_curve_model_get_ydata(gcmodel)[zlevel];
    g_object_get(args->gmodel, "si-unit-y", &unit, NULL);
    gui->vf = gwy_si_unit_get_format_with_digits(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, v, 3, gui->vf);
    g_object_unref(unit);
    vf = gui->vf;
    s = g_strdup_printf("%.*f%s%s", vf->precision, v/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_param_table_info_set_valuestr(gui->table, INFO_VALUE, s);
    g_free(s);
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        gwy_selection_clear(gui->graph_selection);
}

static void
update_image_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint col = gwy_params_get_int(args->params, PARAM_COL);
    gint row = gwy_params_get_int(args->params, PARAM_ROW);
    gint w = gwy_params_get_int(args->params, PARAM_WIDTH);
    gint h = gwy_params_get_int(args->params, PARAM_HEIGHT);
    gdouble xy[4];

    if (w && h) {
        xy[0] = gwy_brick_jtor(brick, col + 0.5);
        xy[1] = gwy_brick_itor(brick, row + 0.5);
        xy[2] = gwy_brick_jtor(brick, col + w - 0.5);
        xy[3] = gwy_brick_itor(brick, row + h - 0.5);
        gwy_selection_set_data(gui->image_selection, 1, xy);
    }
    else
        gwy_selection_clear(gui->image_selection);
}

static void
update_graph_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble z = gwy_brick_ktor_cal(args->brick, gwy_params_get_int(args->params, PARAM_LEVEL));

    gwy_selection_set_data(gui->graph_selection, 1, &z);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    const PlaneStatQuantInfo *info = get_quantity_info(gwy_params_get_enum(params, PARAM_QUANTITY));
    PlaneStatFunc func = info->func;
    GwyBrick *brick = args->brick;
    GwyGraphCurveModel *gcmodel;
    gdouble zreal = gwy_brick_get_zreal(brick), zoff = gwy_brick_get_zoffset(brick);
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint col = gwy_params_get_int(args->params, PARAM_COL);
    gint row = gwy_params_get_int(args->params, PARAM_ROW);
    gint w = gwy_params_get_int(args->params, PARAM_WIDTH);
    gint h = gwy_params_get_int(args->params, PARAM_HEIGHT);
    gdouble *xdata, *ydata;
    gint k, n;

    if (args->calibration)
        xdata = g_memdup(gwy_data_line_get_data(args->calibration), zres*sizeof(gdouble));
    else {
        xdata = g_new(gdouble, zres);
        for (k = 0; k < zres; k++)
            xdata[k] = (k + 0.5)*zreal/zres + zoff;
    }
    ydata = g_new(gdouble, zres);

    gwy_debug("selected %dx%d at (%d,%d)", w, h, col, row);
    if (w < 4 || h < 4 || col < 0 || row < 0) {
        col = row = 0;
        w = xres;
        h = yres;
        gwy_debug("fixed to %dx%d at (%d,%d)", w, h, col, row);
    }

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,ydata,zres,w,h,col,row,func)
#endif
    {
        GwyDataField *field = gwy_data_field_new(w, h, 1.0*w, 1.0*h, FALSE);
        gint kk, kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);

        for (kk = kfrom; kk < kto; kk++) {
            gwy_brick_extract_plane(brick, field, col, row, kk, w, h, -1, FALSE);
            ydata[kk] = func(field);
        }
        g_object_unref(field);
    }

    for (k = n = 0; k < zres; k++) {
        if (gwy_isinf(ydata[k]) || gwy_isnan(ydata[k]) || fabs(ydata[k]) > 0.01*G_MAXDOUBLE)
            continue;
        xdata[n] = xdata[k];
        ydata[n] = ydata[k];
        n++;
    }

    gcmodel = gwy_graph_model_get_curve(args->gmodel, 0);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
    g_object_set(gcmodel, "description", _(info->name), NULL);

    g_free(ydata);
    g_free(xdata);
}

static const PlaneStatQuantInfo*
get_quantity_info(PlaneStatQuantity quantity)
{
    const PlaneStatQuantInfo *info = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(quantities); i++) {
        info = quantities + i;
        if (info->quantity == quantity)
            return info;
    }
    g_assert_not_reached();
    return NULL;
}

static gdouble
get_plane_range(GwyDataField *field)
{
    gdouble min, max;

    gwy_data_field_get_min_max(field, &min, &max);
    return max - min;
}

static gdouble
get_plane_Sa(GwyDataField *field)
{
    gdouble Sa;

    gwy_data_field_get_stats(field, NULL, &Sa, NULL, NULL, NULL);

    return Sa;
}

static gdouble
get_plane_median(GwyDataField *field)
{
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);

    /* Reshuffle the data because the field is just a scratch buffer anyway. */
    return gwy_math_median(xres*yres, gwy_data_field_get_data(field));
}

static gdouble
get_plane_skew(GwyDataField *field)
{
    gdouble rms, skew;

    gwy_data_field_get_stats(field, NULL, NULL, &rms, &skew, NULL);

    return rms > 0.0 ? skew : 0.0;
}

static gdouble
get_plane_kurtosis(GwyDataField *field)
{
    gdouble rms, kurtosis;

    gwy_data_field_get_stats(field, NULL, NULL, &rms, NULL, &kurtosis);

    return rms > 0.0 ? kurtosis : 0.0;
}

static gdouble
get_plane_entropy_deficit(GwyDataField *field)
{
    gdouble H = gwy_data_field_get_entropy(field);
    gdouble rms = gwy_data_field_get_rms(field);

    if (rms > 10.0*G_MINDOUBLE && H < 0.1*G_MAXDOUBLE)
        return ENTROPY_NORMAL + log(rms) - H;
    return 0.0;
}

static inline void
clamp_int_param(GwyParams *params, gint id, gint min, gint max, gint default_value)
{
    gint p = gwy_params_get_int(params, id);

    if (p < min || p > max)
        gwy_params_set_int(params, id, default_value);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    PlaneStatQuantity quantity = gwy_params_get_enum(params, PARAM_QUANTITY);
    const PlaneStatQuantInfo *info = get_quantity_info(quantity);
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint m;

    if (!args->all_equal && (info->flags & GWY_PLANE_STAT_ALL_EQUAL))
        gwy_params_set_enum(params, PARAM_QUANTITY, GWY_PLANE_STAT_MEAN);
    else if (!args->lateral_equal && (info->flags & GWY_PLANE_STAT_LATERAL_EQUAL))
        gwy_params_set_enum(params, PARAM_QUANTITY, GWY_PLANE_STAT_MEAN);

    clamp_int_param(params, PARAM_COL, 0, xres-4, 0);
    clamp_int_param(params, PARAM_ROW, 0, yres-4, 0);
    m = xres - gwy_params_get_int(params, PARAM_COL);
    clamp_int_param(params, PARAM_WIDTH, 0, m, m);
    m = yres - gwy_params_get_int(params, PARAM_ROW);
    clamp_int_param(params, PARAM_HEIGHT, 0, m, m);
    clamp_int_param(params, PARAM_LEVEL, 0, zres-1, zres/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
