/*
 *  $Id: pat_synth.c 24848 2022-07-01 11:29:25Z yeti-dn $
 *  Copyright (C) 2010-2021 David Necas (Yeti).
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
#include <libprocess/correct.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/synth.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils-synth.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define DECLARE_PATTERN(name) \
    static void define_params_##name(GwyParamDef *pardef); \
    static void append_gui_##name(ModuleGUI *gui); \
    static void make_pattern_##name(ModuleArgs *args, GwyRandGenSet *rngset);

#define PATTERN_FUNCS(name) \
    define_params_##name, append_gui_##name, make_pattern_##name, \
    dim_params_##name, G_N_ELEMENTS(dim_params_##name)

/* Each pattern has its own set of parameters but many are common so they get the same symbolic name in PatSynthRng
 * for simpliciy. */
typedef enum {
    /* Deformation (displacement map) is universal. */
    RNG_DISPLAC_X   = 0,
    RNG_DISPLAC_Y   = 1,
    RNG_HEIGHT      = 2,
    RNG_TOP_X       = 3,
    RNG_SIZE_X      = 3,  /* Alias. */
    RNG_TOP_Y       = 4,
    RNG_SLOPE       = 5,
    RNG_OFFSET_X    = 6,
    RNG_OFFSET_Y    = 7,
    RNG_ROUNDNESS   = 8,
    RNG_ORIENTATION = 8,  /* Alias */
    RNG_NRNGS
} PatSynthRng;

typedef enum {
    PAT_SYNTH_STAIRCASE = 0,
    PAT_SYNTH_DBLSTAIR  = 1,
    PAT_SYNTH_GRATING   = 2,
    PAT_SYNTH_AMPHITH   = 3,
    PAT_SYNTH_RINGS     = 4,
    PAT_SYNTH_STAR      = 5,
    PAT_SYNTH_RHOLES    = 6,
    PAT_SYNTH_PILLARS   = 7,
    PAT_SYNTH_NTYPES
} PatSynthType;

typedef enum {
    PILLAR_SHAPE_CIRCLE  = 0,
    PILLAR_SHAPE_SQUARE  = 1,
    PILLAR_SHAPE_HEXAGON = 2,
    PILLAR_SHAPE_NSHAPES,
} PillarShapeType;

enum {
    PARAM_TYPE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_STAIRCASE_PERIOD,
    PARAM_STAIRCASE_POSITION_NOISE,
    PARAM_STAIRCASE_SLOPE,
    PARAM_STAIRCASE_SLOPE_NOISE,
    PARAM_STAIRCASE_HEIGHT,
    PARAM_STAIRCASE_HEIGHT_NOISE,
    PARAM_STAIRCASE_ANGLE,
    PARAM_STAIRCASE_SIGMA,
    PARAM_STAIRCASE_TAU,
    PARAM_STAIRCASE_KEEP_SLOPE,

    PARAM_DBLSTAIR_XPERIOD,
    PARAM_DBLSTAIR_YPERIOD,
    PARAM_DBLSTAIR_XPOSITION_NOISE,
    PARAM_DBLSTAIR_YPOSITION_NOISE,
    PARAM_DBLSTAIR_HEIGHT,
    PARAM_DBLSTAIR_HEIGHT_NOISE,
    PARAM_DBLSTAIR_ANGLE,
    PARAM_DBLSTAIR_SIGMA,
    PARAM_DBLSTAIR_TAU,

    PARAM_GRATING_PERIOD,
    PARAM_GRATING_POSITION_NOISE,
    PARAM_GRATING_TOP_FRAC,
    PARAM_GRATING_TOP_FRAC_NOISE,
    PARAM_GRATING_SLOPE,
    PARAM_GRATING_SLOPE_NOISE,
    PARAM_GRATING_ASYMM,
    PARAM_GRATING_HEIGHT,
    PARAM_GRATING_HEIGHT_NOISE,
    PARAM_GRATING_ANGLE,
    PARAM_GRATING_SIGMA,
    PARAM_GRATING_TAU,
    PARAM_GRATING_SCALE_WITH_WIDTH,

    PARAM_AMPHITH_FLAT,
    PARAM_AMPHITH_POSITION_NOISE,
    PARAM_AMPHITH_SLOPE,
    PARAM_AMPHITH_SLOPE_NOISE,
    PARAM_AMPHITH_HEIGHT,
    PARAM_AMPHITH_HEIGHT_NOISE,
    PARAM_AMPHITH_INVPOWER,
    PARAM_AMPHITH_PARABOLICITY,
    PARAM_AMPHITH_XCENTER,
    PARAM_AMPHITH_YCENTER,
    PARAM_AMPHITH_ANGLE,
    PARAM_AMPHITH_SIGMA,
    PARAM_AMPHITH_TAU,

    PARAM_RINGS_PERIOD,
    PARAM_RINGS_POSITION_NOISE,
    PARAM_RINGS_TOP_FRAC,
    PARAM_RINGS_TOP_FRAC_NOISE,
    PARAM_RINGS_SLOPE,
    PARAM_RINGS_SLOPE_NOISE,
    PARAM_RINGS_ASYMM,
    PARAM_RINGS_HEIGHT,
    PARAM_RINGS_HEIGHT_NOISE,
    PARAM_RINGS_INVPOWER,
    PARAM_RINGS_PARABOLICITY,
    PARAM_RINGS_XCENTER,
    PARAM_RINGS_YCENTER,
    PARAM_RINGS_ANGLE,
    PARAM_RINGS_SIGMA,
    PARAM_RINGS_TAU,
    PARAM_RINGS_SCALE_WITH_WIDTH,

    PARAM_STAR_N_RAYS,
    PARAM_STAR_TOP_FRAC,
    PARAM_STAR_TOP_FRAC_NOISE,
    PARAM_STAR_EDGE_SHIFT,
    PARAM_STAR_SLOPE,
    PARAM_STAR_HEIGHT,
    PARAM_STAR_XCENTER,
    PARAM_STAR_YCENTER,
    PARAM_STAR_ANGLE,
    PARAM_STAR_SIGMA,
    PARAM_STAR_TAU,

    PARAM_RHOLES_XPERIOD,
    PARAM_RHOLES_XPOSITION_NOISE,
    PARAM_RHOLES_YPERIOD,
    PARAM_RHOLES_YPOSITION_NOISE,
    PARAM_RHOLES_XTOP_FRAC,
    PARAM_RHOLES_XTOP_FRAC_NOISE,
    PARAM_RHOLES_YTOP_FRAC,
    PARAM_RHOLES_YTOP_FRAC_NOISE,
    PARAM_RHOLES_SLOPE,
    PARAM_RHOLES_SLOPE_NOISE,
    PARAM_RHOLES_ROUNDNESS,
    PARAM_RHOLES_ROUNDNESS_NOISE,
    PARAM_RHOLES_HEIGHT,
    PARAM_RHOLES_HEIGHT_NOISE,
    PARAM_RHOLES_ANGLE,
    PARAM_RHOLES_SIGMA,
    PARAM_RHOLES_TAU,

    PARAM_PILLARS_SHAPE,
    PARAM_PILLARS_XPERIOD,
    PARAM_PILLARS_XPOSITION_NOISE,
    PARAM_PILLARS_YPERIOD,
    PARAM_PILLARS_YPOSITION_NOISE,
    PARAM_PILLARS_SIZE_FRAC,
    PARAM_PILLARS_SIZE_FRAC_NOISE,
    PARAM_PILLARS_SLOPE,
    PARAM_PILLARS_SLOPE_NOISE,
    PARAM_PILLARS_ORIENTATION,
    PARAM_PILLARS_ORIENTATION_NOISE,
    PARAM_PILLARS_HEIGHT,
    PARAM_PILLARS_HEIGHT_NOISE,
    PARAM_PILLARS_ANGLE,
    PARAM_PILLARS_SIGMA,
    PARAM_PILLARS_TAU,

    PARAM_DIMS0
};

typedef enum {
    Z_DIM,
    X_DIM,
    X_REL,
    Y_REL,
    X_FRAC,
    X_FRAC_OF_MIN,
} DimensionalParamType;

typedef struct {
    DimensionalParamType type;
    gint id;
    gint master_id;
    gint master2_id;
} DimensionalParamInfo;

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
    GwyParamTable *table_type;
    GwyParamTable *table_generator[PAT_SYNTH_NTYPES];
    GwyParamTable *table_placement[PAT_SYNTH_NTYPES];
    GtkWidget *generator_vbox;
    GtkWidget *generator_widget;
    GtkWidget *placement_vbox;
    GtkWidget *placement_widget;
    GwyContainer *data;
    GwyDataField *template_;
    PatSynthType pattern_type;
} ModuleGUI;

typedef void (*DefineParamsFunc) (GwyParamDef *paramdef);
typedef void (*AppendGUIFunc)    (ModuleGUI *gui);
typedef void (*MakePatternFunc)  (ModuleArgs *args, GwyRandGenSet *rngset);

