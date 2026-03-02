/*
 *  $Id: smm.c 26324 2024-05-07 07:50:38Z yeti-dn $
 *  Copyright (C) 2024 David Necas (Yeti), Petr Klapetek.
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
#include <complex.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwynlfit.h>
#include <libprocess/stats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwycombobox.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    NARGS = 8,
    NFITS = 6,
    RESPONSE_FIT = 5,
    RESPONSE_PLOTINITS = 6,
};

typedef enum {
    SMM_SIGNAL_REAL      = 0,
    SMM_SIGNAL_IMAGINARY = 1,
    SMM_SIGNAL_MAG       = 2,
    SMM_SIGNAL_LOGMAG    = 3,
    SMM_SIGNAL_PHASE     = 4,
} SMMSignal;

typedef enum {
    SMM_SHOW_S11_REAL      = 0,
    SMM_SHOW_S11_IMAGINARY = 1,
    SMM_SHOW_CAPACITANCE   = 2,
} SMMShow;


enum {
    PARAM_IMAGE_A,
    PARAM_SIGNAL_A,
    PARAM_IMAGE_B,
    PARAM_SIGNAL_B,
    PARAM_DISPLAY,
    PARAM_SHOW,
    PARAM_REFIMP,
    PARAM_FREQ,
    PARAM_TARGET_GRAPH,
    PARAM_IMAGE_0,                              /* Block with NARGS items; enabled[0] is always TRUE. */
    PARAM_ENABLED_0 = PARAM_IMAGE_0 + NARGS,    /* Block with NARGS items. */
    PARAM_FIT_0 = PARAM_ENABLED_0 + NARGS,      /* Block with NARGS items. */
    PARAM_CAPACITANCE_0 = PARAM_FIT_0 + NARGS,  /* Block with NARGS items. */
    PARAM_IFIT_0 = PARAM_CAPACITANCE_0 + NARGS, /* Fit initials */
    PARAM_DOFIT_0 = PARAM_IFIT_0 + NFITS,       /* Fit or fix the particular parameter */
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel_nominal;
    GwyGraphCurveModel *gcmodel_measured;
    double complex init_e00;
    double complex init_e01;
    double complex init_e11;
    double complex e00;
    double complex e01;
    double complex e11;
    gdouble rmeas[NARGS];
    gdouble imeas[NARGS];
    gdouble rnominal[NARGS];
    gdouble inominal[NARGS];
    gdouble rcalc[NARGS];
    gdouble icalc[NARGS];
    gdouble capacitance[NARGS];
    gdouble capcalc[NARGS];
    gboolean fitme[NARGS];
    gint ndata;
    gdouble rss;
    gboolean fitted;
 } ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkWidget *channel_a;
    GtkWidget *channel_b;
    GtkWidget *mask[NARGS];
    GtkObject *capacitance_adj[NARGS];
    GtkWidget *capacitance_spin[NARGS];
    GtkWidget *enabled[NARGS];
    GtkWidget *fit[NARGS];
    GtkWidget *display[NARGS];
    GtkObject *ifit_adj[NFITS];
    GtkWidget *ifit_spin[NFITS];
    GtkWidget *dofit[NFITS];
    GtkWidget *rfit[NFITS];
    GtkWidget *result[NFITS];
    GtkWidget *rss;
    GwyContainer *data;
    GtkWidget *view;
    gboolean in_update;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             smm                     (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GtkWidget*       create_image_table      (ModuleGUI *gui);
static GtkWidget*       create_fit_table        (ModuleGUI *gui);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             dialog_response         (GwyDialog *dialog,
                                                 gint response,
                                                 ModuleGUI *gui);
static void             preview                 (gpointer user_data);
static void             estimate                (ModuleGUI *gui);
static void             fit                     (ModuleGUI *gui);
static void             enabled_changed         (ModuleGUI *gui,
                                                 GtkToggleButton *check);
static void             fit_changed             (ModuleGUI *gui,
                                                 GtkToggleButton *check);
static void             image_selected          (ModuleGUI *gui,
                                                 GwyDataChooser *chooser);
static void             display_changed         (ModuleGUI *gui,
                                                 GtkToggleButton *toggle);
static void             capacitance_changed     (ModuleGUI *gui,
                                                 GtkAdjustment *adj);
static void             dofit_changed           (ModuleGUI *gui,
                                                 GtkToggleButton *check);
static void             copy_inits              (GObject *button,
                                                 ModuleGUI *gui);
static void             ifit_changed            (ModuleGUI *gui,
                                                 GtkAdjustment *adj);
static void             smmeval                 (double complex s11m[3],
                                                 double complex s11[3],
                                                 double complex *re00,
                                                 double complex *re01,
                                                 double complex *re11);
static gdouble          smmfit                  (double complex *s11m,
                                                 double complex *s11,
                                                 gint n,
                                                 double complex *re00,
                                                 double complex *re01,
                                                 double complex *re11,
                                                 gboolean *fix);
static gboolean         image_filter            (GwyContainer *data,
                                                 gint id,
                                                 gpointer user_data);
static gchar*           create_report           (ModuleGUI *gui);
static void             save_coeffs             (ModuleGUI *gui);
static void             copy_coeffs             (ModuleGUI *gui);


static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Interprets SMM measurement on a calibration sample"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.0",
    "Petr Klapetek & David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, smm)

static gboolean
module_register(void)
{
    gwy_process_func_register("smm",
                              (GwyProcessFunc)&smm,
                              N_("/_SPM Modes/_Electrical/_SMM Calibration..."),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Calibrate SMM using capacitors calibration sample"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum signals[] = {
        { N_("Re"),       SMM_SIGNAL_REAL,       },
        { N_("Im"),       SMM_SIGNAL_IMAGINARY,  },
        { N_("Mag"),      SMM_SIGNAL_MAG,        },
        { N_("logMag"),   SMM_SIGNAL_LOGMAG,     },
        { N_("Phase"),    SMM_SIGNAL_PHASE,      },
    };
    static const GwyEnum shows[] = {
        { N_("Re (S11)"),       SMM_SHOW_S11_REAL,       },
        { N_("Im (S11)"),       SMM_SHOW_S11_IMAGINARY,  },
        { N_("Capacitance"),    SMM_SHOW_CAPACITANCE,    },
    };


    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_int(paramdef, PARAM_DISPLAY, NULL, gwy_sgettext("verb|Display"), 0, NARGS-1, 0);
    gwy_param_def_add_image_id(paramdef, PARAM_IMAGE_A, "image_a", _("First channel"));
    gwy_param_def_add_image_id(paramdef, PARAM_IMAGE_B, "image_b", _("Second channel"));
    gwy_param_def_add_gwyenum(paramdef, PARAM_SIGNAL_A, "signal_a", "",
                              signals, G_N_ELEMENTS(signals), SMM_SIGNAL_REAL);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SIGNAL_B, "signal_b", "",
                              signals, G_N_ELEMENTS(signals), SMM_SIGNAL_IMAGINARY);
    gwy_param_def_add_gwyenum(paramdef, PARAM_SHOW, "show", _("Plot result"),
                              shows, G_N_ELEMENTS(shows), SMM_SHOW_S11_REAL);
    gwy_param_def_add_double(paramdef, PARAM_REFIMP,
                             "refimp", _("Reference impedance"),
                             0.001, 1000, 50);
    gwy_param_def_add_double(paramdef, PARAM_FREQ,
                             "freq", _("Frequency"),
                             0.1, 100, 6);

    /* The strings must be static so just ‘leak’ them. */
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_image_id(paramdef, PARAM_IMAGE_0 + i,
                                   g_strdup_printf("image/%u", i), g_strdup_printf("%s %u", _("Image"), i));
    }
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_boolean(paramdef, PARAM_ENABLED_0 + i,
                                  g_strdup_printf("enabled/%u", i), g_strdup_printf("%s %u", _("Enable"), i),
                                  i == 0 || i == 1);
    }
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_boolean(paramdef, PARAM_FIT_0 + i,
                                  g_strdup_printf("fit/%u", i), g_strdup_printf("%s %u", gwy_sgettext("verb|Fit"), i),
                                  i == 0 || i == 1);
    }
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_double(paramdef, PARAM_CAPACITANCE_0 + i,
                                 g_strdup_printf("capacitance/%u", i), g_strdup_printf("%s %u", _("Capacitance"), i),
                                 1e-20, 1e-9, 1e-15);
    }
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0, "e00r", _("Re(e00)"), -20, 20, 6.9);
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0 + 1, "e00i", _("Im(e00)"), -20, 20, -7.2);
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0 + 2, "e01r", _("Re(e01)"), -20, 20, -0.15);
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0 + 3, "e01i", _("Im(e01)"), -20, 20, 5.35);
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0 + 4, "e11r", _("Re(e11)"), -20, 20, 0.55);
    gwy_param_def_add_double(paramdef, PARAM_IFIT_0 + 5, "e11i", _("Im(e11)"), -20, 20, 0.35);
    for (i = 0; i < NFITS; i++) {
        gwy_param_def_add_boolean(paramdef, PARAM_DOFIT_0 + i,
                                  g_strdup_printf("dofit/%u", i), "", TRUE);
    }

    return paramdef;
}

