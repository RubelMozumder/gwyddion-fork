/*
 *  $Id: fit-shape.c 25439 2023-06-15 08:16:00Z yeti-dn $
 *  Copyright (C) 2016-2021 David Necas (Yeti).
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

/* TODO:
 * - Gradient adaptation only works for full-range mapping.  Simply enforce
 *   full-range mapping with adapted gradients?
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/peaks.h>
#include <libprocess/gwyshapefitpreset.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyinventorystore.h>
#include <libgwymodule/gwymodule-process.h>
#include <libgwymodule/gwymodule-xyz.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

#define FIT_GRADIENT_NAME "__GwyFitDiffGradient"

/* Lower symmetric part indexing */
/* i MUST be greater or equal than j */
#define SLi(a, i, j) a[(i)*((i) + 1)/2 + (j)]

enum { NREDLIM = 4096 };

enum {
    PARAM_FUNCTION,
    PARAM_MASKING,
    PARAM_DISPLAY,
    PARAM_OUTPUT,
    PARAM_REPORT_STYLE,
    PARAM_DIFF_COLOURMAP,
    PARAM_DIFF_EXCLUDED,
};

typedef enum {
    FIT_SHAPE_DISPLAY_DATA   = 0,
    FIT_SHAPE_DISPLAY_RESULT = 1,
    FIT_SHAPE_DISPLAY_DIFF   = 2
} FitShapeDisplayType;

typedef enum {
    FIT_SHAPE_OUTPUT_FIT  = 0,
    FIT_SHAPE_OUTPUT_DIFF = 1,
    FIT_SHAPE_OUTPUT_BOTH = 2,
} FitShapeOutputType;

typedef enum {
    FIT_SHAPE_INITIALISED      = 0,
    FIT_SHAPE_ESTIMATED        = 1,
    FIT_SHAPE_QUICK_FITTED     = 2,
    FIT_SHAPE_FITTED           = 3,
    FIT_SHAPE_USER             = 4,
    FIT_SHAPE_ESTIMATE_FAILED  = 5,
    FIT_SHAPE_QUICK_FIT_FAILED = 6,
    FIT_SHAPE_FIT_FAILED       = 7,
    FIT_SHAPE_FIT_CANCELLED    = 8,
} FitShapeState;

typedef struct {
    GtkWidget *fix;          /* Unused for secondary */
    GtkWidget *name;
    GtkWidget *equals;
    GtkWidget *value;
    GtkWidget *value_unit;
    GtkWidget *pm;
    GtkWidget *error;
    GtkWidget *error_unit;
    gdouble magnitude;       /* Unused for secondary */
} FitParamControl;

typedef struct {
    GwyParams *params;
    /* These are always what we display – when run with XYZ data they are just imge previews.  Conversely, a surface
     * is created for fitting even if the input is images. */
    GwyDataField *field;
    GwyDataField *mask;
    GwyDataField *result;
    GwyDataField *diff;
    GwySurface *surface;
    /* Function values. */
    gdouble *f;
    /* Cached input properties. */
    GwyAppPage pageno;
    gboolean same_units;
} ModuleArgs;

/* Struct with data used in fitter functions. */
typedef struct {
    ModuleArgs *args;
    guint nparam;
    gboolean *param_fixed;
    guint n;
    const GwyXYZ *xyz;
} FitShapeContext;

typedef struct {
    ModuleArgs *args;
    GwyContainer *args_data;
    gint id;
    /* These are actually non-GUI and could be separated for some non-interactive use. */
    FitShapeContext *ctx;
    FitShapeState state;
    GwyShapeFitPreset *preset;
    gdouble *param;
    gdouble *alt_param;
    gboolean *param_edited;
    gdouble *param_err;
    gdouble *correl;
    gdouble *secondary;
    gdouble *secondary_err;
    gdouble rss;
    /* This is GUI but we use the fields in data. */
    GwyContainer *data;
    GwyResults *results;
    GwyGradient *diff_gradient;
    GwyPixmapLayer *player;
    GwyParamTable *table;
    GtkWidget *dialog;
    GtkWidget *rss_label;
    GtkWidget *fit_message;
    GtkWidget *revert;
    GtkWidget *recalculate;
    GtkWidget *fit_param_table;
    GtkWidget *correl_table;
    GArray *param_controls;
    GPtrArray *correl_values;
    GPtrArray *correl_hlabels;
    GPtrArray *correl_vlabels;
    GtkWidget *secondary_table;
    GArray *secondary_controls;
} ModuleGUI;

static gboolean         module_register           (void);
static GwyParamDef*     define_module_params      (void);
static GwyDialogOutcome run_gui                   (ModuleArgs *args,
                                                   GwyContainer *data,
                                                   gint id);
static void             dialog_response           (ModuleGUI *gui,
                                                   gint response);
static void             param_changed             (ModuleGUI *gui,
                                                   gint id);
static void             fit_shape                 (GwyContainer *data,
                                                   GwyRunType runtype,
                                                   const gchar *name);
static void             create_output_fields      (ModuleArgs *args,
                                                   GwyContainer *data,
                                                   gint id);
static void             create_output_xyz         (ModuleArgs *args,
                                                   GwyContainer *data,
                                                   gint id);
static GtkWidget*       basic_tab_new             (ModuleGUI *gui);
static GtkWidget*       parameters_tab_new        (ModuleGUI *gui);
static void             fit_param_table_resize    (ModuleGUI *gui);
static GtkWidget*       correl_tab_new            (ModuleGUI *gui);
static void             fit_correl_table_resize   (ModuleGUI *gui);
static GtkWidget*       secondary_tab_new         (ModuleGUI *gui);
static void             fit_secondary_table_resize(ModuleGUI *gui);
static gboolean         preset_is_available       (const GwyEnum *enumval,
                                                   gpointer user_data);
static void             update_colourmap_key      (ModuleGUI *gui);
static void             fix_changed               (GtkToggleButton *button,
                                                   ModuleGUI *gui);
static void             param_value_activate      (GtkEntry *entry,
                                                   ModuleGUI *gui);
static void             param_value_edited        (GtkEntry *entry,
                                                   ModuleGUI *gui);
static void             update_all_param_values   (ModuleGUI *gui);
static void             revert_params             (ModuleGUI *gui);
static void             calculate_secondary_params(ModuleGUI *gui);
static void             recalculate_image         (ModuleGUI *gui);
static void             update_param_table        (ModuleGUI *gui,
                                                   const gdouble *param,
                                                   const gdouble *param_err);
static void             update_correl_table       (ModuleGUI *gui,
                                                   GwyNLFitter *fitter);
static void             update_secondary_table    (ModuleGUI *gui);
static void             fit_shape_estimate        (ModuleGUI *gui);
static void             fit_shape_quick_fit       (ModuleGUI *gui);
static void             fit_shape_full_fit        (ModuleGUI *gui);
static void             fit_copy_correl_matrix    (ModuleGUI *gui,
                                                   GwyNLFitter *fitter);
static void             update_fields             (ModuleGUI *gui);
static void             update_diff_gradient      (ModuleGUI *gui);
static void             update_fit_state          (ModuleGUI *gui);
static void             update_fit_results        (ModuleGUI *gui,
                                                   GwyNLFitter *fitter);
static void             update_context_data       (ModuleGUI *gui);
static void             fit_context_resize_params (FitShapeContext *ctx,
                                                   guint n_param);
static void             fit_context_free          (FitShapeContext *ctx);
static GwyNLFitter*     fit                       (GwyShapeFitPreset *preset,
                                                   const FitShapeContext *ctx,
                                                   gdouble *param,
                                                   gdouble *rss,
                                                   GwySetFractionFunc set_fraction,
                                                   GwySetMessageFunc set_message,
                                                   gboolean quick_fit);
static void             calculate_field           (GwyShapeFitPreset *preset,
                                                   const gdouble *params,
                                                   GwyDataField *field);
static void             create_results            (ModuleGUI *gui);
static void             fill_results              (ModuleGUI *gui);
static void             sanitise_params           (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Fits predefined geometrical shapes to data."),
    "Yeti <yeti@gwyddion.net>",
    "2.2",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, fit_shape)

