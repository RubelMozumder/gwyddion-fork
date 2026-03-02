/*
 *  $Id: graph_sfuncs.c 24431 2021-10-27 12:52:11Z yeti-dn $
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
    PARAM_OUTPUT_TYPE,
    PARAM_OVERSAMPLE,
    PARAM_FIXRES,
    PARAM_RESOLUTION,
    PARAM_WINDOW,
    PARAM_TARGET_GRAPH,
};

typedef enum {
    GWY_SF_DH   = 0,
    GWY_SF_CDH  = 1,
    GWY_SF_DA   = 2,
    GWY_SF_CDA  = 3,
    GWY_SF_ACF  = 4,
    GWY_SF_HHCF = 5,
    GWY_SF_PSDF = 6,
    GWY_SF_NFUNCTIONS
} GwySFOutputType;

typedef struct {
    GwyParams *params;
    GwyGraphModel *gmodel;
    GwyGraphModel *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register              (void);
static GwyParamDef*     define_module_params         (void);
static void             graph_sfuncs                 (GwyGraph *graph);
static GwyDialogOutcome run_gui                      (ModuleArgs *args);
static void             execute                      (ModuleArgs *args);
static void             param_changed                (ModuleGUI *gui,
                                                      gint id);
static void             preview                      (gpointer user_data);
static void             calculate_stats              (GwyGraphCurveModel *gcmodel,
                                                      GwySIUnit *xunit,
                                                      GwySIUnit *yunit,
                                                      GwyParams *params,
                                                      GwyDataLine *dline);
static gboolean         sfunction_has_native_sampling(GwySFOutputType type);

static const GwyEnum sf_types[] =  {
    { N_("Height distribution"),         GWY_SF_DH,   },
    { N_("Cum. height distribution"),    GWY_SF_CDH,  },
    { N_("Distribution of angles"),      GWY_SF_DA,   },
    { N_("Cum. distribution of angles"), GWY_SF_CDA,  },
    { N_("ACF"),                         GWY_SF_ACF,  },
    { N_("HHCF"),                        GWY_SF_HHCF, },
    { N_("PSDF"),                        GWY_SF_PSDF, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates one-dimensional statistical functions (height distribution, correlations, PSDF)."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2019",
};

GWY_MODULE_QUERY2(module_info, graph_sfuncs)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_sfuncs",
                            (GwyGraphFunc)&graph_sfuncs,
                            N_("/_Statistics/Statistical _Functions..."),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Calculate 1D statistical functions"));

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
    gwy_param_def_add_boolean(paramdef, PARAM_ALL, "all", _("_All curves"), FALSE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("_Quantity"),
                              sf_types, G_N_ELEMENTS(sf_types), GWY_SF_DH);
    gwy_param_def_add_double(paramdef, PARAM_OVERSAMPLE, "oversample", _("O_versampling"), 1.0, 16.0, 4.0);
    gwy_param_def_add_int(paramdef, PARAM_RESOLUTION, "resolution", _("_Fixed resolution"), 4, 16384, 120);
    gwy_param_def_add_boolean(paramdef, PARAM_FIXRES, "fixres", NULL, FALSE);
    gwy_param_def_add_enum(paramdef, PARAM_WINDOW, "window", NULL, GWY_TYPE_WINDOWING_TYPE, GWY_WINDOWING_HANN);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    return paramdef;
}

static void
graph_sfuncs(GwyGraph *graph)
{
    GwyDialogOutcome outcome;
    GwyAppDataId target_graph_id;
    GwyContainer *data;
    ModuleArgs args;

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.gmodel = gwy_graph_get_model(graph);
    args.result = gwy_graph_model_new();
    outcome = run_gui(&args);
    gwy_params_save_to_settings(args.params);

    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

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
    execute(args);

    gwy_clear(&gui, 1);
    gui.args = args;
    g_object_set(args->result, "label-visible", FALSE, NULL);

    gui.dialog = gwy_dialog_new(_("Statistical Functions"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gwy_dialog_have_result(dialog);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    graph = gwy_graph_new(args->result);
    gtk_widget_set_size_request(graph, 480, 300);
    gtk_box_pack_end(GTK_BOX(hbox), graph, TRUE, TRUE, 0);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_graph_curve(table, PARAM_CURVE, args->gmodel);
    gwy_param_table_append_checkbox(table, PARAM_ALL);
    gwy_param_table_append_combo(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_slider(table, PARAM_RESOLUTION);
    gwy_param_table_add_enabler(table, PARAM_FIXRES, PARAM_RESOLUTION);
    gwy_param_table_append_slider(table, PARAM_OVERSAMPLE);
    gwy_param_table_set_unitstr(table, PARAM_OVERSAMPLE, "×");
    gwy_param_table_append_combo(table, PARAM_WINDOW);
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
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_ALL) {
        gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
        gwy_param_table_set_sensitive(table, PARAM_CURVE, !all_curves);
    }
    if (id < 0 || id == PARAM_OUTPUT_TYPE) {
        GwySFOutputType output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);
        gwy_param_table_set_sensitive(table, PARAM_RESOLUTION, !sfunction_has_native_sampling(output_type));
        gwy_param_table_set_sensitive(table, PARAM_WINDOW, output_type == GWY_SF_PSDF);
    }
    if (id != PARAM_TARGET_GRAPH)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_param_table_data_id_refilter(gui->table, PARAM_TARGET_GRAPH);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    static const GwyEnum abscissae[] = {
        { "z",     GWY_SF_DH,   },
        { "z",     GWY_SF_CDH,  },
        { "tan β", GWY_SF_DA,   },
        { "tan β", GWY_SF_CDA,  },
        { "τ",     GWY_SF_ACF,  },
        { "τ",     GWY_SF_HHCF, },
        { "k",     GWY_SF_PSDF, },
    };
    static const GwyEnum ordinates[] = {
        { "ρ",             GWY_SF_DH,   },
        { "D",             GWY_SF_CDH,  },
        { "ρ",             GWY_SF_DA,   },
        { "D",             GWY_SF_CDA,  },
        { "G",             GWY_SF_ACF,  },
        { "H",             GWY_SF_HHCF, },
        { "W<sub>1</sub>", GWY_SF_PSDF, },
    };

    GwyParams *params = args->params;
    GwyGraphModel *gmodel = args->gmodel, *result = args->result;
    GwySFOutputType output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);
    gboolean all_curves = gwy_params_get_boolean(params, PARAM_ALL);
    gint curve = gwy_params_get_int(params, PARAM_CURVE);
    gint ifrom = (all_curves ? 0 : curve);
    gint ito = (all_curves ? gwy_graph_model_get_n_curves(gmodel) : curve+1);
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *xunit, *yunit;
    const gchar *title;
    GwyDataLine *dline;
    gint i;
    gchar *s;

    dline = gwy_data_line_new(1, 1.0, FALSE);
    g_object_get(gmodel, "si-unit-x", &xunit, "si-unit-y", &yunit, NULL);
    gwy_graph_model_remove_all_curves(result);
    title = _(gwy_enum_to_string(output_type, sf_types, G_N_ELEMENTS(sf_types)));
    g_object_set(result,
                 "title", title,
                 "axis-label-bottom", gwy_enum_to_string(output_type, abscissae, G_N_ELEMENTS(abscissae)),
                 "axis-label-left", gwy_enum_to_string(output_type, ordinates, G_N_ELEMENTS(ordinates)),
                 NULL);

    for (i = ifrom; i < ito; i++) {
        gcmodel = gwy_graph_model_get_curve(gmodel, i);
        calculate_stats(gcmodel, xunit, yunit, params, dline);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, dline, 0, 0);
        g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
        if (all_curves) {
            s = g_strdup_printf("%s %d", title, i+1);
            g_object_set(gcmodel,
                         "color", gwy_graph_get_preset_color(i),
                         "description", s,
                         NULL);
            g_free(s);
        }
        else
            g_object_set(gcmodel, "description", title, NULL);
        gwy_graph_model_add_curve(result, gcmodel);
        g_object_unref(gcmodel);
    }

    gwy_graph_model_set_units_from_data_line(result, dline);
    g_object_unref(dline);
    g_object_unref(xunit);
    g_object_unref(yunit);
}

static void
oversample_curve(const gdouble *xdata, const gdouble *ydata, guint ndata, GwyDataLine *oversampled)
{
    guint i, j, nover;
    gdouble xfrom, xto;
    gdouble *d;

    if (ndata == 1)
        gwy_data_line_fill(oversampled, ydata[0]);

    xfrom = xdata[0];
    xto = xdata[ndata-1];
    nover = gwy_data_line_get_res(oversampled);
    d = gwy_data_line_get_data(oversampled);
    for (i = j = 0; i < nover; i++) {
        gdouble t, x = i/(nover - 1.0)*(xto - xfrom) + xfrom;

        while (j < ndata && xdata[j] < x)
            j++;

        if (j == 0)
            d[i] = ydata[0];
        else if (j == ndata)
            d[i] = ydata[ndata-1];
        else if (xdata[j-1] == xdata[j])
            d[i] = 0.5*(ydata[j-1] + ydata[j]);
        else {
            t = (x - xdata[j-1])/(xdata[j] - xdata[j-1]);
            d[i] = t*ydata[j] + (1.0 - t)*ydata[j-1];
        }
    }
}

static void
calculate_stats(GwyGraphCurveModel *gcmodel,
                GwySIUnit *xunit, GwySIUnit *yunit,
                GwyParams *params,
                GwyDataLine *dline)
{
    GwySFOutputType output_type = gwy_params_get_enum(params, PARAM_OUTPUT_TYPE);
    gdouble oversample = gwy_params_get_double(params, PARAM_OVERSAMPLE);
    gboolean fixres = gwy_params_get_boolean(params, PARAM_FIXRES);
    gint resolution = gwy_params_get_int(params, PARAM_RESOLUTION);
    GwyWindowingType window = gwy_params_get_enum(params, PARAM_WINDOW);
    GwyDataLine *oversampled;
    const gdouble *xdata, *ydata;
    guint ndata, i, j, nover;
    gdouble xfrom, xto;
    gdouble *diffdata = NULL;

    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    nover = GWY_ROUND(ndata*oversample);

    xfrom = xdata[0];
    xto = xdata[ndata-1];
    if (xto == xfrom) {
        if (xto) {
            xto += 1e-9*fabs(xto);
            xfrom -= 1e-9*fabs(xfrom);
        }
        else {
            xfrom = -1e-9;
            xto = 1e-9;
        }
    }

    oversampled = gwy_data_line_new(nover, xto - xfrom, FALSE);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_x(oversampled), xunit);
    gwy_si_unit_assign(gwy_data_line_get_si_unit_y(oversampled), yunit);

    /* Oversample derivatives, not values for DA and CDA. */
    if (output_type == GWY_SF_DA || output_type == GWY_SF_CDA) {
        if (ndata == 1)
            diffdata = g_new0(gdouble, 1);
        else {
            diffdata = g_new0(gdouble, ndata-1);
            for (i = j = 0; i < ndata-1; i++) {
                /* Cannot handle infinite derivatives. */
                if (xdata[i] == xdata[j+1])
                    continue;

                diffdata[j] = (ydata[j+1] - ydata[j])/(xdata[j+1] - xdata[j]);
                j++;
            }
            ndata = j;
        }
        ydata = diffdata;

        gwy_si_unit_divide(yunit, xunit, gwy_data_line_get_si_unit_y(oversampled));
    }

    oversample_curve(xdata, ydata, ndata, oversampled);

    if (output_type == GWY_SF_DH || output_type == GWY_SF_DA) {
        gwy_data_line_distribution(oversampled, dline, 0.0, 0.0, TRUE, fixres ? resolution : -1);
    }
    else if (output_type == GWY_SF_CDH || output_type == GWY_SF_CDA) {
        gwy_data_line_distribution(oversampled, dline, 0.0, 0.0, TRUE, fixres ? resolution : -1);
        gwy_data_line_cumulate(dline);
        i = gwy_data_line_get_res(dline);
        gwy_data_line_multiply(dline, 1.0/gwy_data_line_get_val(dline, i-1));
        gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_y(dline), NULL);
    }
    else if (output_type == GWY_SF_ACF) {
        gwy_data_line_add(oversampled, -gwy_data_line_get_avg(oversampled));
        gwy_data_line_acf(oversampled, dline);
    }
    else if (output_type == GWY_SF_HHCF) {
        gwy_data_line_add(oversampled, -gwy_data_line_get_avg(oversampled));
        gwy_data_line_hhcf(oversampled, dline);
    }
    else if (output_type == GWY_SF_PSDF) {
        gwy_data_line_add(oversampled, -gwy_data_line_get_avg(oversampled));
        /* Interpolation is ignored. */
        gwy_data_line_psdf(oversampled, dline, window, GWY_INTERPOLATION_LINEAR);
    }
    else {
        g_assert_not_reached();
    }

    g_object_unref(oversampled);
    g_free(diffdata);
}

static gboolean
sfunction_has_native_sampling(GwySFOutputType type)
{
    return (type == GWY_SF_ACF || type == GWY_SF_HHCF || type == GWY_SF_PSDF);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
