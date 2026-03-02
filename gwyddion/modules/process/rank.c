/*
 *  $Id: rank.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/elliptic.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    FILTER_RANK          = 0,
    FILTER_NORMALIZATION = 1,
    FILTER_RANGE         = 2,
    FILTER_NFILTERS,
} FilterType;

enum {
    PARAM_SIZE,
    PARAM_TYPE
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             rank                (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static gboolean         execute_rank        (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             execute_minmax      (ModuleArgs *args);
static gdouble          local_rank          (GwyDataField *data_field,
                                             gint size,
                                             const gint *xsize,
                                             gint col,
                                             gint row);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Enhances local contrast using a rank transform."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2014",
};

GWY_MODULE_QUERY2(module_info, rank)

static gboolean
module_register(void)
{
    gwy_process_func_register("rank",
                              (GwyProcessFunc)&rank,
                              N_("/_Presentation/_Rank..."),
                              GWY_STOCK_RANK_FILTER,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Presentation with local contrast ehnanced using a rank transform"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum types[] = {
        { N_("Rank transform"),      FILTER_RANK,          },
        { N_("Local normalization"), FILTER_NORMALIZATION, },
        { N_("Value range"),         FILTER_RANGE,         },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("Kernel _size"), 1, 129, 15);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("_Filter type"),
                              types, G_N_ELEMENTS(types), FILTER_RANK);
    return paramdef;
}

static void
rank(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GQuark squark;
    gboolean ok = TRUE;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);
    g_return_if_fail(args.field && squark);

    args.result = NULL;  /* Create it here once we have a GUI. */
    args.params = gwy_params_new_from_settings(define_module_params());
    if (run == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);

    if (gwy_params_get_enum(args.params, PARAM_TYPE) == FILTER_RANK)
        ok = execute_rank(&args, data, id);
    else
        execute_minmax(&args);

    if (ok) {
        gwy_data_field_normalize(args.result);
        gwy_app_undo_qcheckpointv(data, 1, &squark);
        gwy_container_set_object(data, squark, args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Rank Transform")));
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIZE, args->field);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static gboolean
execute_rank(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDataField *showfield = args->result, *field = args->field;
    gdouble *show;
    gint xres, yres, asize, size, k;
    gint *xsize;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    show = gwy_data_field_get_data(showfield);

    asize = gwy_params_get_int(args->params, PARAM_SIZE);
    size = 2*asize + 1;

    gwy_app_wait_start(gwy_app_find_window_for_channel(data, id), _("Rank transform..."));

    xsize = g_new(gint, size);
    for (k = -(gint)asize; k <= (gint)asize; k++) {
        gdouble x = sqrt(0.25*size*size - k*k);
        xsize[k + asize] = (gint)floor(x);
    }

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(show,field,xres,yres,xsize,asize,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j;

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < xres; j++)
                show[i*xres + j] = local_rank(field, asize, xsize, j, i);

            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, i, ifrom, ito, pcancelled))
                break;
        }
    }
    gwy_app_wait_finish();
    g_free(xsize);

    return !cancelled;
}

static gdouble
local_rank(GwyDataField *data_field, gint size, const gint *xsize, gint col, gint row)
{
    gint xres, yres, i, j, yfrom, yto;
    guint r, hr, t;
    const gdouble *data;
    gdouble v;

    xres = data_field->xres;
    yres = data_field->yres;
    data = data_field->data;
    v = data[row*xres + col];

    yfrom = MAX(0, row - size);
    yto = MIN(yres-1, row + size);

    r = hr = t = 0;
    for (i = yfrom; i <= yto; i++) {
        gint xr = xsize[i - row + size];
        gint xfrom = MAX(0, col - xr);
        gint xto = MIN(xres-1, col + xr);
        guint xlen = xto - xfrom + 1;
        const gdouble *d = data + i*xres + xfrom;

        for (j = xlen; j; j--, d++) {
            if (*d <= v) {
                r++;
                if (G_UNLIKELY(*d == v))
                    hr++;
            }
            t++;
        }
    }

    return (r - 0.5*hr)/t;
}

static void
execute_minmax(ModuleArgs *args)
{
    GwyDataField *showfield = args->result, *kernel;
    GwyMinMaxFilterType filtertype;
    gint size;

    size = 2*gwy_params_get_int(args->params, PARAM_SIZE) + 1;
    filtertype = (gwy_params_get_enum(args->params, PARAM_TYPE) == FILTER_NORMALIZATION
                  ? GWY_MIN_MAX_FILTER_NORMALIZATION
                  : GWY_MIN_MAX_FILTER_RANGE);
    kernel = gwy_data_field_new(size, size, size, size, TRUE);
    gwy_data_field_elliptic_area_fill(kernel, 0, 0, size, size, 1.0);
    gwy_data_field_copy(args->field, showfield, FALSE);
    gwy_data_field_area_filter_min_max(showfield, kernel, filtertype, 0, 0, showfield->xres, showfield->yres);
    g_object_unref(kernel);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
