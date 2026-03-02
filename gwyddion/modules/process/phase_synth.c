/*
 *  $Id: phase_synth.c 26003 2023-11-09 10:04:27Z yeti-dn $
 *  Copyright (C) 2017-2021 David Necas (Yeti).
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
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/inttrans.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIZE,
    PARAM_SIZE_NOISE,
    PARAM_HEIGHT,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef enum {
    RNG_AMPLITUDE = 0,
    RNG_PHASE     = 1,
    RNG_NRGNS
} PhaseSynthRng;

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
    GwyParamTable *table_generator;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             phase_synth         (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GtkWidget*       dimensions_tab_new  (ModuleGUI *gui);
static GtkWidget*       generator_tab_new   (ModuleGUI *gui);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates phase-separated structures."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, phase_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("phase_synth",
                              (GwyProcessFunc)&phase_synth,
                              N_("/S_ynthetic/_Order and Disorder/P_hases..."),
                              GWY_STOCK_SYNTHETIC_PHASES,
                              RUN_MODES,
                              0,
                              N_("Generate surface with separated phases"));

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
    gwy_param_def_add_double(paramdef, PARAM_SIZE, "size", _("Si_ze"), 1.0, 400.0, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_SIZE_NOISE, "size_noise", _("_Spread"), 1e-3, 0.5, 0.05);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
phase_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Separated Phases"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
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

    gwy_param_table_append_header(table, -1, _("Generator"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_append_slider(table, PARAM_SIZE_NOISE);
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
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

    return gwy_param_table_widget(table);
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
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_SIZE };

        gwy_synth_update_lateral_alts(gui->table_generator, xyids, G_N_ELEMENTS(xyids));
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

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
}

#ifdef HAVE_SINCOS
#define _gwy_sincos sincos
#else
static inline void
_gwy_sincos(gdouble x, gdouble *s, gdouble *c)
{
    *s = sin(x);
    *c = cos(x);
}
#endif

static void
generate_narrow_freq_surface(gdouble freq, gdouble freq_range,
                             GwyDataField *buf_re, GwyDataField *buf_im,
                             GwyDataField *out_re, GwyDataField *out_im,
                             GwyRandGenSet *rngset, gboolean random_phase)
{
    GRand *rng_f = gwy_rand_gen_set_rng(rngset, 0), *rng_phi = gwy_rand_gen_set_rng(rngset, 1);
    gdouble *re, *im, *ore, *oim;
    gint xres, yres, i, j;

    re = gwy_data_field_get_data(buf_re);
    im = gwy_data_field_get_data(buf_im);

    ore = gwy_data_field_get_data(out_re);
    oim = gwy_data_field_get_data(out_im);
    xres = gwy_data_field_get_xres(out_re);
    yres = gwy_data_field_get_yres(out_re);

    freq /= G_PI;
    freq_range /= G_PI;

    for (i = 0; i < yres; i++) {
        gdouble y = (i <= yres/2 ? i : yres-i)/(0.5*yres);
        for (j = 0; j < xres; j++) {
            gdouble x = (j <= xres/2 ? j : xres-j)/(0.5*xres);
            gdouble r = sqrt(x*x + y*y);
            gdouble f, phi, s, c, h;
            gint k = i*xres + j;

            /* Always consume the random numbers for stability. */
            f = g_rand_double(rng_f);
            if (random_phase)
                phi = 2.0*G_PI*g_rand_double(rng_phi);
            r = fabs((r - freq)/freq_range);
            if (r > 30.0) {
                re[k] = im[k] = 0.0;
                continue;
            }

            if (random_phase)
                _gwy_sincos(phi, &s, &c);
            else {
                /* Use phase of out_re and out_im, which should be the FFT of the original image. */
                h = fmax(sqrt(ore[k]*ore[k] + oim[k]*oim[k]), G_MINDOUBLE);
                c = ore[k]/h;
                s = oim[k]/h;
            }
            r = exp(r);
            f /= r + 1.0/r;
            re[k] = f*c;
            im[k] = f*s;
        }
    }
    re[0] = im[0] = 0.0;

    gwy_data_field_2dfft_raw(buf_re, buf_im, out_re, out_im, GWY_TRANSFORM_DIRECTION_BACKWARD);
}

static void
threshold_based_on_distance(GwyDataField *field, GwyDataField *buf1, GwyDataField *buf2, GwyDataField *result)
{
    gdouble thresh;

    thresh = gwy_data_field_otsu_threshold(field);

    gwy_data_field_copy(field, buf1, FALSE);
    gwy_data_field_threshold(buf1, thresh, 0.0, 1.0);
    gwy_data_field_grains_invert(buf1);
    gwy_data_field_grains_thin(buf1);
    gwy_data_field_mark_extrema(field, result, FALSE);
    gwy_data_field_max_of_fields(buf1, result, buf1);
    gwy_data_field_grains_invert(buf1);
    gwy_data_field_grain_distance_transform(buf1);

    gwy_data_field_copy(field, buf2, FALSE);
    gwy_data_field_threshold(buf2, thresh, 0.0, 1.0);
    gwy_data_field_grains_thin(buf2);
    gwy_data_field_mark_extrema(field, result, TRUE);
    gwy_data_field_max_of_fields(buf2, result, buf2);
    gwy_data_field_grains_invert(buf2);
    gwy_data_field_grain_distance_transform(buf2);

    gwy_data_field_subtract_fields(result, buf1, buf2);
    gwy_data_field_threshold(result, 0.0, 0.0, 1.0);
}

