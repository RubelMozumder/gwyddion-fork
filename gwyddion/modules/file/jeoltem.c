/*
 *  $Id: jeoltem.c 26005 2023-11-09 13:17:31Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
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
 * JEOL TEM image
 * .tif
 * Read[1]
 * [1] The import module is unfinished due to the lack of documentation, testing files and/or people willing to help
 * with the testing.  If you can help please contact us.
 **/

#include "config.h"
#include <stdlib.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "err.h"
#include "gwytiff.h"

enum {
    JEOL_MIN_HEADER_SIZE = 1024  /* No idea yet. */
};

enum {
    JEOL_TIFF_TAG_DOUBLE1 = 65006,
    JEOL_TIFF_TAG_DOUBLE2 = 65007,
    JEOL_TIFF_TAG_DOUBLE3 = 65009,
    JEOL_TIFF_TAG_DOUBLE4 = 65010,
    JEOL_TIFF_TAG_SLONG1  = 65015,
    JEOL_TIFF_TAG_SLONG2  = 65016,
    JEOL_TIFF_TAG_DOUBLE5 = 65024,
    JEOL_TIFF_TAG_DOUBLE6 = 65025,
    JEOL_TIFF_TAG_SLONG3  = 65026,
    JEOL_TIFF_TAG_HEADER  = 65027,
};

typedef enum {
    TERMINATOR             = 0x00,
    FIXED_12_BLOCK         = 0x14,
    VARIABLE_PERCENT_BLOCK = 0x15,
} BlockType;

typedef enum {
    CONTENT_DATA_TYPE = 3,
    CONTENT_INT16     = 4,
    CONTENT_INT32     = 5,
    CONTENT_DOUBLE    = 7,
    CONTENT_BOOLEAN   = 8,
    CONTENT_AREA      = 15,
    CONTENT_UTF16     = 20,
} BlockContentType;

typedef struct {
    gchar *name;
    BlockType type;
    BlockContentType data_type;
    guint n;
    union {
        gdouble d;
        gint i;
        gboolean b;
        gchar *s;
    } value;
} BlockContent;

typedef struct {
    gdouble double1;
    gdouble double2;
    gdouble double3;
    gdouble double4;
    gint int1;
    gint int2;
    gdouble double5;
    gdouble double6;
    gint int3;
} JEOLTEMTags;

typedef struct {
    GwyTIFF *tiff;
    GArray *blocks;
    JEOLTEMTags tags;
    gdouble mag;
    gint camerano;
} JEOLTEMFile;

static gboolean            module_register        (void);
static gint                jeoltem_detect         (const GwyFileDetectInfo *fileinfo,
                                                   gboolean only_name);
static GwyContainer*       jeoltem_load           (const gchar *filename,
                                                   GwyRunType mode,
                                                   GError **error);
static GwyContainer*       jeoltem_load_data      (JEOLTEMFile *jtfile,
                                                   GError **error);
static gboolean            jeoltem_load_header    (JEOLTEMFile *jtfile,
                                                   GError **error);
static GwyContainer*       get_meta               (JEOLTEMFile *jtfile);
static void                jeoltem_file_free      (JEOLTEMFile *jtfile);
static void                jeoltem_read_other_tags(JEOLTEMTags *tags,
                                                   GwyTIFF *tiff);
static const GwyTIFFEntry* jeoltem_find_header    (GwyTIFF *tiff,
                                                   GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports JEOL TEM images."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2021",
};

GWY_MODULE_QUERY2(module_info, jeoltem)

