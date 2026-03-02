/*
 *  $Id: graph_logscale.c 24402 2021-10-22 11:53:09Z yeti-dn $
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

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-graph.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

typedef enum {
    LOGSCALE_AXIS_X    = 1,
    LOGSCALE_AXIS_Y    = 2,
    LOGSCALE_AXIS_BOTH = 3,   /* Used as a bitmask! */
} LogscaleAxisType;

typedef enum {
    LOGSCALE_NEGATIVE_SKIP = 0,
    LOGSCALE_NEGATIVE_ABS = 1,
} LogscaleNegativeType;

typedef enum {
    LOGSCALE_BASE_E    = 0,
    LOGSCALE_BASE_10   = 1,
    LOGSCALE_BASE_2    = 2,
} LogscaleBaseType;

enum {
    PARAM_AXES,
    PARAM_NEGATIVE_X,
    PARAM_NEGATIVE_Y,
    PARAM_BASE_TYPE,
    PARAM_BASE,
};

typedef struct {
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean            module_register     (void);
static GwyParamDef*        define_module_params(void);
static GwyDialogOutcome    run_gui             (ModuleArgs *args);
static void                param_changed       (ModuleGUI *gui,
                                                gint id);
static void                logscale            (GwyGraph *graph);
static gchar*              logscale_label      (const gchar *label,
                                                gdouble base);
static GwyGraphCurveModel* logscale_curve      (GwyGraphCurveModel *gcmodel,
                                                GwyParams *params);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Physically transforms graph data to logarithmic scale."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2016",
};

GWY_MODULE_QUERY2(module_info, graph_logscale)

