/*
 *  $Id: roddeposit_synth.c 23556 2021-04-21 08:00:51Z yeti-dn $
 *  Copyright (C) 2007,2009,2010 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */


#include "config.h"
#include <string.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyrandgenset.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/inttrans.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include "dimensions.h"
#include "preview.h"

#define RODDEPOSIT_SYNTH_RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

// 1. store link to original data
// 2. create result field
// 3. put copy of original or blank field to result
// 3. have function to do preview of result
// 4. run simulation with result field
// 5. repeat (3)

// N. have function to insert result to data browser or swap it for present channel
// N+1. run noninteractive or interactive with function N at end


enum {
    MAXN = 50000,
};

enum {
    PAGE_DIMENSIONS = 0,
    PAGE_GENERATOR = 1,
    PAGE_NPAGES
};

enum {
    RES_TOO_FEW = -1,
    RES_TOO_MANY = -2,
    RES_TOO_SMALL = -3,
    RES_TOO_LARGE = -4
};

typedef struct {
    gint active_page;
    gint seed;
    gboolean randomize;
    gboolean animated;
    gdouble size;
    gdouble width;
    gdouble aspect;
    gdouble aspect_noise;
    gdouble coverage;
    gint revise;
    gdouble gravity;
    gdouble ljsurface;
    gdouble ljparticle;
    gdouble mobility;
    gboolean outstats;
} RodDepositSynthArgs;

typedef struct {
    RodDepositSynthArgs *args;
    GwyDimensions *dims;
    gdouble pxsize;
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *update_now;
    GtkWidget *animated;
    GtkObject *seed;
    GtkWidget *randomize;
    GtkTable *table;
    GtkObject *size;
    GwySIValueFormat *format_size;
    GtkWidget *size_units;
    GtkObject *width;
    GwySIValueFormat *format_width;
    GtkWidget *width_units;
    GtkObject *aspect;
    GtkObject *aspect_noise;
    GtkObject *coverage;
    GtkObject *revise;
    GtkObject *gravity;
    GtkObject *ljsurface;
    GtkObject *ljparticle;
    GtkObject *mobility;
    GtkWidget *message;
    GtkWidget *outstats;
    GwyContainer *mydata;
    GwyDataField *original;
    gboolean data_done;
    GwyDataField *out;
    GwyDataLine *stats_length;
    GwyDataLine *stats_width;
    GwyDataLine *stats_aspectratio;
    GwyDataLine *stats_theta;
    GwyDataLine *stats_phi;
    gboolean in_init;
    gulong sid;
    gdouble *xdata;
    gdouble *ydata;
    gdouble *zdata;
    gdouble *rdata;
    gint ndata;
} RodDepositSynthControls;

static gboolean      module_register             (void);
static void          rod_deposit_synth           (GwyContainer *data,
                                                  GwyRunType run);
static void          run_noninteractive          (RodDepositSynthArgs *args,
                                                  const GwyDimensionArgs *dimsargs,
                                                  GwyContainer *data,
                                                  GwyDataField *dfield,
                                                  gint oldid,
                                                  GQuark quark);
static gboolean      rod_deposit_synth_dialog    (RodDepositSynthArgs *args,
                                                  GwyDimensionArgs *dimsargs,
                                                  GwyContainer *data,
                                                  GwyDataField *dfield,
                                                  gint id,
                                                  GQuark quark);
static GwyDataField* surface_for_preview         (GwyDataField *dfield,
                                                  guint size);
static void          update_controls             (RodDepositSynthControls *controls,
                                                  RodDepositSynthArgs *args);
static void          page_switched               (RodDepositSynthControls *controls,
                                                  GtkNotebookPage *page,
                                                  gint pagenum);
static void          size_changed                (RodDepositSynthControls *controls,
                                                  GtkAdjustment *adj);
static void          width_changed               (RodDepositSynthControls *controls,
                                                  GtkAdjustment *adj);
static void          outstats_changed            (RodDepositSynthControls *controls,
                                                  GtkToggleButton *button);
static void          rod_deposit_synth_invalidate(RodDepositSynthControls *controls);
static void          preview                     (RodDepositSynthControls *controls);
static gint          rod_deposit_synth_do        (const RodDepositSynthArgs *args,
                                                  GwyDataField *dfield,
                                                  GwyDataLine *stats_length,
                                                  GwyDataLine *stats_width,
                                                  GwyDataLine *stats_aspectratio,
                                                  GwyDataLine *stats_theta, 
                                                  GwyDataLine *stats_phi,
                                                  GwyDataField *showfield,
                                                  gboolean *success,
                                                  gboolean outdata,
                                                  gdouble **oxdata,
                                                  gdouble **oydata,
                                                  gdouble **ozdata,
                                                  gdouble **ordata,
                                                  gint *ondata);
static const gchar*  particle_error              (gint code);
static void          rod_deposit_synth_load_args (GwyContainer *container,
                                                  RodDepositSynthArgs *args,
                                                  GwyDimensionArgs *dimsargs);
static void          rod_deposit_synth_save_args (GwyContainer *container,
                                                  const RodDepositSynthArgs *args,
                                                  const GwyDimensionArgs *dimsargs);
static GString*      create_xyz_report           (RodDepositSynthControls *controls);

#define GWY_SYNTH_CONTROLS RodDepositSynthControls
#define GWY_SYNTH_INVALIDATE(controls) rod_deposit_synth_invalidate(controls)

#include "synth.h"

static const RodDepositSynthArgs rod_deposit_synth_defaults = {
    PAGE_DIMENSIONS,
    42, TRUE, TRUE,
    6, 0,
    2, 0,
    10, 100000, 10, 10, 10, 0.5, FALSE,
};

static const GwyDimensionArgs dims_defaults = GWY_DIMENSION_ARGS_INIT;

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Generates rod-like particles using simple dynamical model"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek",
    "2017",
};

GWY_MODULE_QUERY2(module_info, roddeposit_synth)

static gboolean
module_register(void)
{
    gwy_process_func_register("rod_deposit_synth",
                              (GwyProcessFunc)&rod_deposit_synth,
                              N_("/S_ynthetic/_Deposition/_Rods..."),
                              NULL,
                              RODDEPOSIT_SYNTH_RUN_MODES,
                              0,
                              N_("Generate rod-like particles using "
                                 "dynamical model"));

    return TRUE;
}

static void
rod_deposit_synth(GwyContainer *data, GwyRunType run)
{
    RodDepositSynthArgs args;
    GwyDimensionArgs dimsargs;
    GwyDataField *dfield;
    GQuark quark;
    gint id;

    g_return_if_fail(run & RODDEPOSIT_SYNTH_RUN_MODES);
    rod_deposit_synth_load_args(gwy_app_settings_get(), &args, &dimsargs);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_DATA_FIELD_KEY, &quark,
                                     0);

    if (run == GWY_RUN_IMMEDIATE)
        run_noninteractive(&args, &dimsargs, data, dfield, id, quark);
    else if (run == GWY_RUN_INTERACTIVE)
        rod_deposit_synth_dialog(&args, &dimsargs, data, dfield, id, quark);

    gwy_dimensions_free_args(&dimsargs);
}

static void
run_noninteractive(RodDepositSynthArgs *args,
                   const GwyDimensionArgs *dimsargs,
                   GwyContainer *data,
                   GwyDataField *dfield,
                   gint oldid,
                   GQuark quark)
{
    GwyDataField *out;
    GwySIUnit *siunit;
    GwyContainer *newdata;
    GtkWidget *dialog;
    gboolean replace = dimsargs->replace && dfield;
    gboolean add = dimsargs->add && dfield;
    gdouble mag;
    gboolean success;
    gint newid, ndata;

    if (args->randomize)
        args->seed = g_random_int() & 0x7fffffff;

    if (replace) {
        gwy_app_undo_qcheckpointv(data, 1, &quark);

            out = gwy_data_field_new_alike(dfield, FALSE);
        if (add && dfield)
            gwy_data_field_copy(dfield, out, FALSE);
        else
            gwy_data_field_fill(out, 0);

    }
    else {
        if (add && dfield) {
            out = gwy_data_field_new_alike(dfield, FALSE);
            gwy_data_field_copy(dfield, out, FALSE);
        }
        else {
            mag = pow10(dimsargs->xypow10) * dimsargs->measure;
            out = gwy_data_field_new(dimsargs->xres, dimsargs->yres,
                                        mag*dimsargs->xres, mag*dimsargs->yres,
                                        TRUE);

            siunit = gwy_data_field_get_si_unit_xy(out);
            gwy_si_unit_set_from_string(siunit, dimsargs->xyunits);
        }
    }

    gwy_app_wait_start(gwy_app_find_window_for_channel(data, oldid),
                       _("Initializing..."));
    ndata = rod_deposit_synth_do(args, out, NULL, NULL, NULL, NULL, NULL, NULL, &success, 
               FALSE, NULL, NULL, NULL, NULL, NULL);
    gwy_app_wait_finish();

    if (ndata <= 0) {
        dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, oldid),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CLOSE,
                                        "%s", particle_error(ndata));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

    }
    else {
        if (!success) {
            dialog = gtk_message_dialog_new(gwy_app_find_window_for_channel(data, oldid),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_WARNING,
                                            GTK_BUTTONS_CLOSE,
                                            _("Not all the particles could "
                                              "be deposited (%u),\n"
                                              "try more revise steps."),
                                            ndata/3);
            gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
        }
        if (replace) {
            gwy_data_field_copy(out, dfield, FALSE);
            gwy_data_field_data_changed(dfield);
        }
        else {
            if (data) {
                newid = gwy_app_data_browser_add_data_field(out, data, TRUE);
                if (oldid != -1)
                    gwy_app_sync_data_items(data, data, oldid, newid, FALSE,
                                            GWY_DATA_ITEM_GRADIENT,
                                            0);
                gwy_app_set_data_field_title(data, newid, _("Generated"));

            }
            else {
                newid = 0;
                newdata = gwy_container_new();
                gwy_container_set_object(newdata, gwy_app_get_data_key_for_id(newid),
                                         out);
                gwy_app_data_browser_add(newdata);
                gwy_app_data_browser_reset_visibility(newdata,
                                                      GWY_VISIBILITY_RESET_SHOW_ALL);
                g_object_unref(newdata);
                gwy_app_set_data_field_title(newdata, newid, _("Generated"));

            }

        }
    }

    g_object_unref(out);
}

