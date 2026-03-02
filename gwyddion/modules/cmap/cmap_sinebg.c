/*
 *  $Id: cmap_sinebg.c 24486 2021-11-07 21:05:10Z yeti-dn $
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/lawn.h>
#include <libprocess/stats.h>
#include <libprocess/correct.h>
#include <libprocess/gwyprocess.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwygraph.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
};

enum {
    PARAM_RANGE_FROM,
    PARAM_RANGE_TO,
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_SEGMENT,
    PARAM_ENABLE_SEGMENT,
    PARAM_XPOS,
    PARAM_YPOS,
};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyLawn *result;
    GwyDataField *field;
    gint nsegments;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwySelection *selection;
    GwySelection *graph_selection;
    GwyGraphModel *gmodel;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             sinebg                  (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args,
                                                 GtkWindow *window);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             preview                 (gpointer user_data);
static void             set_selection           (ModuleGUI *gui);
static void             point_selection_changed (ModuleGUI *gui,
                                                 gint id,
                                                 GwySelection *selection);
static void             extract_one_curve       (GwyLawn *lawn,
                                                 GwyGraphCurveModel *gcmodel,
                                                 gint col,
                                                 gint row,
                                                 GwyParams *params);
static void             convert_one_curve       (GwyGraphCurveModel *gcmodel,
                                                 GwyParams *params,
                                                 gdouble *fitparams,
                                                 gint nsegments, 
                                                 const gint *segments);
static void             do_sinebg               (const gdouble *xdata,
                                                 const gdouble *ydata,
                                                 gdouble *nydata,
                                                 gint ndata,
                                                 const gint *segments,
                                                 gint segment, 
                                                 gboolean segment_enabled,
                                                 gdouble from,
                                                 gdouble to,
                                                 gboolean subtract,
                                                 gdouble *retparam);
static gdouble          func_sine               (gdouble x,
                                                 gint n_param,
                                                 const gdouble *param,
                                                 G_GNUC_UNUSED gpointer user_data,
                                                 gboolean *fres);

static void             update_graph_model_props(GwyGraphModel *gmodel,
                                                 ModuleArgs *args);
static void             sanitise_params         (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Remove sine background from curves."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_sinebg)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_sinebg",
                                (GwyCurveMapFunc)&sinebg,
                                N_("/Remove _Sine Background..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Remove sine background from all curves"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Abscissa"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Ordinate"));
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_FROM, "from", _("_From"), 0.0, 1.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_RANGE_TO, "to", _("_To"), 0.0, 1.0, 1.0);
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT, "segment", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_SEGMENT, "enable_segment", NULL, FALSE);

    return paramdef;
}

static void
sinebg(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));

    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    args.field = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                    gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), TRUE);
    gwy_data_field_set_xoffset(args.field, gwy_lawn_get_xoffset(lawn));
    gwy_data_field_set_yoffset(args.field, gwy_lawn_get_yoffset(lawn));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.field), gwy_lawn_get_si_unit_xy(lawn));

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        execute(&args, gwy_app_find_window_for_curve_map(data, id));
    }

    /* TODO: Add log, maybe more. */

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.field);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align, *area;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field;
    GwyDialogOutcome outcome;
    GwyGraphCurveModel *gcmodel;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    field = gwy_container_get_object(data, gwy_app_get_lawn_preview_key_for_id(id));
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Remove Sine Background"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    vlayer = g_object_new(g_type_from_name("GwyLayerPoint"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(dataview), vlayer);
    gui.selection = gwy_vector_layer_ensure_selection(vlayer);
    set_selection(&gui);

    gui.gmodel = gwy_graph_model_new();
    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(0),
                 "description", g_strdup(_("data")),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);


    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", gwy_graph_get_preset_color(1),
                 "description", g_strdup(_("fit")),
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);



    graph = gwy_graph_new(gui.gmodel);
    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gwy_graph_area_set_selection_editable(GWY_GRAPH_AREA(area), FALSE);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);


    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    gwy_param_table_append_slider(table, PARAM_RANGE_FROM);
    gwy_param_table_slider_set_factor(table, PARAM_RANGE_FROM, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_RANGE_FROM, "%");
    gwy_param_table_append_slider(table, PARAM_RANGE_TO);
    gwy_param_table_slider_set_factor(table, PARAM_RANGE_TO, 100.0);
    gwy_param_table_set_unitstr(table, PARAM_RANGE_TO, "%");
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT, args->lawn);
        gwy_param_table_add_enabler(table, PARAM_ENABLE_SEGMENT, PARAM_SEGMENT);
    }

    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.gmodel);
    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id)
{
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
set_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = (col + 0.5)*gwy_lawn_get_dx(args->lawn);
    xy[1] = (row + 0.5)*gwy_lawn_get_dy(args->lawn);
    gwy_selection_set_object(gui->selection, 0, xy);
}