typedef struct {
    const gchar *name;
    DefineParamsFunc define_params;
    AppendGUIFunc append_gui;
    MakePatternFunc make_pattern;
    const DimensionalParamInfo *dim_params;
    guint ndimparams;
    gint height_param_id;
} PatSynthPattern;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             pat_synth           (GwyContainer *data,
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
static void             switch_pattern_type (ModuleGUI *gui);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static void             preview             (gpointer user_data);

DECLARE_PATTERN(staircase);
DECLARE_PATTERN(dblstair);
DECLARE_PATTERN(grating);
DECLARE_PATTERN(amphith);
DECLARE_PATTERN(rings);
DECLARE_PATTERN(star);
DECLARE_PATTERN(rholes);
DECLARE_PATTERN(pillars);

static const DimensionalParamInfo dim_params_staircase[] = {
    { X_DIM,  PARAM_STAIRCASE_PERIOD, -1,                     -1, },
    { X_DIM,  PARAM_STAIRCASE_SIGMA,  -1,                     -1, },
    { X_DIM,  PARAM_STAIRCASE_TAU,    -1,                     -1, },
    { Z_DIM,  PARAM_STAIRCASE_HEIGHT, -1,                     -1, },
    { X_FRAC, PARAM_STAIRCASE_SLOPE,  PARAM_STAIRCASE_PERIOD, -1, },
};

static const DimensionalParamInfo dim_params_dblstair[] = {
    { X_DIM, PARAM_DBLSTAIR_XPERIOD, -1, -1, },
    { X_DIM, PARAM_DBLSTAIR_YPERIOD, -1, -1, },
    { X_DIM, PARAM_DBLSTAIR_SIGMA,   -1, -1, },
    { X_DIM, PARAM_DBLSTAIR_TAU,     -1, -1, },
    { Z_DIM, PARAM_DBLSTAIR_HEIGHT,  -1, -1, },
};

static const DimensionalParamInfo dim_params_grating[] = {
    { X_DIM,  PARAM_GRATING_PERIOD,   -1,                   -1, },
    { X_DIM,  PARAM_GRATING_SIGMA,    -1,                   -1, },
    { X_DIM,  PARAM_GRATING_TAU,      -1,                   -1, },
    { Z_DIM,  PARAM_GRATING_HEIGHT,   -1,                   -1, },
    { X_FRAC, PARAM_GRATING_TOP_FRAC, PARAM_GRATING_PERIOD, -1, },
    { X_FRAC, PARAM_GRATING_SLOPE,    PARAM_GRATING_PERIOD, -1, },
};

static const DimensionalParamInfo dim_params_amphith[] = {
    { X_DIM,  PARAM_AMPHITH_FLAT,    -1,                 -1, },
    { X_DIM,  PARAM_AMPHITH_SIGMA,   -1,                 -1, },
    { X_DIM,  PARAM_AMPHITH_TAU,     -1,                 -1, },
    { X_REL,  PARAM_AMPHITH_XCENTER, -1,                 -1, },
    { Y_REL,  PARAM_AMPHITH_YCENTER, -1,                 -1, },
    { Z_DIM,  PARAM_AMPHITH_HEIGHT,  -1,                 -1, },
    { X_FRAC, PARAM_AMPHITH_SLOPE,   PARAM_AMPHITH_FLAT, -1, },
};

static const DimensionalParamInfo dim_params_rings[] = {
    { X_DIM,  PARAM_RINGS_PERIOD,   -1,                 -1, },
    { X_DIM,  PARAM_RINGS_SIGMA,    -1,                 -1, },
    { X_DIM,  PARAM_RINGS_TAU,      -1,                 -1, },
    { X_REL,  PARAM_RINGS_XCENTER,  -1,                 -1, },
    { Y_REL,  PARAM_RINGS_YCENTER,  -1,                 -1, },
    { Z_DIM,  PARAM_RINGS_HEIGHT,   -1,                 -1, },
    { X_FRAC, PARAM_RINGS_TOP_FRAC, PARAM_RINGS_PERIOD, -1, },
    { X_FRAC, PARAM_RINGS_SLOPE,    PARAM_RINGS_PERIOD, -1, },
};

static const DimensionalParamInfo dim_params_star[] = {
    { X_DIM, PARAM_STAR_EDGE_SHIFT, -1, -1, },
    { X_DIM, PARAM_STAR_SLOPE,      -1, -1, },
    { X_DIM, PARAM_STAR_SIGMA,      -1, -1, },
    { X_DIM, PARAM_STAR_TAU,        -1, -1, },
    { X_REL, PARAM_STAR_XCENTER,    -1, -1, },
    { Y_REL, PARAM_STAR_YCENTER,    -1, -1, },
    { Z_DIM, PARAM_STAR_HEIGHT,     -1, -1, },
};

static const DimensionalParamInfo dim_params_rholes[] = {
    { X_DIM,         PARAM_RHOLES_XPERIOD,   -1,                   -1,                   },
    { X_DIM,         PARAM_RHOLES_YPERIOD,   -1,                   -1,                   },
    { X_DIM,         PARAM_RHOLES_SIGMA,     -1,                   -1,                   },
    { X_DIM,         PARAM_RHOLES_TAU,       -1,                   -1,                   },
    { Z_DIM,         PARAM_RHOLES_HEIGHT,    -1,                   -1,                   },
    { X_FRAC,        PARAM_RHOLES_XTOP_FRAC, PARAM_RHOLES_XPERIOD, -1,                   },
    { X_FRAC,        PARAM_RHOLES_YTOP_FRAC, PARAM_RHOLES_YPERIOD, -1,                   },
    { X_FRAC_OF_MIN, PARAM_RHOLES_SLOPE,     PARAM_RHOLES_XPERIOD, PARAM_RHOLES_YPERIOD, },
};

static const DimensionalParamInfo dim_params_pillars[] = {
    { X_DIM,         PARAM_PILLARS_XPERIOD,   -1,                    -1,                    },
    { X_DIM,         PARAM_PILLARS_YPERIOD,   -1,                    -1,                    },
    { X_DIM,         PARAM_PILLARS_SIGMA,     -1,                    -1,                    },
    { X_DIM,         PARAM_PILLARS_TAU,       -1,                    -1,                    },
    { Z_DIM,         PARAM_PILLARS_HEIGHT,    -1,                    -1,                    },
    { X_FRAC_OF_MIN, PARAM_PILLARS_SIZE_FRAC, PARAM_PILLARS_XPERIOD, PARAM_PILLARS_YPERIOD, },
    { X_FRAC_OF_MIN, PARAM_PILLARS_SLOPE,     PARAM_PILLARS_XPERIOD, PARAM_PILLARS_YPERIOD, },
};

/* NB: The order of these and everything else must match the enums.  The GUI order is set up in define_params(). */
static const PatSynthPattern patterns[] = {
    { N_("Staircase"),           PATTERN_FUNCS(staircase), PARAM_STAIRCASE_HEIGHT, },
    { N_("Double staircase"),    PATTERN_FUNCS(dblstair),  PARAM_DBLSTAIR_HEIGHT,  },
    { N_("Grating"),             PATTERN_FUNCS(grating),   PARAM_GRATING_HEIGHT,   },
    { N_("Amphitheater"),        PATTERN_FUNCS(amphith),   PARAM_AMPHITH_HEIGHT,   },
    { N_("Concentric rings"),    PATTERN_FUNCS(rings),     PARAM_RINGS_HEIGHT,     },
    { N_("Siemens star"),        PATTERN_FUNCS(star),      PARAM_STAR_HEIGHT,      },
    { N_("Holes (rectangular)"), PATTERN_FUNCS(rholes),    PARAM_RHOLES_HEIGHT,    },
    { N_("Pillars"),             PATTERN_FUNCS(pillars),   PARAM_PILLARS_HEIGHT,   },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates surfaces representing simple patterns (staircase, amphitheater, grating, holes and pillars, ...)."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti)",
    "2010",
};

GWY_MODULE_QUERY2(module_info, pat_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("pat_synth",
                              (GwyProcessFunc)&pat_synth,
                              N_("/S_ynthetic/_Pattern..."),
                              GWY_STOCK_SYNTHETIC_PATTERN,
                              RUN_MODES,
                              0,
                              N_("Generate patterned surface"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    /* Define GUI feature order. */
    static GwyEnum types[] = {
        { NULL, PAT_SYNTH_STAIRCASE, },
        { NULL, PAT_SYNTH_GRATING,   },
        { NULL, PAT_SYNTH_AMPHITH,   },
        { NULL, PAT_SYNTH_RINGS,     },
        { NULL, PAT_SYNTH_STAR,      },
        { NULL, PAT_SYNTH_RHOLES,    },
        { NULL, PAT_SYNTH_PILLARS,   },
        { NULL, PAT_SYNTH_DBLSTAIR,  },
    };
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    gwy_enum_fill_from_struct(types, G_N_ELEMENTS(types), patterns, sizeof(PatSynthPattern),
                              G_STRUCT_OFFSET(PatSynthPattern, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Pattern"),
                              types, G_N_ELEMENTS(types), PAT_SYNTH_STAIRCASE);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    for (i = 0; i < G_N_ELEMENTS(patterns); i++)
        patterns[i].define_params(paramdef);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
pat_synth(GwyContainer *data, GwyRunType runtype)
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
    GwyParamTable *table;
    GwyDialog *dialog;
    GtkWidget *hbox, *dataview;
    GtkNotebook *notebook;
    ModuleGUI gui;
    guint i;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.template_ = args->field;
    gui.pattern_type = gwy_params_get_enum(args->params, PARAM_TYPE);

    if (gui.template_)
        args->field = gwy_synth_make_preview_data_field(gui.template_, PREVIEW_SIZE);
    else
        args->field = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, PREVIEW_SIZE, TRUE);
    args->result = gwy_synth_make_result_data_field(args->field, args->params, TRUE);

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    if (gui.template_)
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Pattern"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    for (i = 0; i < G_N_ELEMENTS(patterns); i++) {
        const PatSynthPattern *pattern = patterns + i;

        gui.table_generator[i] = gwy_param_table_new(args->params);
        g_object_ref_sink(gui.table_generator[i]);
        gui.table_placement[i] = table = gwy_param_table_new(args->params);
        g_object_ref_sink(gui.table_placement[i]);
        pattern->append_gui(&gui);

        gwy_param_table_append_header(table, -1, _("Options"));
        gwy_param_table_append_seed(table, PARAM_SEED);
        gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
        gwy_param_table_append_separator(table);
        gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    }

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, generator_tab_new(&gui), gtk_label_new(_("Generator")));
    gtk_notebook_append_page(notebook, placement_tab_new(&gui), gtk_label_new(_("Placement")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);
    switch_pattern_type(&gui);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_type, "param-changed", G_CALLBACK(param_changed), &gui);
    for (i = 0; i < G_N_ELEMENTS(patterns); i++) {
        g_signal_connect_swapped(gui.table_generator[i], "param-changed", G_CALLBACK(param_changed), &gui);
        g_signal_connect_swapped(gui.table_placement[i], "param-changed", G_CALLBACK(param_changed), &gui);
    }
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    for (i = 0; i < G_N_ELEMENTS(patterns); i++) {
        g_object_unref(gui.table_generator[i]);
        g_object_unref(gui.table_placement[i]);
    }

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

    gui->generator_vbox = gwy_vbox_new(4);

    table = gui->table_type = gwy_param_table_new(gui->args->params);
    gwy_param_table_append_combo(table, PARAM_TYPE);
    gwy_param_table_set_no_reset(table, PARAM_TYPE, TRUE);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui->table_generator[gui->pattern_type];
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gui->generator_widget = gwy_param_table_widget(table);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gui->generator_widget, FALSE, FALSE, 0);

    return gui->generator_vbox;
}

