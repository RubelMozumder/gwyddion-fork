/*
 *  $Id: grain_stat.c 23666 2021-05-10 21:56:10Z yeti-dn $
 *  Copyright (C) 2014-2021 David Necas (Yeti), Petr Klapetek, Sven Neumann.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net, neumann@jpk.com.
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
#include <stdlib.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwygrainvaluemenu.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES GWY_RUN_INTERACTIVE

enum {
    PARAM_REPORT_STYLE,
    PARAM_EXPANDED,
};

typedef struct {
    GwyGrainValue *gvalue;
    gdouble mean;
    gdouble median;
    gdouble rms;
    gdouble q25;
    gdouble q75;
    GwySIValueFormat *vf;   /* For the treeview */
} GrainQuantityStats;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    GwyDataField *mask;
    /* Cached input image parameters. */
    gboolean same_units;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GrainQuantityStats *stats;
    GtkWidget *dialog;
    GwyParamTable *table;
    GtkWidget *treeview;
    GwyContainer *data;
} ModuleGUI;

static gboolean            module_register        (void);
static GwyParamDef*        define_module_params   (void);
static void                grain_stat             (GwyContainer *data,
                                                   GwyRunType runtype);
static GwyDialogOutcome    run_gui                (ModuleArgs *args);
static void                row_expanded_collapsed (ModuleGUI *gui);
static void                render_grain_stat      (GtkTreeViewColumn *column,
                                                   GtkCellRenderer *renderer,
                                                   GtkTreeModel *model,
                                                   GtkTreeIter *iter,
                                                   gpointer user_data);
static GrainQuantityStats* calculate_stats        (GwyDataField *field,
                                                   GwyDataField *mask);
static gdouble             calc_average           (gdouble *values,
                                                   guint n);
static gdouble             calc_rms               (const gdouble *values,
                                                   guint n,
                                                   gdouble mean);
static gdouble             calc_semicirc_average  (const gdouble *angles,
                                                   guint n);
static gdouble             calc_semicirc_rms      (const gdouble *angles,
                                                   guint n,
                                                   gdouble mean);
static gdouble             calc_semicirc_median   (gdouble *angles,
                                                   guint n,
                                                   guint *medpos);
static void                calc_semicirc_quartiles(const gdouble *angles,
                                                   guint n,
                                                   guint medpos,
                                                   gdouble *q25,
                                                   gdouble *q75);
static gchar*              format_report          (gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Calculates statistics for all grain quantities."),
    "Petr Klapetek <petr@klapetek.cz>, Sven Neumann <neumann@jpk.com>, Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek & Sven Neumann",
    "2015",
};

GWY_MODULE_QUERY2(module_info, grain_stat)

