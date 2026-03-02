/*
 *  $Id: multiprofile.c 24975 2022-08-31 12:05:41Z yeti-dn $
 *  Copyright (C) 2020-2021 David Necas (Yeti).
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
#include <libgwyddion/gwymath.h>
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
    NARGS = 6,
    MAX_THICKNESS = 128,
};

typedef enum {
    MULTIPROF_MODE_PROFILES = 0,
    MULTIPROF_MODE_MEAN_RMS = 1,
    MULTIPROF_MODE_MIN_MAX  = 2,
} MultiprofMode;

enum {
    PARAM_LINENO_FRAC,
    PARAM_THICKNESS,
    PARAM_MASKING,
    PARAM_USE_FIRST_MASK,
    PARAM_MODE,
    PARAM_TARGET_GRAPH,
    PARAM_DISPLAY,
    PARAM_IMAGE_0,                              /* Block with NARGS items; enabled[0] is always TRUE. */
    PARAM_ENABLED_0 = PARAM_IMAGE_0 + NARGS,    /* Block with NARGS items. */
};

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    GwyGraphModel *gmodel;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkWidget *image[NARGS];
    GtkWidget *enabled[NARGS];
    GtkWidget *display[NARGS];
    GwyContainer *data;
    GwySelection *selection;
    GtkWidget *view;
    gboolean in_update;
} ModuleGUI;

static gboolean         module_register         (void);
static GwyParamDef*     define_module_params    (void);
static void             multiprofile            (GwyContainer *data,
                                                 GwyRunType runtype);
static void             execute                 (ModuleArgs *args);
static GwyDialogOutcome run_gui                 (ModuleArgs *args,
                                                 GwyContainer *data,
                                                 gint id);
static GtkWidget*       create_image_table      (ModuleGUI *gui);
static void             param_changed           (ModuleGUI *gui,
                                                 gint id);
static void             dialog_response         (GwyDialog *dialog,
                                                 gint response,
                                                 ModuleGUI *gui);
static void             multiprofile_do_profiles(ModuleArgs *args);
static void             multiprofile_do_stats   (ModuleArgs *args);
static void             preview                 (gpointer user_data);
static void             selection_changed       (ModuleGUI *gui,
                                                 gint hint,
                                                 GwySelection *selection);
static void             enabled_changed         (ModuleGUI *gui,
                                                 GtkToggleButton *check);
static void             image_selected          (ModuleGUI *gui,
                                                 GwyDataChooser *chooser);
static void             display_changed         (ModuleGUI *gui,
                                                 GtkToggleButton *toggle);
static gboolean         image_filter            (GwyContainer *data,
                                                 gint id,
                                                 gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Displays and extracts scan line graphs from multiple images simultaneously."),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti)",
    "2020",
};

GWY_MODULE_QUERY2(module_info, multiprofile)

static gboolean
module_register(void)
{
    gwy_process_func_register("multiprofile",
                              (GwyProcessFunc)&multiprofile,
                              N_("/M_ultidata/_Multiprofile..."),
                              GWY_STOCK_PROFILE_MULTIPLE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA,
                              N_("Read lines from multiple images simultaneously"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum modes[] = {
        { N_("All profiles"),        MULTIPROF_MODE_PROFILES, },
        { N_("Mean and deviation"),  MULTIPROF_MODE_MEAN_RMS, },
        { N_("Minimum and maximum"), MULTIPROF_MODE_MIN_MAX,  },
    };

    static GwyParamDef *paramdef = NULL;
    guint i;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_process_func_current());
    gwy_param_def_add_double(paramdef, PARAM_LINENO_FRAC, "lineno_frac", _("_Scan line"), 0.0, 1.0, 0.5);
    gwy_param_def_add_int(paramdef, PARAM_THICKNESS, "thickness", _("_Thickness"), 1, 128, 1);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);
    gwy_param_def_add_boolean(paramdef, PARAM_USE_FIRST_MASK, "use_first_mask", _("Use _first mask for all images"),
                              TRUE);
    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", _("_Mode"),
                              modes, G_N_ELEMENTS(modes), MULTIPROF_MODE_PROFILES);
    gwy_param_def_add_target_graph(paramdef, PARAM_TARGET_GRAPH, "target_graph", NULL);
    gwy_param_def_add_int(paramdef, PARAM_DISPLAY, NULL, gwy_sgettext("verb|Display"), 0, NARGS-1, 0);
    /* The strings must be static so just ‘leak’ them. */
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_image_id(paramdef, PARAM_IMAGE_0 + i,
                                   g_strdup_printf("image/%u", i), g_strdup_printf("Image %u", i));
    }
    for (i = 0; i < NARGS; i++) {
        gwy_param_def_add_boolean(paramdef, PARAM_ENABLED_0 + i,
                                  g_strdup_printf("enabled/%u", i), g_strdup_printf("Enable %u", i),
                                  i == 0 || i == 1);
    }
    return paramdef;
}

