/*
 *  $Id: classify.c 24707 2022-03-21 17:09:03Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti).
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
#include <libgwyddion/gwyexpr.h>
#include <libprocess/arithmetic.h>
#include <libprocess/stats.h>
#include <libprocess/filters.h>
#include <libprocess/elliptic.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwydgets.h>
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include <app/gwymoduleutils-file.h>
#include "preview.h"

#define CLASSIFY_RUN_MODES GWY_RUN_INTERACTIVE

#define CDEBUG 0

enum {
    NARGS = 4,
    NCRITS = 5,
};


typedef enum {
    CLASSIFY_QUANTITY_VALUE = 0,
    CLASSIFY_QUANTITY_LOG   = 1,
    CLASSIFY_QUANTITY_SLOPE = 2,
    CLASSIFY_QUANTITY_NTYPES
} ClassifyQuantityType;

typedef enum {
    CLASSIFY_DISPLAY_MASK_A   = 0,
    CLASSIFY_DISPLAY_MASK_B   = 1,
    CLASSIFY_DISPLAY_RESULT_A = 2,
    CLASSIFY_DISPLAY_RESULT_B = 3,
    CLASSIFY_DISPLAY_NTYPES
} ClassifyDisplayType;

typedef enum {
    CLASSIFY_SCALE_1X  = 0,
    CLASSIFY_SCALE_2X  = 1,
    CLASSIFY_SCALE_4X  = 2,
    CLASSIFY_SCALE_8X  = 3,
    CLASSIFY_SCALE_16X = 4,
    CLASSIFY_SCALE_32X = 5,
    CLASSIFY_SCALE_NTYPES
} ClassifyScaleType;


typedef struct {
    guint err;
    GwyAppDataId objects[NARGS];
    GwyAppDataId show;
    gint maska;
    gint maskb;
    gint id[2*NCRITS];
    ClassifyQuantityType quantity[2*NCRITS];
    ClassifyScaleType scale[2*NCRITS];
    ClassifyDisplayType display;
} ClassifyArgs;

typedef struct {
    ClassifyArgs *args;
    GtkWidget *show;
    GtkWidget *dialog;
    GtkWidget *display;
    GtkWidget *view;
    GtkWidget *data[NARGS];
    GSList *maska;
    GSList *maskb;
    GwyContainer *mydata;
    GwyDataField *result_a;
    GwyDataField *result_b;
} ClassifyControls;

#define MAXRULES 100
#define MAXBRANCHES 10
#define PURCRIT 1e-2

typedef struct
{
    int nrules;
    int rule_parameter[MAXRULES];   //which parameter (dfield) to use for decision
    double rule_threshold[MAXRULES]; //threshold for decision
    int rule_goto_high[MAXRULES];    //points to either result (-1, -2) or next rule
    int rule_goto_low[MAXRULES];     //points to either result (-1, -2) or next rule
} CTree;

typedef struct
{
    CTree ct[100];
    int verbose;
} Classifier;


static gboolean     module_register        (void);
static void         classify               (GwyContainer *data,
                                            GwyRunType run);
static void         classify_load_args     (GwyContainer *settings,
                                            ClassifyArgs *args);
static void         classify_save_args     (GwyContainer *settings,
                                            ClassifyArgs *args);
static gboolean     classify_dialog        (GwyContainer *data,
                                            gint id,
                                            ClassifyArgs *args);
static void         classify_data_chosen   (GwyDataChooser *chooser,
                                            ClassifyControls *controls);
static void         classify_show_chosen   (GwyDataChooser *chooser,
                                            ClassifyControls *controls);
static void         classify_maska_selected(ClassifyControls *controls);
static void         classify_maskb_selected(ClassifyControls *controls);
//static const gchar* classify_check_fields  (ClassifyArgs *args);
static void         classify_preview       (ClassifyControls *controls);
static GtkWidget*   quantity_selector_new  (ClassifyControls *controls,
                                            gint i);
static GtkWidget*   scale_selector_new     (ClassifyControls *controls,
                                            gint i);
static GtkWidget*   display_selector_new   (ClassifyControls *controls);
static void         id_selected            (ClassifyControls *controls,
                                            GtkAdjustment *adj);
static void         update_criterion_sensitivity(GtkAdjustment *adj);
static void         classify_update_view   (GtkComboBox *combo,
                                            ClassifyControls *controls);
static void         run_classification     (ClassifyControls *controls);
static void         classifier_train_full  (Classifier *cl,
                                            GwyDataField **cldata,
                                            gint ndata,
                                            GwyDataField *mask_a,
                                            GwyDataField *mask_b);
static void         classifier_run         (Classifier *cl,
                                            GwyDataField **cldata,
                                            gint ndata,
                                            GwyDataField *result_a,
                                            GwyDataField *result_b);
static void         ctree_run              (CTree *ct,
                                            GwyDataField **cldata,
                                            gint ndata,
                                            GwyDataField *result_a,
                                            GwyDataField *result_b);

static GwyAppDataId object_ids[NARGS];

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Classify data sets using multiple data fields."),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.1",
    "Petr Klapetek",
    "2020",
};

GWY_MODULE_QUERY2(module_info, classify)

static gboolean
module_register(void)
{
    guint i;

    for (i = 0; i < NARGS; i++) {
        object_ids[i].datano = 0;
        object_ids[i].id = -1;
    }
    gwy_process_func_register("classify",
                              (GwyProcessFunc)&classify,
                              N_("/M_ultidata/_Classify..."),
                              NULL,
                              CLASSIFY_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Classify data sets"));

    return TRUE;
}

void
classify(GwyContainer *data, GwyRunType run)
{
    ClassifyArgs args;
    GwyContainer *settings;
    gint datano, id;

    g_return_if_fail(run & CLASSIFY_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);

    args.objects[0].datano = datano;
    args.objects[0].id = id;

    settings = gwy_app_settings_get();
    classify_load_args(settings, &args);

    classify_dialog(data, id, &args);
    classify_save_args(settings, &args);
}

static gboolean
classify_dialog(GwyContainer *data,
                  gint id,
                  ClassifyArgs *args)
{
    GtkWidget *dialog, *hbox, *vbox, *vbox2, *table, *chooser,
              *label, *button, *combo, *spin;
    GtkObject *adjustment;
    GQuark quark;
    ClassifyControls controls;
    GwyDataField *dfield;
    guint i, row, response;
    gchar *s, name[50];

    controls.args = args;

    dialog = gtk_dialog_new_with_buttons(_("Classify"), NULL, 0, NULL);
    controls.dialog = dialog;
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Execute"),
                                                           GTK_STOCK_EXECUTE),
                                 RESPONSE_PREVIEW);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog),
                          GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gwy_help_add_to_proc_dialog(GTK_DIALOG(dialog), GWY_HELP_DEFAULT);

    hbox = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), hbox,
                       FALSE, FALSE, 4);

    vbox = gtk_vbox_new(FALSE, 4);
    /* Ensure no wild changes of the dialog size due to non-square data. */
    gtk_widget_set_size_request(vbox, PREVIEW_SIZE, PREVIEW_SIZE);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 4);

    controls.result_a = NULL;
    controls.result_b = NULL;

    controls.mydata = gwy_container_new();
    dfield = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE, 1.0, 1.0, TRUE);
    gwy_container_set_object_by_name(controls.mydata, "/0/data", dfield);
    gwy_app_sync_data_items(data, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_MASK_COLOR,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, TRUE);
    ensure_mask_color(controls.mydata, 0);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);

    vbox2 = gtk_vbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 4);

    table = gtk_table_new(6 + NARGS, 5, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 4);
    row = 0;

    label = gtk_label_new(_("Id"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Data"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Mask A"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Mask B"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 3, 4, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    controls.maska = NULL;
    controls.maskb = NULL;
    for (i = 0; i < NARGS; i++) {
        /* VALUE is 0 */

        g_snprintf(name, sizeof(name), "%d:", i+1);
        label = gtk_label_new_with_mnemonic(name);
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                         GTK_FILL, 0, 0, 0);


        chooser = gwy_data_chooser_new_channels();
        gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(chooser),
                                       args->objects + i);
        g_signal_connect(chooser, "changed",
                         G_CALLBACK(classify_data_chosen), &controls);
        g_object_set_data(G_OBJECT(chooser), "index", GUINT_TO_POINTER(i));
        gtk_table_attach(GTK_TABLE(table), chooser, 1, 2, row, row+1,
                         GTK_EXPAND | GTK_FILL, 0, 0, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), chooser);
        controls.data[i] = chooser;

        button = gtk_radio_button_new(controls.maska);
        controls.maska
            = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gwy_radio_button_set_value(button, i);
        s = g_strdup_printf(_("Mask A is in data d%d"), i+1);
        gtk_widget_set_tooltip_text(button, s);
        g_free(s);
        gtk_table_attach(GTK_TABLE(table), button, 2, 3, row, row+1,
                         0, 0, 0, 0);
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(classify_maska_selected),
                                 &controls);

        button = gtk_radio_button_new(controls.maskb);
        controls.maskb
            = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gwy_radio_button_set_value(button, i);
        s = g_strdup_printf(_("Mask B is in data d%d"), i+1);
        gtk_widget_set_tooltip_text(button, s);
        g_free(s);
        gtk_table_attach(GTK_TABLE(table), button, 3, 4, row, row+1,
                         0, 0, 0, 0);
        g_signal_connect_swapped(button, "clicked",
                                 G_CALLBACK(classify_maskb_selected),
                                 &controls);
        row++;
    }
    row++;

    table = gtk_table_new(NCRITS, 7, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_table_set_col_spacing(GTK_TABLE(table), 3, 10);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 4);
    row = 0;

    label = gtk_label_new(_("Id"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Criterion"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 1, 2, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Scale"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Id"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Criterion"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 5, 6, row, row+1,
                     GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Scale"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 6, 7, row, row+1,
                     GTK_FILL, 0, 0, 0);
    row++;

    for (i = 0; i < NCRITS; i++) {
        adjustment = gtk_adjustment_new(args->id[2*i], 0, NARGS, 1, 10, 0);
        g_object_set_data(G_OBJECT(adjustment),
                          "index", GUINT_TO_POINTER(2*i));
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 2);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
        g_signal_connect_swapped(adjustment, "value-changed",
                                 G_CALLBACK(id_selected), &controls);
        gtk_table_attach(GTK_TABLE(table), spin, 0, 1, row, row+1,
                         0, 0, 0, 0);

        combo = quantity_selector_new(&controls, 2*i);
        g_object_set_data(G_OBJECT(adjustment), "quantity", combo);
        gtk_table_attach(GTK_TABLE(table), combo, 1, 2, row, row+1,
                         0, 0, 0, 0);

        combo = scale_selector_new(&controls, 2*i);
        g_object_set_data(G_OBJECT(adjustment), "scale", combo);
        gtk_table_attach(GTK_TABLE(table), combo, 2, 3, row, row+1,
                         0, 0, 0, 0);
        update_criterion_sensitivity(GTK_ADJUSTMENT(adjustment));


        adjustment = gtk_adjustment_new(args->id[2*i + 1], 0, NARGS, 1, 10, 0);
        g_object_set_data(G_OBJECT(adjustment),
                          "index", GUINT_TO_POINTER(2*i + 1));
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 2);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 0);
        g_signal_connect_swapped(adjustment, "value-changed",
                                 G_CALLBACK(id_selected), &controls);
        gtk_table_attach(GTK_TABLE(table), spin, 4, 5, row, row+1,
                         0, 0, 0, 0);

        combo = quantity_selector_new(&controls, 2*i + 1);
        g_object_set_data(G_OBJECT(adjustment), "quantity", combo);
        gtk_table_attach(GTK_TABLE(table), combo, 5, 6, row, row+1,
                         0, 0, 0, 0);

        combo = scale_selector_new(&controls, 2*i + 1);
        g_object_set_data(G_OBJECT(adjustment), "scale", combo);
        gtk_table_attach(GTK_TABLE(table), combo, 6, 7, row, row+1,
                         0, 0, 0, 0);
        update_criterion_sensitivity(GTK_ADJUSTMENT(adjustment));
        row++;
    }

    table = gtk_table_new(5, 3, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(vbox2), table, TRUE, TRUE, 4);
    row = 0;

    label = gtk_label_new(_("Preview:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL, 0, 0, 0);

    controls.show = gwy_data_chooser_new_channels();
    gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(controls.show),
                                   &(args->show));
    g_signal_connect(controls.show, "changed",
                     G_CALLBACK(classify_show_chosen), &controls);
    gtk_table_attach(GTK_TABLE(table), controls.show, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), controls.show);
    row++;

    label = gtk_label_new(_("Display mask:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL, 0, 0, 0);

    controls.display = display_selector_new(&controls);
    gtk_table_attach(GTK_TABLE(table), controls.display, 1, 2, row, row+1,
                     GTK_EXPAND | GTK_FILL, 0, 0, 0);
    row++;

    gtk_widget_show_all(dialog);
    gwy_radio_buttons_set_current(controls.maska, args->maska);
    gwy_radio_buttons_set_current(controls.maskb, args->maskb);
    classify_update_view(GTK_COMBO_BOX(controls.display), &controls);

    do {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        switch (response) {
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            gtk_widget_destroy(dialog);
            case GTK_RESPONSE_NONE:
            return FALSE;
            break;

            case RESPONSE_PREVIEW:
            classify_preview(&controls);
            break;

            case GTK_RESPONSE_OK:
            quark = gwy_app_get_mask_key_for_id(args->show.id);
            gwy_app_undo_qcheckpointv(data, 1, &quark);

            if (controls.result_b && args->display == CLASSIFY_DISPLAY_RESULT_B)
                   gwy_container_set_object(data, gwy_app_get_mask_key_for_id(args->show.id),
                                       controls.result_b);
            else if (controls.result_a)
                   gwy_container_set_object(data, gwy_app_get_mask_key_for_id(args->show.id),
                                       controls.result_a);
            break;

            default:
            g_assert_not_reached();
            break;
        }
    } while (response != GTK_RESPONSE_OK);

    gtk_widget_destroy(dialog);

    return TRUE;
}

static void
id_selected(ClassifyControls *controls, GtkAdjustment *adj)
{
    guint i;

    i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(adj), "index"));
    controls->args->id[i] = gwy_adjustment_get_int(adj);
    update_criterion_sensitivity(adj);
}

