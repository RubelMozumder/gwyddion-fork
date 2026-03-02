/*
 *  $Id: tip_blind.c 24857 2022-07-14 11:08:49Z yeti-dn $
 *  Copyright (C) 2004-2022 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/tip.h>
#include <libgwydgets/gwydataview.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    MIN_RES = 3,
    MAX_RES = 150,
    MIN_STRIPES = 2,
    MAX_STRIPES = 60,
};

enum {
    PARAM_SOURCE,
    PARAM_XRES,
    PARAM_YRES,
    PARAM_SAME_RESOLUTION,
    PARAM_THRESHOLD,
    PARAM_USE_BOUNDARIES,
    PARAM_SPLIT_TO_STRIPES,
    PARAM_NSTRIPES,
    PARAM_PREVIEW_STRIPE,
    PARAM_CREATE_IMAGES,
    PARAM_PLOT_SIZE_GRAPH,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *tip;
    GwyGraphModel *gmodel;
    /* Stripe results */
    guint nstripes;
    GwyDataField **stripetips;
    gboolean *goodtips;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *dataview;
    GwyParamTable *table;
    GwyContainer *data;
} ModuleGUI;

typedef GwyDataField* (*TipFunc)(GwyDataField *tip,
                                 GwyDataField *surface,
                                 gdouble threshold,
                                 gboolean use_edges,
                                 gint *count,
                                 GwySetFractionFunc set_fraction,
                                 GwySetMessageFunc set_message);

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             tip_blind           (GwyContainer *data,
                                             GwyRunType runtype);
static gint             create_output_field (GwyDataField *field,
                                             GwyContainer *data,
                                             gint id,
                                             gint i,
                                             gint nstripes);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             update_tip_preview  (ModuleGUI *gui,
                                             gboolean must_set_it);
static void             dialog_response     (ModuleGUI *gui,
                                             gint response);
static gboolean         resize_stripe_tips  (ModuleArgs *args,
                                             guint n);
static void             clear_tip_images    (ModuleArgs *args);
static gboolean         source_image_filter (GwyContainer *data,
                                             gint id,
                                             gpointer user_data);
static gboolean         execute             (ModuleArgs *args,
                                             gboolean full,
                                             GtkWindow *wait_window);
static void             tip_curvatures      (GwyDataField *tipfield,
                                             gdouble *pc1,
                                             gdouble *pc2);
static void             size_plot           (ModuleArgs *args);
static void             sanitise_params     (ModuleArgs *args,
                                             GwyAppDataId dataid);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Blind estimation of SPM tip using Villarubia's algorithm."),
    "Petr Klapetek <petr@klapetek.cz>",
    "2.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, tip_blind)

