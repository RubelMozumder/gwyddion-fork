/*
 *  $Id: hough.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <libprocess/hough.h>
#include <libprocess/filters.h>
#include <libprocess/cwt.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    GWY_HOUGH_OUTPUT_LINE   = 0,
    GWY_HOUGH_OUTPUT_CIRCLE = 1
} GwyHoughOutputType;

enum {
    PARAM_CIRCLE_SIZE,
    PARAM_OUTPUT,
};

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             hough               (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (GwyParams *params);
static void             param_changed       (GwyParamTable *table,
                                             gint id);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Hough transform."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, hough)

static gboolean
module_register(void)
{
    gwy_process_func_register("hough",
                              (GwyProcessFunc)&hough,
                              N_("/_Integral Transforms/_Hough..."),
                              GWY_STOCK_HOUGH,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Compute Hough transform"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum hough_outputs[] = {
        { N_("Line"),   GWY_HOUGH_OUTPUT_LINE,   },
        { N_("Circle"), GWY_HOUGH_OUTPUT_CIRCLE, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_int(paramdef, PARAM_CIRCLE_SIZE, "circle_size", _("_Circle size"), 1, 240, 10);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "output", _("_Transform type"),
                              hough_outputs, G_N_ELEMENTS(hough_outputs), GWY_HOUGH_OUTPUT_LINE);
    return paramdef;
}

static void
hough(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield, *edgefield, *result, *f1, *f2;
    GwySIUnit *siunit;
    GwyParams *params;
    GwyHoughOutputType output;
    gint id, newid, circle_size;
    gchar *title;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield);

    params = gwy_params_new_from_settings(define_module_params());
    if (run == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(params);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED) {
            g_object_unref(params);
            return;
        }
    }

    output = gwy_params_get_enum(params, PARAM_OUTPUT);
    circle_size = gwy_params_get_int(params, PARAM_CIRCLE_SIZE);

    result = gwy_data_field_new_alike(dfield, FALSE);
    siunit = gwy_si_unit_new(NULL);
    gwy_data_field_set_si_unit_z(result, siunit);
    g_object_unref(siunit);

    edgefield = gwy_data_field_duplicate(dfield);

    f1 = gwy_data_field_duplicate(dfield);
    f2 = gwy_data_field_duplicate(dfield);

    gwy_data_field_filter_canny(edgefield, 0.1);
    gwy_data_field_filter_sobel(f1, GWY_ORIENTATION_HORIZONTAL);
    gwy_data_field_filter_sobel(f2, GWY_ORIENTATION_VERTICAL);
    if (output == GWY_HOUGH_OUTPUT_LINE) {
        title = g_strdup(_("Hough line"));
        gwy_data_field_hough_line(edgefield, f1, f2, result, 1, FALSE);
    }
    else  {
        title = g_strdup_printf(_("Hough circle r=%d"), circle_size);
        gwy_data_field_hough_circle(edgefield, f1, f2, result, circle_size);
    }

    newid = gwy_app_data_browser_add_data_field(result, data, TRUE);
    g_object_unref(result);

    gwy_app_set_data_field_title(data, newid, title);
    gwy_data_field_data_changed(result);
    g_object_unref(edgefield);
    g_object_unref(f1);
    g_object_unref(f2);
    g_object_unref(params);
    g_free(title);

    gwy_app_channel_log_add_proc(data, id, newid);
}

static GwyDialogOutcome
run_gui(GwyParams *params)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Hough Transform")));
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(params);
    gwy_param_table_append_radio(table, PARAM_OUTPUT);
    gwy_param_table_append_slider(table, PARAM_CIRCLE_SIZE);
    gwy_param_table_set_unitstr(table, PARAM_CIRCLE_SIZE, _("px"));

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);
    g_signal_connect(table, "param-changed", G_CALLBACK(param_changed), NULL);

    return gwy_dialog_run(dialog);
}

static void
param_changed(GwyParamTable *table, gint id)
{
    GwyParams *params = gwy_param_table_params(table);

    if (id < 0 || id == PARAM_OUTPUT) {
        GwyHoughOutputType output = gwy_params_get_enum(params, PARAM_OUTPUT);
        gwy_param_table_set_sensitive(table, PARAM_CIRCLE_SIZE, output == GWY_HOUGH_OUTPUT_CIRCLE);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
