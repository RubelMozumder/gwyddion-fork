/*
 *  $Id: volume_psf.c 25577 2023-07-28 12:17:01Z yeti-dn $
 *  Copyright (C) 2017-2023 David Necas (Yeti), Petr Klapetek.
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
#include <fftw3.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/mfm.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwylayer-basic.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwycheckboxes.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"
#include "../process/preview.h"
#include "../process/mfmops.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

#define FIT_GRADIENT_NAME "__GwyFitDiffGradient"

#define field_convolve_default(field, kernel) \
    gwy_data_field_area_ext_convolve((field), \
                                     0, 0, \
                                     gwy_data_field_get_xres(field), \
                                     gwy_data_field_get_yres(field), \
                                     (field), (kernel), \
                                     GWY_EXTERIOR_BORDER_EXTEND, 0.0, TRUE)

#define gwycreal(x) ((x)[0])
#define gwycimag(x) ((x)[1])

/* Do not require FFTW 3.3 just for a couple of trivial macros. */
#define gwy_fftw_new_real(n) (gdouble*)fftw_malloc((n)*sizeof(gdouble))
#define gwy_fftw_new_complex(n) (fftw_complex*)fftw_malloc((n)*sizeof(fftw_complex))

enum {
    RESPONSE_FULL_SIZE = 1000,
};

enum {
    PARAM_IDEAL,
    PARAM_BORDER,
    PARAM_DISPLAY,
    PARAM_ZLEVEL,
    PARAM_DIFF_COLOURMAP,
    PARAM_METHOD,
    PARAM_SIGMA,
    PARAM_ESTIMATE_SIGMA,
    PARAM_TXRES,
    PARAM_TYRES,
    PARAM_ESTIMATE_TRES,
    PARAM_WINDOWING,
    PARAM_AS_INTEGRAL,

    PARAM_OUTPUT_TYPE,

    BUTTON_FULL_SIZE,
    BUTTON_ESTIMATE_SIZE,
    INFO_SIGMA_FITTED_AT,
    WIDGET_RESULTS,
};

typedef enum {
    PSF_METHOD_REGULARISED   = 0,
    PSF_METHOD_LEAST_SQUARES = 1,
    PSF_METHOD_PSEUDO_WIENER = 2,
    PSF_NMETHODS
} PSFMethodType;

typedef enum {
    PSF_DISPLAY_DATA       = 0,
    PSF_DISPLAY_PSF        = 1,
    PSF_DISPLAY_CONVOLVED  = 2,
    PSF_DISPLAY_DIFFERENCE = 3,
    PSF_NDISPLAYS
} PSFDisplayType;

typedef enum {
    PSF_OUTPUT_PSF       = 0,
    PSF_OUTPUT_TF_WIDTH  = 1,
    PSF_OUTPUT_TF_HEIGHT = 2,
    PSF_OUTPUT_TF_NORM   = 3,
    PSF_OUTPUT_DIFF_NORM = 4,
    PSF_OUTPUT_SIGMA     = 5,
} PSFOutputType;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GwyParamTable *table_param;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwyGradient *diff_gradient;
    GwyResults *results;
    GwyDataField *xyplane;
    GwyDataField *psf;
    GwyDataField *convolved;
    GwyDataField *difference;
} ModuleGUI;

static gboolean         module_register                (void);
static GwyParamDef*     define_module_params           (void);
static void             volume_psf                     (GwyContainer *data,
                                                        GwyRunType run);
