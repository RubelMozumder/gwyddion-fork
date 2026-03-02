/*
 *  $Id: local_contrast.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_SIZE,
    PARAM_DEPTH,
    PARAM_WEIGHT,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

static gboolean         module_register        (void);
static GwyParamDef*     define_module_params   (void);
static void             maximize_local_contrast(GwyContainer *data,
                                                GwyRunType runtype);
static void             execute                (ModuleArgs *args);
static GwyDialogOutcome run_gui                (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Maximizes local contrast."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, local_contrast)

static gboolean
module_register(void)
{
    gwy_process_func_register("local_contrast",
                              (GwyProcessFunc)&maximize_local_contrast,
                              N_("/_Presentation/Local _Contrast..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Presentation with maximized local contrast"));

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
    gwy_param_def_add_int(paramdef, PARAM_SIZE, "size", _("Kernel _size"), 1, 30, 7);
    gwy_param_def_add_int(paramdef, PARAM_DEPTH, "depth", _("Blending _depth"), 2, 7, 4);
    gwy_param_def_add_double(paramdef, PARAM_WEIGHT, "weight", _("_Weight"), 0.0, 1.0, 0.7);
    return paramdef;
}

static void
maximize_local_contrast(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GQuark squark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);
    g_return_if_fail(args.field && squark);

    args.result = NULL;  /* Create it here once we have a GUI. */
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    execute(&args);

    gwy_data_field_normalize(args.result);
    gwy_app_undo_qcheckpointv(data, 1, &squark);
    gwy_container_set_object(data, squark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Increase Local Contrast")));
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_SIZE);
    gwy_param_table_slider_add_alt(table, PARAM_SIZE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SIZE, args->field);
    gwy_param_table_append_slider(table, PARAM_DEPTH);
    gwy_param_table_append_slider(table, PARAM_WEIGHT);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *result = args->result, *minfield, *maxfield;
    gint size = gwy_params_get_int(args->params, PARAM_SIZE);
    gint depth = gwy_params_get_int(args->params, PARAM_DEPTH);
    gdouble weight = gwy_params_get_double(args->params, PARAM_WEIGHT);
    const gdouble *dat, *min, *max;
    gdouble *show, *weights;
    gdouble sum, gmin, gmax;
    gint xres, yres, i, j;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    gmin = gwy_data_field_get_min(field);
    gmax = gwy_data_field_get_max(field);
    if (gmax == gmin) {
        gwy_data_field_clear(result);
        return;
    }

    minfield = gwy_data_field_duplicate(field);
    gwy_data_field_filter_minimum(minfield, size);

    maxfield = gwy_data_field_duplicate(field);
    gwy_data_field_filter_maximum(maxfield, size);

    dat = gwy_data_field_get_data_const(field);
    min = gwy_data_field_get_data_const(minfield);
    max = gwy_data_field_get_data_const(maxfield);
    show = gwy_data_field_get_data(result);

    weights = g_new(gdouble, depth);
    sum = 0.0;
    for (i = 0; i < depth; i++) {
        weights[i] = exp(-log(depth - 1.0)*i/(depth - 1.0));
        sum += weights[i];
    }

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(dat,show,min,max,weights,xres,yres,depth,size,weight,gmin,gmax,sum)
#endif
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble minv = dat[i*xres + j], maxv = dat[i*xres + j];
            gdouble mins = minv*weights[0];
            gdouble maxs = maxv*weights[0];
            gdouble v, vc;
            gint k, l;

            for (k = 1; k < depth; k++) {
                gint itop = MAX(0, i - k*size)*xres, ibot = MIN(yres-1, i + k*size)*xres;
                gint jleft = MAX(0, j - k*size), jright = MIN(xres-1, j + k*size);
                for (l = 0; l < 2*k+1; l++) {
                    gint imid = CLAMP(i + (l - k)*size, 0, yres-1)*xres;
                    gint jmid = CLAMP(j + (l - k)*size, 0, xres-1);

                    /* top line */
                    maxv = fmax(maxv, max[itop + jmid]);
                    minv = fmin(minv, min[itop + jmid]);

                    /* bottom line */
                    maxv = fmax(maxv, max[ibot + jmid]);
                    minv = fmin(minv, min[ibot + jmid]);

                    /* left line */
                    maxv = fmax(maxv, max[imid + jleft]);
                    minv = fmin(minv, min[imid + jleft]);

                    /* right line */
                    maxv = fmax(maxv, max[imid + jright]);
                    minv = fmin(minv, min[imid + jright]);
                }
                mins += minv*weights[k];
                maxs += maxv*weights[k];
            }
            mins /= sum;
            maxs /= sum;
            v = dat[i*xres + j];
            if (mins < maxs) {
                vc = (gmax - gmin)/(maxs - mins)*(v - mins) + gmin;
                v = weight*vc + (1.0 - weight)*v;
                v = CLAMP(v, gmin, gmax);
            }
            show[i*xres +j] = v;
        }
    }

    g_free(weights);
    g_object_unref(minfield);
    g_object_unref(maxfield);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
