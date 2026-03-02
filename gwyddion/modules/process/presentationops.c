/*
 *  $Id: presentationops.c 25638 2023-09-07 12:23:28Z yeti-dn $
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
#include <libprocess/arithmetic.h>
#include <libprocess/filters.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define PRESENTATIONOPS_RUN_MODES GWY_RUN_IMMEDIATE
#define PRESENTATION_ATTACH_RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_SOURCE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *target;
} ModuleArgs;

static gboolean         module_register      (void);
static GwyParamDef*     define_attach_params (void);
static void             presentation_remove  (GwyContainer *data,
                                              GwyRunType runtype);
static void             presentation_extract (GwyContainer *data,
                                              GwyRunType runtype);
static void             presentation_logscale(GwyContainer *data,
                                              GwyRunType runtype);
static void             presentation_attach  (GwyContainer *data,
                                              GwyRunType runtype);
static GwyDialogOutcome run_attach_gui       (ModuleArgs *args);
static gboolean         attach_source_filter (GwyContainer *source,
                                              gint id,
                                              gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Basic operations with presentation: extraction, removal."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, presentationops)

static GwyParamDef*
define_attach_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_SOURCE, "source", _("_Data to attach"));
    return paramdef;
}

static gboolean
module_register(void)
{
    gwy_process_func_register("presentation_remove",
                              (GwyProcessFunc)&presentation_remove,
                              N_("/_Presentation/_Remove Presentation"),
                              NULL,
                              PRESENTATIONOPS_RUN_MODES,
                              GWY_MENU_FLAG_DATA_SHOW | GWY_MENU_FLAG_DATA,
                              N_("Remove presentation from data"));
    gwy_process_func_register("presentation_extract",
                              (GwyProcessFunc)&presentation_extract,
                              N_("/_Presentation/E_xtract Presentation"),
                              NULL,
                              PRESENTATIONOPS_RUN_MODES,
                              GWY_MENU_FLAG_DATA_SHOW | GWY_MENU_FLAG_DATA,
                              N_("Extract presentation to a new image"));
    gwy_process_func_register("presentation_attach",
                              (GwyProcessFunc)&presentation_attach,
                              N_("/_Presentation/_Attach Presentation..."),
                              NULL,
                              PRESENTATION_ATTACH_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Attach another data field as presentation"));
    gwy_process_func_register("presentation_logscale",
                              (GwyProcessFunc)&presentation_logscale,
                              N_("/_Presentation/_Logscale"),
                              NULL,
                              PRESENTATIONOPS_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Creates a presentation with logarithmic color scale"));

    return TRUE;
}

static void
presentation_remove(GwyContainer *data, GwyRunType runtype)
{
    GQuark quark;
    gint id;

    g_return_if_fail(runtype & PRESENTATIONOPS_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_SHOW_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(quark);
    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_container_remove(data, quark);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
presentation_extract(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfield;
    GQuark quark;
    gint oldid, newid;

    g_return_if_fail(runtype & PRESENTATIONOPS_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &oldid,
                                     GWY_APP_SHOW_FIELD_KEY, &quark,
                                     GWY_APP_SHOW_FIELD, &dfield,
                                     0);
    g_return_if_fail(dfield && quark);

    dfield = gwy_data_field_duplicate(dfield);
    newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);
    g_object_unref(dfield);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
    gwy_app_set_data_field_title(data, newid, NULL);
    gwy_app_channel_log_add_proc(data, oldid, newid);
}

static void
presentation_logscale(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfield, *sfield;
    GQuark squark;
    gdouble *d;
    gdouble min, max, m0;
    gint xres, yres, i, zeroes, id;

    g_return_if_fail(runtype & PRESENTATIONOPS_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     GWY_APP_SHOW_FIELD, &sfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(dfield && squark);

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    gwy_app_undo_qcheckpointv(data, 1, &squark);
    if (!sfield) {
        sfield = gwy_data_field_duplicate(dfield);
        gwy_container_pass_object(data, squark, sfield);
    }
    else {
        gwy_data_field_resample(sfield, xres, yres, GWY_INTERPOLATION_NONE);
        gwy_data_field_copy(dfield, sfield, FALSE);
    }

    d = gwy_data_field_get_data(sfield);
    zeroes = 0;
    max = 0;
    min = G_MAXDOUBLE;
    for (i = 0; i < xres*yres; i++) {
        d[i] = ABS(d[i]);
        if (G_UNLIKELY(d[i] > max))
            max = d[i];
        if (d[i] == 0.0)
            zeroes++;
        else if (G_UNLIKELY(d[i] < min))
            min = d[i];
    }
    if (min == max || zeroes == xres*yres)
        return;

    if (!zeroes) {
        for (i = 0; i < xres*yres; i++)
            d[i] = log(d[i]);
    }
    else {
        m0 = log(min) - log(max/min)/512.0;
        for (i = 0; i < xres*yres; i++)
            d[i] = d[i] ? log(d[i]) : m0;
    }

    gwy_data_field_data_changed(sfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
presentation_attach(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyDataField *dfield;
    ModuleArgs args;
    GQuark squark;
    gint id;

    g_return_if_fail(runtype & PRESENTATION_ATTACH_RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.target,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);

    args.params = gwy_params_new_from_settings(define_attach_params());
    outcome = run_attach_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    dfield = gwy_data_field_duplicate(args.target);
    gwy_app_undo_qcheckpointv(data, 1, &squark);
    gwy_container_pass_object(data, squark, dfield);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_attach_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Attach Presentation")));
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_SOURCE);
    gwy_param_table_data_id_set_filter(table, PARAM_SOURCE, attach_source_filter, args->target, NULL);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static gboolean
attach_source_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *source, *target = (GwyDataField*)user_data;

    source = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    return !gwy_data_field_check_compatibility(source, target,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