static gboolean
module_register(void)
{
    gwy_process_func_register("grain_stat",
                              (GwyProcessFunc)&grain_stat,
                              N_("/_Grains/S_tatistics..."),
                              GWY_STOCK_GRAINS_STATISTICS,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Grain property statistics"));

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
    gwy_param_def_add_report_type(paramdef, PARAM_REPORT_STYLE, "report_style", _("Save Grain Statistics"),
                                  GWY_RESULTS_EXPORT_PARAMETERS, GWY_RESULTS_REPORT_COLON);
    gwy_param_def_add_int(paramdef, PARAM_EXPANDED, "expanded", NULL, 0, G_MAXINT, 0);
    return paramdef;
}

static void
grain_stat(G_GNUC_UNUSED GwyContainer *data, GwyRunType runtype)
{
    ModuleArgs args;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_MASK_FIELD, &args.mask,
                                     0);
    g_return_if_fail(args.field && args.mask);
    args.same_units = gwy_si_unit_equal(gwy_data_field_get_si_unit_xy(args.field),
                                        gwy_data_field_get_si_unit_z(args.field));
    args.params = gwy_params_new_from_settings(define_module_params());
    run_gui(&args);
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    static const gchar *columns[] = {
        N_("Mean"), N_("Median"), N_("RMS"), N_("IQR"), N_("Units"),
    };

    GtkWidget *scwin, *treeview, *auxbox;
    GwyDialog *dialog;
    GwyParamTable *table;
    GtkTreeSelection *selection;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GwyDialogOutcome outcome;
    ModuleGUI gui;
    guint n, i;

    gui.args = args;
    gui.stats = calculate_stats(args->field, args->mask);

    gui.dialog = gwy_dialog_new(_("Grain Statistics"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_OK, 0);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 640, 640);

    scwin = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gwy_dialog_add_content(dialog, scwin, TRUE, TRUE, 0);

    treeview = gui.treeview = gwy_grain_value_tree_view_new(FALSE, "name", "symbol_markup", NULL);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), TRUE);
    gtk_container_add(GTK_CONTAINER(scwin), treeview);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
    for (i = 0; i < G_N_ELEMENTS(columns); i++) {
        column = gtk_tree_view_column_new();
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(i));
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), column);
        gtk_tree_view_column_set_title(column, columns[i]);
        gtk_tree_view_column_set_alignment(column, 0.5);
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_set_cell_data_func(column, renderer, render_grain_stat, &gui, NULL);
    }

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

    gwy_grain_value_tree_view_set_expanded_groups(GTK_TREE_VIEW(treeview),
                                                  gwy_params_get_int(args->params, PARAM_EXPANDED));

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_report(table, PARAM_REPORT_STYLE);
    gwy_param_table_report_set_formatter(table, PARAM_REPORT_STYLE, format_report, &gui, NULL);
    /* XXX: Silly.  Just want to right-align the export controls for consistency. */
    auxbox = gwy_hbox_new(0);
    gwy_dialog_add_content(dialog, auxbox, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(auxbox), gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    /* FIXME: We would like some helper for this. */
    g_signal_connect_swapped(treeview, "row-expanded", G_CALLBACK(row_expanded_collapsed), &gui);
    g_signal_connect_swapped(treeview, "row-collapsed", G_CALLBACK(row_expanded_collapsed), &gui);

    outcome = gwy_dialog_run(dialog);

    n = gwy_inventory_get_n_items(gwy_grain_values());
    for (i = 0; i < n; i++)
        gwy_si_unit_value_format_free(gui.stats[i].vf);
    g_free(gui.stats);

    return outcome;
}

static void
row_expanded_collapsed(ModuleGUI *gui)
{
    guint expanded = gwy_grain_value_tree_view_get_expanded_groups(GTK_TREE_VIEW(gui->treeview));
    gwy_params_set_int(gui->args->params, PARAM_EXPANDED, expanded);
    gwy_param_table_param_changed(gui->table, PARAM_EXPANDED);
}

static void
render_grain_stat(GtkTreeViewColumn *column, GtkCellRenderer *renderer,
                  GtkTreeModel *model, GtkTreeIter *iter,
                  gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    const GrainQuantityStats *stat;
    GwyGrainValue *gvalue = NULL;
    gdouble value;
    gchar buf[64];
    const gchar *name;
    gint i, id;

    gtk_tree_model_get(model, iter, GWY_GRAIN_VALUE_STORE_COLUMN_ITEM, &gvalue, -1);
    if (!gvalue) {
        g_object_set(renderer, "text", "", NULL);
        return;
    }

    g_object_unref(gvalue);
    if (!gui->args->same_units && (gwy_grain_value_get_flags(gvalue) & GWY_GRAIN_VALUE_SAME_UNITS)) {
        g_object_set(renderer, "text", _("N.A."), NULL);
        return;
    }

    id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "id"));

    name = gwy_resource_get_name(GWY_RESOURCE(gvalue));
    i = gwy_inventory_get_item_position(gwy_grain_values(), name);
    if (i < 0) {
        g_warning("Grain value not present in inventory.");
        g_object_set(renderer, "text", "", NULL);
        return;
    }

    stat = gui->stats + i;
    if (id == 0)
        value = stat->mean;
    else if (id == 1)
        value = stat->median;
    else if (id == 2)
        value = stat->rms;
    else if (id == 3)
        value = stat->q75 - stat->q25;
    else {
        g_object_set(renderer, "markup", stat->vf->units, NULL);
        return;
    }

    g_snprintf(buf, sizeof(buf), "%.*f", stat->vf->precision, value/stat->vf->magnitude);
    g_object_set(renderer, "markup", buf, NULL);
}

