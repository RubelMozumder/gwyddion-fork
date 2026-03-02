/*
 *  $Id: mask_morph.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/elliptic.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

typedef enum {
    MASKMORPH_EROSION     = 0,
    MASKMORPH_DILATION    = 1,
    MASKMORPH_OPENING     = 2,
    MASKMORPH_CLOSING     = 3,
    MASKMORPH_ASF_OPENING = 4,
    MASKMORPH_ASF_CLOSING = 5,
} MaskMorphOperation;

typedef enum {
    MASKMORPH_USER_KERNEL = 0,
    MASKMORPH_DISC        = 1,
    MASKMORPH_OCTAGON     = 2,
    MASKMORPH_SQUARE      = 3,
    MASKMORPH_DIAMOND     = 4,
} MaskMorphShapeType;

enum {
    PARAM_MODE,
    PARAM_SHAPE,
    PARAM_RADIUS,
    PARAM_CROP_KERNEL,
    PARAM_KERNEL,
    PARAM_MASK_COLOR,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             mask_morph          (GwyContainer *data,
                                             GwyRunType run);
static void             execute             (ModuleArgs *args);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static gboolean         kernel_filter       (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static void             sanitise_params     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performs basic morphological operations with masks."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, mask_morph)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_morph",
                              (GwyProcessFunc)&mask_morph,
                              N_("/_Mask/Morpho_logical Operation..."),
                              GWY_STOCK_MASK_MORPH,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Morphological operation with mask"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum operations[] = {
        { N_("Erosion"),        MASKMORPH_EROSION,     },
        { N_("Dilation"),       MASKMORPH_DILATION,    },
        { N_("filter|Opening"), MASKMORPH_OPENING,     },
        { N_("filter|Closing"), MASKMORPH_CLOSING,     },
        { N_("ASF Opening"),    MASKMORPH_ASF_OPENING, },
        { N_("ASF Closing"),    MASKMORPH_ASF_CLOSING, },
    };
    static const GwyEnum shapes[] = {
        { N_("Disc"),         MASKMORPH_DISC,        },
        { N_("Octagon"),      MASKMORPH_OCTAGON,     },
        { N_("Square"),       MASKMORPH_SQUARE,      },
        { N_("Diamond"),      MASKMORPH_DIAMOND,     },
        { N_("Another mask"), MASKMORPH_USER_KERNEL, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("Operation"),
                              operations, G_N_ELEMENTS(operations), MASKMORPH_OPENING);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SHAPE, "shape", _("Structuring element"),
                              shapes, G_N_ELEMENTS(shapes), MASKMORPH_DISC);
    gwy_param_def_add_int(paramdef, PARAM_RADIUS, "radius", _("Radius"), 1, 1025, 5);
    gwy_param_def_add_boolean(paramdef, PARAM_CROP_KERNEL, "crop_kernel", _("_Trim empty borders"), TRUE);
    gwy_param_def_add_image_id(paramdef, PARAM_KERNEL, "kernel", _("_Mask"));
    gwy_param_def_add_mask_color(paramdef, PARAM_MASK_COLOR, NULL, NULL);
    return paramdef;
}

static void
mask_morph(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GQuark mquark;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.mask);

    args.result = gwy_data_field_new_alike(args.mask, TRUE);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    if (gwy_data_field_get_max(args.result) > 0.0)
        gwy_container_set_object(data, mquark, args.result);
    else
        gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();

    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_object_by_name(gui.data, "/0/mask", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Morphological Operation"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, TRUE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio(table, PARAM_MODE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_SHAPE);
    gwy_param_table_append_slider(table, PARAM_RADIUS);
    gwy_param_table_set_unitstr(table, PARAM_RADIUS, _("px"));
    gwy_param_table_append_image_id(table, PARAM_KERNEL);
    gwy_param_table_data_id_set_filter(table, PARAM_KERNEL, kernel_filter, args->mask, NULL);
    gwy_param_table_append_checkbox(table, PARAM_CROP_KERNEL);
    gwy_param_table_append_mask_color(table, PARAM_MASK_COLOR, gui.data, 0, data, id);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;
    MaskMorphOperation operation = gwy_params_get_enum(params, PARAM_MODE);
    MaskMorphShapeType shape = gwy_params_get_enum(params, PARAM_SHAPE);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_KERNEL);
    gboolean is_user_kernel = (shape == MASKMORPH_USER_KERNEL);
    gboolean needs_builtin = (operation == MASKMORPH_ASF_OPENING || operation == MASKMORPH_ASF_CLOSING);

    if (is_user_kernel) {
        if ((id < 0 && is_none) || needs_builtin) {
            needs_builtin = FALSE;
            is_user_kernel = FALSE;
            gwy_param_table_set_enum(table, PARAM_SHAPE, (shape = MASKMORPH_DISC));
        }
    }

    gwy_param_table_set_sensitive(table, PARAM_RADIUS, !is_user_kernel);
    gwy_param_table_set_sensitive(table, PARAM_KERNEL, is_user_kernel);
    gwy_param_table_set_sensitive(table, PARAM_CROP_KERNEL, is_user_kernel);
    gwy_param_table_radio_set_sensitive(table, PARAM_SHAPE, MASKMORPH_USER_KERNEL, !needs_builtin);
    /* XXX: Shouldn't the above code ensure this does not happen? */
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !is_user_kernel || !is_none);
    if (id != PARAM_MASK_COLOR)
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

