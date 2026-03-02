/*
 *  $Id: col_synth.c 23777 2021-05-24 22:04:49Z yeti-dn $
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
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    RELAX_WEAK   = 0,
    RELAX_STRONG = 1,
} RelaxationType;

typedef enum {
    GRAPH_MAX      = 0,
    GRAPH_RMS      = 1,
    GRAPH_NMAX     = 2,
    GRAPH_SKEW     = 3,
    GRAPH_KURTOSIS = 4,
    GRAPH_CORRLEN  = 5,
    GRAPH_NGRAPHS,
} GraphOutputs;

enum {
    PARAM_COVERAGE,
    PARAM_HEIGHT,
    PARAM_HEIGHT_NOISE,
    PARAM_THETA,
    PARAM_THETA_SPREAD,
    PARAM_PHI,
    PARAM_PHI_SPREAD,
    PARAM_RELAXATION,
    PARAM_MELTING,
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
    gdouble x;
    gdouble y;
    gdouble z;
    gdouble vx;
    gdouble vy;
    gdouble vz;
    gdouble tx;
    gdouble ty;
    gint vx_sign;
    gint vy_sign;
    gint k1;
    gint k2;
} Particle;

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
static void             col_synth            (GwyContainer *data,
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
static gdouble          calculate_skew       (GwyDataField *field);
static gdouble          calculate_kurtoris   (GwyDataField *field);
static gdouble          zero_crossing_corrlen(GwyDataField *field);
static gdouble          count_maxima         (GwyDataField *field);

static const EvolutionStatInfo evolution_info[GRAPH_NGRAPHS] = {
    { gwy_data_field_get_max, 0, 1, },
    { gwy_data_field_get_rms, 0, 1, },
    { count_maxima,           0, 0, },
    { calculate_skew,         0, 0, },
    { calculate_kurtoris,     0, 0, },
    { zero_crossing_corrlen,  1, 0, },
};

static const GwyEnum graph_outputs[GRAPH_NGRAPHS] = {
    { N_("Maximum"),                (1 << GRAPH_MAX),      },
    { N_("RMS"),                    (1 << GRAPH_RMS),      },
    { N_("Number of maxima"),       (1 << GRAPH_NMAX),     },
    { N_("Skew"),                   (1 << GRAPH_SKEW),     },
    { N_("Excess kurtosis"),        (1 << GRAPH_KURTOSIS), },
    { N_("Autocorrelation length"), (1 << GRAPH_CORRLEN),  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates columnar surfaces by a simple growth algorithm."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, col_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("col_synth",
                              (GwyProcessFunc)&col_synth,
                              N_("/S_ynthetic/_Deposition/_Columnar..."),
                              GWY_STOCK_SYNTHETIC_COLUMNAR,
                              RUN_MODES,
                              0,
                              N_("Generate columnar surface"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum relaxations[] = {
        { N_("Weak"),   RELAX_WEAK   },
        { N_("Strong"), RELAX_STRONG },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 0.01, 1e4, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-5, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Size spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(paramdef, PARAM_THETA, "theta", _("_Inclination"), TRUE, 4, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_THETA_SPREAD, "theta_spread", _("Spread"), 0.0, 1.0, 1.0);
    gwy_param_def_add_angle(paramdef, PARAM_PHI, "phi", _("_Direction"), FALSE, 1, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_PHI_SPREAD, "phi_spread", _("Spread"), 0.0, 1.0, 1.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_RELAXATION, "relaxation", _("Relaxation type"),
                              relaxations, G_N_ELEMENTS(relaxations), RELAX_WEAK);
    gwy_param_def_add_double(paramdef, PARAM_MELTING, "melting", _("_Melting"), 0.0, 1.0, 0.0);
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
col_synth(GwyContainer *data, GwyRunType runtype)
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
    /* Cheat a bit here.  Using field's rms means coverage of order unity wipes out most of the original topography.
     * So divide by the default coverage. */
    args.zscale = field ? gwy_data_field_get_rms(field)/10.0 : -1.0;

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

    gui.dialog = gwy_dialog_new(_("Grow Columnar Surface"));
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
    gwy_param_table_slider_set_mapping(table, PARAM_COVERAGE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_header(table, -1, _("Particle Size"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_HEIGHT_NOISE);

    gwy_param_table_append_header(table, -1, _("Incidence"));
    gwy_param_table_append_slider(table, PARAM_THETA);
    gwy_param_table_append_slider(table, PARAM_THETA_SPREAD);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_PHI);
    gwy_param_table_append_slider(table, PARAM_PHI_SPREAD);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_RELAXATION);
    gwy_param_table_append_slider(table, PARAM_MELTING);
    gwy_param_table_append_separator(table);
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
convolve_periodic_fast3(GwyDataField *data_field, gdouble k1)
{
    gint xres = gwy_data_field_get_xres(data_field);
    gint yres = gwy_data_field_get_yres(data_field);
    gdouble *d, *row0, *rowprev;
    gint i, j;
    gdouble zprev, z0, z, k0;

    k0 = 1.0 - 2.0*k1;
    g_assert(k0 > 0.5);

    /* Horizontal pass. */
    d = data_field->data;
    for (i = 0; i < yres; i++) {
        z0 = *d;
        zprev = d[xres-1];
        for (j = 0; j < xres-1; j++) {
            z = *d;
            *d *= k0;
            *d += k1*(zprev + d[1]);
            zprev = z;
            d++;
        }
        *d *= k0;
        *d += k1*(zprev + z0);
    }

    /* Vertical pass. */
    d = data_field->data;
    row0 = g_memdup(d, xres*sizeof(gdouble));
    rowprev = g_memdup(d + xres*(yres - 1), xres*sizeof(gdouble));
    for (i = 0; i < yres-1; i++) {
        for (j = 0; j < xres; j++) {
            z = *d;
            *d *= k0;
            *d += k1*(rowprev[j] + d[xres]);
            rowprev[j] = z;
            d++;
        }
    }
    for (j = 0; j < xres; j++) {
        *d *= k0;
        *d += k1*(rowprev[j] + row0[j]);
        d++;
    }

    g_free(rowprev);
    g_free(row0);

    gwy_data_field_invalidate(data_field);
}