static gboolean
module_register(void)
{
    gwy_process_func_register("fit_shape",
                              (GwyProcessFunc)&fit_shape,
                              N_("/Measure _Features/_Fit Shape..."),
                              GWY_STOCK_FIT_SHAPE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Fit geometrical shapes"));
    gwy_xyz_func_register("xyz_fit_shape",
                          (GwyXYZFunc)&fit_shape,
                          N_("/_Fit Shape..."),
                          GWY_STOCK_FIT_SHAPE,
                          RUN_MODES,
                          GWY_MENU_FLAG_XYZ,
                          N_("Fit geometrical shapes"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("Data"),         FIT_SHAPE_DISPLAY_DATA,   },
        { N_("Fitted shape"), FIT_SHAPE_DISPLAY_RESULT, },
        { N_("Difference"),   FIT_SHAPE_DISPLAY_DIFF,   },
    };
    static const GwyEnum outputs[] = {
        { N_("Fitted shape"), FIT_SHAPE_OUTPUT_FIT,  },
        { N_("Difference"),   FIT_SHAPE_OUTPUT_DIFF, },
        { N_("Both"),         FIT_SHAPE_OUTPUT_BOTH, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "fit_shape");
    /* The default must not require same units because then we could not fall back to it in all cases. */
    gwy_param_def_add_resource(paramdef, PARAM_FUNCTION, "function", _("_Function"),
                               gwy_shape_fit_presets(), "Grating (simple)");
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), FIT_SHAPE_DISPLAY_DIFF);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT, "output", _("Output _type"),
                              outputs, G_N_ELEMENTS(outputs), FIT_SHAPE_OUTPUT_BOTH);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", NULL,
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON | GWY_RESULTS_REPORT_MACHINE);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFF_COLOURMAP, "diff_colourmap",
                              _("Show differences with _adapted color map"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DIFF_EXCLUDED, "diff_excluded",
                              _("Calculate differences for e_xcluded pixels"), TRUE);
    return paramdef;
}

static void
fit_shape(GwyContainer *data, GwyRunType runtype, const gchar *name)
{
    GwyDialogOutcome outcome;
    GwySIUnit *xyunit, *zunit;
    ModuleArgs args;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    if (gwy_strequal(name, "xyz_fit_shape")) {
        args.pageno = GWY_PAGE_XYZS;
        gwy_app_data_browser_get_current(GWY_APP_SURFACE, &args.surface,
                                         GWY_APP_SURFACE_ID, &id,
                                         0);
        g_return_if_fail(args.surface);
        xyunit = gwy_surface_get_si_unit_xy(args.surface);
        zunit = gwy_surface_get_si_unit_z(args.surface);
    }
    else {
        args.pageno = GWY_PAGE_CHANNELS;
        gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                         GWY_APP_MASK_FIELD, &args.mask,
                                         GWY_APP_DATA_FIELD_ID, &id,
                                         0);
        g_return_if_fail(args.field);
        xyunit = gwy_data_field_get_si_unit_xy(args.field);
        zunit = gwy_data_field_get_si_unit_z(args.field);
    }
    args.same_units = gwy_si_unit_equal(xyunit, zunit);

    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);
    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    /* The user can press OK and we will produce images of whatever currently exists.  This allows just running it and
     * creating an image with the model shape, for instance. */
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    if (args.pageno == GWY_PAGE_XYZS)
        create_output_xyz(&args, data, id);
    else
        create_output_fields(&args, data, id);

end:
    g_free(args.f);
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.field);
    GWY_OBJECT_UNREF(args.mask);
    GWY_OBJECT_UNREF(args.result);
    GWY_OBJECT_UNREF(args.diff);
    GWY_OBJECT_UNREF(args.surface);
}

static void
create_output_fields(ModuleArgs *args, GwyContainer *data, gint id)
{
    FitShapeOutputType output = gwy_params_get_enum(args->params, PARAM_OUTPUT);
    gint newid;

    if (output == FIT_SHAPE_OUTPUT_FIT || output == FIT_SHAPE_OUTPUT_BOTH) {
        newid = gwy_app_data_browser_add_data_field(args->result, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                GWY_DATA_ITEM_SELECTIONS,
                                0);
        gwy_app_channel_log_add_proc(data, id, newid);
        gwy_app_set_data_field_title(data, newid, _("Fitted shape"));
    }

    if (output == FIT_SHAPE_OUTPUT_DIFF || output == FIT_SHAPE_OUTPUT_BOTH) {
        newid = gwy_app_data_browser_add_data_field(args->diff, data, TRUE);
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                GWY_DATA_ITEM_SELECTIONS,
                                0);
        gwy_app_channel_log_add_proc(data, id, newid);
        gwy_app_set_data_field_title(data, newid, _("Difference"));
    }
}

