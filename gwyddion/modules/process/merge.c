/*
 *  $Id: merge.c 23666 2021-05-10 21:56:10Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correlation.h>
#include <libprocess/hough.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

typedef enum {
    GWY_MERGE_DIRECTION_UP,
    GWY_MERGE_DIRECTION_DOWN,
    GWY_MERGE_DIRECTION_RIGHT,
    GWY_MERGE_DIRECTION_LEFT,
} GwyMergeDirectionType;

typedef enum {
    GWY_MERGE_MODE_CORRELATE,
    GWY_MERGE_MODE_JOIN,
    GWY_MERGE_MODE_NONE,
} GwyMergeModeType;

typedef enum {
    GWY_MERGE_BOUNDARY_FIRST,
    GWY_MERGE_BOUNDARY_SECOND,
    GWY_MERGE_BOUNDARY_AVERAGE,
    GWY_MERGE_BOUNDARY_INTERPOLATE,
} GwyMergeBoundaryType;

enum {
    PARAM_OHTER_IMAGE,
    PARAM_DIRECTION,
    PARAM_MODE,
    PARAM_BOUNDARY,
    PARAM_CROP_TO_RECTANGLE,
    PARAM_CREATE_MASK,
};

typedef struct {
    gint x;
    gint y;
    gint width;
    gint height;
} GwyRectangle;

typedef struct {
    gint x;
    gint y;
} GwyCoord;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
    GwyDataField *result_mask;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             merge                (GwyContainer *data,
                                              GwyRunType runtype);
static GwyDialogOutcome run_gui              (ModuleArgs *args);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static gboolean         other_image_filter   (GwyContainer *data,
                                              gint id,
                                              gpointer user_data);
static void             execute_correlate    (ModuleArgs *args);
static void             execute_join         (ModuleArgs *args);
static void             execute_none         (ModuleArgs *args);
static gdouble          get_row_difference   (GwyDataField *field1,
                                              gint col1,
                                              gint row1,
                                              GwyDataField *field2,
                                              gint col2,
                                              gint row2,
                                              gint width,
                                              gint height);
static gdouble          get_column_difference(GwyDataField *field1,
                                              gint col1,
                                              gint row1,
                                              GwyDataField *field2,
                                              gint col2,
                                              gint row2,
                                              gint width,
                                              gint height);
static void             merge_boundary       (GwyDataField *field1,
                                              GwyDataField *field2,
                                              GwyDataField *result,
                                              GwyRectangle res_rect,
                                              GwyCoord f1_pos,
                                              GwyCoord f2_pos,
                                              GwyMergeBoundaryType boundary);
static void             create_merged_field  (ModuleArgs *args,
                                              gint px1,
                                              gint py1,
                                              gint px2,
                                              gint py2,
                                              GwyMergeBoundaryType boundary,
                                              GwyMergeDirectionType direction,
                                              gboolean create_mask,
                                              gboolean crop_to_rectangle);
static void             put_fields           (GwyDataField *field1,
                                              GwyDataField *field2,
                                              GwyDataField *result,
                                              GwyDataField *outsidemask,
                                              GwyMergeBoundaryType boundary,
                                              gint px1,
                                              gint py1,
                                              gint px2,
                                              gint py2);
static void             crop_result          (GwyDataField *result,
                                              GwyDataField *field1,
                                              GwyDataField *field2,
                                              GwyOrientation orientation,
                                              gint px1,
                                              gint py1,
                                              gint px2,
                                              gint py2);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Merges two images."),
    "Petr Klapetek <klapetek@gwyddion.net>, Yeti <yeti@gwyddion.net>",
    "4.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, merge)

static gboolean
module_register(void)
{
    gwy_process_func_register("merge",
                              (GwyProcessFunc)&merge,
                              N_("/M_ultidata/_Merge..."),
                              GWY_STOCK_MERGE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Merge two images"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("Up"),           GWY_MERGE_DIRECTION_UP,    },
        { N_("Down"),         GWY_MERGE_DIRECTION_DOWN,  },
        { N_("adverb|Right"), GWY_MERGE_DIRECTION_RIGHT, },
        { N_("adverb|Left"),  GWY_MERGE_DIRECTION_LEFT,  },
    };
    static const GwyEnum modes[] = {
        { N_("Correlation"),     GWY_MERGE_MODE_CORRELATE, },
        { N_("merge-mode|Join"), GWY_MERGE_MODE_JOIN,      },
        { N_("merge-mode|None"), GWY_MERGE_MODE_NONE,      },
    };
    static const GwyEnum boundaries[] = {
        { N_("First image"),   GWY_MERGE_BOUNDARY_FIRST,       },
        { N_("Second image"),  GWY_MERGE_BOUNDARY_SECOND,      },
        { N_("Average"),       GWY_MERGE_BOUNDARY_AVERAGE,     },
        { N_("Interpolation"), GWY_MERGE_BOUNDARY_INTERPOLATE, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_OHTER_IMAGE, "other_image", _("_Merge with"));
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", _("_Put second image"),
                              directions, G_N_ELEMENTS(directions), GWY_MERGE_DIRECTION_RIGHT);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("_Align second image"),
                              modes, G_N_ELEMENTS(modes), GWY_MERGE_MODE_CORRELATE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_BOUNDARY, "boundary", _("_Boundary treatment"),
                              boundaries, G_N_ELEMENTS(boundaries), GWY_MERGE_BOUNDARY_FIRST);
    gwy_param_def_add_boolean(paramdef, PARAM_CROP_TO_RECTANGLE, "crop_to_rectangle",
                              _("Crop result to _avoid outside pixels"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_CREATE_MASK, "create_mask", _("Create _mask over exterior"), FALSE);
    return paramdef;
}

static void
merge(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyMergeModeType mode;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    mode = gwy_params_get_enum(args.params, PARAM_MODE);
    if (mode == GWY_MERGE_MODE_NONE)
        execute_none(&args);
    else if (mode == GWY_MERGE_MODE_JOIN)
        execute_join(&args);
    else
        execute_correlate(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("Merged images"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    if (args.result_mask && gwy_data_field_get_max(args.result_mask) > 0.0)
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), args.result_mask);
    gwy_app_channel_log_add_proc(data, -1, newid);

end:
    GWY_OBJECT_UNREF(args.result);
    GWY_OBJECT_UNREF(args.result_mask);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.dialog = gwy_dialog_new(_("Merge Data"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OHTER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OHTER_IMAGE, other_image_filter, args, NULL);
    gwy_param_table_append_combo(table, PARAM_DIRECTION);
    gwy_param_table_append_combo(table, PARAM_MODE);
    gwy_param_table_append_combo(table, PARAM_BOUNDARY);
    gwy_param_table_append_checkbox(table, PARAM_CROP_TO_RECTANGLE);
    gwy_param_table_append_checkbox(table, PARAM_CREATE_MASK);

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

    if (id < 0 || id == PARAM_DIRECTION || id == PARAM_MODE) {
        GwyMergeModeType mode = gwy_params_get_enum(params, PARAM_MODE);

        gwy_param_table_set_sensitive(table, PARAM_CROP_TO_RECTANGLE, mode != GWY_MERGE_MODE_JOIN);
        gwy_param_table_set_sensitive(table, PARAM_BOUNDARY, mode != GWY_MERGE_MODE_JOIN);
        gwy_param_table_data_id_refilter(gui->table, PARAM_OHTER_IMAGE);
    }
    if (id < 0 || id == PARAM_MODE || id == PARAM_CROP_TO_RECTANGLE) {
        GwyMergeModeType mode = gwy_params_get_enum(params, PARAM_MODE);
        gboolean crop_to_rectangle = gwy_params_get_boolean(params, PARAM_CROP_TO_RECTANGLE);

        gwy_param_table_set_sensitive(table, PARAM_CREATE_MASK, mode != GWY_MERGE_MODE_JOIN && !crop_to_rectangle);
    }

    if (id < 0 || id == PARAM_OHTER_IMAGE) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                          !gwy_params_data_id_is_none(params, PARAM_OHTER_IMAGE));
    }
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    ModuleArgs *args = (ModuleArgs*)user_data;
    GwyDataField *field = args->field, *otherfield;
    GwyMergeModeType mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyMergeDirectionType direction = gwy_params_get_enum(args->params, PARAM_DIRECTION);

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    if (gwy_data_field_check_compatibility(field, otherfield,
                                           GWY_DATA_COMPATIBILITY_MEASURE
                                           | GWY_DATA_COMPATIBILITY_LATERAL
                                           | GWY_DATA_COMPATIBILITY_VALUE))
        return FALSE;
    if (mode == GWY_MERGE_MODE_JOIN) {
        if (direction == GWY_MERGE_DIRECTION_UP || direction == GWY_MERGE_DIRECTION_DOWN)
            return gwy_data_field_get_xres(otherfield) == gwy_data_field_get_xres(field);
        if (direction == GWY_MERGE_DIRECTION_LEFT || direction == GWY_MERGE_DIRECTION_RIGHT)
            return gwy_data_field_get_yres(otherfield) == gwy_data_field_get_yres(field);
    }
    return TRUE;
}

static void
execute_correlate(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyMergeDirectionType direction = gwy_params_get_enum(params, PARAM_DIRECTION);
    GwyMergeBoundaryType boundary = gwy_params_get_enum(params, PARAM_BOUNDARY);
    gboolean create_mask = gwy_params_get_boolean(params, PARAM_CREATE_MASK);
    gboolean crop_to_rectangle = gwy_params_get_boolean(params, PARAM_CROP_TO_RECTANGLE);
    GwyDataField *field1 = args->field, *field2 = gwy_params_get_image(params, PARAM_OHTER_IMAGE);
    GwyDataField *correlation_data, *correlation_kernel, *correlation_score;
    GwyRectangle cdata, kdata;
    gint max_col, max_row;
    gint xres1, xres2, yres1, yres2;
    gint px1, py1, px2, py2;
    gdouble xoff, yoff, maxscore;

    if ((field1->xres*field1->yres) < (field2->xres*field2->yres)) {
        GWY_SWAP(GwyDataField*, field1, field2);
        if (direction == GWY_MERGE_DIRECTION_UP)
            direction = GWY_MERGE_DIRECTION_DOWN;
        else if (direction == GWY_MERGE_DIRECTION_DOWN)
            direction = GWY_MERGE_DIRECTION_UP;
        else if (direction == GWY_MERGE_DIRECTION_LEFT)
            direction = GWY_MERGE_DIRECTION_RIGHT;
        else if (direction == GWY_MERGE_DIRECTION_RIGHT)
            direction = GWY_MERGE_DIRECTION_LEFT;
        else
            g_return_if_reached();

        if (boundary == GWY_MERGE_BOUNDARY_FIRST)
            boundary = GWY_MERGE_BOUNDARY_SECOND;
        else if (boundary == GWY_MERGE_BOUNDARY_SECOND)
            boundary = GWY_MERGE_BOUNDARY_FIRST;
    }

    xres1 = gwy_data_field_get_xres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres1 = gwy_data_field_get_yres(field1);
    yres2 = gwy_data_field_get_yres(field2);

    /* Cut data for correlation. */
    if (direction == GWY_MERGE_DIRECTION_UP) {
        cdata.x = 0;
        cdata.y = 0;
        cdata.width = xres1;
        cdata.height = yres1/2;
        kdata.width = MIN(xres2, cdata.width/2);
        kdata.height = MIN(yres2, cdata.height/3);
        kdata.x = MAX(0, xres2/2 - kdata.width/2);
        kdata.y = MAX(0, yres2 - cdata.height/3);
    }
    else if (direction == GWY_MERGE_DIRECTION_DOWN) {
        cdata.x = 0;
        cdata.y = yres1 - (yres1/2);
        cdata.width = xres1;
        cdata.height = yres1/2;
        kdata.width = MIN(xres2, cdata.width/2);
        kdata.height = MIN(yres2, cdata.height/3);
        kdata.x = MAX(0, xres2/2 - kdata.width/2);
        kdata.y = 0;
    }
    else if (direction == GWY_MERGE_DIRECTION_RIGHT) {
        cdata.x = xres1 - (xres1/2);
        cdata.y = 0;
        cdata.width = xres1/2;
        cdata.height = yres1;
        kdata.width = MIN(xres2, cdata.width/3);
        kdata.height = MIN(yres2, cdata.height/2);
        kdata.x = 0;
        kdata.y = MAX(0, yres2/2 - kdata.height/2);
    }
    else if (direction == GWY_MERGE_DIRECTION_LEFT) {
        cdata.x = 0;
        cdata.y = 0;
        cdata.width = xres1/2;
        cdata.height = yres1;
        kdata.width = MIN(xres2, cdata.width/3);
        kdata.height = MIN(yres2, cdata.height/2);
        kdata.x = MAX(0, xres2 - cdata.width/3);
        kdata.y = MAX(0, yres2/2 - kdata.height/2);
    }
    else {
        g_assert_not_reached();
    }

    correlation_data = gwy_data_field_area_extract(field1, cdata.x, cdata.y, cdata.width, cdata.height);
    correlation_kernel = gwy_data_field_area_extract(field2, kdata.x, kdata.y, kdata.width, kdata.height);
    correlation_score = gwy_data_field_new_alike(correlation_data, FALSE);

    max_col = max_row = 0;
    gwy_data_field_correlation_search(correlation_data, correlation_kernel, NULL, correlation_score,
                                      GWY_CORR_SEARCH_COVARIANCE_SCORE, 0.01, GWY_EXTERIOR_BORDER_EXTEND, 0.0);
    if (gwy_data_field_get_local_maxima_list(correlation_score, &xoff, &yoff, &maxscore, 1, 0, 0.0, FALSE)) {
        max_col = GWY_ROUND(xoff);
        max_row = GWY_ROUND(yoff);
    }

    gwy_debug("c: %d %d %dx%d  k: %d %d %dx%d res: %d %d",
              cdata.x, cdata.y, cdata.width, cdata.height,
              kdata.x, kdata.y, kdata.width, kdata.height,
              max_col, max_row);

    px1 = 0;
    px2 = (max_col - kdata.width/2) + cdata.x - kdata.x;
    py1 = 0;
    py2 = (max_row - kdata.height/2) + cdata.y - kdata.y;
    if (px2 < 0) {
        px1 = -px2;
        px2 = 0;
    }
    if (py2 < 0) {
        py1 = -py2;
        py2 = 0;
    }
    create_merged_field(args, px1, py1, px2, py2, boundary, direction, create_mask, crop_to_rectangle);

    g_object_unref(correlation_data);
    g_object_unref(correlation_kernel);
    g_object_unref(correlation_score);
}

