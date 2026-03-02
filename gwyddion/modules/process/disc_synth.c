/*
 *  $Id: disc_synth.c 24204 2021-09-28 08:47:17Z yeti-dn $
 *  Copyright (C) 2018-2021 David Necas (Yeti).
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
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    RNG_POSITION,
    RNG_RADIUS_INIT,
    RNG_HEIGHT,
    RNG_NRNGS
} ObjSynthRng;

enum {
    PARAM_RADIUS_INIT,
    PARAM_RADIUS_INIT_NOISE,
    PARAM_RADIUS_MIN,
    PARAM_SEPARATION,
    PARAM_MAKE_TILES,
    PARAM_GAP_THICKNESS,
    PARAM_APPLY_OPENING,
    PARAM_OPENING_SIZE,
    PARAM_HEIGHT,
    PARAM_HEIGHT_NOISE,
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
    gdouble r;
} Disc;

typedef struct {
    guint i;
    guint j;
    gdouble gap;
} ProductivePair;

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
static void             disc_synth          (GwyContainer *data,
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
    N_("Generates random more or less touching discs."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, disc_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("disc_synth",
                              (GwyProcessFunc)&disc_synth,
                              N_("/S_ynthetic/D_iscs..."),
                              GWY_STOCK_SYNTHETIC_DISCS,
                              RUN_MODES,
                              0,
                              N_("Generate surface of random discs"));

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
    gwy_param_def_add_double(paramdef, PARAM_RADIUS_INIT, "radius_init", _("Starting _radius"), 5.0, 1000.0, 30.0);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS_INIT_NOISE, "radius_init_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS_MIN, "radius_min", _("_Minimum radius"), 3.0, 1000.0, 12.0);
    gwy_param_def_add_double(paramdef, PARAM_SEPARATION, "separation", _("_Separation"), 3.0, 120.0, 3.0);
    gwy_param_def_add_boolean(paramdef, PARAM_MAKE_TILES, "make_tiles", _("_Transform to tiles"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_GAP_THICKNESS, "gap_thickness", _("_Gap thickness"), 1.0, 250.0, 3.0);
    gwy_param_def_add_boolean(paramdef, PARAM_APPLY_OPENING, "apply_opening", _("Apply opening _filter"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_OPENING_SIZE, "opening_size", _("Si_ze"), 1, 250, 20);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Spread"), 0.0, 1.0, 0.5);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
disc_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Random Discs"));
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

    gwy_param_table_append_header(table, -1, _("Discs"));
    gwy_param_table_append_slider(table, PARAM_RADIUS_INIT);
    gwy_param_table_slider_set_mapping(table, PARAM_RADIUS_INIT, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_RADIUS_INIT);
    gwy_param_table_append_slider(table, PARAM_RADIUS_INIT_NOISE);
    gwy_param_table_append_slider(table, PARAM_RADIUS_MIN);
    gwy_param_table_slider_set_mapping(table, PARAM_RADIUS_MIN, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_RADIUS_MIN);
    gwy_param_table_append_slider(table, PARAM_SEPARATION);
    gwy_param_table_slider_set_mapping(table, PARAM_SEPARATION, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_SEPARATION);

    gwy_param_table_append_header(table, -1, _("Tiles"));
    gwy_param_table_append_checkbox(table, PARAM_MAKE_TILES);
    gwy_param_table_append_slider(table, PARAM_GAP_THICKNESS);
    gwy_param_table_slider_set_mapping(table, PARAM_GAP_THICKNESS, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_GAP_THICKNESS);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_APPLY_OPENING);
    gwy_param_table_append_slider(table, PARAM_OPENING_SIZE);
    gwy_param_table_set_unitstr(table, PARAM_OPENING_SIZE, _("px"));

    gwy_param_table_append_header(table, -1, _("Height"));
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
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
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
        static const gint xyids[] = { PARAM_RADIUS_INIT, PARAM_RADIUS_MIN, PARAM_SEPARATION, PARAM_GAP_THICKNESS };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }

    if (id < 0 || id == PARAM_MAKE_TILES || id == PARAM_APPLY_OPENING) {
        gboolean make_tiles = gwy_params_get_boolean(params, PARAM_MAKE_TILES);
        gboolean apply_opening = make_tiles && gwy_params_get_boolean(params, PARAM_APPLY_OPENING);

        gwy_param_table_set_sensitive(table, PARAM_GAP_THICKNESS, make_tiles);
        gwy_param_table_set_sensitive(table, PARAM_APPLY_OPENING, make_tiles);
        gwy_param_table_set_sensitive(table, PARAM_OPENING_SIZE, apply_opening);
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

static inline gdouble
cyclic_prod1(gdouble x1, gdouble x2, gdouble x3, gdouble y1, gdouble y2, gdouble y3)
{
    return x1*(y3 - y2) + x2*(y1 - y3) + x3*(y2 - y1);
}

static inline gdouble
cyclic_prod2(gdouble x1, gdouble x2, gdouble x3, gdouble y1, gdouble y2, gdouble y3)
{
    return x1*(y3 - y2)*(y3 + y2) + x2*(y1 - y3)*(y1 + y3) + x3*(y2 - y1)*(y2 + y1);
}

static inline gdouble
cyclic_prod3(gdouble x1, gdouble x2, gdouble x3)
{
    return (x3 - x2)*(x2 - x1)*(x1 - x3);
}

static inline gdouble
symm_sum2(gdouble x1, gdouble x2, gdouble x3)
{
    return x1*x1 + x2*x2 + x3*x3;
}

static gboolean
solve_apollonius_problem(const Disc *a, const Disc *b, const Disc *c, Disc *solution)
{
    gdouble det, m;
    gdouble Ax, Bx, Ay, By;
    gdouble Sx, Sy, Sr, Qx, Qy, Qr;
    gdouble alpha, beta, gamma, D;
    gdouble ax = a->x, ay = a->y, ar = a->r, bx = b->x, by = b->y, br = b->r, cx = c->x, cy = c->y, cr = c->r;

    det = cyclic_prod1(ax, bx, cx, ay, by, cy);
    m = symm_sum2(1.0/ar, 1.0/br, 1.0/cr);

    /* Almost collinear discs.  We might calculate the disc touching them, but it would be too huge anyway. */
    if (fabs(det) < 1e-10*m)
        return FALSE;

    /* Calculate constants A, B such that x = Ax + Bx*r, y = Ay + By*r */
    Ax = 0.5/det*(cyclic_prod2(ay, by, cy, ar, br, cr)
                  - cyclic_prod2(ay, by, cy, ax, bx, cx)
                  - cyclic_prod3(ay, by, cy));
    Bx = 1.0/det*cyclic_prod1(ay, by, cy, ar, br, cr);
    Ay = -0.5/det*(cyclic_prod2(ax, bx, cx, ar, br, cr)
                   - cyclic_prod2(ax, bx, cx, ay, by, cy)
                   - cyclic_prod3(ax, bx, cx));
    By = -1.0/det*cyclic_prod1(ax, bx, cx, ar, br, cr);

    /* Formulate quadratic equation for r. */
    Sx = ax + bx + cx;
    Sy = ay + by + cy;
    Sr = ar + br + cr;
    Qx = symm_sum2(ax, bx, cx);
    Qy = symm_sum2(ay, by, cy);
    Qr = symm_sum2(ar, br, cr);
    alpha = 3.0*(Bx*Bx + By*By - 1.0);
    beta = 2.0*Bx*(3.0*Ax - Sx) + 2.0*By*(3.0*Ay - Sy) - 2.0*Sr;
    gamma = Ax*(3.0*Ax - 2.0*Sx) + Ay*(3.0*Ay - 2.0*Sy) + (Qx + Qy - Qr);

    if (alpha < 0.0) {
        alpha = -alpha;
        beta = -beta;
        gamma = -gamma;
    }

    D = beta*beta - 4.0*alpha*gamma;
    if (D <= 0.0)
        return FALSE;

    solution->r = -2.0*gamma/(beta + sqrt(D));

    /* When we have the positive solution for r, just use A and B to obtain the coordinates. */
    solution->x = Ax + Bx*solution->r;
    solution->y = Ay + By*solution->r;
    return TRUE;
}

