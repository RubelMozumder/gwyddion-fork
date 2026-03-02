/*
 *  $Id: cmap_linestat.c 24458 2021-11-03 13:59:05Z yeti-dn $
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
#include <libprocess/correct.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "libgwyddion/gwyomp.h"

#define RUN_MODES (GWY_RUN_INTERACTIVE)

enum {
    PREVIEW_SIZE = 360,
    /* 16 is good for current processors; increasing it to 32 might not hurt in the future. */
    BLOCK_SIZE = 16,
};

enum {
    PARAM_QUANTITY,
    PARAM_OUTPUT_TYPE,
    PARAM_CURVE,
    PARAM_SEGMENT,
    PARAM_ENABLE_SEGMENT,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_UPDATE,

    LABEL_VALUE,
    LABEL_INTERPOLATED,
};

typedef enum {
    OUTPUT_IMAGE   = 0,
    OUTPUT_PREVIEW = 1,
    NOUTPUTS
} LineStatOutput;

typedef gdouble (*LineStatFunc)(GwyDataLine *dataline);

typedef struct {
    const gchar *name;
    GwyLineStatQuantity quantity;
    LineStatFunc func;
} LineStatQuantityInfo;

typedef struct {
    GwyParams *params;
    GwyLawn *lawn;
    GwyDataField *result;
    GwyDataField *mask;
    /* Cached input data properties. */
    gint nsegments;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_quantity;
    GwyParamTable *table_options;
    GwyContainer *data;
    GwySelection *image_selection;
    GwyGraphModel *gmodel;
    GwySIValueFormat *vf;
} ModuleGUI;

static gboolean                    module_register        (void);
static GwyParamDef*                define_module_params   (void);
static void                        line_stat              (GwyContainer *data,
                                                           GwyRunType runtype);
static void                        execute                (ModuleArgs *args);
static GwyDialogOutcome            run_gui                (ModuleArgs *args,
                                                           GwyContainer *data,
                                                           gint id);
static void                        param_changed          (ModuleGUI *gui,
                                                           gint id);
static void                        preview                (gpointer user_data);
static void                        set_image_selection    (ModuleGUI *gui);
static void                        point_selection_changed(ModuleGUI *gui,
                                                           gint id,
                                                           GwySelection *selection);
static void                        update_graph_curve     (ModuleGUI *gui);
static void                        update_current_value   (ModuleGUI *gui);
static gdouble                     get_data_line_range    (GwyDataLine *dataline);
static gdouble                     get_data_line_Rz       (GwyDataLine *dataline);
static gdouble                     get_data_line_Rt       (GwyDataLine *dataline);
static gint                        extract_data_line      (GwyLawn *lawn,
                                                           GwyDataLine *target,
                                                           gint col,
                                                           gint row,
                                                           gint curveno,
                                                           gint segment);
static const LineStatQuantityInfo* find_quantity          (GwyLineStatQuantity quantity);
static void                        sanitise_params        (ModuleArgs *args);

