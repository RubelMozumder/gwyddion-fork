/*
 *  $Id: blockstep.c 26087 2024-01-02 16:32:16Z yeti-dn $
 *  Copyright (C) 2003-2023 David Necas (Yeti), Petr Klapetek, Luke Somers.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, lsomers@sas.upenn.edu.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/linestats.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_THRESHOLD,
    PARAM_SCANDIR,
    PARAM_UPDATE,
    PARAM_DISPLAY,
    PARAM_MASK_COLOR,

    INFO_NBLOCKS,
};

typedef enum {
    SCANDIR_RTL = -1,
    SCANDIR_LTR  = 1,
} ScanDirection;

typedef enum {
    PREVIEW_CORRECTED = 0,
    PREVIEW_MASK      = 1,
    PREVIEW_BLOCKS    = 2,
    PREVIEW_NTYPES,
} PreviewType;

typedef struct {
    gint totalsteps;
    gint pos;
    gdouble score;
} LineSplit;

typedef struct {
    gint i;
    gint fromleft;
    gdouble shift;
} BlockStep;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *mask;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             block_correct_step  (GwyContainer *data,
                                             GwyRunType run);
static gint             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Corrects vertical steps in scan lines by block."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, blockstep)

static gboolean
module_register(void)
{
    gwy_process_func_register("block_correct_step",
                              (GwyProcessFunc)&block_correct_step,
                              N_("/_Correct Data/Ste_p Block Correction"),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Correct steps without any line correction"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("Correc_ted data"),          PREVIEW_CORRECTED, },
        { N_("_Marked discontinuities"),  PREVIEW_MASK,      },
        { N_("Marked _block boundaries"), PREVIEW_BLOCKS,    },
    };
    static const GwyEnum scandirs[] = {
        { N_("_Left to right"), SCANDIR_LTR, },
        { N_("_Right to left"), SCANDIR_RTL, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("_Threshold"), 0.1, 10.0, 2.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SCANDIR, "scandir", _("Scanning direction"),
                              scandirs, G_N_ELEMENTS(scandirs), SCANDIR_LTR);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), PREVIEW_CORRECTED);
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
block_correct_step(GwyContainer *data,
                   GwyRunType run)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark dquark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_KEY, &dquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && dquark);

    args.result = gwy_data_field_duplicate(args.field);
    args.mask = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &dquark);
    gwy_data_field_assign(args.field, args.result);
    gwy_data_field_data_changed(args.field);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.mask);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Block Step Correction"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD, _("RMS"));
    gwy_param_table_append_combo(table, PARAM_SCANDIR);
    gwy_param_table_append_info(table, INFO_NBLOCKS, _("Number of detected steps"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, NULL, -1);

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

    if (id < 0 || id == PARAM_DISPLAY) {
        PreviewType ptype = gwy_params_get_enum(params, PARAM_DISPLAY);

        if (ptype == PREVIEW_CORRECTED) {
            gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->result);
            gwy_container_remove(gui->data, gwy_app_get_mask_key_for_id(0));
        }
        else {
            gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->field);
            gwy_container_set_object(gui->data, gwy_app_get_mask_key_for_id(0), args->mask);
        }
    }

    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    gchar *s;
    guint n;

    n = execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_data_field_data_changed(args->mask);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));

    s = g_strdup_printf("%u", n);
    gwy_param_table_info_set_valuestr(gui->table, INFO_NBLOCKS, s);
    g_free(s);
}

static LineSplit*
mark_discontinuities(GwyDataField *field, GwyDataField *mask,
                     gdouble threshold, ScanDirection scandir)
{
    const gdouble *d = gwy_data_field_get_data_const(field);
    gint i, xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gint *imask;
    LineSplit *ls;

    ls = g_new0(LineSplit, yres);
    imask = g_new(gint, xres*yres);
    gwy_clear(imask, xres);

    /* Mark locations of large jumps in the y direction. More precisely, mark the second row (after the jump). */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(d,imask,ls,xres,yres,threshold)
