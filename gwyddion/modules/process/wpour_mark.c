/*
 *  $Id: wpour_mark.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include "string.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define FWHM2SIGMA (1.0/(2.0*sqrt(2*G_LN2)))

typedef enum {
    IMAGE_PREVIEW_ORIGINAL,
    IMAGE_PREVIEW_PREPROC,
} ImagePreviewType;

typedef enum {
    MASK_PREVIEW_NONE,
    MASK_PREVIEW_MARKED,
} MaskPreviewType;

enum {
    PARAM_INVERTED,
    PARAM_UPDATE,
    PARAM_IMAGE_PREVIEW,
    PARAM_MASK_PREVIEW,
    PARAM_BLUR_FWHM,
    PARAM_BARRIER_LEVEL,
    PARAM_PREFILL_LEVEL,
    PARAM_PREFILL_HEIGHT,
    PARAM_GRADIENT_CONTRIB,
    PARAM_CURVATURE_CONTRIB,
    PARAM_COMBINE_TYPE,
    PARAM_COMBINE,
    PARAM_MASK_COLOR,
};

typedef struct {
    guint size;
    guint len;
    gint *data;
} IntList;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *preproc;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             wpour_mark              (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             add_slope_contribs      (GwyDataField *workspace,
                                                 GwyDataField *field,
                                                 gdouble gradient_contrib,
                                                 gdouble curvature_contrib);
static void             normal_vector_difference(GwyDataField *result,
                                                 GwyDataField *xder,
                                                 GwyDataField *yder);
static gdouble          create_barriers         (GwyDataField *field,
                                                 gdouble level);
static void             prefill_minima          (GwyDataField *field,
                                                 GwyDataField *workspace,
                                                 IntList *inqueue,
                                                 IntList *outqueue,
                                                 gdouble depth,
                                                 gdouble height);
static void             replace_value           (GwyDataField *field,
                                                 gdouble from,
                                                 gdouble to);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Segments image using watershed with pre- and postprocessing."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY2(module_info, wpour_mark)

static gboolean
module_register(void)
{
    gwy_process_func_register("wpour_mark",
                              (GwyProcessFunc)&wpour_mark,
                              N_("/_Grains/_Mark by Segmentation..."),
                              GWY_STOCK_GRAINS_WATER,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Segment using watershed "));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum image_previews[] = {
        { N_("Original image"),     IMAGE_PREVIEW_ORIGINAL, },
        { N_("Preprocessed image"), IMAGE_PREVIEW_PREPROC,  },
    };
    static const GwyEnum mask_previews[] = {
        { N_("No mask"),       MASK_PREVIEW_NONE,          },
        { N_("Marked"),        MASK_PREVIEW_MARKED,        },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("_Invert height"), FALSE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_IMAGE_PREVIEW, "image_preview", _("_Image preview"),
                              image_previews, G_N_ELEMENTS(image_previews), IMAGE_PREVIEW_ORIGINAL);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MASK_PREVIEW, "mask_preview", _("_Mask preview"),
                              mask_previews, G_N_ELEMENTS(mask_previews), MASK_PREVIEW_MARKED);
    gwy_param_def_add_double(paramdef, PARAM_BLUR_FWHM, "blur_fwhm", _("Gaussian _smoothing"), 0.0, 25.0, 0.0);
    gwy_param_def_add_percentage(paramdef, PARAM_BARRIER_LEVEL, "barrier_level", _("_Barrier level"), 1.0);
    gwy_param_def_add_percentage(paramdef, PARAM_PREFILL_LEVEL, "prefill_level", _("Prefill _level"), 0.0);
    gwy_param_def_add_percentage(paramdef, PARAM_PREFILL_HEIGHT, "prefill_height", _("Pre_fill from minima"), 0.0);
    gwy_param_def_add_percentage(paramdef, PARAM_GRADIENT_CONTRIB, "gradient_contrib", _("Add _gradient"), 0.0);
    gwy_param_def_add_percentage(paramdef, PARAM_CURVATURE_CONTRIB, "curvature_contrib", _("Add _curvature"), 0.0);
    gwy_param_def_add_enum(paramdef, PARAM_COMBINE_TYPE, "combine_type", NULL, GWY_TYPE_MERGE_TYPE, GWY_MERGE_UNION);
    gwy_param_def_add_boolean(paramdef, PARAM_COMBINE, "combine", NULL, FALSE);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
wpour_mark(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(args.field && mquark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.preproc = gwy_data_field_new_alike(args.field, TRUE);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.params);
    g_object_unref(args.preproc);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    GwyDialogOutcome outcome;

    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Segment by Watershed"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Preprocessing"));
    gwy_param_table_append_slider(table, PARAM_BLUR_FWHM);
    gwy_param_table_slider_add_alt(table, PARAM_BLUR_FWHM);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_BLUR_FWHM, args->field);
    gwy_param_table_append_slider(table, PARAM_GRADIENT_CONTRIB);
    gwy_param_table_append_slider(table, PARAM_CURVATURE_CONTRIB);
    gwy_param_table_append_slider(table, PARAM_BARRIER_LEVEL);
    gwy_param_table_append_slider(table, PARAM_PREFILL_LEVEL);
    gwy_param_table_append_slider(table, PARAM_PREFILL_HEIGHT);

    /* TODO: Some postprocessing was planned, but the section is currently empty... */

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);
    if (args->mask) {
        gwy_param_table_append_radio_buttons(table, PARAM_COMBINE_TYPE, NULL);
        gwy_param_table_add_enabler(table, PARAM_COMBINE, PARAM_COMBINE_TYPE);
    }
    gwy_param_table_append_combo(table, PARAM_IMAGE_PREVIEW);
    gwy_param_table_append_combo(table, PARAM_MASK_PREVIEW);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_param_table_set_no_reset(table, PARAM_UPDATE, TRUE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_IMAGE_PREVIEW) {
        ImagePreviewType image_preview = gwy_params_get_enum(params, PARAM_IMAGE_PREVIEW);
        GwyDataField *field = (image_preview == IMAGE_PREVIEW_ORIGINAL ? args->field : args->preproc);
        gwy_container_set_object_by_name(gui->data, "/0/data", field);
    }
    if (id < 0 || id == PARAM_MASK_PREVIEW) {
        MaskPreviewType mask_preview = gwy_params_get_enum(params, PARAM_MASK_PREVIEW);
        if (mask_preview == MASK_PREVIEW_MARKED)
            gwy_container_set_object_by_name(gui->data, "/0/mask", args->result);
        else
            gwy_container_remove_by_name(gui->data, "/0/mask");
    }

    if (id != PARAM_MASK_COLOR && id != PARAM_UPDATE && id != PARAM_IMAGE_PREVIEW && id != PARAM_MASK_PREVIEW)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->preproc);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
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
int_list_add_unique(IntList **plist, gint i)
{
    IntList *list;
    guint j;

    if (!*plist)
        *plist = int_list_new(0);

    list = *plist;
    for (j = 0; j < list->len; j++) {
        if (list->data[j] == i)
            return;
    }
    int_list_add(list, i);
}

