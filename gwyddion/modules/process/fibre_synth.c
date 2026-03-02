/*
 *  $Id: fibre_synth.c 23813 2021-06-08 12:40:37Z yeti-dn $
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
#include <libprocess/arithmetic.h>
#include <libprocess/spline.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils-synth.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

/* Large spline oversampling is OK because straight segments converge very quickly so we only get substantial
 * oversampling in sharp turns – and we want it there. */
#define OVERSAMPLE 12.0

/* Always consume this many random numbers from RNG_DEFORM when creating a spline for generator stability. */
enum {
    FIBRE_MAX_POINTS = 80,
};

typedef enum {
    RNG_WIDTH,
    RNG_HEIGHT,
    RNG_POSITION,
    RNG_ANGLE,
    RNG_HTRUNC,
    RNG_DEFORM,
    RNG_SEGVAR,
    RNG_NRNGS
} FibreSynthRng;

typedef enum {
    FIBRE_SYNTH_CIRCLE    = 0,
    FIBRE_SYNTH_TRIANGLE  = 1,
    FIBRE_SYNTH_SQUARE    = 2,
    FIBRE_SYNTH_PARABOLA  = 3,
    FIBRE_SYNTH_QUADRATIC = 4,
} FibreSynthType;

enum {
    PARAM_TYPE,
    PARAM_WIDTH,
    PARAM_WIDTH_NOISE,
    PARAM_WIDTH_VAR,
    PARAM_HEIGHT,
    PARAM_HEIGHT_NOISE,
    PARAM_HEIGHT_VAR,
    PARAM_HEIGHT_BOUND,
    PARAM_HTRUNC,
    PARAM_HTRUNC_NOISE,
    PARAM_COVERAGE,
    PARAM_ANGLE,
    PARAM_ANGLE_NOISE,
    PARAM_DEFORM_DENSITY,
    PARAM_LATDEFORM,
    PARAM_LATDEFORM_NOISE,
    PARAM_LENDEFORM,
    PARAM_LENDEFORM_NOISE,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_UPDATE,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,
    INFO_COVERAGE_OBJECTS,

    PARAM_DIMS0
};

typedef struct {
    guint size;
    guint len;
    gint *data;
} IntList;

typedef struct {
    gdouble u;
    gdouble wfactor;
    gdouble hfactor;
} FibreSegmentVar;

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

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             fibre_synth          (GwyContainer *data,
                                              GwyRunType runtype);
static gboolean         execute              (ModuleArgs *args,
                                              GtkWindow *wait_window,
                                              gboolean show_progress_bar);
static GwyDialogOutcome run_gui              (ModuleArgs *args,
                                              GwyContainer *data,
                                              gint id);
static GtkWidget*       dimensions_tab_new   (ModuleGUI *gui);
static GtkWidget*       generator_tab_new    (ModuleGUI *gui);
static GtkWidget*       placement_tab_new    (ModuleGUI *gui);
static void             dialog_response      (ModuleGUI *gui,
                                              gint response);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static void             preview              (gpointer user_data);
static void             fibre_synth_add_one  (GwyDataField *surface,
                                              GwyDataField *fibre,
                                              GwyDataField *ucoord,
                                              IntList *usedpts,
                                              GwySpline *spline,
                                              GArray *segvar,
                                              ModuleArgs *args,
                                              GwyRandGenSet *rngset);
