/*
 *  $Id: anfatec.c 26135 2024-01-15 12:54:27Z yeti-dn $
 *  Copyright (C) 2010-2022 David Necas (Yeti)
 *  E-mail: yeti@gwyddion.net
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-anfatec-spm">
 *   <comment>Anfatec SPM data parameters</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0:160" value=";ANFATEC Parameterfile"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Anfatec SPM data parameters (normally accompained with an unidentifiable
 * # INT data file).
 * 0 search/160 ;ANFATEC\ Parameterfile\x0d\x0a Anfatec SPM parameters text
 * >&0 search/8 Version\x20:\x20
 * >>&0 regex [0-9.]+ version %s
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Anfatec
 * .par, .int
 * Read Curvemap
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/datafield.h>
#include <libprocess/spectra.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC ";ANFATEC Parameterfile"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION_HEADER ".txt"
#define EXTENSION_DATA ".int"

typedef struct {
    gint nabscissae;
    gchar **absnames;
    gchar **absunits;
    gdouble *absdata;
    gboolean *is_changing;
    GwyBrick *brick;
} AnfatecFVMatrix;

static gboolean         module_register           (void);
static gint             anfatec_detect            (const GwyFileDetectInfo *fileinfo,
                                                   gboolean only_name);
static gchar*           anfatec_find_parameterfile(const gchar *filename);
static GwyContainer*    anfatec_load              (const gchar *filename,
                                                   GwyRunType mode,
                                                   GError **error);
static GwyDataField*    anfatec_load_image        (GHashTable *hash,
                                                   gint id,
                                                   const gchar *dirname);
static GwyLawn*         anfatec_load_curvemap     (GHashTable *hash,
                                                   GHashTable *extra_meta,
                                                   gint id,
                                                   const gchar *dirname);
static gboolean         anfatec_try_to_find_data  (const gchar *dirname_glib,
                                                   const gchar *basename_sys,
                                                   gchar **buffer,
                                                   gsize *size);
static AnfatecFVMatrix* read_fv_matrix_file       (gchar *buffer,
                                                   gint header_cols,
                                                   gint header_rows,
                                                   GError **error);
static void             anfatec_fv_matrix_free    (AnfatecFVMatrix *fvm);
static gdouble          lawn_reduce_avg           (gint ncurves,
                                                   gint curvelength,
                                                   const gdouble *curvedata,
                                                   gpointer user_data);
static GwyContainer*    get_meta                  (GHashTable *hash);
static void             add_meta                  (gpointer hkey,
                                                   gpointer hvalue,
                                                   gpointer user_data);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Anfatec data files (two-part .txt + .int)."),
    "Yeti <yeti@gwyddion.net>",
    "0.6",
    "David Nečas (Yeti)",
    "2010",
};

GWY_MODULE_QUERY2(module_info, anfatec)

static gboolean
module_register(void)
{
    gwy_file_func_register("anfatec",
                           N_("Anfatec files (.par + .int)"),
                           (GwyFileDetectFunc)&anfatec_detect,
                           (GwyFileLoadFunc)&anfatec_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
anfatec_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    FILE *fh;
    gchar *parameterfile;
    gchar *buf;
    guint size;
    gboolean result;

    if (only_name)
        return 0;

    if (strstr(fileinfo->head, MAGIC))
        return 90;

    if (!(parameterfile = anfatec_find_parameterfile(fileinfo->name)))
        return 0;

    fh = gwy_fopen(parameterfile, "r");
    if (!fh) {
        g_free(parameterfile);
        return 0;
    }
    gwy_debug("Parameterfile opened");
    buf = g_new(gchar, GWY_FILE_DETECT_BUFFER_SIZE);
    size = fread(buf, 1, GWY_FILE_DETECT_BUFFER_SIZE, fh);
    gwy_debug("size: %u", size);
    buf[MIN(GWY_FILE_DETECT_BUFFER_SIZE-1, size)] = '\0';
    result = (strstr(buf, MAGIC) != NULL);
    gwy_debug("result: %d", result);
    fclose(fh);
    g_free(buf);
    g_free(parameterfile);

    return result ? 90 : 0;
}

static gchar*
anfatec_find_parameterfile(const gchar *filename)
{
    gchar *paramfile;
    /* 4 is the length of .int, we start with removal of that. */
    guint len, removed = 4, ntries = 3;
    gboolean removed_something;

    if (g_str_has_suffix(filename, ".txt") || g_str_has_suffix(filename, ".TXT"))
        return g_strdup(filename);

    if (g_str_has_suffix(filename, ".int") || g_str_has_suffix(filename, ".INT")) {
        gwy_debug("File name ends with .int");
        paramfile = g_strdup(filename);
        len = strlen(paramfile);

        do {
            removed_something = FALSE;

            /* Try to add .txt, both lower- and uppercase */
            strcpy(paramfile + len-removed, ".txt");
            gwy_debug("Looking for %s", paramfile);
            if (g_file_test(paramfile, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)) {
                gwy_debug("Found.");
                return paramfile;
            }
            gwy_debug("Looking for %s", paramfile);
            strcpy(paramfile + len-removed, ".TXT");
            if (g_file_test(paramfile, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)) {
                gwy_debug("Found.");
                return paramfile;
            }

            /* Remove a contiguous sequence matching [A-Z]+[a-z]*.  This means something like TopoFwd. */
            while (removed < len && g_ascii_islower(paramfile[len-removed-1])) {
                removed_something = TRUE;
                removed++;
            }
            while (removed < len && g_ascii_isupper(paramfile[len-removed-1])) {
                removed_something = TRUE;
                removed++;
            }
        } while (removed_something && removed < len && ntries--);

        gwy_debug("No matching paramter file.");
        g_free(paramfile);
    }

    return NULL;
}

