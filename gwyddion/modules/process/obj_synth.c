/*
 *  $Id: obj_synth.c 26058 2023-12-15 13:06:25Z yeti-dn $
 *  Copyright (C) 2009-2021 David Necas (Yeti).
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

#define DECLARE_OBJECT(name) \
    static void create_##name(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle); \
    static gdouble getcov_##name(gdouble aspect)

typedef enum {
    RNG_ID,
    RNG_SIZE,
    RNG_ASPECT,
    RNG_HEIGHT,
    RNG_ANGLE,
    RNG_HTRUNC,
    RNG_SCULPT,
    RNG_NRNGS
} ObjSynthRng;

typedef enum {
    OBJ_SYNTH_HSPHERE  = 0,
    OBJ_SYNTH_PYRAMID  = 1,
    OBJ_SYNTH_HNUGGET  = 2,
    OBJ_SYNTH_THATCH   = 3,
    OBJ_SYNTH_DOUGHNUT = 4,
    OBJ_SYNTH_4HEDRON  = 5,
    OBJ_SYNTH_BOX      = 6,
    OBJ_SYNTH_CONE     = 7,
    OBJ_SYNTH_TENT     = 8,
    OBJ_SYNTH_DIAMOND  = 9,
    OBJ_SYNTH_GAUSSIAN = 10,
    OBJ_SYNTH_PARBUMP  = 11,
    OBJ_SYNTH_SPHERE   = 12,
    OBJ_SYNTH_NUGGET   = 13,
    OBJ_SYNTH_6PYRAMID = 14,
    OBJ_SYNTH_EXPSPIKE = 15,
    OBJ_SYNTH_PYRAMID2 = 16,
} ObjSynthType;

enum {
    PARAM_TYPE,
    PARAM_SCULPT,
    PARAM_STICKOUT,
    PARAM_AVOID_STACKING,
    PARAM_SIZE,
    PARAM_SIZE_NOISE,
    PARAM_ASPECT,
    PARAM_ASPECT_NOISE,
    PARAM_HEIGHT,
    PARAM_HEIGHT_NOISE,
    PARAM_HEIGHT_BOUND,
    PARAM_HTRUNC,
    PARAM_HTRUNC_NOISE,
    PARAM_ANGLE,
    PARAM_ANGLE_NOISE,
    PARAM_COVERAGE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,
    INFO_COVERAGE_OBJECTS,

    PARAM_DIMS0
};

/* Avoid reallocations by using a single buffer for objects that can only grow. */
typedef struct {
    gint xres;
    gint yres;
    gsize size;
    gdouble *data;
} ObjSynthObject;

typedef void    (*CreateFeatureFunc)  (ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle);
typedef void    (*TruncateFeatureFunc)(ObjSynthObject *feature, gdouble htrunc);
typedef gdouble (*GetCoverageFunc)    (gdouble aspect);

typedef struct {
    gboolean is_full;
    const gchar *name;
    CreateFeatureFunc create;
    TruncateFeatureFunc htruncate;
    GetCoverageFunc get_coverage;
} ObjSynthFeature;

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
    GwyParamTable *table_placement;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             obj_synth           (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static GtkWidget*       dimensions_tab_new  (ModuleGUI *gui);
static GtkWidget*       generator_tab_new   (ModuleGUI *gui);
static GtkWidget*       placement_tab_new   (ModuleGUI *gui);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);
static void             object_synth_iter   (ModuleArgs *args,
                                             GwyDataField *surface,
                                             gboolean *seen,
                                             ObjSynthObject *object,
                                             GwyRandGenSet *rngset,
                                             gint nxcells,
                                             gint nycells,
                                             gint xoff,
                                             gint yoff,
                                             gint nobjects,
                                             gint *indices);
static gboolean         check_seen          (gboolean *seen,
                                             gint xres,
                                             gint yres,
                                             ObjSynthObject *object,
                                             gint joff,
                                             gint ioff);
