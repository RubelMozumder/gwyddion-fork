/*
 *  $Id: pileup_synth.c 24448 2021-11-01 14:19:24Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define DECLARE_OBJECT(name) \
    static gdouble render_base_##name(gdouble x, gdouble y, gdouble aspect); \
    static gboolean intersect_##name(GwyXYZ *ptl, GwyXYZ *ptu, gdouble aspect); \
    static gdouble getcov_##name(gdouble aspect)

#define OBJECT_FUNCS(name) \
    render_base_##name, intersect_##name, getcov_##name

typedef enum {
    RNG_ID,
    RNG_WIDTH,
    RNG_ASPECT,
    RNG_ANGLE,
    RNG_NRNGS
} PileupSynthRng;

typedef enum {
    PILEUP_SYNTH_ELLIPSOID = 0,
    PILEUP_SYNTH_BAR       = 1,
    PILEUP_SYNTH_CYLINDER  = 2,
    PILEUP_SYNTH_NUGGET    = 3,
    PILEUP_SYNTH_HEXAGONAL = 4,
} PileupSynthType;

enum {
    PARAM_TYPE,
    PARAM_STICKOUT,
    PARAM_AVOID_STACKING,
    PARAM_WIDTH,
    PARAM_WIDTH_NOISE,
    PARAM_ASPECT,
    PARAM_ASPECT_NOISE,
    PARAM_ANGLE,
    PARAM_ANGLE_NOISE,
    PARAM_COVERAGE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    INFO_COVERAGE_OBJECTS,

    PARAM_DIMS0
};

/* Avoid reallocations by using a single buffer for objects that can only grow. */
typedef struct {
    gint xres;
    gint yres;
    gsize size;
    gdouble *lower;   /* lower surface, as positive */
    gdouble *upper;   /* upper surface */
} PileupSynthObject;

typedef gdouble  (*PileupBaseFunc)     (gdouble x, gdouble y, gdouble aspect);
typedef gboolean (*PileupIntersectFunc)(GwyXYZ *ptl, GwyXYZ *ptu, gdouble aspect);
typedef gdouble  (*GetCoverageFunc)    (gdouble aspect);

