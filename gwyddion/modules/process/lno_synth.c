/*
 *  $Id: lno_synth.c 26070 2023-12-20 14:25:19Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define DECLARE_NOISE(name) \
    static gdouble noise_##name##_both(GwyRandGenSet *rng, gdouble sigma); \
    static gdouble noise_##name##_up(GwyRandGenSet *rng, gdouble sigma); \
    static gdouble noise_##name##_down(GwyRandGenSet *rng, gdouble sigma);

#define NOISE_FUNCS(name) \
    { noise_##name##_both, noise_##name##_up, noise_##name##_down }

#define DECLARE_LNOISE(name) \
    static void define_params_##name(GwyParamDef *pardef); \
    static void append_gui_##name(ModuleGUI *gui); \
    static void dimensions_changed_##name(ModuleGUI *gui); \
    static void make_noise_##name(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise);

#define LLNO_FUNCS(name) \
   define_params_##name, append_gui_##name, dimensions_changed_##name, make_noise_##name

enum {
    PARAM_DISTRIBUTION,
    PARAM_DIRECTION,
    PARAM_TYPE,
    PARAM_SIGMA,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_STEPS_DENSITY,
    PARAM_STEPS_LINEPROB,
    PARAM_STEPS_SCANDIR,
    PARAM_STEPS_CUMULATIVE,

    PARAM_SCARS_COVERAGE,
    PARAM_SCARS_LENGTH,
    PARAM_SCARS_LENGTH_NOISE,

    PARAM_RIDGES_DENSITY,
    PARAM_RIDGES_LINEPROB,
    PARAM_RIDGES_SCANDIR,
    PARAM_RIDGES_WIDTH,

    PARAM_TILT_OFFSET_VAR,

    PARAM_HUM_WAVELENGTH,
    PARAM_HUM_SPREAD,
    PARAM_HUM_NCOMP,

    PARAM_DIMS0
};

typedef enum {
    RNG_POINT_NOISE = 0,
    RNG_LEN = 0,
    RNG_POS = 1,
    RNG_NRGNS
} LNoSynthRng;

typedef enum {
    NOISE_DISTRIBUTION_GAUSSIAN    = 0,
    NOISE_DISTRIBUTION_EXPONENTIAL = 1,
    NOISE_DISTRIBUTION_UNIFORM     = 2,
    NOISE_DISTRIBUTION_TRIANGULAR  = 3,
} NoiseDistributionType;

typedef enum {
    NOISE_DIRECTION_BOTH = 0,
    NOISE_DIRECTION_UP   = 1,
    NOISE_DIRECTION_DOWN = 2,
    NOISE_DIRECTION_NTYPES
} NoiseDirectionType;

typedef enum {
    SCANDIR_RTL = -1,
    SCANDIR_LTR  = 1,
} ScanDirection;

typedef enum {
    LNO_SYNTH_STEPS  = 0,
    LNO_SYNTH_SCARS  = 1,
    LNO_SYNTH_RIDGES = 2,
    LNO_SYNTH_TILT   = 3,
    LNO_SYNTH_HUM    = 4,
    LNO_SYNTH_NTYPES,
} LNoSynthNoiseType;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_type;
    GwyParamTable *table_options;
    GwyParamTable *table_noise[LNO_SYNTH_NTYPES];
    GtkWidget *generator_vbox;
    GtkWidget *noise_table_widget;
    GwyContainer *data;
    GwyDataField *template_;
    LNoSynthNoiseType noise_type;
} ModuleGUI;

typedef gdouble (*PointNoiseFunc)(GwyRandGenSet *rng, gdouble sigma);

typedef struct {
    const gchar *name;
    PointNoiseFunc point_noise[NOISE_DIRECTION_NTYPES];
} NoiseSynthGenerator;

typedef void (*DefineParamsFunc)(GwyParamDef *paramdef);
typedef void (*AppendGUIFunc)(ModuleGUI *gui);
typedef void (*DimensionsChangedFunc)(ModuleGUI *gui);
typedef void (*MakeNoiseFunc)(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise);

typedef struct {
    const gchar *name;
    DefineParamsFunc define_params;
    AppendGUIFunc append_gui;
    DimensionsChangedFunc dimensions_changed;
    MakeNoiseFunc make_noise;
} LNoSynthNoise;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             lno_synth           (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GtkWidget*       dimensions_tab_new  (ModuleGUI *gui);
static GtkWidget*       generator_tab_new   (ModuleGUI *gui);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             switch_noise_type   (ModuleGUI *gui);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);

DECLARE_NOISE(gaussian);
DECLARE_NOISE(exp);
DECLARE_NOISE(uniform);
DECLARE_NOISE(triangle);

DECLARE_LNOISE(steps);
DECLARE_LNOISE(scars);
DECLARE_LNOISE(ridges);
DECLARE_LNOISE(tilt);
DECLARE_LNOISE(hum);

/* NB: The order of these and everything else (like table_noise[]) must match the enums.  See obj_synth.c for how
 * to reorder it in the GUI. */
