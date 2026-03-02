/*
 *  $Id: xyz_raster.c 25483 2023-06-23 13:29:34Z yeti-dn $
 *  Copyright (C) 2016-2023 David Necas (Yeti).
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libprocess/triangulation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "../process/preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE | GWY_RUN_IMMEDIATE)

#define EPSREL 1e-8

/* Use smaller cell sides than the triangulation algorithm as we only need them for identical point detection and
 * border extension. */
#define CELL_SIDE 1.6

enum {
    RESPONSE_RESET_RANGES = 1000,
    RESPONSE_UNZOOM_RANGES,
    RESPONSE_SQUARE_PIXELS,
    UNDEF = G_MAXUINT,
};

enum {
    PARAM_INTERPOLATION,
    PARAM_EXTERIOR,
    PARAM_MASK_EMPTY,
    PARAM_XRES,
    PARAM_YRES,
    PARAM_FIELD_POWER,

    PARAM_XMIN,
    PARAM_XMAX,
    PARAM_YMIN,
    PARAM_YMAX,

    INFO_NUM_POINTS,
    INFO_NUM_MERGED,
    INFO_NUM_ADDED,
    MESSAGE_ERROR,
    BUTTON_SQUARE_PIXELS,
    BUTTON_UNZOOM_RANGES,
    BUTTON_RESET_RANGES,
};

enum {
    GWY_INTERPOLATION_FIELD = -1,
    GWY_INTERPOLATION_AVERAGE = -2,
};

typedef enum {
    LAST_UPDATED_X,
    LAST_UPDATED_Y
} XYZRasLastUpdated;

typedef struct {
    GwyTriangulation *triangulation;
    GArray *points;
    guint norigpoints;
    guint nbasepoints;
    gdouble step;
} XYZRasData;

typedef struct {
    GwyXY min;
    GwyXY max;
} XYZRasRange;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    GwyDataField *regular;
    GwyDataField *raster;
    GwyDataField *nilmask;
    XYZRasData rdata;
    XYZRasRange full_range; /* Real, not scaled. */
    GwySIValueFormat *vf;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GwyContainer *data;
    GtkWidget *dialog;
    GtkWidget *directbox;
    GtkWidget *dataview;
    GwySelection *selection;
    GwyParamTable *table;
    GArray *zoom_stack;
    gboolean changing_selection;
    XYZRasLastUpdated last_updated;
} ModuleGUI;

typedef struct {
    guint *id;
    guint pos;
    guint len;
    guint size;
} WorkQueue;

static gboolean         module_register            (void);
static void             xyzras                     (GwyContainer *data,
                                                    GwyRunType run);
static GwyParamDef*     define_module_params       (void);
static gboolean         execute                    (ModuleArgs *args,
                                                    GtkWindow *window,
                                                    gchar **error);
static void             set_raster_field_properties(ModuleArgs *args);
static void             add_field_to_data          (GwyDataField *field,
                                                    GwyDataField *mask,
                                                    GwyContainer *data,
                                                    gint id);
