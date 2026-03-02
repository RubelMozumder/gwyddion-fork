/*
 *  $Id: cmap_simplemech.c 25639 2023-09-07 12:23:42Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/lawn.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    RESPONSE_FIT = 100,
};

enum {
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_SEGMENT_APPROACH,
    PARAM_SEGMENT_RETRACT,
    PARAM_BASELINE_RANGE,
    PARAM_FIT_UPPER,
    PARAM_FIT_LOWER,
    PARAM_RADIUS,
    PARAM_NU,
    PARAM_OUTPUT,
    PARAM_DISPLAY,
    PARAM_XPOS,
    PARAM_YPOS,
    WIDGET_RESULTS,
};

typedef enum {
    OUTPUT_DMT_MODULUS = 0,
    OUTPUT_ADHESION    = 1,
    OUTPUT_DEFORMATION = 2,
    OUTPUT_DISSIPATION = 3,
    OUTPUT_BASELINE    = 4,
    OUTPUT_PEAK        = 5,
    NOUTPUTS,
    DISPLAY_ORIGINAL = 100,
} NanomechOutput;

typedef struct {
    const gchar *name;
    const gchar *label;
    gint power_x;
    gint power_y;
    gint power_u;  /* Pa */
    gint power_v;  /* eV */
} NanomechOutputInfo;

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyDataField *result[NOUTPUTS];
    GwyDataField *preview;
    GwyDataField *mask;
    /* Cached input data properties. */
    gint nsegments;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_fit;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwySelection *selection;
    GwyResults *results;
    const gchar **result_ids;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             cmap_simplemech         (GwyContainer *data,
                                                 GwyRunType runtype);
static gboolean         execute                 (ModuleArgs *args,
                                                 GtkWindow *wait_window);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GwyResults*      create_results          (void);
static void             dialog_response         (ModuleGUI *gui,
                                                 gint response);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             set_selection           (ModuleGUI *gui);
static void             point_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static gboolean         fit_one_curve           (GwyLawn *lawn,
                                                 gint col,
                                                 gint row,
                                                 gint abscissa,
                                                 gint ordinate,
                                                 gint segment_approach,
                                                 gint segment_retract,
                                                 gdouble baseline_range,
                                                 gdouble fit_upper,
                                                 gdouble fit_lower,
                                                 gdouble radius,
                                                 gdouble nu,
                                                 gdouble *result,
                                                 gdouble *xp,
                                                 gdouble *yp,
                                                 gdouble *xb,
                                                 gdouble *yb,
                                                 gdouble *xf,
                                                 gdouble *yf,
                                                 gint nf);
static void             extract_one_curve       (GwyLawn *lawn,
                                                 GwyGraphCurveModel *gcmodel,
                                                 gint col,
                                                 gint row,
                                                 gint segment,
                                                 gint abscissa,
                                                 gint ordinate);
static void             update_graph_model_props(ModuleGUI *gui);
static void             sanitise_params         (ModuleArgs *args);