typedef struct {
    const gchar *name;
    PileupBaseFunc render_base;
    PileupIntersectFunc intersect;
    GetCoverageFunc get_coverage;
} PileupSynthFeature;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
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
static void             pileup_synth        (GwyContainer *data,
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
static void             pileup_synth_iter   (ModuleArgs *args,
                                             GwyDataField *surface,
                                             gboolean *seen,
                                             PileupSynthObject *object,
                                             GwyRandGenSet *rngset,
                                             gint nxcells,
                                             gint nycells,
                                             gint xoff,
                                             gint yoff,
                                             gint nobjects,
                                             gint *indices);
static void             pileup_one_object   (PileupSynthObject *object,
                                             GwyDataField *surface,
                                             gboolean *seen,
                                             PileupBaseFunc render_base,
                                             PileupIntersectFunc intersect,
                                             gdouble width,
                                             gdouble length,
                                             gdouble angle,
                                             gdouble stickout,
                                             gint j,
                                             gint i);
static gboolean         check_seen          (gboolean *seen,
                                             gint xres,
                                             gint yres,
                                             PileupSynthObject *object,
                                             gint joff,
                                             gint ioff);
static glong            calculate_n_objects (ModuleArgs *args,
                                             guint xres,
                                             guint yres);

DECLARE_OBJECT(ellipsoid);
DECLARE_OBJECT(bar);
DECLARE_OBJECT(cylinder);
DECLARE_OBJECT(nugget);
DECLARE_OBJECT(hexagonal);

/* NB: The order of these and everything else (like table_noise[]) must match the enums.  See obj_synth.c for how
 * to reorder it in the GUI. */
static const PileupSynthFeature features[] = {
    { N_("Ellipsoids"),     OBJECT_FUNCS(ellipsoid), },
    { N_("Bars"),           OBJECT_FUNCS(bar),       },
    { N_("Cylinders"),      OBJECT_FUNCS(cylinder),  },
    { N_("Nuggets"),        OBJECT_FUNCS(nugget),    },
    { N_("Hexagonal rods"), OBJECT_FUNCS(hexagonal), },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates randomly patterned surfaces by piling up geometrical shapes."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, pileup_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("pileup_synth",
                              (GwyProcessFunc)&pileup_synth,
                              N_("/S_ynthetic/_Deposition/_Pile Up..."),
                              GWY_STOCK_SYNTHETIC_PILEUP,
                              RUN_MODES,
                              0,
                              N_("Generate surface of randomly piled up shapes"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *types = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    types = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(features), features, sizeof(PileupSynthFeature),
                                      G_STRUCT_OFFSET(PileupSynthFeature, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Shape"),
                              types, G_N_ELEMENTS(features), PILEUP_SYNTH_ELLIPSOID);
    gwy_param_def_add_double(paramdef, PARAM_STICKOUT, "stickout", _("Colum_narity"), -1.0, 1.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_AVOID_STACKING, "avoid_stacking", _("_Avoid stacking"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH, "width", _("_Width"), 1.0, 1000.0, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH_NOISE, "width_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ASPECT, "aspect", _("_Aspect ratio"), 1.0, 8.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ASPECT_NOISE, "aspect_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ANGLE_NOISE, "angle_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 1e-4, 200.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
pileup_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Pile Up Shapes"));
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
    gwy_synth_append_dimensions_to_param_table(gui->table_dimensions, GWY_SYNTH_FIXED_ZUNIT);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), gui->table_dimensions);

    return gwy_param_table_widget(gui->table_dimensions);
}

static GtkWidget*
generator_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_generator = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_combo(table, PARAM_TYPE);
    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_append_info(table, INFO_COVERAGE_OBJECTS, _("Number of objects"));
    gwy_param_table_append_separator(table);

    gwy_param_table_append_header(table, -1, _("Size"));
    gwy_param_table_append_slider(table, PARAM_WIDTH);
    gwy_param_table_slider_add_alt(table, PARAM_WIDTH);
    gwy_param_table_slider_set_mapping(table, PARAM_WIDTH, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_WIDTH_NOISE);

    gwy_param_table_append_header(table, -1, _("Aspect Ratio"));
    gwy_param_table_append_slider(table, PARAM_ASPECT);
    gwy_param_table_append_slider(table, PARAM_ASPECT_NOISE);

    gwy_param_table_append_header(table, -1, _("Placement"));
    gwy_param_table_append_slider(table, PARAM_STICKOUT);
    gwy_param_table_slider_set_mapping(table, PARAM_STICKOUT, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_AVOID_STACKING);

    gwy_param_table_append_header(table, -1, _("Orientation"));
    gwy_param_table_append_slider(table, PARAM_ANGLE);
    gwy_param_table_append_slider(table, PARAM_ANGLE_NOISE);

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

    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_WIDTH };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0
        || id == PARAM_TYPE || id == PARAM_WIDTH || id == PARAM_WIDTH_NOISE
        || id == PARAM_ASPECT || id == PARAM_COVERAGE) {
        gint xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YRES);
        glong nobj = calculate_n_objects(gui->args, xres, yres);
        gchar *s = g_strdup_printf("%ld", nobj);

        gwy_param_table_info_set_valuestr(gui->table_generator, INFO_COVERAGE_OBJECTS, s);
        g_free(s);
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
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

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gboolean avoid_stacking = gwy_params_get_boolean(params, PARAM_AVOID_STACKING);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gdouble aspect = gwy_params_get_double(params, PARAM_ASPECT);
    GwyDataField *field = args->result, *workspace = NULL;
    gint xres, yres, nxcells, nycells, ncells, cellside, niters, i, extend;
    PileupSynthObject object = { 0, 0, 0, NULL, NULL };
    GwyRandGenSet *rngset;
    gboolean *seen = NULL;
    gint *indices;
    glong nobjects;
    gdouble h;

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    h = sqrt(gwy_data_field_get_dx(field) * gwy_data_field_get_dy(field));
    if (args->field && do_initialise) {
        /* Scale initial surface to pixel-sized cubes.  We measure all shape parameters in pixels.   This effectiely
         * mean scaling real x and y coordinates by 1/h.  So we must do the same with z.  */
        gwy_data_field_copy(args->field, field, FALSE);
        gwy_data_field_multiply(field, 1.0/h);
        /* When adding objects to existing surface which is not level the shapes spill across boundaries.  Prevent
         * that. This means there is no parity between standalone and add-to-surface object sets, but this is not
         * a smooth change anyway.  */
        extend = GWY_ROUND(0.6*width*aspect);
        workspace = gwy_data_field_extend(field, extend, extend, extend, extend, GWY_EXTERIOR_BORDER_EXTEND, 0.0,
                                          FALSE);
        GWY_SWAP(GwyDataField*, field, workspace);
    }
    else
        gwy_data_field_clear(field);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    cellside = sqrt(sqrt(xres*yres));
    nxcells = (xres + cellside-1)/cellside;
    nycells = (yres + cellside-1)/cellside;
    ncells = nxcells*nycells;
    nobjects = calculate_n_objects(args, xres, yres);
    niters = nobjects/ncells;

    if (avoid_stacking)
        seen = g_new0(gboolean, xres*yres);
    indices = g_new(gint, ncells);

    for (i = 0; i < niters; i++)
        pileup_synth_iter(args, field, seen, &object, rngset, nxcells, nycells, i+1, i+1, ncells, indices);
    pileup_synth_iter(args, field, seen, &object, rngset, nxcells, nycells, 0, 0, nobjects % ncells, indices);

    g_free(object.lower);
    g_free(indices);
    g_free(seen);

    /* Scale back to physical. */
    gwy_data_field_multiply(field, h);

    if (workspace) {
        GWY_SWAP(GwyDataField*, field, workspace);
        gwy_data_field_area_copy(workspace, field, extend, extend, xres - 2*extend, yres - 2*extend, 0, 0);
        g_object_unref(workspace);
    }

    /* The units must be the same. */
    if (args->field) {
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_xy(args->field));
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), gwy_data_field_get_si_unit_xy(args->field));
    }
    gwy_data_field_data_changed(field);
}