static GwyDialogOutcome run_gui                    (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static void             set_range                  (ModuleGUI *gui,
                                                    const XYZRasRange *range);
static void             update_selection           (ModuleGUI *gui);
static void             selection_changed          (ModuleGUI *gui,
                                                    gint hint,
                                                    GwySelection *selection);
static void             preview                    (gpointer user_data);
static void             triangulation_info         (ModuleGUI *gui);
static void             render_regular_directly    (ModuleGUI *gui);
static gboolean         interpolate_field          (guint npoints,
                                                    const GwyXYZ *points,
                                                    GwyDataField *field,
                                                    gdouble power,
                                                    GwySetFractionFunc set_fraction,
                                                    GwySetMessageFunc set_message);
static gboolean         extend_borders             (ModuleArgs *args,
                                                    gboolean check_for_changes,
                                                    gdouble epsrel);
static void             initialise_range           (ModuleArgs *args);
static void             set_range_from_field       (GwyDataField *field,
                                                    XYZRasRange *range);
static void             analyse_points             (ModuleArgs *args,
                                                    double epsrel);
static GwyDataField*    check_regular_grid         (GwySurface *surface);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Rasterizes XYZ data to images."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, xyz_raster)

static gboolean
module_register(void)
{
    gwy_xyz_func_register("xyz_raster",
                          (GwyXYZFunc)&xyzras,
                          N_("/_Rasterize..."),
                          GWY_STOCK_RASTERIZE,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Rasterize to image"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum interpolations[] = {
        { N_("Round"),   GWY_INTERPOLATION_ROUND,   },
        { N_("NNA"),     GWY_INTERPOLATION_NNA,     },
        { N_("Linear"),  GWY_INTERPOLATION_LINEAR,  },
        { N_("Field"),   GWY_INTERPOLATION_FIELD,   },
        { N_("Average"), GWY_INTERPOLATION_AVERAGE, },
    };
    static const GwyEnum exteriors[] = {
        { N_("exterior|Border"),   GWY_EXTERIOR_BORDER_EXTEND, },
        { N_("exterior|Mirror"),   GWY_EXTERIOR_MIRROR_EXTEND, },
        { N_("exterior|Periodic"), GWY_EXTERIOR_PERIODIC,      },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_xyz_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_INTERPOLATION, "interpolation", _("_Interpolation type"),
                              interpolations, G_N_ELEMENTS(interpolations), GWY_INTERPOLATION_AVERAGE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_EXTERIOR, "exterior", _("_Exterior type"),
                              exteriors, G_N_ELEMENTS(exteriors), GWY_EXTERIOR_MIRROR_EXTEND);
    gwy_param_def_add_boolean(paramdef, PARAM_MASK_EMPTY, "mask_empty", _("_Mask empty regions"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_XRES, "xres", _("_Horizontal size"), 2, 16384, 512);
    gwy_param_def_add_int(paramdef, PARAM_YRES, "yres", _("_Vertical size"), 2, 16384, 512);
    gwy_param_def_add_double(paramdef, PARAM_FIELD_POWER, "field_power", _("_Power"), 0.5, 5.0, 2.0);
    /* Area is GUI-only. */
    gwy_param_def_add_double(paramdef, PARAM_XMIN, NULL, _("_X-range"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_XMAX, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YMIN, NULL, _("_Y-range"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_YMAX, NULL, NULL, -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);

    return paramdef;
}

static void
xyzras(GwyContainer *data, GwyRunType run)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GtkWidget *dialog;
    gchar *error = NULL;
    gboolean ok = TRUE;
    gint id;

    g_return_if_fail(run & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerRectangle"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                     GWY_APP_SURFACE_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_SURFACE(args.surface));

    args.regular = check_regular_grid(args.surface);
    if (args.regular && run == GWY_RUN_IMMEDIATE) {
        add_field_to_data(args.regular, NULL, data, id);
        g_object_unref(args.regular);
        return;
    }

    args.params = gwy_params_new_from_settings(define_module_params());
    args.rdata.points = g_array_new(FALSE, FALSE, sizeof(GwyXYZ));
    analyse_points(&args, EPSREL);
    initialise_range(&args);
    args.vf = gwy_surface_get_value_format_xy(args.surface, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
    if (args.regular)
        args.raster = gwy_data_field_duplicate(args.regular);
    else {
        args.raster = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                         args.full_range.max.x - args.full_range.min.x,
                                         args.full_range.max.y - args.full_range.min.y,
                                         TRUE);
        gwy_data_field_set_xoffset(args.raster, args.full_range.min.x);
        gwy_data_field_set_yoffset(args.raster, args.full_range.min.y);
        gwy_surface_copy_units_to_data_field(args.surface, args.raster);
    }

    if (run == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    if (outcome != GWY_DIALOG_HAVE_RESULT)
        ok = execute(&args, gwy_app_find_window_for_xyz(data, id), &error);

    if (ok)
        add_field_to_data(args.raster, args.nilmask, data, id);
    else if (run == GWY_RUN_INTERACTIVE) {
        dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                        "%s", error);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
    g_free(error);

end:
    GWY_OBJECT_UNREF(args.rdata.triangulation);
    gwy_si_unit_value_format_free(args.vf);
    g_array_free(args.rdata.points, TRUE);
    g_object_unref(args.params);
}

static void
add_field_to_data(GwyDataField *field, GwyDataField *mask,
                   GwyContainer *data, gint id)
{
    const guchar *s;
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    if (mask)
        gwy_container_set_object(data,  gwy_app_get_mask_key_for_id(newid), mask);
    gwy_app_channel_log_add(data, -1, newid, "xyz::xyz_raster", NULL);

    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &s))
        gwy_container_set_const_string(data, gwy_app_get_data_palette_key_for_id(newid), s);
    if (gwy_container_gis_string(data, gwy_app_get_surface_title_key_for_id(id), &s))
        gwy_container_set_const_string(data, gwy_app_get_data_title_key_for_id(newid), s);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *label, *hbox, *button;
    GwyDialogOutcome outcome;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    const guchar *gradient;
    GwySelection *selection;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.zoom_stack = g_array_new(FALSE, FALSE, sizeof(XYZRasRange));
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->raster);
    /* We actually measure the ranges in displayed units; they are GUI-only. Convert them through the selection-changed
     * chain. */
    gui.last_updated = LAST_UPDATED_X;

    gui.dialog = gwy_dialog_new(_("Rasterize XYZ Data"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    if (args->regular) {
        hbox = gui.directbox = gwy_hbox_new(8);
        gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
        gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

        button = gtk_button_new_with_mnemonic(_("Create Image _Directly"));
        gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
        g_signal_connect_swapped(button, "clicked", G_CALLBACK(render_regular_directly), &gui);

        label = gtk_label_new(_("XY points form a regular grid so interpolation is not necessary."));
        gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    }

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    selection = gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(gui.dataview), 0, "Rectangle", 1, TRUE);
    if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), TRUE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Resolution"));
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_slider_set_mapping(table, PARAM_XRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_slider_set_mapping(table, PARAM_YRES, GWY_SCALE_MAPPING_LOG);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_append_button(table, BUTTON_SQUARE_PIXELS, -1, RESPONSE_SQUARE_PIXELS, _("Make Pixels S_quare"));

    gwy_param_table_append_header(table, -1, _("Physical Dimensions"));
    gwy_param_table_append_range(table, PARAM_XMIN, PARAM_XMAX);
    gwy_param_table_set_unitstr(table, PARAM_XMIN, args->vf->units);
    gwy_param_table_append_range(table, PARAM_YMIN, PARAM_YMAX);
    gwy_param_table_set_unitstr(table, PARAM_YMIN, args->vf->units);
    gwy_param_table_append_button(table, BUTTON_RESET_RANGES, -1, RESPONSE_RESET_RANGES, _("Reset _Area"));
    gwy_param_table_append_button(table, BUTTON_UNZOOM_RANGES, BUTTON_RESET_RANGES, RESPONSE_UNZOOM_RANGES,
                                  _("_Previous Area"));
    gwy_param_table_set_sensitive(gui.table, BUTTON_UNZOOM_RANGES, FALSE);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_INTERPOLATION);
    gwy_param_table_append_slider(table, PARAM_FIELD_POWER);
    gwy_param_table_append_combo(table, PARAM_EXTERIOR);
    gwy_param_table_append_checkbox(table, PARAM_MASK_EMPTY);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_info(table, INFO_NUM_POINTS, _("Number of points"));
    gwy_param_table_append_info(table, INFO_NUM_MERGED, _("Merged as too close"));
    gwy_param_table_append_info(table, INFO_NUM_ADDED, _("Added on boundaries"));
    gwy_param_table_append_message(table, MESSAGE_ERROR, NULL);
    gwy_param_table_message_set_type(table, MESSAGE_ERROR, GTK_MESSAGE_ERROR);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(selection, "changed", G_CALLBACK(selection_changed), &gui);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);
    triangulation_info(&gui);
    gtk_dialog_response(GTK_DIALOG(dialog), RESPONSE_RESET_RANGES);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_array_free(gui.zoom_stack, TRUE);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    gwy_debug("param %d", id);
    if (id == PARAM_XRES)
        gui->last_updated = LAST_UPDATED_X;
    if (id == PARAM_YRES)
        gui->last_updated = LAST_UPDATED_Y;

    if (id < 0 || id == PARAM_INTERPOLATION) {
        gint interpolation = gwy_params_get_enum(params, PARAM_INTERPOLATION);
        gwy_param_table_set_sensitive(gui->table, PARAM_MASK_EMPTY, interpolation == GWY_INTERPOLATION_AVERAGE);
        gwy_param_table_set_sensitive(gui->table, PARAM_FIELD_POWER, interpolation == GWY_INTERPOLATION_FIELD);
    }
    if (id < 0 || id == PARAM_XMIN || id == PARAM_XMAX || id == PARAM_YMIN || id == PARAM_YMAX)
        update_selection(gui);

    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (response == RESPONSE_RESET_RANGES || response == RESPONSE_UNZOOM_RANGES) {
        XYZRasRange range;
        guint n;

        if (response == RESPONSE_RESET_RANGES) {
            range = args->full_range;
            g_array_set_size(gui->zoom_stack, 0);
            n = 0;
        }
        else {
            n = gui->zoom_stack->len-1;
            range = g_array_index(gui->zoom_stack, XYZRasRange, n);
            g_array_set_size(gui->zoom_stack, n);
        }
        set_range(gui, &range);
        set_raster_field_properties(args);
        gwy_data_field_clear(args->raster);
        gwy_data_field_data_changed(args->raster);
        gwy_selection_clear(gui->selection);
        gwy_param_table_set_sensitive(gui->table, BUTTON_UNZOOM_RANGES, n > 0);
    }
    else if (response == RESPONSE_SQUARE_PIXELS) {
        gdouble xmin = gwy_params_get_double(params, PARAM_XMIN);
        gdouble xmax = gwy_params_get_double(params, PARAM_XMAX);
        gdouble ymin = gwy_params_get_double(params, PARAM_YMIN);
        gdouble ymax = gwy_params_get_double(params, PARAM_YMAX);
        gint xres = gwy_params_get_int(params, PARAM_XRES);
        gint yres = gwy_params_get_int(params, PARAM_YRES);

        if (gui->last_updated == LAST_UPDATED_X) {
            gint res = GWY_ROUND((ymax - ymin)/(xmax - xmin)*xres);
            gwy_param_table_set_int(gui->table, PARAM_YRES, CLAMP(res, 2, 16384));
            gui->last_updated = LAST_UPDATED_X;
        }
        else {
            gint res = GWY_ROUND((xmax - xmin)/(ymax - ymin)*yres);
            gwy_param_table_set_int(gui->table, PARAM_XRES, CLAMP(res, 2, 16384));
            gui->last_updated = LAST_UPDATED_Y;
        }
    }
}

static void
set_range(ModuleGUI *gui, const XYZRasRange *range)
{
    GwyParamTable *table = gui->table;
    gdouble mag = gui->args->vf->magnitude;

    g_assert(!gui->changing_selection);
    gui->changing_selection = TRUE;
    gwy_debug("setting %g %g :: %g %g in table (mag = %g)",
              range->min.x/mag, range->max.x/mag, range->min.y/mag, range->max.y/mag, mag);
    gwy_param_table_set_double(table, PARAM_XMIN, range->min.x/mag);
    gwy_param_table_set_double(table, PARAM_XMAX, range->max.x/mag);
    gwy_param_table_set_double(table, PARAM_YMIN, range->min.y/mag);
    gwy_param_table_set_double(table, PARAM_YMAX, range->max.y/mag);
    gui->changing_selection = FALSE;
    gwy_debug("finished set_range()");
}

static void
update_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gdouble xoff, yoff, q = args->vf->magnitude;
    gdouble xy[4];

    if (gui->changing_selection)
        return;

    xoff = gwy_data_field_get_xoffset(args->raster);
    yoff = gwy_data_field_get_yoffset(args->raster);
    xy[0] = q*gwy_params_get_double(params, PARAM_XMIN) - xoff;
    xy[1] = q*gwy_params_get_double(params, PARAM_YMIN) - yoff;
    xy[2] = q*gwy_params_get_double(params, PARAM_XMAX) - xoff;
    xy[3] = q*gwy_params_get_double(params, PARAM_YMAX) - yoff;
    gwy_selection_set_data(gui->selection, 1, xy);
}

