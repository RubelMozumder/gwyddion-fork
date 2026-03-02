/*
 *  $Id: xyzexport.c 25245 2023-02-23 16:16:20Z yeti-dn $
 *  Copyright (C) 2015-2022 David Necas (Yeti).
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

/* NB: Magic is in rawxyz. */

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
#include <libprocess/gwyprocesstypes.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>

#include "err.h"

#define EXTENSION ".xyz"

enum {
    PARAM_ADD_COMMENT,
    PARAM_DECIMAL_DOT,
    PARAM_PRECISION,
    PARAM_MASKING,

    INFO_CHANNEL,
};

typedef struct {
    GwyParams *params;
    GwyAppPage pageno;
    gboolean have_mask;
} ModuleArgs;

static gboolean         module_register            (void);
static gint             xyzexport_detect           (const GwyFileDetectInfo *fileinfo,
                                                    gboolean only_name);
static gboolean         xyzexport_export           (GwyContainer *data,
                                                    const gchar *filename,
                                                    GwyRunType mode,
                                                    GError **error);
static GwyDialogOutcome run_gui                    (ModuleArgs *args,
                                                    const gchar *title);
static gboolean         xyzexport_export_data_field(GwyDataField *dfield,
                                                    GwyDataField *mfield,
                                                    FILE *fh,
                                                    ModuleArgs *args);
static gboolean         xyzexport_export_surface   (GwySurface *surface,
                                                    FILE *fh,
                                                    ModuleArgs *args);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports data as simple XYZ text file."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti)",
    "2015",
};

GWY_MODULE_QUERY2(module_info, xyzexport)

static gboolean
module_register(void)
{
    gwy_file_func_register("xyzexport",
                           N_("XYZ text data (.xyz)"),
                           (GwyFileDetectFunc)&xyzexport_detect,
                           NULL,
                           NULL,
                           (GwyFileSaveFunc)&xyzexport_export);

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
    gwy_param_def_add_boolean(paramdef, PARAM_DECIMAL_DOT, "decimal-dot", _("Use _dot as decimal separator"), TRUE);
    gwy_param_def_add_int(paramdef, PARAM_PRECISION, "precision", _("_Precision"), 0, 16, 5);
    gwy_param_def_add_enum(paramdef, PARAM_MASKING, "Masking", NULL, GWY_TYPE_MASKING_TYPE, GWY_MASK_IGNORE);

    return paramdef;
}

static gint
xyzexport_detect(const GwyFileDetectInfo *fileinfo,
                 G_GNUC_UNUSED gboolean only_name)
{
    return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;
}

static gboolean
xyzexport_export(G_GNUC_UNUSED GwyContainer *data,
                 const gchar *filename,
                 GwyRunType mode,
                 GError **error)
{
    ModuleArgs args;
    GwyDataField *dfield, *mfield;
    GwySurface *surface;
    gint fid, sid;
    const guchar *title = NULL;
    GwyDialogOutcome outcome;
    FILE *fh = NULL;
    gboolean ok = FALSE;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_DATA_FIELD_ID, &fid,
                                     GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_SURFACE, &surface,
                                     GWY_APP_SURFACE_ID, &sid,
                                     GWY_APP_PAGE, &args.pageno,
                                     0);

    /* Ensure at most one is set.  We produce an error if no exportable data type is available or both types are
     * available but neither is active. When only one is available or one is active we assume that is what the user
     * wants to export. */
    if (dfield && surface) {
        if (args.pageno != GWY_PAGE_CHANNELS)
            dfield = NULL;
        if (args.pageno != GWY_PAGE_XYZS)
            surface = NULL;
    }
    if (!dfield && !surface) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    if (dfield) {
        args.pageno = GWY_PAGE_CHANNELS;
    }
    if (surface) {
        args.pageno = GWY_PAGE_XYZS;
        mfield = NULL;
    }

    args.params = gwy_params_new_from_settings(define_module_params());

    if (dfield)
        gwy_container_gis_string(data, gwy_app_get_data_title_key_for_id(fid), &title);
    if (surface)
        gwy_container_gis_string(data, gwy_app_get_surface_title_key_for_id(sid), &title);

    if (!title)
        title = _("Untitled");

    if (mode == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, title);
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

    if (gwy_params_get_boolean(args.params, PARAM_ADD_COMMENT)) {
        GwySIUnit *xyunit = NULL, *zunit = NULL;
        gchar *s;

        if (args.pageno == GWY_PAGE_CHANNELS) {
            gwy_fprintf(fh, "# %s %s\n", _("Channel:"), title);
            xyunit = gwy_data_field_get_si_unit_xy(dfield);
            zunit = gwy_data_field_get_si_unit_z(dfield);
        }
        else if (args.pageno == GWY_PAGE_XYZS) {
            gwy_fprintf(fh, "# %s %s\n", _("XYZ data:"), title);
            xyunit = gwy_surface_get_si_unit_xy(surface);
            zunit = gwy_surface_get_si_unit_z(surface);
        }

        s = gwy_si_unit_get_string(xyunit, GWY_SI_UNIT_FORMAT_VFMARKUP);
        gwy_fprintf(fh, "# %s %s\n", _("Lateral units:"), s);
        g_free(s);

        s = gwy_si_unit_get_string(zunit, GWY_SI_UNIT_FORMAT_VFMARKUP);
        gwy_fprintf(fh, "# %s %s\n", _("Value units:"), s);
        g_free(s);
    }

    if (args.pageno == GWY_PAGE_CHANNELS) {
        if (!xyzexport_export_data_field(dfield, mfield, fh, &args)) {
            err_WRITE(error);
            goto fail;
        }
    }
    else if (args.pageno == GWY_PAGE_XYZS) {
        if (!xyzexport_export_surface(surface, fh, &args)) {
            err_WRITE(error);
            goto fail;
        }
    }

    ok = TRUE;

