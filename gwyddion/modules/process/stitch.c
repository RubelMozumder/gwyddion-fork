/*
 *  $Id: stitch.c 25638 2023-09-07 12:23:28Z yeti-dn $
 *  Copyright (C) 2017 David Necas (Yeti), Petr Klapetek, Petr Grolich.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, pgrolich@cmi.cz.
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
#include <libgwydgets/gwystock.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include "preview.h"

#define STITCH_RUN_MODES GWY_RUN_INTERACTIVE

enum {
    NARGS = 8,
    USER_UNITS_ID = G_MAXINT,
};

enum {
    SENS_EXPR_OK = 1 << 0,
    SENS_USERUINTS = 1 << 1
};

enum {
    STITCH_VALUE,
    STITCH_MASK,
    STITCH_DER_X,
    STITCH_DER_Y,
    STITCH_NVARS,
};

enum {
    STITCH_NARGS = NARGS * STITCH_NVARS + 2  /* 2 for x and y */
};

enum {
    STITCH_OK      = 0,
    STITCH_DATA    = 1,
};

typedef GwyDataField* (*MakeFieldFunc)(GwyDataField *dfield);

typedef struct {
    guint err;
    GwyAppDataId objects[NARGS];
    gint nobjects_in_chooser;
    gint choosers[NARGS];
    gboolean enabled[NARGS];
    gdouble xoffset[NARGS];
    gdouble yoffset[NARGS];
    gdouble zoffset[NARGS];
    gboolean instant_update;
    gboolean initialized;
    GwySIValueFormat *format;
} StitchArgs;

typedef struct {
    StitchArgs *args;
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *choosers[NARGS];
    GtkWidget *enabled[NARGS];
    GtkWidget *push_buttons[NARGS];
    GtkObject *xoffset[NARGS];
    GtkWidget *xoffset_spin[NARGS];
    GtkObject *yoffset[NARGS];
    GtkWidget *yoffset_spin[NARGS];
    GtkObject *zoffset[NARGS];
    GtkWidget *zoffset_spin[NARGS];
    GtkWidget *instant_update;
    GwyContainer *mydata;
} StitchControls;

static gboolean      module_register        (void);
static void          stitch                 (GwyContainer *data,
                                             GwyRunType run);
static void          stitch_load_args       (GwyContainer *settings,
                                            StitchArgs *args);
static void          stitch_save_args       (GwyContainer *settings,
                                             StitchArgs *args);
static gboolean      stitch_dialog          (GwyContainer *data,
                                             gint id,
                                             StitchArgs *args);
static void          stitch_data_chosen     (GwyDataChooser *chooser,
                                             StitchControls *controls);
static void          stitch_data_checked    (StitchControls *controls);
static void          stitch_offset_changed  (StitchControls *controls);
static void          stitch_restore_offset  (GtkWidget *button,
                                             StitchControls *controls);
static void          stitch_instant_update_changed(GtkToggleButton *check,
                                                   StitchControls *controls);
static void          stitch_show_sensitive  (StitchControls *controls);
static void          stitch_preview         (StitchControls *controls);
static GwyDataField* stitch_do              (StitchArgs *args);
static void          get_object_ids         (GwyContainer *container,
                                             gpointer *obsolete);
static void          stitch_format_value    (StitchControls *controls,
                                             GtkAdjustment *adjustment,
                                             gdouble value);
static void          update_data_from_controls(StitchControls *controls);


static const gchar instant_update_key[] = "/module/stitch/instant_update";

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Stitch multiple images based on offsets of origins."),
    "Petr Grolich <pgrolich.cmi.cz>",
    "1.2",
    "Petr Klapetek & Petr Grolich",
    "2017",
};

GWY_MODULE_QUERY2(module_info, stitch)