/* NB: Items are directly indexed by NanomechOutput values.  Fix GUI order in gwy_enum_fill_from_struct() if needed. */
static const NanomechOutputInfo output_info[] = {
    { "modulus",     N_("DMT modulus"),      0, 0, 1, 0  },
    { "adhesion",    N_("Adhesion"),         0, 1, 0, 0, },
    { "deformation", N_("Deformation"),      1, 0, 0, 0, },
    { "dissipation", N_("Dissipated work"),  0, 0, 0, 1, },
    { "baseline",    N_("Baseline force"),   0, 1, 0, 0  },
    { "peak",        N_("Maximum force"),    0, 1, 0, 0, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Get simple mechanical quantities"),
    "Yeti <yeti@gwyddion.net>, Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_simplemech)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_simplemech",
                                (GwyCurveMapFunc)&cmap_simplemech,
                                N_("/_Nanomechanical Fit..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Evaluate DMT modulus, adhesion, deformation and dissipation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum displays[NOUTPUTS+1];
    static GwyEnum *outputs = NULL;
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    gwy_enum_fill_from_struct(displays+1, NOUTPUTS, output_info, sizeof(NanomechOutputInfo),
                              G_STRUCT_OFFSET(NanomechOutputInfo, label), -1);
    displays[0].name = N_("Default");
    displays[0].value = DISPLAY_ORIGINAL;

    outputs = gwy_enum_fill_from_struct(NULL, NOUTPUTS, output_info, sizeof(NanomechOutputInfo),
                                        G_STRUCT_OFFSET(NanomechOutputInfo, label), -1);
    for (i = 0; i < NOUTPUTS; i++)
        outputs[i].value = (1 << outputs[i].value);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Z curve"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Force curve"));
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT_APPROACH, "segment_approach", _("Approach"));
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT_RETRACT, "segment_retract", _("Retract"));
    gwy_param_def_add_double(paramdef, PARAM_BASELINE_RANGE, "baseline", _("Baseline _range"), 0.0, 0.5, 0.2);
    gwy_param_def_add_double(paramdef, PARAM_FIT_UPPER, "upper", _("Fit _upper limit"), 0.6, 1.0, 0.8);
    gwy_param_def_add_double(paramdef, PARAM_FIT_LOWER, "lower", _("Fit _lower limit"), 0.0, 0.4, 0.2);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("_Tip radius"), 0, 500e-9, 20e-9);
    gwy_param_def_add_double(paramdef, PARAM_NU, "nu", _("_Poisson's ratio"), 0, 1, 0.25);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT, "output", _("Output images"),
                               outputs, NOUTPUTS, (1 << OUTPUT_DMT_MODULUS));
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, NULL, gwy_sgettext("verb|Display"),
                              displays, NOUTPUTS+1, DISPLAY_ORIGINAL);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    return paramdef;
}

static void
cmap_simplemech(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyDataField *newmask;
    guint i, output;
    GtkWidget *dialog;
    gint oldid, newid;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);

    /* There are many other nonsensical inputs, but they will just produce garbage.  With unsegmented curves we cannot
     * proceed at all. */
    if (!args.nsegments) {
        if (gwy_app_data_browser_get_gui_enabled() || gwy_app_wait_get_enabled()) {
            dialog = gtk_message_dialog_new(gwy_app_find_window_for_curve_map(data, oldid),
                                            GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                            _("%s: Curves have to be segmented."), _("Nanomechanical Fit"));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        return;
    }

    args.params = gwy_params_new_from_settings(define_module_params());
    args.preview = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(oldid));
    sanitise_params(&args);

    for (i = 0; i < NOUTPUTS; i++) {
        args.result[i] = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                            gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn),
                                            TRUE);
        gwy_data_field_set_xoffset(args.result[i], gwy_lawn_get_xoffset(lawn));
        gwy_data_field_set_yoffset(args.result[i], gwy_lawn_get_yoffset(lawn));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.result[i]), gwy_lawn_get_si_unit_xy(lawn));
    }
    args.mask = gwy_data_field_new_alike(args.result[0], TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        if (!execute(&args, gwy_app_find_window_for_curve_map(data, oldid)))
            goto end;
    }
    output = gwy_params_get_flags(args.params, PARAM_OUTPUT);
    for (i = 0; i < NOUTPUTS; i++) {
        if (!(output & (1 << i)))
            continue;
        newid = gwy_app_data_browser_add_data_field(args.result[i], data, TRUE);
        gwy_container_set_const_string(data, gwy_app_get_data_title_key_for_id(newid), _(output_info[i].label));
        if (gwy_data_field_get_max(args.mask) > 0.0) {
            newmask = gwy_data_field_duplicate(args.mask);
            gwy_container_pass_object(data, gwy_app_get_mask_key_for_id(newid), newmask);
        }
        if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(oldid), &gradient)) {
            gwy_container_set_const_string(data,
                                           gwy_app_get_data_palette_key_for_id(newid), gradient);
        }

        gwy_app_channel_log_add(data, -1, newid, "cmap::cmap_linestat", NULL);
    }

