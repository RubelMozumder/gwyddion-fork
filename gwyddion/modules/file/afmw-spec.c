/*
 *  $Id: afmw-spec.c 25675 2023-09-18 05:41:59Z yeti-dn $
 *  Copyright (C) 2018 David Necas (Yeti).
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-afm-workshop-spectra">
 *   <comment>AFM Workshop spectroscopy data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="Force-Distance Curve">
 *       <match type="string" offset="20:24" value="File format:\t">
 *         <match type="string" offset="30:48" value="Date:\t"/>
 *       </match>
 *     </match>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * AFM Workshop spectroscopy
 * .csv
 * SPS
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define MAGIC1 "Force-Distance Curve"
#define MAGIC1_SIZE (sizeof(MAGIC1)-1)

typedef struct {
    gchar *name;       /* Extend Z-Sense, Retract T-B, ... */
    GwySIUnit *unit;   /* unit */
    gdouble magnitude; /* factor for data conversion */
} AFMWColumn;

typedef struct {
    gdouble x;
    gdouble y;
    guint ncolumns;
    guint nrows;
    AFMWColumn *columns;
    gdouble *data;
} AFMWSingleFile;

typedef struct {
    gchar *filename;
    gint id;
    GTimeVal datetime;
} AFMWFileInfo;

typedef struct {
    GwySpectra **spectra;
    AFMWSingleFile *template;
} AFMWSpectraSet;

static gboolean        module_register          (void);
static gint            afmw_detect              (const GwyFileDetectInfo *fileinfo,
                                                 gboolean only_name);
static GwyContainer*   afmw_load                (const gchar *filename,
                                                 GwyRunType mode,
                                                 GError **error);
static gboolean        check_compatibility      (const AFMWSingleFile *afmwfile,
                                                 const AFMWSingleFile *template);
static void            add_curves_to_spectra_set(AFMWSpectraSet *specset,
                                                 AFMWSingleFile *afmwfile);
static AFMWSingleFile* read_one_afmw_file       (const gchar *filename,
                                                 GError **error);
static gchar**         find_all_file_names      (const gchar *filename);
static void            afmw_single_file_free    (AFMWSingleFile *afmwfile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports AFM Workshop spectrum files."),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, afmw_spec)

static gboolean
module_register(void)
{
    gwy_file_func_register("afmw_spec",
                           N_("AFM Workshop spectrum files (.csv)"),
                           (GwyFileDetectFunc)&afmw_detect,
                           (GwyFileLoadFunc)&afmw_load,
                           NULL,
                           NULL);

    return TRUE;
}

static const guchar*
find_field_in_head(const GwyFileDetectInfo *fileinfo,
                   const guchar *p,
                   const gchar *s)
{
    p = gwy_memmem(p, fileinfo->buffer_len - (p - fileinfo->head), s, strlen(s));
    if (p && (p == fileinfo->head || *(p-1) == '\r' || *(p-1) == '\n'))
        return p;
    return NULL;
}

static gint
afmw_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    const guchar *p;

    if (only_name)
        return 0;

    p = fileinfo->head;
    if (memcmp(p, MAGIC1, MAGIC1_SIZE) != 0)
        return 0;
    p += MAGIC1_SIZE;

    if (*p != '\r' && *p != '\n')
        return 0;
    while (*p == '\r' || *p == '\n')
        p++;

    if (find_field_in_head(fileinfo, p, "File Format:\t")
        && find_field_in_head(fileinfo, p, "Date:\t")
        && find_field_in_head(fileinfo, p, "Time:\t")
        && find_field_in_head(fileinfo, p, "Mode:\t")
        && find_field_in_head(fileinfo, p, "Point:\t"))
        return 90;

    return 0;
}

