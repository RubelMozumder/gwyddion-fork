/*
 *  $Id: relate.c 23788 2021-05-26 12:14:14Z yeti-dn $
 *  Copyright (C) 2018-2021 David Necas (Yeti).
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
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwygraph.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    MAX_PARAMS = 3,
    MAX_PLOT_DATA = 16384,
    PLOT_FUNC_SAMPLES = 241,
};

enum {
    PARAM_FUNC,
    PARAM_MASKING,
    PARAM_OTHER_IMAGE,
    PARAM_TARGET_GRAPH,
    PARAM_REPORT_STYLE,
    LABEL_FORMULA,
    INFO_RSS,
    WIDGET_FIT_RESULT,
};

typedef enum {
    RELATE_FUNCTION_PROP      = 0,
    RELATE_FUNCTION_OFFSET    = 1,
    RELATE_FUNCTION_LINEAR    = 2,
    RELATE_FUNCTION_SQUARE    = 3,
    RELATE_FUNCTION_PARABOLIC = 4,
    RELATE_FUNCTION_UPOWER    = 5,
    RELATE_FUNCTION_LOG       = 6,
} RelateFunction;

typedef gdouble (*RelateEvalFunc)(gdouble z1, const gdouble *params);
typedef void (*RelateMakeLSMFunc)(const gdouble *z1, const gdouble *z2,
                                  guint n,
                                  gdouble *matrix, gdouble *rhs);

typedef struct {
    const char *name;
    gint power_x;
    gint power_y;
} GwyNLFitParam;

typedef struct {
    RelateFunction id;
    const gchar *name;
    const gchar *formula;
    const GwyNLFitParam *paraminfo;
    guint nparams;
    RelateEvalFunc func;
    RelateMakeLSMFunc make_lsm;
} RelateFuncInfo;

typedef struct {
    const gdouble *xdata;
    const gdouble *ydata;
    RelateEvalFunc func;
} RelateNLFittingData;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyGraphModel *gmodel;
    gdouble *xdata;
    gdouble *ydata;
    guint ndata;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkWidget *fit_table;
    GtkWidget *param_name[MAX_PARAMS];
    GtkWidget *param_equal[MAX_PARAMS];
    GtkWidget *param_value[MAX_PARAMS];
    GtkWidget *param_pm[MAX_PARAMS];
    GtkWidget *param_error[MAX_PARAMS];
    GtkWidget *rss_label;

    GwyResults *results;
    GwyContainer *args_data;
    gint id;

    gdouble param[MAX_PARAMS];
    gdouble error[MAX_PARAMS];
    gdouble rss;
    guint ndata;
} ModuleGUI;

#define DECLARE_FIT_FUNC(name) \
    static gdouble relate_func_##name(gdouble z1, const gdouble *param); \
    static void relate_lsm_##name(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)

DECLARE_FIT_FUNC(prop);
DECLARE_FIT_FUNC(offset);
DECLARE_FIT_FUNC(linear);
DECLARE_FIT_FUNC(square);
DECLARE_FIT_FUNC(parabolic);
DECLARE_FIT_FUNC(upower);
DECLARE_FIT_FUNC(log);

static gboolean              module_register        (void);
static GwyParamDef*          define_module_params   (void);
static void                  relate                 (GwyContainer *data,
                                                     GwyRunType runtype);
static GwyDialogOutcome      run_gui                (ModuleArgs *args,
                                                     GwyContainer *data,
                                                     gint id);
static GtkWidget*            create_fit_table       (gpointer user_data);
static void                  param_changed          (ModuleGUI *gui,
                                                     gint id);
static void                  preview                (gpointer user_data);
static gboolean              other_image_filter     (GwyContainer *data,
                                                     gint id,
                                                     gpointer user_data);
static const RelateFuncInfo* find_relate_func       (RelateFunction id);
static void                  replot_data            (ModuleGUI *gui);
static void                  recalculate            (ModuleGUI *gui);
static void                  update_fit_result_table(ModuleGUI *gui);
static void                  fill_fit_result_table  (ModuleGUI *gui);
static void                  update_results         (ModuleGUI *gui);
static void                  fill_results           (ModuleGUI *gui);
static void                  plot_fit               (ModuleGUI *gui);
static gdouble               nlfitter_fit_func      (gdouble x,
                                                     gint nparam,
                                                     const gdouble *param,
                                                     gpointer user_data,
                                                     gboolean *success);
static void                  shuffle_array_stable   (gdouble *a,
                                                     guint n,
                                                     guint nhead);

static const GwyNLFitParam params_prop[] = {
    { "a", -1, 1 },
};

static const GwyNLFitParam params_offset[] = {
    { "b", 0, 1 },
};

static const GwyNLFitParam params_linear[] = {
    { "a", -1, 1 },
    { "b",  0, 1 },
};

static const GwyNLFitParam params_square[] = {
    { "a", -2, 1 },
};

static const GwyNLFitParam params_parabolic[] = {
    { "a", -2, 1 },
    { "b", -1, 1 },
    { "c",  0, 1 },
};

static const GwyNLFitParam params_upower[] = {
    { "p", 0, 0 },
    { "q", 0, 0 },
};

static const GwyNLFitParam params_log[] = {
    { "p", 0, 0 },
    { "q", 0, 0 },
};

static const RelateFuncInfo func_info[] = {
    {
        RELATE_FUNCTION_PROP, N_("Proportion"),
        "<i>z</i><sub>2</sub> = <i>az</i><sub>1</sub>",
        params_prop, G_N_ELEMENTS(params_prop), relate_func_prop, relate_lsm_prop,
    },
    {
        RELATE_FUNCTION_OFFSET, N_("Offset"),
        "<i>z</i><sub>2</sub> = <i>z</i><sub>1</sub> + <i>b</i>",
        params_offset, G_N_ELEMENTS(params_offset), relate_func_offset, relate_lsm_offset,
    },
    {
        RELATE_FUNCTION_LINEAR, N_("Linear"),
        "<i>z</i><sub>2</sub> = <i>az</i><sub>1</sub> + <i>b</i>",
        params_linear, G_N_ELEMENTS(params_linear), relate_func_linear, relate_lsm_linear,
    },
    {
        RELATE_FUNCTION_SQUARE, N_("Square"),
        "<i>z</i><sub>2</sub> = <i>az</i><sub>1</sub><sup>2</sup>",
        params_square, G_N_ELEMENTS(params_square), relate_func_square, relate_lsm_square,
    },
    {
        RELATE_FUNCTION_PARABOLIC, N_("Parabolic"),
        "<i>z</i><sub>2</sub> = <i>az</i><sub>1</sub><sup>2</sup> + <i>bz</i><sub>1</sub> + <i>c</i>",
        params_parabolic, G_N_ELEMENTS(params_parabolic), relate_func_parabolic, relate_lsm_parabolic,
    },
    {
        RELATE_FUNCTION_UPOWER, N_("Power"),
        "ln <i>z</i><sub>2</sub> = <i>p</i>ln <i>z</i><sub>1</sub> + <i>q</i>",
        params_upower, G_N_ELEMENTS(params_upower), relate_func_upower, relate_lsm_upower,
    },
    {
        RELATE_FUNCTION_LOG, N_("Logarithm"),
        "<i>z</i><sub>2</sub> = <i>p</i>ln |<i>z</i><sub>1</sub>| + <i>q</i>",
        params_log, G_N_ELEMENTS(params_log), relate_func_log, relate_lsm_log,
    },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Plots one image data as a function of another and finds relations."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas",
    "2018",
};

GWY_MODULE_QUERY2(module_info, relate)

static gboolean
module_register(void)
{
    gwy_process_func_register("relate",
                              (GwyProcessFunc)&relate,
                              N_("/_Multidata/_Relation..."),
                              GWY_STOCK_IMAGE_RELATION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Find simple relations between data"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum *functions = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    functions = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(func_info), func_info, sizeof(RelateFuncInfo),
                                          G_STRUCT_OFFSET(RelateFuncInfo, name), G_STRUCT_OFFSET(RelateFuncInfo, id));

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_FUNC, "func", _("_Function type"),
                              functions, G_N_ELEMENTS(func_info), RELATE_FUNCTION_PROP);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_image_id(paramdef, PARAM_OTHER_IMAGE, "other_image", _("Second _image"));
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Parameters"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    return paramdef;
}

static void
relate(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyAppDataId target_graph_id;
    ModuleArgs args;
    gint id, n;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.gmodel = gwy_graph_model_new();
    args.params = gwy_params_new_from_settings(define_module_params());
    /* TODO: The correct setup is
     * 1. Load the second image from settings, with filtering.
     * 2. After a second image is chosen (if any available), filter graphs.
     * Only this way the target graph setting can really be preserved.  Leave it to the image chooser ‘select
     * anything’ to select something.  */
    n = gwy_data_field_get_xres(args.field) * gwy_data_field_get_yres(args.field);
    args.xdata = g_new(gdouble, 2*n);
    args.ydata = args.xdata + n;

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        goto end;

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &target_graph_id, 1);