static void             place_add_feature   (GwyDataField *surface,
                                             ObjSynthObject *object,
                                             gint joff,
                                             gint ioff,
                                             gdouble stickout,
                                             gboolean is_up,
                                             gboolean is_full);
static glong            calculate_n_objects (ModuleArgs *args,
                                             guint xres,
                                             guint yres);

DECLARE_OBJECT(hsphere);
DECLARE_OBJECT(pyramid);
DECLARE_OBJECT(box);
DECLARE_OBJECT(hnugget);
DECLARE_OBJECT(thatch);
DECLARE_OBJECT(doughnut);
DECLARE_OBJECT(thedron);
DECLARE_OBJECT(hexpyramid);
DECLARE_OBJECT(cone);
DECLARE_OBJECT(tent);
DECLARE_OBJECT(semipyramid);
DECLARE_OBJECT(diamond);
DECLARE_OBJECT(gaussian);
DECLARE_OBJECT(expspike);
DECLARE_OBJECT(parbump);
DECLARE_OBJECT(sphere);
DECLARE_OBJECT(nugget);

static void htruncate_sphere(ObjSynthObject *feature, gdouble htrunc);
#define htruncate_nugget htruncate_sphere

/* NB: The order of these and everything else (like table_noise[]) must match the enums. */
static const ObjSynthFeature features[] = {
    { FALSE, N_("Half-spheres"),       create_hsphere,     NULL,             getcov_hsphere,     },
    { FALSE, N_("Pyramids"),           create_pyramid,     NULL,             getcov_pyramid,     },
    { FALSE, N_("Half-nuggets"),       create_hnugget,     NULL,             getcov_hnugget,     },
    { FALSE, N_("Thatches"),           create_thatch,      NULL,             getcov_thatch,      },
    { FALSE, N_("Doughnuts"),          create_doughnut,    NULL,             getcov_doughnut,    },
    { FALSE, N_("Tetrahedrons"),       create_thedron,     NULL,             getcov_thedron,     },
    { TRUE,  N_("Boxes"),              create_box,         NULL,             getcov_box,         },
    { FALSE, N_("Cones"),              create_cone,        NULL,             getcov_cone,        },
    { FALSE, N_("Tents"),              create_tent,        NULL,             getcov_tent,        },
    { FALSE, N_("Diamonds"),           create_diamond,     NULL,             getcov_diamond,     },
    { FALSE, N_("Gaussians"),          create_gaussian,    NULL,             getcov_gaussian,    },
    { FALSE, N_("Parabolic bumps"),    create_parbump,     NULL,             getcov_parbump,     },
    { TRUE,  N_("Full spheres"),       create_sphere,      htruncate_sphere, getcov_sphere,      },
    { TRUE,  N_("Full nuggets"),       create_nugget,      htruncate_nugget, getcov_nugget,      },
    { FALSE, N_("Hexagonal pyramids"), create_hexpyramid,  NULL,             getcov_hexpyramid,  },
    { FALSE, N_("Exp spikes"),         create_expspike,    NULL,             getcov_expspike,    },
    { FALSE, N_("Semi-pyramids"),      create_semipyramid, NULL,             getcov_semipyramid, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates randomly patterned surfaces by placing objects."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2009",
};

GWY_MODULE_QUERY2(module_info, obj_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("obj_synth",
                              (GwyProcessFunc)&obj_synth,
                              N_("/S_ynthetic/_Deposition/_Objects..."),
                              GWY_STOCK_SYNTHETIC_OBJECTS,
                              RUN_MODES,
                              0,
                              N_("Generate surface of randomly placed objects"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    /* Define GUI feature order. */
    static GwyEnum types[] = {
        { NULL, OBJ_SYNTH_HSPHERE,  },
        { NULL, OBJ_SYNTH_SPHERE,   },
        { NULL, OBJ_SYNTH_BOX,      },
        { NULL, OBJ_SYNTH_CONE,     },
        { NULL, OBJ_SYNTH_PYRAMID,  },
        { NULL, OBJ_SYNTH_DIAMOND,  },
        { NULL, OBJ_SYNTH_4HEDRON,  },
        { NULL, OBJ_SYNTH_6PYRAMID, },
        { NULL, OBJ_SYNTH_HNUGGET,  },
        { NULL, OBJ_SYNTH_NUGGET,   },
        { NULL, OBJ_SYNTH_THATCH,   },
        { NULL, OBJ_SYNTH_TENT,     },
        { NULL, OBJ_SYNTH_PYRAMID2, },
        { NULL, OBJ_SYNTH_GAUSSIAN, },
        { NULL, OBJ_SYNTH_EXPSPIKE, },
        { NULL, OBJ_SYNTH_DOUGHNUT, },
        { NULL, OBJ_SYNTH_PARBUMP,  },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    gwy_enum_fill_from_struct(types, G_N_ELEMENTS(types), features, sizeof(ObjSynthFeature),
                              G_STRUCT_OFFSET(ObjSynthFeature, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Shape"),
                              types, G_N_ELEMENTS(types), OBJ_SYNTH_HSPHERE);
    gwy_param_def_add_double(paramdef, PARAM_SCULPT, "sculpt", _("_Feature sign"), -1.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_STICKOUT, "stickout", _("Colum_narity"), 0.0, 1.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_AVOID_STACKING, "avoid_stacking", _("_Avoid stacking"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_SIZE, "size", _("Si_ze"), 1.0, 1000.0, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_SIZE_NOISE, "size_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ASPECT, "aspect", _("_Aspect ratio"), 0.2, 5.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ASPECT_NOISE, "aspect_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_HEIGHT_BOUND, "height_bound", _("Scales _with size"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_HTRUNC, "htrunc", _("_Truncate"), 0.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HTRUNC_NOISE, "htrunc_noise", _("Spread"), 0.0, 1.0, 0.0);
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
obj_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Random Objects"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Shape")));
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

    gwy_param_table_append_combo(table, PARAM_TYPE);

    gwy_param_table_append_header(table, -1, _("Size"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_SIZE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_SIZE_NOISE);

    gwy_param_table_append_header(table, -1, _("Aspect Ratio"));
    gwy_param_table_append_slider(table, PARAM_ASPECT);
    gwy_param_table_append_slider(table, PARAM_ASPECT_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_checkbox(table, PARAM_HEIGHT_BOUND);
    gwy_param_table_append_slider(table, PARAM_HEIGHT_NOISE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_HTRUNC);
    gwy_param_table_slider_set_mapping(table, PARAM_HTRUNC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_HTRUNC_NOISE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
placement_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_placement = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_append_info(table, INFO_COVERAGE_OBJECTS, _("Number of objects"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_SCULPT);
    gwy_param_table_slider_set_mapping(table, PARAM_SCULPT, GWY_SCALE_MAPPING_LINEAR);
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

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0
        || id == PARAM_TYPE || id == PARAM_SIZE || id == PARAM_SIZE_NOISE
        || id == PARAM_ASPECT || id == PARAM_COVERAGE) {
        gint xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YRES);
        glong nobj = calculate_n_objects(gui->args, xres, yres);
        gchar *s = g_strdup_printf("%ld", nobj);

        gwy_param_table_info_set_valuestr(gui->table_placement, INFO_COVERAGE_OBJECTS, s);
        g_free(s);
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

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gboolean avoid_stacking = gwy_params_get_boolean(params, PARAM_AVOID_STACKING);
    GwyDataField *field = args->field, *result = args->result;
    gint xres, yres, nxcells, nycells, ncells, cellside, niters, i;
    ObjSynthObject object = { 0, 0, 0, NULL };
    GwyRandGenSet *rngset;
    gboolean *seen = NULL;
    gint *indices;
    glong nobjects;

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    if (field && do_initialise)
        gwy_data_field_copy(field, result, FALSE);
    else
        gwy_data_field_clear(result);

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    cellside = sqrt(sqrt(xres*yres));
    nxcells = (xres + cellside-1)/cellside;
    nycells = (yres + cellside-1)/cellside;
    ncells = nxcells*nycells;
    nobjects = calculate_n_objects(args, xres, yres);
    niters = nobjects/ncells;

    indices = g_new(gint, ncells);
    if (avoid_stacking)
        seen = g_new0(gboolean, xres*yres);

    for (i = 0; i < niters; i++)
        object_synth_iter(args, result, seen, &object, rngset, nxcells, nycells, i+1, i+1, ncells, indices);
    object_synth_iter(args, result, seen, &object, rngset, nxcells, nycells, 0, 0, nobjects % ncells, indices);

    g_free(object.data);
    g_free(indices);
    g_free(seen);
    gwy_rand_gen_set_free(rngset);
}

static void
object_synth_iter(ModuleArgs *args, GwyDataField *surface,
                  gboolean *seen, ObjSynthObject *object, GwyRandGenSet *rngset,
                  gint nxcells, gint nycells, gint xoff, gint yoff,
                  gint nobjects, gint *indices)
{
    GwyParams *params = args->params;
    ObjSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    gdouble size_noise = gwy_params_get_double(params, PARAM_SIZE_NOISE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble htrunc = gwy_params_get_double(params, PARAM_HTRUNC);
    gdouble htrunc_noise = gwy_params_get_double(params, PARAM_HTRUNC_NOISE);
    gdouble aspect = gwy_params_get_double(params, PARAM_ASPECT);
    gdouble aspect_noise = gwy_params_get_double(params, PARAM_ASPECT_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    gdouble angle_noise = gwy_params_get_double(params, PARAM_ANGLE_NOISE);
    gdouble sculpt = gwy_params_get_double(params, PARAM_SCULPT);
    gdouble stickout = gwy_params_get_double(params, PARAM_STICKOUT);
    gboolean height_bound = gwy_params_get_boolean(params, PARAM_HEIGHT_BOUND);
    gboolean avoid_stacking = gwy_params_get_boolean(params, PARAM_AVOID_STACKING);
    gint xres, yres, ncells, k, l, power10z;
    const ObjSynthFeature *feature = features + type;
    gdouble sculpt_threshold;
    gboolean is_up, is_full;
    GRand *rngid;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);
    sculpt_threshold = 0.5*(1.0 - sculpt);

    g_return_if_fail(nobjects <= nxcells*nycells);

    is_full = feature->is_full;
    xres = gwy_data_field_get_xres(surface);
    yres = gwy_data_field_get_yres(surface);
    ncells = nxcells*nycells;

    for (k = 0; k < ncells; k++)
        indices[k] = k;

    rngid = gwy_rand_gen_set_rng(rngset, RNG_ID);
    for (k = 0; k < nobjects; k++) {
        gdouble ksize = size, kaspect = aspect, kheight = height, kangle = angle, khtrunc = htrunc;
        gint id, i, j, from, to;
        gdouble *p;

        id = g_rand_int_range(rngid, 0, ncells - k);
        i = indices[id]/nxcells;
        j = indices[id] % nxcells;
        indices[id] = indices[ncells-1 - k];

        if (size_noise)
            ksize *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_SIZE, size_noise));
        if (aspect_noise)
            kaspect *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_ASPECT, aspect_noise));
        if (angle_noise)
            kangle += gwy_rand_gen_set_gaussian(rngset, RNG_ANGLE, 2*angle_noise);

        if (height_bound)
            kheight *= ksize/size;
        if (height_noise)
            kheight *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_HEIGHT, height_noise));

        feature->create(object, ksize, kaspect, kangle);

        /* Use a specific distribution for htrunc. */
        if (htrunc_noise) {
            gdouble q = exp(gwy_rand_gen_set_gaussian(rngset, RNG_HTRUNC, htrunc_noise));
            khtrunc = q/(q + 1.0/khtrunc - 1.0);
        }
        if (khtrunc < 1.0) {
            if (feature->htruncate)
                feature->htruncate(object, khtrunc);
            else {
                p = object->data;
                for (l = object->xres*object->yres; l; l--, p++) {
                    if (*p > khtrunc)
                        *p = khtrunc;
                }
            }
        }

        p = object->data;
        for (l = object->xres*object->yres; l; l--, p++)
            *p *= kheight;

        from = (j*xres + nxcells/2)/nxcells;
        to = (j*xres + xres + nxcells/2)/nxcells;
        to = MIN(to, xres);
        j = from + xoff + g_rand_int_range(rngid, 0, to - from);
        /* Recalculate centre to corner position. */
        j = (j - object->xres/2 + 16384*xres) % xres;
        g_assert(j >= 0);

        from = (i*yres + nycells/2)/nycells;
        to = (i*yres + yres + nycells/2)/nycells;
        to = MIN(to, yres);
        i = from + yoff + g_rand_int_range(rngid, 0, to - from);
        /* Recalculate centre to corner position. */
        i = (i - object->yres/2 + 16384*yres) % yres;
        g_assert(i >= 0);

        if (avoid_stacking && !check_seen(seen, xres, yres, object, j, i))
            continue;

        is_up = gwy_rand_gen_set_double(rngset, RNG_SCULPT) >= sculpt_threshold;
        place_add_feature(surface, object, j, i, stickout, is_up, is_full);
    }
}

static gboolean
check_seen(gboolean *seen, gint xres, gint yres,
           ObjSynthObject *object, gint joff, gint ioff)
{
    gint kxres, kyres, i, j;
    const gdouble *z;
    gboolean *srow, *end;

    kxres = object->xres;
    kyres = object->yres;

    z = object->data;
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

    z = object->data;
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
obj_synth_object_resize(ObjSynthObject *object, gint xres, gint yres)
{
    object->xres = xres;
    object->yres = yres;
    if ((guint)(xres*yres) > object->size) {
        g_free(object->data);
        object->data = g_new(gdouble, xres*yres);
        object->size = xres*yres;
    }
}

static void
create_sphere_common(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle, gboolean is_full)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(2*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - xc*xc - yc*yc;
            if (r > 0.0) {
                if (is_full)
                    z[i*xres + j] = 0.5 + 0.5*sqrt(r);
                else
                    z[i*xres + j] = sqrt(r);
            }
            else
                z[i*xres + j] = 0.0;
        }
    }
}

static void
create_hsphere(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    create_sphere_common(feature, size, aspect, angle, FALSE);
}

static void
create_sphere(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    create_sphere_common(feature, size, aspect, angle, TRUE);
}

static void
htruncate_sphere(ObjSynthObject *feature, gdouble htrunc)
{
    gint k, n = feature->xres * feature->yres;
    gdouble *z = feature->data;
    gdouble hh = 0.5*htrunc;

    for (k = 0; k < n; k++) {
        if (z[k] > 0.0) {
            z[k] += hh - 0.5;
            if (z[k] > htrunc)
                z[k] = htrunc;
        }
    }
}

static void
create_pyramid(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - MAX(fabs(xc), fabs(yc));
            z[i*xres + j] = MAX(r, 0.0);
        }
    }
}