static void
pileup_synth_iter(ModuleArgs *args, GwyDataField *surface, gboolean *seen,
                  PileupSynthObject *object, GwyRandGenSet *rngset,
                  gint nxcells, gint nycells, gint xoff, gint yoff, gint nobjects,
                  gint *indices)
{
    GwyParams *params = args->params;
    PileupSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gdouble width_noise = gwy_params_get_double(params, PARAM_WIDTH_NOISE);
    gdouble aspect = gwy_params_get_double(params, PARAM_ASPECT);
    gdouble aspect_noise = gwy_params_get_double(params, PARAM_ASPECT_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    gdouble angle_noise = gwy_params_get_double(params, PARAM_ANGLE_NOISE);
    gdouble stickout = gwy_params_get_double(params, PARAM_STICKOUT);
    const PileupSynthFeature *feature = features + type;
    gint xres, yres, ncells, k;
    PileupBaseFunc render_base;
    PileupIntersectFunc intersect;
    GRand *rngid;

    g_return_if_fail(nobjects <= nxcells*nycells);

    render_base = feature->render_base;
    intersect = feature->intersect;
    xres = gwy_data_field_get_xres(surface);
    yres = gwy_data_field_get_yres(surface);
    ncells = nxcells*nycells;

    for (k = 0; k < ncells; k++)
        indices[k] = k;

    rngid = gwy_rand_gen_set_rng(rngset, RNG_ID);
    for (k = 0; k < nobjects; k++) {
        gdouble kwidth = width, kaspect = aspect, kangle = angle, length;
        gint id, i, j, from, to;

        id = g_rand_int_range(rngid, 0, ncells - k);
        i = indices[id]/nxcells;
        j = indices[id] % nxcells;
        indices[id] = indices[ncells-1 - k];

        if (width_noise)
            kwidth *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_WIDTH, width_noise));

        if (aspect_noise) {
            kaspect *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_ASPECT, aspect_noise));
            kaspect = fmax(kaspect, 1.0/kaspect);
        }
        length = kwidth*kaspect;

        if (angle_noise)
            angle += gwy_rand_gen_set_gaussian(rngset, RNG_ANGLE, 2*angle_noise);

        from = (j*xres + nxcells/2)/nxcells;
        to = (j*xres + xres + nxcells/2)/nxcells;
        to = MIN(to, xres);
        j = from + xoff + g_rand_int_range(rngid, 0, to - from);

        from = (i*yres + nycells/2)/nycells;
        to = (i*yres + yres + nycells/2)/nycells;
        to = MIN(to, yres);
        i = from + yoff + g_rand_int_range(rngid, 0, to - from);

        pileup_one_object(object, surface, seen, render_base, intersect, kwidth, length, kangle, stickout, j, i);
    }
}

static gboolean
check_seen(gboolean *seen, gint xres, gint yres,
           PileupSynthObject *object, gint joff, gint ioff)
{
    gint kxres, kyres, i, j;
    const gdouble *z;
    gboolean *srow, *end;

    kxres = object->xres;
    kyres = object->yres;

    z = object->upper;
    for (i = 0; i < kyres; i++) {
        srow = seen + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, z++, srow++) {
            if (*z && *srow)
               return FALSE;
            if (srow == end)
                srow -= xres;
        }
    }

    z = object->upper;
    for (i = 0; i < kyres; i++) {
        srow = seen + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, z++, srow++) {
            if (*z)
                *srow = TRUE;
            if (srow == end)
                srow -= xres;
        }
    }
    return TRUE;
}

static inline void
pileup_synth_object_resize(PileupSynthObject *object, gint xres, gint yres)
{
    object->xres = xres;
    object->yres = yres;
    if ((guint)(xres*yres) > object->size) {
        g_free(object->lower);
        object->lower = g_new(gdouble, 2*xres*yres);
        object->size = xres*yres;
    }
    object->upper = object->lower + xres*yres;
}

/* Rotate the xy plane to plane with slopes bx and by.
 * Quantities b and bh1 are precalculated: b = √(bx² + by²), bh1 = √(b² + 1). */
static inline void
tilt_point(GwyXYZ *v, gdouble bx, gdouble by, gdouble b, gdouble bh1)
{
    /* Use Rodrigues' rotation formula, factoring out 1/bh1 = cos ϑ. */
    GwyXYZ vrot;
    gdouble q;

    if (b < 1e-9)
        return;

    /* v cos ϑ */
    vrot = *v;

    /* k×v sin ϑ */
    vrot.x += bx*v->z;
    vrot.y += by*v->z;
    vrot.z -= bx*v->x + by*v->y;

    /* k (k.v) (1 - cos θ) */
    q = (bx*v->y - by*v->x)/(1.0 + bh1);
    vrot.x -= q*by;
    vrot.y += q*bx;

    /* Multiply with the common factor 1/bh1. */
    v->x = vrot.x/bh1;
    v->y = vrot.y/bh1;
    v->z = vrot.z/bh1;
}