static void
smm(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyAppDataId dataid;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.gmodel = gwy_graph_model_new();
    args.gcmodel_nominal = gwy_graph_curve_model_new();
    args.gcmodel_measured = gwy_graph_curve_model_new();
    args.fitted = FALSE;
    g_object_set(args.gmodel,
                 "title", _("Capacitance"),
                 "axis-label-left", _("S<sub>11</sub>"),
                 "axis-label-bottom", _("Capacitance"),
                 "si-unit_x", gwy_si_unit_new("F"),
                 NULL);
    g_object_set(args.gcmodel_nominal,
                 "mode", GWY_GRAPH_CURVE_POINTS,
                 "description", _("nominal"),
                 "point-type", GWY_GRAPH_POINT_SQUARE,
                 NULL);
    g_object_set(args.gcmodel_measured,
                 "mode", GWY_GRAPH_CURVE_POINTS,
                 "description", _("measured"),
                 "point-type", GWY_GRAPH_POINT_CROSS,
                 NULL);
    gwy_graph_model_add_curve(args.gmodel, args.gcmodel_nominal);
    gwy_graph_model_add_curve(args.gmodel, args.gcmodel_measured);


    args.params = gwy_params_new_from_settings(define_module_params());
    /* The first image is always the current image; it is always enabled and always displayed. */
    dataid.datano = gwy_app_data_browser_get_number(data);
    dataid.id = id;
    gwy_params_set_image_id(args.params, PARAM_IMAGE_0, dataid);
    gwy_params_set_image_id(args.params, PARAM_IMAGE_A, dataid);
    gwy_params_set_image_id(args.params, PARAM_IMAGE_B, dataid);

    outcome = run_gui(&args, data, id);
    gwy_params_save_to_settings(args.params);
    if (outcome == GWY_DIALOG_CANCEL)
        goto end;
    if (outcome != GWY_DIALOG_HAVE_RESULT)
        execute(&args);

    dataid = gwy_params_get_data_id(args.params, PARAM_TARGET_GRAPH);
    gwy_app_add_graph_or_curves(args.gmodel, data, &dataid, 1);

end:
    g_object_unref(args.gmodel);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, GwyContainer *data, gint id)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkWidget *graph, *dataview, *hbox;
    ModuleGUI gui;
    GwyDialogOutcome outcome;

    gwy_clear(&gui, 1);
    gui.args = args;

    gui.data = gwy_container_new();
    gwy_container_set_object_by_name(gui.data, "/0/data", args->field);
    gwy_app_sync_data_items(data, gui.data, id, 0, FALSE,
                            GWY_DATA_ITEM_PALETTE,
                            0);

    gui.dialog = gwy_dialog_new(_("SMM Calibration"));
    dialog = GWY_DIALOG(gui.dialog);

    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Plot Inits"), RESPONSE_PLOTINITS);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Estimate"), RESPONSE_ESTIMATE);
    gtk_dialog_add_button(GTK_DIALOG(dialog), gwy_sgettext("verb|_Fit"), RESPONSE_FIT);
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Reset"), GWY_RESPONSE_RESET);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    dataview = gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 0);

    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SMALL_SIZE);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_image_id(table, PARAM_IMAGE_A);
    gwy_param_table_append_radio_row(table, PARAM_SIGNAL_A);
    gwy_param_table_append_image_id(table, PARAM_IMAGE_B);
    gwy_param_table_data_id_set_filter(table, PARAM_IMAGE_B, image_filter, gui.args, NULL);
    gwy_param_table_append_radio_row(table, PARAM_SIGNAL_B);

    gwy_param_table_append_slider(table, PARAM_FREQ);
    gwy_param_table_set_unitstr(table, PARAM_FREQ, "GHz");
    gwy_param_table_append_slider(table, PARAM_REFIMP);
    gwy_param_table_set_unitstr(table, PARAM_REFIMP, "Ω");

    gwy_param_table_append_radio_row(table, PARAM_SHOW);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    gtk_box_pack_start(GTK_BOX(hbox), create_image_table(&gui), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), create_fit_table(&gui), FALSE, FALSE, 0);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.data);

    return outcome;
}

