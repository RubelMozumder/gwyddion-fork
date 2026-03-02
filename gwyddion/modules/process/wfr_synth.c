/*
 *  $Id: wfr_synth.c 24450 2021-11-01 14:32:53Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-synth.h>
#include "preview.h"
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define OCCUPIED G_MAXSIZE

enum {
    PARAM_COVERAGE,
    PARAM_DIFFUSION,
    PARAM_HEIGHT,
    PARAM_SEED,
    PARAM_RANDOMIZE,
    PARAM_ANIMATED,
    PARAM_ACTIVE_PAGE,
    BUTTON_LIKE_CURRENT_IMAGE,

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
    GwyParamTable *table_dimensions;
    GwyParamTable *table_generator;
    GwyContainer *data;
    GwyDataField *template_;
} ModuleGUI;

typedef struct {
    gdouble v;
    gsize k;
} QueueItem;

typedef struct {
    QueueItem *array;
    guint len;
    guint alloc_size;
} PriorityQueue;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             wfr_synth           (GwyContainer *data,
                                             GwyRunType runtype);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *wait_window);
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
    N_("Generates images by simulating a propagating wetting front."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, wfr_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("wfr_synth",
                              (GwyProcessFunc)&wfr_synth,
                              N_("/S_ynthetic/_Wetting..."),
                              NULL,
                              RUN_MODES,
                              0,
                              N_("Generate image by propagating wetting front"));

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
    gwy_param_def_add_double(paramdef, PARAM_COVERAGE, "coverage", _("Co_verage"), 1e-4, 100.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_DIFFUSION, "diffusion", _("_Diffusion"), -6.0, 0.0, -2.5);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height scale"), 1e-4, 1000.0, 1.0);
    gwy_param_def_add_seed(paramdef, PARAM_SEED, "seed", NULL);
    gwy_param_def_add_randomize(paramdef, PARAM_RANDOMIZE, PARAM_SEED, "randomize", NULL, TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_ANIMATED, "animated", _("Progressive preview"), TRUE);
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);

    gwy_synth_define_dimensions_params(paramdef, PARAM_DIMS0);
    return paramdef;
}

static void
wfr_synth(GwyContainer *data, GwyRunType runtype)
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
    if (gwy_params_get_boolean(args.params, PARAM_ANIMATED))
        gwy_app_wait_preview_data_field(args.result, data, id);
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
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

    gui.dialog = gwy_dialog_new(_("Wetting Front"));
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

    gwy_param_table_append_header(table, -1, _("Simulation Parameters"));
    gwy_param_table_append_slider(table, PARAM_COVERAGE);
    gwy_param_table_slider_set_mapping(table, PARAM_COVERAGE, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_DIFFUSION);
    gwy_param_table_set_unitstr(table, PARAM_DIFFUSION, "log<sub>10</sub>");
    gwy_param_table_slider_set_mapping(table, PARAM_DIFFUSION, GWY_SCALE_MAPPING_LINEAR);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LOG);
    if (gui->template_) {
        gwy_param_table_append_button(table, BUTTON_LIKE_CURRENT_IMAGE, -1, GWY_RESPONSE_SYNTH_INIT_Z,
                                      _("_Like Current Image"));
    }

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
priority_queue_init(PriorityQueue *queue, guint prealloc)
{
    gwy_clear(queue, 1);
    queue->alloc_size = prealloc;
    queue->array = g_new(QueueItem, prealloc);
    queue->len = 0;
}

static void
priority_queue_free(PriorityQueue *queue)
{
    GWY_FREE(queue->array);
}

static inline void
priority_queue_swap_in_sync(PriorityQueue *queue, GHashTable *voxels, gsize i1, gsize i2)
{
    QueueItem *a = queue->array;
    gsize k1 = a[i1].k, k2 = a[i2].k;

    GWY_SWAP(QueueItem, a[i1], a[i2]);
    g_hash_table_insert(voxels, GSIZE_TO_POINTER(k1), GSIZE_TO_POINTER(i2));
    g_hash_table_insert(voxels, GSIZE_TO_POINTER(k2), GSIZE_TO_POINTER(i1));
}

static inline void
priority_queue_heapify_up(PriorityQueue *queue, GHashTable *voxels, gsize k)
{
    QueueItem *a = queue->array;

    while (k && a[k/2].v < a[k].v) {
        priority_queue_swap_in_sync(queue, voxels, k/2, k);
        k /= 2;
    }
}

static inline void
priority_queue_insert(PriorityQueue *queue, GHashTable *voxels, const QueueItem *item)
{
    gsize k;

    if (G_UNLIKELY(queue->len == queue->alloc_size)) {
        queue->alloc_size = MAX(2*queue->alloc_size, 16);
        queue->array = g_renew(QueueItem, queue->array, queue->alloc_size);
    }

    k = queue->len++;
    queue->array[k] = *item;
    g_hash_table_insert(voxels, GSIZE_TO_POINTER(item->k), GSIZE_TO_POINTER(k));
    priority_queue_heapify_up(queue, voxels, k);
}

static inline void
priority_queue_pop(PriorityQueue *queue, GHashTable *voxels, QueueItem *item)
{
    QueueItem *a;
    gsize k, l;

    g_return_if_fail(queue->len);
    a = queue->array;
    *item = a[0];
    /* Do not do this.  We will reinsert the OCCUPIED marker immediately after.
    g_hash_table_remove(voxels, GSIZE_TO_POINTER(item->k));
    */

    if (!(l = --queue->len))
        return;

    /* Heapify down. */
    k = 0;
    a[k] = a[l];
    g_hash_table_insert(voxels, GSIZE_TO_POINTER(a[k].k), GSIZE_TO_POINTER(k));
    while (2*k < l) {
        if (a[2*k].v > a[2*k+1].v) {
            if (a[k].v >= a[2*k].v)
                return;
            priority_queue_swap_in_sync(queue, voxels, k, 2*k);
            k = 2*k;
        }
        else {
            if (a[k].v >= a[2*k+1].v)
                return;
            priority_queue_swap_in_sync(queue, voxels, k, 2*k+1);
            k = 2*k+1;
        }
    }
}