static void
execute_join(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyMergeDirectionType direction = gwy_params_get_enum(params, PARAM_DIRECTION);
    GwyMergeBoundaryType boundary = gwy_params_get_enum(params, PARAM_BOUNDARY);
    GwyDataField *field1 = args->field, *field2 = gwy_params_get_image(params, PARAM_OHTER_IMAGE);
    gint xres1, xres2, yres1, yres2;
    gint maxover, i, off = 0;
    gint px1, py1, px2, py2;
    gdouble s, smin = G_MAXDOUBLE;

    /* Reduce joining to two cases. */
    if (direction == GWY_MERGE_DIRECTION_UP || direction == GWY_MERGE_DIRECTION_LEFT) {
        GWY_SWAP(GwyDataField*, field1, field2);

        if (direction == GWY_MERGE_DIRECTION_UP)
            direction = GWY_MERGE_DIRECTION_DOWN;
        else if (direction == GWY_MERGE_DIRECTION_LEFT)
            direction = GWY_MERGE_DIRECTION_RIGHT;
        else {
            g_assert_not_reached();
        }

        if (boundary == GWY_MERGE_BOUNDARY_FIRST)
            boundary = GWY_MERGE_BOUNDARY_SECOND;
        else if (boundary == GWY_MERGE_BOUNDARY_SECOND)
            boundary = GWY_MERGE_BOUNDARY_FIRST;
    }

    xres1 = gwy_data_field_get_xres(field1);
    yres1 = gwy_data_field_get_yres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres2 = gwy_data_field_get_yres(field2);

    if (direction == GWY_MERGE_DIRECTION_DOWN) {
        g_return_if_fail(xres1 == xres2);
        maxover = 2*MIN(yres1, yres2)/5;
        for (i = 1; i <= maxover; i++) {
            s = get_row_difference(field1, 0, yres1 - i, field2, 0, 0, xres1, i);
            if (s < smin) {
                off = i;
                smin = s;
            }
        }
        /* Turn one-pixel overlap to no overlap. */
        if (off == 1)
            off = 0;
        px1 = px2 = 0;
        py1 = 0;
        py2 = yres1 - off;
    }
    else if (direction == GWY_MERGE_DIRECTION_RIGHT) {
        g_return_if_fail(yres1 == yres2);
        maxover = 2*MIN(xres1, xres2)/5;
        for (i = 1; i <= maxover; i++) {
            s = get_column_difference(field1, xres1 - i, 0, field2, 0, 0, i, yres1);
            if (s < smin) {
                off = i;
                smin = s;
            }
        }
        /* Turn one-pixel overlap to no overlap. */
        if (off == 1)
            off = 0;
        py1 = py2 = 0;
        px1 = 0;
        px2 = xres1 - off;
    }
    else {
        g_assert_not_reached();
    }

    create_merged_field(args, px1, py1, px2, py2, boundary, direction, FALSE, FALSE);
}