static void
create_diamond(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*MAX(a*fabs(c), b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*MAX(a*fabs(s), b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - (fabs(xc) + fabs(yc));
            z[i*xres + j] = MAX(r, 0.0);
        }
    }
}

static void
create_box(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - MAX(fabs(xc), fabs(yc));
            z[i*xres + j] = (r >= 0.0) ? 1.0 : 0.0;
        }
    }
}

static void
create_tent(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - fabs(yc);
            z[i*xres + j] = (fabs(xc) <= 1.0 && r > 0.0) ? r : 0.0;
        }
    }
}

static void
create_semipyramid(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - fabs(yc);
            z[i*xres + j] = (fabs(xc) <= 1.0 && r > 0.0) ? fmin(r, 0.5*(1 + xc)) : 0.0;
        }
    }
}

static void
create_cone(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(2*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - hypot(xc, yc);
            z[i*xres + j] = MAX(r, 0.0);
        }
    }
}

static void
create_nugget_common(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle, gboolean is_full)
{
    gdouble a, b, c, s, r, x, y, xc, yc, excess;
    gint xres, yres, i, j;
    gdouble *z;

    if (aspect == 1.0) {
        create_sphere_common(feature, size, aspect, angle, is_full);
        return;
    }

    /* Ensure a > b */
    if (aspect < 1.0) {
        angle += G_PI/2.0;
        aspect = 1.0/aspect;
    }

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    excess = aspect - 1.0;
    xres = (gint)ceil(2*((a - b)*fabs(c) + b) + 1) | 1;
    yres = (gint)ceil(2*((a - b)*fabs(s) + b) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/b;
            yc = (x*s + y*c)/b;
            xc = fabs(xc) - excess;
            if (xc < 0.0)
                xc = 0.0;
            r = 1.0 - xc*xc - yc*yc;
            if (r > 0.0) {
                if (is_full)
                    z[i*xres + j] = 0.5 + 0.5*sqrt(r);
                else
                    z[i*xres + j] = sqrt(r);
            }
            else
                z[i*xres + j] = 0.0;
        }
    }
}