static const NoiseSynthGenerator generators[] = {
    { N_("distribution|Gaussian"),        NOISE_FUNCS(gaussian),   },
    { N_("distribution|Exponential"),     NOISE_FUNCS(exp),        },
    { N_("distribution|Uniform"),         NOISE_FUNCS(uniform),    },
    { N_("distribution|Triangular"),      NOISE_FUNCS(triangle),   },
};

static const LNoSynthNoise noises[LNO_SYNTH_NTYPES] = {
    { N_("Steps"),  LLNO_FUNCS(steps),  },
    { N_("Scars"),  LLNO_FUNCS(scars),  },
    { N_("Ridges"), LLNO_FUNCS(ridges), },
    { N_("Tilt"),   LLNO_FUNCS(tilt),   },
    { N_("Hum"),    LLNO_FUNCS(hum),    },
};

static const GwyEnum scandirs[] = {
    { N_("_Left to right"), SCANDIR_LTR, },
    { N_("_Right to left"), SCANDIR_RTL, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates various kinds of line noise."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2010",
};

GWY_MODULE_QUERY2(module_info, lno_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("lno_synth",
                              (GwyProcessFunc)&lno_synth,
                              N_("/S_ynthetic/_Noise/_Line Noise..."),
                              GWY_STOCK_SYNTHETIC_LINE_NOISE,
                              RUN_MODES,
                              0,
                              N_("Generate line noise"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("S_ymmetrical"),        NOISE_DIRECTION_BOTH, },
        { N_("One-sided _positive"), NOISE_DIRECTION_UP,   },
        { N_("One-sided _negative"), NOISE_DIRECTION_DOWN, },
    };
    static GwyParamDef *paramdef = NULL;
    static GwyEnum *distributions = NULL, *types = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    distributions = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(generators), generators, sizeof(NoiseSynthGenerator),
                                              G_STRUCT_OFFSET(NoiseSynthGenerator, name), -1);
    types = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(noises), noises, sizeof(LNoSynthNoise),
                                      G_STRUCT_OFFSET(LNoSynthNoise, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Noise type"),
                              types, G_N_ELEMENTS(noises), LNO_SYNTH_STEPS);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISTRIBUTION, "distribution", _("_Distribution"),
                              distributions, G_N_ELEMENTS(generators), NOISE_DISTRIBUTION_GAUSSIAN);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", _("_Noise sign"),
                              directions, G_N_ELEMENTS(directions), NOISE_DIRECTION_BOTH);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_RMS"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    for (i = 0; i < G_N_ELEMENTS(noises); i++)
        noises[i].define_params(paramdef);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
