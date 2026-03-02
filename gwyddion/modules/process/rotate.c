/*
 *  $Id: rotate.c 25638 2023-09-07 12:23:28Z yeti-dn $
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_INTERPOLATION,
    PARAM_RESIZE,
    PARAM_SHOW_GRID,
    PARAM_CREATE_MASK,
    /* Rotate only. */
    PARAM_ANGLE,
    /* Unrotate only. */
    PARAM_SYMMETRY,
    INFO_DETECTED,
    INFO_CORRECTION,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    gboolean is_unrotate;
    /* Cached estimated values for input data field (unrotate). */
    GwyPlaneSymmetry symm;
    gdouble corrections[GWY_SYMMETRY_LAST];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyContainer *data;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GwyParamTable *table;
    GwySelection *selection;
} ModuleGUI;

static gboolean         module_register        (void);
static GwyParamDef*     define_rotate_params   (void);
static GwyParamDef*     define_unrotate_params (void);
static void             rotate_unrotate        (GwyContainer *data,
                                                GwyRunType run,
                                                const gchar *name);
static GwyDialogOutcome run_gui                (ModuleArgs *args,
                                                GwyContainer *data,
                                                gint id);
static GwyContainer*    create_preview_data    (GwyContainer *data,
                                                GwyDataField *field,
                                                gint id);
static void             param_changed          (ModuleGUI *gui,
                                                gint id);
static void             preview                (gpointer user_data);
static void             update_grid            (ModuleGUI *gui);
static void             fix_mask_exterior      (GwyDataField *mask,
                                                GwyDataField *exterior_mask,
                                                gdouble fill_value);
static GwyPlaneSymmetry guess_unrotate_symmetry(GwyDataField *field,
                                                gdouble *corrections);
static gdouble          get_rotation_angle     (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Rotates data by arbitrary angle or to make characteristic directions parallel with x or y axis."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2003",
};

GWY_MODULE_QUERY2(module_info, rotate)

static gboolean
module_register(void)
{
    gwy_process_func_register("rotate",
                              &rotate_unrotate,
                              N_("/_Basic Operations/Rotate by _Angle..."),
                              GWY_STOCK_ROTATE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Rotate by arbitrary angle"));
    gwy_process_func_register("unrotate",
                              &rotate_unrotate,
                              N_("/_Correct Data/_Unrotate..."),
                              GWY_STOCK_UNROTATE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Automatically correct rotation in horizontal plane"));
    return TRUE;
}

static void
define_params_common(GwyParamDef *paramdef)
{
    static const GwyEnum resize_types[] = {
        { N_("_Same as original"),          GWY_ROTATE_RESIZE_SAME_SIZE, },
        { N_("_Expanded to complete data"), GWY_ROTATE_RESIZE_EXPAND,    },
        { N_("C_ut to valid data"),         GWY_ROTATE_RESIZE_CUT,       },
    };
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interp", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_gwyenum(paramdef, PARAM_RESIZE, "resize", _("Result size"),
                              resize_types, G_N_ELEMENTS(resize_types), GWY_ROTATE_RESIZE_SAME_SIZE);
    gwy_param_def_add_boolean(paramdef, PARAM_CREATE_MASK, "create_mask", _("Create _mask over exterior"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_SHOW_GRID, "show_grid", _("Show _grid"), TRUE);
}

static GwyParamDef*
define_rotate_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    define_params_common(paramdef);
    gwy_param_def_add_angle(paramdef, PARAM_ANGLE, "angle", _("Rotate by _angle"), FALSE, 1, 0.0);
    return paramdef;
}

static GwyParamDef*
define_unrotate_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    define_params_common(paramdef);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SYMMETRY, "symmetry", _("_Assume symmetry"),
                              gwy_plane_symmetry_get_enum(), -1, GWY_SYMMETRY_AUTO);
    return paramdef;
}

