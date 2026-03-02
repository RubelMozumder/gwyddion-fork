/*
 *  $Id: volume_linelevel.c 26354 2024-05-21 08:22:32Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correlation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

/* Lower symmetric part indexing; i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]


enum {
    PREVIEW_SIZE = 360,
    MAX_DEGREE = 5,
};

enum {
    PARAM_METHOD,
    PARAM_TRIM_FRACTION,
    PARAM_MAX_DEGREE,
    PARAM_DIRECTION,
    PARAM_Z,
    PARAM_ORDER,
    PARAM_UPDATE
};

typedef enum {
    LINE_MATCH_POLY         = 0,
    LINE_MATCH_MEDIAN       = 1,
    LINE_MATCH_MEDIAN_DIFF  = 2,
    LINE_MATCH_MODUS        = 3,
    LINE_MATCH_MATCH        = 4,
    LINE_MATCH_TRIMMED_MEAN = 5,
    LINE_MATCH_TMEAN_DIFF   = 6,
    LINE_MATCH_FACET_TILT   = 7,
} LineMatchMethod;


typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyBrick *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
} ModuleGUI;


static gboolean              module_register          (void);
static GwyParamDef*          define_module_params     (void);
static void                  line_level               (GwyContainer *data,
                                                       GwyRunType runtype);
static void                  execute                  (ModuleArgs *args);
static GwyDialogOutcome      run_gui                  (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void                  param_changed            (ModuleGUI *gui,
                                                       gint id);
static void                  dialog_response          (GwyDialog *dialog,
                                                       gint response,
                                                       ModuleGUI *gui);
static void                  preview                  (gpointer user_data);
static void                  update_image             (ModuleGUI *gui,
                                                       gint z);
static void                  linematch_do_poly        (GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *means,
                                                       GwyMaskingType masking,
                                                       gint degree);
static void                  linematch_do_trimmed_mean(GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *shifts,
                                                       GwyMaskingType masking,
                                                       gdouble trim_fraction);
static void                  linematch_do_trimmed_diff(GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *shifts,
                                                       GwyMaskingType masking,
                                                       gdouble trim_fraction);
static void                  linematch_do_modus       (GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *shifts,
                                                       GwyMaskingType masking);
static void                  linematch_do_match       (GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *shifts,
                                                       GwyMaskingType masking);
static void                  linematch_do_facet_tilt  (GwyDataField *field,
                                                       GwyDataField *mask,
                                                       GwyDataLine *shifts,
                                                       GwyMaskingType masking);
static void                  zero_level_row_shifts    (GwyDataLine *shifts);

static const GwyEnum methods[] = {
    { N_("linematch|Polynomial"),        LINE_MATCH_POLY,         },
    { N_("Median"),                      LINE_MATCH_MEDIAN,       },
    { N_("Median of differences"),       LINE_MATCH_MEDIAN_DIFF,  },
    { N_("Modus"),                       LINE_MATCH_MODUS,        },
    { N_("linematch|Matching"),          LINE_MATCH_MATCH,        },
    { N_("Trimmed mean"),                LINE_MATCH_TRIMMED_MEAN, },
    { N_("Trimmed mean of differences"), LINE_MATCH_TMEAN_DIFF,   },
    { N_("Facet-level tilt"),            LINE_MATCH_FACET_TILT,   },
};


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performe line leveling for all the volume data levels."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_linelevel)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_linelevel",
                             (GwyVolumeFunc)&line_level,
                             N_("/_Correct Data/XY _Align Rows..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Performs line leveling of all levels"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
//    static const GwyEnum output_types[] = {
//        { N_("_Extract drift"), OUTPUT_DRIFT,  },
//        { N_("_Crop data"),     OUTPUT_CROP,   },
//    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Method"),
                              methods, G_N_ELEMENTS(methods), LINE_MATCH_MEDIAN);
    gwy_param_def_add_int(paramdef, PARAM_MAX_DEGREE, "max_degree", _("_Polynomial degree"), 0, MAX_DEGREE, 1);
    gwy_param_def_add_double(paramdef, PARAM_TRIM_FRACTION, "trim_fraction", _("_Trim fraction"), 0.0, 0.5, 0.05);
    gwy_param_def_add_enum(paramdef, PARAM_DIRECTION, "direction", NULL, GWY_TYPE_ORIENTATION,
                           GWY_ORIENTATION_HORIZONTAL);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_int(paramdef, PARAM_Z, "z", "Preview level", 0, G_MAXINT, 0);
    return paramdef;
}

static void
line_level(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_brick(args.result, NULL, data, TRUE);

    gwy_app_set_brick_title(data, newid, _("Line leveled"));
    gwy_app_sync_volume_items(data, data, oldid, newid, FALSE,
                              GWY_DATA_ITEM_GRADIENT,
                              0);

    gwy_app_volume_log_add_volume(data, -1, newid);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    GwyBrick *brick = args->brick;
    GwyDataField *field = gwy_data_field_new(gwy_brick_get_xres(brick),
                                             gwy_brick_get_yres(brick),
                                             gwy_brick_get_xreal(brick),
                                             gwy_brick_get_yreal(brick),
                                             TRUE);
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    args->result = gwy_brick_duplicate(brick);

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Line level"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_Z);
    gwy_param_table_slider_restrict_range(table, PARAM_Z, 0, gwy_brick_get_zres(brick)-1);

    gwy_param_table_append_radio_header(table, PARAM_METHOD);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MEDIAN);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MEDIAN_DIFF);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MODUS);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_MATCH);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_FACET_TILT);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_POLY);
    gwy_param_table_append_slider(table, PARAM_MAX_DEGREE);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_TRIMMED_MEAN);
    gwy_param_table_append_radio_item(table, PARAM_METHOD, LINE_MATCH_TMEAN_DIFF);
    gwy_param_table_append_slider(table, PARAM_TRIM_FRACTION);
    gwy_param_table_slider_set_steps(table, PARAM_TRIM_FRACTION, 0.01, 0.1);
    gwy_param_table_slider_set_factor(table, PARAM_TRIM_FRACTION, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_TRIM_FRACTION, "%");
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}


static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        gwy_brick_copy(gui->args->brick, gui->args->result, FALSE);

    preview(gui);
}

static void
update_image(ModuleGUI *gui, gint z)
{
    GwyDataField *dfield;
    GwyBrick *brick = gui->args->result;
    dfield = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));

    gwy_brick_extract_xy_plane(brick, dfield, CLAMP(z, 0, brick->zres-1));

    gwy_data_field_data_changed(dfield);
}


static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gint z = gwy_params_get_int(gui->args->params, PARAM_Z);

    execute(gui->args);
    update_image(gui, z);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint k;
    GwyBrick *original = args->brick;
    GwyBrick *brick = args->result;
    GwyDataField *dfield, *myfield;
    GwyDataLine *shifts;
    LineMatchMethod method = gwy_params_get_enum(params, PARAM_METHOD);
    gdouble trim_fraction = gwy_params_get_double(params, PARAM_TRIM_FRACTION);
    gint max_degree = gwy_params_get_int(params, PARAM_MAX_DEGREE);
    GwyOrientation direction = gwy_params_get_enum(params, PARAM_DIRECTION);

    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint zres = gwy_brick_get_zres(brick);

    dfield = gwy_data_field_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);
    shifts = gwy_data_line_new(gwy_data_field_get_yres(dfield), gwy_data_field_get_yreal(dfield), FALSE);

    for (k = 0; k < zres; k++) {
        gwy_brick_extract_xy_plane(original, dfield, k);

        /* Transpose the fields if necessary. */
        myfield = dfield;
        if (direction == GWY_ORIENTATION_VERTICAL) {
            myfield = gwy_data_field_new_alike(dfield, FALSE);
            gwy_data_field_flip_xy(dfield, myfield, FALSE);
        }

        /* Perform the correction. */
        if (method == LINE_MATCH_POLY) {
            if (max_degree == 0)
                linematch_do_trimmed_mean(myfield, NULL, shifts, GWY_MASK_IGNORE, 0.0);
            else
                linematch_do_poly(myfield, NULL, shifts, GWY_MASK_IGNORE, max_degree);
        }
        else if (method == LINE_MATCH_MEDIAN)
            linematch_do_trimmed_mean(myfield, NULL, shifts, GWY_MASK_IGNORE, 0.5);
        else if (method == LINE_MATCH_MEDIAN_DIFF)
            linematch_do_trimmed_diff(myfield, NULL, shifts, GWY_MASK_IGNORE, 0.5);
        else if (method == LINE_MATCH_MODUS)
            linematch_do_modus(myfield, NULL, shifts, GWY_MASK_IGNORE);
        else if (method == LINE_MATCH_MATCH)
            linematch_do_match(myfield, NULL, shifts, GWY_MASK_IGNORE);
        else if (method == LINE_MATCH_FACET_TILT)
            linematch_do_facet_tilt(myfield, NULL, shifts, GWY_MASK_IGNORE);
        else if (method == LINE_MATCH_TRIMMED_MEAN)
            linematch_do_trimmed_mean(myfield, NULL, shifts, GWY_MASK_IGNORE, trim_fraction);
        else if (method == LINE_MATCH_TMEAN_DIFF)
            linematch_do_trimmed_diff(myfield, NULL, shifts, GWY_MASK_IGNORE, trim_fraction);
        else {
            g_assert_not_reached();
        }

        /* Transpose back if necessary. */
        if (direction == GWY_ORIENTATION_VERTICAL) {
            gwy_data_field_flip_xy(myfield, dfield, FALSE);
            g_object_unref(myfield);
        }

        gwy_brick_set_xy_plane(brick, dfield, k);
    }

    g_object_unref(dfield);
    g_object_unref(shifts);
}