static GtkWidget*
create_image_table(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    GtkTable *table;
    GwyDataChooser *chooser;
    GtkWidget *label, *check, *button;
    GwyAppDataId dataid;
    GSList *group = NULL;
    gchar *s;
    guint i;

    table = GTK_TABLE(gtk_table_new(1+NARGS, 6, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);

    gtk_table_attach(table, gwy_label_new_header(_("Masks")), 0, 3, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Capacitance [fF]")), 3, 4, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Use")), 4, 5, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Show")), 5, 6, 0, 1, GTK_FILL, 0, 0, 0);


    for (i = 0; i < NARGS; i++) {
        s = g_strdup_printf("%u", i+1);
        label = gtk_label_new(s);
        g_free(s);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 0, 1, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->enabled[i] = check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), gwy_params_get_boolean(params, PARAM_ENABLED_0 + i));
        g_object_set_data(G_OBJECT(check), "id", GUINT_TO_POINTER(i));
        gtk_table_attach(table, check, 1, 2, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->mask[i] = gwy_data_chooser_new_channels();
        g_object_set_data(G_OBJECT(gui->mask[i]), "id", GUINT_TO_POINTER(i));
        gtk_table_attach(table, gui->mask[i], 2, 3, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->capacitance_adj[i] = gtk_adjustment_new(gwy_params_get_double(params, PARAM_CAPACITANCE_0 + i)/1e-15,
                                                     0.001, 1000, 0.001, 0.01, 0);
        gui->capacitance_spin[i] = gtk_spin_button_new(GTK_ADJUSTMENT(gui->capacitance_adj[i]), 1, 2);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(gui->capacitance_spin[i]), 3);
        g_object_set_data(G_OBJECT(gui->capacitance_adj[i]), "id", GUINT_TO_POINTER(i));
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(gui->capacitance_spin[i]), TRUE);
        gtk_table_attach(GTK_TABLE(table), gui->capacitance_spin[i],
                         3, 4, i+1, i+2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

        gui->fit[i] = check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), gwy_params_get_boolean(params, PARAM_FIT_0 + i));
        g_object_set_data(G_OBJECT(check), "id", GUINT_TO_POINTER(i));
        gtk_table_attach(table, check, 4, 5, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->display[i] = button = gtk_radio_button_new(group);
        g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(i));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), !i);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gtk_table_attach(table, button, 5, 6, i+1, i+2, GTK_FILL, 0, 0, 0);
    }

    dataid = gwy_params_get_data_id(params, PARAM_IMAGE_0);
    gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(gui->mask[0]), &dataid);

    for (i = 0; i < NARGS; i++) {
        chooser = GWY_DATA_CHOOSER(gui->mask[i]);
        gwy_data_chooser_set_filter(chooser, image_filter, gui->args, NULL);
        dataid = gwy_params_get_data_id(params, PARAM_IMAGE_0 + i);
        gwy_data_chooser_set_active_id(chooser, &dataid);
        gwy_data_chooser_get_active_id(chooser, &dataid);
        gwy_params_set_image_id(params, PARAM_IMAGE_0 + i, dataid);
    }

    for (i = 0; i < NARGS; i++) {
        g_signal_connect_swapped(gui->enabled[i], "toggled", G_CALLBACK(enabled_changed), gui);
        g_signal_connect_swapped(gui->fit[i], "toggled", G_CALLBACK(fit_changed), gui);
        g_signal_connect_swapped(gui->mask[i], "changed", G_CALLBACK(image_selected), gui);
        g_signal_connect_swapped(gui->display[i], "toggled", G_CALLBACK(display_changed), gui);
        g_signal_connect_swapped(gui->capacitance_adj[i], "value-changed",
                                 G_CALLBACK(capacitance_changed), gui);
    }

    return GTK_WIDGET(table);
}