static void
execute_none(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyMergeDirectionType direction = gwy_params_get_enum(params, PARAM_DIRECTION);
    GwyMergeBoundaryType boundary = gwy_params_get_enum(params, PARAM_BOUNDARY);
    gboolean create_mask = gwy_params_get_boolean(params, PARAM_CREATE_MASK);
    gboolean crop_to_rectangle = gwy_params_get_boolean(params, PARAM_CROP_TO_RECTANGLE);
    GwyDataField *field1 = args->field, *field2 = gwy_params_get_image(params, PARAM_OHTER_IMAGE);
    gint xres1, xres2, yres1, yres2;
    gint px1, py1, px2, py2;

    xres1 = gwy_data_field_get_xres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres1 = gwy_data_field_get_yres(field1);
    yres2 = gwy_data_field_get_yres(field2);

    if (direction == GWY_MERGE_DIRECTION_UP) {
        px1 = px2 = 0;
        py1 = yres2;
        py2 = 0;
    }
    else if (direction == GWY_MERGE_DIRECTION_DOWN) {
        px1 = px2 = 0;
        py1 = 0;
        py2 = yres1;
    }
    else if (direction == GWY_MERGE_DIRECTION_LEFT) {
        py1 = py2 = 0;
        px1 = xres2;
        px2 = 0;
    }
    else if (direction == GWY_MERGE_DIRECTION_RIGHT) {
        py1 = py2 = 0;
        px1 = 0;
        px2 = xres1;
    }
    else {
        g_assert_not_reached();
    }

    create_merged_field(args, px1, py1, px2, py2, boundary, direction, create_mask, crop_to_rectangle);
}