static void
create_hnugget(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    create_nugget_common(feature, size, aspect, angle, FALSE);
}

static void
create_nugget(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    create_nugget_common(feature, size, aspect, angle, TRUE);
}

static void
create_thatch(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = ((x*c - y*s) - 0.3)/a;
            yc = (x*s + y*c)/b;
            r = 0.5 - 0.5*xc;
            if (r >= 0.0 && r <= 1.0)
                z[i*xres + j] = (fabs(yc) <= r) ? (1.0 - r) : 0.0;
            else
                z[i*xres + j] = 0.0;
        }
    }
}

static void
create_doughnut(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(2*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = hypot(xc, yc) - 0.6;
            r = 1.0 - r*r/0.16;
            z[i*xres + j] = (r > 0.0) ? sqrt(r) : 0.0;
        }
    }
}

static void
create_gaussian(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(8*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(8*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = exp(-4.0*(xc*xc + yc*yc));
            z[i*xres + j] = r;
        }
    }
}

static void
create_expspike(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(8*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(8*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = exp(-4.0*sqrt(xc*xc + yc*yc));
            z[i*xres + j] = r;
        }
    }
}

static void
create_thedron(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, xp, xm, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect)*GWY_SQRT3/2.0;
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a*GWY_SQRT3/2.0 + GWY_SQRT3/6.0;
            yc = (x*s + y*c)/b;
            xp = 0.5*xc + GWY_SQRT3/2.0*yc;
            xm = 0.5*xc - GWY_SQRT3/2.0*yc;
            r = MAX(-xc, xp);
            r = MAX(r, xm);
            r = 1.0 - GWY_SQRT3*r;
            z[i*xres + j] = MAX(r, 0.0);
        }
    }
}