static gboolean
rod_deposit_synth_dialog(RodDepositSynthArgs *args,
                         GwyDimensionArgs *dimsargs,
                         GwyContainer *data,
                         GwyDataField *dfield_template,
                         gint id, GQuark quark)
{
    GtkWidget *dialog, *table, *vbox, *hbox, *notebook, *spin;
    RodDepositSynthControls controls;
    GwyDataField *dfield;
    gboolean finished;
    gint response;
    gint i, row, newid;
    gdouble power10;
    GwyGraphModel *gmodel;
    GString *report;
    GwyGraphCurveModel *gcmodel;

    gwy_clear(&controls, 1);
    controls.in_init = TRUE;
    controls.args = args;
    controls.pxsize = 1.0;
    dialog = gtk_dialog_new_with_buttons(_("Particle Generation"),
                                         NULL, 0,
                                         _("_Reset"), RESPONSE_RESET,
                                         GTK_STOCK_SAVE, RESPONSE_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OK, GTK_RESPONSE_OK,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);
    controls.dialog = dialog;

    controls.original = dfield_template;
    controls.data_done = FALSE;
    controls.xdata = controls.ydata = controls.zdata = controls.rdata = NULL;
    controls.ndata = 0;

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    vbox = gtk_vbox_new(FALSE, 4);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    controls.mydata = gwy_container_new();
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                dimsargs->measure*PREVIEW_SIZE,
                                dimsargs->measure*PREVIEW_SIZE,
                                TRUE);

    if (dfield_template && dimsargs->add) {
        gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                                GWY_DATA_ITEM_PALETTE,
                                0);
        dfield = surface_for_preview(controls.original, PREVIEW_SIZE);
        gwy_data_field_data_changed(dfield);
    }

    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_synth_progressive_preview_new(&controls,
                                                         &controls.update_now,
                                                         &controls.animated,
                                                         &args->animated),
                       FALSE, FALSE, 0);
    g_signal_connect_swapped(controls.update_now, "clicked",
                             G_CALLBACK(preview), &controls);

    gtk_box_pack_start(GTK_BOX(vbox),
                       gwy_synth_random_seed_new(&controls,
                                                 &controls.seed, &args->seed),
                       FALSE, FALSE, 0);

    controls.randomize = gwy_synth_randomize_new(&args->randomize);
    gtk_box_pack_start(GTK_BOX(vbox), controls.randomize, FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(hbox), notebook, FALSE, FALSE, 4);
    g_signal_connect_swapped(notebook, "switch-page",
                             G_CALLBACK(page_switched), &controls);

    controls.dims = gwy_dimensions_new(dimsargs, dfield_template);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             gwy_dimensions_get_widget(controls.dims),
                             gtk_label_new(_("Dimensions")));
    if (controls.dims->add) {
        g_signal_connect_swapped(controls.dims->add, "toggled",
                                 G_CALLBACK(rod_deposit_synth_invalidate),
                                 &controls);
    }

    /* Hide the z units, they must be the same as xy. */
    table = controls.dims->table;
    gtk_container_child_get(GTK_CONTAINER(table), controls.dims->zunits,
                            "top-attach", &i, NULL);
    gtk_widget_set_no_show_all(controls.dims->zunits, TRUE);
    gtk_widget_set_no_show_all(gwy_table_get_child_widget(table, i, 0), TRUE);
    gtk_widget_set_no_show_all(gwy_table_get_child_widget(table, i, 1), TRUE);

    table = gtk_table_new(12 + (dfield_template ? 1 : 0), 3, FALSE);
    /* This is used only for synt.h helpers. */
    controls.table = GTK_TABLE(table);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), table,
                             gtk_label_new(_("Generator")));
    row = 0;

    power10 = pow10(controls.dims->args->xypow10);
    controls.size = gtk_adjustment_new(args->size/power10,
                                       0, 100.0, 0.1, 1.0, 0);
    spin = gwy_table_attach_adjbar(table, row, _("Particle r_adius:"),
                                   controls.dims->args->xyunits,
                                   controls.size, GWY_HSCALE_SQRT);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(spin), FALSE);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.size_units = gwy_table_hscale_get_units(controls.size);
    g_signal_connect_swapped(controls.size, "value-changed",
                             G_CALLBACK(size_changed), &controls);
    row++;

    controls.width = gtk_adjustment_new(args->width/power10,
                                        0, 100.0, 0.1, 1.0, 0);
    spin = gwy_table_attach_adjbar(table, row, _("Distribution _width:"),
                                   controls.dims->args->xyunits,
                                   controls.width, GWY_HSCALE_SQRT);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(spin), FALSE);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 4);
    controls.width_units = gwy_table_hscale_get_units(controls.width);
    g_signal_connect_swapped(controls.width, "value-changed",
                             G_CALLBACK(width_changed), &controls);
    row++;


    controls.aspect = gtk_adjustment_new(args->aspect,
                                         1.01, 3.0, 0.01, 1.0, 0);
    g_object_set_data(G_OBJECT(controls.aspect), "target", &args->aspect);
    spin = gwy_table_attach_adjbar(table, row, _("_Aspect ratio:"), "",
                                   controls.aspect, GWY_HSCALE_LOG);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_signal_connect_swapped(controls.aspect, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.aspect_noise = gtk_adjustment_new(args->aspect_noise,
                                         0, 2, 0.01, 0.1, 0);
    g_object_set_data(G_OBJECT(controls.aspect_noise),
                      "target", &args->aspect_noise);
    spin = gwy_table_attach_adjbar(table, row, _("A_spect ratio variance:"), "",
                                   controls.aspect_noise, GWY_HSCALE_LINEAR);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 2);
    g_signal_connect_swapped(controls.aspect_noise, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.ljparticle = gtk_adjustment_new(args->ljparticle, 0.0, 1000, 1, 10, 0);
    g_object_set_data(G_OBJECT(controls.ljparticle), "target", &args->ljparticle);
    gwy_table_attach_adjbar(table, row, _("LJ _particle strength:"), NULL,
                            controls.ljparticle, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.ljparticle, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.ljsurface = gtk_adjustment_new(args->ljsurface, 0.0, 1000, 1, 10, 0);
    g_object_set_data(G_OBJECT(controls.ljsurface), "target", &args->ljsurface);
    gwy_table_attach_adjbar(table, row, _("LJ _surface strength:"), NULL,
                            controls.ljsurface, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.ljsurface, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.mobility = gtk_adjustment_new(args->mobility, 0.0, 1, 0.01, 0.1, 0);
    g_object_set_data(G_OBJECT(controls.mobility), "target", &args->mobility);
    gwy_table_attach_adjbar(table, row, _("_Surface mobility:"), NULL,
                            controls.mobility, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls.mobility, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;


    controls.gravity = gtk_adjustment_new(args->gravity, 0.0, 1000, 1, 10, 0);
    g_object_set_data(G_OBJECT(controls.gravity), "target", &args->gravity);
    gwy_table_attach_adjbar(table, row, _("_Gravity:"), NULL,
                            controls.gravity, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.gravity, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.coverage = gtk_adjustment_new(args->coverage,
                                           0.0, 100, 0.1, 1, 0);
    g_object_set_data(G_OBJECT(controls.coverage), "target", &args->coverage);
    gwy_table_attach_adjbar(table, row, _("Co_verage:"), "%",
                            controls.coverage, GWY_HSCALE_LINEAR);
    g_signal_connect_swapped(controls.coverage, "value-changed",
                             G_CALLBACK(gwy_synth_double_changed), &controls);
    row++;

    controls.revise = gtk_adjustment_new(args->revise, 0.0, 1000000, 1, 10, 0);
    g_object_set_data(G_OBJECT(controls.revise), "target", &args->revise);
    gwy_table_attach_adjbar(table, row, _("_Relax steps:"), NULL,
                            controls.revise, GWY_HSCALE_SQRT);
    g_signal_connect_swapped(controls.revise, "value-changed",
                             G_CALLBACK(gwy_synth_int_changed), &controls);
    row++;

    /* Does not invalidate, do not use gwy_synth_boolean_changed. */
    controls.outstats
        = gtk_check_button_new_with_mnemonic(_("Output statistics"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.outstats),
                                 args->outstats);
    g_signal_connect_swapped(controls.outstats, "toggled",
                     G_CALLBACK(outstats_changed), &controls);
    gtk_table_attach(GTK_TABLE(table), controls.outstats,
                     0, 2, row, row+1, GTK_FILL, 0, 0, 0);
    row++;


    controls.message = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(controls.message), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), controls.message,
                     0, 4, row, row+1, GTK_FILL, 0, 0, 0);
    row++;

    gtk_widget_show_all(dialog);
    controls.in_init = FALSE;
    /* Must be done when widgets are shown, see GtkNotebook docs */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), args->active_page);
    rod_deposit_synth_invalidate(&controls);

    finished = FALSE;
    while (!finished) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            case GTK_RESPONSE_OK:
            case GTK_RESPONSE_NONE:
            finished = TRUE;
            break;

            case RESPONSE_RESET:
            {
                gint temp2 = args->active_page;
                *args = rod_deposit_synth_defaults;
                args->active_page = temp2;
            }
            controls.in_init = TRUE;
            update_controls(&controls, args);
            controls.in_init = FALSE;
            break;

            case RESPONSE_SAVE:
            if (controls.data_done) { //this should never be needed
               report = create_xyz_report(&controls);
               gwy_save_auxiliary_data(_("Save Fit Report"), GTK_WINDOW(dialog),
                                    -1, report->str);
               g_string_free(report, TRUE);
            }
            break;


            default:
            g_assert_not_reached();
            break;
        }
    }

    rod_deposit_synth_save_args(gwy_app_settings_get(), args, dimsargs);

    if (response == GTK_RESPONSE_OK) {
        if (!controls.data_done)
            preview(&controls);

        if (controls.dims->args->replace) {
            gwy_app_undo_qcheckpointv(data, 1, &quark);
            gwy_data_field_copy(controls.out, controls.original, FALSE);
            gwy_data_field_data_changed(controls.original);
            gwy_app_channel_log_add_proc(data, id, id);
        }
        else {
            if (data) {
                newid = gwy_app_data_browser_add_data_field(controls.out, data,
                                                            TRUE);
                gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                        GWY_DATA_ITEM_GRADIENT,
                                        0);
                gwy_app_set_data_field_title(data, newid, _("Generated"));
                gwy_app_channel_log_add_proc(data, id, newid);
            }
            else {
                newid = 0;
                data = gwy_container_new();
                gwy_container_set_object(data,
                                         gwy_app_get_data_key_for_id(newid),
                                         controls.out);
                gwy_app_data_browser_add(data);
                gwy_app_data_browser_reset_visibility(data,
                                                      GWY_VISIBILITY_RESET_SHOW_ALL);
                g_object_unref(data);
                gwy_app_set_data_field_title(data, newid, _("Generated"));
                gwy_app_channel_log_add_proc(data, -1, newid);
            }

        }

        if (args->outstats) {



            gmodel = gwy_graph_model_new();
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);

            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         controls.stats_length, 0, 0);

            g_object_set(gmodel, "si-unit-x", gwy_si_unit_duplicate(gwy_data_line_get_si_unit_x(controls.stats_length)),
                                 NULL);
            g_object_set(gmodel,
                     "title", "Particle length histogram",
                     "axis-label-bottom", _("Particle length"),
                     "axis-label-left", _("Counts"),
                     NULL);
            g_object_set(gcmodel,
                     "description", "particle length",
                      NULL);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);

            gmodel = gwy_graph_model_new();
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);

            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         controls.stats_width, 0, 0);
            g_object_set(gmodel, "si-unit-x", gwy_si_unit_duplicate(gwy_data_line_get_si_unit_x(controls.stats_width)),
                                 NULL);
            g_object_set(gmodel,
                     "title", "Particle width histogram",
                     "axis-label-bottom", _("Particle width"),
                     "axis-label-left", _("Counts"),
                      NULL);
            g_object_set(gcmodel,
                     "description", "particle width",
                      NULL);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);

            gmodel = gwy_graph_model_new();
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);

            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         controls.stats_aspectratio, 0, 0);
            g_object_set(gmodel, "si-unit-x", gwy_si_unit_duplicate(gwy_data_line_get_si_unit_x(controls.stats_aspectratio)),
                                 NULL);

            g_object_set(gmodel,
                     "title", "Particle aspect ratio histogram",
                     "axis-label-bottom", _("Aspect ratio"),
                     "axis-label-left", _("Counts"),
                     NULL);
            g_object_set(gcmodel,
                     "description", "particle aspect ratio",
                      NULL);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);

            gmodel = gwy_graph_model_new();
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);

            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         controls.stats_theta, 0, 0);
            g_object_set(gmodel, "si-unit-x", gwy_si_unit_new("deg"),
                                 NULL);

            g_object_set(gmodel,
                     "title", "Particle polar angle",
                     "axis-label-bottom", _("ϑ"),
                     "axis-label-left", _("Counts"),
                     NULL);
            g_object_set(gcmodel,
                     "description", "particle polar angle",
                      NULL);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);

            gmodel = gwy_graph_model_new();
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);

            gwy_graph_curve_model_set_data_from_dataline(gcmodel,
                                                         controls.stats_phi, 0, 0);
            g_object_set(gmodel, "si-unit-x", gwy_si_unit_new("deg"),
                                 NULL);

            g_object_set(gmodel,
                     "title", "Particle azimuthal angle",
                     "axis-label-bottom", _("φ"),
                     "axis-label-left", _("Counts"),
                     NULL);
            g_object_set(gcmodel,
                     "description", "particle azimutal angle",
                      NULL);
            gwy_app_data_browser_add_graph_model(gmodel, data, TRUE);

         }

    }
    gtk_widget_destroy(dialog);

    if (controls.sid) {
        g_source_remove(controls.sid);
        controls.sid = 0;
    }

    g_object_unref(controls.mydata);
    gwy_dimensions_free(controls.dims);

    return response == GTK_RESPONSE_OK;
}