static void
create_output_xyz(ModuleArgs *args, GwyContainer *data, gint id)
{
    FitShapeOutputType output = gwy_params_get_enum(args->params, PARAM_OUTPUT);
    GwySurface *surface = args->surface, *result, *diff;
    const guchar *gradient = NULL;
    const GwyXYZ *xyz;
    GwyXYZ *dxyz;
    gint newid;
    guint n, i;

    gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient);
    n = gwy_surface_get_npoints(surface);

    if (output == FIT_SHAPE_OUTPUT_FIT || output == FIT_SHAPE_OUTPUT_BOTH) {
        result = gwy_surface_duplicate(surface);
        dxyz = gwy_surface_get_data(result);
        for (i = 0; i < n; i++)
            dxyz[i].z = args->f[i];

        newid = gwy_app_data_browser_add_surface(result, data, TRUE);
        gwy_app_xyz_log_add_xyz(data, id, newid);
        gwy_app_set_surface_title(data, newid, _("Fitted shape"));
        if (gradient)
            gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);
    }

    if (output == FIT_SHAPE_OUTPUT_DIFF || output == FIT_SHAPE_OUTPUT_BOTH) {
        diff = gwy_surface_duplicate(surface);
        xyz = gwy_surface_get_data_const(surface);
        dxyz = gwy_surface_get_data(diff);
        for (i = 0; i < n; i++)
            dxyz[i].z = xyz[i].z - args->f[i];

        newid = gwy_app_data_browser_add_surface(diff, data, TRUE);
        gwy_app_xyz_log_add_xyz(data, id, newid);
        gwy_app_set_surface_title(data, newid, _("Difference"));
        if (gradient)
            gwy_container_set_const_string(data, gwy_app_get_surface_palette_key_for_id(newid), gradient);
        g_object_unref(diff);
    }
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *vbox, *hbox, *auxbox, *dataview;
    GwyDialog *dialog;
    GtkNotebook *notebook;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    FitShapeContext ctx;

    gwy_clear(&ctx, 1);
    gwy_clear(&gui, 1);
    gui.args = ctx.args = args;
    gui.args_data = data;
    gui.id = id;
    gui.ctx = &ctx;

    if (args->pageno == GWY_PAGE_XYZS) {
        g_object_ref(args->surface);
        args->field = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
        gwy_preview_surface_to_datafield(args->surface, args->field,
                                         PREVIEW_SIZE, PREVIEW_SIZE, GWY_PREVIEW_SURFACE_FILL);
    }
    else {
        g_object_ref(args->field);
        if (args->mask)
            g_object_ref(args->mask);
        args->surface = gwy_surface_new();
    }
    args->result = gwy_data_field_new_alike(args->field, TRUE);
    args->diff = gwy_data_field_new_alike(args->field, TRUE);
    update_context_data(&gui);

    gui.diff_gradient = gwy_inventory_new_item(gwy_gradients(), GWY_GRADIENT_DEFAULT, FIT_GRADIENT_NAME);
    gwy_resource_use(GWY_RESOURCE(gui.diff_gradient));

    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->field);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(1), args->result);
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(2), args->diff);
    if (args->mask)
        gwy_container_set_object(gui.data, gwy_app_get_mask_key_for_id(0), args->mask);
    gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(2), FIT_GRADIENT_NAME);
    gwy_container_set_enum(gui.data, gwy_app_get_data_range_type_key_for_id(2), GWY_LAYER_BASIC_RANGE_FIXED);
    if (args->pageno == GWY_PAGE_XYZS) {
        const guchar *gradient;
        if (gwy_container_gis_string(data, gwy_app_get_surface_palette_key_for_id(id), &gradient))
            gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);
    }
    else {
        gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                                GWY_DATA_ITEM_PALETTE,
                                GWY_DATA_ITEM_MASK_COLOR,
                                GWY_DATA_ITEM_RANGE_TYPE,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
    }

    gui.dialog = gwy_dialog_new(_("Fit Shape"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_COPY, RESPONSE_COPY);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_SAVE, RESPONSE_SAVE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Fit"), RESPONSE_REFINE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Quick Fit"), RESPONSE_CALCULATE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Estimate"), RESPONSE_ESTIMATE);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);
    gui.player = gwy_data_view_get_base_layer(GWY_DATA_VIEW(dataview));

    vbox = gwy_vbox_new(8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

    notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(notebook), TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, basic_tab_new(&gui), gtk_label_new(gwy_sgettext("adjective|Basic")));
    gtk_notebook_append_page(notebook, parameters_tab_new(&gui), gtk_label_new(_("Parameters")));
    gtk_notebook_append_page(notebook, correl_tab_new(&gui), gtk_label_new(_("Correlation Matrix")));
    gtk_notebook_append_page(notebook, secondary_tab_new(&gui), gtk_label_new(_("Derived Quantities")));

    auxbox = gwy_hbox_new(6);
    gtk_box_pack_start(GTK_BOX(vbox), auxbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(auxbox), gtk_label_new(_("Mean square difference:")), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(auxbox), (gui.rss_label = gtk_label_new(NULL)), FALSE, FALSE, 0);

    auxbox = gwy_hbox_new(6);
    gtk_box_pack_start(GTK_BOX(vbox), auxbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(auxbox), (gui.fit_message = gtk_label_new(NULL)), FALSE, FALSE, 0);

    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);

    outcome = gwy_dialog_run(dialog);

    gwy_resource_release(GWY_RESOURCE(gui.diff_gradient));
    gwy_inventory_delete_item(gwy_gradients(), FIT_GRADIENT_NAME);
    g_object_unref(gui.data);
    GWY_OBJECT_UNREF(gui.results);
    g_free(gui.param);
    g_free(gui.alt_param);
    g_free(gui.param_edited);
    g_free(gui.param_err);
    g_free(gui.secondary);
    g_free(gui.secondary_err);
    g_free(gui.correl);
    g_array_free(gui.param_controls, TRUE);
    g_ptr_array_free(gui.correl_values, TRUE);
    g_ptr_array_free(gui.correl_hlabels, TRUE);
    g_ptr_array_free(gui.correl_vlabels, TRUE);
    g_array_free(gui.secondary_controls, TRUE);
    fit_context_free(gui.ctx);

    return outcome;
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_REFINE) {
        fit_shape_full_fit(gui);
        if (gui->state == FIT_SHAPE_FITTED)
            fill_results(gui);
    }
    else if (response == RESPONSE_CALCULATE)
        fit_shape_quick_fit(gui);
    else if (response == RESPONSE_ESTIMATE)
        fit_shape_estimate(gui);
    else if (response == RESPONSE_SAVE || response == RESPONSE_COPY) {
        GwyResultsExportStyle report_style = gwy_params_get_report_type(gui->args->params, PARAM_REPORT_STYLE);
        gchar *report = gwy_results_create_report(gui->results, report_style);

        if (response == RESPONSE_SAVE)
            gwy_save_auxiliary_data(_("Save Fit Report"), GTK_WINDOW(gui->dialog), -1, report);
        else {
            GdkDisplay *display = gtk_widget_get_display(gui->dialog);
            GtkClipboard *clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(clipboard, report, -1);
        }
        g_free(report);
    }
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_FUNCTION) {
        FitShapeContext *ctx = gui->ctx;
        guint i, nparams, nsecondary;

        gui->preset = gwy_inventory_get_item(gwy_shape_fit_presets(), gwy_params_get_string(params, PARAM_FUNCTION));
        nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
        nsecondary = gwy_shape_fit_preset_get_nsecondary(gui->preset);

        gui->param = g_renew(gdouble, gui->param, nparams);
        gui->alt_param = g_renew(gdouble, gui->alt_param, nparams);
        gui->param_edited = g_renew(gboolean, gui->param_edited, nparams);
        gui->param_err = g_renew(gdouble, gui->param_err, nparams);
        gui->secondary = g_renew(gdouble, gui->secondary, nsecondary);
        gui->secondary_err = g_renew(gdouble, gui->secondary_err, nsecondary);
        gui->correl = g_renew(gdouble, gui->correl, (nparams + 1)*nparams/2);
        for (i = 0; i < nparams; i++) {
            gui->param_err[i] = -1.0;
            /* Start from what is shown in the UI. */
            gui->param_edited[i] = TRUE;
        }
        fit_param_table_resize(gui);
        fit_correl_table_resize(gui);
        fit_secondary_table_resize(gui);
        fit_context_resize_params(ctx, nparams);
        gwy_shape_fit_preset_setup(gui->preset, ctx->xyz, ctx->n, gui->param);
        gui->state = FIT_SHAPE_INITIALISED;
        fit_copy_correl_matrix(gui, NULL);
        gwy_assign(gui->alt_param, gui->param, nparams);
        calculate_secondary_params(gui);
        update_param_table(gui, gui->param, NULL);
        update_correl_table(gui, NULL);
        update_fit_results(gui, NULL);
        update_fields(gui);
        update_fit_state(gui);
        create_results(gui);
    }

    if (id < 0 || id == PARAM_DISPLAY) {
        GQuark quark;

        quark = gwy_app_get_data_key_for_id(gwy_params_get_enum(params, PARAM_DISPLAY));
        gwy_pixmap_layer_set_data_key(gui->player, g_quark_to_string(quark));
        update_colourmap_key(gui);
    }
    if (id == PARAM_MASKING) {
        update_context_data(gui);
        gui->state = FIT_SHAPE_INITIALISED;
        update_fit_results(gui, NULL);
        if (!gwy_params_get_boolean(params, PARAM_DIFF_EXCLUDED))
            update_fields(gui);
        update_fit_state(gui);
    }
    if (id == PARAM_DIFF_EXCLUDED) {
        if (gwy_params_get_enum(params, PARAM_MASKING) != GWY_MASK_IGNORE)
            update_fields(gui);
    }
    if (id == PARAM_DIFF_COLOURMAP)
        update_colourmap_key(gui);
}

static GtkWidget*
basic_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_FUNCTION);
    gwy_param_table_combo_set_filter(table, PARAM_FUNCTION, preset_is_available, gui, NULL);
    gwy_param_table_append_combo(table, PARAM_OUTPUT);
    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_DIFF_COLOURMAP);
    if (args->mask) {
        gwy_param_table_append_combo(table, PARAM_MASKING);
        gwy_param_table_append_checkbox(table, PARAM_DIFF_EXCLUDED);
    }
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);

    return gwy_param_table_widget(table);
}

static GtkWidget*
parameters_tab_new(ModuleGUI *gui)
{
    GtkWidget *vbox, *hbox;
    GtkSizeGroup *sizegroup;
    GtkTable *table;

    vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

    gui->fit_param_table = gtk_table_new(1, 8, FALSE);
    table = GTK_TABLE(gui->fit_param_table);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 2);
    gtk_table_set_col_spacing(table, 0, 6);
    gtk_table_set_col_spacing(table, 4, 6);
    gtk_table_set_col_spacing(table, 5, 6);
    gtk_table_set_col_spacing(table, 7, 6);
    gtk_box_pack_start(GTK_BOX(vbox), gui->fit_param_table, FALSE, FALSE, 0);

    gtk_table_attach(table, gwy_label_new_header(_("Fix")), 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Parameter")), 1, 5, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Error")), 6, 8, 0, 1, GTK_FILL, 0, 0, 0);

    gui->param_controls = g_array_new(FALSE, FALSE, sizeof(FitParamControl));

    sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gui->recalculate = gtk_button_new_with_mnemonic(_("_Recalculate Image"));
    gtk_size_group_add_widget(sizegroup, gui->recalculate);
    gtk_box_pack_start(GTK_BOX(hbox), gui->recalculate, FALSE, FALSE, 0);
    g_signal_connect_swapped(gui->recalculate, "clicked", G_CALLBACK(recalculate_image), gui);

    gui->revert = gtk_button_new_with_mnemonic(_("Revert to _Previous Values"));
    gtk_size_group_add_widget(sizegroup, gui->revert);
    gtk_box_pack_start(GTK_BOX(hbox), gui->revert, FALSE, FALSE, 0);
    g_signal_connect_swapped(gui->revert, "clicked", G_CALLBACK(revert_params), gui);

    g_object_unref(sizegroup);

    return vbox;
}