static void
create_hexpyramid(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, yt, yl, yr, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect)*GWY_SQRT3/2.0;
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*(a*fabs(c) + b*fabs(s)) + 1) | 1;
    yres = (gint)ceil(2*(a*fabs(s) + b*fabs(c)) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            yt = fabs(yc);
            yr = fabs(0.5*yc + xc);
            yl = fabs(0.5*yc - xc);
            r = MAX(yl, yr);
            r = MAX(r, yt);
            r = 1.0 - r;
            z[i*xres + j] = MAX(r, 0.0);
        }
    }
}

static void
create_parbump(ObjSynthObject *feature, gdouble size, gdouble aspect, gdouble angle)
{
    gdouble a, b, c, s, r, x, y, xc, yc;
    gint xres, yres, i, j;
    gdouble *z;

    a = size*sqrt(aspect);
    b = size/sqrt(aspect);
    c = cos(angle);
    s = sin(angle);
    xres = (gint)ceil(2*hypot(a*c, b*s) + 1) | 1;
    yres = (gint)ceil(2*hypot(a*s, b*c) + 1) | 1;

    obj_synth_object_resize(feature, xres, yres);
    z = feature->data;
    for (i = 0; i < yres; i++) {
        y = i - yres/2;
        for (j = 0; j < xres; j++) {
            x = j - xres/2;

            xc = (x*c - y*s)/a;
            yc = (x*s + y*c)/b;
            r = 1.0 - xc*xc - yc*yc;
            z[i*xres + j] = (r > 0.0) ? r : 0.0;
        }
    }
}