static void
create_merged_field(ModuleArgs *args, gint px1, gint py1, gint px2, gint py2,
                    GwyMergeBoundaryType boundary, GwyMergeDirectionType direction,
                    gboolean create_mask, gboolean crop_to_rectangle)
{
    GwyDataField *field1 = args->field, *field2 = gwy_params_get_image(args->params, PARAM_OHTER_IMAGE);
    GwyDataField *result, *outsidemask = NULL;
    gint xres1, xres2, yres1, yres2, newxres, newyres;

    xres1 = gwy_data_field_get_xres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres1 = gwy_data_field_get_yres(field1);
    yres2 = gwy_data_field_get_yres(field2);

    gwy_debug("field1 %dx%d", xres1, yres1);
    gwy_debug("field2 %dx%d", xres2, yres2);
    gwy_debug("px1: %d, py1: %d, px2: %d, py2: %d", px1, py1, px2, py2);
    newxres = MAX(xres1 + px1, xres2 + px2);
    newyres = MAX(yres1 + py1, yres2 + py2);

    result = args->result = gwy_data_field_new_alike(field1, FALSE);
    gwy_data_field_resample(result, newxres, newyres, GWY_INTERPOLATION_NONE);

    if (create_mask && !crop_to_rectangle) {
        outsidemask = args->result_mask = gwy_data_field_new_alike(result, FALSE);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(outsidemask), NULL);
    }
    put_fields(field1, field2, result, outsidemask, boundary, px1, py1, px2, py2);

    if (crop_to_rectangle) {
        GwyOrientation orientation = GWY_ORIENTATION_HORIZONTAL;
        if (direction == GWY_MERGE_DIRECTION_UP || direction == GWY_MERGE_DIRECTION_DOWN)
            orientation = GWY_ORIENTATION_VERTICAL;

        crop_result(result, field1, field2, orientation, px1, py1, px2, py2);
    }
}