end:
    g_free(args.xdata);
    g_object_unref(args.params);
    g_object_unref(args.gmodel);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    ModuleGUI gui;
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.args_data = data;
    gui.id = id;

    gui.dialog = gwy_dialog_new(_("Relate"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    g_object_set(args->gmodel,
                 "axis-label-bottom", "z<sub>1</sub>",
                 "axis-label-left", "z<sub>2</sub>",
                 NULL);
    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, 480, 360);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_OTHER_IMAGE);
    gwy_param_table_data_id_set_filter(table, PARAM_OTHER_IMAGE, other_image_filter, args->field, NULL);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);
    if (args->mask)
        gwy_param_table_append_combo(table, PARAM_MASKING);

    gwy_param_table_append_header(table, -1, _("Function"));
    gwy_param_table_append_combo(table, PARAM_FUNC);
    gwy_param_table_append_message(table, LABEL_FORMULA, NULL);

    gwy_param_table_append_header(table, -1, _("Fit Results"));
    gwy_param_table_append_foreign(table, WIDGET_FIT_RESULT, create_fit_table, &gui, NULL);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_info(table, INFO_RSS, _("Mean square difference"));
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    /* TODO: Set results when we have it. */

    gtk_box_pack_end(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    return gwy_dialog_run(dialog);
}

