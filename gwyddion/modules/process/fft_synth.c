/*
 *  $Id: fft_synth.c 26003 2023-11-09 10:04:27Z yeti-dn $
 *  Copyright (C) 2007-2021 David Necas (Yeti).
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
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIGMA,
    PARAM_FREQ_MIN,
    PARAM_FREQ_MAX,
    PARAM_GAUSS_ENABLE,
    PARAM_GAUSS_TAU,
    PARAM_GAUSS_GENERALIZED,
    PARAM_GAUSS_P,
    PARAM_LORENTZ_ENABLE,
    PARAM_LORENTZ_TAU,
    PARAM_POWER_ENABLE,
    PARAM_POWER_P,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

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
static void             fft_synth           (GwyContainer *data,
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
    N_("Generates random surfaces using spectral synthesis."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, fft_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("fft_synth",
                              (GwyProcessFunc)&fft_synth,
                              N_("/S_ynthetic/_Noise/_Spectral..."),
                              GWY_STOCK_SYNTHETIC_SPECTRAL,
                              RUN_MODES,
                              0,
                              N_("Generate surface using spectral synthesis"));

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
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_RMS"), 1e-5, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_FREQ_MIN, "freq_min", _("M_inimum frequency"),
                             0.0, G_SQRT2*G_PI, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_FREQ_MAX, "freq_max", _("Ma_ximum frequency"),
                             0.0, G_SQRT2*G_PI, G_SQRT2*G_PI);
    gwy_param_def_add_boolean(paramdef, PARAM_GAUSS_ENABLE, "gauss_enable", _("Enable _Gaussian multiplier"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_GAUSS_TAU, "gauss_tau", _("Autocorrelation length"), 0.25, 1000.0, 10.0);
    gwy_param_def_add_boolean(paramdef, PARAM_GAUSS_GENERALIZED, "gauss_generalized", _("General power"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_GAUSS_P, "gauss_p", _("General power"), 0.1, 12.0, 2.0);
    gwy_param_def_add_boolean(paramdef, PARAM_LORENTZ_ENABLE, "lorentz_enable", _("Enable _Lorentz multiplier"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_LORENTZ_TAU, "lorentz_tau", _("Autocorrelation length"),
                             0.25, 1000.0, 10.0);
    gwy_param_def_add_boolean(paramdef, PARAM_POWER_ENABLE, "power_enable", _("Enable _power multiplier"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_POWER_P, "power_p", _("Po_wer"), 0.0, 5.0, 1.5);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
fft_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Spectral Synthesis"));
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

    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_set_mapping(table, PARAM_SIGMA, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_FREQ_MIN);
    gwy_param_table_set_unitstr(table, PARAM_FREQ_MIN, _("px<sup>-1</sup>"));
    gwy_param_table_slider_add_alt(table, PARAM_FREQ_MIN);
    gwy_param_table_append_slider(table, PARAM_FREQ_MAX);
    gwy_param_table_set_unitstr(table, PARAM_FREQ_MAX, _("px<sup>-1</sup>"));
    gwy_param_table_slider_add_alt(table, PARAM_FREQ_MAX);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_GAUSS_ENABLE);
    gwy_param_table_append_slider(table, PARAM_GAUSS_TAU);
    gwy_param_table_slider_set_mapping(table, PARAM_GAUSS_TAU, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_GAUSS_TAU);
    gwy_param_table_append_slider(table, PARAM_GAUSS_P);
    gwy_param_table_add_enabler(table, PARAM_GAUSS_GENERALIZED, PARAM_GAUSS_P);
    gwy_param_table_slider_set_mapping(table, PARAM_GAUSS_P, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_LORENTZ_ENABLE);
    gwy_param_table_append_slider(table, PARAM_LORENTZ_TAU);
    gwy_param_table_slider_set_mapping(table, PARAM_LORENTZ_TAU, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_LORENTZ_TAU);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_POWER_ENABLE);
    gwy_param_table_append_slider(table, PARAM_POWER_P);
    gwy_param_table_slider_set_mapping(table, PARAM_POWER_P, GWY_SCALE_MAPPING_LINEAR);

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
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
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
        static const gint xyids[] = { PARAM_GAUSS_TAU, PARAM_LORENTZ_TAU };
        gint power10x, xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
        GwySIUnit *xunit = gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT, &power10x);
        GwySIUnit *ixunit = gwy_si_unit_power(xunit, -1, NULL);
        gdouble dx = gwy_params_get_double(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL)*pow10(power10x)/xres;
        GwySIValueFormat *vf = gwy_si_unit_get_format_with_digits(ixunit, GWY_SI_UNIT_FORMAT_VFMARKUP, 1.0/dx, 4, NULL);

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
        gwy_param_table_alt_set_linear(table, PARAM_FREQ_MIN, 1.0/(dx*vf->magnitude), 0.0, vf->units);
        gwy_param_table_alt_set_linear(table, PARAM_FREQ_MAX, 1.0/(dx*vf->magnitude), 0.0, vf->units);
        g_object_unref(ixunit);
        gwy_si_unit_value_format_free(vf);
    }
    if (id < 0 || id == PARAM_GAUSS_ENABLE) {
        gboolean sens = gwy_params_get_boolean(params, PARAM_GAUSS_ENABLE);
        gwy_param_table_set_sensitive(table, PARAM_GAUSS_TAU, sens);
        gwy_param_table_set_sensitive(table, PARAM_GAUSS_P, sens);
    }
    if (id < 0 || id == PARAM_LORENTZ_ENABLE) {
        gboolean sens = gwy_params_get_boolean(params, PARAM_LORENTZ_ENABLE);
        gwy_param_table_set_sensitive(table, PARAM_LORENTZ_TAU, sens);
    }
    if (id < 0 || id == PARAM_POWER_ENABLE) {
        gboolean sens = gwy_params_get_boolean(params, PARAM_POWER_ENABLE);
        gwy_param_table_set_sensitive(table, PARAM_POWER_P, sens);
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
init_gauss_generalized(GwyDataField *finit_re, GwyDataField *buf_re, GwyDataField *buf_im,
                       gdouble tau, gdouble p)
{
    gint i, xres, yres;
    gdouble tau2 = tau*tau;
    gdouble *bre, *fre;

    xres = gwy_data_field_get_xres(finit_re);
    yres = gwy_data_field_get_yres(finit_re);
    bre = gwy_data_field_get_data(buf_re);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(bre,p,tau2,xres,yres) \
            private(i)
#endif
    for (i = 0; i < yres; i++) {
        gdouble y = (i <= yres/2 ? i : yres-i);
        gdouble *row = bre + i*xres;
        gint j;

        for (j = 0; j < xres; j++) {
            gdouble x = (j <= xres/2 ? j : xres-j);
            gdouble r = (x*x + y*y)/tau2;

            row[j] = exp(-pow(r, 0.5*p));
        }
    }

    gwy_data_field_2dfft_raw(buf_re, NULL, finit_re, buf_im, GWY_TRANSFORM_DIRECTION_BACKWARD);

    /* Calculate magnitude of Fourier coefficients from PSDF. */
    fre = gwy_data_field_get_data(finit_re);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(fre,xres,yres) \
            private(i)
