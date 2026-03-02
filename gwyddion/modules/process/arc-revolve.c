/*
 *  $Id: arc-revolve.c 23815 2021-06-09 12:35:58Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    ARCREV_HORIZONTAL = 1,
    ARCREV_VERTICAL   = 2,
    ARCREV_BOTH       = 3,
} ArcRevDirection;

enum {
    PARAM_RADIUS,
    PARAM_DIRECTION,
    PARAM_INVERTED,
    PARAM_DO_EXTRACT,
    PARAM_UPDATE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *bg;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             arcrev              (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static void             arcrev_horizontal   (GwyDataField *field,
                                             GwyDataField *bg,
                                             gdouble radius);
static GwyDataLine*     make_arc            (gdouble radius,
                                             gint maxres);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Subtracts background by arc revolution."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, arc_revolve)

static gboolean
module_register(void)
{
    gwy_process_func_register("arc_revolve",
                              (GwyProcessFunc)&arcrev,
                              N_("/_Level/Revolve _Arc..."),
                              GWY_STOCK_REVOLVE_ARC,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Level data by arc revolution"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("_Horizontal direction"), ARCREV_HORIZONTAL, },
        { N_("_Vertical direction"),   ARCREV_VERTICAL,   },
        { N_("_Both directions"),      ARCREV_BOTH,       },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("_Radius"), 1.0, 1000.0, 20.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", _("Direction"),
                              directions, G_N_ELEMENTS(directions), ARCREV_HORIZONTAL);
    gwy_param_def_add_boolean(paramdef, PARAM_INVERTED, "inverted", _("_Invert height"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_DO_EXTRACT, "do_extract", _("E_xtract background"), FALSE);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
arcrev(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    gint id, newid;
    GQuark quark;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && quark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    args.bg = gwy_data_field_new_alike(args.field, TRUE);

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_container_set_object(data, gwy_app_get_data_key_for_id(id), args.result);
    gwy_app_channel_log_add_proc(data, id, id);

    if (gwy_params_get_boolean(args.params, PARAM_DO_EXTRACT)) {
        newid = gwy_app_data_browser_add_data_field(args.bg, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_set_data_field_title(data, newid, _("Background"));
        gwy_app_channel_log_add(data, id, newid, NULL, NULL);
    }

end:
    g_object_unref(args.bg);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *dataview, *hbox;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Revolve Arc"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_slider_add_alt(table, PARAM_RADIUS);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_RADIUS, args->field);
    gwy_param_table_append_radio(table, PARAM_DIRECTION);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_INVERTED);
    gwy_param_table_append_checkbox(table, PARAM_DO_EXTRACT);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

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
    if (id != PARAM_UPDATE && id != PARAM_DO_EXTRACT)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    ArcRevDirection direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);
    gboolean inverted = gwy_params_get_boolean(args->params, PARAM_INVERTED);
    gdouble radius = gwy_params_get_double(args->params, PARAM_RADIUS);
    GwyDataField *field = args->field, *bg = args->bg, *tmp;

    /* The only case not needing temporary fields. */
    if (direction == ARCREV_HORIZONTAL && !inverted) {
        arcrev_horizontal(field, bg, radius);
        goto end;
    }

    tmp = gwy_data_field_duplicate(field);
    if (inverted)
        gwy_data_field_multiply(tmp, -1.0);

    if (direction == ARCREV_HORIZONTAL || direction == ARCREV_BOTH)
        arcrev_horizontal(tmp, bg, radius);

    if (direction == ARCREV_HORIZONTAL) {
        gwy_data_field_multiply(bg, -1.0);
        g_object_unref(tmp);
        return;
    }
    if (direction == ARCREV_VERTICAL)
        gwy_data_field_copy(tmp, bg, FALSE);

    gwy_data_field_flip_xy(bg, tmp, FALSE);
    gwy_data_field_resample(bg, gwy_data_field_get_xres(tmp), gwy_data_field_get_yres(tmp), GWY_INTERPOLATION_NONE);
    arcrev_horizontal(tmp, bg, radius);
    gwy_data_field_flip_xy(bg, tmp, FALSE);
    gwy_data_field_assign(bg, tmp);
    g_object_unref(tmp);

    gwy_data_field_set_xreal(bg, gwy_data_field_get_xreal(field));
    gwy_data_field_set_yreal(bg, gwy_data_field_get_yreal(field));
    gwy_data_field_set_xoffset(bg, gwy_data_field_get_xoffset(field));
    gwy_data_field_set_yoffset(bg, gwy_data_field_get_yoffset(field));

    if (inverted)
        gwy_data_field_multiply(bg, -1.0);

end:
    gwy_data_field_subtract_fields(args->result, args->field, bg);
}