static void
put_fields(GwyDataField *field1, GwyDataField *field2,
           GwyDataField *result, GwyDataField *outsidemask,
           GwyMergeBoundaryType boundary,
           gint px1, gint py1, gint px2, gint py2)
{
    GwyRectangle res_rect;
    GwyCoord f1_pos, f2_pos;
    gint w1, w2, h1, h2;
    gdouble xreal, yreal;

    gwy_debug("field1 %dx%d", field1->xres, field1->yres);
    gwy_debug("field2 %dx%d", field2->xres, field2->yres);
    gwy_debug("result %dx%d", result->xres, result->yres);
    gwy_debug("px1: %d, py1: %d, px2: %d, py2: %d", px1, py1, px2, py2);

    gwy_data_field_fill(result, fmin(gwy_data_field_get_min(field1), gwy_data_field_get_min(field2)));

    w1 = gwy_data_field_get_xres(field1);
    h1 = gwy_data_field_get_yres(field1);
    w2 = gwy_data_field_get_xres(field2);
    h2 = gwy_data_field_get_yres(field2);

    if (boundary == GWY_MERGE_BOUNDARY_SECOND) {
        gwy_data_field_area_copy(field1, result, 0, 0, w1, h1, px1, py1);
        gwy_data_field_area_copy(field2, result, 0, 0, w2, h2, px2, py2);
    }
    else {
        gwy_data_field_area_copy(field2, result, 0, 0, w2, h2, px2, py2);
        gwy_data_field_area_copy(field1, result, 0, 0, w1, h1, px1, py1);
    }

    if (outsidemask) {
        gwy_data_field_fill(outsidemask, 1.0);
        gwy_data_field_area_clear(outsidemask, px1, py1, w1, h1);
        gwy_data_field_area_clear(outsidemask, px2, py2, w2, h2);
    }

    /* adjust boundary to be as smooth as possible */
    if (boundary == GWY_MERGE_BOUNDARY_AVERAGE || boundary == GWY_MERGE_BOUNDARY_INTERPOLATE) {
        if (px1 < px2) {
            res_rect.x = px2;
            res_rect.width = px1 + w1 - px2;
        }
        else {
            res_rect.x = px1;
            res_rect.width = px2 + w2 - px1;
        }

        if (py1 < py2) {
            res_rect.y = py2;
            res_rect.height = py1 + h1 - py2;
        }
        else {
            res_rect.y = py1;
            res_rect.height = py2 + h2 - py1;
        }

        res_rect.height = MIN(res_rect.height, MIN(h1, h2));
        res_rect.width = MIN(res_rect.width, MIN(w1, w2));

        /* This is where the result rectangle is positioned in the fields,
         * not where the fields themselves are placed! */
        f1_pos.x = res_rect.x - px1;
        f1_pos.y = res_rect.y - py1;
        f2_pos.x = res_rect.x - px2;
        f2_pos.y = res_rect.y - py2;

        merge_boundary(field1, field2, result, res_rect, f1_pos, f2_pos, boundary);
    }

    /* Use the pixels sizes of field 1 -- they must be identical. */
    xreal = result->xres * gwy_data_field_get_dx(field1);
    yreal = result->yres * gwy_data_field_get_dy(field1);
    gwy_data_field_set_xreal(result, xreal);
    gwy_data_field_set_yreal(result, yreal);
    if (outsidemask) {
        gwy_data_field_set_xreal(outsidemask, xreal);
        gwy_data_field_set_yreal(outsidemask, yreal);
    }
}

