/*
 *  $Id: deposit_synth.c 24450 2021-11-01 14:32:53Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwyrandgenset.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

// 1. store link to original data
// 2. create result field
// 3. put copy of original or blank field to result
// 3. have function to do preview of result
// 4. run simulation with result field
// 5. repeat (3)

// N. have function to insert result to data browser or swap it for present channel
// N+1. run noninteractive or interactive with function N at end

enum {
    MAXN = 10000,
};

enum {
    PAGE_DIMENSIONS = 0,
    PAGE_GENERATOR = 1,
};

enum {
    RES_TOO_FEW   = -1,
    RES_TOO_MANY  = -2,
    RES_TOO_SMALL = -3,
    RES_TOO_LARGE = -4
};

enum {
    PARAM_COVERAGE,
    PARAM_REVISE,
    PARAM_SIZE,
    PARAM_SIZE_NOISE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,
    INFO_COVERAGE_OBJECTS,
    INFO_OBJECTS,

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
    GtkWidget *dataview;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             deposit_synth       (GwyContainer *data,
                                             GwyRunType run);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window,
                                             gint *pndeposited);
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
static gint             calculate_n_objects (ModuleArgs *args);
static const gchar*     particle_error      (gint code);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates particles using simple dynamical model"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "Petr Klapetek",
    "2010",
};

GWY_MODULE_QUERY2(module_info, deposit_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("deposit_synth",
                              (GwyProcessFunc)&deposit_synth,
                              N_("/S_ynthetic/_Deposition/_Particles..."),
                              GWY_STOCK_SYNTHETIC_PARTICLES,
                              RUN_MODES,
                              0,
                              N_("Generate particles using dynamical model"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_percentage(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 0.1);
    gwy_param_def_add_int(paramdef, PARAM_REVISE, "revise", _("_Relax steps"), 0, 100000, 500);
    gwy_param_def_add_double(paramdef, PARAM_SIZE, "size", _("Particle r_adius"), 1.0, 1000.0, 50.0);
    gwy_param_def_add_double(paramdef, PARAM_SIZE_NOISE, "width", _("Distribution _width"), 0.0, 100.0, 0.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
deposit_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GtkWidget *dialog;
    gint id, ndeposited;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.zscale = args.field ? gwy_data_field_get_rms(args.field) : -1.0;

    args.params = gwy_params_new_from_settings(define_module_params());
    gwy_synth_sanitise_params(args.params, PARAM_DIMS0, args.field);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome == GWY_DIALOG_PROCEED) {
        GWY_OBJECT_UNREF(args.result);
        args.result = gwy_synth_make_result_data_field(args.field, args.params, FALSE);
        if (gwy_params_get_boolean(args.params, PARAM_ANIMATED))
            gwy_app_wait_preview_data_field(args.result, data, id);
        if (!execute(&args, gwy_app_find_window_for_channel(data, id), &ndeposited)) {
            if (gwy_app_data_browser_get_gui_enabled() || gwy_app_wait_get_enabled()) {
                dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                                GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                                "%s", particle_error(ndeposited));
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            }
            goto end;
        }
    }
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
    GtkWidget *hbox;
    GtkNotebook *notebook;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.template_ = args->field;

    args->result = gwy_synth_make_result_data_field(args->field, args->params, TRUE);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    if (gui.template_)
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Particle Generation"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static GtkWidget*
dimensions_tab_new(ModuleGUI *gui)
{
    gui->table_dimensions = gwy_param_table_new(gui->args->params);
    gwy_synth_append_dimensions_to_param_table(gui->table_dimensions, GWY_SYNTH_FIXED_ZUNIT);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_dimensions);

    return gwy_param_table_widget(gui->table_dimensions);
}

static GtkWidget*
generator_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_generator = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_header(table, -1, _("Particle Generation"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_SIZE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_SIZE_NOISE);
    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_append_info(table, INFO_COVERAGE_OBJECTS, _("Number of objects"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_REVISE);
    gwy_param_table_slider_set_mapping(table, PARAM_SIZE, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_message(table, INFO_OBJECTS, NULL);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_ANIMATED);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_SIZE };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0
        || id == PARAM_COVERAGE
        || id == PARAM_SIZE
        || id == PARAM_SIZE_NOISE
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_YRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_YREAL) {
        gint nparticles = calculate_n_objects(gui->args);
        gchar *s;

        if (nparticles > 0) {
            s = g_strdup_printf("%d", nparticles);
            gwy_param_table_info_set_valuestr(gui->table_generator, INFO_COVERAGE_OBJECTS, s);
            g_free(s);
            gwy_param_table_set_label(gui->table_generator, INFO_OBJECTS, " ");
            gwy_param_table_message_set_type(gui->table_generator, INFO_OBJECTS, GTK_MESSAGE_INFO);
        }
        else {
            gwy_param_table_info_set_valuestr(gui->table_generator, INFO_COVERAGE_OBJECTS, "0");
            gwy_param_table_set_label(gui->table_generator, INFO_OBJECTS, particle_error(nparticles));
            gwy_param_table_message_set_type(gui->table_generator, INFO_OBJECTS, GTK_MESSAGE_WARNING);
        }
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_ANIMATED && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS)
        gwy_synth_use_dimensions_template(gui->table_dimensions);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *tmp;
    gint ndeposited, nparticles;
    gchar *message;

    /* Update preview size for the full result data field.  Do it immediately because the preview can be animated. */
    tmp = gwy_synth_make_result_data_field(args->field, args->params, TRUE);
    gwy_data_field_assign(args->result, tmp);
    g_object_unref(tmp);
    gwy_data_field_data_changed(args->result);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);

    nparticles = calculate_n_objects(args);
    if (execute(gui->args, GTK_WINDOW(gui->dialog), &ndeposited)) {
        gwy_data_field_data_changed(args->result);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
        if (ndeposited < nparticles) {
            message = g_strdup_printf(_("Only %d particles were deposited. Try more revise steps."), ndeposited);
            gwy_param_table_set_label(gui->table_generator, INFO_OBJECTS, message);
            g_free(message);
        }
        else
            gwy_param_table_set_label(gui->table_generator, INFO_OBJECTS, " ");
        gwy_param_table_message_set_type(gui->table_generator, INFO_OBJECTS, GTK_MESSAGE_INFO);
    }
    else {
        gwy_param_table_set_label(gui->table_generator, INFO_OBJECTS, particle_error(ndeposited));
        gwy_param_table_message_set_type(gui->table_generator, INFO_OBJECTS, GTK_MESSAGE_WARNING);
    }
}