/* Rotate the vertical plane by angle α (sine and cosine provided). */
static inline void
rotate_point(GwyXYZ *v, gdouble ca, gdouble sa)
{
    gdouble x, y;

    x = ca*v->x - sa*v->y;
    y = sa*v->x + ca*v->y;

    v->x = x;
    v->y = y;
}

/* Scale vector by given factors. */
static inline void
scale_point(GwyXYZ *v, gdouble xsize, gdouble ysize, gdouble height)
{
    v->x *= xsize;
    v->y *= ysize;
    v->z *= height;
}

static inline gdouble
find_base_level_melted(const PileupSynthObject *object, const gdouble *surface,
                       gint xres, gint yres, gint joff, gint ioff)
{
    gint kxres = object->xres, kyres = object->yres;
    gint i, j, w;
    const gdouble *srow, *end, *zl;
    gdouble m, v;

    zl = object->lower;
    w = 0;
    m = 0.0;
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, srow++) {
            v = *zl;
            if (v) {
                m += (*srow) + (*zl);
                w++;
            }
            if (srow == end)
                srow -= xres;
        }
    }
    if (!w)
        return 0.0;

    return m/w;
}

static inline gdouble
find_base_level_stickout(const PileupSynthObject *object, const gdouble *surface,
                         gint xres, gint yres, gint joff, gint ioff)
{
    gint kxres = object->xres, kyres = object->yres;
    gint i, j;
    const gdouble *srow, *end, *zl;
    gdouble m;

    zl = object->lower;
    m = -G_MAXDOUBLE;
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, srow++) {
            if (*zl && *srow + *zl > m)
                m = *srow + *zl;
            if (srow == end)
                srow -= xres;
        }
    }
    return (m == -G_MAXDOUBLE) ? 0.0 : m;
}

static inline gdouble
find_base_level_bury(const PileupSynthObject *object, const gdouble *surface,
                     gint xres, gint yres, gint joff, gint ioff)
{
    gint kxres = object->xres, kyres = object->yres;
    gint i, j;
    const gdouble *srow, *end, *zl;
    gdouble m;

    zl = object->lower;
    m = G_MAXDOUBLE;
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, srow++) {
            if (*zl && *srow < m)
                m = *srow;
            if (srow == end)
                srow -= xres;
        }
    }
    return (m == G_MAXDOUBLE) ? 0.0 : m;
}

static gdouble
find_base_level(const PileupSynthObject *object, const gdouble *surface,
                gint xres, gint yres, gint joff, gint ioff,
                gdouble stickout)
{
    gdouble m, mx, sa;

    if (stickout > 1.0 - 1e-6)
        return find_base_level_stickout(object, surface, xres, yres, joff, ioff);
    if (stickout < -1.0 + 1e-6)
        return find_base_level_bury(object, surface, xres, yres, joff, ioff);

    m = find_base_level_melted(object, surface, xres, yres, joff, ioff);
    sa = fabs(stickout);
    if (fabs(sa) < 1e-6)
        return m;

    if (stickout > 0.0)
        mx = find_base_level_stickout(object, surface, xres, yres, joff, ioff);
    else
        mx = find_base_level_bury(object, surface, xres, yres, joff, ioff);

    return sa*mx + (1.0 - sa)*m;
}

static inline void
find_weighted_mean_plane(const PileupSynthObject *object, const gdouble *surface,
                         gint xres, gint yres, gint joff, gint ioff,
                         gdouble *pbx, gdouble *pby)
{
    gint kxres = object->xres, kyres = object->yres;
    gint i, j;
    const gdouble *srow, *end, *zu, *zl;
    gdouble cx, cy, cz, x, y, z, sxz, syz, sxx, sxy, syy, w, v, D;

    zu = object->upper;
    zl = object->lower;
    cx = cy = cz = w = 0.0;
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, zu++, srow++) {
            v = *zl + *zu;
            w += v;
            cx += v*j;
            cy += v*i;
            cz += v*(*srow);
            if (srow == end)
                srow -= xres;
        }
    }
    if (!w) {
        *pbx = *pby = 0.0;
        return;
    }

    cx /= w;
    cy /= w;
    cz /= w;
    zu = object->upper;
    zl = object->lower;
    sxx = sxy = syy = sxz = syz = 0.0;
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, zu++, srow++) {
            v = *zl + *zu;
            x = j - cx;
            y = i - cy;
            z = *srow - cz;
            sxz += v*x*z;
            syz += v*y*z;
            sxx += v*x*x;
            sxy += v*x*y;
            syy += v*y*y;
            if (srow == end)
                srow -= xres;
        }
    }

    D = sxx*syy - sxy*sxy;
    if (fabs(D) > 1e-12*w*w) {
        *pbx = (sxz*syy - syz*sxy)/D;
        *pby = (syz*sxx - sxz*sxy)/D;
    }
    else
        *pbx = *pby = 0.0;
}

