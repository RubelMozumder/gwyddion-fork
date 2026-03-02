/*
 *  $Id: domain_synth.c 26003 2023-11-09 10:04:27Z yeti-dn $
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
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* Cannot change this without losing reproducibility again! */
enum {
    NRANDOM_GENERATORS = 24
};

enum {
    RESPONSE_TAKE_PRESET = 100,
};

enum {
    OUTPUT_U = 0,
    OUTPUT_V = 1,
    OUTPUT_NTYPES,
};

enum {
    PARAM_PREVIEW,
    PARAM_NITERS,
    PARAM_T,
    PARAM_B,
    PARAM_NU,
    PARAM_MU,
    PARAM_DT,
    PARAM_HEIGHT,
    PARAM_QUANTITY,
    PARAM_PRESET,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,
    BUTTON_SELECT_PRESET,

    PARAM_DIMS0
};

typedef struct {
    const gchar *name;
    gint niters;
    gdouble T;
    gdouble B;
    gdouble mu;
    gdouble nu;
    gdouble dt;
} SimulationPreset;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result[OUTPUT_NTYPES];
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyParamTable *table_presets;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             domain_synth        (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GtkWidget*       dimensions_tab_new  (ModuleGUI *gui);
static GtkWidget*       generator_tab_new   (ModuleGUI *gui);
static GtkWidget*       presets_tab_new     (ModuleGUI *gui);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);

static const GwyEnum quantity_types[OUTPUT_NTYPES] = {
    { N_("Discrete state"),       OUTPUT_U, },
    { N_("Continuous inhibitor"), OUTPUT_V, },
};