static void
showit(GwyDataField *result, gdouble *rdisizes, const GwyXYZ *r, gint ndeposited, gint add)
{
    gdouble *data;
    gint xres, yres, i, m, n, xi, yi, k;
    gdouble sum, xreal, yreal;
    gint disize;

    data = gwy_data_field_get_data(result);
    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    xreal = gwy_data_field_get_xreal(result);
    yreal = gwy_data_field_get_yreal(result);
    for (i = 0; i < ndeposited; i++) {
        /* Since r[] are positions in the extended data field, we must consider origin to be (add, add) when rendering
         * the particles and hence subtract it from m and n. */
        xi = xres*(r[i].x/xreal) - add;
        yi = yres*(r[i].y/yreal) - add;

        /* Skip particles too high above the sruface (why exactly?). */
        if (r[i].z > data[CLAMP(yi, 0, yres-1)*xres + CLAMP(xi, 0, xres-1)] + 6*rdisizes[i])
            continue;

        disize = (gint)(xres*rdisizes[i]/xreal);
        for (m = xi-disize; m < xi+disize; m++) {
            if (m < 0 || m >= xres)
                continue;
            for (n = yi-disize; n < yi+disize; n++) {
                if (n < 0 || n >= yres)
                    continue;

                if ((sum = (disize*disize - (xi-m)*(xi-m) - (yi-n)*(yi-n))) > 0) {
                    k = n*xres + m;
                    data[k] = fmax(data[k], r[i].z + sqrt(sum)*xreal/xres);
                }
            }
        }
    }
}

/* Gradient of Lennard–Jones potential between two particles*/
static inline void
lj_potential_grad_spheres(gdouble ax, gdouble ay, gdouble az, gdouble bx, gdouble by, gdouble bz,
                          gdouble asize, gdouble bsize,
                          GwyXYZ *f)
{
    gdouble sigma = 0.82*(asize + bsize);
    gdouble dx = ax - bx, dy = ay - by, dz = az - bz;
    gdouble dist2 = dx*dx + dy*dy + dz*dz;
    gdouble s2 = sigma*sigma, s4, s6, s12, d4, d8, d14, c;

    if (asize <= 0.0 || bsize <= 0.0 || dist2 <= 0.1*s2)
        return;

    s4 = s2*s2;
    s6 = s4*s2;
    s12 = s6*s6;
    d4 = dist2*dist2;
    d8 = d4*d4;
    d14 = d8*d4*dist2;
    /* Gradient of Lennard–Jones potential corrected for particle size (σ⁶/d⁶ - σ¹²/d¹²). */
    c = asize*2e-5 * 6*(s6/d8 - 2*s12/d14);
    f->x += dx*c;
    f->y += dy*c;
    f->z += dz*c;
}