static gboolean
module_register(void)
{
    gwy_process_func_register("tip_blind",
                              (GwyProcessFunc)&tip_blind,
                              N_("/SPM M_odes/_Tip/_Blind Estimation..."),
                              GWY_STOCK_TIP_ESTIMATION,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Blind tip estimation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_image_id(paramdef, PARAM_SOURCE, NULL, _("Related _data"));
    gwy_param_def_add_int(paramdef, PARAM_XRES, "xres", _("_Width"), MIN_RES, MAX_RES, 10);
    gwy_param_def_add_int(paramdef, PARAM_YRES, "yres", _("_Height"), MIN_RES, MAX_RES, 10);
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("Noise suppression t_hreshold"),
                             G_MINDOUBLE, G_MAXDOUBLE, 1e-10);
    gwy_param_def_add_boolean(paramdef, PARAM_SAME_RESOLUTION, "same_resolution", _("_Same resolution"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_BOUNDARIES, "use_boundaries", _("Use _boundaries"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_SPLIT_TO_STRIPES, "split_to_stripes", _("_Split to stripes"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_NSTRIPES, "nstripes", _("_Split to stripes"), MIN_STRIPES, MAX_STRIPES, 16);
    gwy_param_def_add_int(paramdef, PARAM_PREVIEW_STRIPE, NULL, _("_Preview stripe"), 1, MAX_STRIPES, 1);
    gwy_param_def_add_boolean(paramdef, PARAM_CREATE_IMAGES, "create_images", _("Create tip i_mages"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_PLOT_SIZE_GRAPH, "plot_size_graph", _("Plot size _graph"), TRUE);
    return paramdef;
}

static void
tip_blind(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    GwyParams *params;
    ModuleArgs args;
    GwyAppDataId dataid;
    gint id, i, nstripes;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &dataid.datano,
                                     0);
    g_return_if_fail(args.field);

    dataid.id = id;
    args.tip = gwy_data_field_new(3, 3, 1.0, 1.0, TRUE);
    args.gmodel = gwy_graph_model_new();
    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args, dataid);
    /* TODO: limit resolutions to some fraction of image size? */
    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(params);
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        goto end;

    /* XXX: This is extremely weird because we create results from the last successful execution.  But the user could
     * have changed all options after it.  So we only can take the output options and combine them what we have
     * available calculated. */
    if ((nstripes = args.nstripes)) {
        if (gwy_params_get_boolean(params, PARAM_CREATE_IMAGES)) {
            for (i = 0; i < nstripes; i++) {
                if (args.goodtips[i] && args.stripetips[i])
                    create_output_field(args.stripetips[i], data, id, i, nstripes);
            }
        }
        if (gwy_params_get_boolean(params, PARAM_PLOT_SIZE_GRAPH)) {
            if (gwy_graph_model_get_n_curves(args.gmodel))
                gwy_app_data_browser_add_graph_model(args.gmodel, data, TRUE);
        }
    }
    else
        create_output_field(args.tip, data, id, -1, -1);

end:
    resize_stripe_tips(&args, 0);
    g_object_unref(args.tip);
    g_object_unref(args.gmodel);
    g_object_unref(params);
}

static gint
create_output_field(GwyDataField *field, GwyContainer *data, gint id, gint i, gint nstripes)
{
    gint newid = gwy_app_data_browser_add_data_field(field, data, TRUE);
    gwy_app_sync_data_items(data, data, id, newid, FALSE, GWY_DATA_ITEM_GRADIENT, 0);
    if (nstripes > 0) {
        gchar *title = g_strdup_printf("%s %u/%u", _("Estimated tip"), i+1, nstripes);
        gwy_container_set_string(data, gwy_app_get_data_title_key_for_id(newid), title);
    }
    else
        gwy_app_set_data_field_title(data, newid, _("Estimated tip"));
    gwy_app_channel_log_add_proc(data, -1, newid);
    return newid;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *vbox, *align, *graph;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;
    GwyDialogOutcome outcome;
    GwySIValueFormat *vf;
    gdouble rms;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object(gui.data, gwy_app_get_data_key_for_id(0), args->tip);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Blind Tip Estimation"));
    dialog = GWY_DIALOG(gui.dialog);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Run _Partial"), RESPONSE_ESTIMATE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Run _Full"), RESPONSE_REFINE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset Tip"), GWY_RESPONSE_CLEAR);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui.dialog), GTK_RESPONSE_OK, FALSE);

    hbox = gwy_hbox_new(8);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(GWY_DIALOG(dialog), hbox, FALSE, FALSE, 0);

    vbox = gwy_vbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_widget_set_size_request(align, PREVIEW_SMALL_SIZE, -1);
    gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 0);

    gui.dataview = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(gui.dataview));

    graph = gwy_graph_new(args->gmodel);
    g_object_set(args->gmodel, "label-visible", FALSE, NULL);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gwy_axis_set_visible(gwy_graph_get_axis(GWY_GRAPH(graph), GTK_POS_LEFT), FALSE);
    gwy_axis_set_visible(gwy_graph_get_axis(GWY_GRAPH(graph), GTK_POS_BOTTOM), FALSE);
    gtk_widget_set_size_request(graph, -1, 120);
    gtk_box_pack_start(GTK_BOX(vbox), graph, TRUE, TRUE, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_image_id(table, PARAM_SOURCE);
    gwy_param_table_data_id_set_filter(table, PARAM_SOURCE, source_image_filter, args->field, NULL);

    gwy_param_table_append_header(table, -1, _("Tip Size"));
    gwy_param_table_append_slider(table, PARAM_XRES);
    gwy_param_table_set_unitstr(table, PARAM_XRES, _("px"));
    gwy_param_table_append_slider(table, PARAM_YRES);
    gwy_param_table_set_unitstr(table, PARAM_YRES, _("px"));
    gwy_param_table_append_checkbox(table, PARAM_SAME_RESOLUTION);

    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_slider_set_mapping(table, PARAM_THRESHOLD, GWY_SCALE_MAPPING_LOG);
    rms = gwy_data_field_get_rms(args->field);
    vf = gwy_si_unit_get_format(gwy_data_field_get_si_unit_z(args->field), GWY_SI_UNIT_FORMAT_VFMARKUP, 1e-3*rms, NULL);
    vf->precision++;
    gwy_param_table_slider_set_factor(table, PARAM_THRESHOLD, 1.0/vf->magnitude);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD, vf->units);
    gwy_param_table_slider_restrict_range(table, PARAM_THRESHOLD, 5e-5*rms, 1.0*rms);

    gwy_param_table_append_header(table, -1, _("Stripes"));
    gwy_param_table_append_slider(table, PARAM_NSTRIPES);
    gwy_param_table_add_enabler(table, PARAM_SPLIT_TO_STRIPES, PARAM_NSTRIPES);
    gwy_param_table_append_slider(table, PARAM_PREVIEW_STRIPE);
    gwy_param_table_slider_restrict_range(table, PARAM_PREVIEW_STRIPE,
                                          1, gwy_params_get_int(args->params, PARAM_NSTRIPES));
    gwy_param_table_append_checkbox(table, PARAM_PLOT_SIZE_GRAPH);
    gwy_param_table_append_checkbox(table, PARAM_CREATE_IMAGES);
    gwy_dialog_add_param_table(dialog, table);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);

    g_signal_connect_swapped(gui.table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    gboolean same_resolution = gwy_params_get_boolean(params, PARAM_SAME_RESOLUTION);

    if (id < 0 || id == PARAM_SAME_RESOLUTION) {
        if (same_resolution)
            gwy_param_table_set_int(table, PARAM_YRES, gwy_params_get_int(params, PARAM_XRES));
    }
    if (id == PARAM_XRES && same_resolution)
        gwy_param_table_set_int(table, PARAM_YRES, gwy_params_get_int(params, PARAM_XRES));
    if (id == PARAM_YRES && same_resolution)
        gwy_param_table_set_int(table, PARAM_XRES, gwy_params_get_int(params, PARAM_YRES));

    if (id < 0 || id == PARAM_SPLIT_TO_STRIPES) {
        gboolean split_to_stripes = gwy_params_get_boolean(params, PARAM_SPLIT_TO_STRIPES);
        gwy_param_table_set_sensitive(table, PARAM_PREVIEW_STRIPE, split_to_stripes);
        gwy_param_table_set_sensitive(table, PARAM_PLOT_SIZE_GRAPH, split_to_stripes);
        gwy_param_table_set_sensitive(table, PARAM_CREATE_IMAGES, split_to_stripes);
    }
    /* Do not change range of PREVIEW_STRIPE here, it only makes sense to change after we actually calculate the
     * stripes. */
    if (id < 0 || id == PARAM_PREVIEW_STRIPE || id == PARAM_NSTRIPES)
        update_tip_preview(gui, FALSE);

    /* TODO: Check if parameters are sane for image (number of stripes, tip size) and if they are not, show
     * a message and disable the action buttons. */
}