lno_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyDataField *field;
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
    execute(&args);
    gwy_synth_add_result_to_file(args.result, data, id, args.params);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GtkWidget *hbox, *dataview;
    GtkNotebook *notebook;
    ModuleGUI gui;
    guint i;

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

    gui.dialog = gwy_dialog_new(_("Line Noise"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    for (i = 0; i < G_N_ELEMENTS(noises); i++) {
        const LNoSynthNoise *noise = noises + i;
        gui.table_noise[i] = gwy_param_table_new(args->params);
        g_object_ref_sink(gui.table_noise[i]);
        noise->append_gui(&gui);
    }

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);
    switch_noise_type(&gui);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_type, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    for (i = 0; i < G_N_ELEMENTS(noises); i++)
        g_signal_connect_swapped(gui.table_noise[i], "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    for (i = 0; i < G_N_ELEMENTS(noises); i++)
        g_object_unref(gui.table_noise[i]);

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

    gui->generator_vbox = gwy_vbox_new(4);

    table = gui->table_type = gwy_param_table_new(gui->args->params);
    gwy_param_table_append_header(table, -1, _("Line Noise"));
    gwy_param_table_append_combo(table, PARAM_TYPE);
    gwy_param_table_set_no_reset(table, PARAM_TYPE, TRUE);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui->table_options = gwy_param_table_new(gui->args->params);
    gwy_param_table_append_header(table, -1, _("Distribution"));
    gwy_param_table_append_combo(table, PARAM_DISTRIBUTION);
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_set_mapping(table, PARAM_SIGMA, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    return gui->generator_vbox;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    const LNoSynthNoiseType type = gwy_params_get_enum(params, PARAM_TYPE);

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_TYPE) {
        if (type != gui->noise_type)
            switch_noise_type(gui);
    }

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_SIGMA };

        gwy_synth_update_value_unitstrs(gui->table_options, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(gui->table_options, BUTTON_LIKE_CURRENT_IMAGE);
    }
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        noises[type].dimensions_changed(gui);
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
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
            gwy_param_table_set_double(gui->table_options, PARAM_SIGMA, zscale/pow10(power10z));
        }
    }
    else if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS) {
        gwy_synth_use_dimensions_template(gui->table_dimensions);
    }
}

static void
switch_noise_type(ModuleGUI *gui)
{
    LNoSynthNoiseType type = gwy_params_get_enum(gui->args->params, PARAM_TYPE);

    if (gui->noise_table_widget) {
        gwy_dialog_remove_param_table(GWY_DIALOG(gui->dialog), gui->table_noise[gui->noise_type]);
        gtk_widget_destroy(gui->noise_table_widget);
        gui->noise_table_widget = NULL;
    }

    gui->noise_type = type;
    gui->noise_table_widget = gwy_param_table_widget(gui->table_noise[type]);
    gtk_widget_show_all(gui->noise_table_widget);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gui->noise_table_widget, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(gui->generator_vbox), gui->noise_table_widget, 1);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_noise[type]);
    noises[type].dimensions_changed(gui);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    NoiseDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    NoiseDirectionType direction = gwy_params_get_enum(params, PARAM_DIRECTION);
    const LNoSynthNoise *noise = noises + gwy_params_get_enum(params, PARAM_TYPE);
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    GwyDataField *field = args->field, *result = args->result;
    PointNoiseFunc point_noise = generators[distribution].point_noise[direction];
    GwyRandGenSet *rngset;
    gint power10z;

    rngset = gwy_rand_gen_set_new(RNG_NRGNS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    if (field && do_initialise)
        gwy_data_field_copy(field, result, FALSE);
    else
        gwy_data_field_clear(result);

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    sigma *= pow10(power10z);

    noise->make_noise(args, sigma, rngset, point_noise);

    gwy_rand_gen_set_free(rngset);
}

/*****************************************************************************
 *
 * Steps
 *
 *****************************************************************************/

static void
define_params_steps(GwyParamDef *paramdef)
{
    gwy_param_def_add_double(paramdef, PARAM_STEPS_DENSITY, "steps/density", _("Densi_ty"), 5e-4, 200.0, 1.0);
    gwy_param_def_add_percentage(paramdef, PARAM_STEPS_LINEPROB, "steps/lineprob", _("_Within line"), 0.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_STEPS_SCANDIR, "steps/scandir", _("Scanning direction"),
                              scandirs, G_N_ELEMENTS(scandirs), SCANDIR_LTR);
    gwy_param_def_add_boolean(paramdef, PARAM_STEPS_CUMULATIVE, "steps/cumulative", _("C_umulative"), FALSE);
}