static void
multiprofile(GwyContainer *data, GwyRunType runtype)
{
    GwyDialogOutcome outcome;
    ModuleArgs args;
    GwyAppDataId dataid;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(args.field);

    args.gmodel = gwy_graph_model_new();
    gwy_graph_model_set_units_from_data_field(args.gmodel, args.field, 1, 0, 0, 1);

    args.params = gwy_params_new_from_settings(define_module_params());
    /* The first image is always the current image; it is always enabled and always displayed. */
    dataid.datano = gwy_app_data_browser_get_number(data);
    dataid.id = id;
    gwy_params_set_image_id(args.params, PARAM_IMAGE_0, dataid);
    gwy_params_set_boolean(args.params, PARAM_ENABLED_0, TRUE);

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
                            GWY_DATA_ITEM_RANGE,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);

    gui.dialog = gwy_dialog_new(_("Multiprofile"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(0);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    dataview = gui.view = gwy_create_preview(gui.data, 0, PREVIEW_SMALL_SIZE, FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), dataview, FALSE, FALSE, 0);
    gui.selection = gwy_create_preview_vector_layer(GWY_DATA_VIEW(dataview), 0, "Axis", 1, TRUE);
    g_object_set(gui.selection, "orientation", GWY_ORIENTATION_HORIZONTAL, NULL);

    graph = gwy_graph_new(args->gmodel);
    gtk_widget_set_size_request(graph, PREVIEW_SIZE, PREVIEW_SMALL_SIZE);
    gwy_graph_enable_user_input(GWY_GRAPH(graph), FALSE);
    gtk_box_pack_start(GTK_BOX(hbox), graph, TRUE, TRUE, 0);

    hbox = gwy_hbox_new(20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), create_image_table(&gui), FALSE, FALSE, 0);

    table = gui.table = gwy_param_table_new(args->params);

    gwy_param_table_append_header(table, -1, _("Profile"));
    gwy_param_table_append_slider(table, PARAM_LINENO_FRAC);
    gwy_param_table_set_unitstr(table, PARAM_LINENO_FRAC, _("px"));
    gwy_param_table_slider_set_mapping(table, PARAM_LINENO_FRAC, GWY_SCALE_MAPPING_LINEAR);
    gwy_param_table_slider_add_alt(table, PARAM_LINENO_FRAC);

    gwy_param_table_append_slider(table, PARAM_THICKNESS);
    gwy_param_table_slider_add_alt(table, PARAM_THICKNESS);
    gwy_param_table_alt_set_field_pixel_y(table, PARAM_THICKNESS, args->field);

    gwy_param_table_append_separator(table);
    gwy_param_table_append_combo(table, PARAM_MASKING);
    gwy_param_table_append_checkbox(table, PARAM_USE_FIRST_MASK);

    gwy_param_table_append_header(table, -1, _("Output"));
    gwy_param_table_append_combo(table, PARAM_MODE);
    gwy_param_table_append_target_graph(table, PARAM_TARGET_GRAPH, args->gmodel);

    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(gui.selection, "changed", G_CALLBACK(selection_changed), &gui);
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
    GwyDataChooser *chooser;
    GtkTable *table;
    GtkWidget *label, *check, *button;
    GwyAppDataId dataid;
    GSList *group = NULL;
    gchar *s;
    guint i;

    table = GTK_TABLE(gtk_table_new(1+NARGS, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);
    gtk_container_set_border_width(GTK_CONTAINER(table), 4);

    gtk_table_attach(table, gwy_label_new_header(_("Images")), 0, 3, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_table_attach(table, gtk_label_new(_("Show")), 3, 4, 0, 1, GTK_FILL, 0, 0, 0);

    for (i = 0; i < NARGS; i++) {
        s = g_strdup_printf("%u", i+1);
        label = gtk_label_new(s);
        g_free(s);
        gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
        gtk_table_attach(table, label, 0, 1, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->enabled[i] = check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), gwy_params_get_boolean(params, PARAM_ENABLED_0 + i));
        g_object_set_data(G_OBJECT(check), "id", GUINT_TO_POINTER(i));
        /* Not showing any check box looks odd, but it needs to be always checked. */
        gtk_widget_set_sensitive(check, i);
        gtk_table_attach(table, check, 1, 2, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->image[i] = gwy_data_chooser_new_channels();
        g_object_set_data(G_OBJECT(gui->image[i]), "id", GUINT_TO_POINTER(i));
        gtk_table_attach(table, gui->image[i], 2, 3, i+1, i+2, GTK_FILL, 0, 0, 0);

        gui->display[i] = button = gtk_radio_button_new(group);
        g_object_set_data(G_OBJECT(button), "id", GUINT_TO_POINTER(i));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), !i);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
        gtk_table_attach(table, button, 3, 4, i+1, i+2, GTK_FILL, 0, 0, 0);
    }

    dataid = gwy_params_get_data_id(params, PARAM_IMAGE_0);
    gwy_data_chooser_set_active_id(GWY_DATA_CHOOSER(gui->image[0]), &dataid);

    for (i = 1; i < NARGS; i++) {
        chooser = GWY_DATA_CHOOSER(gui->image[i]);
        gwy_data_chooser_set_filter(chooser, image_filter, gui->args, NULL);
        dataid = gwy_params_get_data_id(params, PARAM_IMAGE_0 + i);
        gwy_data_chooser_set_active_id(chooser, &dataid);
        gwy_data_chooser_get_active_id(chooser, &dataid);
        gwy_params_set_image_id(params, PARAM_IMAGE_0 + i, dataid);
    }

    for (i = 0; i < NARGS; i++) {
        g_signal_connect_swapped(gui->enabled[i], "toggled", G_CALLBACK(enabled_changed), gui);
        g_signal_connect_swapped(gui->image[i], "changed", G_CALLBACK(image_selected), gui);
        g_signal_connect_swapped(gui->display[i], "toggled", G_CALLBACK(display_changed), gui);
    }

    return GTK_WIDGET(table);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    GwySIValueFormat *vf;
    guint i, j;
    gint yres, lineno;
    gboolean enabled;
    gdouble yreal, y;

    if (gui->in_update)
        return;

    gui->in_update = TRUE;
    if (id == PARAM_IMAGE_0)
        id = -1;

    i = gwy_params_get_int(params, PARAM_DISPLAY);
    if (id < 0) {
        args->field = gwy_params_get_image(params, PARAM_IMAGE_0);
        args->mask = gwy_params_get_mask(params, PARAM_IMAGE_0);

        yres = gwy_data_field_get_yres(args->field);
        yreal = gwy_data_field_get_yreal(args->field);
        gwy_param_table_slider_set_factor(table, PARAM_LINENO_FRAC, fmax(yres - 1.0, 1.0));
        gwy_param_table_slider_set_steps(table, PARAM_LINENO_FRAC, 1.0/yres, 10.0/yres);
        gwy_param_table_slider_set_digits(table, PARAM_LINENO_FRAC, 0);
        vf = gwy_data_field_get_value_format_xy(args->field, GWY_SI_UNIT_FORMAT_VFMARKUP, NULL);
        gwy_param_table_alt_set_linear(table, PARAM_LINENO_FRAC, yreal/vf->magnitude, 0.0, vf->units);
        gwy_si_unit_value_format_free(vf);
        gwy_param_table_alt_set_field_pixel_y(table, PARAM_THICKNESS, args->field);

        for (j = 1; j < NARGS; j++) {
            enabled = gwy_params_get_boolean(params, PARAM_ENABLED_0 + j);
            gtk_widget_set_sensitive(gui->image[j], enabled);
            gtk_widget_set_sensitive(gui->display[j], enabled);
            gwy_data_chooser_refilter(GWY_DATA_CHOOSER(gui->image[j]));
        }

        gwy_graph_model_set_units_from_data_field(args->gmodel, args->field, 1, 0, 0, 1);
        gwy_param_table_data_id_refilter(table, PARAM_TARGET_GRAPH);
    }

    if (id < 0 || id == PARAM_LINENO_FRAC) {
        yres = gwy_data_field_get_yres(args->field);
        lineno = GWY_ROUND(gwy_params_get_double(params, PARAM_LINENO_FRAC)*(yres - 1.0));
        lineno = CLAMP(lineno, 0, yres-1);
        y = gwy_data_field_itor(args->field, lineno + 0.5);
        gwy_selection_set_data(gui->selection, 1, &y);
    }

    if (id < 0 || id == PARAM_MASKING || id == PARAM_MODE) {
        GwyDataField *mask = args->mask;
        MultiprofMode mode = gwy_params_get_enum(params, PARAM_MODE);
        GwyMaskingType masking = gwy_params_get_masking(params, PARAM_MASKING, &mask);
        gwy_param_table_set_sensitive(table, PARAM_USE_FIRST_MASK,
                                      mode == MULTIPROF_MODE_PROFILES && masking != GWY_MASK_IGNORE);
    }

    if (id >= PARAM_ENABLED_0 && id < PARAM_ENABLED_0 + NARGS) {
        j = id - PARAM_ENABLED_0;
        enabled = gwy_params_get_boolean(params, id);
        gtk_widget_set_sensitive(gui->image[j], enabled);
        gtk_widget_set_sensitive(gui->display[j], enabled);
        /* When we disable an image also stop showing it. */
        if (j == i && !enabled) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->display[0]), TRUE);
            id = PARAM_DISPLAY;
            i = 0;
        }
    }
    if (id < 0 || (id >= PARAM_IMAGE_0 && id < PARAM_IMAGE_0 + NARGS) || id == PARAM_DISPLAY) {
        GwyDataField *field = gwy_params_get_image(params, PARAM_IMAGE_0 + i);
        gwy_container_set_object_by_name(gui->data, "/0/data", field);
        gwy_set_data_preview_size(GWY_DATA_VIEW(gui->view), PREVIEW_SMALL_SIZE);
    }

    gui->in_update = FALSE;

    if (id != PARAM_TARGET_GRAPH && id != PARAM_DISPLAY)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