/* Create a square base surface for preview generation of an exact size */
static GwyDataField*
surface_for_preview(GwyDataField *dfield,
                    guint size)
{
    GwyDataField *retval;
    gint xres, yres, xoff, yoff;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);

    /* If the field is large enough, just cut an area from the centre. */
    if (xres >= size && yres >= size) {
        xoff = (xres - size)/2;
        yoff = (yres - size)/2;
        return gwy_data_field_area_extract(dfield, xoff, yoff, size, size);
    }

    if (xres <= yres) {
        yoff = (yres - xres)/2;
        dfield = gwy_data_field_area_extract(dfield, 0, yoff, xres, xres);
    }
    else {
        xoff = (xres - yres)/2;
        dfield = gwy_data_field_area_extract(dfield, xoff, 0, yres, yres);
    }

    retval = gwy_data_field_new_resampled(dfield, size, size,
                                          GWY_INTERPOLATION_KEY);

    g_object_unref(dfield);

    return retval;
}
static void
update_controls(RodDepositSynthControls *controls,
                RodDepositSynthArgs *args)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->animated),
                                 args->animated);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->seed), args->seed);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls->randomize),
                                 args->randomize);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->size),
                             args->size/pow10(controls->dims->args->xypow10));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->width),
                             args->width/pow10(controls->dims->args->xypow10));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->coverage),
                             args->coverage);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->revise),
                             args->revise);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->gravity),
                             args->gravity);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ljparticle),
                             args->ljparticle);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->ljsurface),
                             args->ljsurface);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->aspect),
                             args->aspect);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->aspect_noise),
                             args->aspect_noise);
    gtk_adjustment_set_value(GTK_ADJUSTMENT(controls->mobility),
                             args->mobility);
}

static void
page_switched(RodDepositSynthControls *controls,
              G_GNUC_UNUSED GtkNotebookPage *page,
              gint pagenum)
{
    if (controls->in_init)
        return;

    controls->args->active_page = pagenum;

    if (pagenum == PAGE_GENERATOR) {
        GwyDimensions *dims = controls->dims;

        if (controls->size_units)
            gtk_label_set_markup(GTK_LABEL(controls->size_units),
                                 dims->xyvf->units);
        if (controls->width_units)
            gtk_label_set_markup(GTK_LABEL(controls->width_units),
                                 dims->xyvf->units);
    }
}

static void
size_changed(RodDepositSynthControls *controls,
             GtkAdjustment *adj)
{
    gdouble power10 = pow10(controls->dims->args->xypow10);

    controls->args->size = gtk_adjustment_get_value(adj)*power10;
    rod_deposit_synth_invalidate(controls);
}

static void
width_changed(RodDepositSynthControls *controls,
             GtkAdjustment *adj)
{
    gdouble power10 = pow10(controls->dims->args->xypow10);

    controls->args->width = gtk_adjustment_get_value(adj)*power10;
    rod_deposit_synth_invalidate(controls);
}

static void
outstats_changed(RodDepositSynthControls *controls,
                 GtkToggleButton *button)
{
    controls->args->outstats = gtk_toggle_button_get_active(button);
}

static void
rod_deposit_synth_invalidate(RodDepositSynthControls *controls)
{
    controls->data_done = FALSE;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_SAVE, FALSE);
}