static GwyDialogOutcome run_gui                        (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static GwyResults*      create_results                 (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static void             param_changed                  (ModuleGUI *gui,
                                                        gint id);
static void             dialog_response                (ModuleGUI *gui,
                                                        gint response);
static void             preview                        (gpointer user_data);
static void             execute_and_create_outputs     (ModuleArgs *args,
                                                        GwyContainer *data,
                                                        gint id);
static void             switch_display                 (ModuleGUI *gui);
static void             prepare_field                  (GwyDataField *field,
                                                        GwyDataField *wfield,
                                                        GwyWindowingType window);
static void             calculate_tf                   (GwyDataField *measured_buf,
                                                        GwyDataField *wideal,
                                                        GwyDataField *psf,
                                                        GwyParams *params);
static void             extract_xyplane                (ModuleGUI *gui);
static gboolean         ideal_image_filter             (GwyContainer *data,
                                                        gint id,
                                                        gpointer user_data);
static gdouble          find_regularization_sigma      (GwyDataField *field,
                                                        GwyDataField *ideal,
                                                        GwyParams *params);
static void             adjust_tf_field_to_non_integral(GwyDataField *psf);
static void             adjust_tf_brick_to_non_integral(GwyBrick *psf);
static gdouble          measure_tf_width               (GwyDataField *psf);
static void             psf_deconvolve_wiener          (GwyDataField *field,
                                                        GwyDataField *operand,
                                                        GwyDataField *out,
                                                        gdouble sigma);
static gboolean         method_is_full_sized           (PSFMethodType method);
static void             estimate_tf_region             (GwyDataField *wmeas,
                                                        GwyDataField *wideal,
                                                        GwyDataField *psf,
                                                        gint *col,
                                                        gint *row,
                                                        gint *width,
                                                        gint *height);
static void             symmetrise_tf_region           (gint pos,
                                                        gint len,
                                                        gint res,
                                                        gint *tres);
static gboolean         clamp_psf_size                 (GwyBrick *brick,
                                                        ModuleArgs *args);
static void             sanitise_params                (ModuleArgs *args);

static const GwyEnum output_types[] = {
    { N_("Transfer function"), (1 << PSF_OUTPUT_PSF),       },
    { N_("TF width"),          (1 << PSF_OUTPUT_TF_WIDTH),  },
    { N_("TF height"),         (1 << PSF_OUTPUT_TF_HEIGHT), },
    { N_("TF norm"),           (1 << PSF_OUTPUT_TF_NORM),   },
    { N_("Difference norm"),   (1 << PSF_OUTPUT_DIFF_NORM), },
    { N_("Sigma"),             (1 << PSF_OUTPUT_SIGMA),     },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates the volume PSF."),
    "Petr Klapetek <pklapetek@gwyddion.net>",
    "3.0",
    "Petr Klapetek, Robb Puttock & David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_psf)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_psf",
                             (GwyVolumeFunc)&volume_psf,
                             N_("/_Statistics/_Transfer Function Guess..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Estimate transfer function from known data and ideal images"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum methods[] = {
        { N_("Regularized filter"), PSF_METHOD_REGULARISED,   },
        { N_("Least squares"),      PSF_METHOD_LEAST_SQUARES, },
        { N_("Wiener filter"),      PSF_METHOD_PSEUDO_WIENER, },
    };
    static const GwyEnum displays[] = {
        { N_("Data"),              PSF_DISPLAY_DATA,       },
        { N_("Transfer function"), PSF_DISPLAY_PSF,        },
        { N_("Convolved"),         PSF_DISPLAY_CONVOLVED,  },
        { N_("Difference"),        PSF_DISPLAY_DIFFERENCE, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_IDEAL, "ideal", _("_Ideal response"));
    gwy_param_def_add_int(paramdef, PARAM_BORDER, "border", _("_Border"), 0, 16384, 2);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|_Display"),
                              displays, G_N_ELEMENTS(displays), PSF_DISPLAY_PSF);
    gwy_param_def_add_int(paramdef, PARAM_ZLEVEL, "zlevel", _("_Z level"), 0, G_MAXINT, 0);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFF_COLOURMAP, "diff_colourmap",
                              _("Show differences with _adapted color map"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("_Method"),
                              methods, G_N_ELEMENTS(methods), PSF_METHOD_REGULARISED);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Sigma"), -8.0, 3.0, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_ESTIMATE_SIGMA, "estimate_sigma", _("_Estimate sigma for each level"),
                              FALSE);
    gwy_param_def_add_int(paramdef, PARAM_TXRES, "txres", _("_Horizontal size"), 3, G_MAXINT, 41);
    gwy_param_def_add_int(paramdef, PARAM_TYRES, "tyres", _("_Vertical size"), 3, G_MAXINT, 41);
    gwy_param_def_add_boolean(paramdef, PARAM_ESTIMATE_TRES, "estimate_tres", _("_Estimate size for each level"),
                              FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOWING, "windowing", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_WELCH);
    gwy_param_def_add_boolean(paramdef, PARAM_AS_INTEGRAL, "as_integral", "Normalize as _integral", TRUE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output"),
                               output_types, G_N_ELEMENTS(output_types),
                               (1 << PSF_OUTPUT_PSF) | (1 << PSF_OUTPUT_TF_WIDTH));
    return paramdef;
}

static void
volume_psf(GwyContainer *data, GwyRunType run)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyBrick *brick = NULL;
    gint id;

    g_return_if_fail(run & RUN_MODES);

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());
    if (!clamp_psf_size(brick, &args)) {
        if (run == GWY_RUN_INTERACTIVE) {
            GtkWidget *dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Image is too small."));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        goto end;
    }
    sanitise_params(&args);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    execute_and_create_outputs(&args, data, id);

end:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    gint xres = gwy_brick_get_xres(args->brick), yres = gwy_brick_get_yres(args->brick);
    GtkWidget *hbox, *notebook;
    GwyDialog *dialog;
    GwyParamTable *table;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args, data, id);
    gui.data = gwy_container_new();
    gui.xyplane = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), gui.xyplane);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(1), FIT_GRADIENT_NAME);
    extract_xyplane(&gui);
    gui.convolved = gwy_data_field_new_alike(gui.xyplane, TRUE);
    gui.difference = gwy_data_field_new_alike(gui.xyplane, TRUE);
    gui.psf = gwy_data_field_new(1, 1, 1.0, 1.0, TRUE);

    gui.diff_gradient = gwy_inventory_new_item(gwy_gradients(), GWY_GRADIENT_DEFAULT, FIT_GRADIENT_NAME);
    gwy_resource_use(GWY_RESOURCE(gui.diff_gradient));

    gui.dialog = gwy_dialog_new(_("Estimate Transfer Function"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Fit Sigma"), RESPONSE_REFINE);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 4);

    table = gui.table_param = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_IDEAL);
    gwy_param_table_data_id_set_filter(table, PARAM_IDEAL, ideal_image_filter, args->brick, NULL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA, "log<sub>10</sub>");
    gwy_param_table_append_checkbox(table, PARAM_ESTIMATE_SIGMA);
    gwy_param_table_append_checkbox(table, PARAM_ESTIMATE_TRES);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_combo(table, PARAM_WINDOWING);
    gwy_param_table_append_slider(table, PARAM_ZLEVEL);
    gwy_param_table_slider_restrict_range(table, PARAM_ZLEVEL, 0, gwy_brick_get_zres(args->brick)-1);
    gwy_param_table_slider_set_mapping(table, PARAM_ZLEVEL, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_append_info(table, INFO_SIGMA_FITTED_AT, _("Sigma was fitted at Z level"));
    gwy_param_table_set_unitstr(table, INFO_SIGMA_FITTED_AT, _("px"));

    gwy_param_table_append_header(table, -1, _("Transfer Function Size"));
    gwy_param_table_append_slider(table, PARAM_TXRES);
    gwy_param_table_slider_set_mapping(table, PARAM_TXRES, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_slider_restrict_range(table, PARAM_TXRES, 3, xres);
    gwy_param_table_append_slider(table, PARAM_TYRES);
    gwy_param_table_slider_set_mapping(table, PARAM_TYRES, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_slider_restrict_range(table, PARAM_TYRES, 3, yres);
    gwy_param_table_append_slider(table, PARAM_BORDER);
    gwy_param_table_slider_restrict_range(table, PARAM_BORDER, 0, MIN(xres, yres)/8);
    gwy_param_table_slider_set_mapping(table, PARAM_BORDER, GWY_SCALE_MAPPING_SQRT);
    gwy_param_table_append_button(table, BUTTON_FULL_SIZE, -1,
                                  RESPONSE_FULL_SIZE, _("_Full Size"));
    gwy_param_table_append_button(table, BUTTON_ESTIMATE_SIZE, BUTTON_FULL_SIZE,
                                  RESPONSE_ESTIMATE, _("_Estimate Size"));

    gwy_param_table_append_header(table, -1, _("Preview Options"));
    gwy_param_table_append_combo(table, PARAM_DISPLAY);
    gwy_param_table_append_checkbox(table, PARAM_DIFF_COLOURMAP);

    gwy_param_table_append_header(table, -1, _("Result"));
    gwy_param_table_append_results(table, WIDGET_RESULTS, gui.results, "width", "height", "l2norm", "residuum", NULL);

    gwy_dialog_add_param_table(dialog, table);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("Parameters"));

    table = gui.table_output = gwy_param_table_new(args->params);
    gwy_param_table_append_checkboxes(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_AS_INTEGRAL);

    gwy_dialog_add_param_table(dialog, table);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), gwy_param_table_widget(table), gtk_label_new("Output"));

    g_signal_connect_swapped(gui.table_param, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_output, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    gwy_resource_release(GWY_RESOURCE(gui.diff_gradient));
    gwy_inventory_delete_item(gwy_gradients(), FIT_GRADIENT_NAME);
    g_object_unref(gui.xyplane);
    g_object_unref(gui.convolved);
    g_object_unref(gui.difference);
    g_object_unref(gui.psf);
    g_object_unref(gui.data);
    g_object_unref(gui.results);

    return outcome;
}

static GwyResults*
create_results(G_GNUC_UNUSED ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyResults *results = gwy_results_new();

    /* XXX: Currently we do not use these because the TF parameters are not exportable. */
    gwy_results_add_header(results, N_("Transfer Function"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "image", N_("Image"));
    gwy_results_add_separator(results);

    gwy_results_add_value_x(results, "width", N_("TF width"));
    gwy_results_add_value_z(results, "height", N_("TF height"));
    gwy_results_add_value(results, "l2norm", N_("TF norm"), "power-u", 1, NULL);
    gwy_results_add_value(results, "residuum", N_("Difference norm"), "power-v", 1, NULL);

    gwy_results_fill_filename(results, "file", data);
    gwy_results_fill_channel(results, "image", data, id);

    return results;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    gboolean full_sized = method_is_full_sized(method);

    if (id < 0 || id == PARAM_ZLEVEL) {
        extract_xyplane(gui);
        gwy_data_field_data_changed(gui->xyplane);
    }
    if (id < 0 || id == PARAM_DISPLAY || id == PARAM_DIFF_COLOURMAP)
        switch_display(gui);

    if (id < 0 || id == PARAM_METHOD || id == PARAM_OUTPUT_TYPE) {
        gboolean have_ideal = !gwy_params_data_id_is_none(params, PARAM_IDEAL);
        guint output = gwy_params_get_flags(params, PARAM_OUTPUT_TYPE);

        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, output && have_ideal);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_REFINE, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_FULL_SIZE, have_ideal && full_sized);
        gwy_param_table_set_sensitive(gui->table_param, PARAM_ESTIMATE_TRES, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, PARAM_ESTIMATE_SIGMA, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_ESTIMATE_SIZE, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, PARAM_BORDER, !full_sized);
        gwy_param_table_set_sensitive(gui->table_output, PARAM_AS_INTEGRAL, output & (1 << PSF_OUTPUT_PSF));
    }
    if (id < 0 || id == PARAM_SIGMA)
        gwy_param_table_info_set_valuestr(gui->table_param, INFO_SIGMA_FITTED_AT, NULL);

    if (id < 0 || id == PARAM_METHOD) {
        gint xres = gwy_brick_get_xres(args->brick);
        gint yres = gwy_brick_get_yres(args->brick);
        gint txres = gwy_params_get_int(args->params, PARAM_TXRES);
        gint tyres = gwy_params_get_int(args->params, PARAM_TYRES);
        gint xupper, yupper;

        if (full_sized) {
            xupper = xres;
            yupper = yres;
        }
        else {
            xupper = (xres/3) | 1;
            yupper = (yres/3) | 1;
        }
        gwy_param_table_slider_restrict_range(gui->table_param, PARAM_TXRES, 3, MAX(xupper, 3));
        gwy_param_table_slider_restrict_range(gui->table_param, PARAM_TYRES, 3, MAX(yupper, 3));

        if (full_sized) {
            gwy_param_table_slider_set_steps(gui->table_param, PARAM_TXRES, 1, 10);
            gwy_param_table_slider_set_steps(gui->table_param, PARAM_TYRES, 1, 10);
        }
        else {
            gwy_param_table_set_int(gui->table_param, PARAM_TXRES, (MIN(txres, xupper) - 1) | 1);
            gwy_param_table_set_int(gui->table_param, PARAM_TYRES, (MIN(tyres, yupper) - 1) | 1);
            gwy_param_table_slider_set_steps(gui->table_param, PARAM_TXRES, 2, 10);
            gwy_param_table_slider_set_steps(gui->table_param, PARAM_TYRES, 2, 10);
        }
    }

    if (id != PARAM_DISPLAY && id != PARAM_OUTPUT_TYPE && id != PARAM_ESTIMATE_SIGMA && id != PARAM_ESTIMATE_TRES)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table_param;

    if (response == RESPONSE_ESTIMATE) {
        GwyDataField *ideal = gwy_params_get_image(params, PARAM_IDEAL);
        GwyDataField *wmeas, *wideal, *psf;
        GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
        gint col, row, width, height, txres, tyres, border;

        wmeas = gwy_data_field_new_alike(gui->xyplane, FALSE);
        wideal = gwy_data_field_new_alike(ideal, FALSE);
        prepare_field(gui->xyplane, wmeas, windowing);
        prepare_field(ideal, wideal, windowing);

        psf = gwy_data_field_new_alike(gui->xyplane, TRUE);
        estimate_tf_region(wmeas, wideal, psf, &col, &row, &width, &height);
        g_object_unref(psf);
        g_object_unref(wideal);
        g_object_unref(wmeas);

        symmetrise_tf_region(col, width, gwy_data_field_get_xres(ideal), &txres);
        symmetrise_tf_region(row, height, gwy_data_field_get_yres(ideal), &tyres);
        border = GWY_ROUND(0.5*log(fmax(txres, tyres)) + 0.5);
        gwy_param_table_set_int(table, PARAM_TXRES, txres);
        gwy_param_table_set_int(table, PARAM_TYRES, tyres);
        gwy_param_table_set_int(table, PARAM_BORDER, border);
    }
    else if (response == RESPONSE_FULL_SIZE) {
        gwy_param_table_set_int(table, PARAM_TXRES, gwy_brick_get_xres(args->brick));
        gwy_param_table_set_int(table, PARAM_TYRES, gwy_brick_get_yres(args->brick));
    }
    else if (response == RESPONSE_REFINE) {
        GwyDataField *measured = gui->xyplane, *ideal = gwy_params_get_image(params, PARAM_IDEAL);
        gint lev = gwy_params_get_int(args->params, PARAM_ZLEVEL);
        gdouble sigma;
        gchar *s;

        sigma = find_regularization_sigma(measured, ideal, params);
        gwy_param_table_set_double(table, PARAM_SIGMA, log(sigma)/G_LN10);
        s = g_strdup_printf("%d", lev);
        gwy_param_table_info_set_valuestr(table, INFO_SIGMA_FITTED_AT, s);
        g_free(s);
    }
}

