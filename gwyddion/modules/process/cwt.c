/*
 *  $Id: cwt.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/inttrans.h>
#include <libprocess/cwt.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_WAVELET,
    PARAM_SCALE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             cwt                 (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Two-dimensional CWT (Continuous Wavelet Transform)."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, cwt)

static gboolean
module_register(void)
{
    gwy_process_func_register("cwt",
                              (GwyProcessFunc)&cwt,
                              N_("/_Integral Transforms/2D _CWT..."),
                              GWY_STOCK_CWT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute continuous wavelet transform"));

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
    gwy_param_def_add_gwyenum(paramdef, PARAM_WAVELET, "wavelet", _("_Wavelet type"),
                              gwy_2d_cwt_wavelet_type_get_enum(), -1, GWY_2DCWT_GAUSS);
    gwy_param_def_add_double(paramdef, PARAM_SCALE, "scale", _("_Scale"), 0.0, 1000.0, 10.0);
    return paramdef;
}

static void
cwt(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *field;
    GwyParams *params;
    ModuleArgs args;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(field);

    args.field = field;
    args.params = params = gwy_params_new_from_settings(define_module_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    field = gwy_data_field_duplicate(field);
    gwy_data_field_cwt(field, GWY_INTERPOLATION_LINEAR, /* ignored */
                       gwy_params_get_double(params, PARAM_SCALE),
                       gwy_params_get_enum(params, PARAM_WAVELET));

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    g_object_unref(field);
    gwy_app_set_data_field_title(data, newid, _("CWT"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("2D CWT"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_SCALE);
    gwy_param_table_slider_add_alt(table, PARAM_SCALE);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_SCALE, args->field);
    gwy_param_table_append_combo(table, PARAM_WAVELET);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