static void
preview(RodDepositSynthControls *controls)
{
    RodDepositSynthArgs *args = controls->args;
    gdouble mag;
    GwySIUnit *siunit;
    GwyDataField *dfield, *surface;
    gchar *message;
    gint ndata;
    gboolean success;

    dfield = GWY_DATA_FIELD(gwy_container_get_object_by_name(controls->mydata,
                                                             "/0/data"));

    if (!controls->original || !controls->dims->args->add) {   //no dfield or not to be used
        if (!controls->out) {

            mag = pow10(controls->dims->args->xypow10) * controls->dims->args->measure;
            controls->out = gwy_data_field_new(controls->dims->args->xres, controls->dims->args->yres,
                                        mag*controls->dims->args->xres, mag*controls->dims->args->yres,
                                        TRUE);

            siunit = gwy_data_field_get_si_unit_xy(controls->out);
            gwy_si_unit_set_from_string(siunit, controls->dims->args->xyunits);

        }
        else if (gwy_data_field_get_xres(controls->out) != controls->dims->args->xres ||
                 gwy_data_field_get_yres(controls->out) != controls->dims->args->yres)
        {
            gwy_data_field_resample(controls->out,
                                    controls->dims->args->xres,
                                    controls->dims->args->yres,
                                    GWY_INTERPOLATION_NONE);
        }

        mag = pow10(controls->dims->args->xypow10) * controls->dims->args->measure;
        if (gwy_data_field_get_xreal(controls->out) != mag*controls->dims->args->xres ||
            gwy_data_field_get_yreal(controls->out) != mag*controls->dims->args->yres)
        {
            gwy_data_field_set_xreal(controls->out, mag*controls->dims->args->xres);
            gwy_data_field_set_yreal(controls->out, mag*controls->dims->args->yres);

        }

        gwy_data_field_fill(controls->out, 0);
    } else {
        if (controls->out && (gwy_data_field_get_xres(controls->original)!=gwy_data_field_get_xres(controls->out) ||
            gwy_data_field_get_yres(controls->original)!=gwy_data_field_get_yres(controls->out)))
        {
            GWY_OBJECT_UNREF(controls->out);
            controls->out = NULL;
        }

        if (!controls->out) {
            controls->out = gwy_data_field_new_alike(controls->original, TRUE);
        }
        if (gwy_data_field_get_xreal(controls->original)!=gwy_data_field_get_xreal(controls->out) ||
            gwy_data_field_get_yreal(controls->original)!=gwy_data_field_get_yreal(controls->out))
        {
            gwy_data_field_set_xreal(controls->out, gwy_data_field_get_xreal(controls->original));
            gwy_data_field_set_yreal(controls->out, gwy_data_field_get_yreal(controls->original));
        }

        gwy_data_field_copy(controls->original, controls->out, TRUE);
    }


    surface = surface_for_preview(controls->out, PREVIEW_SIZE);
    gwy_data_field_copy(surface, dfield, FALSE);
    gwy_data_field_data_changed(dfield);

    controls->stats_length = gwy_data_line_new(50, 50, TRUE);
    controls->stats_width = gwy_data_line_new(50, 50, TRUE);
    controls->stats_aspectratio = gwy_data_line_new(50, 50, TRUE);
    controls->stats_theta = gwy_data_line_new(50, 50, TRUE);
    controls->stats_phi = gwy_data_line_new(50, 50, TRUE);


    /*check arguments for sure again (see sanitize_args)*/
    args->size = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->size))*pow10(controls->dims->args->xypow10);
    args->width = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->width))*pow10(controls->dims->args->xypow10);

    gwy_app_wait_start(GTK_WINDOW(controls->dialog), _("Initializing..."));
    gtk_label_set_text(GTK_LABEL(controls->message), _("Running computation..."));
    ndata = rod_deposit_synth_do(args, controls->out,
                                 controls->stats_length, controls->stats_width, controls->stats_aspectratio,
                                 controls->stats_theta, controls->stats_phi,
                                 dfield, &success, TRUE,
                                 &(controls->xdata), &(controls->ydata), &(controls->zdata), &(controls->rdata), 
                                 &(controls->ndata));
    gwy_app_wait_finish();


    if (ndata >=0 && success)
        message = g_strdup_printf(_("%d particles were deposited"), ndata/3);
    else if (ndata >= 0 && !success)
        message = g_strdup_printf(_("Not all the particles could "
                                    "be deposited (%u),\n"
                                    "try more revise steps."),
                                  ndata/3);
    else
        message = g_strdup(particle_error(ndata));

    gtk_label_set_text(GTK_LABEL(controls->message), message);
    g_free(message);

    GWY_OBJECT_UNREF(surface);
    surface = surface_for_preview(controls->out, PREVIEW_SIZE);
    gwy_data_field_copy(surface, dfield, FALSE);
    gwy_data_field_data_changed(dfield);

    controls->data_done = TRUE;
    gtk_dialog_set_response_sensitive(GTK_DIALOG(controls->dialog),
                                      RESPONSE_SAVE, TRUE);

    gwy_data_field_data_changed(controls->out);


    GWY_OBJECT_UNREF(surface);
}

/*
static void
showit(GwyDataField *lfield, GwyDataField *dfield,
       gdouble *rdisizes, gdouble *rx, gdouble *ry, gdouble *rz,
       gint *xdata, gint *ydata, gint *active, gint ndata,
       gint oxres, gdouble oxreal, gint oyres, gdouble oyreal,
       gint add, gint xres, gint yres)
{
    gint i, m, n;
    gdouble sum, surface, lsurface;
    gint disize;

    for (i = 0; i < ndata; i++) {
        if (active[i]==0) continue;

        xdata[i] = oxres*(rx[i]/oxreal);
        ydata[i] = oyres*(ry[i]/oyreal);

        if (xdata[i] < 0 || ydata[i] < 0
            || xdata[i] > (xres-1) || ydata[i] > (yres-1))
            continue;

        if (rz[i] > (gwy_data_field_get_val(lfield, xdata[i], ydata[i])
                     + 22*rdisizes[i]))
            continue;

        disize = (gint)((gdouble)oxres*rdisizes[i]/oxreal);

        for (m = xdata[i] - disize; m < xdata[i]+disize; m++) {
            for (n = ydata[i] - disize; n < ydata[i]+disize; n++) {
                if (m < 0 || n < 0 || m >= xres || n >= yres)
                    continue;

                if (m >= add && n >= add && m < xres-add && n < yres-add) {
                    surface = gwy_data_field_get_val(dfield, m-add, n-add);
                    lsurface = gwy_data_field_get_val(lfield, m, n);


                    if ((sum = (disize*disize - (xdata[i]-m)*(xdata[i]-m)
                                - (ydata[i]-n)*(ydata[i]-n))) > 0) {
                        surface = MAX(lsurface, rz[i] + sqrt(sum)*oxreal/(double)oxres);
                        gwy_data_field_set_val(lfield, m, n, surface);
                    }
                }
            }
        }
    }
}
*/

static void
showit2(GwyDataField *lfield, GwyDataField *dfield,
        gdouble *rdisizes, gdouble *rx, gdouble *ry, gdouble *rz,
        gint *xdata, gint *ydata, gint *active, gint ndata,
        gint oxres, gdouble oxreal, gint oyres, gdouble oyreal,
        gint add, gint xres, gint yres)
{
    gint i, m, n, k;
    gdouble sum, surface, lsurface;
    gint disize;
    gdouble xpos, ypos, zpos, xstart, ystart, zstart, xend, yend, zend;
    gint npos = 50;
    gint sxpos, sypos;

    for (i = 0; i < ndata; i+=3) {
        if (active[i]==0) continue;

        xdata[i] = oxres*(rx[i]/oxreal);   //this is not needed for anything, maybe debug output
        ydata[i] = oyres*(ry[i]/oyreal);   //this is not needed for anything, maybe debug output

        xstart = oxres*(rx[i]/oxreal);
        ystart = oxres*(ry[i]/oxreal);
        zstart = rz[i];

        xend = oxres*(rx[i+2]/oxreal);
        yend = oxres*(ry[i+2]/oxreal);
        zend = rz[i+2];

        for (k=0; k<npos; k++)
        {
           sxpos = xpos = xstart + (xend-xstart)*(gdouble)k/(gdouble)npos;
           sypos = ypos = ystart + (yend-ystart)*(gdouble)k/(gdouble)npos;
           zpos = zstart + (zend-zstart)*(gdouble)k/(gdouble)npos;

           if (sxpos<0) sxpos = 0;
           if (sypos<0) sypos = 0;
           if (sxpos>(xres-1)) sxpos = xres-1;
           if (sypos>(yres-1)) sypos = yres-1;

           if (zpos > (gwy_data_field_get_val(lfield, sxpos, sypos)
                        + 22*rdisizes[i])) {
                continue;
           }

           disize = (gint)((gdouble)oxres*rdisizes[i]/oxreal);

           for (m = (gint)(xpos - disize); m < (gint)(xpos + disize); m++) {
               for (n = (gint)(ypos - disize); n < (gint)(ypos + disize); n++) {
                   if (m < 0 || n < 0 || m >= xres || n >= yres) {
                       continue;
                   }

                   if (m >= add && n >= add && m < xres-add && n < yres-add) {
                       surface = gwy_data_field_get_val(dfield, m-add, n-add);
                       lsurface = gwy_data_field_get_val(lfield, m, n);

                       if ((sum = (disize*disize - (xpos-m)*(xpos-m)
                                - (ypos-n)*(ypos-n))) > 0)
                       {
                          surface = MAX(lsurface, zpos + sqrt(sum)*oxreal/(double)oxres);
                          gwy_data_field_set_val(lfield, m, n, surface);
                       }
                   }
               }
           }
        }
    }
}



/*lj potential between two particles*/
static gdouble
get_lj_potential_spheres(gdouble ax, gdouble ay, gdouble az, gdouble bx, gdouble by, gdouble bz, gdouble asize, gdouble bsize, gdouble factor)
{
    gdouble sigma = 0.82*(asize+bsize);
    gdouble dist = ((ax-bx)*(ax-bx)
                    + (ay-by)*(ay-by)
                    + (az-bz)*(az-bz));

    if ((asize>0 && bsize>0) && dist > asize/100) {
        gdouble s2 = sigma*sigma, s4 = s2*s2, s6 = s4*s2, s12 = s6*s6;
        gdouble d3 = dist*dist*dist, d6 = d3*d3;
        return (asize)*factor*1e-10*(s12/d6 - s6/d3);
    }
    //return (asize)*3e-5*(pow(sigma, 12)/pow(dist, 6) - pow(sigma, 6)/pow(dist, 3)); //corrected for particle size
    else return 0;
}