static GwyContainer*
afmw_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *container = NULL;
    gchar **filenames;
    AFMWSpectraSet specset;
    AFMWSingleFile *afmwfile;
    GQuark quark;
    guint i, j;

    gwy_clear(&specset, 1);
    filenames = find_all_file_names(filename);
    /* When we cannot enumerate files, just create a list containing the single file name we were given explicitly. */
    if (!filenames) {
        filenames = g_new0(gchar*, 2);
        filenames[0] = g_strdup(filename);
    }

    /* Use the file the user selected as the template. */
    if (!(specset.template = read_one_afmw_file(filename, error)))
        goto fail;

    for (i = 0; filenames[i]; i++) {
        if (!(afmwfile = read_one_afmw_file(filenames[i], error))) {
            g_warning("Cannot read associated file %s.", filenames[i]);
            continue;
        }

        if (check_compatibility(afmwfile, specset.template))
            add_curves_to_spectra_set(&specset, afmwfile);
        /* Simply skip incompatible files. */
        afmw_single_file_free(afmwfile);
    }

    for (i = j = 0; i < specset.template->ncolumns; i++) {
        if (!specset.spectra[i])
            continue;

        if (!container)
            container = gwy_container_new();
        quark = gwy_app_get_spectra_key_for_id(j);
        gwy_container_set_object(container, quark, specset.spectra[i]);
        j++;
    }

    if (!container)
        err_NO_DATA(error);

fail:
    if (specset.template) {
        if (specset.spectra) {
            for (i = 0; i < specset.template->ncolumns; i++)
                GWY_OBJECT_UNREF(specset.spectra[i]);
            g_free(specset.spectra);
        }
        afmw_single_file_free(specset.template);
    }
    g_strfreev(filenames);

    return container;
}

static gboolean
check_compatibility(const AFMWSingleFile *afmwfile,
                    const AFMWSingleFile *template)
{
    const AFMWColumn *datcolumn, *tmplcolumn;
    guint i;

    if (afmwfile->ncolumns != template->ncolumns)
        return FALSE;
    for (i = 0; i < template->ncolumns; i++) {
        datcolumn = afmwfile->columns + i;
        tmplcolumn = template->columns + i;
        if (!gwy_strequal(datcolumn->name, tmplcolumn->name) || !gwy_si_unit_equal(datcolumn->unit, tmplcolumn->unit))
            return FALSE;
    }
    return TRUE;
}

static inline void
strip_space_back(gchar *end, gchar *beg)
{
    do {
        *end = '\0';
        end--;
    } while (end >= beg && g_ascii_isspace(*end));
}

static void
parse_column_header(gchar *colname, AFMWColumn *column)
{
    gchar *p, *q, *unit;
    gint power10;

    column->name = colname;
    column->unit = NULL;
    column->magnitude = 1;

    if ((p = strchr(colname, '('))) {
        if ((q = strchr(p+1, ')'))) {
            unit = p+1;
            strip_space_back(p, colname);
            *q = '\0';
            column->unit = gwy_si_unit_new_parse(unit, &power10);
            column->magnitude = pow10(power10);
        }
        else {
            g_warning("Column header %s has only opening (.", colname);
        }
    }
    if (!column->unit)
        column->unit = gwy_si_unit_new(NULL);
}

