/*
 *  $Id: wave_synth.c 23795 2021-05-27 13:20:02Z yeti-dn $
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
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/simplefft.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* 18 is what fits into L2 cache on all modern processors (3*2¹⁸ floats ≈ 3 MB) */
enum {
    APPROX_WAVE_BITS = 16,
    APPROX_WAVE_SIZE = 1 << APPROX_WAVE_BITS,
    APPROX_WAVE_MASK = APPROX_WAVE_SIZE - 1,
};

typedef enum {
    WAVE_FORM_COSINE  = 0,
    WAVE_FORM_INVCOSH = 1,
    WAVE_FORM_FLATTOP = 2,
} WaveTypeType;

typedef enum {
    WAVE_QUANTITY_DISPLACEMENT = 0,
    WAVE_QUANTITY_AMPLITUDE    = 1,
    WAVE_QUANTITY_PHASE        = 2,
} WaveQuantityType;

enum {
    PARAM_TYPE,
    PARAM_NWAVES,
    PARAM_QUANTITY,
    PARAM_AMPLITUDE,
    PARAM_AMPLITUDE_NOISE,
    PARAM_DECAY,
    PARAM_DECAY_NOISE,
    PARAM_K,
    PARAM_K_NOISE,
    PARAM_X,
    PARAM_X_NOISE,
    PARAM_Y,
    PARAM_Y_NOISE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef struct {
    gdouble x;
    gdouble y;
    gdouble z;
    gdouble k;
    gdouble decay;
} WaveSource;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
    /* Precalculated expensive data. */
    gfloat *wave_table;
    gboolean wave_table_is_valid;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyParamTable *table_placement;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register        (void);
static GwyParamDef*     define_module_params   (void);
static void             wave_synth             (GwyContainer *data,
                                                GwyRunType runtype);
static gboolean         execute                (ModuleArgs *args,
                                                GtkWindow *wait_window,
                                                gboolean show_progress_bar);
static GwyDialogOutcome run_gui                (ModuleArgs *args,
                                                GwyContainer *data,
                                                gint id);
static GtkWidget*       dimensions_tab_new     (ModuleGUI *gui);
static GtkWidget*       generator_tab_new      (ModuleGUI *gui);
static GtkWidget*       placement_tab_new      (ModuleGUI *gui);
static void             param_changed          (ModuleGUI *gui,
                                                gint id);
static void             dialog_response        (ModuleGUI *gui,
                                                gint response);
static void             preview                (gpointer user_data);
static void             precalculate_wave_table(gfloat *tab,
                                                guint n,
                                                WaveTypeType type);
static void             complement_table       (gdouble *dbltab,
                                                gfloat *tab,
                                                guint n);
static void             complement_wave        (const gdouble *cwave,
                                                gdouble *swave,
                                                guint n);
static void             randomize_sources      (WaveSource *sources,
                                                ModuleArgs *args,
                                                guint xres,
                                                guint yres);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates various kinds of waves."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, wave_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("wave_synth",
                              (GwyProcessFunc)&wave_synth,
                              N_("/S_ynthetic/_Waves..."),
                              GWY_STOCK_SYNTHETIC_WAVES,
                              RUN_MODES,
                              0,
                              N_("Generate waves"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum wave_forms[] = {
        { N_("Cosine"),       WAVE_FORM_COSINE  },
        { N_("Inverse cosh"), WAVE_FORM_INVCOSH },
        { N_("Flat top"),     WAVE_FORM_FLATTOP },
    };
    static const GwyEnum quantities[] = {
        { N_("Displacement"), WAVE_QUANTITY_DISPLACEMENT },
        { N_("Amplitude"),    WAVE_QUANTITY_AMPLITUDE    },
        { N_("Phase"),        WAVE_QUANTITY_PHASE        },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_QUANTITY, "quantity", _("_Quantity"),
                              quantities, G_N_ELEMENTS(quantities), WAVE_QUANTITY_AMPLITUDE);
    gwy_param_def_add_int(paramdef, PARAM_NWAVES, "nwaves", _("_Number of waves"), 1, 2000, 50);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Wave form"),
                              wave_forms, G_N_ELEMENTS(wave_forms), WAVE_FORM_COSINE);
    gwy_param_def_add_double(paramdef, PARAM_AMPLITUDE, "amplitude", _("_Amplitude"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_AMPLITUDE_NOISE, "amplitude_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_DECAY, "decay", _("_Decay"), -5.0, 0.0, -5.0);
    gwy_param_def_add_double(paramdef, PARAM_DECAY_NOISE, "decay_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_K, "k", _("_Spatial frequency"), 0.01, 1000.0, 30.0);
    gwy_param_def_add_double(paramdef, PARAM_K_NOISE, "k_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_X, "x", _("_X center"), -1000.0, 1000.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_X_NOISE, "x_noise", _("Spread"), 0.0, 1.0, 0.3);
    gwy_param_def_add_double(paramdef, PARAM_Y, "y", _("_Y center"), -1000.0, 1000.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_Y_NOISE, "y_noise", _("Spread"), 0.0, 1.0, 0.3);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
wave_synth(GwyContainer *data, GwyRunType runtype)
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
    args.wave_table = g_new(gfloat, 2*APPROX_WAVE_SIZE);

    args.params = gwy_params_new_from_settings(define_module_params());
    gwy_synth_sanitise_params(args.params, PARAM_DIMS0, field);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    args.result = gwy_synth_make_result_data_field((args.field = field), args.params, FALSE);
    if (!execute(&args, gwy_app_find_window_for_channel(data, id), TRUE))
        goto end;
    gwy_synth_add_result_to_file(args.result, data, id, args.params);

end:
    g_free(args.wave_table);
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

    gui.dialog = gwy_dialog_new(_("Waves"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gtk_notebook_append_page(notebook, placement_tab_new(&gui), gtk_label_new(_("Placement")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_placement, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

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

    gwy_param_table_append_combo(table, PARAM_QUANTITY);
    gwy_param_table_append_slider(table, PARAM_NWAVES);

    gwy_param_table_append_header(table, -1, _("Amplitude"));
    gwy_param_table_append_combo(table, PARAM_TYPE);
    gwy_param_table_append_slider(table, PARAM_AMPLITUDE);
    gwy_param_table_slider_set_mapping(table, PARAM_AMPLITUDE, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_AMPLITUDE_NOISE);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_DECAY);
    gwy_param_table_set_unitstr(table, PARAM_DECAY, "log<sub>10</sub>");
    gwy_param_table_append_slider(table, PARAM_DECAY_NOISE);

    gwy_param_table_append_header(table, -1, _("Frequency"));
    gwy_param_table_append_slider(table, PARAM_K);
    gwy_param_table_append_slider(table, PARAM_K_NOISE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
placement_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_placement = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_header(table, -1, _("Position"));
    gwy_param_table_append_slider(table, PARAM_X);
    gwy_param_table_append_slider(table, PARAM_X_NOISE);
    gwy_param_table_append_slider(table, PARAM_Y);
    gwy_param_table_append_slider(table, PARAM_Y_NOISE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_AMPLITUDE };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }

    if (id < 0 || id == PARAM_TYPE)
        gui->args->wave_table_is_valid = FALSE;
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
            gwy_param_table_set_double(gui->table_generator, PARAM_AMPLITUDE, zscale/pow10(power10z));
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

    /* Do not show a progress bar for the preview.  For small images it is fast enough. */
    execute(gui->args, NULL, FALSE);
    gwy_data_field_data_changed(gui->args->result);
}

static inline void
approx_wave_sc(const gfloat *tab, gdouble x, gfloat *s, gfloat *c)
{
    guint xi = (guint)(x*(APPROX_WAVE_SIZE/(2.0*G_PI))) & APPROX_WAVE_MASK;
    *c = tab[xi];
    *s = tab[xi + APPROX_WAVE_SIZE];
}

static inline gfloat
approx_wave_c(const gfloat *tab, gdouble x)
{
    guint xi = (guint)(x*(APPROX_WAVE_SIZE/(2.0*G_PI))) & APPROX_WAVE_MASK;
    return tab[xi];
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window, gboolean show_progress_bar)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    guint nwaves = gwy_params_get_int(params, PARAM_NWAVES);
    WaveTypeType type = gwy_params_get_enum(params, PARAM_TYPE);
    WaveQuantityType quantity = gwy_params_get_enum(params, PARAM_QUANTITY);
    GwyDataField *result = args->result;
    guint xres = gwy_data_field_get_xres(result), yres = gwy_data_field_get_yres(result);
    WaveSource *sources;
    gdouble *d;
    gfloat *tab;
    gdouble q;
    GwySetFractionFunc set_fraction = (show_progress_bar ? gwy_app_wait_set_fraction : NULL);
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    if (show_progress_bar)
        gwy_app_wait_start(wait_window, _("Initializing..."));

    if (args->field && do_initialise)
        gwy_data_field_copy(args->field, result, FALSE);
    else
        gwy_data_field_clear(result);

    tab = args->wave_table;
    if (!args->wave_table_is_valid) {
        precalculate_wave_table(tab, APPROX_WAVE_SIZE, type);
        args->wave_table_is_valid = TRUE;
    }

    sources = g_new(WaveSource, nwaves);
    randomize_sources(sources, args, xres, yres);

    if (show_progress_bar && !gwy_app_wait_set_message(_("Rendering surface..."))) {
        cancelled = TRUE;
        goto end;
    }

    d = gwy_data_field_get_data(result);
    if (quantity == WAVE_QUANTITY_DISPLACEMENT) {
        q = 2.0/sqrt(nwaves);
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,sources,tab,xres,yres,nwaves,q,set_fraction,pcancelled)
#endif
        {
            gint ifrom = gwy_omp_chunk_start(yres);
            gint ito = gwy_omp_chunk_end(yres);
            gint i, j, k;

            for (i = ifrom; i < ito; i++) {
                for (j = 0; j < xres; j++) {
                    const WaveSource *source = sources;
                    gfloat z = 0.0;

                    for (k = nwaves; k; k--, source++) {
                        gdouble x = j - source->x, y = i - source->y;
                        gdouble kr = source->k * sqrt(x*x + y*y);
                        gdouble dec = exp(-kr*source->decay);
                        z += dec*source->z * approx_wave_c(tab, kr);
                    }
                    d[i*xres + j] += q*z;
                }
                if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito, pcancelled))
                    break;
            }
        }
    }
    else if (quantity == WAVE_QUANTITY_AMPLITUDE) {
        q = 2.0/sqrt(nwaves);
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,sources,tab,xres,yres,nwaves,q,set_fraction,pcancelled)
#endif
        {
            gint ifrom = gwy_omp_chunk_start(yres);
            gint ito = gwy_omp_chunk_end(yres);
            gint i, j, k;

            for (i = ifrom; i < ito; i++) {
                for (j = 0; j < xres; j++) {
                    const WaveSource *source = sources;
                    gfloat zc = 0.0, zs = 0.0;

                    for (k = nwaves; k; k--, source++) {
                        gdouble x = j - source->x, y = i - source->y;
                        gdouble kr = source->k * sqrt(x*x + y*y);
                        gdouble dec = exp(-kr*source->decay);
                        gfloat c, s;
                        approx_wave_sc(tab, kr, &s, &c);
                        zs += dec*source->z*s;
                        zc += dec*source->z*c;
                    }
                    d[i*xres + j] += q*sqrt(zc*zc + zs*zs);
                }
                if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito, pcancelled))
                    break;
            }
        }
    }
    else if (quantity == WAVE_QUANTITY_PHASE) {
        q = 1.0/GWY_SQRT_PI;
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,sources,tab,xres,yres,nwaves,q,set_fraction,pcancelled)
#endif
        {
            gint ifrom = gwy_omp_chunk_start(yres);
            gint ito = gwy_omp_chunk_end(yres);
            gint i, j, k;

            for (i = 0; i < yres; i++) {
                for (j = 0; j < xres; j++) {
                    const WaveSource *source = sources;
                    gfloat zc = 0.0, zs = 0.0;

                    for (k = nwaves; k; k--, source++) {
                        gdouble x = j - source->x, y = i - source->y;
                        gdouble kr = source->k * sqrt(x*x + y*y);
                        gdouble dec = exp(-kr*source->decay);
                        gfloat c, s;
                        approx_wave_sc(tab, kr, &s, &c);
                        zs += dec*source->z*s;
                        zc += dec*source->z*c;
                    }
                    d[i*xres + j] += q*atan2(zs, zc);
                }
                if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito, pcancelled))
                    break;
            }
        }
    }
    else {
        g_assert_not_reached();
    }