end:
    for (i = 0; i < NOUTPUTS; i++)
        g_object_unref(args.result[i]);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyVectorLayer *vlayer = NULL;
    GwyGraphCurveModel *gcmodel;
    const guchar *gradient;
    guint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gui.gmodel = gwy_graph_model_new();
    gui.results = create_results();
    gui.result_ids = g_new(const gchar*, NOUTPUTS);
    for (i = 0; i < NOUTPUTS; i++)
        gui.result_ids[i] = output_info[i].name;
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Nanomechanical Fit"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Fit"), RESPONSE_FIT);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

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

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", _("Approach"),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 "description", _("Retract"),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_POINTS,
                 "color", gwy_graph_get_preset_color(0),
                 "description", _("Control points"),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(2),
                 "description", _("Baseline fit"),
                 "line-width", 3,
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(3),
                 "description", _("DMT fit"),
                 "line-width", 3,
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);



    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT_APPROACH, args->lawn);
    gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT_RETRACT, args->lawn);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_BASELINE_RANGE);
    gwy_param_table_slider_set_factor(table, PARAM_BASELINE_RANGE, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_BASELINE_RANGE, "%");
    gwy_param_table_append_slider(table, PARAM_FIT_UPPER);
    gwy_param_table_slider_set_factor(table, PARAM_FIT_UPPER, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_FIT_UPPER, "%");
    gwy_param_table_append_slider(table, PARAM_FIT_LOWER);
    gwy_param_table_slider_set_factor(table, PARAM_FIT_LOWER, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_FIT_LOWER, "%");
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_set_factor(table, PARAM_RADIUS, 1e9);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, "nm");
    gwy_param_table_append_slider(table, PARAM_NU);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_fit = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Fit Results"));
    gwy_param_table_append_resultsv(table, WIDGET_RESULTS, gui.results, gui.result_ids, NOUTPUTS);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.data);
    g_object_unref(gui.results);
    g_free(gui.result_ids);

    return outcome;
}