static void
selection_changed(ModuleGUI *gui,
                  G_GNUC_UNUSED gint hint, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    gdouble xoff, yoff;
    XYZRasRange range;
    gdouble xy[4];

    gwy_debug("changing_selection %d", gui->changing_selection);
    if (gui->changing_selection)
        return;

    if (gwy_selection_get_data(selection, NULL) == 1) {
        xoff = gwy_data_field_get_xoffset(args->raster);
        yoff = gwy_data_field_get_yoffset(args->raster);
        gwy_selection_get_data(selection, xy);
        range.min.x = fmin(xy[0], xy[2]) + xoff;
        range.max.x = fmax(xy[0], xy[2]) + xoff;
        range.min.y = fmin(xy[1], xy[3]) + yoff;
        range.max.y = fmax(xy[1], xy[3]) + yoff;
        gwy_debug("real range (selected) %g %g :: %g %g", range.min.x, range.max.x, range.min.y, range.max.y);
    }
    else {
        set_range_from_field(args->raster, &range);
        gwy_debug("real range (full) %g %g :: %g %g", range.min.x, range.max.x, range.min.y, range.max.y);
    }
    set_range(gui, &range);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    gchar *error = NULL;

    GWY_OBJECT_UNREF(args->nilmask);
    if (execute(args, GTK_WINDOW(gui->dialog), &error)) {
        GArray *zst = gui->zoom_stack;
        XYZRasRange range;

        gwy_param_table_set_label(gui->table, MESSAGE_ERROR, NULL);
        set_range_from_field(args->raster, &range);
        if (!zst->len || memcmp(&g_array_index(zst, XYZRasRange, zst->len-1), &range, sizeof(XYZRasRange)))
            g_array_append_val(zst, range);
        gwy_param_table_set_sensitive(gui->table, BUTTON_UNZOOM_RANGES, zst->len > 0);
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    }
    else {
        gwy_param_table_set_label(gui->table, MESSAGE_ERROR, error);
        g_free(error);
        gwy_data_field_clear(args->raster);
    }
    triangulation_info(gui);

    gwy_data_field_data_changed(args->raster);
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);

    /* After doing preview the selection always covers the full data and thus is not useful. */
    gwy_debug("clearing selection");
    gwy_selection_clear(gui->selection);
}