/* XXX: This is more or less identical to tools/linestat.c. */
static const LineStatQuantityInfo quantities[] =  {
    { N_("Mean"),            GWY_LINE_STAT_MEAN,     gwy_data_line_get_avg,      },
    { N_("Median"),          GWY_LINE_STAT_MEDIAN,   gwy_data_line_get_median,   },
    { N_("Minimum"),         GWY_LINE_STAT_MINIMUM,  gwy_data_line_get_min,      },
    { N_("Maximum"),         GWY_LINE_STAT_MAXIMUM,  gwy_data_line_get_max,      },
    /* Positions require a secondary curve for abscissa.  With those available we can also consider the other
     * quantities which require an abscissa.
    { N_("Min. position"),   GWY_LINE_STAT_MINPOS,   gwy_data_line_min_pos_i,    },
    { N_("Max. position"),   GWY_LINE_STAT_MAXPOS,   gwy_data_line_max_pos_i,    },
    { N_("Slope"),              GWY_LINE_STAT_SLOPE,     get_data_line_slope,         },
    { N_("tan β<sub>0</sub>"),  GWY_LINE_STAT_TAN_BETA0, gwy_data_line_get_tan_beta0, },
    { N_("Variation"),          GWY_LINE_STAT_VARIATION, gwy_data_line_get_variation, },
    { N_("Developed length"),   GWY_LINE_STAT_LENGTH,    gwy_data_line_get_length,    },
    */
    { N_("Range"),           GWY_LINE_STAT_RANGE,    get_data_line_range,        },
    { N_("Ra"),              GWY_LINE_STAT_RA,       gwy_data_line_get_ra,       },
    { N_("Rq (RMS)"),        GWY_LINE_STAT_RMS,      gwy_data_line_get_rms,      },
    { N_("Rz"),              GWY_LINE_STAT_RZ,       get_data_line_Rz,           },
    { N_("Rt"),              GWY_LINE_STAT_RT,       get_data_line_Rt,           },
    { N_("Skew"),            GWY_LINE_STAT_SKEW,     gwy_data_line_get_skew,     },
    { N_("Excess kurtosis"), GWY_LINE_STAT_KURTOSIS, gwy_data_line_get_kurtosis, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Summarizes curves in curve map data to an image."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_linestat)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_linestat",
                                (GwyCurveMapFunc)&line_stat,
                                N_("/_Summarize Curves..."),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Summarize curves"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum output_types[] = {
        { N_("_Extract image"), OUTPUT_IMAGE,   },
        { N_("Set _preview"),   OUTPUT_PREVIEW, },
    };
    static GwyEnum *functions = NULL;
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    functions = gwy_enum_fill_from_struct(NULL, G_N_ELEMENTS(quantities), quantities, sizeof(LineStatQuantityInfo),
                                          G_STRUCT_OFFSET(LineStatQuantityInfo, name),
                                          G_STRUCT_OFFSET(LineStatQuantityInfo, quantity));

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_curve_map_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_QUANTITY, "quantity", _("_Quantity"),
                              functions, G_N_ELEMENTS(quantities), GWY_LINE_STAT_MEAN);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output type"),
                              output_types, G_N_ELEMENTS(output_types), OUTPUT_IMAGE);
    gwy_param_def_add_lawn_curve(paramdef, PARAM_CURVE, "curve", NULL);
    gwy_param_def_add_lawn_segment(paramdef, PARAM_SEGMENT, "segment", NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_ENABLE_SEGMENT, "enable_segment", NULL, FALSE);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
line_stat(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyLawn *lawn = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    LineStatOutput output_type;
    GwyLineStatQuantity quantity;
    const gchar *title;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_LAWN(lawn));
    args.lawn = lawn;
    args.nsegments = gwy_lawn_get_n_segments(lawn);
    args.params = gwy_params_new_from_settings(define_module_params());
    args.result = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                     gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn), TRUE);
    gwy_data_field_set_xoffset(args.result, gwy_lawn_get_xoffset(lawn));
    gwy_data_field_set_yoffset(args.result, gwy_lawn_get_yoffset(lawn));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(args.result), gwy_lawn_get_si_unit_xy(lawn));
    args.mask = gwy_data_field_new_alike(args.result, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.mask), NULL);
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, oldid);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    output_type = gwy_params_get_enum(args.params, PARAM_OUTPUT_TYPE);
    quantity = gwy_params_get_enum(args.params, PARAM_QUANTITY);
    if (output_type == OUTPUT_IMAGE) {
        newid = gwy_app_data_browser_add_data_field(args.result, data, TRUE);
        title = gwy_sgettext(find_quantity(quantity)->name);
        gwy_app_set_data_field_title(data, newid, title);
        if (gwy_data_field_get_max(args.mask) > 0.0)
            gwy_container_set_object(data, gwy_app_get_mask_key_for_id(newid), args.mask);
        gwy_app_channel_log_add(data, -1, newid, "cmap::cmap_linestat", NULL);
    }
    else if (output_type == OUTPUT_PREVIEW)
        gwy_container_set_object(data, gwy_app_get_lawn_preview_key_for_id(oldid), args.result);
    else {
        g_assert_not_reached();
    }

