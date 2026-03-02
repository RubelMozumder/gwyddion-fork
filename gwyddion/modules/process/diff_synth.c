/*
 *  $Id: diff_synth.c 23781 2021-05-25 15:40:21Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti).
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
#include <libprocess/grains.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define WORK_UPDATE_CHECK 1000000

/* Cannot change this without losing reproducibility again! */
enum {
    NRANDOM_GENERATORS = 24
};

typedef enum {
    GRAPH_VAR     = 0,
    GRAPH_NGRAINS = 1,
    GRAPH_NGRAPHS
} GraphFlags;

typedef enum {
    NEIGH_UP        = 0,
    NEIGH_LEFT      = 1,
    NEIGH_RIGHT     = 2,
    NEIGH_DOWN      = 3,
    NEIGH_SCHWOEBEL = 4,   /* Bit offset indicating Schwoebel barrier. */
} ParticleNeighbours;

enum {
    PARAM_COVERAGE,
    PARAM_FLUX,
    PARAM_HEIGHT,
    PARAM_P_STICK,
    PARAM_P_BREAK,
    PARAM_SCHWOEBEL,
    PARAM_SCHWOEBEL_ENABLE,
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

/* The model permits one movable particle on top of another.  However, we do not keep track which is which and always
 * assume the particle we are processing right now is the top one.  This adds some mobility boost to such particles
 * and breaks these stacks quickly.  No representation of neighbour relations in the z-direction is then necessary. */
typedef struct {
    guint col;
    guint row;
    guint k;
    guint kup;
    guint kleft;
    guint kright;
    guint kdown;
    /* Nehgibours blocking movement. */
    guint nneigh;
    guint neighbours;
} Particle;

typedef struct {
    GwyRandGenSet *rngset;
    GRand *rng;
    gdouble *numbers;
    gint pos;
    gint size;
} RandomDoubleSource;

typedef struct {
    GwyRandGenSet *rngset;
    GRand *rng;
    guint32 *numbers;
    gint pos;
    gint size;
    gint nspare;
    guint32 spare;
} RandomIntSource;

typedef struct {
    guint *hfield;
    guint xres;
    guint yres;
    GArray *particles;
    GwyRandGenSet *rngset;
    RandomDoubleSource randbl;
    RandomIntSource ranint;
    gdouble flux;        /* Linear */
    gdouble schwoebel;   /* Linear */
    gdouble fluxperiter;
    gdouble fluence;
    guint64 iter;
    gboolean use_schwoebel;
    gdouble p_stick[5];
    gdouble p_break[5];
} DiffSynthState;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
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

static gboolean           module_register          (void);
static GwyParamDef*       define_module_params     (void);
static void               diff_synth               (GwyContainer *data,
                                                    GwyRunType runtype);
static void               plot_evolution_graphs    (ModuleArgs *args,
                                                    const GwyAppDataId *dataid);
static gboolean           execute                  (ModuleArgs *args,
                                                    GtkWindow *wait_window);
static GwyDialogOutcome   run_gui                  (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static GtkWidget*         dimensions_tab_new       (ModuleGUI *gui);
static GtkWidget*         generator_tab_new        (ModuleGUI *gui);
static GtkWidget*         evolution_tab_new        (ModuleGUI *gui);
static void               param_changed            (ModuleGUI *gui,
                                                    gint id);
static void               dialog_response          (ModuleGUI *gui,
                                                    gint response);
static void               preview                  (gpointer user_data);
static void               particle_try_move        (Particle *p,
                                                    guint *hfield,
                                                    guint xres,
                                                    guint yres,
                                                    gboolean use_schwoebel,
                                                    gdouble schwoebel,
                                                    RandomDoubleSource *randbl,
                                                    RandomIntSource *ranint,
                                                    const gdouble *p_break);
static void               one_iteration            (DiffSynthState *dstate);
static void               add_particle             (DiffSynthState *dstate);
static void               finalize_moving_particles(DiffSynthState *dstate);
static DiffSynthState*    diff_synth_state_new     (guint xres,
                                                    guint yres,
                                                    ModuleArgs *args);
static void               diff_synth_state_free    (DiffSynthState *dstate);
static gdouble            count_grains             (GwyDataField *field);

static const EvolutionStatInfo evolution_info[GRAPH_NGRAPHS] = {
    { gwy_data_field_get_variation, 1, 1, },
    { count_grains,                 0, 0, },
};

static const GwyEnum graph_outputs[GRAPH_NGRAPHS] = {
    { N_("Variation"),         (1 << GRAPH_VAR),     },
    { N_("Number of islands"), (1 << GRAPH_NGRAINS), },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates surfaces by diffusion limited aggregation."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, diff_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("diff_synth",
                              (GwyProcessFunc)&diff_synth,
                              N_("/S_ynthetic/_Deposition/_Diffusion..."),
                              GWY_STOCK_SYNTHETIC_DIFFUSION,
                              RUN_MODES,
                              0,
                              N_("Generate surface by diffusion limited aggregation"));

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
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 0.0, 16.0, 0.25);
    gwy_param_def_add_double(paramdef, PARAM_FLUX, "flux", _("_Flux"), -13.0, -3.0, -10.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-5, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_P_STICK, "p_stick", _("_Sticking"), 0.0, 1.0, 0.1);
    gwy_param_def_add_double(paramdef, PARAM_P_BREAK, "p_break", _("_Activation"), 0.0, 1.0, 0.01);
    gwy_param_def_add_double(paramdef, PARAM_SCHWOEBEL, "schwoebel", _("Passing Sch_woebel"), -12.0, 0.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SCHWOEBEL_ENABLE, "schwoebel_enable", NULL, FALSE);
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
diff_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Diffusion Limited Aggregation"));
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

    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_append_slider(table, PARAM_FLUX);
    gwy_param_table_slider_set_mapping(table, PARAM_FLUX, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_set_unitstr(table, PARAM_FLUX, "log<sub>10</sub>");
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    gwy_param_table_append_header(table, -1, _("Probabilities"));
    gwy_param_table_append_slider(table, PARAM_P_STICK);
    gwy_param_table_append_slider(table, PARAM_P_BREAK);
    gwy_param_table_append_slider(table, PARAM_SCHWOEBEL);
    gwy_param_table_set_unitstr(table, PARAM_SCHWOEBEL, "log<sub>10</sub>");
    gwy_param_table_slider_set_mapping(table, PARAM_SCHWOEBEL, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_add_enabler(table, PARAM_SCHWOEBEL_ENABLE, PARAM_SCHWOEBEL);

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

static void
copy_hfield_to_data_field(DiffSynthState *dstate, GwyDataField *field, gdouble zscale)
{
    GArray *particles = dstate->particles;
    guint *hfield = dstate->hfield;
    guint xres = dstate->xres, yres = dstate->yres;
    gdouble *data = gwy_data_field_get_data(field);
    guint k;

    for (k = 0; k < xres*yres; k++)
        data[k] = hfield[k] * zscale;

    /* Exclude the still-free particles.  This makes more sense if plotting for instance the number of islands over
     * time.  It also avoids random scale jumps in the progessive preview. */
    for (k = 0; k < particles->len; k++) {
        Particle *p = &g_array_index(particles, Particle, k);
        data[p->k] -= zscale;
    }
}

static inline gdouble
random_double(RandomDoubleSource *randbl)
{
    if (randbl->pos == randbl->size) {
        gwy_rand_gen_set_fill_doubles(randbl->rngset, randbl->numbers, randbl->size);
        randbl->pos = 0;
    }
    return randbl->numbers[randbl->pos++];
}

static inline guint32
random_int(RandomIntSource *ranint)
{
    if (ranint->pos == ranint->size) {
        gwy_rand_gen_set_fill_ints(ranint->rngset, ranint->numbers, ranint->size);
        ranint->pos = 0;
    }
    return ranint->numbers[ranint->pos++];
}

static inline guint32
random_int_range(RandomIntSource *ranint, guint32 upper_bound)
{
    guint32 value;

    do {
        value = random_int(ranint);
    } while (value >= G_MAXUINT32/upper_bound*upper_bound);

    return value % upper_bound;
}

static inline ParticleNeighbours
random_direction(RandomIntSource *ranint)
{
    ParticleNeighbours direction;

    if (ranint->nspare) {
        direction = ranint->spare & 0x3;
        ranint->spare >>= 2;
        ranint->nspare--;
    }
    else {
        ranint->spare = random_int(ranint);
        direction = ranint->spare & 0x3;
        ranint->spare >>= 2;
        ranint->nspare = 7;
    }

    return direction;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble zscale = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    guint graph_flags = gwy_params_get_flags(params, PARAM_GRAPH_FLAGS);
    GwyDataField *field = args->result;
    GArray **evolution = args->evolution[0] ? args->evolution : NULL;
    gdouble v, nextgraphx, threshold, height, preview_time = (animated ? 1.25 : 0.0);
    guint64 workdone, niter, iter;
    gint xres, yres, k, power10z;
    const gdouble *data;
    GwySynthUpdateType update;
    DiffSynthState *dstate = NULL;
    gboolean finished = FALSE;
    GArray *particles;
    GTimer *timer;
    guint i;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    zscale *= pow10(power10z);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    dstate = diff_synth_state_new(xres, yres, args);

    if (args->field && do_initialise) {
        data = gwy_data_field_get_data_const(args->field);
        threshold = gwy_data_field_otsu_threshold(args->field);
        for (k = 0; k < xres*yres; k++)
            dstate->hfield[k] = (data[k] > threshold);
    }

    particles = dstate->particles;
    workdone = nextgraphx = 0.0;
    niter = (guint64)(coverage/dstate->flux + 0.5);
    iter = 0;
    dstate->fluxperiter = xres*yres * dstate->flux;

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Depositing particles...")))
        goto end;

    while (iter < niter) {
        workdone += particles->len;
        one_iteration(dstate);
        /* Optimize the low-flux case when there may be no free particles at all for relatively long time periods and
         * skip to the time when another particle arrives. */
        if (particles->len)
            iter++;
        else {
            add_particle(dstate);
            iter += (guint64)((1.0 - dstate->fluence)/dstate->fluxperiter + 0.5);
            dstate->fluence = 0.0;
        }

        if (workdone >= WORK_UPDATE_CHECK) {
            update = gwy_synth_update_progress(timer, preview_time, iter, niter);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                copy_hfield_to_data_field(dstate, field, zscale);
                gwy_data_field_data_changed(field);
            }
            workdone -= WORK_UPDATE_CHECK;
        }

        if (evolution && iter >= nextgraphx) {
            copy_hfield_to_data_field(dstate, field, zscale);
            for (i = 0; i < GRAPH_NGRAPHS; i++) {
                if (graph_flags & (1 << i)) {
                    v = evolution_info[i].func(field);
                    g_array_append_val(evolution[i], v);
                }
            }
            height = iter*dstate->fluxperiter * zscale;
            g_array_append_val(evolution[GRAPH_NGRAPHS], height);

            nextgraphx += 0.0001/dstate->flux + MIN(0.2*nextgraphx, 0.08/dstate->flux);
        }
    }

    finalize_moving_particles(dstate);
    copy_hfield_to_data_field(dstate, field, zscale);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_timer_destroy(timer);
    diff_synth_state_free(dstate);

    return finished;
}

static void
particle_update_neighbours(Particle *p, const guint *hfield, gboolean use_schwoebel)
{
    guint h = hfield[p->k];
    guint neighbours = 0, nneigh = 0;

    if (hfield[p->kup] >= h) {
        neighbours |= (1 << NEIGH_UP);
        nneigh++;
    }
    if (hfield[p->kleft] >= h) {
        neighbours |= (1 << NEIGH_LEFT);
        nneigh++;
    }
    if (hfield[p->kright] >= h) {
        neighbours |= (1 << NEIGH_RIGHT);
        nneigh++;
    }
    if (hfield[p->kdown] >= h) {
        neighbours |= (1 << NEIGH_DOWN);
        nneigh++;
    }

    if (use_schwoebel) {
        if (hfield[p->kup] + 1 < h)
            neighbours |= (1 << (NEIGH_UP + NEIGH_SCHWOEBEL));
        if (hfield[p->kleft] + 1 < h)
            neighbours |= (1 << (NEIGH_LEFT + NEIGH_SCHWOEBEL));
        if (hfield[p->kright] + 1 < h)
            neighbours |= (1 << (NEIGH_RIGHT + NEIGH_SCHWOEBEL));
        if (hfield[p->kdown] + 1 < h)
            neighbours |= (1 << (NEIGH_DOWN + NEIGH_SCHWOEBEL));
    }
    p->neighbours = neighbours;
    p->nneigh = nneigh;
}

/* This must be called with nehgibours information updated. */
static void
particle_try_move(Particle *p,
                  guint *hfield, guint xres, guint yres,
                  gboolean use_schwoebel, const gdouble schwoebel,
                  RandomDoubleSource *randbl, RandomIntSource *ranint,
                  const gdouble *p_break)
{
    ParticleNeighbours direction = random_direction(ranint);

    if (p->neighbours & (1 << direction))
        return;

    if (use_schwoebel
        && (p->neighbours & (1 << (direction + NEIGH_SCHWOEBEL)))
        && random_double(randbl) >= schwoebel)
        return;

    if (random_double(randbl) >= p_break[p->nneigh])
        return;

    hfield[p->k]--;
    /* XXX: This is broken for 3×3 images and smaller.  Do we care? */
    if (direction == NEIGH_UP) {
        gint move = -xres, wrap_around = xres*(yres - 1);
        if (p->row >= 2 && p->row < yres-1) {
            p->row--;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
        else if (p->row == 1) {
            p->row--;
            p->k += move;
            p->kup += wrap_around;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
        else if (!p->row) {
            p->row = yres-1;
            p->k += wrap_around;
            p->kup += move;
            p->kleft += wrap_around;
            p->kright += wrap_around;
            p->kdown += move;
        }
        else {
            p->row--;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += wrap_around;
        }
    }
    else if (direction == NEIGH_LEFT) {
        gint move = -1, wrap_around = xres - 1;
        if (p->col >= 2 && p->col < xres-1) {
            p->col--;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
        else if (p->col == 1) {
            p->col--;
            p->k += move;
            p->kup += move;
            p->kleft += wrap_around;
            p->kright += move;
            p->kdown += move;
        }
        else if (!p->col) {
            p->col = xres-1;
            p->k += wrap_around;
            p->kup += wrap_around;
            p->kleft += move;
            p->kright += move;
            p->kdown += wrap_around;
        }
        else {
            p->col--;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += wrap_around;
            p->kdown += move;
        }
    }
    else if (direction == NEIGH_RIGHT) {
        gint move = 1, wrap_around = 1 - xres;
        if (p->col && p->col < xres-2) {
            p->col++;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
        else if (p->col == xres-2) {
            p->col++;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += wrap_around;
            p->kdown += move;
        }
        else if (p->col == xres-1) {
            p->col = 0;
            p->k += wrap_around;
            p->kup += wrap_around;
            p->kleft += move;
            p->kright += move;
            p->kdown += wrap_around;
        }
        else {
            p->col++;
            p->k += move;
            p->kup += move;
            p->kleft += wrap_around;
            p->kright += move;
            p->kdown += move;
        }
    }
    else {
        gint move = xres, wrap_around = xres*(1 - yres);
        if (p->row && p->row < yres-2) {
            p->row++;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
        else if (p->row == yres-2) {
            p->row++;
            p->k += move;
            p->kup += move;
            p->kleft += move;
            p->kright += move;
            p->kdown += wrap_around;
        }
        else if (p->row == xres-1) {
            p->row = 0;
            p->k += wrap_around;
            p->kup += move;
            p->kleft += wrap_around;
            p->kright += wrap_around;
            p->kdown += move;
        }
        else {
            p->row++;
            p->k += move;
            p->kup += wrap_around;
            p->kleft += move;
            p->kright += move;
            p->kdown += move;
        }
    }
    hfield[p->k]++;
}

static void
add_particle(DiffSynthState *dstate)
{
    gint xres = dstate->xres, yres = dstate->yres;
    Particle p;

    p.col = random_int_range(&dstate->ranint, xres);
    p.row = random_int_range(&dstate->ranint, yres);
    p.k = p.row*xres + p.col;
    p.kup = p.row ? p.k - xres : p.k + xres*(yres - 1);
    p.kleft = p.col ? p.k - 1 : p.k + xres - 1;
    p.kright = p.col < xres-1 ? p.k + 1 : p.k - (xres - 1);
    p.kdown = p.row < yres-1 ? p.k + xres : p.k - xres*(yres - 1);
    g_array_append_val(dstate->particles, p);
    dstate->hfield[p.k]++;
}

static void
one_iteration(DiffSynthState *dstate)
{
    GArray *particles = dstate->particles;
    guint xres = dstate->xres, yres = dstate->yres;
    guint *hfield = dstate->hfield;
    const gdouble *p_break = dstate->p_break;
    const gdouble *p_stick = dstate->p_stick;
    gboolean use_schwoebel = dstate->use_schwoebel;
    gdouble schwoebel = dstate->schwoebel;
    RandomDoubleSource *randbl = &dstate->randbl;
    RandomIntSource *ranint = &dstate->ranint;
    guint i = 0;

    while (i < particles->len) {
        Particle *p = &g_array_index(particles, Particle, i);
        gdouble ps;

        particle_update_neighbours(p, hfield, use_schwoebel);
        ps = p_stick[p->nneigh];
        if (ps == 1.0 || (ps && random_double(randbl) < ps))
            g_array_remove_index_fast(particles, i);
        else {
            // XXX: We may also consider desorption here.
            particle_try_move(p, hfield, xres, yres, use_schwoebel, schwoebel, randbl, ranint, p_break);
            i++;
        }
    }

    dstate->fluence += dstate->fluxperiter;
    while (dstate->fluence >= 1.0) {
        add_particle(dstate);
        dstate->fluence -= 1.0;
    }
}

static void
finalize_moving_particles(DiffSynthState *dstate)
{
    GArray *particles = dstate->particles;
    guint *hfield = dstate->hfield;
    const gdouble *p_stick = dstate->p_stick;
    RandomDoubleSource *randbl = &dstate->randbl;
    guint i = 0;

    while (i < particles->len) {
        Particle *p = &g_array_index(particles, Particle, i);
        gdouble ps;

        particle_update_neighbours(p, hfield, FALSE);
        ps = p_stick[p->nneigh];
        if (ps == 1.0 || (ps && random_double(randbl) < ps))
            g_array_remove_index_fast(particles, i);
        else
            i++;
    }
}

static DiffSynthState*
diff_synth_state_new(guint xres, guint yres, ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble p_stick = gwy_params_get_double(params, PARAM_P_STICK);
    gdouble p_break = gwy_params_get_double(params, PARAM_P_BREAK);
    gdouble schwoebel = gwy_params_get_double(params, PARAM_SCHWOEBEL);
    gdouble flux = gwy_params_get_double(params, PARAM_FLUX);
    gboolean schwoebel_enable = gwy_params_get_boolean(params, PARAM_SCHWOEBEL_ENABLE);
    DiffSynthState *dstate = g_new0(DiffSynthState, 1);
    guint i;

    dstate->rngset = gwy_rand_gen_set_new(NRANDOM_GENERATORS);
    gwy_rand_gen_set_init(dstate->rngset, gwy_params_get_int(params, PARAM_SEED));

    dstate->randbl.pos = dstate->randbl.size = xres*yres;
    dstate->randbl.numbers = g_new(gdouble, dstate->randbl.size);
    dstate->randbl.rngset = dstate->rngset;
    dstate->randbl.rng = gwy_rand_gen_set_rng(dstate->rngset, 0);

    dstate->ranint.pos = dstate->ranint.size = 2*xres*yres;
    dstate->ranint.nspare = 0;
    dstate->ranint.numbers = g_new(guint32, dstate->ranint.size);
    dstate->ranint.rngset = dstate->rngset;
    dstate->ranint.rng = gwy_rand_gen_set_rng(dstate->rngset, 0);

    dstate->xres = xres;
    dstate->yres = yres;
    dstate->hfield = g_new0(guint, dstate->xres*dstate->yres);
    dstate->particles = g_array_new(FALSE, FALSE, sizeof(Particle));
    dstate->fluence = 0.0;

    dstate->p_break[0] = 1.0;
    for (i = 1; i < 5; i++)
        dstate->p_break[i] = p_break * dstate->p_break[i-1];

    dstate->p_stick[0] = 0.0;
    dstate->p_stick[1] = p_stick;
    for (i = 2; i < 4; i++) {
        dstate->p_stick[i] = 1.0 - pow(1.0 - dstate->p_stick[i-1], 2.0);
        dstate->p_stick[i] = CLAMP(dstate->p_stick[i], 0.0, 1.0);
    }
    dstate->p_stick[4] = 1.0;

    dstate->flux = pow10(flux);
    dstate->schwoebel = pow10(schwoebel);
    dstate->use_schwoebel = schwoebel_enable;

    return dstate;
}

static void
diff_synth_state_free(DiffSynthState *dstate)
{
    g_free(dstate->randbl.numbers);
    g_free(dstate->ranint.numbers);
    g_free(dstate->hfield);
    gwy_rand_gen_set_free(dstate->rngset);
    g_array_free(dstate->particles, TRUE);
    g_free(dstate);
}

static gdouble
count_grains(GwyDataField *field)
{
    gint ngrains, n = gwy_data_field_get_xres(field)*gwy_data_field_get_yres(field);
    gint *grains = g_new0(gint, n);
    ngrains = gwy_data_field_number_grains_periodic(field, grains);
    g_free(grains);
    return ngrains;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