static void
point_selection_changed(ModuleGUI *gui, gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    gwy_selection_get_object(selection, id, xy);
    i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
    gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
    i = GWY_ROUND(floor(xy[1]/gwy_lawn_get_dy(lawn)));
    gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));

    gwy_param_table_param_changed(gui->table, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table, PARAM_YPOS);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);

    gdouble sel[2];
    GwyGraphCurveModel *gc;
    gdouble xfrom, xto;
    gdouble *xfit, *yfit, *fitparams;
    gint i, nfit = 100;
    gboolean fres;

    fitparams = g_new(gdouble, 4);

    gc = gwy_graph_model_get_curve(gui->gmodel, 0);
    extract_one_curve(args->lawn, gc, col, row, params);
    convert_one_curve(gc, params, fitparams, args->nsegments,
                      gwy_lawn_get_segments(args->lawn, col, row, NULL));
    update_graph_model_props(gui->gmodel, args);

    gwy_graph_curve_model_get_x_range(gc, &xfrom, &xto);
    sel[0] = xfrom + from*(xto-xfrom);
    sel[1] = xfrom + to*(xto-xfrom);

    gwy_selection_set_data(gui->graph_selection, 1, sel);

    gc = gwy_graph_model_get_curve(gui->gmodel, 1);
    xfit = g_new(gdouble, nfit);
    yfit = g_new(gdouble, nfit);

    for (i = 0; i < nfit; i++) {
        xfit[i] = xfrom + i*(xto-xfrom)/nfit;
        yfit[i] = func_sine(xfit[i], 4, fitparams, NULL, &fres);
    }
    gwy_graph_curve_model_set_data(gc, xfit, yfit, nfit);

    g_free(xfit);
    g_free(yfit);

    g_free(fitparams);
}

static void
execute(ModuleArgs *args, GtkWindow *window)
{
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;

    GwyLawn *lawn = args->lawn;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    GArray *newcdata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    const gdouble *cdx, *cdy;
    gdouble *ncdy;
    gint ndata, k, col, row;

    gint nsegments = gwy_lawn_get_n_segments(lawn);
    gint *segments = g_new(gint, 2*nsegments);

    gwy_app_wait_start(window, _("Fitting..."));

    for (k = 0; k < xres*yres; k++) {
        if (!gwy_app_wait_set_fraction((gdouble)k/(xres*yres)))
            break;//what would happen next? 
                  // XXX: What should happen?  You return FALSE and just clean up, producing no output, similarly
                  // to other modules.  All execute() functions which can cancel have a boolean return value.
//        printf("k %d\n", k);

        col = k % xres;
        row = k/xres;
        gwy_assign(segments, gwy_lawn_get_segments(lawn, col, row, NULL), 2*nsegments);

        cdx = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, &ndata);
        cdy = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, NULL);
        g_array_set_size(newcdata, 2*ndata);
        ncdy = &g_array_index(newcdata, gdouble, ndata);

        do_sinebg(cdx, cdy, ncdy, ndata, segments, segment, segment_enabled, from, to, TRUE, NULL);

        gwy_lawn_set_curve_data(lawn, col, row, ordinate, ncdy);
    }

    gwy_app_wait_finish();
    g_array_free(newcdata, TRUE);
    g_free(segments);
}

static void
extract_one_curve(GwyLawn *lawn, GwyGraphCurveModel *gcmodel,
                  gint col, gint row,
                  GwyParams *params)
{
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    const gdouble *xdata, *ydata;
    gint ndata;

    ydata = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, &ndata);
    xdata = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, NULL);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
}

static gdouble
func_sine(gdouble x,
          G_GNUC_UNUSED gint n_param,
          const gdouble *param,
          G_GNUC_UNUSED gpointer user_data,
          gboolean *fres)
{
    *fres = TRUE;
    return param[0]*sin(param[1]*x + param[2]) + param[3];
}


