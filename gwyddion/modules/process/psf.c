/*
 *  $Id: psf.c 25573 2023-07-27 09:01:09Z yeti-dn $
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
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/arithmetic.h>
#include <libprocess/grains.h>
#include <libprocess/inttrans.h>
#include <libprocess/filters.h>
#include <libprocess/stats.h>
#include <libprocess/simplefft.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

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

typedef enum {
    PSF_METHOD_REGULARISED   = 0,
    PSF_METHOD_LEAST_SQUARES = 1,
    PSF_METHOD_PSEUDO_WIENER = 2,
} PSFMethodType;

typedef enum {
    PSF_DISPLAY_DATA       = 0,
    PSF_DISPLAY_PSF        = 1,
    PSF_DISPLAY_CONVOLVED  = 2,
    PSF_DISPLAY_DIFFERENCE = 3,
} PSFDisplayType;

typedef enum {
    PSF_OUTPUT_PSF       = 0,
    PSF_OUTPUT_CONVOLVED = 1,
    PSF_OUTPUT_DIFFERECE = 2,
} PSFOutputType;

enum {
    PARAM_IDEAL,
    PARAM_BORDER,
    PARAM_DISPLAY,
    PARAM_DIFF_COLOURMAP,
    PARAM_METHOD,
    PARAM_SIGMA,
    PARAM_TXRES,
    PARAM_TYRES,
    PARAM_WINDOWING,
    PARAM_AS_INTEGRAL,

    PARAM_OUTPUT_TYPE,

    BUTTON_FULL_SIZE,
    BUTTON_ESTIMATE_SIZE,
    WIDGET_RESULTS,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *psf;
    GwyDataField *convolved;
    GwyDataField *difference;
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
} ModuleGUI;

static gboolean         module_register            (void);
static GwyParamDef*     define_module_params       (void);
static void             psf                        (GwyContainer *data,
                                                    GwyRunType runtype);
static void             execute                    (ModuleArgs *args);
static GwyDialogOutcome run_gui                    (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static GwyResults*      create_results             (ModuleArgs *args,
                                                    GwyContainer *data,
                                                    gint id);
static void             param_changed              (ModuleGUI *gui,
                                                    gint id);
static void             dialog_response            (ModuleGUI *gui,
                                                    gint response);
static void             preview                    (gpointer user_data);
static void             switch_display             (ModuleGUI *gui);
static gboolean         ideal_image_filter         (GwyContainer *data,
                                                    gint id,
                                                    gpointer user_data);
static void             prepare_field              (GwyDataField *field,
                                                    GwyDataField *wfield,
                                                    GwyWindowingType window);
static gboolean         method_is_full_sized       (PSFMethodType method);
static void             estimate_tf_region         (GwyDataField *wmeas,
                                                    GwyDataField *wideal,
                                                    GwyDataField *psf,
                                                    gint *col,
                                                    gint *row,
                                                    gint *width,
                                                    gint *height);
static void             symmetrise_tf_region       (gint pos,
                                                    gint len,
                                                    gint res,
                                                    gint *tres);
static void             psf_deconvolve_wiener      (GwyDataField *field,
                                                    GwyDataField *ideal,
                                                    GwyDataField *out,
                                                    gdouble sigma);
static void             adjust_tf_to_non_integral  (GwyDataField *psf);
static void             set_transfer_function_units(GwyDataField *ideal,
                                                    GwyDataField *measured,
                                                    GwyDataField *transferfunc);
static gdouble          measure_tf_width           (GwyDataField *psf);
static gdouble          find_regularization_sigma  (ModuleArgs *args);
static gint             create_output_field        (GwyDataField *field,
                                                    GwyContainer *data,
                                                    gint id,
                                                    const gchar *name);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Transfer function estimation"),
    "Petr Klapetek <klapetek@gwyddion.net>, Yeti <yeti@gwyddion.net>",
    "4.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, psf)

static gboolean
module_register(void)
{
    gwy_process_func_register("psf",
                              (GwyProcessFunc)&psf,
                              N_("/_Statistics/_Transfer Function Guess..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Estimate transfer function from known data and ideal image"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum outputs[] = {
        { N_("Transfer function"), (1 << PSF_OUTPUT_PSF),       },
        { N_("Convolved"),         (1 << PSF_OUTPUT_CONVOLVED), },
        { N_("Difference"),        (1 << PSF_OUTPUT_DIFFERECE), },
    };
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
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_IDEAL, "ideal", _("_Ideal response"));
    gwy_param_def_add_int(paramdef, PARAM_BORDER, "border", _("_Border"), 0, 16384, 3);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|_Display"),
                              displays, G_N_ELEMENTS(displays), PSF_DISPLAY_PSF);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFF_COLOURMAP, "diff_colourmap",
                              _("Show differences with _adapted color map"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_METHOD, "method", _("_Method"),
                              methods, G_N_ELEMENTS(methods), PSF_METHOD_REGULARISED);
    gwy_param_def_add_double(paramdef, PARAM_SIGMA, "sigma", _("_Sigma"), -8.0, 3.0, 1.0);
    gwy_param_def_add_int(paramdef, PARAM_TXRES, "txres", _("_Horizontal size"), 3, G_MAXINT, 51);
    gwy_param_def_add_int(paramdef, PARAM_TYRES, "tyres", _("_Vertical size"), 3, G_MAXINT, 51);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOWING, "windowing", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_WELCH);
    gwy_param_def_add_boolean(paramdef, PARAM_AS_INTEGRAL, "as_integral", "Normalize as _integral", TRUE);
    gwy_param_def_add_gwyflags(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output"),
                               outputs, G_N_ELEMENTS(outputs), (1 << PSF_OUTPUT_PSF));
    return paramdef;
}

static void
psf(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    GwyDataField *field;
    ModuleArgs args;
    guint output;
    gint xres, yres, id;

    g_return_if_fail(runtype & RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    args.field = field;
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    if (MIN(xres, yres) < 24) {
        if (runtype == GWY_RUN_INTERACTIVE) {
            GtkWidget *dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, id),
                                                       GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                                                       GTK_BUTTONS_OK,
                                                       _("Image is too small."));
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        return;
    }

    args.params = gwy_params_new_from_settings(define_module_params());
    args.psf = gwy_data_field_new_alike(field, TRUE);
    args.convolved = gwy_data_field_new_alike(field, TRUE);
    args.difference = gwy_data_field_new_alike(field, TRUE);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    output = gwy_params_get_flags(args.params, PARAM_OUTPUT_TYPE);
    if (!output || !gwy_params_get_image(args.params, PARAM_IDEAL))
        goto end;

    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    if (output & (1 << PSF_OUTPUT_PSF))
        create_output_field(args.psf, data, id, _("Transfer function"));
    if (output & (1 << PSF_OUTPUT_CONVOLVED))
        create_output_field(args.convolved, data, id, _("Convolved"));
    if (output & (1 << PSF_OUTPUT_DIFFERECE))
        create_output_field(args.difference, data, id, _("Difference"));

end:
    g_object_unref(args.difference);
    g_object_unref(args.convolved);
    g_object_unref(args.psf);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *notebook;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    gint xres, yres;
    GwyDialogOutcome outcome;

    xres = gwy_data_field_get_xres(args->field);
    yres = gwy_data_field_get_yres(args->field);

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.results = create_results(args, data, id);
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(1), FIT_GRADIENT_NAME);

    gui.diff_gradient = gwy_inventory_new_item(gwy_gradients(), GWY_GRADIENT_DEFAULT, FIT_GRADIENT_NAME);
    gwy_resource_use(GWY_RESOURCE(gui.diff_gradient));

    gui.dialog = gwy_dialog_new(_("Estimate Transfer Function"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Fit Sigma"), RESPONSE_REFINE);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(gui.dataview), FALSE);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, TRUE, TRUE, 0);

    table = gui.table_param = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_IDEAL);
    gwy_param_table_data_id_set_filter(table, PARAM_IDEAL, ideal_image_filter, args->field, NULL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_combo(table, PARAM_METHOD);
    gwy_param_table_append_slider(table, PARAM_SIGMA);
    gwy_param_table_set_unitstr(table, PARAM_SIGMA, "log<sub>10</sub>");
    gwy_param_table_append_combo(table, PARAM_WINDOWING);

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

    if (id < 0 || id == PARAM_DISPLAY || id == PARAM_DIFF_COLOURMAP)
        switch_display(gui);

    if (id < 0 || id == PARAM_METHOD || id == PARAM_OUTPUT_TYPE) {
        gboolean have_ideal = !gwy_params_data_id_is_none(params, PARAM_IDEAL);
        guint output = gwy_params_get_flags(params, PARAM_OUTPUT_TYPE);

        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, output && have_ideal);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_REFINE, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_FULL_SIZE, have_ideal && full_sized);
        gwy_param_table_set_sensitive(gui->table_param, BUTTON_ESTIMATE_SIZE, have_ideal);
        gwy_param_table_set_sensitive(gui->table_param, PARAM_BORDER, !full_sized);
        gwy_param_table_set_sensitive(gui->table_output, PARAM_AS_INTEGRAL, output & (1 << PSF_OUTPUT_PSF));
    }

    if (id < 0 || id == PARAM_METHOD) {
        gint xres = gwy_data_field_get_xres(args->field);
        gint yres = gwy_data_field_get_yres(args->field);
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

    if (id != PARAM_DISPLAY && id != PARAM_OUTPUT_TYPE)
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

        wmeas = gwy_data_field_new_alike(args->field, FALSE);
        wideal = gwy_data_field_new_alike(ideal, FALSE);
        prepare_field(args->field, wmeas, windowing);
        prepare_field(ideal, wideal, windowing);

        psf = gwy_data_field_new_alike(args->field, TRUE);
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
        gwy_param_table_set_int(table, PARAM_TXRES, gwy_data_field_get_xres(args->field));
        gwy_param_table_set_int(table, PARAM_TYRES, gwy_data_field_get_yres(args->field));
    }
    else if (response == RESPONSE_REFINE)
        gwy_param_table_set_double(table, PARAM_SIGMA, log(find_regularization_sigma(gui->args))/G_LN10);
}

static gint
create_output_field(GwyDataField *field,
                    GwyContainer *data,
                    gint id,
                    const gchar *name)
{
    gint newid;

    newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            0);
    gwy_app_set_data_field_title(data, newid, name);
    gwy_app_channel_log_add_proc(data, id, newid);

    return newid;
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
    GwyDataField *psf = args->psf, *convolved = args->convolved;
    gboolean as_integral = gwy_params_get_boolean(args->params, PARAM_AS_INTEGRAL);
    gdouble min, max, l2norm, resid;
    GwyResults *results = gui->results;
    GwySIUnit *unit;

    execute(args);
    switch_display(gui);

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
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
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
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->field);
    else if (display == PSF_DISPLAY_PSF)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->psf);
    else if (display == PSF_DISPLAY_CONVOLVED)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->convolved);
    else if (display == PSF_DISPLAY_DIFFERENCE) {
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->difference);
        if (diff_colourmap) {
            gdouble min, max, dispmin, dispmax;

            i = 1;
            range_type = GWY_LAYER_BASIC_RANGE_FIXED;
            gwy_data_field_get_min_max(args->difference, &min, &max);
            gwy_data_field_get_autorange(args->difference, &dispmin, &dispmax);
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
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *ideal = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    if (ideal == field)
        return FALSE;

    return !gwy_data_field_check_compatibility(ideal, field,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *measured = args->field, *psf = args->psf, *wmeas, *wideal;
    GwyDataField *convolved = args->convolved, *difference = args->difference;
    GwyDataField *ideal = gwy_params_get_image(params, PARAM_IDEAL);
    gdouble r, sigma = pow10(gwy_params_get_double(params, PARAM_SIGMA));
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    gint txres = gwy_params_get_int(params, PARAM_TXRES);
    gint tyres = gwy_params_get_int(params, PARAM_TYRES);
    gint border = gwy_params_get_int(params, PARAM_BORDER);
    gint xres, yres, xborder, yborder;

    if (!ideal) {
        gwy_data_field_clear(psf);
        gwy_data_field_clear(convolved);
        gwy_data_field_clear(difference);
        return;
    }

    wmeas = gwy_data_field_new_alike(measured, FALSE);
    wideal = gwy_data_field_new_alike(ideal, FALSE);
    prepare_field(measured, wmeas, windowing);
    prepare_field(ideal, wideal, windowing);
    if (method == PSF_METHOD_REGULARISED)
        gwy_data_field_deconvolve_regularized(wmeas, wideal, psf, sigma);
    else if (method == PSF_METHOD_PSEUDO_WIENER)
        psf_deconvolve_wiener(wmeas, wideal, psf, sigma);
    else {
        gwy_data_field_resample(psf, txres, tyres, GWY_INTERPOLATION_NONE);
        gwy_data_field_deconvolve_psf_leastsq(wmeas, wideal, psf, sigma, border);
    }
    g_object_unref(wideal);
    g_object_unref(wmeas);

    if (method_is_full_sized(method)) {
        xres = gwy_data_field_get_xres(psf);
        yres = gwy_data_field_get_yres(psf);
        xborder = (xres - txres + 1)/2;
        yborder = (yres - tyres + 1)/2;
        if (xborder || yborder) {
            gwy_data_field_resize(psf, xborder, yborder, xborder + txres, yborder + tyres);
            r = (txres + 1 - txres % 2)/2.0;
            gwy_data_field_set_xoffset(psf, -gwy_data_field_jtor(psf, r));
            r = (tyres + 1 - tyres % 2)/2.0;
            gwy_data_field_set_yoffset(psf, -gwy_data_field_itor(psf, r));
        }
    }

    gwy_data_field_assign(convolved, ideal);
    gwy_data_field_add(convolved, -gwy_data_field_get_avg(convolved));
    field_convolve_default(convolved, psf);
    gwy_data_field_add(convolved, gwy_data_field_get_avg(measured));
    gwy_data_field_subtract_fields(difference, measured, convolved);

    /* Change the normalisation to the discrete (i.e. wrong) one after all calculations are done. */
    if (!gwy_params_get_boolean(params, PARAM_AS_INTEGRAL))
        adjust_tf_to_non_integral(psf);
}

