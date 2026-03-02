/*
 *  $Id: volume_asciiexport.c 25254 2023-02-24 17:05:18Z yeti-dn $
 *  Copyright (C) 2018-2023 David Necas (Yeti).
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
#include <locale.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-volume.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwydgetutils.h>
#include <libgwydgets/gwycombobox.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>

#define RUN_MODES (GWY_RUN_IMMEDIATE | GWY_RUN_INTERACTIVE)

enum {
    PARAM_STYLE,
    PARAM_DECIMAL_DOT,
    PARAM_PRECISION,
};

typedef enum {
    VOLUME_EXPORT_VTK      = 0,
    VOLUME_EXPORT_ZLINES   = 1,
    VOLUME_EXPORT_LAYERS   = 2,
    VOLUME_EXPORT_MATRICES = 3,
    VOLUME_EXPORT_NTYPES,
} VolumeExportStyle;

typedef struct {
    GwyParams *params;
    gboolean need_decimal_dot_option;
    GwyBrick *brick;
    /* Cached input data properties. */
    const guchar *title;
} ModuleArgs;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static void             volume_ascii_export  (GwyContainer *data,
                                              GwyRunType mode);
static GwyDialogOutcome run_gui              (ModuleArgs *args);
static gchar*           export_brick         (gpointer user_data,
                                              gssize *data_len);
static void             destroy_brick        (gchar *data,
                                              gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports volume data in simple ASCII formats."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_asciiexport)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_asciiexport",
                             (GwyVolumeFunc)&volume_ascii_export,
                             N_("/_Basic Operations/Export _Text..."),
                             NULL,
                             RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Export volume data to a text file"));

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static const GwyEnum styles[] = {
        { N_("VTK structured grid"),           VOLUME_EXPORT_VTK,      },
        { N_("One Z-profile per line"),        VOLUME_EXPORT_ZLINES,   },
        { N_("One XY-layer per line"),         VOLUME_EXPORT_LAYERS,   },
        { N_("Blank-line separated matrices"), VOLUME_EXPORT_MATRICES, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_volume_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_STYLE, "style", _("Style"),
                              styles, G_N_ELEMENTS(styles), VOLUME_EXPORT_MATRICES);
    gwy_param_def_add_boolean(paramdef, PARAM_DECIMAL_DOT, "decimal-dot", _("Use _dot as decimal separator"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_PRECISION, "precision", _("_Precision"), 0, 16, 5);
    return paramdef;
}

static void
volume_ascii_export(GwyContainer *data, GwyRunType mode)
{
    ModuleArgs args;
    GwyDialogOutcome outcome;
    gint id;

    g_return_if_fail(mode & RUN_MODES);

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_BRICK, &args.brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(args.brick));

    args.params = gwy_params_new_from_settings(define_module_params());
    if (mode == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL)
            goto finish;
    }

    if (!gwy_container_gis_string(data, gwy_app_get_brick_title_key_for_id(id), &args.title))
        args.title = _("Volume data");

    gwy_save_auxiliary_with_callback(_("Export to Text File"), NULL, export_brick, destroy_brick, &args);

finish:
    g_object_unref(args.params);
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    gboolean needs_decimal_dot_option;
    GwyDialog *dialog;
    GwyParamTable *table;

    needs_decimal_dot_option = !gwy_strequal(gwy_get_decimal_separator(), ".");

    dialog = GWY_DIALOG(gwy_dialog_new(_("Export Text")));
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_combo(table, PARAM_STYLE);
    if (needs_decimal_dot_option)
        gwy_param_table_append_checkbox(table, PARAM_DECIMAL_DOT);
    gwy_param_table_append_slider(table, PARAM_PRECISION);
    gwy_param_table_slider_set_mapping(table, PARAM_PRECISION, GWY_SCALE_MAPPING_LINEAR);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static gchar*
export_brick(gpointer user_data, gssize *data_len)
{
    static const gchar vtk_header[] =
        "# vtk DataFile Version 2.0\n"
        "%s\n"
        "ASCII\n"
        "DATASET STRUCTURED_POINTS\n"
        "DIMENSIONS %u %u %u\n"
        "ASPECT_RATIO 1 1 1\n"
        "SPACING %g %g %g\n"
        "ORIGIN 0 0 0\n"
        "POINT_DATA %u\n"
        "SCALARS volume_scalars double 1\n"
        "LOOKUP_TABLE default\n"
        ;

    ModuleArgs *args = (ModuleArgs*)user_data;
    GwyParams *params = args->params;
    gint precision = gwy_params_get_int(params, PARAM_PRECISION);
    VolumeExportStyle style = gwy_params_get_enum(params, PARAM_STYLE);
    gboolean decimal_dot = gwy_params_get_boolean(params, PARAM_DECIMAL_DOT);
    GwyBrick *brick = args->brick;
    guint xres, yres, zres, i, j, n;
    GString *str;
    const gdouble *d, *dd;

    xres = gwy_brick_get_xres(brick);
    yres = gwy_brick_get_yres(brick);
    zres = gwy_brick_get_zres(brick);
    d = gwy_brick_get_data_const(brick);

    str = g_string_new(NULL);
    if (style == VOLUME_EXPORT_VTK) {
        n = xres*yres*zres;
        g_string_append_printf(str, vtk_header, args->title, xres, yres, zres,
                               gwy_brick_get_dx(brick), gwy_brick_get_dy(brick), gwy_brick_get_dz(brick),
                               n);
        gwy_append_doubles_to_gstring(str, d, n, precision, "\n", decimal_dot);
        g_string_append_c(str, '\n');
    }
    else if (style == VOLUME_EXPORT_ZLINES) {
        n = xres*yres;
        for (i = 0; i < n; i++) {
            dd = d + i;
            for (j = 0; j < zres; j++) {
                gwy_append_doubles_to_gstring(str, dd, 1, precision, "", decimal_dot);
                g_string_append_c(str, j == zres-1 ? '\n' : '\t');
                dd += n;
            }
        }
    }
    else if (style == VOLUME_EXPORT_LAYERS) {
        n = xres*yres;
        for (i = 0; i < zres; i++) {
            dd = d + i*n;
            gwy_append_doubles_to_gstring(str, dd, n, precision, "\t", decimal_dot);
            g_string_append_c(str, '\n');
        }
    }
    else if (style == VOLUME_EXPORT_MATRICES) {
        n = xres*yres;
        for (i = 0; i < zres; i++) {
            for (j = 0; j < yres; j++) {
                dd = d + i*n + j*xres;
                gwy_append_doubles_to_gstring(str, dd, xres, precision, "\t", decimal_dot);
                g_string_append_c(str, '\n');
            }
            g_string_append_c(str, '\n');
        }
    }
    else {
        g_assert_not_reached();
    }

    *data_len = str->len;
    return g_string_free(str, FALSE);
}

static void
destroy_brick(gchar *data, G_GNUC_UNUSED gpointer user_data)
{
    g_free(data);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