#endif
    for (i = 0; i < xres*yres; i++)
        fre[i] = sqrt(fabs(fre[i]));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gboolean gauss_enable = gwy_params_get_boolean(params, PARAM_GAUSS_ENABLE);
    gboolean gauss_generalized = gwy_params_get_boolean(params, PARAM_GAUSS_GENERALIZED);
    gboolean lorentz_enable = gwy_params_get_boolean(params, PARAM_LORENTZ_ENABLE);
    gboolean power_enable = gwy_params_get_boolean(params, PARAM_POWER_ENABLE);
    gdouble freq_min = gwy_params_get_double(params, PARAM_FREQ_MIN)/G_PI;
    gdouble freq_max = gwy_params_get_double(params, PARAM_FREQ_MAX)/G_PI;
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble gauss_tau = gwy_params_get_double(params, PARAM_GAUSS_TAU)*G_PI/2.0;
    gdouble gauss_p = gwy_params_get_double(params, PARAM_GAUSS_P);
    gdouble lorentz_tau = gwy_params_get_double(params, PARAM_LORENTZ_TAU)*G_PI/2.0;
    gdouble power_p = gwy_params_get_double(params, PARAM_POWER_P);
    GwyDataField *in_re, *in_im, *out_im, *out_re = args->result;
    gdouble *re, *im, *finit;
    gint xres, yres, i, power10z;
    gdouble rms;
    GRand *rng;

    rng = g_rand_new();
    g_rand_set_seed(rng, gwy_params_get_int(params, PARAM_SEED));

    xres = gwy_data_field_get_xres(out_re);
    yres = gwy_data_field_get_yres(out_re);
    out_im = gwy_data_field_new_alike(out_re, FALSE);
    in_re = gwy_data_field_new_alike(out_re, FALSE);
    in_im = gwy_data_field_new_alike(out_re, FALSE);

    if (gauss_generalized && gauss_enable) {
        init_gauss_generalized(out_re, in_re, in_im, gauss_tau, gauss_p);
        /* Disable normal Gaussian when we use generalised. */
        gauss_enable = FALSE;
    }
    else
        gwy_data_field_fill(out_re, 1.0);

    re = gwy_data_field_get_data(in_re);
    im = gwy_data_field_get_data(in_im);
    finit = gwy_data_field_get_data(out_re);

    /* Doing this always exactly the same way is necessary for reproducibility and stability.  */
    for (i = 0; i < xres*yres; i++) {
        re[i] = g_rand_double(rng);
        im[i] = g_rand_double(rng);
    }

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(re,im,finit,xres,yres,freq_min,freq_max,power_enable,gauss_enable,lorentz_enable,power_p,gauss_tau,lorentz_tau) \
            private(i)
