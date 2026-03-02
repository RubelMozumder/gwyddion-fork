/*
 *  $Id: convolve.c 24441 2021-10-29 15:30:17Z yeti-dn $
 *  Copyright (C) 2018-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/correlation.h>
#include <libprocess/filters.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

typedef enum {
    CONVOLVE_SIZE_CROP   = 0,
    CONVOLVE_SIZE_KEEP   = 1,
    CONVOLVE_SIZE_EXTEND = 2,
} ConvolveSizeType;

enum {
    PARAM_EXTERIOR,
    PARAM_OUTSIZE,
    PARAM_KERNEL,
    PARAM_AS_INTEGRAL,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             convolve            (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         kernel_filter       (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Convolves two images."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, convolve)

static gboolean
module_register(void)
{
    gwy_process_func_register("convolve",
                              (GwyProcessFunc)&convolve,
                              N_("/M_ultidata/_Convolve..."),
                              GWY_STOCK_CONVOLVE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Convolve two images"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum exteriors[] = {
        { N_("Zero"),              GWY_EXTERIOR_FIXED_VALUE,   },
        { N_("exterior|Border"),   GWY_EXTERIOR_BORDER_EXTEND, },
        { N_("exterior|Mirror"),   GWY_EXTERIOR_MIRROR_EXTEND, },
        { N_("exterior|Periodic"), GWY_EXTERIOR_PERIODIC,      },
        { N_("exterior|Laplace"),  GWY_EXTERIOR_LAPLACE,       },
    };
    static const GwyEnum outsizes[] = {
        { N_("Crop to interior"),    CONVOLVE_SIZE_CROP,   },
        { N_("Keep size"),           CONVOLVE_SIZE_KEEP,   },
        { N_("Extend to convolved"), CONVOLVE_SIZE_EXTEND, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_EXTERIOR, "exterior", _("_Exterior type"),
                              exteriors, G_N_ELEMENTS(exteriors), GWY_EXTERIOR_FIXED_VALUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTSIZE, "outsize", _("Output _size"),
                              outsizes, G_N_ELEMENTS(outsizes), CONVOLVE_SIZE_KEEP);
    gwy_param_def_add_image_id(paramdef, PARAM_KERNEL, "kernel", _("Convolution _kernel"));
    gwy_param_def_add_boolean(paramdef, PARAM_AS_INTEGRAL, "as_integral", _("Normalize as _integral"), FALSE);
    return paramdef;
}

static void
convolve(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    gint newid, id;

    gwy_clear(&args, 1);
    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);
    args.params = gwy_params_new_from_settings(define_module_params());

    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Convolved"));
    gwy_app_channel_log_add_proc(data, id, newid);

end:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Convolve"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_KERNEL);
    gwy_param_table_data_id_set_filter(table, PARAM_KERNEL, kernel_filter, args, NULL);
    gwy_param_table_append_combo(table, PARAM_EXTERIOR);
    gwy_param_table_append_combo(table, PARAM_OUTSIZE);
    gwy_param_table_append_checkbox(table, PARAM_AS_INTEGRAL);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_OUTSIZE)
        gwy_param_table_data_id_refilter(table, PARAM_KERNEL);

    if (id < 0 || id == PARAM_OUTSIZE || id == PARAM_KERNEL) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                          !gwy_params_data_id_is_none(params, PARAM_KERNEL));
    }
}

static gboolean
kernel_filter(GwyContainer *data, gint id, gpointer user_data)
{
    ModuleArgs *args = (ModuleArgs*)user_data;
    GwyDataField *kernel, *field = args->field;
    gint xres, yres, kxres, kyres;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &kernel))
        return FALSE;
    if (gwy_data_field_check_compatibility(kernel, field,
                                           GWY_DATA_COMPATIBILITY_LATERAL | GWY_DATA_COMPATIBILITY_MEASURE))
        return FALSE;

    kxres = gwy_data_field_get_xres(kernel);
    kyres = gwy_data_field_get_yres(kernel);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    /* For non-empty result with CONVOLVE_SIZE_CROP we need a sharp inequality. */
    if (gwy_params_get_enum(args->params, PARAM_OUTSIZE) == CONVOLVE_SIZE_CROP)
        return kxres < xres/2 && kyres < yres/2;
    return kxres <= xres && kyres <= yres;
}

static void
execute(ModuleArgs *args)
{
    ConvolveSizeType outsize = gwy_params_get_enum(args->params, PARAM_OUTSIZE);
    GwyExteriorType exterior = gwy_params_get_enum(args->params, PARAM_EXTERIOR);
    gboolean as_integral = gwy_params_get_boolean(args->params, PARAM_AS_INTEGRAL);
    GwyDataField *field = args->field, *kernel = gwy_params_get_image(args->params, PARAM_KERNEL);
    GwyDataField *extfield, *result;
    gint xres, yres, kxres, kyres;

    kxres = gwy_data_field_get_xres(kernel);
    kyres = gwy_data_field_get_yres(kernel);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);

    if (outsize == CONVOLVE_SIZE_EXTEND) {
        /* XXX: This way we extend the field twice.  The extension in gwy_data_field_area_ext_convolve() then just
         * messes things up, for instance for periodic exterior.  So extend it all the way here, use zero-filled
         * exterior and crop. */
        extfield = gwy_data_field_extend(field, kxres, kxres, kyres, kyres, exterior, 0.0, FALSE);
        result = args->result = gwy_data_field_new_alike(extfield, FALSE);
        gwy_data_field_area_ext_convolve(extfield, 0, 0, xres + 2*kxres, yres + 2*kyres,
                                         result, kernel,
                                         GWY_EXTERIOR_FIXED_VALUE, 0.0, as_integral);
        g_object_unref(extfield);
        gwy_data_field_resize(result, kxres/2, kyres/2, xres + 2*kxres - kxres/2, yres + 2*kyres - kyres/2);
    }
    else {
        result = args->result = gwy_data_field_new_alike(field, FALSE);
        gwy_data_field_area_ext_convolve(field, 0, 0, xres, yres,
                                         result, kernel,
                                         exterior, 0.0, as_integral);
        if (outsize == CONVOLVE_SIZE_CROP)
            gwy_data_field_resize(result, kxres/2, kyres/2, xres + kxres/2 - kxres, yres + kyres/2 - kyres);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
