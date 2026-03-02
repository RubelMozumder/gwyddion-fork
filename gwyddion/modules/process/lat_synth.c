/*
 *  $Id: lat_synth.c 24975 2022-08-31 12:05:41Z yeti-dn $
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/synth.h>
#include <libgwydgets/gwynullstore.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define EPS 0.0000001
#define PHI 1.6180339887498948482
#define SQRT5 2.23606797749978969640

/* How many points a lattice point placing functions can create. */
#define MAXLATPOINTS 12

/* How larger the squarized grid should be (measured in squares). */
#define SQBORDER 2

/* A convience macro to make the source readable.
   Usage: VOBJ(p->next)->angle = M_PI2 */
#define VOBJ(x) ((VoronoiObject*)(x)->data)

#define DOTPROD_SS(a, b) ((a).x*(b).x + (a).y*(b).y)
#define DOTPROD_SP(a, b) ((a).x*(b)->x + (a).y*(b)->y)
#define DOTPROD_PS(a, b) ((a)->x*(b).x + (a)->y*(b).y)

#define CROSSPROD_SS(a, b) ((a).x*(b).y - (a).y*(b).x)
#define CROSSPROD_SP(a, b) ((a).x*(b)->y - (a).y*(b)->x)
#define CROSSPROD_PS(a, b) ((a)->x*(b).y - (a)->y*(b).x)
#define CROSSPROD_PP(a, b) ((a)->x*(b)->y - (a)->y*(b)->x)

#define DECLARE_SURFACE(x) \
    static gdouble surface_##x(const GwyXY *point, const VoronoiObject *owner, gdouble scale)

/* The random grid uses the generators differently so there are aliases. */
typedef enum {
    RNG_POINTS     = 0,
    RNG_MISSING    = 0,
    RNG_EXTRA      = 1,
    RNG_DISPLAC_X  = 2,
    RNG_DISPLAC_Y  = 3,
    RNG_NRNGS
} LatSynthRng;

typedef enum {
    LATTICE_RANDOM       = 0,
    LATTICE_SQUARE       = 1,
    LATTICE_HEXAGONAL    = 2,
    LATTICE_TRIANGULAR   = 3,
    LATTICE_SQTRIG_VERT  = 4,
    LATTICE_SQTRIG_CENT  = 5,
    LATTICE_TRUNC_SQUARE = 6,
    LATTICE_SI_7X7_SKEW  = 7,
    LATTICE_PENROSE_VERT = 8,
    LATTICE_PENROSE_CENT = 9,
    LATTICE_SI_7X7       = 10,
} LatSynthType;

typedef enum {
    LAT_SURFACE_FLAT       = 0,
    LAT_SURFACE_LINEAR     = 1,
    LAT_SURFACE_BUMPY      = 2,
    LAT_SURFACE_RADIAL     = 3,
    LAT_SURFACE_SEGMENTED  = 4,
    LAT_SURFACE_ZSEGMENTED = 5,
    LAT_SURFACE_BORDER     = 6,
    LAT_SURFACE_ZBORDER    = 7,
    LAT_SURFACE_SECOND     = 8,
    LAT_NSURFACES,
} LatSynthSurfaceType;

enum {
    PARAM_SURF_ENABLED,
    PARAM_SURF_WEIGHT,
    PARAM_SURF_LOWER,
    PARAM_SURF_UPPER,
    NSURFPARAMS
};

enum {
    PARAM_TYPE,
    PARAM_SIZE,
    PARAM_LRELAXATION,
    PARAM_HRELAXATION,
    PARAM_ANGLE,
    PARAM_SIGMA,
    PARAM_TAU,
    PARAM_HEIGHT,
    PARAM_ACTIVE_SURFACE,

    PARAM_SURF0,
    /* Here we reserve space for all the parameters of individual surface (renderer) types. */

    PARAM_SEED = PARAM_SURF0 + LAT_NSURFACES*NSURFPARAMS,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,
    HEADER_ORIENTATION,
    HEADER_DEFORMATION,
    HEADER_SURFACE,

    PARAM_DIMS0
};

typedef struct {
    GwyXY v; /* line equation: v*r == d */
    gdouble d;
} VoronoiLine;

typedef struct {
    GwyXY pos; /* coordinates */
    VoronoiLine rel; /* precomputed coordinates relative to currently processed object and their norm */
    gdouble angle; /* precomputed angle relative to currently processed object (similar as rel) */
    gdouble random;  /* a random number in [0,1], generated to be always the same for the same grid size */
    gdouble rlxrandom; /* relaxed random */
    GSList *ne; /* neighbour list */
} VoronoiObject;

typedef struct {
    GwyRandGenSet *rngset;
    GSList **squares; /* (hsq+2*SQBORDER)*(wsq+2*SQBORDER) VoronoiObject list */
    gint wsq;         /* width in squares (unextended) */
    gint hsq;         /* height in squares (unextended) */
    gdouble scale;    /* ratio of square side to the average cell size */
} VoronoiState;

typedef guint   (*LatPlacePointsFunc) (int i, int j, GwyXY *xy);
typedef void    (*LatIteratePointFunc)(int *i, int *j);
typedef gdouble (*RenderFunc)         (const GwyXY *point, const VoronoiObject *owner, gdouble scale);

typedef struct {
    LatPlacePointsFunc place_points;
    LatIteratePointFunc iterate;
    gdouble point_density;
} LatSynthLattice;

typedef struct {
    GwyXY a;
    GwyXY b;
    GwyXY c;
    gboolean is_wide;
} LatSynthPenroseTriangle;

typedef struct {
    const gchar *key;
    const gchar *name;
    RenderFunc render;
} LatSynthSurface;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Expensive calculated data. */
    VoronoiState *vstate;
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_lattice;
    GwyParamTable *table_surface[LAT_NSURFACES];
    GtkWidget *surface_widget;
    GtkWidget *surface_vbox;
    GtkWidget *surface_treeview;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register             (void);
static GwyParamDef*     define_module_params        (void);
static void             lat_synth                   (GwyContainer *data,
                                                     GwyRunType runtype);
static gboolean         execute                     (ModuleArgs *args,
                                                     GtkWindow *wait_window,
                                                     gboolean show_progress_bar);
static GwyDialogOutcome run_gui                     (ModuleArgs *args,
                                                     GwyContainer *data,
                                                     gint id);
static GtkWidget*       dimensions_tab_new          (ModuleGUI *gui);
static GtkWidget*       lattice_tab_new             (ModuleGUI *gui);
static GtkWidget*       surface_tab_new             (ModuleGUI *gui);
static GwyParamTable*   make_surface_param_table    (ModuleGUI *gui,
                                                     LatSynthSurfaceType i);
static void             param_changed               (ModuleGUI *gui,
                                                     gint id);
static void             dialog_response             (GwyDialog *dialog,
                                                     gint response,
                                                     ModuleGUI *gui);
static void             preview                     (gpointer user_data);
static void             update_surface_sensitivity  (ModuleGUI *gui,
                                                     LatSynthSurfaceType i);
static GtkWidget*       create_surface_treeview     (ModuleGUI *gui);
static void             construct_surface           (ModuleArgs *args);
static VoronoiState*    make_randomized_grid        (ModuleArgs *args);
static void             random_squarized_points     (VoronoiState *vstate,
                                                     guint npts);
static void             create_regular_points       (VoronoiState *vstate,
                                                     ModuleArgs *args,
                                                     gint xres,
                                                     gint yres);
static void             create_penrose_points       (VoronoiState *vstate,
                                                     ModuleArgs *args,
                                                     gint xres,
                                                     gint yres);
static guint            penrose_double_step         (LatSynthPenroseTriangle *triangles,
                                                     guint n,
                                                     LatSynthPenroseTriangle *buf);
static guint            penrose_single_step         (const LatSynthPenroseTriangle *coarse,
                                                     guint n,
                                                     LatSynthPenroseTriangle *fine);
static guint            sort_uniq_points            (GwyXYZ *points,
                                                     guint n);
static gboolean         place_point_to_square       (VoronoiState *vstate,
                                                     GwyXY *pos,
                                                     gdouble random);
static GwyDataField*    make_displacement_map       (guint xres,
                                                     guint yres,
                                                     gdouble sigma,
                                                     gdouble tau,
                                                     GRand *rng);
static VoronoiState*    relax_lattice               (VoronoiState *vstate,
                                                     gdouble relax);
static gdouble          cell_area_and_centre_of_mass(VoronoiObject *obj,
                                                     GwyXY *centre);
static void             find_cell_vertices          (VoronoiObject *obj);
static void             init_relaxed_random         (VoronoiState *vstate);
static void             relax_random_values         (VoronoiState *vstate,
                                                     gdouble relax);
static void             find_voronoi_neighbours_iter(VoronoiState *vstate,
                                                     gint iter);
static VoronoiObject*   find_owner                  (VoronoiState *vstate,
                                                     const GwyXY *point);
static void             neighbourize                (GSList *ne0,
                                                     const GwyXY *center);
static void             compute_segment_angles      (GSList *ne0);
static VoronoiObject*   move_along_line             (const VoronoiObject *owner,
                                                     const GwyXY *start,
                                                     const GwyXY *end,
                                                     gint *next_safe);
static void             voronoi_state_free          (VoronoiState *vstate);

DECLARE_SURFACE(flat);
DECLARE_SURFACE(linear);
DECLARE_SURFACE(bumpy);
DECLARE_SURFACE(radial);
DECLARE_SURFACE(segmented);
DECLARE_SURFACE(zsegmented);
DECLARE_SURFACE(border);
DECLARE_SURFACE(zborder);
DECLARE_SURFACE(second);