#endif
    for (i = 1; i < yres; i++) {
        const gdouble *row = d + i*xres;
        const gdouble *prev = row - xres;
        gint *mrow = imask + i*xres;
        gint j, c = 0;

        for (j = 0; j < xres; j++)
            c += mrow[j] = (fabs(row[j] - prev[j]) > threshold);
        ls[i].totalsteps = c;
    }

    for (i = 1; i < yres; i++) {
        if (ls[i].totalsteps > xres/10) {
            gwy_debug("[%d] %d steps", i, ls[i].totalsteps);
        }
    }

    /* Compute how far a too large step goes from either side for each row. If they meet (or almost) consider it a
     * full interruption. We consider only shapes like
     *
     * ----------------------------------------------------------------------------------------------------------
     *
     * and
     *                                                      -----------------------------------------------------
     * -----------------------------------------------------
     *
     * We might want an option for different scan line orientation in the image (like backward scans). */

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(imask,ls,xres,yres,scandir)
#endif
    for (i = 1; i < yres; i++) {
        gint *row = imask + i*xres;
        gint *prev = row - xres;
        gint j, best = 0, bestpos = xres/2, nleft, nright, seenup = 0, seendown = 0, ntotal;

        /* Try to find the best split to the upper (right) part and lower (left) segments.
         * For i == 1 the upper row is always empty, so the best will always be the entire lower row (pos = xres).
         *
         * If there are complete rows, we will find them twice, with pos = xres at index i and with pos = 0 at index
         * i+1. */
        ntotal = ls[scandir == SCANDIR_LTR ? i-1 : i].totalsteps;
        j = 0;
        while (TRUE) {
            if (scandir == SCANDIR_LTR) {
                nleft = seendown;
                nright = ntotal - seenup;
            }
            else {
                nleft = seenup;
                nright = ntotal - seendown;
            }

            if (nleft + nright > best) {
                best = nleft + nright;
                bestpos = j;
            }
            if (j == xres)
                break;

            seenup += prev[j];
            seendown += row[j];
            j++;
        }

        ls[i].pos = bestpos;
        ls[i].score = best;
    }

    if (mask) {
        gdouble *m = gwy_data_field_get_data(mask);

        for (i = 0; i < xres*yres - xres; i++)
            m[i] = fmax(imask[i], imask[i + xres]);
        for (i = xres*yres - xres; i < xres*yres; i++)
            m[i] = imask[i];
    }
    g_free(imask);

    return ls;
}

static inline void
process_one_step_segment(const gdouble *row, gdouble *mrow, gdouble *shifts,
                         gint xres, gint from, gint len)
{
    gint j;

    row += from;
    shifts += from;
    for (j = 0; j < len; j++)
        shifts[j] = row[j + xres] - row[j];
    if (mrow) {
        mrow += from;
        for (j = 0; j < len; j++)
            mrow[j + xres] = mrow[j] = 1.0;
    }
}