static void
extract_xyplane(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint lev = gwy_params_get_int(args->params, PARAM_ZLEVEL);

    gwy_brick_extract_xy_plane(args->brick, gui->xyplane, lev);
}

static gdouble
calculate_l2_norm(GwyDataField *field, gboolean as_integral,
                  GwySIUnit *unit)
{
    gdouble l2norm, q;

    l2norm = gwy_data_field_get_mean_square(field);

    /* In the integral formulation, we calculate the integral of squared values and units of dx dy are reflected in
     * the result.  In non-integral, we calculate a mere sum of squared values and the result has the same units as
     * the field values. */
    if (as_integral) {
        q = gwy_data_field_get_xreal(field) * gwy_data_field_get_yreal(field);
        if (unit)
            gwy_si_unit_multiply(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field), unit);
    }
    else {
        q = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
        if (unit)
            gwy_si_unit_assign(unit, gwy_data_field_get_si_unit_z(field));
    }

    return sqrt(q*l2norm);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyWindowingType windowing = gwy_params_get_enum(args->params, PARAM_WINDOWING);
    gboolean as_integral = gwy_params_get_boolean(args->params, PARAM_AS_INTEGRAL);
    GwyDataField *field1 = gui->xyplane, *psf = gui->psf, *convolved = gui->convolved, *difference = gui->difference;
    GwyDataField *field2 = gwy_params_get_image(args->params, PARAM_IDEAL);
    GwyDataField *wfield2;
    gdouble min, max, l2norm, resid;
    GwyResults *results;
    GwySIUnit *unit;

    wfield2 = gwy_data_field_duplicate(field2);
    prepare_field(wfield2, wfield2, windowing);
    calculate_tf(field1, wfield2, psf, args->params);
    g_object_unref(wfield2);

    gwy_data_field_assign(convolved, field2);
    gwy_data_field_add(convolved, -gwy_data_field_get_avg(convolved));
    field_convolve_default(convolved, psf);
    gwy_data_field_add(convolved, gwy_data_field_get_avg(field1));

    gwy_data_field_assign(difference, field1);
    gwy_data_field_subtract_fields(difference, field1, convolved);

    /* Change the normalisation to the discrete (i.e. wrong) one after all calculations are done. */
    if (!as_integral)
        adjust_tf_field_to_non_integral(psf);
    switch_display(gui);

    results = gui->results;
    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_xy(psf));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_xy(psf));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(psf));
    gwy_data_field_get_min_max(psf, &min, &max);
    unit = gwy_si_unit_new(NULL);
    l2norm = calculate_l2_norm(psf, as_integral, unit);
    gwy_results_set_unit(results, "u", unit);
    resid = calculate_l2_norm(convolved, as_integral, unit);
    gwy_results_set_unit(results, "v", unit);
    g_object_unref(unit);
    gwy_results_fill_values(results,
                           "width", measure_tf_width(psf),
                           "height", fmax(fabs(min), fabs(max)),
                           "l2norm", l2norm,
                           "residuum", resid,
                           NULL);
    gwy_param_table_results_fill(gui->table_param, WIDGET_RESULTS);

    gwy_data_field_data_changed(gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0)));
}

