/*
 *  $Id: asciiexport.c 25905 2023-10-22 16:32:36Z yeti-dn $
 *  Copyright (C) 2003-2023 David Necas (Yeti).
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

/**
 * [FILE-MAGIC-USERGUIDE]
 * Text matrix of data values
 * .txt
 * Export
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Export only.
 **/

#include "config.h"
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/datafield.h>
#include <app/gwyapp.h>

#include "err.h"

#define EXTENSION ".txt"

enum {
    PARAM_ADD_COMMENT,
    PARAM_ENGLISH_COMMENT,
    PARAM_DECIMAL_DOT,
    PARAM_CONCAT_ALL,
    PARAM_PRECISION,
};

typedef struct {
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table;
} ModuleGUI;

static gboolean         module_register     (void);
static GwyParamDef*     define_module_params(void);
static gint             asciiexport_detect  (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer*    asciiexport_load    (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean         asciiexport_export  (GwyContainer *data,
                                             const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static GwyDialogOutcome run_gui             (ModuleArgs *args);
static void             param_changed       (ModuleGUI *gui,
                                             gint id);
static gboolean         export_one_channel  (GwyContainer *data,
                                             gint id,
                                             ModuleArgs *args,
                                             FILE *fh);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports data as simple ASCII matrix and reads back this format."),
    "Yeti <yeti@gwyddion.net>",
    "3.0",
    "David Nečas (Yeti)",
    "2004",
};

GWY_MODULE_QUERY2(module_info, asciiexport)

static gboolean
module_register(void)
{
    gwy_file_func_register("asciiexport",
                           N_("ASCII data matrix (.txt)"),
                           (GwyFileDetectFunc)&asciiexport_detect,
                           (GwyFileLoadFunc)&asciiexport_load,
                           NULL,
                           (GwyFileSaveFunc)&asciiexport_export);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_file_func_current());
    gwy_param_def_add_boolean(paramdef, PARAM_ADD_COMMENT, "add-comment", _("Add _informational comment header"),
                              FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_ENGLISH_COMMENT, "english-comment", _("Keep comment in English"),
                              FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_DECIMAL_DOT, "decimal-dot", _("Use _dot as decimal separator"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_CONCAT_ALL, "concat-all", _("Conca_tenate exports of all images"), FALSE);
    gwy_param_def_add_int(paramdef, PARAM_PRECISION, "precision", _("_Precision"), 0, 16, 5);

    return paramdef;
}

static inline const gchar*
to_end_of_line(const gchar *s)
{
    while (*s && *s != '\n' && *s != '\r')
        s++;
    return s;
}

static const gchar*
check_header_field(const gchar **s, const gchar **labels, guint nlabels)
{
    guint i, n;

    if (strncmp(*s, "# ", 2))
        return NULL;

    *s += 2;
    for (i = 0; i < nlabels; i++) {
        n = strlen(labels[i]);
        if (!strncmp(*s, labels[i], n)) {
            *s += n;
            if (**s == ' ') {
                const gchar *retval = *s+1;
                *s = to_end_of_line(*s + 1);
                if (!**s)
                    return NULL;
                while (**s == '\n' || **s == '\r')
                    (*s)++;
                return retval;
            }
            return NULL;
        }
    }
    return NULL;
}

/* Semi-parse the header, taking into account translated fields, but avoid memory allocations and floating point
 * value parsing for make it better suited for detection. */
static gboolean
parse_header(const gchar **s,
             const gchar **channel, const gchar **width, const gchar **height, const gchar **zunit)
{
    static const gchar *key_channel[] = {
        "Channel:", "Kanál:", "Kanal:", "Canal:", "Canal :", "Canale:", "チャネル：", "채널:", "Canal:", "Канал:",
    };
    static const gchar *key_width[] = {
        "Width:", "Šířka:", "Breite:", "Anchura:", "Largeur :", "Larghezza:", "幅:", "폭:", "Largura:", "Ширина:",
    };
    static const gchar *key_height[] = {
        "Height:", "Výška:", "Höhe:", "Altura:", "Hauteur :", "Altezza:", "高さ：", "높이:", "Высота:",
    };
    static const gchar *key_zunit[] = {
        "Value units:", "Jednotky hodnot:", "Einheiten:", "Unités :", "unità valore:", "値の単位:", "값 단위:",
        "Unidades de valor:", "Единицы измерения:",
    };

    gwy_debug("trying to parse header");
    if (!(*channel = check_header_field(s, key_channel, G_N_ELEMENTS(key_channel))))
        return FALSE;
    gwy_debug("recognised Channel");
    if (!(*width = check_header_field(s, key_width, G_N_ELEMENTS(key_width))))
        return FALSE;
    gwy_debug("recognised Width");
    if (!(*height = check_header_field(s, key_height, G_N_ELEMENTS(key_height))))
        return FALSE;
    gwy_debug("recognised Height");
    if (!(*zunit = check_header_field(s, key_zunit, G_N_ELEMENTS(key_zunit))))
        return FALSE;
    gwy_debug("recognised Value units");
    return TRUE;
}

static gint
asciiexport_detect(const GwyFileDetectInfo *fileinfo,
                   gboolean only_name)
{
    const gchar *channel, *width, *height, *zunit, *p;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    p = fileinfo->head;
    if (!parse_header(&p, &channel, &width, &height, &zunit))
        return 0;

    return 50;
}

static GwyContainer*
asciiexport_load(const gchar *filename,
                 G_GNUC_UNUSED GwyRunType mode,
                 GError **error)
{
    GwyContainer *container = NULL;
    gchar *buffer, *s, *end;
    gsize size;
    const gchar *channel, *width, *height, *zunit, *p;
    gboolean found_header = FALSE, moved;
    GwyDataField *field;
    GError *err = NULL;
    gint xres, yres, i, id, power10;
    gdouble r;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    p = buffer;
    id = 0;
    while (parse_header(&p, &channel, &width, &height, &zunit)) {
        found_header = TRUE;
        i = 0;
        /* Guess the width and height; gwy_parse_doubles() is not good enough, especially when the file may contain
         * multiple concatenated images. */
        /* Count items on the first line. */
        xres = 0;
        while (p[i] && p[i] != '\n' && p[i] != '\r') {
            while (p[i] == ' ' || p[i] == '\t')
                i++;
            moved = FALSE;
            while (p[i] && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && p[i] != '\r') {
                moved = TRUE;
                i++;
            }
            if (moved)
                xres++;
        }
        /* Count lines with data. */
        yres = 0;
        while (p[i] && p[i] != '#') {
            if (p[i] == '\n' || p[i] == '\r') {
                while (p[i] == '\n' || p[i] == '\r' || p[i] == ' ' || p[i] == '\t')
                    i++;
                yres++;
            }
            else
                i++;
        }

        if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres)) {
            GWY_OBJECT_UNREF(container);
            break;
        }

        field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);
        if (!gwy_parse_doubles(p, gwy_data_field_get_data(field), 0, &yres, &xres, NULL, error)) {
            GWY_OBJECT_UNREF(field);
            GWY_OBJECT_UNREF(container);
            break;
        }

        if (!container)
            container = gwy_container_new();

        /* Z unit. */
        s = g_strndup(zunit, to_end_of_line(zunit) - zunit);
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(field), s, &power10);
        g_free(s);
        gwy_data_field_multiply(field, pow10(power10));

        /* X and Y */
        r = g_strtod(height, &end);
        sanitise_real_size(&r, "y size");
        s = g_strndup(end, to_end_of_line(end) - end);
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(field), s, &power10);
        gwy_data_field_set_yreal(field, r*pow10(power10));
        g_free(s);

        r = g_strtod(width, &end);
        sanitise_real_size(&r, "x size");
        s = g_strndup(end, to_end_of_line(end) - end);
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(field), s, &power10);
        gwy_data_field_set_xreal(field, r*pow10(power10));
        g_free(s);

        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), field);

        /* Title. */
        s = g_strstrip(g_strndup(channel, to_end_of_line(channel) - channel));
        gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(id), s);

        id++;
        p += i;
    }

    if (!found_header) {
        err_FILE_TYPE(error, "ASCII export");
    }

    g_free(buffer);

    return container;
}