end:
    if (show_progress_bar)
        gwy_app_wait_finish();
    g_free(sources);

    return !cancelled;
}

static void
precalculate_wave_table(gfloat *tab, guint n, WaveTypeType type)
{
    guint i;

    if (type == WAVE_FORM_COSINE) {
        for (i = 0; i < n; i++) {
            gdouble x = (i + 0.5)/n * 2.0*G_PI;
            tab[i] = cos(x);
            tab[i + n] = sin(x);
        }
    }
    else if (type == WAVE_FORM_INVCOSH) {
        gdouble *dbltab = g_new(gdouble, 2*n);

        for (i = 0; i < n; i++) {
            gdouble x = (i + 0.5)/n * 10.0;
            dbltab[i] = 1.0/cosh(x) + 1.0/cosh(10.0 - x);
        }

        complement_table(dbltab, tab, n);
        g_free(dbltab);
    }
    else if (type == WAVE_FORM_FLATTOP) {
        for (i = 0; i < n; i++) {
            gdouble x = (i + 0.5)/n * 2.0*G_PI;
            tab[i] = cos(x) - cos(3*x)/6 + cos(5*x)/50;
            tab[i + n] = sin(x) - sin(3*x)/6 + sin(5*x)/50;
        }
    }
    else {
        g_return_if_reached();
    }
}