static const LatSynthSurface surfaces[LAT_NSURFACES] = {
    { "flat",       N_("Random constant"),         surface_flat,       },
    { "linear",     N_("Random linear"),           surface_linear,     },
    { "bumpy",      N_("Random bumpy"),            surface_bumpy,      },
    { "radial",     N_("Radial distance"),         surface_radial,     },
    { "segmented",  N_("Segmented distance"),      surface_segmented,  },
    { "zsegmented", N_("Segmented random"),        surface_zsegmented, },
    { "border",     N_("Border distance"),         surface_border,     },
    { "zborder",    N_("Border random"),           surface_zborder,    },
    { "second",     N_("Second nearest distance"), surface_second,     },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates surfaces based on regular or random lattices."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, lat_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("lat_synth",
                              (GwyProcessFunc)&lat_synth,
                              N_("/S_ynthetic/_Lattice..."),
                              GWY_STOCK_SYNTHETIC_LATTICE,
                              RUN_MODES,
                              0,
                              N_("Generate lattice based surface"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum lattices[] = {
        { N_("lattice|Random"),             LATTICE_RANDOM,       },
        { N_("lattice|Square"),             LATTICE_SQUARE,       },
        { N_("lattice|Hexagonal"),          LATTICE_HEXAGONAL,    },
        { N_("lattice|Triangular"),         LATTICE_TRIANGULAR,   },
        { N_("lattice|Cairo"),              LATTICE_SQTRIG_VERT,  },
        { N_("lattice|Snub square"),        LATTICE_SQTRIG_CENT,  },
        { N_("lattice|Truncated square"),   LATTICE_TRUNC_SQUARE, },
        { N_("Silicon 7x7"),                LATTICE_SI_7X7,       },
        { N_("Skewed silicon 7x7"),         LATTICE_SI_7X7_SKEW,  },
        { N_("lattice|Penrose (vertices)"), LATTICE_PENROSE_VERT, },
        { N_("lattice|Penrose (centers)"),  LATTICE_PENROSE_CENT, },
    };
    static GwyEnum *surface_enum = NULL;
    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    surface_enum = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(surfaces), surfaces, sizeof(LatSynthSurface),
                                             G_STRUCT_OFFSET(LatSynthSurface, name), -1);

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Lattice"),
                              lattices, G_N_ELEMENTS(lattices), LATTICE_RANDOM);
    gwy_param_def_add_double(paramdef, PARAM_SIZE, "size", _("Si_ze"), 4.0, 1000.0, 40.0);
    gwy_param_def_add_double(paramdef, PARAM_LRELAXATION, "lrelaxation",  _("Lattice rela_xation"), 0.0, 16.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HRELAXATION, "hrelaxation",  _("_Height relaxation"), 0.0, 200.0, 0.0);
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Amplitude"), 0.0, 100.0, 10.0);
    gwy_param_def_add_double(paramdef, PARAM_TAU, "tau", _("_Lateral scale"), 0.1, 1000.0, 50.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ACTIVE_SURFACE, "active_surface", NULL,
                              surface_enum, G_N_ELEMENTS(surfaces), LAT_SURFACE_RADIAL);
    for (i = 0; i < LAT_NSURFACES; i++) {
        gboolean enabled_by_default = (i == LAT_SURFACE_RADIAL);
        const gchar *key = surfaces[i].key;
        /* NB: We leak the keys intentionally to make them static. */
        gwy_param_def_add_boolean(paramdef, PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_ENABLED,
                                  g_strconcat(key, "/enabled", NULL), _("Enabled"), enabled_by_default);
        gwy_param_def_add_double(paramdef, PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_WEIGHT,
                                 g_strconcat(key, "/weight", NULL), _("_Weight"), -1.0, 1.0, 1.0);
        gwy_param_def_add_double(paramdef, PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_LOWER,
                                 g_strconcat(key, "/lower", NULL), _("Lower threshold"), 0.0, 1.0, 0.0);
        gwy_param_def_add_double(paramdef, PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_UPPER,
                                 g_strconcat(key, "/upper", NULL), _("Upper threshold"), 0.0, 1.0, 1.0);
    }
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
lat_synth(GwyContainer *data, GwyRunType runtype)
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
    if (!execute(&args, gwy_app_find_window_for_channel(data, id), TRUE))
        goto end;
    gwy_synth_add_result_to_file(args.result, data, id, args.params);

end:
    if (args.vstate)
        voronoi_state_free(args.vstate);
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GtkNotebook *notebook;
    GtkWidget *dataview, *hbox;
    ModuleGUI gui;
    guint i;

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

    gui.dialog = gwy_dialog_new(_("Lattice"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    for (i = 0; i < LAT_NSURFACES; i++)
        gui.table_surface[i] = make_surface_param_table(&gui, i);

    gtk_notebook_append_page(notebook, dimensions_tab_new(&gui), gtk_label_new(_("Dimensions")));
    gtk_notebook_append_page(notebook, lattice_tab_new(&gui), gtk_label_new(_("Lattice")));
    gtk_notebook_append_page(notebook, surface_tab_new(&gui), gtk_label_new(_("Surface")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    g_signal_connect_swapped(gui.table_dimensions, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_lattice, "param-changed", G_CALLBACK(param_changed), &gui);
    for (i = 0; i < LAT_NSURFACES; i++)
        g_signal_connect_swapped(gui.table_surface[i], "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
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
lattice_tab_new(ModuleGUI *gui)
{
    GwyParamTable *table;

    table = gui->table_lattice = gwy_param_table_new(gui->args->params);

    gwy_param_table_append_combo(table, PARAM_TYPE);

    gwy_param_table_append_header(table, -1, _("Size"));
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_SIZE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_LRELAXATION);
    gwy_param_table_set_unitstr(table, PARAM_LRELAXATION, _("steps"));
    gwy_param_table_append_slider(table, PARAM_HRELAXATION);
    gwy_param_table_set_unitstr(table, PARAM_HRELAXATION, _("steps"));

    gwy_param_table_append_header(table, HEADER_ORIENTATION, _("Orientation"));
    gwy_param_table_append_slider(table, PARAM_ANGLE);

    gwy_param_table_append_header(table, HEADER_DEFORMATION, _("Deformation"));
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_slider_add_alt(table, PARAM_SIGMA);
    gwy_param_table_append_slider(table, PARAM_TAU);
    gwy_param_table_slider_set_mapping(table, PARAM_TAU, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_TAU);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
surface_tab_new(ModuleGUI *gui)
{
    LatSynthSurfaceType i = gwy_params_get_enum(gui->args->params, PARAM_ACTIVE_SURFACE);
    GwyParamTable *partable = gui->table_surface[i];
    GtkWidget *vbox, *scwin;

    vbox = gui->surface_vbox = gwy_vbox_new(4);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    //gtk_widget_set_size_request(scwin, -1, 240);
    gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

    gui->surface_treeview = create_surface_treeview(gui);
    gtk_container_add(GTK_CONTAINER(scwin), gui->surface_treeview);

    gui->surface_widget = gwy_param_table_widget(partable);
    gtk_box_pack_end(GTK_BOX(vbox), gui->surface_widget, FALSE, FALSE, 0);

    return vbox;
}

static GwyParamTable*
make_surface_param_table(ModuleGUI *gui, LatSynthSurfaceType i)
{
    GwyParamTable *table = gwy_param_table_new(gui->args->params);
    gint first_id = PARAM_SURF0 + i*NSURFPARAMS;

    gwy_param_table_append_header(table, HEADER_SURFACE, _(surfaces[i].name));
    gwy_param_table_append_slider(table, first_id + PARAM_SURF_WEIGHT);
    gwy_param_table_slider_set_mapping(table, first_id + PARAM_SURF_WEIGHT, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, first_id + PARAM_SURF_LOWER);
    gwy_param_table_slider_set_mapping(table, first_id + PARAM_SURF_LOWER, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_slider(table, first_id + PARAM_SURF_UPPER);
    gwy_param_table_slider_set_mapping(table, first_id + PARAM_SURF_UPPER, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return table;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table_lattice;
    gboolean id_is_surface = (id >= PARAM_DIMS0 && id < PARAM_DIMS0 + LAT_NSURFACES*NSURFPARAMS);
    guint i;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_HEIGHT };

        for (i = 0; i < LAT_NSURFACES; i++) {
            gwy_synth_update_value_unitstrs(gui->table_surface[i], zids, G_N_ELEMENTS(zids));
            gwy_synth_update_like_current_button_sensitivity(gui->table_surface[i], BUTTON_LIKE_CURRENT_IMAGE);
        }
    }
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_SIZE, PARAM_SIGMA, PARAM_TAU };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0 || id == PARAM_TYPE) {
        gboolean is_non_random = (gwy_params_get_enum(params, PARAM_TYPE) != LATTICE_RANDOM);

        gwy_param_table_set_sensitive(table, HEADER_ORIENTATION, is_non_random);
        gwy_param_table_set_sensitive(table, PARAM_ANGLE, is_non_random);
        gwy_param_table_set_sensitive(table, HEADER_DEFORMATION, is_non_random);
        gwy_param_table_set_sensitive(table, PARAM_SIGMA, is_non_random);
        gwy_param_table_set_sensitive(table, PARAM_TAU, is_non_random);
    }
    if (id < 0 || id_is_surface) {
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(gui->surface_treeview));

        gwy_null_store_row_changed(GWY_NULL_STORE(model), gwy_params_get_enum(params, PARAM_ACTIVE_SURFACE));
    }

    if (id < 0 || id == PARAM_TYPE || id == PARAM_SIZE || id == PARAM_LRELAXATION || id == PARAM_HRELAXATION
        || id == PARAM_ANGLE || id == PARAM_SIGMA || id == PARAM_TAU || id == PARAM_SEED) {
        if (args->vstate) {
            voronoi_state_free(args->vstate);
            args->vstate = NULL;
        }
    }

    if ((id < PARAM_DIMS0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE)
        && id != PARAM_UPDATE && id != PARAM_RANDOMIZE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    guint i;

    if (response == GWY_RESPONSE_SYNTH_INIT_Z) {
        gdouble zscale = args->zscale;
        gint power10z;

        if (zscale > 0.0) {
            gwy_params_get_unit(args->params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
            for (i = 0; i < LAT_NSURFACES; i++)
                gwy_param_table_set_double(gui->table_surface[i], PARAM_HEIGHT, zscale/pow10(power10z));
        }
    }
    else if (response == GWY_RESPONSE_SYNTH_TAKE_DIMS) {
        gwy_synth_use_dimensions_template(gui->table_dimensions);
    }
    else if (response == GWY_RESPONSE_RESET) {
        for (i = 0; i < LAT_NSURFACES; i++)
            gwy_params_reset(args->params, PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_ENABLED);
        param_changed(gui, -1);
        gtk_widget_queue_draw(gui->surface_treeview);
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args, NULL, FALSE);
    gwy_data_field_data_changed(gui->args->result);
}

static void
enabled_toggled(ModuleGUI *gui, const gchar *strpath, G_GNUC_UNUSED GtkCellRendererToggle *toggle)
{
    GtkTreeView *treeview = GTK_TREE_VIEW(gui->surface_treeview);
    GtkTreeModel *model = gtk_tree_view_get_model(treeview);
    GtkTreePath *path;
    GtkTreeIter iter;
    gint id;
    guint i;

    path = gtk_tree_path_new_from_string(strpath);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);
    gtk_tree_model_get(model, &iter, 0, &i, -1);
    id = PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_ENABLED;
    gwy_params_set_boolean(gui->args->params, id, !gwy_params_get_boolean(gui->args->params, id));
    gwy_null_store_row_changed(GWY_NULL_STORE(model), i);
    param_changed(gui, id);
}

static void
render_enabled(G_GNUC_UNUSED GtkTreeViewColumn *column, GtkCellRenderer *renderer,
               GtkTreeModel *model, GtkTreeIter *iter,
               gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gint id;
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    id = PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_ENABLED;
    g_object_set(renderer, "active", gwy_params_get_boolean(gui->args->params, id), NULL);
}

static void
render_name(G_GNUC_UNUSED GtkTreeViewColumn *column, GtkCellRenderer *renderer,
            GtkTreeModel *model, GtkTreeIter *iter,
            G_GNUC_UNUSED gpointer user_data)
{
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    g_object_set(renderer, "text", _(surfaces[i].name), NULL);
}

static void
render_weight(G_GNUC_UNUSED GtkTreeViewColumn *column, GtkCellRenderer *renderer,
              GtkTreeModel *model, GtkTreeIter *iter,
              gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gchar buf[12];
    gint id;
    guint i;

    gtk_tree_model_get(model, iter, 0, &i, -1);
    id = PARAM_SURF0 + i*NSURFPARAMS + PARAM_SURF_WEIGHT;
    g_snprintf(buf, sizeof(buf), "%.03f", gwy_params_get_double(gui->args->params, id));
    g_object_set(renderer, "text", buf, NULL);
}

static void
surface_selected(ModuleGUI *gui, GtkTreeSelection *selection)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    guint i;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &i, -1);
    gwy_params_set_enum(gui->args->params, PARAM_ACTIVE_SURFACE, i);

    if (gui->surface_widget) {
        gtk_widget_destroy(gui->surface_widget);
        gui->surface_widget = NULL;
    }
    gui->surface_widget = gwy_param_table_widget(gui->table_surface[i]);
    gtk_widget_show_all(gui->surface_widget);
    gtk_box_pack_end(GTK_BOX(gui->surface_vbox), gui->surface_widget, FALSE, FALSE, 0);
    update_surface_sensitivity(gui, i);
}

static void
update_surface_sensitivity(ModuleGUI *gui, LatSynthSurfaceType i)
{
    gint first_id = PARAM_SURF0 + i*NSURFPARAMS;
    gboolean enabled = gwy_params_get_boolean(gui->args->params, first_id + PARAM_SURF_ENABLED);
    GwyParamTable *partable = gui->table_surface[i];

    gwy_param_table_set_sensitive(partable, HEADER_SURFACE, enabled);
    gwy_param_table_set_sensitive(partable, first_id + PARAM_SURF_WEIGHT, enabled);
    gwy_param_table_set_sensitive(partable, first_id + PARAM_SURF_LOWER, enabled);
    gwy_param_table_set_sensitive(partable, first_id + PARAM_SURF_UPPER, enabled);
}

static GtkWidget*
create_surface_treeview(ModuleGUI *gui)
{
    GtkTreeModel *model;
    GtkWidget *treeview;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeSelection *selection;

    model = GTK_TREE_MODEL(gwy_null_store_new(LAT_NSURFACES));
    treeview = gtk_tree_view_new_with_model(model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    g_object_unref(model);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_toggle_new();
    g_object_set(renderer, "activatable", TRUE, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_enabled, gui, NULL);
    g_signal_connect_swapped(renderer, "toggled", G_CALLBACK(enabled_toggled), gui);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_name, gui, NULL);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_expand(column, FALSE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "width-chars", 7, "xalign", 1.0, NULL);
    gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(column), renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func(column, renderer, render_weight, gui, NULL);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);
    g_signal_connect_swapped(selection, "changed", G_CALLBACK(surface_selected), gui);

    return treeview;
}