static void
fill_sub_data(const gdouble *data, gdouble *subdata, gint xres, gint yres, gint nsub)
{
    gint subxres = xres/nsub, subyres = yres/nsub, i, j, ii, jj;
    gdouble max;

    for (i = 0; i < subyres; i++) {
        for (j = 0; j < subxres; j++) {
            max = -G_MAXDOUBLE;
            for (ii = 0; ii < nsub; ii++) {
                for (jj = 0; jj < nsub; jj++)
                    max = fmax(max, data[i*nsub*xres + j*nsub + ii*xres + jj]);
            }
            subdata[i*subxres + j] = max;
        }
    }
}

static void
init_particle(Particle *p, gdouble x, gdouble y, gdouble z, gdouble theta, gdouble phi)
{
    /* Calculate speed vectors (avoid exact zeros).
     * Calculate full-pixel traversal times and traversal signs. */
    p->vx = cos(phi);
    if (fabs(p->vx) < 1e-16)
        p->vx = copysign(1e-16, p->vx);
    p->tx = 1.0/fabs(p->vx);
    p->vx_sign = (p->vx > 0.0 ? 1 : -1);

    p->vy = sin(phi);
    if (fabs(p->vy) < 1e-16)
        p->vy = copysign(1e-16, p->vy);
    p->ty = 1.0/fabs(p->vy);
    p->vy_sign = (p->vy > 0.0 ? 1 : -1);

    p->vz = -1.0/tan(MAX(theta, 1e-16));

    p->x = x;
    p->y = y;
    p->z = z;
}