static inline void
maybe_enqueue(PriorityQueue *queue, GHashTable *voxels,
              gsize xres, gsize yres, gsize l, gsize i, gsize j,
              GRand *rng)
{
    gsize kk, k = (l*yres + i)*xres + j;
    QueueItem *a = queue->array;
    QueueItem item;

    kk = GPOINTER_TO_SIZE(g_hash_table_lookup(voxels, GSIZE_TO_POINTER(k)));
    if (kk == OCCUPIED)
        return;

    if (kk) {
        a[kk].v += g_rand_double(rng);
        priority_queue_heapify_up(queue, voxels, kk);
        return;
    }

    item.k = k;
    item.v = g_rand_double(rng);
    priority_queue_insert(queue, voxels, &item);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    gboolean do_initialise = gwy_params_get_boolean(params, PARAM_DIMS0 + GWY_DIMS_PARAM_INITIALIZE);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gboolean animated = gwy_params_get_boolean(params, PARAM_ANIMATED);
    gdouble coverage = gwy_params_get_double(params, PARAM_COVERAGE);
    gdouble diffusion = pow10(gwy_params_get_double(params, PARAM_DIFFUSION));
    GwySynthUpdateType update;
    GwyDataField *field = args->field, *result = args->result;
    gsize xres, yres, n, k, i, j, l, niters, iter;
    gint power10z;
    PriorityQueue queue;
    GHashTable *voxels;
    gdouble *d;
    const gdouble *f;
    QueueItem item = { 0, 0 };  /* Placate GCC. */
    gdouble preview_time = (animated ? 1.25 : 0.0);
    GRand *rng;
    GTimer *timer;
    gboolean finished = FALSE;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    gwy_params_get_unit(params, PARAM_DIMS0 + GWY_DIMS_PARAM_ZUNIT, &power10z);
    height *= pow10(power10z);

    rng = g_rand_new();
    g_rand_set_seed(rng, gwy_params_get_int(params, PARAM_SEED));

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    gwy_data_field_clear(result);
    d = gwy_data_field_get_data(result);
    n = xres*yres;
    niters = GWY_ROUND(coverage*n);

    voxels = g_hash_table_new(g_direct_hash, g_direct_equal);
    priority_queue_init(&queue, n);
    if (field && do_initialise) {
        f = gwy_data_field_get_data_const(field);
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++) {
                item.k = i*xres + j;
                item.v = f[item.k]/height;
                priority_queue_insert(&queue, voxels, &item);
            }
        }
    }
    else {
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++)
                maybe_enqueue(&queue, voxels, xres, yres, 0, i, j, rng);
        }
    }

    timer = g_timer_new();
    gwy_synth_update_progress(NULL, 0, 0, 0);
    if (!gwy_app_wait_set_message(_("Running computation...")))
        goto end;

    for (iter = 0; iter < niters; iter++) {
        priority_queue_pop(&queue, voxels, &item);
        g_hash_table_insert(voxels, GSIZE_TO_POINTER(item.k), GSIZE_TO_POINTER(OCCUPIED));
        l = item.k/n;
        k = item.k % n;
        i = k/xres;
        j = k % xres;

        d[k] = l;
        //d[k]++;

        if (l)
            maybe_enqueue(&queue, voxels, xres, yres, l-1, i, j, rng);
        maybe_enqueue(&queue, voxels, xres, yres, l+1, i, j, rng);
        maybe_enqueue(&queue, voxels, xres, yres, l, (i + yres-1) % yres, j, rng);
        maybe_enqueue(&queue, voxels, xres, yres, l, (i + 1) % yres, j, rng);
        maybe_enqueue(&queue, voxels, xres, yres, l, i, (j + xres-1) % xres, rng);
        maybe_enqueue(&queue, voxels, xres, yres, l, i, (j + 1) % xres, rng);

        for (i = 0; i < 3; i++) {
            k = g_rand_int_range(rng, 0, queue.len);
            queue.array[k].v += diffusion*g_rand_double(rng);
            priority_queue_heapify_up(&queue, voxels, k);
        }

        if (iter % 100000 == 0) {
            update = gwy_synth_update_progress(timer, preview_time, iter, niters);
            if (update == GWY_SYNTH_UPDATE_CANCELLED)
                return FALSE;
            if (update == GWY_SYNTH_UPDATE_DO_PREVIEW) {
                gwy_data_field_invalidate(result);
                gwy_data_field_data_changed(result);
            }
        }
    }

    gwy_data_field_invalidate(result);
    gwy_data_field_multiply(result, height);
    finished = TRUE;

end:
    gwy_app_wait_finish();
    g_timer_destroy(timer);
    g_rand_free(rng);
    priority_queue_free(&queue);
    g_hash_table_destroy(voxels);

    return finished;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