static void
add_curves_to_spectra_set(AFMWSpectraSet *specset,
                          AFMWSingleFile *afmwfile)
{
    guint i, j, ncolumns, nrows;
    gdouble real, off;
    gboolean reversed;
    gdouble *data = afmwfile->data, *d;
    GwyDataLine *dline;
    GwySpectra *spec;
    const AFMWColumn *abscissa = NULL, *ordinate;

    ncolumns = afmwfile->ncolumns;
    nrows = afmwfile->nrows;
    /* Columns that are not spectra values remain NULL later. */
    if (!specset->spectra)
        specset->spectra = g_new0(GwySpectra*, ncolumns);

    reversed = FALSE;
    for (i = 0; i < ncolumns; i++) {
        /* Check if the column is Z-Sense, i.e. abscissa.  */
        if (gwy_stramong(afmwfile->columns[i].name, "Extend Z-Sense", "Retract Z-Sense", NULL)) {
            abscissa = afmwfile->columns + i;
            real = data[i + ncolumns*(nrows - 1)];
            off = data[i];
            if (real < off) {
                GWY_SWAP(gdouble, real, off);
                reversed = TRUE;
            }
            real -= off;
            continue;
        }
        /* Otherwise it must be ordinate. */
        ordinate = afmwfile->columns + i;
        if (!abscissa) {
            g_warning("Ordinate column %s found before any abscissa.", ordinate->name);
            continue;
        }

        dline = gwy_data_line_new(nrows, real, FALSE);
        gwy_data_line_set_offset(dline, off);
        gwy_si_unit_assign(gwy_data_line_get_si_unit_x(dline), abscissa->unit);
        gwy_si_unit_assign(gwy_data_line_get_si_unit_y(dline), ordinate->unit);
        d = gwy_data_line_get_data(dline);
        if (reversed) {
            for (j = 0; j < nrows; j++)
                d[nrows-1 - j] = data[i + j*ncolumns];
        }
        else {
            for (j = 0; j < nrows; j++)
                d[j] = data[i + j*ncolumns];
        }

        if (!(spec = specset->spectra[i])) {
            spec = specset->spectra[i] = gwy_spectra_new();
            gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spec), "m");
            gwy_spectra_set_title(spec, ordinate->name);
            gwy_spectra_set_spectrum_x_label(spec, abscissa->name);
            gwy_spectra_set_spectrum_y_label(spec, ordinate->name);
        }

        gwy_spectra_add_spectrum(specset->spectra[i], dline, afmwfile->x, afmwfile->y);
        g_object_unref(dline);
    }
}