static void
trace_particle(Particle *p, gdouble *data, gint xres, gint yres, gboolean final)
{
    gboolean move_across, side;
    gdouble vx = p->vx, vy = p->vy, vz = p->vz, tx = p->tx, ty = p->ty, x = p->x, y = p->y, z = p->z;
    gint vx_sign = p->vx_sign, vy_sign = p->vy_sign;
    gdouble t_across, t_adj, t, t_prev, u;
    gint row, col, iold, jold;

    row = iold = (gint)floor(y);
    col = jold = (gint)floor(x);

    /* Find the first intersection with side and initialise side type.
     * Set u to the intersection point (u is along the side the trajectory is intersecting – the other coordinate is
     * always at the edge, row.e. 0 or 1).
     * Set (col, row) to the pixel we start with.
     * More precisely, (col,row) is always the pixel the line is going into – from the left or right edge, depending on
     * the sign.
     * Update z and check if we landed already.
     */
    /* Reuse t_accross and t_adj for x and y. */
    /* A trick: Function u*s + (1-s)/2 takes values u and 1-u for for s=1 and s=-1, respectively.
     * So instead of conditional expressions choosing between u and 1-u we use this expression, starting with the
     * integer part. */
    t_across = ((vx_sign + 1)/2 - vx_sign*(x - col))*tx;
    t_adj = ((vy_sign + 1)/2 - vy_sign*(y - row))*ty;
    t = fmin(t_across, t_adj);
    if (t_across <= t_adj) {
        z += t_across*vz;
        side = TRUE;
        u = y - row + vy*t_across;
        col += vx_sign;
    }
    else {
        z += t_adj*vz;
        side = FALSE;
        u = x - col + vx*t_adj;
        row += vy_sign;
    }
    col = (col + xres) % xres;
    row = (row + yres) % yres;
    if (z <= data[iold*xres + jold] || z <= data[row*xres + col]) {
        if (!final)
            return;
        goto landed;
    }

    /* Split the inner loop into four different cases according to vx and vy signs to help the compiler figure out
     * optimisations better. */
    if (vx_sign == 1 && vy_sign == 1) {
        while (TRUE) {
            t_prev = t;
            if (!side) {
                t_across = ty;
                t_adj = tx*(1 - u);
            }
            else {
                t_across = tx;
                t_adj = ty*(1 - u);
            }
            t = fmin(t_across, t_adj);
            z += vz*t;
            if (z <= data[row*xres + col])
                break;
            move_across = (t_across <= t_adj);
            if (!side) {
                if (move_across) {
                    row = (G_UNLIKELY(row == yres-1) ? 0 : row+1);
                    u += t*vx;
                }
                else {
                    col = (G_UNLIKELY(col == xres-1) ? 0 : col+1);
                    u = t*vy;
                }
            }
            else {
                if (move_across) {
                    col = (G_UNLIKELY(col == xres-1) ? 0 : col+1);
                    u += t*vy;
                }
                else {
                    row = (G_UNLIKELY(row == yres-1) ? 0 : row+1);
                    u = t*vx;
                }
            }
            side ^= !move_across;
        }
    }
    else if (vx_sign == 1 && vy_sign == -1) {
        while (TRUE) {
            t_prev = t;
            if (!side) {
                t_across = ty;
                t_adj = tx*(1.0 - u);
            }
            else {
                t_across = tx;
                t_adj = ty*u;
            }
            t = fmin(t_across, t_adj);
            z += vz*t;
            if (z <= data[row*xres + col])
                break;
            move_across = (t_across <= t_adj);
            if (!side) {
                if (move_across) {
                    row = (G_UNLIKELY(!row) ? yres-1 : row-1);
                    u += t*vx;
                }
                else {
                    col = (G_UNLIKELY(col == xres-1) ? 0 : col+1);
                    u = 1 + t*vy;
                }
            }
            else {
                if (move_across) {
                    col = (G_UNLIKELY(col == xres-1) ? 0 : col+1);
                    u += t*vy;
                }
                else {
                    row = (G_UNLIKELY(!row) ? yres-1 : row-1);
                    u = t*vx;
                }
            }
            side ^= !move_across;
        }
    }
    else if (vx_sign == -1 && vy_sign == 1) {
        while (TRUE) {
            t_prev = t;
            if (!side) {
                t_across = ty;
                t_adj = tx*u;
            }
            else {
                t_across = tx;
                t_adj = ty*(1 - u);
            }
            t = fmin(t_across, t_adj);
            z += vz*t;
            if (z <= data[row*xres + col])
                break;
            move_across = (t_across <= t_adj);
            if (!side) {
                if (move_across) {
                    row = (G_UNLIKELY(row == yres-1) ? 0 : row+1);
                    u += t*vx;
                }
                else {
                    col = (G_UNLIKELY(!col) ? xres-1 : col-1);
                    u = t*vy;
                }
            }
            else {
                if (move_across) {
                    col = (G_UNLIKELY(!col) ? xres-1 : col-1);
                    u += t*vy;
                }
                else {
                    row = (G_UNLIKELY(row == yres-1) ? 0 : row+1);
                    u = 1 + t*vx;
                }
            }
            side ^= !move_across;
        }
    }
    else {
        while (TRUE) {
            t_prev = t;
            if (!side) {
                t_across = ty;
                t_adj = tx*u;
            }
            else {
                t_across = tx;
                t_adj = ty*u;
            }
            t = fmin(t_across, t_adj);
            z += vz*t;
            if (z <= data[row*xres + col])
                break;
            move_across = (t_across <= t_adj);
            if (!side) {
                if (move_across) {
                    row = (G_UNLIKELY(!row) ? yres-1 : row-1);
                    u += t*vx;
                }
                else {
                    col = (G_UNLIKELY(!col) ? xres-1 : col-1);
                    u = 1 + t*vy;
                }
            }
            else {
                if (move_across) {
                    col = (G_UNLIKELY(!col) ? xres-1 : col-1);
                    u += t*vy;
                }
                else {
                    row = (G_UNLIKELY(!row) ? yres-1 : row-1);
                    u = 1 + t*vx;
                }
            }
            side ^= !move_across;
        }
    }

landed:
    if (final) {
        /* Trace back the previous pixel from the current position and side.  Not worth remembering it in the loop.
         * We do not care about precise (x,y,z), only the pixel where we landed. */
        iold = row;
        jold = col;
        if (!side)
            iold = (iold + yres - vy_sign) % yres;
        else
            jold = (jold + xres - vx_sign) % xres;

        p->k1 = iold*xres + jold;
        p->k2 = row*xres + col;
    }
    else {
        /* Return to a safe place before we hit anything.  The caller intends to continue the simulation, presumably
         * in a finer grid.  We do not care about the pixel but need precise (x,y,z). */
        if (side) {
            x = col + (vx_sign == -1);
            y = row + u;
        }
        else {
            x = col + u;
            y = row + (vy_sign == -1);
        }
        p->x = fmod(x - 0.5*t_prev*vx + xres, xres);
        p->y = fmod(y - 0.5*t_prev*vy + yres, yres);
        p->z = z - (t + 0.5*t_prev)*vz;
    }
}

