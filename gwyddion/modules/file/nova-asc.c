/*
 *  $Id: nova-asc.c 25636 2023-09-07 12:19:33Z yeti-dn $
 *  Copyright (C) 2022 David Necas (Yeti).
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

/* Files may be created by NT-MDT Nova, maybe by something else.  The format is quite similar to nova-asc, but the
 * header lines do not start with # and the fields are named differently. */
/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-nova-asc">
 *   <comment>Nova ASCII data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="File Format = ASCII\r\n"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Nova ASCII data
 * 0 string File\ Format\ =\ ASCII\r\n Nova ASCII export SPM text data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Nova ASCII
 * .txt
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyversion.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC "File Format = ASCII"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define MAGIC2 "Created by "
#define MAGIC2_SIZE (sizeof(MAGIC2)-1)
#define EXTENSION ".txt"

#define Nanometer (1e-9)

static gboolean      module_register(void);
static gint          nova_detect    (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* nova_load      (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static GwyContainer* read_image_data(GHashTable *hash,
                                     gchar *p,
                                     const gchar *filename,
                                     GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Nova ASC files."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2022",
};

GWY_MODULE_QUERY2(module_info, nova_asc)

static gboolean
module_register(void)
{
    gwy_file_func_register("nova-asc",
                           N_("Nova ASCII files (.txt)"),
                           (GwyFileDetectFunc)&nova_detect,
                           (GwyFileLoadFunc)&nova_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
nova_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    gint seplen;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (fileinfo->file_size < MAGIC_SIZE + MAGIC2_SIZE + 4 || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    if (fileinfo->head[MAGIC_SIZE] == '\r')
        seplen = (fileinfo->head[MAGIC_SIZE+1] == '\n' ? 2 : 1);
    else if (fileinfo->head[MAGIC_SIZE] == '\n')
        seplen = 1;
    else
        return 0;

    /* Return nonzero score for files with matching first line, but a high score only for files with matching second
     * line. */
    return memcmp(fileinfo->head + MAGIC_SIZE + seplen, MAGIC2, MAGIC2_SIZE) ? 50 : 95;
}

static gboolean
header_error(G_GNUC_UNUSED const GwyTextHeaderContext *context,
             GError *error,
             G_GNUC_UNUSED gpointer user_data)
{
    return error->code == GWY_TEXT_HEADER_ERROR_TERMINATOR;
}

static void
header_end(G_GNUC_UNUSED const GwyTextHeaderContext *context,
           gsize length,
           gpointer user_data)
{
    gchar **pp = (gchar**)user_data;

    *pp += length;
}

static GwyContainer*
nova_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *container = NULL;
    GwyTextHeaderParser parser;
    gchar *p, *line, *buffer = NULL;
    GHashTable *hash = NULL;
    gsize size;
    GError *err = NULL;

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    p = buffer;
    line = gwy_str_next_line(&p);
    if (!gwy_strequal(line, MAGIC)) {
        err_FILE_TYPE(error, "Nova ASCII data");
        goto fail;
    }

    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    parser.terminator = "Start of Data :";
    parser.error = &header_error;
    parser.end = &header_end;
    if (!(hash = gwy_text_header_parse(p, &parser, &p, &err))) {
        g_propagate_error(error, err);
        goto fail;
    }
    if (require_keys(hash, NULL, "NX", "NY", "Scale X", "Scale Y", "Unit X", "Unit Data", NULL))
        container = read_image_data(hash, p, filename, error);
    else
        err_NO_DATA(error);

fail:
    g_free(buffer);
    if (hash)
        g_hash_table_destroy(hash);

    return container;
}

static GwyContainer*
read_image_data(GHashTable *hash, gchar *p, const gchar *filename,
                GError **error)
{
    GwyContainer *container = NULL;
    GwyDataField *field = NULL;
    gchar *value;
    gdouble xreal, yreal, q;
    gint i, xres, yres, power10;
    gdouble *data;

    xres = atoi(g_hash_table_lookup(hash, "NX"));
    yres = atoi(g_hash_table_lookup(hash, "NY"));
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return NULL;

    field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);

    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(field),
                                      g_hash_table_lookup(hash, "Unit X"), &power10);
    /* FIXME: We cannot have completely different units, but could still handle Unit X being nm and Unit Y µm. */
    q = pow10(power10);
    xreal = q*xres*g_ascii_strtod(g_hash_table_lookup(hash, "Scale X"), NULL);
    yreal = q*yres*g_ascii_strtod(g_hash_table_lookup(hash, "Scale Y"), NULL);
    sanitise_real_size(&xreal, "x size");
    sanitise_real_size(&yreal, "y size");
    gwy_data_field_set_xreal(field, xreal);
    gwy_data_field_set_yreal(field, yreal);

    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(field),
                                      g_hash_table_lookup(hash, "Unit Data"), &power10);
    q = pow10(power10);

    /* FIXME: There is a field Scale Data and DataScaleNeeded, which is normally no.  When it is yes, should we
     * rescale data according to Scale Data? */

    data = gwy_data_field_get_data(field);
    value = p;
    for (i = 0; i < xres*yres; i++) {
        data[i] = q*g_ascii_strtod(value, &p);
        if (p == value && (!*p || g_ascii_isspace(*p))) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("End of file reached when reading sample #%d of %d"), i, xres*yres);
            g_object_unref(field);
            return NULL;
        }
        if (p == value) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Malformed data encountered when reading sample #%d of %d"), i, xres*yres);
            g_object_unref(field);
            return NULL;
        }
        value = p;
    }

    container = gwy_container_new();
    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(0), field);
    gwy_app_channel_title_fall_back(container, 0);
    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