static void
make_tilted_bounding_box(PileupSynthObject *object,
                         gdouble width2, gdouble length2, gdouble angle, gdouble bx, gdouble by)
{
    gdouble ca, sa, b, bh1, xmin, xmax, ymin, ymax;
    gint xres, yres;
    GwyXYZ v;
    guint i;

    ca = cos(angle);
    sa = sin(angle);
    b = sqrt(bx*bx + by*by);
    bh1 = sqrt(b*b + 1.0);

    xmin = ymin = G_MAXDOUBLE;
    xmax = ymax = -G_MAXDOUBLE;

    for (i = 0; i < 8; i++) {
        v.x = (i & 1) ? 1.0 : -1.0;
        v.y = (i & 2) ? 1.0 : -1.0;
        v.z = (i & 4) ? 1.0 : -1.0;
        /* Do everything with opposite signs, i.e. compensate the un-. */
        scale_point(&v, length2, width2, width2);
        rotate_point(&v, ca, sa);
        tilt_point(&v, -bx, -by, b, bh1);
        if (v.x > xmax)
            xmax = v.x;
        if (v.x < xmin)
            xmin = v.x;
        if (v.y > ymax)
            ymax = v.y;
        if (v.y < ymin)
            ymin = v.y;
    }

    xres = 2*(gint)ceil(MAX(xmax, -xmin) + 1.0) | 1;
    yres = 2*(gint)ceil(MAX(ymax, -ymin) + 1.0) | 1;
    pileup_synth_object_resize(object, xres, yres);
}

static inline void
middle_point(const GwyXYZ *a, const GwyXYZ *b, GwyXYZ *c)
{
    c->x = 0.5*(a->x + b->x);
    c->y = 0.5*(a->y + b->y);
    c->z = 0.5*(a->z + b->z);
}

static inline void
point_on_line(const GwyXYZ *c, const GwyXYZ *v, gdouble t, GwyXYZ *pt)
{
    pt->x = c->x + t*v->x;
    pt->y = c->y + t*v->y;
    pt->z = c->z + t*v->z;
}

static inline void
vecdiff(const GwyXYZ *a, const GwyXYZ *b, GwyXYZ *c)
{
    c->x = a->x - b->x;
    c->y = a->y - b->y;
    c->z = a->z - b->z;
}

static inline gdouble
dotprod(const GwyXYZ *a, const GwyXYZ *b)
{
    return a->x*b->x + a->y*b->y + a->z*b->z;
}

static inline gdouble
vecnorm2(const GwyXYZ *a)
{
    return a->x*a->x + a->y*a->y + a->z*a->z;
}

/* We are only interested in equations with two real solutions. */
static inline gboolean
solve_quadratic(gdouble a, gdouble b, gdouble c, gdouble *t1, gdouble *t2)
{
    gdouble D, bsD;

    D = b*b - 4.0*a*c;
    if (D <= 0.0)
        return FALSE;

    if (b >= 0.0)
        bsD = -0.5*(sqrt(D) + b);
    else
        bsD = 0.5*(sqrt(D) - b);

    *t1 = c/bsD;
    *t2 = bsD/a;
    return TRUE;
}

static void
render_base_object(PileupSynthObject *object, PileupBaseFunc render_base,
                   gdouble width2, gdouble length2, gdouble angle)
{
    gdouble ca, sa, xc, yc, aspect;
    gint xres, yres, x, y, i, j;
    gdouble *zl, *zu;

    xres = object->xres;
    yres = object->yres;
    zl = object->lower;
    zu = object->upper;

    aspect = length2/width2;
    ca = cos(angle);
    sa = sin(angle);
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++, zl++, zu++) {
            x = j - xres/2;

            xc = (x*ca + y*sa)/length2;
            yc = (-x*sa + y*ca)/width2;
            *zl = *zu = render_base(xc, yc, aspect);
        }
    }
}

static void
render_general_shape(PileupSynthObject *object, PileupIntersectFunc intersect,
                     gdouble width2, gdouble length2, gdouble angle, gdouble bx, gdouble by)
{
    gdouble b, bh1, ca, sa, x, y, aspect;
    gint xres, yres, i, j;
    gdouble *zl, *zu;
    GwyXYZ ptl, ptu;

    xres = object->xres;
    yres = object->yres;
    zl = object->lower;
    zu = object->upper;

    aspect = length2/width2;
    ca = cos(angle);
    sa = sin(angle);
    b = sqrt(bx*bx + by*by);
    bh1 = sqrt(b*b + 1.0);

    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++, zl++, zu++) {
            x = j - xres/2;

            /* Choose a vertical line passing through the pixel, given by two points.  Transform coordinates to ones
             * where the shape is in the canonical position.  The line is no longer vertical but it remains a straight
             * line.  Find intersections with the shape.  Transform back.  The larger one becomes upper, the smaller
             * one -lower. */
            ptl.x = x;
            ptl.y = y;
            ptl.z = -5.0;
            tilt_point(&ptl, bx, by, b, bh1);
            rotate_point(&ptl, ca, -sa);
            scale_point(&ptl, 1.0/length2, 1.0/width2, 1.0/width2);

            ptu.x = x;
            ptu.y = y;
            ptu.z = 5.0;
            tilt_point(&ptu, bx, by, b, bh1);
            rotate_point(&ptu, ca, -sa);
            scale_point(&ptu, 1.0/length2, 1.0/width2, 1.0/width2);

            if (!intersect(&ptl, &ptu, aspect)) {
                *zl = *zu = 0.0;
                continue;
            }

            scale_point(&ptl, length2, width2, width2);
            rotate_point(&ptl, ca, sa);
            tilt_point(&ptl, -bx, -by, b, bh1);

            scale_point(&ptu, length2, width2, width2);
            rotate_point(&ptu, ca, sa);
            tilt_point(&ptu, -bx, -by, b, bh1);

            if (ptl.z <= ptu.z) {
                *zl = -ptl.z;
                *zu = ptu.z;
            }
            else {
                *zl = -ptu.z;
                *zu = ptl.z;
            }
        }
    }
}

