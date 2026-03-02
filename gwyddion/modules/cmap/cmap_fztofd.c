/*
 *  $Id: cmap_fztofd.c 24504 2021-11-10 15:29:43Z klapetek $
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

typedef enum {
    GWY_FZ_HEIGHT   = 0,
    GWY_FZ_ZPIEZO   = 1,
    GWY_FZ_NTYPES
} GwyFZInputType;


enum {
    PARAM_ABSCISSA,
    PARAM_ORDINATE,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_STIFFNESS,
    PARAM_TILT,
    PARAM_DEFLSENS,
    PARAM_INPUT_TYPE,
};

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyLawn *result;
    GwyDataField *field;
    gboolean use_deflsens;
    gboolean use_stiffness;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyParamTable *table_output;
    GwyContainer *data;
    GwySelection *selection;
    GwyGraphModel *gmodel;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             fztofd                  (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
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
                                                 gboolean use_deflsens,
                                                 gboolean use_stiffness);
static void             do_fz_to_fd             (const gdouble *xdata,
                                                 const gdouble *ydata,
                                                 gdouble *nxdata,
                                                 gdouble *nydata, gint ndata,
                                                 GwyFZInputType input_type,
                                                 gdouble stiffness,
                                                 gdouble tilt,
                                                 gdouble deflsens,
                                                 gboolean use_deflsens,
                                                 gboolean use_stiffness);

static void             update_graph_model_props(GwyGraphModel *gmodel,
                                                 ModuleArgs *args);
static void             sanitise_params         (ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Converts FZ to FD curve map."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_fztofd)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_fztofd",
                                (GwyCurveMapFunc)&fztofd,
                                N_("/_FZ Curves to FD..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Convert Force-Z Piezo to Force-Distance"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{


    static const GwyEnum input_types[] =  {
        { N_("Height"),             GWY_FZ_HEIGHT,   },
        { N_("Piezo extension"),    GWY_FZ_ZPIEZO,  },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ABSCISSA, "abscissa", _("Abscissa"));
    gwy_param_def_add_lawn_curve(paramdef, PARAM_ORDINATE, "ordinate", _("Ordinate"));
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_gwyenum(paramdef, PARAM_INPUT_TYPE, "input_type", _("_Z input"),
                               input_types, G_N_ELEMENTS(input_types), GWY_FZ_HEIGHT);
    gwy_param_def_add_double(paramdef, PARAM_STIFFNESS, "stiffness", _("Cantilever _stiffness"), 0.001, 1000, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_TILT, "tilt", _("Cantilever _tilt"), 0, 20, 0);
    gwy_param_def_add_double(paramdef, PARAM_DEFLSENS, "deflsens", _("_Deflection sensitivity"), 0.001, 1000, 100);

    return paramdef;
}

static void
fztofd(GwyContainer *data, GwyRunType runtype)
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
    args.params = gwy_params_new_from_settings(define_module_params());
    args.use_deflsens = FALSE;
    args.use_stiffness = FALSE;
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
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    /* TODO: Add log, maybe more. */