static void
switch_display(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyLayerBasic *blayer = GWY_LAYER_BASIC(gwy_data_view_get_base_layer(GWY_DATA_VIEW(gui->dataview)));
    PSFDisplayType display = gwy_params_get_enum(args->params, PARAM_DISPLAY);
    gboolean diff_colourmap = gwy_params_get_boolean(gui->args->params, PARAM_DIFF_COLOURMAP);
    GwyLayerBasicRangeType range_type = GWY_LAYER_BASIC_RANGE_FULL;
    gint i = 0;

    if (display == PSF_DISPLAY_DATA)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), gui->xyplane);
    else if (display == PSF_DISPLAY_PSF)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), gui->psf);
    else if (display == PSF_DISPLAY_CONVOLVED)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), gui->convolved);
    else if (display == PSF_DISPLAY_DIFFERENCE) {
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), gui->difference);
        if (diff_colourmap) {
            gdouble min, max, dispmin, dispmax;

            i = 1;
            range_type = GWY_LAYER_BASIC_RANGE_FIXED;
            gwy_data_field_get_min_max(gui->difference, &min, &max);
            gwy_data_field_get_autorange(gui->difference, &dispmin, &dispmax);
            gwy_set_gradient_for_residuals(gui->diff_gradient, min, max, &dispmin, &dispmax);
            gwy_container_set_double_by_name(gui->data, "/0/base/min", dispmin);
            gwy_container_set_double_by_name(gui->data, "/0/base/max", dispmax);
        }
        else
            range_type = GWY_LAYER_BASIC_RANGE_AUTO;
    }
    gwy_container_set_enum(gui->data, gwy_app_get_data_range_type_key_for_id(0), range_type);
    gwy_layer_basic_set_gradient_key(blayer, g_quark_to_string(gwy_app_get_data_palette_key_for_id(i)));
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SIZE);
    /* Prevent the size changing wildly the moment someone touches the size adjbars. */
    gtk_widget_set_size_request(gui->dataview, PREVIEW_SIZE, PREVIEW_SIZE);
}