static gboolean
source_image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    GwyDataField *field = (GwyDataField*)user_data;
    GwyDataField *source = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));

    return !gwy_data_field_check_compatibility(source, field,
                                               GWY_DATA_COMPATIBILITY_MEASURE
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
update_tip_preview(ModuleGUI *gui, gboolean must_set_it)
{
    /* XXX: This is extremely weird because we show results from the last successful execution.  But the user could
     * have changed all options after it. */
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean split_to_stripes = gwy_params_get_boolean(params, PARAM_SPLIT_TO_STRIPES);
    gint preview_stripe = gwy_params_get_int(params, PARAM_PREVIEW_STRIPE)-1;

    if (split_to_stripes && preview_stripe < args->nstripes)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->stripetips[preview_stripe]);
    else if (must_set_it)
        gwy_container_set_object(gui->data, gwy_app_get_data_key_for_id(0), args->tip);
    else
        return;
    gwy_set_data_preview_size(GWY_DATA_VIEW(gui->dataview), PREVIEW_SMALL_SIZE);
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    ModuleArgs *args = gui->args;
    gboolean ok = FALSE;

    if (response != GWY_RESPONSE_CLEAR && response != RESPONSE_ESTIMATE && response != RESPONSE_REFINE)
        return;

    gwy_graph_model_remove_all_curves(args->gmodel);
    if (response == GWY_RESPONSE_CLEAR)
        clear_tip_images(args);
    else
        ok = execute(args, response == RESPONSE_REFINE, GTK_WINDOW(gui->dialog));

    gwy_param_table_slider_restrict_range(gui->table, PARAM_PREVIEW_STRIPE,
                                          1, gwy_params_get_int(args->params, PARAM_NSTRIPES));
    update_tip_preview(gui, TRUE);
    gwy_data_field_data_changed(gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0)));
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, ok);
    /* Use the invalidate/have-data mechanism for our own means.  Invalidate will schedule a preview, but we have no
     * such thing so the dialog will remain in doesn't-have-data state until we run the tip estimation successfully. */
    if (ok)
        gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
    else
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static gboolean
prepare_tip_field(GwyDataField *tip, GwyDataField *surface, gint xres, gint yres)
{
    gboolean keep = TRUE;

    /* If user has changed the tip size, start from scratch. */
    if (gwy_data_field_get_xres(tip) != xres || gwy_data_field_get_yres(tip) != yres) {
        gwy_data_field_resample(tip, xres, yres, GWY_INTERPOLATION_NONE);
        gwy_data_field_clear(tip);
        keep = FALSE;
    }
    /* Do this always to be on the safe side (for instance during the dialog init). */
    gwy_data_field_set_xreal(tip, gwy_data_field_get_dx(surface)*gwy_data_field_get_xres(tip));
    gwy_data_field_set_yreal(tip, gwy_data_field_get_dy(surface)*gwy_data_field_get_yres(tip));
    gwy_data_field_copy_units(surface, tip);
    return keep;
}