end:
    g_object_unref(args.result);
    g_object_unref(args.mask);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *dataview, *align;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field = args->result;
    GwyGraphCurveModel *gcmodel;
    GwyDialogOutcome outcome;
    GwyVectorLayer *vlayer = NULL;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_lawn_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Summarize Map Curves"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    vlayer = g_object_new(g_type_from_name("GwyLayerPoint"), NULL);
    gwy_vector_layer_set_selection_key(vlayer, "/0/select/pointer");
    gwy_data_view_set_top_layer(GWY_DATA_VIEW(dataview), vlayer);
    gui.image_selection = gwy_vector_layer_ensure_selection(vlayer);
    gwy_selection_set_max_objects(gui.image_selection, 1);

    gui.gmodel = gwy_graph_model_new();
    g_object_set(gui.gmodel,
                 "label-visible", FALSE,
                 "axis-label-bottom", _("sample"),
                 NULL);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(GWY_DIALOG(gui.dialog), hbox, TRUE, TRUE, 4);

    table = gui.table_quantity = gwy_param_table_new(args->params);
    gwy_param_table_append_lawn_curve(table, PARAM_CURVE, args->lawn);
    if (args->nsegments) {
        gwy_param_table_append_lawn_segment(table, PARAM_SEGMENT, args->lawn);
        gwy_param_table_add_enabler(table, PARAM_ENABLE_SEGMENT, PARAM_SEGMENT);
    }
    gwy_param_table_append_combo(table, PARAM_QUANTITY);
    gwy_param_table_append_info(table, LABEL_VALUE, _("Value"));
    gwy_param_table_append_info(table, LABEL_INTERPOLATED, NULL);
    gwy_param_table_append_separator(table);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    set_image_selection(&gui);
    g_signal_connect_swapped(gui.table_quantity, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.image_selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);
    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    if (id != PARAM_UPDATE && id != PARAM_OUTPUT_TYPE && id != PARAM_XPOS && id != PARAM_YPOS)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
set_image_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = (col + 0.5)*gwy_lawn_get_dx(args->lawn);
    xy[1] = (row + 0.5)*gwy_lawn_get_dy(args->lawn);
    gwy_selection_set_object(gui->image_selection, 0, xy);
}

static void
point_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyLawn *lawn = args->lawn;
    gint i, xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    gdouble xy[2];

    if (!gwy_selection_get_object(selection, 0, xy)) {
        gwy_params_set_int(args->params, PARAM_XPOS, xres/2);
        gwy_params_set_int(args->params, PARAM_YPOS, yres/2);
    }
    else {
        i = GWY_ROUND(floor(xy[0]/gwy_lawn_get_dx(lawn)));
        gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
        i = GWY_ROUND(ceil(xy[1]/gwy_lawn_get_dy(lawn)));
        gwy_params_set_int(args->params, PARAM_YPOS, CLAMP(i, 0, yres-1));
    }
    gwy_param_table_param_changed(gui->table_quantity, PARAM_XPOS);
    gwy_param_table_param_changed(gui->table_quantity, PARAM_YPOS);
    update_graph_curve(gui);
}

static void
update_graph_curve(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyLawn *lawn = args->lawn;
    GwyDataLine *line = gwy_data_line_new(1, 1.0, FALSE);
    gint col = gwy_params_get_int(params, PARAM_XPOS);
    gint row = gwy_params_get_int(params, PARAM_YPOS);
    gint curveno = gwy_params_get_int(params, PARAM_CURVE);
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->gmodel, 0);
    const gchar *label;

    if (extract_data_line(lawn, line, col, row, curveno, segment))
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);
    else
        gwy_graph_curve_model_set_data(gcmodel, NULL, NULL, 0);
    g_object_unref(line);
    label = gwy_lawn_get_curve_label(lawn, curveno);
    g_object_set(gui->gmodel,
                 "si-unit-y", gwy_lawn_get_si_unit_curve(lawn, curveno),
                 "axis-label-left", label ? label : _("Untitled"),
                 NULL);

    update_current_value(gui);
}