static GtkWidget*
placement_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    gui->placement_vbox = gwy_vbox_new(0);

    table = gui->table_placement[gui->pattern_type];
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gui->placement_widget = gwy_param_table_widget(table);
    gtk_box_pack_start(GTK_BOX(gui->placement_vbox), gui->placement_widget, FALSE, FALSE, 0);

    return gui->placement_vbox;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    const PatSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    GwyParamTable *table_generator = gui->table_generator[type], *table_placement = gui->table_placement[type];
    const DimensionalParamInfo *dinfo = patterns[type].dim_params;
    guint i, n = patterns[type].ndimparams;
    GwySIValueFormat *vf = NULL;
    gboolean update_fractional = FALSE;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_TYPE) {
        if (type != gui->pattern_type) {
            switch_pattern_type(gui);
            id = -1;
        }
    }

    /* Update height-like parameters. */
    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        for (i = 0; i < n; i++) {
            if (dinfo[i].type == Z_DIM)
                gwy_synth_update_value_unitstrs(table_generator, &dinfo[i].id, 1);
        }
        /* NB: We assume there is one the table for each pattern. */
        gwy_synth_update_like_current_button_sensitivity(table_generator, BUTTON_LIKE_CURRENT_IMAGE);
    }

    /* Update lateral parameters.   These can be in either table; try both. */
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        for (i = 0; i < n; i++) {
            if (dinfo[i].type == X_DIM) {
                if (gwy_param_table_exists(table_generator, dinfo[i].id))
                    gwy_synth_update_lateral_alts(table_generator, &dinfo[i].id, 1);
                else if (gwy_param_table_exists(table_placement, dinfo[i].id))
                    gwy_synth_update_lateral_alts(table_placement, &dinfo[i].id, 1);
                else {
                    g_warning("Cannot find x-like parameter %d in any table.", dinfo[i].id);
                }
            }
        }
        update_fractional = TRUE;
    }

    /* Update lateral parameters that are fractions of other lateral parameters.  Or, in the most convoluted case,
     * fractions of minimuma of two other parameters.  */
    for (i = 0; i < n; i++) {
        gdouble master_value;

        if (dinfo[i].type == X_FRAC && (update_fractional || dinfo[i].master_id == id))
            master_value = gwy_params_get_double(params, dinfo[i].master_id);
        else if (dinfo[i].type == X_FRAC_OF_MIN && (update_fractional
                                                    || dinfo[i].master_id == id || dinfo[i].master2_id == id)) {
            master_value = fmin(gwy_params_get_double(params, dinfo[i].master_id),
                                gwy_params_get_double(params, dinfo[i].master2_id));
        }
        else
            continue;

        /* XXX: This replicates gwy_synth_update_lateral_alts() logic. */
        if (!vf) {
            gint power10xy;
            GwySIUnit *unit = gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT, &power10xy);
            gdouble q = pow10(power10xy);
            gint xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
            gdouble xreal = gwy_params_get_double(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL)*q;
            gdouble dx = xreal/xres;

            vf = gwy_si_unit_get_format_with_resolution(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, xreal, dx, NULL);
            /* Real value but in prefixed diplay units (not base SI).  The fractional value has the same units and
             * its [0,1] range maps to [0,master_value] range.  Remember it in vf->magnitude. */
            vf->magnitude /= dx;
        }

        gwy_param_table_alt_set_linear(table_generator, dinfo[i].id, master_value/vf->magnitude, 0.0, vf->units);
    }
    GWY_SI_VALUE_FORMAT_FREE(vf);

    /* Update lateral parameters that are fractions of image size.  The logic is as above, just a bit more
     * straightforward. */
    for (i = 0; i < n; i++) {
        if (dinfo[i].type == X_REL || dinfo[i].type == Y_REL) {
            gint power10xy;
            GwySIUnit *unit = gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT, &power10xy);
            gdouble real, h, q = pow10(power10xy);
            gint res;

            if (dinfo[i].type == X_REL) {
                res = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
                real = gwy_params_get_double(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL)*q;
            }
            else {
                res = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YRES);
                real = gwy_params_get_double(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YREAL)*q;
            }
            h = real/res;
            vf = gwy_si_unit_get_format_with_resolution(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, h, NULL);
            gwy_param_table_alt_set_linear(table_placement, dinfo[i].id, real/vf->magnitude, 0.0, vf->units);
            GWY_SI_VALUE_FORMAT_FREE(vf);
        }
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
switch_pattern_type(ModuleGUI *gui)
{
    PatSynthType type = gwy_params_get_enum(gui->args->params, PARAM_TYPE);
    GwyParamTable *table;

    gwy_dialog_remove_param_table(GWY_DIALOG(gui->dialog), gui->table_generator[gui->pattern_type]);
    gwy_dialog_remove_param_table(GWY_DIALOG(gui->dialog), gui->table_placement[gui->pattern_type]);
    if (gui->generator_widget) {
        gtk_widget_destroy(gui->generator_widget);
        gui->generator_widget = NULL;
    }
    if (gui->placement_widget) {
        gtk_widget_destroy(gui->placement_widget);
        gui->placement_widget = NULL;
    }

    gui->pattern_type = type;

    table = gui->table_generator[gui->pattern_type];
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gui->generator_widget = gwy_param_table_widget(table);
    gtk_widget_show_all(gui->generator_widget);
    gtk_box_pack_start(GTK_BOX(gui->generator_vbox), gui->generator_widget, FALSE, FALSE, 0);

    table = gui->table_placement[gui->pattern_type];
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gui->placement_widget = gwy_param_table_widget(table);
    gtk_widget_show_all(gui->placement_widget);
    gtk_box_pack_start(GTK_BOX(gui->placement_vbox), gui->placement_widget, FALSE, FALSE, 0);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;

    if (response == GWY_RESPONSE_SYNTH_INIT_Z) {
        PatSynthType type = gwy_params_get_enum(gui->args->params, PARAM_TYPE);
        gdouble zscale = args->zscale;
        gint power10z, id = patterns[type].height_param_id;

        if (zscale > 0.0) {
            gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
            gwy_param_table_set_double(gui->table_generator[type], id, zscale/pow10(power10z));
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
    PatSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    const PatSynthPattern *pattern = patterns + type;
    GwyRandGenSet *rngset;

    if (args->field && do_initialise)
        gwy_data_field_copy(args->field, args->result, FALSE);
    else
        gwy_data_field_clear(args->result);

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));
    pattern->make_pattern(args, rngset);
    gwy_rand_gen_set_free(rngset);
}

/************************************************************************************************************
 *
 * Common helpers
 *
 ************************************************************************************************************/

/* Iterating through square in a spiral fashion from the origin to preserve the centre conrer if it's randomly
 * generated.  Field @k holds the current index in the two-dimensional array.  */
typedef struct {
    gint n;
    gint i, j, k;
    gint istep, jstep;
    gint s, segmentend, ntotalstep;
} GrowingIter;

static inline void
growing_iter_init(GrowingIter *giter, guint n)
{
    giter->n = n;
    giter->j = giter->i = 0;
    giter->istep = 0;
    giter->jstep = -1;
    giter->ntotalstep = n*n;
    giter->segmentend = MIN(1, n*n);
    giter->s = 0;
    giter->k = (n/2 - giter->i)*n + (giter->j + n/2);
}

static inline gboolean
growing_iter_next(GrowingIter *giter)
{
    giter->i += giter->istep;
    giter->j += giter->jstep;
    giter->k = (giter->n/2 - giter->i)*giter->n + (giter->j + giter->n/2);
    giter->s++;
    if (giter->s == giter->segmentend) {
        if (giter->s == giter->ntotalstep)
            return FALSE;

        if (giter->i == giter->j + 1) {
            giter->istep = 1;
            giter->jstep = 0;
            giter->segmentend = 1 - 2*giter->i;
        }
        else if (giter->i == giter->j) {
            giter->istep = -1;
            giter->jstep = 0;
            giter->segmentend = 2*giter->i;
        }
        else if (giter->j > 0) {
            giter->istep = 0;
            giter->jstep = -1;
            giter->segmentend = 2*giter->j + 1;
        }
        else {
            giter->istep = 0;
            giter->jstep = 1;
            giter->segmentend = 2*giter->i;
        }
        giter->segmentend += giter->s;
        giter->segmentend = MIN(giter->segmentend, giter->ntotalstep);
    }
    return TRUE;
}

static guint
bisect_lower(const gdouble *a, guint n, gdouble x)
{
    guint lo = 0, hi = n-1;

    if (G_UNLIKELY(x < a[lo]))
        return 0;
    if (G_UNLIKELY(x >= a[hi]))
        return n-1;

    while (hi - lo > 1) {
        guint mid = (hi + lo)/2;

        if (x < a[mid])
            hi = mid;
        else
            lo = mid;
    }

    return lo;
}

static inline gdouble
superellipse(gdouble x, gdouble y, gdouble invpower)
{
    gdouble m, M;

    x = fabs(x);
    y = fabs(y);
    m = fmin(x, y);
    M = fmax(x, y);
    /* This is t = (x^n + y^n)^{1/n}.
     * However, we rewrite is as x(1 + (y/x)^n)^{1/n} for x ≥ y, making the parenthesis converging quickly to 1 for
     * large n, making all the powers numerically safe. */
    return M*pow(1.0 + pow(m/M, 2.0/invpower), invpower/2.0);
}

static gdouble
parabolic_transform(gdouble x, gdouble alpha)
{
    if (alpha > 0.0)
        return (1.0 - alpha)*x + alpha*x*x;
    if (alpha < 0.0) {
        gdouble a1 = 1.0 + alpha;
        return 2.0*x/(sqrt(a1*a1 - 4.0*alpha*x) + a1);
    }
    return x;
}

/* Random number within the range [-1/2, 1/2] going from bell shape for small s to uniform for large s. */
static inline gdouble
random_constrained_shift(GwyRandGenSet *rngset, guint rngid, gdouble s)
{
    gdouble r, ss;

    r = gwy_rand_gen_set_double(rngset, rngid);
    ss = s*4.6;
    if (ss < 1.0)
        return ss/G_PI * asin(2.0*r - 1.0);
    return 0.5/asin(1.0/ss) * asin((2.0*r - 1.0)/ss);
}

static GwyDataField*
make_displacement_map(guint xres, guint yres, gdouble sigma, gdouble tau, GwyRandGenSet *rngset, guint rngid)
{
    GwyDataField *field = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);
    gwy_data_field_synth_gaussian_displacement(field, sigma, tau, gwy_rand_gen_set_rng(rngset, rngid));
    return field;
}

/* Usually tmap is the same data field as displacement_x. */
static void
displacement_to_t_linear(GwyDataField *tmap,
                         GwyDataField *displacement_x, GwyDataField *displacement_y,
                         gdouble angle, gdouble period)
{
    gdouble *xdata = gwy_data_field_get_data(displacement_x);
    gdouble *ydata = gwy_data_field_get_data(displacement_y);
    gdouble *tdata = gwy_data_field_get_data(tmap);
    guint xres = gwy_data_field_get_xres(tmap);
    guint yres = gwy_data_field_get_yres(tmap);
    gdouble c = cos(angle), s = sin(angle);
    gdouble toff = 0.5*(s*(yres - 1) - c*(xres - 1));
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xdata,ydata,tdata,toff,c,s,period,xres,yres)
#endif
    for (i = 0; i < yres; i++) {
        gdouble *xrow = xdata + i*xres;
        gdouble *yrow = ydata + i*xres;
        gdouble *trow = tdata + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble t = toff;

            t += (j + xrow[j])*c - (i + yrow[j])*s;
            t /= period;
            trow[j] = t;
        }
    }
}

/* Usually tmap is the same data field as displacement_x. */
static void
displacement_to_t_superellipse(GwyDataField *tmap,
                               GwyDataField *displacement_x, GwyDataField *displacement_y,
                               gdouble angle, gdouble xcentre, gdouble ycentre,
                               gdouble invpower, gdouble radius)
{
    gdouble *xdata = gwy_data_field_get_data(displacement_x);
    gdouble *ydata = gwy_data_field_get_data(displacement_y);
    gdouble *tdata = gwy_data_field_get_data(tmap);
    guint xres = gwy_data_field_get_xres(tmap);
    guint yres = gwy_data_field_get_yres(tmap);
    gdouble c = cos(angle), s = sin(angle);
    guint i;

    /* Account for percieved step direction being along the diagonals, not along the axes, and so the steps seen as
     * narrower. */
    if (invpower > 1.0)
        radius *= pow(2.0, invpower/2.0)/G_SQRT2;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xdata,ydata,tdata,xcentre,ycentre,invpower,radius,c,s,xres,yres)
#endif
    for (i = 0; i < yres; i++) {
        gdouble *xrow = xdata + i*xres;
        gdouble *yrow = ydata + i*xres;
        gdouble *trow = tdata + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble x, y, t;

            x = j + xrow[j] - 0.5*(xres - 1) - xres*xcentre;
            y = i + yrow[j] - 0.5*(yres - 1) - yres*ycentre;
            t = x*c - y*s;
            y = x*s + y*c;
            x = t;

            if (invpower < 0.000001)
                t = fmax(fabs(x), fabs(y));
            else if (invpower > 1.999999)
                t = fabs(x) + fabs(y);
            else if (invpower > 0.999999 && invpower < 1.000001)
                t = sqrt(x*x + y*y);
            else
                t = superellipse(x, y, invpower);
            trow[j] = t/radius;
        }
    }
}

/* Usually umap and vmap are the same data field as displacement_x and y. */
static void
displacement_to_uv_linear(GwyDataField *umap, GwyDataField *vmap,
                          GwyDataField *displacement_x, GwyDataField *displacement_y,
                          gdouble angle, gdouble periodu, gdouble periodv)
{
    gdouble *xdata = gwy_data_field_get_data(displacement_x);
    gdouble *ydata = gwy_data_field_get_data(displacement_y);
    gdouble *udata = gwy_data_field_get_data(umap);
    gdouble *vdata = gwy_data_field_get_data(vmap);
    guint xres = gwy_data_field_get_xres(umap);
    guint yres = gwy_data_field_get_yres(umap);
    gdouble c = cos(angle), s = sin(angle);
    gdouble uoff = 0.5*(s*(yres - 1) - c*(xres - 1));
    gdouble voff = -0.5*(c*(xres - 1) + s*(xres - 1));
    guint i;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(xdata,ydata,udata,vdata,uoff,voff,c,s,periodu,periodv,xres,yres)
#endif
    for (i = 0; i < yres; i++) {
        gdouble *xrow = xdata + i*xres;
        gdouble *yrow = ydata + i*xres;
        gdouble *urow = udata + i*xres;
        gdouble *vrow = vdata + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble x = j + xrow[j], y = i + yrow[j];

            urow[j] = (x*c - y*s + uoff)/periodu;
            vrow[j] = (x*s + y*c + voff)/periodv;
        }
    }
}

