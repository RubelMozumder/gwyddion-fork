/*
 *  $Id: graph_fztofd.c 24586 2022-02-05 19:40:46Z klapetek $
 *  Copyright (C) 2019-2021 David Necas (Yeti).
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
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

enum {
    PARAM_CURVE,
    PARAM_ALL,
    PARAM_STIFFNESS,
    PARAM_TILT,
    PARAM_POS,
    PARAM_DEFLSENS,
    PARAM_INPUT_TYPE,
    PARAM_ZERO_TYPE,
    PARAM_TARGET_GRAPH,
};

typedef enum {
    GWY_FZ_HEIGHT   = 0,
    GWY_FZ_ZPIEZO   = 1,
    GWY_FZ_NTYPES
} GwyFZInputType;

typedef enum {
    GWY_FZZ_DMIN   = 0,
    GWY_FZZ_DMAX   = 1,
    GWY_FZZ_FMIN   = 2,
    GWY_FZZ_FMAX   = 3,
    GWY_FZZ_PICK   = 4,
    GWY_FZZ_NTYPES
} GwyFZZeroType;


typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
    GwyGraphModel *result;
    gboolean have_pos;
    gboolean use_deflsens;
    gboolean use_stiffness;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwySelection *xsel;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             graph_fztofd        (GwyGraph *graph);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             execute             (ModuleArgs *args,
                                             gboolean apply_pick_zeropos);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);

static void             graph_selected      (GwySelection* selection,
                                             gint i,
                                             ModuleGUI *gui);

static const GwyEnum input_types[] =  {
    { N_("Height"),             GWY_FZ_HEIGHT,   },
    { N_("Piezo extension"),    GWY_FZ_ZPIEZO,  },
};

static const GwyEnum zero_types[] =  {
    { N_("Min. distance"),    GWY_FZZ_DMIN,  },
    { N_("Max. distance"),    GWY_FZZ_DMAX,  },
    { N_("Min. force"),       GWY_FZZ_FMIN,  },
    { N_("Max. force"),       GWY_FZZ_FMAX,  },
    { N_("Pick from graph"),  GWY_FZZ_PICK,  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Convert FZ to FD curve"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, graph_fztofd)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_fztofd",
                            (GwyGraphFunc)&graph_fztofd,
                            N_("/_Force Distance/_FZ to FD Curve..."),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Convert Force-Z Piezo to Force-Distance"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_graph_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ALL, "all", _("_All curves"), TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_INPUT_TYPE, "input_type", _("_Z input"),
                               input_types, G_N_ELEMENTS(input_types), GWY_FZ_HEIGHT);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZERO_TYPE, "zero_type", _("Z_ero point"),
                               zero_types, G_N_ELEMENTS(zero_types), GWY_FZZ_DMIN);

    gwy_param_def_add_double(paramdef, PARAM_STIFFNESS, "stiffness", _("Cantilever _stiffness"), 0.001, 1000, 20.0);
    gwy_param_def_add_double(paramdef, PARAM_DEFLSENS, "deflsens", _("_Deflection sensitivity"), 0.001, 1000, 100);
    gwy_param_def_add_double(paramdef, PARAM_TILT, "tilt", _("Cantilever _tilt"), 0, 20, 0);
    gwy_param_def_add_double(paramdef, PARAM_POS, "pos", _("Zero position"), -G_MAXDOUBLE, G_MAXDOUBLE, 0);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
graph_fztofd(GwyGraph *graph)
{
    GwyDialogOutcome outcome;
    GwyAppDataId target_graph_id;
    GwyContainer *data;
    GwySIUnit *siunit;
    ModuleArgs args;

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    args.result = gwy_graph_model_new_alike(args.gmodel);

    g_object_get(args.gmodel, "si-unit-y", &siunit, NULL);
    if (gwy_si_unit_equal_string(siunit, "V"))
        args.use_deflsens = TRUE;
    else
        args.use_deflsens = FALSE;

    if (gwy_si_unit_equal_string(siunit, "m"))
        args.use_stiffness = TRUE;
    else
        args.use_stiffness = FALSE;


    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
//    if (outcome != GWY_DIALOG_HAVE_RESULT)  //do this always
        execute(&args, TRUE);

    target_graph_id = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_app_add_graph_or_curves(args.result, data, &target_graph_id, 1);

end:
    g_object_unref(args.params);
    g_object_unref(args.result);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    GwyDialogOutcome outcome;
    GtkWidget *hbox, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    /* This is to get the target graph filter right. */
    execute(args, FALSE);

    gwy_clear(&gui, 1);
    gui.args = args;
    g_object_set(args->result, "label-visible", FALSE, NULL);


    gui.dialog = gwy_dialog_new(_("Convert FZ to FD Curve"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gwy_dialog_have_result(dialog);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(args->result);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);

    gwy_graph_set_status(GWY_GRAPH(graph), GWY_GRAPH_STATUS_XLINES);
    gui.xsel = gwy_graph_area_get_selection(GWY_GRAPH_AREA(gwy_graph_get_area(GWY_GRAPH(graph))),
                                            GWY_GRAPH_STATUS_XLINES);
    gwy_selection_set_max_objects(gui.xsel, 1);
    g_signal_connect(gui.xsel, "changed", G_CALLBACK(graph_selected), &gui);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    gwy_param_table_append_checkbox(table, PARAM_ALL);
    gwy_param_table_append_combo(table, PARAM_INPUT_TYPE);
    gwy_param_table_append_slider(table, PARAM_STIFFNESS);
    gwy_param_table_set_unitstr(table, PARAM_STIFFNESS, "N/m");
    gwy_param_table_append_slider(table, PARAM_TILT);
    gwy_param_table_set_unitstr(table, PARAM_TILT, "deg");
    gwy_param_table_append_slider(table, PARAM_DEFLSENS);
    gwy_param_table_set_unitstr(table, PARAM_DEFLSENS, "nm/V");
    gwy_param_table_append_combo(table, PARAM_ZERO_TYPE);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->result);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, TRUE, 0);

    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);
    outcome = gwy_dialog_run(dialog);

    g_object_set(args->result, "label-visible", TRUE, NULL);

    return outcome;
}