static void
append_gui_steps(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_noise[LNO_SYNTH_STEPS];

    gwy_param_table_append_slider(table, PARAM_STEPS_DENSITY);
    gwy_param_table_slider_set_mapping(table, PARAM_STEPS_DENSITY, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_STEPS_LINEPROB);
    gwy_param_table_append_combo(table, PARAM_STEPS_SCANDIR);
    gwy_param_table_append_checkbox(table, PARAM_STEPS_CUMULATIVE);
}

static void
dimensions_changed_steps(G_GNUC_UNUSED ModuleGUI *gui)
{
}

static void
make_noise_steps(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise)
{
    enum { BATCH_SIZE = 64 };

    GwyParams *params = args->params;
    gdouble density = gwy_params_get_double(params, PARAM_STEPS_DENSITY);
    gdouble lineprob = gwy_params_get_double(params, PARAM_STEPS_LINEPROB);
    ScanDirection scandir = gwy_params_get_enum(params, PARAM_STEPS_SCANDIR);
    gboolean cumulative = gwy_params_get_boolean(params, PARAM_STEPS_CUMULATIVE);
    GwyDataField *field = args->result;
    gdouble *steps, *data;
    guint xres, yres, nbatches, nsteps, i, j, ib, is;
    gdouble h;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    nsteps = GWY_ROUND(yres*density);
    nsteps = MAX(nsteps, 1);
    steps = g_new(gdouble, nsteps + 1);

    /* Generate the steps in batches because (a) it speeds up sorting (b) it makes them more uniform. */
    nbatches = (nsteps + BATCH_SIZE-1)/BATCH_SIZE;

    for (ib = 0; ib < nbatches; ib++) {
        guint base = ib*nsteps/nbatches, nextbase = (ib + 1)*nsteps/nbatches;
        gdouble min = base/(gdouble)nsteps, max = nextbase/(gdouble)nsteps;

        for (i = base; i < nextbase; i++)
            steps[i] = gwy_rand_gen_set_range(rngset, RNG_POS, min, max);

        gwy_math_sort(nextbase - base, steps + base);
    }
    /* Sentinel */
    steps[nsteps] = 1.01;

    data = gwy_data_field_get_data(field);
    is = 0;
    h = 0.0;
    for (i = 0; i < yres; i++) {
        gdouble *row = data + i*xres;
        for (j = 0; j < xres; j++) {
            gdouble x = (lineprob*(j + 0.5)/xres + i)/yres;

            while (x > steps[is]) {
                if (cumulative)
                    h += point_noise(rngset, sigma);
                else
                    h = point_noise(rngset, sigma);
                is++;
            }

            if (scandir == SCANDIR_LTR)
                row[j] += h;
            else
                row[xres-1 - j] += h;
        }
    }

    g_free(steps);
}

/*****************************************************************************
 *
 * Scars
 *
 *****************************************************************************/

static void
define_params_scars(GwyParamDef *paramdef)
{
    gwy_param_def_add_double(paramdef, PARAM_SCARS_COVERAGE, "scars/coverage", _("Co_verage"), 1e-4, 20.0, 0.01);
    gwy_param_def_add_double(paramdef, PARAM_SCARS_LENGTH, "scars/length", _("_Length"), 1.0, 1e4, 10.0);
    gwy_param_def_add_double(paramdef, PARAM_SCARS_LENGTH_NOISE, "scars/length_var", _("Spread"), 0.0, 1.0, 0.0);
}

static void
append_gui_scars(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_noise[LNO_SYNTH_SCARS];

    gwy_param_table_append_slider(table, PARAM_SCARS_COVERAGE);
    gwy_param_table_append_slider(table, PARAM_SCARS_LENGTH);
    gwy_param_table_slider_set_mapping(table, PARAM_SCARS_LENGTH, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_SCARS_LENGTH);
    gwy_param_table_append_slider(table, PARAM_SCARS_LENGTH_NOISE);
}

static void
dimensions_changed_scars(ModuleGUI *gui)
{
    static const gint xyids[] = { PARAM_SCARS_LENGTH };

    gwy_synth_update_lateral_alts(gui->table_noise[LNO_SYNTH_SCARS], xyids, G_N_ELEMENTS(xyids));
}