static void
prepare_stripe_fields(ModuleArgs *args, gint nstripes, gboolean keep)
{
    guint i;

    /* If the user did not change anything relevant we can keep the tip images as they are. */
    if (resize_stripe_tips(args, nstripes) && keep)
        return;

    /* Otherwise initialise all the stripe tips from the global one.  It iself could be reset or kept, but whatever is
     * the state, we want to copy it. */
    for (i = 0; i < nstripes; i++)
        gwy_data_field_copy(args->tip, args->stripetips[i], FALSE);
}

static gboolean
execute(ModuleArgs *args, gboolean full, GtkWindow *wait_window)
{
    GwyParams *params = args->params;
    GwyDataField *surface = gwy_params_get_image(params, PARAM_SOURCE);
    GwyDataField *tip = args->tip, *stripe;
    gint txres = gwy_params_get_int(params, PARAM_XRES);
    gint tyres = gwy_params_get_int(params, PARAM_YRES);
    gdouble threshold = gwy_params_get_double(params, PARAM_THRESHOLD);
    gboolean use_boundaries = gwy_params_get_boolean(params, PARAM_USE_BOUNDARIES);
    gboolean split_to_stripes = gwy_params_get_boolean(params, PARAM_SPLIT_TO_STRIPES);
    gint nstripes = (split_to_stripes ? gwy_params_get_int(params, PARAM_NSTRIPES) : 0);
    TipFunc tipfunc = full ? gwy_tip_estimate_full : gwy_tip_estimate_partial;
    gboolean keep, ok;
    gint count = -1;

    gwy_app_wait_start(wait_window, _("Initializing..."));

    keep = prepare_tip_field(tip, surface, txres, tyres);
    prepare_stripe_fields(args, nstripes, keep);

    if (split_to_stripes) {
        gint xres = gwy_data_field_get_xres(surface), yres = gwy_data_field_get_yres(surface);
        gint anygood = 0, i;
        gchar *prefix = NULL;

        for (i = 0; i < nstripes; i++) {
            gint row = i*(yres - tyres)/nstripes;
            gint height = (i + 1)*(yres - tyres)/nstripes + tyres - row;

            /* TRANSLATORS: Prefix for the progressbar message. */
            prefix = g_strdup_printf(_("Stripe %u: "), i+1);
            if (!gwy_app_wait_set_message_prefix(prefix))
                break;
            GWY_FREE(prefix);

            /* Do not crash in the silly case. */
            if (height < tyres)
                continue;

            gwy_debug("[%u] (%u, %u) of %u", i, row, height, yres);
            count = -1;
            stripe = gwy_data_field_area_extract(surface, 0, row, xres, height);
            ok = !!tipfunc(args->stripetips[i], stripe, threshold, use_boundaries, &count,
                           gwy_app_wait_set_fraction, gwy_app_wait_set_message);
            g_object_unref(stripe);
            /* Cancelled by user */
            if (!ok)
                break;
            args->goodtips[i] = ok && count > 0;
            gwy_debug("[%u] count = %d", i, count);
            anygood += args->goodtips[i];
        }
        if (i < nstripes) {
            GWY_FREE(prefix);
            anygood = 0;
            gwy_clear(args->goodtips, nstripes);
        }
        if ((ok = anygood > 0))
            size_plot(args);
    }
    else {
        ok = (tipfunc(tip, surface, threshold, use_boundaries, &count,
                      gwy_app_wait_set_fraction, gwy_app_wait_set_message)
              && count > 0);
        gwy_debug("count = %d", count);
    }
    gwy_app_wait_finish();

    return ok;
}