static void
update_current_value(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    GwySIValueFormat *vf;
    GwySIUnit *unit;
    gdouble v, m;
    gchar *s;

    v = gwy_data_field_get_val(args->result, col, row);
    m = gwy_data_field_get_val(args->mask, col, row);
    unit = gwy_data_field_get_si_unit_z(args->result);
    vf = gui->vf = gwy_si_unit_get_format_with_digits(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, v, 3, gui->vf);
    s = g_strdup_printf("%.*f%s%s", vf->precision, v/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_param_table_info_set_valuestr(gui->table_quantity, LABEL_VALUE, s);
    g_free(s);
    gwy_param_table_info_set_valuestr(gui->table_quantity, LABEL_INTERPOLATED, m > 0.0 ? _("(interpolated)") : NULL);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    execute(args);
    gwy_data_field_data_changed(args->result);
    gwy_data_field_data_changed(args->mask);
    update_graph_curve(gui);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static gdouble
get_data_line_range(GwyDataLine *dataline)
{
    gdouble min, max;

    gwy_data_line_get_min_max(dataline, &min, &max);
    return max - min;
}

static gdouble
get_data_line_Rt(GwyDataLine *dataline)
{
    gwy_data_line_add(dataline, -gwy_data_line_get_avg(dataline));
    return gwy_data_line_get_xtm(dataline, 1, 1);
}

static gdouble
get_data_line_Rz(GwyDataLine *dataline)
{
    gwy_data_line_add(dataline, -gwy_data_line_get_avg(dataline));
    return gwy_data_line_get_xtm(dataline, 5, 1);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLineStatQuantity quantity = gwy_params_get_enum(params, PARAM_QUANTITY);
    gint curveno = gwy_params_get_int(params, PARAM_CURVE);
    gboolean segment_enabled = args->nsegments ? gwy_params_get_boolean(params, PARAM_ENABLE_SEGMENT) : FALSE;
    gint segment = segment_enabled ? gwy_params_get_int(params, PARAM_SEGMENT) : -1;
    GwyLawn *lawn = args->lawn;
    GwyDataField *field = args->result, *mask = args->mask;
    GwySIUnit *imgunit, *valunit;
    gint xres = gwy_lawn_get_xres(lawn), yres = gwy_lawn_get_yres(lawn);
    LineStatFunc lsfunc = NULL;
    gdouble *data, *mdata;

    /* Quantities we handle (somewhat inefficiently) by using DataLine statistics. */
    lsfunc = find_quantity(quantity)->func;

    gwy_data_field_clear(mask);
    data = gwy_data_field_get_data(field);
    mdata = gwy_data_field_get_data(mask);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(lawn,xres,yres,curveno,segment,data,mdata,lsfunc)
#endif
    {
        GwyDataLine *dline = gwy_data_line_new(1, 1.0, FALSE);
        guint kfrom = gwy_omp_chunk_start(xres*yres);
        guint kto = gwy_omp_chunk_end(xres*yres);
        guint k;

        for (k = kfrom; k < kto; k++) {
            if (extract_data_line(lawn, dline, k % xres, k/xres, curveno, segment))
                data[k] = lsfunc(dline);
            else
                mdata[k] = 1.0;
        }

        g_object_unref(dline);
    }

    imgunit = gwy_data_field_get_si_unit_z(field);
    valunit = gwy_lawn_get_si_unit_curve(lawn, curveno);

    if ((quantity == GWY_LINE_STAT_SKEW) || (quantity == GWY_LINE_STAT_KURTOSIS))
        gwy_si_unit_set_from_string(imgunit, NULL);
    else
        gwy_si_unit_assign(imgunit, valunit);

    if (gwy_data_field_get_max(mask) > 0.0)
        gwy_data_field_laplace_solve(field, mask, -1, 1.0);
}

static gint
extract_data_line(GwyLawn *lawn, GwyDataLine *target,
                  gint col, gint row, gint curveno, gint segment)
{
    gint pos = 0, len;
    const gdouble *cdata;
    const gint *segments;
    gdouble *ldata;

    cdata = gwy_lawn_get_curve_data_const(lawn, col, row, curveno, &len);
    if (!len)
        return 0;

    if (segment >= 0) {
        segments = gwy_lawn_get_segments(lawn, col, row, NULL);
        pos = segments[2*segment];
        len = segments[2*segment + 1] - pos;
        if (!len)
            return 0;
    }

    gwy_data_line_resample(target, len, GWY_INTERPOLATION_NONE);
    ldata = gwy_data_line_get_data(target);
    gwy_assign(ldata, cdata + pos, len);
    gwy_data_line_set_real(target, len);

    return len;
}

static const LineStatQuantityInfo*
find_quantity(GwyLineStatQuantity quantity)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(quantities); i++) {
        if (quantities[i].quantity == quantity)
            return quantities + i;
    }
    g_assert_not_reached();
    return NULL;
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