static gboolean
check_progress(VoronoiState *vstate, const gchar *message, gdouble step, gdouble nsteps, gboolean show_progress_bar)
{
    if (!show_progress_bar)
        return TRUE;
    if (gwy_app_wait_set_message(message) && gwy_app_wait_set_fraction(step/nsteps))
        return TRUE;
    if (vstate)
        voronoi_state_free(vstate);
    gwy_app_wait_finish();
    return FALSE;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window, gboolean show_progress_bar)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble lrelaxation = gwy_params_get_double(params, PARAM_LRELAXATION);
    gdouble hrelaxation = gwy_params_get_double(params, PARAM_HRELAXATION);
    VoronoiState *vstate = args->vstate;
    guint iter, niter, nsteps, step = 0;
    gdouble r;

    if (show_progress_bar)
        gwy_app_wait_start(wait_window, _("Initializing..."));

    nsteps = 2 + (guint)ceil(lrelaxation/1.25) + 2;
    if (!check_progress(NULL, _("Constructing lattice..."), step, nsteps, show_progress_bar))
        return FALSE;

    if (!vstate) {
        r = lrelaxation;
        vstate = make_randomized_grid(args);
        if (!check_progress(vstate, _("Triangulating..."), ++step, nsteps, show_progress_bar))
            return FALSE;
        niter = (vstate->wsq + 2*SQBORDER)*(vstate->hsq + 2*SQBORDER);
        for (iter = 0; iter < niter; iter++) {
            find_voronoi_neighbours_iter(vstate, iter);
            /* TODO: Update progress bar, if necessary. */
        }

        while (r > 1e-9) {
            if (!check_progress(vstate, _("Relaxing lattice..."), ++step, nsteps, show_progress_bar))
                return FALSE;
            /* Overrelax slightly, but not much. */
            vstate = relax_lattice(vstate, MIN(r, 1.25));
            r -= 1.25;
        }
    }

    if (!check_progress(vstate, _("Relaxing heights..."), ++step, nsteps, show_progress_bar))
        return FALSE;
    init_relaxed_random(vstate);
    r = hrelaxation;
    while (r > 1e-9) {
        relax_random_values(vstate, MIN(r, 1.0));
        r -= 1.0;
    }

    if (!check_progress(vstate, _("Rendering surface..."), ++step, nsteps, show_progress_bar))
        return FALSE;

    args->vstate = vstate;
    gwy_data_field_clear(args->result);
    construct_surface(args);
    if (args->field && do_initialise)
        gwy_data_field_sum_fields(args->result, args->result, args->field);

    if (show_progress_bar)
        gwy_app_wait_finish();
    return TRUE;
}

static void
construct_surface(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    GwyDataField *field = args->result, *tmpfield = gwy_data_field_new_alike(field, FALSE);
    VoronoiState *vstate = args->vstate;
    VoronoiObject *owner, *line_start;
    GwyXY z, zline, tmp;
    gint hsafe, vsafe, power10z;
    guint xres, yres, x, y, i;
    gdouble q, xoff, yoff, scale;
    gdouble *data;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    if (xres <= yres) {
        q = (gdouble)vstate->wsq/xres;
        xoff = SQBORDER;
        yoff = SQBORDER + 0.5*(q*yres - vstate->hsq);
    }
    else {
        q = (gdouble)vstate->hsq/yres;
        xoff = SQBORDER + 0.5*(q*xres - vstate->wsq);
        yoff = SQBORDER;
    }

    scale = vstate->scale;
    data = gwy_data_field_get_data(tmpfield);
    for (i = 0; i < LAT_NSURFACES; i++) {
        gint first_id = PARAM_SURF0 + i*NSURFPARAMS;
        gboolean enabled = gwy_params_get_boolean(params, first_id + PARAM_SURF_ENABLED);
        gdouble weight = gwy_params_get_double(params, first_id + PARAM_SURF_WEIGHT);
        gdouble lower = gwy_params_get_double(params, first_id + PARAM_SURF_LOWER);
        gdouble upper = gwy_params_get_double(params, first_id + PARAM_SURF_UPPER);

        if (!enabled || !weight || lower > upper)
            continue;

        gwy_data_field_clear(tmpfield);
        zline.x = xoff;
        zline.y = yoff;
        line_start = find_owner(vstate, &zline);
        vsafe = 0;

        for (y = 0; y < yres; ) {
            hsafe = 0;
            z = zline;
            owner = line_start;

            neighbourize(owner->ne, &owner->pos);
            compute_segment_angles(owner->ne);

            tmp.y = zline.y;

            for (x = 0; x < xres; ) {
                data[y*xres + x] = surfaces[i].render(&z, owner, scale);

                /* Move right. */
                x++;
                if (hsafe-- == 0) {
                    tmp.x = q*x + xoff;
                    owner = move_along_line(owner, &z, &tmp, &hsafe);
                    neighbourize(owner->ne, &owner->pos);
                    compute_segment_angles(owner->ne);
                    z.x = tmp.x;
                }
                else
                    z.x = q*x + xoff;
            }

            /* Move down. */
            y++;
            if (vsafe-- == 0) {
                tmp.x = xoff;
                tmp.y = q*y + yoff;
                line_start = move_along_line(line_start, &zline, &tmp, &vsafe);
                zline.y = tmp.y;
            }
            else
                zline.y = q*y + yoff;
        }

        gwy_data_field_invalidate(tmpfield);
        gwy_data_field_normalize(tmpfield);
        if (lower > 0.0 || upper < 1.0)
            gwy_data_field_clamp(tmpfield, lower, upper);
        gwy_data_field_linear_combination(field, 1.0, field, weight, tmpfield, 0.0);
    }

    g_object_unref(tmpfield);
    gwy_data_field_renormalize(field, height, 0.0);
}