static void
crop_result(GwyDataField *result, GwyDataField *field1, GwyDataField *field2,
            GwyOrientation orientation,
            gint px1, gint py1, gint px2, gint py2)
{
    if (orientation == GWY_ORIENTATION_HORIZONTAL) {
        gint top = MAX(MAX(py1, py2), 0);
        gint bot = MIN(MIN(field1->yres + py1, field2->yres + py2), result->yres);
        gdouble yreal = (bot - top)*gwy_data_field_get_dy(result);
        g_return_if_fail(bot > top);
        gwy_data_field_resize(result, 0, top, result->xres, bot);
        gwy_data_field_set_yreal(result, yreal);
    }
    else {
        gint left = MAX(MAX(px1, px2), 0);
        gint right = MIN(MIN(field1->xres + px1, field2->xres + px2), result->xres);
        gdouble xreal = (right - left)*gwy_data_field_get_dx(result);
        g_return_if_fail(right > left);
        gwy_data_field_resize(result, left, 0, right, result->yres);
        gwy_data_field_set_xreal(result, xreal);
    }
}

/* Note this is not a correlation score since we care also about absolute
 * differences and try to suppress the influence of outliers. */
static gdouble
get_row_difference(GwyDataField *field1, gint col1, gint row1,
                   GwyDataField *field2, gint col2, gint row2,
                   gint width, gint height)
{
    gint xres1, yres1, xres2, yres2, i, j;
    const gdouble *data1, *data2;
    gdouble *row;
    gdouble s = 0.0;

    g_return_val_if_fail(width > 0, G_MAXDOUBLE);
    g_return_val_if_fail(height > 0, G_MAXDOUBLE);

    xres1 = gwy_data_field_get_xres(field1);
    yres1 = gwy_data_field_get_yres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres2 = gwy_data_field_get_yres(field2);
    data1 = gwy_data_field_get_data_const(field1);
    data2 = gwy_data_field_get_data_const(field2);

    g_return_val_if_fail(col1 + width <= xres1, G_MAXDOUBLE);
    g_return_val_if_fail(col2 + width <= xres2, G_MAXDOUBLE);
    g_return_val_if_fail(row1 + height <= yres1, G_MAXDOUBLE);
    g_return_val_if_fail(row2 + height <= yres2, G_MAXDOUBLE);

    row = g_new(gdouble, width);

    for (i = 0; i < height; i++) {
        const gdouble *d1 = data1 + (row1 + i)*xres1 + col1;
        const gdouble *d2 = data2 + (row2 + i)*xres2 + col2;
        gdouble d;

        for (j = 0; j < width; j++) {
            d = d1[j] - d2[j];
            row[j] = d;
        }
        d = gwy_math_median(width, row);
        s += d*d;
    }

    g_free(row);

    return sqrt(s/height);
}