static gboolean
module_register(void)
{
    gwy_process_func_register("stitch",
                              (GwyProcessFunc)&stitch,
                              N_("/M_ultidata/_Stitch..."),
                              GWY_STOCK_STITCH,
                              STITCH_RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Stitch images using offsets"));

    return TRUE;
}

static gboolean
stitch_chooser_filter(GwyContainer *data,
                      gint id,
                      gpointer user_data)
{
    GwyAppDataId *object = (GwyAppDataId*)user_data;
    GwyDataField *op1, *op2;
    GQuark quark;

    quark = gwy_app_get_data_key_for_id(id);
    op1 = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    data = gwy_app_data_browser_get(object->datano);
    quark = gwy_app_get_data_key_for_id(object->id);
    op2 = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

    return !gwy_data_field_check_compatibility(op1, op2,
                                               GWY_DATA_COMPATIBILITY_MEASURE
                                               | GWY_DATA_COMPATIBILITY_LATERAL
                                               | GWY_DATA_COMPATIBILITY_VALUE);
}

static void
get_object_ids(GwyContainer *container, gpointer *data)
{
    gint *ids;
    guint i;
    StitchArgs *args = (StitchArgs*)data;

    if (args->nobjects_in_chooser > NARGS)
        return;

    ids = gwy_app_data_browser_get_data_ids(container);
    for (i = 0; ids[i] >= 0; i++) {
        args->objects[args->nobjects_in_chooser].id = ids[i];
        args->objects[args->nobjects_in_chooser].datano
            = gwy_app_data_browser_get_number(container);
        args->nobjects_in_chooser++;
    }
    g_free(ids);
}

static void
stitch_format_value(StitchControls *controls,
                    GtkAdjustment *adjustment,
                    gdouble value)
{
    gtk_adjustment_set_value(adjustment,
                             value / controls->args->format->magnitude);
}

void
stitch(GwyContainer *data, GwyRunType run)
{
    StitchArgs args;
    GwyContainer *settings;
    gboolean dorun;
    gint datano, id, newid;

    g_return_if_fail(run & STITCH_RUN_MODES);

    args.nobjects_in_chooser = 0;
    gwy_app_data_browser_foreach((GwyAppDataForeachFunc)get_object_ids, &args);

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id,
                                     GWY_APP_CONTAINER_ID, &datano,
                                     0);

    settings = gwy_app_settings_get();
    stitch_load_args(settings, &args);

    dorun = stitch_dialog(data, id, &args);
    stitch_save_args(settings, &args);
    if (dorun) {
        GwyDataField *result = stitch_do(&args);

        newid = gwy_app_data_browser_add_data_field(result, data, TRUE);
        g_object_unref(result);
        gwy_app_set_data_field_title(data, newid, _("Calculated"));
        gwy_app_sync_data_items(data, data, id, newid, FALSE,
                                GWY_DATA_ITEM_GRADIENT,
                                GWY_DATA_ITEM_REAL_SQUARE,
                                0);
        gwy_app_channel_log_add_proc(data, -1, newid);
    }

    gwy_si_unit_value_format_free(args.format);
}