static GtkWidget*
create_fit_table(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    gui->fit_table = gtk_table_new(1, 5, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(gui->fit_table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(gui->fit_table), 8);
    return gui->fit_table;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_FUNC) {
        RelateFunction func = gwy_params_get_enum(args->params, PARAM_FUNC);
        const RelateFuncInfo *finfo = find_relate_func(func);

        gwy_param_table_set_label(table, LABEL_FORMULA, finfo->formula);
        update_fit_result_table(gui);
        update_results(gui);
    }
    if (id < 0 || id == PARAM_OTHER_IMAGE) {
        gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK,
                                          !gwy_params_data_id_is_none(params, PARAM_OTHER_IMAGE));
    }
    if (id != PARAM_REPORT_STYLE && id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    replot_data(gui);
    recalculate(gui);
    gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static gboolean
other_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *otherfield, *field = (GwyDataField*)user_data;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    if (otherfield == field)
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield,
                                               GWY_DATA_COMPATIBILITY_RES
                                               | GWY_DATA_COMPATIBILITY_REAL
                                               | GWY_DATA_COMPATIBILITY_LATERAL);
}

static const RelateFuncInfo*
find_relate_func(RelateFunction id)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(func_info); i++) {
        if (func_info[i].id == id)
            return func_info + i;
    }
    return NULL;
}