static void
int_list_free(IntList *list)
{
    g_free(list->data);
    g_slice_free(IntList, list);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *result = args->result, *field = args->field, *preproc = args->preproc, *mask = args->mask;
    GwyParams *params = args->params;
    gboolean combine = gwy_params_get_boolean(params, PARAM_COMBINE);
    GwyMergeType combine_type = gwy_params_get_enum(params, PARAM_COMBINE_TYPE);
    gboolean inverted = gwy_params_get_boolean(params, PARAM_INVERTED);
    gdouble blur_fwhm = gwy_params_get_double(params, PARAM_BLUR_FWHM);
    gdouble gradient_contrib = gwy_params_get_double(params, PARAM_GRADIENT_CONTRIB);
    gdouble curvature_contrib = gwy_params_get_double(params, PARAM_CURVATURE_CONTRIB);
    gdouble barrier_level = gwy_params_get_double(params, PARAM_BARRIER_LEVEL);
    gdouble prefill_level = gwy_params_get_double(params, PARAM_PREFILL_LEVEL);
    gdouble prefill_height = gwy_params_get_double(params, PARAM_PREFILL_HEIGHT);
    IntList *inqueue = int_list_new(0);
    IntList *outqueue = int_list_new(0);
    guint xres, yres;
    gdouble barmax;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    gwy_data_field_copy(field, preproc, FALSE);
    if (inverted)
        gwy_data_field_invert(preproc, FALSE, FALSE, TRUE);

    /* Use result as a scratch buffer. */
    gwy_data_field_add(preproc, -gwy_data_field_get_max(preproc));
    if (blur_fwhm)
        gwy_data_field_area_filter_gaussian(preproc, blur_fwhm*FWHM2SIGMA, 0, 0, xres, yres);
    add_slope_contribs(result, preproc, gradient_contrib, curvature_contrib);
    barmax = create_barriers(preproc, barrier_level);
    prefill_minima(preproc, result, inqueue, outqueue, prefill_level, prefill_height);

    replace_value(preproc, barmax, HUGE_VAL);
    gwy_data_field_waterpour(preproc, result, NULL);
    /* Lower the barriers again to avoid HUGE_VAL in the preview. */
    replace_value(preproc, HUGE_VAL, barmax);

    int_list_free(outqueue);
    int_list_free(inqueue);

    if (mask && combine) {
        if (combine_type == GWY_MERGE_UNION)
            gwy_data_field_grains_add(result, mask);
        else if (combine_type == GWY_MERGE_INTERSECTION)
            gwy_data_field_grains_intersect(result, mask);
    }

#if 0
    /* FIXME: neither grains nor gnetwork used for anything at this moment. */
    grains = g_new0(gint, xres*yres);
    ngrains = gwy_data_field_number_grains(result, grains);
    gnetwork = analyse_grain_network(preproc, grains);
    g_free(grains);
    g_hash_table_destroy(gnetwork);
#endif
}