static void
update_criterion_sensitivity(GtkAdjustment *adj)
{
    GObject *obj = G_OBJECT(adj);
    GtkWidget *widget;
    gboolean sens;

    sens = gwy_adjustment_get_int(adj);
    widget = GTK_WIDGET(g_object_get_data(obj, "quantity"));
    gtk_widget_set_sensitive(widget, sens);
    widget = GTK_WIDGET(g_object_get_data(obj, "scale"));
    gtk_widget_set_sensitive(widget, sens);
}

static GtkWidget*
quantity_selector_new(ClassifyControls *controls, gint i)
{
    static const GwyEnum quantity_types[] = {
        { N_("Value"), CLASSIFY_QUANTITY_VALUE, },
        { N_("LoG"),   CLASSIFY_QUANTITY_LOG,   },
        { N_("Slope"), CLASSIFY_QUANTITY_SLOPE, },
    };
    GtkWidget *combo;

    combo = gwy_enum_combo_box_new(quantity_types, G_N_ELEMENTS(quantity_types),
                                   G_CALLBACK(gwy_enum_combo_box_update_int),
                                   &(controls->args->quantity[i]), controls->args->quantity[i], TRUE);
    return combo;
}

static GtkWidget*
display_selector_new(ClassifyControls *controls)
{
    static const GwyEnum display_types[] = {
        { N_("Mask A"),   CLASSIFY_DISPLAY_MASK_A   },
        { N_("Mask B"),   CLASSIFY_DISPLAY_MASK_B   },
        { N_("Result A"), CLASSIFY_DISPLAY_RESULT_A },
        { N_("Result B"), CLASSIFY_DISPLAY_RESULT_B },
    };
    GtkWidget *combo;

    combo = gwy_enum_combo_box_new(display_types, G_N_ELEMENTS(display_types),
                                   G_CALLBACK(classify_update_view),
                                   controls, controls->args->display, TRUE);
    return combo;
}