/* Calculate the minimum distance between any pair of periodic images of discs a and b.  */
static inline gdouble
discs_centre_distance(const Disc *a, const Disc *b, gdouble xreal, gdouble yreal)
{
    gdouble dx = fmod(a->x - b->x + 2.5*xreal, xreal) - 0.5*xreal;
    gdouble dy = fmod(a->y - b->y + 2.5*yreal, yreal) - 0.5*yreal;
    return sqrt(dx*dx + dy*dy);
}

/* Check if we can add candidate @c. */
static gboolean
candidate_is_admissible(GArray *discs, const Disc *c,
                        gdouble xreal, gdouble yreal, gdouble minr, gdouble maxr, gdouble separation)
{
    guint i, n;

    /* This does not need to be precise; we will weed out any other copies when we use one of them. */
    if (c->x < -1e-3 || c->x > xreal + 1e-3 || c->y < -1e-3 || c->y > yreal + 1e-3)
        return FALSE;
    if (c->r < minr + separation)
        return FALSE;
    if (c->r > maxr + separation)
        return FALSE;

    n = discs->len;
    for (i = 0; i < n; i++) {
        const Disc *d = &g_array_index(discs, Disc, i);
        gdouble dist = discs_centre_distance(c, d, xreal, yreal);
        if (dist + 0.1 < d->r + c->r || dist < d->r + minr + separation)
            return FALSE;
    }
    return TRUE;
}