static gboolean
stitch_dialog(GwyContainer *container, gint id, StitchArgs *args)
{
    GQuark quark;
    GwyContainer *data;
    StitchControls controls;
    gint i;
    guint row, response;
    gboolean is_active;
    gdouble xoffset, yoffset, zoffset;
    gchar* title;
    GwyDataField *dfield_preview, *dfield_current, *dfield;
    GtkWidget *dialog, *hbox, *vbox, *table, *chooser, *label, *spin;
    GwyAppDataId* active_object;

    controls.args = args;
    controls.args->initialized = FALSE;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield_current, 0);
    controls.args->format = gwy_data_field_get_value_format_xy
                            (dfield_current, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);

    dialog = gtk_dialog_new_with_buttons(_("Stitch"), NULL, 0, NULL);
    controls.dialog = dialog;
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog),
                                 gwy_stock_like_button_new(_("_Update"),
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

    controls.mydata = gwy_container_new();
    dfield_preview = gwy_data_field_new(PREVIEW_SIZE, PREVIEW_SIZE,
                                        1.0, 1.0, TRUE);
    gwy_container_pass_object_by_name(controls.mydata, "/0/data", dfield_preview);
    gwy_app_sync_data_items(container, controls.mydata, id, 0, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    controls.view = gwy_create_preview(controls.mydata, 0, PREVIEW_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), controls.view, FALSE, FALSE, 0);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls.view), PREVIEW_SIZE);

    table = gtk_table_new(5 + NARGS, 9, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);
    gtk_box_pack_start(GTK_BOX(hbox), table, TRUE, TRUE, 4);
    row = 0;

    label = gtk_label_new(_("Channels"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row+1,
                     GTK_FILL, 0, 0, 0);

    title = g_strdup_printf("X offset [%s]", controls.args->format->units);
    label = gtk_label_new(title);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, row, row + 1,
                     GTK_FILL, 0, 0, 0);
    g_free(title);

    title = g_strdup_printf("Y offset [%s]", controls.args->format->units);
    label = gtk_label_new(title);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 3, 4, row, row + 1,
                     GTK_FILL, 0, 0, 0);
    g_free(title);

    title = g_strdup_printf("Z offset [%s]", controls.args->format->units);
    label = gtk_label_new(title);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, row, row + 1,
                     GTK_FILL, 0, 0, 0);
    g_free(title);

    row++;

    for (i = 0; i < NARGS; i++) {
        chooser = gwy_data_chooser_new_channels();
        gwy_data_chooser_set_filter(GWY_DATA_CHOOSER(chooser),
                                    stitch_chooser_filter,
                                    &args->objects,
                                    NULL);
        if (i < args->nobjects_in_chooser) {
            active_object = args->objects + i;
            /*gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(chooser),
                                           active_object);*/
        }
        else {
            active_object = args->objects + args->nobjects_in_chooser - 1;
            /*gwy_data_chooser_set_none(GWY_DATA_CHOOSER(chooser), _(""));
            gwy_data_chooser_set_active(GWY_DATA_CHOOSER(chooser), NULL, 0);*/
        }

        gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(chooser),
                                       active_object);

        g_signal_connect(chooser, "changed",
                         G_CALLBACK(stitch_data_chosen), &controls);
        g_object_set_data(G_OBJECT(chooser), "index", GUINT_TO_POINTER(i));
        gtk_table_attach(GTK_TABLE(table), chooser, 0, 1, row, row+1,
                         GTK_FILL, 0, 0, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), chooser);
        controls.choosers[i] = chooser;

        controls.enabled[i] = gtk_check_button_new();
        is_active = (i < args->nobjects_in_chooser);
        gtk_toggle_button_set_active
            (GTK_TOGGLE_BUTTON(controls.enabled[i]), is_active);
        args->enabled[i] = is_active;
        gtk_widget_set_tooltip_text(controls.enabled[i], _("Stitch data"));
        gtk_table_attach
            (GTK_TABLE(table), controls.enabled[i], 1, 2, row, row+1,
             0, 0, 0, 0);
        g_signal_connect_swapped(controls.enabled[i], "clicked",
                                 G_CALLBACK(stitch_data_checked),
                                 &controls);

        data = gwy_app_data_browser_get(active_object->datano);

        xoffset = yoffset = zoffset = 0;
        if (NULL != data) {
            quark = gwy_app_get_data_key_for_id(active_object->id);
            dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));
            xoffset = gwy_data_field_get_xoffset(dfield);
            yoffset = gwy_data_field_get_yoffset(dfield);
            zoffset = gwy_data_field_get_avg(dfield);
        }

        controls.xoffset[i] = gtk_adjustment_new(0, -10000, 10000, 0.1, 1, 0);
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.xoffset[i]), 1, 2);
        controls.xoffset_spin[i] = spin;
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin),
                                   controls.args->format->precision + 2);
        g_signal_connect_swapped(controls.xoffset[i], "value-changed",
                                 G_CALLBACK(stitch_offset_changed), &controls);
        stitch_format_value(&controls, GTK_ADJUSTMENT(controls.xoffset[i]),
                            xoffset);
        gtk_table_attach(GTK_TABLE(table), spin,
                         2, 3, row, row + 1, GTK_FILL, 0, 0, 0);


        args->yoffset[i] = 0;
        controls.yoffset[i] = gtk_adjustment_new(0, -10000, 10000, 0.1, 1, 0);
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.yoffset[i]), 1, 2);
        controls.yoffset_spin[i] = spin;
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin),
                                   controls.args->format->precision + 2);
        g_signal_connect_swapped(controls.yoffset[i], "value-changed",
                                 G_CALLBACK(stitch_offset_changed), &controls);
        stitch_format_value(&controls, GTK_ADJUSTMENT(controls.yoffset[i]),
                            yoffset);
        gtk_table_attach(GTK_TABLE(table), spin,
                         3, 4, row, row + 1, GTK_FILL, 0, 0, 0);

        args->zoffset[i] = 0;
        controls.zoffset[i] = gtk_adjustment_new(0, -10000, 10000, 0.01, 1, 0);
        spin = gtk_spin_button_new(GTK_ADJUSTMENT(controls.zoffset[i]), 1, 2);
        controls.zoffset_spin[i] = spin;
        gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin),
                                   controls.args->format->precision + 2);
        g_signal_connect_swapped(controls.zoffset[i], "value-changed",
                                 G_CALLBACK(stitch_offset_changed), &controls);
        stitch_format_value(&controls, GTK_ADJUSTMENT(controls.zoffset[i]),
                            zoffset);
        gtk_table_attach(GTK_TABLE(table), spin,
                         4, 5, row, row + 1, GTK_FILL, 0, 0, 0);


        controls.push_buttons[i] = gtk_button_new_with_label(_("Restore"));
        gtk_table_attach(GTK_TABLE(table), controls.push_buttons[i],
                         5, 6, row, row + 1, 0, 0, 0, 0);
        g_signal_connect(controls.push_buttons[i], "clicked",
                         G_CALLBACK(stitch_restore_offset), &controls);

        row++;
    }

    controls.instant_update = gtk_check_button_new_with_mnemonic
                                (_("_Instant updates"));
    gtk_table_attach(GTK_TABLE(table), controls.instant_update,
                     0, 3, row, row + 1, GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(controls.instant_update),
                                 args->instant_update);
    g_signal_connect(controls.instant_update, "toggled",
                     G_CALLBACK(stitch_instant_update_changed),
                     &controls);
    row++;

    gtk_widget_show_all(dialog);

    controls.args->err = STITCH_OK;
    controls.args->initialized = TRUE;

    stitch_show_sensitive(&controls);
    if (controls.args->instant_update)
        stitch_preview(&controls);

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
                stitch_preview(&controls);
            break;

            case GTK_RESPONSE_OK:
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
stitch_data_chosen(GwyDataChooser *chooser,
                   StitchControls *controls)
{
    gint chooser_index;
    GQuark quark;
    GwyContainer *data;
    GwyDataField *dfield;
    StitchArgs *args = controls->args;

    for (chooser_index = 0; chooser_index < NARGS; chooser_index++) {
        if (GWY_DATA_CHOOSER(controls->choosers[chooser_index]) == chooser)
            break;
    }

    gwy_data_chooser_get_active_id
        (GWY_DATA_CHOOSER(controls->choosers[chooser_index]),
         &args->objects[chooser_index]);
    data = gwy_app_data_browser_get(args->objects[chooser_index].datano);

    g_return_if_fail(data);

    quark = gwy_app_get_data_key_for_id(args->objects[chooser_index].id);
    dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->xoffset[chooser_index]),
                        gwy_data_field_get_xoffset(dfield));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->yoffset[chooser_index]),
                        gwy_data_field_get_yoffset(dfield));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->zoffset[chooser_index]),
                        gwy_data_field_get_avg(dfield));
}