static GwyResults*
create_results(void)
{
    GwyResults *results;
    guint i;

    results = gwy_results_new();
    gwy_results_add_header(results, N_("Results"));
    for (i = 0; i < NOUTPUTS; i++) {
        gwy_results_add_value(results, output_info[i].name, output_info[i].label,
                              "power-x", output_info[i].power_x,
                              "power-y", output_info[i].power_y,
                              "power-u", output_info[i].power_u,
                              "power-v", output_info[i].power_v,
                              NULL);
    }
    gwy_results_set_unit_str(results, "u", "Pa");
    gwy_results_set_unit_str(results, "v", "eV");

    return results;
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_FIT) {
        if (execute(gui->args, GTK_WINDOW(gui->dialog)))
            gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
        gwy_data_field_data_changed(gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0)));
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_DISPLAY) {
        gint i = gwy_params_get_enum(params, PARAM_DISPLAY);
        if (i < NOUTPUTS)
            gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->result[i]);
        else
            gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->preview);
    }
    if (id < 0 || id == PARAM_OUTPUT) {
        guint output = gwy_params_get_flags(params, PARAM_OUTPUT);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !!output);
    }

    if (id != PARAM_OUTPUT && id != PARAM_DISPLAY)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
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
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    gwy_selection_get_object(selection, id, xy);
    i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
    i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));

    gwy_param_table_param_changed(gui->table, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table, PARAM_YPOS);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    gint segment_approach = gwy_params_get_int(params, PARAM_SEGMENT_APPROACH);
    gint segment_retract = gwy_params_get_int(params, PARAM_SEGMENT_RETRACT);
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    gdouble baseline_range = gwy_params_get_double(params, PARAM_BASELINE_RANGE);
    gdouble fit_upper = gwy_params_get_double(params, PARAM_FIT_UPPER);
    gdouble fit_lower = gwy_params_get_double(params, PARAM_FIT_LOWER);
    gdouble radius = gwy_params_get_double(params, PARAM_RADIUS);
    gdouble nu = gwy_params_get_double(params, PARAM_NU);


    GwyGraphCurveModel *gcmodel_approach = gwy_graph_model_get_curve(gui->gmodel, 0);
    GwyGraphCurveModel *gcmodel_retract = gwy_graph_model_get_curve(gui->gmodel, 1);
    GwyGraphCurveModel *gcmodel_points = gwy_graph_model_get_curve(gui->gmodel, 2);
    GwyGraphCurveModel *gcmodel_baseline = gwy_graph_model_get_curve(gui->gmodel, 3);
    GwyGraphCurveModel *gcmodel_dmt = gwy_graph_model_get_curve(gui->gmodel, 4);
    GwyResults *results = gui->results;
    gdouble values[NOUTPUTS];
    guint i;
    gdouble *xp, *yp, *xb, *yb, *xf, *yf;
    gint nf = 100;

    extract_one_curve(args->lawn, gcmodel_approach, col, row, segment_approach, abscissa, ordinate);
    extract_one_curve(args->lawn, gcmodel_retract, col, row, segment_retract, abscissa, ordinate);
    update_graph_model_props(gui);

    gwy_results_set_unit(results, "x", gwy_lawn_get_si_unit_curve(args->lawn, abscissa));
    gwy_results_set_unit(results, "y", gwy_lawn_get_si_unit_curve(args->lawn, ordinate));

    xp = g_new(gdouble, 3);
    yp = g_new(gdouble, 3);
    xb = g_new(gdouble, 2);
    yb = g_new(gdouble, 2);
    xf = g_new(gdouble, nf);
    yf = g_new(gdouble, nf);

    if (fit_one_curve(args->lawn, col, row, abscissa, ordinate, segment_approach, segment_retract,
                      baseline_range, fit_upper, fit_lower, radius, nu, values,
                      xp, yp, xb, yb, xf, yf, nf)) {
        /* values[] are filled in base units; convert here. */
        for (i = 0; i < NOUTPUTS; i++)
            gwy_results_fill_values(results, output_info[i].name, values[i], NULL);
        gwy_param_table_results_fill(gui->table_fit, WIDGET_RESULTS);

        gwy_graph_curve_model_set_data(gcmodel_points, xp, yp, 3);
        gwy_graph_curve_model_set_data(gcmodel_baseline, xb, yb, 2);
        gwy_graph_curve_model_set_data(gcmodel_dmt, xf, yf, nf);
    }
    else {
        gwy_param_table_results_clear(gui->table_fit, WIDGET_RESULTS);
    }
    g_free(xp);
    g_free(yp);
    g_free(xb);
    g_free(yb);
    g_free(xf);
    g_free(yf);
}

//param[0]: F_ad, param[1]: E (modulus), param[2]: R, param[3]: xshift, param[4] nu
static gdouble
func_dmt(gdouble x,
         G_GNUC_UNUSED gint n_param,
         const gdouble *param,
         G_GNUC_UNUSED gpointer user_data,
         gboolean *fres)
{
    /*xc, F_ad, R, E, nu*/
    double xr = param[0] - x;
    *fres = TRUE;
    if (xr > 0)
        return 4.0*param[3]/3.0/(1-param[4]*param[4])*sqrt(param[2]*xr*xr*xr) + param[1];
    else
        return param[1];
}


