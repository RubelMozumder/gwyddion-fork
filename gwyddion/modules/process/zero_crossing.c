/*
 *  $Id: zero_crossing.c 24365 2021-10-13 12:45:57Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwythreads.h>
#include <libprocess/gwyprocess.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "preview.h"

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

#define FWHM2SIGMA (1.0/(2.0*sqrt(2*G_LN2)))

typedef enum {
    DISPLAY_DATA = 0,
    DISPLAY_LOG  = 1,
    DISPLAY_SHOW = 2,
} DisplayType;

enum {
    PARAM_GAUSSIAN_FWHM,
    PARAM_THRESHOLD,
    PARAM_DISPLAY,
    PARAM_UPDATE,
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *gaussconv;
    GwyDataField *result;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GwyContainer *data;
    gboolean gaussconv_valid;
    gdouble nrms;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static void             zero_crossing       (GwyContainer *data,
                                             GwyRunType runtype);
static GwyDialogOutcome run_gui             (ModuleArgs *args,
                                             GwyContainer *data,
                                             gint id);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static void             preview             (gpointer user_data);
static gdouble          do_log              (GwyDataField *field,
                                             GwyDataField *gauss,
                                             gdouble gaussian_fwhm);
static void             do_edge             (GwyDataField *show,
                                             GwyDataField *gauss,
                                             gdouble theshold);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Zero crossing edge detection."),
    "Yeti <Yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2019",
};

GWY_MODULE_QUERY2(module_info, zero_crossing)

static gboolean
module_register(void)
{
    gwy_process_func_register("zero_crossing",
                              (GwyProcessFunc)&zero_crossing,
                              N_("/_Presentation/_Edge Detection/_Zero Crossing..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Zero crossing step detection presentation"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum displays[] = {
        { N_("Original _image"), DISPLAY_DATA, },
        { N_("_LoG convolved"),  DISPLAY_LOG,  },
        { N_("Detected st_ep"),  DISPLAY_SHOW, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    /* Units: RMS */
    gwy_param_def_add_double(paramdef, PARAM_THRESHOLD, "threshold", _("_Threshold"), 0.0, 3.0, 0.1);
    gwy_param_def_add_double(paramdef, PARAM_GAUSSIAN_FWHM, "gaussian-fwhm", _("_Gaussian FWHM"), 0.0, 30.0, 3.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_DISPLAY, "display", gwy_sgettext("verb|Display"),
                              displays, G_N_ELEMENTS(displays), DISPLAY_DATA);
    gwy_param_def_add_instant_updates(paramdef, PARAM_UPDATE, "update", NULL, FALSE);
    return paramdef;
}

static void
zero_crossing(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome = GWY_DIALOG_PROCEED;
    ModuleArgs args;
    GQuark squark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_SHOW_FIELD_KEY, &squark,
                                     0);
    g_return_if_fail(args.field && squark);

    args.result = gwy_data_field_new_alike(args.field, TRUE);
    args.gaussconv = gwy_data_field_new_alike(args.field, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(args.result), NULL);
    args.params = gwy_params_new_from_settings(define_module_params());

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, data, id);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto end;
    }
    if (outcome != GWY_DIALOG_HAVE_RESULT) {
        gdouble nrms = do_log(args.field, args.gaussconv, gwy_params_get_double(args.params, PARAM_GAUSSIAN_FWHM));
        do_edge(args.result, args.gaussconv, nrms*gwy_params_get_double(args.params, PARAM_THRESHOLD));
    }

    gwy_app_undo_qcheckpointv(data, 1, &squark);
    gwy_container_set_object(data, squark, args.result);
    gwy_app_channel_log_add_proc(data, id, id);