static void
replot_data(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyGraphModel *gmodel = args->gmodel;
    GwyDataField *field = args->field, *mask = args->mask;
    GwyDataField *otherfield = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    GwyGraphCurveModel *gcmodel;
    const gdouble *d1, *d2, *m;
    gdouble *xdata = args->xdata, *ydata = args->ydata;
    gint nc, n, i, ndata;

    nc = gwy_graph_model_get_n_curves(gmodel);
    if (nc > 0)
        gcmodel = gwy_graph_model_get_curve(gmodel, 0);
    else {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_POINTS,
                     "point-type", GWY_GRAPH_POINT_SQUARE,
                     "point-size", 1,
                     "color", gwy_graph_get_preset_color(0),
                     "description", _("Data"),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }

    if (!otherfield)
        return;

    g_object_set(gmodel,
                 "si-unit-x", gwy_data_field_get_si_unit_z(field),
                 "si-unit-y", gwy_data_field_get_si_unit_z(otherfield),
                 NULL);

    n = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
    d1 = gwy_data_field_get_data_const(field);
    d2 = gwy_data_field_get_data_const(otherfield);
    if (!mask) {
        gwy_assign(xdata, d1, n);
        gwy_assign(ydata, d2, n);
        ndata = n;
    }
    else {
        m = gwy_data_field_get_data_const(mask);
        ndata = 0;
        for (i = 0; i < n; i++) {
            if ((masking == GWY_MASK_INCLUDE && m[i] >= 1.0) || (masking == GWY_MASK_EXCLUDE && m[i] <= 0.0)) {
                xdata[ndata] = d1[i];
                ydata[ndata] = d2[i];
                ndata++;
            }
        }
    }

    args->ndata = ndata;
    if (ndata > MAX_PLOT_DATA) {
        shuffle_array_stable(xdata, ndata, MAX_PLOT_DATA);
        shuffle_array_stable(ydata, ndata, MAX_PLOT_DATA);
        ndata = MAX_PLOT_DATA;
    }
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
}

static void
recalculate(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    RelateFunction func = gwy_params_get_enum(args->params, PARAM_FUNC);
    const RelateFuncInfo *finfo = find_relate_func(func);
    guint i, nparam = finfo->nparams;
    GwyNLFitter *fitter;
    gdouble *matrix;
    gboolean ok = TRUE;
    gdouble rss;

    if (gwy_params_data_id_is_none(args->params, PARAM_OTHER_IMAGE) || nparam >= args->ndata)
        return;

    /* Linear fitting.  For simple relations this is the same as the final fit but for transformed we take it just as
     * an estimate. */
    matrix = g_new0(gdouble, nparam*(nparam+1)/2);
    gwy_clear(gui->param, MAX_PARAMS);
    finfo->make_lsm(args->xdata, args->ydata, args->ndata, matrix, gui->param);
    if (gwy_math_choleski_decompose(nparam, matrix))
        gwy_math_choleski_solve(nparam, matrix, gui->param);
    else
        ok = FALSE;
    g_free(matrix);

    /* Non-linear fitting. */
    if (ok) {
        fitter = gwy_math_nlfit_new(nlfitter_fit_func, NULL);
        rss = gwy_math_nlfit_fit(fitter, args->ndata, args->xdata, args->ydata, nparam, gui->param, finfo->func);
        if (rss >= 0.0) {
            gui->rss = sqrt(rss/(args->ndata - nparam));
            for (i = 0; i < nparam; i++)
                gui->error[i] = gwy_math_nlfit_get_sigma(fitter, i);
        }
        else
            ok = FALSE;
        gwy_math_nlfit_free(fitter);
    }

    if (!ok) {
        gwy_clear(gui->param, nparam);
        gwy_clear(gui->error, nparam);
        gui->rss = 0.0;
    }

    fill_results(gui);
    fill_fit_result_table(gui);
    plot_fit(gui);

    if (!ok)
        g_warning("Fit failed!");
}