static guint
find_t_range(GwyDataField *tmap, gboolean positive)
{
    gdouble tmin, tmax, tt;
    guint n;

    gwy_data_field_get_min_max(tmap, &tmin, &tmax);
    if (positive) {
        /* Cover the range [0, tmax]. */
        g_warn_if_fail(tmin >= 0.0);
        n = (GWY_ROUND(tmax + 3.5) | 1);
    }
    else {
        /* Cover a symmetrical range (presumably tmin ≈ -tmax). */
        tt = fmax(tmax, -tmin);
        n = 2*GWY_ROUND(tt + 3.5) + 1;
    }
    gwy_debug("tmin = %g, tmax = %g, n = %u", tmin, tmax, n);

    return n;
}

static gdouble*
make_values_1d(guint n, gdouble mean, gdouble noise,
               GwyRandGenSet *rngset, guint rngid)
{
    gdouble *values;
    guint i, centre = n/2;
    gdouble r;

    g_return_val_if_fail(n & 1, NULL);
    values = g_new(gdouble, n);

    values[centre] = mean*gwy_rand_gen_set_multiplier(rngset, rngid, noise);
    for (i = 1; i <= n/2; i++) {
        r = gwy_rand_gen_set_multiplier(rngset, rngid, noise);
        values[centre + i] = mean*r;
        r = gwy_rand_gen_set_multiplier(rngset, rngid, noise);
        values[centre - i] = mean*r;
    }

    return values;
}

static gdouble*
make_values_2d(guint n, gdouble mean, gdouble noise,
               GwyRandGenSet *rngset, guint rngid)
{
    GrowingIter giter;
    gdouble *values;
    gdouble r;

    g_return_val_if_fail(n & 1, NULL);
    values = g_new(gdouble, n*n);
    growing_iter_init(&giter, n);

    do {
        r = gwy_rand_gen_set_multiplier(rngset, rngid, noise);
        values[giter.k] = mean*r;
    } while (growing_iter_next(&giter));

    return values;
}

static gdouble*
make_values_2d_gaussian(guint n, gdouble mean, gdouble noise,
                        GwyRandGenSet *rngset, guint rngid)
{
    GrowingIter giter;
    gdouble *values;
    gdouble r;

    g_return_val_if_fail(n & 1, NULL);
    values = g_new(gdouble, n*n);
    growing_iter_init(&giter, n);

    do {
        r = gwy_rand_gen_set_gaussian(rngset, rngid, noise);
        values[giter.k] = mean + r;
    } while (growing_iter_next(&giter));

    return values;
}

static gdouble*
distribute_left_to_left_and_right(gdouble *left, guint n, gdouble asymm)
{
    gdouble *right;
    guint i;

    right = g_new(gdouble, n);
    for (i = 0; i < n; i++) {
        right[i] = 0.5*(1.0 + asymm)*left[i];
        left[i] = 0.5*(1.0 - asymm)*left[i];
    }

    return right;
}

static void
transform_to_scaled_grating(gdouble *abscissae, gdouble *widths, gdouble *leftslopes, gdouble *rightslopes,
                            guint n, gboolean zero_based)
{
    gdouble *newabscissae;
    gdouble w, c, a0 = sizeof("GCC is broken");
    guint i;

    newabscissae = g_new(gdouble, n);
    if (zero_based) {
        a0 = abscissae[0];
        abscissae[0] = -abscissae[-1];
    }
    for (i = 0; i < n; i++) {
        if (!i) {
            w = abscissae[i+1] - abscissae[i];
            c = abscissae[i];
        }
        else if (i == n-1) {
            w = abscissae[i] - abscissae[i-1];
            c = abscissae[i];
        }
        else {
            w = 0.5*(abscissae[i+1] - abscissae[i-1]);
            c = 0.5*abscissae[i] + 0.25*(abscissae[i+1] + abscissae[i-1]);
        }
        widths[i] *= w;
        leftslopes[i] *= w;
        rightslopes[i] *= w;
        newabscissae[i] = c + 0.5*(leftslopes[i] - rightslopes[i]);
    }
    if (zero_based)
        newabscissae[0] = a0;
    gwy_assign(abscissae, newabscissae, n);
    g_free(newabscissae);
}

static void
transform_to_sine_cosine(gdouble *angles_sines, gdouble *cosines, guint n)
{
    guint i;

    for (i = 0; i < n; i++) {
        cosines[i] = cos(angles_sines[i]);
        angles_sines[i] = sin(angles_sines[i]);
    }
}

static gdouble*
make_positions_1d_linear(guint n, gdouble noise,
                         GwyRandGenSet *rngset, guint rngid)
{
    gdouble *abscissae;
    guint i, centre = n/2;
    gdouble r;

    g_return_val_if_fail(n & 1, NULL);
    abscissae = g_new(gdouble, n);

    /* Fill the positions from centre for stability. */
    r = random_constrained_shift(rngset, rngid, noise);
    abscissae[centre] = r;
    for (i = 1; i <= n/2; i++) {
        r = random_constrained_shift(rngset, rngid, noise);
        abscissae[centre + i] = r + i;
        r = random_constrained_shift(rngset, rngid, noise);
        abscissae[centre - i] = r - i;
    }

    return abscissae;
}

static gdouble*
make_positions_1d_radial(guint n, gdouble noise, gdouble scale, gdouble parabolicity,
                         GwyRandGenSet *rngset, guint rngid)
{
    gdouble *radii;
    gdouble r, a;
    guint i;

    g_return_val_if_fail(n & 1, NULL);
    radii = g_new(gdouble, n);

    radii[0] = -100.0;
    for (i = 1; i < n; i++) {
        r = random_constrained_shift(rngset, rngid, noise);
        a = parabolic_transform(i/scale, -parabolicity)*scale;
        radii[i] = r + a;
    }

    return radii;
}

static gdouble*
make_positions_2d_linear(guint n, gdouble noise, gboolean is_y,
                         GwyRandGenSet *rngset, guint rngid)
{
    gdouble *abscissae;
    gdouble r;
    GrowingIter giter;

    g_return_val_if_fail(n & 1, NULL);
    abscissae = g_new(gdouble, n*n);
    growing_iter_init(&giter, n);

    do {
        r = random_constrained_shift(rngset, rngid, noise);
        abscissae[giter.k] = r + (is_y ? -giter.i : giter.j);
    } while (growing_iter_next(&giter));

    return abscissae;
}

/* abscissae[] has n elements, but heights[] have n+1. */
static gdouble*
make_heights_staircase(const gdouble *abscissae, guint n,
                       gdouble h, gdouble noise, gboolean keep_slope, gboolean zero_based,
                       GwyRandGenSet *rngset, guint rngid)
{
    gdouble *heights;
    guint i, centre = n/2;
    gdouble r;

    g_return_val_if_fail(n & 1, NULL);
    heights = g_new(gdouble, n+1);

    /* Generate n steps. */
    if (keep_slope) {
        heights[0] = heights[n] = h;
        for (i = 1; i < n; i++)
            heights[i] = 0.5*h*(abscissae[i+1] - abscissae[i-1]);
    }
    else {
        for (i = 1; i <= n; i++)
            heights[i] = h;
    }

    heights[centre+1] *= gwy_rand_gen_set_multiplier(rngset, rngid, noise);
    for (i = 1; i <= n/2; i++) {
        r = gwy_rand_gen_set_multiplier(rngset, rngid, noise);
        heights[centre+1 + i] *= r;
        r = gwy_rand_gen_set_multiplier(rngset, rngid, noise);
        heights[centre+1 - i] *= r;
    }

    /* Convert them to n+1 absolute heights. */
    heights[0] = 0.0;
    for (i = 1; i <= n; i++)
        heights[i] += heights[i-1];

    if (!zero_based) {
        h = 0.5*(heights[centre] + heights[centre+1]);
        for (i = 0; i <= n; i++)
            heights[i] -= h;
    }

    return heights;
}

static inline gdouble
step_func(gdouble x, gdouble w)
{
    if (G_LIKELY(w != 0.0))
        return fmin(fmax(x/w + 0.5, 0.0), 1.0);
    return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0);
}

static inline gdouble
ridge_func(gdouble x, gdouble w, gdouble sleft, gdouble sright)
{
    if (x < -0.5*w) {
        x += 0.5*w;
        return x <= -sleft ? 0.0 : 1.0 + x/sleft;
    }
    if (x > 0.5*w) {
        x -= 0.5*w;
        return x >= sright ? 0.0 : 1.0 - x/sright;
    }
    return 1.0;
}

/* It is actually inverted hole to keep the sign convention. */
static inline gdouble
hole_func(gdouble x, gdouble y, gdouble ax, gdouble ay, gdouble r, gdouble s)
{
    gdouble d;

    x = fabs(x);
    y = fabs(y);

    if (ax < ay) {
        GWY_SWAP(gdouble, ax, ay);
        GWY_SWAP(gdouble, x, y);
    }
    r = fmin(r, ay);

    if (x <= ax - r && x-y <= ax - ay)
        d = ay - y;
    else if (y <= ay - r && x-y >= ax - ay)
        d = ax - x;
    else {
        x -= ax - r;
        y -= ay - r;
        d = r - sqrt(x*x + y*y);
    }

    if (s == 0.0)
        return d > 0.0;

    return fmax(fmin(d/s, 1.0), 0.0);
}

static void
render_staircase(GwyDataField *image, GwyDataField *tmap,
                 const gdouble *abscissae, const gdouble *heights, const gdouble *slopes, guint n)
{
    const gdouble *tdata;
    gdouble *data;
    guint i, xres, yres;

    xres = gwy_data_field_get_xres(image);
    yres = gwy_data_field_get_yres(image);
    tdata = gwy_data_field_get_data_const(tmap);
    data = gwy_data_field_get_data(image);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,tdata,abscissae,heights,slopes,xres,yres,n)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *trow = tdata + i*xres;
        gdouble *row = data + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble z, t, s, x, w;
            guint k, m, mfrom, mto;

            t = trow[j];
            k = bisect_lower(abscissae, n, t);
            mfrom = (k < 1 ? 0: k-1);
            mto = (k >= n-2 ? n-1 : k+2);
            z = heights[mfrom];
            for (m = mfrom; m <= mto; m++) {
                s = heights[m+1] - heights[m];
                x = abscissae[m];
                w = slopes[m];
                z += s*step_func(t - x, w);
            }
            row[j] += z;
        }
    }
}

static void
render_double_staircase(GwyDataField *image, GwyDataField *umap, GwyDataField *vmap,
                        const gdouble *abscissaeu, const gdouble *abscissaev, const gdouble *heights,
                        guint nu, guint nv)
{
    const gdouble *udata, *vdata;
    gdouble *data;
    guint i, n, xres, yres;

    xres = gwy_data_field_get_xres(image);
    yres = gwy_data_field_get_yres(image);
    udata = gwy_data_field_get_data_const(umap);
    vdata = gwy_data_field_get_data_const(vmap);
    data = gwy_data_field_get_data(image);
    n = ((nu + nv) | 1);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,udata,vdata,abscissaeu,abscissaev,heights,xres,yres,nu,nv,n)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *urow = udata + i*xres;
        const gdouble *vrow = vdata + i*xres;
        gdouble *row = data + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble z, u, v;
            guint ku, kv;

            u = urow[j];
            v = vrow[j];
            ku = bisect_lower(abscissaeu, nu, u);
            kv = bisect_lower(abscissaev, nv, v);
            z = heights[MIN(ku + kv, n-1)];
            row[j] += z;
        }
    }
}