static GtkWidget*
create_fit_table(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    GtkTable *table;
    GtkWidget *label, *check, *button, *hbox;
    guint i;

    table = GTK_TABLE(gtk_table_new(1+NFITS, 6, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);

    gtk_table_attach(table, gwy_label_new_header(_("Parameter")), 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Fit")), 1, 2, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Result")), 2, 3, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gwy_label_new_header(_("Initial")), 4, 5, 0, 1, GTK_FILL, 0, 0, 0);


    for (i = 0; i < NFITS; i++) {
        label = gtk_label_new(gwy_param_def_get_param_label(gwy_params_get_def(params),
                                                            PARAM_IFIT_0 + i));
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 0, 1, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->dofit[i] = check = gtk_check_button_new();
        g_object_set_data(G_OBJECT(check), "id", GUINT_TO_POINTER(i));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), gwy_params_get_boolean(params, PARAM_DOFIT_0 + i));
        gtk_table_attach(table, check, 1, 2, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->result[i] = label = gtk_label_new(NULL);
        gtk_table_attach(table, label, 2, 3, i+1, i+2, GTK_FILL, 0, 0, 0);

        button = gtk_button_new_with_label("→");
        gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
        gtk_table_attach(table, button, 3, 4, i+1, i+2, 0, 0, 0, 0);
        g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(i));
        g_signal_connect(button, "clicked", G_CALLBACK(copy_inits), gui);

        gui->ifit_adj[i] = gtk_adjustment_new(gwy_params_get_double(params, PARAM_IFIT_0 + i),
                                              -100, 100, 0.001, 0.01, 0);
        g_object_set_data(G_OBJECT(gui->ifit_adj[i]), "id", GUINT_TO_POINTER(i));
        gui->ifit_spin[i] = gtk_spin_button_new(GTK_ADJUSTMENT(gui->ifit_adj[i]), 1, 2);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(gui->ifit_spin[i]), 3);
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(gui->ifit_spin[i]), TRUE);
        gtk_table_attach(GTK_TABLE(table), gui->ifit_spin[i],
                         4, 5, i+1, i+2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
    }

    for (i = 0; i < NFITS; i++) {
        g_signal_connect_swapped(gui->dofit[i], "toggled", G_CALLBACK(dofit_changed), gui);
        g_signal_connect_swapped(gui->ifit_adj[i], "value-changed",
                                 G_CALLBACK(ifit_changed), gui);
    }

    gui->rss = gtk_label_new(_("Data not fitted."));
    gtk_misc_set_alignment(GTK_MISC(gui->rss), 0.0, 0.5);
    gtk_table_attach(table, gui->rss, 0, 5, i+1, i+2, GTK_FILL, 0, 0, 0);
    i++;

    hbox = gwy_hbox_new(0);
    gtk_table_attach(GTK_TABLE(table), hbox,
                     4, 5, i+1, i+2, GTK_EXPAND | GTK_FILL, 0, 0, 0);


    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Save table to a file"));
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(GTK_STOCK_SAVE, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(save_coeffs), gui);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(button, _("Copy table to clipboard"));
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(GTK_STOCK_COPY, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
    g_signal_connect_swapped(button, "clicked", G_CALLBACK(copy_coeffs), gui);


    return GTK_WIDGET(table);
}