/*integrate over some volume around particle (ax, ay, az), if there is substrate, add this to potential*/
static gdouble
integrate_lj_substrate(gdouble zval, gdouble az, gdouble size, gdouble factor)
{
    /*make l-j only from idealistic substrate now*/
    gdouble sigma, dist;
    sigma = 1.2*size; //empiric

    dist = sqrt((az-zval)*(az-zval));

    if (size>0 && dist > size/100)
    {
        gdouble s2 = sigma*sigma, s4 = s2*s2, s6 = s4*s2, s12 = s6*s6;
        gdouble d3 = dist*dist*dist, d9 = d3*d3*d3;
        return size*factor*1e-4*(s12/d9/45.0 - s6/d3/6.0); //corrected for particle size  //was size*1.0e-3*...
    }
    //return size*1e-3*(pow(sigma, 12)/45.0/pow(dist, 9) - pow(sigma, 6)/6.0/pow(dist, 3)); //corrected for particle size
    else return 0;
}

static gdouble
dotcos(gdouble *rx, gdouble *ry, gdouble *rz, gint ia, gint ib, gint ic)
{
    return ((rx[ib] - rx[ia])*(rx[ic] - rx[ia])
            + (ry[ib] - ry[ia])*(ry[ic] - ry[ia])
            + (rz[ib] - rz[ia])*(rz[ic] - rz[ia]))
        /(sqrt((rx[ib] - rx[ia])*(rx[ib] - rx[ia])
           + (ry[ib] - ry[ia])*(ry[ib] - ry[ia])
           + (rz[ib] - rz[ia])*(rz[ib] - rz[ia]))*
          sqrt(((rx[ic] - rx[ia])*(rx[ic] - rx[ia])
           + (ry[ic] - ry[ia])*(ry[ic] - ry[ia])
           + (rz[ic] - rz[ia])*(rz[ic] - rz[ia]))));
}

static gdouble
vcomp(gdouble *rx, gdouble *ry, gdouble *rz,
      gdouble *vx, gdouble *vy, gdouble *vz,
      gint ia, gint ib)
{
    gdouble ex = rx[ib] - rx[ia];
    gdouble ey = ry[ib] - ry[ia];
    gdouble ez = rz[ib] - rz[ia];
    gdouble tvx = vx[ib] - vx[ia];
    gdouble tvy = vy[ib] - vy[ia];
    gdouble tvz = vz[ib] - vz[ia];

    gdouble norm = sqrt(ex*ex + ey*ey + ez*ez);
    ex /= norm;
    ey /= norm;
    ez /= norm;

    return ex*tvx + ey*tvy + ez*tvz;

}

static gint
rod_deposit_synth_do(const RodDepositSynthArgs *args,
                 GwyDataField *dfield, GwyDataLine *stats_length, GwyDataLine *stats_width, GwyDataLine *stats_aspectratio, 
                 GwyDataLine *stats_theta, GwyDataLine *stats_phi,
                 GwyDataField *showfield,
                 gboolean *success, gboolean outdata,
                 gdouble **oxdata, gdouble **oydata, gdouble **ozdata, gdouble **ordata, gint *ondata)
{
    gint i, ii, j, m, k;
    GwyRandGenSet *rngset;
    GRand *rng;
    GwyDataField *surface=NULL, *lfield, *zlfield, *zdfield; //FIXME all of them?
    gint xres, yres, oxres, oyres, ndata, steps;
    gdouble xreal, yreal, oxreal, oyreal, diff;
    gdouble size, width;
    gdouble mass=1,  timestep = 0.5, rxv, ryv, rzv, zval;
    gint add, presetval;
    gint *xdata, *ydata;
    gdouble *disizes, *rdisizes;
    gdouble *rx, *ry, *rz;
    gdouble *vx, *vy, *vz;
    gdouble *ax, *ay, *az;
    gdouble *fx, *fy, *fz;
    gint *bp, *active;
    gdouble disize;
    gint xpos, ypos, too_close;
    gint nloc, maxloc = 1;
    gint max = 50000000;
    gdouble norm, maxdist, angle;
    gdouble ma, mb, mc, ca, cb, cc, vab, vca, vbc, tab, tca, tbc, xd;
    gint ia, ib, ic, ntr;
    gdouble eabx, eaby, eabz, ebcx, ebcy, ebcz, ecax, ecay, ecaz, snorm, aspect;
    gdouble length_from, length_to;
    gdouble width_from, width_to;
    gdouble aspectratio_from, aspectratio_to;
    gdouble theta_from, theta_to;
    gdouble phi_from, phi_to;
    gdouble *wdata, *ldata, *adata, *thetadata, *phidata, length, theta, phi;
    GString *message;
    gint nstat = 50;
    gint nactive = 0;

//    FILE *fo;

    /* The units must be the same. */
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield),
                       gwy_data_field_get_si_unit_xy(dfield));

    //FIXME renormalize everything for size of field 1x1, including z. Change parameters of potentials.
    norm = 1/gwy_data_field_get_xreal(dfield);