static void
render_grating(GwyDataField *image, GwyDataField *tmap,
               const gdouble *abscissae, const gdouble *widths, const gdouble *heights,
               const gdouble *leftslopes, const gdouble *rightslopes,
               gint sign, guint n)
{
    const gdouble *tdata;
    gdouble *data;
    guint i, xres, yres;

    xres = gwy_data_field_get_xres(image);
    yres = gwy_data_field_get_yres(image);
    tdata = gwy_data_field_get_data_const(tmap);
    data = gwy_data_field_get_data(image);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,tdata,abscissae,heights,widths,leftslopes,rightslopes,xres,yres,sign,n)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *trow = tdata + i*xres;
        gdouble *row = data + i*xres;
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble z, t, h, sl, sr, x, w;
            guint k, m, mfrom, mto;

            t = trow[j];
            k = bisect_lower(abscissae, n, t);
            mfrom = (k < 1 ? 0: k-1);
            mto = (k >= n-2 ? n-1 : k+2);
            z = 0;
            for (m = mfrom; m <= mto; m++) {
                x = abscissae[m];
                h = heights[m];
                w = widths[m];
                sl = leftslopes[m];
                sr = rightslopes[m];
                z = fmax(z, h*ridge_func(t - x, w, sl, sr));
            }
            row[j] += sign*z;
        }
    }
}

static void
render_holes(GwyDataField *image, GwyDataField *umap, GwyDataField *vmap,
             const gdouble *abscissaeu, const gdouble *abscissaev,
             const gdouble *xsizes, const gdouble *ysizes,
             const gdouble *slopes, const gdouble *roundnesses, const gdouble *heights,
             guint n, gdouble aratio)
{
    const gdouble *udata, *vdata;
    gdouble *data;
    guint i, xres, yres;
    gdouble ax, ay;

    if (aratio <= 1.0) {
        ax = 1.0;
        ay = aratio;
    }
    else {
        ax = 1.0/aratio;
        ay = 1.0;
    }

    xres = gwy_data_field_get_xres(image);
    yres = gwy_data_field_get_yres(image);
    udata = gwy_data_field_get_data_const(umap);
    vdata = gwy_data_field_get_data_const(vmap);
    data = gwy_data_field_get_data(image);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,udata,vdata,abscissaeu,abscissaev,xsizes,ysizes,slopes,roundnesses,heights,xres,yres,n,ax,ay)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *urow = udata + i*xres;
        const gdouble *vrow = vdata + i*xres;
        gdouble *row = data + i*xres;
        gdouble amin = fmin(ax, ay);
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble z = 0.0, u, v, x, y, h, lx, ly, s, r;
            guint ku, kv, mufrom, muto, mvfrom, mvto, mu, mv, kk;

            u = urow[j];
            v = vrow[j];
            ku = (guint)(n/2 + floor(u));
            kv = (guint)(n/2 + floor(v));
            ku = MIN(ku, n-1);
            kv = MIN(kv, n-1);
            mufrom = (ku < 1 ? 0: ku-1);
            muto = (ku >= n-2 ? n-1 : ku+2);
            mvfrom = (kv < 1 ? 0: kv-1);
            mvto = (kv >= n-2 ? n-1 : kv+2);
            for (mv = mvfrom; mv <= mvto; mv++) {
                for (mu = mufrom; mu <= muto; mu++) {
                    kk = mv*n + mu;
                    h = heights[kk];
                    s = slopes[kk] * amin;
                    r = roundnesses[kk] * amin;
                    y = v - abscissaev[kk];
                    y = (2.0*y - 1) * ay;
                    ly = ysizes[kk] * ay;
                    x = u - abscissaeu[kk];
                    x = (2.0*x - 1) * ax;
                    lx = xsizes[kk] * ax;
                    z = fmax(z, h*hole_func(x, y, lx, ly, r, s));
                }
            }
            row[j] -= z;
        }
    }
}

static void
render_pillars(GwyDataField *image, GwyDataField *umap, GwyDataField *vmap,
               PillarShapeType shape,
               const gdouble *abscissaeu, const gdouble *abscissaev,
               const gdouble *sizes, const gdouble *slopes,
               const gdouble *sines, const gdouble *cosines,
               const gdouble *heights,
               guint n, gdouble aratio)
{
    const gdouble *udata, *vdata;
    gdouble *data;
    guint i, xres, yres;
    gdouble ax, ay;

    if (aratio <= 1.0) {
        ax = 1.0;
        ay = aratio;
    }
    else {
        ax = 1.0/aratio;
        ay = 1.0;
    }

    xres = gwy_data_field_get_xres(image);
    yres = gwy_data_field_get_yres(image);
    udata = gwy_data_field_get_data_const(umap);
    vdata = gwy_data_field_get_data_const(vmap);
    data = gwy_data_field_get_data(image);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,udata,vdata,shape,abscissaeu,abscissaev,sizes,slopes,sines,cosines,heights,xres,yres,n,ax,ay)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *urow = udata + i*xres;
        const gdouble *vrow = vdata + i*xres;
        gdouble *row = data + i*xres;
        gdouble amin = fmin(ax, ay);
        guint j;

        for (j = 0; j < xres; j++) {
            gdouble z = 0.0, u, v, h, w, s, t, x, y, ca, sa;
            guint ku, kv, mufrom, muto, mvfrom, mvto, mu, mv, kk;

            u = urow[j];
            v = vrow[j];
            ku = (guint)(n/2 + floor(u));
            kv = (guint)(n/2 + floor(v));
            ku = MIN(ku, n-1);
            kv = MIN(kv, n-1);
            mufrom = (ku < 1 ? 0: ku-1);
            muto = (ku >= n-2 ? n-1 : ku+2);
            mvfrom = (kv < 1 ? 0: kv-1);
            mvto = (kv >= n-2 ? n-1 : kv+2);
            for (mv = mvfrom; mv <= mvto; mv++) {
                for (mu = mufrom; mu <= muto; mu++) {
                    kk = mv*n + mu;
                    h = heights[kk];
                    w = sizes[kk] * amin;
                    s = slopes[kk] * amin;
                    y = v - abscissaev[kk];
                    y = (2.0*y - 1) * ay;
                    x = u - abscissaeu[kk];
                    x = (2.0*x - 1) * ax;

                    if (shape == PILLAR_SHAPE_CIRCLE)
                        t = sqrt(x*x + y*y);
                    else {
                        ca = cosines[kk];
                        sa = sines[kk];
                        if (shape == PILLAR_SHAPE_SQUARE)
                            t = fmax(fabs(ca*x - sa*y), fabs(sa*x + ca*y));
                        else {
                            t = ca*x - sa*y;
                            y = sa*x + ca*y;
                            x = t;
                            t = fmax(fabs(x), fmax(0.5*fabs(x + GWY_SQRT3*y), 0.5*fabs(x - GWY_SQRT3*y)));
                        }
                    }

                    if (s == 0.0)
                        t = (t <= w);
                    else
                        t = fmax(fmin(1.0 + (w - t)/s, 1.0), 0.0);
                    z = fmax(z, h*t);
                }
            }
            row[j] += z;
        }
    }
}

static void
append_gui_placement_common(GwyParamTable *table,
                            gint angle_id, gint sigma_id, gint tau_id, gint xcenter_id, gint ycenter_id)
{
    gwy_param_table_append_header(table, -1, _("Orientation"));
    gwy_param_table_append_slider(table, angle_id);

    gwy_param_table_append_header(table, -1, _("Deformation"));
    gwy_param_table_append_slider(table, sigma_id);
    gwy_param_table_slider_add_alt(table, sigma_id);
    gwy_param_table_append_slider(table, tau_id);
    gwy_param_table_slider_set_mapping(table, tau_id, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, tau_id);

    if (xcenter_id < 0 && ycenter_id < 0)
        return;

    gwy_param_table_append_header(table, -1, _("Position"));
    if (xcenter_id >= 0) {
        gwy_param_table_append_slider(table, xcenter_id);
        gwy_param_table_slider_add_alt(table, xcenter_id);
    }
    if (ycenter_id >= 0) {
        gwy_param_table_append_slider(table, ycenter_id);
        gwy_param_table_slider_add_alt(table, ycenter_id);
    }
}

/************************************************************************************************************
 *
 * Staircase
 *
 ************************************************************************************************************/

static void
define_params_staircase(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_PERIOD, "staircase/period", _("_Terrace width"),
                             1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_POSITION_NOISE, "staircase/position_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_SLOPE, "staircase/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_SLOPE_NOISE, "staircase/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_HEIGHT, "staircase/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_HEIGHT_NOISE, "staircase/height_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_STAIRCASE_ANGLE, "staircase/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_SIGMA, "staircase/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAIRCASE_TAU, "staircase/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
    gwy_param_def_add_boolean(pardef, PARAM_STAIRCASE_KEEP_SLOPE, "staircase/keep_slope", _("Scales _with width"),
                              FALSE);
}

static void
append_gui_staircase(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_STAIRCASE];

    gwy_param_table_append_header(table, -1, _("Terrace"));
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_PERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_STAIRCASE_PERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_STAIRCASE_PERIOD);
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_POSITION_NOISE);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_STAIRCASE_SLOPE);
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_SLOPE_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_STAIRCASE_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_STAIRCASE_HEIGHT_NOISE);
    gwy_param_table_append_checkbox(table, PARAM_STAIRCASE_KEEP_SLOPE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_STAIRCASE],
                                PARAM_STAIRCASE_ANGLE, PARAM_STAIRCASE_SIGMA, PARAM_STAIRCASE_TAU, -1, -1);
}

static void
make_pattern_staircase(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble position_noise = gwy_params_get_double(params, PARAM_STAIRCASE_POSITION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_STAIRCASE_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_STAIRCASE_HEIGHT_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_STAIRCASE_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_STAIRCASE_SLOPE_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_STAIRCASE_ANGLE);
    gdouble period = gwy_params_get_double(params, PARAM_STAIRCASE_PERIOD);
    gdouble sigma = gwy_params_get_double(params, PARAM_STAIRCASE_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_STAIRCASE_TAU);
    gboolean keep_slope = gwy_params_get_boolean(params, PARAM_STAIRCASE_KEEP_SLOPE);
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    gint power10z;
    gdouble *abscissa, *height, *slope;
    GwyDataField *displx, *disply, *tmap;
    guint n;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    tmap = displx;  /* Alias for clarity. */
    displacement_to_t_linear(tmap, displx, disply, angle, period);
    n = find_t_range(tmap, FALSE);
    abscissa = make_positions_1d_linear(n, position_noise, rngset, RNG_OFFSET_X);
    height = make_heights_staircase(abscissa, n, height_mean, height_noise, keep_slope, FALSE, rngset, RNG_HEIGHT);
    slope = make_values_1d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    render_staircase(args->result, tmap, abscissa, height, slope, n);

    g_free(slope);
    g_free(height);
    g_free(abscissa);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Double staircase
 *
 ************************************************************************************************************/

static void
define_params_dblstair(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_XPERIOD, "dblstair/xperiod", _("Terrace _X width"),
                             1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_XPOSITION_NOISE, "dblstair/xposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_YPERIOD, "dblstair/yperiod", _("Terrace _Y width"),
                             1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_YPOSITION_NOISE, "dblstair/yposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_HEIGHT, "dblstair/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_HEIGHT_NOISE, "dblstair/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_DBLSTAIR_ANGLE, "dblstair/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_SIGMA, "dblstair/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_DBLSTAIR_TAU, "dblstair/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
}

static void
append_gui_dblstair(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_DBLSTAIR];

    gwy_param_table_append_header(table, -1, _("Terrace"));
    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_XPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_DBLSTAIR_XPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_DBLSTAIR_XPERIOD);
    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_XPOSITION_NOISE);

    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_YPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_DBLSTAIR_YPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_DBLSTAIR_YPERIOD);
    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_YPOSITION_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_DBLSTAIR_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_DBLSTAIR_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_DBLSTAIR],
                                PARAM_DBLSTAIR_ANGLE, PARAM_DBLSTAIR_SIGMA, PARAM_DBLSTAIR_TAU, -1, -1);
}