/* Integrate over some volume around particle (ax, ay, az), if there is substrate, add this to potential. */
static inline gdouble
integrate_lj_substrate(const gdouble *zldata, gint xres, gint yres, gdouble dx, gdouble dy,
                       gdouble ax, gdouble ay, gdouble az, gdouble size)
{
    /*make l-j only from idealistic substrate now*/
    gdouble zval, sigma, dist;
    gint i, j;

    sigma = 1.2*size; //empiric
    j = CLAMP((gint)(ax/dx), 0, xres-1);
    i = CLAMP((gint)(ay/dy), 0, yres-1);
    zval = zldata[i*xres + j];
    dist = fabs(az - zval);
    dist = MAX(dist, size/100);

    if (size > 0.0) {
        gdouble s2 = sigma*sigma, s4 = s2*s2, s6 = s4*s2, s12 = s6*s6;
        gdouble d3 = dist*dist*dist, d9 = d3*d3*d3;
        return size*2.0e-3*(s12/d9/45.0 - s6/d3/6.0); //corrected for particle size
    }
    return 0;
}

static gboolean
try_to_add_particle(GwyXYZ *r, gdouble *rdisizes, gint ndeposited,
                    const gdouble *zldata, gint xres, gint yres,
                    gdouble dx, gdouble dy,
                    gdouble size, gdouble size_noise,
                    GRand *rng_pos, GwyRandGenSet *rngset)
{
    GwyXYZ rnew;
    gdouble disize;
    gint xpos, ypos, k;

    size += gwy_rand_gen_set_gaussian(rngset, 0, size_noise);
    size = fmax(size, size/100.0);
    disize = size/dx;

    xpos = g_rand_int_range(rng_pos, (gint)disize, xres - 2*(gint)disize);
    ypos = g_rand_int_range(rng_pos, (gint)disize, yres - 2*(gint)disize);
    xpos = CLAMP(xpos, 0, xres-1);
    ypos = CLAMP(ypos, 0, yres-1);

    /* XXX: So here we obviously interpret positions r[] in the extended data field. */
    rnew.x = xpos*dx;
    rnew.y = ypos*dy;
    rnew.z = zldata[ypos*xres + xpos] + size;

    for (k = 0; k < ndeposited; k++) {
        gdouble dxk = rnew.x - r[k].x, dyk = rnew.y - r[k].y, dzk = rnew.z - r[k].z;
        if (dxk*dxk + dyk*dyk + dzk*dzk < 4.0*size*size)
            return FALSE;
    }

    rdisizes[ndeposited] = size;
    r[ndeposited].x = rnew.x;
    r[ndeposited].y = rnew.y;
    r[ndeposited].z = rnew.z;
    return TRUE;
}