static void
linematch_do_poly(GwyDataField *field, GwyDataField *mask, GwyDataLine *means,
                  GwyMaskingType masking, gint degree)
{
    gint xres, yres;
    gdouble xc, avg;
    const gdouble *m;
    gdouble *d;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    xc = 0.5*(xres - 1);
    avg = gwy_data_field_get_avg(field);
    d = gwy_data_field_get_data(field);

    m = mask ? gwy_data_field_get_data_const(mask) : NULL;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,xres,yres,masking,avg,degree,xc,means)
#endif
    {
        gdouble *xpowers = g_new(gdouble, 2*degree+1);
        gdouble *zxpowers = g_new(gdouble, degree+1);
        gdouble *matrix = g_new(gdouble, (degree+1)*(degree+2)/2);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j, k;

        for (i = ifrom; i < ito; i++) {
            const gdouble *mrow = m ? m + i*xres : NULL;
            gdouble *drow = d + i*xres;

            gwy_clear(xpowers, 2*degree+1);
            gwy_clear(zxpowers, degree+1);

            for (j = 0; j < xres; j++) {
                gdouble p = 1.0, x = j - xc;

                if ((masking == GWY_MASK_INCLUDE && mrow[j] <= 0.0)
                    || (masking == GWY_MASK_EXCLUDE && mrow[j] >= 1.0))
                    continue;

                for (k = 0; k <= degree; k++) {
                    xpowers[k] += p;
                    zxpowers[k] += p*drow[j];
                    p *= x;
                }
                for (k = degree+1; k <= 2*degree; k++) {
                    xpowers[k] += p;
                    p *= x;
                }
            }

            /* Solve polynomial coefficients. */
            if (xpowers[0] > degree) {
                for (j = 0; j <= degree; j++) {
                    for (k = 0; k <= j; k++)
                        SLi(matrix, j, k) = xpowers[j + k];
                }
                gwy_math_choleski_decompose(degree+1, matrix);
                gwy_math_choleski_solve(degree+1, matrix, zxpowers);
            }
            else
                gwy_clear(zxpowers, degree+1);

            /* Subtract. */
            zxpowers[0] -= avg;
            gwy_data_line_set_val(means, i, zxpowers[0]);
            for (j = 0; j < xres; j++) {
                gdouble p = 1.0, x = j - xc, z = 0.0;

                for (k = 0; k <= degree; k++) {
                    z += p*zxpowers[k];
                    p *= x;
                }

                drow[j] -= z;
            }
        }

        g_free(matrix);
        g_free(zxpowers);
        g_free(xpowers);
    }
}