/* Does NOT preserve order in @candidates.  We expect to do more operations with the @candidates and sort it at the
 * end. */
static void
remove_inadmissible_candidates(GArray *candidates, const Disc *d,
                               gdouble xreal, gdouble yreal, gdouble minr, gdouble separation)
{
    guint i = 0;

    while (i < candidates->len) {
        const Disc *c = &g_array_index(candidates, Disc, i);
        gdouble dist = discs_centre_distance(c, d, xreal, yreal);
        if (dist + 0.1 < d->r + c->r + separation || dist < d->r + minr + separation)
            g_array_remove_index_fast(candidates, i);
        else
            i++;
    }
}

static gint
compare_candidates(gconstpointer pa, gconstpointer pb)
{
    const Disc *a = (const Disc*)pa;
    const Disc *b = (const Disc*)pb;

    if (a->r > b->r)
        return -1;
    if (a->r < b->r)
        return 1;
    /* Do not care about the ordering if radii are equal. */
    return 0;
}

/* Find combination of movements of candidates to neighbour rectangles which do not distribute them too wildly.  At
 * least one must be still the base rectangle and they must still be neighbours.  If they are 2 steps apart then there
 * is another (closer) copy of the same disc between. */
static void
find_good_candidate_shifts(GArray *shifts, gdouble xreal, gdouble yreal)
{
    gint ix, iy, jx, jy, kx, ky;
    gdouble oneshift[6];

    g_array_set_size(shifts, 0);
    for (iy = -1; iy <= 1; iy++) {
        for (ix = -1; ix <= 1; ix++) {
            for (jy = -1; jy <= 1; jy++) {
                if (jy - iy > 1 || jy - iy < -1)
                    continue;
                for (jx = -1; jx <= 1; jx++) {
                    if (jx - ix > 1 || jx - ix < -1)
                        continue;
                    for (ky = -1; ky <= 1; ky++) {
                        if (ky - iy > 1 || ky - iy < -1)
                            continue;
                        if (ky - jy > 1 || ky - jy < -1)
                            continue;
                        for (kx = -1; kx <= 1; kx++) {
                            if (kx - ix > 1 || kx - ix < -1)
                                continue;
                            if (kx - jx > 1 || kx - jx < -1)
                                continue;
                            if ((iy || ix) && (jy || jx) && (ky || kx))
                                continue;

                            oneshift[0] = iy*yreal;
                            oneshift[1] = ix*xreal;
                            oneshift[2] = jy*yreal;
                            oneshift[3] = jx*xreal;
                            oneshift[4] = ky*yreal;
                            oneshift[5] = kx*xreal;
                            g_array_append_vals(shifts, oneshift, 6);
                        }
                    }
                }
            }
        }
    }
}

static void
filter_productive_pairs(GArray *ppairs, gdouble maxgap)
{
    guint i = 0;

    while (i < ppairs->len) {
        if (g_array_index(ppairs, ProductivePair, i).gap > maxgap + 0.1)
            g_array_remove_index_fast(ppairs, i);
        else
            i++;
    }
}