static void
rotate_unrotate(GwyContainer *data, GwyRunType runtype, const gchar *name)
{
    GwyDataField *fields[3], *exterior_mask = NULL;
    gint oldid, newid, i;
    ModuleArgs args;
    GwyParams *params;
    GwyInterpolationType interp;
    GwyRotateResizeType resize;
    gboolean create_mask;
    gdouble angle;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, fields + 0,
                                     GWY_APP_MASK_FIELD, fields + 1,
                                     GWY_APP_SHOW_FIELD, fields + 2,
                                     GWY_APP_DATA_FIELD_ID, &oldid,
                                     0);
    g_return_if_fail(fields[0]);

    args.field = fields[0];
    args.is_unrotate = gwy_strequal(name, "unrotate");
    if (args.is_unrotate) {
        args.symm = guess_unrotate_symmetry(args.field, args.corrections);
        params = args.params = gwy_params_new_from_settings(define_unrotate_params());
    }
    else
        params = args.params = gwy_params_new_from_settings(define_rotate_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        GwyDialogOutcome outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(params);
        if (outcome != GWY_DIALOG_PROCEED)
            goto end;
    }

    angle = get_rotation_angle(&args);
    interp = gwy_params_get_enum(params, PARAM_INTERPOLATION);
    resize = gwy_params_get_enum(params, PARAM_RESIZE);
    create_mask = gwy_params_get_boolean(params, PARAM_CREATE_MASK);

    if (fields[1] || (create_mask && resize != GWY_ROTATE_RESIZE_CUT))
        exterior_mask = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);

    fields[0] = gwy_data_field_new_rotated(fields[0], exterior_mask, angle, interp, resize);
    if (fields[1]) {
        fields[1] = gwy_data_field_new_rotated(fields[1], NULL, angle, GWY_INTERPOLATION_ROUND, resize);
        /* The rotation fill exterior with average value of inside; which is kind of random and anyway unwanted for
         * masks.  Fill the exterior with either 0 or 1 (if we were asked to add an exterior mask). */
        fix_mask_exterior(fields[1], exterior_mask, create_mask ? 1.0 : 0.0);
    }
    else {
        /* If we were asked to add an exterior mask use exterior_mask as the mask.  Otherwise do nothing. */
        if (exterior_mask) {
            g_object_ref(exterior_mask);
            fields[1] = exterior_mask;
        }
    }
    GWY_OBJECT_UNREF(exterior_mask);

    if (fields[2])
        fields[2] = gwy_data_field_new_rotated(fields[2], NULL, angle, interp, resize);

    newid = gwy_app_data_browser_add_data_field(fields[0], data, TRUE);
    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    if (fields[1])
        gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), fields[1]);
    if (fields[2])
        gwy_container_set_object(data, gwy_app_get_show_key_for_id(newid), fields[2]);

    gwy_app_set_data_field_title(data, newid, _("Rotated Data"));
    gwy_app_channel_log_add_proc(data, oldid, newid);

    for (i = 0; i < 3; i++)
        GWY_OBJECT_UNREF(fields[i]);

end:
    g_object_unref(params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *dataview;
    GwyDialog *dialog;
    GwyParamTable *table;
    const gchar *s;
    ModuleGUI gui;

    gui.args = args;
    gui.data = create_preview_data(data, args->field, id);

    gui.dialog = gwy_dialog_new(args->is_unrotate ? _("Correct Rotation") : _("Rotate"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    /* TODO: Add selection setup helper outside preview.h. */
    gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Lattice", 1, FALSE);

    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), TRUE);

    table = gui.table = gwy_param_table_new(args->params);

    if (args->is_unrotate) {
        gwy_param_table_append_header(table, -1, _("Structure"));
        gwy_param_table_append_info(table, INFO_DETECTED, _("Detected"));
        s = gwy_sgettext(gwy_enum_to_string(args->symm, gwy_plane_symmetry_get_enum(), -1));
        gwy_param_table_info_set_valuestr(table, INFO_DETECTED, s);
        gwy_param_table_append_combo(table, PARAM_SYMMETRY);
        gwy_param_table_append_info(table, INFO_CORRECTION, _("Correction"));
        gwy_param_table_set_unitstr(table, INFO_CORRECTION, _("deg"));
    }
    else {
        gwy_param_table_append_header(table, -1, _("Rotate"));
        gwy_param_table_append_slider(table, PARAM_ANGLE);
        gwy_param_table_slider_set_steps(table, PARAM_ANGLE, 0.01 * G_PI/180.0, 5.0 * G_PI/180.0);
    }

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);
    gwy_param_table_append_radio(table, PARAM_RESIZE);
    gwy_param_table_append_checkbox(table, PARAM_SHOW_GRID);
    gwy_param_table_append_checkbox(table, PARAM_CREATE_MASK);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static GwyContainer*