static inline void
update_x_v_a(gdouble *x, gdouble *v, gdouble *a, gdouble f, gdouble dt, gdouble m)
{
    *x += (*v)*dt + 0.5*(*a)*dt*dt;
    *v += 0.5*(*a)*dt;
    *a = f/m;
    *v += 0.5*(*a)*dt;
    *v *= 0.9;
    if (fabs(*v) > 0.01)
        *v = 0.0;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window, gint *pndeposited)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    gdouble size_noise = gwy_params_get_double(params, PARAM_SIZE_NOISE);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    gint revise = gwy_params_get_int(params, PARAM_REVISE);
    guint32 seed = gwy_params_get_int(params, PARAM_SEED);
    GwyDataField *field = args->field, *result = args->result;
    GwyDataField *extfield;
    GwyRandGenSet *rngset;
    GRand *rng;
    gint xres, yres, oxres, oyres, add;
    gint nparticles, ndeposited, steps;
    gdouble xreal, yreal, diff, dx, dy;
    gdouble *rdisizes = NULL, *extdata;
    GwyXYZ *r = NULL, *v = NULL, *a = NULL, *f = NULL;
    gint i, k, nloc, maxloc = 1, maxsteps = 10000;
    gdouble preview_time = (animated ? 1.25 : 0.0);
    gdouble norm;
    GTimer *timer;
    gboolean finished = FALSE;

    if ((nparticles = calculate_n_objects(args)) < 0) {
        *pndeposited = nparticles;
        return FALSE;
    }

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    gwy_app_wait_start(wait_window, _("Initializing..."));

    /* Here we expect args->result to be the full-sized result data field.  The caller is responsible for that. */
    if (do_initialise && field)
        gwy_data_field_copy(field, result, FALSE);
    else
        gwy_data_field_clear(result);

    oxres = gwy_data_field_get_xres(result);
    oyres = gwy_data_field_get_yres(result);
    dx = gwy_data_field_get_dx(result);
    dy = gwy_data_field_get_dy(result);
    add = CLAMP(size + size_noise, 0, oxres/4);

    rngset = gwy_rand_gen_set_new(2);
    gwy_rand_gen_set_init(rngset, seed);
    rng = gwy_rand_gen_set_rng(rngset, 1);

    /* Renormalize everything for field size 1×1 (not pixel units!), including z.  Change parameters of potentials. */
    norm = 1.0/gwy_data_field_get_xreal(result);
    gwy_data_field_multiply(result, norm);
    gwy_data_field_set_xreal(result, gwy_data_field_get_xreal(result)*norm);
    gwy_data_field_set_yreal(result, gwy_data_field_get_yreal(result)*norm);
    size /= oxres;
    size_noise /= oxres;
    /* Now everything is normalized to be close to 1. */

    extfield = gwy_data_field_extend(result, add, add, add, add, GWY_EXTERIOR_MIRROR_EXTEND, 0.0, FALSE);
    xres = gwy_data_field_get_xres(extfield);
    yres = gwy_data_field_get_yres(extfield);
    xreal = gwy_data_field_get_xreal(extfield);
    yreal = gwy_data_field_get_yreal(extfield);
    dx = xreal/xres;
    dy = yreal/yres;
    diff = 0.1*dx;

    extdata = gwy_data_field_get_data(extfield);
    rdisizes = g_new(gdouble, nparticles);
    r = g_new(GwyXYZ, nparticles);
    v = g_new0(GwyXYZ, nparticles);
    a = g_new0(GwyXYZ, nparticles);
    f = g_new(GwyXYZ, nparticles);

    ndeposited = steps = 0;
    if (!gwy_app_wait_set_message(_("Initial particle set...")))
        goto end;

    while (ndeposited < nparticles && steps < maxsteps) {
        if (try_to_add_particle(r, rdisizes, ndeposited, extdata, xres, yres, dx, dy, size, size_noise, rng, rngset))
            ndeposited++;
        steps++;
    };

    gwy_data_field_area_copy(extfield, result, add, add, oxres, oyres, 0, 0);
    showit(result, rdisizes, r, ndeposited, add);
    gwy_data_field_data_changed(result);

    /*revise steps*/
    if (!gwy_app_wait_set_message("Running revise..."))
        goto end;

    for (i = 0; i < revise; i++) {
        /* Try to add some particles if necessary, do this only for first part of molecular dynamics steps. */
        if (ndeposited < nparticles && i < 3*revise/4) {
            nloc = 0;
            while (ndeposited < nparticles && nloc < maxloc) {
                if (try_to_add_particle(r, rdisizes, ndeposited, extdata, xres, yres, dx, dy, size, size_noise,
                                        rng, rngset))
                    ndeposited++;
                nloc++;
            };
        }

        /* test succesive LJ steps on substrate */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(k) \
            shared(r,f,ndeposited,rdisizes,diff,extdata,xres,yres,dx,dy)
#endif
        for (k = 0; k < ndeposited; k++) {
            gdouble rxk = r[k].x, ryk = r[k].y, rzk = r[k].z, sizek = rdisizes[k];
            GwyXYZ fk = { 0.0, 0.0, 0.0 };
            gint m;

            if (rxk/dx < 0.0 || rxk/dx >= xres || ryk/dy < 0.0 || ryk/dy >= yres) {
                f[k] = fk;
                continue;
            }

            /* Forces from other particles. */
            for (m = 0; m < ndeposited; m++) {
                if (m != k)
                    lj_potential_grad_spheres(r[m].x, r[m].y, r[m].z, rxk, ryk, rzk, sizek, rdisizes[m], &fk);
            }

            /* Force from substracte.
             * FIXME: This one is strange and cannot be differentiated exactly until the rounding is replaced by some
             * continous surface height interpolation.  */
            fk.x -= (integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk+diff, ryk, rzk, sizek)
                     - integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk-diff, ryk, rzk, sizek))/2/diff;
            fk.y -= (integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk, ryk-diff, rzk, sizek)
                     - integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk, ryk+diff, rzk, sizek))/2/diff;
            fk.z -= (integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk, ryk, rzk+diff, sizek)
                     - integrate_lj_substrate(extdata, xres, yres, dx, dy, rxk, ryk, rzk-diff, sizek))/2/diff;

            f[k] = fk;
        }

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(k) \
            shared(r,v,a,f,ndeposited,rdisizes,xres,yres,dx,dy,xreal,yreal)