static GrainQuantityStats*
calculate_stats(GwyDataField *field, GwyDataField *mask)
{
    GwySIUnitFormatStyle style = GWY_SI_UNIT_FORMAT_VFMARKUP;
    GwyInventory *inventory;
    GwyGrainValue **gvalues;
    GrainQuantityStats *stats, *stat;
    GwyGrainQuantity quantity;
    GwySIUnit *siunitxy, *siunitz, *siunit;
    guint n, i, j, xres, yres, ngrains;
    gdouble p[3], pv[3], v;
    gint *grains;
    gint powerxy, powerz;
    gboolean is_angle;
    gdouble **values;

    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    grains = g_new0(gint, xres*yres);
    ngrains = gwy_data_field_number_grains(mask, grains);

    inventory = gwy_grain_values();
    n = gwy_inventory_get_n_items(inventory);

    gvalues = g_new(GwyGrainValue*, n);
    values = g_new(gdouble*, n);
    for (i = 0; i < n; i++) {
        gvalues[i] = gwy_inventory_get_nth_item(inventory, i);
        values[i] = g_new(gdouble, ngrains + 1);
    }

    gwy_grain_values_calculate(n, gvalues, values, field, ngrains, grains);
    g_free(grains);

    stats = g_new(GrainQuantityStats, n);
    p[0] = 25.0;
    p[1] = 50.0;
    p[2] = 75.0;
    siunit = NULL;
    for (i = 0; i < n; i++) {
        stat = stats + i;
        stat->gvalue = gvalues[i];
        is_angle = (gwy_grain_value_get_flags(stat->gvalue) & GWY_GRAIN_VALUE_IS_ANGLE);
        /* Exclude the zeroth value of the array as no-grain. */
        if (is_angle) {
            stat->mean = calc_semicirc_average(values[i]+1, ngrains);
            stat->rms = calc_semicirc_rms(values[i]+1, ngrains, stat->mean);
            stat->median = calc_semicirc_median(values[i]+1, ngrains, &j);
            calc_semicirc_quartiles(values[i]+1, ngrains, j, &stat->q25, &stat->q75);
        }
        else {
            gwy_math_percentiles(ngrains, values[i]+1, GWY_PERCENTILE_INTERPOLATION_LINEAR, 3, p, pv);
            stat->q25 = pv[0];
            stat->median = pv[1];
            stat->q75 = pv[2];
            stat->mean = calc_average(values[i]+1, ngrains);
            stat->rms = calc_rms(values[i]+1, ngrains, stat->mean);
        }

        quantity = gwy_grain_value_get_quantity(stat->gvalue);
        if (quantity == GWY_GRAIN_VALUE_PIXEL_AREA) {
            stat->vf = gwy_si_unit_value_format_new(1.0, 1, _("px<sup>2</sup>"));
            continue;
        }
        if (is_angle) {
            stat->vf = gwy_si_unit_value_format_new(G_PI/180.0, 2, _("deg"));
            continue;
        }

        siunitxy = gwy_data_field_get_si_unit_xy(field);
        siunitz = gwy_data_field_get_si_unit_z(field);
        powerxy = gwy_grain_value_get_power_xy(stat->gvalue);
        powerz = gwy_grain_value_get_power_z(stat->gvalue);
        siunit = gwy_si_unit_power_multiply(siunitxy, powerxy, siunitz, powerz, siunit);
        v = fmax(fabs(stat->mean), fabs(stat->median));
        v = fmax(v, stat->rms);
        v = fmax(v, stat->q75 - stat->q25);
        stat->vf = gwy_si_unit_get_format_with_digits(siunit, style, v, 3, NULL);
    }
    gwy_object_unref(siunit);
    g_free(values);
    g_free(gvalues);

    return stats;
}

static gdouble
calc_average(gdouble *values, guint n)
{
    return gwy_math_trimmed_mean(n, values, 0, 0);
}

static gdouble
calc_rms(const gdouble *values, guint n, gdouble mean)
{
    gdouble v, s2 = 0.0;
    guint j;

    if (n < 2)
        return 0.0;

    for (j = 0; j < n; j++) {
        v = values[j] - mean;
        s2 += v*v;
    }
    return sqrt(s2/(n - 1));
}

/* We need an average value that does not distinguishes between opposite
 * directions because all grain angular quantities are unoriented.  Do it by
 * multiplying the angles by 2. */
static gdouble
calc_semicirc_average(const gdouble *angles, guint n)
{
    gdouble sc = 0.0, ss = 0.0;
    guint j;

    for (j = 0; j < n; j++) {
        sc += cos(2.0*angles[j]);
        ss += sin(2.0*angles[j]);
    }
    return gwy_canonicalize_angle(0.5*atan2(ss, sc), FALSE, FALSE);
}