static void
add_slope_contribs(GwyDataField *workspace, GwyDataField *field,
                   gdouble gradient_contrib, gdouble curvature_contrib)
{
    GwyDataField *xder, *yder;
    gdouble r, rg, rc;

    if (!gradient_contrib && !curvature_contrib)
        return;

    r = gwy_data_field_get_rms(field);
    if (!r)
        return;

    xder = gwy_data_field_new_alike(field, FALSE);
    yder = gwy_data_field_new_alike(field, FALSE);

    gwy_data_field_filter_slope(field, xder, yder);
    gwy_data_field_multiply(field, 1.0 - MAX(gradient_contrib, curvature_contrib));

    /* We need this for both operations. */
    gwy_data_field_hypot_of_fields(workspace, xder, yder);
    rg = gwy_data_field_get_rms(workspace);

    if (gradient_contrib) {
        gwy_data_field_multiply(workspace, gradient_contrib * r/rg);
        gwy_data_field_sum_fields(field, field, workspace);
    }

    if (curvature_contrib) {
        gwy_data_field_multiply(xder, 1.0/rg);
        gwy_data_field_multiply(yder, 1.0/rg);
        normal_vector_difference(workspace, xder, yder);
        rc = gwy_data_field_get_rms(workspace);
        if (rc) {
            gwy_data_field_multiply(workspace, curvature_contrib * r/rc);
            gwy_data_field_sum_fields(field, field, workspace);
        }
    }

    g_object_unref(yder);
    g_object_unref(xder);

    gwy_data_field_invalidate(field);
    gwy_data_field_invalidate(workspace);
}

static void
normal_vector(gdouble bx, gdouble by, gdouble *nx, gdouble *ny, gdouble *nz)
{
    gdouble b = sqrt(1.0 + bx*bx + by*by);

    *nx = -bx/b;
    *ny = -by/b;
    *nz = 1.0/b;
}

static void
normal_vector_difference(GwyDataField *result, GwyDataField *xder, GwyDataField *yder)
{
    const gdouble *bx, *by;
    guint xres, yres, i, j;
    gdouble *d;

    gwy_data_field_clear(result);
    xres = result->xres;
    yres = result->yres;
    d = result->data;
    bx = xder->data;
    by = yder->data;

    for (i = 0; i < yres; i++) {
        gdouble *row = d + i*xres, *next = row + xres;
        const gdouble *bxrow = bx + i*xres, *nextbx = bxrow + xres;
        const gdouble *byrow = by + i*xres, *nextby = byrow + xres;

        for (j = 0; j < xres; j++) {
            gdouble nx, ny, nz;
            gdouble nxr, nyr, nzr;
            gdouble nxd, nyd, nzd;
            gdouble ch, cv;

            normal_vector(bxrow[j], byrow[j], &nx, &ny, &nz);
            if (j < xres-1) {
                normal_vector(bxrow[j+1], byrow[j+1], &nxr, &nyr, &nzr);
                ch = nxr - nx;
                row[j] += ch;
                row[j+1] += ch;
            }

            if (i < yres-1) {
                normal_vector(nextbx[j], nextby[j], &nxd, &nyd, &nzd);
                cv = nyd - ny;
                row[j] += cv;
                next[j] += cv;
            }
        }
    }

    gwy_data_field_invalidate(result);
}