static void
fit_param_table_resize(ModuleGUI *gui)
{
    GtkTable *table;
    guint i, row, old_nparams, nparams;

    old_nparams = gui->param_controls->len;
    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    gwy_debug("%u -> %u", old_nparams, nparams);
    for (i = old_nparams; i > nparams; i--) {
        FitParamControl *cntrl = &g_array_index(gui->param_controls, FitParamControl, i-1);
        gtk_widget_destroy(cntrl->fix);
        gtk_widget_destroy(cntrl->name);
        gtk_widget_destroy(cntrl->equals);
        gtk_widget_destroy(cntrl->value);
        gtk_widget_destroy(cntrl->value_unit);
        gtk_widget_destroy(cntrl->pm);
        gtk_widget_destroy(cntrl->error);
        gtk_widget_destroy(cntrl->error_unit);
        g_array_set_size(gui->param_controls, i-1);
    }

    table = GTK_TABLE(gui->fit_param_table);
    gtk_table_resize(table, 1+nparams, 8);
    row = old_nparams + 1;

    for (i = old_nparams; i < nparams; i++) {
        FitParamControl cntrl;

        cntrl.fix = gtk_check_button_new();
        gtk_table_attach(table, cntrl.fix, 0, 1, row, row+1, 0, 0, 0, 0);
        g_object_set_data(G_OBJECT(cntrl.fix), "id", GUINT_TO_POINTER(i));
        g_signal_connect(cntrl.fix, "toggled", G_CALLBACK(fix_changed), gui);

        cntrl.name = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.name), 1.0, 0.5);
        gtk_table_attach(table, cntrl.name, 1, 2, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.equals = gtk_label_new("=");
        gtk_table_attach(table, cntrl.equals, 2, 3, row, row+1, 0, 0, 0, 0);

        cntrl.value = gtk_entry_new();
        gtk_entry_set_width_chars(GTK_ENTRY(cntrl.value), 12);
        gtk_table_attach(table, cntrl.value, 3, 4, row, row+1, GTK_FILL, 0, 0, 0);
        g_object_set_data(G_OBJECT(cntrl.value), "id", GUINT_TO_POINTER(i));
        g_signal_connect(cntrl.value, "activate", G_CALLBACK(param_value_activate), gui);
        g_signal_connect(cntrl.value, "changed", G_CALLBACK(param_value_edited), gui);
        gwy_widget_set_activate_on_unfocus(cntrl.value, TRUE);

        cntrl.value_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.value_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.value_unit, 4, 5, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.pm = gtk_label_new("±");
        gtk_table_attach(table, cntrl.pm, 5, 6, row, row+1, 0, 0, 0, 0);

        cntrl.error = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error), 1.0, 0.5);
        gtk_table_attach(table, cntrl.error, 6, 7, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.error_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.error_unit, 7, 8, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.magnitude = 1.0;
        g_array_append_val(gui->param_controls, cntrl);
        row++;
    }

    for (i = 0; i < nparams; i++) {
        FitParamControl *cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
        const gchar *name = gwy_shape_fit_preset_get_param_name(gui->preset, i);
        const gchar *desc = gwy_shape_fit_preset_get_param_description(gui->preset, i);

        gtk_label_set_markup(GTK_LABEL(cntrl->name), name);
        /* Set it on all three. */
        gtk_widget_set_tooltip_markup(cntrl->name, desc);
        gtk_widget_set_tooltip_markup(cntrl->equals, desc);
        gtk_widget_set_tooltip_markup(cntrl->value, desc);
    }

    gtk_widget_show_all(gui->fit_param_table);
}

static GtkWidget*
correl_tab_new(ModuleGUI *gui)
{
    GtkWidget *scwin;
    GtkTable *table;

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scwin), 4);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);

    gui->correl_table = gtk_table_new(1, 1, TRUE);
    table = GTK_TABLE(gui->correl_table);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scwin), gui->correl_table);

    gui->correl_values = g_ptr_array_new();
    gui->correl_hlabels = g_ptr_array_new();
    gui->correl_vlabels = g_ptr_array_new();

    return scwin;
}

static void
fit_correl_table_resize(ModuleGUI *gui)
{
    GtkTable *table;
    GtkWidget *label;
    guint i, j, nparams;
    GPtrArray *vlabels = gui->correl_vlabels, *hlabels = gui->correl_hlabels, *values = gui->correl_values;

    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    gwy_debug("%u -> %u", hlabels->len, nparams);
    if (hlabels->len != nparams) {
        for (i = 0; i < hlabels->len; i++)
            gtk_widget_destroy((GtkWidget*)g_ptr_array_index(hlabels, i));
        g_ptr_array_set_size(hlabels, 0);

        for (i = 0; i < vlabels->len; i++)
            gtk_widget_destroy((GtkWidget*)g_ptr_array_index(vlabels, i));
        g_ptr_array_set_size(vlabels, 0);

        for (i = 0; i < values->len; i++)
            gtk_widget_destroy((GtkWidget*)g_ptr_array_index(values, i));
        g_ptr_array_set_size(values, 0);

        table = GTK_TABLE(gui->correl_table);
        gtk_table_resize(table, nparams+1, nparams+1);

        for (i = 0; i < nparams; i++) {
            label = gtk_label_new(NULL);
            gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
            gtk_table_attach(table, label, 0, 1, i, i+1, GTK_FILL, 0, 0, 0);
            g_ptr_array_add(vlabels, label);
        }

        for (i = 0; i < nparams; i++) {
            label = gtk_label_new(NULL);
            gtk_table_attach(table, label, i+1, i+2, nparams, nparams+1, GTK_FILL, 0, 0, 0);
            g_ptr_array_add(hlabels, label);
        }

        for (i = 0; i < nparams; i++) {
            for (j = 0; j <= i; j++) {
                label = gtk_label_new(NULL);
                gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
                gtk_table_attach(table, label, j+1, j+2, i, i+1, GTK_FILL, 0, 0, 0);
                g_ptr_array_add(values, label);
            }
        }
    }

    for (i = 0; i < nparams; i++) {
        const gchar *name = gwy_shape_fit_preset_get_param_name(gui->preset, i);

        gtk_label_set_markup(g_ptr_array_index(vlabels, i), name);
        gtk_label_set_markup(g_ptr_array_index(hlabels, i), name);
    }

    gtk_widget_show_all(gui->correl_table);
}

static GtkWidget*
secondary_tab_new(ModuleGUI *gui)
{
    GtkTable *table;

    gui->secondary_table = gtk_table_new(1, 7, FALSE);
    table = GTK_TABLE(gui->secondary_table);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 2);
    gtk_table_set_col_spacing(table, 3, 6);
    gtk_table_set_col_spacing(table, 4, 6);
    gtk_table_set_col_spacing(table, 6, 6);

    gui->secondary_controls = g_array_new(FALSE, FALSE, sizeof(FitParamControl));
    return gui->secondary_table;
}