static const SimulationPreset presets[] = {
    { "Alien biology",       1000, 2.0,  44.0, 2.0,  -0.5,  2.0,  },
    { "Angular",             700,  0.1,  20.0, 0.2,   0.0,  10.0, },
    { "Brain waves",         1200, 1.8,  42.0, 2.0,   0.3,  3.0,  },
    { "Chaos",               500,  2.0,  9.0,  1.0,   0.0,  90.0, },
    { "Islands",             500,  0.7,  8.0,  1.0,   0.0,  0.05, },
    { "Mixed spirals",       1200, 0.12, 0.85, 27.0,  0.0,  45.0, },
    { "Multiscale blotches", 740,  0.8,  6.0,  1.2,  -0.4,  7.0,  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates domain images using a hybrid Ising model."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, domain_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("domain_synth",
                              (GwyProcessFunc)&domain_synth,
                              N_("/S_ynthetic/_Order and Disorder/_Domains..."),
                              GWY_STOCK_SYNTHETIC_DOMAINS,
                              RUN_MODES,
                              0,
                              N_("Generate image with domains"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *outputs = NULL, *preset_enum = NULL;
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    outputs = g_new(GwyEnum, OUTPUT_NTYPES);
    for (i = 0; i < OUTPUT_NTYPES; i++) {
        outputs[i].name = quantity_types[i].name;
        outputs[i].value = (1 << quantity_types[i].value);
    }
    preset_enum = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(presets), presets, sizeof(SimulationPreset),
                                            G_STRUCT_OFFSET(SimulationPreset, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_PREVIEW, "preview_quantity", gwy_sgettext("verb|_Display"),
                              quantity_types, G_N_ELEMENTS(quantity_types), OUTPUT_U);
    gwy_param_def_add_int(paramdef, PARAM_NITERS, "niters", _("_Number of iterations"), 1, 100000, 500);
    gwy_param_def_add_double(paramdef, PARAM_T, "T", _("_Temperature"), 0.001, 5.0, 0.8);
    gwy_param_def_add_double(paramdef, PARAM_B, "B", _("_Inhibitor strength"), 0.001, 100.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_MU, "mu", _("In_hibitor coupling"), 0.001, 100.0, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_NU, "nu", _("_Bias"), -1.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_DT, "dt", _("_Monte Carlo time step"), 0.001, 100.0, 5.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-5, 1000.0, 1.0);
    gwy_param_def_add_gwyflags(paramdef, PARAM_QUANTITY, "quantity", _("Output type"),
                               outputs, G_N_ELEMENTS(quantity_types), 1 << OUTPUT_U);
    gwy_param_def_add_gwyenum(paramdef, PARAM_PRESET, "preset", _("Preset"),
                              preset_enum, G_N_ELEMENTS(presets), G_N_ELEMENTS(presets)-1);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
domain_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyDataField *field;
    guint i, output;
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
    args.field = field;
    for (i = 0; i < OUTPUT_NTYPES; i++)
        args.result[i] = gwy_synth_make_result_data_field(field, args.params, FALSE);
    /* TODO: preview u if u or both are output; but preview v if only v is output. */
    output = gwy_params_get_flags(args.params, PARAM_QUANTITY);
    if (gwy_params_get_boolean(args.params, PARAM_ANIMATED)) {
        if (output & (1 << OUTPUT_U))
            gwy_app_wait_preview_data_field(args.result[OUTPUT_U], data, id);
        else
            gwy_app_wait_preview_data_field(args.result[OUTPUT_V], data, id);
    }
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;
    for (i = 0; i < OUTPUT_NTYPES; i++) {
        if (output & (1 << i))
            gwy_synth_add_result_to_file(args.result[i], data, id, args.params);
    }

end:
    for (i = 0; i < OUTPUT_NTYPES; i++)
        GWY_OBJECT_UNREF(args.result[i]);
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
    args->result[OUTPUT_U] = gwy_synth_make_result_data_field(args->field, args->params, TRUE);
    args->result[OUTPUT_V] = gwy_data_field_new_alike(args->result[OUTPUT_U], TRUE);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result[OUTPUT_U]);
    if (gui.template_)
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Domains"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gtk_notebook_append_page(notebook, presets_tab_new(&gui), gtk_label_new(_("Presets")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_generator, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_presets, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(args->field);
    GWY_OBJECT_UNREF(args->result[OUTPUT_V]);
    GWY_OBJECT_UNREF(args->result[OUTPUT_U]);

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

    gwy_param_table_append_combo(table, PARAM_PREVIEW);

    gwy_param_table_append_header(table, -1, _("Simulation Parameters"));
    gwy_param_table_append_slider(table, PARAM_NITERS);
    gwy_param_table_slider_set_mapping(table, PARAM_NITERS, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_T);
    gwy_param_table_append_slider(table, PARAM_B);
    gwy_param_table_append_slider(table, PARAM_MU);
    gwy_param_table_append_slider(table, PARAM_NU);
    gwy_param_table_append_slider(table, PARAM_DT);
    gwy_param_table_set_unitstr(table, PARAM_DT, "×10<sup>-3</sup>");

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_checkboxes(table, PARAM_QUANTITY);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_ANIMATED);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
presets_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_presets = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_radio(table, PARAM_PRESET);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_SELECT_PRESET, -1, RESPONSE_TAKE_PRESET,
                                  _("Use Selected _Preset"));

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
        static const gint zids[] = { PARAM_HEIGHT };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }
    if (id < 0 || id == PARAM_QUANTITY || id == PARAM_DIMS0 + GWY_DIMS_PARAM_REPLACE) {
        guint output = gwy_params_get_flags(params, PARAM_QUANTITY);
        gboolean do_replace = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_REPLACE);
        gboolean sens = (!do_replace && output) || output == (1 << OUTPUT_U) || output == (1 << OUTPUT_V);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, sens);
    }
    if (id < 0 || id == PARAM_PREVIEW) {
        gint preview = gwy_params_get_enum(params, PARAM_PREVIEW);
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->result[preview]);
    }
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table = gui->table_generator;

    if (response == GWY_RESPONSE_SYNTH_INIT_Z) {
        gdouble zscale = args->zscale;
        gint power10z;

        if (zscale > 0.0) {
            gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
            gwy_param_table_set_double(table, PARAM_HEIGHT, zscale/pow10(power10z));
        }
    }
    else if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS) {
        gwy_synth_use_dimensions_template(gui->table_dimensions);
    }
    else if (response == RESPONSE_TAKE_PRESET) {
        const SimulationPreset *preset = presets + gwy_params_get_enum(args->params, PARAM_PRESET);
        gwy_param_table_set_int(table, PARAM_NITERS, preset->niters);
        gwy_param_table_set_double(table, PARAM_T, preset->T);
        gwy_param_table_set_double(table, PARAM_B, preset->B);
        gwy_param_table_set_double(table, PARAM_MU, preset->mu);
        gwy_param_table_set_double(table, PARAM_NU, preset->nu);
        gwy_param_table_set_double(table, PARAM_DT, preset->dt);
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    if (execute(gui->args, GTK_WINDOW(gui->dialog))) {
        gwy_data_field_data_changed(gui->args->result[OUTPUT_U]);
        gwy_data_field_data_changed(gui->args->result[OUTPUT_V]);
    }
}

