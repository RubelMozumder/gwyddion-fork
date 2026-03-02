/*
 *  $Id: plateau_synth.c 25751 2023-10-02 11:05:17Z yeti-dn $
 *  Copyright (C) 2023 David Necas (Yeti).
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
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/spline.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    GRID_SIZE = 32,
};

enum {
    PARAM_MIN_SIZE,
    PARAM_MAX_SIZE,
    PARAM_SIZE_POWER,
    PARAM_IRREGULARITY,
    PARAM_OVERLAP,
    PARAM_HEIGHT,
    PARAM_HEIGHT_POWER,
    PARAM_HEIGHT_NOISE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

    PARAM_DIMS0
};

typedef enum {
    RNG_NPOINTS,
    RNG_SIZE,
    RNG_POINTS,
    RNG_POS,
    RNG_HEIGHT,
    RNG_NRNGS
} ObjSynthRng;

typedef struct {
    GwyXY *points;
    GwyXY *linemin;
    GwyXY *linemax;
    GwyXY min;
    GwyXY max;
    guint n;
    gdouble height;
    gdouble r0;
    /* For initial generation. */
    gboolean is_candidate;
} SampledSpline;

typedef struct {
    guint objno : 16;
    guint segno : 15;
    gboolean leaving_edge : 1;
} EdgeKey;

typedef struct {
    gdouble t;
    union {
        EdgeKey id;
        guint32 u;
    } key;
} Edge;

typedef struct {
    GArray *hevents;
    GArray *splines;
} AddHEventData;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image parameters. */
    gdouble zscale;  /* Negative value means there is no input image. */
    /* Cached computed splines. */
    GArray *sampled_splines;
    GArray **spline_grid;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             plateau_synth        (GwyContainer *data,
                                              GwyRunType runtype);
static gboolean         execute              (ModuleArgs *args,
                                              GtkWindow *wait_window);
static GwyDialogOutcome run_gui              (ModuleArgs *args,
                                              GwyContainer *data,
                                              gint id);
static GtkWidget*       dimensions_tab_new   (ModuleGUI *gui);
static GtkWidget*       generator_tab_new    (ModuleGUI *gui);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             dialog_response      (ModuleGUI *gui,
                                              gint response);