static void
clear_tip_images(ModuleArgs *args)
{
    guint i;

    gwy_data_field_clear(args->tip);
    if (args->nstripes) {
        for (i = 0; i < args->nstripes; i++)
            gwy_data_field_clear(args->stripetips[i]);
        gwy_clear(args->goodtips, args->nstripes);
    }
}

static gboolean
resize_stripe_tips(ModuleArgs *args, guint n)
{
    gint xres = gwy_data_field_get_xres(args->tip), yres = gwy_data_field_get_yres(args->tip);
    guint i;

    if (args->nstripes == n) {
        if (!args->nstripes || (gwy_data_field_get_xres(args->stripetips[0]) == xres
                                && gwy_data_field_get_yres(args->stripetips[0]) == yres))
            return TRUE;
    }

    for (i = n; i < args->nstripes; i++)
        g_object_unref(args->stripetips[i]);
    if (n) {
        args->stripetips = g_renew(GwyDataField*, args->stripetips, n);
        args->goodtips = g_renew(gboolean, args->goodtips, n);
        gwy_clear(args->goodtips, n);
        for (i = args->nstripes; i < n; i++)
            args->stripetips[i] = gwy_data_field_new_alike(args->tip, TRUE);
        for (i = 0; i < MIN(n, args->nstripes); i++)
            gwy_data_field_resample(args->stripetips[i], xres, yres, GWY_INTERPOLATION_NONE);
    }
    else {
        GWY_FREE(args->stripetips);
        GWY_FREE(args->goodtips);
    }
    args->nstripes = n;
    return FALSE;
}

