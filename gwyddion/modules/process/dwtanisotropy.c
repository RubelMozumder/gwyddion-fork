/*
 *  $Id: dwtanisotropy.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libgwyddion/gwyenum.h>
#include <libprocess/stats.h>
#include <libprocess/inttrans.h>
#include <libprocess/dwt.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_INTERP,
    PARAM_WAVELET,
    PARAM_RATIO,
    PARAM_LOWLIMIT,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input image properties. */
    gint goodsize;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             dwt_anisotropy      (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("2D DWT anisotropy detection based on X/Y components ratio."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, dwtanisotropy)

static gboolean
module_register(void)
{
    gwy_process_func_register("dwtanisotropy",
                              (GwyProcessFunc)&dwt_anisotropy,
                              N_("/_Integral Transforms/DWT _Anisotropy..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("DWT anisotropy detection"));

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
    gwy_param_def_add_enum(paramdef, PARAM_INTERP, "interp", NULL,
                           GWY_TYPE_INTERPOLATION_TYPE, GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_gwyenum(paramdef, PARAM_WAVELET, "wavelet", _("_Wavelet type"),
                              gwy_dwt_type_get_enum(), -1, GWY_DWT_DAUB12);
    gwy_param_def_add_double(paramdef, PARAM_RATIO, "ratio", _("X/Y ratio threshold"), 0.0001, 10.0, 0.2);
    gwy_param_def_add_int(paramdef, PARAM_LOWLIMIT, "lowlimit", _("Low level exclude limit"), 1, 20, 4);
    return paramdef;
}

static void
dwt_anisotropy(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GQuark mquark;
    gint id, i;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     0);
    g_return_if_fail(args.field);

    if (!gwy_require_square_image(args.field, data, id, _("DWT Anisotropy")))
        return;

    args.goodsize = 1;
    for (i = gwy_data_field_get_xres(args.field)-1; i; i >>= 1)
        args.goodsize <<= 1;

    args.params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    gint xres = gwy_data_field_get_xres(args->field);
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    gchar *s;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("2D DWT Anisotropy"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_WAVELET);
    gwy_param_table_append_slider(table, PARAM_RATIO);
    gwy_param_table_slider_set_mapping(table, PARAM_RATIO, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_append_slider(table, PARAM_LOWLIMIT);
    gwy_param_table_slider_set_mapping(table, PARAM_LOWLIMIT, GWY_SCALE_MAPPING_LINEAR);
    if (xres != args->goodsize) {
        gwy_param_table_append_separator(table);
        s = g_strdup_printf(_("Size %d is not a power of 2."), xres);
        gwy_param_table_append_message(table, -1, s);
        g_free(s);
        s = g_strdup_printf(_("Image will be resampled to %d×%d for DWT."), args->goodsize, args->goodsize);
        gwy_param_table_append_message(table, -1, s);
        g_free(s);
        gwy_param_table_append_separator(table);
    }
    gwy_param_table_append_combo(table, PARAM_INTERP);
    gwy_param_table_set_sensitive(table, PARAM_INTERP, xres != args->goodsize);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static void
execute(ModuleArgs *args)
{
    GwyDWTType wavelet = gwy_params_get_enum(args->params, PARAM_WAVELET);
    GwyInterpolationType interp = gwy_params_get_enum(args->params, PARAM_INTERP);
    gdouble ratio = gwy_params_get_double(args->params, PARAM_RATIO);
    gint limit = gwy_params_get_int(args->params, PARAM_LOWLIMIT);
    gint xres = gwy_data_field_get_xres(args->field);
    GwyDataField *resampled, *mask;
    GwyDataLine *wtcoefs;

    resampled = gwy_data_field_new_resampled(args->field, args->goodsize, args->goodsize, interp);
    gwy_data_field_add(resampled, -gwy_data_field_get_avg(resampled));

    mask = args->result = gwy_data_field_new_alike(resampled, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);

    wtcoefs = gwy_data_line_new(1, 1.0, TRUE);
    wtcoefs = gwy_dwt_set_coefficients(wtcoefs, wavelet);
    gwy_data_field_dwt_mark_anisotropy(resampled, mask, wtcoefs, ratio, (1u << limit));
    g_object_unref(wtcoefs);
    g_object_unref(resampled);

    gwy_data_field_resample(mask, xres, xres, GWY_INTERPOLATION_ROUND);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