static void
adjust_tf_to_non_integral(GwyDataField *psf)
{
    GwySIUnit *xyunit, *zunit;

    xyunit = gwy_data_field_get_si_unit_xy(psf);
    zunit = gwy_data_field_get_si_unit_z(psf);
    gwy_si_unit_power_multiply(zunit, 1, xyunit, 2, zunit);
    gwy_data_field_multiply(psf, gwy_data_field_get_dx(psf) * gwy_data_field_get_dy(psf));
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

static void
prepare_field(GwyDataField *field, GwyDataField *wfield, GwyWindowingType window)
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

static gboolean
method_is_full_sized(PSFMethodType method)
{
    return method == PSF_METHOD_REGULARISED || method == PSF_METHOD_PSEUDO_WIENER;
}

static void
estimate_tf_region(GwyDataField *wmeas, GwyDataField *wideal, GwyDataField *psf, /* scratch buffer */
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
     * size overestimation here.
     * XXX: We might want to use a proportional to 1/sqrt(xres*yres) here. */
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
    gwy_debug("maximum at (%d,%d)", imax, jmax);
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

    gwy_debug("estimated region: %dx%d centered at (%d,%d)",
              *width, *height, *col + *width/2, *row + *height/2);

    /* Use some default reasonable size when things get out of hand... */
    *width = MIN(*width, xres/6);
    *height = MIN(*height, yres/6);
}

static void
symmetrise_tf_region(gint pos, gint len, gint res, gint *tres)
{
    gint epos = pos + len-1;
    len = MAX(epos, res-1 - pos) - MIN(pos, res-1 - epos) + 1;
    *tres = len | 1;
}

typedef struct {
    ModuleArgs *args;
    GwyDataField *psf;         /* Real-space PSF */
    GwyDataField *wideal;      /* Windowed ideal. */
    GwyDataField *wmeas;       /* Windowed measured. */
    gint col, row;
    gint width, height;
} PSFSigmaOptData;

static void
psf_sigmaopt_prepare(ModuleArgs *args, PSFSigmaOptData *sodata)
{
    GwyParams *params = args->params;
    GwyWindowingType windowing = gwy_params_get_enum(params, PARAM_WINDOWING);
    PSFMethodType method = gwy_params_get_enum(params, PARAM_METHOD);
    GwyDataField *ideal = gwy_params_get_image(params, PARAM_IDEAL);

    gwy_clear(sodata, 1);
    sodata->args = args;
    sodata->wideal = gwy_data_field_new_alike(ideal, FALSE);
    sodata->wmeas = gwy_data_field_new_alike(args->field, FALSE);
    prepare_field(args->field, sodata->wmeas, windowing);
    prepare_field(ideal, sodata->wideal, windowing);
    if (method == PSF_METHOD_PSEUDO_WIENER) {
        sodata->psf = gwy_data_field_new_alike(args->field, FALSE);
        estimate_tf_region(sodata->wmeas, sodata->wideal, sodata->psf,
                           &sodata->col, &sodata->row, &sodata->width, &sodata->height);
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
    ModuleArgs *args = sodata->args;
    PSFMethodType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    GwyDataField *psf = sodata->psf;
    gdouble sigma, w;

    g_assert(method == PSF_METHOD_PSEUDO_WIENER);
    sigma = exp(logsigma);
    psf_deconvolve_wiener(sodata->wmeas, sodata->wideal, psf, sigma);
    gwy_data_field_area_abs(psf, sodata->col, sodata->row, sodata->width, sodata->height);
    w = gwy_data_field_area_get_dispersion(psf, NULL, GWY_MASK_IGNORE,
                                           sodata->col, sodata->row, sodata->width, sodata->height,
                                           NULL, NULL);
    return sqrt(w);
}

static gdouble
find_regularization_sigma(ModuleArgs *args)
{
    PSFMethodType method = gwy_params_get_enum(args->params, PARAM_METHOD);
    GwyDataField *ideal = gwy_params_get_image(args->params, PARAM_IDEAL);
    PSFSigmaOptData sodata;
    gdouble logsigma, sigma;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(args->field), 1.0);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(ideal), 1.0);
    g_return_val_if_fail(!gwy_data_field_check_compatibility(args->field, ideal,
                                                             GWY_DATA_COMPATIBILITY_RES
                                                             | GWY_DATA_COMPATIBILITY_REAL
                                                             | GWY_DATA_COMPATIBILITY_LATERAL), 1.0);

    psf_sigmaopt_prepare(args, &sodata);
    if (method == PSF_METHOD_REGULARISED) {
        sigma = gwy_data_field_find_regularization_sigma_for_psf(sodata.wmeas, sodata.wideal);
    }
    else if (method == PSF_METHOD_LEAST_SQUARES) {
        gint txres = gwy_params_get_int(args->params, PARAM_TXRES);
        gint tyres = gwy_params_get_int(args->params, PARAM_TYRES);
        gint border = gwy_params_get_int(args->params, PARAM_BORDER);
        sigma = gwy_data_field_find_regularization_sigma_leastsq(sodata.wmeas, sodata.wideal, txres, tyres, border);
    }
    else {
        logsigma = gwy_math_find_minimum_1d(psf_sigmaopt_evaluate, log(1e-8), log(1e3), &sodata);
        /* Experimentally determined fudge factor from large-scale simulations. */
        sigma = 0.375*exp(logsigma);
    }
    psf_sigmaopt_free(&sodata);

    return sigma;
}