static GwyContainer*
anfatec_load(const gchar *filename,
             GwyRunType mode,
             GError **error)
{
    GwyContainer *container = NULL, *meta = NULL;
    GHashTable *hash = NULL, *hash2 = NULL;
    gchar *line, *value, *key, *dirname = NULL, *text = NULL, *p;
    GError *err = NULL;
    gsize size;
    gint sectdepth, id, maxid;

    if (!g_file_get_contents(filename, &text, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (!gwy_memmem(text, MIN(size, GWY_FILE_DETECT_BUFFER_SIZE), MAGIC, MAGIC_SIZE)) {
        gchar *paramfile = anfatec_find_parameterfile(filename);

        /* If we are given data but find a suitable parameter file, recurse. */
        if (paramfile) {
            if (!gwy_strequal(paramfile, filename))
                container = anfatec_load(paramfile, mode, error);
            else {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                            _("The parameter file cannot be loaded."));
            }
            g_free(paramfile);
        }
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                        _("Cannot find the corresponding parameter file."));
        }
        g_free(text);
        return container;
    }

    /* Cannot use GwyTextHeaderParser due to unlabelled sections. */
    hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    hash2 = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    sectdepth = 0;
    id = -1;
    p = text;
    while ((line = gwy_str_next_line(&p))) {
        g_strstrip(line);
        if (!line[0] || line[0] == ';')
            continue;

        if (gwy_strequal(line, "FileDescBegin")) {
            if (sectdepth) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("FileDescBegin cannot be inside another FileDesc."));
                goto fail;
            }
            sectdepth++;
            id++;
            continue;
        }
        if (gwy_strequal(line, "FileDescEnd")) {
            if (!sectdepth) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("FileDescEnd has no corresponding FileDescBegin."));
                goto fail;
            }
            sectdepth--;
            continue;
        }

        if (!(value = strchr(line, ':'))) {
            g_warning("Cannot parse line %s", line);
            continue;
        }

        *value = '\0';
        value++;
        g_strchomp(line);
        g_strchug(value);
        if (sectdepth)
            key = g_strdup_printf("%d::%s", id, line);
        else
            key = g_strdup(line);
        gwy_debug("<%s> = <%s>", key, value);
        g_hash_table_replace(hash, key, value);
    }

    if (sectdepth) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("FileDescBegin has no corresponding FileDescEnd."));
        goto fail;
    }

    if (id == -1) {
        err_NO_DATA(error);
        goto fail;
    }

    if (!require_keys(hash, error, "xPixel", "yPixel", "XScanRange", "YScanRange", NULL))
        goto fail;

    container = gwy_container_new();
    meta = get_meta(hash);
    dirname = g_path_get_dirname(filename);
    maxid = id;
    for (id = 0; id <= maxid; id++) {
        GwyDataField *dfield;
        GwyLawn *lawn;
        GwyContainer *metacopy;
        const gchar *dataname, *title;
        gint ncurves;

        key = g_strdup_printf("%d::Caption", id);
        title = g_hash_table_lookup(hash, key);
        g_free(key);

        key = g_strdup_printf("%d::FileName", id);
        dataname = g_hash_table_lookup(hash, key);
        g_free(key);

        g_hash_table_remove_all(hash2);

        if ((lawn = anfatec_load_curvemap(hash, hash2, id, dirname))) {
            ncurves = gwy_lawn_get_n_curves(lawn);
            gwy_container_pass_object(container, gwy_app_get_lawn_key_for_id(id), lawn);

            if (meta) {
                metacopy = gwy_container_duplicate(meta);
                g_hash_table_foreach(hash2, add_meta, metacopy);
                gwy_container_pass_object(container, gwy_app_get_lawn_meta_key_for_id(id), metacopy);
            }
            if (title) {
                gwy_container_set_const_string(container, gwy_app_get_lawn_title_key_for_id(id), title);
                gwy_lawn_set_curve_label(lawn, ncurves-1, title);
            }

            dfield = gwy_data_field_new(gwy_lawn_get_xres(lawn), gwy_lawn_get_yres(lawn),
                                        gwy_lawn_get_xreal(lawn), gwy_lawn_get_yreal(lawn),
                                        FALSE);
            gwy_lawn_reduce_to_plane(lawn, dfield, lawn_reduce_avg, NULL);
            gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), gwy_lawn_get_si_unit_curve(lawn, ncurves-1));
            gwy_container_pass_object(container, gwy_app_get_lawn_preview_key_for_id(id), dfield);

            gwy_file_curve_map_import_log_add(container, id, NULL, dataname);
        }
        else if ((dfield = anfatec_load_image(hash, id, dirname))) {
            gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), dfield);
            if (meta) {
                gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(id),
                                          gwy_container_duplicate(meta));
            }
            if (title)
                gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(id), title);
            else
                gwy_app_channel_title_fall_back(container, id);

            gwy_file_channel_import_log_add(container, id, NULL, dataname);
        }
    }

    if (!gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
    }
