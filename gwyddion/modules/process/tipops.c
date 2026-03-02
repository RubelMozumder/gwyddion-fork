/*
 *  $Id: tipops.c 24682 2022-03-15 17:33:44Z yeti-dn $
 *  Copyright (C) 2004-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/arithmetic.h>
#include <libprocess/tip.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

typedef enum {
    DILATION,
    EROSION,
    CERTAINTY_MAP
} TipOperation;

enum {
    PARAM_TIP,

    MESSAGE_RESAMPLING,
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
static void             tipops              (GwyContainer *data,
                                             GwyRunType runtype,
                                             const gchar *name);
static gboolean         execute             (ModuleArgs *args,
                                             TipOperation op,
                                             GtkWindow *wait_window);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             TipOperation op);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         tip_image_filter    (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Tip operations: dilation (convolution), erosion (reconstruction) and certainty map."),
    "Petr Klapetek <klapetek@gwyddion.net>, Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, tipops)

static gboolean
module_register(void)
{
    gwy_process_func_register("tip_dilation",
                              &tipops,
                              N_("/SPM M_odes/_Tip/_Dilation..."),
                              GWY_STOCK_TIP_DILATION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Surface dilation by defined tip"));
    gwy_process_func_register("tip_reconstruction",
                              &tipops,
                              N_("/SPM M_odes/_Tip/_Surface Reconstruction..."),
                              GWY_STOCK_TIP_EROSION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Surface reconstruction by defined tip"));
    gwy_process_func_register("tip_map",
                              &tipops,
                              N_("/SPM M_odes/_Tip/_Certainty Map..."),
                              GWY_STOCK_TIP_MAP,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Tip certainty map"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "tipops");    /* Share tip settings. */
    gwy_param_def_add_image_id(paramdef, PARAM_TIP, "tip", _("_Tip morphology"));
    return paramdef;
}

static void
tipops(GwyContainer *data,
       GwyRunType runtype,
       const gchar *name)
{
    static const GwyEnum ops[] = {
        { "tip_dilation",       DILATION,      },
        { "tip_reconstruction", EROSION,       },
        { "tip_map",            CERTAINTY_MAP, },
    };
    static const GwyEnum data_titles[] = {
        { N_("Dilated data"),           DILATION,      },
        { N_("Surface reconstruction"), EROSION,       },
    };
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    TipOperation op;
    ModuleArgs args;
    gint id, newid;
    GQuark quark;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    if ((op = gwy_string_to_enum(name, ops, G_N_ELEMENTS(ops))) == (TipOperation)-1) {
        g_warning("tipops does not provide function `%s'", name);
        return;
    }

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args, op);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (!execute(&args, op, gwy_app_find_window_for_channel(data, id)))
        goto end;

    if (op == DILATION || op == EROSION) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_MASK_COLOR,
                                0);
        gwy_app_set_data_field_title(data, newid, gwy_enum_to_string(op, data_titles, G_N_ELEMENTS(data_titles)));
        gwy_app_channel_log_add_proc(data, id, newid);
    }
    else {
        quark = gwy_app_get_mask_key_for_id(id);
        gwy_app_undo_qcheckpointv(data, 1, &quark);
        gwy_container_set_object(data, quark, args.result);
        gwy_app_channel_log_add_proc(data, id, id);
    }

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, TipOperation op)
{
    static const GwyEnum titles[] = {
        { N_("Tip Dilation"),           DILATION,      },
        { N_("Surface Reconstruction"), EROSION,       },
        { N_("Certainty Map Analysis"), CERTAINTY_MAP, },
    };
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(gwy_enum_to_string(op, titles, G_N_ELEMENTS(titles)));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_TIP);
    gwy_param_table_data_id_set_filter(table, PARAM_TIP, tip_image_filter, args->field, NULL);
    gwy_param_table_append_message(table, MESSAGE_RESAMPLING, NULL);
    gwy_param_table_message_set_type(table, MESSAGE_RESAMPLING, GTK_MESSAGE_WARNING);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static gboolean
tip_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *tip = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    if (gwy_data_field_get_xreal(tip) > gwy_data_field_get_xreal(field)/4
        || gwy_data_field_get_yreal(tip) > gwy_data_field_get_yreal(field)/4)
        return FALSE;

    return !gwy_data_field_check_compatibility(tip, field,
                                               GWY_DATA_COMPATIBILITY_LATERAL | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_TIP) {
        if (gwy_params_data_id_is_none(params, PARAM_TIP))
            gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, FALSE);
        else {
            GwyDataField *tip = gwy_params_get_image(params, PARAM_TIP), *field = args->field;

            if (!gwy_data_field_check_compatibility(tip, field, GWY_DATA_COMPATIBILITY_MEASURE))
                gwy_param_table_set_label(gui->table, MESSAGE_RESAMPLING, NULL);
            else {
                gint xres = GWY_ROUND(gwy_data_field_get_xreal(tip)/gwy_data_field_get_dx(field));
                gint yres = GWY_ROUND(gwy_data_field_get_yreal(tip)/gwy_data_field_get_dy(field));
                gchar *s;

                xres = MAX(xres, 1);
                yres = MAX(yres, 1);
                s = g_strdup_printf(_("Tip pixel size does not match data.\n"
                                      "It will be resampled from %d×%d to %d×%d."),
                                    gwy_data_field_get_xres(tip), gwy_data_field_get_yres(tip), xres, yres);
                gwy_param_table_set_label(gui->table, MESSAGE_RESAMPLING, s);
                g_free(s);
            }
        }
    }
}

static gboolean
execute(ModuleArgs *args, TipOperation op, GtkWindow *wait_window)
{
    GwyDataField *field = args->field, *result, *tip = gwy_params_get_image(args->params, PARAM_TIP);
    gboolean ok = FALSE;

    g_return_val_if_fail(tip, FALSE);
    result = args->result = gwy_data_field_new_alike(field, FALSE);

    gwy_app_wait_start(wait_window, _("Initializing..."));
    if (op == DILATION)
        ok = !!gwy_tip_dilation(tip, field, result, gwy_app_wait_set_fraction, gwy_app_wait_set_message);
    else if (op == EROSION)
        ok = !!gwy_tip_erosion(tip, field, result, gwy_app_wait_set_fraction, gwy_app_wait_set_message);
    else if (op == CERTAINTY_MAP)
        ok = !!gwy_tip_cmap(tip, field, result, gwy_app_wait_set_fraction, gwy_app_wait_set_message);
    else {
        g_assert_not_reached();
    }
    gwy_app_wait_finish();

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