static void
make_noise_scars(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise)
{
    GwyParams *params = args->params;
    gdouble coverage = gwy_params_get_double(params, PARAM_SCARS_COVERAGE);
    gdouble length = gwy_params_get_double(params, PARAM_SCARS_LENGTH);
    gdouble length_noise = gwy_params_get_double(params, PARAM_SCARS_LENGTH_NOISE);
    GwyDataField *field = args->result;
    gdouble *data, *row;
    gint xres, yres, i, j, len, from, to, L;
    guint is, n, nscars, m, t;
    gdouble noise_corr, stickout_corr, h;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    n = xres*yres;

    noise_corr = exp(length_noise*length_noise);
    stickout_corr = (length + xres)/length;
    nscars = GWY_ROUND(coverage*n*stickout_corr/(length*noise_corr));
    nscars = MAX(nscars, 1);
    L = GWY_ROUND(length);

    i = yres*(xres + L);
    m = G_MAXUINT32/i*i;

    data = gwy_data_field_get_data(field);
    for (is = 0; is < nscars; is++) {
        do {
            t = gwy_rand_gen_set_int(rngset, RNG_POS);
        } while (t >= m);
        i = t % yres;
        j = t/yres % (xres + L) + L/2-L;
        h = point_noise(rngset, sigma);
        if (length_noise) {
            len = gwy_rand_gen_set_gaussian(rngset, RNG_LEN, length_noise);
            len = GWY_ROUND(length*exp(len));
        }
        else
            len = L;
        row = data + i*xres;
        from = MAX(j - len/2, 0);
        to = MIN(j+len - len/2, xres-1);
        for (j = from; j <= to; j++)
            row[j] += h;
    }
}

/*****************************************************************************
 *
 * Ridges
 *
 *****************************************************************************/

typedef struct {
    gdouble pos;
    gdouble dh;
} LNoSynthRidgeEvent;

static void
define_params_ridges(GwyParamDef *paramdef)
{
    gwy_param_def_add_double(paramdef, PARAM_RIDGES_DENSITY, "ridges/density", _("Densi_ty"), 5e-4, 200.0, 0.1);
    gwy_param_def_add_percentage(paramdef, PARAM_RIDGES_LINEPROB, "ridges/lineprob", _("_Within line"), 0.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_RIDGES_SCANDIR, "ridges/scandir", _("Scanning direction"),
                              scandirs, G_N_ELEMENTS(scandirs), SCANDIR_LTR);
    gwy_param_def_add_double(paramdef, PARAM_RIDGES_WIDTH, "ridges/width", _("Wi_dth"), 1e-4, 1.0, 0.01);
}

static void
append_gui_ridges(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_noise[LNO_SYNTH_RIDGES];

    gwy_param_table_append_slider(table, PARAM_RIDGES_DENSITY);
    gwy_param_table_slider_set_mapping(table, PARAM_RIDGES_DENSITY, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_RIDGES_LINEPROB);
    gwy_param_table_append_combo(table, PARAM_RIDGES_SCANDIR);
    gwy_param_table_append_slider(table, PARAM_RIDGES_WIDTH);
}

static void
dimensions_changed_ridges(G_GNUC_UNUSED ModuleGUI *gui)
{
}

static gint
compare_ridge_events(gconstpointer pa, gconstpointer pb)
{
    const LNoSynthRidgeEvent *a = (const LNoSynthRidgeEvent*)pa;
    const LNoSynthRidgeEvent *b = (const LNoSynthRidgeEvent*)pb;

    if (a->pos < b->pos)
        return -1;
    if (a->pos > b->pos)
        return 1;

    /* Ensure comparison stability. */
    if (a < b)
        return -1;
    if (a > b)
        return 1;

    return 0;
}