static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    guint i, j, nfits;
    gboolean sens;
    gint signala, signalb;
    gboolean enabled;

    if (gui->in_update)
        return;

    gui->in_update = TRUE;
    if (id == PARAM_IMAGE_0)
        id = -1;

    i = gwy_params_get_int(params, PARAM_DISPLAY);
    if (id < 0) {
        for (j = 1; j < NARGS; j++) {
            enabled = gwy_params_get_boolean(params, PARAM_ENABLED_0 + j);
            gtk_widget_set_sensitive(gui->mask[j], enabled);
            gtk_widget_set_sensitive(gui->display[j], enabled);
            gtk_widget_set_sensitive(gui->fit[j], enabled);
            gtk_widget_set_sensitive(gui->capacitance_spin[j], enabled);
            gwy_data_chooser_refilter(GWY_DATA_CHOOSER(gui->mask[j]));
        }
    }
    if (id < 0 || (id >= PARAM_FIT_0 && id < PARAM_FIT_0 + NARGS) || id == PARAM_SIGNAL_A || PARAM_SIGNAL_B) {
        nfits = 0;
        for (j = 0; j < NARGS; j++)
            nfits += gwy_params_get_boolean(params, PARAM_FIT_0 + j);

        signala = gwy_params_get_enum(params, PARAM_SIGNAL_A);
        signalb = gwy_params_get_enum(params, PARAM_SIGNAL_B);

        sens = FALSE;
        if (signala == SMM_SIGNAL_REAL && signalb == SMM_SIGNAL_IMAGINARY)
            sens = TRUE;

        if (signalb == SMM_SIGNAL_REAL && signala == SMM_SIGNAL_IMAGINARY)
            sens = TRUE;

        if (signalb == SMM_SIGNAL_PHASE
            && (signala == SMM_SIGNAL_MAG || signala == SMM_SIGNAL_LOGMAG))
            sens = TRUE;

        if (signala == SMM_SIGNAL_PHASE
            && (signalb == SMM_SIGNAL_MAG || signalb == SMM_SIGNAL_LOGMAG))
            sens = TRUE;

         gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_ESTIMATE, nfits > 2 && sens);
         gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_FIT, nfits > 3 && sens);
         gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), RESPONSE_PLOTINITS, sens);
         gtk_dialog_set_response_sensitive(GTK_DIALOG(gui->dialog), GTK_RESPONSE_OK, sens);
     }

    if (id < 0 || (id >= PARAM_IFIT_0 && id < PARAM_IFIT_0 + NFITS)) {
        args->init_e00 = gwy_params_get_double(params, PARAM_IFIT_0)
                       + I*gwy_params_get_double(params, PARAM_IFIT_0 + 1);
        args->init_e01 = gwy_params_get_double(params, PARAM_IFIT_0 + 2)
                       + I*gwy_params_get_double(params, PARAM_IFIT_0 + 3);
        args->init_e11 = gwy_params_get_double(params, PARAM_IFIT_0 + 4)
                       + I*gwy_params_get_double(params, PARAM_IFIT_0 + 5);
    }

    if (id < 0 || (id >= PARAM_IMAGE_0 && id < PARAM_IMAGE_0 + NARGS) || id == PARAM_DISPLAY) {
        GwyDataField *field = gwy_params_get_image(params, PARAM_IMAGE_0 + i);
        gwy_container_set_object_by_name(gui->data, "/0/data", field);
        gwy_data_field_data_changed(field);
        gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view), PREVIEW_SMALL_SIZE);
    }

    if (id >= PARAM_ENABLED_0 && id < PARAM_ENABLED_0 + NARGS) {
        j = id - PARAM_ENABLED_0;
        enabled = gwy_params_get_boolean(params, id);
        gtk_widget_set_sensitive(gui->mask[j], enabled);
        gtk_widget_set_sensitive(gui->display[j], enabled);
        gtk_widget_set_sensitive(gui->fit[j], enabled);
        gtk_widget_set_sensitive(gui->capacitance_spin[j], enabled);
    }

    gui->in_update = FALSE;

    if (id != PARAM_TARGET_GRAPH && id != PARAM_DISPLAY)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    guint i;

    if (response == GWY_RESPONSE_RESET) {
        GwyParams *params = gui->args->params;

        gui->args->fitted = 0;
        gwy_params_reset(params, PARAM_DISPLAY);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->display[gwy_params_get_int(params, PARAM_DISPLAY)]), TRUE);
        for (i = 0; i < NARGS; i++) {
            gwy_params_reset(params, PARAM_ENABLED_0 + i);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->enabled[i]),
                                         gwy_params_get_boolean(params, PARAM_ENABLED_0 + i));
            gwy_param_table_param_changed(gui->table, PARAM_ENABLED_0 + i);
        }
        for (i = 0; i < NFITS; i++)
            gtk_label_set_text(GTK_LABEL(gui->result[i]), "");
    } else if (response == RESPONSE_ESTIMATE)
        estimate(gui);
    else if (response == RESPONSE_FIT)
        fit(gui);
    else if (response == RESPONSE_PLOTINITS) {
        gui->args->fitted = 0;
        gtk_label_set_text(GTK_LABEL(gui->rss), _("Data not fitted."));

        for (i = 0; i < NFITS; i++)
            gtk_label_set_text(GTK_LABEL(gui->result[i]), "");
    }
}

static void
enabled_changed(ModuleGUI *gui, GtkToggleButton *check)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(check), "id"));
    gboolean changed;

    changed = gwy_params_set_boolean(gui->args->params, PARAM_ENABLED_0 + i, gtk_toggle_button_get_active(check));
    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_ENABLED_0 + i);
}

static void
fit_changed(ModuleGUI *gui, GtkToggleButton *check)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(check), "id"));
    gboolean changed;

    changed = gwy_params_set_boolean(gui->args->params, PARAM_FIT_0 + i, gtk_toggle_button_get_active(check));
    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_FIT_0 + i);
}

static void
image_selected(ModuleGUI *gui, GwyDataChooser *chooser)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(chooser), "id"));
    gboolean changed;
    GwyAppDataId dataid;

    gwy_data_chooser_get_active_id(chooser, &dataid);
    changed = gwy_params_set_image_id(gui->args->params, PARAM_IMAGE_0 + i, dataid);
    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_IMAGE_0 + i);
}

static void
display_changed(ModuleGUI *gui, GtkToggleButton *toggle)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(toggle), "id"));
    gboolean changed;

    if (!gtk_toggle_button_get_active(toggle))
        return;
    changed = gwy_params_set_int(gui->args->params, PARAM_DISPLAY, i);
    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_DISPLAY);
}

static void
capacitance_changed(ModuleGUI *gui, GtkAdjustment *adj)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(adj), "id"));
    gboolean changed;
    gdouble val;

    val = gtk_adjustment_get_value(adj)*1e-15;
    changed = gwy_params_set_double(gui->args->params, PARAM_CAPACITANCE_0 + i, val);

    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_CAPACITANCE_0 + i);
}

static void
ifit_changed(ModuleGUI *gui, GtkAdjustment *adj)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(adj), "id"));
    gboolean changed;
    gdouble val;

    val = gtk_adjustment_get_value(adj);
    changed = gwy_params_set_double(gui->args->params, PARAM_IFIT_0 + i, val);

    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_IFIT_0 + i);
}

static void
dofit_changed(ModuleGUI *gui, GtkToggleButton *check)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(check), "id"));
    gboolean changed;

    changed = gwy_params_set_boolean(gui->args->params, PARAM_DOFIT_0 + i, gtk_toggle_button_get_active(check));
    if (changed && !gui->in_update)
        gwy_param_table_param_changed(gui->table, PARAM_DOFIT_0 + i);
}