//xp,yp: important points to show to user, can be NULL to avoid filling it. Fixed size: 3 points
//xb,yb: baseline fit to show to user, can be NULL to avoid filling it. Fixed size: 2 points
//xf,yf: DMT fit to show to user, can be NULL to avoid filling it. Size can be set and must be allocated by user.
static gboolean
evaluate_curve(const gdouble *xdata, const gdouble *ydata,
               gint approach_from, gint approach_to, gint retract_from, gint retract_to,
               gdouble baseline_range, gdouble fit_upper, gdouble fit_lower, gdouble radius,
               gdouble nu,
               gdouble *result_modulus, gdouble *result_adhesion, gdouble *result_deformation,
               gdouble *result_dissipation, gdouble *result_baseline, gdouble *result_peak,
               gdouble *xp, gdouble *yp, gdouble *xb, gdouble *yb, gdouble *xf, gdouble *yf, gint nf)
{
    GwyNLFitter *fitter;
    gboolean fres, fit_done;
    gint i, nadata, nrdata, nbaseline;
    gint fix[5];
    gint iupper, ilower, iadhesion;
    gdouble upperval, lowerval, xupper, xlower;
    gdouble afrom, ato, rfrom, rto;
    gdouble param[5];
    gdouble modulus, adhesion, deformation, baseline, peak;
    gdouble xadhesion, xpeak, xzero, yzero;
    gdouble adis, rdis;

    const gdouble *xadata = xdata + approach_from;
    const gdouble *yadata = ydata + approach_from;
    const gdouble *xrdata = xdata + retract_from;
    const gdouble *yrdata = ydata + retract_from;

    nadata = approach_to - approach_from;
    nrdata = retract_to - retract_from;

    peak = -G_MAXDOUBLE;
    xpeak = xrdata[0];

    afrom = G_MAXDOUBLE;
    ato = -G_MAXDOUBLE;

    //get peak force and approach curve range
    adis = 0;
    for (i = 0; i < nadata; i++) {
        if (peak < yadata[i]) {
            peak = yadata[i];
            xpeak = xadata[i];
        }
        if (xadata[i] < afrom)
            afrom = xadata[i];
        if (xadata[i] > ato)
            ato = xadata[i];

        if (i < (nadata - 1))
            adis += fabs(xadata[i] - xadata[i+1])*(yadata[i] + yadata[i+1])/2.0;
    }

    //fit baseline - average value on approach curve flat part
    baseline = 0;
    nbaseline = 0;
    for (i = 0; i < nadata; i++) {
        if (xadata[i] > (ato - baseline_range*(ato - afrom))) {
            baseline += yadata[i];
            nbaseline++;
        }
    }
    if (nbaseline>0) baseline /= nbaseline;
    else baseline = yadata[nadata-1];

    //find zero force - point where we reach baseline force on approach curve when going from peak value
    xzero = xadata[nadata-1]; //safe defaults
    yzero = yadata[nadata-1];
    for (i = (nadata - 1); i > 1; i--) {
        if (yadata[i] >= baseline && yadata[i+1] < baseline) {
            xzero = xadata[i];
            yzero = yadata[i];
        }
    }

    //calculate deformation
    deformation = xzero - xpeak;

    //get adhesion as minimum on retract curve and get retract curve range
    adhesion = G_MAXDOUBLE;
    xadhesion = xrdata[0];
    iadhesion = 0;
    rfrom = G_MAXDOUBLE;
    rto = -G_MAXDOUBLE;
    rdis = 0;
    for (i = 0; i < nrdata; i++) {
        if (adhesion > yrdata[i]) {
            adhesion = yrdata[i];
            xadhesion = xrdata[i];
            iadhesion = i;
        }
        if (peak < yrdata[i]) {
            peak = yrdata[i];
            xpeak = xrdata[i];
        }
        if (xrdata[i] < rfrom)
            rfrom = xrdata[i];
        if (xrdata[i] > rto)
            rto = xrdata[i];

        if (i < nrdata-1)
            rdis += fabs(xrdata[i]-xrdata[i+1])*(yrdata[i]+yrdata[i+1])/2.0;
    }

    //find dmt fit limits as points where we reach the points on retract curve when going from peak value
    xupper = xpeak;     //safe inits
    xlower = xadhesion;
    iupper = 0;
    ilower = iadhesion;
    upperval = adhesion + fit_upper*(peak-adhesion);
    lowerval = adhesion + fit_lower*(peak-adhesion);

    for (i = (nrdata - 1); i > 1; i--) {
        if (yrdata[i] >= upperval && yrdata[i + 1] < upperval) {
            xupper = xrdata[i];
            iupper = i;
        }
        if (yrdata[i] >= lowerval && yrdata[i+1] < lowerval) {
            xlower = xrdata[i];
            ilower = i;
        }
    }

    fit_done = FALSE;
    modulus = 5e7;
    if ((ilower - iupper) > 4) {
       //fit dmt
       fitter = gwy_math_nlfit_new((GwyNLFitFunc)func_dmt, gwy_math_nlfit_diff);

       param[0] = xadhesion; //adhesion comes from direct measurement
       fix[0] = 0;

       param[1] = adhesion; //modulus comes from some guess?
       fix[1] = 1;

       param[2] = radius;  //radius comes from user
       fix[2] = 1;

       param[3] = modulus; //x shift of the curve, where this should be?
       fix[3] = 0;

       param[4] = nu;
       fix[4] = 1;


       if (gwy_math_nlfit_fit_full(fitter, ilower-iupper, xrdata + iupper, yrdata + iupper, NULL,
                                   5, param, fix, NULL, NULL) >= 0) {
          modulus = param[3];
          fit_done = TRUE;

      //    printf("param: %g %g %g %g\n", param[0], param[1], param[2], param[3], param[4]);

          if (xf && yf && nf) {
              for (i = 0; i < nf; i++) {
                  xf[i] = xupper + (gdouble)i*(xlower-xupper)/(gdouble)nf;
                  yf[i] = func_dmt(xf[i], 5, param, NULL, &fres);
//                  printf("%g %g\n", xf[i], yf[i]);
              }
          }
       } else
           printf("fit failed\n");

       gwy_math_nlfit_free(fitter);
    }

    //fill graphs if requested
    if (xp && yp) {
       xp[0] = xadhesion;
       yp[0] = adhesion;
       xp[1] = xpeak;
       yp[1] = peak;
       xp[2] = xzero;
       yp[2] = yzero;
    }

    if (xb && yb) {
       xb[0] = ato - baseline_range*(ato-afrom);
       yb[0] = baseline;
       xb[1] = ato;
       yb[1] = baseline;
    }

    *result_adhesion = adhesion-baseline;
    *result_baseline = baseline;
    *result_peak = peak;
    *result_deformation = deformation;
    *result_dissipation = (adis-rdis)/1.602176634e-19;

    if (fit_done)
         *result_modulus = modulus;
    else
        *result_modulus = 0;

    return fit_done;
}

