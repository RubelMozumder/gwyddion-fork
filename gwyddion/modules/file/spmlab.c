/*
 *  $Id: spmlab.c 25800 2023-10-06 16:05:45Z yeti-dn $
 *  Copyright (C) 2004-2016 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  Roughly based on code in Kasgira by MV <kasigra@seznam.cz>.
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
 * <mime-type type="application/x-spmlab-spm">
 *   <comment>SPMLab SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="#R3"/>
 *     <match type="string" offset="0" value="#R4"/>
 *     <match type="string" offset="0" value="#R5"/>
 *     <match type="string" offset="0" value="#R6"/>
 *     <match type="string" offset="0" value="#R7"/>
 *   </magic>
 *   <glob pattern="*.zfp"/>
 *   <glob pattern="*.zrp"/>
 *   <glob pattern="*.zfr"/>
 *   <glob pattern="*.zrr"/>
 *   <glob pattern="*.ffp"/>
 *   <glob pattern="*.frp"/>
 *   <glob pattern="*.ffr"/>
 *   <glob pattern="*.frr"/>
 *   <glob pattern="*.lfp"/>
 *   <glob pattern="*.lrp"/>
 *   <glob pattern="*.lfr"/>
 *   <glob pattern="*.lrr"/>
 *   <glob pattern="*.sfp"/>
 *   <glob pattern="*.srp"/>
 *   <glob pattern="*.sfr"/>
 *   <glob pattern="*.srr"/>
 *   <glob pattern="*.1fp"/>
 *   <glob pattern="*.1rp"/>
 *   <glob pattern="*.1fr"/>
 *   <glob pattern="*.1rr"/>
 *   <glob pattern="*.2fp"/>
 *   <glob pattern="*.2rp"/>
 *   <glob pattern="*.2fr"/>
 *   <glob pattern="*.2rr"/>
 *   <glob pattern="*.ZFP"/>
 *   <glob pattern="*.ZRP"/>
 *   <glob pattern="*.ZFR"/>
 *   <glob pattern="*.ZRR"/>
 *   <glob pattern="*.FFP"/>
 *   <glob pattern="*.FRP"/>
 *   <glob pattern="*.FFR"/>
 *   <glob pattern="*.FRR"/>
 *   <glob pattern="*.LFP"/>
 *   <glob pattern="*.LRP"/>
 *   <glob pattern="*.LFR"/>
 *   <glob pattern="*.LRR"/>
 *   <glob pattern="*.SFP"/>
 *   <glob pattern="*.SRP"/>
 *   <glob pattern="*.SFR"/>
 *   <glob pattern="*.SRR"/>
 *   <glob pattern="*.1FP"/>
 *   <glob pattern="*.1RP"/>
 *   <glob pattern="*.1FR"/>
 *   <glob pattern="*.1RR"/>
 *   <glob pattern="*.2FP"/>
 *   <glob pattern="*.2RP"/>
 *   <glob pattern="*.2FR"/>
 *   <glob pattern="*.2RR"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # SpmLab
 * # Not very specific.  Can we remember the version somehow to prevent
 * # matching it twice?
 * 0 string \x23R
 * >2 regex [3-7]\.[0-9]+\x23\x20[0-9]+
 * >>2 regex [3-7]\.[0-9]+ Thermicroscopes SpmLab SPM data version %s
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Thermicroscopes SPMLab R4-R7
 * .tfr, .ffr, etc.
 * Read
 **/

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/gwyapp.h>
#include <app/data-browser.h>

#include "err.h"

typedef struct {
    guint dataoffset;
    guint xres;
    guint yres;
    guint nlayers;
    gint datatype;
    gchar version;
    gint direction;
    gint datamode;
    gint probetype;
    gint stagetype;
    gdouble xoff;
    gdouble yoff;
    gdouble xreal;
    gdouble yreal;
    gdouble q;
    gdouble z0;
    gdouble qrate;
    gdouble layers_from;
    gdouble layers_to;
    GwySIUnit *unitxy;
    GwySIUnit *unitz;
    GwySIUnit *unitrate;
    gchar *datatype_str;
    gchar *probetype_str;
    gchar *datamode_str;
    gchar *model_str;
    gchar *release;
    gchar *datetime;
    gchar *description;
    gchar *scantype;
} SPMLabFile;

static gboolean      module_register    (void);
static gint          spmlab_detect      (const GwyFileDetectInfo *fileinfo,
                                         gboolean only_name);
static GwyContainer* spmlab_load        (const gchar *filename,
                                         GwyRunType mode,
                                         GError **error);