static void
sculpt_up(const PileupSynthObject *object,
          gdouble *surface, gint xres, gint yres,
          gint joff, gint ioff, gdouble m)
{
    const gdouble *zu, *zl;
    gdouble *srow, *end;
    gint kxres, kyres, i, j;

    kxres = object->xres;
    kyres = object->yres;
    zl = object->lower;
    zu = object->upper;

    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, zl++, zu++, srow++) {
            if ((*zu || *zl) && *srow < *zu + m)
                *srow = *zu + m;
            if (srow == end)
                srow -= xres;
        }
    }
}

static void
pileup_one_object(PileupSynthObject *object, GwyDataField *surface, gboolean *seen,
                  PileupBaseFunc render_base, PileupIntersectFunc intersect,
                  gdouble width, gdouble length, gdouble angle, gdouble stickout,
                  gint j, gint i)
{
    gint xres, yres, ioff, joff;
    gdouble *d;
    gdouble m, bx, by, width2, length2;

    xres = gwy_data_field_get_xres(surface);
    yres = gwy_data_field_get_yres(surface);
    d = gwy_data_field_get_data(surface);

    /* We prefer to work with half-axes, i.e. have the base bounding box
     * [-1,-1,-1] to [1,1,1]. */
    length2 = 0.5*length;
    width2 = 0.5*width;

    make_tilted_bounding_box(object, width2, length2, -angle, 0.0, 0.0);
    render_base_object(object, render_base, width2, length2, -angle);

    /* Recalculate centre to corner position. */
    joff = (j - object->xres/2 + 16384*xres) % xres;
    ioff = (i - object->yres/2 + 16384*yres) % yres;

    if (seen && !check_seen(seen, xres, yres, object, joff, ioff))
        return;

    find_weighted_mean_plane(object, d, xres, yres, joff, ioff, &bx, &by);
    make_tilted_bounding_box(object, width2, length2, -angle, bx, by);
    render_general_shape(object, intersect, width2, length2, -angle, bx, by);
    m = find_base_level(object, d, xres, yres, joff, ioff, stickout);

    /* Recalculate centre to corner position. */
    joff = (j - object->xres/2 + 16384*xres) % xres;
    ioff = (i - object->yres/2 + 16384*yres) % yres;

    sculpt_up(object, d, xres, yres, joff, ioff, m);
}

static glong
calculate_n_objects(ModuleArgs *args, guint xres, guint yres)
{
    GwyParams *params = args->params;
    PileupSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gdouble width_noise = gwy_params_get_double(params, PARAM_WIDTH_NOISE);
    gdouble aspect = gwy_params_get_double(params, PARAM_ASPECT);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    /* The distribution of area differs from the distribution of width. */
    gdouble noise_corr = exp(2.0*width_noise*width_noise);
    gdouble area_ratio = features[type].get_coverage(aspect);
    gdouble base_area = width*width*aspect;
    gdouble mean_obj_area = base_area * area_ratio * noise_corr;
    gdouble must_cover = coverage*xres*yres;
    return (glong)ceil(must_cover/mean_obj_area);
}

static inline void
extend_intersection_times(gdouble t, gdouble *t1, gdouble *t2)
{
    if (t < *t1)
        *t1 = t;
    if (t > *t2)
        *t2 = t;
}

static gboolean
intersect_ellipsoid(GwyXYZ *pt1, GwyXYZ *pt2, G_GNUC_UNUSED gdouble aspect)
{
    GwyXYZ c, v;
    gdouble t1, t2;

    middle_point(pt1, pt2, &c);
    vecdiff(pt2, pt1, &v);
    if (!solve_quadratic(vecnorm2(&v), 2.0*dotprod(&v, &c), vecnorm2(&c) - 1.0, &t1, &t2))
        return FALSE;

    point_on_line(&c, &v, t1, pt1);
    point_on_line(&c, &v, t2, pt2);
    return TRUE;
}

