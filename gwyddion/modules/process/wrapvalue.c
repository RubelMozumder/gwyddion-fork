/*
 *  $Id: wrapvalue.c 24630 2022-03-03 17:00:41Z yeti-dn $
 *  Copyright (C) 2019-2022 David Necas (Yeti)
 *  E-mail: yeti@gwyddion.net
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
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    DATA_UNIT_DEG,
    DATA_UNIT_UNITLESS,
    DATA_UNIT_OTHER,
} DataUnitType;

typedef enum {
    WRAP_VALUE_RANGE_USER,
    WRAP_VALUE_RANGE_360_DEG,
    WRAP_VALUE_RANGE_180_DEG,
    WRAP_VALUE_RANGE_2PI,
    WRAP_VALUE_RANGE_PI,
    WRAP_VALUE_RANGE_1,
    WRAP_VALUE_RANGE_KEEP,
} WrapValueRangeType;

enum {
    PARAM_OFFSET,
    PARAM_RANGE,
    PARAM_TYPE_DEG,
    PARAM_TYPE_UNITLESS,
    PARAM_TYPE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    /* Cached input data properties. */
    gdouble min;
    gdouble max;
    DataUnitType unit_type;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             wrap_value          (GwyContainer *data,
                                             GwyRunType runtype);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static gdouble          get_range           (ModuleArgs *args);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Wraps periodic values to a different range."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, wrapvalue)

