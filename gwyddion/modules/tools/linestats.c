/*
 *  $Id: linestats.c 25178 2022-12-16 13:24:16Z yeti-dn $
 *  Copyright (C) 2003-2022 David Necas (Yeti), Petr Klapetek.
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
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-tool.h>
#include <libprocess/gwyprocesstypes.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libgwydgets/gwystock.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

enum {
    PARAM_OUTPUT_TYPE,
    PARAM_MASKING,
    PARAM_DIRECTION,
    PARAM_INSTANT_UPDATE,
    PARAM_TARGET_GRAPH,
    PARAM_HOLD_SELECTION,
    PARAM_OPTIONS_VISIBLE,

    INFO_AVERAGE,
};

#define GWY_TYPE_TOOL_LINE_STATS            (gwy_tool_line_stats_get_type())
#define GWY_TOOL_LINE_STATS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_TOOL_LINE_STATS, GwyToolLineStats))
#define GWY_IS_TOOL_LINE_STATS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_TOOL_LINE_STATS))
#define GWY_TOOL_LINE_STATS_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_TOOL_LINE_STATS, GwyToolLineStatsClass))

typedef struct _GwyToolLineStats      GwyToolLineStats;
typedef struct _GwyToolLineStatsClass GwyToolLineStatsClass;

struct _GwyToolLineStats {
    GwyPlainTool parent_instance;

    GwyParams *params;

    GwyRectSelectionLabels *rlabels;

    GwyDataLine *line;
    GwyDataLine *weights;
    gint isel[4];
    gint isel_prev[4];

    GwyGraphModel *gmodel;
    GtkWidget *update;
    GwyParamTable *table_quantity;
    GwyParamTable *table_options;

    /* potential class data */
    GType layer_type_rect;
};

struct _GwyToolLineStatsClass {
    GwyPlainToolClass parent_class;
};

static gboolean     module_register                      (void);
static GwyParamDef* define_module_params                 (void);
static GType        gwy_tool_line_stats_get_type         (void)                        G_GNUC_CONST;
static void         gwy_tool_line_stats_finalize         (GObject *object);
static void         gwy_tool_line_stats_init_dialog      (GwyToolLineStats *tool);
static void         gwy_tool_line_stats_data_switched    (GwyTool *gwytool,
                                                          GwyDataView *data_view);
static void         gwy_tool_line_stats_response         (GwyTool *tool,
                                                          gint response_id);
static void         gwy_tool_line_stats_data_changed     (GwyPlainTool *plain_tool);
static void         gwy_tool_line_stats_mask_changed     (GwyPlainTool *plain_tool);
static void         gwy_tool_line_stats_selection_changed(GwyPlainTool *plain_tool,
                                                          gint hint);
static void         update_selected_rectangle            (GwyToolLineStats *tool);
static void         update_sensitivity                   (GwyToolLineStats *tool);
static void         update_curve                         (GwyToolLineStats *tool);
static void         param_changed                        (GwyToolLineStats *tool,
                                                          gint id);
static void         gwy_tool_line_stats_apply            (GwyToolLineStats *tool);
static void         calculate_avg_rms_for_rms            (GwyDataLine *dline,
                                                          gdouble *avg,
                                                          gdouble *rms);
static gint         set_data_from_dataline_filtered      (GwyGraphCurveModel *gcmodel,
                                                          GwyDataLine *dline,
                                                          GwyDataLine *weight,
                                                          gdouble threshold);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Row/column statistical function tool, mean values, medians, maxima, minima, RMS, ..., of rows or columns."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

/* If you add something here, consider adding it also to ../volume/volume_linestat.c */
static const GwyEnum quantities[] =  {
    { N_("Mean"),               GWY_LINE_STAT_MEAN,      },
    { N_("Median"),             GWY_LINE_STAT_MEDIAN,    },
    { N_("Minimum"),            GWY_LINE_STAT_MINIMUM,   },
    { N_("Maximum"),            GWY_LINE_STAT_MAXIMUM,   },
    { N_("Range"),              GWY_LINE_STAT_RANGE,     },
    { N_("Developed length"),   GWY_LINE_STAT_LENGTH,    },
    { N_("Slope"),              GWY_LINE_STAT_SLOPE,     },
    { N_("tan β<sub>0</sub>"),  GWY_LINE_STAT_TAN_BETA0, },
    { N_("Variation"),          GWY_LINE_STAT_VARIATION, },
    { N_("Ra"),                 GWY_LINE_STAT_RA,        },
    { N_("Rq (RMS)"),           GWY_LINE_STAT_RMS,       },
    { N_("Rz"),                 GWY_LINE_STAT_RZ,        },
    { N_("Rt"),                 GWY_LINE_STAT_RT,        },
    { N_("Skew"),               GWY_LINE_STAT_SKEW,      },
    { N_("Excess kurtosis"),    GWY_LINE_STAT_KURTOSIS,  },
    { N_("Min. position"),      GWY_LINE_STAT_MINPOS,    },
    { N_("Max. position"),      GWY_LINE_STAT_MAXPOS,    },
};