static gboolean
intersect_bar(GwyXYZ *pt1, GwyXYZ *pt2, G_GNUC_UNUSED gdouble aspect)
{
    GwyXYZ c, v, r;
    gdouble t;
    gdouble t1 = G_MAXDOUBLE, t2 = -G_MAXDOUBLE;

    middle_point(pt1, pt2, &c);
    vecdiff(pt2, pt1, &v);

    if (fabs(v.z) > 1e-14) {
        /* z = 1 */
        t = (1.0 - c.z)/v.z;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.x) <= 1.0 && fabs(r.y) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
        /* z = -1 */
        t = -(1.0 + c.z)/v.z;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.x) <= 1.0 && fabs(r.y) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
    }

    if (fabs(v.y) > 1e-14) {
        /* y = 1 */
        t = (1.0 - c.y)/v.y;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.x) <= 1.0 && fabs(r.z) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
        /* y = -1 */
        t = -(1.0 + c.y)/v.y;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.x) <= 1.0 && fabs(r.z) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
    }

    if (fabs(v.x) > 1e-14) {
        /* x = 1 */
        t = (1.0 - c.x)/v.x;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.z) <= 1.0 && fabs(r.y) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
        /* x = -1 */
        t = -(1.0 + c.x)/v.x;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.z) <= 1.0 && fabs(r.y) <= 1.0)
            extend_intersection_times(t, &t1, &t2);
    }

    if (t1 >= t2)
        return FALSE;

    point_on_line(&c, &v, t1, pt1);
    point_on_line(&c, &v, t2, pt2);
    return TRUE;
}

static gboolean
intersect_cylinder(GwyXYZ *pt1, GwyXYZ *pt2, G_GNUC_UNUSED gdouble aspect)
{
    GwyXYZ c, v;
    gdouble t1, t2;

    middle_point(pt1, pt2, &c);
    vecdiff(pt2, pt1, &v);
    /* First, we must hit the infinite cylinder at all. */
    if (!solve_quadratic(v.z*v.z + v.y*v.y, 2.0*(v.z*c.z + v.y*c.y), c.z*c.z + c.y*c.y - 1.0, &t1, &t2))
        return FALSE;

    point_on_line(&c, &v, t1, pt1);
    point_on_line(&c, &v, t2, pt2);
    if (pt1->x > pt2->x)
        GWY_SWAP(GwyXYZ, *pt1, *pt2);

    if (pt2->x < -1.0 || pt1->x > 1.0)
        return FALSE;

    if (pt1->x < -1.0) {
        t1 = -(1.0 + c.x)/v.x;
        point_on_line(&c, &v, t1, pt1);
    }
    if (pt2->x > 1.0) {
        t2 = (1.0 - c.x)/v.x;
        point_on_line(&c, &v, t2, pt2);
    }

    return TRUE;
}

static gboolean
intersect_nugget(GwyXYZ *pt1, GwyXYZ *pt2, gdouble aspect)
{
    GwyXYZ c, v, r1, r2;
    gdouble qa, qb, qc, t1, t2;

    middle_point(pt1, pt2, &c);
    vecdiff(pt2, pt1, &v);

    /* First try to hit the cylinder.   We know if we do not hit the infinitely
     * long version of it we cannot hit the object at all.*/
    if (!solve_quadratic(v.z*v.z + v.y*v.y, 2.0*(v.z*c.z + v.y*c.y), c.z*c.z + c.y*c.y - 1.0, &t1, &t2))
        return FALSE;

    point_on_line(&c, &v, t1, pt1);
    point_on_line(&c, &v, t2, pt2);
    if (pt1->x > pt2->x) {
        GWY_SWAP(GwyXYZ, *pt1, *pt2);
    }

    if (pt2->x < -1.0 || pt1->x > 1.0)
        return FALSE;

    /* If necessary, find intersections with the two terminating ellipsoids. */
    if (pt1->x < -1.0 + 1.0/aspect) {
        c.x *= aspect;
        v.x *= aspect;
        qa = vecnorm2(&v);
        qb = dotprod(&v, &c) + (aspect - 1.0)*v.x;
        qc = vecnorm2(&c) + aspect*(aspect - 2.0) + 2.0*(aspect - 1.0)*c.x;
        /* We may miss the rounded end completely. */
        if (!solve_quadratic(qa, 2.0*qb, qc, &t1, &t2))
            return FALSE;
        c.x /= aspect;
        v.x /= aspect;
        point_on_line(&c, &v, t1, &r1);
        point_on_line(&c, &v, t2, &r2);
        /* Either one or both intersections can be with the rounded part. */
        *pt1 = (r1.x <= r2.x) ? r1 : r2;
        if (pt2->x < -1.0 + 1.0/aspect)
            *pt2 = (r1.x <= r2.x) ? r2 : r1;
    }

    if (pt2->x > 1.0 - 1.0/aspect) {
        c.x *= aspect;
        v.x *= aspect;
        qa = vecnorm2(&v);
        qb = dotprod(&v, &c) - (aspect - 1.0)*v.x;
        qc = vecnorm2(&c) + aspect*(aspect - 2.0) - 2.0*(aspect - 1.0)*c.x;
        /* We may miss the rounded end completely. */
        if (!solve_quadratic(qa, 2.0*qb, qc, &t1, &t2))
            return FALSE;
        c.x /= aspect;
        v.x /= aspect;
        point_on_line(&c, &v, t1, &r1);
        point_on_line(&c, &v, t2, &r2);
        /* Either one or both intersections can be with the rounded part. */
        *pt2 = (r1.x >= r2.x) ? r1 : r2;
        if (pt1->x > 1.0 - 1.0/aspect)
            *pt1 = (r1.x >= r2.x) ? r2 : r1;
    }

    return TRUE;
}