static gboolean
module_register(void)
{
    gwy_file_func_register("jeol-tem",
                           N_("JEOL TIF TEM image (.tif)"),
                           (GwyFileDetectFunc)&jeoltem_detect,
                           (GwyFileLoadFunc)&jeoltem_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
jeoltem_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    const GwyTIFFEntry *entry;
    guint score = 0;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    guint byteorder = G_LITTLE_ENDIAN;
    guint four, zero, tagsize;
    const guchar *t, *p;

    if (only_name)
        return score;

    /* Weed out non-TIFFs */
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len, &version, &byteorder))
        return 0;

    /* Use GwyTIFF for detection to avoid problems with fragile libtiff.  Progressively try more fine tests. */
    if (!(tiff = gwy_tiff_load(fileinfo->name, NULL)))
        return 0;

    /* Check the beginning of the binary header. */
    if (!(entry = jeoltem_find_header(tiff, NULL)) || entry->count < 26)
        goto end;

    t = entry->value;
    p = tiff->data + tiff->get_guint32(&t);
    four = gwy_get_guint32_be(&p);
    zero = gwy_get_guint32_be(&p);
    tagsize = gwy_get_guint32_be(&p);
    gwy_debug("%u %u %u", four, zero, tagsize);

    if (four == 4 && zero == 0 && tagsize+24 == entry->count)
        score = 100;

end:
    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
jeoltem_load(const gchar *filename, G_GNUC_UNUSED GwyRunType mode, GError **error)
{
    JEOLTEMFile jtfile;
    GwyContainer *container = NULL;

    gwy_clear(&jtfile, 1);

    jtfile.tiff = gwy_tiff_load(filename, error);
    if (!jtfile.tiff)
        return NULL;
    if (!jeoltem_load_header(&jtfile, error))
        goto end;

    jeoltem_read_other_tags(&jtfile.tags, jtfile.tiff);
    container = jeoltem_load_data(&jtfile, error);
    if (container)
        gwy_file_channel_import_log_add(container, 0, NULL, filename);

end:
    jeoltem_file_free(&jtfile);

    return container;
}

static gchar*
read_latin1_string(const guchar **p, const guchar *blockend, GError **error)
{
    guint len;
    gchar *s;

    if (blockend - *p < sizeof(guint16)) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }
    len = gwy_get_guint16_be(p);
    if (blockend - *p < len) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }

    s = gwy_convert_to_utf8(*p, len, "ISO-8859-1");
    *p += len;

    return s ? s : g_strdup("???");
}

static gchar*
read_utf16_string(const guchar **p, const guchar *blockend, GError **error)
{
    gsize len;
    gchar *s;

    if (blockend - *p < sizeof(guint16)) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }
    len = gwy_get_guint64_be(p);
    if (blockend - *p < 2*len) {
        err_TRUNCATED_PART(error, "string");
        return NULL;
    }

    s = gwy_utf16_to_utf8((const gunichar2*)*p, len, GWY_BYTE_ORDER_LITTLE_ENDIAN);
    *p += 2*len;

    return s ? s : g_strdup("???");
}

