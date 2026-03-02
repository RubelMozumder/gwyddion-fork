/*
 *  $Id: tip_model.c 24686 2022-03-16 16:43:48Z yeti-dn $
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
#include <libprocess/stats.h>
#include <libprocess/tip.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_TIP_TYPE,
    PARAM_NSIDES,
    PARAM_ANGLE,
    PARAM_THETA,
    PARAM_RADIUS,
    PARAM_ANISOTROPY,
    PARAM_SQUARE_TIP,

    INFO_SIZE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *tip;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register             (void);
static GwyParamDef*     define_module_params        (void);
static void             tip_model                   (GwyContainer *data,
                                                     GwyRunType runtype);
static void             execute                     (ModuleArgs *args);
static GwyDialogOutcome run_gui                     (ModuleArgs *args,
                                                     GwyContainer *data,
                                                     gint id);
static void             param_changed               (ModuleGUI *gui,
                                                     gint id);
static void             preview                     (gpointer user_data);
static void             update_parameter_sensitivity(ModuleGUI *gui);

static const struct {
    GwyTipParamType type;
    guint id;
}
tip_param_map[] = {
    { GWY_TIP_PARAM_RADIUS,     PARAM_RADIUS,     },
    { GWY_TIP_PARAM_NSIDES,     PARAM_NSIDES,     },
    { GWY_TIP_PARAM_ROTATION,   PARAM_THETA,      },
    { GWY_TIP_PARAM_SLOPE,      PARAM_ANGLE,      },
    { GWY_TIP_PARAM_ANISOTROPY, PARAM_ANISOTROPY, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Models SPM tip."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, tip_model)

static gboolean
module_register(void)
{
    gwy_process_func_register("tip_model",
                              (GwyProcessFunc)&tip_model,
                              N_("/SPM M_odes/_Tip/_Model Tip..."),
                              GWY_STOCK_TIP_MODEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Model AFM tip"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *tip_types = NULL;
    static GwyParamDef *paramdef = NULL;
    guint i, ntypes;

    if (paramdef)
        return paramdef;

    ntypes = gwy_tip_model_get_npresets();
    tip_types = g_new(GwyEnum, ntypes);
    for (i = 0; i < ntypes; i++) {
        tip_types[i].value = i;
        tip_types[i].name = gwy_tip_model_get_preset_tip_name(gwy_tip_model_get_preset(i));
    }

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_TIP_TYPE, "tip_type", _("_Tip type"), tip_types, ntypes, GWY_TIP_PYRAMID);
    gwy_param_def_add_int(paramdef, PARAM_NSIDES, "nsides", _("_Number of sides"), 3, 24, 4);
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("Tip _slope"), TRUE, 4, 54.73561032*G_PI/180.0);
    gwy_param_def_add_angle(paramdef, PARAM_THETA, "theta", _("Tip _rotation"), FALSE, 1, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RADIUS, "radius", _("Tip _apex radius"), G_MINDOUBLE, G_MAXDOUBLE, 200e-9);
    gwy_param_def_add_double(paramdef, PARAM_ANISOTROPY, "anisotropy", _("Tip _anisotropy"), 0.1, 10.0, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SQUARE_TIP, "square_tip", _("Make tip image square"), TRUE);
    return paramdef;
}

static void
tip_model(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    if (!gwy_require_image_same_units(args.field, data, id, _("Model Tip")))
        return;

    args.params = gwy_params_new_from_settings(define_module_params());
    args.tip = gwy_data_field_new(3, 3, 1.0, 1.0, TRUE);
    gwy_data_field_copy_units(args.field, args.tip);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.tip, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, _("Modeled tip"));
    gwy_app_channel_log_add_proc(data, -1, newid);

end:
    g_object_unref(args.tip);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwySIValueFormat *vf;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->tip);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE, GWY_DATA_ITEM_GRADIENT, 0);

    gui.dialog = gwy_dialog_new(_("Model Tip"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_TIP_TYPE);
    gwy_param_table_append_slider(table, PARAM_NSIDES);
    gwy_param_table_append_slider(table, PARAM_ANGLE);
    gwy_param_table_slider_restrict_range(table, PARAM_ANGLE, 0.1*G_PI/180.0, 89.9*G_PI/180.0);
    gwy_param_table_append_slider(table, PARAM_THETA);
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    vf = gwy_si_unit_get_format(gwy_data_field_get_si_unit_xy(args->field), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                5.0*gwy_data_field_get_dx(args->field), NULL);
    vf->precision++;
    gwy_param_table_slider_set_factor(table, PARAM_RADIUS, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, vf->units);
    gwy_param_table_slider_restrict_range(table, PARAM_RADIUS,
                                          0.1*gwy_data_field_get_dx(args->field),
                                          0.5*gwy_data_field_get_xreal(args->field));
    gwy_param_table_slider_set_mapping(table, PARAM_RADIUS, GWY_SCALE_MAPPING_LOG);
    gwy_si_unit_value_format_free(vf);
    gwy_param_table_append_slider(table, PARAM_ANISOTROPY);
    gwy_param_table_append_checkbox(table, PARAM_SQUARE_TIP);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_info(table, INFO_SIZE, _("Tip resolution"));

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);
    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id < 0 || id == PARAM_TIP_TYPE)
        update_parameter_sensitivity(gui);

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyDataField *tip = args->tip;
    gchar *s;

    execute(args);
    gwy_data_field_data_changed(tip);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SMALL_SIZE);

    s = g_strdup_printf("%d %s × %d %s",
                        gwy_data_field_get_xres(tip), _("px"),
                        gwy_data_field_get_yres(tip), _("px"));
    gwy_param_table_info_set_valuestr(gui->table, INFO_SIZE, s);
    g_free(s);

    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
update_parameter_sensitivity(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    const GwyTipModelPreset *preset = gwy_tip_model_get_preset(gwy_params_get_enum(params, PARAM_TIP_TYPE));
    const GwyTipParamType *tipparams;
    guint i, j, nvalues;

    g_return_if_fail(preset);
    nvalues = gwy_tip_model_get_preset_nparams(preset);
    tipparams = gwy_tip_model_get_preset_params(preset);
    for (j = 0; j < G_N_ELEMENTS(tip_param_map); j++) {
        for (i = 0; i < nvalues; i++) {
            if (tip_param_map[j].type == tipparams[i])
                break;
        }
        gwy_param_table_set_sensitive(gui->table, tip_param_map[j].id, i < nvalues);
    }
}

static gdouble*
fill_tip_params(ModuleArgs *args, gdouble *zrange)
{
    GwyParams *params = args->params;
    const GwyTipModelPreset *preset = gwy_tip_model_get_preset(gwy_params_get_enum(params, PARAM_TIP_TYPE));
    const GwyTipParamType *tipparams;
    guint i, j, nvalues;
    gdouble *values;
    gdouble min, max;

    gwy_data_field_get_min_max(args->field, &min, &max);
    *zrange = max - min;

    g_return_val_if_fail(preset, NULL);
    nvalues = gwy_tip_model_get_preset_nparams(preset);
    tipparams = gwy_tip_model_get_preset_params(preset);
    values = g_new(gdouble, nvalues);
    for (i = 0; i < nvalues; i++) {
        if (tipparams[i] == GWY_TIP_PARAM_HEIGHT) {
            values[i] = *zrange;
            continue;
        }
        for (j = 0; j < G_N_ELEMENTS(tip_param_map); j++) {
            if (tip_param_map[j].type == tipparams[i]) {
                if (tip_param_map[j].id == PARAM_NSIDES)
                    values[i] = gwy_params_get_int(params, tip_param_map[j].id);
                else
                    values[i] = gwy_params_get_double(params, tip_param_map[j].id);
                break;
            }
        }
        if (j == G_N_ELEMENTS(tip_param_map)) {
            g_warning("Unhandled parameter type %u.", tipparams[i]);
            values[i] = 1.0;
        }
    }

    return values;
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    const GwyTipModelPreset *preset = gwy_tip_model_get_preset(gwy_params_get_enum(params, PARAM_TIP_TYPE));
    gboolean square_tip = gwy_params_get_boolean(params, PARAM_SQUARE_TIP);
    GwyDataField *tip = args->tip, *field = args->field;
    gdouble zrange;
    gdouble *values;

    g_return_if_fail(preset);

    /* Ensure the tip field has the same pixel size as the data field. */
    gwy_data_field_set_xreal(tip, gwy_data_field_get_xres(tip)*gwy_data_field_get_dx(field));
    gwy_data_field_set_yreal(tip, gwy_data_field_get_yres(tip)*gwy_data_field_get_dy(field));

    values = fill_tip_params(args, &zrange);
    gwy_tip_model_preset_create_for_zrange(preset, tip, zrange, square_tip, values);
    g_free(values);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