/* This is an exact replica of gwy_data_field_deconvolve_regularized(). The only difference is that instead of σ² the
 * regularisation term is σ²/|P|², corresponding to pseudo-Wiener filter with the assumption of uncorrelated point
 * noise. */
static void
psf_deconvolve_wiener(GwyDataField *field,
                      GwyDataField *ideal,
                      GwyDataField *out,
                      gdouble sigma)
{
    gint xres, yres, i, cstride;
    gdouble lambda, q, frms, orms;
    fftw_complex *ffield, *foper;
    fftw_plan fplan, bplan;

    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    g_return_if_fail(GWY_IS_DATA_FIELD(ideal));
    g_return_if_fail(GWY_IS_DATA_FIELD(out));

    xres = field->xres;
    yres = field->yres;
    cstride = xres/2 + 1;
    g_return_if_fail(ideal->xres == xres);
    g_return_if_fail(ideal->yres == yres);
    gwy_data_field_resample(out, xres, yres, GWY_INTERPOLATION_NONE);

    orms = gwy_data_field_get_rms(ideal);
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
    if (gwy_threads_are_enabled())
        fftw_plan_with_nthreads(gwy_omp_max_threads());
#endif
    fplan = fftw_plan_dft_r2c_2d(yres, xres, out->data, ffield, FFTW_DESTROY_INPUT);
    bplan = fftw_plan_dft_c2r_2d(yres, xres, ffield, out->data, FFTW_DESTROY_INPUT);

    gwy_data_field_copy(ideal, out, FALSE);
    fftw_execute(fplan);
    gwy_assign(foper, ffield, cstride*yres);

    gwy_data_field_copy(field, out, FALSE);
    fftw_execute(fplan);
    fftw_destroy_plan(fplan);

    /* This seems wrong, but we just compensate the FFT. */
    orms *= sqrt(xres*yres);
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
    out->xoff = field->xoff;
    out->yoff = field->yoff;

    gwy_data_field_invalidate(out);
    set_transfer_function_units(ideal, field, out);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
