/*
 *  $Id: correct_perspective.c 25638 2023-09-07 12:23:28Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

typedef enum {
    IMAGE_DATA,
    IMAGE_CORRECTED,
} ImageMode;

enum {
    PARAM_INTERPOLATION,
    PARAM_FIXRES,
    PARAM_XRES,
    PARAM_YRES,
    PARAM_IMAGE_MODE,
    PARAM_NEW_IMAGE,
    PARAM_DISTRIBUTE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    gdouble xy[8];
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *view;
    GwyVectorLayer *vlayer;
    GwySelection *selection;
    gboolean corrected_computed;
} ModuleGUI;

static gboolean         module_register                (void);
static GwyParamDef*     define_module_params           (void);
static void             correct_perspective            (GwyContainer *data,
                                                        GwyRunType run);
static void             gather_quarks_for_one_image    (GwyContainer *data,
                                                        gint id,
                                                        GArray *quarks);
static void             apply_correction_to_one_image  (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static GwyDialogOutcome run_gui                        (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static void             param_changed                  (ModuleGUI *gui,
                                                        gint id);
static void             dialog_response                (GwyDialog *dialog,
                                                        gint response,
                                                        ModuleGUI *gui);
static void             preview                        (gpointer user_data);
static void             init_coordinates               (GwyDataField *field,
                                                        gdouble *xy);
static void             selection_changed              (ModuleGUI *gui);
static void             guess_pixel_dimensions         (GwyParamTable *table,
                                                        GwySelection *selection,
                                                        GwyDataField *field);
static GwyDataField*    create_corrected_field         (GwyDataField *field,
                                                        const gdouble *xy,
                                                        gint xres,
                                                        gint yres,
                                                        GwyInterpolationType interp);
static gboolean         solve_projection_from_rectangle(const gdouble *xy,
                                                        gint xres,
                                                        gint yres,
                                                        gdouble *matrix);
static void             estimate_reasonable_dimensions (const gdouble *xy,
                                                        gdouble *lx,
                                                        gdouble *ly);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Corrects or applies perspective distortion of images."),
    "Yeti <yeti@gwyddion.net>",
    "2.2",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, correct_perspective)

static gboolean
module_register(void)
{
    gwy_process_func_register("correct_perspective",
                              (GwyProcessFunc)&correct_perspective,
                              N_("/_Distortion/_Perspective..."),
                              GWY_STOCK_PERSPECTIVE_DISTORT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Correct perspective distortion"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum image_modes[] = {
        { N_("_Data"),           IMAGE_DATA,      },
        { N_("Correc_ted data"), IMAGE_CORRECTED, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interpolation", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_LINEAR);
    gwy_param_def_add_boolean(paramdef, PARAM_FIXRES, "fixres", _("Specify output _dimensions"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_XRES, "xres", _("_X resolution"), 2, 32768, 512);
    gwy_param_def_add_int(paramdef, PARAM_YRES, "yres", _("_Y resolution"), 2, 32768, 512);
    gwy_param_def_add_gwyenum(paramdef, PARAM_IMAGE_MODE, NULL, gwy_sgettext("verb|Display"),
                              image_modes, G_N_ELEMENTS(image_modes), IMAGE_DATA);
    gwy_param_def_add_boolean(paramdef, PARAM_NEW_IMAGE, "new-image", _("Create new image"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DISTRIBUTE, "distribute", _("_Apply to all compatible images"), FALSE);
    return paramdef;
}

static void
correct_perspective(GwyContainer *data, GwyRunType run)
{
    enum { compat_flags = GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_REAL | GWY_DATA_COMPATIBILITY_LATERAL };
    ModuleArgs args;
    GwyParams *params;
    GwyDataField *field;
    GwySelection *selection;
    GwyDialogOutcome outcome;
    gint i, id;
    gboolean distribute, new_image;
    GArray *undo_quarks;
    gint *image_ids;
    gchar selkey[40];

    g_return_if_fail(run & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerProjective"));
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(field);

    gwy_clear(&args, 1);
    args.field = field;
    args.params = params = gwy_params_new_from_settings(define_module_params());
    init_coordinates(field, args.xy);
    g_snprintf(selkey, sizeof(selkey), "/%d/select/projective", id);
    if (gwy_container_gis_object_by_name(data, selkey, &selection)
        && gwy_selection_get_data(GWY_SELECTION(selection), NULL))
        gwy_selection_get_object(GWY_SELECTION(selection), 0, args.xy);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(params);

    selection = g_object_new(g_type_from_name("GwySelectionProjective"), "max-objects", 1, NULL);
    gwy_selection_set_object(GWY_SELECTION(selection), 0, args.xy);
    gwy_container_pass_object_by_name(data, selkey, selection);

    if (outcome != GWY_DIALOG_PROCEED)
        goto end;

    new_image = gwy_params_get_boolean(params, PARAM_NEW_IMAGE);
    distribute = gwy_params_get_boolean(params, PARAM_DISTRIBUTE);
    if (!distribute) {
        if (!new_image) {
            undo_quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));
            gather_quarks_for_one_image(data, id, undo_quarks);
            gwy_app_undo_qcheckpointv(data, undo_quarks->len, (GQuark*)undo_quarks->data);
            g_array_free(undo_quarks, TRUE);
        }
        apply_correction_to_one_image(&args, data, id);
        goto end;
    }

    image_ids = gwy_app_data_browser_get_data_ids(data);
    g_object_ref(field);
    if (!new_image) {
        undo_quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));
        for (i = 0; image_ids[i] != -1; i++) {
            GwyDataField *otherfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(image_ids[i]));
            if (!gwy_data_field_check_compatibility(field, otherfield, compat_flags))
                gather_quarks_for_one_image(data, image_ids[i], undo_quarks);
        }
        gwy_app_undo_qcheckpointv(data, undo_quarks->len, (GQuark*)undo_quarks->data);
        g_array_free(undo_quarks, TRUE);
    }
    for (i = 0; image_ids[i] != -1; i++) {
        GwyDataField *otherfield = gwy_container_get_object(data, gwy_app_get_data_key_for_id(image_ids[i]));
        if (gwy_data_field_check_compatibility(field, otherfield, compat_flags))
            continue;

        apply_correction_to_one_image(&args, data, image_ids[i]);
    }
    g_object_unref(field);
    g_free(image_ids);

end:
    g_object_unref(params);
}

static void
gather_quarks_for_one_image(GwyContainer *data, gint id, GArray *quarks)
{
    GObject *object;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(id);
    object = gwy_container_get_object(data, quark);
    g_assert(GWY_IS_DATA_FIELD(object));
    g_array_append_val(quarks, quark);

    quark = gwy_app_get_mask_key_for_id(id);
    if (gwy_container_gis_object(data, quark, &object) && GWY_IS_DATA_FIELD(object))
        g_array_append_val(quarks, quark);

    quark = gwy_app_get_show_key_for_id(id);
    if (gwy_container_gis_object(data, quark, &object) && GWY_IS_DATA_FIELD(object))
        g_array_append_val(quarks, quark);
}

static void
apply_correction_to_one_image(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyParams *params = args->params;
    GwyInterpolationType interpolation = gwy_params_get_enum(params, PARAM_INTERPOLATION);
    gboolean new_image = gwy_params_get_boolean(args->params, PARAM_NEW_IMAGE);
    gboolean distribute = gwy_params_get_boolean(params, PARAM_DISTRIBUTE);
    gboolean fixres = gwy_params_get_boolean(params, PARAM_FIXRES);
    gint xres = (fixres ? gwy_params_get_int(params, PARAM_XRES) : 0);
    gint yres = (fixres ? gwy_params_get_int(params, PARAM_YRES) : 0);
    GwyDataField *field, *mask = NULL, *show = NULL;
    gchar *title, *newtitle;
    gint newid;

    field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    g_assert(GWY_IS_DATA_FIELD(field));
    gwy_container_gis_object(data, gwy_app_get_mask_key_for_id(id), &mask);
    gwy_container_gis_object(data, gwy_app_get_show_key_for_id(id), &show);

    field = create_corrected_field(field, args->xy, xres, yres, interpolation);
    if (!new_image) {
        gwy_container_set_object(data, gwy_app_get_data_key_for_id(id), field);
        newid = id;
    }
    else {
        newid = gwy_app_data_browser_add_data_field(field, data, !distribute);
        title = gwy_app_get_data_field_title(data, id);
        newtitle = g_strconcat(title, " ", _("Corrected"), NULL);
        gwy_app_set_data_field_title(data, newid, newtitle);
        g_free(newtitle);
        g_free(title);
    }
    g_object_unref(field);

    if (mask) {
        mask = create_corrected_field(mask, args->xy, xres, yres, GWY_INTERPOLATION_ROUND);
        gwy_container_pass_object(data, gwy_app_get_mask_key_for_id(newid), mask);
    }
    if (show) {
        show = create_corrected_field(show, args->xy, xres, yres, interpolation);
        gwy_container_pass_object(data, gwy_app_get_show_key_for_id(newid), show);
    }

    if (!new_image) {
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_RANGE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
    }
    gwy_app_channel_log_add_proc(data, id, newid);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    GwyDialogOutcome outcome;

    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Projective Correction"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.view), 0, "Projective", 1, TRUE);
    g_object_ref(gui.selection);
    gwy_selection_set_data(gui.selection, 1, args->xy);
    gui.vlayer = gwy_data_view_get_top_layer(GWY_DATA_VIEW(gui.view));
    g_object_ref(gui.vlayer);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(selection_changed), &gui);

    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_radio(table, PARAM_IMAGE_MODE);
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_FIXRES);
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_set_no_reset(table, PARAM_XRES, TRUE);
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_set_no_reset(table, PARAM_YRES, TRUE);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_NEW_IMAGE);
    gwy_param_table_append_checkbox(table, PARAM_DISTRIBUTE);

    if (!gwy_params_get_boolean(args->params, PARAM_FIXRES))
        guess_pixel_dimensions(table, gui.selection, args->field);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.selection);
    g_object_unref(gui.vlayer);
    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_FIXRES) {
        gboolean fixres = gwy_params_get_boolean(params, PARAM_FIXRES);
        gwy_param_table_set_sensitive(table, PARAM_XRES, fixres);
        gwy_param_table_set_sensitive(table, PARAM_YRES, fixres);
        if (!fixres)
            guess_pixel_dimensions(table, gui->selection, gui->args->field);
    }
    if (id < 0 || id == PARAM_INTERPOLATION) {
        gui->corrected_computed = FALSE;
        if (gwy_params_get_enum(params, PARAM_IMAGE_MODE) == IMAGE_CORRECTED)
            gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
    }
    if (id < 0 || id == PARAM_IMAGE_MODE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;

    if (response == GWY_RESPONSE_RESET) {
        init_coordinates(args->field, args->xy);
        gwy_selection_set_data(gui->selection, 1, args->xy);
        guess_pixel_dimensions(gui->table, gui->selection, args->field);
    }
}

static void
selection_changed(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;

    gwy_selection_get_object(gui->selection, 0, args->xy);
    if (!gwy_params_get_boolean(args->params, PARAM_FIXRES))
        guess_pixel_dimensions(gui->table, gui->selection, args->field);
    gui->corrected_computed = FALSE;
}

static void
guess_pixel_dimensions(GwyParamTable *table, GwySelection *selection, GwyDataField *field)
{
    gdouble xypix[8], xy[8];
    gdouble newxpix, newypix;
    gint i;

    gwy_selection_get_object(selection, 0, xy);
    for (i = 0; i < 4; i++) {
        xypix[2*i + 0] = gwy_data_field_rtoj(field, xy[2*i + 0]);
        xypix[2*i + 1] = gwy_data_field_rtoi(field, xy[2*i + 1]);
    }
    estimate_reasonable_dimensions(xypix, &newxpix, &newypix);
    gwy_param_table_set_int(table, PARAM_XRES, MAX(GWY_ROUND(newxpix + 1.0), 2));
    gwy_param_table_set_int(table, PARAM_YRES, MAX(GWY_ROUND(newypix + 1.0), 2));
}

static void
init_coordinates(GwyDataField *field, gdouble *xy)
{
    gdouble xreal, yreal;
    gint i, j;

    xreal = gwy_data_field_get_xreal(field);
    yreal = gwy_data_field_get_yreal(field);
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            xy[2*(2*i + j) + 0] = 0.25*xreal + 0.5*(i ? 1-j : j)*xreal;
            xy[2*(2*i + j) + 1] = 0.25*yreal + 0.5*i*yreal;
        }
    }
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    ImageMode mode = gwy_params_get_enum(args->params, PARAM_IMAGE_MODE);
    GwyDataView *dataview = GWY_DATA_VIEW(gui->view);
    GwyPixmapLayer *layer = gwy_data_view_get_base_layer(dataview);
    GwyDataField *corrected;

    if (mode == IMAGE_DATA) {
        g_object_set(layer, "data-key", "/0/data", NULL);
        gwy_data_view_set_top_layer(dataview, gui->vlayer);
        gwy_set_data_preview_size(dataview, PREVIEW_SIZE);
        return;
    }

    if (!gui->corrected_computed) {
        GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);
        corrected = create_corrected_field(args->field, args->xy, 0, 0, interpolation);
        gwy_container_pass_object_by_name(gui->data, "/1/data", corrected);
        gui->corrected_computed = TRUE;
    }
    g_object_set(layer, "data-key", "/1/data", NULL);
    gwy_data_view_set_top_layer(dataview, NULL);
    gwy_set_data_preview_size(dataview, PREVIEW_SIZE);
}

static void
project(gdouble x, gdouble y, gdouble *px, gdouble *py, gpointer user_data)
{
    const gdouble *matrix = (const gdouble*)user_data;
    const gdouble *mx = matrix, *my = matrix + 3, *m1 = matrix + 6;
    gdouble d;

    d = m1[0]*x + m1[1]*y + m1[2];
    *px = (mx[0]*x + mx[1]*y + mx[2])/d;
    *py = (my[0]*x + my[1]*y + my[2])/d;
}

static void
estimate_reasonable_dimensions(const gdouble *xy, gdouble *lx, gdouble *ly)
{
    gdouble lx1, lx2, ly1, ly2;

    lx1 = hypot(xy[2] - xy[0], xy[3] - xy[1]);
    lx2 = hypot(xy[6] - xy[4], xy[7] - xy[5]);
    ly1 = hypot(xy[4] - xy[2], xy[5] - xy[3]);
    ly2 = hypot(xy[0] - xy[6], xy[1] - xy[5]);

    *lx = hypot(lx1, lx2)/G_SQRT2;
    *ly = hypot(ly1, ly2)/G_SQRT2;
}

static GwyDataField*
create_corrected_field(GwyDataField *field,
                       const gdouble *xy,
                       gint xres, gint yres,
                       GwyInterpolationType interp)
{
    GwyDataField *corrected;
    gdouble matrix[9], xypix[8];
    gdouble newxreal, newyreal, newxpix, newypix;
    gint i, newxres, newyres;

    estimate_reasonable_dimensions(xy, &newxreal, &newyreal);
    for (i = 0; i < 4; i++) {
        xypix[2*i + 0] = gwy_data_field_rtoj(field, xy[2*i + 0]);
        xypix[2*i + 1] = gwy_data_field_rtoi(field, xy[2*i + 1]);
    }
    if (xres && yres) {
        newxpix = newxres = xres;
        newypix = newyres = yres;
    }
    else {
        estimate_reasonable_dimensions(xypix, &newxpix, &newypix);
        newxres = MAX(GWY_ROUND(newxpix + 1.0), 2);
        newyres = MAX(GWY_ROUND(newypix + 1.0), 2);
    }
    corrected = gwy_data_field_new(newxres, newyres, newxreal*newxres/newxpix, newyreal*newyres/newypix, FALSE);
    gwy_data_field_copy_units(field, corrected);

    solve_projection_from_rectangle(xypix, newxres, newyres, matrix);
    gwy_data_field_distort(field, corrected, project, matrix, interp, GWY_EXTERIOR_MIRROR_EXTEND, 0.0);

    return corrected;
}

static gboolean
solve_projection(const gdouble *xyfrom, const gdouble *xyto, gdouble *matrix)
{
    gdouble a[64], rhs[8];
    guint i;

    gwy_clear(a, 64);
    for (i = 0; i < 4; i++) {
        gdouble xf = xyfrom[2*i + 0], yf = xyfrom[2*i + 1];
        gdouble xt = xyto[2*i + 0], yt = xyto[2*i + 1];
        gdouble *axrow = a + 16*i, *ayrow = axrow + 8, *r = rhs + 2*i;

        axrow[0] = ayrow[3] = xf;
        axrow[1] = ayrow[4] = yf;
        axrow[2] = ayrow[5] = 1.0;
        axrow[6] = -xf*xt;
        axrow[7] = -yf*xt;
        ayrow[6] = -xf*yt;
        ayrow[7] = -yf*yt;
        r[0] = xt;
        r[1] = yt;
    }

    if (!gwy_math_lin_solve_rewrite(8, a, rhs, matrix))
        return FALSE;

    matrix[8] = 1.0;
    return TRUE;
}

static gboolean
solve_projection_from_rectangle(const gdouble *xy, gint xres, gint yres, gdouble *matrix)
{
    gdouble rectangle[8];

    rectangle[0] = rectangle[1] = rectangle[3] = rectangle[6] = 0.5;
    rectangle[2] = rectangle[4] = xres - 0.5;
    rectangle[5] = rectangle[7] = yres - 0.5;

    return solve_projection(rectangle, xy, matrix);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