//subtract means that the data will be changed, which is good for final operation
//fitparams are used to get the fit results and plot it with original data, which is good for GUI
static void
do_sinebg(const gdouble *xdata, const gdouble *ydata,
          gdouble *nydata, gint ndata,
          const gint *segments,
          gint segment, gboolean segment_enabled,
          gdouble from, gdouble to,
          gboolean subtract, gdouble *fitparams)
{
    gint i, j, n;
    gdouble startval, endval, xmin, xmax, ymin, ymax;
    gdouble *xf, *yf;
    GwyNLFitter *fitter;
    gdouble param[4];
    gboolean fres;
    gint segment_from, segment_to;


    //get the total range and fit range
    xmin = G_MAXDOUBLE;
    xmax = -G_MAXDOUBLE;
    ymin = G_MAXDOUBLE;
    ymax = -G_MAXDOUBLE;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] < xmin)
            xmin = xdata[i];
        if (xdata[i] > xmax)
            xmax = xdata[i];
        if (ydata[i] < ymin)
            ymin = ydata[i];
        if (ydata[i] > ymax)
            ymax = ydata[i];
    }
    startval = xmin + from*(xmax-xmin);
    endval = xmin + to*(xmax-xmin);

    if (segment_enabled) {
        segment_from = segments[2*segment];
        segment_to = segments[2*segment + 1];
    } else {
        segment_from = 0;
        segment_to = G_MAXINT;
    }


    //determine number of points to fit
    n = 0;
    for (i = 0; i <  ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval && i >= segment_from && i < segment_to)
            n++;
    }

    //fill the data to fit
    // XXX: Please allocate the buffers outside the function.  If it is a GArray you can then just do
    // g_array_set_size() and then assign the items normally, without going throug g_array_append_val().
    xf = g_new(gdouble, n);
    yf = g_new(gdouble, n);
    j = 0;
    for (i = 0; i < ndata; i++) {
        if (xdata[i] >= startval && xdata[i] < endval && i >= segment_from && i < segment_to) {
            xf[j] = xdata[i];
            yf[j] = ydata[i];
            j++;
        }
    }

    // XXX: Please allocate the fitter outside the function.
    // XXX: Also, the original interface is weird, with functions getting never-used arguments like n_param.
    // Usually gwy_math_nlfit_new_idx() is better.
    fitter = gwy_math_nlfit_new((GwyNLFitFunc)func_sine, gwy_math_nlfit_diff);

    param[0] = (ymax-ymin)/2.0; //amplitude
    param[1] = 2*M_PI/(xmax-xmin); //frequency
    param[2] = 0; //phase
    param[3] = (ymax+ymin)/2.0; //offset

    gwy_math_nlfit_fit_full(fitter, n, xf, yf, NULL, 4, param, NULL, NULL, NULL);

    if (subtract && nydata) {
        for (i = 0; i < ndata; i++) {
            nydata[i] = ydata[i] - func_sine(xdata[j], 4, param, NULL, &fres);
        }
    }

    if (fitparams)
        gwy_assign(fitparams, param, 4);

    gwy_math_nlfit_free(fitter);
    g_free(xf);
    g_free(yf);
}

static void
convert_one_curve(GwyGraphCurveModel *gcmodel, GwyParams *params, gdouble *fitparams, 
                  gint nsegments, const gint *segments)
{
    gdouble from = gwy_params_get_double(params, PARAM_RANGE_FROM);
    gdouble to = gwy_params_get_double(params, PARAM_RANGE_TO);
    gboolean segment_enabled = nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;

    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    gint ndata = gwy_graph_curve_model_get_ndata(gcmodel);

    do_sinebg(xdata, ydata, NULL, ndata, segments, segment, segment_enabled, from, to, FALSE, fitparams);
}

static void
update_graph_model_props(GwyGraphModel *gmodel, ModuleArgs *args)
{
    GwyLawn *lawn = args->lawn;
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwySIUnit *xunit, *yunit;
    const gchar *xlabel, *ylabel;

    xunit = gwy_lawn_get_si_unit_curve(lawn, abscissa);
    xlabel = gwy_lawn_get_curve_label(lawn, abscissa);
    yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
    ylabel = gwy_lawn_get_curve_label(lawn, ordinate);

    g_object_set(gmodel,
                 "si-unit-x", xunit,
                 "si-unit-y", yunit,
                 "axis-label-bottom", xlabel ? xlabel : _("Untitled"),
                 "axis-label-left", ylabel ? ylabel : _("Untitled"),
                 NULL);
}

static void
sanitise_one_param(GwyParams *params, gint id, gint min, gint max, gint defval)
{
    gint v;

    v = gwy_params_get_int(params, id);
    if (v >= min && v <= max) {
        gwy_debug("param #%d is %d, i.e. within range [%d..%d]", id, v, min, max);
        return;
    }
    gwy_debug("param #%d is %d, setting it to the default %d", id, v, defval);
    gwy_params_set_int(params, id, defval);
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLawn *lawn = args->lawn;

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_lawn_get_xres(lawn)-1, gwy_lawn_get_xres(lawn)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_lawn_get_yres(lawn)-1, gwy_lawn_get_yres(lawn)/2);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
