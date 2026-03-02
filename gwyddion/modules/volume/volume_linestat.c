/*
 *  $Id: volume_linestat.c 25565 2023-07-25 14:56:22Z yeti-dn $
 *  Copyright (C) 2015-2021 David Necas (Yeti).
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
#include <libprocess/brick.h>
#include <libprocess/linestats.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwymodule/gwymodule-volume.h>
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
    PARAM_ZFROM,
    PARAM_ZTO,
    PARAM_ZFROM_REAL,
    PARAM_ZTO_REAL,
    PARAM_XPOS,
    PARAM_YPOS,
    PARAM_UPDATE,

    LABEL_VALUE,
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
    GwyBrick *brick;
    const gdouble *db;
    GwyDataLine *dline;
    gdouble *buf;
    guint npts;
    guint npixels;
    guint planesize;
    guint k;
} LineStatIter;

typedef struct {
    GwyParams *params;
    GwyBrick *brick;
    GwyDataField *result;
    /* Cached input brick info. */
    gboolean units_equal;
    GwyDataLine *calibration;
    GwySIUnit *zunit;
    gdouble zmin;
    gdouble zmax;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_quantity;
    GwyParamTable *table_options;
    GwyContainer *data;
    GwySelection *image_selection;
    GwyGraphModel *gmodel;
    GwySelection *graph_selection;
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
static gboolean                    quantity_filter        (const GwyEnum *enumval,
                                                           gpointer user_data);
static void                        param_changed          (ModuleGUI *gui,
                                                           gint id);
static void                        dialog_response        (GwyDialog *dialog,
                                                           gint response,
                                                           ModuleGUI *gui);
static void                        preview                (gpointer user_data);
static void                        set_image_selection    (ModuleGUI *gui);
static void                        set_graph_selection    (ModuleGUI *gui);
static void                        point_selection_changed(ModuleGUI *gui,
                                                           gint id,
                                                           GwySelection *selection);
static void                        graph_selection_changed(ModuleGUI *gui,
                                                           gint id,
                                                           GwySelection *selection);
static void                        update_graph_curve     (ModuleGUI *gui);
static void                        update_current_value   (ModuleGUI *gui);
static gdouble                     get_data_line_range    (GwyDataLine *dataline);
static gdouble                     get_data_line_slope    (GwyDataLine *dataline);
static gdouble                     get_data_line_Rz       (GwyDataLine *dataline);
static gdouble                     get_data_line_Rt       (GwyDataLine *dataline);
static const LineStatQuantityInfo* find_quantity          (GwyLineStatQuantity quantity);
static void                        sanitise_params        (ModuleArgs *args);