#endif
    for (i = 0; i < yres; i++) {
        gdouble y = (i <= yres/2 ? i : yres-i)/(0.5*yres);
        gdouble *rrow = re + i*xres, *irow = im + i*xres;
        const gdouble *frow = finit + i*xres;
        gint j;

        for (j = 0; j < xres; j++) {
            gdouble x = (j <= xres/2 ? j : xres-j)/(0.5*xres);
            gdouble r = sqrt(x*x + y*y);
            gdouble f, phi, s, c, t;

            if (r < freq_min || r > freq_max) {
                rrow[j] = irow[j] = 0.0;
                continue;
            }

            f = rrow[j]*frow[j];
            phi = 2.0*G_PI*irow[j];

            /* Note we construct Fourier coefficients, not PSDF.  So things may appear square-rooted. */
            if (power_enable)
                f /= pow(r, power_p);
            if (gauss_enable) {
                t = r*gauss_tau;
                f /= exp(0.5*t*t);
            }
            /* This is actually something that gives exponential ACF. */
            if (lorentz_enable) {
                t = r*lorentz_tau;
                t = 1.0 + t*t;
                f /= sqrt(sqrt(t*t*t));
            }

            _gwy_sincos(phi, &s, &c);
            rrow[j] = f*s;
            irow[j] = f*c;
        }
    }
    re[0] = im[0] = 0.0;

    gwy_data_field_2dfft_raw(in_re, in_im, out_re, out_im, GWY_TRANSFORM_DIRECTION_BACKWARD);

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    sigma *= pow10(power10z);
    rms = gwy_data_field_get_rms(out_re);
    if (rms)
        gwy_data_field_multiply(out_re, sigma/rms);

    if (args->field && do_initialise)
        gwy_data_field_sum_fields(out_re, out_re, args->field);

    g_rand_free(rng);
    g_object_unref(in_im);
    g_object_unref(in_re);
    g_object_unref(out_im);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
