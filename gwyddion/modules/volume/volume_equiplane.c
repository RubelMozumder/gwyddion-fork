/*
 *  $Id: volume_equiplane.c 25569 2023-07-26 14:13:05Z yeti-dn $
 *  Copyright (C) 2019-2023 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_ZPOS,
    PARAM_SEARCH_DIR,
    PARAM_KEEP_SIGN,
    PARAM_SHOW_TYPE,
    PARAM_UPDATE,

    INFO_WVALUE,
};

typedef enum {
    SHOW_DATA  = 0,
    SHOW_RESULT = 1,
} EquiplaneShow;

typedef enum {
    SEARCH_DOWN      = -1,
    SEARCH_BILATERAL = 0,
    SEARCH_UP        = 1,
} EquiplaneSearchDir;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyDataField *result;
    GwyDataLine *calibration;
    gdouble value;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyContainer *data;
    GwyDataField *xyplane;
    GtkWidget *dialog;
    GwyParamTable *table_value;
    GwyParamTable *table_output;
    GwyGraphModel *gmodel;
    GtkWidget *dataview;
    GwySelection *image_selection;
    GwySelection *graph_selection;
    GwySIValueFormat *vf;
} ModuleGUI;

static gboolean         module_register        (void);
static GwyParamDef*     define_module_params   (void);
static void             equiplane              (GwyContainer *data,
                                                GwyRunType run);
static void             execute                (ModuleArgs *args);
static GwyDialogOutcome run_gui                (ModuleArgs *args,
                                                GwyContainer *data,
                                                gint id);
static void             param_changed          (ModuleGUI *gui,
                                                gint id);
static void             dialog_response_after  (GtkDialog *dialog,
                                                gint response,
                                                ModuleGUI *gui);
static void             point_selection_changed(ModuleGUI *gui,
                                                gint id,
                                                GwySelection *selection);
static void             graph_selection_changed(ModuleGUI *gui,
                                                gint id,
                                                GwySelection *selection);
static void             extract_xyplane        (ModuleGUI *gui);
static void             preview                (gpointer user_data);
static void             extract_graph_curve    (ModuleArgs *args,
                                                GwyGraphCurveModel *gcmodel);
static gdouble          get_constant_value     (ModuleArgs *args);
static void             setup_gmodel           (ModuleArgs *args,
                                                GwyGraphModel *gmodel);
static void             sanitise_params        (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Extracts z-coordinates of isosurfaces from volume data to an image."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "2.0",
    "Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, volume_equiplane)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_equiplane",
                             (GwyVolumeFunc)&equiplane,
                             N_("/SPM M_odes/_Isosurface Image..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Extract z-coordinates of isosurface"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("_Data"),   SHOW_DATA,  },
        { N_("_Result"), SHOW_RESULT, },
    };
    static const GwyEnum searchdirs[] = {
        { N_("Upwards"),         SEARCH_UP,        },
        { N_("Downwards"),       SEARCH_DOWN,      },
        { N_("Both directions"), SEARCH_BILATERAL, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", _("_X"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", _("_Y"), -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_ZPOS, "zpos", _("_Z value"), -1, G_MAXINT, -1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SEARCH_DIR, "search_dir", _("_Search direction"),
                              searchdirs, G_N_ELEMENTS(searchdirs), SEARCH_BILATERAL);
    gwy_param_def_add_boolean(paramdef, PARAM_KEEP_SIGN, "keep_sign", _("Preserve _intersection sign"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SHOW_TYPE, "show_type", gwy_sgettext("verb|_Display"),
                              displays, G_N_ELEMENTS(displays), SHOW_DATA);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static GwySIValueFormat*
make_zvf(GwyBrick *brick)
{
    return gwy_si_unit_get_format_with_digits(gwy_brick_get_si_unit_z(brick), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                              gwy_brick_get_zreal(brick), 5, NULL);
}

static void
equiplane(GwyContainer *data, GwyRunType run)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    GwySIValueFormat *zvf;
    GwyBrick *brick;
    gint id, newid;
    GwySIUnit *zunit;
    gdouble const_value;
    gchar *title;

    g_return_if_fail(run & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));

    brick = args.brick;
    args.calibration = gwy_brick_get_zcalibration(brick);
    if (args.calibration && gwy_brick_get_zres(brick) != gwy_data_line_get_res(args.calibration))
        args.calibration = NULL;
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    args.result = gwy_data_field_new(1, 1, 1.0, 1.0, TRUE);
    gwy_brick_extract_xy_plane(brick, args.result, 0);
    gwy_data_field_clear(args.result);
    /* Do this after gwy_brick_extract_xy_plane() because the units actually differ. */
    zunit = (args.calibration ? gwy_data_line_get_si_unit_y(args.calibration) : gwy_brick_get_si_unit_z(args.brick));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(args.result), zunit);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    zvf = make_zvf(brick);
    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    const_value = get_constant_value(&args);
    title = g_strdup_printf(_("Isosurface z for %.*f %s"), zvf->precision, const_value/zvf->magnitude, zvf->units);
    gwy_container_set_string(data, gwy_app_get_data_key_for_id(newid), title);
    gwy_app_channel_log_add(data, -1, newid, "volume::volume_equiplane", NULL);
    gwy_si_unit_value_format_free(zvf);

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyBrick *brick = args->brick;
    GtkWidget *hbox, *align;
    GwyGraphArea *area;
    GwyGraph *graph;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gui.xyplane = gwy_data_field_new_alike(args->result, FALSE);
    extract_xyplane(&gui);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->result);

    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.vf = gwy_si_unit_get_format_with_digits(gwy_brick_get_si_unit_w(brick), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                gwy_brick_get_max(brick) - gwy_brick_get_min(brick), 5, /* 5 digits */
                                                NULL);

    gui.gmodel = gwy_graph_model_new();
    setup_gmodel(args, gui.gmodel);

    gui.dialog = gwy_dialog_new(_("Extract Z Isosurfaces"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 4);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), gui.dataview);
    gui.image_selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.dataview), 0, "Point", 1, TRUE);

    graph = GWY_GRAPH(gwy_graph_new(gui.gmodel));
    gwy_graph_enable_user_input(graph, FALSE);
    gtk_widget_set_size_request(GTK_WIDGET(graph), PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(graph), TRUE, TRUE, 0);

    area = GWY_GRAPH_AREA(gwy_graph_get_area(graph));
    gwy_graph_area_set_status(area, GWY_GRAPH_STATUS_XLINES);
    gui.graph_selection = gwy_graph_area_get_selection(area, GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_max_objects(gui.graph_selection, 1);

    hbox = gwy_hbox_new(24);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox, TRUE, TRUE, 4);

    gui.table_value = table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_ZPOS);
    gwy_param_table_slider_restrict_range(table, PARAM_ZPOS, 0, gwy_brick_get_zres(brick)-1);
    gwy_param_table_slider_add_alt(table, PARAM_ZPOS);
    if (args->calibration)
        gwy_param_table_alt_set_calibration(table, PARAM_ZPOS, args->calibration);
    else
        gwy_param_table_alt_set_brick_pixel_z(table, PARAM_ZPOS, brick);
    gwy_param_table_append_info(table, INFO_WVALUE, _("Constant value"));
    gwy_param_table_set_unitstr(table, INFO_WVALUE, gui.vf->units);
    gwy_param_table_append_combo(table, PARAM_SEARCH_DIR);
    gwy_param_table_append_checkbox(table, PARAM_KEEP_SIGN);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    gui.table_output = table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_SHOW_TYPE);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.table_value, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.image_selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    g_signal_connect_swapped(gui.graph_selection, "changed", G_CALLBACK(graph_selection_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response_after), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.xyplane);
    g_object_unref(gui.data);
    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static void