static GwyDataField*
create_kernel(MaskMorphShapeType shape, gint radius)
{
    GwyDataField *kernel;
    gint i, j, res;
    gdouble *d;

    res = 2*radius + 1;
    kernel = gwy_data_field_new(res, res, res, res, TRUE);
    if (shape == MASKMORPH_DISC)
        gwy_data_field_elliptic_area_fill(kernel, 0, 0, res, res, 1.0);
    else if (shape == MASKMORPH_OCTAGON || shape == MASKMORPH_DIAMOND) {
        gint rlim = (shape == MASKMORPH_OCTAGON ? GWY_ROUND(res/G_SQRT2) : radius);
        d = gwy_data_field_get_data(kernel);
        for (i = 0; i < res; i++) {
            gint ii = ABS(i - radius);
            for (j = 0; j < res; j++) {
                gint jj = ABS(j - radius);
                if (ii + jj <= rlim)
                    d[i*res + j] = 1.0;
            }
        }
    }
    else if (shape == MASKMORPH_SQUARE)
        gwy_data_field_fill(kernel, 1.0);
    else {
        g_assert_not_reached();
    }

    return kernel;
}

static void
execute(ModuleArgs *args)
{
    static struct {
        GwyMinMaxFilterType filtertype;
        MaskMorphOperation operation;
    }
    operation_map[] = {
        { GWY_MIN_MAX_FILTER_EROSION,  MASKMORPH_EROSION,  },
        { GWY_MIN_MAX_FILTER_DILATION, MASKMORPH_DILATION, },
        { GWY_MIN_MAX_FILTER_OPENING,  MASKMORPH_OPENING,  },
        { GWY_MIN_MAX_FILTER_CLOSING,  MASKMORPH_CLOSING,  },
    };

    GwyDataField *kernel, *mask = args->result;
    GwyParams *params = args->params;
    MaskMorphOperation operation = gwy_params_get_enum(params, PARAM_MODE);
    MaskMorphShapeType shape = gwy_params_get_enum(params, PARAM_SHAPE);
    guint radius = gwy_params_get_int(params, PARAM_RADIUS);
    gboolean crop_kernel = gwy_params_get_boolean(params, PARAM_CROP_KERNEL);
    GwyMinMaxFilterType filtertype1, filtertype2;
    guint i, xres, yres;

    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    gwy_data_field_copy(args->mask, mask, FALSE);
    for (i = 0; i < G_N_ELEMENTS(operation_map); i++) {
        if (operation_map[i].operation != operation)
            continue;

        if (shape == MASKMORPH_USER_KERNEL) {
            if (!(kernel = gwy_params_get_mask(params, PARAM_KERNEL)))
                continue;
            kernel = gwy_data_field_duplicate(kernel);
            if (crop_kernel)
                gwy_data_field_grains_autocrop(kernel, FALSE, NULL, NULL, NULL, NULL);
        }
        else
            kernel = create_kernel(shape, radius);

        gwy_data_field_area_filter_min_max(mask, kernel, operation_map[i].filtertype, 0, 0, xres, yres);
        g_object_unref(kernel);
        return;
    }

    g_return_if_fail(operation == MASKMORPH_ASF_OPENING || operation == MASKMORPH_ASF_CLOSING);

    /* We can get here by repeating the operation or module call. */
    if (shape == MASKMORPH_USER_KERNEL)
        return;

    if (shape == MASKMORPH_DISC) {
        gwy_data_field_area_filter_disc_asf(mask, radius, operation == MASKMORPH_ASF_CLOSING, 0, 0, xres, yres);
        return;
    }

    if (operation == MASKMORPH_ASF_CLOSING) {
        filtertype1 = GWY_MIN_MAX_FILTER_OPENING;
        filtertype2 = GWY_MIN_MAX_FILTER_CLOSING;
    }
    else {
        filtertype1 = GWY_MIN_MAX_FILTER_CLOSING;
        filtertype2 = GWY_MIN_MAX_FILTER_OPENING;
    }

    for (i = 1; i <= radius; i++) {
        kernel = create_kernel(shape, i);
        gwy_data_field_area_filter_min_max(mask, kernel, filtertype1, 0, 0, xres, yres);
        gwy_data_field_area_filter_min_max(mask, kernel, filtertype2, 0, 0, xres, yres);
        g_object_unref(kernel);
    }
}

static gboolean
kernel_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *kernel, *mask = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_mask_key_for_id(id), &kernel))
        return FALSE;
    if (kernel->xres > mask->xres/2 || kernel->yres > mask->yres/2)
        return FALSE;

    return TRUE;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    MaskMorphShapeType shape = gwy_params_get_enum(params, PARAM_SHAPE);
    GwyAppDataId kernel = gwy_params_get_data_id(params, PARAM_KERNEL);
    gboolean is_none = gwy_params_data_id_is_none(params, PARAM_KERNEL);

    if (shape == MASKMORPH_USER_KERNEL
        && (is_none || !kernel_filter(gwy_app_data_browser_get(kernel.datano), kernel.id, args->mask)))
        gwy_params_reset(params, PARAM_SHAPE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