static void
stitch_data_checked(StitchControls *controls)
{
    gint i, ntoggled = 0;
    StitchArgs *args = controls->args;

    for (i = 0; i < NARGS; i++) {
        args->enabled[i]
            = gtk_toggle_button_get_active
              (GTK_TOGGLE_BUTTON(controls->enabled[i]));
        if (args->enabled[i])
            ntoggled++;
    }

    if (ntoggled == 0)
        args->err |= STITCH_DATA;
    else
        args->err &= ~STITCH_DATA;
    stitch_show_sensitive(controls);

    if (controls->args->instant_update)
        stitch_preview(controls);
}

static void
stitch_offset_changed(StitchControls *controls)
{
    gint i;
    if (controls->args->initialized) {
        for (i = 0; i < NARGS; i++) {
            controls->args->xoffset[i]
                = gtk_adjustment_get_value
                  (GTK_ADJUSTMENT(controls->xoffset[i]));
            controls->args->yoffset[i]
                = gtk_adjustment_get_value
                  (GTK_ADJUSTMENT(controls->yoffset[i]));
            controls->args->zoffset[i]
                = gtk_adjustment_get_value
                  (GTK_ADJUSTMENT(controls->zoffset[i]));
        }

        if (controls->args->instant_update)
            stitch_preview(controls);
    }
}