static gdouble
grow_surface(Particle *p, gdouble *data, gint xres, gint yres, gdouble size,
             RelaxationType relaxation, GwyRandGenSet *rngset)
{
    gint k1 = p->k1, k2 = p->k2, row = k2/xres, col = k2 % xres;
    gint i, j, k;

    /* Relaxation – important to do at least the two-site k1-k2 relaxation!
     * It prevents exponential growth of spikes with periodic boundary conditions. */
    if (relaxation == RELAX_STRONG) {
        for (i = -1; i <= 1; i++) {
            for (j = -1; j <= 1; j++) {
                if (!j && !i)
                    continue;

                k = ((row + yres + i) % yres)*xres + (col + xres + j) % xres;
                if (data[k] < data[k2]) {
                    if (gwy_rand_gen_set_double(rngset, 0) < 0.5/(i*i + j*j))
                        k2 = k;
                }
            }
        }
    }
    k = (data[k2] < data[k1]) ? k2 : k1;
    data[k] += size;

    /* Store the final location to k1, k2 so that the caller knows which pixel was increased. */
    p->k1 = k % xres;
    p->k2 = k/xres;

    return data[k];
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    static const gint factors[] = { 6, 5, 7, 8, 4, 3, 2 };
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gdouble melting = gwy_params_get_double(params, PARAM_MELTING);
    gdouble theta = gwy_params_get_double(params, PARAM_THETA);
    gdouble theta_spread = gwy_params_get_double(params, PARAM_THETA_SPREAD);
    gdouble phi = gwy_params_get_double(params, PARAM_PHI);
    gdouble phi_spread = gwy_params_get_double(params, PARAM_PHI_SPREAD);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    guint graph_flags = gwy_params_get_flags(params, PARAM_GRAPH_FLAGS);
    RelaxationType relaxation = gwy_params_get_enum(params, PARAM_RELAXATION);
    GwyDataField *field = args->result;
    GArray **evolution = args->evolution[0] ? args->evolution : NULL;
    GwyRandGenSet *rngset;
    gint xres, yres, power10z, nsub;
    guint64 npart, ip;
    guint i;
    gdouble zmax, zsum, nextgraphx, nextconvolve, zoff;
    gdouble preview_time = (animated ? 1.25 : 0.0);
    GwySynthUpdateType update;
    GTimer *timer;
    gboolean finished = FALSE;
    gdouble *data, *subdata = NULL;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    rngset = gwy_rand_gen_set_new(2);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    /* NB: We could have a particle size parameter (determinig how much the height grows when a particle sticks to the
     * surface), but by scaling tan(ϑ) and this parameter together we would obtain the same surface.  So it would be
     * redundant.
     *
     * By scaling by user-given height scale, we can assume our particles are unit cubes so iheight can be simply
     * taken as 1.0. */
    if (args->field && do_initialise) {
        gwy_data_field_copy(args->field, field, FALSE);
        gwy_data_field_multiply(field, 1.0/height);
    }
    else
        gwy_data_field_clear(field);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    for (i = 0; i < G_N_ELEMENTS(factors); i++) {
        nsub = factors[i];
        if (xres % nsub == 0 && yres % nsub == 0 && xres/nsub >= 12 && yres/nsub >= 12)
            break;
        nsub = 0;
    }

    zoff = gwy_data_field_get_max(field);
    gwy_data_field_add(field, -zoff);
    zmax = zsum = nextgraphx = 0.0;
    nextconvolve = melting ? 0.0 : G_MAXDOUBLE;
    data = gwy_data_field_get_data(field);

    if (nsub) {
        subdata = g_new(gdouble, (xres/nsub)*(yres/nsub));
        fill_sub_data(data, subdata, xres, yres, nsub);
    }

    npart = coverage * (guint64)(xres*yres);

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Depositing particles...")))
        goto end;

    for (ip = 0; ip < npart; ip++) {
        gdouble itheta = theta, iphi = phi, iheight = 1.0, cth, x, y, z, v;
        Particle p;

        if (height_noise)
            iheight *= exp(gwy_rand_gen_set_gaussian(rngset, 0, height_noise));

        if (theta_spread) {
            do {
                cth = (cos(itheta) + (gwy_rand_gen_set_gaussian(rngset, 0, G_PI*theta_spread)));
            } while (cth < 0.0 || cth > 0.99);

            itheta = acos(1.0 - cth);
        }

        if (phi_spread)
            iphi += gwy_rand_gen_set_gaussian(rngset, 0, 2.0*G_PI*phi_spread);

        x = xres*gwy_rand_gen_set_double(rngset, 0);
        y = yres*gwy_rand_gen_set_double(rngset, 0);
        z = zmax + 5.0;
        init_particle(&p, x, y, z, itheta, iphi);
        if (nsub) {
            /* Voxels in subdata are nsub times taller than in the full data.  Compensate by scaling the z velocity
             * component (alternatively this can be viewed that we are moving nsub times faster in x and y).  We could
             * fix vx and vy instead – this would require fixing the tx and ty too. */
            p.x /= nsub;
            p.y /= nsub;
            p.vz *= nsub;
            trace_particle(&p, subdata, xres/nsub, yres/nsub, FALSE);
            p.vz /= nsub;
            p.x *= nsub;
            p.y *= nsub;
        }
        trace_particle(&p, data, xres, yres, TRUE);
        z = grow_surface(&p, data, xres, yres, iheight, relaxation, rngset);
        zmax = fmax(z, zmax);
        if (nsub) {
            i = (p.k2/nsub)*(xres/nsub) + (p.k1/nsub);
            subdata[i] = fmax(subdata[i], data[p.k2*xres + p.k1]);
        }

        if (ip % 1000 == 0) {
            update = gwy_synth_update_progress(timer, preview_time, ip, npart);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                goto end;
            if (animated && update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                gwy_data_field_invalidate(field);
                gwy_data_field_data_changed(field);
            }
        }

        zsum += iheight;
        if (zsum/(xres*yres) >= nextconvolve) {
            convolve_periodic_fast3(field, 0.001*sqrt(melting));
            if (nsub)
                fill_sub_data(data, subdata, xres, yres, nsub);
            nextconvolve += 0.0003/sqrt(melting);
        }
        if (evolution && ip >= nextgraphx) {
            gwy_data_field_invalidate(field);
            for (i = 0; i < GRAPH_NGRAPHS; i++) {
                if (graph_flags & (1 << i)) {
                    v = evolution_info[i].func(field);
                    v *= gwy_powi(height, evolution_info[i].power_z);
                    g_array_append_val(evolution[i], v);
                }
            }
            v = zsum/(xres*yres) * height;
            g_array_append_val(evolution[GRAPH_NGRAPHS], v);

            nextgraphx = 1.2*nextgraphx + 1.0;
        }
    }

    gwy_data_field_invalidate(field);
    gwy_data_field_add(field, zoff);
    gwy_data_field_multiply(field, height);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_free(subdata);
    g_timer_destroy(timer);
    gwy_rand_gen_set_free(rngset);

    return finished;
}

