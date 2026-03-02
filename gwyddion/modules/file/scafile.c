/*
 *  $Id: scafile.c 26341 2024-05-15 09:06:23Z yeti-dn $
 *  Copyright (C) 2024 David Necas (Yeti).
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
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

#define MAGIC "scan\t"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION_HEADER ".sca"
#define EXTENSION_DATA ".dat"

typedef struct {
    const gchar *filename;
    gchar *full_filename;  /* This one is allocated. */
    const gchar *title;
    const gchar *unit;
    const gchar *some_other_info;  /* Sometimes a pair of numbers, repeating the unknown numbers from the main header,
                                      sometimes text note like counts/10ms. */
} SCADataInfo;

typedef struct {
    const gchar *filename;
    const gchar *format_version;
    const gchar *date;
    const gchar *time;
    const gchar *comment;
    gint xres;
    gint yres;
    gint unknown1; /* Literally the number 1, perhaps some kind of zres? */
    gdouble xreal;
    gdouble yreal;
    gdouble zreal;
    gdouble xoff;
    gdouble yoff;
    gdouble zoff;
    const gchar *unit;
    gdouble unknown2;
    gdouble unknown3;
    gdouble unknown4;
    gdouble unknown5;
    guint nfiles;
    SCADataInfo *datfiles;
} SCAFile;

static gboolean      module_register        (void);
static gint          scafile_detect         (const GwyFileDetectInfo *fileinfo,
                                             gboolean only_name);
static GwyContainer* scafile_load           (const gchar *filename,
                                             GwyRunType mode,
                                             GError **error);
static gboolean      scafile_read_header    (gchar *buffer,
                                             SCAFile *scafile,
                                             GError **error);
static gint          scafile_sscanf         (const gchar *str,
                                             const gchar *format,
                                             ...);
static GwyDataField* scafile_read_data_field(SCAFile *scafile,
                                             guint i,
                                             GError **error);
static const gchar*  guess_title            (const SCAFile *scafile,
                                             guint i);
static void          scafile_free           (SCAFile *scafile);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports SCA data files (two-part .sca + .dat)."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2024",
};

GWY_MODULE_QUERY2(module_info, scafile)

static gboolean
module_register(void)
{
    gwy_file_func_register("scafile",
                           N_("SCA files (.sca + .dat)"),
                           (GwyFileDetectFunc)&scafile_detect,
                           (GwyFileLoadFunc)&scafile_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
scafile_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    SCAFile scafile;
    gchar *header;
    gint score = 0;
    guint i;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_HEADER) ? 10 : 0;

    /* Quickly filter out clear non-matches. */
    if (fileinfo->buffer_len <= MAGIC_SIZE || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE))
        return 0;
    gwy_debug("magic matches SCA");

    /* Assume the header fits into the buffer – it should fit there like ten times. */
    header = g_memdup(fileinfo->head, fileinfo->buffer_len);
    gwy_clear(&scafile, 1);
    scafile.filename = fileinfo->name;
    if (scafile_read_header(header, &scafile, NULL)) {
        gwy_debug("header parsed OK");
        for (i = 0; i < scafile.nfiles; i++) {
            if (!g_file_test(scafile.datfiles[i].full_filename, G_FILE_TEST_IS_REGULAR)) {
                gwy_debug("data file %s does not exist", scafile.datfiles[i].full_filename);
                break;
            }
        }
        if (i == scafile.nfiles) {
            gwy_debug("data files exist");
            score = 100;
        }
    }
    scafile_free(&scafile);
    g_free(header);

    return score;
}