static glong            calculate_n_fibres   (ModuleArgs *args,
                                              guint xres,
                                              guint yres);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates surfaces composed from randomly placed fibers."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, fibre_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("fibre_synth",
                              (GwyProcessFunc)&fibre_synth,
                              N_("/S_ynthetic/_Deposition/_Fibers..."),
                              GWY_STOCK_SYNTHETIC_FIBRES,
                              RUN_MODES,
                              0,
                              N_("Generate surface of randomly placed fibers"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum shapes[] = {
        { N_("Semi-circle"),      FIBRE_SYNTH_CIRCLE,    },
        { N_("Triangle"),         FIBRE_SYNTH_TRIANGLE,  },
        { N_("Rectangle"),        FIBRE_SYNTH_SQUARE,    },
        { N_("Parabola"),         FIBRE_SYNTH_PARABOLA,  },
        { N_("Quadratic spline"), FIBRE_SYNTH_QUADRATIC, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Shape"),
                              shapes, G_N_ELEMENTS(shapes), FIBRE_SYNTH_CIRCLE);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH, "width", _("_Width"), 1.0, 1000.0, 5.0);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH_NOISE, "width_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH_VAR, "width_var", _("Along fiber"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_NOISE, "height_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT_VAR, "height_var", _("Along fiber"), 0.0, 1.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_HEIGHT_BOUND, "height_bound", _("Scales _with width"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_HTRUNC, "htrunc", _("_Truncate"), 0.0, 1.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_HTRUNC_NOISE, "htrunc_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 1e-4, 20.0, 0.5);
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("Orien_tation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ANGLE_NOISE, "angle_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_DEFORM_DENSITY, "deform_density", _("Densi_ty"),
                             0.5, FIBRE_MAX_POINTS - 1.0, 5.0);
    gwy_param_def_add_double(paramdef, PARAM_LATDEFORM, "latdeform", _("_Lateral"), 0.0, 1.0, 0.1);
    gwy_param_def_add_double(paramdef, PARAM_LATDEFORM_NOISE, "latdeform_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_LENDEFORM, "lendeform", _("Le_ngthwise"), 0.0, 1.0, 0.05);
    gwy_param_def_add_double(paramdef, PARAM_LENDEFORM_NOISE, "lendeform_noise", _("Spread"), 0.0, 1.0, 0.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);
    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
fibre_synth(GwyContainer *data, GwyRunType runtype)
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

    gui.dialog = gwy_dialog_new(_("Random Fibers"));
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
    gwy_param_table_append_slider(table, PARAM_WIDTH);
    gwy_param_table_slider_add_alt(table, PARAM_WIDTH);
    gwy_param_table_slider_set_mapping(table, PARAM_WIDTH, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_WIDTH_NOISE);
    gwy_param_table_append_slider(table, PARAM_WIDTH_VAR);

    gwy_param_table_append_header(table, -1, _("Height"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }
    gwy_param_table_append_checkbox(table, PARAM_HEIGHT_BOUND);
    gwy_param_table_append_slider(table, PARAM_HEIGHT_NOISE);
    gwy_param_table_append_slider(table, PARAM_HEIGHT_VAR);
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

    gwy_param_table_append_header(table, -1, _("Orientation"));
    gwy_param_table_append_slider(table, PARAM_ANGLE);
    gwy_param_table_append_slider(table, PARAM_ANGLE_NOISE);

    gwy_param_table_append_header(table, -1, _("Deformation"));
    gwy_param_table_append_slider(table, PARAM_DEFORM_DENSITY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_LATDEFORM);
    gwy_param_table_append_slider(table, PARAM_LATDEFORM_NOISE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_LENDEFORM);
    gwy_param_table_append_slider(table, PARAM_LENDEFORM_NOISE);

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
        static const gint xyids[] = { PARAM_WIDTH };

        gwy_synth_update_lateral_alts(table, xyids, G_N_ELEMENTS(xyids));
    }
    if (id < 0
        || id == PARAM_WIDTH || id == PARAM_WIDTH_NOISE || id == PARAM_COVERAGE) {
        gint xres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_DIMS0 + GWY_DIMS_PARAM_YRES);
        glong nobj = calculate_n_fibres(gui->args, xres, yres);
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
    gboolean instant_updates = gwy_params_get_boolean(gui->args->params, PARAM_UPDATE);

    if (execute(gui->args, GTK_WINDOW(gui->dialog), !instant_updates))
        gwy_data_field_data_changed(gui->args->result);
}

static inline IntList*
int_list_new(guint prealloc)
{
    IntList *list = g_slice_new0(IntList);
    prealloc = MAX(prealloc, 16);
    list->size = prealloc;
    list->data = g_new(gint, list->size);
    return list;
}

static inline void
int_list_add(IntList *list, gint i)
{
    if (G_UNLIKELY(list->len == list->size)) {
        list->size = MAX(2*list->size, 16);
        list->data = g_renew(gint, list->data, list->size);
    }

    list->data[list->len] = i;
    list->len++;
}

static inline void
int_list_free(IntList *list)
{
    g_free(list->data);
    g_slice_free(IntList, list);
}

static inline GwyXY
vecdiff(const GwyXY *a, const GwyXY *b)
{
    GwyXY r;

    r.x = a->x - b->x;
    r.y = a->y - b->y;

    return r;
}

static inline GwyXY
veclincomb(const GwyXY *a, gdouble qa, const GwyXY *b, gdouble qb)
{
    GwyXY r;

    r.x = qa*a->x + qb*b->x;
    r.y = qa*a->y + qb*b->y;

    return r;
}

static inline gdouble
dotprod(const GwyXY *a, const GwyXY *b)
{
    return a->x*b->x + a->y*b->y;
}

static inline gdouble
vecnorm2(const GwyXY *a)
{
    return a->x*a->x + a->y*a->y;
}

static inline gdouble
vecprodz(const GwyXY *a, const GwyXY *b)
{
    return a->x*b->y - a->y*b->x;
}

static void
order_trapezoid_vertically(const GwyXY *p, const GwyXY *q,
                           const GwyXY *pp, const GwyXY *qq,
                           const GwyXY **top, const GwyXY **m1,
                           const GwyXY **m2, const GwyXY **bottom)
{
    const GwyXY *pts[4];
    guint i, j;

    pts[0] = p;
    pts[1] = q;
    pts[2] = pp;
    pts[3] = qq;
    for (i = 0; i < 3; i++) {
        for (j = i; j < 4; j++) {
            if (pts[i]->y > pts[j]->y || (pts[i]->y == pts[j]->y && pts[i]->x == pts[j]->x))
                GWY_SWAP(const GwyXY*, pts[i], pts[j]);
        }
    }

    *top = pts[0];
    *m1 = pts[1];
    *m2 = pts[2];
    *bottom = pts[3];
}

static void
fill_vsegment(const GwyXY *lfrom, const GwyXY *lto,
              const GwyXY *rfrom, const GwyXY *rto,
              gdouble *fdata, gdouble *udata, gint xres, gint yres,
              gint ifrom, gint ito,
              const GwyXY *r, const GwyXY *rp, const GwyXY *rq, const GwyXY *d,
              gdouble wp, gdouble wq, gdouble lp, gdouble lq,
              gboolean positive,
              IntList *usedpts)
{
    gdouble denoml, denomr;
    gint i, j, jfrom, jto, jleftlim, jrightlim;
    gdouble tl, tr, u, v, s, w, dnorm;
    gdouble *frow, *urow;
    GwyXY pt, rr;

    dnorm = vecnorm2(d);
    denoml = MAX(lto->y - lfrom->y, 1e-9);
    denomr = MAX(rto->y - rfrom->y, 1e-9);

    jleftlim = (gint)floor(fmin(lfrom->x, lto->x) - 1.0);
    jleftlim = MAX(jleftlim, 0);
    jrightlim = (gint)ceil(fmax(rfrom->x, rto->x) + 1.0);
    jrightlim = MIN(jrightlim, xres-1);

    for (i = ifrom; i <= ito; i++) {
        tl = (i - lfrom->y)/denoml;
        jfrom = (gint)floor(tl*lto->x + (1.0 - tl)*lfrom->x);
        jfrom = MAX(jfrom, jleftlim);

        tr = (i - rfrom->y)/denomr;
        jto = (gint)ceil(tr*rto->x + (1.0 - tr)*rfrom->x);
        jto = MIN(jto, jrightlim);

        pt.y = i - r->y;
        frow = fdata + i*xres;
        urow = udata + i*xres;
        g_assert(i >= 0);
        g_assert(i < yres);
        for (j = jfrom; j <= jto; j++) {
            pt.x = j - r->x;
            /* u is the approximate coordinate along the segment
             * v is the approximate distance from the centre
             * both are from [0, 1] inside the trapezoid.
             * Exact coordinates can be introduced and calculated but it seems to require solving some ugly quadratic
             * equations and is not necessary for rendering the fibre.  If we want fibre height varying continuously
             * along its length we also need to remember u coordinates somewhere. */
            u = dotprod(&pt, d)/dnorm + 0.5;
            u = CLAMP(u, 0.0, 1.0);
            w = wp*(1.0 - u) + wq*u;
            rr = veclincomb(rp, 1.0 - u, rq, u);
            /* This is one Newton iteration of w*|r'| from initial estimate |r'| ≈ w, which should be always good.  It
             * avoid slow sqrt() and as a bonus, it is a sum of two positive terms so it has to behave nicely. */
            s = 0.5*(w*w + vecnorm2(&rr));
            v = dotprod(&pt, &rr)/s;
            g_assert(j >= 0);
            g_assert(j < xres);
            if (v >= 0.0 && v <= 1.0 && v < fabs(frow[j])) {
                /* Only add the pixel when first encountering it. */
                if (frow[j] == G_MAXDOUBLE)
                    int_list_add(usedpts, i*xres + j);
                frow[j] = positive ? v : -v;
                urow[j] = lp*(1.0 - u) + lq*u;
            }
        }
    }
}

/* p-q is the fibre ‘spine’, pp-qq is the outer boundary. */
static void
fill_trapezoid(gdouble *fdata, gdouble *udata, gint xres, gint yres,
               const GwyXY *p, const GwyXY *q,
               const GwyXY *pp, const GwyXY *qq,
               gdouble wp, gdouble wq, gdouble lp, gdouble lq,
               gboolean positive, IntList *usedpts)
{
    const GwyXY *top, *mid1, *mid2, *bottom;
    const GwyXY *lfrom, *lto, *rfrom, *rto;
    GwyXY d, dd, diag, s, r, rp, rq;
    gboolean mid1_is_right, mid2_is_right;
    gint ifrom, ito;

    /* If we are totally outside, abort.  This does not detect trapezoids hugging the rectangle boundary line, but
     * there are only a small fraction of them. */
    if (fmin(fmin(p->x, q->x), fmin(pp->x, qq->x)) > xres+1.0)
        return;
    if (fmin(fmin(p->y, q->y), fmin(pp->y, qq->y)) > yres+1.0)
        return;
    if (fmax(fmax(p->x, q->x), fmax(pp->x, qq->x)) < -1.0)
        return;
    if (fmax(fmax(p->y, q->y), fmax(pp->y, qq->y)) < -1.0)
        return;

    /* If the points on the outer boundary are in reverse order (too large width compared to local curvature), just
     * invert the order to get some kind of untwisted trapezoid.  The result still does not have to be convex, but the
     * filling does not fail if the outer boundary is weird because we do not use the pp-qq vector. */
    d = vecdiff(q, p);
    dd = vecdiff(qq, pp);
    if (dotprod(&d, &dd) < 0.0) {
        GWY_SWAP(const GwyXY*, pp, qq);
    }

    r = veclincomb(p, 0.5, q, 0.5);
    rp = vecdiff(pp, p);
    rq = vecdiff(qq, q);

    order_trapezoid_vertically(p, q, pp, qq, &top, &mid1, &mid2, &bottom);
    diag = vecdiff(bottom, top);
    s = vecdiff(mid1, top);
    mid1_is_right = (vecprodz(&s, &diag) >= 0.0);
    s = vecdiff(mid2, top);
    mid2_is_right = (vecprodz(&s, &diag) > 0.0);

    /* The top triangle.  May be skipped if the top line is horizontal. */
    if (mid1->y > top->y + 1e-9) {
        ifrom = (gint)floor(top->y);
        ifrom = MAX(ifrom, 0);
        ito = (gint)ceil(mid1->y);
        ito = MIN(ito, yres-1);

        lfrom = rfrom = top;
        rto = mid1_is_right ? mid1 : (mid2_is_right ? mid2 : bottom);
        lto = mid1_is_right ? (mid2_is_right ? bottom : mid2) : mid1;
        fill_vsegment(lfrom, lto, rfrom, rto,
                      fdata, udata, xres, yres, ifrom, ito,
                      &r, &rp, &rq, &d, wp, wq, lp, lq, positive, usedpts);
    }

    /* The middle part.  May be skipped if the mid1 and mid2 are on the same horizontal line.*/
    if (mid2->y > mid1->y + 1e-9) {
        ifrom = (gint)floor(mid1->y);
        ifrom = MAX(ifrom, 0);
        ito = (gint)ceil(mid2->y);
        ito = MIN(ito, yres-1);

        lfrom = mid1_is_right ? top : mid1;
        rfrom = mid1_is_right ? mid1 : top;
        lto = mid2_is_right ? bottom : mid2;
        rto = mid2_is_right ? mid2 : bottom;
        fill_vsegment(lfrom, lto, rfrom, rto,
                      fdata, udata, xres, yres, ifrom, ito,
                      &r, &rp, &rq, &d, wp, wq, lp, lq, positive, usedpts);
    }

    /* The bottom triangle.  May be skipped if the bottom line is horizontal. */
    if (bottom->y > mid2->y + 1e-9) {
        ifrom = (gint)floor(mid2->y);
        ifrom = MAX(ifrom, 0);
        ito = (gint)ceil(bottom->y);
        ito = MIN(ito, yres-1);

        lfrom = mid2_is_right ? (mid1_is_right ? bottom : mid1) : mid2;
        rfrom = mid2_is_right ? mid2 : (mid1_is_right ? mid1 : top);
        lto = rto = bottom;
        fill_vsegment(lfrom, lto, rfrom, rto,
                      fdata, udata, xres, yres, ifrom, ito,
                      &r, &rp, &rq, &d, wp, wq, lp, lq, positive, usedpts);
    }
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window, gboolean show_progress_bar)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    GwyDataField *fibre, *ucoord, *extfield, *field = args->field, *result = args->result;
    GwyRandGenSet *rngset;
    GwySpline *spline;
    GArray *segvar;
    IntList *usedpts;
    guint i, nfib, xres, yres, extw;
    gboolean ok = FALSE;

    if (show_progress_bar)
        gwy_app_wait_start(wait_window, _("Initializing..."));

    rngset = gwy_rand_gen_set_new(RNG_NRNGS);
    gwy_rand_gen_set_init(rngset, gwy_params_get_int(params, PARAM_SEED));

    if (field && do_initialise)
        gwy_data_field_copy(field, result, FALSE);
    else
        gwy_data_field_clear(result);

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    extw = MIN(xres, yres)/8 + GWY_ROUND(2*width) + 16;
    extfield = gwy_data_field_extend(result, extw, extw, extw, extw, GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);

    usedpts = int_list_new(0);
    segvar = g_array_new(FALSE, FALSE, sizeof(FibreSegmentVar));
    fibre = gwy_data_field_new_alike(extfield, TRUE);
    ucoord = gwy_data_field_new_alike(extfield, TRUE);
    spline = gwy_spline_new();
    gwy_data_field_fill(fibre, G_MAXDOUBLE);

    if (show_progress_bar && !gwy_app_wait_set_message(_("Generating fibers...")))
        goto finish;

    nfib = calculate_n_fibres(args, xres, yres);
    for (i = 0; i < nfib; i++) {
        fibre_synth_add_one(extfield, fibre, ucoord, usedpts, spline, segvar, args, rngset);
        if (show_progress_bar && !gwy_app_wait_set_fraction((i + 1.0)/nfib))
            goto finish;
    }
    gwy_data_field_area_copy(extfield, result, extw, extw, xres, yres, 0, 0);

    ok = TRUE;

finish:
    if (show_progress_bar)
        gwy_app_wait_finish();

    g_array_free(segvar, TRUE);
    gwy_spline_free(spline);
    gwy_rand_gen_set_free(rngset);
    int_list_free(usedpts);
    g_object_unref(extfield);
    g_object_unref(fibre);
    g_object_unref(ucoord);

    return ok;
}

static gdouble
generate_deformed(GwyRandGenSet *rngset, gdouble deformation, gdouble noise)
{
    gdouble delta;

    delta = gwy_rand_gen_set_gaussian(rngset, RNG_DEFORM, noise);
    delta = deformation*exp(delta);
    return gwy_rand_gen_set_gaussian(rngset, RNG_DEFORM, delta);
}

static void
calculate_segment_var(const GwyXY *xy, guint n, GArray *segvar,
                      GwyRandGenSet *rngset, gdouble ptstep,
                      gdouble width_var, gdouble height_var, gboolean height_bound)
{
    FibreSegmentVar *segvardata;
    gdouble s, l;
    guint i;

    g_array_set_size(segvar, n);
    segvardata = &g_array_index(segvar, FibreSegmentVar, 0);
    segvardata[0].u = 0.0;
    segvardata[0].wfactor = 0.0;
    segvardata[0].hfactor = 0.0;
    for (i = 1; i < n; i++) {
        GwyXY d = vecdiff(xy + i, xy + i-1);

        l = sqrt(vecnorm2(&d))/OVERSAMPLE;
        segvardata[i].u = segvardata[i-1].u + l;

        /* Mix a new random number with the previous one for short segments. */
        l = fmin(l/ptstep, 1.0);
        l *= l;
        s = gwy_rand_gen_set_gaussian(rngset, RNG_SEGVAR, width_var);
        segvardata[i].wfactor = (1.0 - l)*segvardata[i-1].wfactor + l*s;
        s = gwy_rand_gen_set_gaussian(rngset, RNG_SEGVAR, height_var);
        segvardata[i].hfactor = (1.0 - l)*segvardata[i-1].hfactor + l*s;
    }

    for (i = 0; i < n; i++) {
        segvardata[i].wfactor = exp(2.0*segvardata[i].wfactor);
        segvardata[i].hfactor = exp(2.0*segvardata[i].hfactor);
        if (height_bound)
            segvardata[i].hfactor *= segvardata[i].wfactor;
    }
}

static void
generate_fibre_spline(gint xres, gint yres,
                      GwySpline *spline, GArray *segvar, ModuleArgs *args, GwyRandGenSet *rngset)
{
    static GwyXY points[2*FIBRE_MAX_POINTS + 1];

    GwyParams *params = args->params;
    gdouble angle = gwy_params_get_double(params, PARAM_ANGLE);
    gdouble angle_noise = gwy_params_get_double(params, PARAM_ANGLE_NOISE);
    gdouble deform_density = gwy_params_get_double(params, PARAM_DEFORM_DENSITY);
    gdouble latdeform = gwy_params_get_double(params, PARAM_LATDEFORM);
    gdouble latdeform_noise = gwy_params_get_double(params, PARAM_LATDEFORM_NOISE);
    gdouble lendeform = gwy_params_get_double(params, PARAM_LENDEFORM);
    gdouble lendeform_noise = gwy_params_get_double(params, PARAM_LENDEFORM_NOISE);
    gdouble width_var = gwy_params_get_double(params, PARAM_WIDTH_VAR);
    gdouble height_var = gwy_params_get_double(params, PARAM_HEIGHT_VAR);
    gboolean height_bound = gwy_params_get_boolean(params, PARAM_HEIGHT_BOUND);
    gdouble xoff, yoff, x, y, s, ptstep, ca, sa;
    const GwyXY *xy;
    guint i, npts;

    angle = angle;
    if (angle_noise)
        angle += gwy_rand_gen_set_gaussian(rngset, RNG_ANGLE, 2*angle_noise);
    ca = cos(angle);
    sa = sin(angle);

    s = hypot(xres, yres);
    x = s*(gwy_rand_gen_set_double(rngset, RNG_POSITION) - 0.5);
    y = s*(gwy_rand_gen_set_double(rngset, RNG_POSITION) - 0.5);
    xoff = xres/2 + ca*x + sa*y;
    yoff = yres/2 - sa*x + ca*y;
    ptstep = s/deform_density;

    /* Generate the full number of points for image stability when parameters
     * change. */
    points[FIBRE_MAX_POINTS].x = xoff;
    points[FIBRE_MAX_POINTS].y = yoff;
    for (i = 1; i < FIBRE_MAX_POINTS; i++) {
        x = ptstep*(i + generate_deformed(rngset, lendeform, lendeform_noise));
        y = ptstep*generate_deformed(rngset, latdeform, latdeform_noise);
        points[FIBRE_MAX_POINTS + i].x = ca*x + sa*y + xoff;
        points[FIBRE_MAX_POINTS + i].y = -sa*x + ca*y + yoff;

        x = -ptstep*(i + generate_deformed(rngset, lendeform, lendeform_noise));
        y = ptstep*generate_deformed(rngset, latdeform, latdeform_noise);
        points[FIBRE_MAX_POINTS - i].x = ca*x + sa*y + xoff;
        points[FIBRE_MAX_POINTS - i].y = -sa*x + ca*y + yoff;
    }

    /* Generate the last point always undisturbed so it cannot lie inside. */
    x = ptstep*FIBRE_MAX_POINTS;
    points[2*FIBRE_MAX_POINTS].x = ca*x + xoff;
    points[2*FIBRE_MAX_POINTS].y = -sa*x + yoff;

    x = -ptstep*FIBRE_MAX_POINTS;
    points[0].x = ca*x + xoff;
    points[0].y = -sa*x + yoff;

    for (i = 0; i < G_N_ELEMENTS(points); i++) {
        points[i].x *= OVERSAMPLE;
        points[i].y *= OVERSAMPLE;
    }

    gwy_spline_set_points(spline, points, 2*FIBRE_MAX_POINTS + 1);

    /* XXX: This depends on spline not freeing xy[] before it makes a copy. */
    xy = gwy_spline_sample_naturally(spline, &npts);
    gwy_spline_set_points(spline, xy, npts);

    calculate_segment_var(xy, npts, segvar, rngset, ptstep, width_var, height_var, height_bound);
}

static void
fibre_synth_add_one(GwyDataField *surface, GwyDataField *fibre, GwyDataField *ucoord,
                    IntList *usedpts, GwySpline *spline, GArray *segvar,
                    ModuleArgs *args, GwyRandGenSet *rngset)
{
    GwyParams *params = args->params;
    FibreSynthType type = gwy_params_get_enum(params, PARAM_TYPE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble height_noise = gwy_params_get_double(params, PARAM_HEIGHT_NOISE);
    gdouble height_var = gwy_params_get_double(params, PARAM_HEIGHT_VAR);
    gdouble width = 0.5*gwy_params_get_double(params, PARAM_WIDTH);
    gdouble width_noise = gwy_params_get_double(params, PARAM_WIDTH_NOISE);
    gdouble width_var = gwy_params_get_double(params, PARAM_WIDTH_VAR);
    gdouble htrunc = gwy_params_get_double(params, PARAM_HTRUNC);
    gdouble htrunc_noise = gwy_params_get_double(params, PARAM_HTRUNC_NOISE);
    gboolean height_bound = gwy_params_get_boolean(params, PARAM_HEIGHT_BOUND);
    FibreSegmentVar *segvardata;
    gdouble m;
    guint k, npts, nused;
    const GwyXY *xy, *txy;
    gint xres, yres, power10z;
    gdouble *data, *fdata, *udata;
    gboolean needs_heightvar;
    const gint *useddata;

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    xres = gwy_data_field_get_xres(fibre);
    yres = gwy_data_field_get_yres(fibre);
    fdata = gwy_data_field_get_data(fibre);
    udata = gwy_data_field_get_data(ucoord);
    data = gwy_data_field_get_data(surface);

    needs_heightvar = (height_var > 0.0 || (width_var > 0.0 && height_bound));

    if (width_noise)
        width *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_WIDTH, width_noise));

    if (height_bound)
        height *= width/width;
    if (height_noise)
        height *= exp(gwy_rand_gen_set_gaussian(rngset, RNG_HEIGHT, height_noise));

    /* Use a specific distribution for htrunc. */
    if (htrunc_noise) {
        gdouble q = exp(gwy_rand_gen_set_gaussian(rngset, RNG_HTRUNC, htrunc_noise));
        htrunc = q/(q + 1.0/htrunc - 1.0);
    }
    else
        htrunc = htrunc;

    generate_fibre_spline(xres, yres, spline, segvar, args, rngset);
    npts = gwy_spline_get_npoints(spline);
    xy = gwy_spline_get_points(spline);
    txy = gwy_spline_get_tangents(spline);
    segvardata = &g_array_index(segvar, FibreSegmentVar, 0);

    for (k = 0; k+1 < npts; k++) {
        GwyXY p, q, pp, qq;
        gdouble wp = width*segvardata[k].wfactor;
        gdouble wq = width*segvardata[k+1].wfactor;

        p.x = xy[k].x/OVERSAMPLE;
        p.y = xy[k].y/OVERSAMPLE;
        q.x = xy[k+1].x/OVERSAMPLE;
        q.y = xy[k+1].y/OVERSAMPLE;

        pp.x = p.x - wp*txy[k].y;
        pp.y = p.y + wp*txy[k].x;
        qq.x = q.x - wq*txy[k+1].y;
        qq.y = q.y + wq*txy[k+1].x;
        fill_trapezoid(fdata, udata, xres, yres, &p, &q, &pp, &qq, wp, wq, k, k+1, TRUE, usedpts);

        pp.x = p.x + wp*txy[k].y;
        pp.y = p.y - wp*txy[k].x;
        qq.x = q.x + wq*txy[k+1].y;
        qq.y = q.y - wq*txy[k+1].x;
        fill_trapezoid(fdata, udata, xres, yres, &p, &q, &pp, &qq, wp, wq, k, k+1, FALSE, usedpts);
    }

    m = G_MAXDOUBLE;
    nused = usedpts->len;
    useddata = usedpts->data;
    for (k = 0; k < nused; k++)
        m = fmin(m, data[useddata[k]]);

    for (k = 0; k < nused; k++) {
        gint i = useddata[k];
        gdouble z = fdata[i];

        if (type == FIBRE_SYNTH_CIRCLE)
            z = sqrt(1.0 - fmin(z*z, 1.0));
        else if (type == FIBRE_SYNTH_TRIANGLE)
            z = 1.0 - fabs(z);
        else if (type == FIBRE_SYNTH_SQUARE)
            z = 1.0;
        else if (type == FIBRE_SYNTH_PARABOLA)
            z = 1.0 - z*z;
        else if (type == FIBRE_SYNTH_QUADRATIC) {
            z = fabs(z);
            z = (z <= 1.0/3.0) ? 0.75*(1.0 - 3*z*z) : 1.125*(1.0 - z)*(1.0 - z);
        }
        else {
            g_assert_not_reached();
        }

        if (z > htrunc)
            z = htrunc;
        z *= height;

        if (needs_heightvar) {
            gdouble u = udata[i];
            gint j = floor(u);

            u -= j;
            if (G_UNLIKELY(j >= npts-1)) {
                j = npts-2;
                u = 1.0;
            }
            else if (G_UNLIKELY(j < 0)) {
                j = 0;
                u = 0.0;
            }
            z *= (1.0 - u)*segvardata[j].hfactor + u*segvardata[j+1].hfactor;
        }

        data[i] = fmax(data[i], m + z);
        fdata[i] = G_MAXDOUBLE;
    }
    usedpts->len = 0;
}

static glong
calculate_n_fibres(ModuleArgs *args, guint xres, guint yres)
{
    GwyParams *params = args->params;
    /* The distribution of area differs from the distribution of widths. */
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gdouble width_noise = gwy_params_get_double(params, PARAM_WIDTH_NOISE);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gdouble noise_corr = exp(width_noise*width_noise);
    /* FIXME: Should correct for deformation which increases the length, possibly for orientation distribution
     * (orthogonal are shorter but more likely completely inside, so the dependence is unclear). */
    gdouble length = hypot(xres, yres);
    gdouble mean_fibre_area = 0.125*width * length * noise_corr;
    gdouble must_cover = coverage*xres*yres;
    return (glong)ceil(must_cover/mean_fibre_area);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