static gdouble
calc_semicirc_rms(const gdouble *angles, guint n, gdouble mean)
{
    gdouble v, s2 = 0.0;
    guint j;

    if (n < 2)
        return 0.0;

    for (j = 0; j < n; j++) {
        /* Move the difference to [-π/2,π/2] range. */
        v = gwy_canonicalize_angle(angles[j] - mean, FALSE, FALSE);
        s2 += v*v;
    }
    return sqrt(s2/(n - 1));
}

/* Find semicircular median in linear time (or close to, we also sort the
 * array).  */
static gdouble
calc_semicirc_median(gdouble *angles, guint n, guint *medpos)
{
    gdouble Sforw, Sback, v, Sbest;
    guint j, jmed, jopposite, jbest;

    /* If there is one angle it is the median.  If there are two then any of
     * them is the median. */
    if (n < 3) {
        *medpos = 0;
        return angles[0];
    }

    gwy_math_sort(n, angles);
    /* Choose one value to be a speculative median at random.  Calculate the
     * sums of distances.  Find the first angle which is closer in the opposite
     * direction.  */
    jmed = jopposite = 0;
    Sforw = Sback = 0.0;
    for (j = 1; j < n; j++) {
        v = angles[j] - angles[jmed];
        if (v >= G_PI_2) {
            jopposite = j;
            break;
        }
        Sforw += v;
    }
    while (j < n) {
        v = (G_PI + angles[jmed]) - angles[j];
        Sback += v;
        j++;
    }
    Sbest = Sforw + Sback;
    jbest = 0;

    /* Now sequentially try all the other angles.  When we move by delta
     * forward, we can recalculate Sforw and Sback and then possible advance
     * jopposite. */
    while (++jmed < n) {
        v = angles[jmed] - angles[jmed-1];
        if (jopposite > jmed) {
            Sforw -= (jopposite - jmed)*v;
            Sback += (jmed + n - jopposite)*v;
        }
        else {
            Sforw -= (jopposite + n - jmed)*v;
            Sback += (jmed - jopposite)*v;
        }

        while (TRUE) {
            v = angles[jopposite] - angles[jmed];
            if (jopposite > jmed && v < G_PI_2) {
                Sback += v - G_PI;
                Sforw += v;
                jopposite = (jopposite + 1) % n;
            }
            else if (jopposite < jmed && -v > G_PI_2) {
                Sback += v;
                Sforw += v + G_PI;
                jopposite++;
            }
            else
                break;
        }

        if (Sback + Sforw < Sbest) {
            Sbest = Sback + Sforw;
            jbest = jmed;
        }
    }

    *medpos = jbest;
    return angles[jbest];
}

static void
calc_semicirc_quartiles(const gdouble *angles, guint n, guint medpos,
                        gdouble *q25, gdouble *q75)
{
    guint j;

    if (n < 3) {
        *q25 = *q75 = angles[medpos];
        return;
    }

    j = (medpos + n + n/4 - n/2) % n;
    *q25 = angles[j];

    j = (medpos + 3*n/4 - n/2) % n;
    *q75 = angles[j];
}

static void
append_separator(GString *str, GwyResultsReportType base_type)
{
    if (base_type == GWY_RESULTS_REPORT_TABSEP)
        g_string_append_c(str, '\t');
    if (base_type == GWY_RESULTS_REPORT_CSV)
        g_string_append(str, "\",\"");
}

static void
format_value(GString *str, gdouble v, GwySIValueFormat *vf)
{
    if (vf)
        g_string_append_printf(str, "%.*f", vf->precision, v/vf->magnitude);
    else {
        gchar dbuf[64];

        g_ascii_formatd(dbuf, 64, "%g", v);
        g_string_append(str, dbuf);
    }
}

