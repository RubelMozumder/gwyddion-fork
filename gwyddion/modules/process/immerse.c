/*
 *  $Id: immerse.c 24450 2021-11-01 14:32:53Z yeti-dn $
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
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correlation.h>
#include <libprocess/hough.h>
#include <libdraw/gwypixfield.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

/* Some empirical factors */

/* Search window of improve for kernel dimension @k and image dimension @i */
#define improve_search_window(k, i) GWY_ROUND(1.0/(2.0/(k) + 6.0/(i)))

/* Universal downsample factor to get approximately optimal run time in two-stage search */
#define downsample_factor 0.18

/* But don't downsample kernels below certain size (in pixels) */
#define downsample_limit 20

typedef enum {
    GWY_IMMERSE_SAMPLING_UP,
    GWY_IMMERSE_SAMPLING_DOWN,
    GWY_IMMERSE_SAMPLING_LAST
} GwyImmerseSamplingType;

typedef enum {
    GWY_IMMERSE_LEVEL_NONE,
    GWY_IMMERSE_LEVEL_MEAN,
    GWY_IMMERSE_LEVEL_LAST
} GwyImmerseLevelType;

enum {
    PARAM_DETAIL,
    PARAM_SAMPLING,
    PARAM_LEVELING,
    PARAM_DRAW_FRAME,
    PARAM_DRAW_DETAIL,
    PARAM_XPOS,
    PARAM_YPOS,
    BUTTON_LOCATE,
    BUTTON_REFINE,
    INFO_XYPOS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    GtkWidget *view;
    GdkPixbuf *detail;
    GwySIValueFormat *vf;
    gdouble xmax;
    gdouble ymax;
    gint xc;
    gint yc;
    gint button;
    GdkCursor *near_cursor;
    GdkCursor *move_cursor;
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             immerse                    (GwyContainer *data,
                                                    GwyRunType runtype);
static void             execute                    (ModuleArgs *args);
static GwyDialogOutcome run_gui                    (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static void             dialog_mapped              (GtkWidget *dialog,
                                                    ModuleGUI *gui);
static void             dialog_unmapped            (GtkWidget *dialog,
                                                    ModuleGUI *gui);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static void             immerse_update_detail      (ModuleGUI *gui);
static gboolean         detail_filter              (GwyContainer *data,
                                                    gint id,
                                                    gpointer user_data);
static void             immerse_search             (ModuleGUI *gui,
                                                    gint search_type);
static void             immerse_correlate          (GwyDataField *image,
                                                    GwyDataField *kernel,
                                                    gint *col,
                                                    gint *row);
static gboolean         immerse_view_expose        (GtkWidget *view,
                                                    GdkEventExpose *event,
                                                    ModuleGUI *gui);
static gboolean         immerse_view_button_press  (GtkWidget *view,
                                                    GdkEventButton *event,
                                                    ModuleGUI *gui);
static gboolean         immerse_view_button_release(GtkWidget *view,
                                                    GdkEventButton *event,
                                                    ModuleGUI *gui);
static gboolean         immerse_view_motion_notify (GtkWidget *view,
                                                    GdkEventMotion *event,
                                                    ModuleGUI *gui);
static gboolean         immerse_view_inside_detail (ModuleGUI *gui,
                                                    gint x,
                                                    gint y);
static void             clamp_detail_offset        (ModuleGUI *gui,
                                                    gdouble xpos,
                                                    gdouble ypos);
static void             redraw                     (ModuleGUI *gui);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Immerse high resolution detail into overall image."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, immerse)

static gboolean
module_register(void)
{
    gwy_process_func_register("immerse",
                              (GwyProcessFunc)&immerse,
                              N_("/M_ultidata/_Immerse Detail..."),
                              GWY_STOCK_IMMERSE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Immerse a detail into image"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum samplings[] = {
        { N_("_Upsample large image"), GWY_IMMERSE_SAMPLING_UP,   },
        { N_("_Downsample detail"),    GWY_IMMERSE_SAMPLING_DOWN, },
    };
    static const GwyEnum levelings[] = {
        { N_("levelling|_None"), GWY_IMMERSE_LEVEL_NONE, },
        { N_("_Mean value"),     GWY_IMMERSE_LEVEL_MEAN, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_DETAIL, "detail", _("_Detail image"));
    gwy_param_def_add_gwyenum(paramdef, PARAM_SAMPLING, "sampling", _("Result sampling"),
                              samplings, G_N_ELEMENTS(samplings), GWY_IMMERSE_SAMPLING_UP);
    gwy_param_def_add_gwyenum(paramdef, PARAM_LEVELING, "leveling", _("Detail leveling"),
                              levelings, G_N_ELEMENTS(levelings), GWY_IMMERSE_LEVEL_MEAN);
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_FRAME, "draw_frame", _("Show _frame"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_DETAIL, "draw_detail", _("Show _detail"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_XPOS, NULL, _("X position"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_YPOS, NULL, _("Y position"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    return paramdef;
}

static void
immerse(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.params = gwy_params_new_from_settings(define_module_params());
    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute(&args);

    newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
    gwy_app_set_data_field_title(data, newid, _("Immersed detail"));
    gwy_app_channel_log_add_proc(data, id, newid);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE_TYPE,
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

end:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *hbox;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_container_set_boolean_by_name(gui.data, "/0/data/realsquare", TRUE);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_RANGE,
                            0);
    gui.vf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    gui.dialog = gwy_dialog_new(_("Immerse Detail"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.view), FALSE);

    g_signal_connect_after(gui.view, "expose-event", G_CALLBACK(immerse_view_expose), &gui);
    g_signal_connect(gui.view, "button-press-event", G_CALLBACK(immerse_view_button_press), &gui);
    g_signal_connect(gui.view, "button-release-event", G_CALLBACK(immerse_view_button_release), &gui);
    g_signal_connect(gui.view, "motion-notify-event", G_CALLBACK(immerse_view_motion_notify), &gui);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_DETAIL);
    gwy_param_table_data_id_set_filter(table, PARAM_DETAIL, detail_filter, args->field, NULL);
    gwy_param_table_append_info(table, INFO_XYPOS, _("Position"));
    gwy_param_table_set_unitstr(table, INFO_XYPOS, gui.vf->units);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_button(table, BUTTON_LOCATE, -1, RESPONSE_ESTIMATE, _("_Locate"));
    gwy_param_table_append_button(table, BUTTON_REFINE, BUTTON_LOCATE, RESPONSE_REFINE, _("_Refine"));

    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_SAMPLING);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_LEVELING);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_FRAME);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_DETAIL);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    g_signal_connect_after(dialog, "map", G_CALLBACK(dialog_mapped), &gui);
    g_signal_connect(dialog, "unmap", G_CALLBACK(dialog_unmapped), &gui);

    outcome = gwy_dialog_run(dialog);

    gwy_si_unit_value_format_free(gui.vf);
    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(gui.detail);

    return outcome;
}

static void
dialog_mapped(GtkWidget *dialog, ModuleGUI *gui)
{
    GdkDisplay *display = gtk_widget_get_display(dialog);

    gui->near_cursor = gdk_cursor_new_for_display(display, GDK_FLEUR);
    gui->move_cursor = gdk_cursor_new_for_display(display, GDK_CROSS);
    immerse_update_detail(gui);
}

static void
dialog_unmapped(G_GNUC_UNUSED GtkWidget *dialog, ModuleGUI *gui)
{
    gdk_cursor_unref(gui->near_cursor);
    gdk_cursor_unref(gui->move_cursor);
    gui->near_cursor = gui->move_cursor = NULL;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id < 0 || id == PARAM_DETAIL)
        immerse_update_detail(gui);