static void
check_and_add_productive_pair(GArray *discs, GArray *ppairs,
                              guint i, guint j,
                              gdouble xreal, gdouble yreal, gdouble maxgap)
{
    const Disc *a = &g_array_index(discs, Disc, i);
    const Disc *b = &g_array_index(discs, Disc, j);
    gdouble d, gap;

    g_assert(j >= i);

    if (G_LIKELY(i != j))
        d = discs_centre_distance(a, b, xreal, yreal);
    else
        d = fmin(xreal, yreal);

    gap = d - a->r - b->r;
    if (gap <= maxgap + 0.1) {
        ProductivePair ppair = { i, j, gap };
        g_array_append_val(ppairs, ppair);
    }
}

static void
add_productive_pairs_with_new_disc(GArray *discs, GArray *ppairs,
                                   gdouble xreal, gdouble yreal, gdouble maxgap)
{
    guint i, n = discs->len;

    if (n < 2)
        return;
    for (i = 0; i < n; i++)
        check_and_add_productive_pair(discs, ppairs, i, n-1, xreal, yreal, maxgap);
}

static void
find_productive_pairs(GArray *ppairs, GArray *discs,
                      gdouble xreal, gdouble yreal, gdouble maxgap)
{
    guint n, i, j;

    n = discs->len;
    for (i = 0; i < n; i++) {
        for (j = i; j < n; j++)
            check_and_add_productive_pair(discs, ppairs, i, j, xreal, yreal, maxgap);
    }
}

static gboolean
discs_are_too_far(const Disc *a, const Disc *b, gdouble maxgap)
{
    gdouble dx = a->x - b->x, dy = a->y - b->y;
    gdouble d2 = dx*dx + dy*dy;
    gdouble dmax = a->r + b->r + maxgap + 0.1;

    return d2 > dmax*dmax;
}

static void
find_candidates_one_triplet(GArray *discs, GArray *candidates, GArray *shifts,
                            guint i, guint j, guint k,
                            gdouble xreal, gdouble yreal, gdouble minr, gdouble maxr, gdouble separation)
{
    Disc a, b, c, aimage, bimage, cimage, d;
    gdouble maxgap = 2.0*(maxr + separation);
    guint s, ns;

    a = g_array_index(discs, Disc, i);
    b = g_array_index(discs, Disc, j);
    c = g_array_index(discs, Disc, k);
    aimage.r = a.r;
    bimage.r = b.r;
    cimage.r = c.r;
    ns = shifts->len/6;
    for (s = 0; s < ns; s++) {
        const gdouble *ss = &g_array_index(shifts, gdouble, 6*s);
        aimage.y = a.y + ss[0];
        aimage.x = a.x + ss[1];
        bimage.y = b.y + ss[2];
        bimage.x = b.x + ss[3];
        cimage.y = c.y + ss[4];
        cimage.x = c.x + ss[5];
        if (discs_are_too_far(&aimage, &bimage, maxgap)
            || discs_are_too_far(&bimage, &cimage, maxgap)
            || discs_are_too_far(&cimage, &aimage, maxgap))
            continue;
        /* This produces full-size disc actually touching the other ones.  The subsequent admissibility check needs to
         * take into account the radius is larger by separation. */
        if (!solve_apollonius_problem(&aimage, &bimage, &cimage, &d))
            continue;
        if (!candidate_is_admissible(discs, &d, xreal, yreal, minr, maxr, separation))
            continue;
        d.r -= separation;
        g_array_append_val(candidates, d);
    }
}