static gboolean
fit_one_curve(GwyLawn *lawn, gint col, gint row,
              gint abscissa, gint ordinate, gint segment_approach, gint segment_retract,
              gdouble baseline_range, gdouble fit_upper, gdouble fit_lower, gdouble radius, gdouble nu,
              gdouble *result,
              gdouble *xp, gdouble *yp, gdouble *xb, gdouble *yb, gdouble *xf, gdouble *yf, gint nf)
{
    const gdouble *xdata, *ydata;
    gint ndata, approach_from, approach_end, retract_from, retract_end;
    const gint *segments;
    gdouble modulus, adhesion, deformation, dissipation, baseline, peak;
    gboolean fit_ok;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);

    if (ndata < 6)
        return FALSE;

    segments = gwy_lawn_get_segments(lawn, col, row, NULL);
    approach_from = segments[2*segment_approach];
    approach_end = segments[2*segment_approach + 1];
    retract_from = segments[2*segment_retract];
    retract_end = segments[2*segment_retract + 1];

    if (!evaluate_curve(xdata, ydata,
                        approach_from, approach_end,
                        retract_from, retract_end,
                        baseline_range, fit_upper, fit_lower, radius, nu,
                        &modulus, &adhesion, &deformation, &dissipation, &baseline, &peak,
                        xp, yp, xb, yb, xf, yf, nf))
        fit_ok = FALSE;
    else
        fit_ok = TRUE;

    result[OUTPUT_DMT_MODULUS] = modulus;
    result[OUTPUT_ADHESION] = adhesion;
    result[OUTPUT_DEFORMATION] = deformation;
    result[OUTPUT_DISSIPATION] = dissipation;
    result[OUTPUT_BASELINE] = baseline;
    result[OUTPUT_PEAK] = peak;

    return fit_ok;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gint segment_approach = gwy_params_get_int(params, PARAM_SEGMENT_APPROACH);
    gint segment_retract = gwy_params_get_int(params, PARAM_SEGMENT_RETRACT);
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    gdouble baseline_range = gwy_params_get_double(params, PARAM_BASELINE_RANGE);
    gdouble fit_upper = gwy_params_get_double(params, PARAM_FIT_UPPER);
    gdouble fit_lower = gwy_params_get_double(params, PARAM_FIT_LOWER);
    gdouble radius = gwy_params_get_double(params, PARAM_RADIUS);
    gdouble nu = gwy_params_get_double(params, PARAM_NU);

    GwySIUnit *unit, *xunit, *yunit;
    gboolean cancelled = FALSE;
    gdouble *rdata[NOUTPUTS], *mdata;
    guint i;

    /* Find segment cut points. */
    gwy_app_wait_start(wait_window, _("Fitting in progress..."));

    xunit = gwy_lawn_get_si_unit_curve(lawn, abscissa);
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    for (i = 0; i < NOUTPUTS; i++) {
        unit = gwy_data_field_get_si_unit_z(args->result[i]);
        if (output_info[i].power_u)
            gwy_si_unit_set_from_string(unit, "Pa");
        else if (output_info[i].power_v)
            gwy_si_unit_set_from_string(unit, "eV");
        else
            gwy_si_unit_power_multiply(xunit, output_info[i].power_x, yunit, output_info[i].power_y, unit);
        gwy_data_field_clear(args->result[i]);
        rdata[i] = gwy_data_field_get_data(args->result[i]);
    }
    gwy_data_field_clear(args->mask);
    mdata = gwy_data_field_get_data(args->mask);