static VoronoiState*
make_randomized_grid(ModuleArgs *args)
{
    GwyParams *params = args->params;
    LatSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble size = gwy_params_get_double(params, PARAM_SIZE);
    VoronoiState *vstate;
    gdouble scale, a;
    guint xres, yres, npts, wsq, hsq, extwsq, exthsq, extxres, extyres;

    xres = gwy_data_field_get_xres(args->result);
    yres = gwy_data_field_get_yres(args->result);

    /* Compute square size trying to get density per square around 7. The shorter side of the field will be divided to
     * squares exactly, the longer side may have more squares, i.e. slightly wider border around the field than
     * SQBORDER. */
    gwy_debug("Field: %ux%u, size %g", xres, yres, size);
    if (xres <= yres) {
        wsq = (gint)ceil(xres/(sqrt(7.0)*size));
        a = xres/(gdouble)wsq;
        hsq = (gint)ceil((1.0 - EPS)*yres/a);
    }
    else {
        hsq = (gint)ceil(yres/(sqrt(7.0)*size));
        a = yres/(gdouble)hsq;
        wsq = (gint)ceil((1.0 - EPS)*xres/a);
    }
    gwy_debug("Squares: %ux%u", wsq, hsq);
    scale = a/size;
    gwy_debug("Scale: %g, Density: %g", scale, scale*scale);
    extwsq = wsq + 2*SQBORDER;
    exthsq = hsq + 2*SQBORDER;
    npts = ceil(exthsq*extwsq*scale*scale);
    if (npts < exthsq*extwsq) {
        /* XXX: This means we have only a handful of points in the image. The result is not worth much anyway. */
        npts = exthsq*extwsq;
    }

    vstate = g_new(VoronoiState, 1);
    vstate->squares = g_new0(GSList*, extwsq*exthsq);
    vstate->hsq = hsq;
    vstate->wsq = wsq;
    vstate->scale = scale;
    vstate->rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(vstate->rngset, gwy_params_get_int(params, PARAM_SEED));

    if (type == LATTICE_RANDOM) {
        random_squarized_points(vstate, npts);
        return vstate;
    }

    extxres = GWY_ROUND(a*extwsq);
    extyres = GWY_ROUND(a*exthsq);
    if (type == LATTICE_PENROSE_VERT || type == LATTICE_PENROSE_CENT)
        create_penrose_points(vstate, args, extxres, extyres);
    else
        create_regular_points(vstate, args, extxres, extyres);

    return vstate;
}

static void
random_squarized_points(VoronoiState *vstate, guint npts)
{
    guint exthsq = vstate->hsq + 2*SQBORDER;
    guint extwsq = vstate->wsq + 2*SQBORDER;
    GRand *rng = gwy_rand_gen_set_rng(vstate->rngset, RNG_POINTS);
    VoronoiObject *obj;
    guint i, j, k, nsq, nempty, nrem;

    nsq = extwsq*exthsq;
    g_assert(npts >= nsq);
    nempty = nsq;
    nrem = npts;

    /* First place points randomly to the entire area.  For preiew, this part does not depend on the mean cell size
     * which is good because the radnom lattice changes more or less smoothly with size then. */
    while (nrem > nempty) {
        GwyXY pos;
        pos.x = g_rand_double(rng)*(extwsq - 2.0*EPS) + EPS;
        pos.y = g_rand_double(rng)*(exthsq - 2.0*EPS) + EPS;
        if (place_point_to_square(vstate, &pos, g_rand_double(rng)))
            nempty--;
        nrem--;
    }

    gwy_debug("Placed %u points into %u squares, %u empty squares left.", npts, nsq, nrem);

    if (!nrem)
        return;

    /* We still have some empty squares.  Must place a point to each.  This depends strongly on the mean cell size but
     * influences only a tiny fraction (≈ 10⁻⁴) of points. */
    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            if (vstate->squares[k])
                continue;

            obj = g_slice_new0(VoronoiObject);
            obj->pos.x = (1.0 - 2.0*EPS)*g_rand_double(rng) + EPS + j;
            obj->pos.y = (1.0 - 2.0*EPS)*g_rand_double(rng) + EPS + i;
            obj->random = g_rand_double(rng);
            vstate->squares[k] = g_slist_prepend(NULL, obj);
        }
    }
}

static inline void
iterate_square(int *i, int *j)
{
    if (*i > 0 && (ABS(*j) < *i || *j == *i))
        (*j)--;
    else if (*i <= 0 && ABS(*j) <= -(*i))
        (*j)++;
    else if (*j > 0 && ABS(*i) < *j)
        (*i)++;
    else
        (*i)--;
}

static inline void
iterate_hexagonal(int *i, int *j)
{
    if (*i <= 0 && *j <= 0) {
        (*i)--;
        (*j)++;
    }
    else if (*i >= 0 && *j > 0) {
        (*i)++;
        (*j)--;
    }
    else if (*j > 0 && -(*i) <= *j)
        (*i)++;
    else if (*j < 0 && *i <= -(*j))
        (*i)--;
    else if (*i > 0)
        (*j)--;
    else
        (*j)++;
}

static inline guint
place_points_square(int i, int j, GwyXY *xy)
{
    xy[0].x = j;
    xy[0].y = -i;
    return 1;
}

static inline guint
place_points_hexagonal(int i, int j, GwyXY *xy)
{
    xy[0].x = j + 0.26794919243112270648*i;
    xy[0].y = -i - 0.26794919243112270648*j;
    return 1;
}

static inline guint
place_points_triangular(int i, int j, GwyXY *xy)
{
    if (ABS(j - i) % 3 == 0)
        return 0;

    /* Scale factor ensures the same point density as for random (square). */
    xy[0].x = j + 0.26794919243112270648*i;
    xy[0].y = -i - 0.26794919243112270648*j;
    return 1;
}

static inline guint
place_points_sqtrig_vert(int i, int j, GwyXY *xy)
{
    xy[0].x = j;
    xy[0].y = -i;
    xy[1].x = j + 0.5;
    xy[1].y = -i + 0.13397459621556135323;
    xy[2].x = j + 0.36602540378443864675;
    xy[2].y = -i - 0.36602540378443864675;
    xy[3].x = j - 0.13397459621556135323;
    xy[3].y = -i + 0.5;
    return 4;
}

static inline guint
place_points_sqtrig_cent(int i, int j, GwyXY *xy)
{
    xy[0].x = j + 0.18301270189221932337;
    xy[0].y = -i + 0.31698729810778067661;
    xy[1].x = j - 0.21132486540518711774;
    xy[1].y = -i + 0.21132486540518711774;
    xy[2].x = j - 0.31698729810778067661;
    xy[2].y = -i - 0.18301270189221932337;
    xy[3].x = j + 0.0773502691896257645;
    xy[3].y = -i - 0.28867513459481288225;
    xy[4].x = j + 0.28867513459481288225;
    xy[4].y = -i - 0.0773502691896257645;
    xy[5].x = j - 0.42264973081037423548;
    xy[5].y = -i + 0.42264973081037423548;
    return 6;
}

static inline guint
place_points_trunc_square(int i, int j, GwyXY *xy)
{
    xy[0].x = j;
    xy[0].y = -i - 0.29289321881345254;
    xy[1].x = j;
    xy[1].y = -i + 0.29289321881345254;
    xy[2].x = j - 0.29289321881345254;
    xy[2].y = -i;
    xy[3].x = j + 0.29289321881345254;
    xy[3].y = -i;
    return 4;
}

/* This is an accident.  But the lattice is nice, so keep it. */
static inline guint
place_points_si7x7skew(int i, int j, GwyXY *xy)
{
    gdouble xc = j + 0.26794919243112270648*i;
    gdouble yc = -i - 0.26794919243112270648*j;

    xy[0].x = xc + 0.24832722628789;
    xy[0].y = yc - 0.066539079742502;
    xy[1].x = xc + 0.066539079742502;
    xy[1].y = yc - 0.24832722628789;
    xy[2].x = xc - 0.18178814654539;
    xy[2].y = yc - 0.18178814654539;
    xy[3].x = xc - 0.24832722628789;
    xy[3].y = yc + 0.066539079742502;
    xy[4].x = xc - 0.066539079742502;
    xy[4].y = yc + 0.24832722628789;
    xy[5].x = xc + 0.18178814654539;
    xy[5].y = yc + 0.18178814654539;
    xy[6].x = xc + 0.53326953987125;
    xy[6].y = yc - 0.0098109830716139;
    xy[7].x = xc - 0.45691947705714;
    xy[7].y = yc - 0.27513133051174;
    xy[8].x = xc + 0.25813820935951;
    xy[8].y = yc - 0.46673046012875;
    xy[9].x = xc + 0.46673046012875;
    xy[9].y = yc - 0.25813820935951;
    xy[10].x = xc - 0.27513133051174;
    xy[10].y = yc - 0.45691947705714;
    xy[11].x = xc + 0.0098109830716141;
    xy[11].y = yc - 0.53326953987125;
    return 12;
}

static inline guint
place_points_si7x7(int i, int j, GwyXY *xy)
{
    gdouble xc = j + 0.26794919243112270648*i;
    gdouble yc = -i - 0.26794919243112270648*j;

    xy[0].x = xc + 0.24832722628789;
    xy[0].y = yc + 0.066539079742502;
    xy[1].x = xc + 0.18178814654539;
    xy[1].y = yc - 0.18178814654539;
    xy[2].x = xc - 0.066539079742502;
    xy[2].y = yc - 0.24832722628789;
    xy[3].x = xc - 0.24832722628789;
    xy[3].y = yc - 0.066539079742502;
    xy[4].x = xc - 0.18178814654539;
    xy[4].y = yc + 0.18178814654539;
    xy[5].x = xc + 0.066539079742502;
    xy[5].y = yc + 0.24832722628789;
    xy[6].x = xc + 0.53326953987125;
    xy[6].y = yc - 0.0098109830716139;
    xy[7].x = xc - 0.45691947705714;
    xy[7].y = yc - 0.27513133051174;
    xy[8].x = xc + 0.25813820935951;
    xy[8].y = yc - 0.46673046012875;
    xy[9].x = xc + 0.46673046012875;
    xy[9].y = yc - 0.25813820935951;
    xy[10].x = xc - 0.27513133051174;
    xy[10].y = yc - 0.45691947705714;
    xy[11].x = xc + 0.0098109830716141;
    xy[11].y = yc - 0.53326953987125;
    return 12;
}