//    printf("do (%d %d) %g %g, norm %g\n", gwy_data_field_get_xres(dfield), gwy_data_field_get_yres(dfield),
//                           gwy_data_field_get_xreal(dfield), gwy_data_field_get_yreal(dfield), norm);
//    printf("size %g width %g, coverage %g, revise %d, datafield real %g x %g, rms %g\n",
//           args->size, args->width, args->coverage, args->revise, oxreal, oyreal, gwy_data_field_get_rms(dfield));

    rngset = gwy_rand_gen_set_new(1);
    gwy_rand_gen_set_init(rngset, args->seed);
    rng = gwy_rand_gen_set_rng(rngset, 0);

    /*normalize all*/
    gwy_data_field_multiply(dfield, norm);
    gwy_data_field_set_xreal(dfield, gwy_data_field_get_xreal(dfield)*norm);
    gwy_data_field_set_yreal(dfield, gwy_data_field_get_yreal(dfield)*norm);
    size = norm*args->size;
    width = norm*args->width;
    /*now everything is normalized to be close to 1*/

    oxres = gwy_data_field_get_xres(dfield);
    oyres = gwy_data_field_get_yres(dfield);
    oxreal = gwy_data_field_get_xreal(dfield);
    oyreal = gwy_data_field_get_yreal(dfield);
    diff = oxreal/oxres/10;

    add = CLAMP(gwy_data_field_rtoi(dfield, size + width), 0, oxres/4);
    xres = oxres + 2*add;
    yres = oyres + 2*add;
    xreal = xres*oxreal/(gdouble)oxres;
    yreal = yres*oyreal/(gdouble)oyres;

    presetval = 3*args->coverage/100 * xreal*yreal/(G_PI*size*size);
    if (presetval <= 0)
        return RES_TOO_FEW;
    if (presetval > MAXN)
        return RES_TOO_MANY;
    if (2*size*xres < xreal)
        return RES_TOO_SMALL;
    if (4*size > xreal)
        return RES_TOO_LARGE;

    xdata = g_new(gint, presetval);
    ydata = g_new(gint, presetval);
    disizes = g_new(gdouble, presetval);
    rdisizes = g_new(gdouble, presetval);
    rx = g_new(gdouble, presetval);
    ry = g_new(gdouble, presetval);
    rz = g_new(gdouble, presetval);
    vx = g_new0(gdouble, presetval);
    vy = g_new0(gdouble, presetval);
    vz = g_new0(gdouble, presetval);
    ax = g_new0(gdouble, presetval);
    ay = g_new0(gdouble, presetval);
    az = g_new0(gdouble, presetval);
    fx = g_new(gdouble, presetval);
    fy = g_new(gdouble, presetval);
    fz = g_new(gdouble, presetval);
    bp = g_new0(gint, presetval);    // list of triplets (having the same value)
    active = g_new0(gint, presetval); // list of reasonably located particles


    /*allocate field with increased size, do all the computation and cut field back, return dfield again*/
    lfield = gwy_data_field_new(xres, yres,
                                xreal, yreal,
                                TRUE);
    gwy_data_field_area_copy(dfield, lfield, 0, 0, oxres, oyres, add, add);

    gwy_data_field_invert(dfield, 1, 0, 0);
    gwy_data_field_area_copy(dfield, lfield, 0, oyres-add-1, oxres, add, add, 0);
    gwy_data_field_area_copy(dfield, lfield, 0, 0, oxres, add, add, yres-add-1);
    gwy_data_field_invert(dfield, 1, 0, 0);

    gwy_data_field_invert(dfield, 0, 1, 0);
    gwy_data_field_area_copy(dfield, lfield, oxres-add-1, 0, add, oyres, 0, add);
    gwy_data_field_area_copy(dfield, lfield, 0, 0, add, oyres, xres-add-1, add);
    gwy_data_field_invert(dfield, 0, 1, 0);

    gwy_data_field_invert(dfield, 1, 1, 0);
    gwy_data_field_area_copy(dfield, lfield, oxres-add-1, oyres-add-1, add, add, 0, 0);
    gwy_data_field_area_copy(dfield, lfield, 0, 0, add, add, xres-add-1, yres-add-1);
    gwy_data_field_area_copy(dfield, lfield, oxres-add-1, 0, add, add, 0, yres-add-1);
    gwy_data_field_area_copy(dfield, lfield, 0, oyres-add-1, add, add, xres-add-1, 0);
    gwy_data_field_invert(dfield, 1, 1, 0);


    zlfield = gwy_data_field_duplicate(lfield);
    zdfield = gwy_data_field_duplicate(dfield);

    ndata = steps = ntr = 0;

    width_from = G_MAXDOUBLE;
    width_to = -G_MAXDOUBLE;

    /*revise steps*/
    message = g_string_new(NULL);
    for (i = 0; i < args->revise; i++) {
        g_string_printf(message,
                        _("Running revise (%d active particles)..."),
                        nactive/3);
        if (!gwy_app_wait_set_message(message->str))
            break;

        /*try to add some particles, wait some time until the previous can relax*/
        if ((i%200)==0 && ndata<(presetval-3) && i<(3*args->revise/4)) {

            ii = 0;
            nloc = 0;

            while (ndata < presetval && ii < max/1000 && nloc < maxloc) {
                size = norm*args->size + gwy_rand_gen_set_gaussian(rngset, 0,
                                                                   norm*args->width);
                if (size<args->size/100)
                    size = args->size/100;

                disize = gwy_data_field_rtoi(dfield, size);

                xpos = CLAMP(disize+(g_rand_double(rng)*(xres-2*(gint)(disize+1))) + 1, 0, xres-1);
                ypos = CLAMP(disize+(g_rand_double(rng)*(yres-2*(gint)(disize+1))) + 1, 0, yres-1);

                ii++;
                too_close = 0;

                rdisizes[ndata] = size;

                rxv = ((gdouble)xpos*oxreal/(gdouble)oxres);
                ryv = ((gdouble)ypos*oyreal/(gdouble)oyres);
                rzv = gwy_data_field_get_val(zlfield, xpos, ypos) + rdisizes[ndata] + 10*size;

                angle = G_PI*g_rand_double(rng);
                aspect = (args->aspect + gwy_rand_gen_set_gaussian(rngset, 0, args->aspect_noise)) - 1.0;

                for (k = 0; k < ndata; k++) {
                    if (((rxv-rx[k])*(rxv-rx[k])
                         + (ryv-ry[k])*(ryv-ry[k])
                         + (rzv-rz[k])*(rzv-rz[k])) < 10.0*size*size) {
                        too_close = 1;
                        break;
                    }
                }
                if (too_close)
                    continue;

                if (ndata >= 10000)
                    break;


                if (width_from > 2*size/norm) width_from = 2*size/norm;
                if (width_to < 2*size/norm) width_to = 2*size/norm;

                xdata[ndata] = xpos;
                ydata[ndata] = ypos;
                disizes[ndata] = disize;
                rdisizes[ndata] = size;
                rx[ndata] = rxv - aspect*size*cos(angle);
                ry[ndata] = ryv - aspect*size*sin(angle);
                rz[ndata] = rzv;
                vz[ndata] = -0.005;
                bp[ndata] = ntr;
                active[ndata] = 1;
                ndata++;

                xdata[ndata] = xpos;
                ydata[ndata] = ypos;
                disizes[ndata] = disize;
                rdisizes[ndata] = size;
                rx[ndata] = rxv;
                ry[ndata] = ryv;
                rz[ndata] = rzv;
                vz[ndata] = -0.005;
                bp[ndata] = ntr;
                active[ndata] = 1;
                ndata++;

                xdata[ndata] = xpos;
                ydata[ndata] = ypos;
                disizes[ndata] = disize;
                rdisizes[ndata] = size;
                rx[ndata] = rxv + aspect*size*cos(angle);
                ry[ndata] = ryv + aspect*size*sin(angle);
                rz[ndata] = rzv;
                vz[ndata] = -0.005;
                bp[ndata] = ntr;
                active[ndata] = 1;

                //printf("adding triplet %d of size %g to %d %d %d   %g %g %g\n", ntr, rdisizes[ndata], ndata-2, ndata-1, ndata, rx[ndata], ry[ndata], rz[ndata]);

                ntr++;
                ndata++;
                nloc++;

            };
        }

        maxdist = 100*size*size; //tolerance for single rod bond stretch


        /*calculate forces for all the active particles*/
        for (k = 0; k < ndata; k++) {

            if (active[k]==0) continue;


            fx[k] = fy[k] = fz[k] = 0;
            /*calculate forces for all particles on substrate*/

            if (gwy_data_field_rtoi(lfield, rx[k]) < 0
                || gwy_data_field_rtoj(lfield, ry[k]) < 0
                || gwy_data_field_rtoi(lfield, rx[k]) >= xres
                || gwy_data_field_rtoj(lfield, ry[k]) >= yres)
                continue;

            for (m = 0; m < ndata; m++) {
                if (m == k || bp[m] == bp[k])
                    continue;

                fx[k] -= (get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k]+diff, ry[k], rz[k], rdisizes[k], rdisizes[m], args->ljparticle)
                              -get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k]-diff, ry[k], rz[k], rdisizes[k], rdisizes[m], args->ljparticle))/2/diff;
                fy[k] -= (get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k], ry[k]+diff, rz[k], rdisizes[k], rdisizes[m], args->ljparticle)
                              -get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k], ry[k]-diff, rz[k], rdisizes[k], rdisizes[m], args->ljparticle))/2/diff;
                fz[k] -= (get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k], ry[k], rz[k]+diff, rdisizes[k], rdisizes[m], args->ljparticle)
                              -get_lj_potential_spheres(rx[m], ry[m], rz[m], rx[k], ry[k], rz[k]-diff, rdisizes[k], rdisizes[m], args->ljparticle))/2/diff;

            }
            zval = gwy_data_field_get_val(lfield, CLAMP(gwy_data_field_rtoi(zlfield, rx[k]), 0, gwy_data_field_get_xres(zlfield)-1),
                                          CLAMP(gwy_data_field_rtoi(zlfield, ry[k]), 0, gwy_data_field_get_yres(zlfield)-1));


            fz[k] -= (integrate_lj_substrate(zval, rz[k]+diff, rdisizes[k], args->ljsurface)
                    - integrate_lj_substrate(zval, rz[k]-diff, rdisizes[k], args->ljsurface))/2/diff;

            

            //effects on surface
            if ((rz[k]-zval)>1.2*size) {
                 fz[k] -= args->gravity*1e-7; //some 'gravity' everywhere to let it fall down even from large heights where integrated L-J is almost zero

            } else {
               vx[k] *= args->mobility;
               vy[k] *= args->mobility;

            }

        }


        //clamp forces to prevent too fast movements at extreme parameters cases

        nactive = 0;
        for (k = 0; k < ndata; k++) {
            if (!active[k])
                continue;

            fx[k] = CLAMP(fx[k], -100, 100);
            fy[k] = CLAMP(fy[k], -100, 100);
            fz[k] = CLAMP(fz[k], -100, 100);

            nactive++;
        }

        g_string_printf(message, _("Running revise (%d active particles)..."),
                        nactive/3);
        if (!gwy_app_wait_set_message(message->str))
            break;


        //run Verlet algorithm
        for (k=0; k<ndata; k++)
        {
            if (active[k]==0) continue;

            rx[k] += vx[k]*timestep + 0.5*ax[k]*timestep*timestep;
            vx[k] += 0.5*ax[k]*timestep;
            ax[k] = fx[k]/mass;
            vx[k] += 0.5*ax[k]*timestep;
            vx[k] *= 0.95;

            ry[k] += vy[k]*timestep + 0.5*ay[k]*timestep*timestep;
            vy[k] += 0.5*ay[k]*timestep;
            ay[k] = fy[k]/mass;
            vy[k] += 0.5*ay[k]*timestep;
            vy[k] *= 0.95;

            rz[k] += vz[k]*timestep + 0.5*az[k]*timestep*timestep;
            vz[k] += 0.5*az[k]*timestep;
            az[k] = fz[k]/mass;
            vz[k] += 0.5*az[k]*timestep;
            vz[k] *= 0.95;

        }

        //exclude what is no more usable (only deactivate it)
        for (k=0; k<ndata; k++)
        {
            if (active[k]==0) continue;

            //too far lateraly from the surface check
            if (rx[k]<(-2*rdisizes[k]) || ry[k]<(-2*rdisizes[k]) || rx[k]>(xreal+2*rdisizes[k]) || ry[k]>(yreal+2*rdisizes[k])) {
                printf("%d lost\n", k);
                active[3*(int)(k/3)] = active[3*(int)(k/3)+1] = active[3*(int)(k/3)+2] = 0;
            }
        }
        for (k=0; k<ndata; k+=3)
        {
            if (active[k]==0) continue;

            //consistency check
            if (((rx[k]-rx[k+1])*(rx[k]-rx[k+1]) + (ry[k]-ry[k+1])*(ry[k]-ry[k+1]) + (rz[k]-rz[k+1])*(rz[k]-rz[k+1]))>maxdist
               || ((rx[k+1]-rx[k+2])*(rx[k+1]-rx[k+2]) + (ry[k+1]-ry[k+2])*(ry[k+1]-ry[k+2]) + (rz[k+1]-rz[k+2])*(rz[k+1]-rz[k+2]))>maxdist)
            {
                printf("%d stretched too much\n", k);
                active[k] = active[k+1] = active[k+2] = 0;

            }
        }

       //run SETTLE algorithm
       for (k=0; k<ndata; k+=3)
        {
            if (active[k]==0) continue;

            ib = k;
            ic = k+1;
            ia = k+2;
            ma = mb = mc = mass;

            ca = dotcos(rx, ry, rz, ia, ib, ic);
            cb = dotcos(rx, ry, rz, ib, ia, ic);
            cc = dotcos(rx, ry, rz, ic, ia, ib);

            vab = vcomp(rx, ry, rz, vx, vy, vz, ia, ib);
            vca = vcomp(rx, ry, rz, vx, vy, vz, ic, ia);
            vbc = vcomp(rx, ry, rz, vx, vy, vz, ib, ic);


            tab = ma*(vab*(2*(ma + mb) - ma*ca*ca)
                    + vbc*(mb*cc*ca - (ma + mb)*cb)
                    + vca*(ma*cb*cc - 2*mb*ca));

            tbc = vbc*((ma + mb)*(ma + mb) - mb*mb*ca*ca)
                + vca*ma*(mb*ca*cb - (ma+mb)*cc)
                + vab*ma*(mb*cc*ca - (ma + mb)*cb);

            tca = ma*(vca*(2*(ma + mb) - ma*cb*cb)
                    + vab*(ma*cb*cc - 2*mb*ca)
                    + vbc*(mb*ca*cb - (ma+mb)*cc));

            xd = timestep*(2*(ma+mb)*(ma+mb) + 2*ma*mb*ca*cb*cc
                                - 2*mb*mb*ca*ca - ma*(ma + mb)
                                *(cb*cb + cc*cc))/2.0/mb;


 /*           tab = mass*(vab*(4*mass - mass*ca*ca)
                    + vbc*(mass*cc*ca - 2*mass*cb)
                    + vca*(mass*cb*cc - 2*mass*ca));

            tbc = vbc*(4*mass*mass - mass*mass*ca*ca)
                + vca*mass*(mb*ca*cb - 2*mass*cc)
                + vab*mass*(mb*cc*ca - 2*mass*cb);

            tca = mass*(vca*(4*mass - mass*cb*cb)
                    + vab*(mass*cb*cc - 2*mass*ca)
                    + vbc*(mass*ca*cb - 2*mass*cc));

            xd = timestep*(4*mass*mass + 2*mass*mass*ca*cb*cc
                                - 2*mass*mass*ca*ca - 2*mass*mass
                                *(cb*cb + cc*cc))/2.0/mass;
*/


            if (fabs(xd)>1e-15) {
               tab /= xd;
               tbc /= xd;
               tca /= xd;
            }

            eabx = rx[ib] - rx[ia];
            eaby = ry[ib] - ry[ia];
            eabz = rz[ib] - rz[ia];
            snorm = sqrt(eabx*eabx + eaby*eaby + eabz*eabz);
            eabx /= snorm;
            eaby /= snorm;
            eabz /= snorm;

            ebcx = rx[ic] - rx[ib];
            ebcy = ry[ic] - ry[ib];
            ebcz = rz[ic] - rz[ib];
            snorm = sqrt(ebcx*ebcx + ebcy*ebcy + ebcz*ebcz);
            ebcx /= snorm;
            ebcy /= snorm;
            ebcz /= snorm;

            ecax = rx[ia] - rx[ic];
            ecay = ry[ia] - ry[ic];
            ecaz = rz[ia] - rz[ic];
            snorm = sqrt(ecax*ecax + ecay*ecay + ecaz*ecaz);
            ecax /= snorm;
            ecay /= snorm;
            ecaz /= snorm;

            //multiplication factor of 2 added experimentally to strengthen the constraint
            vx[ia] += 1*timestep/2.0/ma*(tab*eabx - tca*ecax);
            vy[ia] += 1*timestep/2.0/ma*(tab*eaby - tca*ecay);
            vz[ia] += 1*timestep/2.0/ma*(tab*eabz - tca*ecaz);

            vx[ib] += 1*timestep/2.0/mb*(tbc*ebcx - tab*eabx);
            vy[ib] += 1*timestep/2.0/mb*(tbc*ebcy - tab*eaby);
            vz[ib] += 1*timestep/2.0/mb*(tbc*ebcz - tab*eabz);

            vx[ic] += 1*timestep/2.0/mc*(tca*ecax - tbc*ebcx);
            vy[ic] += 1*timestep/2.0/mc*(tca*ecay - tbc*ebcy);
            vz[ic] += 1*timestep/2.0/mc*(tca*ecaz - tbc*ebcz);

        }

        gwy_data_field_copy(zlfield, lfield, 0);

        /* XXX: Still makes progressive preview much slower.  Can showit2() be
         * made faster? */
        if (args->animated && i % 50 == 49) {
            if (showfield) {
                showit2(lfield, zdfield,
                        rdisizes, rx, ry, rz, xdata, ydata, active, ndata,
                        oxres, oxreal, oyres, oyreal, add, xres, yres);
                GWY_OBJECT_UNREF(surface);
                surface = surface_for_preview(dfield, PREVIEW_SIZE);
                gwy_data_field_copy(surface, showfield, FALSE);
                gwy_data_field_data_changed(showfield);
            }

            gwy_data_field_area_copy(lfield, dfield, add, add, oxres, oyres, 0, 0);
            gwy_data_field_data_changed(dfield);
        }


        if (!gwy_app_wait_set_fraction((gdouble)i/(gdouble)args->revise))
              break;
    }
    g_string_free(message, TRUE);

    //debug output