static void
update_fit_result_table(ModuleGUI *gui)
{
    RelateFunction func = gwy_params_get_enum(gui->args->params, PARAM_FUNC);
    const RelateFuncInfo *finfo = find_relate_func(func);
    GtkTable *table = GTK_TABLE(gui->fit_table);
    GtkWidget *label;
    guint i, nparam = finfo->nparams;

    for (i = 0; i < MAX_PARAMS && gui->param_name[i]; i++) {
        gtk_widget_destroy(gui->param_name[i]);
        gtk_widget_destroy(gui->param_equal[i]);
        gtk_widget_destroy(gui->param_value[i]);
        gtk_widget_destroy(gui->param_pm[i]);
        gtk_widget_destroy(gui->param_error[i]);
    }
    gwy_clear(gui->param_name, i);
    gwy_clear(gui->param_equal, i);
    gwy_clear(gui->param_value, i);
    gwy_clear(gui->param_pm, i);
    gwy_clear(gui->param_error, i);

    gtk_table_resize(table, nparam, 5);
    for (i = 0; i < nparam; i++) {
        label = gui->param_name[i] = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), finfo->paraminfo[i].name);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(table, label, 0, 1, i, i+1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        label = gui->param_equal[i] = gtk_label_new("=");
        gtk_table_attach(table, label, 1, 2, i, i+1, GTK_FILL, 0, 0, 0);

        label = gui->param_value[i] = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 2, 3, i, i+1, GTK_FILL, 0, 0, 0);

        label = gui->param_pm[i] = gtk_label_new("±");
        gtk_table_attach(table, label, 3, 4, i, i+1, GTK_FILL, 0, 0, 0);

        label = gui->param_error[i] = gtk_label_new(NULL);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(table, label, 4, 5, i, i+1, GTK_FILL, 0, 0, 0);
    }
    gtk_widget_show_all(gui->fit_table);
}