static void
complement_table(gdouble *dbltab, gfloat *tab, guint n)
{
    guint i;
    gdouble s = 0.0, s2 = 0.0;

    for (i = 0; i < n; i++)
        s += dbltab[i];
    s /= n;

    for (i = 0; i < n; i++) {
        dbltab[i] -= s;
        s2 += dbltab[i]*dbltab[i];
    }
    s2 = sqrt(s2/n);

    complement_wave(dbltab, dbltab + n, n);
    for (i = 0; i < 2*n; i++)
        tab[i] = dbltab[i]/s2;
}

static void
complement_wave(const gdouble *cwave, gdouble *swave, guint n)
{
    gdouble *buf1 = g_new(gdouble, 3*n);
    gdouble *buf2 = buf1 + n;
    gdouble *buf3 = buf2 + n;
    guint i;

    gwy_clear(swave, n);
    gwy_fft_simple(GWY_TRANSFORM_DIRECTION_FORWARD, n, 1, cwave, swave, 1, buf1, buf2);
    for (i = 0; i < n/2; i++) {
        gdouble t = buf2[i];
        buf2[i] = buf1[i];
        buf1[i] = t;
    }
    for (i = n/2; i < n; i++) {
        gdouble t = buf2[i];
        buf2[i] = -buf1[i];
        buf1[i] = t;
    }
    gwy_fft_simple(GWY_TRANSFORM_DIRECTION_BACKWARD, n, 1, buf1, buf2, 1, swave, buf3);
    g_free(buf1);
}