static gboolean
intersect_hexagonal(GwyXYZ *pt1, GwyXYZ *pt2, G_GNUC_UNUSED gdouble aspect)
{
    GwyXYZ c, v, r;
    gdouble t1, t2, t, d;

    middle_point(pt1, pt2, &c);
    vecdiff(pt2, pt1, &v);

    /* First, we must hit the infinite rod at all. */
    t1 = G_MAXDOUBLE, t2 = -G_MAXDOUBLE;
    if (fabs(v.z) > 1e-14) {
        /* z = 1 */
        t = (1.0 - c.z)/v.z;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y) <= 0.5)
            extend_intersection_times(t, &t1, &t2);
        /* z = -1 */
        t = -(1.0 + c.z)/v.z;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y) <= 0.5)
            extend_intersection_times(t, &t1, &t2);
    }

    d = v.y + 0.5*v.z;
    if (fabs(d) > 1e-14) {
        /* z = 2(1-y) */
        t = (1.0 - c.y - 0.5*c.z)/d;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y - 0.75) <= 0.25)
            extend_intersection_times(t, &t1, &t2);
        /* z = -2(1+y) */
        t = -(1.0 + c.y + 0.5*c.z)/d;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y + 0.75) <= 0.25)
            extend_intersection_times(t, &t1, &t2);
    }

    d = v.y - 0.5*v.z;
    if (fabs(d) > 1e-14) {
        /* z = 2(y-1) */
        t = (1.0 - c.y + 0.5*c.z)/d;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y - 0.75) <= 0.25)
            extend_intersection_times(t, &t1, &t2);
        /* z = 2(y+1) */
        t = -(1.0 + c.y - 0.5*c.z)/d;
        point_on_line(&c, &v, t, &r);
        if (fabs(r.y + 0.75) <= 0.25)
            extend_intersection_times(t, &t1, &t2);
    }
    if (t1 >= t2)
        return FALSE;

    point_on_line(&c, &v, t1, pt1);
    point_on_line(&c, &v, t2, pt2);
    if (pt1->x > pt2->x)
        GWY_SWAP(GwyXYZ, *pt1, *pt2);

    if (pt2->x < -1.0 || pt1->x > 1.0)
        return FALSE;

    if (pt1->x < -1.0) {
        t1 = -(1.0 + c.x)/v.x;
        point_on_line(&c, &v, t1, pt1);
    }
    if (pt2->x > 1.0) {
        t2 = (1.0 - c.x)/v.x;
        point_on_line(&c, &v, t2, pt2);
    }

    return TRUE;
}

static gdouble
render_base_ellipsoid(gdouble x, gdouble y, G_GNUC_UNUSED gdouble aspect)
{
    gdouble r;

    r = 1.0 - x*x - y*y;
    return (r > 0.0) ? sqrt(r) : 0.0;
}

static gdouble
render_base_bar(gdouble x, gdouble y, G_GNUC_UNUSED gdouble aspect)
{
    return (fmax(fabs(x), fabs(y)) <= 1.0) ? 1.0 : 0.0;
}

static gdouble
render_base_cylinder(gdouble x, gdouble y, G_GNUC_UNUSED gdouble aspect)
{
    return (fmax(fabs(x), fabs(y)) <= 1.0) ? sqrt(1.0 - y*y) : 0.0;
}

static gdouble
render_base_nugget(gdouble x, gdouble y, gdouble aspect)
{
    gdouble h, r;

    h = 1.0 - 1.0/aspect;
    r = 1.0 - y*y;
    if (r <= 0.0)
        return 0.0;

    x = fabs(x);
    if (x <= h)
        return sqrt(r);

    x = aspect*(x - h);
    r -= x*x;
    return (r > 0.0) ? sqrt(r) : 0.0;
}

static gdouble
render_base_hexagonal(gdouble x, gdouble y, G_GNUC_UNUSED gdouble aspect)
{
    y = fabs(y);
    if (fmax(fabs(x), y) >= 1.0)
       return 0.0;
    return (y <= 0.5) ? 1.0 : 2.0*(1.0 - y);
}

static gdouble
getcov_ellipsoid(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0;
}

static gdouble
getcov_bar(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_cylinder(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_nugget(gdouble aspect)
{
    return 1.0 - (1.0 - G_PI/4.0)/aspect;
}

static gdouble
getcov_hexagonal(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