static gboolean
ideal_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyBrick *brick = (GwyBrick*)user_data;
    GwyDataField *ideal = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    return !gwy_data_field_check_compatibility_with_brick_xy(ideal, brick,
                                                             GWY_DATA_COMPATIBILITY_RES
                                                             | GWY_DATA_COMPATIBILITY_REAL
                                                             | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
execute_and_create_outputs(ModuleArgs *args, GwyContainer *data, gint id)
{
    static const PSFOutputType graph_outputs[] = {
        1 << PSF_OUTPUT_TF_WIDTH, 1 << PSF_OUTPUT_TF_HEIGHT,
        1 << PSF_OUTPUT_TF_NORM, 1 << PSF_OUTPUT_DIFF_NORM,
        1 << PSF_OUTPUT_SIGMA,
    };
    enum { NGRAPH_OUTPUTS = G_N_ELEMENTS(graph_outputs) };

    GwyParams *params = args->params;
    GwyDataField *ideal = gwy_params_get_image(params, PARAM_IDEAL);
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    guint output_type = gwy_params_get_flags(params, PARAM_OUTPUT_TYPE);
    GwyDataField *wideal;
    GtkWindow *window;
    GwyBrick *result = NULL, *brick = args->brick;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyDataLine *plots[NGRAPH_OUTPUTS];
    GwyDataLine *zcal;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    gint i, newid;
    gdouble dx, dy, zreal, min, max;
    const gchar *name;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    window = gwy_app_find_window_for_volume(data, id);
    gwy_app_wait_start(window, _("Calculating volume transfer function..."));

    wideal = gwy_data_field_duplicate(ideal);
    prepare_field(wideal, wideal, windowing);

    dx = gwy_brick_get_dx(brick);
    dy = gwy_brick_get_dy(brick);
    zreal = gwy_brick_get_zreal(brick);
    if (output_type & (1 << PSF_OUTPUT_PSF)) {
        gint txres = gwy_params_get_int(params, PARAM_TXRES);
        gint tyres = gwy_params_get_int(params, PARAM_TXRES);

        result = gwy_brick_new(txres, tyres, zres, txres*dx, tyres*dy, zreal, FALSE);
        gwy_brick_copy_units(brick, result);
        gwy_brick_copy_zcalibration(brick, result);
    }
    for (i = 0; i < NGRAPH_OUTPUTS; i++) {
        if (output_type & graph_outputs[i]) {
            plots[i] = gwy_data_line_new(zres, zreal, FALSE);
            gwy_si_unit_assign(gwy_data_line_get_si_unit_x(plots[i]), gwy_brick_get_si_unit_z(brick));
        }
        else
            plots[i] = NULL;
    }

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,result,ideal,wideal,plots,args,xres,yres,zres,min,max,windowing,pcancelled)
#endif
    {
        gint kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);
        GwyDataField *measured, *psf, *buf, *convolved = NULL, *wmeas = NULL;
        GwyParams *tparams = gwy_params_duplicate(args->params);
        gboolean estimate_tres = gwy_params_get_boolean(tparams, PARAM_ESTIMATE_TRES);
        gboolean estimate_sigma = gwy_params_get_boolean(tparams, PARAM_ESTIMATE_SIGMA);
        gboolean as_integral = gwy_params_get_boolean(tparams, PARAM_AS_INTEGRAL);
        gint txres = gwy_params_get_int(tparams, PARAM_TXRES);
        gint tyres = gwy_params_get_int(tparams, PARAM_TYRES);
        gdouble sigma = pow10(gwy_params_get_double(tparams, PARAM_SIGMA));
        GwySIUnit *unit;
        gint k;

        /* Measured does not have correct units here yet. */
        psf = gwy_data_field_new_alike(ideal, FALSE);
        measured = gwy_data_field_new(xres, yres, gwy_brick_get_xreal(brick), gwy_brick_get_yreal(brick), FALSE);
        for (k = kfrom; k < kto; k++) {
            gwy_brick_extract_xy_plane(brick, measured, k);
            if (estimate_tres) {
                gint col, row, width, height;

                if (!wmeas)
                    wmeas = gwy_data_field_new_alike(measured, FALSE);
                prepare_field(measured, wmeas, windowing);
                estimate_tf_region(wmeas, wideal, psf, &col, &row, &width, &height);
                symmetrise_tf_region(col, width, xres, &txres);
                symmetrise_tf_region(row, height, yres, &tyres);
                gwy_params_set_int(tparams, PARAM_TXRES, txres);
                gwy_params_set_int(tparams, PARAM_TYRES, tyres);
                /* find_regularization_sigma() does its own windowing */
                if (estimate_sigma) {
                    sigma = find_regularization_sigma(measured, ideal, tparams);
                    gwy_params_set_double(tparams, PARAM_SIGMA, log(sigma)/G_LN10);
                }
                calculate_tf(measured, wideal, psf, tparams);
                width = gwy_data_field_get_xres(psf);
                height = gwy_data_field_get_yres(psf);
                buf = gwy_data_field_extend(psf,
                                            (txres - width)/2, (tyres - height)/2,
                                            (txres - width)/2, (tyres - height)/2,
                                            GWY_EXTERIOR_FIXED_VALUE, 0.0, FALSE);
                gwy_data_field_assign(psf, buf);
                g_object_unref(buf);
            }
            else if (estimate_sigma) {
                /* find_regularization_sigma() does its own windowing */
                sigma = find_regularization_sigma(measured, ideal, tparams);
                gwy_params_set_double(tparams, PARAM_SIGMA, log(sigma)/G_LN10);
                calculate_tf(measured, wideal, psf, tparams);
            }
            else
                calculate_tf(measured, wideal, psf, tparams);

            if (result) {
                gwy_brick_set_xy_plane(result, psf, k);
                if (!k) {
                    gwy_si_unit_assign(gwy_brick_get_si_unit_w(result), gwy_data_field_get_si_unit_z(psf));
                    gwy_brick_set_xoffset(result, gwy_data_field_get_xoffset(psf));
                    gwy_brick_set_yoffset(result, gwy_data_field_get_yoffset(psf));
                }
            }

            /* PSF_OUTPUT_TF_WIDTH */
            if (plots[0])
                gwy_data_line_set_val(plots[0], k, measure_tf_width(psf));
            /* Calculate this first because we may need to adjust psf to non-integral for height and norm. */
            /* PSF_OUTPUT_DIFF_NORM */
            if (plots[3]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                if (!convolved)
                    convolved = gwy_data_field_new_alike(ideal, FALSE);
                gwy_data_field_copy(ideal, convolved, TRUE);
                gwy_data_field_add(convolved, -gwy_data_field_get_avg(convolved));
                field_convolve_default(convolved, psf);
                gwy_data_field_subtract_fields(convolved, measured, convolved);
                gwy_data_field_add(convolved, gwy_data_field_get_avg(measured));
                gwy_data_line_set_val(plots[3], k, calculate_l2_norm(convolved, as_integral, unit));
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[3]), unit);
                    gwy_object_unref(unit);
                }
            }
            if ((plots[1] || plots[2]) && !as_integral)
                adjust_tf_field_to_non_integral(psf);
            /* PSF_OUTPUT_TF_HEIGHT */
            if (plots[1]) {
                if (!k)
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[1]), gwy_data_field_get_si_unit_z(psf));
                gwy_data_field_get_min_max(psf, &min, &max);
                gwy_data_line_set_val(plots[1], k, fmax(fabs(min), fabs(max)));
            }
            /* PSF_OUTPUT_TF_NORM */
            if (plots[2]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                gwy_data_line_set_val(plots[2], k, calculate_l2_norm(psf, as_integral, unit));
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[2]), unit);
                    gwy_object_unref(unit);
                }
            }
            /* PSF_OUTPUT_SIGMA */
            if (plots[4]) {
                if (!k)
                    unit = gwy_si_unit_new(NULL);
                gwy_data_line_set_val(plots[4], k, sigma);
                if (unit) {
                    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[4]), unit);
                    gwy_object_unref(unit);
                }
            }

            if (gwy_omp_set_fraction_check_cancel(gwy_app_wait_set_fraction, k, kfrom, kto, pcancelled))
                break;
        }

        g_object_unref(measured);
        g_object_unref(psf);
        g_object_unref(tparams);
        gwy_object_unref(convolved);
        gwy_object_unref(wmeas);
    }

    if (cancelled)
        goto fail;

    if (plots[0])
        gwy_si_unit_assign(gwy_data_line_get_si_unit_y(plots[0]), gwy_brick_get_si_unit_x(brick));

    if (result) {
        if (gwy_params_get_boolean(args->params, PARAM_AS_INTEGRAL))
            adjust_tf_brick_to_non_integral(result);

        newid = gwy_app_data_browser_add_brick(result, NULL, data, TRUE);
        gwy_app_set_brick_title(data, newid, _("Volume TF"));
        gwy_app_volume_log_add_volume(data, id, newid);
        gwy_app_sync_volume_items(data, data, id, newid, GWY_DATA_ITEM_GRADIENT, 0);
    }

    zcal = gwy_brick_get_zcalibration(brick);
    for (i = 0; i < NGRAPH_OUTPUTS; i++) {
        if (!plots[i])
            continue;

        gmodel = gwy_graph_model_new();
        gwy_graph_model_set_units_from_data_line(gmodel, plots[i]);
        name = gwy_enum_to_string(graph_outputs[i], output_types, G_N_ELEMENTS(output_types));
        g_object_set(gmodel,
                     "title", _(name),
                     "axis-label-left", _(name),
                     "axis-label-bottom", _("z level"),
                     NULL);

        gcmodel = gwy_graph_curve_model_new();
        if (zcal) {
            gwy_graph_curve_model_set_data(gcmodel,
                                           gwy_data_line_get_data(zcal), gwy_data_line_get_data(plots[i]), zres);
            g_object_set(gmodel, "si-unit-x", gwy_data_line_get_si_unit_y(zcal), NULL);
        }
        else
            gwy_graph_curve_model_set_data_from_dataline(gcmodel, plots[i], -1, -1);
        g_object_set(gcmodel,
                     "description", _(name),
                     "mode", GWY_GRAPH_CURVE_LINE,
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
        gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);
        g_object_unref(gmodel);
    }

