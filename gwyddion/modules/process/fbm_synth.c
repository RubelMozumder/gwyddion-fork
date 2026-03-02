/*
 *  $Id: fbm_synth.c 26003 2023-11-09 10:04:27Z yeti-dn $
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
#include <libprocess/arithmetic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    NOISE_DISTRIBUTION_GAUSSIAN    = 0,
    NOISE_DISTRIBUTION_EXPONENTIAL = 1,
    NOISE_DISTRIBUTION_UNIFORM     = 2,
    NOISE_DISTRIBUTION_POWER       = 3,
} NoiseDistributionType;

enum {
    PARAM_H,
    PARAM_HOM_SCALE,
    PARAM_DISTRIBUTION,
    PARAM_POWER,
    PARAM_SIGMA,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef struct {
    GwyDataField *field;
    gdouble *H_powers;
    gboolean *visited;
    gdouble hom_sigma;
    gdouble power;
    guint xres;
    guint yres;
    guint hom_scale;
    NoiseDistributionType distribution;
    GwyRandGenSet *rngset;
} FBMSynthState;

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
static void             fbm_synth           (GwyContainer *data,
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
    N_("Generates random surfaces similar to fractional Brownian motion."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, fbm_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("fbm_synth",
                              (GwyProcessFunc)&fbm_synth,
                              N_("/S_ynthetic/_Noise/_Brownian..."),
                              GWY_STOCK_SYNTHETIC_BROWNIAN_MOTION,
                              RUN_MODES,
                              0,
                              N_("Generate fractional Brownian motion-like surface"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum generators[] = {
        { N_("distribution|Uniform"),     NOISE_DISTRIBUTION_UNIFORM,     },
        { N_("distribution|Gaussian"),    NOISE_DISTRIBUTION_GAUSSIAN,    },
        { N_("distribution|Exponential"), NOISE_DISTRIBUTION_EXPONENTIAL, },
        { N_("distribution|Power"),       NOISE_DISTRIBUTION_POWER,       },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_H, "H", _("_Hurst exponent"), -0.999, 0.999, 0.5);
    gwy_param_def_add_int(paramdef, PARAM_HOM_SCALE, "hom_scale", _("_Stationarity scale"), 2, 16384, 16384);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISTRIBUTION, "distribution", _("_Distribution"),
                              generators, G_N_ELEMENTS(generators), NOISE_DISTRIBUTION_GAUSSIAN);
    gwy_param_def_add_double(paramdef, PARAM_POWER, "power", _("Po_wer"), 2.01, 12.0, 3.0);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_RMS"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
fbm_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Fractional Brownian Motion"));
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

    gwy_param_table_append_slider(table, PARAM_H);
    gwy_param_table_slider_set_mapping(table, PARAM_H, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_HOM_SCALE);
    gwy_param_table_slider_add_alt(table, PARAM_HOM_SCALE);
    gwy_param_table_slider_set_mapping(table, PARAM_HOM_SCALE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_combo(table, PARAM_DISTRIBUTION);
    gwy_param_table_append_slider(table, PARAM_POWER);
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

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_SIGMA };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_HOM_SCALE };
        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0 || id == PARAM_DISTRIBUTION) {
        NoiseDistributionType distribution = gwy_params_get_enum(gui->args->params, PARAM_DISTRIBUTION);
        gwy_param_table_set_sensitive(table, PARAM_POWER, distribution == NOISE_DISTRIBUTION_POWER);
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
            gwy_param_table_set_double(gui->table_generator, PARAM_SIGMA, zscale/pow10(power10z));
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

static FBMSynthState*
fbm_synth_state_new(ModuleArgs *args)
{
    GwyParams *params = args->params;
    NoiseDistributionType distribution = gwy_params_get_enum(params, PARAM_DISTRIBUTION);
    guint hom_scale = gwy_params_get_int(params, PARAM_HOM_SCALE);
    gdouble H = gwy_params_get_double(params, PARAM_H);
    gdouble power = gwy_params_get_double(params, PARAM_POWER);
    guint xres = gwy_data_field_get_xres(args->result), yres = gwy_data_field_get_yres(args->result);
    FBMSynthState *fbm = g_new0(FBMSynthState, 1);
    guint npowers, i;

    fbm->field = args->result;
    fbm->xres = xres;
    fbm->yres = yres;
    fbm->hom_scale = hom_scale;
    fbm->distribution = distribution;
    fbm->power = power;
    fbm->visited = g_new0(gboolean, xres*yres);
    fbm->rngset = gwy_rand_gen_set_new(1);
    gwy_rand_gen_set_init(fbm->rngset, gwy_params_get_int(params, PARAM_SEED));

    npowers = MAX(xres, yres) + 1;
    fbm->H_powers = g_new(gdouble, npowers);
    fbm->H_powers[0] = 0.0;
    for (i = 1; i < npowers; i++)
        fbm->H_powers[i] = pow(i, H);

    fbm->hom_sigma = pow(hom_scale, H);

    return fbm;
}

static void
fbm_synth_state_free(FBMSynthState *fbm)
{
    g_free(fbm->H_powers);
    g_free(fbm->visited);
    gwy_rand_gen_set_free(fbm->rngset);
    g_free(fbm);
}

static void
initialise(FBMSynthState *fbm)
{
    GwyRandGenSet *rngset = fbm->rngset;
    guint xres = fbm->xres, yres = fbm->yres;
    gdouble *data = gwy_data_field_get_data(fbm->field);
    gboolean *visited = fbm->visited;
    gdouble sigma = fbm->hom_sigma;

    data[0] = gwy_rand_gen_set_uniform(rngset, 0, sigma);
    data[xres-1] = gwy_rand_gen_set_uniform(rngset, 0, sigma);
    data[xres*(yres-1)] = gwy_rand_gen_set_uniform(rngset, 0, sigma);
    data[xres*yres - 1] = gwy_rand_gen_set_uniform(rngset, 0, sigma);
    visited[0] = TRUE;
    visited[xres-1] = TRUE;
    visited[xres*(yres-1)] = TRUE;
    visited[xres*yres - 1] = TRUE;
}

static gdouble
generate_midvalue(gdouble a, guint da, gdouble b, guint db, FBMSynthState *fbm)
{
    GwyRandGenSet *rngset = fbm->rngset;
    guint dtot = da + db;

    if (dtot >= (guint)fbm->hom_scale)
        return gwy_rand_gen_set_uniform(rngset, 0, fbm->hom_sigma);
    else {
        const gdouble *H_powers = fbm->H_powers;
        gdouble daH = H_powers[da], dbH = H_powers[db], dtotH = H_powers[dtot];
        gdouble da2 = da*da, db2 = db*db, dtot2 = dtot*dtot;
        gdouble mid = (a*db + b*da)/dtot;
        gdouble sigma2 = 0.5*(daH*daH + dbH*dbH - dtotH*dtotH*(da2 + db2)/dtot2);
        gdouble sigma = sqrt(sigma2);

        if (fbm->distribution == NOISE_DISTRIBUTION_UNIFORM)
            return mid + gwy_rand_gen_set_uniform(rngset, 0, sigma);
        if (fbm->distribution == NOISE_DISTRIBUTION_GAUSSIAN)
            return mid + gwy_rand_gen_set_gaussian(rngset, 0, sigma);
        if (fbm->distribution == NOISE_DISTRIBUTION_EXPONENTIAL)
            return mid + gwy_rand_gen_set_exponential(rngset, 0, sigma);
        if (fbm->distribution == NOISE_DISTRIBUTION_POWER) {
            GRand *rng = gwy_rand_gen_set_rng(rngset, 0);
            gdouble r = 1.0/pow(g_rand_double(rng), 1.0/fbm->power) - 1.0;
            if (g_rand_boolean(rng))
                return mid + sigma*r;
            else
                return mid - sigma*r;
        }
        g_return_val_if_reached(0.0);
    }
}

static void
recurse(FBMSynthState *fbm, guint xlow, guint ylow, guint xhigh, guint yhigh, guint depth)
{
    gdouble *data = fbm->field->data;
    gboolean *visited = fbm->visited;
    guint xres = fbm->xres;

    if (xhigh - xlow + (depth % 2) > yhigh - ylow) {
        guint xc = (xlow + xhigh)/2;
        guint k = ylow*xres + xc;

        if (!visited[k]) {
            data[k] = generate_midvalue(data[ylow*xres + xlow], xc - xlow, data[ylow*xres + xhigh], xhigh - xc, fbm);
            visited[k] = TRUE;
        }

        k = yhigh*xres + xc;
        data[k] = generate_midvalue(data[yhigh*xres + xlow], xc - xlow, data[yhigh*xres + xhigh], xhigh - xc, fbm);
        visited[k] = TRUE;

        if (yhigh - ylow > 1 || xc - xlow > 1)
            recurse(fbm, xlow, ylow, xc, yhigh, depth+1);
        if (yhigh - ylow > 1 || xhigh - xc > 1)
            recurse(fbm, xc, ylow, xhigh, yhigh, depth+1);
    }
    else {
        guint yc = (ylow + yhigh)/2;
        guint k = yc*xres + xlow;

        if (!visited[k]) {
            data[k] = generate_midvalue(data[ylow*xres + xlow], yc - ylow, data[yhigh*xres + xlow], yhigh - yc, fbm);
            visited[k] = TRUE;
        }

        k = yc*xres + xhigh;
        data[k] = generate_midvalue(data[ylow*xres + xhigh], yc - ylow, data[yhigh*xres + xhigh], yhigh - yc, fbm);
        visited[k] = TRUE;

        if (xhigh - xlow > 1 || yc - ylow > 1)
            recurse(fbm, xlow, ylow, xhigh, yc, depth+1);
        if (xhigh - xlow > 1 || yhigh - yc > 1)
            recurse(fbm, xlow, yc, xhigh, yhigh, depth+1);
    }
}

static void
execute(ModuleArgs *args)
{
    gboolean do_initialise = gwy_params_get_boolean(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble sigma = gwy_params_get_double(args->params, PARAM_SIGMA);
    GwyDataField *result = args->result;
    FBMSynthState *fbm;
    gdouble rms;
    gint power10z;

    gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    sigma *= pow10(power10z);

    gwy_data_field_clear(result);
    fbm = fbm_synth_state_new(args);
    initialise(fbm);
    recurse(fbm, 0, 0, fbm->xres-1, fbm->yres-1, 0);
    fbm_synth_state_free(fbm);

    rms = gwy_data_field_get_rms(result);
    if (rms)
        gwy_data_field_multiply(result, sigma/rms);

    if (args->field && do_initialise)
        gwy_data_field_sum_fields(result, result, args->field);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