static void
make_noise_ridges(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise)
{
    enum { BATCH_SIZE = 64 };

    GwyParams *params = args->params;
    gdouble density = gwy_params_get_double(params, PARAM_RIDGES_DENSITY);
    gdouble width = gwy_params_get_double(params, PARAM_RIDGES_WIDTH);
    gdouble lineprob = gwy_params_get_double(params, PARAM_RIDGES_LINEPROB);
    ScanDirection scandir = gwy_params_get_enum(params, PARAM_RIDGES_SCANDIR);
    GwyDataField *field = args->result;
    LNoSynthRidgeEvent *ridges;
    gdouble *data;
    guint xres, yres, nridges, i, j, is;
    gdouble h;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    nridges = GWY_ROUND(yres*(1.0 + width)*density);
    nridges = MAX(nridges, 1);
    ridges = g_new(LNoSynthRidgeEvent, 2*nridges + 1);

    for (i = 0; i < nridges; i++) {
        gdouble centre = gwy_rand_gen_set_range(rngset, 0, -width, 1.0 + width);
        gdouble w = noise_exp_up(rngset, width);
        gdouble dh = point_noise(rngset, sigma);

        ridges[2*i + 0].pos = centre - w;
        ridges[2*i + 0].dh = dh;
        ridges[2*i + 1].pos = centre + w;
        ridges[2*i + 1].dh = -dh;
    }
    qsort(ridges, 2*nridges, sizeof(LNoSynthRidgeEvent), compare_ridge_events);

    /* Ensure sentinel */
    ridges[2*nridges].pos = 1.01;
    ridges[2*nridges].dh = 0.0;

    data = gwy_data_field_get_data(field);
    is = 0;
    h = 0.0;
    for (i = 0; i < yres; i++) {
        gdouble *row = data + i*xres;
        for (j = 0; j < xres; j++) {
            gdouble x = (lineprob*(j + 0.5)/xres + i)/yres;

            while (x > ridges[is].pos) {
                h += ridges[is].dh;
                is++;
            }

            if (scandir == SCANDIR_LTR)
                row[j] += h;
            else
                row[xres-1 - j] += h;
        }
    }

    g_free(ridges);
}

/*****************************************************************************
 *
 * Tilt
 *
 *****************************************************************************/

static void
define_params_tilt(GwyParamDef *paramdef)
{
    gwy_param_def_add_double(paramdef, PARAM_TILT_OFFSET_VAR, "tilt/offset_var", _("Offset _dispersion"),
                             0.0, 1.0, 0.3);
}

static void
append_gui_tilt(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_noise[LNO_SYNTH_TILT];

    gwy_param_table_append_slider(table, PARAM_TILT_OFFSET_VAR);
}

static void
dimensions_changed_tilt(G_GNUC_UNUSED ModuleGUI *gui)
{
}

static void
make_noise_tilt(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise)
{
    GwyParams *params = args->params;
    gdouble offset_var = gwy_params_get_double(params, PARAM_TILT_OFFSET_VAR);
    GwyDataField *field = args->result;
    gdouble *data;
    guint xres, yres, i, j;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    data = gwy_data_field_get_data(field);
    for (i = 0; i < yres; i++) {
        gdouble *row = data + i*xres;
        gdouble dz = point_noise(rngset, sigma);
        gdouble dx = gwy_rand_gen_set_gaussian(rngset, RNG_POS, 2.0*offset_var);

        for (j = 0; j < xres; j++) {
            gdouble x = (2.0*j + 1.0)/xres - 1.0 + dx;
            row[j] += x*dz;
        }
    }
}

/*****************************************************************************
 *
 * Hum
 *
 *****************************************************************************/

typedef struct {
    gdouble frequency;
    gdouble amplitude;
    gdouble phase;
} HumComponent;

static void
define_params_hum(GwyParamDef *paramdef)
{
    gwy_param_def_add_double(paramdef, PARAM_HUM_WAVELENGTH, "hum/wavelength", _("_Wavelength"), 1.0, 1e4, 10.0);
    gwy_param_def_add_double(paramdef, PARAM_HUM_SPREAD, "hum/spread", _("_Spread"), 1e-4, 1.0, 0.001);
    gwy_param_def_add_int(paramdef, PARAM_HUM_NCOMP, "hum/ncontrip", _("Co_mponents"), 1, 100, 16);
}

static void
append_gui_hum(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_noise[LNO_SYNTH_HUM];

    gwy_param_table_append_slider(table, PARAM_HUM_WAVELENGTH);
    gwy_param_table_slider_set_mapping(table, PARAM_HUM_WAVELENGTH, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_HUM_WAVELENGTH);
    gwy_param_table_append_slider(table, PARAM_HUM_SPREAD);
    gwy_param_table_append_slider(table, PARAM_HUM_NCOMP);
}