GWY_MODULE_QUERY2(module_info, linestats)

G_DEFINE_TYPE(GwyToolLineStats, gwy_tool_line_stats, GWY_TYPE_PLAIN_TOOL)

static gboolean
module_register(void)
{
    gwy_tool_func_register(GWY_TYPE_TOOL_LINE_STATS);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum directions[] = {
        { N_("Ro_ws"),    GWY_ORIENTATION_HORIZONTAL, },
        { N_("Co_lumns"), GWY_ORIENTATION_VERTICAL,   },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "linestats");
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("_Quantity"),
                              quantities, G_N_ELEMENTS(quantities), GWY_LINE_STAT_MEAN);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DIRECTION, "direction", NULL, directions, G_N_ELEMENTS(directions),
                              GWY_ORIENTATION_HORIZONTAL);
    gwy_param_def_add_instant_updates(paramdef, PARAM_INSTANT_UPDATE, "instant_update", NULL, TRUE);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, NULL, NULL);
    gwy_param_def_add_hold_selection(paramdef, PARAM_HOLD_SELECTION, "hold_selection", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_OPTIONS_VISIBLE, "options_visible", NULL, FALSE);

    return paramdef;
}

static void
gwy_tool_line_stats_class_init(GwyToolLineStatsClass *klass)
{
    GwyPlainToolClass *ptool_class = GWY_PLAIN_TOOL_CLASS(klass);
    GwyToolClass *tool_class = GWY_TOOL_CLASS(klass);
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_tool_line_stats_finalize;

    tool_class->stock_id = GWY_STOCK_GRAPH_VERTICAL;
    tool_class->title = _("Row/Column Statistics");
    tool_class->tooltip = _("Calculate row/column statistical functions");
    tool_class->prefix = "/module/line_stats";
    tool_class->default_width = 640;
    tool_class->default_height = 400;
    tool_class->data_switched = gwy_tool_line_stats_data_switched;
    tool_class->response = gwy_tool_line_stats_response;

    ptool_class->data_changed = gwy_tool_line_stats_data_changed;
    ptool_class->mask_changed = gwy_tool_line_stats_mask_changed;
    ptool_class->selection_changed = gwy_tool_line_stats_selection_changed;
}

static void
gwy_tool_line_stats_finalize(GObject *object)
{
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(object);

    gwy_params_save_to_settings(tool->params);
    GWY_OBJECT_UNREF(tool->params);
    GWY_OBJECT_UNREF(tool->line);
    GWY_OBJECT_UNREF(tool->weights);
    GWY_OBJECT_UNREF(tool->gmodel);

    G_OBJECT_CLASS(gwy_tool_line_stats_parent_class)->finalize(object);
}

static void
gwy_tool_line_stats_init(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    tool->layer_type_rect = gwy_plain_tool_check_layer_type(plain_tool, "GwyLayerRectangle");
    if (!tool->layer_type_rect)
        return;

    plain_tool->unit_style = GWY_SI_UNIT_FORMAT_MARKUP;
    plain_tool->lazy_updates = TRUE;

    tool->params = gwy_params_new_from_settings(define_module_params());
    tool->line = gwy_data_line_new(4, 1.0, FALSE);
    tool->weights = gwy_data_line_new(4, 1.0, FALSE);
    memset(tool->isel_prev, 0xff, 4*sizeof(gint));

    gwy_plain_tool_connect_selection(plain_tool, tool->layer_type_rect, "rectangle");
    gwy_plain_tool_enable_selection_holding(plain_tool);

    gwy_tool_line_stats_init_dialog(tool);
}