static gboolean
read_variable_block(BlockContent *b, const guchar **p, const guchar *end, GError **error)
{
    guint size;

    gwy_debug("variable size percent-block");
    if (end - *p < 20) {
        err_TRUNCATED_PART(error, "percent-block");
        return FALSE;
    }

    size = gwy_get_guint16_be(p);
    gwy_debug("percent-block size %u", size);
    if (size > end - *p) {
        err_TRUNCATED_PART(error, "percent-block");
        return FALSE;
    }
    gwy_debug("four percents: %02x %02x %02x %02x", (*p)[0], (*p)[1], (*p)[2], (*p)[3]);
    *p += 4;
    gwy_debug("six zeros: %02x %02x %02x %02x %02x %02x", (*p)[0], (*p)[1], (*p)[2], (*p)[3], (*p)[4], (*p)[5]);
    *p += 6;

    size -= 20;
    b->n = gwy_get_guint16_be(p);
    b->data_type = gwy_get_guint64_be(p);
    gwy_debug("n %u, data_type %u, real size %u", b->n, b->data_type, size);

    if (b->data_type == CONTENT_BOOLEAN) {
        if (b->n != 1)
            g_warning("Expected n = 1, got %u.", b->n);
        if (size != 1) {
            err_TRUNCATED_PART(error, "boolean block");
            return FALSE;
        }
        b->value.b = **p;
        (*p)++;
        gwy_debug("boolean %d", b->value.b);
    }
    else if (b->data_type == CONTENT_INT32 || b->data_type == CONTENT_DATA_TYPE) {
        if (b->n != 1)
            g_warning("Expected n = 1, got %u.", b->n);
        if (size != sizeof(gint32)) {
            err_TRUNCATED_PART(error, "int32 block");
            return FALSE;
        }
        /* Yes, the value is little-endian.  Bite me. */
        b->value.i = gwy_get_gint32_le(p);
        gwy_debug("int32 %d", b->value.i);
    }
    else if (b->data_type == CONTENT_INT16) {
        if (b->n != 1)
            g_warning("Expected n = 1, got %u.", b->n);
        if (size != sizeof(gint16)) {
            err_TRUNCATED_PART(error, "int16 block");
            return FALSE;
        }
        /* Yes, the value is little-endian.  Bite me. */
        b->value.i = gwy_get_gint16_le(p);
        gwy_debug("int16 %d", b->value.i);
    }
    else if (b->data_type == CONTENT_DOUBLE) {
        if (b->n != 1)
            g_warning("Expected n = 1, got %u.", b->n);
        if (size != sizeof(gdouble)) {
            err_TRUNCATED_PART(error, "double block");
            return FALSE;
        }
        /* Yes, the value is little-endian.  Bite me. */
        b->value.d = gwy_get_gdouble_le(p);
        gwy_debug("double %g", b->value.d);
    }
    else if (b->data_type == CONTENT_UTF16) {
        G_GNUC_UNUSED guint four;

        if (b->n != 3)
            g_warning("Expected n = 3, got %u.", b->n);
        if (size < 2*sizeof(guint64)) {
            err_TRUNCATED_PART(error, "utf-16 string block");
            return FALSE;
        }
        four = gwy_get_guint64_be(p);
        size -= sizeof(guint64);
        gwy_debug("four %u", four);
        if (!(b->value.s = read_utf16_string(p, *p + size, error)))
            return FALSE;
        gwy_debug("string <%s>", b->value.s);
    }
    else if (b->data_type == CONTENT_AREA) {
        /* Encounted 3 types so far:
         * 1) n = 7, size = 64, apparently consists of 6×int64be, 2×int64le
         * 2) n = 7, size = 56, apparently consists of 6×int64be, 2×float32le
         * 3) n = 11, size = 112, apparently consists of 10×int64be, 4×int64le
         * But I have no idea what to do with any of them, so just skip it. */
#ifdef DEBUG
        if ((b->n == 7 && (size == 56 || size == 64)) || (b->n == 11 && size == 112))
            gwy_debug("ignoring known area block");
        else
            gwy_debug("ignoring UNKNOWN area block");
#endif

        *p += size;
    }
    else {
#ifdef DEBUG
        GString *str = g_string_new("content");
        guint j;

        for (j = 0; j < size; j++) {
            g_string_append_printf(str, " %02x", (*p)[j]);
        }
        gwy_debug("%s", str->str);
        g_string_free(str, TRUE);
#endif
        *p += size;
    }

    return TRUE;
}