static gboolean
module_register(void)
{
    gwy_process_func_register("wrapvalue",
                              (GwyProcessFunc)&wrap_value,
                              N_("/_Basic Operations/_Wrap Value..."),
                              GWY_STOCK_WRAP_VALUE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Rewrap periodic values"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum deg_types[] = {
        { N_("360 deg"),         WRAP_VALUE_RANGE_360_DEG, },
        { N_("180 deg"),         WRAP_VALUE_RANGE_180_DEG, },
        { N_("_Keep unchanged"), WRAP_VALUE_RANGE_KEEP,    },
        { N_("Specify _range"),  WRAP_VALUE_RANGE_USER,    },
    };
    static const GwyEnum unitless_types[] = {
        { N_("2π"),              WRAP_VALUE_RANGE_2PI,  },
        { N_("π"),               WRAP_VALUE_RANGE_PI,   },
        { N_("1"),               WRAP_VALUE_RANGE_1,    },
        { N_("_Keep unchanged"), WRAP_VALUE_RANGE_KEEP, },
        { N_("Specify _range"),  WRAP_VALUE_RANGE_USER, },
    };
    static const GwyEnum types[] = {
        { N_("_Keep unchanged"), WRAP_VALUE_RANGE_KEEP, },
        { N_("Specify _range"),  WRAP_VALUE_RANGE_USER, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_OFFSET, "offset", _("O_ffset"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGE, "range", _("Specify _range"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE, "type", _("Value range"),
                              types, G_N_ELEMENTS(types), WRAP_VALUE_RANGE_KEEP);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE_DEG, "type-deg", _("Value range"),
                              deg_types, G_N_ELEMENTS(deg_types), WRAP_VALUE_RANGE_KEEP);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TYPE_UNITLESS, "type-unitless", _("Value range"),
                              unitless_types, G_N_ELEMENTS(unitless_types), WRAP_VALUE_RANGE_KEEP);
    return paramdef;
}

static void
wrap_value(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark quark;
    GwySIUnit *unit;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     0);
    g_return_if_fail(args.field);
    gwy_data_field_get_min_max(args.field, &args.min, &args.max);
    unit = gwy_data_field_get_si_unit_z(args.field);
    if (gwy_si_unit_equal_string(unit, "deg"))
        args.unit_type = DATA_UNIT_DEG;
    else if (gwy_si_unit_equal_string(unit, NULL))
        args.unit_type = DATA_UNIT_UNITLESS;
    else
        args.unit_type = DATA_UNIT_OTHER;

    args.result = gwy_data_field_new_alike(args.field, TRUE);

    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
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

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    GwySIValueFormat *vf;
    gdouble range = get_range(args);

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    vf = gwy_data_field_get_value_format_z(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    gui.dialog = gwy_dialog_new(_("Wrap Value"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    if (args->unit_type == DATA_UNIT_DEG)
        gwy_param_table_append_radio(table, PARAM_TYPE_DEG);
    else if (args->unit_type == DATA_UNIT_UNITLESS)
        gwy_param_table_append_radio(table, PARAM_TYPE_UNITLESS);
    else
        gwy_param_table_append_radio(table, PARAM_TYPE);

    gwy_param_table_append_entry(table, PARAM_RANGE);
    gwy_param_table_entry_set_value_format(table, PARAM_RANGE, vf);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_slider(table, PARAM_OFFSET);
    gwy_param_table_slider_set_factor(table, PARAM_OFFSET, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_OFFSET, vf->units);
    /* Must do this here, otherwise the entry tries to accommodate G_MAXDOUBLE values. */
    gwy_param_table_slider_restrict_range(gui.table, PARAM_OFFSET, -range, range);

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    gwy_si_unit_value_format_free(vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    WrapValueRangeType type = WRAP_VALUE_RANGE_USER;
    gboolean changing_type = FALSE, changing_range = FALSE;

    if ((id < 0 && args->unit_type == DATA_UNIT_DEG) || id == PARAM_TYPE_DEG) {
        type = gwy_params_get_enum(params, PARAM_TYPE_DEG);
        changing_type = TRUE;
    }
    if ((id < 0 && args->unit_type == DATA_UNIT_UNITLESS) || id == PARAM_TYPE_UNITLESS) {
        type = gwy_params_get_enum(params, PARAM_TYPE_UNITLESS);
        changing_type = TRUE;
    }
    if ((id < 0 && args->unit_type == DATA_UNIT_OTHER) || id == PARAM_TYPE) {
        type = gwy_params_get_enum(params, PARAM_TYPE);
        changing_type = TRUE;
    }

    if (changing_type) {
        gwy_param_table_set_sensitive(gui->table, PARAM_RANGE, type == WRAP_VALUE_RANGE_USER);
        changing_range = TRUE;
    }

    if (id < 0 || id == PARAM_RANGE || changing_range) {
        gdouble range = get_range(args);
        gwy_param_table_slider_restrict_range(gui->table, PARAM_OFFSET, -range, range);
    }

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *result = args->result;
    gdouble offset = gwy_params_get_double(args->params, PARAM_OFFSET);
    gdouble range = get_range(args);
    gdouble *d;
    gint n, i;

    if (!(range > 0.0)) {
        gwy_data_field_clear(result);
        return;
    }

    gwy_data_field_copy(field, result, FALSE);
    n = gwy_data_field_get_xres(result)*gwy_data_field_get_yres(result);
    d = gwy_data_field_get_data(result);
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(d,n,offset,range)
#endif
    for (i = 0; i < n; i++) {
        d[i] = fmod(d[i] - offset, range);
        if (d[i] < 0.0)
            d[i] += range;
        d[i] += offset;
    }
}

static gdouble
get_range(ModuleArgs *args)
{
    /* NB: USER is unused and KEEP must be handled specially */
    static const gdouble ranges[] = {
        0.0, 360.0, 180.0, 2.0*G_PI, G_PI, 1.0, 0.0,
    };

    GwyParams *params = args->params;
    WrapValueRangeType type;

    if (args->unit_type == DATA_UNIT_DEG)
        type = gwy_params_get_enum(params, PARAM_TYPE_DEG);
    else if (args->unit_type == DATA_UNIT_UNITLESS)
        type = gwy_params_get_enum(params, PARAM_TYPE_UNITLESS);
    else
        type = gwy_params_get_enum(params, PARAM_TYPE);

    if (type == WRAP_VALUE_RANGE_USER)
        return gwy_params_get_double(params, PARAM_RANGE);
    if (type == WRAP_VALUE_RANGE_KEEP)
        return args->max - args->min;

    return ranges[type];
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble r = args->max - args->min;
    gdouble range = get_range(args);
    gdouble offset = gwy_params_get_double(params, PARAM_OFFSET);

    /* If remembered range differs wildly from data range, try to guess something of reasonable order of magnitude... */
    if (!r)
        gwy_params_set_double(params, PARAM_RANGE, (range = 1.0));
    else if (!range || !(fabs(log(r/range)) < 2.0))
        gwy_params_set_double(params, PARAM_RANGE, (range = r));

    gwy_params_set_double(params, PARAM_OFFSET, CLAMP(offset, -range, range));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