point_selection_changed(ModuleGUI *gui,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
    gdouble xy[2];

    if (!gwy_selection_get_object(selection, 0, xy))
        return;

    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(gwy_brick_rtoi(brick, xy[0]), 0, xres-1));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(gwy_brick_rtoj(brick, xy[1]), 0, yres-1));
    gwy_param_table_param_changed(gui->table_value, PARAM_XPOS);
}

static void
graph_selection_changed(ModuleGUI *gui,
                        G_GNUC_UNUSED gint id,
                        GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint lev, zres = gwy_brick_get_zres(brick);
    gdouble z;

    /* XXX: When clicking on a new position graph emits two updates, one with old selected line removed and another
     * with the new selection. It is silly. Just ignore updates with no selected line. */
    if (!gwy_selection_get_object(selection, 0, &z))
        return;

    lev = GWY_ROUND(gwy_brick_rtok_cal(brick, z));
    lev = CLAMP(lev, 0, zres-1);
    gwy_param_table_set_int(gui->table_value, PARAM_ZPOS, lev);
}

static void
update_constant_value(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwySIValueFormat *vf = gui->vf;
    gdouble const_value = get_constant_value(args);
    gchar *s;

    s = g_strdup_printf("%.*f", vf->precision, const_value/vf->magnitude);
    gwy_param_table_info_set_valuestr(gui->table_value, INFO_WVALUE, s);
    g_free(s);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    EquiplaneShow show_type = gwy_params_get_enum(params, PARAM_SHOW_TYPE);
    gdouble z;

    if (id < 0 || id == PARAM_ZPOS || id == PARAM_SHOW_TYPE) {
        z = gwy_brick_ktor_cal(args->brick, gwy_params_get_int(params, PARAM_ZPOS));
        gwy_selection_set_object(gui->graph_selection, 0, &z);
        /* preview() is not called when instant updates are disabled – but we still want to update the image if
         * original data are show. */
        if (show_type == SHOW_DATA && !gwy_params_get_boolean(params, PARAM_UPDATE)) {
            extract_xyplane(gui);
            gwy_data_field_data_changed(gui->xyplane);
        }
    }
    if (id < 0 || id == PARAM_XPOS || id == PARAM_YPOS)
        extract_graph_curve(args, gwy_graph_model_get_curve(gui->gmodel, 0));
    if (id < 0 || id == PARAM_ZPOS || id == PARAM_XPOS || id == PARAM_YPOS)
        update_constant_value(gui);
    if (id < 0 || id == PARAM_SHOW_TYPE) {
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0),
                                 show_type == SHOW_DATA ? gui->xyplane : args->result);
    }
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
extract_xyplane(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint lev = gwy_params_get_int(args->params, PARAM_ZPOS);

    gwy_brick_extract_xy_plane(args->brick, gui->xyplane, lev);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    EquiplaneShow show_type = gwy_params_get_enum(args->params, PARAM_SHOW_TYPE);

    if (show_type == SHOW_RESULT) {
        execute(args);
        gwy_data_field_data_changed(args->result);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
    /* Keep the result invalidated when showing the original data not running execute()! */
    if (show_type == SHOW_DATA) {
        extract_xyplane(gui);
        gwy_data_field_data_changed(gui->xyplane);
    }
}

static void
decide_intersection_signs(ModuleArgs *args, gboolean *find_increasing, gboolean *find_decreasing)
{
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint xpos = gwy_params_get_int(args->params, PARAM_XPOS);
    gint ypos = gwy_params_get_int(args->params, PARAM_YPOS);
    gint zpos = gwy_params_get_int(args->params, PARAM_ZPOS);
    gboolean keep_sign = gwy_params_get_boolean(args->params, PARAM_KEEP_SIGN);
    const gdouble *d = gwy_brick_get_data_const(brick);
    gdouble value, lower, upper;

    *find_increasing = *find_decreasing = TRUE;
    if (!keep_sign)
        return;

    value = d[zpos*xres*yres + ypos*xres + xpos];
    lower = d[MAX(zpos-1, 0)*xres*yres + ypos*xres + xpos];
    upper = d[MIN(zpos+1, zres-1)*xres*yres + ypos*xres + xpos];
    if ((upper > value && lower <= value) || (upper >= value && lower < value)) {
        *find_decreasing = FALSE;
        return;
    }
    if ((upper < value && lower >= value) || (upper <= value && lower > value)) {
        *find_increasing = FALSE;
        return;
    }
    /* Otherwise we are confused and will look for both signs. */
}

static void
isosurface_nearest(ModuleArgs *args)
{
    GwyBrick *brick = args->brick;
    GwyDataField *field = args->result;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint zpos = gwy_params_get_int(args->params, PARAM_ZPOS);
    gint scan_dir = (gint)gwy_params_get_enum(args->params, PARAM_SEARCH_DIR);
    gint k, dist, lev, dir;
    gdouble const_value = get_constant_value(args);
    const gdouble *lowerplane, *upperplane, *d = gwy_brick_get_data_const(brick);
    gdouble *f = gwy_data_field_get_data(field);
    gint n = xres*yres, remaining = n;
    gint *intersection = g_new(gint, n);
    gboolean find_increasing, find_decreasing;

    decide_intersection_signs(args, &find_increasing, &find_decreasing);
    n = xres*yres;
    for (k = 0; k < n; k++)
        intersection[k] = G_MAXINT;

    dist = (scan_dir < 0 ? 1 : 0);
    dir = (scan_dir < 0 ? -1 : 1);
    while (remaining && dist < zres-1) {
        gint intersections_in_plane = 0;

        lev = zpos + dir*dist;
        if (lev < 0 || lev >= zres-1)
            goto next_iter;

        lowerplane = d + n*lev;
        upperplane = d + n*(lev + 1);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            shared(lowerplane,upperplane,intersection,lev,n,const_value,find_increasing,find_decreasing) \
            private(k) \
            reduction(+:intersections_in_plane)
#endif
        for (k = 0; k < n; k++) {
            gdouble lower = lowerplane[k], upper = upperplane[k];

            if (intersection[k] != G_MAXINT)
                continue;

            if (find_increasing && (fmin(upper, fmax(lower, const_value)) == const_value)) {
                intersection[k] = lev;
                intersections_in_plane++;
                continue;
            }
            if (find_decreasing && (fmin(lower, fmax(upper, const_value)) == const_value)) {
                intersection[k] = lev;
                intersections_in_plane++;
                continue;
            }
        }

next_iter:
        /* Move to further distance. But for bidirectional scan (scan_dir == 0), alternate. */
        if (scan_dir != 0)
            dist++;
        else {
            if (dir < 0)
                dist++;
            dir = -dir;
        }
        remaining -= intersections_in_plane;
    }

    for (k = 0; k < n; k++) {
        gdouble lower, upper, lev_interp;

        lev = intersection[k];
        if (lev == G_MAXINT) {
            f[k] = gwy_brick_ktor_cal(brick, zpos);
            continue;
        }

        lower = d[n*lev + k];
        upper = d[n*(lev + 1) + k];
        if (lower == upper)
            lev_interp = lev + 0.5;
        else {
            lev_interp = (const_value - lower)/(upper - lower);
            lev_interp = lev + fmax(fmin(lev_interp, 1.0), 0.0);
        }
        f[k] = gwy_brick_ktor_cal(brick, lev_interp);
    }

    g_free(intersection);
}

static void
execute(ModuleArgs *args)
{
    isosurface_nearest(args);
}

static void
extract_graph_curve(ModuleArgs *args, GwyGraphCurveModel *gcmodel)
{
    GwyBrick *brick = args->brick;
    gint zres = gwy_brick_get_zres(brick);
    gint xpos = gwy_params_get_int(args->params, PARAM_XPOS);
    gint ypos = gwy_params_get_int(args->params, PARAM_YPOS);
    GwyDataLine *line, *calibration = NULL;

    line = gwy_data_line_new(1, 1.0, FALSE);
    gwy_brick_extract_line(brick, line, xpos, ypos, 0, xpos, ypos, zres, FALSE);
    gwy_data_line_set_offset(line, gwy_brick_get_zoffset(brick));

    if (args->calibration) {
        gwy_graph_curve_model_set_data(gcmodel,
                                       gwy_data_line_get_data(calibration), gwy_data_line_get_data(line),
                                       gwy_data_line_get_res(line));
        gwy_graph_curve_model_enforce_order(gcmodel);
    }
    else
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);

    g_object_unref(line);
}