static void
linematch_do_trimmed_mean(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                          GwyMaskingType masking, gdouble trimfrac)
{
    GwyDataLine *myshifts;

    myshifts = gwy_data_field_find_row_shifts_trimmed_mean(field, mask, masking, trimfrac, 0);
    gwy_data_field_subtract_row_shifts(field, myshifts);
    gwy_data_line_assign(shifts, myshifts);
    g_object_unref(myshifts);
}
static void
linematch_do_trimmed_diff(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                          GwyMaskingType masking, gdouble trimfrac)
{
    GwyDataLine *myshifts;

    myshifts = gwy_data_field_find_row_shifts_trimmed_diff(field, mask, masking, trimfrac, 0);
    gwy_data_field_subtract_row_shifts(field, myshifts);
    gwy_data_line_assign(shifts, myshifts);
    g_object_unref(myshifts);
}

static void
linematch_do_modus(GwyDataField *field, GwyDataField *mask, GwyDataLine *modi,
                   GwyMaskingType masking)
{
    gint xres, yres;
    const gdouble *d, *m;
    gdouble total_median;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    total_median = gwy_data_field_area_get_median_mask(field, mask, masking, 0, 0, xres, yres);

    d = gwy_data_field_get_data(field);
    m = mask ? gwy_data_field_get_data_const(mask) : NULL;
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,modi,xres,yres,masking,total_median)
#endif
    {
        GwyDataLine *line = gwy_data_line_new(xres, 1.0, FALSE);
        gdouble *buf = gwy_data_line_get_data(line);
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i;

        for (i = ifrom; i < ito; i++) {
            const gdouble *row = d + i*xres;
            const gdouble *mrow = m ? m + i*xres : NULL;
            gdouble modus;
            gint count = 0, j;

            for (j = 0; j < xres; j++) {
                if ((masking == GWY_MASK_INCLUDE && mrow[j] <= 0.0)
                    || (masking == GWY_MASK_EXCLUDE && mrow[j] >= 1.0))
                    continue;

                buf[count++] = row[j];
            }

            if (!count)
                modus = total_median;
            else if (count < 9)
                modus = gwy_math_median(count, buf);
            else {
                gint seglen = GWY_ROUND(sqrt(count)), bestj = 0;
                gdouble diff, bestdiff = G_MAXDOUBLE;

                gwy_math_sort(count, buf);
                for (j = 0; j + seglen-1 < count; j++) {
                    diff = buf[j + seglen-1] - buf[j];
                    if (diff < bestdiff) {
                        bestdiff = diff;
                        bestj = j;
                    }
                }
                modus = 0.0;
                count = 0;
                for (j = seglen/3; j < seglen - seglen/3; j++, count++)
                    modus += buf[bestj + j];
                modus /= count;
            }

            gwy_data_line_set_val(modi, i, modus);
        }

        g_object_unref(line);
    }

    zero_level_row_shifts(modi);
    gwy_data_field_subtract_row_shifts(field, modi);
}
static void
linematch_do_match(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                   GwyMaskingType masking)
{
    gint xres, yres, k;
    const gdouble *d, *m;
    gdouble *s;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data(field);
    m = mask ? gwy_data_field_get_data_const(mask) : NULL;
    s = gwy_data_line_get_data(shifts);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(d,m,s,xres,yres,masking)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres-1) + 1;
        gint ito = gwy_omp_chunk_end(yres-1) + 1;
        gdouble *w = g_new(gdouble, xres-1);
        const gdouble *a, *b, *ma, *mb;
        gdouble q, wsum, lambda, x;
        gint i, j;

        for (i = ifrom; i < ito; i++) {
            a = d + xres*(i - 1);
            b = d + xres*i;
            ma = m + xres*(i - 1);
            mb = m + xres*i;

            /* Diffnorm */
            wsum = 0.0;
            for (j = 0; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0)))
                    continue;

                x = a[j+1] - a[j] - b[j+1] + b[j];
                wsum += fabs(x);
            }
            if (wsum == 0) {
                s[i] = 0.0;
                continue;
            }
            q = wsum/(xres-1);

            /* Weights */
            wsum = 0.0;
            for (j = 0; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0))) {
                    w[j] = 0.0;
                    continue;
                }

                x = a[j+1] - a[j] - b[j+1] + b[j];
                w[j] = exp(-(x*x/(2.0*q)));
                wsum += w[j];
            }

            /* Correction */
            lambda = (a[0] - b[0])*w[0];
            for (j = 1; j < xres-1; j++) {
                if ((masking == GWY_MASK_INCLUDE && (ma[j] <= 0.0 || mb[j] <= 0.0))
                    || (masking == GWY_MASK_EXCLUDE && (ma[j] >= 1.0 || mb[j] >= 1.0)))
                    continue;

                lambda += (a[j] - b[j])*(w[j-1] + w[j]);
            }
            lambda += (a[xres-1] - b[xres-1])*w[xres-2];
            lambda /= 2.0*wsum;

            gwy_debug("%g %g %g", q, wsum, lambda);

            s[i] = -lambda;
        }

        g_free(w);
    }

    s[0] = 0.0;
    for (k = 1; k < yres; k++)
        s[k] += s[k-1];

    zero_level_row_shifts(shifts);
    gwy_data_field_subtract_row_shifts(field, shifts);
}
static gdouble
row_fit_facet_tilt(const gdouble *drow,
                   const gdouble *mrow,
                   GwyMaskingType masking,
                   guint res, gdouble dx,
                   guint mincount)
{
    const gdouble c = 1.0/200.0;

    gdouble vx, q, sumvx, sumvz, sigma2;
    guint i, n;

    sigma2 = 0.0;
    n = 0;
    if (mrow && masking == GWY_MASK_INCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] >= 1.0 && mrow[i+1] >= 1.0) {
                vx = (drow[i+1] - drow[i])/dx;
                sigma2 += vx*vx;
                n++;
            }
        }
    }
    else if (mrow && masking == GWY_MASK_EXCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] <= 0.0 && mrow[i+1] <= 0.0) {
                vx = (drow[i+1] - drow[i])/dx;
                sigma2 += vx*vx;
                n++;
            }
        }
    }
    else {
        for (i = 0; i < res-1; i++) {
            vx = (drow[i+1] - drow[i])/dx;
            sigma2 += vx*vx;
        }
        n = res-1;
    }
    /* Do not try to level from some random pixel */
    gwy_debug("n=%d", n);
    if (n < mincount)
        return 0.0;

    sigma2 = c*sigma2/n;
    sumvx = sumvz = 0.0;
    if (mrow && masking == GWY_MASK_INCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] >= 1.0 && mrow[i+1] >= 1.0) {
                vx = (drow[i+1] - drow[i])/dx;
                q = exp(vx*vx/sigma2);
                sumvx += vx/q;
                sumvz += 1.0/q;
            }
        }
    }
    else if (mrow && masking == GWY_MASK_EXCLUDE) {
        for (i = 0; i < res-1; i++) {
            if (mrow[i] <= 0.0 && mrow[i+1] <= 0.0) {
                vx = (drow[i+1] - drow[i])/dx;
                q = exp(vx*vx/sigma2);
                sumvx += vx/q;
                sumvz += 1.0/q;
            }
        }
    }
    else {
        for (i = 0; i < res-1; i++) {
            vx = (drow[i+1] - drow[i])/dx;
            q = exp(vx*vx/sigma2);
            sumvx += vx/q;
            sumvz += 1.0/q;
        }
    }

    return sumvx/sumvz * dx;
}