static void
gwy_tool_line_stats_rect_updated(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);

    gwy_rect_selection_labels_select(tool->rlabels, plain_tool->selection, plain_tool->data_field);
}

static void
gwy_tool_line_stats_init_dialog(GwyToolLineStats *tool)
{
    GtkDialog *dialog = GTK_DIALOG(GWY_TOOL(tool)->dialog);
    GtkWidget *hbox, *vbox, *options, *image, *graph;
    GwyParamTable *table;

    tool->gmodel = gwy_graph_model_new();

    hbox = gwy_hbox_new(4);
    gtk_box_pack_start(GTK_BOX(dialog->vbox), hbox, TRUE, TRUE, 0);

    /* Left pane */
    vbox = gwy_vbox_new(6);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    /* Selection info */
    tool->rlabels = gwy_rect_selection_labels_new(TRUE, G_CALLBACK(gwy_tool_line_stats_rect_updated), tool);
    gtk_box_pack_start(GTK_BOX(vbox), gwy_rect_selection_labels_get_table(tool->rlabels), FALSE, FALSE, 0);

    /* Output type */
    table = tool->table_quantity = gwy_param_table_new(tool->params);
    gwy_param_table_append_combo(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_info(table, INFO_AVERAGE, _("Average"));
    gwy_plain_tool_add_param_table(GWY_PLAIN_TOOL(tool), table);
    gtk_box_pack_start(GTK_BOX(vbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    /* Options */
    options = gwy_create_expander_with_param(_("<b>Options</b>"), tool->params, PARAM_OPTIONS_VISIBLE);
    gtk_box_pack_start(GTK_BOX(vbox), options, FALSE, FALSE, 0);

    table = tool->table_options = gwy_param_table_new(tool->params);
    gwy_param_table_append_checkbox(table, PARAM_INSTANT_UPDATE);
    gwy_param_table_append_radio(table, PARAM_DIRECTION);
    gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, tool->gmodel);
    gwy_param_table_append_hold_selection(table, PARAM_HOLD_SELECTION);
    gwy_plain_tool_add_param_table(GWY_PLAIN_TOOL(tool), table);
    gtk_container_add(GTK_CONTAINER(options), gwy_param_table_widget(table));

    /* Graph */
    graph = gwy_graph_new(tool->gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 2);

    tool->update = gtk_dialog_add_button(dialog, _("_Update"), GWY_TOOL_RESPONSE_UPDATE);
    image = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(tool->update), image);
    gwy_plain_tool_add_clear_button(GWY_PLAIN_TOOL(tool));
    gwy_tool_add_hide_button(GWY_TOOL(tool), FALSE);
    gtk_dialog_add_button(dialog, GTK_STOCK_APPLY, GTK_RESPONSE_APPLY);
    gtk_dialog_set_default_response(dialog, GTK_RESPONSE_APPLY);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_APPLY, FALSE);
    gwy_help_add_to_tool_dialog(dialog, GWY_TOOL(tool), GWY_HELP_DEFAULT);

    update_sensitivity(tool);
    g_signal_connect_swapped(tool->table_quantity, "param-changed", G_CALLBACK(param_changed), tool);
    g_signal_connect_swapped(tool->table_options, "param-changed", G_CALLBACK(param_changed), tool);

    gtk_widget_show_all(dialog->vbox);
}

static void
gwy_tool_line_stats_data_switched(GwyTool *gwytool,
                                  GwyDataView *data_view)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(gwytool);
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(gwytool);
    gboolean ignore = (data_view == plain_tool->data_view);

    GWY_TOOL_CLASS(gwy_tool_line_stats_parent_class)->data_switched(gwytool, data_view);

    if (ignore || plain_tool->init_failed)
        return;

    if (data_view) {
        gwy_object_set_or_reset(plain_tool->layer, tool->layer_type_rect,
                                "editable", TRUE,
                                "focus", -1,
                                NULL);
        gwy_selection_set_max_objects(plain_tool->selection, 1);
        gwy_plain_tool_hold_selection(plain_tool, gwy_params_get_flags(tool->params, PARAM_HOLD_SELECTION));
    }

    update_curve(tool);
}

static void
gwy_tool_line_stats_response(GwyTool *tool,
                             gint response_id)
{
    GWY_TOOL_CLASS(gwy_tool_line_stats_parent_class)->response(tool, response_id);

    if (response_id == GTK_RESPONSE_APPLY)
        gwy_tool_line_stats_apply(GWY_TOOL_LINE_STATS(tool));
    else if (response_id == GWY_TOOL_RESPONSE_UPDATE)
        update_curve(GWY_TOOL_LINE_STATS(tool));
}