static gdouble
get_column_difference(GwyDataField *field1, gint col1, gint row1,
                      GwyDataField *field2, gint col2, gint row2,
                      gint width, gint height)
{
    gint xres1, yres1, xres2, yres2, i, j;
    const gdouble *data1, *data2;
    gdouble *column;
    gdouble s = 0.0;

    g_return_val_if_fail(width > 0, G_MAXDOUBLE);
    g_return_val_if_fail(height > 0, G_MAXDOUBLE);

    xres1 = gwy_data_field_get_xres(field1);
    yres1 = gwy_data_field_get_yres(field1);
    xres2 = gwy_data_field_get_xres(field2);
    yres2 = gwy_data_field_get_yres(field2);
    data1 = gwy_data_field_get_data_const(field1);
    data2 = gwy_data_field_get_data_const(field2);

    g_return_val_if_fail(col1 + width <= xres1, G_MAXDOUBLE);
    g_return_val_if_fail(col2 + width <= xres2, G_MAXDOUBLE);
    g_return_val_if_fail(row1 + height <= yres1, G_MAXDOUBLE);
    g_return_val_if_fail(row2 + height <= yres2, G_MAXDOUBLE);

    column = g_new(gdouble, height);

    for (j = 0; j < width; j++) {
        const gdouble *d1 = data1 + row1*xres1 + col1 + j;
        const gdouble *d2 = data2 + row2*xres2 + col2 + j;
        gdouble d;

        for (i = 0; i < height; i++) {
            d = d1[i*xres1] - d2[i*xres2];
            column[i] = d;
        }
        d = gwy_math_median(height, column);
        s += d*d;
    }

    g_free(column);

    return sqrt(s/height);
}

