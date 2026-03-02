/*
 *  $Id: facet-level.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2004-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/level.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_MASKING,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             facet_level         (GwyContainer *data,
                                             GwyRunType run);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static gboolean         execute             (ModuleArgs *args,
                                             GtkWindow *window);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Automatic facet-orientation based leveling. Levels data to make facets point up."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, facet_level)

static gboolean
module_register(void)
{
    gwy_process_func_register("facet-level",
                              (GwyProcessFunc)&facet_level,
                              N_("/_Level/_Facet Level"),
                              GWY_STOCK_FACET_LEVEL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Level data to make facets point upward"));

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
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "mode", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_EXCLUDE);
    return paramdef;
}

static void
facet_level(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GQuark quark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field && quark);

    if (!gwy_require_image_same_units(args.field, data, id, _("Facet Level")))
        return;

    args.result = gwy_data_field_new_alike(args.field, FALSE);
    args.params = gwy_params_new_from_settings(define_module_params());
    if (run != GWY_RUN_IMMEDIATE && args.mask) {
        GwyDialogOutcome outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }
    if (!execute(&args, gwy_app_find_window_for_channel(data, id)))
        goto end;

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_data_field_copy(args.result, args.field, FALSE);
    gwy_app_channel_log_add_proc(data, id, id);
    gwy_data_field_data_changed(args.field);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;

    dialog = GWY_DIALOG(gwy_dialog_new(_("Facet Level")));
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_MASKING);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static gboolean
execute(ModuleArgs *args, GtkWindow *window)
{
    GwyDataField *mask = args->mask, *result = args->result;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    gdouble c, bx, by, b2;
    gdouble p, progress, maxb2 = 666.0, eps = 1e-9;
    gint i;

    gwy_data_field_copy(args->field, result, FALSE);
    /* Converge.  FIXME: this can take a long time. */
    i = 0;
    progress = 0.0;
    gwy_app_wait_start(window, _("Facet-leveling..."));
    while (i < 100) {
        /* Not actually cancelled, but do not save undo */
        if (!gwy_data_field_fit_facet_plane(result, mask, masking, &c, &bx, &by))
            break;
        gwy_data_field_plane_level(result, c, bx, by);
        bx /= gwy_data_field_get_dx(result);
        by /= gwy_data_field_get_dy(result);
        b2 = bx*bx + by*by;
        if (!i)
            maxb2 = MAX(b2, eps);
        if (b2 < eps) {
            i = 100;
            break;
        }
        i++;
        p = log(b2/maxb2)/log(eps/maxb2);
        gwy_debug("progress = %f, p = %f, ip = %f", progress, p, i/100.0);
        /* never decrease progress, that would look silly */
        progress = MAX(progress, p);
        progress = MAX(progress, i/100.0);
        if (!gwy_app_wait_set_fraction(progress))
            break;
    }

    gwy_app_wait_finish();
    return i == 100;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