static void
gwy_tool_line_stats_data_changed(GwyPlainTool *plain_tool)
{
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(plain_tool);

    update_selected_rectangle(tool);
    update_curve(tool);
}

static void
gwy_tool_line_stats_mask_changed(GwyPlainTool *plain_tool)
{
    update_curve(GWY_TOOL_LINE_STATS(plain_tool));
}

static void
update_selected_rectangle(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwySelection *selection = plain_tool->selection;
    GwyDataField *field = plain_tool->data_field;
    gint n = selection ? gwy_selection_get_data(selection, NULL) : 0;

    gwy_rect_selection_labels_fill(tool->rlabels, n == 1 ? selection : NULL, field, NULL, tool->isel);
}

static void
gwy_tool_line_stats_selection_changed(GwyPlainTool *plain_tool,
                                      gint hint)
{
    GwyToolLineStats *tool = GWY_TOOL_LINE_STATS(plain_tool);

    g_return_if_fail(hint <= 0);
    update_selected_rectangle(tool);
    if (gwy_params_get_boolean(tool->params, PARAM_INSTANT_UPDATE)) {
        if (memcmp(tool->isel, tool->isel_prev, 4*sizeof(gint)) != 0)
            update_curve(tool);
    }
}

static void
update_sensitivity(GwyToolLineStats *tool)
{
    gtk_widget_set_sensitive(tool->update, !gwy_params_get_boolean(tool->params, PARAM_INSTANT_UPDATE));
}

static void
update_curve(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyDataField *field = plain_tool->data_field;
    GwyDataField *mask = plain_tool->mask_field;
    GwyMaskingType masking = gwy_params_get_masking(tool->params, PARAM_MASKING, &mask);
    GwyOrientation dir = gwy_params_get_enum(tool->params, PARAM_DIRECTION);
    GwyLineStatQuantity quantity = gwy_params_get_enum(tool->params, PARAM_OUTPUT_TYPE);
    GwyGraphCurveModel *gcmodel;
    gchar *result;
    gint nsel, col, row, w, h;
    const gchar *title;
    GwySIUnit *siunit;
    GwySIValueFormat *format;
    gdouble avg, rms;

    gwy_graph_model_remove_all_curves(tool->gmodel);
    gwy_param_table_info_set_valuestr(tool->table_quantity, INFO_AVERAGE, NULL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GTK_RESPONSE_APPLY, FALSE);
    if (!field)
        return;

    if (quantity == GWY_LINE_STAT_LENGTH
        && !gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(field), gwy_data_field_get_si_unit_z(field)))
        return;

    if (plain_tool->pending_updates & GWY_PLAIN_TOOL_CHANGED_SELECTION)
        update_selected_rectangle(tool);
    plain_tool->pending_updates = 0;

    gwy_assign(tool->isel_prev, tool->isel, 4);
    col = tool->isel[0];
    row = tool->isel[1];
    w = tool->isel[2]+1 - tool->isel[0];
    h = tool->isel[3]+1 - tool->isel[1];
    nsel = (w >= 4 && h >= 4) ? 1 : 0;
    gwy_debug("%d x %d at (%d, %d)", w, h, col, row);

    if (nsel == 0)
        return;

    gwy_data_field_get_line_stats_mask(field, plain_tool->mask_field, masking, tool->line, tool->weights,
                                       col, row, w, h, quantity, dir);

    gcmodel = gwy_graph_curve_model_new();
    if (!set_data_from_dataline_filtered(gcmodel, tool->line, tool->weights, 5.0)) {
        g_object_unref(gcmodel);
        return;
    }
    gwy_graph_model_add_curve(tool->gmodel, gcmodel);
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    g_object_unref(gcmodel);

    title = gettext(gwy_enum_to_string(quantity, quantities, G_N_ELEMENTS(quantities)));
    g_object_set(gcmodel, "description", title, NULL);
    g_object_set(tool->gmodel, "title", title, NULL);
    gwy_graph_model_set_units_from_data_line(tool->gmodel, tool->line);
    gwy_param_table_data_id_refilter(tool->table_options, PARAM_TARGET_GRAPH);

    siunit = gwy_data_line_get_si_unit_y(tool->line);
    format = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_MARKUP, gwy_data_line_get_avg(tool->line), NULL);
    if (quantity == GWY_LINE_STAT_RMS)
        calculate_avg_rms_for_rms(tool->line, &avg, &rms);
    else {
        avg = gwy_data_line_get_avg(tool->line);
        rms = gwy_data_line_get_rms(tool->line);
    }

    if (*format->units)
        result = g_strdup_printf("(%.4g ± %.4g) %s", avg/format->magnitude, rms/format->magnitude, format->units);
    else
        result = g_strdup_printf("%.4g ± %.4g", avg/format->magnitude, rms/format->magnitude);

    gwy_param_table_info_set_valuestr(tool->table_quantity, INFO_AVERAGE, result);
    g_free(result);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(GWY_TOOL(tool)->dialog), GTK_RESPONSE_APPLY, TRUE);
}