static void
make_pattern_dblstair(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble xperiod = gwy_params_get_double(params, PARAM_DBLSTAIR_XPERIOD);
    gdouble yperiod = gwy_params_get_double(params, PARAM_DBLSTAIR_YPERIOD);
    gdouble xposition_noise = gwy_params_get_double(params, PARAM_DBLSTAIR_XPOSITION_NOISE);
    gdouble yposition_noise = gwy_params_get_double(params, PARAM_DBLSTAIR_YPOSITION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_DBLSTAIR_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_DBLSTAIR_HEIGHT_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_DBLSTAIR_ANGLE);
    gdouble sigma = gwy_params_get_double(params, PARAM_DBLSTAIR_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_DBLSTAIR_TAU);
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    gdouble *abscissau, *abscissav, *height;
    GwyDataField *displx, *disply, *umap, *vmap;
    gint power10z;
    guint nu, nv;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    umap = displx;  /* Alias for clarity. */
    vmap = disply;  /* Alias for clarity. */
    displacement_to_uv_linear(umap, vmap, displx, disply, angle, xperiod, yperiod);
    nu = find_t_range(umap, FALSE);
    nv = find_t_range(vmap, FALSE);
    abscissau = make_positions_1d_linear(nu, xposition_noise, rngset, RNG_OFFSET_X);
    abscissav = make_positions_1d_linear(nv, yposition_noise, rngset, RNG_OFFSET_Y);
    height = make_heights_staircase(NULL, (nu + nv) | 1, height_mean, height_noise, FALSE, FALSE, rngset, RNG_HEIGHT);
    render_double_staircase(args->result, umap, vmap, abscissau, abscissav, height, nu, nv);

    g_free(height);
    g_free(abscissau);
    g_free(abscissav);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Grating
 *
 ************************************************************************************************************/

static void
define_params_grating(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_GRATING_PERIOD, "grating/period", _("_Period"), 1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_POSITION_NOISE, "grating/position_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_TOP_FRAC, "grating/top_frac", _("_Top fraction"), 0.0, 1.0, 0.45);
    gwy_param_def_add_double(pardef, PARAM_GRATING_TOP_FRAC_NOISE, "grating/top_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_ASYMM, "grating/asymm", _("_Asymmetry"), -1.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_SLOPE, "grating/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_GRATING_SLOPE_NOISE, "grating/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_HEIGHT, "grating/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_HEIGHT_NOISE, "grating/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_GRATING_ANGLE, "grating/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_SIGMA, "grating/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_GRATING_TAU, "grating/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
    gwy_param_def_add_boolean(pardef, PARAM_GRATING_SCALE_WITH_WIDTH, "grating/scale_with_width",
                              _("Scale features with _width"), FALSE);
}

static void
append_gui_grating(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_GRATING];

    gwy_param_table_append_header(table, -1, _("Period"));
    gwy_param_table_append_slider(table, PARAM_GRATING_PERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_GRATING_PERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_GRATING_PERIOD);
    gwy_param_table_append_slider(table, PARAM_GRATING_POSITION_NOISE);
    gwy_param_table_append_checkbox(table, PARAM_GRATING_SCALE_WITH_WIDTH);

    gwy_param_table_append_header(table, -1, _("Duty Cycle"));
    gwy_param_table_append_slider(table, PARAM_GRATING_TOP_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_GRATING_TOP_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_GRATING_TOP_FRAC);
    gwy_param_table_append_slider(table, PARAM_GRATING_TOP_FRAC_NOISE);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_GRATING_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_GRATING_SLOPE);
    gwy_param_table_append_slider(table, PARAM_GRATING_SLOPE_NOISE);

    gwy_param_table_append_slider(table, PARAM_GRATING_ASYMM);
    gwy_param_table_slider_set_mapping(table, PARAM_GRATING_ASYMM, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_GRATING_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_GRATING_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_GRATING_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_GRATING],
                                PARAM_GRATING_ANGLE, PARAM_GRATING_SIGMA, PARAM_GRATING_TAU, -1, -1);
}

static void
make_pattern_grating(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble position_noise = gwy_params_get_double(params, PARAM_GRATING_POSITION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_GRATING_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_GRATING_HEIGHT_NOISE);
    gdouble top_frac_mean = gwy_params_get_double(params, PARAM_GRATING_TOP_FRAC);
    gdouble top_frac_noise = gwy_params_get_double(params, PARAM_GRATING_TOP_FRAC_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_GRATING_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_GRATING_SLOPE_NOISE);
    gdouble asymm = gwy_params_get_double(params, PARAM_GRATING_ASYMM);
    gdouble angle = gwy_params_get_double(params, PARAM_GRATING_ANGLE);
    gdouble period = gwy_params_get_double(params, PARAM_GRATING_PERIOD);
    gdouble sigma = gwy_params_get_double(params, PARAM_GRATING_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_GRATING_TAU);
    gboolean scale_with_width = gwy_params_get_boolean(params, PARAM_GRATING_SCALE_WITH_WIDTH);
    gint power10z;
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    gdouble *abscissa, *height, *width, *slopeleft, *sloperight;
    GwyDataField *displx, *disply, *tmap;
    guint n;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    tmap = displx;  /* Alias for clarity. */
    displacement_to_t_linear(tmap, displx, disply, angle, period);
    n = find_t_range(tmap, FALSE);
    abscissa = make_positions_1d_linear(n, position_noise, rngset, RNG_OFFSET_X);
    width = make_values_1d(n, top_frac_mean, top_frac_noise, rngset, RNG_TOP_X);
    height = make_values_1d(n, height_mean, height_noise, rngset, RNG_HEIGHT);
    slopeleft = make_values_1d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    sloperight = distribute_left_to_left_and_right(slopeleft, n, asymm);
    if (scale_with_width)
        transform_to_scaled_grating(abscissa, width, slopeleft, sloperight, n, FALSE);
    render_grating(args->result, tmap, abscissa, width, height, slopeleft, sloperight, 1, n);

    g_free(slopeleft);
    g_free(sloperight);
    g_free(width);
    g_free(height);
    g_free(abscissa);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Amphitheatre
 *
 ************************************************************************************************************/

static void
define_params_amphith(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_FLAT, "amphith/flat", _("_Terrace width"), 1.0, 1000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_POSITION_NOISE, "amphith/position_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_SLOPE, "amphith/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_SLOPE_NOISE, "amphith/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_HEIGHT, "amphith/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_HEIGHT_NOISE, "amphith/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_INVPOWER, "amphith/invpower", _("Super_ellipse parameter"),
                             0.0, 2.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_PARABOLICITY, "amphith/parabolicity", _("_Parabolicity"),
                             -1.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_XCENTER, "amphith/xcenter", _("_X center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_YCENTER, "amphith/ycenter", _("_Y center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_AMPHITH_ANGLE, "amphith/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_SIGMA, "amphith/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_AMPHITH_TAU, "amphith/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
}

static void
append_gui_amphith(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_AMPHITH];

    gwy_param_table_append_slider(table, PARAM_AMPHITH_INVPOWER);
    gwy_param_table_slider_set_mapping(table, PARAM_AMPHITH_INVPOWER, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Terrace"));
    gwy_param_table_append_slider(table, PARAM_AMPHITH_FLAT);
    gwy_param_table_slider_set_mapping(table, PARAM_AMPHITH_FLAT, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_AMPHITH_FLAT);
    gwy_param_table_append_slider(table, PARAM_AMPHITH_POSITION_NOISE);
    gwy_param_table_append_slider(table, PARAM_AMPHITH_PARABOLICITY);
    gwy_param_table_slider_set_mapping(table, PARAM_AMPHITH_PARABOLICITY, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_AMPHITH_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_AMPHITH_SLOPE);
    gwy_param_table_append_slider(table, PARAM_AMPHITH_SLOPE_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_AMPHITH_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_AMPHITH_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_AMPHITH_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_AMPHITH],
                                PARAM_AMPHITH_ANGLE, PARAM_AMPHITH_SIGMA, PARAM_AMPHITH_TAU,
                                PARAM_AMPHITH_XCENTER, PARAM_AMPHITH_YCENTER);
}

static void
make_pattern_amphith(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble position_noise = gwy_params_get_double(params, PARAM_AMPHITH_POSITION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_AMPHITH_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_AMPHITH_HEIGHT_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_AMPHITH_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_AMPHITH_SLOPE_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_AMPHITH_ANGLE);
    gdouble flat = gwy_params_get_double(params, PARAM_AMPHITH_FLAT);
    gdouble invpower = gwy_params_get_double(params, PARAM_AMPHITH_INVPOWER);
    gdouble parabolicity = gwy_params_get_double(params, PARAM_AMPHITH_PARABOLICITY);
    gdouble xcenter = gwy_params_get_double(params, PARAM_AMPHITH_XCENTER);
    gdouble ycenter = gwy_params_get_double(params, PARAM_AMPHITH_YCENTER);
    gdouble sigma = gwy_params_get_double(params, PARAM_AMPHITH_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_AMPHITH_TAU);
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    GwyDataField *displx, *disply, *tmap;
    gdouble *radius, *height, *slope;
    gdouble scale;
    gint power10z;
    guint n;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    tmap = displx;  /* Alias for clarity. */
    displacement_to_t_superellipse(tmap, displx, disply, angle, xcenter, ycenter, invpower, flat);
    n = find_t_range(tmap, TRUE);
    scale = 0.5*hypot(xres, yres)/flat;
    radius = make_positions_1d_radial(n, position_noise, scale, parabolicity, rngset, RNG_OFFSET_X);
    height = make_heights_staircase(radius, n, height_mean, height_noise, FALSE, TRUE, rngset, RNG_HEIGHT);
    slope = make_values_1d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    render_staircase(args->result, tmap, radius, height, slope, n);

    g_free(slope);
    g_free(height);
    g_free(radius);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Concentric rings
 *
 ************************************************************************************************************/

static void
define_params_rings(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_RINGS_PERIOD, "rings/period", _("_Period"), 1.0, 1000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_POSITION_NOISE, "rings/position_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_TOP_FRAC, "rings/top_frac", _("_Top fraction"), 0.0, 1.0, 0.45);
    gwy_param_def_add_double(pardef, PARAM_RINGS_TOP_FRAC_NOISE, "rings/top_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_SLOPE, "rings/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_RINGS_SLOPE_NOISE, "rings/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_ASYMM, "rings/asymm", _("_Asymmetry"), -1.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_HEIGHT, "rings/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_HEIGHT_NOISE, "rings/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_INVPOWER, "rings/invpower", _("Super_ellipse parameter"),
                             0.0, 2.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_PARABOLICITY, "rings/parabolicity", _("_Parabolicity"),
                             -1.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_XCENTER, "rings/xcenter", _("_X center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_YCENTER, "rings/ycenter", _("_Y center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_RINGS_ANGLE, "rings/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_SIGMA, "rings/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RINGS_TAU, "rings/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
    gwy_param_def_add_boolean(pardef, PARAM_RINGS_SCALE_WITH_WIDTH, "rings/scale_with_width",
                              _("Scale features with _width"), FALSE);
}

static void
append_gui_rings(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_RINGS];

    gwy_param_table_append_slider(table, PARAM_RINGS_INVPOWER);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_INVPOWER, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Period"));
    gwy_param_table_append_slider(table, PARAM_RINGS_PERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_PERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_RINGS_PERIOD);
    gwy_param_table_append_slider(table, PARAM_RINGS_POSITION_NOISE);
    gwy_param_table_append_slider(table, PARAM_RINGS_PARABOLICITY);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_PARABOLICITY, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_checkbox(table, PARAM_RINGS_SCALE_WITH_WIDTH);

    gwy_param_table_append_header(table, -1, _("Duty Cycle"));
    gwy_param_table_append_slider(table, PARAM_RINGS_TOP_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_TOP_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_RINGS_TOP_FRAC);
    gwy_param_table_append_slider(table, PARAM_RINGS_TOP_FRAC_NOISE);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_RINGS_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_RINGS_SLOPE);
    gwy_param_table_append_slider(table, PARAM_RINGS_SLOPE_NOISE);

    gwy_param_table_append_slider(table, PARAM_RINGS_ASYMM);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_ASYMM, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_RINGS_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_RINGS_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_RINGS_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_RINGS],
                                PARAM_RINGS_ANGLE, PARAM_RINGS_SIGMA, PARAM_RINGS_TAU,
                                PARAM_RINGS_XCENTER, PARAM_RINGS_YCENTER);
}