static void
stitch_restore_offset(GtkWidget *button, StitchControls *controls)
{
    gint pb_index;
    GQuark quark;
    GwyContainer *data;
    GwyDataField *dfield;
    StitchArgs *args = controls->args;

    for (pb_index = 0; pb_index < NARGS; pb_index++) {
        if (controls->push_buttons[pb_index] == button)
            break;
    }

    gwy_data_chooser_get_active_id
        (GWY_DATA_CHOOSER(controls->choosers[pb_index]),
         &args->objects[pb_index]);
    data = gwy_app_data_browser_get(args->objects[pb_index].datano);

    g_return_if_fail(data);

    quark = gwy_app_get_data_key_for_id(args->objects[pb_index].id);
    dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->xoffset[pb_index]),
                        gwy_data_field_get_xoffset(dfield));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->yoffset[pb_index]),
                        gwy_data_field_get_yoffset(dfield));
    stitch_format_value(controls,
                        GTK_ADJUSTMENT(controls->zoffset[pb_index]),
                        gwy_data_field_get_avg(dfield));
}

static void stitch_instant_update_changed(GtkToggleButton *check,
                                          StitchControls *controls)
{
    controls->args->instant_update = gtk_toggle_button_get_active(check);
    if (controls->args->instant_update)
        stitch_preview(controls);
    stitch_show_sensitive(controls);
}

static void
stitch_show_sensitive(StitchControls *controls)
{
    StitchArgs *args = controls->args;
    GtkDialog *dialog = GTK_DIALOG(controls->dialog);
    gboolean ok, sensitive;
    gint i;

    for (i = 0; i < NARGS; i++) {
        controls->args->enabled[i] = gtk_toggle_button_get_active
            (GTK_TOGGLE_BUTTON(controls->enabled[i]));
        sensitive = args->enabled[i];
        gtk_widget_set_sensitive(controls->choosers[i], sensitive);
        gtk_widget_set_sensitive(controls->xoffset_spin[i], sensitive);
        gtk_widget_set_sensitive(controls->yoffset_spin[i], sensitive);
        gtk_widget_set_sensitive(controls->zoffset_spin[i], sensitive);
        gtk_widget_set_sensitive(controls->push_buttons[i], sensitive);
    }

    ok = (args->err == STITCH_OK);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_OK, ok);
    gtk_dialog_set_response_sensitive(dialog, RESPONSE_PREVIEW,
                                      (ok && !args->instant_update));
}

static void
stitch_preview(StitchControls *controls)
{
    GwyDataField *result;

    /* We can also get here by activation of the entry so check again. */
    if (controls->args->err != STITCH_OK) {
        gwy_container_set_object_by_name(controls->mydata, "/0/data", NULL);
        return;
    }

    update_data_from_controls(controls);

    result = stitch_do(controls->args);
    g_return_if_fail(result);

    gwy_container_set_object_by_name(controls->mydata, "/0/data", result);
    gwy_data_field_data_changed(result);
    gwy_set_data_preview_size(GWY_DATA_VIEW(controls->view), PREVIEW_SIZE);
    g_object_unref(result);
}