static void
param_changed(GwyToolLineStats *tool, gint id)
{
    GwyParams *params = tool->params;
    gboolean do_update = (id != PARAM_INSTANT_UPDATE && id != PARAM_MASKING
                          && id != PARAM_TARGET_GRAPH && id != PARAM_OPTIONS_VISIBLE);

    if (id == PARAM_INSTANT_UPDATE)
        do_update = do_update || gwy_params_get_boolean(params, PARAM_INSTANT_UPDATE);
    if (id == PARAM_MASKING)
        do_update = do_update || (GWY_PLAIN_TOOL(tool)->data_field && GWY_PLAIN_TOOL(tool)->mask_field);
    if (id < 0 || id == PARAM_INSTANT_UPDATE || id == PARAM_OUTPUT_TYPE)
        update_sensitivity(tool);
    if (do_update)
        update_curve(tool);
    if (id < 0 || id == PARAM_OUTPUT_TYPE)
        gwy_param_table_data_id_refilter(tool->table_options, PARAM_TARGET_GRAPH);
}

static void
gwy_tool_line_stats_apply(GwyToolLineStats *tool)
{
    GwyPlainTool *plain_tool = GWY_PLAIN_TOOL(tool);
    GwyGraphModel *gmodel;

    g_return_if_fail(plain_tool->selection);

    if ((gmodel = gwy_params_get_graph(tool->params, PARAM_TARGET_GRAPH))) {
        gwy_graph_model_append_curves(gmodel, tool->gmodel, 1);
        return;
    }

    gmodel = gwy_graph_model_duplicate(tool->gmodel);
    gwy_app_data_browser_add_graph_model(gmodel, plain_tool->container, TRUE);
    g_object_unref(gmodel);
}

static void
calculate_avg_rms_for_rms(GwyDataLine *dline, gdouble *avg, gdouble *rms)
{
    const gdouble *d = gwy_data_line_get_data_const(dline);
    gdouble z, s2 = 0.0, s4 = 0.0;
    gint n, i;

    n = gwy_data_line_get_res(dline);
    for (i = 0; i < n; i++) {
        z = d[i];
        s2 += z*z;
    }
    s2 /= n;
    for (i = 0; i < n; i++) {
        z = d[i];
        s4 += (z*z - s2)*(z*z - s2);
    }
    s4 /= n;

    *avg = sqrt(s2);
    *rms = 0.5*sqrt(s4)/(*avg);
}

static gint
set_data_from_dataline_filtered(GwyGraphCurveModel *gcmodel,
                                GwyDataLine *dline, GwyDataLine *weight,
                                gdouble threshold)
{
    gint res, i, n = 0;
    gdouble off, dx;
    gdouble *xdata, *ydata;
    const gdouble *d, *w;

    res = gwy_data_line_get_res(dline);
    dx = gwy_data_line_get_real(dline)/res;
    off = gwy_data_line_get_offset(dline);
    d = gwy_data_line_get_data(dline);
    w = gwy_data_line_get_data(weight);

    xdata = g_new(gdouble, res);
    ydata = g_new(gdouble, res);
    for (i = 0; i < res; i++) {
        if (w[i] >= threshold) {
            xdata[n] = i*dx + off;
            ydata[n] = d[i];
            n++;
        }
    }

    if (n)
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
    else {
        xdata[0] = ydata[0] = 0.0;
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, 1);
    }

    g_free(xdata);
    g_free(ydata);
    return n;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