static void
create_regular_points(VoronoiState *vstate, ModuleArgs *args, gint xres, gint yres)
{
    static const LatSynthLattice lattice_types[] = {
        { NULL,                      NULL,              0.0,                },
        { place_points_square,       iterate_square,    1.0,                },
        { place_points_hexagonal,    iterate_hexagonal, 1.0379548493020427, },
        { place_points_triangular,   iterate_hexagonal, 0.8474865856124707, },
        { place_points_sqtrig_vert,  iterate_square,    2.0,                },
        { place_points_sqtrig_cent,  iterate_square,    2.449489742783178,  },
        { place_points_trunc_square, iterate_square,    2.0,                },
        { place_points_si7x7skew,    iterate_hexagonal, 3.5955810699072708, },
        { NULL,                      NULL,              0.0,                },
        { NULL,                      NULL,              0.0,                },
        { place_points_si7x7,        iterate_hexagonal, 3.5955810699072708, },
    };

    GwyParams *params = args->params;
    LatSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_TAU);
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    GwyDataField *displacement_x, *displacement_y;
    guint exthsq = vstate->hsq + 2*SQBORDER;
    guint extwsq = vstate->wsq + 2*SQBORDER;
    gdouble limit = MAX(exthsq*exthsq, extwsq*extwsq);
    GRand *rng = gwy_rand_gen_set_rng(vstate->rngset, RNG_POINTS);
    GRand *rng_x = gwy_rand_gen_set_rng(vstate->rngset, RNG_DISPLAC_X);
    GRand *rng_y = gwy_rand_gen_set_rng(vstate->rngset, RNG_DISPLAC_Y);
    gdouble scale = vstate->scale, cth, sth, maxdist2 = 0.0;
    LatPlacePointsFunc place_points = lattice_types[type].place_points;
    LatIteratePointFunc iterate = lattice_types[type].iterate;
    GwyXY cpos[MAXLATPOINTS], pos;
    gint i = 0, j = 0, disp_i, disp_j;
    const gdouble *dx_data, *dy_data;
    guint npts, ipt;
    G_GNUC_UNUSED guint total_npts = 0;

    g_return_if_fail(place_points && iterate);

    displacement_x = make_displacement_map(xres, yres, 0.1*sigma, tau, rng_x);
    displacement_y = make_displacement_map(xres, yres, 0.1*sigma, tau, rng_y);
    dx_data = gwy_data_field_get_data(displacement_x);
    dy_data = gwy_data_field_get_data(displacement_y);

    scale /= lattice_types[type].point_density;
    cth = cos(angle);
    sth = sin(angle);
    do {
        npts = place_points(i, j, cpos);
        for (ipt = 0; ipt < npts; ipt++) {
            /* Rotate and scale. */
            pos.x = (cth*cpos[ipt].x + sth*cpos[ipt].y)/scale;
            pos.y = (-sth*cpos[ipt].x + cth*cpos[ipt].y)/scale;
            maxdist2 = fmax(DOTPROD_SS(pos, pos), maxdist2);

            pos.x += 0.5*extwsq;
            pos.y += 0.5*exthsq;

            disp_j = GWY_ROUND(pos.x/extwsq*xres);
            disp_j = CLAMP(disp_j, 0, xres-1);
            disp_i = GWY_ROUND(pos.y/exthsq*yres);
            disp_i = CLAMP(disp_i, 0, yres-1);

            pos.x += dx_data[disp_i*xres + disp_j];
            pos.y += dy_data[disp_i*xres + disp_j];

            /* The randomisation here is to avoid some numeric troubles when there is no displacement. */
            pos.x += 0.0001*(g_rand_double(rng) - 0.00005);
            pos.y += 0.0001*(g_rand_double(rng) - 0.00005);

            if (pos.x >= EPS && pos.y >= EPS && pos.x <= extwsq - 2*EPS && pos.y <= exthsq - 2*EPS) {
                place_point_to_square(vstate, &pos, g_rand_double(rng));
                total_npts++;
            }
        }
        iterate(&i, &j);
    } while (maxdist2 <= limit);

    gwy_debug("number of points: %u", total_npts);

    g_object_unref(displacement_y);
    g_object_unref(displacement_x);
}

static void
create_penrose_points(VoronoiState *vstate, ModuleArgs *args, gint xres, gint yres)
{
    GwyParams *params = args->params;
    LatSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble sigma = gwy_params_get_double(params, PARAM_SIGMA);
    gdouble tau = gwy_params_get_double(params, PARAM_TAU);
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    GwyDataField *displacement_x, *displacement_y;
    guint exthsq = vstate->hsq + 2*SQBORDER;
    guint extwsq = vstate->wsq + 2*SQBORDER;
    gdouble limit = hypot(exthsq, extwsq);
    GRand *rng = gwy_rand_gen_set_rng(vstate->rngset, RNG_POINTS);
    GRand *rng_x = gwy_rand_gen_set_rng(vstate->rngset, RNG_DISPLAC_X);
    GRand *rng_y = gwy_rand_gen_set_rng(vstate->rngset, RNG_DISPLAC_Y);
    gdouble scale = vstate->scale, cth, sth, t, maxdist2 = 0.0;
    GwyXYZ *points;
    GwyXY cpos, pos;
    gint disp_i, disp_j;
    const gdouble *dx_data, *dy_data;
    guint i, ntri, n, nsteps, total_npts = 0;
    LatSynthPenroseTriangle *triangles, *tribuf;

    /* For a reason not completely clear to me, this is a good point density for both types. */
    scale /= G_SQRT2;
    nsteps = (guint)ceil(log(0.5*scale*limit/cos(G_PI/10.0))/log(PHI));
    nsteps |= 1;
    gwy_debug("number of refinement steps: %u", nsteps);
    cth = cos((nsteps + 1)*G_PI/10.0);
    sth = sin((nsteps + 1)*G_PI/10.0);

    ntri = (guint)ceil(((5.0 + 3.0*SQRT5)*pow(0.5*(3.0 + SQRT5), nsteps)
                        + (5.0 - 3.0*SQRT5)*pow(0.5*(3.0 - SQRT5), nsteps)));
    gwy_debug("estimated number of triangles: %u", ntri);

    triangles = g_new(LatSynthPenroseTriangle, ntri);
    tribuf = g_new(LatSynthPenroseTriangle, ntri);
    for (i = 0; i < 10; i++) {
        tribuf[i].a.x = tribuf[i].a.y = 0.0;
        if (i % 2) {
            tribuf[i].b.x = cos((2.0*i - 1.0)*G_PI/10.0);
            tribuf[i].b.y = sin((2.0*i - 1.0)*G_PI/10.0);
            tribuf[i].c.x = cos((2.0*i + 1.0)*G_PI/10.0);
            tribuf[i].c.y = sin((2.0*i + 1.0)*G_PI/10.0);
        }
        else {
            tribuf[i].b.x = cos((2.0*i + 1.0)*G_PI/10.0);
            tribuf[i].b.y = sin((2.0*i + 1.0)*G_PI/10.0);
            tribuf[i].c.x = cos((2.0*i - 1.0)*G_PI/10.0);
            tribuf[i].c.y = sin((2.0*i - 1.0)*G_PI/10.0);
        }
        tribuf[i].is_wide = FALSE;
    }
    n = 10;

    /* Fix the mutual rotation of different refinements. */
    while (nsteps >= 2) {
        n = penrose_double_step(tribuf, n, triangles);
        nsteps -= 2;
    }
    g_assert(nsteps == 1);
    n = penrose_single_step(tribuf, n, triangles);
    nsteps--;
    gwy_debug("true number of triangles: %u", n);
    g_free(tribuf);

    if (type == LATTICE_PENROSE_VERT) {
        points = g_new(GwyXYZ, 3*n);
        for (i = 0; i < n; i++) {
            points[3*i + 0].x = triangles[i].a.x;
            points[3*i + 0].y = triangles[i].a.y;
            points[3*i + 1].x = triangles[i].b.x;
            points[3*i + 1].y = triangles[i].b.y;
            points[3*i + 2].x = triangles[i].c.x;
            points[3*i + 2].y = triangles[i].c.y;
        }
        n = 3*n;
    }
    else if (type == LATTICE_PENROSE_CENT) {
        points = g_new(GwyXYZ, n);
        for (i = 0; i < n; i++) {
            points[i].x = (triangles[i].c.x + triangles[i].a.x/PHI)/PHI;
            points[i].y = (triangles[i].c.y + triangles[i].a.y/PHI)/PHI;
        }
    }
    else {
        g_return_if_reached();
    }
    g_free(triangles);

    for (i = 0; i < n; i++) {
        t = points[i].x*cth + points[i].y*sth;
        points[i].y = -points[i].x*sth + points[i].y*cth;
        points[i].x = t;
    }

    n = sort_uniq_points(points, n);
    gwy_debug("number of unique points: %u", n);

    displacement_x = make_displacement_map(xres, yres, 0.1*sigma, tau, rng_x);
    displacement_y = make_displacement_map(xres, yres, 0.1*sigma, tau, rng_y);
    dx_data = gwy_data_field_get_data(displacement_x);
    dy_data = gwy_data_field_get_data(displacement_y);

    cth = cos(angle);
    sth = sin(angle);
    for (i = 0; i < n; i++) {
        cpos.x = points[i].x;
        cpos.y = points[i].y;

        /* Rotate and scale. */
        pos.x = (cth*cpos.x + sth*cpos.y)/scale;
        pos.y = (-sth*cpos.x + cth*cpos.y)/scale;

        pos.x += 0.5*extwsq;
        pos.y += 0.5*exthsq;
        maxdist2 = fmax(DOTPROD_SS(pos, pos), maxdist2);

        disp_j = GWY_ROUND(pos.x/extwsq*xres);
        disp_j = CLAMP(disp_j, 0, xres-1);
        disp_i = GWY_ROUND(pos.y/exthsq*yres);
        disp_i = CLAMP(disp_i, 0, yres-1);

        pos.x += dx_data[disp_i*xres + disp_j];
        pos.y += dy_data[disp_i*xres + disp_j];

        if (pos.x >= 0.0001 && pos.y >= 0.0001 && pos.x <= extwsq - 0.0001 && pos.y <= exthsq - 0.0001) {
            /* The randomisation here is to avoid some numeric troubles when there is no displacement. */
            pos.x += 0.0001*(g_rand_double(rng) - 0.00005);
            pos.y += 0.0001*(g_rand_double(rng) - 0.00005);
            place_point_to_square(vstate, &pos, g_rand_double(rng));
            total_npts++;
        }
    }

    gwy_debug("number of points: %u (%g)", total_npts, total_npts/(exthsq*extwsq*vstate->scale*vstate->scale));
    gwy_debug("true maxdist: %g (limit %g)", sqrt(maxdist2), limit);

    g_object_unref(displacement_y);
    g_object_unref(displacement_x);
}

/* Perform always two steps of refinement.  The odd and even refinements are different.  We want the ‘sun’
 * configuration with a star inside (not a decagon).  Furthermore, even two refinement steps do not provide the same
 * points; the pattern is rotated by pi/5.  But we can fix that by an explicit final rotation. */
static guint
penrose_double_step(LatSynthPenroseTriangle *triangles, guint n, LatSynthPenroseTriangle *buf)
{
    n = penrose_single_step(triangles, n, buf);
    n = penrose_single_step(buf, n, triangles);
    return n;
}