fail:
    GWY_OBJECT_UNREF(meta);
    g_free(dirname);
    g_free(text);
    if (hash)
        g_hash_table_destroy(hash);
    if (hash2)
        g_hash_table_destroy(hash2);

    return container;
}

static GwyDataField*
anfatec_load_image(GHashTable *hash,
                   gint id,
                   const gchar *dirname)
{
    GwyDataField *dfield = NULL;
    GwySIUnit *unitx = NULL, *unity = NULL, *unitz = NULL;
    const gchar *filename;
    gint xres, yres, power10x, power10y, power10z;
    gdouble xreal, yreal, q, offset;
    gchar *buffer = NULL;
    gsize size;
    gchar *key, *value;

    xres = atoi(g_hash_table_lookup(hash, "xPixel"));
    yres = atoi(g_hash_table_lookup(hash, "yPixel"));
    if (err_DIMENSION(NULL, xres) || err_DIMENSION(NULL, yres))
        return NULL;

    /* Do not even try to load the file as an image if there is HeaderCols, i.e. it looks like a curve map.  It would
     * probably succeed because the text curve map file is large enough, but it would be utter nonsense. */
    key = g_strdup_printf("%d::HeaderCols", id);
    value = g_hash_table_lookup(hash, key);
    g_free(key);
    if (value) {
        gwy_debug("found HeaderCols, not trying to load it as an image");
        return NULL;
    }

    key = g_strdup_printf("%d::FileName", id);
    filename = g_hash_table_lookup(hash, key);
    g_free(key);
    if (!filename) {
        g_warning("Missing FileName in channel %d.", id);
        return NULL;
    }
    gwy_debug("filename: %s", filename);

    if (!anfatec_try_to_find_data(dirname, filename, &buffer, &size)) {
        g_warning("Cannot open %s.", filename);
        goto fail;
    }
    if (err_SIZE_MISMATCH(NULL, xres*yres*sizeof(gint32), size, FALSE)) {
        g_warning("File is too short %s.", filename);
        goto fail;
    }

    xreal = g_ascii_strtod(g_hash_table_lookup(hash, "XScanRange"), NULL);
    sanitise_real_size(&xreal, "x size");
    yreal = g_ascii_strtod(g_hash_table_lookup(hash, "YScanRange"), NULL);
    sanitise_real_size(&yreal, "y size");

    unitx = gwy_si_unit_new_parse(g_hash_table_lookup(hash, "XPhysUnit"), &power10x);
    unity = gwy_si_unit_new_parse(g_hash_table_lookup(hash, "YPhysUnit"), &power10y);
    if (!gwy_si_unit_equal(unitx, unity))
        g_warning("X and Y units differ, using X");

    key = g_strdup_printf("%d::PhysUnit", id);
    unitz = gwy_si_unit_new_parse(g_hash_table_lookup(hash, key), &power10z);
    g_free(key);

    dfield = gwy_data_field_new(xres, yres, xreal*pow10(power10x), yreal*pow10(power10y), FALSE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), unitx);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), unitz);
    if ((value = g_hash_table_lookup(hash, "xCenter"))) {
        offset = (g_ascii_strtod(value, NULL) - 0.5*xreal)*pow10(power10x);
        gwy_data_field_set_xoffset(dfield, offset);
    }
    if ((value = g_hash_table_lookup(hash, "yCenter"))) {
        offset = (g_ascii_strtod(value, NULL) - 0.5*yreal)*pow10(power10y);
        gwy_data_field_set_yoffset(dfield, offset);
    }

    q = pow10(power10z);
    key = g_strdup_printf("%d::Scale", id);
    if ((value = g_hash_table_lookup(hash, key)))
        q *= g_ascii_strtod(value, NULL);
    g_free(key);

    gwy_convert_raw_data(buffer, xres*yres, 1, GWY_RAW_DATA_SINT32, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), q, 0.0);