static gboolean
asciiexport_export(GwyContainer *data,
                   const gchar *filename,
                   GwyRunType mode,
                   GError **error)
{
    ModuleArgs args;
    gint i, id, *ids;
    GwyDialogOutcome outcome;
    FILE *fh = NULL;
    gboolean ok = FALSE;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id, 0);
    if (id < 0) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    args.params = gwy_params_new_from_settings(define_module_params());
    if (mode == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto fail;
        }
    }

    if (!(fh = gwy_fopen(filename, "w"))) {
        err_OPEN_WRITE(error);
        goto fail;
    }

    if (gwy_params_get_boolean(args.params, PARAM_CONCAT_ALL)) {
        ids = gwy_app_data_browser_get_data_ids(data);
        for (i = 0; ids[i] >= 0; i++) {
            if (!export_one_channel(data, ids[i], &args, fh) || gwy_fprintf(fh, "\n") < 0) {
                err_WRITE(error);
                goto fail;
            }
        }
    }
    else {
        if (!export_one_channel(data, id, &args, fh)) {
            err_WRITE(error);
            goto fail;
        }
    }

    ok = TRUE;

fail:
    if (fh)
        fclose(fh);
    if (!ok)
        g_unlink(filename);

    g_object_unref(args.params);

    return ok;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    gboolean needs_decimal_dot_option;
    GwyDialog *dialog;
    GwyParamTable *table;
    ModuleGUI gui;

    gui.args = args;
    needs_decimal_dot_option = !gwy_strequal(gwy_get_decimal_separator(), ".");

    gui.dialog = gwy_dialog_new(_("Export Text"));
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Options"));
    if (needs_decimal_dot_option)
        gwy_param_table_append_checkbox(table, PARAM_DECIMAL_DOT);
    gwy_param_table_append_checkbox(table, PARAM_ADD_COMMENT);
    gwy_param_table_append_checkbox(table, PARAM_ENGLISH_COMMENT);
    gwy_param_table_append_checkbox(table, PARAM_CONCAT_ALL);
    gwy_param_table_append_slider(table, PARAM_PRECISION);
    gwy_param_table_slider_set_mapping(table, PARAM_PRECISION, GWY_SCALE_MAPPING_LINEAR);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    GwyParams *params = gui->args->params;

    if (id < 0 || id == PARAM_ADD_COMMENT) {
        gwy_param_table_set_sensitive(gui->table, PARAM_ENGLISH_COMMENT,
                                      gwy_params_get_boolean(params, PARAM_ADD_COMMENT));
    }
}