fail:
    gwy_app_wait_finish();
    g_object_unref(wideal);
    for (i = 0; i < NGRAPH_OUTPUTS; i++)
        gwy_object_unref(plots[i]);
    gwy_object_unref(result);
}

static void
prepare_field(GwyDataField *field, GwyDataField *wfield,
              GwyWindowingType window)
{
    /* Prepare field in place if requested. */
    if (wfield != field) {
        gwy_data_field_resample(wfield, gwy_data_field_get_xres(field), gwy_data_field_get_yres(field),
                                GWY_INTERPOLATION_NONE);
        gwy_data_field_copy(field, wfield, TRUE);
    }
    gwy_data_field_add(wfield, -gwy_data_field_get_avg(wfield));
    gwy_data_field_fft_window(wfield, window);
}

static void
calculate_tf(GwyDataField *measured, GwyDataField *wideal,
             GwyDataField *psf, GwyParams *params)
{
    GwyDataField *wmeas;
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    gdouble r, sigma = pow10(gwy_params_get_double(params, PARAM_SIGMA));
    gint txres = gwy_params_get_int(params, PARAM_TXRES);
    gint tyres = gwy_params_get_int(params, PARAM_TYRES);
    gint border = gwy_params_get_int(params, PARAM_BORDER);
    gint xres, yres, xborder, yborder;

    wmeas = gwy_data_field_new_alike(measured, FALSE);
    prepare_field(measured, wmeas, windowing);
    if (method == PSF_METHOD_REGULARISED)
        gwy_data_field_deconvolve_regularized(wmeas, wideal, psf, sigma);
    else if (method == PSF_METHOD_PSEUDO_WIENER)
        psf_deconvolve_wiener(wmeas, wideal, psf, sigma);
    else {
        gwy_data_field_resample(psf, txres, tyres, GWY_INTERPOLATION_NONE);
        gwy_data_field_deconvolve_psf_leastsq(wmeas, wideal, psf, sigma, border);
    }
    g_object_unref(wmeas);

    if (!method_is_full_sized(method))
        return;

    xres = gwy_data_field_get_xres(psf);
    yres = gwy_data_field_get_yres(psf);
    xborder = (xres - txres + 1)/2;
    yborder = (yres - tyres + 1)/2;
    if (!xborder && !yborder)
        return;

    gwy_data_field_resize(psf, xborder, yborder, xborder + txres, yborder + tyres);
    r = (txres + 1 - txres % 2)/2.0;
    gwy_data_field_set_xoffset(psf, -gwy_data_field_jtor(psf, r));
    r = (tyres + 1 - tyres % 2)/2.0;
    gwy_data_field_set_yoffset(psf, -gwy_data_field_itor(psf, r));
}

static void
adjust_tf_field_to_non_integral(GwyDataField *psf)
{
    GwySIUnit *xyunit, *zunit;
    gdouble hxhy;

    xyunit = gwy_data_field_get_si_unit_xy(psf);
    zunit = gwy_data_field_get_si_unit_z(psf);
    gwy_si_unit_power_multiply(zunit, 1, xyunit, 2, zunit);

    hxhy = gwy_data_field_get_dx(psf) * gwy_data_field_get_dy(psf);
    gwy_data_field_multiply(psf, hxhy);
    gwy_data_field_data_changed(psf);
}

static void
adjust_tf_brick_to_non_integral(GwyBrick *psf)
{
    GwySIUnit *xunit, *yunit, *wunit;
    gdouble hxhy;

    xunit = gwy_brick_get_si_unit_x(psf);
    yunit = gwy_brick_get_si_unit_y(psf);
    wunit = gwy_brick_get_si_unit_w(psf);
    gwy_si_unit_multiply(wunit, xunit, wunit);
    gwy_si_unit_multiply(wunit, yunit, wunit);

    hxhy = gwy_brick_get_dx(psf) * gwy_brick_get_dy(psf);
    gwy_brick_multiply(psf, hxhy);
    gwy_brick_data_changed(psf);
}

static gdouble
measure_tf_width(GwyDataField *psf)
{
    GwyDataField *mask, *abspsf;
    gint xres, yres;
    gdouble s2;

    xres = gwy_data_field_get_xres(psf);
    yres = gwy_data_field_get_yres(psf);
    mask = gwy_data_field_duplicate(psf);
    gwy_data_field_threshold(mask, 0.15*gwy_data_field_get_max(mask), 0.0, 1.0);
    if (gwy_data_field_get_val(mask, xres/2, yres/2) == 0.0) {
        g_object_unref(mask);
        return 0.0;
    }

    gwy_data_field_grains_extract_grain(mask, xres/2, yres/2);
    gwy_data_field_grains_grow(mask, 0.5*log(xres*yres), GWY_DISTANCE_TRANSFORM_EUCLIDEAN, FALSE);
    abspsf = gwy_data_field_duplicate(psf);
    gwy_data_field_abs(abspsf);
    s2 = gwy_data_field_area_get_dispersion(abspsf, mask, GWY_MASK_INCLUDE, 0, 0, xres, yres, NULL, NULL);
    g_object_unref(mask);
    g_object_unref(abspsf);

    return sqrt(s2);
}