/* XXX: This is more or less identical to tools/linestat.c. */
static const LineStatQuantityInfo quantities[] =  {
    { N_("Mean"),               GWY_LINE_STAT_MEAN,      gwy_data_line_get_avg,       },
    { N_("Median"),             GWY_LINE_STAT_MEDIAN,    gwy_data_line_get_median,    },
    { N_("Minimum"),            GWY_LINE_STAT_MINIMUM,   gwy_data_line_get_min,       },
    { N_("Maximum"),            GWY_LINE_STAT_MAXIMUM,   gwy_data_line_get_max,       },
    { N_("Min. position"),      GWY_LINE_STAT_MINPOS,    gwy_data_line_min_pos_i,     },
    { N_("Max. position"),      GWY_LINE_STAT_MAXPOS,    gwy_data_line_max_pos_i,     },
    { N_("Range"),              GWY_LINE_STAT_RANGE,     get_data_line_range,         },
    { N_("Slope"),              GWY_LINE_STAT_SLOPE,     get_data_line_slope,         },
    { N_("tan β<sub>0</sub>"),  GWY_LINE_STAT_TAN_BETA0, gwy_data_line_get_tan_beta0, },
    { N_("Variation"),          GWY_LINE_STAT_VARIATION, gwy_data_line_get_variation, },
    { N_("Developed length"),   GWY_LINE_STAT_LENGTH,    gwy_data_line_get_length,    },
    { N_("Ra"),                 GWY_LINE_STAT_RA,        gwy_data_line_get_ra,        },
    { N_("Rq (RMS)"),           GWY_LINE_STAT_RMS,       gwy_data_line_get_rms,       },
    { N_("Rz"),                 GWY_LINE_STAT_RZ,        get_data_line_Rz,            },
    { N_("Rt"),                 GWY_LINE_STAT_RT,        get_data_line_Rt,            },
    { N_("Skew"),               GWY_LINE_STAT_SKEW,      gwy_data_line_get_skew,      },
    { N_("Excess kurtosis"),    GWY_LINE_STAT_KURTOSIS,  gwy_data_line_get_kurtosis,  },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Summarizes profiles of volume data to a channel."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, volume_linestat)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_linestat",
                             (GwyVolumeFunc)&line_stat,
                             N_("/_Statistics/Summarize _Profiles..."),
                             GWY_STOCK_VOLUME_LINE_STATS,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Summarize profiles"));

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
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_QUANTITY, "quantity", _("_Quantity"),
                              functions, G_N_ELEMENTS(quantities), GWY_LINE_STAT_MEAN);
    gwy_param_def_add_gwyenum(paramdef, PARAM_OUTPUT_TYPE, "output_type", _("Output type"),
                              output_types, G_N_ELEMENTS(output_types), OUTPUT_IMAGE);
    gwy_param_def_add_double(paramdef, PARAM_ZFROM_REAL, NULL, _("Z _from"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_ZTO_REAL, NULL, _("Z _to"), -G_MAXDOUBLE, G_MAXDOUBLE, 0.0);
    gwy_param_def_add_int(paramdef, PARAM_ZFROM, "zfrom", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_ZTO, "zto", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_XPOS, "xpos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_int(paramdef, PARAM_YPOS, "ypos", NULL, -1, G_MAXINT, -1);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, TRUE);
    return paramdef;
}

static void
line_stat(GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;
    GwyBrick *brick = NULL;
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    LineStatOutput output_type;
    GwyLineStatQuantity quantity;
    const gchar *title;
    gint oldid, newid;

    g_return_if_fail(runtype & RUN_MODES);
    g_return_if_fail(g_type_from_name("GwyLayerPoint"));

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &oldid,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));
    args.result = NULL;
    args.brick = brick;
    args.params = gwy_params_new_from_settings(define_module_params());
    args.result = gwy_data_field_new(gwy_brick_get_xres(brick), gwy_brick_get_yres(brick), 1.0, 1.0, TRUE);
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
        gwy_app_channel_log_add(data, -1, newid, "volume::volume_linestat", NULL);
    }
    else if (output_type == OUTPUT_PREVIEW)
        gwy_container_set_object(data, gwy_app_get_brick_preview_key_for_id(oldid), args.result);
    else {
        g_assert_not_reached();
    }

end:
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *graph, *area, *dataview, *align;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;
    GwyDataField *field = args->result;
    GwyGraphCurveModel *gcmodel;
    GwyDialogOutcome outcome;
    GwyBrick *brick = args->brick;
    const guchar *gradient;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.vf = gwy_si_unit_get_format_with_digits(args->zunit, GWY_SI_UNIT_FORMAT_VFMARKUP,
                                                fmax(fabs(args->zmax), fabs(args->zmin)), 5, NULL);
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), field);
    if (gwy_container_gis_string(data, gwy_app_get_brick_palette_key_for_id(id), &gradient))
        gwy_container_set_const_string(gui.data, gwy_app_get_data_palette_key_for_id(0), gradient);

    gui.dialog = gwy_dialog_new(_("Summarize Volume Profiles"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, FALSE, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), dataview);
    gui.image_selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Point", 1, TRUE);

    gui.gmodel = gwy_graph_model_new();
    g_object_set(gui.gmodel,
                 "label-visible", FALSE,
                 "si-unit-x", args->zunit,
                 "si-unit-y", gwy_brick_get_si_unit_w(brick),
                 NULL);

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "mode", GWY_GRAPH_CURVE_LINE, NULL);
    gwy_graph_model_add_curve(gui.gmodel, gcmodel);
    g_object_unref(gcmodel);

    graph = gwy_graph_new(gui.gmodel);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    area = gwy_graph_get_area(GWY_GRAPH(graph));
    gwy_graph_area_set_status(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gui.graph_selection = gwy_graph_area_get_selection(GWY_GRAPH_AREA(area), GWY_GRAPH_STATUS_XSEL);
    gwy_selection_set_max_objects(gui.graph_selection, 1);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 4);

    table = gui.table_quantity = gwy_param_table_new(args->params);
    gwy_param_table_append_combo(table, PARAM_QUANTITY);
    gwy_param_table_combo_set_filter(table, PARAM_QUANTITY, quantity_filter, args, NULL);
    gwy_param_table_append_info(table, LABEL_VALUE, _("Value"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_entry(table, PARAM_ZFROM_REAL);
    gwy_param_table_set_no_reset(table, PARAM_ZFROM_REAL, TRUE);
    gwy_param_table_entry_set_value_format(table, PARAM_ZFROM_REAL, gui.vf);
    gwy_param_table_append_entry(table, PARAM_ZTO_REAL);
    gwy_param_table_set_no_reset(table, PARAM_ZTO_REAL, TRUE);
    gwy_param_table_entry_set_value_format(table, PARAM_ZTO_REAL, gui.vf);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    table = gui.table_options = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_OUTPUT_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);

    set_image_selection(&gui);
    set_graph_selection(&gui);
    g_signal_connect_swapped(gui.table_quantity, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_options, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.image_selection, "changed", G_CALLBACK(point_selection_changed), &gui);
    g_signal_connect_swapped(gui.graph_selection, "changed", G_CALLBACK(graph_selection_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);
    g_object_unref(gui.gmodel);
    gwy_si_unit_value_format_free(gui.vf);

    return outcome;
}

static gboolean
quantity_filter(const GwyEnum *enumval, gpointer user_data)
{
    ModuleArgs *args = (ModuleArgs*)user_data;

    if (args->units_equal)
        return TRUE;
    return enumval->value != GWY_LINE_STAT_LENGTH;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyBrick *brick = args->brick;

    if (id < 0 || id == PARAM_ZFROM) {
        gwy_param_table_set_double(gui->table_quantity, PARAM_ZFROM_REAL,
                                   gwy_brick_ktor_cal(brick, gwy_params_get_int(params, PARAM_ZFROM) - 0.5));
    }
    if (id < 0 || id == PARAM_ZTO) {
        gwy_param_table_set_double(gui->table_quantity, PARAM_ZTO_REAL,
                                   gwy_brick_ktor_cal(brick, gwy_params_get_int(params, PARAM_ZTO) + 0.5));
    }
    if (id == PARAM_ZFROM_REAL || id == PARAM_ZTO_REAL)
        set_graph_selection(gui);

    if (id != PARAM_UPDATE && id != PARAM_OUTPUT_TYPE && id != PARAM_XPOS && id != PARAM_YPOS)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET)
        gwy_selection_clear(gui->graph_selection);
}