/* An efficient summing algorithm.  Although I'm author of this code, don't ask me how it works... */
static void
moving_sums(gint res, const gdouble *row, gdouble *buffer, gint size)
{
    gdouble *sum, *sum2;
    gint i, ls2, rs2;

    memset(buffer, 0, 2*res*sizeof(gdouble));
    sum = buffer;
    sum2 = buffer + res;

    ls2 = size/2;
    rs2 = (size - 1)/2;

    /* Shortcut: very large size */
    if (rs2 >= res) {
        for (i = 0; i < res; i++) {
            sum[i] += row[i];
            sum2[i] += row[i]*row[i];
        }
        for (i = 1; i < res; i++) {
            sum[i] = sum[0];
            sum2[i] = sum2[0];
        }
        return;
    }

    /* Phase 1: Fill first element */
    for (i = 0; i <= rs2; i++) {
       sum[0] += row[i];
       sum2[0] += row[i]*row[i];
    }

    /* Phase 2: Next elements only gather new data */
    for (i = 1; i <= MIN(ls2, res-1 - rs2); i++) {
        sum[i] = sum[i-1] + row[i + rs2];
        sum2[i] = sum2[i-1] + row[i + rs2]*row[i + rs2];
    }

    /* Phase 3a: Moving a sprat! */
    for (i = ls2+1; i <= res-1 - rs2; i++) {
        sum[i] = sum[i-1] + row[i + rs2] - row[i - ls2 - 1];
        sum2[i] = sum2[i-1] + row[i + rs2]*row[i + rs2] - row[i - ls2 - 1]*row[i - ls2 - 1];
    }

    /* Phase 3b: Moving a whale! */
    for (i = res-1 - rs2; i <= ls2; i++) {
        sum[i] = sum[i-1];
        sum2[i] = sum2[i-1];
    }

    /* Phase 4: Next elements only lose data */
    for (i = MAX(ls2+1, res - rs2); i < res; i++) {
        sum[i] = sum[i-1] - row[i - ls2 - 1];
        sum2[i] = sum2[i-1] - row[i - ls2 - 1]*row[i - ls2 - 1];
    }
}

static void
arcrev_horizontal(GwyDataField *field, GwyDataField *bg, gdouble radius)
{
    GwyDataLine *arc;
    gdouble *rdata, *arcdata, *sum, *sum2, *weight, *tmp;
    const gdouble *data;
    gdouble q;
    gint i, j, k, size, xres, yres;

    data = gwy_data_field_get_data_const(field);
    xres = gwy_data_field_get_xres(bg);
    yres = gwy_data_field_get_yres(bg);
    rdata = gwy_data_field_get_data(bg);

    q = gwy_data_field_get_rms(field)/sqrt(2.0/3.0 - G_PI/16.0);
    arc = make_arc(radius, gwy_data_field_get_xres(field));

    /* Scale-freeing.
     * Data is normalized to have the same RMS as if it was composed from arcs of radius args->radius.  Actually we
     * normalize the arc instead, but the effect is the same.  */
    gwy_data_line_multiply(arc, -q);
    arcdata = gwy_data_line_get_data(arc);
    size = gwy_data_line_get_res(arc)/2;

    sum = g_new(gdouble, 4*xres);
    sum2 = sum + xres;
    weight = sum + 2*xres;
    tmp = sum + 3*xres;

    /* Weights for RMS filter.  The fool-proof way is to sum 1's. */
    for (j = 0; j < xres; j++)
        weight[j] = 1.0;
    moving_sums(xres, weight, sum, size);
    gwy_assign(weight, sum, xres);

    for (i = 0; i < yres; i++) {
        const gdouble *drow = data + i*xres;
        gdouble *rrow = rdata + i*xres;

        /* Kill data that stick down too much */
        moving_sums(xres, data + i*xres, sum, size);
        for (j = 0; j < xres; j++) {
            /* transform to avg - 2.5*rms */
            sum[j] = sum[j]/weight[j];
            sum2[j] = 2.5*sqrt(sum2[j]/weight[j] - sum[j]*sum[j]);
            sum[j] -= sum2[j];
        }
        for (j = 0; j < xres; j++)
            tmp[j] = MAX(drow[j], sum[j]);

        /* Find the touching point */
        for (j = 0; j < xres; j++) {
            gdouble *row = tmp + j;
            gint from, to;
            gdouble min;

            from = MAX(0, j-size) - j;
            to = MIN(j+size, xres-1) - j;
            min = G_MAXDOUBLE;
            for (k = from; k <= to; k++) {
                gdouble d = -arcdata[size+k] + row[k];
                if (d < min)
                    min = d;
            }
            rrow[j] = min;
        }
    }

    g_free(sum);
    g_object_unref(arc);
}

static GwyDataLine*
make_arc(gdouble radius, gint maxres)
{
    GwyDataLine *arc;
    gdouble *data;
    gint i, size;
    gdouble u, z;

    size = GWY_ROUND(MIN(radius, maxres));
    arc = gwy_data_line_new(2*size+1, 1.0, FALSE);
    data = gwy_data_line_get_data(arc);

    for (i = 0; i <= size; i++) {
        u = i/radius;

        /* Pathological case: very flat arc. */
        if (radius/8 > maxres)
            z = u*u/2.0*(1.0 + u*u/4.0*(1 + u*u/2.0));
        else if (G_UNLIKELY(u > 1.0))
            z = 1.0;
        else
            z = 1.0 - sqrt(1.0 - u*u);

        data[size+i] = data[size-i] = z;
    }

    return arc;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