#endif
        for (k = 0; k < ndeposited; k++) {
            const gdouble mass = 1.0, timestep = 0.5;
            GwyXYZ rk = r[k], vk = v[k], ak = a[k];
            gdouble sizek = rdisizes[k];

            if (rk.x/dx < 0.0 || rk.x/dx >= xres || rk.y/dy < 0.0 || rk.y/dy >= yres)
                continue;

            /* Move all particles. */
            update_x_v_a(&rk.x, &vk.x, &ak.x, f[k].x, timestep, mass);
            r[k].x = fmax(fmin(rk.x, xreal-sizek), sizek);

            update_x_v_a(&rk.y, &vk.y, &ak.y, f[k].y, timestep, mass);
            r[k].y = fmax(fmin(rk.y, yreal-sizek), sizek);

            update_x_v_a(&rk.z, &vk.z, &ak.z, f[k].z, timestep, mass);
            r[k].z = rk.z;

            v[k] = vk;
            a[k] = ak;
        }

        if (i % 100 == 99) {
            GwySynthUpdateType update = gwy_synth_update_progress(timer, preview_time, i, revise);

            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                gwy_data_field_area_copy(extfield, result, add, add, oxres, oyres, 0, 0);
                showit(result, rdisizes, r, ndeposited, add);
                gwy_data_field_data_changed(result);
            }
        }
    }
    finished = TRUE;

end:
    gwy_app_wait_finish();

    if (pndeposited)
        *pndeposited = ndeposited;

    g_timer_destroy(timer);
    g_free(rdisizes);
    g_free(r);
    g_free(v);
    g_free(a);
    g_free(f);

    gwy_rand_gen_set_free(rngset);

    if (finished) {
        gwy_data_field_area_copy(extfield, result, add, add, oxres, oyres, 0, 0);
        showit(result, rdisizes, r, ndeposited, add);
    }
    g_object_unref(extfield);

    /* Denormalize the result back. */
    gwy_data_field_multiply(result, 1.0/norm);
    gwy_data_field_set_xreal(result, gwy_data_field_get_xreal(result)/norm);
    gwy_data_field_set_yreal(result, gwy_data_field_get_yreal(result)/norm);

    return finished;
}

static gint
calculate_n_objects(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    gdouble size_noise = gwy_params_get_double(params, PARAM_SIZE_NOISE);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gint xres, yres, add, nparticles;

    if (gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE) && args->field) {
        xres = gwy_data_field_get_xres(args->field);
        yres = gwy_data_field_get_yres(args->field);
    }
    else {
        xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
        yres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YRES);
    }

    add = CLAMP((size + size_noise), 0, xres/4);
    nparticles = GWY_ROUND(coverage * (xres + 2*add)*(yres + 2*add)/(G_PI*size*size));
    if (nparticles <= 0)
        return RES_TOO_FEW;
    if (nparticles > MAXN)
        return RES_TOO_MANY;
    /* FIXME: Should be prevented by minimum value of size being 1 pixel. */
    if (size < 0.5)
        return RES_TOO_SMALL;
    if (size > 0.25*MIN(xres, yres))
        return RES_TOO_LARGE;
    return nparticles;
}

static const gchar*
particle_error(gint code)
{
    static const GwyEnum errors[] = {
        { N_("Error: too many particles."),  RES_TOO_MANY,  },
        { N_("Error: no particles."),        RES_TOO_FEW,   },
        { N_("Error: particles too large."), RES_TOO_LARGE, },
        { N_("Error: particles too small."), RES_TOO_SMALL, },
    };
    return _(gwy_enum_to_string(code, errors, G_N_ELEMENTS(errors)));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
