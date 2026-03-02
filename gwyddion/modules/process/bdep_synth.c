/*
 *  $Id: bdep_synth.c 23773 2021-05-24 20:06:40Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define WORK_UPDATE_CHECK 1000000

typedef enum {
    GRAPH_MEAN = 0,
    GRAPH_RMS  = 1,
    GRAPH_NGRAPHS,
} GraphOutputs;

enum {
    PARAM_COVERAGE,
    PARAM_HEIGHT,
    PARAM_HEIGHT_NOISE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_GRAPH_FLAGS,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef gdouble (*DataFieldStatFunc)(GwyDataField *field);

typedef struct {
    DataFieldStatFunc func;
    gint power_xy;
    gint power_z;
} EvolutionStatInfo;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GArray *evolution[GRAPH_NGRAPHS+1];
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyParamTable *table_evolution;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             bdep_synth           (GwyContainer *data,
                                              GwyRunType runtype);
static void             plot_evolution_graphs(ModuleArgs *args,
                                              const GwyAppDataId *dataid);
static gboolean         execute              (ModuleArgs *args,
                                              GtkWindow *wait_window);
static GwyDialogOutcome run_gui              (ModuleArgs *args,
                                              GwyContainer *data,
                                              gint id);
static GtkWidget*       dimensions_tab_new   (ModuleGUI *gui);
static GtkWidget*       generator_tab_new    (ModuleGUI *gui);
static GtkWidget*       evolution_tab_new    (ModuleGUI *gui);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             dialog_response      (ModuleGUI *gui,
                                              gint response);
static void             preview              (gpointer user_data);

static const EvolutionStatInfo evolution_info[GRAPH_NGRAPHS] = {
    { gwy_data_field_get_avg, 0, 1 },
    { gwy_data_field_get_rms, 0, 1 },
};

static const GwyEnum graph_outputs[GRAPH_NGRAPHS] = {
    { N_("Mean value"), (1 << GRAPH_MEAN) },
    { N_("RMS"),        (1 << GRAPH_RMS)  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates surfaces by ballistic deposition."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, bdep_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("bdep_synth",
                              (GwyProcessFunc)&bdep_synth,
                              N_("/S_ynthetic/_Deposition/_Ballistic..."),
                              GWY_STOCK_SYNTHETIC_BALLISTIC_DEPOSITION,
                              RUN_MODES,
                              0,
                              N_("Generate surface by ballistic deposition"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 0.01, 1e4, 10.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_GRAPH_FLAGS, "graph_flags", _("Plot evolution graphs"),
                               graph_outputs, G_N_ELEMENTS(graph_outputs), 0);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
bdep_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyDataField *field;
    GwyAppDataId dataid;
    guint i;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.field = field;
    args.zscale = field ? gwy_data_field_get_rms(field) : -1.0;

    args.params = gwy_params_new_from_settings(define_module_params());
    gwy_synth_sanitise_params(args.params, PARAM_DIMS0, field);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    args.result = gwy_synth_make_result_data_field((args.field = field), args.params, FALSE);
    for (i = 0; i <= GRAPH_NGRAPHS; i++)
        args.evolution[i] = g_array_new(FALSE, FALSE, sizeof(gdouble));
    if (gwy_params_get_boolean(args.params, PARAM_ANIMATED))
        gwy_app_wait_preview_data_field(args.result, data, id);
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;
    dataid = gwy_synth_add_result_to_file(args.result, data, id, args.params);
    plot_evolution_graphs(&args, &dataid);

end:
    GWY_OBJECT_UNREF(args.result);
    for (i = 0; i <= GRAPH_NGRAPHS; i++) {
        if (args.evolution[i])
            g_array_free(args.evolution[i], TRUE);
    }
    g_object_unref(args.params);
}

static void
plot_evolution_graphs(ModuleArgs *args, const GwyAppDataId *dataid)
{
    GArray **evolution = args->evolution;
    const gdouble *xdata = &g_array_index(evolution[GRAPH_NGRAPHS], gdouble, 0);
    guint i, n = evolution[GRAPH_NGRAPHS]->len;
    guint graph_flags = gwy_params_get_flags(args->params, PARAM_GRAPH_FLAGS);
    GwyGraphCurveModel *gcmodel;
    GwyGraphModel *gmodel;
    GwyContainer *data;
    gchar *s, *title;
    const gchar *name;

    if (!graph_flags)
        return;

    data = gwy_app_data_browser_get(dataid->datano);
    for (i = 0; i < GRAPH_NGRAPHS; i++) {
        if (!(graph_flags & (1 << i)))
            continue;

        name = _(graph_outputs[i].name);

        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata, &g_array_index(evolution[i], gdouble, 0), n);
        g_object_set(gcmodel, "description", name, NULL);

        gmodel = gwy_graph_model_new();
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        s = gwy_app_get_data_field_title(data, dataid->id);
        title = g_strdup_printf("%s (%s)", name, s);
        g_free(s);
        g_object_set(gmodel,
                     "title", title,
                     "x-logarithmic", TRUE,
                     "y-logarithmic", TRUE,
                     "axis-label-bottom", _("Mean deposited thickness"),
                     "axis-label-left", name,
                     NULL);
        g_free(title);

        gwy_graph_model_set_units_from_data_field(gmodel, args->result,
                                                  0, 1, evolution_info[i].power_xy, evolution_info[i].power_z);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
    }
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GtkWidget *hbox, *dataview;
    GtkNotebook *notebook;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.template_ = args->field;

    if (gui.template_)
        args->field = gwy_synth_make_preview_data_field(gui.template_, PREVIEW_SIZE);
    else
        args->field = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, TRUE);
    args->result = gwy_synth_make_result_data_field(args->field, args->params, TRUE);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    if (gui.template_)
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Ballistic Deposition"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gtk_notebook_append_page(notebook, evolution_tab_new(&gui), gtk_label_new(_("Evolution")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_evolution, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(args->field);
    GWY_OBJECT_UNREF(args->result);

    return outcome;
}

static GtkWidget*
dimensions_tab_new(ModuleGUI *gui)
{
    gui->table_dimensions = gwy_param_table_new(gui->args->params);
    gwy_synth_append_dimensions_to_param_table(gui->table_dimensions, 0);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_dimensions);

    return gwy_param_table_widget(gui->table_dimensions);
}

static GtkWidget*
generator_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_generator = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_header(table, -1, _("Ballistic Deposition"));
    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_slider_set_mapping(table, PARAM_COVERAGE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_HEIGHT_NOISE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_ANIMATED);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
evolution_tab_new(ModuleGUI *gui)
{
    gui->table_evolution = gwy_param_table_new(gui->args->params);
    gwy_param_table_append_checkboxes(gui->table_evolution, PARAM_GRAPH_FLAGS);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_evolution);

    return gwy_param_table_widget(gui->table_evolution);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_HEIGHT };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;

    if (response == GWY_RESPONSE_SYNTH_INIT_Z) {
        gdouble zscale = args->zscale;
        gint power10z;

        if (zscale > 0.0) {
            gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
            gwy_param_table_set_double(gui->table_generator, PARAM_HEIGHT, zscale/pow10(power10z));
        }
    }
    else if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS) {
        gwy_synth_use_dimensions_template(gui->table_dimensions);
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    if (execute(gui->args, GTK_WINDOW(gui->dialog)))
        gwy_data_field_data_changed(gui->args->result);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    guint graph_flags = gwy_params_get_flags(params, PARAM_GRAPH_FLAGS);
    GwyDataField *field, *result = args->result;
    GArray **evolution = args->evolution[0] ? args->evolution : NULL;
    GwyRandGenSet *rngset;
    GRand *rng_k, *rng_height;
    GTimer *timer;
    gint xres, yres, xext, yext, n, power10z;
    gdouble flux, nextgraphx, preview_time = (animated ? 1.25 : 0.0);
    GwySynthUpdateType update;
    guint64 workdone, niter, iter;
    gdouble *d;
    gboolean finished = FALSE;
    guint i;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    rngset = gwy_rand_gen_set_new(2);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));
    rng_k = gwy_rand_gen_set_rng(rngset, 0);
    rng_height = gwy_rand_gen_set_rng(rngset, 1);

    if (args->field && do_initialise)
        gwy_data_field_copy(args->field, result, FALSE);
    else
        gwy_data_field_clear(result);

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    xext = gwy_data_field_get_xres(result)/12;
    yext = gwy_data_field_get_yres(result)/12;
    field = gwy_data_field_extend(result, xext, xext, yext, yext, GWY_EXTERIOR_MIRROR_EXTEND, 0.0, FALSE);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    n = xres*yres;
    flux = 1.0/n;

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Depositing particles...")))
        goto end;

    d = field->data;
    niter = (guint64)(coverage/flux + 0.5);
    nextgraphx = 0.0;
    workdone = 0.0;
    iter = 0;

    while (iter < niter) {
        guint k = g_rand_int_range(rng_k, 0, n);
        gdouble v = (height_noise ? height_noise*g_rand_double(rng_height) + 1.0 - height_noise : 1.0);
        guint ii = (k/xres)*xres, j = k % xres;
        gdouble h = d[k] + v*height;
        guint iim = G_LIKELY(ii) ? ii - xres : 0;
        guint iip = G_LIKELY(ii != n - xres) ? ii + xres : n - xres;
        guint jl = G_LIKELY(j) ? j-1 : 0;
        guint jr = G_LIKELY(j != xres-1) ? j+1 : xres-1;
        gdouble h1 = MAX(d[iim + j], d[ii + jl]);
        gdouble h2 = MAX(d[ii + jr], d[iip + j]);

        d[k] = MAX(h, MAX(h1, h2));
        iter++;
        workdone++;

        if (workdone >= WORK_UPDATE_CHECK) {
            update = gwy_synth_update_progress(timer, preview_time, iter, niter);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (animated && update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                gwy_data_field_area_copy(field, result, xext, yext, xres - 2*xext, yres - 2*yext, 0, 0);
                gwy_data_field_data_changed(result);
            }
            workdone -= WORK_UPDATE_CHECK;
        }

        if (evolution && iter >= nextgraphx) {
            gwy_data_field_invalidate(field);
            for (i = 0; i < GRAPH_NGRAPHS; i++) {
                if (graph_flags & (1 << i)) {
                    v = evolution_info[i].func(field);
                    g_array_append_val(evolution[i], v);
                }
            }
            v = iter*flux*height;
            g_array_append_val(evolution[GRAPH_NGRAPHS], v);

            nextgraphx += 0.0001/flux + MIN(0.2*nextgraphx, 0.08/flux);
        }
    }

    gwy_data_field_area_copy(field, result, xext, yext, xres - 2*xext, yres - 2*yext, 0, 0);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_object_unref(field);
    g_timer_destroy(timer);
    gwy_rand_gen_set_free(rngset);

    return finished;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