static gboolean      spmlab_read_header (SPMLabFile *slfile,
                                         const guchar *buffer,
                                         guint size,
                                         GError **error);
static void          read_data_field    (SPMLabFile *slfile,
                                         const guchar *buffer,
                                         GwyContainer *container,
                                         guint i);
static void          add_meta           (SPMLabFile *slfile,
                                         GwyContainer *container,
                                         guint i);
static void          spmlab_file_free   (SPMLabFile *slfile);
static const gchar*  datatype_to_string (gint type);
static const gchar*  datamode_to_string (gint mode);
static const gchar*  stagetype_to_string(gint type);
static const gchar*  probetype_to_string(gint type);
static const gchar*  direction_to_string(gint type);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Thermicroscopes SpmLab R3 to R7 data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.12",
    "David Nečas (Yeti) & Petr Klapetek",
    "2005",
};

GWY_MODULE_QUERY2(module_info, spmlab)

static gboolean
module_register(void)
{
    gwy_file_func_register("spmlab",
                           N_("Thermicroscopes SpmLab files"),
                           (GwyFileDetectFunc)&spmlab_detect,
                           (GwyFileLoadFunc)&spmlab_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
spmlab_detect(const GwyFileDetectInfo *fileinfo,
              gboolean only_name)
{
    gint score = 0;

    if (only_name) {
        guint len;
        gchar ext[3];

        len = strlen(fileinfo->name_lowercase);
        if (len < 5)
            return 0;

        /* Match case insensitive *.[12zfls][fr][rp] */
        ext[0] = fileinfo->name_lowercase[len-3];
        ext[1] = fileinfo->name_lowercase[len-2];
        ext[2] = fileinfo->name_lowercase[len-1];
        if (fileinfo->name_lowercase[len-4] == '.'
            && (ext[2] == 'r' || ext[2] == 'p') && (ext[1] == 'f' || ext[1] == 'r')
            && (ext[0] == '1' || ext[0] == '2' || ext[0] == 'z' || ext[0] == 'f' || ext[0] == 'l' || ext[0] == 's'))
            score = 15;
        return score;
    }

    if (fileinfo->buffer_len >= 2048
        && fileinfo->head[0] == '#' && fileinfo->head[1] == 'R' && fileinfo->head[2] >= '3' && fileinfo->head[2] <= '7'
        && memchr(fileinfo->head+1, '#', 11))
        score = 85;

    return score;
}

static GwyContainer*
spmlab_load(const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0, datablocksize;
    GError *err = NULL;
    SPMLabFile slfile;
    guint i;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    gwy_clear(&slfile, 1);
    if (buffer[0] != '#' || buffer[1] != 'R') {
        err_FILE_TYPE(error, "Thermicroscopes SpmLab");
        goto fail;
    }
    slfile.version = buffer[2];
    if (slfile.version < '3' || slfile.version > '7') {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Unknown format version %c."), buffer[2]);
        goto fail;
    }
    /* 2048 is wrong; moreover, it differs for r5 and r4, kasigra uses 5752 for r5.  But we essentially need a value
     * larger than the last thing we read from the header. */
    if (size < 2048 || (slfile.version == 7 && size < 3216)) {
        err_TOO_SHORT(error);
        goto fail;
    }

    if (!spmlab_read_header(&slfile, buffer, size, error))
        goto fail;

    if (!slfile.nlayers) {
        g_warning("Zero nlayers.");
        slfile.nlayers = 1;
    }
    datablocksize = slfile.xres*slfile.yres*sizeof(guint16);
    /* err_SIZE_MISMATCH() gets hairy with multiple channels and integer overflow possibility... */
    if (slfile.dataoffset >= size || (size - slfile.dataoffset)/datablocksize < slfile.nlayers) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Data block is truncated."));
        goto fail;
    }

    container = gwy_container_new();
    for (i = 0; i < slfile.nlayers; i++) {
        read_data_field(&slfile, buffer, container, i);
        gwy_file_channel_import_log_add(container, i, NULL, filename);
    }

fail:
    spmlab_file_free(&slfile);
    gwy_file_abandon_contents(buffer, size, NULL);
    return container;
}

static gdouble
get_gfloat_le_as_double(const guchar **p)
{
    return gwy_get_gfloat_le(p);
}

static gboolean
spmlab_read_header(SPMLabFile *slfile,
                   const guchar *buffer, guint size, GError **error)
{
    enum {
        UNIT_LEN        = 10,
        RELEASE_LEN     = 16,
        DATETIME_LEN    = 20,
        DESCRIPTION_LEN = 40,
        STRING_LEN      = 64,
        TITLE_LEN       = 256,
        SCANTYPE_LEN    = 6,
        MIN_REMAINDER   = 2620,
    };
    /* Different version have the same information at different offsets. Use an indirect indexing to find things... */
    enum {
        DATASTART_IDX = 0,
        PIXDIM_IDX    = 1,
        PHYSDIM_IDX   = 2,
        SCALING_IDX   = 3,
        UNITSTR_IDX   = 4,
        DATATYPE_IDX  = 5,   /* if offset is zero use channel title */
        STRINGS_IDX   = 6,   /* if offset is zero use data type */
        NLAYERS_IDX   = 7,
        SCANTYPE_IDX  = 8,
        LAYERPOS_IDX  = 9,
        NOFFSETS
    };
    /* Information offsets in the various versions, in r5+ relative to data
     * start. */
    const guint offsets34[NOFFSETS] = {
        0x0104, 0x0196, 0x01a2, 0x01b2, 0x01c2, 0x0400, 0x0000, 0x01e0, 0x0458, 0x0000,
    };
    const guint offsets56[NOFFSETS] = {
        0x0104, 0x025c, 0x0268, 0x0288, 0x02a0, 0x0708, 0x0000, 0x02be, 0x0798, 0x08c0,
    };
    const guint offsets7[NOFFSETS] = {
        0x0104, 0x029c, 0x02a8, 0x02c8, 0x02e0, 0x0000, 0x0a58, 0x02fe, 0x0000, 0x0000,
    };
    gint power10;
    const guint *offsets;
    const guchar *p, *r, *last;
    gchar *s;
    gchar version = slfile->version;
    /* get floats in single precision from r4 but double from r5+ */
    gdouble (*getflt)(const guchar**);

    slfile->datatype = -1;
    slfile->direction = -1;
    slfile->datamode = -1;
    slfile->probetype = -1;
    slfile->stagetype = -1;

    if (version >= '5' && version <= '7') {
        /* There are more headers in r5, try to find something that looks like #R5. */
        last = r = buffer;
        while ((p = memchr(r, '#', size - (r - buffer) - MIN_REMAINDER))) {
            if (p[1] == 'R' && p[2] == version && p[3] == '.') {
                gwy_debug("pos: %ld", (long)(p - buffer));
                last = p;
                r = p + MIN_REMAINDER-1;
            }
            else
                r = p + 1;
        }
        offsets = (version == '7' ? offsets7 : offsets56);
        /* Everything is relative to data start in r5+. */
        slfile->dataoffset += last - buffer;
        buffer = last;
        getflt = &gwy_get_gdouble_le;
    }
    else {
        offsets = offsets34;
        getflt = &get_gfloat_le_as_double;
    }

    /* This appears to be the same number as in the ASCII miniheader -- so get it here since it's easier */
    p = buffer + offsets[DATASTART_IDX];
    slfile->dataoffset += gwy_get_guint32_le(&p);
    gwy_debug("data offset = %u", slfile->dataoffset);

    /* The release string includes also header size of it needs to be cleaned up for metadata. */
    slfile->release = g_strndup(p, RELEASE_LEN);
    p += RELEASE_LEN;
    slfile->datetime = g_strndup(p, DATETIME_LEN);
    p += DATETIME_LEN;
    slfile->description = g_strndup(p, DESCRIPTION_LEN);
    p += DESCRIPTION_LEN;

    gwy_debug("release %s", slfile->release);
    gwy_debug("datetime %s", slfile->datetime);

    p = buffer + offsets[NLAYERS_IDX];
    slfile->nlayers = gwy_get_guint16_le(&p);
    gwy_debug("nlayers %u", slfile->nlayers);

    p = buffer + offsets[PIXDIM_IDX];
    slfile->xres = gwy_get_guint32_le(&p);
    slfile->yres = gwy_get_guint32_le(&p);
    if (err_DIMENSION(error, slfile->xres) || err_DIMENSION(error, slfile->yres))
        return FALSE;

    p = buffer + offsets[PHYSDIM_IDX];
    slfile->xoff = -getflt(&p);
    slfile->xreal = getflt(&p) - slfile->xoff;
    sanitise_real_size(&slfile->xreal, "x size");
    slfile->yoff = -getflt(&p);
    slfile->yreal = getflt(&p) - slfile->yoff;
    sanitise_real_size(&slfile->yreal, "y size");

    p = buffer + offsets[SCALING_IDX];
    slfile->q = getflt(&p);
    slfile->z0 = getflt(&p);
    gwy_debug("xreal.raw = %g, yreal.raw = %g, q.raw = %g, z0.raw = %g",
              slfile->xreal, slfile->yreal, slfile->q, slfile->z0);

    p = buffer + offsets[UNITSTR_IDX];
    s = g_strndup(p, UNIT_LEN);
    slfile->unitz = gwy_si_unit_new_parse(s, &power10);
    g_free(s);
    slfile->q *= pow10(power10);
    slfile->z0 *= pow10(power10);

    p += UNIT_LEN;
    s = g_strndup(p, UNIT_LEN);
    slfile->unitxy = gwy_si_unit_new_parse(s, &power10);
    g_free(s);
    slfile->xreal *= pow10(power10);
    slfile->yreal *= pow10(power10);
    slfile->xoff *= pow10(power10);
    slfile->yoff *= pow10(power10);
    gwy_debug("xres = %d, yres = %d, xreal = %g, yreal = %g, q = %g, z0 = %g",
              slfile->xres, slfile->yres, slfile->xreal, slfile->yreal, slfile->q, slfile->z0);

    p += UNIT_LEN;
    s = g_strndup(p, UNIT_LEN);
    slfile->unitrate = gwy_si_unit_new_parse(s, &power10);
    g_free(s);
    slfile->qrate = pow10(power10);

    /* Optional stuff, i.e. this that either exists only in some version or
     * we only know how to read in certain versions. */
    if (offsets[STRINGS_IDX]) {
        p = buffer + offsets[STRINGS_IDX];
        slfile->probetype_str = g_strndup(p, size - (p - buffer));
        p += STRING_LEN;
        slfile->model_str = g_strndup(p, size - (p - buffer));
        p += STRING_LEN;
        p += 184;   /* No idea why.  Perhaps there can be something between. */
        slfile->datatype_str = g_strndup(p, size - (p - buffer));
        p += TITLE_LEN;
        slfile->datamode_str = g_strndup(p, size - (p - buffer));
        gwy_debug("title = <%s>", slfile->datatype_str);
    }
    if (offsets[DATATYPE_IDX]) {
        p = buffer + offsets[DATATYPE_IDX];
        slfile->datatype = gwy_get_gint16_le(&p);
        slfile->direction = gwy_get_gint16_le(&p);
        slfile->datamode = gwy_get_gint16_le(&p);
        gwy_debug("type = %d, dir = %d", slfile->datatype, slfile->direction);
    }
    if (offsets[SCANTYPE_IDX]) {
        p = buffer + offsets[SCANTYPE_IDX];
        slfile->scantype = g_strndup(p, SCANTYPE_LEN);
        p += SCANTYPE_LEN;
        slfile->probetype = gwy_get_gint16_le(&p);
        slfile->stagetype = gwy_get_gint16_le(&p);
    }
    if (offsets[LAYERPOS_IDX]) {
        p = buffer + offsets[LAYERPOS_IDX];
        slfile->layers_from = getflt(&p);
        slfile->layers_to = getflt(&p);
    }

    if (!slfile->datatype_str)
        slfile->datatype_str = g_strdup(datatype_to_string(slfile->datatype));

    return TRUE;
}

static void
read_data_field(SPMLabFile *slfile, const guchar *buffer,
                GwyContainer *container, guint i)
{
    const guchar *p = buffer + slfile->dataoffset;
    guint xres = slfile->xres, yres = slfile->yres, nlayers = slfile->nlayers;
    gsize datablocksize;
    GwyDataField *dfield;

    datablocksize = slfile->xres*slfile->yres*sizeof(guint16);
    dfield = gwy_data_field_new(xres, yres, slfile->xreal, slfile->yreal, FALSE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), slfile->unitxy);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), slfile->unitz);
    gwy_convert_raw_data(p + i*datablocksize, xres*yres, 1, GWY_RAW_DATA_UINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), slfile->q, slfile->z0);

    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(i), dfield);

    if (slfile->datatype_str) {
        if (nlayers < 2)
            gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), slfile->datatype_str);
        else {
            gdouble from = slfile->layers_from, to = slfile->layers_to, z;
            gchar *title;

            if (from || to) {
                z = (i*(to - from)/(nlayers - 1.0) + from);
                title = g_strdup_printf("%s (%g nm)", slfile->datatype_str, z);
            }
            else
                title = g_strdup_printf("%s %u", slfile->datatype_str, i+1);
            gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(i), (const guchar*)title);
        }
    }
    else
        gwy_app_channel_title_fall_back(container, i);

    add_meta(slfile, container, i);
}