static void
copy_inits(GObject *button, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    gint i;

    i = GPOINTER_TO_INT(g_object_get_data(button, "id"));
    if (args->fitted) {
        if (i == 0) {
            args->init_e00 = creal(args->e00) + I*cimag(args->init_e00);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[0]), creal(args->init_e00));
        } else if (i == 1) {
            args->init_e00 = creal(args->init_e00) + I*cimag(args->e00);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[1]), cimag(args->init_e00));
        } else if (i == 2) {
            args->init_e01 = creal(args->e01) + I*cimag(args->init_e01);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[2]), creal(args->init_e01));
        } else if (i == 3) {
            args->init_e01 = creal(args->init_e01) + I*cimag(args->e01);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[3]), cimag(args->init_e01));
        } else if (i == 4) {
            args->init_e11 = creal(args->e11) + I*cimag(args->init_e11);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[4]), creal(args->init_e11));
        } else if (i == 5) {
            args->init_e11 = creal(args->init_e11) + I*cimag(args->e11);
            gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[5]), cimag(args->init_e11));
        }
    }
}

static gboolean
image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    ModuleArgs *args = (ModuleArgs*)user_data;
    GwyDataField *otherfield, *field = args->field;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield, GWY_DATA_COMPATIBILITY_RES);
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;

    execute(gui->args);
    gwy_dialog_have_result(GWY_DIALOG(gui->dialog));
}

static void
execute(ModuleArgs *args)
{
    GwyParams *params = args->params;
    GwyDataField *mask, *dfa, *dfb;
    gdouble xdata[NARGS], freq, refimp, aval, bval, res11m, ims11m;
    double complex s11m, s11n, s11c, zs, zr;
    gint i, ndata, show, signala, signalb;
    gboolean enabled, fit;
    gdouble capacitance, mult;

    dfa = gwy_params_get_image(params, PARAM_IMAGE_A);
    dfb = gwy_params_get_image(params, PARAM_IMAGE_B);
    signala = gwy_params_get_enum(params, PARAM_SIGNAL_A);
    signalb = gwy_params_get_enum(params, PARAM_SIGNAL_B);
    show = gwy_params_get_enum(params, PARAM_SHOW);
    freq = gwy_params_get_double(params, PARAM_FREQ)*1e9;
    refimp = gwy_params_get_double(params, PARAM_REFIMP);
  
    //the situations below should be prevented elsewhere 
    if (gwy_data_field_check_compatibility(dfa, dfb, 
                                           GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_REAL)) {
        return;
    }

    for (i = 0; i < NARGS; i++) {
        if (gwy_params_get_boolean(params, PARAM_ENABLED_0 + i)) {
           mask = gwy_params_get_image(params, PARAM_IMAGE_0 + i);
           if (gwy_data_field_check_compatibility(dfa, mask,
                                                  GWY_DATA_COMPATIBILITY_RES | GWY_DATA_COMPATIBILITY_REAL)) {
               return;
           }
        }
    }

 
    zr = refimp;

    ndata = 0;
    for (i = 0; i < NARGS; i++) {
        enabled = gwy_params_get_boolean(params, PARAM_ENABLED_0 + i);
        fit = gwy_params_get_boolean(params, PARAM_FIT_0 + i);
        capacitance = gwy_params_get_double(params, PARAM_CAPACITANCE_0 + i);

        if (enabled) {
            xdata[ndata] = capacitance;
            zs = 1.0/(I*2*G_PI*freq*capacitance);
            s11n = (zs-zr)/(zs+zr);
            args->rnominal[ndata] = creal(s11n);
            args->inominal[ndata] = cimag(s11n);

            mask = gwy_params_get_image(params, PARAM_IMAGE_0 + i);

            aval =  gwy_data_field_area_get_avg(dfa, mask, 0, 0,
                                                gwy_data_field_get_xres(dfa),
                                                gwy_data_field_get_yres(dfa));

            bval =  gwy_data_field_area_get_avg(dfb, mask, 0, 0,
                                                gwy_data_field_get_xres(dfb),
                                                gwy_data_field_get_yres(dfb));

            if (signala == SMM_SIGNAL_REAL && signalb == SMM_SIGNAL_IMAGINARY) {
                res11m = aval;
                ims11m = bval;
            } else if (signalb == SMM_SIGNAL_REAL && signala == SMM_SIGNAL_IMAGINARY) {
                res11m = bval;
                ims11m = aval;
            } else if (signalb == SMM_SIGNAL_PHASE
                       && (signala == SMM_SIGNAL_MAG || signala == SMM_SIGNAL_LOGMAG)) {
                if (signala == SMM_SIGNAL_LOGMAG)
                    aval = pow(10, aval/20);

                if (gwy_si_unit_equal_string(gwy_data_field_get_si_unit_z(dfb), "rad"))
                    mult = 1;
                else
                    mult = G_PI/180;

                res11m = aval*cos(bval*mult);
                ims11m = aval*sin(bval*mult);
            } else if (signala == SMM_SIGNAL_PHASE
                       && (signalb == SMM_SIGNAL_MAG || signalb == SMM_SIGNAL_LOGMAG)) {
                if (signalb == SMM_SIGNAL_LOGMAG)
                    bval = pow(10, bval/20);

                if (gwy_si_unit_equal_string(gwy_data_field_get_si_unit_z(dfa), "rad"))
                    mult = 1;
                else
                    mult = G_PI/180;

                res11m = bval*cos(aval*mult);
                ims11m = bval*sin(aval*mult);
            } else { //should not be reached
                res11m = 0;
                ims11m = 0;
            }

            if (args->fitted) {
               s11m = res11m + I*ims11m;
               s11c = (s11m - args->e00)/(args->e01 + args->e11*(s11m - args->e00));
            } else {
               s11m = res11m + I*ims11m;
               s11c = (s11m - args->init_e00)/(args->init_e01 + args->init_e11*(s11m - args->init_e00));
            }

            args->rmeas[ndata] = creal(s11m);
            args->imeas[ndata] = cimag(s11m);

            args->rcalc[ndata] = creal(s11c);
            args->icalc[ndata] = cimag(s11c);

            args->capacitance[ndata] = capacitance;
            args->capcalc[ndata] = creal(1.0/(I*2*G_PI*freq*(zr*(1+s11c)/(1-s11c))));

            /*if (i==0) printf("crosscheck: a %g b %g s11m %g %g s11c %g %g ztip %g %g  cal %g\n",
                             aval, bval, creal(s11m), cimag(s11m), creal(s11c), cimag(s11c),
                             creal(zr*(1+s11c)/(1-s11c)), cimag(zr*(1+s11c)/(1-s11c)),
                             creal(1.0/(I*2*G_PI*freq*(zr*(1+s11c)/(1-s11c)))));*/

            args->fitme[ndata] = fit;

            /*printf("param %d: fit %d  capacitance %g  nom %g %g   meas %g %g  calc %g %g\n", i, fit, capacitance,
                   args->rnominal[ndata], args->inominal[ndata], args->rmeas[ndata], args->imeas[ndata],
                   args->rcalc[ndata], args->icalc[ndata]);
                   */

            ndata++;
        }
    }
    args->ndata = ndata;

    if (show == SMM_SHOW_S11_REAL) {
        gwy_graph_curve_model_set_data(args->gcmodel_nominal, xdata, args->rnominal, ndata);
        gwy_graph_curve_model_set_data(args->gcmodel_measured, xdata, args->rcalc, ndata);
        g_object_set(args->gmodel,
                     "title", _("Re(S<sub>11</sub>)"),
                     "axis-label-left", _("Re(S<sub>11</sub>)"),
                     NULL);
    }
    else if (show == SMM_SHOW_S11_IMAGINARY) {
        gwy_graph_curve_model_set_data(args->gcmodel_nominal, xdata, args->inominal, ndata);
        gwy_graph_curve_model_set_data(args->gcmodel_measured, xdata, args->icalc, ndata);
        g_object_set(args->gmodel,
                     "title", _("Im(S<sub>11</sub>)"),
                     "axis-label-left", _("Im(S<sub>11</sub>)"),
                     NULL);
    } else {
        gwy_graph_curve_model_set_data(args->gcmodel_nominal, xdata, args->capacitance, ndata);
        gwy_graph_curve_model_set_data(args->gcmodel_measured, xdata, args->capcalc, ndata);
        g_object_set(args->gmodel,
                     "title", _("Capacitance"),
                     "axis-label-left", _("Capacitance"),
                     NULL);
    }
}