static guint
penrose_single_step(const LatSynthPenroseTriangle *coarse, guint n, LatSynthPenroseTriangle *fine)
{
    guint i, j = 0;

    for (i = 0; i < n; i++) {
        GwyXY A, B, C, P, Q, R;

        A.x = PHI*coarse[i].a.x;
        A.y = PHI*coarse[i].a.y;
        B.x = PHI*coarse[i].b.x;
        B.y = PHI*coarse[i].b.y;
        C.x = PHI*coarse[i].c.x;
        C.y = PHI*coarse[i].c.y;
        if (coarse[i].is_wide) {
            P.x = coarse[i].b.x + coarse[i].a.x/PHI;
            P.y = coarse[i].b.y + coarse[i].a.y/PHI;

            fine[j].a = A;
            fine[j].b = P;
            fine[j].c = C;
            fine[j].is_wide = FALSE;
            j++;

            fine[j].a = B;
            fine[j].b = C;
            fine[j].c = P;
            fine[j].is_wide = TRUE;
            j++;
        }
        else {
            Q.x = coarse[i].a.x + coarse[i].b.x/PHI;
            Q.y = coarse[i].a.y + coarse[i].b.y/PHI;
            R.x = coarse[i].c.x + coarse[i].a.x/PHI;
            R.y = coarse[i].c.y + coarse[i].a.y/PHI;

            fine[j].a = A;
            fine[j].b = R;
            fine[j].c = Q;
            fine[j].is_wide = TRUE;
            j++;

            fine[j].a = B;
            fine[j].b = Q;
            fine[j].c = R;
            fine[j].is_wide = FALSE;
            j++;

            fine[j].a = B;
            fine[j].b = C;
            fine[j].c = R;
            fine[j].is_wide = FALSE;
            j++;
        }
    }

    return j;
}

static int
compare_xyz_z(const void *a, const void *b)
{
    const GwyXYZ *pa = (const GwyXYZ*)a;
    const GwyXYZ *pb = (const GwyXYZ*)b;

    if (pa->z < pb->z)
        return -1;
    if (pa->z > pb->z)
        return 1;
    return 0;
}

/* This should be used for undeformed penrose lattice (regular grids are already create with stable point order).  The
 * z-coordinate is used as a scratch space. */
static guint
sort_uniq_points(GwyXYZ *points, guint n)
{
    guint i, ii, j, start;
    gdouble firstval;

    for (i = 0; i < n; i++)
        points[i].z = points[i].x*points[i].x + points[i].y*points[i].y;
    qsort(points, n, sizeof(GwyXYZ), compare_xyz_z);

    i = ii = 0;
    do {
        start = i;
        firstval = points[start].z;
        do {
            i++;
        } while (i < n && points[i].z - firstval < 1e-9);

        /* Fix angles around the split line. */
        for (j = start; j < i; j++)
            points[j].z = gwy_canonicalize_angle(atan2(points[j].y, points[j].x) + 1e-9, FALSE, TRUE);
        qsort(points + start, i-start, sizeof(GwyXYZ), compare_xyz_z);

        do {
            j = start;
            firstval = points[start].z;
            do {
                j++;
            } while (j < i && points[j].z - firstval < 1e-9);
            points[ii++] = points[start];
            start = j;
        } while (start < i);
    } while (i < n);

    return ii;
}

static gboolean
place_point_to_square(VoronoiState *vstate, GwyXY *pos, gdouble prandom)
{
    VoronoiObject *obj;
    G_GNUC_UNUSED guint exthsq = vstate->hsq + 2*SQBORDER;
    guint extwsq = vstate->wsq + 2*SQBORDER;
    gint i = (gint)floor(pos->y);
    gint j = (gint)floor(pos->x);
    guint k;

#ifdef DEBUG
    g_assert(i >= 0);
    g_assert(j >= 0);
    g_assert(i < exthsq);
    g_assert(j < extwsq);
#endif

    obj = g_slice_new0(VoronoiObject);
    obj->pos = *pos;
    obj->random = prandom;

    k = extwsq*i + j;
    if (!vstate->squares[k]) {
        vstate->squares[k] = g_slist_prepend(NULL, obj);
        return TRUE;
    }

    vstate->squares[k] = g_slist_prepend(vstate->squares[k], obj);
    return FALSE;
}

static GwyDataField*
make_displacement_map(guint xres, guint yres, gdouble sigma, gdouble tau, GRand *rng)
{
    GwyDataField *field = gwy_data_field_new(xres, yres, 1.0, 1.0, TRUE);
    gwy_data_field_synth_gaussian_displacement(field, sigma, tau, rng);
    return field;
}

static inline GwyXY
coords_minus(const GwyXY *a, const GwyXY *b)
{
    GwyXY z;

    z.x = a->x - b->x;
    z.y = a->y - b->y;

    return z;
}

static inline GwyXY
coords_plus(const GwyXY *a, const GwyXY *b)
{
    GwyXY z;

    z.x = a->x + b->x;
    z.y = a->y + b->y;

    return z;
}

static VoronoiState*
relax_lattice(VoronoiState *oldvstate, gdouble relax)
{
    VoronoiState *vstate;
    guint extwsq = oldvstate->wsq + 2*SQBORDER;
    guint exthsq = oldvstate->hsq + 2*SQBORDER;
    guint i, j, k;
    gdouble r;
    GSList *l;

    vstate = g_new0(VoronoiState, 1);
    vstate->squares = g_new0(GSList*, extwsq*exthsq);
    vstate->hsq = oldvstate->hsq;
    vstate->wsq = oldvstate->wsq;
    vstate->scale = oldvstate->scale;

    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            r = ((i == 0 || j == 0 || i == exthsq-1 || j == extwsq-1) ? 0.0 : relax);

            for (l = oldvstate->squares[k]; l; l = l->next) {
                VoronoiObject *oldobj = VOBJ(l);

                if (r > 0.0) {
                    GwyXY pos;
                    cell_area_and_centre_of_mass(oldobj, &pos);
                    pos.x = r*pos.x + (1.0 - r)*oldobj->pos.x;
                    pos.y = r*pos.y + (1.0 - r)*oldobj->pos.y;
                    place_point_to_square(vstate, &pos, oldobj->random);
                }
                else {
                    place_point_to_square(vstate, &oldobj->pos, oldobj->random);
                }
            }
        }
    }

    vstate->rngset = oldvstate->rngset;
    oldvstate->rngset = NULL;
    voronoi_state_free(oldvstate);

    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            find_voronoi_neighbours_iter(vstate, k);
        }
    }

    return vstate;
}

/* ne requirements: cyclic
 * destroys neighbourisaion by recycling rel! */
static gdouble
cell_area_and_centre_of_mass(VoronoiObject *obj, GwyXY *centre)
{
    GSList *ne = obj->ne, *ne2 = ne->next;
    gdouble area = 0.0;

    find_cell_vertices(obj);
    gwy_clear(centre, 1);
    do {
        const GwyXY *v1 = &VOBJ(ne)->rel.v;
        const GwyXY *v2 = &VOBJ(ne2)->rel.v;
        GwyXY mid = coords_plus(v1, v2);
        gdouble a = CROSSPROD_PP(v1, v2);

        area += a;
        centre->x += mid.x * a;
        centre->y += mid.y * a;

        ne = ne2;
        ne2 = ne->next;
    } while (ne != obj->ne);

    centre->x = obj->pos.x + centre->x/(6.0*area);
    centre->y = obj->pos.y + centre->y/(6.0*area);

    return 0.5*area;
}

/* Calculate vertices of the Voronoi cell, storing them in rel. */
static void
find_cell_vertices(VoronoiObject *obj)
{
    GSList *ne = obj->ne, *ne2;

    do {
        GwyXY v1, v2;
        gdouble D, l1, l2;

        ne2 = ne->next;
        v1 = coords_minus(&VOBJ(ne)->pos, &obj->pos);
        v2 = coords_minus(&VOBJ(ne2)->pos, &obj->pos);

        l1 = DOTPROD_SS(v1, v1);
        l2 = DOTPROD_SS(v2, v2);
        D = 2.0*CROSSPROD_SS(v1, v2);
        VOBJ(ne)->rel.v.x = (l1*v2.y - l2*v1.y)/D;
        VOBJ(ne)->rel.v.y = (v1.x*l2 - v2.x*l1)/D;

        ne = ne2;
    } while (ne != obj->ne);
}

static void
init_relaxed_random(VoronoiState *vstate)
{
    guint extwsq = vstate->wsq + 2*SQBORDER;
    guint exthsq = vstate->hsq + 2*SQBORDER;
    guint i, j, k;
    GSList *l;

    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            for (l = vstate->squares[k]; l; l = l->next) {
                VoronoiObject *obj = VOBJ(l);
                obj->rlxrandom = obj->random;
            }
        }
    }
}

static void
relax_random_values(VoronoiState *vstate, gdouble relax)
{
    guint extwsq = vstate->wsq + 2*SQBORDER;
    guint exthsq = vstate->hsq + 2*SQBORDER;
    guint i, j, k;
    GSList *l;

    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            for (l = vstate->squares[k]; l; l = l->next) {
                VoronoiObject *obj = VOBJ(l);
                GSList *ne = obj->ne;
                gdouble w = 0.0, z = 0.0;

                do {
                    GwyXY v = coords_minus(&VOBJ(ne)->pos, &obj->pos);
                    gdouble v2 = 1.0/DOTPROD_SS(v, v);

                    w += v2;
                    z += v2*VOBJ(ne)->rlxrandom;
                    ne = ne->next;
                } while (ne != obj->ne);

                obj->angle = z/w;
            }
        }
    }

    for (i = 0; i < exthsq; i++) {
        for (j = 0; j < extwsq; j++) {
            k = extwsq*i + j;
            for (l = vstate->squares[k]; l; l = l->next) {
                VoronoiObject *obj = VOBJ(l);
                obj->rlxrandom += 0.5*relax*(obj->angle - obj->rlxrandom);
            }
        }
    }
}

static inline gdouble
angle(const GwyXY *r)
{
    return atan2(r->y, r->x);
}

static gint
vobj_angle_compare(gconstpointer x, gconstpointer y)
{
    gdouble xangle, yangle;

    xangle = ((const VoronoiObject*)x)->angle;
    yangle = ((const VoronoiObject*)y)->angle;

    if (xangle < yangle)
        return -1;
    if (xangle > yangle)
        return 1;
    return 0;
}