static void
dimensions_changed_hum(ModuleGUI *gui)
{
    static const gint xyids[] = { PARAM_HUM_WAVELENGTH };

    gwy_synth_update_lateral_alts(gui->table_noise[LNO_SYNTH_HUM], xyids, G_N_ELEMENTS(xyids));
}

static void
make_noise_hum(ModuleArgs *args, gdouble sigma, GwyRandGenSet *rngset, PointNoiseFunc point_noise)
{
    GwyParams *params = args->params;
    gdouble wavelength = gwy_params_get_double(params, PARAM_HUM_WAVELENGTH);
    gdouble spread = gwy_params_get_double(params, PARAM_HUM_SPREAD);
    gint ncomp = gwy_params_get_int(params, PARAM_HUM_NCOMP);
    GwyDataField *field = args->result;
    gdouble *data;
    guint xres, yres, i, j, k;
    HumComponent *humcomp;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    humcomp = g_new(HumComponent, ncomp*yres);
    /* Fill contributions in a manner stable wrt ncomp. */
    for (k = 0; k < ncomp; k++) {
        for (i = 0; i < yres; i++) {
            HumComponent *hc = humcomp + i*ncomp + k;

            hc->amplitude = point_noise(rngset, sigma);
            hc->frequency = 2.0*G_PI/(wavelength*gwy_rand_gen_set_multiplier(rngset, RNG_POS, 0.999999*spread));
            hc->phase = 2.0*G_PI*gwy_rand_gen_set_double(rngset, RNG_POS);
        }
    }
    for (i = 0; i < yres; i++) {
        HumComponent *block = humcomp + i*ncomp;
        gdouble s = 0.0;

        for (k = 0; k < ncomp; k++)
            s += block[k].amplitude * block[k].amplitude;
        if (!s)
            s = 1.0;
        s = G_SQRT2*sigma/sqrt(s);

        for (k = 0; k < ncomp; k++)
            block[k].amplitude *= s;
    }

    data = gwy_data_field_get_data(field);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(data,humcomp,xres,yres,ncomp) \
            private(i,j,k)
#endif
    for (i = 0; i < yres; i++) {
        gdouble *row = data + i*xres;
        const HumComponent *block = humcomp + i*ncomp;

        for (j = 0; j < xres; j++) {
            gdouble s = 0.0;

            for (k = 0; k < ncomp; k++)
                s += sin(j*block[k].frequency + block[k].phase)*block[k].amplitude;
            row[j] += s;
        }
    }

    g_free(humcomp);
}

/*****************************************************************************
 *
 * Noise generators
 *
 *****************************************************************************/

/* XXX: Sometimes the generators seem unnecessarily complicated; this is to make the positive and negative noise
 * related to the symmetrical one. */

static gdouble
noise_gaussian_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_gaussian(rng, 0, sigma);
}

static gdouble
noise_gaussian_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_gaussian(rng, 0, sigma));
}

static gdouble
noise_gaussian_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_gaussian(rng, 0, sigma));
}

static gdouble
noise_exp_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_exponential(rng, 0, sigma);
}

static gdouble
noise_exp_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_exponential(rng, 0, sigma));
}

static gdouble
noise_exp_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_exponential(rng, 0, sigma));
}

static gdouble
noise_uniform_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_uniform(rng, 0, sigma);
}

static gdouble
noise_uniform_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_uniform(rng, 0, sigma));
}

static gdouble
noise_uniform_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_uniform(rng, 0, sigma));
}

static gdouble
noise_triangle_both(GwyRandGenSet *rng, gdouble sigma)
{
    return gwy_rand_gen_set_triangular(rng, 0, sigma);
}

static gdouble
noise_triangle_up(GwyRandGenSet *rng, gdouble sigma)
{
    return fabs(gwy_rand_gen_set_triangular(rng, 0, sigma));
}

static gdouble
noise_triangle_down(GwyRandGenSet *rng, gdouble sigma)
{
    return -fabs(gwy_rand_gen_set_triangular(rng, 0, sigma));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