static AFMWSingleFile*
read_one_afmw_file(const gchar *filename, GError **error)
{
    AFMWSingleFile *afmwfile;
    GError *err = NULL;
    gsize size;
    gchar *buf, *line, *p, *sep, **colnames;
    gint nrows, ncols;
    guint i, j;

    gwy_debug("reading %s", filename);
    if (!g_file_get_contents(filename, &buf, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    afmwfile = g_new0(AFMWSingleFile, 1);

    p = buf;
    if (memcmp(p, MAGIC1, MAGIC1_SIZE) != 0) {
        err_FILE_TYPE(error, "AFM Workshop SPM");
        goto fail;
    }

    /* Header */
    while ((line = gwy_str_next_line(&p)) && *line)
        ;

    /* Info */
    while ((line = gwy_str_next_line(&p)) && *line) {
        sep = strchr(line, '\t');
        if (!sep || sep == line)
            continue;
        *sep = '\0';
        if (*(sep-1) == ':')
            *(sep-1) = '\0';
        sep++;
        if (g_str_has_prefix(line, "X, ") || g_str_has_prefix(line, "Y, ")) {
            gint power10;
            GwySIUnit *unit = gwy_si_unit_new_parse(line + 3, &power10);
            gdouble v = pow10(power10) * g_ascii_strtod(sep, NULL);

            if (line[0] == 'Y') {
                afmwfile->y = v;
                gwy_debug("y %g", afmwfile->y);
            }
            else {
                afmwfile->x = v;
                gwy_debug("x %g", afmwfile->x);
            }
            g_object_unref(unit);
        }
        /* We do not care about the other fields because we cannot do anything meaningful with them. */
    }

    /* Data */
    if (!(line = gwy_str_next_line(&p)) || !*line) {
        err_NO_DATA(error);
        goto fail;
    }
    gwy_debug("column headers %s", line);
    colnames = g_strsplit(line, ",", 0);
    afmwfile->ncolumns = g_strv_length(colnames);
    gwy_debug("ncols %u", afmwfile->ncolumns);
    if (!afmwfile->ncolumns) {
        err_NO_DATA(error);
        g_free(colnames);
        goto fail;
    }
    afmwfile->columns = g_new0(AFMWColumn, afmwfile->ncolumns);
    for (i = 0; i < afmwfile->ncolumns; i++)
        parse_column_header(colnames[i], afmwfile->columns + i);
    /* Individual strings are eaten by parse_column_header(). */
    g_free(colnames);

    g_strdelimit(p, ",", '\t');
    ncols = afmwfile->ncolumns;
    nrows = -1;
    if (!(afmwfile->data = gwy_parse_doubles(p, NULL, 0, &nrows, &ncols, NULL, &err))) {
        err_PARSE_DOUBLES(error, &err);
        goto fail;
    }
    afmwfile->nrows = nrows;
    gwy_debug("nrows %u", afmwfile->nrows);
    for (i = 0; i < afmwfile->nrows; i++) {
        for (j = 0; j < afmwfile->ncolumns; j++)
            afmwfile->data[i*ncols + j] *= afmwfile->columns[j].magnitude;
    }

    g_free(buf);

    return afmwfile;

fail:
    g_free(buf);
    if (afmwfile->columns) {
        for (i = 0; i < afmwfile->ncolumns; i++) {
            g_free(afmwfile->columns[i].name);
            GWY_OBJECT_UNREF(afmwfile->columns[i].unit);
        }
    }
    g_free(afmwfile);

    return NULL;
}

static void
afmw_single_file_free(AFMWSingleFile *afmwfile)
{
    guint i;

    for (i = 0; i < afmwfile->ncolumns; i++) {
        g_free(afmwfile->columns[i].name);
        GWY_OBJECT_UNREF(afmwfile->columns[i].unit);
    }
    g_free(afmwfile->columns);
    g_free(afmwfile->data);
    g_free(afmwfile);
}

static gint
compare_fileinfos(gconstpointer pa, gconstpointer pb)
{
    const AFMWFileInfo *afi = (const AFMWFileInfo*)pa;
    const AFMWFileInfo *bfi = (const AFMWFileInfo*)pb;

    if (afi->datetime.tv_sec < bfi->datetime.tv_sec)
        return -1;
    if (afi->datetime.tv_sec > bfi->datetime.tv_sec)
        return 1;

    if (afi->datetime.tv_usec < bfi->datetime.tv_usec)
        return -1;
    if (afi->datetime.tv_usec > bfi->datetime.tv_usec)
        return 1;

    /* There is no way to include the id here? */

    return 0;
}

static void
make_file_info(GMatchInfo *info, gboolean is_map, AFMWFileInfo *finfo)
{
    gchar buf[20];
    gchar *s;

    s = g_match_info_fetch_named(info, "date");
    g_assert(strlen(s) == 10);
    memcpy(buf, s+6, 4);
    buf[4] = '-';
    memcpy(buf + 5, s+3, 2);
    buf[7] = '-';
    memcpy(buf + 8, s+0, 2);
    buf[10] = 'T';
    g_free(s);

    s = g_match_info_fetch_named(info, "time");
    g_assert(strlen(s) == 8);
    memcpy(buf + 11, s, 8);
    buf[13] = ':';
    buf[16] = ':';
    g_free(s);

    buf[19] = '\0';
    gwy_debug("iso datetime <%s>", buf);

    g_time_val_from_iso8601(buf, &finfo->datetime);

    if (is_map) {
        s = g_match_info_fetch_named(info, "ptid");
        finfo->id = atoi(s);
        g_free(s);
    }
    else
        finfo->id = 0;
}

static gchar**
find_all_file_names(const gchar *filename)
{
    GRegex *name_regex;
    GMatchInfo *info = NULL;
    guint i, len;
    gchar *dirname = NULL, *basename = NULL, *commonname = NULL, *cname;
    gchar **filelist;
    AFMWFileInfo finfo;
    const gchar *fname;
    GArray *files;
    gboolean is_map = TRUE;
    GDir *dir;

    /* It is unclear how to do this.  The files seem to be all called like
     * FD Curve, Single, HH_MM_SS, DD.MM.YYYY.csv
     * FD Curve, Mapping, Point NN, HH_MM_SS, DD.MM.YYYY.csv
     * so it is quite difficult to distinguish between curve sets.  We try to load all files in a directory, but that
     * is rather agressive. */
    basename = g_path_get_basename(filename);
    len = strlen(basename);

    if (len < 24) {
        g_free(basename);
        return NULL;
    }

    gwy_debug("trying mapping regex");
    name_regex = g_regex_new("^(?<name>.*), "
                             "Point (?<ptid>[0-9]+), "
                             "(?P<time>[0-9]{2}_[0-9]{2}_[0-9]{2}), "
                             "(?P<date>[0-9]{2}\\.[0-9]{2}\\.[0-9]{4})\\."
                             "(csv|CSV)$",
                             G_REGEX_NO_AUTO_CAPTURE | G_REGEX_OPTIMIZE, 0, NULL);
    g_return_val_if_fail(name_regex, NULL);
    if (!g_regex_match(name_regex, basename, 0, &info)) {
        gwy_debug("trying single regex");
        g_match_info_free(info);
        info = NULL;
        g_regex_unref(name_regex);
        name_regex = g_regex_new("^(?<name>.*), "
                                 "(?P<time>[0-9]{2}_[0-9]{2}_[0-9]{2}), "
                                 "(?P<date>[0-9]{2}\\.[0-9]{2}\\.[0-9]{4})\\."
                                 "(csv|CSV)$",
                                 G_REGEX_NO_AUTO_CAPTURE | G_REGEX_OPTIMIZE, 0, NULL);
        g_return_val_if_fail(name_regex, NULL);
        if (!g_regex_match(name_regex, basename, 0, &info)) {
            gwy_debug("cannot match given file name to any regex");
            g_match_info_free(info);
            g_regex_unref(name_regex);
            g_free(basename);
            return NULL;
        }
        is_map = FALSE;
    }
    commonname = g_match_info_fetch_named(info, "name");
    g_match_info_free(info);
    gwy_debug("common name <%s>", commonname);

    dirname = g_path_get_dirname(filename);
    if (!(dir = g_dir_open(dirname, 0, NULL))) {
        /* We will likely fail anyway when this happen, but fail with some cannot-read-given-file message... */
        g_free(dirname);
        g_free(basename);
        g_free(commonname);
        g_regex_unref(name_regex);
        return NULL;
    }

    files = g_array_new(FALSE, FALSE, sizeof(AFMWFileInfo));
    /* Find files with the same filename pattern fooNNN.dat */
    while ((fname = g_dir_read_name(dir))) {
        gwy_debug("found file %s", fname);
        if (!g_regex_match(name_regex, fname, 0, &info)) {
            g_match_info_free(info);
            continue;
        }
        cname = g_match_info_fetch_named(info, "name");
        if (!gwy_strequal(cname, commonname)) {
            g_free(cname);
            g_match_info_free(info);
            continue;
        }
        g_free(cname);
        gwy_debug("seems matching");
        make_file_info(info, is_map, &finfo);
        finfo.filename = g_build_filename(dirname, fname, NULL);
        g_array_append_val(files, finfo);
        g_match_info_free(info);
    }
    g_dir_close(dir);
    g_free(dirname);
    g_free(basename);
    g_free(commonname);
    g_regex_unref(name_regex);

    /* This should not normally happen, but something might be changing files on disk... */
    if (!files->len) {
        for (i = 0; i < files->len; i++)
            g_free(g_array_index(files, AFMWFileInfo, i).filename);
        g_array_free(files, TRUE);
        return NULL;
    }

    g_array_sort(files, compare_fileinfos);
    /* XXX: For mapping we can try to cut a single consecutive block of filenames which (1) contains the currectly
     * selected file (2) has increasing sequence of ids.  For single we have no idea what might constitute a spectrum
     * group. */
    filelist = g_new(gchar*, files->len+1);
    for (i = 0; i < files->len; i++)
        filelist[i] = g_array_index(files, AFMWFileInfo, i).filename;
    filelist[files->len] = NULL;
    g_array_free(files, TRUE);

    return filelist;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