/* Returns TRUE if owner does not change and we can assume everything is neighbourised.  FALSE is returned if we moved
 * to another cell. */
static gboolean
find_delaunay_triangle(const GwyXY *point,
                       const VoronoiObject **owner,
                       const VoronoiObject **neigh1,
                       const VoronoiObject **neigh2)
{
    GwyXY dist;
    const GwyXY *v1, *v2, *v;
    const VoronoiObject *pivot;
    GSList *ne1, *ne2, *ne;
    gdouble cp1, cp2;
    guint iter = 0;

    /* Find the two neighbours that bracket the direction to the point. */
    dist = coords_minus(point, &((*owner)->pos));
    ne1 = (*owner)->ne;
    ne2 = ne1->next;
    while (TRUE) {
        v1 = &VOBJ(ne1)->rel.v;
        v2 = &VOBJ(ne2)->rel.v;
        if ((cp1 = CROSSPROD_PS(v1, dist)) >= 0 && (cp2 = CROSSPROD_SP(dist, v2)) >= 0)
            break;
        ne1 = ne2;
        ne2 = ne2->next;
    }

    if (CROSSPROD_PP(v1, v2) - cp1 - cp2 >= 0.0) {
        /* OK, we are inside the right Delaunay triangle. */
        *neigh1 = VOBJ(ne1);
        *neigh2 = VOBJ(ne2);
        return TRUE;
    }

    /* We are not.  The somewhat slower path is to check the opposite cell that also has ne1 and ne2 neighbours. */
    while (TRUE) {
        GwyXY tdist;
        gdouble a1, a2, a12;

        /* Find ne1 and the third point (ne) in the neighbour list of ne2. */
        pivot = VOBJ(ne2);
        for (ne = pivot->ne; (const VoronoiObject*)VOBJ(ne) != VOBJ(ne1); ne = ne->next)
            ;
        ne1 = ne;
        ne2 = ne->next;

        v = &pivot->pos;
        v1 = &VOBJ(ne1)->pos;
        v2 = &VOBJ(ne2)->pos;

        dist = coords_minus(point, v);
        tdist = coords_minus(v1, v);
        a1 = CROSSPROD_SS(tdist, dist);
        /* Are both sides of the line the wrong side?  Well...  Probably we are almost exactly on that line so nothing
         * bad will happen if we just give up.  Seems very rare in practice. */
        if (G_UNLIKELY(a1 < 0))
            break;

        dist = coords_minus(point, v1);
        tdist = coords_minus(v2, v1);
        a12 = CROSSPROD_SS(tdist, dist);

        dist = coords_minus(point, v2);
        tdist = coords_minus(v, v2);
        a2 = CROSSPROD_SS(tdist, dist);

        if (a2 >= 0.0 && a12 >= 0.0)
            break;

        if (a12 >= 0.0) {
            ne1 = ne2;
            ne2 = ne;
        }
        else if (a2 >= 0.0) {
            /* ne1 and ne2 are already set as expected */
        }
        else {
            /* Is out point really in the shadow of ne2?  Well...  Just move on.  A more sophisticated decision method
             * could be used here but this again is almost impossible to trigger. */
            if (a2 < a1) {
                ne1 = ne2;
                ne2 = ne;
            }
        }

        /* Safety measure.  Seems very rare in practice. */
        if (++iter == 8)
            break;
    }

    *owner = pivot;   /* Does not mean anything, just the third vertex. */
    *neigh1 = VOBJ(ne1);
    *neigh2 = VOBJ(ne2);

    return FALSE;
}

/* owner->ne requirements: NONE */
static gdouble
surface_flat(G_GNUC_UNUSED const GwyXY *point, const VoronoiObject *owner, G_GNUC_UNUSED gdouble scale)
{
    return owner->rlxrandom;
}

/* owner->ne requirements: cyclic, neighbourized, segment angles */
static gdouble
surface_linear(const GwyXY *point, const VoronoiObject *owner, G_GNUC_UNUSED gdouble scale)
{
    const VoronoiObject *neigh1, *neigh2;
    GwyXY dist, v1, v2;
    gdouble D, c1, c2, c;

    if (find_delaunay_triangle(point, &owner, &neigh1, &neigh2)) {
        v1 = neigh1->rel.v;
        v2 = neigh2->rel.v;
    }
    else {
        v1 = coords_minus(&neigh1->pos, &owner->pos);
        v2 = coords_minus(&neigh2->pos, &owner->pos);
    }

    dist = coords_minus(point, &owner->pos);
    D = CROSSPROD_SS(v1, v2);
    c1 = -CROSSPROD_SS(v2, dist)/D;
    c2 = CROSSPROD_SS(v1, dist)/D;
    c = 1.0 - (c2 + c1);

    return c*owner->rlxrandom + c1*neigh1->rlxrandom + c2*neigh2->rlxrandom;
}

/* owner->ne requirements: cyclic, neighbourized, segment angles */
static gdouble
surface_bumpy(const GwyXY *point, const VoronoiObject *owner, G_GNUC_UNUSED gdouble scale)
{
    const VoronoiObject *neigh1, *neigh2;
    GwyXY dist, v1, v2;
    gdouble D, c1, c2, c, cs;

    if (find_delaunay_triangle(point, &owner, &neigh1, &neigh2)) {
        v1 = neigh1->rel.v;
        v2 = neigh2->rel.v;
    }
    else {
        v1 = coords_minus(&neigh1->pos, &owner->pos);
        v2 = coords_minus(&neigh2->pos, &owner->pos);
    }

    dist = coords_minus(point, &owner->pos);
    D = CROSSPROD_SS(v1, v2);
    c1 = -CROSSPROD_SS(v2, dist)/D;
    c2 = CROSSPROD_SS(v1, dist)/D;
    c = 1.0 - (c2 + c1);
    c1 *= c1*c1;
    c2 *= c2*c2;
    c *= c*c;
    cs = c + c1 + c2;

    return (c*owner->rlxrandom + c1*neigh1->rlxrandom + c2*neigh2->rlxrandom)/cs;
}

/* owner->ne requirements: NONE */
static gdouble
surface_radial(const GwyXY *point, const VoronoiObject *owner, gdouble scale)
{
    GwyXY dist;

    dist = coords_minus(point, &owner->pos);
    return scale*sqrt(DOTPROD_SS(dist, dist));
}

/* owner->ne requirements: cyclic, neighbourized, segment angles */
static gdouble
surface_segmented(const GwyXY *point, const VoronoiObject *owner, G_GNUC_UNUSED gdouble scale)
{
    GwyXY dist;
    gdouble phi;
    GSList *ne;

    ne = owner->ne;
    dist = coords_minus(point, &owner->pos);
    phi = angle(&dist);

    while ((phi >= VOBJ(ne)->angle) + (phi < VOBJ(ne->next)->angle) + (VOBJ(ne)->angle > VOBJ(ne->next)->angle) < 2)
        ne = ne->next;

    return 2*DOTPROD_SS(dist, VOBJ(ne)->rel.v)/VOBJ(ne)->rel.d;
}

/* owner->ne requirements: cyclic, neighbourized, segment angles */
static gdouble
surface_zsegmented(const GwyXY *point, const VoronoiObject *owner, G_GNUC_UNUSED gdouble scale)
{
    GwyXY dist;
    gdouble phi;
    GSList *ne;

    ne = owner->ne;
    dist = coords_minus(point, &owner->pos);
    phi = angle(&dist);

    while ((phi >= VOBJ(ne)->angle) + (phi < VOBJ(ne->next)->angle) + (VOBJ(ne)->angle > VOBJ(ne->next)->angle) < 2)
        ne = ne->next;

    return owner->rlxrandom*(2*DOTPROD_SS(dist, VOBJ(ne)->rel.v)/VOBJ(ne)->rel.d - 1);
}

/* owner->ne requirements: neighbourized */
static gdouble
surface_border(const GwyXY *point, const VoronoiObject *owner, gdouble scale)
{
    GwyXY dist;
    gdouble r, r_min = G_MAXDOUBLE;
    GSList *ne;

    dist = coords_minus(point, &owner->pos);

    for (ne = owner->ne; ; ne = ne->next) {
        r = fabs(VOBJ(ne)->rel.d/2 - DOTPROD_SS(dist, VOBJ(ne)->rel.v))/sqrt(VOBJ(ne)->rel.d);
        r_min = fmin(r_min, r);
        if (ne->next == owner->ne)
            break;
    }

    return 1.0 - 2.0*r_min*scale;
}

/* owner->ne requirements: neighbourized */
static gdouble
surface_zborder(const GwyXY *point, const VoronoiObject *owner, gdouble scale)
{
    GwyXY dist;
    gdouble r, r_min = G_MAXDOUBLE;
    GSList *ne;

    dist = coords_minus(point, &owner->pos);

    for (ne = owner->ne; ; ne = ne->next) {
        r = fabs(VOBJ(ne)->rel.d/2 - DOTPROD_SS(dist, VOBJ(ne)->rel.v))/sqrt(VOBJ(ne)->rel.d);
        r_min = fmin(r_min, r);
        if (ne->next == owner->ne)
            break;
    }

    return 1.0 - 2.0*r_min*scale*owner->rlxrandom;
}

/* owner->ne requirements: NONE */
static gdouble
surface_second(const GwyXY *point, const VoronoiObject *owner, gdouble scale)
{
    GwyXY dist;
    gdouble r, r_min = G_MAXDOUBLE;
    GSList *ne;

    for (ne = owner->ne; ; ne = ne->next) {
        dist = coords_minus(point, &VOBJ(ne)->pos);
        r = DOTPROD_SS(dist, dist);
        r_min = fmin(r_min, r);
        if (ne->next == owner->ne)
            break;
    }

    return 1.0 - sqrt(r_min)*scale;
}

/* compute segment angles
 * more precisely, VOBJ(ne)->angle will be set to start angle for segment from ne to ne->next (so end angle is in
 * ne->next)
 *
 * ne0 requirements: cyclic and neighbourized */
static void
compute_segment_angles(GSList *ne0)
{
    GSList *ne;
    VoronoiObject *p, *q;
    GwyXY z;

    ne = ne0;
    do {
        p = VOBJ(ne);
        q = VOBJ(ne->next);
        z.x = p->rel.d * q->rel.v.y - q->rel.d * p->rel.v.y;
        z.y = q->rel.d * p->rel.v.x - p->rel.d * q->rel.v.x;
        q->angle = angle(&z);
        ne = ne->next;
    } while (ne != ne0);
}