static void
make_pattern_rings(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble position_noise = gwy_params_get_double(params, PARAM_RINGS_POSITION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_RINGS_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_RINGS_HEIGHT_NOISE);
    gdouble top_frac_mean = gwy_params_get_double(params, PARAM_RINGS_TOP_FRAC);
    gdouble top_frac_noise = gwy_params_get_double(params, PARAM_RINGS_TOP_FRAC_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_RINGS_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_RINGS_SLOPE_NOISE);
    gdouble asymm = gwy_params_get_double(params, PARAM_RINGS_ASYMM);
    gdouble angle = gwy_params_get_double(params, PARAM_RINGS_ANGLE);
    gdouble period = gwy_params_get_double(params, PARAM_RINGS_PERIOD);
    gdouble invpower = gwy_params_get_double(params, PARAM_RINGS_INVPOWER);
    gdouble parabolicity = gwy_params_get_double(params, PARAM_RINGS_PARABOLICITY);
    gdouble xcenter = gwy_params_get_double(params, PARAM_RINGS_XCENTER);
    gdouble ycenter = gwy_params_get_double(params, PARAM_RINGS_YCENTER);
    gdouble sigma = gwy_params_get_double(params, PARAM_RINGS_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_RINGS_TAU);
    gboolean scale_with_width = gwy_params_get_boolean(params, PARAM_GRATING_SCALE_WITH_WIDTH);
    gint power10z;
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    GwyDataField *displx, *disply, *tmap;
    gdouble *radius, *width, *height, *slopeleft, *sloperight;
    gdouble scale;
    guint n;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    tmap = displx;  /* Alias for clarity. */
    displacement_to_t_superellipse(tmap, displx, disply, angle, xcenter, ycenter, invpower, period);
    n = find_t_range(tmap, TRUE);
    scale = 0.5*hypot(xres, yres)/period;
    radius = make_positions_1d_radial(n, position_noise, scale, parabolicity, rngset, RNG_OFFSET_X);
    width = make_values_1d(n, top_frac_mean, top_frac_noise, rngset, RNG_TOP_X);
    height = make_values_1d(n, height_mean, height_noise, rngset, RNG_HEIGHT);
    slopeleft = make_values_1d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    sloperight = distribute_left_to_left_and_right(slopeleft, n, asymm);
    if (scale_with_width)
        transform_to_scaled_grating(radius, width, slopeleft, sloperight, n, TRUE);
    render_grating(args->result, tmap, radius, width, height, slopeleft, sloperight, 1, n);

    g_free(sloperight);
    g_free(slopeleft);
    g_free(height);
    g_free(width);
    g_free(radius);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Siemens star
 *
 ************************************************************************************************************/

static void
define_params_star(GwyParamDef *pardef)
{
    gwy_param_def_add_int(pardef, PARAM_STAR_N_RAYS, "star/n_rays", _("_Number of sectors"), 2, 36, 8);
    gwy_param_def_add_double(pardef, PARAM_STAR_TOP_FRAC, "star/top_frac", _("_Top fraction"), 0.01, 0.99, 0.5);
    gwy_param_def_add_double(pardef, PARAM_STAR_TOP_FRAC_NOISE, "star/top_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_EDGE_SHIFT, "star/edge_shift", _("_Edge shift"), -100.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_SLOPE, "star/slope", _("_Slope width"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_STAR_HEIGHT, "star/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_XCENTER, "star/xcenter", _("_X center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_YCENTER, "star/ycenter", _("_Y center"), -2.0, 2.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_STAR_ANGLE, "star/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_SIGMA, "star/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_STAR_TAU, "star/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
}

static void
append_gui_star(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_STAR];

    gwy_param_table_append_slider(table, PARAM_STAR_N_RAYS);
    gwy_param_table_slider_set_mapping(table, PARAM_STAR_N_RAYS, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Duty Cycle"));
    gwy_param_table_append_slider(table, PARAM_STAR_TOP_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_STAR_TOP_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_STAR_TOP_FRAC_NOISE);

    gwy_param_table_append_header(table, -1, _("Edge"));
    gwy_param_table_append_slider(table, PARAM_STAR_EDGE_SHIFT);
    gwy_param_table_slider_add_alt(table, PARAM_STAR_EDGE_SHIFT);
    gwy_param_table_append_slider(table, PARAM_STAR_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_STAR_SLOPE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_STAR_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_STAR_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_STAR],
                                PARAM_STAR_ANGLE, PARAM_STAR_SIGMA, PARAM_STAR_TAU,
                                PARAM_STAR_XCENTER, PARAM_STAR_YCENTER);
}

static inline gdouble
wedge_outer_distance(gdouble x, gdouble y, const gdouble *u0v0, const gdouble *p)
{
    gdouble du, dv, su, sv;

    x -= p[0];
    y -= p[1];
    du = x*u0v0[1] - y*u0v0[0];
    dv = -x*u0v0[3] + y*u0v0[2];
    if (du <= 0.0 && dv <= 0.0)
        return 0.0;

    su = x*u0v0[0] + y*u0v0[1];
    if (du >= 0.0 && dv <= 0.0 && su >= 0.0)
        return du;

    sv = x*u0v0[2] + y*u0v0[3];
    if (dv >= 0.0 && du <= 0.0 && sv >= 0.0)
        return dv;

    return sqrt(x*x + y*y);
}

static void
make_pattern_star(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    guint n_rays = gwy_params_get_int(params, PARAM_STAR_N_RAYS);
    gdouble height = gwy_params_get_double(params, PARAM_STAR_HEIGHT);
    gdouble top_frac = gwy_params_get_double(params, PARAM_STAR_TOP_FRAC);
    gdouble top_frac_noise = gwy_params_get_double(params, PARAM_STAR_TOP_FRAC_NOISE);
    gdouble slope = gwy_params_get_double(params, PARAM_STAR_SLOPE);
    gdouble edge_shift = gwy_params_get_double(params, PARAM_STAR_EDGE_SHIFT);
    gdouble angle = gwy_params_get_double(params, PARAM_STAR_ANGLE);
    gdouble xcenter = gwy_params_get_double(params, PARAM_STAR_XCENTER);
    gdouble ycenter = gwy_params_get_double(params, PARAM_STAR_YCENTER);
    gdouble sigma = gwy_params_get_double(params, PARAM_STAR_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_STAR_TAU);
    gint power10z;
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    GwyDataField *displx, *disply;
    guint nedge, i;
    gdouble *v0edge, *pedge, *data, *dx_data, *dy_data;
    gdouble c, s, xoff, yoff;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    xoff = (0.5 + xcenter)*xres;
    yoff = (0.5 + ycenter)*yres;
    c = cos(angle);
    s = sin(angle);

    /* Extend the spoke edge list two back and four forward to ensure we can always safely move forward and backward
     * without doing mod nedge. */
    nedge = 2*n_rays + 6;
    v0edge = g_new(gdouble, 2*nedge);
    pedge = g_new(gdouble, nedge);
    for (i = 0; i < n_rays; i++) {
        gdouble phi = 2.0*G_PI*i/n_rays;
        gdouble width = G_PI/n_rays*top_frac;
        if (top_frac_noise) {
            gdouble t = gwy_rand_gen_set_double(rngset, RNG_TOP_X);
            if (t > 0.5)
                width += (2.0*t - 1.0)*(1.0 - top_frac)*G_PI/n_rays*top_frac_noise;
            else if (t < 0.5)
                width -= (1.0 - 2.0*t)*top_frac*G_PI/n_rays*top_frac_noise;
        }
        pedge[2*i+2] = phi - width;
        pedge[2*i+3] = phi + width;
    }
    pedge[0] = pedge[nedge-6] - 2.0*G_PI;
    pedge[1] = pedge[nedge-5] - 2.0*G_PI;
    pedge[nedge-4] = pedge[2] + 2.0*G_PI;
    pedge[nedge-3] = pedge[3] + 2.0*G_PI;
    pedge[nedge-2] = pedge[4] + 2.0*G_PI;
    pedge[nedge-1] = pedge[5] + 2.0*G_PI;
    for (i = 0; i < nedge; i++) {
        v0edge[2*i] = cos(pedge[i]);
        v0edge[2*i+1] = sin(pedge[i]);
    }

    pedge = g_new(gdouble, nedge);
    for (i = 0; i < nedge/2; i++) {
        pedge[2*i] = -(v0edge[4*i + 0] + v0edge[4*i + 2])*edge_shift;
        pedge[2*i + 1] = -(v0edge[4*i + 1] + v0edge[4*i + 3])*edge_shift;
    }

    data = gwy_data_field_get_data(args->result);
    dx_data = gwy_data_field_get_data(displx);
    dy_data = gwy_data_field_get_data(disply);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,dx_data,dy_data,pedge,v0edge,xoff,yoff,c,s,height,slope,xres,yres,nedge)
#endif
    for (i = 0; i < yres; i++) {
        guint j, k;

        for (j = 0; j < xres; j++) {
            gdouble x, y, xu, yu, d1, d;

            xu = (j - xoff)*c - (i - yoff)*s;
            yu = (j - xoff)*s + (i - yoff)*c;
            x = xu + dx_data[i*xres + j];
            y = yu + dy_data[i*xres + j];
            d = wedge_outer_distance(x, y, v0edge, pedge);
            for (k = 1; k < nedge/2; k++) {
                d1 = wedge_outer_distance(x, y, v0edge + 4*k, pedge + 2*k);
                d = fmin(d, d1);
            }
            d = slope ? fmax(1.0 - d/slope, 0.0) : !d;
            data[i*xres + j] = height*d;
        }
    }

    g_free(pedge);
    g_free(v0edge);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Holes (rectangular)
 *
 ************************************************************************************************************/

static void
define_params_rholes(GwyParamDef *pardef)
{
    gwy_param_def_add_double(pardef, PARAM_RHOLES_XPERIOD, "rholes/xperiod", _("_X Period"), 1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_XPOSITION_NOISE, "rholes/xposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_YPERIOD, "rholes/yperiod", _("_Y Period"), 1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_YPOSITION_NOISE, "rholes/yposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_XTOP_FRAC, "rholes/xtop_frac", _("X top fraction"), 0.0, 1.0, 0.3);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_XTOP_FRAC_NOISE, "rholes/xtop_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_YTOP_FRAC, "rholes/ytop_frac", _("Y top fraction"), 0.0, 1.0, 0.3);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_YTOP_FRAC_NOISE, "rholes/ytop_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_SLOPE, "rholes/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_SLOPE_NOISE, "rholes/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_ROUNDNESS, "rholes/roundness", _("Roundn_ess"), 0.0, 1.0, 0.1);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_ROUNDNESS_NOISE, "rholes/roundness_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_HEIGHT, "rholes/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_HEIGHT_NOISE, "rholes/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_RHOLES_ANGLE, "rholes/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_SIGMA, "rholes/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_RHOLES_TAU, "rholes/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
}

static void
append_gui_rholes(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_RHOLES];

    gwy_param_table_append_header(table, -1, _("Period"));
    gwy_param_table_append_slider(table, PARAM_RHOLES_XPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_XPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_RHOLES_XPERIOD);
    gwy_param_table_append_slider(table, PARAM_RHOLES_XPOSITION_NOISE);
    gwy_param_table_append_slider(table, PARAM_RHOLES_YPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_YPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_RHOLES_YPERIOD);
    gwy_param_table_append_slider(table, PARAM_RHOLES_YPOSITION_NOISE);

    gwy_param_table_append_header(table, -1, _("Duty Cycle"));
    gwy_param_table_append_slider(table, PARAM_RHOLES_XTOP_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_XTOP_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_RHOLES_XTOP_FRAC);
    gwy_param_table_append_slider(table, PARAM_RHOLES_XTOP_FRAC_NOISE);
    gwy_param_table_append_slider(table, PARAM_RHOLES_YTOP_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_YTOP_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_RHOLES_YTOP_FRAC);
    gwy_param_table_append_slider(table, PARAM_RHOLES_YTOP_FRAC_NOISE);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_RHOLES_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_RHOLES_SLOPE);
    gwy_param_table_append_slider(table, PARAM_RHOLES_SLOPE_NOISE);

    gwy_param_table_append_header(table, -1, _("Roundness"));
    gwy_param_table_append_slider(table, PARAM_RHOLES_ROUNDNESS);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_ROUNDNESS, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, PARAM_RHOLES_ROUNDNESS_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_RHOLES_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_RHOLES_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_RHOLES_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_RHOLES],
                                PARAM_RHOLES_ANGLE, PARAM_RHOLES_SIGMA, PARAM_RHOLES_TAU, -1, -1);
}