static GwyContainer*
scafile_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    SCAFile scafile;
    GwyContainer *container = NULL;
    gchar *header = NULL;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield;
    const gchar *title;
    guint i;

    if (!g_file_get_contents(filename, &header, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&scafile, 1);
    scafile.filename = filename;
    if (!scafile_read_header(header, &scafile, error))
        goto end;
    if (!scafile.nfiles) {
        err_NO_DATA(error);
        goto end;
    }

    container = gwy_container_new();
    for (i = 0; i < scafile.nfiles; i++) {
        if (!(dfield = scafile_read_data_field(&scafile, i, error))) {
            GWY_OBJECT_UNREF(container);
            goto end;
        }
        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(i), dfield);
        if ((title = guess_title(&scafile, i)))
            gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), title);
        else
            gwy_app_channel_title_fall_back(container, i);
        gwy_file_channel_import_log_add(container, i, NULL, scafile.datfiles[i].full_filename);
    }

end:
    scafile_free(&scafile);
    g_free(header);

    return container;
}

static gboolean
scafile_read_header(gchar *buffer,
                    SCAFile *scafile,
                    GError **error)
{
    GArray *infoarray = NULL;
    gchar *line, *dirname;
    guint i, nimages;

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;

    if (!g_str_has_prefix(line, MAGIC)) {
        err_FILE_TYPE(error, "SCA");
        return FALSE;
    }
    scafile->format_version = line + MAGIC_SIZE;
    gwy_debug("format_version %s", scafile->format_version);
    if (!gwy_strequal(scafile->format_version, "2.0")) {
        err_UNSUPPORTED(error, _("format version"));
    }

    if (!(scafile->date = gwy_str_next_line(&buffer))
        || !(scafile->time = gwy_str_next_line(&buffer))
        || !(scafile->comment = gwy_str_next_line(&buffer)))
        goto truncated;
    gwy_debug("date-time %s %s", scafile->date, scafile->time);
    gwy_debug("comment %s", scafile->comment);

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;
    if (sscanf(line, "%d %d %d", &scafile->xres, &scafile->yres, &scafile->unknown1) != 3) {
        err_INVALID(error, _("resolution"));
        return FALSE;
    }
    gwy_debug("xres %d, yres %d", scafile->xres, scafile->yres);
    if (err_DIMENSION(error, scafile->xres) || err_DIMENSION(error, scafile->yres))
        return FALSE;

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;
    if (scafile_sscanf(line, "ddd", &scafile->xreal, &scafile->yreal, &scafile->zreal) != 3) {
        err_INVALID(error, "max parameters");
        return FALSE;
    }

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;
    if (scafile_sscanf(line, "ddd", &scafile->xoff, &scafile->yoff, &scafile->zoff) != 3) {
        err_INVALID(error, "min parameters");
        return FALSE;
    }

    if (!(scafile->unit = gwy_str_next_line(&buffer)))
        goto truncated;

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;
    if (scafile_sscanf(line, "dddd",
                       &scafile->unknown2, &scafile->unknown3, &scafile->unknown4, &scafile->unknown5) != 4) {
        err_INVALID(error, "unknown parameters");
        return FALSE;
    }

    if (!(line = gwy_str_next_line(&buffer)))
        goto truncated;
    if (sscanf(line, "%u", &nimages) != 1) {
        err_INVALID(error, "nimages");
        return FALSE;
    }
    gwy_debug("nimages %u", nimages);

    infoarray = g_array_new(FALSE, FALSE, sizeof(SCADataInfo));
    for (i = 0; i < nimages; i++) {
        SCADataInfo info;

        gwy_clear(&info, 1);
        if (!(info.filename = gwy_str_next_line(&buffer))
            || !(info.title = gwy_str_next_line(&buffer))
            || !(info.unit = gwy_str_next_line(&buffer))
            || !(info.some_other_info = gwy_str_next_line(&buffer)))
            goto truncated;

        gwy_debug("image[%d] = %s", i, info.filename);
        g_array_append_val(infoarray, info);
    }
    scafile->nfiles = nimages;
    scafile->datfiles = (SCADataInfo*)g_array_free(infoarray, FALSE);

    sanitise_real_size(&scafile->xreal, "x size");
    sanitise_real_size(&scafile->yreal, "y size");

    dirname = g_path_get_dirname(scafile->filename);
    for (i = 0; i < nimages; i++)
        scafile->datfiles[i].full_filename = g_build_filename(dirname, scafile->datfiles[i].filename, NULL);
    g_free(dirname);

    return TRUE;

truncated:
    err_TRUNCATED_HEADER(error);
    if (infoarray)
        g_array_free(infoarray, TRUE);

    return FALSE;
}