static gboolean
module_register(void)
{
    gwy_graph_func_register("graph_logscale",
                            (GwyGraphFunc)&logscale,
                            N_("/_Correct Data/_Logscale Transform..."),
                            NULL,
                            GWY_MENU_FLAG_GRAPH_CURVE,
                            N_("Transform graph axes to logarithmic scale"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum axes[] = {
        { N_("Abscissa _X"), LOGSCALE_AXIS_X,    },
        { N_("Ordinate _Y"), LOGSCALE_AXIS_Y,    },
        { N_("_Both"),       LOGSCALE_AXIS_BOTH, },
    };
    static const GwyEnum negatives[] = {
        { N_("O_mit"),                LOGSCALE_NEGATIVE_SKIP, },
        { N_("_Take absolute value"), LOGSCALE_NEGATIVE_ABS,  },
    };
    static const GwyEnum bases[] = {
        { N_("Natural (e)"), LOGSCALE_BASE_E,  },
        { N_("10"),          LOGSCALE_BASE_10, },
        { N_("2"),           LOGSCALE_BASE_2,  },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_graph_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_AXES, "axes", _("Axes to transform"),
                              axes, G_N_ELEMENTS(axes), LOGSCALE_AXIS_BOTH);
    gwy_param_def_add_gwyenum(paramdef, PARAM_NEGATIVE_X, "negative_x", _("Negative abscissa handling"),
                              negatives, G_N_ELEMENTS(negatives), LOGSCALE_NEGATIVE_ABS );
    gwy_param_def_add_gwyenum(paramdef, PARAM_NEGATIVE_Y, "negative_y", _("Negative ordinate handling"),
                              negatives, G_N_ELEMENTS(negatives), LOGSCALE_NEGATIVE_ABS );
    gwy_param_def_add_gwyenum(paramdef, PARAM_BASE_TYPE, NULL, _("Base"),
                              bases, G_N_ELEMENTS(bases), LOGSCALE_BASE_E );
    gwy_param_def_add_double(paramdef, PARAM_BASE, "base", _("Base"), G_MINDOUBLE, G_MAXDOUBLE, G_E);
    return paramdef;
}

static void
logscale(GwyGraph *graph)
{
    GwyContainer *data;
    ModuleArgs args;
    GwyParams *params;
    GwyGraphModel *gmodel, *newgmodel;
    GwyGraphCurveModel *gcmodel;
    GwyDialogOutcome outcome;
    GwySIUnit *nullunit;
    guint axes, i, n;
    gdouble base;
    gchar *label, *llabel;

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data, 0);
    gwy_clear(&args, 1);
    args.params = params = gwy_params_new_from_settings(define_module_params());

    base = gwy_params_get_double(params, PARAM_BASE);
    if (fabs(base - 10.0)/10.0 < 1e-6)
        gwy_params_set_enum(params, PARAM_BASE_TYPE, LOGSCALE_BASE_10);
    else if (fabs(base - 2.0)/2.0 < 1e-6)
        gwy_params_set_enum(params, PARAM_BASE_TYPE, LOGSCALE_BASE_2);
    else
        gwy_params_set_enum(params, PARAM_BASE_TYPE, LOGSCALE_BASE_E);

    outcome = run_gui(&args);
    gwy_params_save_to_settings(params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;

    gmodel = gwy_graph_get_model(graph);
    newgmodel = gwy_graph_model_new_alike(gmodel);

    axes = gwy_params_get_enum(params, PARAM_AXES);
    base = gwy_params_get_double(params, PARAM_BASE);
    nullunit = gwy_si_unit_new(NULL);
    if (axes & LOGSCALE_AXIS_X) {
        g_object_get(gmodel, "axis-label-bottom", &label, NULL);
        llabel = logscale_label(label, base);
        g_free(label);
        g_object_set(newgmodel,
                     "axis-label-bottom", llabel,
                     "si-unit-x", nullunit,
                     NULL);
        g_free(llabel);
    }
    if (axes & LOGSCALE_AXIS_Y) {
        g_object_get(gmodel, "axis-label-left", &label, NULL);
        llabel = logscale_label(label, base);
        g_free(label);
        g_object_set(newgmodel,
                     "axis-label-left", llabel,
                     "si-unit-y", nullunit,
                     NULL);
        g_free(llabel);
    }
    g_object_unref(nullunit);

    n = gwy_graph_model_get_n_curves(gmodel);
    for (i = 0; i < n; i++) {
        if ((gcmodel = logscale_curve(gwy_graph_model_get_curve(gmodel, i), params))) {
            gwy_graph_model_add_curve(newgmodel, gcmodel);
            g_object_unref(gcmodel);
        }
    }

    if (gwy_graph_model_get_n_curves(newgmodel))
        gwy_app_data_browser_add_graph_model(newgmodel, data, TRUE);
    g_object_unref(newgmodel);

end:
    g_object_unref(params);
}

static gchar*
logscale_label(const gchar *label, gdouble base)
{
    if (fabs(base - G_E)/G_E < 1e-6)
        return g_strdup_printf("ln %s", label);
    if (fabs(base - 10.0)/10.0 < 1e-6)
        return g_strdup_printf("log %s", label);
    return g_strdup_printf("log<sub>%g</sub> %s", base, label);
}

static gboolean
transform_value(gdouble *v, gboolean islog, gdouble logbase, LogscaleNegativeType negtype)
{
    if (!islog)
        return TRUE;
    /* There is no way to fix exact zero so we always skip it. */
    if (*v == 0.0)
        return FALSE;
    if (*v < 0.0) {
        if (negtype == LOGSCALE_NEGATIVE_SKIP)
            return FALSE;
        *v = fabs(*v);
    }

    *v = log(*v)/logbase;
    return TRUE;
}

static GwyGraphCurveModel*
logscale_curve(GwyGraphCurveModel *gcmodel, GwyParams *params)
{
    gboolean logscale_x = gwy_params_get_enum(params, PARAM_AXES) & LOGSCALE_AXIS_X;
    gboolean logscale_y = gwy_params_get_enum(params, PARAM_AXES) & LOGSCALE_AXIS_Y;
    LogscaleNegativeType neg_x = gwy_params_get_enum(params, PARAM_NEGATIVE_X);
    LogscaleNegativeType neg_y = gwy_params_get_enum(params, PARAM_NEGATIVE_Y);
    gdouble logbase = log(gwy_params_get_double(params, PARAM_BASE));
    GwyGraphCurveModel *newgcmodel = gwy_graph_curve_model_new_alike(gcmodel);
    const gdouble *xdata, *ydata;
    gdouble *newxydata;
    guint ndata, i, n;

    xdata = gwy_graph_curve_model_get_xdata(gcmodel);
    ydata = gwy_graph_curve_model_get_ydata(gcmodel);
    ndata = gwy_graph_curve_model_get_ndata(gcmodel);
    newxydata = g_new(gdouble, 2*ndata);
    for (i = n = 0; i < ndata; i++) {
        gdouble x = xdata[i], y = ydata[i];

        if (!transform_value(&x, logscale_x, logbase, neg_x) || !transform_value(&y, logscale_y, logbase, neg_y))
            continue;

        newxydata[n++] = x;
        newxydata[n++] = y;
    }

    if (n) {
        gwy_graph_curve_model_set_data_interleaved(newgcmodel, newxydata, n/2);
        /* Theoretially we only need to do this after folding absissa values. Be on the safe side.  This is cheap if
         * values are already sorted. */
        gwy_graph_curve_model_enforce_order(newgcmodel);
    }
    else
        GWY_OBJECT_UNREF(newgcmodel);

    g_free(newxydata);

    return newgcmodel;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    ModuleGUI gui;
    GwyDialog *dialog;
    GwyParamTable *table;

    gui.args = args;
    gui.dialog = gwy_dialog_new(_("Logscale Transform"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_AXES);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_NEGATIVE_X);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_NEGATIVE_Y);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_BASE_TYPE);
    gwy_dialog_add_param_table(dialog, table);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_BASE_TYPE) {
        LogscaleBaseType base_type = gwy_params_get_enum(params, PARAM_BASE_TYPE);
        if (base_type == LOGSCALE_BASE_10)
            gwy_params_set_double(params, PARAM_BASE, 10.0);
        else if (base_type == LOGSCALE_BASE_E)
            gwy_params_set_double(params, PARAM_BASE, G_E);
        else if (base_type == LOGSCALE_BASE_2)
            gwy_params_set_double(params, PARAM_BASE, 2.0);
    }
    if (id < 0 || id == PARAM_AXES) {
        guint axes = gwy_params_get_enum(params, PARAM_AXES);
        gwy_param_table_set_sensitive(gui->table, PARAM_NEGATIVE_X, axes & LOGSCALE_AXIS_X);
        gwy_param_table_set_sensitive(gui->table, PARAM_NEGATIVE_Y, axes & LOGSCALE_AXIS_Y);
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