static void
find_initial_candidates(GArray *discs, GArray *candidates, GArray *shifts,
                        gdouble xreal, gdouble yreal, gdouble minr, gdouble separation)
{
    guint i, j, k, n;
    gulong *counts;

    n = discs->len;
    g_array_set_size(candidates, 0);
    /* Parallelise by splitting on i, giving each thread its own candidate array.  Except that i is a non-linear
     * index; we do much less work for larger i, so linearise it. */
    counts = g_new0(gulong, n);
    for (i = 0; i < n; i++) {
        for (j = i; j < n; j++)
            counts[i] += n+1 - j;
    }
    for (i = 1; i < n; i++)
        counts[i] += counts[i-1];

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(discs,candidates,shifts,counts,n,xreal,yreal,minr,separation) \
            private(i,j,k)
#endif
    {
        GArray *tcand = g_array_new(FALSE, FALSE, sizeof(Disc));
        gulong workfrom = gwy_omp_chunk_start(counts[n-1]);
        gulong workto = gwy_omp_chunk_end(counts[n-1]);
        guint ifrom = 0, ito = n;
        gdouble maxr = 0.5*fmin(xreal, yreal);

        for (i = 0; i < n; i++) {
            if (counts[i] > workfrom) {
                ifrom = i;
                break;
            }
        }
        for (; i < n; i++) {
            if (counts[i] > workto) {
                ito = i;
                break;
            }
        }

        for (i = ifrom; i < ito; i++) {
            for (j = i; j < n; j++) {
                for (k = j; k < n; k++)
                    find_candidates_one_triplet(discs, tcand, shifts, i, j, k, xreal, yreal, minr, maxr, separation);
            }
        }
#ifdef _OPENMP
#pragma omp critical
#endif
        {
            g_array_append_vals(candidates, tcand->data, tcand->len);
        }
        g_array_free(tcand, TRUE);
    }
    g_free(counts);

    g_array_sort(candidates, compare_candidates);
}

static void
commit_one_candidate(GArray *discs, GArray *ppairs, GArray *candidates, GArray *shifts,
                     gdouble xreal, gdouble yreal, gdouble minr, gdouble separation)
{
    guint i, nc = candidates->len, nd = discs->len, np = ppairs->len;
    gdouble maxr, maxgap;

    g_return_if_fail(nc);

    /* The next candidate cannot be much than this one. */
    maxr = g_array_index(candidates, Disc, 0).r;
    g_array_append_vals(discs, &g_array_index(candidates, Disc, 0), 1);
    g_array_remove_index_fast(candidates, 0);
    remove_inadmissible_candidates(candidates, &g_array_index(discs, Disc, nd), xreal, yreal, minr, separation);
    for (i = 0; i < np; i++) {
        const ProductivePair *ppair = &g_array_index(ppairs, ProductivePair, i);
        find_candidates_one_triplet(discs, candidates, shifts, ppair->i, ppair->j, nd,
                                    xreal, yreal, minr, maxr, separation);
    }

    if (!candidates->len)
        return;

    /* Remove productive pairs too far for the new maximum radius.
     * Add productive pairs formed with the new disc. */
    g_array_sort(candidates, compare_candidates);
    maxr = g_array_index(candidates, Disc, 0).r;
    maxgap = 2.0*(maxr + separation);
    filter_productive_pairs(ppairs, maxgap);
    add_productive_pairs_with_new_disc(discs, ppairs, xreal, yreal, maxgap);
}

static void
generate_discs(GArray *discs, gdouble xreal, gdouble yreal, gdouble minr, gdouble separation)
{
    GArray *candidates = g_array_new(FALSE, FALSE, sizeof(Disc));
    GArray *shifts = g_array_new(FALSE, FALSE, sizeof(gdouble));
    GArray *ppairs = g_array_new(FALSE, FALSE, sizeof(ProductivePair));
    const Disc *d;

    find_good_candidate_shifts(shifts, xreal, yreal);
    find_initial_candidates(discs, candidates, shifts, xreal, yreal, minr, separation);
    if (candidates->len) {
        d = &g_array_index(candidates, Disc, 0);
        find_productive_pairs(ppairs, discs, xreal, yreal, 2.0*(separation + d->r));
    }
    while (candidates->len) {
        commit_one_candidate(discs, ppairs, candidates, shifts, xreal, yreal, minr, separation);
        d = &g_array_index(discs, Disc, discs->len-1);
    }
    g_array_free(candidates, TRUE);
    g_array_free(ppairs, TRUE);
    g_array_free(shifts, TRUE);
}