static gdouble
create_barriers(GwyDataField *field, gdouble level)
{
    guint k, xres = field->xres, yres = field->yres;
    gdouble min, max, barmax;

    gwy_data_field_get_min_max(field, &min, &max);
    barmax = 1.01*max;
    if (min == max)
        return barmax;

    if (level < 1.0) {
        gdouble threshold = level*(max - min) + min;
        gdouble *d = field->data;

        barmax = max;
        for (k = 0; k < xres*yres; k++) {
            if (d[k] >= threshold)
                d[k] = barmax;
        }
        gwy_data_field_invalidate(field);
    }

    return barmax;
}

static void
prefill_minima(GwyDataField *field, GwyDataField *workspace,
               IntList *inqueue, IntList *outqueue,
               gdouble depth, gdouble height)
{
    guint i, j, k, m, xres = field->xres, yres = field->yres;
    gdouble min, max;

    gwy_data_field_get_min_max(field, &min, &max);
    if (min == max)
        return;

    /* Simple absolute prefilling corresponding to plain mark-by-threshold. */
    if (depth > 0.0) {
        gdouble depththreshold = depth*(max - min) + min;
        gdouble *d = field->data;

        for (k = 0; k < xres*yres; k++) {
            if (d[k] < depththreshold)
                d[k] = depththreshold;
        }
        gwy_data_field_invalidate(field);
    }

    /* Simple height prefilling which floods all pixels with heights only
     * little above the minimum. */
    if (height > 0.0) {
        gdouble heightthreshold = height*(max - min);
        gdouble *d = field->data, *w = workspace->data;

        gwy_data_field_mark_extrema(field, workspace, FALSE);

        inqueue->len = 0;
        for (k = 0; k < xres*yres; k++) {
            if (w[k])
                int_list_add(inqueue, k);
        }

        while (inqueue->len) {
            outqueue->len = 0;
            for (m = 0; m < inqueue->len; m++) {
                gdouble z, zth;

                k = inqueue->data[m];
                i = k/xres;
                j = k % xres;
                z = d[k];
                zth = z + heightthreshold*fabs(z)/(max - min);

                if (i > 0 && d[k-xres] > z && d[k-xres] < zth) {
                    d[k-xres] = z;
                    int_list_add(outqueue, k-xres);
                }
                if (j > 0 && d[k-1] > z && d[k-1] < zth) {
                    d[k-1] = z;
                    int_list_add(outqueue, k-1);
                }
                if (j < xres-1 && d[k+1] > z && d[k+1] < zth) {
                    d[k+1] = z;
                    int_list_add(outqueue, k+1);
                }
                if (i < yres-1 && d[k+xres] > z && d[k+xres] < zth) {
                    d[k+xres] = z;
                    int_list_add(outqueue, k+xres);
                }
            }

            GWY_SWAP(IntList*, inqueue, outqueue);
        }

        gwy_data_field_invalidate(field);
    }
}

static void
replace_value(GwyDataField *field, gdouble from, gdouble to)
{
    guint k, xres = field->xres, yres = field->yres;
    gdouble *d = field->data;

    for (k = 0; k < xres*yres; k++) {
        if (d[k] == from)
            d[k] = to;
    }
    gwy_data_field_invalidate(field);
}

#if 0
typedef struct {
    guint a;
    guint b;
} UIntPair;

typedef struct {
    gdouble min_barrier;
    gdouble min_bsum;
} GrainNeighbour;

static guint
uint_pair_hash(gconstpointer p)
{
    const UIntPair *pair = (const UIntPair*)p;
    return pair->a*1103515245 + pair->b;
}

static gboolean
uint_pair_equal(gconstpointer pa, gconstpointer pb)
{
    const UIntPair *paira = (const UIntPair*)pa;
    const UIntPair *pairb = (const UIntPair*)pb;
    return paira->a == pairb->a && paira->b == pairb->b;
}