static void
add_meta(SPMLabFile *slfile, GwyContainer *container, guint i)
{
    GwyContainer *meta = gwy_container_new();
    const gchar *s;

    if ((s = slfile->datetime) && *s)
        gwy_container_set_const_string_by_name(meta, "Date and time", s);
    if ((s = slfile->description) && *s)
        gwy_container_set_const_string_by_name(meta, "Description", s);
    if ((s = slfile->scantype) && *s)
        gwy_container_set_const_string_by_name(meta, "Scan type", s);
    if ((s = slfile->model_str) && *s)
        gwy_container_set_const_string_by_name(meta, "Scan type", s);

    if (slfile->release && (s = strrchr(slfile->release, '#')))
        gwy_container_set_string_by_name(meta, "Version", g_strndup(slfile->release, s+1 - slfile->release));

    if (((s = slfile->datatype_str) && *s) || (s = datatype_to_string(slfile->datatype)))
        gwy_container_set_const_string_by_name(meta, "Data type", s);

    if (((s = slfile->datamode_str) && *s) || (s = datamode_to_string(slfile->datamode)))
        gwy_container_set_const_string_by_name(meta, "Data mode", s);

    if (((s = slfile->probetype_str) && *s) || (s = probetype_to_string(slfile->probetype)))
        gwy_container_set_const_string_by_name(meta, "Probe type", s);

    if ((s = stagetype_to_string(slfile->stagetype)))
        gwy_container_set_const_string_by_name(meta, "Stage type", s);
    if ((s = direction_to_string(slfile->direction)))
        gwy_container_set_const_string_by_name(meta, "Direction", s);

    if (gwy_container_get_n_items(meta))
        gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(i), meta);
    g_object_unref(meta);
}