static void
assign_edge(gint edgepos, gint pos1, gint pos2, gint *w1, gint *w2)
{
    gboolean onedge1 = (pos1 == edgepos);
    gboolean onedge2 = (pos2 == edgepos);

    gwy_debug("%d :: %d %d", edgepos, pos1, pos2);
    g_assert(onedge1 || onedge2);
    *w1 = onedge1;
    *w2 = onedge2;
}

static void
merge_boundary(GwyDataField *field1, GwyDataField *field2, GwyDataField *result,
               GwyRectangle res_rect, GwyCoord f1_pos, GwyCoord f2_pos,
               GwyMergeBoundaryType boundary)
{
    gint xres1, xres2, xres, yres1, yres2, col, row;
    gdouble weight, val1, val2;
    gint w1top = 0, w1bot = 0, w1left = 0, w1right = 0;
    gint w2top = 0, w2bot = 0, w2left = 0, w2right = 0;
    const gdouble *d1, *d2;
    gdouble *d;

    xres1 = field1->xres;
    yres1 = field1->yres;
    xres2 = field2->xres;
    yres2 = field2->yres;
    xres = result->xres;

    gwy_debug("field1: %d x %d at (%d, %d)", xres1, yres1, f1_pos.x, f1_pos.y);
    gwy_debug("field2: %d x %d at (%d, %d)", xres2, yres2, f2_pos.x, f2_pos.y);
    gwy_debug("result: %d x %d", xres, result->yres);
    gwy_debug("rect in result : %d x %d at (%d,%d)", res_rect.width, res_rect.height, res_rect.x, res_rect.y);

    assign_edge(0, f1_pos.x, f2_pos.x, &w1left, &w2left);
    gwy_debug("left: %d %d", w1left, w2left);
    assign_edge(0, f1_pos.y, f2_pos.y, &w1top, &w2top);
    gwy_debug("top: %d %d", w1top, w2top);
    assign_edge(res_rect.width, xres1 - f1_pos.x, xres2 - f2_pos.x, &w1right, &w2right);
    gwy_debug("right: %d %d", w1right, w2right);
    assign_edge(res_rect.height, yres1 - f1_pos.y, yres2 - f2_pos.y, &w1bot, &w2bot);
    gwy_debug("bot: %d %d", w1bot, w2bot);

    d1 = gwy_data_field_get_data_const(field1);
    d2 = gwy_data_field_get_data_const(field2);
    d = gwy_data_field_get_data(result);

    for (row = 0; row < res_rect.height; row++) {
        gint dtop = row + 1, dbot = res_rect.height - row;
        for (col = 0; col < res_rect.width; col++) {
            weight = 0.5;
            if (boundary == GWY_MERGE_BOUNDARY_INTERPOLATE) {
                gint dleft = col + 1, dright = res_rect.width - col;
                gint d1min = G_MAXINT, d2min = G_MAXINT;
                /* FIXME: This can be probably simplified... */
                if (w1top && dtop < d1min)
                    d1min = dtop;
                if (w1bot && dbot < d1min)
                    d1min = dbot;
                if (w1left && dleft < d1min)
                    d1min = dleft;
                if (w1right && dright < d1min)
                    d1min = dright;
                if (w2top && dtop < d2min)
                    d2min = dtop;
                if (w2bot && dbot < d2min)
                    d2min = dbot;
                if (w2left && dleft < d2min)
                    d2min = dleft;
                if (w2right && dright < d2min)
                    d2min = dright;

                weight = (gdouble)d2min/(d1min + d2min);
            }
            val1 = d1[xres1*(row + f1_pos.y) + (col + f1_pos.x)];
            val2 = d2[xres2*(row + f2_pos.y) + (col + f2_pos.x)];
            d[xres*(row + res_rect.y) + col + res_rect.x] = (1.0 - weight)*val1 + weight*val2;
        }
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