static void
uint_pair_free(gpointer p)
{
    g_slice_free(UIntPair, p);
}

static void
grain_neighbour_free(gpointer p)
{
    g_slice_free(GrainNeighbour, p);
}

static inline gboolean
is_merge_pixel(const gint *gc, gint *pg1, gint *pg2)
{
    gint gnn[4], g1, g2;
    guint n, i;

    n = 0;
    for (i = 0; i < 4; i++) {
        if (gc[i])
            gnn[n++] = gc[i];
    }

    if (n < 2)
        return FALSE;

    g1 = gnn[0];
    g2 = gnn[1];
    if (n >= 3) {
        if (g1 == g2) {
            g2 = gnn[2];
            if (n == 4 && g1 == g2)
                g2 = gnn[3];
        }
        else {
            if (gnn[2] != g1 && gnn[2] != g2)
                return FALSE;
            if (n == 4 && gnn[3] != g1 && gnn[3] != g2)
                return FALSE;
        }
    }

    if (g1 == g2)
        return FALSE;

    *pg1 = g1;
    *pg2 = g2;
    return TRUE;
}

static GHashTable*
analyse_grain_network(GwyDataField *field, const gint *grains)
{
    GHashTable *gnetwork;
    guint xres = field->xres, yres = field->yres;
    const gdouble *data = field->data;
    guint i, j, k;

    gnetwork = g_hash_table_new_full(uint_pair_hash, uint_pair_equal, uint_pair_free, grain_neighbour_free);

    /* Scan possible merge-pixles.  A pixel touching more than 2 different grains is not a possible merge-pixel if
     * don't do simultaneous multi-grain merging.  We never merge through a dialonal; mergeable grains always touch
     * via a 4-connected pixel. */
    k = 0;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++, k++) {
            gint g1, g2, gc[4], n = 0, m;
            gdouble min_barrier, min_bsum, z, zc[4];
            GrainNeighbour *neighbour;
            UIntPair pair;

            if (grains[k])
                continue;

            /* Figure out how many different grains touch this non-grain pixel.
             */
            gc[0] = i ? grains[k - xres] : 0;
            gc[1] = j ? grains[k-1] : 0;
            gc[2] = j < xres-1 ? grains[k+1] : 0;
            gc[3] = i < yres-1 ? grains[k + xres] : 0;
            if (!is_merge_pixel(gc, &g1, &g2))
                continue;

            /* Now we know we have a non-grain pixel that can connect exactly
             * two grains together.  So we will be definitely adding it.
             * Find the barrier. */
            min_barrier = min_bsum = G_MAXDOUBLE;
            z = data[k];
            zc[0] = i ? fabs(z - data[k - xres]) : G_MAXDOUBLE;
            zc[1] = j ? fabs(z - data[k-1]) : G_MAXDOUBLE;
            zc[2] = j < xres-1 ? fabs(z - data[k+1]) : G_MAXDOUBLE;
            zc[3] = i < yres-1 ? fabs(z - data[k + xres]) : G_MAXDOUBLE;
            for (n = 1; n < 4; n++) {
                if (!gc[n])
                    continue;

                min_barrier = MIN(min_barrier, zc[n]);
                for (m = 0; m < n; m++) {
                    if (!gc[m] || gc[m] == gc[n])
                        continue;

                    min_bsum = MIN(min_bsum, zc[m] + zc[n]);
                }
            }

            pair.a = MIN(g1, g2);
            pair.b = MAX(g1, g2);
            neighbour = g_hash_table_lookup(gnetwork, &pair);
            if (neighbour) {
                neighbour->min_barrier = MIN(neighbour->min_barrier, min_barrier);
                neighbour->min_bsum = MIN(neighbour->min_bsum, min_bsum);
            }
            else {
                UIntPair *ppair = g_slice_dup(UIntPair, &pair);
                neighbour = g_slice_new(GrainNeighbour);
                neighbour->min_barrier = min_barrier;
                neighbour->min_bsum = min_bsum;
                g_hash_table_insert(gnetwork, ppair, neighbour);
            }
        }
    }

    /* Now we have the symmetrical irreflexive neighbour relation encoded
     * in gnetwork together with barriers between the neighbour grains. */
    return gnetwork;
}
#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