static gboolean
method_is_full_sized(PSFMethodType method)
{
    return (method == PSF_METHOD_REGULARISED || method == PSF_METHOD_PSEUDO_WIENER);
}

static void
estimate_tf_region(GwyDataField *wmeas, GwyDataField *wideal,
                   GwyDataField *psf,  /* scratch buffer */
                   gint *col, gint *row, gint *width, gint *height)
{
    gint xres, yres, i, j, imin, jmin, imax, jmax, ext;
    const gdouble *d;
    gdouble m;

    xres = gwy_data_field_get_xres(wmeas);
    yres = gwy_data_field_get_yres(wmeas);
    *col = xres/3;
    *row = yres/3;
    *width = xres - 2*(*col);
    *height = yres - 2*(*row);
    /* Use a fairly large but not yet insane sigma value 4.0 to estimate the width.  We want to err on the side of
     * size overestimation here. XXX: We might want to use a proportional to 1/sqrt(xres*yres) here. */
    gwy_data_field_deconvolve_regularized(wmeas, wideal, psf, 4.0);
    d = gwy_data_field_get_data_const(psf);

    /* FIXME: From here it the same as to libprocess/filter.c psf_sigmaopt_estimate_size(). */
    imax = yres/2;
    jmax = xres/2;
    m = 0.0;
    for (i = *row; i < *row + *height; i++) {
        for (j = *col; j < *col + *width; j++) {
            if (d[i*xres + j] > m) {
                m = d[i*xres + j];
                imax = i;
                jmax = j;
            }
        }
    }
    gwy_debug("maximum %g at (%d,%d)", m, imax, jmax);
    gwy_data_field_threshold(psf, 0.05*m, 0.0, 1.0);
    g_return_if_fail(d[imax*xres + jmax] > 0.0);
    gwy_data_field_grains_extract_grain(psf, jmax, imax);

    imin = imax;
    jmin = jmax;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            if (d[i*xres + j] > 0.0) {
                if (i < imin)
                    imin = i;
                if (i > imax)
                    imax = i;
                if (j < jmin)
                    jmin = j;
                if (j > jmax)
                    jmax = j;
            }
        }
    }

    ext = GWY_ROUND(0.5*log(xres*yres)) + 1;
    *col = jmin - ext;
    *row = imin - ext;
    *width = jmax+1 - jmin + 2*ext;
    *height = imax+1 - imin + 2*ext;
    if (*col < 0) {
        *width += *col;
        *col = 0;
    }
    if (*row < 0) {
        *height += *row;
        *row = 0;
    }
    if (*col + *width > xres)
        *width = xres - *col;
    if (*row + *height > yres)
        *height = yres - *row;

    gwy_debug("estimated region: %dx%d centered at (%d,%d)", *width, *height, *col + *width/2, *row + *height/2);

    /* Use some default reasonable size when things get out of hand... */
    *width = MIN(*width, xres/4);
    *height = MIN(*height, yres/4);
}

static void
symmetrise_tf_region(gint pos, gint len, gint res, gint *tres)
{
    gint epos = pos + len-1;
    len = MAX(epos, res-1 - pos) - MIN(pos, res-1 - epos) + 1;
    *tres = len | 1;
}

typedef struct {
    GwyParams *params;
    GwyDataField *psf;         /* Real-space PSF */
    GwyDataField *wideal;      /* Windowed ideal. */
    GwyDataField *wmeas;       /* Windowed measured. */
    gint col, row;
    gint width, height;
} PSFSigmaOptData;

static void
psf_sigmaopt_prepare(GwyDataField *measured, GwyDataField *ideal,
                     GwyParams *params,
                     PSFSigmaOptData *sodata)
{
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);

    gwy_clear(sodata, 1);
    sodata->params = params;
    sodata->wideal = gwy_data_field_new_alike(ideal, FALSE);
    sodata->wmeas = gwy_data_field_new_alike(measured, FALSE);
    prepare_field(measured, sodata->wmeas, windowing);
    prepare_field(ideal, sodata->wideal, windowing);
    if (method == PSF_METHOD_PSEUDO_WIENER) {
        sodata->psf = gwy_data_field_new_alike(measured, FALSE);
        estimate_tf_region(sodata->wmeas, sodata->wideal, sodata->psf, &sodata->col, &sodata->row,
                           &sodata->width, &sodata->height);
    }
}

static void
psf_sigmaopt_free(PSFSigmaOptData *sodata)
{
    GWY_OBJECT_UNREF(sodata->psf);
    g_object_unref(sodata->wmeas);
    g_object_unref(sodata->wideal);
}

static gdouble
psf_sigmaopt_evaluate(gdouble logsigma, gpointer user_data)
{
    PSFSigmaOptData *sodata = (PSFSigmaOptData*)user_data;
    GwyParams *params = sodata->params;
    GwyDataField *psf = sodata->psf;
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    gdouble sigma, w;

    g_assert(method == PSF_METHOD_PSEUDO_WIENER);
    sigma = exp(logsigma);
    psf_deconvolve_wiener(sodata->wmeas, sodata->wideal, psf, sigma);
    gwy_data_field_area_abs(psf, sodata->col, sodata->row, sodata->width, sodata->height);
    w = gwy_data_field_area_get_dispersion(psf, NULL, GWY_MASK_IGNORE,
                                           sodata->col, sodata->row, sodata->width, sodata->height, NULL, NULL);
    return sqrt(w);
}