    if (id != PARAM_SAMPLING && id != PARAM_LEVELING)
        redraw(gui);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_REFINE)
        immerse_search(gui, TRUE);
    else if (response == RESPONSE_ESTIMATE)
        immerse_search(gui, FALSE);
}

static void
immerse_update_detail(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *field = args->field, *detail = gwy_params_get_image(args->params, PARAM_DETAIL);
    gdouble xpos = gwy_params_get_double(args->params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(args->params, PARAM_YPOS);
    GwyAppDataId dataid = gwy_params_get_data_id(args->params, PARAM_DETAIL);
    gdouble xreald, yreald;
    GwyGradient *gradient;
    GdkPixbuf *pixbuf;
    const guchar *name;
    gint w, h, xresd, yresd;

    GWY_OBJECT_UNREF(gui->detail);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, !!detail);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_ESTIMATE, !!detail);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_REFINE, !!detail);

    if (!detail || !GTK_WIDGET_DRAWABLE(gui->view))
        return;

    xresd = gwy_data_field_get_xres(detail);
    yresd = gwy_data_field_get_yres(detail);
    xreald = gwy_data_field_get_xreal(detail);
    yreald = gwy_data_field_get_yreal(detail);
    gui->xmax = gwy_data_field_get_xreal(field) - xreald + gwy_data_field_get_dx(field)/2.0;
    gui->ymax = gwy_data_field_get_yreal(field) - yreald + gwy_data_field_get_dy(field)/2.0;

    clamp_detail_offset(gui, xpos, ypos);

    name = NULL;
    gwy_container_gis_string(gwy_app_data_browser_get(dataid.datano),
                             gwy_app_get_data_palette_key_for_id(dataid.id),
                             &name);
    gradient = gwy_gradients_get_gradient(name);

    /* Handle real-square properly by using an intermediate pixel-square pixbuf with sufficient resolution */
    gwy_data_view_coords_real_to_xy(GWY_DATA_VIEW(gui->view), xreald, yreald, &w, &h);
    gwy_debug("%dx%d", w, h);
    w = MAX(w, 2);
    h = MAX(h, 2);

    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, xresd, yresd);
    gwy_pixbuf_draw_data_field(pixbuf, detail, gradient);
    gui->detail = gdk_pixbuf_scale_simple(pixbuf, w, h, GDK_INTERP_TILES);
    g_object_unref(pixbuf);

    redraw(gui);
}