static GtkWidget*
scale_selector_new(ClassifyControls *controls, gint i)
{
    static GwyEnum scale_types[] = {
        { NULL, CLASSIFY_SCALE_1X,  },
        { NULL, CLASSIFY_SCALE_2X,  },
        { NULL, CLASSIFY_SCALE_4X,  },
        { NULL, CLASSIFY_SCALE_8X,  },
        { NULL, CLASSIFY_SCALE_16X, },
        { NULL, CLASSIFY_SCALE_32X, },
    };
    GtkWidget *combo;
    ClassifyScaleType *scale = controls->args->scale + i;

    if (!scale_types[0].name) {
        scale_types[CLASSIFY_SCALE_1X].name = g_strdup_printf("%u %s",
                                                              1, _("px"));
        scale_types[CLASSIFY_SCALE_2X].name = g_strdup_printf("%u %s",
                                                              2, _("px"));
        scale_types[CLASSIFY_SCALE_4X].name = g_strdup_printf("%u %s",
                                                              4, _("px"));
        scale_types[CLASSIFY_SCALE_8X].name = g_strdup_printf("%u %s",
                                                              8, _("px"));
        scale_types[CLASSIFY_SCALE_16X].name = g_strdup_printf("%u %s",
                                                               16, _("px"));
        scale_types[CLASSIFY_SCALE_32X].name = g_strdup_printf("%u %s",
                                                               32, _("px"));
    }

    combo = gwy_enum_combo_box_new(scale_types, G_N_ELEMENTS(scale_types),
                                   G_CALLBACK(gwy_enum_combo_box_update_int),
                                   scale, *scale, TRUE);
    return combo;
}