static void
spmlab_file_free(SPMLabFile *slfile)
{
    GWY_OBJECT_UNREF(slfile->unitxy);
    GWY_OBJECT_UNREF(slfile->unitz);
    GWY_OBJECT_UNREF(slfile->unitrate);
    g_free(slfile->release);
    g_free(slfile->datetime);
    g_free(slfile->description);
    g_free(slfile->scantype);
    g_free(slfile->datatype_str);
    g_free(slfile->probetype_str);
    g_free(slfile->datamode_str);
    g_free(slfile->model_str);
}

static const gchar*
datatype_to_string(gint type)
{
    const gchar *str;

    str = gwy_enuml_to_string(type,
                              "Height", 0,
                              "Current", 1,
                              "FFM", 2,
                              "Spect", 3,
                              "SpectV", 4,
                              "ADC1", 5,
                              "ADC2", 6,
                              "TipV", 7,
                              "DAC1", 8,
                              "DAC2", 9,
                              "ZPiezo", 10,
                              "Height error", 11,
                              "Linearized Z", 12,
                              "Feedback", 13,
                              NULL);
    return *str ? str : NULL;
}

static const gchar*
datamode_to_string(gint mode)
{
    const gchar *str;

    str = gwy_enuml_to_string(mode,
                              "Image", 0,
                              "Cits", 1,
                              "Dits", 2,
                              "FIS", 3,
                              "MFM", 4,
                              "EFM", 5,
                              "IV", 10,
                              "IS", 11,
                              "FS", 12,
                              "MS", 13,
                              "ES", 14,
                              "Electrochemistry", 15,
                              "Electrochemistry_Line_Average", 16,
                              NULL);
    return *str ? str : NULL;
}

static const gchar*
stagetype_to_string(gint type)
{
    const gchar *str;

    str = gwy_enuml_to_string(type,
                              "Discoverer_AFM", 0,
                              "Discoverer_STM", 2,
                              "Explorer_AFM", 3,
                              "Explorer_STM", 4,
                              "Universal", 5,
                              "SNOM", 6,
                              "Observer_AFM", 7,
                              "Observer_STM", 8,
                              "Topocron_AFM", 9,
                              "Topocron_STM", 10,
                              "Topocron", 12,
                              NULL);
    return *str ? str : NULL;
}

static const gchar*
probetype_to_string(gint type)
{
    const gchar *str;

    str = gwy_enuml_to_string(type, "AFM", 0, "STM", 1, NULL);
    return *str ? str : NULL;
}

static const gchar*
direction_to_string(gint type)
{
    const gchar *str;

    str = gwy_enuml_to_string(type, "Forward", 0, "Reverse", 1, NULL);
    return *str ? str : NULL;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