static void
fit_secondary_table_resize(ModuleGUI *gui)
{
    GtkTable *table;
    guint i, row, old_nsecondary, nsecondary;

    old_nsecondary = gui->secondary_controls->len;
    nsecondary = gwy_shape_fit_preset_get_nsecondary(gui->preset);
    gwy_debug("%u -> %u", old_nsecondary, nsecondary);
    for (i = old_nsecondary; i > nsecondary; i--) {
        FitParamControl *cntrl = &g_array_index(gui->secondary_controls, FitParamControl, i-1);
        gtk_widget_destroy(cntrl->name);
        gtk_widget_destroy(cntrl->equals);
        gtk_widget_destroy(cntrl->value);
        gtk_widget_destroy(cntrl->value_unit);
        gtk_widget_destroy(cntrl->pm);
        gtk_widget_destroy(cntrl->error);
        gtk_widget_destroy(cntrl->error_unit);
        g_array_set_size(gui->secondary_controls, i-1);
    }

    table = GTK_TABLE(gui->secondary_table);
    gtk_table_resize(table, 1+nsecondary, 8);
    row = old_nsecondary + 1;

    for (i = old_nsecondary; i < nsecondary; i++) {
        FitParamControl cntrl;

        cntrl.name = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.name), 1.0, 0.5);
        gtk_table_attach(table, cntrl.name, 0, 1, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.equals = gtk_label_new("=");
        gtk_table_attach(table, cntrl.equals, 1, 2, row, row+1, 0, 0, 0, 0);

        cntrl.value = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.value), 1.0, 0.5);
        gtk_table_attach(table, cntrl.value, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.value_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.value_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.value_unit, 3, 4, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.pm = gtk_label_new("±");
        gtk_table_attach(table, cntrl.pm, 4, 5, row, row+1, 0, 0, 0, 0);

        cntrl.error = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error), 1.0, 0.5);
        gtk_table_attach(table, cntrl.error, 5, 6, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.error_unit = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(cntrl.error_unit), 0.0, 0.5);
        gtk_table_attach(table, cntrl.error_unit, 6, 7, row, row+1, GTK_FILL, 0, 0, 0);

        cntrl.magnitude = 1.0;
        g_array_append_val(gui->secondary_controls, cntrl);
        row++;
    }

    for (i = 0; i < nsecondary; i++) {
        FitParamControl *cntrl = &g_array_index(gui->secondary_controls, FitParamControl, i);
        const gchar *name = gwy_shape_fit_preset_get_secondary_name(gui->preset, i);
        const gchar *desc = gwy_shape_fit_preset_get_secondary_description(gui->preset, i);

        gtk_label_set_markup(GTK_LABEL(cntrl->name), name);
        /* Set it on all three. */
        gtk_widget_set_tooltip_markup(cntrl->name, desc);
        gtk_widget_set_tooltip_markup(cntrl->equals, desc);
        gtk_widget_set_tooltip_markup(cntrl->value, desc);
    }

    gtk_widget_show_all(gui->secondary_table);
}

static gboolean
preset_is_available(const GwyEnum *enumval, gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    gboolean same_units = gui->args->same_units;

    if (same_units)
        return TRUE;

    return !gwy_shape_fit_preset_needs_same_units(gwy_inventory_get_item(gwy_shape_fit_presets(), enumval->name));
}

static void
update_colourmap_key(ModuleGUI *gui)
{
    GwyLayerBasic *blayer = GWY_LAYER_BASIC(gui->player);
    gboolean diff_colourmap = gwy_params_get_boolean(gui->args->params, PARAM_DIFF_COLOURMAP);
    FitShapeDisplayType display = gwy_params_get_enum(gui->args->params, PARAM_DISPLAY);
    gint i;

    i = (diff_colourmap && display == FIT_SHAPE_DISPLAY_DIFF ? 2 : 0);
    gwy_layer_basic_set_gradient_key(blayer, g_quark_to_string(gwy_app_get_data_palette_key_for_id(i)));
    gwy_layer_basic_set_range_type_key(blayer, g_quark_to_string(gwy_app_get_data_range_type_key_for_id(i)));
    gwy_layer_basic_set_min_max_key(blayer, g_quark_to_string(gwy_app_get_data_base_key_for_id(i)));
}

static void
fix_changed(GtkToggleButton *button, ModuleGUI *gui)
{
    gboolean fixed = gtk_toggle_button_get_active(button);
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "id"));
    const FitShapeContext *ctx = gui->ctx;

    ctx->param_fixed[i] = fixed;
}

static gdouble
transform_value(gdouble v, GwyNLFitParamFlags flags)
{
    if (flags & GWY_NLFIT_PARAM_ANGLE)
        v *= 180.0/G_PI;
    if (flags & GWY_NLFIT_PARAM_ABSVAL)
        v = fabs(v);
    return v;
}

static gdouble
transform_value_back(gdouble v, GwyNLFitParamFlags flags)
{
    if (flags & GWY_NLFIT_PARAM_ANGLE)
        v *= G_PI/180.0;
    if (flags & GWY_NLFIT_PARAM_ABSVAL)
        v = fabs(v);
    return v;
}

static void
update_param_value(ModuleGUI *gui, guint i)
{
    FitParamControl *cntrl;
    GwyNLFitParamFlags flags;
    GtkEntry *entry;

    cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
    entry = GTK_ENTRY(cntrl->value);
    flags = gwy_shape_fit_preset_get_param_flags(gui->preset, i);
    gui->param[i] = g_strtod(gtk_entry_get_text(entry), NULL);
    gui->param[i] *= cntrl->magnitude;
    gui->param[i] = transform_value_back(gui->param[i], flags);
}

static void
param_value_activate(GtkEntry *entry, ModuleGUI *gui)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "id"));

    update_param_value(gui, i);
    /* This (a) clears error labels in the table (b) reformats the parameter, e.g. by moving the power-of-10 base
     * appropriately. */
    gui->state = FIT_SHAPE_USER;
    calculate_secondary_params(gui);
    update_param_table(gui, gui->param, NULL);
    update_correl_table(gui, NULL);
    update_secondary_table(gui);
    update_fit_state(gui);
}

static void
param_value_edited(GtkEntry *entry, ModuleGUI *gui)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(entry), "id"));

    gui->param_edited[i] = TRUE;
}

static void
update_all_param_values(ModuleGUI *gui)
{
    guint i;
    for (i = 0; i < gui->param_controls->len; i++) {
        if (gui->param_edited[i])
            update_param_value(gui, i);
    }
}

static void
revert_params(ModuleGUI *gui)
{
    guint i, nparams;

    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    update_all_param_values(gui);
    for (i = 0; i < nparams; i++) {
        if (gui->param[i] != gui->alt_param[i])
            gui->param_edited[i] = TRUE;
        GWY_SWAP(gdouble, gui->param[i], gui->alt_param[i]);
    }

    gui->state = FIT_SHAPE_USER;
    calculate_secondary_params(gui);
    update_param_table(gui, gui->param, NULL);
    update_correl_table(gui, NULL);
    update_secondary_table(gui);
    update_fit_state(gui);
}

static void
recalculate_image(ModuleGUI *gui)
{
    gui->state = FIT_SHAPE_USER;
    update_all_param_values(gui);
    update_fit_results(gui, NULL);
    update_fields(gui);
    update_fit_state(gui);
}

static void
update_param_table(ModuleGUI *gui, const gdouble *param, const gdouble *param_err)
{
    const GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_VFMARKUP;
    guint i, nparams;
    GwySIUnit *unit, *xyunit, *zunit;
    GwySIValueFormat *vf = NULL;

    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    xyunit = gwy_data_field_get_si_unit_xy(gui->args->field);
    zunit = gwy_data_field_get_si_unit_z(gui->args->field);
    unit = gwy_si_unit_new(NULL);

    for (i = 0; i < nparams; i++) {
        FitParamControl *cntrl = &g_array_index(gui->param_controls, FitParamControl, i);
        GwyNLFitParamFlags flags;
        guchar buf[64];
        gdouble v, e;

        flags = gwy_shape_fit_preset_get_param_flags(gui->preset, i);
        v = transform_value(param[i], flags);
        if (flags & GWY_NLFIT_PARAM_ANGLE)
            gwy_si_unit_set_from_string(unit, "deg");
        else {
            g_object_unref(unit);
            unit = gwy_shape_fit_preset_get_param_units(gui->preset, i, xyunit, zunit);
        }
        /* If the user enters exact zero, do not update the magnitude because
         * that means an unexpected reset to base units. */
        if (G_UNLIKELY(v == 0.0)) {
            gint power10 = GWY_ROUND(log10(cntrl->magnitude));
            vf = gwy_si_unit_get_format_for_power10(unit, style, power10, vf);
            vf->precision += 3;
        }
        else if (param_err && param_err[i]) {
            e = transform_value(param_err[i], flags);
            vf = gwy_si_unit_get_format_with_resolution(unit, style, v, fmin(0.1*e, 0.01*v), vf);
        }
        else {
            vf = gwy_si_unit_get_format(unit, style, v, vf);
            vf->precision += 3;
        }
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, v/vf->magnitude);
        gtk_entry_set_text(GTK_ENTRY(cntrl->value), buf);
        gtk_label_set_markup(GTK_LABEL(cntrl->value_unit), vf->units);
        cntrl->magnitude = vf->magnitude;

        if (!param_err) {
            gtk_label_set_text(GTK_LABEL(cntrl->error), "");
            gtk_label_set_text(GTK_LABEL(cntrl->error_unit), "");
            continue;
        }

        v = transform_value(param_err[i], flags);
        vf = gwy_si_unit_get_format(unit, style, v, vf);
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, v/vf->magnitude);
        gtk_label_set_text(GTK_LABEL(cntrl->error), buf);
        gtk_label_set_markup(GTK_LABEL(cntrl->error_unit), vf->units);
    }

    gwy_si_unit_value_format_free(vf);
    g_object_unref(unit);
}