static void
triangulation_info(ModuleGUI *gui)
{
    XYZRasData *rdata = &gui->args->rdata;
    GwyParamTable *table = gui->table;
    gchar *s;

    s = g_strdup_printf("%u", rdata->norigpoints);
    gwy_param_table_info_set_valuestr(table, INFO_NUM_POINTS, s);
    g_free(s);

    s = g_strdup_printf("%u", rdata->norigpoints - rdata->nbasepoints);
    gwy_param_table_info_set_valuestr(table, INFO_NUM_MERGED, s);
    g_free(s);

    s = g_strdup_printf("%u", rdata->points->len - rdata->nbasepoints);
    gwy_param_table_info_set_valuestr(table, INFO_NUM_ADDED, s);
    g_free(s);
}

static void
render_regular_directly(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;

    gwy_object_unref(args->raster);
    args->raster = g_object_ref(args->regular);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    gtk_dialog_response(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK);
}

/* FIXME: Update data allocation logic. */
static gboolean
execute(ModuleArgs *args, GtkWindow *window, gchar **error)
{
    XYZRasData *rdata = &args->rdata;
    GwyParams *params = args->params;
    gint interpolation = gwy_params_get_enum(params, PARAM_INTERPOLATION);
    gdouble field_power = gwy_params_get_double(params, PARAM_FIELD_POWER);
    gdouble xmin = gwy_params_get_double(params, PARAM_XMIN);
    gdouble xmax = gwy_params_get_double(params, PARAM_XMAX);
    gdouble ymin = gwy_params_get_double(params, PARAM_YMIN);
    gdouble ymax = gwy_params_get_double(params, PARAM_YMAX);
    gint xres = gwy_params_get_int(params, PARAM_XRES);
    gint yres = gwy_params_get_int(params, PARAM_YRES);
    gboolean mask_empty = gwy_params_get_boolean(params, PARAM_MASK_EMPTY);
    GwyTriangulation *triangulation = rdata->triangulation;
    GArray *points = rdata->points;
    GwyDataField *field = args->raster, *mask;
    GwySetMessageFunc set_message = (window ? gwy_app_wait_set_message : NULL);
    GwySetFractionFunc set_fraction = (window ? gwy_app_wait_set_fraction : NULL);
    gboolean ok = TRUE, extended;

    GWY_OBJECT_UNREF(args->nilmask);
    gwy_debug("%g %g :: %g %g", xmin, xmax, ymin, ymax);
    if (!(xmax > xmin) || !(ymax > ymin)) {
        *error = g_strdup(_("Physical dimensions are invalid."));
        return FALSE;
    }
    gwy_data_field_resample(field, xres, yres, GWY_INTERPOLATION_NONE);
    set_raster_field_properties(args);

    if ((gint)interpolation == GWY_INTERPOLATION_FIELD) {
        if (window)
            gwy_app_wait_start(window, _("Initializing..."));

        extend_borders(args, FALSE, EPSREL);
        ok = interpolate_field(points->len, (const GwyXYZ*)points->data, field, field_power, set_fraction, set_message);
        if (window)
            gwy_app_wait_finish();
    }
    else if ((gint)interpolation == GWY_INTERPOLATION_AVERAGE) {
        extend_borders(args, FALSE, EPSREL);
        if (mask_empty) {
            args->nilmask = mask = gwy_data_field_new_alike(field, FALSE);
            gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);
            gwy_data_field_average_xyz(field, mask, (const GwyXYZ*)points->data, points->len);
            gwy_data_field_threshold(mask, G_MINDOUBLE, 1.0, 0.0);
        }
        else {
            gwy_data_field_average_xyz(field, NULL, (const GwyXYZ*)points->data, points->len);
        }
        ok = TRUE;
    }
    else {
        if (window)
            gwy_app_wait_start(window, _("Initializing..."));
        /* [Try to] perform triangulation if either there is none yet or extend_borders() reports the points have
         * changed. */
        gwy_debug("have triangulation: %d", !!triangulation);
        extended = extend_borders(args, TRUE, EPSREL);
        if (!triangulation || extended) {
            gwy_debug("must triangulate");
            if (!triangulation)
                rdata->triangulation = triangulation = gwy_triangulation_new();
            /* This can fail for two different reasons:
             * 1) numerical failure
             * 2) cancellation */
            ok = gwy_triangulation_triangulate_iterative(triangulation, points->len, points->data, sizeof(GwyXYZ),
                                                         set_fraction, set_message);
        }
        else {
            gwy_debug("points did not change, recycling triangulation");
        }

        if (triangulation && ok) {
            if (window)
                ok = set_message(_("Interpolating..."));
            if (ok)
                ok = gwy_triangulation_interpolate(triangulation, interpolation, field);
        }
        if (window)
            gwy_app_wait_finish();
    }

    if (!ok) {
        GWY_OBJECT_UNREF(rdata->triangulation);
        *error = g_strdup(_("XYZ data regularization failed due to\nnumerical instability or was interrupted."));
        return FALSE;
    }

    return TRUE;
}