static void
fill_fit_result_table(ModuleGUI *gui)
{
    const GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_VFMARKUP;
    ModuleArgs *args = gui->args;
    RelateFunction func = gwy_params_get_enum(args->params, PARAM_FUNC);
    const RelateFuncInfo *finfo = find_relate_func(func);
    guint i, nparam = finfo->nparams;
    GwySIUnit *unit, *xunit, *yunit;
    GwySIValueFormat *vf = NULL;
    gchar buf[80];

    xunit = gwy_data_field_get_si_unit_z(args->field);
    yunit = gwy_data_field_get_si_unit_z(gwy_params_get_image(args->params, PARAM_OTHER_IMAGE));
    unit = gwy_si_unit_new(NULL);
    for (i = 0; i < nparam; i++) {
        gwy_si_unit_power_multiply(xunit, finfo->paraminfo[i].power_x, yunit, finfo->paraminfo[i].power_y, unit);
        vf = gwy_si_unit_get_format(unit, style, gui->param[i], vf);
        vf->precision += 3;
        g_snprintf(buf, sizeof(buf), "%.*f%s%s",
                   vf->precision, gui->param[i]/vf->magnitude, *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(GTK_LABEL(gui->param_value[i]), buf);

        vf = gwy_si_unit_get_format(unit, style, gui->error[i], vf);
        g_snprintf(buf, sizeof(buf), "%.*f%s%s",
                   vf->precision, gui->error[i]/vf->magnitude, *vf->units ? " " : "", vf->units);
        gtk_label_set_markup(GTK_LABEL(gui->param_error[i]), buf);
    }

    vf = gwy_si_unit_get_format(yunit, style, gui->rss, vf);
    g_snprintf(buf, sizeof(buf), "%.*f %s",
               vf->precision, gui->rss/vf->magnitude, vf->units);
    gwy_param_table_info_set_valuestr(gui->table, INFO_RSS, buf);

    gwy_si_unit_value_format_free(vf);
    g_object_unref(unit);
}

static void
update_results(ModuleGUI *gui)
{
    RelateFunction func = gwy_params_get_enum(gui->args->params, PARAM_FUNC);
    const RelateFuncInfo *finfo = find_relate_func(func);
    guint i, nparam = finfo->nparams;
    GwyResults *results;

    GWY_OBJECT_UNREF(gui->results);
    results = gui->results = gwy_results_new();
    gwy_results_add_header(results, N_("Fit Results"));
    gwy_results_add_value_str(results, "file", N_("File"));
    gwy_results_add_value_str(results, "channel1", N_("First image"));
    gwy_results_add_value_str(results, "channel2", N_("Second image"));
    /* TRANSLATORS: %{n}i and %{total}i are ids, do NOT translate them. */
    gwy_results_add_format(results, "npts", N_("Number of points"), TRUE, N_("%{n}i of %{ntotal}i"), NULL);
    gwy_results_add_value_str(results, "func", N_("Fitted function"));
    gwy_results_add_value_z(results, "rss", N_("Mean square difference"));

    gwy_results_add_separator(results);
    gwy_results_add_header(results, N_("Parameters"));

    for (i = 0; i < nparam; i++) {
        gwy_results_add_value(results, finfo->paraminfo[i].name, "",
                              "symbol", finfo->paraminfo[i].name,
                              "is-fitting-param", TRUE,
                              "power-x", finfo->paraminfo[i].power_x,
                              "power-y", finfo->paraminfo[i].power_y,
                              NULL);
    }

    gwy_param_table_report_set_results(gui->table, PARAM_REPORT_STYLE, results);
}

static void
fill_results(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    RelateFunction func = gwy_params_get_enum(args->params, PARAM_FUNC);
    const RelateFuncInfo *finfo = find_relate_func(func);
    GwyDataField *field = args->field, *otherfield = gwy_params_get_image(args->params, PARAM_OTHER_IMAGE);
    GwyResults *results = gui->results;
    const GwyAppDataId dataid = gwy_params_get_data_id(args->params, PARAM_OTHER_IMAGE);
    guint n, i;

    n = gwy_data_field_get_xres(field) * gwy_data_field_get_yres(field);
    gwy_results_fill_channel(results, "channel1", gui->args_data, gui->id);
    gwy_results_fill_channel(results, "channel2", gwy_app_data_browser_get(dataid.datano), dataid.id);
    gwy_results_set_unit(results, "x", gwy_data_field_get_si_unit_z(field));
    gwy_results_set_unit(results, "y", gwy_data_field_get_si_unit_z(otherfield));
    gwy_results_set_unit(results, "z", gwy_data_field_get_si_unit_z(otherfield));

    gwy_results_fill_filename(results, "file", gui->args_data);
    gwy_results_fill_values(results,
                            "func", finfo->name,
                            "rss", gui->rss,
                            NULL);
    gwy_results_fill_format(results, "npts",
                            "n", args->ndata,
                            "ntotal", n,
                            NULL);

    for (i = 0; i < finfo->nparams; i++)
        gwy_results_fill_values_with_errors(results, finfo->paraminfo[i].name, gui->param[i], gui->error[i], NULL);
}

static void
plot_fit(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    RelateFunction func = gwy_params_get_enum(args->params, PARAM_FUNC);
    GwyDataField *field = args->field, *mask = args->mask;
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mask);
    const RelateFuncInfo *finfo = find_relate_func(func);
    GwyGraphModel *gmodel = args->gmodel;
    GwyGraphCurveModel *gcmodel;
    gdouble min, max;
    gdouble *xdata, *ydata;
    RelateEvalFunc evalfunc;
    gint i, ndata = args->ndata, nc, xres, yres;

    nc = gwy_graph_model_get_n_curves(gmodel);
    if (nc < 2) {
        gcmodel = gwy_graph_curve_model_new();
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(1),
                     "description", _("Fit"),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    else
        gcmodel = gwy_graph_model_get_curve(gmodel, 1);

    ndata = PLOT_FUNC_SAMPLES;
    xdata = g_new(gdouble, 2*ndata);
    ydata = xdata + ndata;
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    gwy_data_field_area_get_min_max_mask(field, mask, masking, 0, 0, xres, yres, &min, &max);

    evalfunc = finfo->func;
    for (i = 0; i < ndata; i++) {
        gdouble t = i/(ndata - 1.0);

        xdata[i] = t*max + (1.0 - t)*min;
        ydata[i] = evalfunc(xdata[i], gui->param);
    }
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
    g_free(xdata);
}

/* Deterministically shuffle the beginning an array.  Arrays of the same size is always shuffled the same way. */
static void
shuffle_array_stable(gdouble *a, guint n, guint nhead)
{
    GRand *rng = g_rand_new_with_seed(42);
    guint i, nshuf = MIN(n, nhead);

    for (i = 0; i < nshuf; i++) {
        guint j = g_rand_int_range(rng, 0, n);

        GWY_SWAP(gdouble, a[i], a[j]);
    }

    g_rand_free(rng);
}

static gdouble
nlfitter_fit_func(gdouble x,
                  G_GNUC_UNUSED gint nparam,
                  const gdouble *param,
                  gpointer user_data,
                  gboolean *success)
{
    RelateEvalFunc func = (RelateEvalFunc)user_data;

    *success = TRUE;
    return func(x, param);
}

static gdouble
relate_func_prop(gdouble z1, const gdouble *param)
{
    return z1*param[0];
}

static void
relate_lsm_prop(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i];

        matrix[0] += x*x;
        rhs[0] += y*x;
    }
}

