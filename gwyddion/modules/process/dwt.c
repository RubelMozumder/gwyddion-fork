/*
 *  $Id: dwt.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_INTERP,
    PARAM_WAVELET,
    PARAM_INVERSE_TRANSFORM,
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
static void             dwt                 (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Two-dimensional DWT (Discrete Wavelet Transform)."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, dwt)

static gboolean
module_register(void)
{
    gwy_process_func_register("dwt",
                              (GwyProcessFunc)&dwt,
                              N_("/_Integral Transforms/2D _DWT..."),
                              GWY_STOCK_DWT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute Discrete Wavelet Transform"));

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
    gwy_param_def_add_boolean(paramdef, PARAM_INVERSE_TRANSFORM, "inverse_transform", _("_Inverse transform"), FALSE);
    return paramdef;
}

static void
dwt(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    gint id, newid, i;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    if (!gwy_require_square_image(args.field, data, id, _("DWT")))
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

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("DWT"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_channel_log_add_proc(data, id, newid);

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

    gui.dialog = gwy_dialog_new(_("2D DWT"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_WAVELET);
    gwy_param_table_append_checkbox(table, PARAM_INVERSE_TRANSFORM);
    gwy_param_table_set_sensitive(table, PARAM_INVERSE_TRANSFORM, xres == args->goodsize);
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
    gboolean is_inverse = gwy_params_get_boolean(args->params, PARAM_INVERSE_TRANSFORM);
    GwyTransformDirection dir = (is_inverse ? GWY_TRANSFORM_DIRECTION_BACKWARD : GWY_TRANSFORM_DIRECTION_FORWARD);
    GwyDataField *field = args->field, *result;
    GwyDataLine *wtcoefs;

    result = args->result = gwy_data_field_new_resampled(field, args->goodsize, args->goodsize, interp);
    if (!is_inverse)
        gwy_data_field_add(result, -gwy_data_field_get_avg(result));

    wtcoefs = gwy_data_line_new(1, 1.0, TRUE);
    wtcoefs = gwy_dwt_set_coefficients(wtcoefs, wavelet);
    gwy_data_field_dwt(result, wtcoefs, dir, 4);
    g_object_unref(wtcoefs);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