static void
update_correl_table(ModuleGUI *gui, GwyNLFitter *fitter)
{
    const gboolean *param_fixed = gui->ctx->param_fixed;
    GPtrArray *values = gui->correl_values;
    guint i, j, nparams;

    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    g_assert(values->len == (nparams + 1)*nparams/2);
    gwy_debug("fitter %p", fitter);

    for (i = 0; i < nparams; i++) {
        for (j = 0; j <= i; j++) {
            GtkWidget *label = g_ptr_array_index(values, i*(i + 1)/2 + j);

            if (fitter) {
                gchar buf[16];
                gdouble c = SLi(gui->correl, i, j);

                if (param_fixed[i] || param_fixed[j]) {
                    if (i == j) {
                        g_snprintf(buf, sizeof(buf), "%.3f", 1.0);
                        gtk_label_set_text(GTK_LABEL(label), buf);
                    }
                    else
                        gtk_label_set_text(GTK_LABEL(label), "—");
                    set_widget_as_ok_message(label);
                    continue;
                }

                g_snprintf(buf, sizeof(buf), "%.3f", c);
                gtk_label_set_text(GTK_LABEL(label), buf);
                if (i != j) {
                    if (fabs(c) >= 0.99)
                        set_widget_as_error_message(label);
                    else if (fabs(c) >= 0.9)
                        set_widget_as_warning_message(label);
                    else
                        set_widget_as_ok_message(label);
                }
            }
            else
                gtk_label_set_text(GTK_LABEL(label), "");
        }
    }

    /* For some reason, this does not happen automatically after the set-text
     * call so the labels that had initially zero width remain invisible even
     * though there is a number to display now. */
    if (fitter)
        gtk_widget_queue_resize(gui->correl_table);
}

static void
update_secondary_table(ModuleGUI *gui)
{
    const GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_VFMARKUP;
    gboolean is_fitted = (gui->state == FIT_SHAPE_FITTED || gui->state == FIT_SHAPE_QUICK_FITTED);
    guint i, nsecondary;
    GwySIUnit *unit, *xyunit, *zunit;
    GwySIValueFormat *vf = NULL;

    nsecondary = gwy_shape_fit_preset_get_nsecondary(gui->preset);
    xyunit = gwy_data_field_get_si_unit_xy(gui->args->field);
    zunit = gwy_data_field_get_si_unit_z(gui->args->field);
    unit = gwy_si_unit_new(NULL);

    for (i = 0; i < nsecondary; i++) {
        FitParamControl *cntrl = &g_array_index(gui->secondary_controls, FitParamControl, i);
        GwyNLFitParamFlags flags;
        guchar buf[64];
        gdouble v, e;

        flags = gwy_shape_fit_preset_get_secondary_flags(gui->preset, i);
        v = transform_value(gui->secondary[i], flags);
        if (flags & GWY_NLFIT_PARAM_ANGLE)
            gwy_si_unit_set_from_string(unit, "deg");
        else {
            g_object_unref(unit);
            unit = gwy_shape_fit_preset_get_secondary_units(gui->preset, i, xyunit, zunit);
        }
        if (is_fitted && gui->secondary_err[i]) {
            e = transform_value(gui->secondary_err[i], flags);
            vf = gwy_si_unit_get_format_with_resolution(unit, style, v, fmin(0.1*e, 0.01*v), vf);
        }
        else {
            vf = gwy_si_unit_get_format(unit, style, v, vf);
            vf->precision += 3;
        }
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, v/vf->magnitude);
        gtk_label_set_text(GTK_LABEL(cntrl->value), buf);
        gtk_label_set_markup(GTK_LABEL(cntrl->value_unit), vf->units);

        if (!is_fitted) {
            gtk_label_set_text(GTK_LABEL(cntrl->error), "");
            gtk_label_set_text(GTK_LABEL(cntrl->error_unit), "");
            continue;
        }

        v = transform_value(gui->secondary_err[i], flags);
        vf = gwy_si_unit_get_format(unit, style, v, vf);
        g_snprintf(buf, sizeof(buf), "%.*f", vf->precision, v/vf->magnitude);
        gtk_label_set_text(GTK_LABEL(cntrl->error), buf);
        gtk_label_set_markup(GTK_LABEL(cntrl->error_unit), vf->units);
    }

    GWY_SI_VALUE_FORMAT_FREE(vf);
    g_object_unref(unit);
}

static void
fit_shape_estimate(ModuleGUI *gui)
{
    const FitShapeContext *ctx = gui->ctx;
    guint i, nparams;

    gwy_app_wait_cursor_start(GTK_WINDOW(gui->dialog));
    gwy_debug("start estimate");
    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    gwy_assign(gui->alt_param, gui->param, nparams);
    if (gwy_shape_fit_preset_guess(gui->preset, ctx->xyz, ctx->n, gui->param))
        gui->state = FIT_SHAPE_ESTIMATED;
    else
        gui->state = FIT_SHAPE_ESTIMATE_FAILED;

    /* XXX: We honour fixed parameters by reverting to previous values and pretending nothing happened.  Is it OK? */
    for (i = 0; i < nparams; i++) {
        gwy_debug("[%u] %g", i, gui->param[i]);
        if (ctx->param_fixed[i])
            gui->param[i] = gui->alt_param[i];
    }
    update_fit_results(gui, NULL);
    update_fields(gui);
    update_fit_state(gui);
    gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
}

static void
fit_shape_quick_fit(ModuleGUI *gui)
{
    const FitShapeContext *ctx = gui->ctx;
    GwyNLFitter *fitter;
    gdouble rss;
    guint nparams;

    gwy_app_wait_cursor_start(GTK_WINDOW(gui->dialog));
    gwy_debug("start quick fit");
    update_all_param_values(gui);
    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    gwy_assign(gui->alt_param, gui->param, nparams);
    fitter = fit(gui->preset, ctx, gui->param, &rss, NULL, NULL, TRUE);

    if (rss >= 0.0)
        gui->state = FIT_SHAPE_QUICK_FITTED;
    else
        gui->state = FIT_SHAPE_QUICK_FIT_FAILED;

#ifdef DEBUG
    {
        guint i;
        for (i = 0; i < nparams; i++)
            gwy_debug("[%u] %g", i, gui->param[i]);
    }
#endif
    fit_copy_correl_matrix(gui, fitter);
    update_fit_results(gui, fitter);
    update_fields(gui);
    update_fit_state(gui);
    gwy_math_nlfit_free(fitter);
    gwy_app_wait_cursor_finish(GTK_WINDOW(gui->dialog));
}

