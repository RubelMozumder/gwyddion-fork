/*
 *  $Id: volume_localdetect.c 26354 2024-05-21 08:22:32Z yeti-dn $
 *  Copyright (C) 2015-2023 David Necas (Yeti).
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/brick.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/correlation.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    PARAM_METHOD,
    PARAM_WIDTH,
    PARAM_HEIGHT,
    PARAM_THRESHOLD,
    PARAM_UPSCALE,
    PARAM_KEEPUP,
//    PARAM_UPDATE
};

typedef enum {
    METHOD_PIXEL    = 0,
    METHOD_SUBPIXEL = 1,
    NMETHODS
} SearchMethod;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_options;
    GwyContainer *data;
} ModuleGUI;


static gboolean              module_register          (void);
static GwyParamDef*          define_module_params     (void);
static void                  localdetect              (GwyContainer *data,
                                                       GwyRunType runtype);
static void                  execute                  (ModuleArgs *args,
                                                       GtkWindow *wait_window,
                                                       gboolean resample);
static GwyDialogOutcome      run_gui                  (ModuleArgs *args,
                                                       GwyContainer *data,
                                                       gint id);
static void                  param_changed            (ModuleGUI *gui,
                                                       gint id);
static void                  dialog_response          (GwyDialog *dialog,
                                                       gint response,
                                                       ModuleGUI *gui);
static void                  preview                  (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Performs localization merge of all the levels"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, volume_localdetect)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_localdetect",
                             (GwyVolumeFunc)&localdetect,
                             N_("/SPM M_odes/_Localization Merge..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Perform localization merge"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;
    static const GwyEnum methods[] = {
        { N_("_pixel"),      METHOD_PIXEL,     },
        { N_("_sub-pixel"),  METHOD_SUBPIXEL,  },
    };

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("Local maxima search"),
                              methods, G_N_ELEMENTS(methods), METHOD_PIXEL);

    gwy_param_def_add_int(paramdef, PARAM_UPSCALE, "upscale", _("_Upsampling factor"), 1, 5, 1);
    gwy_param_def_add_boolean(paramdef, PARAM_KEEPUP, "keepup", _("Keep upsampled"), FALSE);
    gwy_param_def_add_double(paramdef, PARAM_WIDTH, "peak_width", _("_Peak width"),
                             0, G_MAXDOUBLE, 0.2e-9);
    gwy_param_def_add_double(paramdef, PARAM_HEIGHT, "height", _("_Height threshold"),
                             0, G_MAXDOUBLE, 0.2e-9);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "filter", _("Noise _filter width"),
                             0, G_MAXDOUBLE, 0.2e-9);
//    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
localdetect(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint oldid, newid;
    gboolean keepup;
    const guchar *gradient;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }

    keepup = gwy_params_get_boolean(args.params, PARAM_KEEPUP);
    if (outcome != GWY_DIALOG_HAVE_RESULT || keepup) {
        execute(&args, gwy_app_find_window_for_volume(data, oldid), !keepup);
    }

    if (args.result) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);

        if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(oldid), &gradient))
            gwy_container_set_const_string(data, gwy_app_get_data_palette_key_for_id(newid), gradient);

        gwy_app_set_data_field_title(data, newid, _("Localization result"));
        gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                0);
    }

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    GwyBrick *brick = args->brick;
    GwyDataField *result = gwy_data_field_new(gwy_brick_get_xres(brick), gwy_brick_get_yres(brick),
                                              gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick),
                                              TRUE);
    GwyDataField *dfield = gwy_data_field_new_alike(result, FALSE);
    const guchar *gradient;
    GwySIValueFormat *vf;
    gdouble min, max;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    args->result = result;

    gwy_data_field_set_si_unit_xy(result, gwy_brick_get_si_unit_x(brick));
    gwy_data_field_set_si_unit_z(result, gwy_brick_get_si_unit_w(brick));

    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), result);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Localization Merge"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table_options = gwy_param_table_new(args->params);

    gwy_param_table_append_combo(table, PARAM_METHOD);

    gwy_param_table_append_slider(table, PARAM_UPSCALE);
    gwy_param_table_append_checkbox(table, PARAM_KEEPUP);

    gwy_brick_extract_xy_plane(brick, dfield, 0);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD, "px");
    vf = gwy_si_unit_get_format(gwy_data_field_get_si_unit_xy(dfield), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                gwy_data_field_get_xreal(dfield)/10, NULL);
    vf->precision++;
    gwy_param_table_slider_set_factor(table, PARAM_THRESHOLD, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD, vf->units);
    gwy_param_table_slider_restrict_range(table, PARAM_THRESHOLD, 0, gwy_data_field_get_xreal(dfield)/10);


    gwy_param_table_append_slider(table, PARAM_HEIGHT);
    gwy_param_table_slider_set_mapping(table, PARAM_HEIGHT, GWY_SCALE_MAPPING_LINEAR);
    gwy_data_field_get_min_max(dfield, &min, &max);
    vf = gwy_si_unit_get_format(gwy_data_field_get_si_unit_z(dfield), GWY_SI_UNIT_FORMAT_VFMARKUP, max-min, NULL);
    vf->precision++;
    gwy_param_table_slider_set_factor(table, PARAM_HEIGHT, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_HEIGHT, vf->units);
    gwy_param_table_slider_restrict_range(table, PARAM_HEIGHT, 0, max-min);

    gwy_param_table_append_slider(table, PARAM_WIDTH);
    vf = gwy_si_unit_get_format(gwy_data_field_get_si_unit_xy(dfield), GWY_SI_UNIT_FORMAT_VFMARKUP,
                                gwy_data_field_get_xreal(dfield)/10, NULL);
    vf->precision++;
    gwy_param_table_slider_set_factor(table, PARAM_WIDTH, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_WIDTH, vf->units);
    gwy_param_table_slider_restrict_range(table, PARAM_WIDTH, 0, gwy_data_field_get_xreal(dfield)/10);

    //gwy_param_table_set_unitstr(table, PARAM_WIDTH, "px");

//    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_UPON_REQUEST, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(dfield);

    return outcome;
}


static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, G_GNUC_UNUSED gint response, G_GNUC_UNUSED ModuleGUI *gui)
{
//    if (response == GWY_RESPONSE_RESET)
//        gwy_brick_copy(gui->args->brick, gui->args->result, FALSE);

    //printf("dialog response calling preview\n");
    //preview(gui);
}


static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    execute(gui->args, GTK_WINDOW(gui->dialog), TRUE);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
filter_extrema_field(GwyDataField *dfield, GwyDataField *maxs, gdouble threshold)
{
    gint i, n = gwy_data_field_get_xres(dfield)*gwy_data_field_get_yres(dfield);
    gdouble *data = gwy_data_field_get_data(dfield);
    gdouble *mdata = gwy_data_field_get_data(maxs);

    for (i = 0; i < n; i++)
        mdata[i] = (mdata[i] > 0 && data[i] > threshold) ? data[i] : 0.0;
}

static void
add_gaussian(gdouble *data, gint xres, gint yres, gdouble x, gdouble y, gdouble height, gdouble width)
{
    gint i, j;
    gint ix = (gint)x;
    gint iy = (gint)y;
    gint size;
    gdouble xc, yc;
    gdouble ww = 2*width*width;

    size = 10*width;

    for (i = MAX(0, ix - size); i < MIN(xres, ix + size); i++) {
        for (j = MAX(0, iy - size); j < MIN(yres, iy + size); j++) {
            xc = x - i;
            yc = y - j;
            data[i + xres*j] += height * exp(-(xc*xc + yc*yc)/ww);
        }
    }
}

static void
subpixel_gaussians(GwyDataField *topography, GwyDataField *peaks, gdouble width)
{
    gint i, j, ri, rj, rn;
    gdouble xoffset, yoffset, *pdata, *tdata, *gdata;
    GwyDataField *original = gwy_data_field_duplicate(peaks);
    gint xres = gwy_data_field_get_xres(original);
    gint yres = gwy_data_field_get_yres(original);
    gdouble zvals[9];

    gwy_data_field_clear(peaks);
    gdata = gwy_data_field_get_data(peaks);
    pdata = gwy_data_field_get_data(original);
    tdata = gwy_data_field_get_data(topography);

    for (j = 2; j < yres-2; j++) {
        for (i = 2; i < xres-2; i++) {
            if (pdata[i + xres*j] > 0) { //a local maximum in copied peaks channel
                //refine local maximum position
                rn = 0;
                for (rj = -1; rj <= 1; rj++) {
                    for (ri = -1; ri <= 1; ri++) {
                        zvals[rn++] = tdata[i + ri + xres*(j + rj)]; //use topography for neighborhood
                    }
                }
                gwy_math_refine_maximum_2d(zvals, &xoffset, &yoffset);
                xoffset += i;
                yoffset += j;

                add_gaussian(gdata, xres, yres, xoffset, yoffset, tdata[i + xres*j], width);
            }
        }
    }
    g_object_unref(original);
}


static void
execute(ModuleArgs *args, GtkWindow *wait_window, gboolean resample)
{
    GwyParams *params = args->params;
    gint k;
    GwyBrick *brick = args->brick;
    GwyDataField *result = args->result;
    GwyDataField *dfield, *maxs, *sum;
    gint upscale = gwy_params_get_int(params, PARAM_UPSCALE);
    gdouble width = gwy_params_get_double(params, PARAM_WIDTH);
    gdouble height = gwy_params_get_double(params, PARAM_HEIGHT);
    gdouble threshold = gwy_params_get_double(params, PARAM_THRESHOLD);
    gint method = gwy_params_get_enum(params, PARAM_METHOD);
    gboolean cancelled = FALSE;

    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint zres = gwy_brick_get_zres(brick);

    gwy_app_wait_start(wait_window, _("Running localization..."));

    dfield = gwy_data_field_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);
    sum = gwy_data_field_new(upscale*xres, upscale*yres,
                             gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick),
                             TRUE);
    maxs = gwy_data_field_new_alike(sum, TRUE);

    width = gwy_data_field_rtoi(dfield, width);
    threshold = gwy_data_field_rtoi(dfield, threshold);

    for (k = 0; k < zres; k++) {
        gwy_brick_extract_xy_plane(brick, dfield, k);
        gwy_data_field_filter_gaussian(dfield, threshold);  //run pre-filtering
        gwy_data_field_add(dfield, -gwy_data_field_get_min(dfield)); //shift to minimum to enable height threshold

        if (upscale != 1)   //if requested, work with upsampled images
            gwy_data_field_resample(dfield, upscale*xres, upscale*yres, GWY_INTERPOLATION_SCHAUM);

        gwy_data_field_mark_extrema(dfield, maxs, TRUE);    //find maxima mask
        filter_extrema_field(dfield, maxs, height); //add height information there

        if (method == METHOD_PIXEL) {
            gwy_data_field_filter_gaussian(maxs, upscale*width);  //broaden it only
        }
        else {
            subpixel_gaussians(dfield, maxs, upscale*width); //find sub-pixel peak positions and broaden it
        }

        gwy_data_field_sum_fields(sum, sum, maxs);     //add it to the result

        if (!gwy_app_wait_set_fraction((gdouble)k/zres)) {
            cancelled = TRUE;
            break;
        }
    }
    gwy_app_wait_finish();

    if (!cancelled) {
        //upsample result field or downsample calculation output
        if (!resample)
            gwy_data_field_resample(result, upscale*xres, upscale*yres, GWY_INTERPOLATION_NONE);
        else
            gwy_data_field_resample(sum, xres, yres, GWY_INTERPOLATION_SCHAUM);

        gwy_data_field_copy(sum, result, FALSE);
        gwy_data_field_data_changed(result);
        gwy_data_field_multiply(result, 1.0/zres); //divide by number of images to get an average
    }

    g_object_unref(dfield);
    g_object_unref(maxs);
    g_object_unref(sum);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