fail:
    g_object_unref(args.params);
    if (fh)
        fclose(fh);
    if (!ok)
        g_unlink(filename);

    return ok;
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, const gchar *title)
{
    gboolean needs_decimal_dot_option;
    GwyDialog *dialog;
    GwyParamTable *table;
    gchar *desc = NULL;

    needs_decimal_dot_option = !gwy_strequal(gwy_get_decimal_separator(), ".");

    dialog = GWY_DIALOG(gwy_dialog_new(_("Export XYZ")));
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    if (args->pageno == GWY_PAGE_CHANNELS)
        desc = _("Channel");
    else if (args->pageno == GWY_PAGE_XYZS)
        desc = _("XYZ data");

    table = gwy_param_table_new(args->params);
    gwy_param_table_append_info(table, INFO_CHANNEL, desc);
    gwy_param_table_info_set_valuestr(table, INFO_CHANNEL, title);
    gwy_param_table_append_header(table, -1, _("Options"));
    if (needs_decimal_dot_option)
        gwy_param_table_append_checkbox(table, PARAM_DECIMAL_DOT);
    gwy_param_table_append_checkbox(table, PARAM_ADD_COMMENT);
    gwy_param_table_append_slider(table, PARAM_PRECISION);
    gwy_param_table_slider_set_mapping(table, PARAM_PRECISION, GWY_SCALE_MAPPING_LINEAR);
    if (args->have_mask)
        gwy_param_table_append_radio(table, PARAM_MASKING);
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), FALSE, FALSE, 0);
    gwy_dialog_add_param_table(dialog, table);

    return gwy_dialog_run(dialog);
}

static gboolean
xyzexport_export_data_field(GwyDataField *dfield, GwyDataField *mfield,
                            FILE *fh, ModuleArgs *args)
{
    gboolean decimal_dot = gwy_params_get_boolean(args->params, PARAM_DECIMAL_DOT);
    gint precision = gwy_params_get_int(args->params, PARAM_PRECISION);
    GwyMaskingType masking = gwy_params_get_masking(args->params, PARAM_MASKING, &mfield);
    gdouble dx, dy, xoff, yoff;
    const gdouble *d, *m;
    gint xres, yres, i, j;
    gdouble xyz[3];
    GString *str;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    xoff = gwy_data_field_get_xoffset(dfield);
    yoff = gwy_data_field_get_yoffset(dfield);

    d = gwy_data_field_get_data_const(dfield);
    m = mfield ? gwy_data_field_get_data_const(mfield) : NULL;

    str = g_string_new(NULL);
    for (i = 0; i < yres; i++) {
        xyz[1] = dy*(i + 0.5) + yoff;
        for (j = 0; j < xres; j++) {
            if (m) {
                if (masking == GWY_MASK_EXCLUDE && m[i*xres + j] > 0.5)
                    continue;
                if (masking == GWY_MASK_INCLUDE && m[i*xres + j] < 0.5)
                    continue;
            }

            xyz[0] = dx*(j + 0.5) + xoff;
            xyz[2] = d[i*xres + j];

            g_string_truncate(str, 0);
            gwy_append_doubles_to_gstring(str, xyz, 3, precision, "\t", decimal_dot);
            g_string_append_c(str, '\n');
            if (fputs(str->str, fh) == EOF) {
                g_string_free(str, TRUE);
                return FALSE;
            }
        }
    }
    g_string_free(str, TRUE);

    return TRUE;
}

static gboolean
xyzexport_export_surface(GwySurface *surface,
                         FILE *fh, ModuleArgs *args)
{
    gboolean decimal_dot = gwy_params_get_boolean(args->params, PARAM_DECIMAL_DOT);
    gint precision = gwy_params_get_int(args->params, PARAM_PRECISION);
    const GwyXYZ *data = gwy_surface_get_data(surface);
    guint i, n = gwy_surface_get_npoints(surface);
    GString *str;

    str = g_string_new(NULL);
    for (i = 0; i < n; i++) {
        g_string_truncate(str, 0);
        gwy_append_doubles_to_gstring(str, (gdouble*)(data + i), 3, precision, "\t", decimal_dot);
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