static void
set_image_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    gdouble xy[2];

    xy[0] = gwy_brick_itor(args->brick, col);
    xy[1] = gwy_brick_jtor(args->brick, row);
    gwy_selection_set_object(gui->image_selection, 0, xy);
}

static void
set_graph_selection(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gdouble z1z2[2];

    z1z2[0] = gwy_params_get_double(args->params, PARAM_ZFROM_REAL);
    z1z2[1] = gwy_params_get_double(args->params, PARAM_ZTO_REAL);
    gwy_debug("params [%g..%g] full range [%g..%g], check: %d %d",
              z1z2[0], z1z2[1], args->zmin, args->zmax, z1z2[0] <= args->zmin, z1z2[1] >= args->zmax);
    if (z1z2[0] <= args->zmin && z1z2[1] >= args->zmax)
        gwy_selection_clear(gui->graph_selection);
    else
        gwy_selection_set_object(gui->graph_selection, 0, z1z2);
}

static void
point_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint i, xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
    gdouble xy[2];

    if (!gwy_selection_get_object(selection, 0, xy)) {
        gwy_params_set_int(args->params, PARAM_XPOS, xres/2);
        gwy_params_set_int(args->params, PARAM_YPOS, yres/2);
    }
    else {
        i = gwy_brick_rtoi(brick, xy[0]);
        gwy_params_set_int(args->params, PARAM_XPOS, CLAMP(i, 0, xres-1));
        i = gwy_brick_rtoj(brick, xy[1]);
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
    GwyBrick *brick = args->brick;
    GwyDataLine *line = gwy_data_line_new(1, 1.0, FALSE);
    gint col = gwy_params_get_int(args->params, PARAM_XPOS);
    gint row = gwy_params_get_int(args->params, PARAM_YPOS);
    GwyGraphCurveModel *gcmodel = gwy_graph_model_get_curve(gui->gmodel, 0);
    gdouble *xdata, *ydata;

    gwy_brick_extract_line(brick, line, col, row, 0, col, row, gwy_brick_get_zres(brick), TRUE);
    if (args->calibration) {
        xdata = gwy_data_line_get_data(args->calibration);
        ydata = gwy_data_line_get_data(line);
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, gwy_brick_get_zres(brick));
    }
    else
        gwy_graph_curve_model_set_data_from_dataline(gcmodel, line, 0, 0);
    g_object_unref(line);

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
    gdouble v;
    gchar *s;

    v = gwy_data_field_get_val(args->result, col, row);
    unit = gwy_data_field_get_si_unit_z(args->result);
    vf = gui->vf = gwy_si_unit_get_format_with_digits(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, v, 3, gui->vf);
    s = g_strdup_printf("%.*f%s%s", vf->precision, v/vf->magnitude, *vf->units ? " " : "", vf->units);
    gwy_param_table_info_set_valuestr(gui->table_quantity, LABEL_VALUE, s);
    g_free(s);
}