static void
init_ufield_from_surface(GwyDataField *field, GwyDataField *ufield, GRand *rng)
{
    guint xres = ufield->xres, yres = ufield->yres, k;
    gdouble *u = gwy_data_field_get_data(ufield);

    if (field) {
        gdouble med = gwy_data_field_get_median(field);
        const gdouble *d = field->data;

        for (k = xres*yres; k; k--, d++, u++)
            *u = (*d <= med) ? -1 : 1;
    }
    else {
        for (k = xres*yres; k; k--, u++)
            *u = g_rand_boolean(rng) ? 1 : -1;
    }
}

static inline gint
mc_step8(gint u, gint u1, gint u2, gint u3, gint u4, gint u5, gint u6, gint u7, gint u8,
         gdouble random_number, gdouble T, gdouble B, gdouble v)
{
    gint s1 = (u == u1) + (u == u2) + (u == u3) + (u == u4);
    gint s2 = (u == u5) + (u == u6) + (u == u7) + (u == u8);
    gdouble E = 6.0 - s1 - 0.5*s2 + B*u*v;
    gdouble Enew = s1 + 0.5*s2 - B*u*v;
    if (Enew < E - T*G_LN2 || random_number < 0.5*exp((E - Enew)/T))
        return -u;
    return u;
}

static void
field_mc_step8(const GwyDataField *vfield, const gint *u, gint *unew,
               gdouble T, gdouble B,
               const GwyDataField *random_numbers)
{
    guint xres = vfield->xres, yres = vfield->yres, n = xres*yres;
    const gdouble *v = vfield->data, *r = random_numbers->data;
    guint i, j;

    /* Top row. */
    unew[0] = mc_step8(u[0],
                       u[1], u[xres-1], u[xres], u[n-xres],
                       u[xres+1], u[2*xres-1], u[n-xres+1], u[n-1],
                       r[0], T, B, v[0]);

    for (j = 1; j < xres-1; j++) {
        unew[j] = mc_step8(u[j],
                           u[j-1], u[j+1], u[j+xres], u[j + n-xres],
                           u[j+xres-1], u[j+xres+1], u[j-1 + n-xres], u[j+1 + n-xres],
                           r[j], T, B, v[j]);
    }

    j = xres-1;
    unew[j] = mc_step8(u[j],
                       u[0], u[j+xres], u[j-1], u[n-1],
                       u[2*xres-2],  u[xres], u[n-2], u[n-xres],
                       r[j], T, B, v[j]);

    /* Inner rows. */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(xres,yres,u,unew,v,r,T,B) \
            private(i,j)
#endif
    for (i = 1; i < yres-1; i++) {
        gint *unewrow = unew + i*xres;
        const gint *urow = u + i*xres;
        const gint *uprevrow = u + (i - 1)*xres;
        const gint *unextrow = u + (i + 1)*xres;
        const gdouble *vrow = v + i*xres;
        const gdouble *rrow = r + i*xres;

        unewrow[0] = mc_step8(urow[0],
                              uprevrow[0], urow[1], unextrow[0], urow[xres-1],
                              uprevrow[1], uprevrow[xres-1], unextrow[1], unextrow[xres-1],
                              rrow[0], T, B, vrow[0]);

        for (j = 1; j < xres-1; j++) {
            unewrow[j] = mc_step8(urow[j],
                                  uprevrow[j], urow[j-1], urow[j+1], unextrow[j],
                                  uprevrow[j-1], uprevrow[j+1], unextrow[j-1], unextrow[j+1],
                                  rrow[j], T, B, vrow[j]);
        }

        j = xres-1;
        unewrow[j] = mc_step8(urow[j],
                              uprevrow[j], urow[0], urow[xres-2], unextrow[j],
                              uprevrow[0], uprevrow[xres-2], unextrow[0], unextrow[xres-2],
                              rrow[j], T, B, vrow[j]);
    }

    /* Bottom row. */
    j = i = n-xres;
    unew[j] = mc_step8(u[j],
                       u[j+1], u[0], u[n-1], u[j-xres],
                       u[j - xres-1], u[j - xres+1], u[1], u[xres-1],
                       r[j], T, B, v[j]);

    for (j = 1; j < xres-1; j++) {
        unew[i + j] = mc_step8(u[i + j],
                               u[i + j-1], u[i + j+1], u[i + j-xres], u[j],
                               u[i + j-xres-1], u[i + j-xres+1], u[j-1], u[j+1],
                               r[i + j], T, B, v[i + j]);
    }

    j = n-1;
    unew[j] = mc_step8(u[j],
                       u[i], u[j-xres], u[xres-1], u[j-1],
                       u[0], u[xres-2], u[i-2], u[i-xres],
                       r[j], T, B, v[j]);
}