fail:
    g_free(buffer);
    GWY_OBJECT_UNREF(unitx);
    GWY_OBJECT_UNREF(unity);
    GWY_OBJECT_UNREF(unitz);

    return dfield;
}

static GwyLawn*
anfatec_load_curvemap(GHashTable *hash,
                      GHashTable *extra_meta,
                      gint id,
                      const gchar *dirname)
{
    AnfatecFVMatrix *fvm = NULL;
    GwyLawn *lawn = NULL;
    const gchar *filename;
    gchar *key, *value, *buffer = NULL;
    gsize size;
    gint header_cols, xres, yres, zres, ncurves, i, j, k, ntotalcurves;
    gint *abscissa_map = NULL;
    gdouble *b, *datablock = NULL, *q = NULL;
    gdouble qdata = 1.0;
    GRegex *regex;
    GMatchInfo *matchinfo;

    key = g_strdup_printf("%d::FileName", id);
    filename = g_hash_table_lookup(hash, key);
    g_free(key);
    if (!filename) {
        g_warning("Missing FileName in channel %d.", id);
        return NULL;
    }
    gwy_debug("filename: %s", filename);

    key = g_strdup_printf("%d::HeaderCols", id);
    value = g_hash_table_lookup(hash, key);
    g_free(key);
    if (!value)
        return NULL;
    header_cols = atoi(value);
    gwy_debug("HeaderCols: %d", header_cols);

    if (!anfatec_try_to_find_data(dirname, filename, &buffer, &size)) {
        g_warning("Cannot open %s.", filename);
        goto fail;
    }

    if (!(fvm = read_fv_matrix_file(buffer, header_cols, 2, NULL)))
        goto fail;
    GWY_FREE(buffer);

    /* Create a map for filtering out constants. */
    ntotalcurves = fvm->nabscissae;
    ncurves = 0;
    abscissa_map = g_new(gint, ntotalcurves);
    for (i = 0; i < ntotalcurves; i++) {
        abscissa_map[i] = -1;
        if (fvm->is_changing[i])
            abscissa_map[i] = ncurves++;
        else {
            key = g_strconcat("Matrix::", fvm->absnames[i], NULL);
            value = g_strdup_printf("%g %s", fvm->absdata[i], fvm->absunits[i]);
            gwy_debug("turning a constant channel to metadata <%s> = <%s>", key, value);
            g_hash_table_replace(extra_meta, key, value);
        }
    }

    xres = gwy_brick_get_xres(fvm->brick);
    yres = gwy_brick_get_yres(fvm->brick);
    zres = gwy_brick_get_zres(fvm->brick);
    lawn = gwy_lawn_new(xres, yres, gwy_brick_get_xreal(fvm->brick), gwy_brick_get_yreal(fvm->brick), ncurves+1, 0);
    gwy_lawn_set_xoffset(lawn, gwy_brick_get_xoffset(fvm->brick));
    gwy_lawn_set_yoffset(lawn, gwy_brick_get_yoffset(fvm->brick));
    gwy_si_unit_set_from_string(gwy_lawn_get_si_unit_xy(lawn), "m");
    b = gwy_brick_get_data(fvm->brick);

    q = g_new(gdouble, ntotalcurves);
    for (i = 0; i < ntotalcurves; i++) {
        if ((j = abscissa_map[i]) >= 0) {
            gwy_lawn_set_curve_label(lawn, j, fvm->absnames[i]);
            gwy_si_unit_set_from_string_parse(gwy_lawn_get_si_unit_curve(lawn, j), fvm->absunits[i], &k);
            q[i] = pow10(k);
        }
    }

    regex = g_regex_new("^.+_[0-9]+([A-Za-z]+)_Matrix\\.txt$", 0, 0, NULL);
    g_assert(regex);
    if (g_regex_match(regex, filename, 0, &matchinfo)) {
        gchar *name = g_match_info_fetch(matchinfo, 1);
        const gchar *unit = NULL;

        gwy_debug("matched data kind from file name: %s", name);
        if (gwy_stramong(name, "Phase", NULL))
            unit = "deg";
        else if (gwy_stramong(name, "Amplitude", "TB", "Force", NULL))
            unit = "mV";
        else {
            gwy_info("Unknown data kind %s, cannot guess the unit.", name);
        }
        g_free(name);

        if (unit) {
            gwy_si_unit_set_from_string_parse(gwy_lawn_get_si_unit_curve(lawn, ncurves), unit, &k);
            qdata = pow10(k);
        }
    }
    else {
        g_warning("Cannot parse Matrix file name %s.", filename);
    }
    g_match_info_free(matchinfo);
    g_regex_unref(regex);

    datablock = g_new(gdouble, zres*(ncurves + 1));
    for (k = 0; k < zres; k++) {
        for (i = 0; i < ntotalcurves; i++) {
            if ((j = abscissa_map[i]) >= 0)
                datablock[j*zres + k] = q[i]*fvm->absdata[k*ntotalcurves + i];
        }
    }
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            for (k = 0; k < zres; k++)
                datablock[zres*ncurves + k] = qdata*b[k*xres*yres + i*xres + j];
            gwy_lawn_set_curves(lawn, j, i, zres, datablock, NULL);
        }
    }