static void
graph_selected(GwySelection* selection, gint i, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble pos;

    g_return_if_fail(i <= 0);

    if (gwy_selection_get_data(selection, NULL) <= 0)
        args->have_pos = FALSE;
    else {
        gwy_selection_get_object(selection, 0, &pos);
        args->have_pos = TRUE;
    }

    //update_range_entries(gui, pos);
    if (gwy_params_set_double(args->params, PARAM_POS, pos))
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));

    //printf("graph selected completed\n");
}



static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_ALL) {
        gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
        gwy_param_table_set_sensitive(table, PARAM_CURVE, !all_curves);
    }

    if (id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args, FALSE);
    gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args, gboolean apply_pick_zeropos)
{
    GwyParams *params = args->params;
    GwyGraphModel *gmodel = args->gmodel, *result = args->result;
    gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
    GwyFZInputType input_type = gwy_params_get_enum(params, PARAM_INPUT_TYPE);
    GwyFZZeroType zero_type = gwy_params_get_enum(params, PARAM_ZERO_TYPE);
    gdouble stiffness = gwy_params_get_double(params, PARAM_STIFFNESS);
    gdouble tilt = gwy_params_get_double(params, PARAM_TILT)*M_PI/180;
    gdouble deflsens = gwy_params_get_double(params, PARAM_DEFLSENS)*1e-9;
    gboolean use_deflsens = args->use_deflsens;
    gboolean use_stiffness = args->use_stiffness;
    gdouble pos = gwy_params_get_double(params, PARAM_POS);
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    gint ifrom = (all_curves ? 0 : curve);
    gint ito = (all_curves ? gwy_graph_model_get_n_curves(gmodel) : curve+1);

    const gdouble *xdata, *ydata;
    gdouble *nxdata, *nydata;
    gdouble tiltcorr, zeropos = 0, posval, forceval, xval;
    GwyGraphCurveModel *gcmodel, *ngcmodel;
    gint i, j, ndata;
    gdouble vtof = 1.0;
    
    if (use_deflsens) vtof = deflsens*stiffness;
    else if (use_stiffness) vtof = stiffness;

    gwy_graph_model_remove_all_curves(result);

    //estimate the zero, FIXME, this does not account yet for hysteresis! Shift it to the end of the function.

    if (zero_type == GWY_FZZ_PICK)
    {
       if (args->have_pos && apply_pick_zeropos) {
           zeropos = pos;
       }
       else zeropos = 0;
       //how many times I have to initialize it to prevent compiler complaining that it is unitialized?
    }
    else {
        if (zero_type == GWY_FZZ_DMIN)
        {
            posval = G_MAXDOUBLE;
            for (i = ifrom; i < ito; i++) {
                gcmodel = gwy_graph_model_get_curve(gmodel, i);
                xdata = gwy_graph_curve_model_get_xdata(gcmodel);
                ydata = gwy_graph_curve_model_get_ydata(gcmodel);
                ndata = gwy_graph_curve_model_get_ndata(gcmodel);

                for (j = 0; j < ndata; j++)
                {
                    if (input_type == GWY_FZ_HEIGHT) xval = xdata[j] + vtof*ydata[j]/stiffness;
                    else xval = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[j] - vtof*ydata[j]/stiffness);
                    if (xval < posval) posval = xval;
                }
           }
           zeropos = posval;
        }
        else if (zero_type == GWY_FZZ_DMAX)
        {
            posval = -G_MAXDOUBLE;
            for (i = ifrom; i < ito; i++) {
                gcmodel = gwy_graph_model_get_curve(gmodel, i);
                xdata = gwy_graph_curve_model_get_xdata(gcmodel);
                ydata = gwy_graph_curve_model_get_ydata(gcmodel);
                ndata = gwy_graph_curve_model_get_ndata(gcmodel);

                for (j = 0; j < ndata; j++)
                {
                    if (input_type == GWY_FZ_HEIGHT) xval = xdata[j] + vtof*ydata[j]/stiffness;
                    else xval = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[j] - vtof*ydata[j]/stiffness);
                    if (xval > posval) posval = xval;
                }
           }
           zeropos = posval;
        }
        else if (zero_type == GWY_FZZ_FMIN)
        {
            forceval = G_MAXDOUBLE;
            for (i = ifrom; i < ito; i++) {
                gcmodel = gwy_graph_model_get_curve(gmodel, i);
                xdata = gwy_graph_curve_model_get_xdata(gcmodel);
                ydata = gwy_graph_curve_model_get_ydata(gcmodel);
                ndata = gwy_graph_curve_model_get_ndata(gcmodel);

                for (j = 0; j < ndata; j++)
                {
                    if (input_type == GWY_FZ_HEIGHT) xval = xdata[j] + vtof*ydata[j]/stiffness;
                    else xval = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[j] - vtof*ydata[j]/stiffness);
                    if (vtof*ydata[j] < forceval)
                    {
                        posval = xval;
                        forceval = vtof*ydata[j];
                    }
                }
           }
           zeropos = posval;
        }
        else if (zero_type == GWY_FZZ_FMAX)
        {
            forceval = -G_MAXDOUBLE;
            for (i = ifrom; i < ito; i++) {
                gcmodel = gwy_graph_model_get_curve(gmodel, i);
                xdata = gwy_graph_curve_model_get_xdata(gcmodel);
                ydata = gwy_graph_curve_model_get_ydata(gcmodel);
                ndata = gwy_graph_curve_model_get_ndata(gcmodel);

                for (j = 0; j < ndata; j++)
                {
                    if (input_type == GWY_FZ_HEIGHT) xval = xdata[j] + vtof*ydata[j]/stiffness;
                    else xval = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[j] - vtof*ydata[j]/stiffness);
                    if (vtof*ydata[j] > forceval)
                    {
                        posval = xval;
                        forceval = vtof*ydata[j];
                    }
                }
           }
           zeropos = posval;
        } else zeropos = 0;
    }

    //do the conversion
    for (i = ifrom; i < ito; i++) {
        gcmodel = gwy_graph_model_get_curve(gmodel, i);

        ngcmodel = gwy_graph_curve_model_duplicate(gcmodel);

        xdata = gwy_graph_curve_model_get_xdata(gcmodel);
        ydata = gwy_graph_curve_model_get_ydata(gcmodel);
        ndata = gwy_graph_curve_model_get_ndata(gcmodel);

        nxdata = g_new(gdouble, ndata);
        nydata = g_new(gdouble, ndata);

        if (input_type == GWY_FZ_HEIGHT) //peak on the left side
        {
           for (j = 0; j < ndata; j++) {
               nxdata[j] = xdata[j] + vtof*ydata[j]/stiffness;
               nydata[j] = vtof*ydata[j];
           }
        } else {                         //peak on the right side
           for (j = 0; j < ndata; j++) {
               nxdata[j] = (xdata[ndata-1] - vtof*ydata[ndata-1]/stiffness) - (xdata[j] - vtof*ydata[j]/stiffness);
               nydata[j] = vtof*ydata[j];
           }
        }

        tiltcorr = 1.0/(cos(tilt)*cos(tilt));
        if (tilt > 0) for (j = 0; j < ndata; j++) nydata[j] *= tiltcorr;

        for (j = 0; j < ndata; j++) nxdata[j] -= zeropos;

        gwy_graph_curve_model_set_data(ngcmodel, nxdata, nydata, ndata);
        g_free(nxdata);
        g_free(nydata);

        g_object_set(ngcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);

        if (all_curves)
            g_object_set(ngcmodel, "color", gwy_graph_get_preset_color(i), NULL);
        else
            g_object_set(ngcmodel, "description", _("FD curve"), NULL);

        gwy_graph_curve_model_enforce_order(ngcmodel);
        gwy_graph_model_add_curve(result, ngcmodel);

        g_object_unref(ngcmodel);
    }

    if (use_deflsens || use_stiffness) 
        g_object_set(result, "si-unit-y", gwy_si_unit_new("N"), NULL); 

    g_object_set(result, "axis-label-bottom", "probe-sample distance", NULL);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