static void
circular_area_fill_periodic(GwyDataField *data_field, gint col, gint row,
                            gdouble radius, gdouble value)
{
    gint i, j, ii, jj, r, r2, xres, yres, jfrom, jto;
    gdouble *d, *drow;
    gdouble s;

    if (radius < 0.0)
        return;

    r2 = floor(radius*radius + 1e-12);
    r = floor(radius + 1e-12);
    xres = data_field->xres;
    yres = data_field->yres;
    d = data_field->data;

    /* Ensure circle centre inside the base area. */
    col %= xres;
    if (col < 0)
        col += xres;

    row %= yres;
    if (row < 0)
        row += yres;

    for (i = -r; i <= r; i++) {
        ii = (i + row + yres) % yres;
        drow = d + ii*xres;

        s = sqrt(r2 - i*i);
        jfrom = col + ceil(-s);
        jto = col + floor(s);
        /* If we are to fill the entire row just do it simply.  As a benefit,
         * we can assume we fill less than one entire row in the other cases. */
        if (jto+1 - jfrom >= xres) {
            for (j = xres; j; j--, drow++)
                *drow = value;
        }
        /* Sticking outside to the left. */
        else if (jfrom < 0) {
            for (j = 0; j <= jto; j++)
                drow[j] = value;
            jj = (jfrom + xres) % xres;
            for (j = jj; j < xres; j++)
                drow[j] = value;
        }
        /* Sticking outside to the right. */
        else if (jto >= xres) {
            jj = (jto + 1) % xres;
            for (j = 0; j < jj; j++)
                drow[j] = value;
            for (j = jfrom; j < xres; j++)
                drow[j] = value;
        }
        /* Entirely inside. */
        else {
            for (j = jfrom; j <= jto; j++)
                drow[j] = value;
        }
    }
}

static void
generate_seed_discs(ModuleArgs *args, GwyDataField *field, GArray *discs, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble separation = gwy_params_get_double(params, PARAM_SEPARATION);
    gdouble radius_init = gwy_params_get_double(params, PARAM_RADIUS_INIT);
    gdouble radius_init_noise = gwy_params_get_double(params, PARAM_RADIUS_INIT_NOISE);
    gint i, xres, yres, failcount;
    GRand *rng;

    rng = gwy_rand_gen_set_rng(rngset, RNG_POSITION);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_xres(field);
    gwy_data_field_fill(field, 1.0);

    failcount = 0;
    while (failcount < 15) {
        Disc d;

        d.x = g_rand_double(rng)*xres;
        d.y = g_rand_double(rng)*yres;
        d.r = radius_init;
        if (radius_init_noise)
            d.r *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_RADIUS_INIT, radius_init_noise));

        for (i = 0; i < discs->len; i++) {
            const Disc *p = &g_array_index(discs, Disc, i);
            gdouble dx, dy, s;

            if (d.x <= p->x)
                dx = MIN(p->x - d.x, d.x + xres - p->x);
            else
                dx = MIN(d.x - p->x, p->x + xres - d.x);

            if (d.y <= p->y)
                dy = MIN(p->y - d.y, d.y + yres - p->y);
            else
                dy = MIN(d.y - p->y, p->y + yres - d.y);

            s = d.r + p->r + separation;
            if (dx*dx + dy*dy <= s*s)
                break;
        }
        if (i == discs->len) {
            g_array_append_val(discs, d);
            failcount = 0;
        }
        else
            failcount++;
    }
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gboolean make_tiles = gwy_params_get_boolean(params, PARAM_MAKE_TILES);
    gboolean apply_opening = make_tiles && gwy_params_get_boolean(params, PARAM_APPLY_OPENING);
    gdouble radius_min = gwy_params_get_double(params, PARAM_RADIUS_MIN);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble separation = gwy_params_get_double(params, PARAM_SEPARATION);
    gdouble gap_thickness = gwy_params_get_double(params, PARAM_GAP_THICKNESS);
    gint opening_size = gwy_params_get_int(params, PARAM_OPENING_SIZE);
    GwyDataField *discfield = args->result, *workspace = NULL, *shifted;
    guint xres, yres, ext, i, gno;
    gint ngrains, power10z;
    GwyRandGenSet *rngset;
    GArray *discs;
    gint *grains;
    gdouble *heights, *d;

    xres = gwy_data_field_get_xres(discfield);
    yres = gwy_data_field_get_yres(discfield);

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    discs = g_array_new(FALSE, FALSE, sizeof(Disc));
    generate_seed_discs(args, discfield, discs, rngset);
    generate_discs(discs, 1.0*xres, 1.0*yres, radius_min, separation);
    gwy_data_field_fill(discfield, 1.0);
    ext = 0;
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(max:ext) \
            shared(discs,discfield) \
            private(i)