static void
fit_shape_full_fit(ModuleGUI *gui)
{
    const FitShapeContext *ctx = gui->ctx;
    GwyNLFitter *fitter;
    gdouble rss;
    guint nparams;

    gwy_app_wait_start(GTK_WINDOW(gui->dialog), _("Fitting..."));
    gwy_debug("start fit");
    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    update_all_param_values(gui);
    gwy_assign(gui->alt_param, gui->param, nparams);
    fitter = fit(gui->preset, ctx, gui->param, &rss, gwy_app_wait_set_fraction, gwy_app_wait_set_message, FALSE);

    if (rss >= 0.0)
        gui->state = FIT_SHAPE_FITTED;
    else if (rss == -2.0)
        gui->state = FIT_SHAPE_FIT_CANCELLED;
    else
        gui->state = FIT_SHAPE_FIT_FAILED;

#ifdef DEBUG
    {
        guint i;
        for (i = 0; i < nparams; i++)
            gwy_debug("[%u] %g", i, gui->param[i]);
    }
#endif
    fit_copy_correl_matrix(gui, fitter);
    update_fit_results(gui, fitter);
    update_fields(gui);
    update_fit_state(gui);
    gwy_math_nlfit_free(fitter);
    gwy_app_wait_finish();
}

static void
fit_copy_correl_matrix(ModuleGUI *gui, GwyNLFitter *fitter)
{
    gboolean is_fitted = (gui->state == FIT_SHAPE_FITTED || gui->state == FIT_SHAPE_QUICK_FITTED);
    guint i, j, nparams;

    nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
    gwy_clear(gui->correl, (nparams + 1)*nparams/2);
    if (is_fitted) {
        g_return_if_fail(fitter && gwy_math_nlfit_get_covar(fitter));
        for (i = 0; i < nparams; i++) {
            for (j = 0; j <= i; j++)
                SLi(gui->correl, i, j) = gwy_math_nlfit_get_correlations(fitter, i, j);
        }
    }
}

static void
calculate_secondary_params(ModuleGUI *gui)
{
    gboolean is_fitted = (gui->state == FIT_SHAPE_FITTED || gui->state == FIT_SHAPE_QUICK_FITTED);
    guint i, nsecondary;

    nsecondary = gwy_shape_fit_preset_get_nsecondary(gui->preset);
    gwy_clear(gui->secondary_err, nsecondary);
    for (i = 0; i < nsecondary; i++) {
        gui->secondary[i] = gwy_shape_fit_preset_get_secondary_value(gui->preset, i, gui->param);
        if (is_fitted) {
            gui->secondary_err[i] = gwy_shape_fit_preset_get_secondary_error(gui->preset, i,
                                                                             gui->param, gui->param_err, gui->correl);
        }
        gwy_debug("[%u] %g +- %g", i, gui->secondary[i], gui->secondary_err[i]);
    }
}

static void
update_fields(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *field = args->field, *result = args->result, *diff = args->diff, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    gboolean diff_excluded = gwy_params_get_boolean(args->params, PARAM_DIFF_EXCLUDED);
    GwySurface *surface;
    FitShapeContext *ctx = gui->ctx;
    guint xres, yres, n, k;
    GwyXYZ *xyz;

    xres = gwy_data_field_get_xres(args->field);
    yres = gwy_data_field_get_yres(args->field);
    n = xres*yres;
    if (args->pageno == GWY_PAGE_CHANNELS && !mask) {
        /* We know args->f contains all the theoretical values. */
        g_assert(ctx->n == n);
        gwy_debug("directly copying f[] to result field");
        gwy_assign(gwy_data_field_get_data(result), args->f, n);
    }
    else if (args->pageno == GWY_PAGE_XYZS) {
        surface = gwy_surface_duplicate(args->surface);
        n = gwy_surface_get_npoints(surface);
        g_assert(ctx->n == n);
        xyz = gwy_surface_get_data(surface);
        for (k = 0; k < n; k++)
            xyz[k].z = args->f[k];
        gwy_preview_surface_to_datafield(surface, result, PREVIEW_SIZE, PREVIEW_SIZE, GWY_PREVIEW_SURFACE_FILL);
        g_object_unref(surface);
    }
    else {
        /* Either the input is XYZ or we are using masking.  Just recalculate everything, even values that are in
         * args->f. */
        gwy_debug("recalculating result field the hard way");
        calculate_field(gui->preset, gui->param, result);
    }

    gwy_data_field_data_changed(result);
    gwy_data_field_subtract_fields(diff, field, result);
    if (!diff_excluded && mask) {
        masking = (masking == GWY_MASK_INCLUDE ? GWY_MASK_EXCLUDE : GWY_MASK_INCLUDE);
        gwy_data_field_area_fill_mask(diff, mask, masking, 0, 0, xres, yres, 0.0);
    }
    gwy_data_field_data_changed(diff);
    update_diff_gradient(gui);
}

static void
update_diff_gradient(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *mask = args->mask, *diff = args->diff;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    gboolean diff_excluded = gwy_params_get_boolean(args->params, PARAM_DIFF_EXCLUDED);
    gdouble min, max, dispmin, dispmax;

    if (!diff_excluded && mask) {
        gint xres = gwy_data_field_get_xres(mask);
        gint yres = gwy_data_field_get_yres(mask);
        gwy_data_field_area_get_min_max_mask(diff, mask, masking, 0, 0, xres, yres, &min, &max);
        gwy_data_field_area_get_autorange(diff, mask, masking, 0, 0, xres, yres, &dispmin, &dispmax);
    }
    else {
        gwy_data_field_get_min_max(diff, &min, &max);
        gwy_data_field_get_autorange(diff, &dispmin, &dispmax);
    }

    gwy_set_gradient_for_residuals(gui->diff_gradient, min, max, &dispmin, &dispmax);
    gwy_container_set_double_by_name(gui->data, "/2/base/min", dispmin);
    gwy_container_set_double_by_name(gui->data, "/2/base/max", dispmax);
}

static void
update_fit_state(ModuleGUI *gui)
{
    const gchar *message = "";

    if (gui->state == FIT_SHAPE_ESTIMATE_FAILED)
        message = _("Parameter estimation failed");
    else if (gui->state == FIT_SHAPE_FIT_FAILED || gui->state == FIT_SHAPE_QUICK_FIT_FAILED)
        message = _("Fit failed");
    else if (gui->state == FIT_SHAPE_FIT_CANCELLED)
        message = _("Fit was interrupted");

    set_widget_as_error_message(gui->fit_message);
    gtk_label_set_text(GTK_LABEL(gui->fit_message), message);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_SAVE, gui->state == FIT_SHAPE_FITTED);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_COPY, gui->state == FIT_SHAPE_FITTED);
}

static void
update_fit_results(ModuleGUI *gui, GwyNLFitter *fitter)
{
    ModuleArgs *args = gui->args;
    const FitShapeContext *ctx = gui->ctx;
    gdouble z, rss = 0.0;
    guint k, n = ctx->n, i, nparams;
    GwySIUnit *zunit;
    GwySIValueFormat *vf;
    gboolean is_fitted = (gui->state == FIT_SHAPE_FITTED || gui->state == FIT_SHAPE_QUICK_FITTED);
    const GwyXYZ *xyz = ctx->xyz;
    guchar buf[48];

    if (is_fitted)
        g_return_if_fail(fitter);

    gwy_shape_fit_preset_calculate_z(gui->preset, xyz, args->f, n, gui->param);
    for (k = 0; k < n; k++) {
        z = args->f[k] - xyz[k].z;
        rss += z*z;
    }
    gui->rss = sqrt(rss/n);

    if (is_fitted) {
        nparams = gwy_shape_fit_preset_get_nparams(gui->preset);
        for (i = 0; i < nparams; i++) {
            gui->param_edited[i] = FALSE;
            if (ctx->param_fixed[i])
                gui->param_err[i] = 0.0;
            else
                gui->param_err[i] = gwy_math_nlfit_get_sigma(fitter, i);
        }
    }

    zunit = gwy_data_field_get_si_unit_z(args->field);
    vf = gwy_si_unit_get_format(zunit, GWY_SI_UNIT_FORMAT_VFMARKUP, gui->rss, NULL);

    g_snprintf(buf, sizeof(buf), "%.*f%s%s",
               vf->precision+1, gui->rss/vf->magnitude,
               *vf->units ? " " : "", vf->units);
    gtk_label_set_markup(GTK_LABEL(gui->rss_label), buf);

    gwy_si_unit_value_format_free(vf);

    calculate_secondary_params(gui);
    update_param_table(gui, gui->param, is_fitted ? gui->param_err : NULL);
    update_correl_table(gui, is_fitted ? fitter : NULL);
    update_secondary_table(gui);
}