static gdouble
relate_func_offset(gdouble z1, const gdouble *param)
{
    return z1 + param[0];
}

static void
relate_lsm_offset(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i];

        matrix[0] += 1.0;
        rhs[0] += y - x;
    }
}

static gdouble
relate_func_linear(gdouble z1, const gdouble *param)
{
    return z1*param[0] + param[1];
}

static void
relate_lsm_linear(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i];

        matrix[0] += x*x;
        matrix[1] += x;
        matrix[2] += 1.0;
        rhs[0] += y*x;
        rhs[1] += y;
    }
}

static gdouble
relate_func_square(gdouble z1, const gdouble *param)
{
    return z1*z1*param[0];
}

static void
relate_lsm_square(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i];
        gdouble xx = x*x;

        matrix[0] += xx*xx;
        rhs[0] += y*xx;
    }
}

static gdouble
relate_func_parabolic(gdouble z1, const gdouble *param)
{
    return z1*(z1*param[0] + param[1]) + param[2];
}

static void
relate_lsm_parabolic(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i];
        gdouble xx = x*x;

        matrix[0] += xx*xx;
        matrix[1] += xx*x;
        matrix[2] += xx;
        matrix[3] += xx;
        matrix[4] += x;
        matrix[5] += 1.0;
        rhs[0] += y*xx;
        rhs[1] += y*x;
        rhs[2] += y;
    }
}

static gdouble
relate_func_upower(gdouble z1, const gdouble *param)
{
    if (z1 == 0.0)
        return 0.0;
    if (z1 < 0.0)
        return -pow(fabs(z1), param[0]) * exp(param[1]);
    return pow(fabs(z1), param[0]) * exp(param[1]);
}

static void
relate_lsm_upower(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i], w, lx, ly;

        if (x == 0.0 || y == 0.0)
            continue;

        w = fabs(x) + fabs(y);
        lx = log(fabs(x));
        ly = log(fabs(y));
        matrix[0] += lx*lx*w;
        matrix[1] += lx*w;
        matrix[2] += w;
        rhs[0] += ly*lx*w;
        rhs[1] += ly*w;
    }
}

static gdouble
relate_func_log(gdouble z1, const gdouble *param)
{
    if (z1 == 0.0)
        return 0.0;
    return param[0]*log(fabs(z1)) + param[1];
}

static void
relate_lsm_log(const gdouble *z1, const gdouble *z2, guint n, gdouble *matrix, gdouble *rhs)
{
    guint i;

    for (i = 0; i < n; i++) {
        gdouble x = z1[i], y = z2[i], lx;

        if (x == 0.0)
            continue;

        lx = log(fabs(x));
        matrix[0] += lx*lx;
        matrix[1] += lx;
        matrix[2] += 1.0;
        rhs[0] += y*lx;
        rhs[1] += y;
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