/*
    fo = fopen("xo.txt", "w");
    for (k=0; k<ndata; k++)
    {
        if (active[k]==0) continue;
        fprintf(fo, "%g %g %g %d    %d\n", rx[k], ry[k], rz[k], bp[k], active[k]);
    }
    fclose(fo);
*/

    gwy_data_field_copy(zlfield, lfield, 0);

    showit2(lfield, zdfield,
            rdisizes, rx, ry, rz, xdata, ydata, active, ndata,
            oxres, oxreal, oyres, oyreal, add, xres, yres);
    GWY_OBJECT_UNREF(surface);

    if (showfield) {
        surface = surface_for_preview(dfield, PREVIEW_SIZE);
        gwy_data_field_copy(surface, showfield, FALSE);
        gwy_data_field_data_changed(showfield);
    }

    if (outdata) {
        *ondata = 0;

        for (k = 0; k < ndata; k += 3) {
            if (active[k] != 0) (*ondata) += 3;
        }

        g_free(*oxdata);
        g_free(*oydata);
        g_free(*ozdata);
        g_free(*ordata);

        *oxdata = g_new(gdouble, *ondata);
        *oydata = g_new(gdouble, *ondata);
        *ozdata = g_new(gdouble, *ondata);
        *ordata = g_new(gdouble, *ondata);

        i = 0;
        for (k = 0; k < ndata; k += 3) {
            if (active[k] == 0) continue;

            for (j=0; j<3; j++) {
               (*oxdata)[i] = rx[k+j]/norm;
               (*oydata)[i] = ry[k+j]/norm;
               (*ozdata)[i] = rz[k+j]/norm;
               (*ordata)[i] = rx[k+j]/norm;
               i++;
            }
        }
    }


    //calculate statistics
    if (args->outstats && stats_length && stats_width && stats_aspectratio) {
        length_from = G_MAXDOUBLE;
        length_to = -G_MAXDOUBLE;
        aspectratio_from = G_MAXDOUBLE;
        aspectratio_to = -G_MAXDOUBLE;
        theta_from = G_MAXDOUBLE;
        theta_to = -G_MAXDOUBLE;
        phi_from = G_MAXDOUBLE;
        phi_to = -G_MAXDOUBLE;


        for (k = 0; k < ndata; k += 3) {
            if (active[k] == 0)
                continue;

            length = (sqrt((rx[k]-rx[k+2])*(rx[k]-rx[k+2]) + (ry[k]-ry[k+2])*(ry[k]-ry[k+2]) + (rz[k]-rz[k+2])*(rz[k]-rz[k+2])) + 2*rdisizes[k])/norm;
            aspect = length/(2*rdisizes[k]/norm);

            if (length>0) theta = 180*asin(((rz[k+2] - rz[k])/norm)/length)/G_PI;
            else theta = 0;
            phi = 180*atan2(ry[k+2] - ry[k], rx[k+2]-rx[k])/G_PI;
            if (phi<0) phi += 180;

            if (length_from > length)
                length_from = length;
            if (length_to < length)
                length_to = length;

            if (aspectratio_from > aspect)
                aspectratio_from = aspect;
            if (aspectratio_to < aspect)
                aspectratio_to = aspect;

            if (theta_from > theta)
                theta_from = theta;
            if (theta_to < theta)
                theta_to = theta;

            if (phi_from > phi)
                phi_from = phi;
            if (phi_to < phi)
                phi_to = phi;
          }

        if (length_from == length_to) {
            length_from -= 0.01*length_from;
            length_to += 0.01*length_to;
        }
        if (width_from == width_to) {
            width_from -= 0.01*width_from;
            width_to += 0.01*width_to;
        }
        if (aspectratio_from == aspectratio_to) {
            aspectratio_from -= 0.01*aspectratio_from;
            aspectratio_to += 0.01*aspectratio_to;
        }
        if (theta_from == theta_to) {
            theta_from -= 0.01;
            theta_to += 0.01;
        }
        if (phi_from == phi_to) {
            phi_from -= 0.01;
            phi_to += 0.01;
        }
        

        if (aspectratio_to>5) aspectratio_to = 5; //prevent some completely wrong particle to destroy the statistics
        if (length_to>(5*aspectratio_to*width_to)) length_to = 5*aspectratio_to*width_to;

        gwy_data_line_set_si_unit_x(stats_length, gwy_data_field_get_si_unit_xy(dfield));
        gwy_data_line_set_si_unit_x(stats_width, gwy_data_field_get_si_unit_xy(dfield));

        gwy_data_line_resample(stats_length, nstat, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(stats_width, nstat, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(stats_aspectratio, nstat, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(stats_theta, nstat, GWY_INTERPOLATION_NONE);
        gwy_data_line_resample(stats_phi, nstat, GWY_INTERPOLATION_NONE);
 
        ldata = gwy_data_line_get_data(stats_length);
        wdata = gwy_data_line_get_data(stats_width);
        adata = gwy_data_line_get_data(stats_aspectratio);
        thetadata = gwy_data_line_get_data(stats_theta);
        phidata = gwy_data_line_get_data(stats_phi);

        for (k = 0; k < ndata; k += 3) {
             wdata[(gint)((nstat-1)*(2*rdisizes[k]/norm-width_from)/(width_to-width_from))] += 1;

             length = (sqrt((rx[k]-rx[k+2])*(rx[k]-rx[k+2]) + (ry[k]-ry[k+2])*(ry[k]-ry[k+2]) + (rz[k]-rz[k+2])*(rz[k]-rz[k+2])) + 2*rdisizes[k])/norm;
             ldata[(gint)((nstat-1)*(length-length_from)/(length_to-length_from))] += 1;

             aspect = length/(2*rdisizes[k]/norm);
             adata[(gint)((nstat-1)*(aspect-aspectratio_from)/(aspectratio_to-aspectratio_from))] += 1;

             if (length>0) theta = 180*asin(((rz[k+2] - rz[k])/norm)/length)/G_PI;
             else theta = 0;

             thetadata[(gint)((nstat-1)*(theta-theta_from)/(theta_to-theta_from))] += 1;

             phi = 180*atan2(ry[k+2] - ry[k], rx[k+2]-rx[k])/G_PI;
             if (phi<0) phi += 180;
             phidata[(gint)((nstat-1)*(phi-phi_from)/(phi_to-phi_from))] += 1;

        }

        gwy_data_line_set_offset(stats_length, length_from);
        gwy_data_line_set_offset(stats_width, width_from);
        gwy_data_line_set_offset(stats_aspectratio, aspectratio_from);
        gwy_data_line_set_offset(stats_theta, theta_from);
        gwy_data_line_set_offset(stats_phi, phi_from);

        gwy_data_line_set_real(stats_length, (length_to - length_from)*(gdouble)nstat/((gdouble)(nstat-1)));
        gwy_data_line_set_real(stats_width, (width_to - width_from)*(gdouble)nstat/((gdouble)(nstat-1)));
        gwy_data_line_set_real(stats_aspectratio, (aspectratio_to - aspectratio_from)*(gdouble)nstat/((gdouble)(nstat-1)));
        gwy_data_line_set_real(stats_theta, (theta_to - theta_from)*(gdouble)nstat/((gdouble)(nstat-1)));
        gwy_data_line_set_real(stats_phi, (phi_to - phi_from)*(gdouble)nstat/((gdouble)(nstat-1)));
  
    }


    gwy_data_field_area_copy(lfield, dfield, add, add, oxres, oyres, 0, 0);
    gwy_data_field_data_changed(dfield);

    /*denormalize all*/
    gwy_data_field_multiply(dfield, 1/norm);
    gwy_data_field_set_xreal(dfield, gwy_data_field_get_xreal(dfield)/norm);
    gwy_data_field_set_yreal(dfield, gwy_data_field_get_yreal(dfield)/norm);
    /*denormalized*/

    GWY_OBJECT_UNREF(lfield);
    GWY_OBJECT_UNREF(zlfield);
    GWY_OBJECT_UNREF(zdfield);

    g_free(xdata);
    g_free(ydata);
    g_free(disizes);
    g_free(rdisizes);
    g_free(rx);
    g_free(ry);
    g_free(rz);
    g_free(vx);
    g_free(vy);
    g_free(vz);
    g_free(ax);
    g_free(ay);
    g_free(az);
    g_free(fx);
    g_free(fy);
    g_free(fz);
    g_free(bp);

    gwy_rand_gen_set_free(rngset);

    if (ndata != presetval)
        *success = FALSE;
    else
        *success = TRUE;

    return ndata;
}

static const gchar*
particle_error(gint code)
{
    if (code == RES_TOO_MANY)
        return _("Error: too many particles.");
    if (code == RES_TOO_FEW)
        return _("Error: no particles.");
    if (code == RES_TOO_LARGE)
        return _("Error: particles too large.");
    if (code == RES_TOO_SMALL)
        return _("Error: particles too small.");
    return "";
}

static const gchar prefix[]           = "/module/rod_deposit_synth";
static const gchar active_page_key[]  = "/module/rod_deposit_synth/active_page";
static const gchar animated_key[]     = "/module/rod_deposit_synth/animated";
static const gchar aspect_key[]       = "/module/rod_deposit_synth/aspect";
static const gchar aspect_noise_key[] = "/module/rod_deposit_synth/aspect_noise";
static const gchar coverage_key[]     = "/module/rod_deposit_synth/coverage";
static const gchar gravity_key[]      = "/module/rod_deposit_synth/gravity";
static const gchar ljparticle_key[]   = "/module/rod_deposit_synth/ljparticle";
static const gchar ljsurface_key[]    = "/module/rod_deposit_synth/ljsurface";
static const gchar mobility_key[]     = "/module/rod_deposit_synth/mobility";
static const gchar outstats_key[]     = "/module/rod_deposit_synth/outstats";
static const gchar randomize_key[]    = "/module/rod_deposit_synth/randomize";
static const gchar revise_key[]       = "/module/rod_deposit_synth/revise";
static const gchar seed_key[]         = "/module/rod_deposit_synth/seed";
static const gchar size_key[]         = "/module/rod_deposit_synth/size";
static const gchar width_key[]        = "/module/rod_deposit_synth/width";

static void
rod_deposit_synth_sanitize_args(RodDepositSynthArgs *args)
{
    args->active_page = CLAMP(args->active_page,
                              PAGE_DIMENSIONS, PAGE_NPAGES-1);
    args->animated = !!args->animated;
    args->outstats = !!args->outstats;
    args->seed = MAX(0, args->seed);
    args->randomize = !!args->randomize;
    args->size = CLAMP(args->size, 0.0, 100); //FIXME this should be absolute value!
    args->width = CLAMP(args->width, 0.0, 100); //here as well
    args->coverage = CLAMP(args->coverage, 0, 100);
    args->aspect = CLAMP(args->aspect, 1.01, 3);
    args->aspect_noise = CLAMP(args->aspect_noise, 0, 2);
    args->revise = CLAMP(args->revise, 0, 1000000);
    args->gravity = CLAMP(args->gravity, 0, 1000);
    args->mobility = CLAMP(args->mobility, 0, 1);
    args->ljsurface = CLAMP(args->ljsurface, 0, 1000);
    args->ljparticle = CLAMP(args->ljparticle, 0, 1000);
}

static void
rod_deposit_synth_load_args(GwyContainer *container,
                            RodDepositSynthArgs *args,
                            GwyDimensionArgs *dimsargs)
{
    *args = rod_deposit_synth_defaults;

    gwy_container_gis_int32_by_name(container, active_page_key,
                                    &args->active_page);
    gwy_container_gis_boolean_by_name(container, animated_key, &args->animated);
    gwy_container_gis_boolean_by_name(container, outstats_key, &args->outstats);
    gwy_container_gis_int32_by_name(container, seed_key, &args->seed);
    gwy_container_gis_boolean_by_name(container, randomize_key,
                                      &args->randomize);
    gwy_container_gis_double_by_name(container, size_key, &args->size);
    gwy_container_gis_double_by_name(container, width_key, &args->width);
    gwy_container_gis_double_by_name(container, coverage_key, &args->coverage);
    gwy_container_gis_double_by_name(container, aspect_key, &args->aspect);
    gwy_container_gis_double_by_name(container, aspect_noise_key, &args->aspect_noise);
    gwy_container_gis_double_by_name(container, gravity_key, &args->gravity);
    gwy_container_gis_double_by_name(container, mobility_key, &args->mobility);
    gwy_container_gis_double_by_name(container, ljsurface_key, &args->ljsurface);
    gwy_container_gis_double_by_name(container, ljparticle_key, &args->ljparticle);
    gwy_container_gis_int32_by_name(container, revise_key, &args->revise);
    rod_deposit_synth_sanitize_args(args);

    gwy_clear(dimsargs, 1);
    gwy_dimensions_copy_args(&dims_defaults, dimsargs);
    gwy_dimensions_load_args(dimsargs, container, prefix);
}

static void
rod_deposit_synth_save_args(GwyContainer *container,
                            const RodDepositSynthArgs *args,
                            const GwyDimensionArgs *dimsargs)
{
    gwy_container_set_int32_by_name(container, active_page_key,
                                    args->active_page);
    gwy_container_set_boolean_by_name(container, animated_key, args->animated);
    gwy_container_set_boolean_by_name(container, outstats_key, args->outstats);
    gwy_container_set_int32_by_name(container, seed_key, args->seed);
    gwy_container_set_boolean_by_name(container, randomize_key,
                                      args->randomize);
    gwy_container_set_double_by_name(container, size_key, args->size);
    gwy_container_set_double_by_name(container, width_key, args->width);
    gwy_container_set_double_by_name(container, coverage_key, args->coverage);
    gwy_container_set_double_by_name(container, aspect_key, args->aspect);
    gwy_container_set_double_by_name(container, aspect_noise_key, args->aspect_noise);
    gwy_container_set_double_by_name(container, gravity_key, args->gravity);
    gwy_container_set_double_by_name(container, mobility_key, args->mobility);
    gwy_container_set_double_by_name(container, ljsurface_key, args->ljsurface);
    gwy_container_set_double_by_name(container, ljparticle_key, args->ljparticle);
    gwy_container_set_int32_by_name(container, revise_key, args->revise);

    gwy_dimensions_save_args(dimsargs, container, prefix);
}


static GString*     
create_xyz_report     (RodDepositSynthControls *controls)
{
   GString *report = g_string_new(NULL);
   gint i;

   g_string_append_printf(report, "%d\n", controls->ndata);
   g_string_append_printf(report, "\n");

   g_return_val_if_fail(controls->xdata, report);
   g_return_val_if_fail(controls->ydata, report);
   g_return_val_if_fail(controls->zdata, report);
   g_return_val_if_fail(controls->rdata, report);

   for (i=0; i<controls->ndata; i++)
   {  
       gwy_format_result_table_row(report, GWY_RESULTS_REPORT_MACHINE | GWY_RESULTS_REPORT_TABSEP,
                    4, controls->rdata[i], controls->xdata[i], controls->ydata[i], controls->zdata[i]); 
//       g_string_append_printf(report, "%g %g %g %g\n", controls->rdata[i], controls->xdata[i], controls->ydata[i], controls->zdata[i]);
   }

   return report;
}


/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