static inline gdouble
v_rk4_step(gdouble v, gint u, gdouble mu, gdouble nu, gdouble dt)
{
    return v + dt*(1.0 - dt*(0.5 - dt*(1.0/6.0 - dt/24.0)))*(mu*u - v - nu);
}

static void
field_rk4_step(GwyDataField *vfield, const gint *u,
               gdouble mu, gdouble nu, gdouble dt)
{
    guint xres = vfield->xres, yres = vfield->yres, n = xres*yres;
    gdouble *v = vfield->data;
    guint k;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(u,v,n,mu,nu,dt) \
            private(k)
#endif
    for (k = 0; k < n; k++)
        v[k] = v_rk4_step(v[k], u[k], mu, nu, dt);
}

static void
ufield_to_data_field(const gint *u, const gint *ubuf, GwyDataField *field)
{
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);
    guint k;

    for (k = 0; k < xres*yres; k++)
        field->data[k] = 0.5*(u[k] + ubuf[k]);

    gwy_data_field_invalidate(field);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gint niters = gwy_params_get_int(params, PARAM_NITERS);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble T = gwy_params_get_double(params, PARAM_T);
    gdouble B = gwy_params_get_double(params, PARAM_B);
    gdouble mu = gwy_params_get_double(params, PARAM_MU);
    gdouble nu = gwy_params_get_double(params, PARAM_NU);
    gdouble dt = gwy_params_get_double(params, PARAM_DT) * 1e-3;
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    GwyDataField *random_numbers = NULL;
    GwyDataField *ufield = args->result[OUTPUT_U], *vfield = args->result[OUTPUT_V];
    GwyRandGenSet *rngset;
    GwySynthUpdateType update;
    gdouble preview_time = (animated ? 1.25 : 0.0);
    GTimer *timer;
    gint *u = NULL, *ubuf = NULL;
    gint i, k, xres, yres, power10z;
    gboolean finished = FALSE;
    gdouble *udata;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    rngset = gwy_rand_gen_set_new(NRANDOM_GENERATORS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    if (args->field && do_initialise)
        init_ufield_from_surface(args->field, ufield, gwy_rand_gen_set_rng(rngset, 0));
    else
        init_ufield_from_surface(NULL, ufield, gwy_rand_gen_set_rng(rngset, 0));
    gwy_data_field_clear(vfield);

    xres = gwy_data_field_get_xres(ufield);
    yres = gwy_data_field_get_yres(ufield);
    udata = gwy_data_field_get_data(ufield);

    u = g_new(gint, xres*yres);
    ubuf = g_new(gint, xres*yres);
    for (k = 0; k < xres*yres; k++)
        u[k] = (gint)udata[k];

    random_numbers = gwy_data_field_new(xres, yres, xres, yres, FALSE);

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Running computation...")))
        goto end;

    for (i = 0; i < niters; i++) {
        gwy_rand_gen_set_fill_doubles(rngset, random_numbers->data, xres*yres);
        field_mc_step8(vfield, u, ubuf, T, B, random_numbers);
        field_rk4_step(vfield, ubuf, mu, nu, dt);
        gwy_rand_gen_set_fill_doubles(rngset, random_numbers->data, xres*yres);
        field_mc_step8(vfield, ubuf, u, T, B, random_numbers);
        field_rk4_step(vfield, u, mu, nu, dt);

        if (i % 20 == 0) {
            update = gwy_synth_update_progress(timer, preview_time, i, niters);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                ufield_to_data_field(u, ubuf, ufield);
                gwy_data_field_invalidate(vfield);
                gwy_data_field_data_changed(ufield);
                gwy_data_field_data_changed(vfield);
            }
        }
    }

    ufield_to_data_field(u, ubuf, ufield);
    gwy_data_field_invalidate(vfield);
    finished = TRUE;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);
    gwy_data_field_renormalize(ufield, height, 0.0);
    gwy_data_field_renormalize(vfield, height, 0.0);

end:
    gwy_app_wait_finish();
    gwy_rand_gen_set_free(rngset);
    GWY_OBJECT_UNREF(random_numbers);
    g_timer_destroy(timer);
    g_free(u);
    g_free(ubuf);

    return finished;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