static void
classify_update_view(G_GNUC_UNUSED GtkComboBox *combo, ClassifyControls *controls)
{
    ClassifyArgs *args;
    GwyContainer *data;
    GwyDataField *result, *mask=NULL;
    GQuark quark;

    args = controls->args;
    args->display = gwy_enum_combo_box_get_active(GTK_COMBO_BOX(controls->display));

    gwy_data_chooser_get_active_id(GWY_DATA_CHOOSER(controls->show), &(args->show));

    data = gwy_app_data_browser_get(args->show.datano);
    quark = gwy_app_get_data_key_for_id(args->show.id);
    result = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    gwy_container_set_object_by_name(controls->mydata, "/0/data", result);
    //g_object_unref(result);

    if (args->display == CLASSIFY_DISPLAY_MASK_A) {
       if (CDEBUG>1) printf("getting mask A for %d\n", args->maska);
       data = gwy_app_data_browser_get(args->objects[args->maska % NARGS].datano);
       quark = gwy_app_get_mask_key_for_id(args->objects[args->maska % NARGS].id);
       //if (!gwy_container_gis_object(data, quark, &mask)) printf("There is no mask A in channel %d\n", args->maska);
    }
    else if (args->display == CLASSIFY_DISPLAY_MASK_B) {
       data = gwy_app_data_browser_get(args->objects[args->maskb % NARGS].datano);
       quark = gwy_app_get_mask_key_for_id(args->objects[args->maskb % NARGS].id);
       //if (!gwy_container_gis_object(data, quark, &mask)) printf("There is no mask B in channel %d\n", args->maska);
    }
    else if (args->display == CLASSIFY_DISPLAY_RESULT_A)
    {
        mask = controls->result_a;
    }
    else if (args->display == CLASSIFY_DISPLAY_RESULT_B)
    {
        mask = controls->result_b;
    }

    if (mask) {
        gwy_container_set_object_by_name(controls->mydata, "/0/mask", mask);
        //g_object_unref(mask);
    }
    else
        gwy_container_remove_by_name(controls->mydata, "/0/mask");

    gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
}


static void
classify_data_chosen(GwyDataChooser *chooser,
                     ClassifyControls *controls)
{
    ClassifyArgs *args;
    guint i;

    args = controls->args;
    i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(chooser), "index"));
    gwy_data_chooser_get_active_id(chooser, args->objects + i);
    classify_update_view(GTK_COMBO_BOX(controls->display), controls);

}

static void
classify_show_chosen(G_GNUC_UNUSED GwyDataChooser *chooser,
                     ClassifyControls *controls)
{
    classify_update_view(GTK_COMBO_BOX(controls->display), controls);
}

static void
classify_maska_selected(ClassifyControls *controls)
{
    ClassifyArgs *args = controls->args;
    args->maska = gwy_radio_buttons_get_current(controls->maska);
    classify_update_view(GTK_COMBO_BOX(controls->display), controls);

}
static void
classify_maskb_selected(ClassifyControls *controls)
{
    ClassifyArgs *args = controls->args;
    args->maskb = gwy_radio_buttons_get_current(controls->maskb);
    classify_update_view(GTK_COMBO_BOX(controls->display), controls);

}

/*
static const gchar*
classify_check_fields(ClassifyArgs *args)
{
    guint first = 0, i;
    GwyContainer *data;
    GQuark quark;
    GwyDataField *dfirst, *dfield;
    GwyDataCompatibilityFlags diff;

    data = gwy_app_data_browser_get(args->objects[first].datano);
    g_return_val_if_fail(data, NULL);
    quark = gwy_app_get_data_key_for_id(args->objects[first].id);
    dfirst = GWY_DATA_FIELD(gwy_container_get_object(data, quark));
    for (i = first+1; i < NARGS; i++) {

        data = gwy_app_data_browser_get(args->objects[i].datano);
        g_return_val_if_fail(data, NULL);
        quark = gwy_app_get_data_key_for_id(args->objects[i].id);
        dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

        diff = gwy_data_field_check_compatibility
                                            (dfirst, dfield,
                                             GWY_DATA_COMPATIBILITY_RES
                                             | GWY_DATA_COMPATIBILITY_REAL
                                             | GWY_DATA_COMPATIBILITY_LATERAL);
        if (diff) {
            if (diff & GWY_DATA_COMPATIBILITY_RES)
                return _("Pixel dimensions differ");
            if (diff & GWY_DATA_COMPATIBILITY_LATERAL)
                return _("Lateral dimensions are different physical "
                         "quantities");
            if (diff & GWY_DATA_COMPATIBILITY_REAL)
                return _("Physical dimensions differ");
        }
    }

    return NULL;
}
*/

static void
classify_preview(ClassifyControls *controls)
{
    run_classification(controls);
    classify_update_view(GTK_COMBO_BOX(controls->display), controls);
}



static gdouble
fit_local_plane_by_pos(gint n,
                       const gint *xp, const gint *yp, const gdouble *z,
                       gdouble *bx, gdouble *by)
{
    gdouble m[12], b[4];
    gint i;

    gwy_clear(m, 6);
    gwy_clear(b, 4);
    for (i = 0; i < n; i++) {
        m[1] += xp[i];
        m[2] += xp[i]*xp[i];
        m[3] += yp[i];
        m[4] += xp[i]*yp[i];
        m[5] += yp[i]*yp[i];
        b[0] += z[i];
        b[1] += xp[i]*z[i];
        b[2] += yp[i]*z[i];
        b[3] += z[i]*z[i];
    }
    m[0] = n;
    gwy_assign(m + 6, m, 6);
    if (gwy_math_choleski_decompose(3, m))
        gwy_math_choleski_solve(3, m, b);
    else
        b[0] = b[1] = b[2] = 0.0;

    *bx = b[1];
    *by = b[2];
    return (b[3] - (b[0]*b[0]*m[6+0] + b[1]*b[1]*m[6+2] + b[2]*b[2]*m[6+5])
            - 2.0*(b[0]*b[1]*m[6+1] + b[0]*b[2]*m[6+3] + b[1]*b[2]*m[6+4]));
}