static void
graph_selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint id, GwySelection *selection)
{
    ModuleArgs *args = gui->args;
    GwyBrick *brick = args->brick;
    gint zres = gwy_brick_get_zres(brick);
    gdouble z1z2[2];
    gint zfrom = -1, zto = -1;

    if (gwy_selection_get_object(selection, 0, z1z2)) {
        zfrom = CLAMP(gwy_brick_rtok_cal(brick, z1z2[0])+0.49, 0, zres-1);
        zto = CLAMP(gwy_brick_rtok_cal(brick, z1z2[1])+0.5, 0, zres-1);
        GWY_ORDER(gint, zfrom, zto);
        if (zto - zfrom < 2)
            zfrom = zto = -1;
    }

    if (zfrom == -1) {
        zfrom = 0;
        zto = zres-1;
    }
    gwy_params_set_int(args->params, PARAM_ZFROM, zfrom);
    gwy_params_set_int(args->params, PARAM_ZTO, zto);

    gwy_param_table_param_changed(gui->table_quantity, PARAM_ZFROM);
    gwy_param_table_param_changed(gui->table_quantity, PARAM_ZTO);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_data_field_data_changed(gui->args->result);
    update_graph_curve(gui);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
line_stat_iter_init(LineStatIter *iter, GwyBrick *brick,
                    gint kfrom, gint kto,
                    gint zfrom, gint zto)
{
    gwy_clear(iter, 1);
    iter->brick = brick;
    iter->npts = zto - zfrom;
    iter->npixels = kto - kfrom;
    iter->planesize = brick->xres*brick->yres;
    iter->db = gwy_brick_get_data_const(brick) + zfrom*iter->planesize + kfrom;
    iter->buf = g_new(gdouble, MIN(BLOCK_SIZE, iter->npixels) * iter->npts);
    iter->dline = gwy_data_line_new(1, 1.0, FALSE);
    iter->k = (guint)(-1);
    /* Sets up line properties. */
    gwy_brick_extract_line(brick, iter->dline, 0, 0, zfrom, 0, 0, zto, TRUE);
}

static void
line_stat_iter_next(LineStatIter *iter)
{
    guint blocksize, npts, kk, m, planesize;

    npts = iter->npts;
    planesize = iter->planesize;
    iter->k++;
    g_return_if_fail(iter->k < iter->npixels);

    kk = iter->k % BLOCK_SIZE;
    if (!kk) {
        blocksize = MIN(BLOCK_SIZE, iter->npixels - iter->k);
        for (m = 0; m < npts; m++) {
            const gdouble *db = iter->db + m*planesize + iter->k;
            for (kk = 0; kk < blocksize; kk++)
                iter->buf[kk*npts + m] = db[kk];
        }
        kk = 0;
    }
    memcpy(iter->dline->data, iter->buf + kk*npts, npts * sizeof(gdouble));
}

static void
line_stat_iter_free(LineStatIter *iter)
{
    g_free(iter->buf);
    gwy_object_unref(iter->dline);
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

static gdouble
get_data_line_slope(GwyDataLine *dataline)
{
    gdouble b;
    gwy_data_line_get_line_coeffs(dataline, NULL, &b);
    return b*gwy_data_line_get_res(dataline)/gwy_data_line_get_real(dataline);
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyLineStatQuantity quantity = gwy_params_get_enum(params, PARAM_QUANTITY);
    gint zfrom = gwy_params_get_int(params, PARAM_ZFROM);
    gint zto = gwy_params_get_int(params, PARAM_ZTO);
    GwyBrick *brick = args->brick;
    GwyDataField *field = args->result;
    GwySIUnit *imgunit, *wunit;
    GwyDataLine *calibration = args->calibration;
    gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick), zres = gwy_brick_get_zres(brick);
    LineStatFunc lsfunc = NULL;
    gint i, j;
    guint k;
    gdouble *data, val, zreal, zoffset;

    /* Quantities we handle (somewhat inefficiently) by using DataLine statistics. */
    lsfunc = find_quantity(quantity)->func;

    if (zfrom == -1 && zto == -1) {
        zfrom = 0;
        zto = zres;
    }
    gwy_brick_extract_xy_plane(brick, field, 0);

    /* Use an iterator interface to formally process data profile by profle, but physically extract them from the
     * brick by larger blocks, gaining a speedup about 3 from the much improved memory access pattern. */
#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            private(k) \
            shared(brick,xres,yres,zfrom,zto,field,lsfunc)
#endif
    {
        LineStatIter iter;
        guint kfrom = gwy_omp_chunk_start(xres*yres);
        guint kto = gwy_omp_chunk_end(xres*yres);

        line_stat_iter_init(&iter, brick, kfrom, kto, zfrom, zto);
        for (k = kfrom; k < kto; k++) {
            line_stat_iter_next(&iter);
            field->data[k] = lsfunc(iter.dline);
        }
        line_stat_iter_free(&iter);
    }

    if ((quantity == GWY_LINE_STAT_MINPOS) || (quantity == GWY_LINE_STAT_MAXPOS)) {
        gwy_data_field_add(field, zfrom);
        if (calibration) {
            data = gwy_data_field_get_data(field);
            for (i = 0; i < xres*yres; i++) {
                j = *(data);
                val = gwy_data_line_get_val(calibration, j);
                *(data++) = val;
            }
            gwy_data_field_data_changed(field);
        }
        else {
            zreal = gwy_brick_get_zreal(brick);
            zoffset = gwy_brick_get_zoffset(brick);
            gwy_data_field_multiply(field, zreal/zres);
            gwy_data_field_add(field, zoffset);
        }
    }

    imgunit = gwy_data_field_get_si_unit_z(field);
    wunit = gwy_brick_get_si_unit_w(brick);

    if ((quantity == GWY_LINE_STAT_MINPOS) || (quantity == GWY_LINE_STAT_MAXPOS))
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), args->zunit);
    else if ((quantity == GWY_LINE_STAT_TAN_BETA0) || (quantity == GWY_LINE_STAT_SLOPE))
        gwy_si_unit_divide(wunit, args->zunit, imgunit);
    else if ((quantity == GWY_LINE_STAT_SKEW) || (quantity == GWY_LINE_STAT_KURTOSIS))
        gwy_si_unit_set_from_string(imgunit, NULL);
    else if (quantity == GWY_LINE_STAT_VARIATION)
        gwy_si_unit_multiply(wunit, args->zunit, imgunit);

    gwy_data_field_invalidate(field);
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
    GwyBrick *brick = args->brick;
    GwyDataLine *calibration;
    GwySIUnit *wunit;

    calibration = gwy_brick_get_zcalibration(brick);
    if (calibration && (gwy_brick_get_zres(brick) != gwy_data_line_get_res(calibration)))
        calibration = NULL;
    args->calibration = calibration;

    wunit = gwy_brick_get_si_unit_w(brick);
    args->zunit = (calibration ? gwy_data_line_get_si_unit_y(calibration) : gwy_brick_get_si_unit_z(brick));
    args->units_equal = gwy_si_unit_equal(wunit, args->zunit);
    if (!args->units_equal) {
        if (gwy_params_get_enum(params, PARAM_QUANTITY) == GWY_LINE_STAT_LENGTH)
            gwy_params_set_enum(params, PARAM_QUANTITY, GWY_LINE_STAT_TAN_BETA0);
    }
    args->zmin = (calibration ? gwy_data_line_get_min(calibration) : gwy_brick_get_zoffset(brick));
    args->zmax = (calibration ? gwy_data_line_get_max(calibration) : args->zmin + gwy_brick_get_zreal(brick));

    sanitise_one_param(params, PARAM_XPOS, 0, gwy_brick_get_xres(brick)-1, gwy_brick_get_xres(brick)/2);
    sanitise_one_param(params, PARAM_YPOS, 0, gwy_brick_get_yres(brick)-1, gwy_brick_get_yres(brick)/2);
    sanitise_one_param(params, PARAM_ZFROM, 0, gwy_brick_get_zres(brick)-1, 0);
    sanitise_one_param(params, PARAM_ZTO, 0, gwy_brick_get_zres(brick)-1, gwy_brick_get_zres(brick)-1);

    gwy_params_set_double(params, PARAM_ZFROM_REAL,
                          gwy_brick_ktor_cal(brick, gwy_params_get_int(params, PARAM_ZFROM) - 0.5));
    gwy_params_set_double(params, PARAM_ZTO_REAL,
                          gwy_brick_ktor_cal(brick, gwy_params_get_int(params, PARAM_ZTO) + 0.5));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