static void
estimate(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    double complex e00, e01, e11, s11m[3], s11[3];
    gint i, imax, imin, iother;
    gdouble capacitance, mincap, maxcap;

    mincap = G_MAXDOUBLE;
    maxcap = 0;

    imax = imin = 0;
    for (i = 0; i < args->ndata; i++) {
        if (args->fitme[i]) {
           capacitance = args->capacitance[i];
           if (mincap > capacitance) {
               mincap = capacitance;
               imin = i;
           }
           if (maxcap < capacitance) {
               maxcap = capacitance;
               imax = i;
           }
        }
    }

    iother = -1;
    for (i = 0; i < args->ndata; i++) {
        if (args->fitme[i]) {
           if ((i != imin) && (i != imax))
               iother = i;
        }
    }

    //printf("is %d %d %d\n", imin, iother, imax);

    s11m[0] = args->rmeas[imin] + I*args->imeas[imin];
    s11m[1] = args->rmeas[iother] + I*args->imeas[iother];
    s11m[2] = args->rmeas[imax] + I*args->imeas[imax];

    s11[0] = args->rnominal[imin] + I*args->inominal[imin];
    s11[1] = args->rnominal[iother] + I*args->inominal[iother];
    s11[2] = args->rnominal[imax] + I*args->inominal[imax];

    smmeval(s11m, s11, &e00, &e01, &e11);

    args->init_e00 = e00;
    args->init_e01 = e01;
    args->init_e11 = e11;

    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[0]), creal(e00));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[1]), cimag(e00));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[2]), creal(e01));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[3]), cimag(e01));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[4]), creal(e11));
    gtk_adjustment_set_value(GTK_ADJUSTMENT(gui->ifit_adj[5]), cimag(e11));

    gwy_param_table_param_changed(gui->table, PARAM_IFIT_0);
}

static void
fit(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    ModuleArgs *args = gui->args;
    double complex e00, e01, e11, s11m[NARGS], s11[NARGS];
    gboolean fix[NFITS];
    gchar result[50];
    gint i, n;

    n = 0;
    for (i = 0; i < args->ndata; i++) {
        if (args->fitme[i]) {
            s11m[n] = args->rmeas[i] + I*args->imeas[i];
            s11[n] = args->rnominal[i] + I*args->inominal[i];
            n++;
         }
    }
    for (i = 0; i < NFITS; i++) {
        fix[i] = !(gwy_params_get_boolean(params, PARAM_DOFIT_0 + i));
    }

    e00 = args->init_e00;
    e01 = args->init_e01;
    e11 = args->init_e11;

    args->rss = smmfit(s11m, s11, n, &e00, &e01, &e11, fix);

    args->e00 = e00;
    args->e01 = e01;
    args->e11 = e11;

    args->fitted = TRUE;

    gwy_param_table_param_changed(gui->table, PARAM_IFIT_0);

    snprintf(result, sizeof(result), "%g", creal(e00));
    gtk_label_set_text(GTK_LABEL(gui->result[0]), result);
    snprintf(result, sizeof(result), "%g", cimag(e00));
    gtk_label_set_text(GTK_LABEL(gui->result[1]), result);

    snprintf(result, sizeof(result), "%g", creal(e01));
    gtk_label_set_text(GTK_LABEL(gui->result[2]), result);
    snprintf(result, sizeof(result), "%g", cimag(e01));
    gtk_label_set_text(GTK_LABEL(gui->result[3]), result);

    snprintf(result, sizeof(result), "%g", creal(e11));
    gtk_label_set_text(GTK_LABEL(gui->result[4]), result);
    snprintf(result, sizeof(result), "%g", cimag(e11));
    gtk_label_set_text(GTK_LABEL(gui->result[5]), result);

    if (args->rss >= 0)
        snprintf(result, sizeof(result), _("Mean square difference: %g"), args->rss);
    else
        snprintf(result, sizeof(result), _("Data not fitted."));

    gtk_label_set_text(GTK_LABEL(gui->rss), result);
}