fail:
    g_free(q);
    g_free(buffer);
    g_free(datablock);
    g_free(abscissa_map);
    anfatec_fv_matrix_free(fvm);

    return lawn;
}

/* We get the directory name in GLib encoding and the basename in system encoding.  Which ensures lots of fun for the
 * long winter evenings.  */
static gboolean
anfatec_try_to_find_data(const gchar *dirname_glib,
                         const gchar *basename_sys,
                         gchar **buffer,
                         gsize *size)
{
    static const gchar *encodings[] = {
        "UTF-16", "CP1252", "CP1251", "CP1250", "CP1253", "CP1254", "CP1255", "CP1256", "CP1257", "CP1258",
    };
    guint i;
    gssize len = strlen(basename_sys);
    gchar *fullname_asis;

    /* Fingers crossed... */
    fullname_asis = g_build_filename(dirname_glib, basename_sys, NULL);
    gwy_debug("Trying as-is: <%s>", fullname_asis);
    if (g_file_get_contents(fullname_asis, buffer, size, NULL)) {
        g_free(fullname_asis);
        return TRUE;
    }

    for (i = 0; i < G_N_ELEMENTS(encodings); i++) {
        gchar *filename_utf8 = gwy_convert_to_utf8(basename_sys, len, encodings[i]);
        if (filename_utf8) {
            gchar *filename_glib = g_filename_from_utf8(filename_utf8, -1, NULL, NULL, NULL);

            g_free(filename_utf8);
            if (filename_glib) {
                gchar *fullname_glib = g_build_filename(dirname_glib, filename_glib, NULL);

                g_free(filename_glib);
                gwy_debug("Trying encoding %s: <%s>", encodings[i], fullname_glib);
                if (g_file_get_contents(fullname_glib, buffer, size, NULL)) {
                    g_free(fullname_glib);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

static gboolean
analyse_header_line(const gchar *line, gint *header_cols, gint *coord_cols)
{
    const gchar *q;
    gboolean in_header = TRUE;
    gint hc = 0, cc = 0;

    while (line) {
        q = strchr(line, '\t');
        if (q)
            q++;

        if (in_header) {
            if (g_ascii_isdigit(line[0]) || line[0] == '-' || line[0] == '+' || line[0] == '.')
                in_header = FALSE;
        }
        if (in_header)
            hc++;
        else
            cc++;

        line = q;
    }

    gwy_debug("header_cols = %d (expecting %d)", hc, *header_cols);
    gwy_debug("coord_cols = %d (expecting %d)", cc, *coord_cols);

    if (*header_cols >= 0 && *header_cols != hc)
        return FALSE;
    if (*coord_cols >= 0 && *coord_cols != cc)
        return FALSE;

    *header_cols = hc;
    *coord_cols = cc;

    return TRUE;
}

static gchar**
read_strings(gchar *line, gint n, gchar **end)
{
    gchar *q;
    gchar **strs = g_new0(gchar*, n+1);
    gint i;

    for (i = 0; i < n; i++) {
        q = strchr(line, '\t');
        if (q)
            *q = '\0';
        else if (i+1 < n) {
            g_strfreev(strs);
            return NULL;
        }
        else
            q = line + strlen(line)-1;

        strs[i] = g_strdup(line);
        gwy_debug("str[%d] = <%s>", i, strs[i]);
        line = q+1;
    }

    *end = line;
    return strs;
}

static gboolean
read_numbers(gchar *line, gdouble *values, gint n, gchar **end)
{
    gchar *q, *e;
    gint i;

    /* Unfortunately, the text data seem represented in a random locale.  This function is only used then the rest
     * of the line contains just numbers.  So just fix all commas to dots. */
    g_strdelimit(line, ",", '.');

    for (i = 0; i < n; i++) {
        q = strchr(line, '\t');
        if (q)
            *q = '\0';
        else if (i+1 < n)
            return FALSE;
        else
            q = line + strlen(line)-1;

        values[i] = g_ascii_strtod(line, &e);
        if (*e != '\0')
            return FALSE;
        line = q+1;
    }

    *end = line;
    return TRUE;
}

static AnfatecFVMatrix*
read_fv_matrix_file(gchar *buffer,
                    gint header_cols, gint header_rows,
                    GError **error)
{
    AnfatecFVMatrix *fvm = NULL;
    gchar *line, *p, *end;
    gint lineno = 0, ncols = -1, totalcols, ntoread, xres, yres, zres, i, j, k;
    gdouble *xdata = NULL, *ydata = NULL, *interleaved = NULL, *datatoread, *d, *b;
    gint *xyindex = NULL;
    GArray *data = NULL;
    gboolean ok = FALSE;
    GwyXY offsets, steps;

    if (header_rows >= 0 && header_rows != 2) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Wrong number of header rows or columns."));
        return NULL;
    }

    /* Physically read the file. */
    data = g_array_new(FALSE, FALSE, sizeof(gdouble));
    p = buffer;
    fvm = g_new0(AnfatecFVMatrix, 1);
    while ((line = gwy_str_next_line(&p))) {
        g_strstrip(line);
        if (lineno < 2) {
            if (!analyse_header_line(line, &header_cols, &ncols)) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Wrong number of header rows or columns."));
                goto fail;
            }
            if (!header_cols || !ncols) {
                err_NO_DATA(error);
                goto fail;
            }
            totalcols = header_cols + ncols;
            if (!lineno) {
                fvm->nabscissae = header_cols;
                fvm->absnames = read_strings(line, header_cols, &end);
                line = end;
                xdata = g_new(gdouble, ncols);
                ydata = g_new(gdouble, ncols);
                datatoread = xdata;
            }
            else {
                fvm->absunits = read_strings(line, header_cols, &end);
                line = end;
                datatoread = ydata;
            }
            ntoread = ncols;
        }
        else {
            g_array_set_size(data, data->len + totalcols);
            datatoread = &g_array_index(data, gdouble, data->len - totalcols);
            ntoread = totalcols;
        }
        if (!read_numbers(line, datatoread, ntoread, &end)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Cannot parse data values at line %d."), lineno+1);
            goto fail;
        }
        if (*end) {
            g_warning("Extra data at line %d.", lineno+1);
        }
        lineno++;
    }
    gwy_debug("read %d lines", lineno);
    if (lineno < 3) {
        err_NO_DATA(error);
        goto fail;
    }
    zres = lineno-2;

    /* Check if x and y form a grid. */
    interleaved = g_new(gdouble, 2*ncols);
    for (i = 0; i < ncols; i++) {
        interleaved[2*i + 0] = xdata[i];
        interleaved[2*i + 1] = ydata[i];
    }
    if (!(xyindex = gwy_check_regular_2d_grid(interleaved, 2, ncols, -1.0, &xres, &yres, &offsets, &steps))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Coordinates do not form a regular grid."));
        goto fail;
    }
    gwy_debug("xres %d, yres %d", xres, yres);

    /* Make a Brick where we put all the data in correct order and separate from abscissare.  The called with convert
     * it all to some Lawn. */
    fvm->brick = gwy_brick_new(xres, yres, zres, xres*1e-6*steps.x, yres*1e-6*steps.y, 1.0, FALSE);
    gwy_brick_set_xoffset(fvm->brick, 1e-6*offsets.x);
    gwy_brick_set_yoffset(fvm->brick, 1e-6*offsets.y);
    d = &g_array_index(data, gdouble, 0);
    b = gwy_brick_get_data(fvm->brick);
    fvm->absdata = g_new(gdouble, zres*header_cols);
    for (k = 0; k < zres; k++) {
        for (i = 0; i < header_cols; i++)
            fvm->absdata[k*header_cols + i] = d[k*totalcols + i];
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++)
                b[k*xres*yres + i*xres + j] = d[k*totalcols + header_cols + xyindex[i*xres + j]];
        }
    }

    /* Find out which abscissae do not change.  We filter them out since they just take space. */
    fvm->is_changing = g_new0(gboolean, header_cols);
    for (i = 0; i < header_cols; i++) {
        for (k = 1; k < zres; k++) {
            if (fvm->absdata[k*header_cols + i] != fvm->absdata[i]) {
                fvm->is_changing[i] = TRUE;
                break;
            }
        }
        gwy_debug("abscissa[%d] is %s", i, fvm->is_changing[i] ? "changing" : "constant");
    }

    ok = TRUE;

fail:
    if (!ok) {
        anfatec_fv_matrix_free(fvm);
        fvm = NULL;
    }
    g_free(xyindex);
    g_free(interleaved);
    g_free(xdata);
    g_free(ydata);
    if (data)
        g_array_free(data, TRUE);

    return fvm;
}

static void
anfatec_fv_matrix_free(AnfatecFVMatrix *fvm)
{
    if (!fvm)
        return;
    g_strfreev(fvm->absnames);
    g_strfreev(fvm->absunits);
    g_free(fvm->absdata);
    g_free(fvm->is_changing);
    GWY_OBJECT_UNREF(fvm->brick);
    g_free(fvm);
}

static gdouble
lawn_reduce_avg(gint ncurves, gint curvelength, const gdouble *curvedata,
                G_GNUC_UNUSED gpointer user_data)
{
    gdouble s = 0.0;
    gint i;

    if (!curvelength)
        return 0.0;

    curvedata += (ncurves - 1)*curvelength;
    for (i = 0; i < curvelength; i++)
        s += curvedata[i];
    return s/curvelength;
}

static GwyContainer*
get_meta(GHashTable *hash)
{
    GwyContainer *meta = gwy_container_new();
    g_hash_table_foreach(hash, add_meta, meta);
    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
    return NULL;
}

static void
add_meta(gpointer hkey, gpointer hvalue, gpointer user_data)
{
    GwyContainer *meta = (GwyContainer*)user_data;
    gchar *value = hvalue, *skey = hkey;

    if (!*value || !*skey || g_ascii_isdigit(skey[0]))
        return;

    gwy_container_set_const_string_by_name(meta, skey, value);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