static void
set_raster_field_properties(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gdouble q = args->vf->magnitude;
    gdouble xmin = q*gwy_params_get_double(params, PARAM_XMIN);
    gdouble xmax = q*gwy_params_get_double(params, PARAM_XMAX);
    gdouble ymin = q*gwy_params_get_double(params, PARAM_YMIN);
    gdouble ymax = q*gwy_params_get_double(params, PARAM_YMAX);
    GwyDataField *field = args->raster;

    gwy_data_field_set_xreal(field, xmax - xmin);
    gwy_data_field_set_yreal(field, ymax - ymin);
    gwy_data_field_set_xoffset(field, xmin);
    gwy_data_field_set_yoffset(field, ymin);
    gwy_surface_copy_units_to_data_field(args->surface, field);
}

static gboolean
interpolate_field(guint npoints,
                  const GwyXYZ *points,
                  GwyDataField *field,
                  gdouble power,
                  GwySetFractionFunc set_fraction,
                  GwySetMessageFunc set_message)
{
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    gdouble xoff, yoff, qx, qy, p;
    guint xres, yres;
    gint ipwr;
    gdouble *d;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    xoff = gwy_data_field_get_xoffset(field);
    yoff = gwy_data_field_get_yoffset(field);
    qx = gwy_data_field_get_xreal(field)/xres;
    qy = gwy_data_field_get_yreal(field)/yres;
    d = gwy_data_field_get_data(field);

    p = 0.5*power;
    if (fabs(power - GWY_ROUND(power)) < EPSREL)
        ipwr = GWY_ROUND(power);
    else
        ipwr = -1;

    if (set_message)
        set_message(_("Interpolating..."));

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(d,xres,yres,points,npoints,xoff,yoff,qx,qy,ipwr,p,set_fraction,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j, k;

        for (i = ifrom; i < ito; i++) {
            gdouble y = yoff + qy*(i + 0.5);
            gdouble *drow = d + i*xres;

            for (j = 0; j < xres; j++) {
                gdouble x = xoff + qx*(j + 0.5);
                gdouble w = 0.0;
                gdouble s = 0.0;

                for (k = 0; k < npoints; k++) {
                    const GwyXYZ *pt = points + k;
                    gdouble dx = x - pt->x;
                    gdouble dy = y - pt->y;
                    gdouble r2 = dx*dx + dy*dy;

                    r2 *= r2;
                    if (G_UNLIKELY(r2 == 0.0)) {
                        s = pt->z;
                        w = 1.0;
                        break;
                    }

                    if (ipwr == 2)
                        r2 = 1.0/r2;
                    else if (ipwr == 1)
                        r2 = 1.0/sqrt(r2);
                    else if (ipwr == 3)
                        r2 = 1.0/(sqrt(r2)*r2);
                    else if (ipwr == 4)
                        r2 = 1.0/(r2*r2);
                    else if (ipwr == 5)
                        r2 = 1.0/(sqrt(r2)*r2*r2);
                    else
                        r2 = 1.0/pow(r2, p);

                    w += r2;
                    s += r2*pt->z;
                }
                drow[j] = s/w;
            }

            if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito, pcancelled))
                break;
        }
    }

    return !cancelled;
}