static void
update_context_data(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyDataField *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    FitShapeContext *ctx = gui->ctx;

    if (args->pageno == GWY_PAGE_CHANNELS)
        gwy_surface_set_from_data_field_mask(args->surface, args->field, mask, masking);

    ctx->n = gwy_surface_get_npoints(args->surface);
    args->f = g_renew(gdouble, args->f, ctx->n);
    ctx->xyz = gwy_surface_get_data_const(args->surface);
}

static void
fit_context_resize_params(FitShapeContext *ctx, guint n_param)
{
    guint i;

    ctx->nparam = n_param;
    ctx->param_fixed = g_renew(gboolean, ctx->param_fixed, n_param);
    for (i = 0; i < n_param; i++)
        ctx->param_fixed[i] = FALSE;
}

static void
fit_context_free(FitShapeContext *ctx)
{
    g_free(ctx->param_fixed);
    gwy_clear(ctx, 1);
}

static GwyNLFitter*
fit(GwyShapeFitPreset *preset, const FitShapeContext *ctx,
    gdouble *param, gdouble *rss,
    GwySetFractionFunc set_fraction, GwySetMessageFunc set_message,
    gboolean quick_fit)
{
    GwyNLFitter *fitter;

    fitter = gwy_shape_fit_preset_create_fitter(preset);
    if (set_fraction || set_message)
        gwy_math_nlfit_set_callbacks(fitter, set_fraction, set_message);

    if (quick_fit)
        gwy_shape_fit_preset_quick_fit(preset, fitter, ctx->xyz, ctx->n, param, ctx->param_fixed, rss);
    else
        gwy_shape_fit_preset_fit(preset, fitter, ctx->xyz, ctx->n, param, ctx->param_fixed, rss);
    gwy_debug("rss from nlfit %g", *rss);

    return fitter;
}

static void
calculate_field(GwyShapeFitPreset *preset, const gdouble *params,
                GwyDataField *field)
{
    GwySurface *surface = gwy_surface_new();

    gwy_surface_set_from_data_field_mask(surface, field, NULL, GWY_MASK_IGNORE);
    gwy_shape_fit_preset_calculate_z(preset,
                                     gwy_surface_get_data_const(surface),
                                     gwy_data_field_get_data(field),
                                     gwy_surface_get_npoints(surface),
                                     params);
    g_object_unref(surface);
}

static void
create_results(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyShapeFitPreset *preset = gui->preset;
    GwyNLFitParamFlags flags;
    GwyResults *results;
    const gchar *name;
    gint power_xy, power_z;
    guint nparams, i;
    const gchar **names;

    GWY_OBJECT_UNREF(gui->results);
    results = gui->results = gwy_results_new();
    gwy_results_add_header(results, N_("Fit Results"));
    gwy_results_add_value_str(results, "file", N_("File"));
    if (args->pageno == GWY_PAGE_XYZS)
        gwy_results_add_value_str(results, "channel", N_("XYZ data"));
    else
        gwy_results_add_value_str(results, "channel", N_("Image"));
    gwy_results_add_format(results, "npts", N_("Number of points"), TRUE,
    /* TRANSLATORS: %{n}i and %{total}i are ids, do NOT translate them. */
                           N_("%{n}i of %{ntotal}i"), NULL);
    gwy_results_add_value_str(results, "func", N_("Fitted function"));
    gwy_results_add_value_z(results, "rss", N_("Mean square difference"));

    gwy_results_add_separator(results);
    gwy_results_add_header(results, N_("Parameters"));
    nparams = gwy_shape_fit_preset_get_nparams(preset);
    names = g_new(const gchar*, nparams);
    for (i = 0; i < nparams; i++) {
        names[i] = name = gwy_shape_fit_preset_get_param_name(preset, i);
        flags = gwy_shape_fit_preset_get_param_flags(preset, i);
        power_xy = gwy_shape_fit_preset_get_param_power_xy(preset, i);
        power_z = gwy_shape_fit_preset_get_param_power_z(preset, i);
        gwy_results_add_value(results, name, "",
                              "symbol", name, "is-fitting-param", TRUE,
                              "power-x", power_xy, "power-z", power_z,
                              "is-angle", (flags & GWY_NLFIT_PARAM_ANGLE),
                              NULL);
    }

    gwy_results_add_separator(results);
    gwy_results_add_covariance_matrixv(results, "covar", N_("Correlation Matrix"), nparams, names);
    g_free(names);

    nparams = gwy_shape_fit_preset_get_nsecondary(preset);
    if (!nparams)
        return;

    gwy_results_add_separator(results);
    gwy_results_add_header(results, N_("Derived Quantities"));
    for (i = 0; i < nparams; i++) {
        name = gwy_shape_fit_preset_get_secondary_name(preset, i);
        flags = gwy_shape_fit_preset_get_secondary_flags(preset, i);
        power_xy = gwy_shape_fit_preset_get_secondary_power_xy(preset, i);
        power_z = gwy_shape_fit_preset_get_secondary_power_z(preset, i);
        gwy_results_add_value(results, name, "",
                              "symbol", name,
                              "power-x", power_xy, "power-z", power_z,
                              "is-angle", (flags & GWY_NLFIT_PARAM_ANGLE),
                              NULL);
    }
}

static void
fill_results(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyShapeFitPreset *preset = gui->preset;
    const gboolean *param_fixed = gui->ctx->param_fixed;
    GwyResults *results = gui->results;
    GwySIUnit *xyunit, *zunit;
    guint n, i, nparams;
    const gchar *name;
    gdouble param, err;

    if (args->pageno == GWY_PAGE_XYZS) {
        xyunit = gwy_surface_get_si_unit_xy(args->surface);
        zunit = gwy_surface_get_si_unit_z(args->surface);
        n = gwy_surface_get_npoints(args->surface);
        gwy_results_fill_xyz(results, "channel", gui->args_data, gui->id);
    }
    else {
        xyunit = gwy_data_field_get_si_unit_xy(args->field);
        zunit = gwy_data_field_get_si_unit_z(args->field);
        n = gwy_data_field_get_xres(args->field)*gwy_data_field_get_yres(args->field);
        gwy_results_fill_channel(results, "channel", gui->args_data, gui->id);
    }
    gwy_results_set_unit(results, "x", xyunit);
    gwy_results_set_unit(results, "y", xyunit);
    gwy_results_set_unit(results, "z", zunit);

    gwy_results_fill_filename(results, "file", gui->args_data);
    gwy_results_fill_values(results,
                            "func", gwy_resource_get_name(GWY_RESOURCE(preset)),
                            "rss", gui->rss,
                            NULL);
    gwy_results_fill_format(results, "npts",
                            "n", gui->ctx->n,
                            "ntotal", n,
                            NULL);

    nparams = gwy_shape_fit_preset_get_nparams(preset);
    for (i = 0; i < nparams; i++) {
        name = gwy_shape_fit_preset_get_param_name(preset, i);
        param = gui->param[i];
        err = gui->param_err[i];

        if (param_fixed[i])
            gwy_results_fill_values(results, name, param, NULL);
        else
            gwy_results_fill_values_with_errors(results, name, param, err, NULL);
    }

    gwy_results_fill_covariance_matrix(results, "covar", param_fixed, gui->correl);

    nparams = gwy_shape_fit_preset_get_nsecondary(preset);
    for (i = 0; i < nparams; i++) {
        name = gwy_shape_fit_preset_get_secondary_name(preset, i);
        param = gui->secondary[i];
        err = gui->secondary_err[i];
        gwy_results_fill_values_with_errors(results, name, param, err, NULL);
    }
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    const gchar *function = gwy_params_get_string(params, PARAM_FUNCTION);
    GwyShapeFitPreset *preset = gwy_inventory_get_item(gwy_shape_fit_presets(), function);

    if (!args->same_units && gwy_shape_fit_preset_needs_same_units(preset))
        gwy_params_reset(params, PARAM_FUNCTION);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