static gdouble
find_regularization_sigma(GwyDataField *field,
                          GwyDataField *ideal,
                          GwyParams *params)
{
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    gint txres = gwy_params_get_int(params, PARAM_TXRES);
    gint tyres = gwy_params_get_int(params, PARAM_TYRES);
    gint border = gwy_params_get_int(params, PARAM_BORDER);
    PSFSigmaOptData sodata;
    gdouble logsigma, sigma;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(field), 0.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(ideal), 0.0);
    g_return_val_if_fail(!gwy_data_field_check_compatibility(field, ideal,
                                                             GWY_DATA_COMPATIBILITY_RES
                                                             | GWY_DATA_COMPATIBILITY_REAL
                                                             | GWY_DATA_COMPATIBILITY_LATERAL),
                         0.0);

    psf_sigmaopt_prepare(field, ideal, params, &sodata);
    if (method == PSF_METHOD_REGULARISED)
        sigma = gwy_data_field_find_regularization_sigma_for_psf(sodata.wmeas, sodata.wideal);
    else if (method == PSF_METHOD_LEAST_SQUARES)
        sigma = gwy_data_field_find_regularization_sigma_leastsq(sodata.wmeas, sodata.wideal, txres, tyres, border);
    else {
        logsigma = gwy_math_find_minimum_1d(psf_sigmaopt_evaluate, log(1e-8), log(1e3), &sodata);
        /* Experimentally determined fudge factor from large-scale simulations. */
        sigma = 0.375*exp(logsigma);
    }
    psf_sigmaopt_free(&sodata);

    return sigma;
}

static void
set_transfer_function_units(GwyDataField *ideal, GwyDataField *measured, GwyDataField *transferfunc)
{
    GwySIUnit *sunit, *iunit, *tunit, *xyunit;

    xyunit = gwy_data_field_get_si_unit_xy(measured);
    sunit = gwy_data_field_get_si_unit_z(ideal);
    iunit = gwy_data_field_get_si_unit_z(measured);
    tunit = gwy_data_field_get_si_unit_z(transferfunc);
    gwy_si_unit_divide(iunit, sunit, tunit);
    gwy_si_unit_power_multiply(tunit, 1, xyunit, -2, tunit);
}

/* This is an exact replica of gwy_data_field_deconvolve_regularized(). The only difference is that instead of σ² the
 * regularisation term is σ²/|P|², corresponding to pseudo-Wiener filter with the assumption of uncorrelated point
 * noise. */
static void
psf_deconvolve_wiener(GwyDataField *field,
                      GwyDataField *operand,
                      GwyDataField *out,
                      gdouble sigma)
{
    gint xres, yres, i, cstride;
    gdouble lambda, q, r, frms, orms;
    fftw_complex *ffield, *foper;
    fftw_plan fplan, bplan;

    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    g_return_if_fail(GWY_IS_DATA_FIELD(operand));
    g_return_if_fail(GWY_IS_DATA_FIELD(out));

    xres = field->xres;
    yres = field->yres;
    cstride = xres/2 + 1;
    g_return_if_fail(operand->xres == xres);
    g_return_if_fail(operand->yres == yres);
    gwy_data_field_resample(out, xres, yres, GWY_INTERPOLATION_NONE);

    orms = gwy_data_field_get_rms(operand);
    frms = gwy_data_field_get_rms(field);
    if (!orms) {
        g_warning("Deconvolution by zero.");
        gwy_data_field_clear(out);
        return;
    }
    if (!frms) {
        gwy_data_field_clear(out);
        return;
    }

    ffield = gwy_fftw_new_complex(cstride*yres);
    foper = gwy_fftw_new_complex(cstride*yres);
#if defined(_OPENMP) && defined(HAVE_FFTW_WITH_OPENMP)
    fftw_plan_with_nthreads(1);
#endif
    fplan = fftw_plan_dft_r2c_2d(yres, xres, out->data, ffield, FFTW_DESTROY_INPUT);
    bplan = fftw_plan_dft_c2r_2d(yres, xres, ffield, out->data, FFTW_DESTROY_INPUT);

    gwy_data_field_copy(operand, out, FALSE);
    fftw_execute(fplan);
    gwy_assign(foper, ffield, cstride*yres);

    gwy_data_field_copy(field, out, FALSE);
    fftw_execute(fplan);
    fftw_destroy_plan(fplan);

    /* This seems wrong, but we just compensate the FFT. */
    orms *= sqrt(xres*yres);
    /* XXX: Is this correct now? */
    frms *= sqrt(xres*yres);
    lambda = sigma*sigma * orms*orms * frms*frms;
    /* NB: We normalize it as an integral.  So one recovers the convolution with TRUE in ext-convolve! */
    q = 1.0/(field->xreal * field->yreal);
    for (i = 1; i < cstride*yres; i++) {
        gdouble fre = gwycreal(ffield[i]), fim = gwycimag(ffield[i]);
        gdouble ore = gwycreal(foper[i]), oim = gwycimag(foper[i]);
        gdouble inorm = ore*ore + oim*oim;
        gdouble fnorm = fre*fre + fim*fim;
        gdouble f = fnorm/(inorm*fnorm + lambda);
        gwycreal(ffield[i]) = (fre*ore + fim*oim)*f;
        gwycimag(ffield[i]) = (-fre*oim + fim*ore)*f;
    }
    fftw_free(foper);
    gwycreal(ffield[0]) = gwycimag(ffield[0]) = 0.0;
    fftw_execute(bplan);
    fftw_destroy_plan(bplan);
    fftw_free(ffield);

    gwy_data_field_multiply(out, q);
    gwy_data_field_2dfft_humanize(out);

    out->xreal = field->xreal;
    out->yreal = field->yreal;

    r = (xres + 1 - xres % 2)/2.0;
    gwy_data_field_set_xoffset(out, -gwy_data_field_jtor(out, r));

    r = (xres + 1 - xres % 2)/2.0;
    gwy_data_field_set_yoffset(out, -gwy_data_field_itor(out, r));

    gwy_data_field_invalidate(out);
    set_transfer_function_units(operand, field, out);
}

static inline void
clamp_int_param(GwyParams *params, gint id, gint min, gint max, gint default_value)
{
    gint p = gwy_params_get_int(params, id);

    if (p < min || p > max)
        gwy_params_set_int(params, id, default_value);
}

static gboolean
clamp_psf_size(GwyBrick *brick, ModuleArgs *args)
{
    GwyParams *params = args->params;
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);

    if (MIN(xres, yres) < 24)
        return FALSE;

    if (method_is_full_sized(method)) {
        clamp_int_param(params, PARAM_TXRES, 3, xres, MIN(xres, 41));
        clamp_int_param(params, PARAM_TYRES, 3, yres, MIN(yres, 41));
    }
    else {
        clamp_int_param(params, PARAM_TXRES, 3, xres/3 | 1, MIN(xres/3 | 1, 41));
        clamp_int_param(params, PARAM_TYRES, 3, yres/3 | 1, MIN(yres/3 | 1, 41));
    }
    clamp_int_param(params, PARAM_BORDER, 0, MIN(xres, yres)/8, MIN(MIN(xres, yres)/8, 2));
    return TRUE;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick;
    gint zres = gwy_brick_get_zres(brick);

    clamp_int_param(params, PARAM_ZLEVEL, 0, zres-1, 0);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