/* Return TRUE if extpoints have changed. */
static gboolean
extend_borders(ModuleArgs *args,
               gboolean check_for_changes,
               gdouble epsrel)
{
    GwyParams *params = args->params;
    gdouble xmin = gwy_params_get_double(params, PARAM_XMIN);
    gdouble xmax = gwy_params_get_double(params, PARAM_XMAX);
    gdouble ymin = gwy_params_get_double(params, PARAM_YMIN);
    gdouble ymax = gwy_params_get_double(params, PARAM_YMAX);
    GwyExteriorType exterior = gwy_params_get_enum(params, PARAM_EXTERIOR);
    XYZRasData *rdata = &args->rdata;
    GwySurface *surface = args->surface;
    gdouble xreal, yreal, eps, sxmin, sxmax, symin, symax;
    gdouble *oldextpoints = NULL;
    guint i, nbase, noldext;
    gboolean extchanged;

    /* Remember previous extpoints.  If they do not change we do not need to repeat the triangulation. */
    nbase = rdata->nbasepoints;
    noldext = rdata->points->len - nbase;
    gwy_debug("check for changes: %d", check_for_changes);
    if (check_for_changes) {
        gwy_debug("copying %u old extpoints", noldext);
        oldextpoints = g_memdup(&g_array_index(rdata->points, GwyXYZ, nbase), noldext*sizeof(GwyXYZ));
    }
    g_array_set_size(rdata->points, nbase);

    if (exterior == GWY_EXTERIOR_BORDER_EXTEND) {
        gwy_debug("exterior is BORDER, just reducing points to base");
        g_free(oldextpoints);
        return noldext > 0 || !check_for_changes;
    }

    gwy_surface_get_xrange(surface, &sxmin, &sxmax);
    gwy_surface_get_yrange(surface, &symin, &symax);
    xreal = sxmax - sxmin;
    yreal = symax - symin;

    xmin = xmin - 2*rdata->step;
    xmax = xmax + 2*rdata->step;
    ymin = ymin - 2*rdata->step;
    ymax = ymax + 2*rdata->step;
    eps = epsrel*rdata->step;

    /* Extend the field according to requester boder extension, however, create at most 3 full copies (4 halves and
     * 4 quarters) of the base set. Anyone asking for more is either clueless or malicious. */
    for (i = 0; i < nbase; i++) {
        const GwyXYZ pt = g_array_index(rdata->points, GwyXYZ, i);
        GwyXYZ pt2;
        gdouble txl, txr, tyt, tyb;
        gboolean txlok, txrok, tytok, tybok;

        pt2.z = pt.z;
        if (exterior == GWY_EXTERIOR_MIRROR_EXTEND) {
            txl = 2.0*sxmin - pt.x;
            tyt = 2.0*symin - pt.y;
            txr = 2.0*sxmax - pt.x;
            tyb = 2.0*symax - pt.y;
            txlok = pt.x - sxmin < 0.5*xreal;
            tytok = pt.y - symin < 0.5*yreal;
            txrok = sxmax - pt.x < 0.5*xreal;
            tybok = symax - pt.y < 0.5*yreal;
        }
        else if (exterior == GWY_EXTERIOR_PERIODIC) {
            txl = pt.x - xreal;
            tyt = pt.y - yreal;
            txr = pt.x + xreal;
            tyb = pt.y + yreal;
            txlok = sxmax - pt.x < 0.5*xreal;
            tytok = symax - pt.y < 0.5*yreal;
            txrok = pt.x - sxmin < 0.5*xreal;
            tybok = pt.y - symin < 0.5*yreal;
        }
        else {
            g_assert_not_reached();
        }

        txlok = txlok && (txl >= xmin && txl <= xmax && fabs(txl - sxmin) > eps);
        tytok = tytok && (tyt >= ymin && tyt <= ymax && fabs(tyt - symin) > eps);
        txrok = txrok && (txr >= ymin && txr <= xmax && fabs(txr - sxmax) > eps);
        tybok = tybok && (tyb >= ymin && tyb <= xmax && fabs(tyb - symax) > eps);

        if (txlok) {
            pt2.x = txl;
            pt2.y = pt.y - eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txlok && tytok) {
            pt2.x = txl + eps;
            pt2.y = tyt - eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (tytok) {
            pt2.x = pt.x + eps;
            pt2.y = tyt;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok && tytok) {
            pt2.x = txr + eps;
            pt2.y = tyt + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok) {
            pt2.x = txr;
            pt2.y = pt.y + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (txrok && tybok) {
            pt2.x = txr - eps;
            pt2.y = tyb + eps;
            g_array_append_val(rdata->points, pt2);
        }
        if (tybok) {
            pt2.x = pt.x - eps;
            pt2.y = tyb;
            g_array_append_val(rdata->points, pt2);
        }
        if (txlok && tybok) {
            pt2.x = txl - eps;
            pt2.y = tyb - eps;
            g_array_append_val(rdata->points, pt2);
        }
    }
    gwy_debug("after extension we have %u extpoints", rdata->points->len - nbase);

    if (!check_for_changes) {
        gwy_debug("do not check for changes, so just state expoints changed");
        g_assert(!oldextpoints);
        return TRUE;
    }

    extchanged = (noldext != rdata->points->len - nbase
                  || memcmp(&g_array_index(rdata->points, GwyXYZ, nbase), oldextpoints, noldext*sizeof(GwyXYZ)));
    g_free(oldextpoints);
    gwy_debug("comparison says extchanged = %d", extchanged);
    return extchanged;
}

static gdouble
round_with_base(gdouble x, gdouble base)
{
    gint s;

    s = (x < 0) ? -1 : 1;
    x = fabs(x)/base;
    if (x <= 1.0)
        return GWY_ROUND(10.0*x)/10.0*s*base;
    else if (x <= 2.0)
        return GWY_ROUND(5.0*x)/5.0*s*base;
    else if (x <= 5.0)
        return GWY_ROUND(2.0*x)/2.0*s*base;
    else
        return GWY_ROUND(x)*s*base;
}

static void
round_to_nice(gdouble *minval, gdouble *maxval)
{
    gdouble range = *maxval - *minval;
    gdouble base = pow10(floor(log10(range) - 2.0));

    *minval = round_with_base(*minval, base);
    *maxval = round_with_base(*maxval, base);
}

static void
initialise_range(ModuleArgs *args)
{
    GwySurface *surface = args->surface;
    XYZRasRange *range = &args->full_range;

    gwy_surface_get_xrange(surface, &range->min.x, &range->max.x);
    round_to_nice(&range->min.x, &range->max.x);
    gwy_surface_get_yrange(surface, &range->min.y, &range->max.y);
    round_to_nice(&range->min.y, &range->max.y);
    gwy_debug("%g %g :: %g %g", range->min.x, range->max.x, range->min.y, range->max.y);
}

static void
set_range_from_field(GwyDataField *field, XYZRasRange *range)
{
    gdouble xoff = gwy_data_field_get_xoffset(field);
    gdouble yoff = gwy_data_field_get_yoffset(field);

    range->min.x = xoff;
    range->max.x = gwy_data_field_get_xreal(field) + xoff;
    range->min.y = yoff;
    range->max.y = gwy_data_field_get_yreal(field) + yoff;
}

static inline guint
coords_to_grid_index(guint xres,
                     guint yres,
                     gdouble step,
                     gdouble x,
                     gdouble y)
{
    guint ix, iy;

    ix = (guint)floor(x/step);
    if (G_UNLIKELY(ix >= xres))
        ix--;

    iy = (guint)floor(y/step);
    if (G_UNLIKELY(iy >= yres))
        iy--;

    return iy*xres + ix;
}

static inline void
index_accumulate(guint *index_array,
                 guint n)
{
    guint i;

    for (i = 1; i <= n; i++)
        index_array[i] += index_array[i-1];
}

static inline void
index_rewind(guint *index_array,
             guint n)
{
    guint i;

    for (i = n; i; i--)
        index_array[i] = index_array[i-1];
    index_array[0] = 0;
}

static void
work_queue_init(WorkQueue *queue)
{
    queue->size = 64;
    queue->len = 0;
    queue->id = g_new(guint, queue->size);
}

static void
work_queue_destroy(WorkQueue *queue)
{
    g_free(queue->id);
}

static void
work_queue_add(WorkQueue *queue,
               guint id)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size *= 2;
        queue->id = g_renew(guint, queue->id, queue->size);
    }
    queue->id[queue->len] = id;
    queue->len++;
}