typedef struct {
    double complex *s11;
    double complex *s11m;
} FitData;


static gdouble
smmfitfunc(guint i, const gdouble *param, gpointer user_data, gboolean *success)
{
    FitData *fd = (FitData*)user_data;
    double complex e00 = param[0] + I*param[1];
    double complex e01 = param[2] + I*param[3];
    double complex e11 = param[4] + I*param[5];
    double complex s11c, dist;

    s11c = (fd->s11m[i/2] - e00)/(e01 + e11*(fd->s11m[i/2] - e00));
    dist = s11c - fd->s11[i/2];

    *success = TRUE;

    if (i%2 == 0)
        return creal(dist);
    else
        return cimag(dist);
}



static gdouble
smmfit(double complex *s11m, double complex *s11, gint n,
       double complex *re00, double complex *re01, double complex *re11,
       gboolean *fix)
{
    FitData fdata;
    gdouble rss, param[6];
    GwyNLFitter *fitter = fitter = gwy_math_nlfit_new_idx(smmfitfunc, NULL);

    gwy_math_nlfit_set_max_iterations(fitter, 1000*gwy_math_nlfit_get_max_iterations(fitter));

    fdata.s11 = s11;
    fdata.s11m = s11m;

    param[0] = creal(*re00);
    param[1] = cimag(*re00);
    param[2] = creal(*re01);
    param[3] = cimag(*re01);
    param[4] = creal(*re11);
    param[5] = cimag(*re11);

    /*
    printf("fitter inputs:\n");
    for (i = 0; i<6; i++) {
        printf("param %d  initial  %g  fix %d\n", i, param[i], fix[i]);
    }
    for (i = 0; i<n; i++) {
        printf("data %d  s11  %g %g   s11m  %g, %g\n", i, creal(fdata.s11[i]), cimag(fdata.s11[i]),
               creal(fdata.s11m[i]), cimag(fdata.s11m[i]));
    }*/

    rss = gwy_math_nlfit_fit_idx_full(fitter, n, 6, param, fix, NULL, &fdata);

    *re00 = param[0] + I*param[1];
    *re01 = param[2] + I*param[3];
    *re11 = param[4] + I*param[5];

    /*printf("fit result: rss %g   e00 %g %g    e01 %g %g    e11 %g %g\n", rss, creal(*re00), cimag(*re00),
           creal(*re01), cimag(*re01), creal(*re11), cimag(*re11));*/

    return rss;
}

static gchar*
create_report(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GString *text = g_string_new(NULL);

    g_string_append_printf(text, "e00 = %g + %g * I\n", creal(args->e00), cimag(args->e00));
    g_string_append_printf(text, "e01 = %g + %g * I\n", creal(args->e01), cimag(args->e01));
    g_string_append_printf(text, "e11 = %g + %g * I\n", creal(args->e11), cimag(args->e11));

    if (args->fitted) {
        if (args->rss >= 0)
            g_string_append_printf(text, _("Mean square difference: %g"), args->rss);
        else
            g_string_append_printf(text, _("Fit failed."));
        g_string_append_c(text, '\n');
    }

    return g_string_free(text, FALSE);
}


static void
save_coeffs(ModuleGUI *gui)
{
    gchar *text;

    text = create_report(gui);
    gwy_save_auxiliary_data(_("Save Table"), GTK_WINDOW(gui->dialog), -1, text);
    g_free(text);
}

static void
copy_coeffs(ModuleGUI *gui)
{
    GtkClipboard *clipboard;
    GdkDisplay *display;
    gchar *text;

    text = create_report(gui);
    display = gtk_widget_get_display(gui->dialog);
    clipboard = gtk_clipboard_get_for_display(display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    g_free(text);
}


static void
smmeval(double complex s11m[3], double complex s11[3],
        double complex *re00, double complex *re01, double complex *re11)
{
    double complex e00, e01, e11;
    double complex hlp = s11[0]*s11[1]*s11m[0] - s11[0]*s11[1]*s11m[1]
                       - s11[0]*s11[2]*s11m[0] + s11[0]*s11[2]*s11m[2]
                       + s11[1]*s11[2]*s11m[1] - s11[1]*s11[2]*s11m[2];

    e00 = (s11[0]*s11[1]*s11m[0]*s11m[2] - s11[0]*s11[1]*s11m[1]*s11m[2]
           - s11[0]*s11[2]*s11m[0]*s11m[1] + s11[0]*s11[2]*s11m[1]*s11m[2]
           + s11[1]*s11[2]*s11m[0]*s11m[1] - s11[1]*s11[2]*s11m[0]*s11m[2])/hlp;
    e01 = (s11[0] - s11[1])*(s11[0] - s11[2])*(s11[1] - s11[2])
        *(s11m[0] - s11m[1])*(s11m[0] - s11m[2])*(s11m[1] - s11m[2])/hlp/hlp;
    e11 = (-s11[0]*s11m[1] + s11[0]*s11m[2] + s11[1]*s11m[0] - s11[1]*s11m[2]
           - s11[2]*s11m[0] + s11[2]*s11m[1])/hlp;

    *re00 = e00;
    *re01 = e01;
    *re11 = e11;
}


/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