static gdouble
calculate_skew(GwyDataField *field)
{
    gdouble skew;
    gwy_data_field_get_stats(field, NULL, NULL, NULL, &skew, NULL);
    return skew;
}

static gdouble
calculate_kurtoris(GwyDataField *field)
{
    gdouble kurtosis;
    gwy_data_field_get_stats(field, NULL, NULL, NULL, NULL, &kurtosis);
    return kurtosis;
}

static gdouble
find_decay_point(GwyDataLine *line, gdouble q)
{
    const gdouble *d = gwy_data_line_get_data(line);
    guint res = gwy_data_line_get_res(line);
    gdouble max = d[0];
    gdouble threshold = q*max;
    guint i;
    gdouble v0, v1, t;

    for (i = 1; i < res; i++) {
        if (d[i] <= threshold) {
            if (d[i] == threshold)
                return gwy_data_line_itor(line, i);

            v0 = d[i-1] - threshold;
            v1 = d[i] - threshold;
            t = v0/(v0 - v1);
            return gwy_data_line_itor(line, i-1 + t);
        }
    }

    return -1.0;
}

static gdouble
zero_crossing_corrlen(GwyDataField *field)
{
    GwyDataLine *acf;
    gdouble T;

    acf = gwy_data_line_new(1, 1.0, FALSE);
    gwy_data_field_acf(field, acf, GWY_ORIENTATION_HORIZONTAL, GWY_INTERPOLATION_LINEAR, -1);
    T = find_decay_point(acf, 0.0);
    g_object_unref(acf);

    return (T > 0.0 ? T : gwy_data_field_get_xreal(field));
}

static gdouble
count_maxima(GwyDataField *field)
{
    return gwy_data_field_count_maxima(field);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