static void
work_queue_ensure(WorkQueue *queue,
                  guint id)
{
    guint i;

    for (i = 0; i < queue->len; i++) {
        if (queue->id[i] == id)
            return;
    }
    work_queue_add(queue, id);
}

static inline gdouble
point_dist2(const GwyXYZ *p,
            const GwyXYZ *q)
{
    gdouble dx = p->x - q->x;
    gdouble dy = p->y - q->y;

    return dx*dx + dy*dy;
}

static gboolean
maybe_add_point(WorkQueue *pointqueue,
                const GwyXYZ *newpoints,
                guint ii,
                gdouble eps2)
{
    const GwyXYZ *pt;
    guint i;

    pt = newpoints + pointqueue->id[ii];
    for (i = 0; i < pointqueue->pos; i++) {
        if (point_dist2(pt, newpoints + pointqueue->id[i]) < eps2) {
            GWY_SWAP(guint, pointqueue->id[ii], pointqueue->id[pointqueue->pos]);
            pointqueue->pos++;
            return TRUE;
        }
    }
    return FALSE;
}

/* Calculate coordinate ranges and ensure points are more than epsrel*cellside appart where cellside is the side of
 * equivalent-area square for one point. */
static void
analyse_points(ModuleArgs *args, double epsrel)
{
    XYZRasData *rdata = &args->rdata;
    GwySurface *surface = args->surface;
    WorkQueue cellqueue, pointqueue;
    const GwyXYZ *points, *pt;
    GwyXYZ *newpoints;
    gdouble xreal, yreal, eps, eps2, xr, yr, step;
    guint npoints, i, ii, j, ig, xres, yres, ncells, oldpos;
    gdouble xmin, xmax, ymin, ymax;
    guint *cell_index;

    /* Calculate data ranges */
    npoints = rdata->norigpoints = surface->n;
    points = surface->data;
    gwy_surface_get_xrange(surface, &xmin, &xmax);
    gwy_surface_get_yrange(surface, &ymin, &ymax);

    xreal = xmax - xmin;
    yreal = ymax - ymin;

    if (xreal == 0.0 || yreal == 0.0) {
        g_warning("All points lie on a line, we are going to crash.");
    }

    /* Make a virtual grid */
    xr = xreal/sqrt(npoints)*CELL_SIDE;
    yr = yreal/sqrt(npoints)*CELL_SIDE;

    if (xr <= yr) {
        xres = (guint)ceil(xreal/xr);
        step = xreal/xres;
        yres = (guint)ceil(yreal/step);
    }
    else {
        yres = (guint)ceil(yreal/yr);
        step = yreal/yres;
        xres = (guint)ceil(xreal/step);
    }
    rdata->step = step;
    eps = epsrel*step;
    eps2 = eps*eps;

    ncells = xres*yres;
    cell_index = g_new0(guint, ncells + 1);

    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        cell_index[ig]++;
    }

    index_accumulate(cell_index, xres*yres);
    g_assert(cell_index[xres*yres] == npoints);
    index_rewind(cell_index, xres*yres);
    newpoints = g_new(GwyXYZ, npoints);

    /* Sort points by cell */
    for (i = 0; i < npoints; i++) {
        pt = points + i;
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        newpoints[cell_index[ig]] = *pt;
        cell_index[ig]++;
    }
    g_assert(cell_index[xres*yres] == npoints);
    index_rewind(cell_index, xres*yres);

    /* Find groups of identical (i.e. closer than epsrel) points we need to merge.  We collapse all merged points to
     * that with the lowest id. Closeness must be transitive so the group must be gathered iteratively until it no
     * longer grows. */
    work_queue_init(&pointqueue);
    work_queue_init(&cellqueue);
    g_array_set_size(rdata->points, 0);
    for (i = 0; i < npoints; i++) {
        /* Ignore merged points */
        if (newpoints[i].z == G_MAXDOUBLE)
            continue;

        pointqueue.len = 0;
        cellqueue.len = 0;
        cellqueue.pos = 0;
        work_queue_add(&pointqueue, i);
        pointqueue.pos = 1;
        oldpos = 0;

        do {
            /* Update the list of cells to process.  Most of the time this is no-op. */
            while (oldpos < pointqueue.pos) {
                gdouble x, y;
                gint ix, iy;

                pt = newpoints + pointqueue.id[oldpos];
                x = (pt->x - xmin)/step;
                ix = (gint)floor(x);
                x -= ix;
                y = (pt->y - ymin)/step;
                iy = (gint)floor(y);
                y -= iy;

                if (ix < xres && iy < yres)
                    work_queue_ensure(&cellqueue, iy*xres + ix);
                if (ix > 0 && iy < yres && x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix-1);
                if (ix < xres && iy > 0 && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix);
                if (ix > 0 && iy > 0 && x < eps && y <= eps)
                    work_queue_ensure(&cellqueue, (iy - 1)*xres + ix-1);
                if (ix+1 < xres && iy < yres && 1-x <= eps)
                    work_queue_ensure(&cellqueue, iy*xres + ix+1);
                if (ix < xres && iy+1 < yres && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix);
                if (ix+1 < xres && iy+1 < yres && 1-x <= eps && 1-y <= eps)
                    work_queue_ensure(&cellqueue, (iy + 1)*xres + ix+1);

                oldpos++;
            }

            /* Process all points from the cells and check if they belong to the currently merged group. */
            while (cellqueue.pos < cellqueue.len) {
                j = cellqueue.id[cellqueue.pos];
                for (ii = cell_index[j]; ii < cell_index[j+1]; ii++) {
                    if (ii != i && newpoints[ii].z != G_MAXDOUBLE)
                        work_queue_add(&pointqueue, ii);
                }
                cellqueue.pos++;
            }

            /* Compare all not-in-group points with all group points, adding them to the group on success. */
            for (ii = pointqueue.pos; ii < pointqueue.len; ii++)
                maybe_add_point(&pointqueue, newpoints, ii, eps2);
        } while (oldpos != pointqueue.pos);

        /* Calculate the representant of all contributing points. */
        {
            GwyXYZ avg = { 0.0, 0.0, 0.0 };

            for (ii = 0; ii < pointqueue.pos; ii++) {
                GwyXYZ *ptii = newpoints + pointqueue.id[ii];
                avg.x += ptii->x;
                avg.y += ptii->y;
                avg.z += ptii->z;
                ptii->z = G_MAXDOUBLE;
            }

            avg.x /= pointqueue.pos;
            avg.y /= pointqueue.pos;
            avg.z /= pointqueue.pos;
            g_array_append_val(rdata->points, avg);
        }
    }

    work_queue_destroy(&cellqueue);
    work_queue_destroy(&pointqueue);
    g_free(cell_index);
    g_free(newpoints);

    rdata->nbasepoints = rdata->points->len;
}

/* Create a data field directly if the XY positions form a complete regular grid.  */
static GwyDataField*
check_regular_grid(GwySurface *surface)
{
    GwyXY xymin, dxy;
    guint n, xres, yres, k;
    GwyDataField *field;
    gdouble *data;
    guint *map;

    n = surface->n;
    if (!(map = gwy_check_regular_2d_grid((const gdouble*)surface->data, 3, n, -1.0, &xres, &yres, &xymin, &dxy)))
        return NULL;

    field = gwy_data_field_new(xres, yres, xres*dxy.x, yres*dxy.y, FALSE);
    data = gwy_data_field_get_data(field);
    for (k = 0; k < n; k++)
        data[k] = surface->data[map[k]].z;
    g_free(map);

    gwy_data_field_set_xoffset(field, xymin.x);
    gwy_data_field_set_yoffset(field, xymin.y);
    gwy_surface_copy_units_to_data_field(surface, field);
    return field;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