dialog_response(G_GNUC_UNUSED GwyDialog *dialog, gint response, ModuleGUI *gui)
{
    if (response == GWY_RESPONSE_RESET) {
        GwyParams *params = gui->args->params;
        guint i;

        gwy_params_reset(params, PARAM_DISPLAY);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->display[gwy_params_get_int(params, PARAM_DISPLAY)]), TRUE);
        for (i = 1; i < NARGS; i++) {
            gwy_params_reset(params, PARAM_ENABLED_0 + i);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->enabled[i]),
                                         gwy_params_get_boolean(params, PARAM_ENABLED_0 + i));
            gwy_param_table_param_changed(gui->table, PARAM_ENABLED_0 + i);
        }
    }
}

static void
selection_changed(ModuleGUI *gui, G_GNUC_UNUSED gint hint, GwySelection *selection)
{
    gdouble y;

    if (gui->in_update)
        return;
    if (!gwy_selection_get_object(selection, 0, &y))
        return;
    gwy_param_table_set_double(gui->table, PARAM_LINENO_FRAC, y/gwy_data_field_get_yreal(gui->args->field));
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

static GwyDataField*
get_chosen_image(ModuleArgs *args, guint i, gboolean want_mask)
{
    GwyParams *params = args->params;

    if (!gwy_params_get_boolean(params, PARAM_ENABLED_0 + i))
        return NULL;
    return want_mask ? gwy_params_get_mask(params, PARAM_IMAGE_0 + i) : gwy_params_get_image(params, PARAM_IMAGE_0 + i);
}

static gboolean
image_filter(GwyContainer *data, gint id, gpointer user_data)
{
    ModuleArgs *args = (ModuleArgs*)user_data;
    GwyDataField *otherfield, *field = args->field;

    if (!gwy_container_gis_object(data, gwy_app_get_data_key_for_id(id), &otherfield))
        return FALSE;
    return !gwy_data_field_check_compatibility(field, otherfield, GWY_DATA_COMPATIBILITY_ALL);
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
    if (gwy_params_get_enum(args->params, PARAM_MODE) == MULTIPROF_MODE_PROFILES)
        multiprofile_do_profiles(args);
    else
        multiprofile_do_stats(args);
}

/* XXX: Duplicate code with tools/cprofile.c */
static void
extract_row_profile(GwyDataField *field, GwyDataField *mask, GwyMaskingType masking,
                    GArray *xydata, gint row, gint thickness)
{
    gint xres, yres, ifrom, ito, i, j;
    const gdouble *d, *drow, *m, *mrow;
    gdouble dx;
    GwyXY *xy;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    dx = gwy_data_field_get_dx(field);
    d = gwy_data_field_get_data_const(field);

    g_array_set_size(xydata, xres);
    xy = &g_array_index(xydata, GwyXY, 0);
    gwy_clear(xy, xres);

    ifrom = row - (thickness - 1)/2;
    ifrom = MAX(ifrom, 0);
    ito = row + thickness/2 + 1;
    ito = MIN(ito, yres);

    if (mask && masking != GWY_MASK_IGNORE)
        m = gwy_data_field_get_data_const(mask);
    else {
        m = NULL;
        for (j = 0; j < xres; j++)
            xy[j].x = ito - ifrom;
    }

    for (i = ifrom; i < ito; i++) {
        drow = d + i*xres;
        if (m) {
            mrow = m + i*xres;
            if (masking == GWY_MASK_INCLUDE) {
                for (j = 0; j < xres; j++) {
                    if (mrow[j] > 0.0) {
                        xy[j].y += drow[j];
                        xy[j].x += 1.0;
                    }
                }
            }
            else {
                for (j = 0; j < xres; j++) {
                    if (mrow[j] <= 0.0) {
                        xy[j].y += drow[j];
                        xy[j].x += 1.0;
                    }
                }
            }
        }
        else {
            for (j = 0; j < xres; j++)
                xy[j].y += drow[j];
        }
    }

    for (i = j = 0; j < xres; j++) {
        if (xy[j].x > 0.0) {
            xy[i].y = xy[j].y/xy[j].x;
            xy[i].x = dx*j;
            i++;
        }
    }
    g_array_set_size(xydata, i);
}

static void
multiprofile_do_profiles(ModuleArgs *args)
{
    gdouble lineno_frac = gwy_params_get_double(args->params, PARAM_LINENO_FRAC);
    gint thickness = gwy_params_get_int(args->params, PARAM_THICKNESS);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, NULL);
    gboolean use_first_mask = gwy_params_get_boolean(args->params, PARAM_USE_FIRST_MASK);
    GwyGraphModel *gmodel = args->gmodel;
    GwyGraphCurveModel *gcmodel;
    GwyDataField *field, *mask = NULL;
    GArray *xydata;
    gint lineno, yres;
    gchar *title;
    guint i;

    field = get_chosen_image(args, 0, FALSE);
    yres = gwy_data_field_get_yres(field);
    lineno = GWY_ROUND(lineno_frac*(yres - 1.0));
    lineno = CLAMP(lineno, 0, yres-1);

    gwy_graph_model_remove_all_curves(gmodel);

    xydata = g_array_new(FALSE, FALSE, sizeof(GwyXY));
    for (i = 0; i < NARGS; i++) {
        if (!(field = get_chosen_image(args, i, FALSE)))
            continue;

        if (masking != GWY_MASK_IGNORE)
            mask = get_chosen_image(args, use_first_mask ? 0 : i, TRUE);

        extract_row_profile(field, mask, masking, xydata, lineno, thickness);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data_interleaved(gcmodel, (gdouble*)xydata->data, xydata->len);
        title = g_strdup_printf("%u", i+1);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     "description", title,
                     NULL);
        g_free(title);

        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    g_array_free(xydata, TRUE);
}