static gboolean
export_one_channel(GwyContainer *data, gint id, ModuleArgs *args, FILE *fh)
{
    GwyDataField *field = gwy_container_get_object(data, gwy_app_get_data_key_for_id(id));
    gboolean decimal_dot = gwy_params_get_boolean(args->params, PARAM_DECIMAL_DOT);
    gint precision = gwy_params_get_int(args->params, PARAM_PRECISION);
    gboolean add_comment = gwy_params_get_boolean(args->params, PARAM_ADD_COMMENT);
    gboolean english_comment = gwy_params_get_boolean(args->params, PARAM_ENGLISH_COMMENT);
    gint xres, yres, i;
    const gdouble *d;
    GString *str;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(field), FALSE);
    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    d = gwy_data_field_get_data_const(field);

    str = g_string_new(NULL);
    if (add_comment) {
        gdouble xreal = gwy_data_field_get_xreal(field), yreal = gwy_data_field_get_yreal(field);
        GwySIUnit *units;
        GwySIValueFormat *vf = NULL;
        gdouble v;
        gchar *s;

        s = gwy_app_get_data_field_title(data, id);
        g_string_printf(str, "# %s %s\n", english_comment ? "Channel:" : _("Channel:"), s);
        g_free(s);

        vf = gwy_data_field_get_value_format_xy(field, GWY_SI_UNIT_FORMAT_VFMARKUP, vf);

        g_string_append_printf(str, "# %s ", english_comment ? "Width:" : _("Width:"));
        v = xreal/vf->magnitude;
        gwy_append_doubles_to_gstring(str, &v, 1, precision, "", decimal_dot);
        g_string_append_printf(str, " %s\n", vf->units);

        g_string_append_printf(str, "# %s ", english_comment ? "Height:" : _("Height:"));
        v = yreal/vf->magnitude;
        gwy_append_doubles_to_gstring(str, &v, 1, precision, "", decimal_dot);
        g_string_append_printf(str, " %s\n", vf->units);

        units = gwy_data_field_get_si_unit_z(field);
        s = gwy_si_unit_get_string(units, GWY_SI_UNIT_FORMAT_VFMARKUP);
        g_string_append_printf(str, "# %s %s\n", english_comment ? "Value units:" : _("Value units:"), s);
        g_free(s);

        fputs(str->str, fh);

        gwy_si_unit_value_format_free(vf);
    }

    for (i = 0; i < yres; i++) {
        g_string_truncate(str, 0);
        gwy_append_doubles_to_gstring(str, d + i*xres, xres, precision, "\t", decimal_dot);
        g_string_append_c(str, '\n');
        if (fputs(str->str, fh) == EOF) {
            g_string_free(str, TRUE);
            return FALSE;
        }
    }
    g_string_free(str, TRUE);

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