static gdouble
find_base_level_for_up(const gdouble *object, gint kxres, gint kyres,
                       const gdouble *surface, gint xres, gint yres,
                       gint joff, gint ioff,
                       gdouble stickout, gdouble zcorr)
{
    gdouble m_bury, m_stack;
    const gdouble *srow, *end;
    gint i, j;

    m_bury = (stickout > 1 - 1e-6) ? 0.0 : G_MAXDOUBLE;
    m_stack = (stickout < 1e-6) ? 0.0 : -G_MAXDOUBLE;
    /* Optimise the simple cases when only one quantity needs to be found. */
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        if (stickout < 1e-6) {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object && *srow < m_bury)
                    m_bury = *srow;
                if (srow == end)
                    srow -= xres;
            }
        }
        else if (stickout > 1 - 1e-6) {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object && *srow + *object - zcorr > m_stack)
                    m_stack = *srow + *object - zcorr;
                if (srow == end)
                    srow -= xres;
            }
        }
        else {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object) {
                    if (*srow < m_bury)
                        m_bury = *srow;
                    if (*srow + *object - zcorr > m_stack)
                        m_stack = *srow + *object - zcorr;
                }
                if (srow == end)
                    srow -= xres;
            }
        }
    }

    return stickout*m_stack + (1.0 - stickout)*m_bury;
}