static GwyGraphCurveModel*
add_curve(GwyGraphModel *gmodel, GwyDataLine *dline, GwyDataLine *weight,
          const GwyRGBA *colour, const gchar *description,
          GArray *xydata)
{
    GwyGraphCurveModel *gcmodel;
    const gdouble *w, *d;
    guint i, res;
    gdouble dx;
    GwyXY xy;

    res = gwy_data_line_get_res(dline);
    dx = gwy_data_line_get_dx(dline);
    d = gwy_data_line_get_data(dline);
    w = gwy_data_line_get_data(weight);

    g_array_set_size(xydata, 0);
    for (i = 0; i < res; i++) {
        if (!w[i])
            continue;

        xy.x = dx*i;
        xy.y = d[i];
        g_array_append_val(xydata, xy);
    }
    gcmodel = gwy_graph_curve_model_new();
    gwy_graph_curve_model_set_data_interleaved(gcmodel, (gdouble*)xydata->data, xydata->len);
    g_object_set(gcmodel,
                 "mode", GWY_GRAPH_CURVE_LINE,
                 "color", colour,
                 "description", description,
                 NULL);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);

    return gcmodel;
}

static void
multiprofile_do_stats(ModuleArgs *args)
{
    static const GwyRGBA upper_colour = { 1.000, 0.386, 0.380, 1.000 };
    static const GwyRGBA lower_colour = { 0.380, 0.625, 1.000, 1.000 };

    gdouble lineno_frac = gwy_params_get_double(args->params, PARAM_LINENO_FRAC);
    gint thickness = gwy_params_get_int(args->params, PARAM_THICKNESS);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, NULL);
    MultiprofMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyGraphModel *gmodel = args->gmodel;
    GwyDataField *field, *mask, *bigfield, *bigmask = NULL;
    GwyDataLine *avg, *aux1, *aux2, *weight;
    GArray *xydata;
    gint lineno, xres, yres, ifrom, ito, blockheight;
    gdouble dx;
    guint i, nimages;

    gwy_graph_model_remove_all_curves(gmodel);

    field = get_chosen_image(args, 0, FALSE);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    dx = gwy_data_field_get_dx(field);
    lineno = GWY_ROUND(lineno_frac*(yres - 1.0));
    lineno = CLAMP(lineno, 0, yres-1);

    ifrom = lineno - (thickness - 1)/2;
    ifrom = MAX(ifrom, 0);
    ito = lineno + thickness/2 + 1;
    ito = MIN(ito, yres);
    blockheight = ito - ifrom;

    nimages = 0;
    for (i = 0; i < NARGS; i++) {
        if (get_chosen_image(args, i, FALSE))
            nimages++;
    }
    g_return_if_fail(nimages);

    bigfield = gwy_data_field_new(xres, nimages*blockheight, dx*xres, 1.0, FALSE);
    if (masking != GWY_MASK_IGNORE)
        bigmask = gwy_data_field_new_alike(bigfield, FALSE);

    nimages = 0;
    for (i = 0; i < NARGS; i++) {
        if (!(field = get_chosen_image(args, i, FALSE)))
            continue;

        gwy_data_field_area_copy(field, bigfield, 0, ifrom, xres, blockheight, 0, nimages*blockheight);
        if (bigmask) {
            mask = get_chosen_image(args, i, TRUE);
            if (mask) {
                gwy_data_field_area_copy(mask, bigmask, 0, ifrom, xres, blockheight, 0, nimages*blockheight);
            }
            else {
                gwy_data_field_area_fill(bigmask, 0, nimages*blockheight, xres, blockheight,
                                         masking == GWY_MASK_INCLUDE);
            }
        }
        nimages++;
    }

    xydata = g_array_new(FALSE, FALSE, sizeof(GwyXY));
    avg = gwy_data_line_new(1, 1.0, FALSE);
    weight = gwy_data_line_new(1, 1.0, FALSE);

    gwy_data_field_get_line_stats_mask(bigfield, bigmask, masking, avg, weight,
                                       0, 0, xres, nimages*blockheight,
                                       GWY_LINE_STAT_MEAN, GWY_ORIENTATION_VERTICAL);
    add_curve(gmodel, avg, weight, gwy_graph_get_preset_color(0), _("Mean"), xydata);

    aux1 = gwy_data_line_new_alike(avg, FALSE);
    aux2 = gwy_data_line_new_alike(avg, FALSE);
    if (mode == MULTIPROF_MODE_MEAN_RMS) {
        gwy_data_field_get_line_stats_mask(bigfield, bigmask, masking, aux1, NULL,
                                           0, 0, xres, nimages*blockheight,
                                           GWY_LINE_STAT_RMS, GWY_ORIENTATION_VERTICAL);
        gwy_data_line_subtract_lines(aux2, avg, aux1);
        add_curve(gmodel, aux2, weight, &lower_colour, _("Lower"), xydata);
        gwy_data_line_sum_lines(aux2, avg, aux1);
        add_curve(gmodel, aux2, weight, &upper_colour, _("Upper"), xydata);
    }
    else {
        gwy_data_field_get_line_stats_mask(bigfield, bigmask, masking, aux1, NULL,
                                           0, 0, xres, nimages*blockheight,
                                           GWY_LINE_STAT_MINIMUM, GWY_ORIENTATION_VERTICAL);
        gwy_data_field_get_line_stats_mask(bigfield, bigmask, masking, aux2, NULL,
                                           0, 0, xres, nimages*blockheight,
                                           GWY_LINE_STAT_MAXIMUM, GWY_ORIENTATION_VERTICAL);
        add_curve(gmodel, aux1, weight, &lower_colour, _("Lower"), xydata);
        add_curve(gmodel, aux2, weight, &upper_colour, _("Upper"), xydata);
    }

    g_object_unref(avg);
    g_object_unref(aux1);
    g_object_unref(aux2);
    g_object_unref(weight);
    g_object_unref(bigfield);
    GWY_OBJECT_UNREF(bigmask);
    g_array_free(xydata, TRUE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