end:
    g_object_unref(args.gaussconv);
    g_object_unref(args.result);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GtkWidget *hbox, *dataview;
    GwyDialogOutcome outcome;
    GwyParamTable *table;
    GwyDialog *dialog;
    ModuleGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->result);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Zero Crossing Step Detection"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_UPDATE, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    dataview = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    hbox = gwy_create_dialog_preview_hbox(GTK_DIALOG(dialog), GWY_DATA_VIEW(dataview), FALSE);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_slider(table, PARAM_GAUSSIAN_FWHM);
    gwy_param_table_slider_add_alt(table, PARAM_GAUSSIAN_FWHM);
    gwy_param_table_alt_set_field_pixel_x(table, PARAM_GAUSSIAN_FWHM, args->field);
    gwy_param_table_append_slider(table, PARAM_THRESHOLD);
    gwy_param_table_set_unitstr(table, PARAM_THRESHOLD, _("RMS"));
    gwy_param_table_append_radio(table, PARAM_DISPLAY);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_UPDATE);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;

    if (id < 0 || id == PARAM_GAUSSIAN_FWHM)
        gui->gaussconv_valid = FALSE;

    if (id < 0 || id == PARAM_DISPLAY) {
        DisplayType display = gwy_params_get_enum(params, PARAM_DISPLAY);
        GQuark quark = gwy_app_get_data_key_for_id(0);
        if (display == DISPLAY_DATA)
            gwy_container_set_object(gui->data, quark, args->field);
        else if (display == DISPLAY_LOG)
            gwy_container_set_object(gui->data, quark, args->gaussconv);
        else if (display == DISPLAY_SHOW)
            gwy_container_set_object(gui->data, quark, args->result);
    }

    /* FIXME: We could avoid recalculation for DISPLAY_DATA by using a more complicated logic (as freq_split does). */
    if (id != PARAM_UPDATE)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;

    if (!gui->gaussconv_valid) {
        gui->nrms = do_log(args->field, args->gaussconv, gwy_params_get_double(args->params, PARAM_GAUSSIAN_FWHM));
        gwy_data_field_data_changed(args->gaussconv);
        gui->gaussconv_valid = TRUE;
    }

    do_edge(args->result, args->gaussconv, gui->nrms*gwy_params_get_double(args->params, PARAM_THRESHOLD));
    gwy_data_field_data_changed(args->result);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static gdouble
do_log(GwyDataField *field, GwyDataField *gauss, gdouble gaussian_fwhm)
{
    const gdouble *data;
    gint xres, yres, i, j;
    gdouble nrms;

    gwy_data_field_copy(field, gauss, FALSE);
    gwy_data_field_filter_gaussian(gauss, gaussian_fwhm*FWHM2SIGMA);
    gwy_data_field_filter_laplacian(gauss);

    xres = gwy_data_field_get_xres(gauss);
    yres = gwy_data_field_get_yres(gauss);
    data = gwy_data_field_get_data_const(gauss);
    nrms = 0.0;

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(+:nrms) \
            private(i,j) \
            shared(xres,yres,data)
#endif
    for (i = 0; i < yres-1; i++) {
        const gdouble *row = data + i*xres;
        for (j = 0; j < xres; j++)
            nrms += (row[j] - row[j + xres])*(row[j] - row[j + xres]);
    }

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            reduction(+:nrms) \
            private(i,j) \
            shared(xres,yres,data)
#endif
    for (i = 0; i < yres; i++) {
        const gdouble *row = data + i*xres;
        for (j = 0; j < xres-1; j++)
            nrms += (row[j] - row[j + 1])*(row[j] - row[j + 1]);
    }

    nrms /= 2*xres*yres - xres - yres;
    return sqrt(nrms);
}

static void
do_edge(GwyDataField *result, GwyDataField *gauss, gdouble threshold)
{
    gdouble *data;
    const gdouble *bdata;
    gint xres, yres, i, j;

    gwy_data_field_clear(result);

    xres = gwy_data_field_get_xres(result);
    yres = gwy_data_field_get_yres(result);
    data = gwy_data_field_get_data(result);
    bdata = gwy_data_field_get_data_const(gauss);

    /* Vertical pass */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(xres,yres,bdata,data,threshold)
#endif
    for (i = 1; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gint n = i*xres + j;
            gdouble dm = bdata[n - xres];
            gdouble dp = bdata[n];
            if (dm*dp <= 0.0) {
                dm = fabs(dm);
                dp = fabs(dp);
                if (dm >= threshold || dp >= threshold) {
                    if (dm < dp)
                        data[n - xres] = 1.0;
                    else if (dp < dm)
                        data[n] = 1.0;
                    /* If they are equal and different from zero, sigh and choose an arbitrary one */
                    else if (dm > 0.0)
                        data[n] = 1.0;
                }
            }
        }
    }

    /* Horizontal pass */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(xres,yres,bdata,data,threshold)
#endif
    for (i = 0; i < yres; i++) {
        for (j = 1; j < xres; j++) {
            gint n = i*xres + j;
            gdouble dm = bdata[n - 1];
            gdouble dp = bdata[n];
            if (dm*dp <= 0.0) {
                dm = fabs(dm);
                dp = fabs(dp);
                if (dm >= threshold || dp >= threshold) {
                    if (dm < dp)
                        data[n - 1] = 1.0;
                    else if (dp < dm)
                        data[n] = 1.0;
                    /* If they are equal and different from zero, sigh and choose an arbitrary one */
                    else if (dm > 0.0)
                        data[n] = 1.0;
                }
            }
        }
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