static gdouble
get_constant_value(ModuleArgs *args)
{
    gint xpos = gwy_params_get_int(args->params, PARAM_XPOS);
    gint ypos = gwy_params_get_int(args->params, PARAM_YPOS);
    gint zpos = gwy_params_get_int(args->params, PARAM_ZPOS);

    return gwy_brick_get_val(args->brick, xpos, ypos, zpos);
}

static void
setup_gmodel(ModuleArgs *args, GwyGraphModel *gmodel)
{
    GwyBrick *brick = args->brick;
    GwyDataLine *calibration = args->calibration;
    GwySIUnit *xunit = (calibration ? gwy_data_line_get_si_unit_y(calibration) : gwy_brick_get_si_unit_z(brick));
    GwyGraphCurveModel *gcmodel;

    g_object_set(gmodel,
                 "label-visible", FALSE,
                 "si-unit-x", xunit,
                 "si-unit-y", gwy_brick_get_si_unit_w(brick),
                 "axis-label-bottom", "z",
                 "axis-label-left", "w",
                 NULL);
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
}

static void
dialog_response_after(G_GNUC_UNUSED GtkDialog *dialog, gint response, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;

    if (response == GWY_RESPONSE_RESET) {
        gwy_params_set_int(args->params, PARAM_XPOS, gwy_brick_get_xres(brick)/2);
        gwy_params_set_int(args->params, PARAM_YPOS, gwy_brick_get_yres(brick)/2);
        gwy_params_set_int(args->params, PARAM_ZPOS, gwy_brick_get_zres(brick)/2);
    }
}

static inline void
clamp_int_param(GwyParams *params, gint id, gint min, gint max, gint default_value)
{
    gint p = gwy_params_get_int(params, id);

    if (p < min || p > max)
        gwy_params_set_int(params, id, default_value);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);

    clamp_int_param(params, PARAM_XPOS, 0, xres-1, xres/2);
    clamp_int_param(params, PARAM_YPOS, 0, yres-1, yres/2);
    clamp_int_param(params, PARAM_ZPOS, 0, zres-1, zres/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