/*#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(lawn,xres,yres,abscissa,ordinate,segment_approach,segment_retract,rdata,mdata,cancelled,\
                   baseline_range,fit_upper,fit_lower,radius)
#endif*/
    {
        gdouble values[NOUTPUTS];
        guint kfrom = gwy_omp_chunk_start(xres*yres);
        guint kto = gwy_omp_chunk_end(xres*yres);
        guint k, j;

        for (k = kfrom; k < kto; k++) {
            if (fit_one_curve(lawn, k % xres, k/xres, abscissa, ordinate, segment_approach, segment_retract,
                              baseline_range, fit_upper, fit_lower, radius, nu,
                              values, NULL, NULL, NULL, NULL, NULL, NULL, 0)) {
                for (j = 0; j < NOUTPUTS; j++)
                    rdata[j][k] = values[j];
            }
            else
                mdata[k] = 1.0;

            if (k % 1000)
                continue;
            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, k, kfrom, kto, &cancelled))
                break;
        }
    }

    gwy_app_wait_finish();

    if (cancelled) {
        for (i = 0; i < NOUTPUTS; i++)
            gwy_data_field_clear(args->result[i]);
        return FALSE;
    }

    for (i = 0; i < NOUTPUTS; i++) {
        if (gwy_data_field_get_max(args->mask) > 0.0)
            gwy_data_field_laplace_solve(args->result[i], args->mask, -1, 1.0);
    }
    return TRUE;
}

static void
extract_one_curve(GwyLawn *lawn, GwyGraphCurveModel *gcmodel,
                  gint col, gint row, gint segment,
                  gint abscissa, gint ordinate)
{
    const gdouble *xdata, *ydata;
    gint ndata, from, end;
    const gint *segments;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);

    segments = gwy_lawn_get_segments(lawn, col, row, NULL);
    from = segments[2*segment];
    end = segments[2*segment + 1];
    xdata += from;
    ydata += from;
    ndata = end - from;

    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
}

static void
update_graph_model_props(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwyGraphModel *gmodel = gui->gmodel;
    GwySIUnit *xunit, *yunit;
    const gchar *xlabel, *ylabel;

    xunit = gwy_lawn_get_si_unit_curve(lawn, abscissa);
    xlabel = gwy_lawn_get_curve_label(lawn, abscissa);
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    ylabel = gwy_lawn_get_curve_label(lawn, ordinate);
    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel ? xlabel : _("Untitled"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 NULL);
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