static void
inclination_filter(GwyDataField *dfield)
{
    GwyDataField *show = gwy_data_field_new_alike(dfield, FALSE);
    static const gdouble r = 2.5;
    gint xres, yres, i, j, size;
    gdouble qx, qy;
    gint *xp, *yp;
    gdouble *d, *z;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    d = gwy_data_field_get_data(show);
    qx = gwy_data_field_get_dx(dfield);
    qy = gwy_data_field_get_dx(dfield);

    size = gwy_data_field_get_circular_area_size(r);
    z = g_new(gdouble, size);
    xp = g_new(gint, 2*size);
    yp = xp + size;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble bx, by;
            gint n;

            n = gwy_data_field_circular_area_extract_with_pos(dfield, j, i, r,
                                                              z, xp, yp);
            fit_local_plane_by_pos(n, xp, yp, z, &bx, &by);
            bx /= qx;
            by /= qy;
            d[i*xres + j] = atan(hypot(bx, by));
        }
    }
    g_free(xp);
    g_free(z);

    gwy_data_field_copy(show, dfield, FALSE);
    gwy_object_unref(show);

}

static void
run_classification(ClassifyControls *controls)
{
    GwyDataField *mask_a=NULL, *mask_b=NULL, *dfield;
    GwyDataField **cldata;
    ClassifyArgs *args;
    GwyContainer *data;
    GtkWidget *dialog;
    GQuark quark;
    gint i, n, ncriteria;
    Classifier cl;

    args = controls->args;

    ncriteria = 0;
    for (i=0; i<2*NCRITS; i++) {
        if (args->id[i]>0) {
            if (CDEBUG>1) printf("data %d using quantity %d on scale %d\n", args->id[i], args->quantity[i], args->scale[i]);
            ncriteria++;
        }
    }

    if (ncriteria==0) {

        dialog = gtk_message_dialog_new(GTK_WINDOW(controls->dialog),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_OK,
                                        _("No data are selected for any criterion (all IDs are 0)."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (CDEBUG>1) printf("Error: there are no valid criteria to apply\n");
        return;
    }


    if (CDEBUG>1) printf("getting mask A for %d\n", args->maska);
    data = gwy_app_data_browser_get(args->objects[args->maska % NARGS].datano);

    quark = gwy_app_get_mask_key_for_id(args->objects[args->maska % NARGS].id);
    if (!gwy_container_gis_object(data, quark, &mask_a))
    {
        dialog = gtk_message_dialog_new(GTK_WINDOW(controls->dialog),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_OK,
                                        _("Image A has no mask."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (CDEBUG>1) printf("Error: There is no mask A in channel %d\n", args->maska);
        return;
    }

    if (CDEBUG>1) printf("getting mask B for %d\n", args->maskb);
    data = gwy_app_data_browser_get(args->objects[args->maskb % NARGS].datano);
    quark = gwy_app_get_mask_key_for_id(args->objects[args->maskb % NARGS].id);
    if (!gwy_container_gis_object(data, quark, &mask_b))
    {
        dialog = gtk_message_dialog_new(GTK_WINDOW(controls->dialog),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_OK,
                                        _("Image B has no mask."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (CDEBUG>1) printf("Error: There is no mask B in channel %d\n", args->maskb);
        return;
    }

    if (CDEBUG) printf("filling the data for %d criteria\n", ncriteria);
    //create the data sets - one field for each valid criterion, either allocated and filled or pointer
    cldata = (GwyDataField **)g_malloc(ncriteria*sizeof(GwyDataField *));
    n = 0;
    for (i=0; i<2*NCRITS; i++) {
        if (args->id[i]==0) continue;

        data = gwy_app_data_browser_get(args->objects[args->id[i]-1].datano);
        quark = gwy_app_get_data_key_for_id(args->objects[args->id[i]-1].id);
        dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

        cldata[n] = gwy_data_field_duplicate(dfield);
        gwy_data_field_filter_gaussian(cldata[n], pow(2, args->scale[i]));

        if (args->quantity[i]==1) gwy_data_field_filter_laplacian_of_gaussians(cldata[n]);
        else if (args->quantity[i]==2) inclination_filter(cldata[n]);

        n++;
    }


    //train the classifier using the data set, mask_a and mask_b
    cl.verbose = 1;
    gwy_app_wait_start(GTK_WINDOW(controls->dialog), _("Training classifier..."));
    classifier_train_full(&cl, cldata, ncriteria, mask_a, mask_b);
    gwy_app_wait_finish();

    //run the classification, creating result_a and result_b
    if (!controls->result_a) controls->result_a = gwy_data_field_duplicate(mask_b);
    if (!controls->result_b) controls->result_b = gwy_data_field_duplicate(mask_a);

    classifier_run(&cl, cldata, ncriteria, controls->result_a, controls->result_b);

    classify_update_view(GTK_COMBO_BOX(controls->display), controls);


    if (CDEBUG) printf("freeing the data\n");
    for (i=0; i<ncriteria; i++) gwy_object_unref(cldata[i]);
    g_free(cldata);
}

//typedef struct
//{
//    int nrules;
//    int rule_parameter[MAXRULES];
//    double rule_threshold[MAXRULES];
//    int rule_goto_high[MAXRULES];
//    int rule_godo_low[MAXRULES];
//} CTree;


//mask_a: user defined mask a
//mask_b: user defined mask b
//result_a: computed mask a
//result_b: computed mask b
//selection: masked data will be evaluated only, or NULL for evaluating whole images (still based on mask_a, mask_b only).
//a_purity: how much the A is really filled with As
//b_purity: how much the B is really filled with Bs
static gdouble
get_score(GwyDataField *mask_a, GwyDataField *mask_b, GwyDataField *result_a, GwyDataField *result_b,
          GwyDataField *selection, gdouble *a_purity, gdouble *b_purity, gdouble *sumsa, gdouble *sumsb)
{
    gint i;
    gint n = gwy_data_field_get_xres(mask_a)*gwy_data_field_get_yres(mask_a);
    gdouble *ma, *mb, *ra, *rb, *s;
    gdouble sumainb, sumbina, nma, sumbinb, sumaina, nmb, paina, painb, pbina, pbinb;
    gboolean selall = 0;
    gdouble ginia, ginib;

    ma = gwy_data_field_get_data(mask_a);
    mb = gwy_data_field_get_data(mask_b);
    ra = gwy_data_field_get_data(result_a);
    rb = gwy_data_field_get_data(result_b);

    if (selection==NULL) selall = 1;
    else s = gwy_data_field_get_data(selection);

    sumaina = sumbina = nma = 0;
    sumainb = sumbinb = nmb = 0;
    for (i=0; i<n; i++)
    {
        if (selall || s[i]) {
           sumaina += ma[i]*ra[i];
           sumbina += mb[i]*ra[i];
           nma += ma[i]*ra[i] + mb[i]*ra[i];
           sumainb += ma[i]*rb[i];
           sumbinb += mb[i]*rb[i];
           nmb += ma[i]*rb[i] + mb[i]*rb[i];
        }
    }
    if ((sumaina+sumbina)>0) {
       paina = sumaina/(sumaina + sumbina);
       pbina = sumbina/(sumaina + sumbina);
    } else paina = pbina = 0;

    if ((sumainb+sumbinb)>0) {
       painb = sumainb/(sumainb + sumbinb);
       pbinb = sumbinb/(sumainb + sumbinb);
    } else painb = pbinb = 0;

    ginia = paina*(1-paina) + pbina*(1-pbina);
    ginib = painb*(1-painb) + pbinb*(1-pbinb);

    *sumsa = sumaina + sumainb;
    *sumsb = sumbina + sumbinb;
    *a_purity = ginia;
    *b_purity = ginib;

    if (CDEBUG>1) printf(" pura %g purb %g score %g  sumaina %g  sumainb %g  sumbina %g  sumbinb %g  nma %g nmb %g\n", ginia, ginib, nma*ginia/(nma+nmb) + nmb*ginib/(nma+nmb), sumaina, sumainb, sumbina, sumbinb, nma, nmb);


    return nma*ginia/(nma+nmb) + nmb*ginib/(nma+nmb);
}

static void
print_ct(CTree *ct)
{
    int n;

    printf("Printing tree, it has %d rules\n", ct->nrules);
    for (n=0; n<ct->nrules; n++)
    {
       printf("Node %d: ------------------------\n", n);
       printf("if quantity %d is bigger than %g:\n", ct->rule_parameter[n], ct->rule_threshold[n]);
       printf("     goto %d\n", ct->rule_goto_high[n]);
       printf("else goto %d\n", ct->rule_goto_low[n]);
       printf("--------------------------------\n");
    }

}

//find the best splitting criterion and threshold value
//returns index of the best criterion (field in cldata) for splitting,
//threshold to split most efficiently,
//purity of the found set A,
//purity of the found set B
//last crit is the criterion that was used last time, to be skipped
static gint
get_next_split(GwyDataField **cldata, gint ndata,
               GwyDataField *mask_a, GwyDataField *mask_b, GwyDataField *selection, gdouble *threshold,
               gdouble *a_purity, gdouble *b_purity, gdouble *gini, gdouble *sumsa, gdouble *sumsb, GwyDataField *result_a, GwyDataField *result_b,
               gint lastcrit)
{
   gint n, bestcrit;
   gdouble bestscore, bestth, bestgini, bestthreshold, min, max, step, th, score, suma, sumb;
   gdouble apur, bpur, bestapur, bestbpur, bestapurity, bestbpurity, bestsuma, bestsumb, bestsumsa, bestsumsb;
   CTree ct;

   if (CDEBUG>1) printf("Called get next split\n");

   //go thorugh criteria (fields in cldata) one by one and all possible thresholds, searching for lowest gini impurity
   bestgini = 1;
   bestcrit = 0;
   bestthreshold = 0;
   bestapurity = 0;
   bestbpurity = 0;
   bestsumsa = 0;
   bestsumsb = 0;
   for (n=0; n<ndata; n++)
   {
       if (n==lastcrit) continue; //skip what was used for splitting last time

       ct.rule_parameter[0] = n;
       ct.rule_goto_high[0] = -1;
       ct.rule_goto_low[0] = -2;

       min = gwy_data_field_area_get_min(cldata[ct.rule_parameter[0]], selection, 0, 0, gwy_data_field_get_xres(cldata[ct.rule_parameter[0]]), gwy_data_field_get_yres(cldata[ct.rule_parameter[0]]));
       max = gwy_data_field_area_get_max(cldata[ct.rule_parameter[0]], selection, 0, 0, gwy_data_field_get_xres(cldata[ct.rule_parameter[0]]), gwy_data_field_get_yres(cldata[ct.rule_parameter[0]]));
       if (CDEBUG>1) printf("criterion %d min %g max %g\n", ct.rule_parameter[0], min, max);
       step = (max-min)/100;

       bestscore = 1;
       bestth = 0;
       bestapur = 0;
       bestbpur = 0;
       bestsuma = 0;
       bestsumb = 0;
       for (th=min; th<max; th+=step)
       {
           ct.rule_threshold[0] = th;
           ctree_run(&ct, cldata, ndata, result_a, result_b);
           if (CDEBUG>1) printf("threshold %g ", th);
           score = get_score(mask_a, mask_b, result_a, result_b, selection, &apur, &bpur, &suma, &sumb);
         //  printf("n %d  th %g  score %g  apur %g  bpur %g  sums %g\n", n, th, score, apur, bpur, sum);
           if (score < bestscore) {
               bestscore = score;
               bestth = th;
               bestapur = apur;
               bestbpur = bpur;
               bestsuma = suma;
               bestsumb = sumb;
           }
       }
       if (CDEBUG) printf("best threshold for quantity n: %d  gini %g threshold %g  purities %g %g  sum %g %g\n", n, bestscore, bestth, bestapur, bestbpur, bestsuma, bestsumb);

       if (bestscore<bestgini) {
           bestgini = bestscore;
           bestcrit = n;
           bestthreshold = bestth;
           bestapurity = bestapur;
           bestbpurity = bestbpur;
           bestsumsa = bestsuma;
           bestsumsb = bestsumb;
       }
   }
   if (CDEBUG) printf("Get branch result: criterion %d gini %g threshold %g  purities %g %g  sums %g %g\n", bestcrit, bestgini, bestthreshold, bestapurity, bestbpurity, bestsumsa, bestsumsb);

   //fill the results with mask of a and b
   ct.rule_parameter[0] = bestcrit;
   ct.rule_threshold[0] = bestthreshold;
   ctree_run(&ct, cldata, ndata, result_a, result_b);

   *threshold = bestthreshold;
   *gini = bestgini;
   *a_purity = bestapurity;
   *b_purity = bestbpurity;
   *sumsa = bestsumsa;
   *sumsb = bestsumsb;
   return bestcrit;
}


static void
print_dfield(GwyDataField *df, gint index)
{
    FILE *fw;
    int i, j;
    char filename[100];

    if (!df) return;

    printf("printing index %d\n", index);

    sprintf(filename, "sel_%d_%dx%d.txt", index, gwy_data_field_get_xres(df), gwy_data_field_get_yres(df));

    fw = fopen(filename, "w");

    if (df) {
       for (j=0; j<gwy_data_field_get_yres(df); j++)
       {
           for (i=0; i<gwy_data_field_get_xres(df); i++)
           {
              fprintf(fw, "%g ", gwy_data_field_get_val(df, i, j));
           }
           fprintf(fw, "\n");
       }
    }

    fclose(fw);
}

static gint
process_branch(CTree *ct, GwyDataField **cldata, GwyDataField *mask_a, GwyDataField *mask_b,
                 gint ndata, gint *n, GwyDataField *selection, gint lastcrit)
{
    GwyDataField *result_a = gwy_data_field_new_alike(cldata[0], TRUE);
    GwyDataField *result_b = gwy_data_field_new_alike(cldata[0], TRUE);
    GwyDataField *sel_a = gwy_data_field_new_alike(cldata[0], TRUE);
    GwyDataField *sel_b = gwy_data_field_new_alike(cldata[0], TRUE);
    gdouble apur, bpur, gini, threshold, sumsa, sumsb, retval;
    gint thisn = *n;
    gint nextn, ret;

    if (CDEBUG) printf("Processing branch %d\n", thisn);

    if (CDEBUG>1) print_dfield(selection, thisn);

    if (ndata==1) //special case when only one criterion exists, so we can't swap them
        ct->rule_parameter[thisn] = get_next_split(cldata, ndata,
                                           mask_a, mask_b, selection, &threshold,
                                           &apur, &bpur, &gini, &sumsa, &sumsb, result_a, result_b, -1);
    else //normal case, last criterion is not used for next split
        ct->rule_parameter[thisn] = get_next_split(cldata, ndata,
                                           mask_a, mask_b, selection, &threshold,
                                           &apur, &bpur, &gini, &sumsa, &sumsb, result_a, result_b, lastcrit);

    if (CDEBUG>1) print_dfield(result_a, 100+thisn);
    if (CDEBUG>1) print_dfield(result_b, 200+thisn);

    ct->rule_threshold[thisn] = threshold;
    if (CDEBUG) printf("(%d) sugggested rule for split: crit %d  threshold %g, purities %g %g  sums %g %g\n", thisn, ct->rule_parameter[thisn], ct->rule_threshold[thisn], apur, bpur, sumsa, sumsb);

    if (sumsa == 0 || sumsb == 0) { //one of branches has no members, so report this to what it had called and don't create new rule.
        if (sumsa>=sumsb) retval = -1;
        else retval = -2;
        if (CDEBUG) printf("Error: one branch does not have members, stop further branching and return %g\n", retval);
    }
    else //setup new rule
    {
        if (CDEBUG) printf("Rule accepted and will be further developed\n");
        ct->nrules++;
        retval = 0;

        if (apur>PURCRIT || *n>MAXBRANCHES) //make this adjustable!
        {
            ct->rule_goto_high[thisn] = -1;
            if (CDEBUG) printf("(%d) step high: we are done (purity %g), response is -1\n", thisn, apur);
        }
        else {
            *n += 1;
            nextn = *n;
            ct->rule_goto_high[thisn] = nextn;
            if (CDEBUG) printf("(%d) step high: go to next branch at index %d\n", thisn, ct->rule_goto_high[thisn]);

            //create actual selection, combining the previous selection with last result_a
            if (selection==NULL)
                gwy_data_field_copy(result_a, sel_a, FALSE);
            else
                gwy_data_field_multiply_fields(sel_a, selection, result_a);

            if (CDEBUG) printf("(%d) selection for next process %d has %g points\n", thisn, nextn, gwy_data_field_get_sum(sel_a));
            if (CDEBUG) printf("now will process branch A with number %d\n", nextn);
            if ((ret = process_branch(ct, cldata, mask_a, mask_b, ndata, n, sel_a, ct->rule_parameter[thisn]))!=0)
            {
                if (CDEBUG) printf("Branch could not be further developed, goto_high in this branch %d will be %d\n", thisn, ret);
                ct->rule_goto_high[thisn] = ret;
                *n -= 1;
            }
        }

        if (bpur>PURCRIT || *n>MAXBRANCHES) //make this adjustable!
        {
            ct->rule_goto_low[thisn] = -2;
            if (CDEBUG) printf("(%d) step low: we are done (purity %g), response is -2\n", thisn, apur);
        }
        else {
            *n += 1;
            nextn = *n;
            ct->rule_goto_low[thisn] = nextn;
            if (CDEBUG) printf("(%d) step low: go to next branch at index %d\n", thisn, ct->rule_goto_low[thisn]);

            //create actual selection, combining the previous selection with last result_a
            if (selection==NULL)
               gwy_data_field_copy(result_b, sel_b, FALSE);
            else
               gwy_data_field_multiply_fields(sel_b, selection, result_b);

            if (CDEBUG) printf("(%d) selection for next process %d has %g points\n", thisn, nextn, gwy_data_field_get_sum(sel_b));
            if (CDEBUG) printf("now will process branch B with number %d\n", nextn);

            if ((ret = process_branch(ct, cldata, mask_a, mask_b, ndata, n, sel_b, ct->rule_parameter[thisn]))!=0) //we could not branch further, stop it
            {
                if (CDEBUG) printf("Branch could not be further developed, goto_high in this branch %d will be %d\n", thisn, ret);
                ct->rule_goto_low[thisn] = ret;
                *n -= 1;
            }

        }
    }

    if (CDEBUG) printf("End of processing branch %d\n", thisn);

    gwy_object_unref(result_a);
    gwy_object_unref(result_b);
    gwy_object_unref(sel_a);
    gwy_object_unref(sel_b);

    return retval;
}



static void
train_tree(CTree *ct, GwyDataField **cldata,
           gint ndata, GwyDataField *mask_a, GwyDataField *mask_b,
           GwyDataField *selection)
{
    gint n = 0;

    process_branch(ct, cldata, mask_a, mask_b, ndata, &n, selection, -1);
    if (CDEBUG) print_ct(ct);
}

//setup whole forest
static void
classifier_train_full(Classifier *cl, GwyDataField **cldata,
                 gint ndata, GwyDataField *mask_a, GwyDataField *mask_b)
{
    CTree *ct;
    if (CDEBUG) printf("Classifier train started on %d data sets\n", ndata);

    ct = cl->ct + 0;
    ct->nrules = 0;
    train_tree(ct, cldata, ndata, mask_a, mask_b, NULL);
}

//run single tree on single point in the image
static gint
run_ct(CTree *ct, GwyDataField **cldata, G_GNUC_UNUSED gint ndata, gint xpos, gint ypos)
{
    gint i, n;

    n = 0;
    for (i=0; i<1000; i++)
    {
        //printf("rp at %d %d  n %d param %d\n", xpos, ypos, n, ct->rule_parameter[n]);
        if (gwy_data_field_get_val(cldata[ct->rule_parameter[n]], xpos, ypos)>ct->rule_threshold[n])
        {
            if (ct->rule_goto_high[n]<0) {
                return ct->rule_goto_high[n];
            }
            else n = ct->rule_goto_high[n];
        }
        else
        {
            if (ct->rule_goto_low[n]<0) {
                return ct->rule_goto_low[n];
            }

            n = ct->rule_goto_low[n];
        }
        //printf("next n: %d\n", n);
    }
    //printf("Error: CT run did not finish after 1000 iterations\n");
    return -3;
}

//run a single tree on whole image
static void
ctree_run(CTree *ct, GwyDataField **cldata,
                 gint ndata, GwyDataField *result_a, GwyDataField *result_b)
{
    gint i, j, result;
    gint xres = gwy_data_field_get_xres(cldata[0]);
    gint yres = gwy_data_field_get_yres(cldata[0]);

    for (i=0; i<xres; i++) {
        for (j=0; j<yres; j++) {
            result = run_ct(ct, cldata, ndata, i, j);
            if (result == -1)
            {
                gwy_data_field_set_val(result_a, i, j, 1);
                gwy_data_field_set_val(result_b, i, j, 0);
            }
            if (result == -2)
            {
                gwy_data_field_set_val(result_a, i, j, 0);
                gwy_data_field_set_val(result_b, i, j, 1);
            }
        }
    }

}

//run the forest on whole image
static void
classifier_run(Classifier *cl, GwyDataField **cldata,
                 gint ndata, GwyDataField *result_a, GwyDataField *result_b)
{
    ctree_run(cl->ct + 0, cldata, ndata, result_a, result_b);  //now just run the first tree
}


static const gchar mask_a_key[]   = "/module/classify/mask_a";
static const gchar mask_b_key[]   = "/module/classify/mask_b";
static const gchar display_key[]  = "/module/classify/display";

static void
classify_load_args(GwyContainer *settings,
                     ClassifyArgs *args)
{
    guint i;
    gchar key[50];

    args->maska = 1;
    args->maskb = 2;
    args->display = 0;
    gwy_container_gis_int32_by_name(settings, mask_a_key, &args->maska);
    gwy_container_gis_int32_by_name(settings, mask_b_key, &args->maskb);
    gwy_container_gis_enum_by_name(settings, display_key, &args->display);

    for (i=0; i<2*NCRITS; i++)
    {
        args->id[i] = args->scale[i] = args->quantity[i] = 0;
    }
    for (i = 0; i < 2*NCRITS; i++) {
        g_snprintf(key, sizeof(key), "/module/classify/id%u", i);
        gwy_container_gis_int32_by_name(settings, key, &(args->id[i]));

        g_snprintf(key, sizeof(key), "/module/classify/quantity%u", i);
        gwy_container_gis_enum_by_name(settings, key, &(args->quantity[i]));

        g_snprintf(key, sizeof(key), "/module/classify/scale%u", i);
        gwy_container_gis_enum_by_name(settings, key, &(args->scale[i]));
     }

    args->show = object_ids[0]; //this should be done better, saving the last selection

    for (i = 1; i < NARGS; i++) {
        args->objects[i] = object_ids[i];
        // Init to d1 instead of ‘none’ when we lose the fields.
        if (!gwy_app_data_id_verify_channel(args->objects + i))
            args->objects[i] = args->objects[0];
    }
}

static void
classify_save_args(GwyContainer *settings,
                     ClassifyArgs *args)
{
    guint i;
    gchar key[50];

    gwy_assign(object_ids, args->objects, NARGS);

    gwy_container_set_int32_by_name(settings, mask_a_key,
                                     args->maska);
    gwy_container_set_int32_by_name(settings, mask_b_key,
                                     args->maskb);
    gwy_container_set_enum_by_name(settings, display_key,
                                     args->display);

    for (i = 0; i < 2*NCRITS; i++) {
        g_snprintf(key, sizeof(key), "/module/classify/id%u", i);
        gwy_container_set_int32_by_name(settings, key, args->id[i]);

        g_snprintf(key, sizeof(key), "/module/classify/quantity%u", i);
        gwy_container_set_enum_by_name(settings, key, args->quantity[i]);

        g_snprintf(key, sizeof(key), "/module/classify/scale%u", i);
        gwy_container_set_enum_by_name(settings, key, args->scale[i]);
     }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