static void
make_pattern_rholes(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    gdouble xperiod = gwy_params_get_double(params, PARAM_RHOLES_XPERIOD);
    gdouble yperiod = gwy_params_get_double(params, PARAM_RHOLES_YPERIOD);
    gdouble xposition_noise = gwy_params_get_double(params, PARAM_RHOLES_XPOSITION_NOISE);
    gdouble yposition_noise = gwy_params_get_double(params, PARAM_RHOLES_YPOSITION_NOISE);
    gdouble xtop_frac = gwy_params_get_double(params, PARAM_RHOLES_XTOP_FRAC);
    gdouble xtop_frac_noise = gwy_params_get_double(params, PARAM_RHOLES_XTOP_FRAC_NOISE);
    gdouble ytop_frac = gwy_params_get_double(params, PARAM_RHOLES_YTOP_FRAC);
    gdouble ytop_frac_noise = gwy_params_get_double(params, PARAM_RHOLES_YTOP_FRAC_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_RHOLES_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_RHOLES_SLOPE_NOISE);
    gdouble roundness_mean = gwy_params_get_double(params, PARAM_RHOLES_ROUNDNESS);
    gdouble roundness_noise = gwy_params_get_double(params, PARAM_RHOLES_ROUNDNESS_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_RHOLES_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_RHOLES_HEIGHT_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_RHOLES_ANGLE);
    gdouble sigma = gwy_params_get_double(params, PARAM_RHOLES_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_RHOLES_TAU);
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    gint power10z;
    gdouble *abscissau, *abscissav, *height, *xsize, *ysize, *slope, *roundness;
    GwyDataField *displx, *disply, *umap, *vmap;
    guint n, nu, nv;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    umap = displx;  /* Alias for clarity. */
    vmap = disply;  /* Alias for clarity. */
    displacement_to_uv_linear(umap, vmap, displx, disply, angle, xperiod, yperiod);
    nu = find_t_range(umap, FALSE);
    nv = find_t_range(vmap, FALSE);
    n = MAX(nu, nv);
    abscissau = make_positions_2d_linear(n, xposition_noise, FALSE, rngset, RNG_OFFSET_X);
    abscissav = make_positions_2d_linear(n, yposition_noise, TRUE, rngset, RNG_OFFSET_Y);
    xsize = make_values_2d(n, 1.0 - xtop_frac, xtop_frac_noise, rngset, RNG_TOP_X);
    ysize = make_values_2d(n, 1.0 - ytop_frac, ytop_frac_noise, rngset, RNG_TOP_Y);
    slope = make_values_2d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    roundness = make_values_2d(n, roundness_mean, roundness_noise, rngset, RNG_ROUNDNESS);
    height = make_values_2d(n, height_mean, height_noise, rngset, RNG_HEIGHT);
    render_holes(args->result, umap, vmap, abscissau, abscissav, xsize, ysize, slope, roundness, height, n,
                 yperiod/xperiod);

    g_free(height);
    g_free(roundness);
    g_free(slope);
    g_free(ysize);
    g_free(xsize);
    g_free(abscissau);
    g_free(abscissav);
    g_object_unref(displx);
    g_object_unref(disply);
}

/************************************************************************************************************
 *
 * Pillars
 *
 ************************************************************************************************************/

static void
define_params_pillars(GwyParamDef *pardef)
{
    static const GwyEnum shapes[] = {
        { N_("Circle"),  PILLAR_SHAPE_CIRCLE,  },
        { N_("Square"),  PILLAR_SHAPE_SQUARE,  },
        { N_("Hexagon"), PILLAR_SHAPE_HEXAGON, },
    };

    gwy_param_def_add_gwyenum(pardef, PARAM_PILLARS_SHAPE, "pillars/shape", _("S_hape"),
                              shapes, G_N_ELEMENTS(shapes), PILLAR_SHAPE_CIRCLE);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_XPERIOD, "pillars/xperiod", _("_X Period"), 1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_XPOSITION_NOISE, "pillars/xposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_YPERIOD, "pillars/yperiod", _("_Y Period"), 1.0, 2000.0, 50.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_YPOSITION_NOISE, "pillars/yposition_noise", _("Position spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_SIZE_FRAC, "pillars/size_frac", _("Si_ze fraction"), 0.0, 1.0, 0.5);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_SIZE_FRAC_NOISE, "pillars/size_frac_noise", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_SLOPE, "pillars/slope", _("_Slope fraction"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_SLOPE_NOISE, "pillars/slope_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_PILLARS_ORIENTATION, "pillars/orientation", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_ORIENTATION_NOISE, "pillars/orientation", _("Spread"),
                             0.0, 1.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_HEIGHT, "pillars/height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_HEIGHT_NOISE, "pillars/height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_angle(pardef, PARAM_PILLARS_ANGLE, "pillars/angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_SIGMA, "pillars/sigma", _("_Amplitude"), 0.0, 100.0, 0.0);
    gwy_param_def_add_double(pardef, PARAM_PILLARS_TAU, "pillars/tau", _("_Lateral scale"), 0.1, 1000.0, 10.0);
}

static void
append_gui_pillars(ModuleGUI *gui)
{
    GwyParamTable *table = gui->table_generator[PAT_SYNTH_PILLARS];

    gwy_param_table_append_combo(table, PARAM_PILLARS_SHAPE);

    gwy_param_table_append_header(table, -1, _("Period"));
    gwy_param_table_append_slider(table, PARAM_PILLARS_XPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_PILLARS_XPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_PILLARS_XPERIOD);
    gwy_param_table_append_slider(table, PARAM_PILLARS_XPOSITION_NOISE);
    gwy_param_table_append_slider(table, PARAM_PILLARS_YPERIOD);
    gwy_param_table_slider_set_mapping(table, PARAM_PILLARS_YPERIOD, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_PILLARS_YPERIOD);
    gwy_param_table_append_slider(table, PARAM_PILLARS_YPOSITION_NOISE);

    gwy_param_table_append_header(table, -1, _("Size"));
    gwy_param_table_append_slider(table, PARAM_PILLARS_SIZE_FRAC);
    gwy_param_table_slider_set_mapping(table, PARAM_PILLARS_SIZE_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_PILLARS_SIZE_FRAC);
    gwy_param_table_append_slider(table, PARAM_PILLARS_SIZE_FRAC_NOISE);

    gwy_param_table_append_header(table, -1, _("Slope"));
    gwy_param_table_append_slider(table, PARAM_PILLARS_SLOPE);
    gwy_param_table_slider_add_alt(table, PARAM_PILLARS_SLOPE);
    gwy_param_table_append_slider(table, PARAM_PILLARS_SLOPE_NOISE);

    gwy_param_table_append_header(table, -1, _("Orientation"));
    gwy_param_table_append_slider(table, PARAM_PILLARS_ORIENTATION);
    gwy_param_table_append_slider(table, PARAM_PILLARS_ORIENTATION_NOISE);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_PILLARS_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_PILLARS_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_PILLARS_HEIGHT_NOISE);

    append_gui_placement_common(gui->table_placement[PAT_SYNTH_PILLARS],
                                PARAM_PILLARS_ANGLE, PARAM_PILLARS_SIGMA, PARAM_PILLARS_TAU, -1, -1);
}

static void
make_pattern_pillars(ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    PillarShapeType shape = gwy_params_get_enum(params, PARAM_PILLARS_SHAPE);
    gdouble xperiod = gwy_params_get_double(params, PARAM_PILLARS_XPERIOD);
    gdouble yperiod = gwy_params_get_double(params, PARAM_PILLARS_YPERIOD);
    gdouble xposition_noise = gwy_params_get_double(params, PARAM_PILLARS_XPOSITION_NOISE);
    gdouble yposition_noise = gwy_params_get_double(params, PARAM_PILLARS_YPOSITION_NOISE);
    gdouble size_frac = gwy_params_get_double(params, PARAM_PILLARS_SIZE_FRAC);
    gdouble size_frac_noise = gwy_params_get_double(params, PARAM_PILLARS_SIZE_FRAC_NOISE);
    gdouble slope_mean = gwy_params_get_double(params, PARAM_PILLARS_SLOPE);
    gdouble slope_noise = gwy_params_get_double(params, PARAM_PILLARS_SLOPE_NOISE);
    gdouble orientation = gwy_params_get_double(params, PARAM_PILLARS_ORIENTATION);
    gdouble orientation_noise = gwy_params_get_double(params, PARAM_PILLARS_ORIENTATION_NOISE);
    gdouble height_mean = gwy_params_get_double(params, PARAM_PILLARS_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_PILLARS_HEIGHT_NOISE);
    gdouble angle = gwy_params_get_double(params, PARAM_PILLARS_ANGLE);
    gdouble sigma = gwy_params_get_double(params, PARAM_PILLARS_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_PILLARS_TAU);
    gint power10z;
    guint xres = gwy_data_field_get_xres(args->result);
    guint yres = gwy_data_field_get_yres(args->result);
    gdouble *abscissau, *abscissav, *height, *size, *slope, *sine, *cosine;
    GwyDataField *displx, *disply, *umap, *vmap;
    gdouble orinoise = 0.0;
    guint n, nu, nv;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height_mean *= pow10(power10z);
    displx = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_X);
    disply = make_displacement_map(xres, yres, sigma, tau, rngset, RNG_DISPLAC_Y);
    umap = displx;  /* Alias for clarity. */
    vmap = disply;  /* Alias for clarity. */
    displacement_to_uv_linear(umap, vmap, displx, disply, angle, xperiod, yperiod);
    nu = find_t_range(umap, FALSE);
    nv = find_t_range(vmap, FALSE);
    n = MAX(nu, nv);
    abscissau = make_positions_2d_linear(n, xposition_noise, FALSE, rngset, RNG_OFFSET_X);
    abscissav = make_positions_2d_linear(n, yposition_noise, TRUE, rngset, RNG_OFFSET_Y);
    size = make_values_2d(n, size_frac, size_frac_noise, rngset, RNG_SIZE_X);
    slope = make_values_2d(n, slope_mean, slope_noise, rngset, RNG_SLOPE);
    if (shape == PILLAR_SHAPE_SQUARE)
        orinoise = G_PI/4.0 * orientation_noise;
    else if (shape == PILLAR_SHAPE_HEXAGON)
        orinoise = G_PI/6.0 * orientation_noise;
    sine = make_values_2d_gaussian(n, orientation, orinoise, rngset, RNG_ORIENTATION);
    cosine = g_new(gdouble, n*n);
    transform_to_sine_cosine(sine, cosine, n*n);
    height = make_values_2d(n, height_mean, height_noise, rngset, RNG_HEIGHT);
    render_pillars(args->result, umap, vmap, shape, abscissau, abscissav, size, slope, sine, cosine, height, n,
                   yperiod/xperiod);

    g_free(height);
    g_free(sine);
    g_free(cosine);
    g_free(slope);
    g_free(size);
    g_free(abscissau);
    g_free(abscissav);
    g_object_unref(displx);
    g_object_unref(disply);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