static gboolean
jeoltem_load_header(JEOLTEMFile *jtfile, GError **error)
{
    GwyTIFF *tiff = jtfile->tiff;
    const GwyTIFFEntry *entry;
    const guchar *t, *p, *end;
    GArray *blocks = NULL;
    gchar *name = NULL;
    G_GNUC_UNUSED guint tagsize, four, zero;
    gboolean ok = FALSE;

    if (!(entry = jeoltem_find_header(tiff, error)))
        return FALSE;

    t = entry->value;
    p = tiff->data + tiff->get_guint32(&t);
    end = p + entry->count;

    if (end - p < 26) {
        err_TRUNCATED_HEADER(error);
        return FALSE;
    }
    four = gwy_get_guint32_be(&p);
    zero = gwy_get_guint32_be(&p);
    /* This is entry->count minus 24. */
    tagsize = gwy_get_guint32_be(&p);
    gwy_debug("four %u, zero %u, tagsize %u", four, zero, tagsize);
    /* The following bytes seem to be always 00 00 00 01 01 00 */
    gwy_debug("start2: %02x %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    p += 6;
    /* The following bytes seem to be always 00 00 00 00 00 00 00 06 */
    gwy_debug("start3: %02x %02x %02x %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    p += 8;

    jtfile->blocks = blocks = g_array_new(FALSE, FALSE, sizeof(BlockContent));

    while (p < end) {
        BlockContent b;

        gwy_clear(&b, 1);
        if (end - p < 3) {
            err_TRUNCATED_HEADER(error);
            goto end;
        }
        b.type = *(p++);
        gwy_debug("block type %02x", b.type);
        if (!(b.name = name = read_latin1_string(&p, end, error)))
            goto end;
        gwy_debug("name: <%s>", b.name);
        gwy_debug("remaining bytes: %u", (guint)(end - p));

        if (b.type == TERMINATOR && !*b.name && end - p == 5) {
            gwy_debug("terminator %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4]);
            GWY_FREE(name);
            p += 5;
            continue;
        }

        if (end - p < 6) {
            err_TRUNCATED_PART(error, "block");
            goto end;
        }
        gwy_debug("six zeros: %02x %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4], p[5]);
        p += 6;

        if (b.type == FIXED_12_BLOCK) {
            G_GNUC_UNUSED guint i1, i2, i3;

            gwy_debug("fixed size 12-block");
            if (end - p < 12) {
                err_TRUNCATED_PART(error, "12-block");
                goto end;
            }
            /* No idea, really, but reading the numbers like this gives reasonably-sized values. */
            i1 = gwy_get_guint32_le(&p);
            i2 = gwy_get_guint32_le(&p);
            i3 = gwy_get_guint32_be(&p);
            gwy_debug("block-12a: first (%u, %u or %u) zero %u, last %u", i1 & 0xff, i1 >> 8, i1, i2, i3);
            GWY_FREE(name);
        }
        else if (b.type == VARIABLE_PERCENT_BLOCK) {
            if (!read_variable_block(&b, &p, end, error))
                goto end;

            g_array_append_val(blocks, b);
            name = NULL;

            /* FIXME: This is completely wrong.  It just happens to work for a handful of my files. */
            if (b.data_type == CONTENT_DOUBLE && gwy_strequal(b.name, "Actual Magnification"))
                jtfile->mag = b.value.d;
            else if (b.data_type == CONTENT_INT32 && gwy_strequal(b.name, "Camera Number"))
                jtfile->camerano = b.value.i;
        }
        else {
            err_INVALID(error, "block type");
            goto end;
        }
    }
    ok = TRUE;

end:
    g_free(name);

    return ok;
}

static GwyContainer*
jeoltem_load_data(JEOLTEMFile *jtfile, GError **error)
{
    GwyContainer *container, *meta;
    GwyDataField *dfield;
    GwyTIFFImageReader *reader;
    gdouble xstep, ystep, q;
    gdouble *data;
    guint i;

    /* Request a reader, this ensures dimensions and stuff are defined. */
    if (!(reader = gwy_tiff_get_image_reader(jtfile->tiff, 0, 3, error)))
        return NULL;

    /* FIXME: This is completely wrong.  The values just happen to work for a handful of my files. */
    if (jtfile->camerano == 1)
        xstep = ystep = 7.32e-6/jtfile->mag;
    else
        xstep = ystep = 17.87e-6/jtfile->mag;

    q = 1.0/((1 << reader->bits_per_sample) - 1);
    dfield = gwy_data_field_new(reader->width, reader->height, reader->width * xstep, reader->height * ystep, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    data = gwy_data_field_get_data(dfield);
    for (i = 0; i < reader->height; i++)
        gwy_tiff_read_image_row_averaged(jtfile->tiff, reader, i, q, 0.0, data + i*reader->width);

    container = gwy_container_new();

    gwy_container_pass_object_by_name(container, "/0/data", dfield);

    gwy_container_set_const_string_by_name(container, "/0/data/title", "Intensity");

    if ((meta = get_meta(jtfile))) {
        gwy_container_pass_object_by_name(container, "/0/meta", meta);
    }

    gwy_tiff_image_reader_free(reader);

    return container;
}

/* NB: This consumes string values in blocks.  We do not have to free them later, but we cannot access them later
 * either. */
static GwyContainer*
get_meta(JEOLTEMFile *jtfile)
{
    GwyContainer *meta = gwy_container_new();
    GArray *blocks = jtfile->blocks;
    guint i, n = blocks->len;

    for (i = 0; i < n; i++) {
        BlockContent *bci = &g_array_index(blocks, BlockContent, i);

        if (bci->type != VARIABLE_PERCENT_BLOCK || !*bci->name)
            continue;

        if (bci->data_type == CONTENT_INT16 || bci->data_type == CONTENT_INT32)
            gwy_container_set_string_by_name(meta, bci->name, g_strdup_printf("%d", bci->value.i));
        else if (bci->data_type == CONTENT_BOOLEAN)
            gwy_container_set_const_string_by_name(meta, bci->name, bci->value.b ? "True" : "False");
        else if (bci->data_type == CONTENT_DOUBLE)
            gwy_container_set_string_by_name(meta, bci->name, g_strdup_printf("%g", bci->value.d));
        else if (bci->data_type == CONTENT_UTF16) {
            if (*bci->value.s) {
                gwy_container_set_string_by_name(meta, bci->name, bci->value.s);
                bci->value.s = NULL;
            }
        }
    }

    if (gwy_container_get_n_items(meta))
        return meta;

    g_object_unref(meta);
    return NULL;
}

static void
jeoltem_file_free(JEOLTEMFile *jtfile)
{
    GArray *blocks = jtfile->blocks;
    guint i;

    if (blocks) {
        for (i = 0; i < blocks->len; i++) {
            BlockContent *bci = &g_array_index(blocks, BlockContent, i);
            g_free(bci->name);
            if (bci->data_type == CONTENT_UTF16)
                g_free(bci->value.s);
        }
        g_array_free(blocks, TRUE);
        jtfile->blocks = NULL;
    }
    if (jtfile->tiff) {
        gwy_tiff_free(jtfile->tiff);
        jtfile->tiff = NULL;
    }
}

static void
jeoltem_read_other_tags(JEOLTEMTags *tags, GwyTIFF *tiff)
{
    gwy_clear(tags, 1);

    /* These are present but do not seem to contain anything useful.  Just some ones and zeros. */
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE1, &tags->double1)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE1, tags->double1);
    }
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE2, &tags->double2)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE2, tags->double2);
    }
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE3, &tags->double3)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE3, tags->double3);
    }
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE4, &tags->double4)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE4, tags->double4);
    }
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE5, &tags->double5)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE5, tags->double5);
    }
    if (gwy_tiff_get_float0(tiff, JEOL_TIFF_TAG_DOUBLE6, &tags->double6)) {
        gwy_debug("tag%u = %g", JEOL_TIFF_TAG_DOUBLE6, tags->double6);
    }
    if (gwy_tiff_get_sint0(tiff, JEOL_TIFF_TAG_SLONG1, &tags->int1)) {
        gwy_debug("tag%u = %d", JEOL_TIFF_TAG_SLONG1, tags->int1);
    }
    if (gwy_tiff_get_sint0(tiff, JEOL_TIFF_TAG_SLONG2, &tags->int2)) {
        gwy_debug("tag%u = %d", JEOL_TIFF_TAG_SLONG2, tags->int2);
    }
    if (gwy_tiff_get_sint0(tiff, JEOL_TIFF_TAG_SLONG3, &tags->int3)) {
        gwy_debug("tag%u = %d", JEOL_TIFF_TAG_SLONG3, tags->int3);
    }
}

static const GwyTIFFEntry*
jeoltem_find_header(GwyTIFF *tiff, GError **error)
{
    const GwyTIFFEntry *entry;

    if (!(entry = gwy_tiff_find_tag(tiff, 0, JEOL_TIFF_TAG_HEADER))
        || (entry->type != GWY_TIFF_BYTE && entry->type != GWY_TIFF_SBYTE)
        || entry->count < JEOL_MIN_HEADER_SIZE) {
        err_FILE_TYPE(error, "JEOL TEM");
        return NULL;
    }

    return entry;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