#endif
    for (i = 0; i < discs->len; i++) {
        const Disc *p = &g_array_index(discs, Disc, i);

        circular_area_fill_periodic(discfield, floor(p->x), floor(p->y), p->r, 0.0);
        ext = MAX(ext, (gint)ceil(p->r));
    }

    if (height_noise) {
        /* Recycle r (which is no longer needed) for heights. */
        for (i = 0; i < discs->len; i++) {
            Disc *p = &g_array_index(discs, Disc, i);
            gdouble z = gwy_rand_gen_set_gaussian(rngset, RNG_HEIGHT, height_noise);
            p->r = sqrt(z*z + 1.0) + z;   /* To (0, ∞) */
        }
    }

    if (apply_opening)
        ext = MAX(ext, 4*opening_size/3 + 1);
    gwy_data_field_invalidate(discfield);

    if (make_tiles) {
        workspace = gwy_data_field_extend(discfield, ext, ext, ext, ext, GWY_EXTERIOR_PERIODIC, 0.0, FALSE);
        gwy_data_field_grains_invert(workspace);
        gwy_data_field_grains_grow(workspace, 0.5*MIN(xres, yres), GWY_DISTANCE_TRANSFORM_EUCLIDEAN, TRUE);
        gwy_data_field_grains_invert(workspace);
        if (gap_thickness >= 2.0) {
            gwy_data_field_grains_grow(workspace, 0.7*gap_thickness, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);
            gwy_data_field_grains_shrink(workspace, 0.2*gap_thickness, GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);
        }
        gwy_data_field_grains_invert(workspace);
    }
    else
        gwy_data_field_grains_invert(discfield);

    if (apply_opening) {
        shifted = gwy_data_field_new(opening_size, opening_size, opening_size, opening_size, TRUE);
        gwy_data_field_elliptic_area_fill(shifted, 0, 0, opening_size, opening_size, 1.0);
        gwy_data_field_area_filter_min_max(workspace, shifted, GWY_MIN_MAX_FILTER_OPENING,
                                           0, 0, xres + 2*ext, yres + 2*ext);
        g_object_unref(shifted);
    }
    if (make_tiles)
        gwy_data_field_area_copy(workspace, discfield, ext, ext, xres, yres, 0, 0);

    if (height_noise) {
        grains = g_new0(gint, xres*yres);
        ngrains = gwy_data_field_number_grains_periodic(discfield, grains);
        heights = g_new0(gdouble, ngrains+1);
        heights[0] = 0.0;

        /* First assign heights to all grains which contain the original disc centre.  Conflicts are resolved on the
         * first come first served basis since it (usually) means larger discs have precedence.  */
        for (i = 0; i < discs->len; i++) {
            Disc *p = &g_array_index(discs, Disc, i);
            gint col = GWY_ROUND(p->x), row = GWY_ROUND(p->y);
            col = CLAMP(col, 0, xres-1);
            row = CLAMP(row, 0, yres-1);
            if ((gno = grains[row*xres + col]) && !heights[gno])
                heights[gno] = p->r;
        }
        /* Generate the remaining heights arbitrarily. */
        for (gno = 1; gno <= ngrains; gno++) {
            if (!heights[gno]) {
                gdouble z = gwy_rand_gen_set_gaussian(rngset, RNG_HEIGHT, height_noise);
                heights[gno] = sqrt(z*z + 1.0) + z;   /* To (0, ∞) */
            }
        }

        d = gwy_data_field_get_data(discfield);
        for (i = 0; i < xres*yres; i++)
            d[i] = heights[grains[i]];
        g_free(heights);
        g_free(grains);
    }
    gwy_data_field_multiply(discfield, height);

    if (args->field && do_initialise)
        gwy_data_field_sum_fields(discfield, discfield, args->field);

    g_array_free(discs, TRUE);
    gwy_rand_gen_set_free(rngset);

    GWY_OBJECT_UNREF(workspace);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