/* calculate intersection time t for intersection of lines:
 *
 * r = linevec*t + start
 * |r - a| = |r - b|
 */
static inline gdouble
intersection_time(const GwyXY *a, const GwyXY *b, const GwyXY *linevec, const GwyXY *start)
{
    GwyXY p, q;
    gdouble s;

    /* line dividing a-neighbourhood and b-neighbourhood */
    q = coords_minus(b, a);
    p = coords_plus(b, a);

    /* XXX: can be numerically unstable */
    s = DOTPROD_SP(q, linevec);
    if (fabs(s) < 1e-14)
        s = 1e-14; /* better than nothing */
    return (DOTPROD_SS(q, p)/2 - DOTPROD_SP(q, start))/s;
}

/* being in point start owned by owner (XXX: this condition MUST be true)
 * we want to get to point end and know our new owner
 * returns the new owner; in addition, when next_safe is not NULL it stores there number of times we can repeat move
 * along (end - start) vector still remaining in the new owner */
static VoronoiObject*
move_along_line(const VoronoiObject *owner, const GwyXY *start, const GwyXY *end, gint *next_safe)
{
    GwyXY linevec;
    VoronoiObject *ow;
    GSList *ne, *nearest = NULL;
    gdouble t, t_min, t_back;

    ow = (VoronoiObject*)owner;
    linevec = coords_minus(end, start);
    t_back = 0;
    /* XXX: start must be owned by owner, or else strange things will happen */
    while (TRUE) {
        t_min = HUGE_VAL;
        ne = ow->ne;
        do {
            /* find intersection with border line between ow and ne
             * FIXME: there apparently exist values t > t_back && t_back > t */
            t = intersection_time(&ow->pos, &VOBJ(ne)->pos, &linevec, start);
            if (t - t_back >= EPS && t < t_min) {
                t_min = t;
                nearest = ne;
            }
            ne = ne->next;
        } while (ne != ow->ne);

        /* no intersection inside the abscissa? then we are finished and can compute how many steps the same direction
         * will remain in ow's neighbourhood */
        if (t_min > 1) {
            if (next_safe == NULL)
                return ow;
            if (t_min == HUGE_VAL)
                *next_safe = G_MAXINT;
            else
                *next_safe = floor(t_min) - 1;
            return ow;
        }

        /* otherwise nearest intersection determines a new owner */
        ow = VOBJ(nearest);
        t_back = t_min; /* time value showing we are going back */
    }
}

/* find and return the owner of a point
 * NB: this is crude and should not be used for anything else than initial grip, use move_along_line() then works for
 * both cyclic and noncyclic ne-> */
static VoronoiObject*
find_owner(VoronoiState *vstate, const GwyXY *point)
{
    GSList *ne, **squares = vstate->squares;
    VoronoiObject *owner = NULL;
    GwyXY dist;
    gint jx, jy;
    gint ix, iy;
    gint wsq = vstate->wsq, hsq = vstate->hsq;
    gint extwsq = wsq + 2*SQBORDER;
    gdouble norm_min;

    jx = floor(point->x);
    jy = floor(point->y);

    /* These might be slightly non-true due to rounding errors.  Use clamps in production code. */
#ifdef DEBUG
    g_return_val_if_fail(jx >= SQBORDER, NULL);
    g_return_val_if_fail(jy >= SQBORDER, NULL);
    g_return_val_if_fail(jx < wsq + SQBORDER, NULL);
    g_return_val_if_fail(jy < hsq + SQBORDER, NULL);
#endif
    jx = CLAMP(jx, SQBORDER, wsq + SQBORDER-1);
    jy = CLAMP(jy, SQBORDER, hsq + SQBORDER-1);

    /* scan the 25-neighbourhood */
    norm_min = HUGE_VAL;
    for (ix = -SQBORDER; ix <= SQBORDER; ix++) {
        gint x = jx + ix;
        for (iy = -SQBORDER; iy <= SQBORDER; iy++) {
            gint y = jy + iy;
            gint k = y*extwsq + x;
            for (ne = squares[k]; ne != NULL; ne = ne->next) {
                dist = coords_minus(&VOBJ(ne)->pos, point);
                if (DOTPROD_SS(dist, dist) < norm_min) {
                    norm_min = DOTPROD_SS(dist, dist);
                    owner = VOBJ(ne);
                }
                if (ne->next == squares[k])
                    break;
            }
        }
    }

    return owner;
}

/* compute angles from rel.v relative coordinates
 *
 * ne0 requirements: neighbourized */
static void
compute_straight_angles(GSList *ne0)
{
    GSList *ne;
    VoronoiObject *p;

    for (ne = ne0; ne; ne = ne->next) {
        p = VOBJ(ne);
        p->angle = angle(&p->rel.v);
        if (ne->next == ne0)
            return;
    }
}

/* compute relative positions and norms to center center
 *
 * ne0 requirements: NONE */
static void
neighbourize(GSList *ne0, const GwyXY *center)
{
    GSList *ne;

    for (ne = ne0; ne; ne = ne->next) {
        VoronoiObject *p = VOBJ(ne);

        p->rel.v = coords_minus(&p->pos, center);
        p->rel.d = DOTPROD_SS(p->rel.v, p->rel.v);
        if (ne->next == ne0)
            return;
    }
}

/* return true iff point z (given as VoronoiLine) is shadowed by points a and b
 * (XXX: all coordiantes are relative) */
static inline gboolean
in_shadow(const VoronoiLine *a, const VoronoiLine *b, const GwyXY *z)
{
    GwyXY r, oa, ob, rz;
    gdouble s;

    /* Artifical fix for periodic grids, because in Real World This Just Does Not Happen; also mitigates the s == 0
     * case below, as the offending point would be probably removed here. */
    if (DOTPROD_SP(a->v, z) > 1.01*a->d && fabs(CROSSPROD_SP(a->v, z)) < 1e-12)
        return TRUE;
    if (DOTPROD_SP(b->v, z) > 1.01*b->d && fabs(CROSSPROD_SP(b->v, z)) < 1e-12)
        return TRUE;

    s = 2*CROSSPROD_SS(a->v, b->v);
    /* FIXME: what to do when s == 0 (or very near)??? */
    r.x = (a->d * b->v.y - b->d * a->v.y)/s;
    r.y = (b->d * a->v.x - a->d * b->v.x)/s;
    oa.x = -a->v.y;
    oa.y = a->v.x;
    ob.x = -b->v.y;
    ob.y = b->v.x;
    rz = coords_minus(z, &r);
    return (DOTPROD_SS(rz, rz) > DOTPROD_SS(r, r)
            && DOTPROD_PS(z, oa)*DOTPROD_SS(b->v, oa) > 0
            && DOTPROD_PS(z, ob)*DOTPROD_SS(a->v, ob) > 0);
}

static GSList*
extract_neighbourhood(GSList **squares, gint wsq, gint hsq, VoronoiObject *p)
{
    GSList *ne = NULL;
    gint jx, jy;
    gint ix, iy;
    gint xwsq, xhsq;

    xwsq = wsq + 2*SQBORDER;
    xhsq = hsq + 2*SQBORDER;

    jx = floor(p->pos.x);
    jy = floor(p->pos.y);

    /* construct the 37-neighbourhood list */
    for (ix = -3; ix <= 3; ix++) {
        gint x = jx + ix;
        if (x < 0 || x >= xwsq)
            continue;
        for (iy = -3; iy <= 3; iy++) {
            gint y = jy + iy;
            if ((ix == 3 || ix == -3) && (iy == 3 || iy == -3))
                continue;
            if (y < 0 || y >= xhsq)
                continue;
            ne = g_slist_concat(g_slist_copy(squares[y*xwsq + x]), ne);
            if (ix == 0 && iy == 0)
                ne = g_slist_remove(ne, p);
        }
    }

    g_assert(ne != NULL);

    /* compute relative coordinates and angles */
    neighbourize(ne, &p->pos);
    compute_straight_angles(ne);

    return ne;
}

static GSList*
shadow_filter(GSList *ne)
{
    GSList *ne1, *ne2;
    gint notremoved;
    gint len;

    if (ne == NULL)
        return ne;

    /* make the list cyclic if it isn't already (we have to unlink elements ourself then) */
    len = 1;
    for (ne2 = ne; ne2->next && ne2->next != ne; ne2 = ne2->next)
        len++;
    if (len < 3)
        return ne;
    ne2->next = ne;

    /* remove objects shadowed by their ancestors and successors
     * XXX: in non-degenerate case this is O(n*log(n)), but can be O(n*n) */
    ne1 = ne;
    notremoved = 0;
    do {
        ne2 = ne1->next;
        if (in_shadow(&VOBJ(ne1)->rel, &VOBJ(ne2->next)->rel, &VOBJ(ne2)->rel.v)) {
            ne1->next = ne2->next;
            g_slist_free_1(ne2);
            notremoved = 0;
            len--;
        }
        else {
            ne1 = ne2;
            notremoved++;
        }
    } while (notremoved < len && len > 2);

    return ne1; /* return cyclic list */
}

static void
find_voronoi_neighbours_iter(VoronoiState *vstate, gint iter)
{
    GSList *this;

    for (this = vstate->squares[iter]; this; this = this->next) {
        VoronoiObject *obj = VOBJ(this);

        obj->ne = extract_neighbourhood(vstate->squares, vstate->wsq, vstate->hsq, obj);
        obj->ne = g_slist_sort(obj->ne, &vobj_angle_compare);
        obj->ne = shadow_filter(obj->ne);
    }
}

static void
voronoi_state_free(VoronoiState *vstate)
{
    GSList *l;
    guint extwsq, exthsq, i;

    if (vstate->rngset)
        gwy_rand_gen_set_free(vstate->rngset);

    extwsq = vstate->wsq + 2*SQBORDER;
    exthsq = vstate->hsq + 2*SQBORDER;

    /* Neighbourhoods. */
    for (i = 0; i < extwsq*exthsq; i++) {
        for (l = vstate->squares[i]; l; l = l->next) {
            if (l && l->data && VOBJ(l)->ne) {
                GSList *ne = VOBJ(l)->ne->next;
                VOBJ(l)->ne->next = NULL; /* break cycles */
                g_slist_free(ne);
            }
        }
    }

    /* Grid contents. */
    for (i = 0; i < extwsq*exthsq; i++) {
        for (l = vstate->squares[i]; l; l = l->next)
            g_slice_free(VoronoiObject, l->data);
        g_slist_free(vstate->squares[i]);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