end:
    GWY_OBJECT_UNREF(args.result);
    g_object_unref(args.field);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align;
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

    gui.dialog = gwy_dialog_new(_("Convert FZ to FD Curve"));
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
                 NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);


    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_ABSCISSA, args->lawn);
    gwy_param_table_append_lawn_curve(table, PARAM_ORDINATE, args->lawn);
    gwy_param_table_append_combo(table, PARAM_INPUT_TYPE);
    gwy_param_table_append_slider(table, PARAM_STIFFNESS);
    gwy_param_table_set_unitstr(table, PARAM_STIFFNESS, "N/m");
    gwy_param_table_append_slider(table, PARAM_TILT);
    gwy_param_table_set_unitstr(table, PARAM_TILT, "deg");
    gwy_param_table_append_slider(table, PARAM_DEFLSENS);
    gwy_param_table_set_unitstr(table, PARAM_DEFLSENS, "nm/V");


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
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);

    if (gwy_si_unit_equal_string(gwy_lawn_get_si_unit_curve(args->lawn, ordinate), "V"))
        args->use_deflsens = TRUE;
    else
        args->use_deflsens = FALSE;

    if (gwy_si_unit_equal_string(gwy_lawn_get_si_unit_curve(args->lawn, ordinate), "m"))
        args->use_stiffness = TRUE;
     else
        args->use_stiffness = FALSE;

    gwy_param_table_set_sensitive(gui->table, PARAM_DEFLSENS, args->use_deflsens);

    extract_one_curve(args->lawn, gwy_graph_model_get_curve(gui->gmodel, 0), col, row, params);
    convert_one_curve(gwy_graph_model_get_curve(gui->gmodel, 0), params, args->use_deflsens, args->use_stiffness);
    update_graph_model_props(gui->gmodel, args);

}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    gint abscissa = gwy_params_get_int(params, PARAM_ABSCISSA);
    gint ordinate = gwy_params_get_int(params, PARAM_ORDINATE);
    GwyFZInputType input_type = gwy_params_get_enum(params, PARAM_INPUT_TYPE);
    gdouble stiffness = gwy_params_get_double(params, PARAM_STIFFNESS);
    gdouble tilt = gwy_params_get_double(params, PARAM_TILT)*M_PI/180;
    gdouble deflsens = gwy_params_get_double(params, PARAM_DEFLSENS)*1e-9;
    gboolean use_deflsens = args->use_deflsens;
    gboolean use_stiffness = args->use_stiffness;
    GwyLawn *lawn = args->lawn;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    GArray *newcdata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    const gdouble *cdx, *cdy;
    gdouble *ncdx, *ncdy;
    gint ndata, k, col, row;

    for (k = 0; k < xres*yres; k++) {
        col = k % xres;
        row = k/xres;

        cdx = gwy_lawn_get_curve_data_const(lawn, col, row, abscissa, &ndata);
        cdy = gwy_lawn_get_curve_data_const(lawn, col, row, ordinate, NULL);
        g_array_set_size(newcdata, 2*ndata);
        ncdx = &g_array_index(newcdata, gdouble, 0);
        ncdy = &g_array_index(newcdata, gdouble, ndata);

        do_fz_to_fd(cdx, cdy, ncdx, ncdy, ndata, input_type, stiffness, tilt, deflsens, use_deflsens, use_stiffness);

        gwy_lawn_set_curve_data(lawn, col, row, abscissa, ncdx);
        gwy_lawn_set_curve_data(lawn, col, row, ordinate, ncdy);
    }

    if (use_deflsens || use_stiffness)
       gwy_lawn_set_si_unit_curve(args->lawn, ordinate, gwy_si_unit_new("N"));

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

static void
do_fz_to_fd(const gdouble *xdata, const gdouble *ydata, gdouble *nxdata, gdouble *nydata, gint ndata,
            GwyFZInputType input_type, gdouble stiffness, gdouble tilt, gdouble deflsens, 
            gboolean use_deflsens, gboolean use_stiffness)
{
    gint i;
    gdouble tiltcorr =  1.0/(cos(tilt)*cos(tilt));
    gdouble vtof = 1.0;

    if (use_deflsens) 
        vtof = deflsens*stiffness;
    else if (use_stiffness) 
        vtof = stiffness;

    if (input_type == GWY_FZ_HEIGHT) {  //peak on the left side
        for (i = 0; i < ndata; i++) {
            nxdata[i] = xdata[i] + vtof*ydata[i]/stiffness;
            nydata[i] = vtof*ydata[i];
        }
    }
    else {                              //peak on the right side
        for (i = 0; i < ndata; i++) {
            nxdata[i] = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[i] - vtof*ydata[i]/stiffness);
            nydata[i] = vtof*ydata[i];
        }
    }

    if (tilt > 0) {
        for (i = 0; i < ndata; i++)
            nydata[i] *= tiltcorr;
    }
}

static void
convert_one_curve(GwyGraphCurveModel *gcmodel, GwyParams *params, gboolean use_deflsens, gboolean use_stiffness)
{
    GwyFZInputType input_type = gwy_params_get_enum(params, PARAM_INPUT_TYPE);
    gdouble stiffness = gwy_params_get_double(params, PARAM_STIFFNESS);
    gdouble tilt = gwy_params_get_double(params, PARAM_TILT)*M_PI/180;
    gdouble deflsens = gwy_params_get_double(params, PARAM_DEFLSENS)*1e-9;

    const gdouble *xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    const gdouble *ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    gint ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    gdouble *nxdata, *nydata;


    nxdata = g_new(gdouble, ndata);
    nydata = g_new(gdouble, ndata);

    do_fz_to_fd(xdata, ydata, nxdata, nydata, ndata, input_type, stiffness, tilt, deflsens, 
                use_deflsens, use_stiffness);

    gwy_graph_curve_model_set_data(gcmodel, nxdata, nydata, ndata);

    g_free(nxdata);
    g_free(nydata);
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

    if (args->use_deflsens || args->use_stiffness) {
        yunit = gwy_si_unit_new("N");
        ylabel = g_strdup("Force");
    }
    else {
        yunit = gwy_lawn_get_si_unit_curve(lawn, ordinate);
        ylabel = gwy_lawn_get_curve_label(lawn, ordinate);
    }

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