static gdouble
find_base_level_for_down(const gdouble *object, gint kxres, gint kyres,
                         const gdouble *surface, gint xres, gint yres,
                         gint joff, gint ioff,
                         gdouble stickout, gdouble zcorr)
{
    gdouble m_bury, m_stack;
    const gdouble *srow, *end;
    gint i, j;

    m_bury = (stickout > 1 - 1e-6) ? 0.0 : -G_MAXDOUBLE;
    m_stack = (stickout < 1e-6) ? 0.0 : G_MAXDOUBLE;
    /* Optimise the simple cases when only one quantity needs to be found. */
    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        if (stickout < 1e-6) {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object && *srow > m_bury)
                    m_bury = *srow;
                if (srow == end)
                    srow -= xres;
            }
        }
        else if (stickout > 1 - 1e-6) {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object && *srow - *object + zcorr < m_stack)
                    m_stack = *srow - *object + zcorr;
                if (srow == end)
                    srow -= xres;
            }
        }
        else {
            for (j = 0; j < kxres; j++, object++, srow++) {
                if (*object) {
                    if (*srow > m_bury)
                        m_bury = *srow;
                    if (*srow - *object + zcorr < m_stack)
                        m_stack = *srow - *object + zcorr;
                }
                if (srow == end)
                    srow -= xres;
            }
        }
    }

    return stickout*m_stack + (1.0 - stickout)*m_bury;
}

static void
sculpt_down(const gdouble *object, gint kxres, gint kyres,
            gdouble *surface, gint xres, gint yres,
            gint joff, gint ioff, gdouble m)
{
    gdouble *srow, *end;
    gint i, j;

    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, object++, srow++) {
            if (*object && *srow > m - *object)
                *srow = m - *object;
            if (srow == end)
                srow -= xres;
        }
    }
}

static void
sculpt_up(const gdouble *object, gint kxres, gint kyres,
          gdouble *surface, gint xres, gint yres,
          gint joff, gint ioff, gdouble m)
{
    gdouble *srow, *end;
    gint i, j;

    for (i = 0; i < kyres; i++) {
        srow = surface + ((ioff + i) % yres)*xres;
        end = srow + xres-1;
        srow += joff;
        for (j = 0; j < kxres; j++, object++, srow++) {
            if (*object && *srow < m + *object)
                *srow = m + *object;
            if (srow == end)
                srow -= xres;
        }
    }
}