static void
tip_curvatures(GwyDataField *tip, gdouble *pc1, gdouble *pc2)
{
    gint xres = gwy_data_field_get_xres(tip), yres = gwy_data_field_get_yres(tip);
    gdouble dx = gwy_data_field_get_dx(tip), dy = gwy_data_field_get_dy(tip);
    const gdouble *data = gwy_data_field_get_data_const(tip);
    gdouble R = 2 + 0.25*log(xres*yres);
    gdouble sx2 = 0.0, sy2 = 0.0, sx4 = 0.0, sx2y2 = 0.0, sy4 = 0.0;
    gdouble sz = 0.0, szx = 0.0, szy = 0.0, szx2 = 0.0, szxy = 0.0, szy2 = 0.0;
    gdouble scale = sqrt(dx*dy)*R;
    gdouble xc = 0.5*xres - 0.5, yc = 0.5*yres - 0.5;
    gdouble a[21], b[6], k1, k2;
    gint i, j, n = 0;

    for (i = 0; i < yres; i++) {
        gdouble y = (2.0*i + 1.0 - yres)*dy/scale;
        for (j = 0; j < xres; j++) {
            gdouble x = (2.0*j + 1.0 - xres)*dx/scale;
            gdouble z = data[i*xres + j]/scale;
            gdouble rr = (i - yc)*(i - yc) + (j - xc)*(j - xc);
            gdouble xx = x*x, yy = y*y;

            /* Exclude also the central pixel – unreliable. */
            if (rr > R*R || rr < 1e-6)
                continue;

            sx2 += xx;
            sx2y2 += xx*yy;
            sy2 += yy;
            sx4 += xx*xx;
            sy4 += yy*yy;

            sz += z;
            szx += x*z;
            szy += y*z;
            szx2 += xx*z;
            szxy += x*y*z;
            szy2 += yy*z;
            n++;
        }
    }

    gwy_clear(a, 21);
    a[0] = n;
    a[2] = a[6] = sx2;
    a[5] = a[15] = sy2;
    a[18] = a[14] = sx2y2;
    a[9] = sx4;
    a[20] = sy4;
    if (gwy_math_choleski_decompose(6, a)) {
        b[0] = sz;
        b[1] = szx;
        b[2] = szy;
        b[3] = szx2;
        b[4] = szxy;
        b[5] = szy2;
        gwy_math_choleski_solve(6, a, b);
    }
    else {
        *pc1 = *pc2 = 0.0;
        return;
    }

    gwy_math_curvature_at_origin(b, &k1, &k2, NULL, NULL);
    *pc1 = k1/scale;
    *pc2 = k2/scale;
}

static void
size_plot(ModuleArgs *args)
{
    GwyDataField *surface = gwy_params_get_image(args->params, PARAM_SOURCE);
    gint tyres = gwy_data_field_get_yres(args->tip);
    gint yres = gwy_data_field_get_yres(surface);
    GwyGraphModel *gmodel = args->gmodel;
    GwyGraphCurveModel *gcmodel;
    gdouble *xdata, *ydata;
    gint i, ngood = 0, nstripes = args->nstripes;
    gdouble dy = gwy_data_field_get_dy(surface);

    g_object_set(gmodel,
                 "title", _("Tip radius evolution"),
                 "axis-label-bottom", "y",
                 "axis-label-left", "r",
                 NULL);
    gwy_graph_model_set_units_from_data_field(gmodel, args->field, 1, 0, 1, 0);

    xdata = g_new(gdouble, nstripes);
    ydata = g_new(gdouble, nstripes);

    for (i = 0; i < nstripes; i++) {
        guint row = i*(yres - tyres)/nstripes;
        gint height = ((i + 1)*(yres - tyres)/nstripes + yres - row);
        gdouble k1, k2, y = (row + 0.5*height)*dy;

        if (!args->goodtips[i] || !args->stripetips[i])
            continue;

        tip_curvatures(args->stripetips[i], &k1, &k2);
        if (k1 == 0.0 || k2 == 0.0)
            continue;

        xdata[ngood] = y;
        /* The tip image is upside down, make curvatures positive. */
        ydata[ngood] = -2.0/(k1 + k2);
        ngood++;
    }

    gcmodel = gwy_graph_curve_model_new();
    g_object_set(gcmodel, "description", _("Tip radius evolution"), NULL);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ngood);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    g_free(xdata);
    g_free(ydata);
}

static void
sanitise_params(ModuleArgs *args, GwyAppDataId dataid)
{
    GwyParams *params = args->params;

    gwy_params_set_image_id(params, PARAM_SOURCE, dataid);
    if (gwy_params_get_int(params, PARAM_XRES) != gwy_params_get_int(params, PARAM_YRES))
        gwy_params_set_boolean(params, PARAM_SAME_RESOLUTION, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