static gchar*
format_report(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    const GrainQuantityStats *stats = gui->stats;
    GwyResultsReportType report_style = gwy_params_get_report_type(gui->args->params, PARAM_REPORT_STYLE);
    GwyDataField *field = gui->args->field;
    GwyResultsReportType base_style;
    const GrainQuantityStats *stat;
    GwyGrainQuantity quantity;
    GwyGrainValueGroup idgroup = GWY_GRAIN_VALUE_GROUP_ID;
    GwySIUnit *siunit, *siunitxy, *siunitz;
    GwySIUnitFormatStyle style;
    gboolean is_angle, for_machine;
    GwySIValueFormat *vf = NULL;
    GString *str;
    const gchar *name, *s;
    gchar *padding = NULL, *unitstr = NULL;
    gint powerxy, powerz;
    gdouble v;
    guint n, i, w, maxwidth;

    for_machine = (report_style & GWY_RESULTS_REPORT_MACHINE);
    base_style = (report_style & 0xff);
    n = gwy_inventory_get_n_items(gwy_grain_values());
    str = g_string_new(NULL);
    siunit = NULL;
    style = (for_machine ? GWY_SI_UNIT_FORMAT_PLAIN : GWY_SI_UNIT_FORMAT_VFUNICODE);

    maxwidth = 0;
    if (base_style == GWY_RESULTS_REPORT_COLON) {
        for (i = 0; i < n; i++) {
            stat = stats + i;
            if (gwy_grain_value_get_group(stat->gvalue) == idgroup)
                continue;
            name = gwy_resource_get_name(GWY_RESOURCE(stat->gvalue));
            if (!for_machine)
                name = _(name);
            w = gwy_str_fixed_font_width(name);
            maxwidth = MAX(w, maxwidth);
        }
        padding = g_new(gchar, maxwidth+2);
        memset(padding, ' ', maxwidth+1);
        padding[maxwidth+1] = '\0';
    }

    for (i = 0; i < n; i++) {
        stat = stats + i;
        if (gwy_grain_value_get_group(stat->gvalue) == idgroup)
            continue;
        is_angle = (gwy_grain_value_get_flags(stat->gvalue) & GWY_GRAIN_VALUE_IS_ANGLE);
        quantity = gwy_grain_value_get_quantity(stat->gvalue);

        /* Name */
        name = gwy_resource_get_name(GWY_RESOURCE(stat->gvalue));
        if (!for_machine)
            name = _(name);

        if (base_style == GWY_RESULTS_REPORT_CSV)
            g_string_append(str, "\"");
        g_string_append(str, name);
        if (base_style == GWY_RESULTS_REPORT_COLON) {
            g_string_append(str, ": ");
            g_string_append_len(str, padding, maxwidth - gwy_str_fixed_font_width(name));
        }
        append_separator(str, base_style);

        /* Value format */
        if (!for_machine && is_angle) {
            if (vf)
                gwy_si_unit_value_format_free(vf);
            vf = gwy_si_unit_value_format_new(G_PI/180.0, 2, _("deg"));
        }
        else {
            if (quantity == GWY_GRAIN_VALUE_PIXEL_AREA) {
                gwy_object_unref(siunit);
                siunit = gwy_si_unit_new("px^2");
            }
            else {
                siunitxy = gwy_data_field_get_si_unit_xy(field);
                siunitz = gwy_data_field_get_si_unit_z(field);
                powerxy = gwy_grain_value_get_power_xy(stat->gvalue);
                powerz = gwy_grain_value_get_power_z(stat->gvalue);
                siunit = gwy_si_unit_power_multiply(siunitxy, powerxy, siunitz, powerz, siunit);
            }
            if (for_machine) {
                g_free(unitstr);
                unitstr = gwy_si_unit_get_string(siunit, style);
            }
            else {
                v = fmax(fabs(stat->mean), fabs(stat->median));
                v = fmax(v, stat->rms);
                v = fmax(v, stat->q75 - stat->q25);
                vf = gwy_si_unit_get_format_with_digits(siunit, style, v, 3, vf);
            }
        }

        format_value(str, stat->mean, vf);
        if (base_style == GWY_RESULTS_REPORT_COLON)
            g_string_append(str, " ± ");
        else
            append_separator(str, base_style);

        format_value(str, stat->rms, vf);
        if (base_style == GWY_RESULTS_REPORT_COLON)
            g_string_append(str, ", ");
        else
            append_separator(str, base_style);

        format_value(str, stat->median, vf);
        if (base_style == GWY_RESULTS_REPORT_COLON)
            g_string_append(str, " ± ");
        else
            append_separator(str, base_style);

        format_value(str, stat->q75 - stat->q25, vf);
        s = for_machine ? unitstr : vf->units;
        if (base_style == GWY_RESULTS_REPORT_COLON) {
            if (*s) {
                g_string_append(str, " ");
                g_string_append(str, s);
            }
        }
        else {
            append_separator(str, base_style);
            g_string_append(str, s);
            if (base_style == GWY_RESULTS_REPORT_CSV)
                g_string_append(str, "\"");
        }
        g_string_append(str, "\n");
    }
    gwy_object_unref(siunit);
    g_free(unitstr);
    g_free(padding);
    if (vf)
        gwy_si_unit_value_format_free(vf);

    return g_string_free(str, FALSE);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