static void
untilt_row(gdouble *drow, guint res, gdouble bx)
{
    gdouble x;
    guint i;

    if (!bx)
        return;

    for (i = 0; i < res; i++) {
        x = i - 0.5*(res - 1);
        drow[i] -= bx*x;
    }
}
static void
linematch_do_facet_tilt(GwyDataField *field, GwyDataField *mask, GwyDataLine *shifts,
                        GwyMaskingType masking)
{
    guint xres, yres, i, j, mincount;
    const gdouble *mdata, *mrow;
    gdouble *data, *drow;
    gdouble dx, tilt;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    dx = gwy_data_field_get_dx(field);
    mincount = GWY_ROUND(log(xres) + 1);

    data = gwy_data_field_get_data(field);
    mdata = mask ? gwy_data_field_get_data_const(mask) : NULL;
    for (i = 0; i < yres; i++) {
        drow = data + i*xres;
        mrow = mdata ? mdata + i*xres : NULL;
        for (j = 0; j < 30; j++) {
            tilt = row_fit_facet_tilt(drow, mrow, masking, xres, dx, mincount);
            untilt_row(drow, xres, tilt);
            if (fabs(tilt/dx) < 1e-6)
                break;
        }
    }

    /* FIXME: Should we put the tilts there to confuse the user?  We need to
     * make sure all functions set the units correctly in such case. */
    gwy_data_line_clear(shifts);
}

static void
zero_level_row_shifts(GwyDataLine *shifts)
{
    gwy_data_line_add(shifts, -gwy_data_line_get_avg(shifts));
}



/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