create_preview_data(GwyContainer *data, GwyDataField *field, gint id)
{
    GwyContainer *preview_data;
    GwyDataField *small_field, *rotated_field;
    gint xres, yres;
    gdouble zoomval;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    zoomval = (gdouble)PREVIEW_SIZE/MAX(xres, yres);
    small_field = gwy_data_field_new_resampled(field, xres*zoomval, yres*zoomval, GWY_INTERPOLATION_LINEAR);
    rotated_field = gwy_data_field_duplicate(small_field);

    preview_data = gwy_container_new();
    gwy_container_pass_object_by_name(preview_data, "/1/data", small_field);
    gwy_container_pass_object_by_name(preview_data, "/0/data", rotated_field);

    gwy_app_sync_data_items(data, preview_data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    return preview_data;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_RESIZE) {
        GwyRotateResizeType resize = gwy_params_get_enum(params, PARAM_RESIZE);
        gwy_param_table_set_sensitive(table, PARAM_CREATE_MASK, resize != GWY_ROTATE_RESIZE_CUT);
    }

    if (id < 0 || id == PARAM_SHOW_GRID)
        update_grid(gui);

    if (args->is_unrotate && (id < 0 || id == PARAM_SYMMETRY)) {
        gchar *s = g_strdup_printf("%.2f", 180.0/G_PI*get_rotation_angle(args));
        gwy_param_table_info_set_valuestr(table, INFO_CORRECTION, s);
        g_free(s);
    }

    if (id != PARAM_INTERPOLATION && id != PARAM_CREATE_MASK && id != PARAM_SHOW_GRID)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *field, *rfield;
    gdouble angle = get_rotation_angle(args);
    GwyInterpolationType interp = gwy_params_get_enum(params, PARAM_INTERPOLATION);
    GwyRotateResizeType resize = gwy_params_get_enum(params, PARAM_RESIZE);

    field = GWY_DATA_FIELD(gwy_container_get_object_by_name(gui->data, "/1/data"));
    rfield = gwy_data_field_new_rotated(field, NULL, angle, interp, resize);
    gwy_container_set_object_by_name(gui->data, "/0/data", rfield);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
    gtk_widget_set_size_request(gui->dataview, PREVIEW_SIZE, -1);
    update_grid(gui);
}

static void
update_grid(ModuleGUI *gui)
{
    GwySelection *selection = gui->selection;
    GwyParams *params = gui->args->params;
    GwyDataField *field;
    gdouble xy[4];

    if (!gwy_params_get_boolean(params, PARAM_SHOW_GRID)) {
        gwy_selection_clear(selection);
        return;
    }

    field = gwy_container_get_object_by_name(gui->data, "/0/data");
    xy[0] = gwy_data_field_get_xreal(field)/12.0;
    xy[1] = xy[2] = 0.0;
    xy[3] = gwy_data_field_get_yreal(field)/12.0;
    gwy_selection_set_data(selection, 1, xy);
}

static void
fix_mask_exterior(GwyDataField *mask, GwyDataField *exterior_mask,
                  gdouble fill_value)
{
    const gdouble *exm = gwy_data_field_get_data_const(exterior_mask);
    gdouble *m = gwy_data_field_get_data(mask);
    gint n, k;

    n = gwy_data_field_get_xres(mask) * gwy_data_field_get_yres(mask);
    for (k = 0; k < n; k++) {
        if (exm[k])
            m[k] = fill_value;
    }
}

static GwyPlaneSymmetry
guess_unrotate_symmetry(GwyDataField *field, gdouble *corrections)
{
    enum { nder = 4800 };
    GwyDataLine *derdist;
    GwyPlaneSymmetry symm;

    derdist = gwy_data_line_new(nder, 2.0*G_PI, FALSE);
    gwy_data_field_slope_distribution(field, derdist, 5);
    symm = gwy_data_field_unrotate_find_corrections(derdist, corrections);
    g_object_unref(derdist);

    return symm;
}

static gdouble
get_rotation_angle(ModuleArgs *args)
{
    GwyPlaneSymmetry symm;

    if (!args->is_unrotate)
        return gwy_params_get_double(args->params, PARAM_ANGLE);

    symm = gwy_params_get_enum(args->params, PARAM_SYMMETRY);
    if (symm == GWY_SYMMETRY_AUTO)
        symm = args->symm;
    return args->corrections[symm];
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