static gint
scafile_sscanf(const gchar *str,
               const gchar *format,
               ...)
{
    va_list ap;
    gchar *endptr;
    gint *pi;
    gdouble *pd;
    gint count = 0;

    va_start(ap, format);
    while (*format) {
        switch (*format++) {
            case 'i':
            pi = va_arg(ap, gint*);
            g_assert(pi);
            *pi = strtol(str, &endptr, 10);
            break;

            case 'd':
            pd = va_arg(ap, gdouble*);
            g_assert(pd);
            *pd = g_ascii_strtod(str, &endptr);
            break;

            default:
            g_return_val_if_reached(0);
            break;
        }
        if ((gchar*)str == endptr)
            break;

        count++;
        str = endptr;
    }
    va_end(ap);

    return count;
}

static GwyDataField*
scafile_read_data_field(SCAFile *scafile,
                        guint i,
                        GError **error)
{
    GError *err = NULL;
    guchar *buffer;
    const guchar *p;
    gsize size, expected_size;
    guint xres, yres, zres, n;
    GwyDataField *dfield;
    SCADataInfo *info;
    gint power10;
    gboolean ok;

    g_return_val_if_fail(i < scafile->nfiles, NULL);

    info = scafile->datfiles + i;
    ok = gwy_file_get_contents(info->full_filename, &buffer, &size, &err);
    if (!ok) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    n = scafile->xres*scafile->yres;
    expected_size = (n + 3)*sizeof(guint16);
    if (err_SIZE_MISMATCH(error, expected_size, size, TRUE)) {
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    p = buffer;
    xres = gwy_get_guint16_le(&p);
    yres = gwy_get_guint16_le(&p);
    zres = gwy_get_guint16_le(&p);
    gwy_debug("xres %d, yres %d, zres %d", xres, yres, zres);
    if (xres != scafile->xres || yres != scafile->yres || zres != 1) {
        g_warning("Header and data file resolution mismatch.");
    }

    dfield = gwy_data_field_new(scafile->xres, scafile->yres, scafile->xreal, scafile->yreal, FALSE);
    gwy_convert_raw_data(p, n, 1, GWY_RAW_DATA_UINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), 1.0, 0.0);
    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(dfield), scafile->unit, &power10);
    gwy_data_field_set_xreal(dfield, gwy_data_field_get_xreal(dfield)*pow10(power10));
    gwy_data_field_set_yreal(dfield, gwy_data_field_get_yreal(dfield)*pow10(power10));
    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(dfield), info->unit, &power10);
    gwy_data_field_multiply(dfield, pow10(power10));

    gwy_file_abandon_contents(buffer, size, NULL);

    return dfield;
}

static const gchar*
guess_title(const SCAFile *scafile, guint i)
{
    SCADataInfo *info;
    const gchar *dot;
    gchar typec;

    g_return_val_if_fail(i < scafile->nfiles, NULL);

    info = scafile->datfiles + i;
    if (!(dot = strrchr(info->filename, '.')) || dot == info->filename)
        return NULL;

    typec = g_ascii_tolower(*(dot-1));
    if (typec == 't')
        return "Topography";
    if (typec == 'n')
        return "Error";
    if (typec == 'i' || typec == 'j')
        return "APD";
    return NULL;
}

static void
scafile_free(SCAFile *scafile)
{
    guint i;

    for (i = 0; i < scafile->nfiles; i++)
        g_free(scafile->datfiles[i].full_filename);
    g_free(scafile->datfiles);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