static GwyDataField*
stitch_do(StitchArgs *args)
{
    GQuark quark;
    GwyContainer *data;
    GwyDataField *dfield, *df_shift, *result = NULL;
    gint i, xres, yres, nfields, destcol, destrow;
    gdouble xreal, yreal, left, top, right, bottom, avg, x, y;
    GwySIUnit *siunitxy = NULL, *siunitz = NULL;

    dfield = NULL;
    xres = yres = nfields = 0;
    xreal = yreal = left = top = right = bottom = avg = 0.0;

    for (i = 0; i < NARGS; i++) {
        if (args->enabled[i]) {
            data = gwy_app_data_browser_get(args->objects[i].datano);
            g_return_val_if_fail(data, NULL);

            quark = gwy_app_get_data_key_for_id(args->objects[i].id);
            dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

            if (nfields == 0) {
                left = args->xoffset[i] * args->format->magnitude;
                top = args->yoffset[i] * args->format->magnitude;
                right = (args->xoffset[i] * args->format->magnitude
                         + gwy_data_field_get_xreal(dfield));
                bottom = (args->yoffset[i] * args->format->magnitude
                          + gwy_data_field_get_yreal(dfield));
                siunitxy = gwy_data_field_get_si_unit_xy(dfield);
                siunitz = gwy_data_field_get_si_unit_z(dfield);
            }
            else {
                left = MIN(left, args->xoffset[i] * args->format->magnitude);
                top = MIN(top, args->yoffset[i] * args->format->magnitude);
                right = MAX(right,
                            (args->xoffset[i] * args->format->magnitude)
                            + gwy_data_field_get_xreal(dfield));
                bottom = MAX(bottom,
                             (args->yoffset[i] * args->format->magnitude)
                             + gwy_data_field_get_yreal(dfield));
            }

            nfields++;
        }
    }

    xreal = right - left;
    yreal = bottom - top;

    g_return_val_if_fail((xreal > 0.0) && (yreal > 0.0) && nfields && dfield,
                         NULL);

    xres = GWY_ROUND(gwy_data_field_rtoj(dfield, xreal));
    yres = GWY_ROUND(gwy_data_field_rtoi(dfield, yreal));

    result = gwy_data_field_new(xres, yres, xreal, yreal, TRUE);
    if (siunitxy)
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(result), siunitxy);
    if (siunitz)
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(result), siunitz);

    for (i = 0; i < NARGS; i++) {
        if (args->enabled[i]) {
            data = gwy_app_data_browser_get(args->objects[i].datano);

            g_return_val_if_fail(data, NULL);

            quark = gwy_app_get_data_key_for_id(args->objects[i].id);
            dfield = GWY_DATA_FIELD(gwy_container_get_object(data, quark));

            /* shift data to provide consistent data */
            df_shift = gwy_data_field_duplicate(dfield);
            avg = args->zoffset[i] * args->format->magnitude;
            gwy_data_field_add(df_shift, -avg);

            x = (args->xoffset[i] * args->format->magnitude) - left;
            y = (args->yoffset[i] * args->format->magnitude) - top;
            destcol = GWY_ROUND(gwy_data_field_rtoj(df_shift, x));
            destrow = GWY_ROUND(gwy_data_field_rtoi(df_shift, y));
            gwy_data_field_area_copy(df_shift,
                                     result,
                                     0,
                                     0,
                                     gwy_data_field_get_xres(df_shift),
                                     gwy_data_field_get_yres(df_shift),
                                     destcol,
                                     destrow);

            g_object_unref(df_shift);
        }
    }

    return result;
}


static void
stitch_load_args(GwyContainer *settings,
                 StitchArgs *args)
{
    gint i;

    gwy_container_gis_boolean_by_name(settings, instant_update_key,
                                      &args->instant_update);

    for (i = 1; i < args->nobjects_in_chooser; i++) {
        if (!gwy_app_data_id_verify_channel(args->objects + i))
            args->objects[i] = args->objects[0];
    }
}

static void
stitch_save_args(GwyContainer *settings,
                 StitchArgs *args)
{
    gwy_container_set_boolean_by_name(settings, instant_update_key,
                                      args->instant_update);
}

static void
update_data_from_controls(StitchControls *controls)
{
    StitchArgs *args = controls->args;
    gint i;

    for (i = 0; i < NARGS; i++) {
        gwy_data_chooser_get_active(GWY_DATA_CHOOSER(controls->choosers[i]),
                                    &args->choosers[i]);
        args->xoffset[i]
            = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->xoffset[i]));
        args->yoffset[i]
            = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->yoffset[i]));
        args->zoffset[i]
            = gtk_adjustment_get_value(GTK_ADJUSTMENT(controls->zoffset[i]));
        args->enabled[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(controls->enabled[i]));
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