static void
regularise_with_asf(GwyDataField *field, GwyDataField *buf, GwyDataField *kernel, guint maxksize)
{
    guint i, res, xres = field->xres, yres = field->yres;

    gwy_data_field_copy(field, buf, FALSE);
    for (i = 1; i <= maxksize; i++) {
        res = 2*i + 1;
        gwy_data_field_resample(kernel, res, res, GWY_INTERPOLATION_NONE);
        gwy_data_field_clear(kernel);
        gwy_data_field_elliptic_area_fill(kernel, 0, 0, res, res, 1.0);
        gwy_data_field_area_filter_min_max(field, kernel, GWY_MIN_MAX_FILTER_OPENING, 0, 0, xres, yres);
        gwy_data_field_area_filter_min_max(field, kernel, GWY_MIN_MAX_FILTER_CLOSING, 0, 0, xres, yres);
        gwy_data_field_area_filter_min_max(buf, kernel, GWY_MIN_MAX_FILTER_CLOSING, 0, 0, xres, yres);
        gwy_data_field_area_filter_min_max(buf, kernel, GWY_MIN_MAX_FILTER_OPENING, 0, 0, xres, yres);
    }
    gwy_data_field_linear_combination(field, 0.5, buf, 0.5, field, 0.0);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    gdouble size_noise = gwy_params_get_double(params, PARAM_SIZE_NOISE);
    gdouble freq_range = size_noise, freq = G_PI/size;
    GwyDataField *field = args->field, *result = args->result;
    GwyDataField *buf1, *buf2, *buf3, *kernel, *tmp;
    guint xres, yres, kxres, kyres, extsize, extxres, extyres, asfradius;
    gint power10z;
    GwyRandGenSet *rngset;

    rngset = gwy_rand_gen_set_new(RNG_NRGNS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    /* spread is relative */
    freq_range *= freq;
    /* rough mean frequency correction */
    freq /= 1.0 + pow(freq_range/freq, 2)/3.0;

    kxres = GWY_ROUND(2.0*G_PI/freq * 1.2) | 1;
    kyres = GWY_ROUND(2.0*G_PI/freq * 1.2) | 1;
    extsize = GWY_ROUND(G_PI/freq);
    asfradius = GWY_ROUND(0.08*2.0*G_PI/freq);
    gwy_debug("kernel %ux%u, extsize %u, asf %u", kxres, kyres, extsize, asfradius);

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);

    buf1 = gwy_data_field_new_alike(result, FALSE);
    buf2 = gwy_data_field_new_alike(result, FALSE);
    buf3 = gwy_data_field_new_alike(result, FALSE);

    if (field && do_initialise)
        gwy_data_field_2dfft_raw(field, NULL, result, buf3, GWY_TRANSFORM_DIRECTION_FORWARD);

    generate_narrow_freq_surface(freq, freq_range, buf1, buf2, result, buf3, rngset, !do_initialise);
    tmp = gwy_data_field_extend(result, extsize, extsize, extsize, extsize, GWY_EXTERIOR_PERIODIC, 0.0, FALSE);
    extxres = gwy_data_field_get_xres(tmp);
    extyres = gwy_data_field_get_yres(tmp);
    gwy_data_field_resample(buf1, extxres, extyres, GWY_INTERPOLATION_NONE);
    gwy_data_field_resample(buf2, extxres, extyres, GWY_INTERPOLATION_NONE);
    gwy_data_field_resample(buf3, extxres, extyres, GWY_INTERPOLATION_NONE);

    kernel = gwy_data_field_new(kxres, kyres, 1.0, 1.0, TRUE);
    gwy_data_field_elliptic_area_fill(kernel, 0, 0, kxres, kyres, 1.0);
    gwy_data_field_area_filter_min_max(result, kernel, GWY_MIN_MAX_FILTER_NORMALIZATION, 0, 0, xres, yres);
    gwy_data_field_copy(tmp, buf3, FALSE);
    threshold_based_on_distance(buf3, buf1, buf2, tmp);
    regularise_with_asf(tmp, buf1, kernel, asfradius);

    gwy_data_field_area_copy(tmp, result, extsize, extsize, xres, yres, 0, 0);
    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    gwy_data_field_multiply(result, height*pow10(power10z));

    g_object_unref(kernel);
    g_object_unref(tmp);
    g_object_unref(buf1);
    g_object_unref(buf2);
    g_object_unref(buf3);

    gwy_rand_gen_set_free(rngset);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