static GArray*
construct_blocks(GwyDataField *field, GwyDataField *mask,
                 const LineSplit *ls, ScanDirection scandir)
{
    gint xres = gwy_data_field_get_xres(field), yres = gwy_data_field_get_yres(field);
    gint i, minlength = (gint)(3*xres/4);
    const gdouble *d = gwy_data_field_get_data_const(field);
    gdouble *m = (mask ? gwy_data_field_get_data(mask) : NULL);
    GArray *blocksteps;
    gdouble *shifts;
    guint k;

    /* Select blocks covering enough of the scan line width. */
    blocksteps = g_array_new(FALSE, FALSE, sizeof(BlockStep));
    for (i = 1; i < yres; i++) {
        if (ls[i].score >= minlength) {
            BlockStep bs;

            gwy_debug("[%d] pos=%d, score=%g", i, ls[i].pos, ls[i].score);
            /* If the discontiuity is full-width, make it between the first two rows, not the second two. */
            if (scandir == SCANDIR_LTR && ls[i].pos == xres) {
                if (i == yres-1)
                    continue;
                bs.i = i+1;
                bs.fromleft = 0;
            }
            else if (scandir == SCANDIR_RTL && ls[i].pos == 0) {
                if (i == yres-1)
                    continue;
                bs.i = i+1;
                bs.fromleft = xres;
            }
            else {
                bs.i = i;
                bs.fromleft = ls[i].pos;
            }
            /* Temporarily use shift for the score. */
            bs.shift = ls[i].score;
            gwy_debug("new block at %d, fromleft=%d", bs.i, bs.fromleft);
            g_array_append_val(blocksteps, bs);
        }
    }

    /* Do not allow blocks on consecutive lines. */
    if (blocksteps->len > 1) {
        for (k = blocksteps->len-1; k; k--) {
            BlockStep *bs0 = &g_array_index(blocksteps, BlockStep, k-1);
            BlockStep *bs1 = &g_array_index(blocksteps, BlockStep, k);

            if (bs1->i - bs0->i <= 1) {
                gwy_debug("adjacent blocks at %d and %d", bs0->i, bs1->i);
                g_array_remove_index(blocksteps, bs1->shift > bs0->shift ? k-1 : k);
            }
        }
    }

    shifts = g_new(gdouble, xres);
    if (mask)
        gwy_data_field_clear(mask);

    for (k = 0; k < blocksteps->len; k++) {
        BlockStep *bs = &g_array_index(blocksteps, BlockStep, k);
        const gdouble *row = d + (bs->i - 1)*xres;
        gdouble *mrow = m ? m + (bs->i - 1)*xres : NULL;
        gint fromleft = bs->fromleft;

        if (scandir == SCANDIR_LTR)
            process_one_step_segment(row, mrow, shifts, xres, 0, fromleft);
        else
            process_one_step_segment(row, mrow, shifts, xres, fromleft, xres - fromleft);
        row -= xres;
        if (scandir == SCANDIR_LTR)
            process_one_step_segment(row, mrow, shifts, xres, fromleft, xres - fromleft);
        else
            process_one_step_segment(row, mrow, shifts, xres, 0, fromleft);

        bs->shift = gwy_math_trimmed_mean(xres, shifts, xres/4, xres/4);
        /* We kind of have not decided on the definition of vertical position… */
        bs->i--;

        gwy_debug("[block step %d] leftlen %d, step %g", bs->i, fromleft, bs->shift);
    }
    g_free(shifts);

    if (blocksteps->len) {
        BlockStep bs;

        /* Sentinel. */
        bs.i = yres+1;
        bs.fromleft = xres;
        bs.shift = 0.0;
        g_array_append_val(blocksteps, bs);
    }
    else {
        g_array_free(blocksteps, TRUE);
        blocksteps = NULL;
    }

    return blocksteps;
}

static void
apply_correction(GwyDataField *field, const BlockStep *bs, ScanDirection scandir)
{
    gdouble *d, *row;
    gdouble shift = 0.0;
    gint i, j, xres, yres;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data(field);

    for (i = bs->i; i < yres; i++) {
        row = d + i*xres;
        if (i == bs->i) {
            if (scandir == SCANDIR_LTR) {
                for (j = 0; j < bs->fromleft; j++)
                    row[j] += shift;
                shift -= bs->shift;
                for (j = bs->fromleft; j < xres; j++)
                    row[j] += shift;
            }
            else {
                for (j = bs->fromleft; j < xres; j++)
                    row[j] += shift;
                shift -= bs->shift;
                for (j = 0; j < bs->fromleft; j++)
                    row[j] += shift;
            }

            bs++;
        }
        else {
            for (j = 0; j < xres; j++)
                row[j] += shift;
        }
    }
}

/* Return whether the image has actually changed – even though no one uses that currently. */
static gint
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *result = args->result, *mask = args->mask;
    gdouble threshold = gwy_params_get_double(args->params, PARAM_THRESHOLD);
    ScanDirection scandir = gwy_params_get_enum(args->params, PARAM_SCANDIR);
    PreviewType display = gwy_params_get_enum(args->params, PARAM_DISPLAY);
    GwyDataLine *rmsline;
    LineSplit *ls;
    GArray *blocksteps;
    guint nblocks;
    gdouble rms;

    /* Get root square mean vertical difference (pixelwise, tan β₀ is in real units). */
    rmsline = gwy_data_line_new(1, 1, FALSE);
    gwy_data_field_get_line_stats(field, rmsline, GWY_LINE_STAT_TAN_BETA0, GWY_ORIENTATION_VERTICAL);
    rms = gwy_data_line_get_avg(rmsline) * gwy_data_field_get_dy(field);
    g_object_unref(rmsline);
    threshold *= rms;

    ls = mark_discontinuities(field, (display == PREVIEW_MASK ? mask : NULL), threshold, scandir);
    blocksteps = construct_blocks(field, (display == PREVIEW_BLOCKS ? mask : NULL), ls, scandir);
    g_free(ls);
    gwy_data_field_assign(result, field);
    if (!blocksteps)
        return 0;

    nblocks = blocksteps->len-1;
    apply_correction(result, &g_array_index(blocksteps, BlockStep, 0), scandir);
    g_array_free(blocksteps, TRUE);

    return nblocks;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