static gboolean
detail_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *detail, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &detail))
        return FALSE;
    if (detail == field)
        return FALSE;
    if (gwy_data_field_check_compatibility(field, detail,
                                           GWY_DATA_COMPATIBILITY_LATERAL | GWY_DATA_COMPATIBILITY_VALUE))
        return FALSE;
    if (gwy_data_field_get_xreal(field) < gwy_data_field_get_xreal(detail)
        || gwy_data_field_get_yreal(field) < gwy_data_field_get_yreal(detail))
        return FALSE;

    return TRUE;
}

static void
immerse_search(ModuleGUI *gui, gboolean is_refine)
{
    ModuleArgs *args = gui->args;
    gdouble xpos = gwy_params_get_double(args->params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(args->params, PARAM_YPOS);
    GwyDataField *field = args->field, *detail = gwy_params_get_image(args->params, PARAM_DETAIL);
    GwyDataField *subfield, *iarea;
    gdouble wr, hr, deltax, deltay;
    gint w, h, xfrom, xto, yfrom, yto, ixres, iyres, col, row;

    ixres = gwy_data_field_get_xres(field);
    iyres = gwy_data_field_get_yres(field);

    wr = gwy_data_field_get_xreal(detail)/gwy_data_field_get_dx(field);
    hr = gwy_data_field_get_yreal(detail)/gwy_data_field_get_dy(field);
    if (wr*hr < 6.0) {
        g_warning("Detail image is too small for correlation");
        return;
    }

    w = GWY_ROUND(MAX(wr, 1.0));
    h = GWY_ROUND(MAX(hr, 1.0));
    gwy_debug("w: %d, h: %d", w, h);
    g_assert(w <= ixres && h <= iyres);
    if (is_refine) {
        xfrom = gwy_data_field_rtoj(field, xpos);
        yfrom = gwy_data_field_rtoi(field, ypos);
        /* Calculate the area we will search the detail in */
        deltax = improve_search_window(w, ixres);
        deltay = improve_search_window(h, iyres);
        gwy_debug("deltax: %g, deltay: %g", deltax, deltay);
        xto = MIN(xfrom + w + deltax, ixres);
        yto = MIN(yfrom + h + deltay, iyres);
        xfrom = MAX(xfrom - deltax, 0);
        yfrom = MAX(yfrom - deltay, 0);
    }
    else {
        xfrom = yfrom = 0;
        xto = ixres;
        yto = iyres;
    }
    gwy_debug("x: %d..%d, y: %d..%d", xfrom, xto, yfrom, yto);

    /* Cut out only the interesting part from the image data detail */
    if (xfrom == 0 && yfrom == 0 && xto == ixres && yto == iyres)
        iarea = g_object_ref(field);
    else
        iarea = gwy_data_field_area_extract(field, xfrom, yfrom, xto - xfrom, yto - yfrom);

    subfield = gwy_data_field_new_resampled(detail, w, h, GWY_INTERPOLATION_LINEAR);

    immerse_correlate(iarea, subfield, &col, &row);
    gwy_debug("[c] col: %d, row: %d", col, row);
    col += xfrom;
    row += yfrom;
    xpos = gwy_data_field_jtor(subfield, col + 0.5);
    ypos = gwy_data_field_itor(subfield, row + 0.5);
    g_object_unref(iarea);
    g_object_unref(subfield);
    gwy_debug("[C] col: %d, row: %d", col, row);

    /* Upsample and refine */
    xfrom = MAX(col - 1, 0);
    yfrom = MAX(row - 1, 0);
    xto = MIN(col + w + 1, ixres);
    yto = MIN(row + h + 1, iyres);
    gwy_debug("x: %d..%d, y: %d..%d", xfrom, xto, yfrom, yto);
    iarea = gwy_data_field_area_extract(field, xfrom, yfrom, xto - xfrom, yto - yfrom);
    wr = gwy_data_field_get_xreal(iarea)/gwy_data_field_get_dx(detail);
    hr = gwy_data_field_get_yreal(iarea)/gwy_data_field_get_dy(detail);
    gwy_data_field_resample(iarea, GWY_ROUND(wr), GWY_ROUND(hr), GWY_INTERPOLATION_LINEAR);
    immerse_correlate(iarea, detail, &col, &row);
    gwy_debug("[U] col: %d, row: %d", col, row);

    xpos = gwy_data_field_jtor(detail, col + 0.5) + gwy_data_field_jtor(field, xfrom);
    ypos = gwy_data_field_itor(detail, row + 0.5) + gwy_data_field_itor(field, yfrom);

    g_object_unref(iarea);
    clamp_detail_offset(gui, xpos, ypos);
    redraw(gui);
}

static void
immerse_correlate(GwyDataField *image, GwyDataField *kernel,
                  gint *col, gint *row)
{
    GwyDataField *subimage, *subkernel, *score, *imagearea;
    gdouble factor, sxreal, syreal, zunused;
    gint ixres, iyres, kxres, kyres;
    gint sixres, siyres, skxres, skyres;
    gint xfrom, yfrom, xto, yto;
    gint sx, sy, delta;

    ixres = gwy_data_field_get_xres(image);
    iyres = gwy_data_field_get_yres(image);
    kxres = gwy_data_field_get_xres(kernel);
    kyres = gwy_data_field_get_yres(kernel);
    gwy_debug("kernel: %dx%d, image: %dx%d", kxres, kyres, ixres, iyres);

    factor = MAX(downsample_factor, downsample_limit/sqrt(kxres*kyres));
    factor = MIN(factor, 1.0);

    skxres = GWY_ROUND(factor*kxres);
    skyres = GWY_ROUND(factor*kyres);
    sixres = GWY_ROUND(factor*ixres);
    siyres = GWY_ROUND(factor*iyres);
    gwy_debug("skernel: %dx%d, simage: %dx%d", skxres, skyres, sixres, siyres);

    subimage = gwy_data_field_new_resampled(image, sixres, siyres, GWY_INTERPOLATION_LINEAR);
    score = gwy_data_field_new_alike(subimage, FALSE);
    subkernel = gwy_data_field_new_resampled(kernel, skxres, skyres, GWY_INTERPOLATION_LINEAR);

    gwy_data_field_correlation_search(subimage, subkernel, NULL, score,
                                      GWY_CORR_SEARCH_COVARIANCE_SCORE, 0.01, GWY_EXTERIOR_BORDER_EXTEND, 0.0);
    gwy_data_field_get_local_maxima_list(score, &sxreal, &syreal, &zunused, 1, 0, 0.0, FALSE);
    sx = GWY_ROUND(sxreal);
    sy = GWY_ROUND(syreal);
    gwy_debug("sx: %d, sy: %d", sx, sy);
    g_object_unref(score);
    g_object_unref(subkernel);
    g_object_unref(subimage);

    /* Top left corner coordinate */
    sx -= skxres/2;
    sy -= skyres/2;
    /* Upscaled to original size */
    sx = GWY_ROUND((gdouble)ixres/sixres*sx);
    sy = GWY_ROUND((gdouble)iyres/siyres*sy);
    /* Uncertainty margin */
    delta = GWY_ROUND(1.5/factor + 1);
    /* Subarea to search */
    xfrom = MAX(sx - delta, 0);
    yfrom = MAX(sy - delta, 0);
    xto = MIN(sx + kxres + delta, ixres);
    yto = MIN(sy + kyres + delta, iyres);

    imagearea = gwy_data_field_area_extract(image, xfrom, yfrom, xto - xfrom, yto - yfrom);
    score = gwy_data_field_new_alike(imagearea, FALSE);
    gwy_data_field_correlation_search(imagearea, kernel, NULL, score,
                                      GWY_CORR_SEARCH_COVARIANCE_SCORE, 0.01, GWY_EXTERIOR_BORDER_EXTEND, 0.0);
    gwy_data_field_get_local_maxima_list(score, &sxreal, &syreal, &zunused, 1, 0, 0.0, FALSE);
    g_object_unref(score);
    g_object_unref(imagearea);

    *col = GWY_ROUND(sxreal) + xfrom - kxres/2;
    *row = GWY_ROUND(syreal) + yfrom - kyres/2;
}

static void
execute(ModuleArgs *args)
{
    GwyDataField *field = args->field, *detail = gwy_params_get_image(args->params, PARAM_DETAIL);
    GwyDataField *resampled, *result;
    GwyImmerseLevelType levelling = gwy_params_get_enum(args->params, PARAM_LEVELING);
    GwyImmerseSamplingType sampling = gwy_params_get_enum(args->params, PARAM_SAMPLING);
    gdouble xpos = gwy_params_get_double(args->params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(args->params, PARAM_YPOS);
    gint kxres, kyres;
    gint x, y, w, h;
    gdouble iavg, davg;

    davg = gwy_data_field_get_avg(detail);
    kxres = gwy_data_field_get_xres(detail);
    kyres = gwy_data_field_get_yres(detail);

    if (sampling == GWY_IMMERSE_SAMPLING_DOWN) {
        result = args->result = gwy_data_field_duplicate(field);
        x = gwy_data_field_rtoj(field, xpos);
        y = gwy_data_field_rtoi(field, ypos);
        w = GWY_ROUND(gwy_data_field_get_xreal(detail)/gwy_data_field_get_dx(field));
        h = GWY_ROUND(gwy_data_field_get_yreal(detail)/gwy_data_field_get_dy(field));
        w = MAX(w, 1);
        h = MAX(h, 1);
        gwy_debug("w: %d, h: %d", w, h);
        resampled = gwy_data_field_new_resampled(detail, w, h, GWY_INTERPOLATION_LINEAR);
        if (levelling == GWY_IMMERSE_LEVEL_MEAN) {
            iavg = gwy_data_field_area_get_avg_mask(result, NULL, GWY_MASK_IGNORE, x, y, w, h);
            gwy_data_field_add(resampled, iavg - davg);
        }
        gwy_data_field_area_copy(resampled, result, 0, 0, w, h, x, y);
        g_object_unref(resampled);
    }
    else if (sampling == GWY_IMMERSE_SAMPLING_UP) {
        w = GWY_ROUND(gwy_data_field_get_xreal(field)/gwy_data_field_get_dx(detail));
        h = GWY_ROUND(gwy_data_field_get_yreal(field)/gwy_data_field_get_dy(detail));
        gwy_debug("w: %d, h: %d", w, h);
        result = args->result = gwy_data_field_new_resampled(field, w, h, GWY_INTERPOLATION_LINEAR);
        x = gwy_data_field_rtoj(result, xpos);
        y = gwy_data_field_rtoi(result, ypos);
        if (levelling == GWY_IMMERSE_LEVEL_MEAN) {
            iavg = gwy_data_field_area_get_avg_mask(result, NULL, GWY_MASK_IGNORE, x, y, kxres, kyres);
            gwy_data_field_area_copy(detail, result, 0, 0, kxres, kyres, x, y);
            gwy_data_field_area_add(result, x, y, kxres, kyres, iavg - davg);
        }
        else
            gwy_data_field_area_copy(detail, result, 0, 0, kxres, kyres, x, y);
    }
    else {
        g_return_if_reached();
    }
}

static gboolean
immerse_view_expose(GtkWidget *view, GdkEventExpose *event, ModuleGUI *gui)
{
    GdkColor white = { 0, 0xffff, 0xffff, 0xffff };
    GwyParams *params = gui->args->params;
    gdouble xpos = gwy_params_get_double(params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(params, PARAM_YPOS);
    GdkGC *gc;
    gint w, h, xoff, yoff;

    if (event->count > 0)
        return FALSE;

    if (!gui->detail)
        return FALSE;

    gwy_data_view_coords_real_to_xy(GWY_DATA_VIEW(view), xpos, ypos, &xoff, &yoff);
    w = gdk_pixbuf_get_width(gui->detail);
    h = gdk_pixbuf_get_height(gui->detail);
    /* gwy_debug("(%d,%d) %dx%d", xoff, yoff, w, h); */
    gc = gdk_gc_new(view->window);
    if (gwy_params_get_boolean(params, PARAM_DRAW_DETAIL))
        gdk_draw_pixbuf(view->window, gc, gui->detail, 0, 0, xoff, yoff, w, h, GDK_RGB_DITHER_NORMAL, 0, 0);
    if (gwy_params_get_boolean(params, PARAM_DRAW_FRAME)) {
        gdk_gc_set_function(gc, GDK_XOR);
        gdk_gc_set_rgb_fg_color(gc, &white);
        gdk_draw_rectangle(view->window, gc, FALSE, xoff, yoff, w-1, h-1);
    }
    g_object_unref(gc);
    return FALSE;
}

static gboolean
immerse_view_button_press(GtkWidget *view, GdkEventButton *event, ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    gdouble xpos = gwy_params_get_double(params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(params, PARAM_YPOS);
    gint xoff, yoff;

    if (event->button != 1 || !immerse_view_inside_detail(gui, event->x, event->y))
        return FALSE;

    gwy_data_view_coords_real_to_xy(GWY_DATA_VIEW(view), xpos, ypos, &xoff, &yoff);
    gui->button = event->button;
    /* Cursor offset wrt top left corner */
    gui->xc = event->x - xoff;
    gui->yc = event->y - yoff;
    gdk_window_set_cursor(view->window, gui->move_cursor);

    return TRUE;
}

static gboolean
immerse_view_button_release(GtkWidget *view, GdkEventButton *event, ModuleGUI *gui)
{
    gdouble xpos, ypos;

    if (event->button != gui->button || !gui->detail)
        return FALSE;

    gui->button = 0;
    gwy_data_view_coords_xy_to_real(GWY_DATA_VIEW(view), event->x - gui->xc, event->y - gui->yc, &xpos, &ypos);
    clamp_detail_offset(gui, xpos, ypos);
    gdk_window_set_cursor(view->window, gui->near_cursor);
    redraw(gui);

    return TRUE;
}

static gboolean
immerse_view_motion_notify(GtkWidget *view, GdkEventMotion *event, ModuleGUI *gui)
{
    GdkWindow *window;
    gdouble xpos, ypos;
    gint x, y;

    if (!gui->detail)
        return FALSE;

    window = view->window;
    if (event->is_hint)
        gdk_window_get_pointer(window, &x, &y, NULL);
    else {
        x = event->x;
        y = event->y;
    }

    if (!gui->button) {
        if (immerse_view_inside_detail(gui, x, y))
            gdk_window_set_cursor(window, gui->near_cursor);
        else
            gdk_window_set_cursor(window, NULL);
    }
    else {
        gwy_data_view_coords_xy_to_real(GWY_DATA_VIEW(view), x - gui->xc, y - gui->yc, &xpos, &ypos);
        clamp_detail_offset(gui, xpos, ypos);
        redraw(gui);
    }

    return TRUE;
}

static gboolean
immerse_view_inside_detail(ModuleGUI *gui, gint x, gint y)
{
    GwyParams *params = gui->args->params;
    gdouble xpos = gwy_params_get_double(params, PARAM_XPOS);
    gdouble ypos = gwy_params_get_double(params, PARAM_YPOS);
    gint xoff, yoff, w, h;

    if (!gui->detail)
        return FALSE;

    gwy_data_view_coords_real_to_xy(GWY_DATA_VIEW(gui->view), xpos, ypos, &xoff, &yoff);
    w = gdk_pixbuf_get_width(gui->detail);
    h = gdk_pixbuf_get_height(gui->detail);
    return x >= xoff && x < xoff + w && y >= yoff && y < yoff + h;
}

static void
clamp_detail_offset(ModuleGUI *gui, gdouble xpos, gdouble ypos)
{
    GwyParams *params = gui->args->params;
    gchar *s;

    xpos = CLAMP(xpos, 0.0, gui->xmax);
    ypos = CLAMP(ypos, 0.0, gui->ymax);

    gwy_params_set_double(params, PARAM_XPOS, xpos);
    gwy_params_set_double(params, PARAM_YPOS, ypos);
    s = g_strdup_printf("(%.*f, %.*f)",
                        gui->vf->precision + 1, xpos/gui->vf->magnitude,
                        gui->vf->precision + 1, ypos/gui->vf->magnitude);
    gwy_param_table_info_set_valuestr(gui->table, INFO_XYPOS, s);
    g_free(s);
}

static void
redraw(ModuleGUI *gui)
{
    if (GTK_WIDGET_DRAWABLE(gui->view))
        gtk_widget_queue_draw(gui->view);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