static void
randomize_sources(WaveSource *sources, ModuleArgs *args, guint xres, guint yres)
{
    GwyParams *params = args->params;
    guint nsources = gwy_params_get_int(params, PARAM_NWAVES);
    gdouble amplitude = gwy_params_get_double(params, PARAM_AMPLITUDE);
    gdouble amplitude_noise = gwy_params_get_double(params, PARAM_AMPLITUDE_NOISE);
    gdouble decay = gwy_params_get_double(params, PARAM_DECAY);
    gdouble decay_noise = gwy_params_get_double(params, PARAM_DECAY_NOISE);
    gdouble k = gwy_params_get_double(params, PARAM_K);
    gdouble k_noise = gwy_params_get_double(params, PARAM_K_NOISE);
    gdouble x = gwy_params_get_double(params, PARAM_X);
    gdouble x_noise = gwy_params_get_double(params, PARAM_X_NOISE);
    gdouble y = gwy_params_get_double(params, PARAM_Y);
    gdouble y_noise = gwy_params_get_double(params, PARAM_Y_NOISE);
    gint power10z;
    gdouble q = sqrt(xres*yres), r = 2.0*G_PI/q;
    gdouble xsigma = 1000.0*x_noise*x_noise, ysigma = 1000.0*y_noise*y_noise;
    GwyRandGenSet *rngset;
    guint i;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    amplitude *= pow10(power10z);

    rngset = gwy_rand_gen_set_new(1);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    for (i = 0; i < nsources; i++) {
        sources[i].x = q*(x + gwy_rand_gen_set_gaussian(rngset, 0, xsigma)) + 0.5*xres;
        sources[i].y = q*(y + gwy_rand_gen_set_gaussian(rngset, 0, ysigma)) + 0.5*yres;
        sources[i].k = r*k*exp(gwy_rand_gen_set_gaussian(rngset, 0, 4.0*k_noise));
        sources[i].z = (amplitude * exp(gwy_rand_gen_set_gaussian(rngset, 0, 4.0*amplitude_noise)));
        /* Phase is already scaled so this is measured in wavelengths as it should. */
        sources[i].decay = pow10(decay + gwy_rand_gen_set_gaussian(rngset, 0, 4.0*decay_noise));
    }

    gwy_rand_gen_set_free(rngset);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