static void             preview              (gpointer user_data);
static void             free_sampled_spline  (SampledSpline *ss);
static void             clear_sampled_splines(ModuleArgs *args);
static void             sanitise_params      (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates images with stacked plateau-like structures."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, plateau_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("plateau_synth",
                              (GwyProcessFunc)&plateau_synth,
                              N_("/S_ynthetic/_Deposition/_Plateaus..."),
                              GWY_STOCK_SYNTHETIC_PLATEAUS,
                              RUN_MODES,
                              0,
                              N_("Generate image with random plateaus"));

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
    gwy_param_def_add_double(paramdef, PARAM_MAX_SIZE, "max-size", _("Ma_ximum size"), 6.0, 1200.0, 600.0);
    gwy_param_def_add_double(paramdef, PARAM_MIN_SIZE, "min-size", _("M_inimum size"), 5.0, 1000.0, 15.0);
    gwy_param_def_add_double(paramdef, PARAM_SIZE_POWER, "size-power", _("Size power _law"), 0.1, 1.0, 0.6);
    gwy_param_def_add_double(paramdef, PARAM_IRREGULARITY, "irregularity", _("Shape _irregularity"), 0.0, 1.0, 0.2);
    gwy_param_def_add_double(paramdef, PARAM_OVERLAP, "overlap", _("O_verlap fraction"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_POWER, "height-power", _("Scale with _power of size"),
                             -1.0, 2.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Height _spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);

    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
plateau_synth(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GwyDataField *field;
    gint id;
    guint i;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.field = field;
    args.zscale = field ? gwy_data_field_get_rms(field) : -1.0;
    args.sampled_splines = g_array_new(FALSE, FALSE, sizeof(SampledSpline));
    args.spline_grid = g_new(GArray*, GRID_SIZE*GRID_SIZE);
    for (i = 0; i < GRID_SIZE*GRID_SIZE; i++)
        args.spline_grid[i] = g_array_new(FALSE, FALSE, sizeof(guint));

    args.params = gwy_params_new_from_settings(define_module_params());
    gwy_synth_sanitise_params(args.params, PARAM_DIMS0, field);
    sanitise_params(&args);
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    args.result = gwy_synth_make_result_data_field((args.field = field), args.params, FALSE);
    clear_sampled_splines(&args);
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;
    gwy_synth_add_result_to_file(args.result, data, id, args.params);

end:
    clear_sampled_splines(&args);
    for (i = 0; i < GRID_SIZE*GRID_SIZE; i++)
        g_array_free(args.spline_grid[i], TRUE);
    g_free(args.spline_grid);
    g_array_free(args.sampled_splines, TRUE);
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

    gui.dialog = gwy_dialog_new(_("Random Plateaus"));
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

    gwy_param_table_append_header(table, -1, _("Generator"));
    gwy_param_table_append_slider(table, PARAM_MAX_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_MAX_SIZE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_MAX_SIZE);
    gwy_param_table_append_slider(table, PARAM_MIN_SIZE);
    gwy_param_table_slider_set_mapping(table, PARAM_MIN_SIZE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_slider_add_alt(table, PARAM_MIN_SIZE);
    gwy_param_table_append_slider(table, PARAM_SIZE_POWER);
    gwy_param_table_append_slider(table, PARAM_IRREGULARITY);
    gwy_param_table_append_slider(table, PARAM_OVERLAP);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_HEIGHT_NOISE);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_slider(table, PARAM_HEIGHT_POWER);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT_POWER, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_seed(table, PARAM_SEED);
    gwy_param_table_append_checkbox(table, PARAM_RANDOMIZE);

    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParamTable *table = gui->table_generator;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (gwy_synth_handle_param_changed(gui->table_dimensions, id))
        id = -1;

    if (id < 0 || id == PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT) {
        static const gint zids[] = { PARAM_HEIGHT };

        gwy_synth_update_value_unitstrs(table, zids, G_N_ELEMENTS(zids));
        gwy_synth_update_like_current_button_sensitivity(table, BUTTON_LIKE_CURRENT_IMAGE);
    }
    if (id < 0 || id == PARAM_MIN_SIZE) {
        gdouble rmin = gwy_params_get_double(params, PARAM_MIN_SIZE);
        gdouble rmax = gwy_params_get_double(params, PARAM_MAX_SIZE);
        if (rmax < rmin)
            gwy_param_table_set_double(table, PARAM_MAX_SIZE, rmin);
    }
    if (id < 0 || id == PARAM_MAX_SIZE) {
        gdouble rmin = gwy_params_get_double(params, PARAM_MIN_SIZE);
        gdouble rmax = gwy_params_get_double(params, PARAM_MAX_SIZE);
        if (rmax < rmin)
            gwy_param_table_set_double(table, PARAM_MIN_SIZE, rmax);
    }
    if (id < 0
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XYUNIT
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XRES
        || id == PARAM_DIMS0 + GWY_DIMS_PARAM_XREAL) {
        static const gint xyids[] = { PARAM_MAX_SIZE, PARAM_MIN_SIZE };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    /* These parameters do not necessitate generating splines afresh. */
    if (id != PARAM_HEIGHT && id != PARAM_HEIGHT_POWER && id != PARAM_HEIGHT_NOISE && id != PARAM_RANDOMIZE)
        clear_sampled_splines(args);

    if (id != PARAM_RANDOMIZE)
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

    if (execute(gui->args, GTK_WINDOW(gui->dialog)))
        gwy_data_field_data_changed(gui->args->result);
}

/* NB: This function assumes 1 < 2! */
static inline gboolean
intervals_intersect(gdouble a1, gdouble a2, gdouble b1, gdouble b2)
{
    /* For ordered interval endpoints there are only two non-intersecting cases. Detect those. */
    return !(a2 < b1 || b2 < a1);
}

/* NB: This function assumes 1 < 2 in both coordinates! */
static inline gboolean
boxes_intersect(const GwyXY *a1, const GwyXY *a2,
                const GwyXY *b1, const GwyXY *b2)
{
    return intervals_intersect(a1->x, a2->x, b1->x, b2->x) && intervals_intersect(a1->y, a2->y, b1->y, b2->y);
}

/* NB: This function assumes 1 < 2 in both coordinates! */
static inline gboolean
line_segments_intersect(const GwyXY *a1, const GwyXY *a2,
                        const GwyXY *b1, const GwyXY *b2,
                        GwyXY *intersection)
{
    gdouble dax = a2->x - a1->x, day = a2->y - a1->y;
    gdouble dbx = b2->x - b1->x, dby = b2->y - b1->y;
    gdouble D = dby*dax - day*dbx;
    gdouble ra, rb, x, y, la2, lb2, dpa, dpb;

    if (D == 0.0)
        return FALSE;

    ra = day*a1->x - dax*a1->y;
    rb = dby*b1->x - dbx*b1->y;
    x = (-ra*dbx + rb*dax)/D;
    y = (day*rb - dby*ra)/D;

    /* Since r-a1 is parallel to a2-a1, the inside-segment condition is just (r-a1)⋅(a2-a1)/|a1-a1|² ∈ [0,1].
     * And we write t ∈ [0,1] as |t-1/2| ≤ 1/2. */
    dpa = (x - a1->x)*dax + (y - a1->y)*day;
    dpb = (x - b1->x)*dbx + (y - b1->y)*dby;
    la2 = dax*dax + day*day;
    lb2 = dbx*dbx + dby*dby;

    if (intersection) {
        intersection->x = x;
        intersection->y = y;
    }

    return fabs(dpa/la2 - 0.5) <= 0.5 && fabs(dpb/lb2 - 0.5) <= 0.5;
}

static void
precompute_sampled_spline(GwySpline *spline, SampledSpline *ss)
{
    const GwyXY *natpoints;
    guint n, j;

    natpoints = gwy_spline_sample_naturally(spline, &n);
    ss->n = n;
    ss->points = g_memdup(natpoints, n*sizeof(GwyXY));
    ss->min.x = ss->min.y = G_MAXDOUBLE;
    ss->max.x = ss->max.y = -G_MAXDOUBLE;
    ss->linemax = g_new(GwyXY, n);
    ss->linemin = g_new(GwyXY, n);
    for (j = 0; j < n-1; j++) {
        ss->linemin[j] = ss->points[j];
        ss->linemax[j] = ss->points[j+1];
        GWY_ORDER(gdouble, ss->linemin[j].x, ss->linemax[j].x);
        GWY_ORDER(gdouble, ss->linemin[j].y, ss->linemax[j].y);
    }
    ss->linemin[n-1] = ss->points[n-1];
    ss->linemax[n-1] = ss->points[0];
    GWY_ORDER(gdouble, ss->linemin[n-1].x, ss->linemax[n-1].x);
    GWY_ORDER(gdouble, ss->linemin[n-1].y, ss->linemax[n-1].y);

    for (j = 0; j < n; j++) {
        ss->min.x = fmin(ss->min.x, ss->points[j].x);
        ss->max.x = fmax(ss->max.x, ss->points[j].x);
        ss->min.y = fmin(ss->min.y, ss->points[j].y);
        ss->max.y = fmax(ss->max.y, ss->points[j].y);
    }
}

static gboolean
sampled_splines_intersect(const SampledSpline *ss1, const SampledSpline *ss2)
{
    guint i1, i2, n1, n2;

    if (!boxes_intersect(&ss1->min, &ss1->max, &ss2->min, &ss2->max))
        return FALSE;

    n1 = ss1->n;
    n2 = ss2->n;
    for (i1 = 0; i1 < n1; i1++) {
        for (i2 = 0; i2 < n2; i2++) {
            if (!boxes_intersect(ss1->linemin + i1, ss1->linemax + i1, ss2->linemin + i2, ss2->linemax + i2))
                continue;
            if (line_segments_intersect(ss1->points + i1, ss1->points + (i1 + 1) % n1,
                                        ss2->points + i2, ss2->points + (i2 + 1) % n2,
                                        NULL))
                return TRUE;
        }
    }
    return FALSE;
}

static gboolean
sampled_spline_is_selfintersecting(const SampledSpline *ss)
{
    guint i1, i2, n = ss->n;

    for (i1 = 0; i1 < n-2; i1++) {
        for (i2 = i1+2; i2 < n; i2++) {
            if (i1 == 0 && i2 == n-1)
                continue;
            if (!boxes_intersect(ss->linemin + i1, ss->linemax + i1, ss->linemin + i2, ss->linemax + i2))
                continue;
            if (line_segments_intersect(ss->points + i1, ss->points + (i1 + 1) % n,
                                        ss->points + i2, ss->points + (i2 + 1) % n,
                                        NULL))
                return TRUE;
        }
    }
    return FALSE;
}

static Edge*
gather_vertical_events(GArray *splines, guint *pnseg)
{
    Edge *vevents;
    guint i, j, k, nseg;

    nseg = 0;
    for (i = 0; i < splines->len; i++) {
        SampledSpline *ss = &g_array_index(splines, SampledSpline, i);
        nseg += ss->n;
    }

    /* Gather where we enter and leave the vertical range of all line segments. This allows us to keep an active set
     * for each image row. */
    vevents = g_new(Edge, 2*nseg);
    k = 0;
    for (i = 0; i < splines->len; i++) {
        SampledSpline *ss = &g_array_index(splines, SampledSpline, i);
        for (j = 0; j < ss->n; j++) {
            /* Create edges. */
            vevents[k].t = ss->linemin[j].y;
            vevents[k].key.id.objno = i;
            vevents[k].key.id.segno = j;
            vevents[k].key.id.leaving_edge = FALSE;
            k++;
            vevents[k].t = ss->linemax[j].y;
            vevents[k].key.id.objno = i;
            vevents[k].key.id.segno = j;
            vevents[k].key.id.leaving_edge = TRUE;
            k++;
        }
    }
    qsort(vevents, 2*nseg, sizeof(Edge), gwy_compare_double);

    *pnseg = nseg;

    return vevents;
}

static void
add_hevent(gpointer key, G_GNUC_UNUSED gpointer value, gpointer user_data)
{
    AddHEventData *hed = (AddHEventData*)user_data;
    Edge edge;

    edge.key.u = GPOINTER_TO_UINT(key);
    edge.key.id.leaving_edge = FALSE;
    g_array_append_val(hed->hevents, edge);
}

static void
gather_image_row_events(GHashTable *active_set, GArray *splines, gdouble y, GArray *hevents)
{
    GwyXY hlinea, hlineb, intersection;
    AddHEventData hed;
    guint i;

    /* Gather all segments in range. */
    g_array_set_size(hevents, 0);
    hed.hevents = hevents;
    hed.splines = splines;
    g_hash_table_foreach(active_set, add_hevent, &hed);

    /* Find actual intersections with the horizontal line. */
    hlinea.y = hlineb.y = y;
    i = 0;
    while (i < hevents->len) {
        Edge *edge = &g_array_index(hevents, Edge, i);
        guint objno = edge->key.id.objno, segno = edge->key.id.segno;
        SampledSpline *ss = &g_array_index(splines, SampledSpline, objno);
        const GwyXY *a = ss->points + segno, *b = ss->points + (segno + 1) % ss->n;

        hlinea.x = ss->linemin[segno].x;
        hlineb.x = ss->linemax[segno].x;
        if (line_segments_intersect(a, b, &hlinea, &hlineb, &intersection)) {
            edge->t = intersection.x;
            i++;
        }
        else
            g_array_remove_index_fast(hevents, i);
    }
    qsort(hevents->data, hevents->len, sizeof(Edge), gwy_compare_double);
}

static gint
move_to_larger(const Edge *events, gint nedges, GHashTable *active_set, gdouble t, gint beforeedge)
{
    G_GNUC_UNUSED gboolean removed;
    Edge edge;

    gwy_debug("beforeedge %d, t %g, next edge t %g",
              beforeedge, t, beforeedge < nedges-1 ? events[beforeedge+1].t : G_MAXDOUBLE);
    while (beforeedge < nedges-1 && events[beforeedge+1].t <= t) {
        beforeedge++;
        edge = events[beforeedge];
        if (edge.key.id.leaving_edge) {
            /* The object id is stored as edge key with leaving_edge = FALSE. */
            edge.key.id.leaving_edge = FALSE;
            gwy_debug("leaving the range of object %u, segment %u", edge.key.id.objno, edge.key.id.segno);
            removed = g_hash_table_remove(active_set, GUINT_TO_POINTER(edge.key.u));
            g_assert(removed);
        }
        else {
            gwy_debug("entering the range of object %u, segment %u", edge.key.id.objno, edge.key.id.segno);
            g_hash_table_insert(active_set, GUINT_TO_POINTER(edge.key.u), GUINT_TO_POINTER(edge.key.u));
        }
    }

    return beforeedge;
}

/* ifrom is inclusive, ito is exclusive. */
static void
range_of_intersecting_grid_cells(gdouble tmin, gdouble tmax, gdouble res,
                                 guint *ifrom, guint *ito)
{
    gdouble t0 = -0.2*res, tn = 1.2*res;

    if (tmin <= t0)
        *ifrom = 0;
    else {
        *ifrom = (gint)floor((tmin - t0)/(1.4*res)*(GRID_SIZE - 1));
        *ifrom = MIN(*ifrom, GRID_SIZE-1);
    }

    if (tmax >= tn)
        *ito = GRID_SIZE;
    else {
        *ito = (gint)floor((tn - tmax)/(1.4*res)*(GRID_SIZE - 1));
        *ito = MIN(*ito, GRID_SIZE-1);
        *ito = GRID_SIZE-1 - *ito;
    }
}

static void
add_spline_to_grid_cells(const SampledSpline *ss, guint idx,
                         GArray **spline_grid,
                         gint xres, gint yres)
{
    guint ifrom, ito, jfrom, jto, i, j;

    range_of_intersecting_grid_cells(ss->min.x, ss->max.x, xres, &jfrom, &jto);
    range_of_intersecting_grid_cells(ss->min.y, ss->max.y, yres, &ifrom, &ito);
    for (i = ifrom; i < ito; i++) {
        for (j = jfrom; j < jto; j++)
            g_array_append_val(spline_grid[i*GRID_SIZE + j], idx);
    }
}

static void
find_intersection_candidates(const SampledSpline *ss,
                             GArray *splines, GArray **spline_grid, GArray *candidates,
                             gint xres, gint yres)
{
    guint ifrom, ito, jfrom, jto, i, j, k;
    GArray *grid_cell;

    /* Start by clearing the candidate flag. Crucially, it must not be touched anywhere else so we can assume all the
     * previous candidates are listed in candidates[] and do not have to go through all of splines[]. */
    for (i = 0; i < candidates->len; i++) {
        k = g_array_index(candidates, guint, i);
        g_array_index(splines, SampledSpline, k).is_candidate = FALSE;
    }
    g_array_set_size(candidates, 0);

    range_of_intersecting_grid_cells(ss->min.x, ss->max.x, xres, &jfrom, &jto);
    range_of_intersecting_grid_cells(ss->min.y, ss->max.y, yres, &ifrom, &ito);
    for (i = ifrom; i < ito; i++) {
        for (j = jfrom; j < jto; j++) {
            grid_cell = spline_grid[i*GRID_SIZE + j];
            for (k = 0; k < grid_cell->len; k++) {
                guint l = g_array_index(grid_cell, guint, k);
                SampledSpline *sscand = &g_array_index(splines, SampledSpline, l);

                /* Only add each spline once to candidates. */
                if (!sscand->is_candidate) {
                    g_array_append_val(candidates, l);
                    sscand->is_candidate = TRUE;
                }
            }
        }
    }
}

static gdouble
generate_spline(GwySpline *spline,
                gdouble sizereduction, gdouble scale, gdouble irreg,
                gint xres, gint yres,
                GwyRandGenSet *rngset)
{
    gdouble sizefactor = (0.5 + 1.5*gwy_rand_gen_set_double(rngset, RNG_SIZE))*sizereduction;
    gint npts = 4 + gwy_rand_gen_set_int(rngset, RNG_NPOINTS) % GWY_ROUND(sqrt(2.0*sizefactor/scale));
    gdouble phi0 = 2.0*G_PI*gwy_rand_gen_set_double(rngset, RNG_SIZE);
    gdouble phi_ell = 2.0*G_PI*gwy_rand_gen_set_double(rngset, RNG_SIZE);
    gdouble ellipticity = irreg*gwy_rand_gen_set_double(rngset, RNG_SIZE);
    GwyXY *pts = g_new(GwyXY, npts);
    GwyXY centre;
    gdouble r0;
    guint j;

    r0 = sizefactor/scale;
    centre.x = xres*(1.4*gwy_rand_gen_set_double(rngset, RNG_POS) - 0.2);
    centre.y = yres*(1.4*gwy_rand_gen_set_double(rngset, RNG_POS) - 0.2);
    for (j = 0; j < npts; j++) {
        gdouble phi = phi0 + 2.0*G_PI*j/npts + irreg*G_PI/npts*gwy_rand_gen_set_double(rngset, RNG_POINTS);
        gdouble r = r0*sqrt(1.0 + 0.8*irreg*(gwy_rand_gen_set_double(rngset, RNG_POINTS) - 0.5));
        r *= sqrt(1.0 + ellipticity*sin(phi - phi_ell));
        pts[j].x = centre.x + r*cos(phi) + r*0.8*irreg*(gwy_rand_gen_set_double(rngset, RNG_POS) - 0.5);
        pts[j].y = centre.y + r*sin(phi) + r*0.8*irreg*(gwy_rand_gen_set_double(rngset, RNG_POS) - 0.5);
    }
    gwy_spline_set_points(spline, pts, npts);
    g_free(pts);

    return r0;
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble irreg = gwy_params_get_double(params, PARAM_IRREGULARITY);
    gdouble overlap_allowed = gwy_params_get_double(params, PARAM_OVERLAP);
    gdouble minsize = gwy_params_get_double(params, PARAM_MIN_SIZE);
    gdouble maxsize = gwy_params_get_double(params, PARAM_MAX_SIZE);
    gdouble height_power = gwy_params_get_double(params, PARAM_HEIGHT_POWER);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble size_power = gwy_params_get_double(params, PARAM_SIZE_POWER);
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    GwyDataField *field = args->field, *result = args->result;
    guint xres, yres, i, j, k, nseg, expected_nobj, noverlap;
    GArray *splines = args->sampled_splines, *candidates = NULL;
    GArray **spline_grid = args->spline_grid;
    GwySynthUpdateType update;
    gint power10z;
    gdouble *d;
    gdouble r0, scale, s, sw;
    GwyRandGenSet *rngset;
    GTimer *timer;
    gboolean finished = FALSE;
    GwySpline *spline = NULL;
    Edge *vevents = NULL;
    gulong ntries = 0;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    gwy_data_field_clear(result);
    d = gwy_data_field_get_data(result);

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);

    /* Only generate objects if the array was cleared in param_changed(), indicating spline parameters have changed
     * (not just rendering parameters). */
    if (!splines->len) {
        if (!gwy_app_wait_set_message(_("Generating objects...")))
            goto end;

        scale = 2.0/maxsize;
        expected_nobj = 0;
        do {
            gdouble len_effective = expected_nobj * PREVIEW_SIZE*PREVIEW_SIZE/(gdouble)(xres*yres);
            gdouble sizereduction = 1.0/pow(1.0 + len_effective, size_power);
            r0 = sizereduction/scale;
            expected_nobj++;
        } while (r0 >= minsize);

        /* Generate flakes as non-intersecting splines with decreasing size. */
        spline = gwy_spline_new();
        gwy_spline_set_closed(spline, TRUE);

        candidates = g_array_new(FALSE, FALSE, sizeof(guint));

        noverlap = 0;
        do {
            gdouble len_effective = splines->len * PREVIEW_SIZE*PREVIEW_SIZE/(gdouble)(xres*yres);
            gdouble sizereduction = 1.0/pow(1.0 + len_effective, size_power);

            while (TRUE) {
                SampledSpline ss;

                r0 = ss.r0 = generate_spline(spline, sizereduction, scale, irreg, xres, yres, rngset);

                if (ntries++ % 100 == 0) {
                    /* We do not know if we really generate the same number of objects as estimated. So just hover
                     * around in ‘almost done’ progress until done. */
                    update = gwy_synth_update_progress(timer, 0.0, splines->len, MAX(expected_nobj, splines->len+1));
                    if (update == GWY_SYNTH_UPDATE_CANCELLED)
                        goto end;
                }

                precompute_sampled_spline(spline, &ss);
                if (!sampled_spline_is_selfintersecting(&ss)) {
                    find_intersection_candidates(&ss, splines, spline_grid, candidates, xres, yres);
                    for (j = 0; j < candidates->len; j++) {
                        k = g_array_index(candidates, guint, j);
                        if (sampled_splines_intersect(&ss, &g_array_index(splines, SampledSpline, k)))
                            break;
                    }
                    if (j == candidates->len || noverlap/(splines->len + 0.5) < overlap_allowed) {
                        if (j < candidates->len)
                            noverlap++;
                        ss.is_candidate = FALSE;
                        g_array_append_val(splines, ss);
                        add_spline_to_grid_cells(&ss, splines->len-1, spline_grid, xres, yres);
                        break;
                    }
                }
                free_sampled_spline(&ss);
            }
        } while (r0 >= minsize/2);  /* User probably thinks of size as diameter. */
    }

    /* Generate raw heights at an arbitrary scalre and transform them to get something like the requested scale. */
    s = sw = 0.0;
    for (i = 0; i < splines->len; i++) {
        SampledSpline *ss = &g_array_index(splines, SampledSpline, i);
        r0 = ss->r0;
        ss->height = pow(ss->r0, height_power) * exp(gwy_rand_gen_set_gaussian(rngset, RNG_HEIGHT, height_noise));
        s += ss->height * r0*r0;
        sw += r0*r0;
    }
    s /= sw;
    for (i = 0; i < splines->len; i++) {
        SampledSpline *ss = &g_array_index(splines, SampledSpline, i);
        ss->height *= height/s;
    }

    if (!gwy_app_wait_set_message(_("Rendering surface...")))
        goto end;

    /* Render image. */
    gwy_data_field_clear(result);
    vevents = gather_vertical_events(splines, &nseg);
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(xres,yres,d,splines,vevents,nseg,pcancelled) \
            private(i,j)
#endif
    {
        guint tno = gwy_omp_thread_num(), nthreads = gwy_omp_num_threads();
        GArray *hevents = g_array_new(FALSE, FALSE, sizeof(Edge));
        GHashTable *active_set = g_hash_table_new(g_direct_hash, g_direct_equal);
        gint nhevents, xbeforeedge, ybeforeedge = -1;
        gdouble x, y, z;

        for (i = tno; i < yres; i += nthreads) {
            y = i + 0.5;
            ybeforeedge = move_to_larger(vevents, 2*nseg, active_set, y, ybeforeedge);
            gather_image_row_events(active_set, splines, y, hevents);
            gwy_debug("row %u intersects %u segments", i, hevents->len);
            nhevents = hevents->len;
            xbeforeedge = -1;
            z = 0.0;
            for (j = 0; j < xres; j++) {
                x = j + 0.5;
                while (xbeforeedge < nhevents-1 && g_array_index(hevents, Edge, xbeforeedge+1).t <= x) {
                    const Edge *edge = &g_array_index(hevents, Edge, xbeforeedge+1);
                    guint objno = edge->key.id.objno, segno = edge->key.id.segno;
                    SampledSpline *ss = &g_array_index(splines, SampledSpline, objno);
                    const GwyXY *a = ss->points + segno, *b = ss->points + (segno + 1) % ss->n;

                    if (b->y < a->y)
                        z += ss->height;
                    else if (b->y > a->y)
                        z -= ss->height;
                    else if (b->x > a->x)
                        z += ss->height;
                    else
                        z -= ss->height;

                    xbeforeedge++;
                }
                d[i*xres + j] = z;
            }
            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction,
                                                  i/nthreads, 0, yres/nthreads+1, pcancelled))
                break;
        }
        g_array_free(hevents, TRUE);
        g_hash_table_destroy(active_set);
    }

    gwy_data_field_invalidate(result);
    gwy_data_field_multiply(result, height);
    if (field && do_initialise)
        gwy_data_field_sum_fields(result, result, field);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_timer_destroy(timer);
    if (candidates)
        g_array_free(candidates, TRUE);
    if (spline)
        gwy_spline_free(spline);
    g_free(vevents);
    gwy_rand_gen_set_free(rngset);

    return finished;
}

static void
free_sampled_spline(SampledSpline *ss)
{
    g_free(ss->points);
    g_free(ss->linemin);
    g_free(ss->linemax);
}

static void
clear_sampled_splines(ModuleArgs *args)
{
    guint i;

    for (i = 0; i < args->sampled_splines->len; i++)
        free_sampled_spline(&g_array_index(args->sampled_splines, SampledSpline, i));
    g_array_set_size(args->sampled_splines, 0);

    for (i = 0; i < GRID_SIZE*GRID_SIZE; i++)
        g_array_set_size(args->spline_grid[i], 0);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble rmin = gwy_params_get_double(params, PARAM_MIN_SIZE);
    gdouble rmax = gwy_params_get_double(params, PARAM_MAX_SIZE);

    gwy_params_set_double(params, PARAM_MIN_SIZE, fmin(rmin, rmax));
    gwy_params_set_double(params, PARAM_MAX_SIZE, fmax(rmin, rmax));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