static void
place_add_feature(GwyDataField *surface,
                  ObjSynthObject *object, gint joff, gint ioff,
                  gdouble stickout, gboolean is_up, gboolean is_full)
{
    gint xres, yres, kxres, kyres, i;
    gdouble m, zcorr;
    const gdouble *z;
    gdouble *d;

    xres = gwy_data_field_get_xres(surface);
    yres = gwy_data_field_get_yres(surface);
    kxres = object->xres;
    kyres = object->yres;

    d = gwy_data_field_get_data(surface);
    z = object->data;

    /* Full shapes already have the lower side included in the object height so they would be twice as high in the
     * columnar mode if we did not correct for this. */
    zcorr = 0.0;
    if (is_full && stickout >= 1e-6) {
        for (i = 0; i < kxres*kyres; i++) {
            if (z[i] > zcorr)
                zcorr = z[i];
        }
    }

    if (is_up) {
        m = find_base_level_for_up(z, kxres, kyres, d, xres, yres, joff, ioff, stickout, zcorr);
        sculpt_up(z, kxres, kyres, d, xres, yres, joff, ioff, m);
    }
    else {
        m = find_base_level_for_down(z, kxres, kyres, d, xres, yres, joff, ioff, stickout, zcorr);
        sculpt_down(z, kxres, kyres, d, xres, yres, joff, ioff, m);
    }
}

/* The distribution of area differs from the distribution of size. */
static glong
calculate_n_objects(ModuleArgs *args, guint xres, guint yres)
{
    GwyParams *params = args->params;
    ObjSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    gdouble size_noise = gwy_params_get_double(params, PARAM_SIZE_NOISE);
    gdouble aspect = gwy_params_get_double(params, PARAM_ASPECT);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gdouble noise_corr = exp(2.0*size_noise*size_noise);
    gdouble area_ratio = features[type].get_coverage(aspect);
    /* Size is radius, not diameter, so multiply by 4. */
    gdouble mean_obj_area = 4.0*size*size * area_ratio * noise_corr;
    gdouble must_cover = coverage*xres*yres;
    return (glong)ceil(must_cover/mean_obj_area);
}

static gdouble
getcov_hsphere(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0;
}

static gdouble
getcov_sphere(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0;
}

static gdouble
getcov_pyramid(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_diamond(G_GNUC_UNUSED gdouble aspect)
{
    return 0.5;
}

static gdouble
getcov_box(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_tent(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_semipyramid(G_GNUC_UNUSED gdouble aspect)
{
    return 1.0;
}

static gdouble
getcov_cone(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0;
}

static gdouble
getcov_hnugget(gdouble aspect)
{
    return 1.0 - (1.0 - G_PI/4.0)/MAX(aspect, 1.0/aspect);
}

static gdouble
getcov_nugget(gdouble aspect)
{
    return 1.0 - (1.0 - G_PI/4.0)/MAX(aspect, 1.0/aspect);
}

static gdouble
getcov_thatch(G_GNUC_UNUSED gdouble aspect)
{
    return 0.5;
}

static gdouble
getcov_doughnut(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0 * 24.0/25.0;
}

static gdouble
getcov_gaussian(G_GNUC_UNUSED gdouble aspect)
{
    /* Just an `effective' value estimate, returning 1 would make the gaussians too tiny wrt to other objects */
    return G_PI/8.0;
}

static gdouble
getcov_expspike(G_GNUC_UNUSED gdouble aspect)
{
    /* Just an `effective' value estimate, returning 1 would make the spikes too tiny wrt to other objects */
    return G_PI/18.0;
}

static gdouble
getcov_thedron(G_GNUC_UNUSED gdouble aspect)
{
    return GWY_SQRT3/4.0;
}

static gdouble
getcov_hexpyramid(G_GNUC_UNUSED gdouble aspect)
{
    return 0.75;
}

static gdouble
getcov_parbump(G_GNUC_UNUSED gdouble aspect)
{
    return G_PI/4.0;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
