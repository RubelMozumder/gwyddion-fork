/*
 *  $Id: volume_1dfft.c 26354 2024-05-21 08:22:32Z yeti-dn $
 *  Copyright (C) 2015-2023 David Necas (Yeti).
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
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correlation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    PREVIEW_GRAPH_SIZE = 420,
};

typedef enum {
    SUPPRESS_NULL         = 0,
    SUPPRESS_NEIGBOURHOOD = 1
} GwyFFTFilt1DSuppressType;

typedef enum {
    OUTPUT_MARKED   = 0,
    OUTPUT_UNMARKED = 1
} GwyFFTFilt1DOutputType;

typedef enum {
    MODULUS_INDIVIDUAL  = 0,
    MODULUS_AVERAGE     = 1
} GwyModulusType;

enum {
    PARAM_Z,
    PARAM_SUPPRESS,
    PARAM_OUTPUT,
    PARAM_DIRECTION,
    PARAM_INTERPOLATION,
    PARAM_MODULUS
//    PARAM_UPDATE
};


typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
    GwyDataLine *modulus;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
    GwyGraphModel *gmodel;
    GwySelection *selection;
    gboolean computed;
} ModuleGUI;


static gboolean              module_register          (void);
static GwyParamDef*          define_module_params     (void);
static void                  volfftf_1d               (GwyContainer *data,
                                                       GwyRunType runtype);
static GwyDialogOutcome      run_gui                  (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void                  param_changed            (ModuleGUI *gui,
                                                       gint id);
static void                  dialog_response          (GwyDialog *dialog,
                                                       gint response,
                                                       ModuleGUI *gui);
static void                  preview                  (gpointer user_data);
static void                  update_image             (ModuleGUI *gui,
                                                       gint z);
static void                  graph_selected           (ModuleGUI *gui);
static void                  ensure_modulus           (ModuleArgs *args);
static void                  plot_modulus             (ModuleGUI *gui);
static GwyDataLine*          calculate_weights        (ModuleArgs *args,
                                                       GwySelection *selection);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performs 1D FFT filtering for all the volume data levels."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_1dfft)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_1dfft",
                             (GwyVolumeFunc)&volfftf_1d,
                             N_("/_Correct Data/1D _FFT Filtering..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("1D FFT filtering of all levels"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Marked"),    OUTPUT_MARKED,    },
        { N_("Unmarked"),  OUTPUT_UNMARKED,  },
    };
    static const GwyEnum suppresses[] = {
        { N_("Null"),      SUPPRESS_NULL,         },
        { N_("Suppress"),  SUPPRESS_NEIGBOURHOOD, },
    };
    static const GwyEnum modulus[] = {
        { N_("spectra|Individual"), MODULUS_INDIVIDUAL, },
        { N_("spectra|Average"),    MODULUS_AVERAGE,    },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_SUPPRESS, "suppress", _("_Suppress type"),
                              suppresses, G_N_ELEMENTS(suppresses), SUPPRESS_NEIGBOURHOOD);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "output", _("_Filter type"),
                              outputs, G_N_ELEMENTS(outputs), OUTPUT_UNMARKED);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODULUS, "modulus", _("_Shown spectrum"),
                              modulus, G_N_ELEMENTS(modulus), MODULUS_INDIVIDUAL);
    gwy_param_def_add_enum(paramdef, PARAM_DIRECTION, "direction", NULL, GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_HORIZONTAL);
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interpolation", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
//    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_int(paramdef, PARAM_Z, "z", "Preview level", 0, G_MAXINT, 0);
    return paramdef;
}

static void
volfftf_1d(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.modulus = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        goto end;

    newid = gwy_app_data_browser_add_brick(args.result, NULL, data, TRUE);

    gwy_app_set_brick_title(data, newid, _("1D FFT Filtered"));
    gwy_app_sync_volume_items(data, data, oldid, newid, FALSE,
                              GWY_DATA_ITEM_GRADIENT,
                              0);

    gwy_app_volume_log_add_volume(data, -1, newid);

end:
    GWY_OBJECT_UNREF(args.modulus);
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview, *align;
    GwyParamTable *table;
    GwyDialog *dialog;
    GwyGraph *graph;
    GwySelection *selection;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    GwyBrick *brick = args->brick;
    GwyDataField *field = gwy_data_field_new(gwy_brick_get_xres(brick),
                                             gwy_brick_get_yres(brick),
                                             gwy_brick_get_xreal(brick),
                                             gwy_brick_get_yreal(brick),
                                             TRUE);
    GwyDataField *result = gwy_data_field_new_alike(field, TRUE);

    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gui.computed = FALSE;

    args->result = gwy_brick_duplicate(brick);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(1), result);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(1), gradient);


    gui.dialog = gwy_dialog_new(_("1D FFT Filter"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_CLEAR, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);


    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 4);

    dataview = gwy_create_preview(gui.data, 1, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 4);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    gui.gmodel = gwy_graph_model_new();
    graph = GWY_GRAPH(gwy_graph_new(gui.gmodel));
    gwy_graph_set_status(graph, GWY_GRAPH_STATUS_XSEL);
    gtk_widget_set_size_request(GTK_WIDGET(graph), -1, PREVIEW_GRAPH_SIZE);
    gwy_graph_enable_user_input(graph, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(graph), TRUE, TRUE, 4);

    selection = gui.selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(graph)),
                                                             GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(selection, 20);
    g_signal_connect_swapped(selection, "changed", G_CALLBACK(graph_selected), &gui);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_Z);
    gwy_param_table_slider_restrict_range(table, PARAM_Z, 0, gwy_brick_get_zres(brick)-1);

    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_combo(table, PARAM_SUPPRESS);
    gwy_param_table_append_combo(table, PARAM_OUTPUT);
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);
    gwy_param_table_append_combo(table, PARAM_MODULUS);
//    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_container_add(GTK_CONTAINER(align), gwy_param_table_widget(table));

    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);


    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);

    return outcome;
}


static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_options;
    GwyParams *params = gui->args->params;
    GwyModulusType mtype = gwy_params_get_enum(gui->args->params, PARAM_MODULUS);


    if (id < 0 || id == PARAM_SUPPRESS) {
        GwyFFTFilt1DSuppressType suppres = gwy_params_get_enum(params, PARAM_SUPPRESS);
        GwyFFTFilt1DOutputType output = gwy_params_get_enum(params, PARAM_OUTPUT);
        if (suppres == SUPPRESS_NEIGBOURHOOD && output == OUTPUT_MARKED)
            gwy_param_table_set_enum(table, PARAM_OUTPUT, (output = OUTPUT_UNMARKED));
        gwy_param_table_set_sensitive(table, PARAM_OUTPUT, suppres == SUPPRESS_NULL);
        gui->computed = FALSE;
    }
    if (id < 0 || id == PARAM_DIRECTION || id == PARAM_MODULUS
        || (id == PARAM_Z && mtype == MODULUS_INDIVIDUAL)) {
        GWY_OBJECT_UNREF(gui->args->modulus);
        if (id < 0 || id == PARAM_DIRECTION)
            gwy_selection_clear(gui->selection);
        ensure_modulus(gui->args);
        plot_modulus(gui);
        gui->computed = FALSE;
    }
    if (id < 0 || id == PARAM_OUTPUT || id == PARAM_INTERPOLATION) {
        gui->computed = FALSE;
    }

    //if (id != PARAM_UPDATE)
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_CLEAR)
        gwy_selection_clear(gui->selection);

    if (response == GWY_RESPONSE_RESET)
        gwy_brick_copy(gui->args->brick, gui->args->result, FALSE);

    preview(gui);
}

static void
update_image(ModuleGUI *gui, gint z)
{
    GwyDataField *dfield;
    dfield = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
    gwy_brick_extract_xy_plane(gui->args->brick, dfield, CLAMP(z, 0, gui->args->brick->zres-1));
    gwy_data_field_data_changed(dfield);

    dfield = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(1));
    gwy_brick_extract_xy_plane(gui->args->result, dfield, CLAMP(z, 0, gui->args->result->zres-1));

    gwy_data_field_data_changed(dfield);
}


static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    gint z = gwy_params_get_int(gui->args->params, PARAM_Z);
    GwyOrientation direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);
    GwyDataLine *weights;
    gint k, xres, yres, zres;
    GwyBrick *brick = args->brick;
    GwyBrick *result = args->result;
    GwyDataField *dfield, *myfield;

    if (!gui->computed) {
       xres = gwy_brick_get_xres(brick);
       yres = gwy_brick_get_yres(brick);
       zres = gwy_brick_get_zres(brick);

       dfield = gwy_data_field_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);
       myfield = gwy_data_field_new_alike(dfield, FALSE);
       ensure_modulus(args);

       //printf("comp\n");
       for (k = 0; k < zres; k++) {
            gwy_brick_extract_xy_plane(brick, dfield, k);
            weights = calculate_weights(args, gui->selection);
            gwy_data_field_fft_filter_1d(dfield, myfield, weights, direction, interpolation);
            gwy_brick_set_xy_plane(result, myfield, k);
       }
       gui->computed = TRUE;
       g_object_unref(dfield);
       g_object_unref(myfield);
    }

    update_image(gui, z);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
graph_selected(ModuleGUI *gui)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                      gwy_selection_get_data(gui->selection, NULL));
    gui->computed = FALSE;
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
ensure_modulus(ModuleArgs *args)
{
    GwyOrientation direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    GwyBrick *brick = args->brick;
    gint z = gwy_params_get_int(args->params, PARAM_Z);
    GwyModulusType mtype = gwy_params_get_enum(args->params, PARAM_MODULUS);
    GwyDataField *dfield;
    GwyDataLine *modulus = NULL, *levmodulus;
    gint i, k, res, zres;
    gdouble m;
    gdouble *d;

    if (args->modulus)
        return;

    dfield = gwy_data_field_new(gwy_brick_get_xres(brick), gwy_brick_get_yres(brick),
                                gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);

    if (mtype == MODULUS_INDIVIDUAL) {
        modulus = args->modulus = gwy_data_line_new(1, 1.0, FALSE);
        gwy_brick_extract_xy_plane(brick, dfield, z);
        gwy_data_field_psdf(dfield, modulus, direction, GWY_INTERPOLATION_LINEAR, GWY_WINDOWING_RECT, -1);
    } else {
        levmodulus = gwy_data_line_new(1, 1.0, FALSE);
        zres = gwy_brick_get_zres(brick);
        for (k = 0; k < zres; k++) {
            gwy_brick_extract_xy_plane(brick, dfield, k);
            gwy_data_field_psdf(dfield, levmodulus, direction, GWY_INTERPOLATION_LINEAR, GWY_WINDOWING_RECT, -1);
            if (k == 0)
                modulus = args->modulus = gwy_data_line_new_alike(levmodulus, TRUE);
            gwy_data_line_sum_lines(modulus, modulus, levmodulus);
        }
        gwy_data_line_multiply(modulus, 1.0/(gdouble)zres);
        g_object_unref(levmodulus);
    }

    m = gwy_data_line_get_max(modulus);
    if (!m)
        m = 1.0;
    d = gwy_data_line_get_data(modulus);
    res = gwy_data_line_get_res(modulus);
    for (i = 0; i < res; i++)
        d[i] = (d[i] > 0.0 ? sqrt(d[i]/m) : 0.0);

    g_object_unref(dfield);
}

static void
plot_modulus(ModuleGUI *gui)
{
    GwyGraphCurveModel *cmodel;
    GwyDataLine *modulus = gui->args->modulus;

    gwy_graph_model_remove_all_curves(gui->gmodel);

    cmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data_from_dataline(cmodel, modulus, 0, 0);
    g_object_set(cmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "description", _("FFT Modulus"),
                 NULL);
    g_object_set(gui->gmodel,
                 "si-unit-x", gwy_data_line_get_si_unit_x(modulus),
                 "axis-label-bottom", "k",
                 "axis-label-left", "",
                 NULL);

    gwy_graph_model_add_curve(gui->gmodel, cmodel);
    g_object_unref(cmodel);
}

static GwyDataLine*
calculate_weights(ModuleArgs *args, GwySelection *selection)
{
    GwyFFTFilt1DSuppressType suppres = gwy_params_get_enum(args->params, PARAM_SUPPRESS);
    GwyFFTFilt1DOutputType output = gwy_params_get_enum(args->params, PARAM_OUTPUT);
    GwyDataLine *weights, *modulus = args->modulus;
    gint res, k, nsel;
    gdouble sel[2];
    gint fill_from, fill_to;

    res = gwy_data_line_get_res(modulus);
    weights = gwy_data_line_new_alike(modulus, TRUE);

    nsel = gwy_selection_get_data(selection, NULL);
    for (k = 0; k < nsel; k++) {
        gwy_selection_get_object(selection, k, sel);
        GWY_ORDER(gdouble, sel[0], sel[1]);
        fill_from = MAX(0, gwy_data_line_rtoi(weights, sel[0]));
        fill_from = MIN(res, fill_from);
        fill_to = MIN(res, gwy_data_line_rtoi(weights, sel[1]));
        gwy_data_line_part_fill(weights, fill_from, fill_to, 1.0);
    }

    /* For Suppress, interpolate PSDF linearly between endpoints.  Since we pass weights to the filter, not PSDF
     * itself, we have to divide to get the weight. */
    if (suppres == SUPPRESS_NEIGBOURHOOD) {
        GwyDataLine *buf = gwy_data_line_duplicate(modulus);
        gdouble *b, *m, *w;

        gwy_data_line_correct_laplace(buf, weights);
        b = gwy_data_line_get_data(buf);
        m = gwy_data_line_get_data(modulus);
        w = gwy_data_line_get_data(weights);
        for (k = 0; k < res; k++)
            w[k] = (m[k] > 0.0 ? fmin(b[k]/m[k], 1.0) : 0.0);
        g_object_unref(buf);
    }
    else if (output == OUTPUT_UNMARKED) {
        gdouble *w = gwy_data_line_get_data(weights);
        for (k = 0; k < res; k++)
            w[k] = 1.0 - w[k];
    }

    return weights;
}


/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
