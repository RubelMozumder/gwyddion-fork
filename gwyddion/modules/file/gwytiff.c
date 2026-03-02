/*
 *  $Id: gwytiff.c 26196 2024-02-22 12:37:13Z yeti-dn $
 *  Copyright (C) 2007-2024 David Necas (Yeti).
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

#include <glib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <app/gwymoduleutils-file.h>
#include "get.h"
#include "err.h"
#include "gwytiff.h"

/**
 * [FILE-MAGIC-MISSING]
 * This is a TIFF file format helper, not a specific file module.
 **/

/* File header size, real files must be actually larger. */
#define GWY_TIFF_HEADER_SIZE     8
#define GWY_TIFF_HEADER_SIZE_BIG 16

#define gwy_tiff_round_up_div(a, b) (((a) + (b)-1)/(b))

/* Parameters version and byteorder are inout.  If they are non-zero, the file must match the specified value to be
 * accepted.  In any case, they are set to the true values on success. */
const guchar*
gwy_tiff_detect(const guchar *buffer,
                gsize size,
                GwyTIFFVersion *version,
                GwyByteOrder *byteorder)
{
    GwyByteOrder tiffbyteorder;
    guint bom, vm;

    if (size < GWY_TIFF_HEADER_SIZE)
        return NULL;

    bom = gwy_get_guint16_le(&buffer);
    if (bom == 0x4949) {
        tiffbyteorder = GWY_BYTE_ORDER_LITTLE_ENDIAN;
        vm = gwy_get_guint16_le(&buffer);
    }
    else if (bom == 0x4d4d) {
        tiffbyteorder = GWY_BYTE_ORDER_BIG_ENDIAN;
        vm = gwy_get_guint16_be(&buffer);
    }
    else
        return NULL;

    if (vm != GWY_TIFF_CLASSIC && vm != GWY_TIFF_BIG)
        return NULL;

    if (vm == GWY_TIFF_BIG && size < GWY_TIFF_HEADER_SIZE_BIG)
        return NULL;

    // Typecast because of bloddy C++.
    if (version) {
        if (*version && *version != (GwyTIFFVersion)vm)
            return NULL;
        *version = (GwyTIFFVersion)vm;
    }

    if (byteorder) {
        if (*byteorder && *byteorder != tiffbyteorder)
            return NULL;
        *byteorder = tiffbyteorder;
    }

    return buffer;
}

/* By default compressed files are not allowed because (almost) no one saves SPM data this way.
 *
 * When files are not compressed gwy_tiff_read_image_row() can never fail because we can easily check the sizes of
 * everything before attempting to read data.  Consequently, most GwyTIFF callers do not need any error handling after
 * successfully creating a GwyTIFFImageReader.  If you allow compression you need to handle errors while reading
 * the image. */
void
gwy_tiff_allow_compressed(GwyTIFF *tiff,
                          gboolean setting)
{
    tiff->allow_compressed = setting;
}

gboolean
gwy_tiff_data_fits(const GwyTIFF *tiff,
                   guint64 offset,
                   guint64 item_size,
                   guint64 nitems)
{
    guint64 bytesize;

    /* Overflow in total size */
    if (nitems > G_GUINT64_CONSTANT(0xffffffffffffffff)/item_size)
        return FALSE;

    bytesize = nitems*item_size;
    /* Overflow in addition */
    if (offset + bytesize < offset)
        return FALSE;

    return offset + bytesize <= tiff->size;
}

guint
gwy_tiff_data_type_size(GwyTIFFDataType type)
{
    switch (type) {
        case GWY_TIFF_BYTE:
        case GWY_TIFF_SBYTE:
        case GWY_TIFF_ASCII:
        return 1;
        break;

        case GWY_TIFF_SHORT:
        case GWY_TIFF_SSHORT:
        return 2;
        break;

        case GWY_TIFF_LONG:
        case GWY_TIFF_SLONG:
        case GWY_TIFF_FLOAT:
        return 4;
        break;

        case GWY_TIFF_RATIONAL:
        case GWY_TIFF_SRATIONAL:
        case GWY_TIFF_DOUBLE:
        case GWY_TIFF_LONG8:
        case GWY_TIFF_SLONG8:
        return 8;
        break;

        default:
        return 0;
        break;
    }
}

const guchar*
gwy_tiff_entry_get_data_pointer(const GwyTIFF *tiff, const GwyTIFFEntry *entry)
{
    gsize offset, size;
    const guchar *p = entry->value;

    size = entry->count * gwy_tiff_data_type_size(entry->type);
    if (size > tiff->tagvaluesize) {
        offset = tiff->get_length(&p);
        p = tiff->data + offset;
    }
    return p;
}

GArray*
gwy_tiff_scan_ifd(const GwyTIFF *tiff, guint64 offset,
                  const guchar **pafter, GError **error)
{
    guint16 (*get_guint16)(const guchar **p) = tiff->get_guint16;
    guint64 (*get_length)(const guchar **p) = tiff->get_length;
    guint ifdsize = tiff->ifdsize;
    guint tagsize = tiff->tagsize;
    guint valuesize = tiff->tagvaluesize;
    guint64 nentries, i;
    const guchar *p;
    GArray *tags;

    if (!gwy_tiff_data_fits(tiff, offset, ifdsize, 1)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("TIFF directory %lu ended unexpectedly."), (gulong)tiff->dirs->len);
        return NULL;
    }

    p = tiff->data + offset;
    if (tiff->version == GWY_TIFF_CLASSIC)
        nentries = get_guint16(&p);
    else if (tiff->version == GWY_TIFF_BIG)
        nentries = tiff->get_guint64(&p);
    else {
        g_assert_not_reached();
    }

    if (!gwy_tiff_data_fits(tiff, offset + ifdsize, tagsize, nentries)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("TIFF directory %lu ended unexpectedly."), (gulong)tiff->dirs->len);
        return NULL;
    }

    tags = g_array_sized_new(FALSE, FALSE, sizeof(GwyTIFFEntry), nentries);
    for (i = 0; i < nentries; i++) {
        GwyTIFFEntry entry;

        entry.tag = get_guint16(&p);
        entry.type = (GwyTIFFDataType)get_guint16(&p);
        entry.count = get_length(&p);
        memcpy(entry.value, p, valuesize);
        p += tiff->tagvaluesize;
        g_array_append_val(tags, entry);
    }
    if (pafter)
        *pafter = p;

    return tags;
}

gboolean
gwy_tiff_ifd_is_vaild(const GwyTIFF *tiff, const GArray *tags, GError **error)
{
    const guchar *p;
    guint j;

    for (j = 0; j < tags->len; j++) {
        const GwyTIFFEntry *entry;
        guint64 item_size, offset;

        entry = &g_array_index(tags, GwyTIFFEntry, j);
        if (tiff->version == GWY_TIFF_CLASSIC
            && (entry->type == GWY_TIFF_LONG8 || entry->type == GWY_TIFF_SLONG8 || entry->type == GWY_TIFF_IFD8)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("BigTIFF data type %u was found in a classic TIFF."), entry->type);
            return FALSE;
        }
        p = entry->value;
        offset = tiff->get_length(&p);
        item_size = gwy_tiff_data_type_size(entry->type);
        /* Uknown types are implicitly OK.  If we cannot read it we never read it by definition, so let the hell take
         * whatever it refers to. This also means readers of custom types have to check the size themselves. */
        if (item_size
            && entry->count > tiff->tagvaluesize/item_size
            && !gwy_tiff_data_fits(tiff, offset, item_size, entry->count)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Invalid tag data positions were found."));
            return FALSE;
        }
    }

    return TRUE;
}

/* Does not need to free tags on failure, the caller takes care of it. */
static gboolean
gwy_tiff_load_impl(GwyTIFF *tiff,
                   const gchar *filename,
                   GError **error)
{
    GError *err = NULL;
    GArray *tags;
    const guchar *p;
    guint64 offset;
    gsize size;

    if (!gwy_file_get_contents(filename, &tiff->data, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return FALSE;
    }
    tiff->size = size;

    p = tiff->data;
    if (!(p = gwy_tiff_detect(p, tiff->size, &tiff->version, &tiff->byte_order))) {
        err_FILE_TYPE(error, "TIFF");
        return FALSE;
    }

    if (tiff->byte_order == GWY_BYTE_ORDER_LITTLE_ENDIAN) {
        tiff->get_guint16 = gwy_get_guint16_le;
        tiff->get_gint16 = gwy_get_gint16_le;
        tiff->get_guint32 = gwy_get_guint32_le;
        tiff->get_gint32 = gwy_get_gint32_le;
        tiff->get_guint64 = gwy_get_guint64_le;
        tiff->get_gint64 = gwy_get_gint64_le;
        tiff->get_gfloat = gwy_get_gfloat_le;
        tiff->get_gdouble = gwy_get_gdouble_le;
        if (tiff->version == GWY_TIFF_BIG)
            tiff->get_length = gwy_get_guint64_le;
        else
            tiff->get_length = gwy_get_guint32as64_le;
    }
    else if (tiff->byte_order == GWY_BYTE_ORDER_BIG_ENDIAN) {
        tiff->get_guint16 = gwy_get_guint16_be;
        tiff->get_gint16 = gwy_get_gint16_be;
        tiff->get_guint32 = gwy_get_guint32_be;
        tiff->get_gint32 = gwy_get_gint32_be;
        tiff->get_guint64 = gwy_get_guint64_be;
        tiff->get_gint64 = gwy_get_gint64_be;
        tiff->get_gfloat = gwy_get_gfloat_be;
        tiff->get_gdouble = gwy_get_gdouble_be;
        if (tiff->version == GWY_TIFF_BIG)
            tiff->get_length = gwy_get_guint64_be;
        else
            tiff->get_length = gwy_get_guint32as64_be;
    }
    else {
        g_assert_not_reached();
    }

    if (tiff->version == GWY_TIFF_CLASSIC) {
        tiff->ifdsize = 2 + 4;
        tiff->tagsize = 12;
        tiff->tagvaluesize = 4;
    }
    else if (tiff->version == GWY_TIFF_BIG) {
        if (tiff->size < GWY_TIFF_HEADER_SIZE_BIG) {
            err_TOO_SHORT(error);
            return FALSE;
        }
        tiff->ifdsize = 8 + 8;
        tiff->tagsize = 20;
        tiff->tagvaluesize = 8;
    }
    else {
        g_assert_not_reached();
    }

    if (tiff->version == GWY_TIFF_BIG) {
        guint bytesize = tiff->get_guint16(&p);
        guint reserved0 = tiff->get_guint16(&p);

        if (bytesize != 8 || reserved0 != 0) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("BigTIFF reserved fields are %u and %u instead of 8 and 0."), bytesize, reserved0);
            return FALSE;
        }
    }

    tiff->dirs = g_ptr_array_new();
    while ((offset = tiff->get_length(&p))) {
        if (!(tags = gwy_tiff_scan_ifd(tiff, offset, &p, error)))
            return FALSE;
        g_ptr_array_add(tiff->dirs, tags);
    }

    return TRUE;
}

void
gwy_tiff_free(GwyTIFF *tiff)
{
    if (tiff->dirs) {
        guint i;

        for (i = 0; i < tiff->dirs->len; i++) {
            GArray *dir = (GArray*)g_ptr_array_index(tiff->dirs, i);
            if (dir)
                g_array_free(dir, TRUE);
        }

        g_ptr_array_free(tiff->dirs, TRUE);
    }

    if (tiff->data)
        gwy_file_abandon_contents(tiff->data, tiff->size, NULL);

    g_free(tiff);
}

gboolean
gwy_tiff_tags_valid(const GwyTIFF *tiff, GError **error)
{
    gsize i;

    for (i = 0; i < tiff->dirs->len; i++) {
        const GArray *tags = (const GArray*)g_ptr_array_index(tiff->dirs, i);

        if (!gwy_tiff_ifd_is_vaild(tiff, tags, error))
            return FALSE;
    }

    return TRUE;
}

const GwyTIFFEntry*
gwy_tiff_find_tag_in_dir(const GArray *tags, guint tag)
{
    const GwyTIFFEntry *entry;
    gsize lo, hi, m;

    lo = 0;
    hi = tags->len-1;
    while (hi - lo > 1) {
        m = (lo + hi)/2;
        entry = &g_array_index(tags, GwyTIFFEntry, m);
        if (entry->tag > tag)
            hi = m;
        else
            lo = m;
    }

    entry = &g_array_index(tags, GwyTIFFEntry, lo);
    if (entry->tag == tag)
        return entry;

    entry = &g_array_index(tags, GwyTIFFEntry, hi);
    if (entry->tag == tag)
        return entry;

    return NULL;
}

const GwyTIFFEntry*
gwy_tiff_find_tag(const GwyTIFF *tiff, guint dirno, guint tag)
{
    const GwyTIFFEntry *entry;
    const GArray *tags;

    if (!tiff->dirs)
        return NULL;

    /* If dirno is GWY_TIFF_ANY_DIR, search in all directories. */
    if (dirno == GWY_TIFF_ANY_DIR) {
        for (dirno = 0; dirno < tiff->dirs->len; dirno++) {
            tags = (const GArray*)g_ptr_array_index(tiff->dirs, dirno);
            if ((entry = gwy_tiff_find_tag_in_dir(tags, tag)))
                return entry;
        }
        return NULL;
    }

    if (dirno >= tiff->dirs->len)
        return NULL;

    tags = (const GArray*)g_ptr_array_index(tiff->dirs, dirno);
    return gwy_tiff_find_tag_in_dir(tags, tag);
}


gboolean
gwy_tiff_get_uint_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        guint *retval)
{
    GwyTIFFDataType type;
    const guchar *p;

    if (!entry || entry->count != 1)
        return FALSE;

    /* These always fit, so we do not have to use gwy_tiff_entry_get_data_pointer(). */
    p = entry->value;
    type = entry->type;
    if (type == GWY_TIFF_BYTE)
        *retval = p[0];
    else if (type == GWY_TIFF_SHORT)
        *retval = tiff->get_guint16(&p);
    else if (type == GWY_TIFF_LONG)
        *retval = tiff->get_guint32(&p);
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_uint(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  guint *retval)
{
    return gwy_tiff_get_uint_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

gboolean
gwy_tiff_get_size_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        guint64 *retval)
{
    GwyTIFFDataType type;
    const guchar *p;

    if (!entry || entry->count != 1)
        return FALSE;

    type = entry->type;
    p = gwy_tiff_entry_get_data_pointer(tiff, entry);
    if (type == GWY_TIFF_BYTE)
        *retval = p[0];
    else if (type == GWY_TIFF_SHORT)
        *retval = tiff->get_guint16(&p);
    else if (type == GWY_TIFF_LONG)
        *retval = tiff->get_guint32(&p);
    else if (type == GWY_TIFF_LONG8)
        *retval = tiff->get_guint64(&p);
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_size(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  guint64 *retval)
{
    return gwy_tiff_get_size_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

gboolean
gwy_tiff_get_uints_entry(const GwyTIFF *tiff,
                         const GwyTIFFEntry *entry,
                         guint64 expected_count,
                         gboolean at_least,
                         guint *retval)
{
    GwyTIFFDataType type;
    const guchar *p;
    guint64 i;

    if (!entry || entry->count < expected_count || (!at_least && entry->count != expected_count))
        return FALSE;

    type = entry->type;
    p = gwy_tiff_entry_get_data_pointer(tiff, entry);
    if (type == GWY_TIFF_BYTE) {
        for (i = 0; i < expected_count; i++)
            retval[i] = p[i];
    }
    else if (type == GWY_TIFF_SHORT) {
        for (i = 0; i < expected_count; i++)
            retval[i] = tiff->get_guint16(&p);
    }
    else if (type == GWY_TIFF_LONG) {
        for (i = 0; i < expected_count; i++)
            retval[i] = tiff->get_guint32(&p);
    }
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_uints(const GwyTIFF *tiff,
                   guint dirno,
                   guint tag,
                   guint64 expected_count,
                   gboolean at_least,
                   guint *retval)
{
    return gwy_tiff_get_uints_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), expected_count, at_least, retval);
}

gboolean
gwy_tiff_get_sint_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        gint *retval)
{
    const guchar *p;
    const gchar *q;
    GwyTIFFDataType type;

    if (!entry || entry->count != 1)
        return FALSE;

    /* These always fit, so we do not have to use gwy_tiff_entry_get_data_pointer(). */
    p = entry->value;
    type = entry->type;
    if (type == GWY_TIFF_SBYTE) {
        q = (const gchar*)p;
        *retval = q[0];
    }
    else if (type == GWY_TIFF_BYTE)
        *retval = p[0];
    else if (type == GWY_TIFF_SSHORT)
        *retval = tiff->get_gint16(&p);
    else if (type == GWY_TIFF_SHORT)
        *retval = tiff->get_guint16(&p);
    else if (type == GWY_TIFF_SLONG)
        *retval = tiff->get_gint32(&p);
    /* XXX: If the value does not fit to a signed type, this is wrong no matter what. */
    else if (type == GWY_TIFF_LONG)
        *retval = tiff->get_guint32(&p);
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_sint(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  gint *retval)
{
    return gwy_tiff_get_sint_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

gboolean
gwy_tiff_get_bool_entry(const GwyTIFF *tiff,
                        const GwyTIFFEntry *entry,
                        gboolean *retval)
{
    const guchar *p;
    GwyTIFFDataType type;

    if (!entry || entry->count != 1)
        return FALSE;

    /* These always fit, so we do not have to use gwy_tiff_entry_get_data_pointer(). */
    p = entry->value;
    type = entry->type;
    if (type == GWY_TIFF_SBYTE || type == GWY_TIFF_BYTE)
        *retval = !!p[0];
    else if (type == GWY_TIFF_SHORT || type == GWY_TIFF_SSHORT)
        *retval = !!tiff->get_guint16(&p);
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_bool(const GwyTIFF *tiff,
                  guint dirno,
                  guint tag,
                  gboolean *retval)
{
    return gwy_tiff_get_bool_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

gboolean
gwy_tiff_get_float_entry(const GwyTIFF *tiff,
                         const GwyTIFFEntry *entry,
                         gdouble *retval)
{
    const guchar *p;
    GwyTIFFDataType type;

    if (!entry || entry->count != 1)
        return FALSE;

    type = entry->type;
    p = gwy_tiff_entry_get_data_pointer(tiff, entry);
    if (type == GWY_TIFF_FLOAT)
        *retval = tiff->get_gfloat(&p);
    else if (type == GWY_TIFF_DOUBLE)
        *retval = tiff->get_gdouble(&p);
    else
        return FALSE;

    return TRUE;
}

gboolean
gwy_tiff_get_float(const GwyTIFF *tiff,
                   guint dirno,
                   guint tag,
                   gdouble *retval)
{
    return gwy_tiff_get_float_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

gboolean
gwy_tiff_get_string_entry(const GwyTIFF *tiff,
                          const GwyTIFFEntry *entry,
                          gchar **retval)
{
    const guchar *p;

    if (!entry || entry->type != GWY_TIFF_ASCII)
        return FALSE;

    p = gwy_tiff_entry_get_data_pointer(tiff, entry);
    *retval = g_new(gchar, entry->count);
    memcpy(*retval, p, entry->count);
    (*retval)[entry->count-1] = '\0';

    return TRUE;
}

gboolean
gwy_tiff_get_string(const GwyTIFF *tiff,
                    guint dirno,
                    guint tag,
                    gchar **retval)
{
    return gwy_tiff_get_string_entry(tiff, gwy_tiff_find_tag(tiff, dirno, tag), retval);
}

/* Unpack a data segment compressed using the PackBits algorithm.
 *
 * Returns the number of bytes consumed, except on failure when zero is returned.
 *
 * The caller must provide output buffer which can hold the entire segment. Since TIFF forbids packing across row
 * boundaries, we consider an error when we do not stop exactly at the requested number of bytes. */
static guint
gwy_tiff_unpack_packbits(const guchar *packed,
                         guint packedsize,
                         guchar *unpacked,
                         guint tounpack)
{
    guint x, b, i = 0;

    while (tounpack) {
        if (i == packedsize)
            return 0;

        x = packed[i++];
        if (x <= 127) {
            /* Copy next x+1 bytes literally. */
            x++;
            if (x > packedsize - i || x > tounpack)
                return 0;
            memcpy(unpacked, packed + i, x);
            unpacked += x;
            tounpack -= x;
            i += x;
        }
        else if (x > 128) {
            /* Take the number as negative and copy the next byte x+1 times. */
            x = 257 - x;
            if (i == packedsize || x > tounpack)
                return 0;
            b = packed[i++];
            tounpack -= x;
            while (x--)
                *(unpacked++) = b;
        }
        else {
            /* And this is apparently also a thing (x = 128 AKA -128). */
        }
    }

    return i;
}

static guint
gwy_tiff_lzw_get_code(const guchar *packed, guint packedsize,
                      guint *bitpos, guint nbits)
{
    const guchar *p = packed + *bitpos/8;
    guint bi = *bitpos % 8, x = 0;

    if (*bitpos + nbits > 8*packedsize)
        return G_MAXUINT;

    *bitpos += nbits;

    /* All our codes are larger than one byte so we always consume everything from the first byte. */
    x = ((0xff >> bi) & p[0]) << (nbits + bi - 8);
    if (nbits + bi <= 16)
        /* Another byte is enough. */
        return x | (p[1] >> (16 - nbits - bi));

    /* Another byte is not enough, so consume it all. */
    x |= (p[1] << (nbits + bi - 16));

    /* And with the next byte it is definitely enough because we can get at least 17 bits this way, but TIFF LZW needs
     * at most 12. */
    return x | (p[2] >> (24 - nbits - bi));
}

static inline gboolean
gwy_tiff_lzw_append(const guchar *bytes,
                    guint nbytes,
                    guchar *unpacked,
                    guint tounpack,
                    guint *outpos)
{
    if (nbytes >= tounpack - *outpos) {
        memcpy(unpacked + *outpos, bytes, tounpack - *outpos);
        *outpos = tounpack;
        return TRUE;
    }

    memcpy(unpacked + *outpos, bytes, nbytes);
    *outpos += nbytes;
    return FALSE;
}

static inline gboolean
gwy_tiff_lzw_append1(guint code,
                     guchar *unpacked,
                     guint tounpack,
                     guint *outpos)
{
    unpacked[*outpos] = code;
    (*outpos)++;
    return *outpos == tounpack;
}

static guint
gwy_tiff_unpack_lzw(const guchar *packed,
                    guint packedsize,
                    guchar *unpacked,
                    guint tounpack)
{
    enum {
        GWY_TIFF_NLZW = 4096,
        GWY_TIFF_LZW_CLEAR = 0x100,
        GWY_TIFF_LZW_END = 0x101,
        GWY_TIFF_LZW_FIRST = 0x102,
    };
    typedef struct {
        guint pos;
        guint which : 1;
        guint len : 31;
    } GwyTIFF_LZWCode;
    guint code, prev, i, bitpos, table_pos, nbits, outpos, len, retval = 0;
    GByteArray *buffer;
    GwyTIFF_LZWCode *table;
    const guchar *t;

    table = g_new(GwyTIFF_LZWCode, GWY_TIFF_NLZW);
    buffer = g_byte_array_sized_new(8192);
    g_byte_array_set_size(buffer, 0x100);
    /* We should never need the pos fields for the first 0x100 entries. */
    for (i = 0; i < 0x100; i++) {
        table[i].len = 1;
        table[i].which = 1;
        table[i].pos = i;
        buffer->data[i] = i;
    }

    table_pos = GWY_TIFF_LZW_FIRST;
    nbits = 9;
    bitpos = 0;
    outpos = 0;
    prev = sizeof("Silly, silly GCC");
    while (TRUE) {
        code = gwy_tiff_lzw_get_code(packed, packedsize, &bitpos, nbits);
        if (!i && code != GWY_TIFF_LZW_CLEAR) {
            gwy_debug("first code is not CLEAR");
            goto finalise;
        }
        if (code == GWY_TIFF_LZW_END) {
end:
            if (outpos == tounpack)
                retval = bitpos/8;
            else {
                gwy_debug("stream is shorter than requested");
            }
            goto finalise;
        }
        else if (code == GWY_TIFF_LZW_CLEAR) {
            nbits = 9;
            code = gwy_tiff_lzw_get_code(packed, packedsize, &bitpos, nbits);
            if (code > 0x100) {
                gwy_debug("first code after CLEAR is > 0x100");
                goto finalise;
            }
            if (code == GWY_TIFF_LZW_END)
                goto end;
            if (gwy_tiff_lzw_append1(code, unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos = GWY_TIFF_LZW_FIRST;
            g_byte_array_set_size(buffer, 0x100);
        }
        else if (code < table_pos) {
            table[table_pos].which = 1;
            table[table_pos].pos = buffer->len;
            table[table_pos].len = table[prev].len + 1;
            t = (table[prev].which ? buffer->data : unpacked) + table[prev].pos;
            g_byte_array_append(buffer, (const guint8*)t, table[prev].len);
            t = (table[code].which ? buffer->data : unpacked) + table[code].pos;
            len = table[code].len;
            g_byte_array_append(buffer, (const guint8*)t, 1);
            if (gwy_tiff_lzw_append(t, len, unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos++;
        }
        else if (code == table_pos) {
            table[table_pos].which = 0;
            table[table_pos].pos = outpos;
            table[table_pos].len = table[prev].len + 1;
            t = (table[prev].which ? buffer->data : unpacked) + table[prev].pos;
            len = table[prev].len;
            if (gwy_tiff_lzw_append(t, len, unpacked, tounpack, &outpos)
                || gwy_tiff_lzw_append1(t[0], unpacked, tounpack, &outpos)) {
                retval = bitpos/8;
                goto finalise;
            }
            table_pos++;
        }
        else {
            /* Any unseen code must be the next available.  Getting some other large number means things went awry.
             * This also covers getting G_MAXUINT from get-code. */
            gwy_debug("random unseen large code %u (expecting %u)", code, table_pos);
            goto finalise;
        }

        if (table_pos == 511 || table_pos == 1023 || table_pos == 2047)
            nbits++;
        if (table_pos == 4095) {
            gwy_debug("reached table pos 4095, so the next code would be 13bit, even if it was CLEAR");
            goto finalise;
        }
        prev = code;
    }

finalise:
    g_free(table);
    g_byte_array_free(buffer, TRUE);

    return retval;
}

/* Used for strip/tile offsets and byte counts. */
static gboolean
gwy_tiff_read_image_reader_sizes(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 GwyTIFFTag tag,
                                 guint64 *values,
                                 guint nvalues,
                                 GError **error)
{
    GwyTIFFDataType type;
    const GwyTIFFEntry *entry;
    const guchar *p;
    guint64 i;

    if (nvalues == 1) {
        if (!gwy_tiff_get_size(tiff, reader->dirno, tag, values))
            return !!err_TIFF_REQUIRED_TAG(error, tag);
        return TRUE;
    }

    /* Do not require entry->count == nvalues because some (Zeiss LSM) like to add extra zero-length strips to the end
     * just for extra fun (and we validate the values we actually read and need). */
    if (!(entry = gwy_tiff_find_tag(tiff, reader->dirno, tag))
        || (entry->type != GWY_TIFF_SHORT && entry->type != GWY_TIFF_LONG && entry->type != GWY_TIFF_LONG8)
        || entry->count < nvalues) {
        return !!err_TIFF_REQUIRED_TAG(error, tag);
    }

    /* Matching type ensured the tag data is at a valid position in the file. */
    type = entry->type;
    p = gwy_tiff_entry_get_data_pointer(tiff, entry);
    if (type == GWY_TIFF_LONG) {
        for (i = 0; i < nvalues; i++)
            values[i] = tiff->get_guint32(&p);
    }
    else if (type == GWY_TIFF_LONG8) {
        for (i = 0; i < nvalues; i++)
            values[i] = tiff->get_guint64(&p);
    }
    else if (type == GWY_TIFF_SHORT) {
        for (i = 0; i < nvalues; i++)
            values[i] = tiff->get_guint16(&p);
    }
    else {
        g_return_val_if_reached(FALSE);
    }

    return TRUE;
}

static gboolean
gwy_tiff_init_image_reader_striped(const GwyTIFF *tiff,
                                   GwyTIFFImageReader *reader,
                                   GError **error)
{
    guint64 nstrips, i, j, strip_size, max_strip_size;
    guint p, nplanes, spp;

    if (reader->strip_rows == 0) {
        err_INVALID(error, "RowsPerStrip");
        return FALSE;
    }

    spp = reader->samples_per_pixel;
    nplanes = (reader->planar_config == GWY_TIFF_PLANAR_CONFIG_SEPARATE ? spp : 1);
    nstrips = gwy_tiff_round_up_div(reader->height, reader->strip_rows);
    reader->offsets = g_new(guint64, nstrips * nplanes);
    reader->bytecounts = g_new(guint64, nstrips * nplanes);
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_STRIP_OFFSETS,
                                          reader->offsets, nstrips * nplanes, error))
        goto fail;
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, GWY_TIFFTAG_STRIP_BYTE_COUNTS,
                                          reader->bytecounts, nstrips * nplanes, error))
        goto fail;

    /* Calculate row strides. For SEPARATE planar config they can differ among channels – and they are also shorter
     * because they contain only single channel data. */
    if (nplanes > 1)
        reader->rowstride = gwy_tiff_round_up_div(reader->bits_per_sample * reader->width, 8);
    else
        reader->rowstride = gwy_tiff_round_up_div(reader->bits_per_sample * spp * reader->width, 8);

    /* Validate strip offsets and sizes.  Strips are not padded so the last strip can be shorter.*/
    /* For SEPARATE planar config, the outer index is plane (channel) and the inner strip within the channel. */
    max_strip_size = reader->rowstride * reader->strip_rows;
    for (p = 0; p < nplanes; p++) {
        strip_size = max_strip_size;
        for (i = 0; i < nstrips; i++) {
            if (i == nstrips-1 && reader->height % reader->strip_rows)
                strip_size = reader->rowstride * (reader->height % reader->strip_rows);
            j = p*nstrips + i;
            if ((reader->compression == GWY_TIFF_COMPRESSION_NONE && strip_size != reader->bytecounts[j])
                || reader->offsets[j] + reader->bytecounts[j] > tiff->size) {
                err_INVALID(error, "StripOffsets");
                goto fail;
            }
        }
    }

    if (reader->compression != GWY_TIFF_COMPRESSION_NONE) {
        reader->unpacked_alloc_size = max_strip_size;
        reader->unpacked = g_new(guchar, max_strip_size);
    }

    return TRUE;

fail:
    GWY_FREE(reader->offsets);
    GWY_FREE(reader->bytecounts);
    return FALSE;
}

static gboolean
gwy_tiff_init_image_reader_tiled(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 GError **error)
{
    guint64 nhtiles, nvtiles, ntiles, i, j, tsize, twidth, theight, max_strip_size;
    guint nplanes, p, spp;
    GwyTIFFTag offsets_tag = GWY_TIFFTAG_TILE_OFFSETS, bytes_tag = GWY_TIFFTAG_TILE_BYTE_COUNTS;

    twidth = reader->tile_width;
    if (twidth == 0 || tiff->size/twidth == 0) {
        err_INVALID(error, "TileWidth");
        return FALSE;
    }
    theight = reader->tile_height;
    if (theight == 0 || tiff->size/theight == 0) {
        err_INVALID(error, "TileLength");   /* The specs calls it length. */
        return FALSE;
    }

    spp = reader->samples_per_pixel;
    nplanes = (reader->planar_config == GWY_TIFF_PLANAR_CONFIG_SEPARATE ? spp : 1);
    nhtiles = gwy_tiff_round_up_div(reader->width, twidth);
    nvtiles = gwy_tiff_round_up_div(reader->height, theight);
    ntiles = nhtiles*nvtiles;
    reader->offsets = g_new(guint64, ntiles * nplanes);
    reader->bytecounts = g_new(guint64, ntiles * nplanes);
    /* XXX: The TIFF test image set contains an image which is tiled but uses the Strip (NOT Tile) tags to give
     * the offsets and byte counts. So it is valid somehow? */
    if (!gwy_tiff_find_tag(tiff, reader->dirno, offsets_tag)
        && !gwy_tiff_find_tag(tiff, reader->dirno, bytes_tag)
        && gwy_tiff_find_tag(tiff, reader->dirno, GWY_TIFFTAG_STRIP_OFFSETS)
        && gwy_tiff_find_tag(tiff, reader->dirno, GWY_TIFFTAG_STRIP_BYTE_COUNTS)) {
        offsets_tag = GWY_TIFFTAG_STRIP_OFFSETS;
        bytes_tag = GWY_TIFFTAG_STRIP_BYTE_COUNTS;
    }
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, offsets_tag, reader->offsets, ntiles * nplanes, error))
        goto fail;
    if (!gwy_tiff_read_image_reader_sizes(tiff, reader, bytes_tag, reader->bytecounts, ntiles * nplanes, error))
        goto fail;

    /* Calculate row strides. For SEPARATE planar config they can differ among channels – and they are also shorter
     * because they contain only single channel data. */
    if (nplanes > 1)
        reader->rowstride = gwy_tiff_round_up_div(reader->bits_per_sample * twidth, 8);
    else
        reader->rowstride = gwy_tiff_round_up_div(reader->bits_per_sample * spp * twidth, 8);

    /* Validate tile offsets and sizes.  Tiles are padded so size must be reserved for entire tiles.  The standard
     * says the tile width must be a multiple of 16 so we can ignore alignment as only invalid files would need row
     * padding.
     *
     * The strip size is for an entire row of tiles because that much space we would use for decompressed data. */
    max_strip_size = reader->rowstride * theight;
    for (p = 0; p < nplanes; p++) {
        tsize = max_strip_size;
        for (i = 0; i < ntiles; i++) {
            j = p*ntiles + i;
            if ((reader->compression == GWY_TIFF_COMPRESSION_NONE && tsize != reader->bytecounts[j])
                || reader->offsets[j] + reader->bytecounts[j] > tiff->size) {
                err_INVALID(error, "TileOffsets");
                goto fail;
            }
        }
    }

    if (reader->compression != GWY_TIFF_COMPRESSION_NONE) {
        reader->unpacked_alloc_size = max_strip_size;
        reader->unpacked = g_new(guchar, max_strip_size);
    }

    return TRUE;

fail:
    GWY_FREE(reader->offsets);
    GWY_FREE(reader->bytecounts);
    return FALSE;
}

static gboolean
gwy_tiff_reader_find_raw_type(GwyTIFFImageReader *reader)
{
    struct {
        guint bps;
        GwyRawDataType unsigned_type;
        GwyRawDataType signed_type;
        GwyRawDataType float_type;
    } supported_bit_depths[] = {
        { 1,  GWY_RAW_DATA_BIT,    GWY_RAW_DATA_BIT,    (GwyRawDataType)-1,  },
        { 4,  GWY_RAW_DATA_UINT4,  GWY_RAW_DATA_SINT4,  (GwyRawDataType)-1,  },
        { 8,  GWY_RAW_DATA_UINT8,  GWY_RAW_DATA_SINT8,  (GwyRawDataType)-1,  },
        { 12, GWY_RAW_DATA_UINT12, GWY_RAW_DATA_SINT12, (GwyRawDataType)-1,  },
        { 16, GWY_RAW_DATA_UINT16, GWY_RAW_DATA_SINT16, GWY_RAW_DATA_HALF,   },
        { 24, GWY_RAW_DATA_UINT24, GWY_RAW_DATA_SINT24, (GwyRawDataType)-1,  },
        { 32, GWY_RAW_DATA_UINT32, GWY_RAW_DATA_SINT32, GWY_RAW_DATA_FLOAT,  },
        { 64, GWY_RAW_DATA_UINT64, GWY_RAW_DATA_SINT64, GWY_RAW_DATA_DOUBLE, },
    };
    guint i, bps = reader->bits_per_sample;
    GwyTIFFSampleFormat sformat = reader->sample_format;
    GwyRawDataType rawtype = (GwyRawDataType)-1;

    for (i = 0; i < G_N_ELEMENTS(supported_bit_depths); i++) {
        if (bps == supported_bit_depths[i].bps) {
            if (sformat == GWY_TIFF_SAMPLE_FORMAT_FLOAT)
                rawtype = supported_bit_depths[i].float_type;
            else if (sformat == GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER)
                rawtype = supported_bit_depths[i].unsigned_type;
            else if (sformat == GWY_TIFF_SAMPLE_FORMAT_SIGNED_INTEGER)
                rawtype = supported_bit_depths[i].signed_type;
            break;
        }
    }

    if (rawtype == (GwyRawDataType)-1)
        return FALSE;

    reader->rawtype = rawtype;
    return TRUE;
}

GwyTIFFImageReader*
gwy_tiff_get_image_reader(const GwyTIFF *tiff,
                          guint dirno,
                          guint max_samples,
                          GError **error)
{
    GwyTIFFImageReader reader;
    guint i, v, spp;
    guint *bps;

    gwy_clear(&reader, 1);
    reader.dirno = dirno;
    reader.which_unpacked = G_MAXUINT64;

    /* Required integer fields */
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_IMAGE_WIDTH, &reader.width))
        return (GwyTIFFImageReader*)err_TIFF_REQUIRED_TAG(error, GWY_TIFFTAG_IMAGE_WIDTH);
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_IMAGE_LENGTH, &reader.height))
        return (GwyTIFFImageReader*)err_TIFF_REQUIRED_TAG(error, GWY_TIFFTAG_IMAGE_LENGTH);

    /* The TIFF specs say this is required, but it seems to default to 1. */
    if (!gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_SAMPLES_PER_PIXEL, &reader.samples_per_pixel))
        reader.samples_per_pixel = 1;
    spp = reader.samples_per_pixel;
    if (spp == 0 || spp > max_samples) {
        err_UNSUPPORTED(error, "SamplesPerPixel");
        return NULL;
    }

    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_EXTRA_SAMPLES, &reader.extra_samples)) {
        if (reader.extra_samples >= reader.samples_per_pixel) {
            err_INVALID(error, "ExtraSamples");
            return NULL;
        }
    }

    /* Planar config. Currently we only support contiguous. */
    reader.planar_config = GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_PLANAR_CONFIG, &v)) {
        reader.planar_config = (GwyTIFFPlanarConfig)v;
        if (reader.planar_config != GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS
            && reader.planar_config != GWY_TIFF_PLANAR_CONFIG_SEPARATE) {
            err_UNSUPPORTED(error, "PlanarConfig");
            return NULL;
        }
        /* This may not even be a valid TIFF, but just fix the config to contiguous if there is just one channel. */
        if (spp == 1)
            reader.planar_config = GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS;
    }

    /* The TIFF specs say this is required, but it seems to default to 1. */
    reader.bits_per_sample = 1;
    bps = g_new(guint, spp);
    /* Zeiss LSM gives three BitsPerSample values for two channels. Other TIFF readers do not seem to complain about
     * it, so apparently redundant values are not a big deal (it is not even possible to create a file with different
     * BitsPerSample for each channel using libTIFF, so no one probably cares). Pass TRUE for at_least. I still do not
     * like it as a general behaviour. */
    if (gwy_tiff_get_uints(tiff, dirno, GWY_TIFFTAG_BITS_PER_SAMPLE, spp, TRUE, bps)) {
        for (i = 1; i < spp; i++) {
            if (bps[i] != bps[i-1]) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Non-uniform bits per sample are unsupported."));
                g_free(bps);
                return NULL;
            }
        }
        reader.bits_per_sample = bps[0];
    }
    g_free(bps);

    /* The TIFF specs say this is required, but it seems to default to MAXINT.  Setting more reasonably
     * RowsPerStrip = ImageLength achieves the same end.  Also it is not required for tiled images. */
    if (!gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_ROWS_PER_STRIP, &reader.strip_rows))
        reader.strip_rows = reader.height;

    /* The data sample type (default is unsigned integer).
     * NB: Explicit typecasts to placate C++ compilers. */
    reader.sample_format = GWY_TIFF_SAMPLE_FORMAT_UNSIGNED_INTEGER;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_SAMPLE_FORMAT, &v))
        reader.sample_format = (GwyTIFFSampleFormat)v;

    /* Orientation. */
    /* We ignore it and behave as if it was TOPLEFT (which is OK for a baseline reader).
     * This is probably sensible because the reading is normally done by image row anyway so we do not really have
     * any means to change them to image columns read from the right or something. Let the caller fix it later. */
    reader.orientation = GWY_TIFF_ORIENTATION_TOPLEFT;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_ORIENTATION, &v))
        reader.orientation = (GwyTIFFOrientation)v;

    /* Photometric. Informative for the caller who will just get the raw data. Except for palette images which we do
     * not pretend we know how to read. */
    reader.photometric = (spp > 1 ? GWY_TIFF_PHOTOMETRIC_RGB : GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK);
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_PHOTOMETRIC, &v)) {
        reader.photometric = (GwyTIFFPhotometric)v;
        /* XXX: Unfortunately, some idiots use PALETTE images for data. So, even though refusing PALETTE images is
         * correct we cannot do it.
         *
         * In the best case the palette should be just ignored then and the palette indices 0-255 simply used as
         * integer data. But it is also possible they just do not understand the difference between data and a picture
         * of data, so the data are alrerady irreversibly mangled by pseudocolour mapping and there is nothing we can
         * do. */
        /*
        if (reader.photometric == GWY_TIFF_PHOTOMETRIC_PALETTE) {
            err_UNSUPPORTED(error, "PhotometricInterpreation");
            return NULL;
        }
        */
    }

    /* Subfile type, purely informative for the caller. */
    reader.subfiletype = GWY_TIFF_SUBFILE_FULL_IMAGE_DATA;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_SUB_FILE_TYPE, &v))
        reader.subfiletype = (GwyTIFFSubFileType)v;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_NEW_SUB_FILE_TYPE, &v))
        reader.newsubfiletype = (GwyTIFFNewSubFileType)v;

    /* Compression. The default is uncompressed (and in fact we do not allow compression by default). */
    reader.compression = GWY_TIFF_COMPRESSION_NONE;
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_COMPRESSION, &v))
        reader.compression = (GwyTIFFCompression)v;

    /* We may also fail later if compression does not mix well with some other feature. */
    if (tiff->allow_compressed) {
        if (reader.compression == GWY_TIFF_COMPRESSION_PACKBITS)
            reader.unpack_func = gwy_tiff_unpack_packbits;
        else if (reader.compression == GWY_TIFF_COMPRESSION_LZW)
            reader.unpack_func = gwy_tiff_unpack_lzw;
        else if (reader.compression != GWY_TIFF_COMPRESSION_NONE) {
            err_UNSUPPORTED(error, "Compression");
            return NULL;
        }
    }
    else if (reader.compression != GWY_TIFF_COMPRESSION_NONE) {
        err_UNSUPPORTED(error, "Compression");
        return NULL;
    }

    /* Check for some features which we do not support and would seriously mess up the image if used.
     *
     * Predictor can be implemented easily for floating point types because we can do the de-differencing on the
     * result (just do not add z0 immediately, only at the end). However, both signed and usigned integers rely on
     * integer wraparound. Even though it can be emulated with the resulting floating point data (of course slower
     * than with the integers), it is messy. */
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_FILL_ORDER, &v) && v != GWY_TIFF_FILL_ORDER_MSBITS) {
        err_UNSUPPORTED(error, "FillOrder");
        return NULL;
    }
    if (gwy_tiff_get_uint(tiff, dirno, GWY_TIFFTAG_PREDICTOR, &v) && v != GWY_TIFF_PREDICTOR_NONE) {
        err_UNSUPPORTED(error, "Predictor");
        return NULL;
    }

    /* Sample format and bits per sample combinations. */
    if (!gwy_tiff_reader_find_raw_type(&reader)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Unsupported sample format"));
        return NULL;
    }

    /* Apparently in Zeiss SEM files, RowsPerStrip can be *anything* larger than the imager height. */
    if (reader.strip_rows > reader.height)
        reader.strip_rows = reader.height;

    if (err_DIMENSION(error, reader.width) || err_DIMENSION(error, reader.height))
        return NULL;

    /* If we can read the tile dimensions assume it is a tiled image and report possible errors as for a tiled image.
     * If the image contains just one of them ignore it (and report errors as for a non-tiled image). */
    if (gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_TILE_WIDTH, &reader.tile_width)
        && gwy_tiff_get_size(tiff, dirno, GWY_TIFFTAG_TILE_LENGTH, &reader.tile_height)) {
        reader.strip_rows = 0;
        if (!gwy_tiff_init_image_reader_tiled(tiff, &reader, error))
            return NULL;
    }
    else {
        reader.tile_width = reader.tile_height = 0;
        if (!gwy_tiff_init_image_reader_striped(tiff, &reader, error))
            return NULL;
    }

    /* If we got here we are convinced we can read the image data. */
    return (GwyTIFFImageReader*)g_memdup(&reader, sizeof(GwyTIFFImageReader));
}

/* For sub-byte data with offset we must figure out the correct sub-byte addressing.
 * And for 12bit data it is extra funny mess. */
static inline void
gwy_tiff_adjust_params_for_fractional_size(guint bps, guint nplanes, guint spp, guint channelno,
                                           guint *stride, GwyByteOrder *byteorder)
{
    if (bps < 8) {
        *stride *= 8/bps;
        if (nplanes == 1 && spp > 1)
            *stride += ((bps*channelno) % 8)/bps;
        *byteorder = GWY_BYTE_ORDER_BIG_ENDIAN;
    }
    else if (bps == 12) {
        *stride *= 2;
        if (nplanes == 1 && spp > 1)
            *stride += channelno % 2;
        *byteorder = GWY_BYTE_ORDER_BIG_ENDIAN;
    }
}

static gboolean
gwy_tiff_read_image_row_striped(const GwyTIFF *tiff,
                                GwyTIFFImageReader *reader,
                                guint channelno,
                                guint rowno,
                                gdouble q,
                                gdouble z0,
                                gdouble *dest)
{
    GwyRawDataType rawtype = reader->rawtype;
    GwyByteOrder byteorder = tiff->byte_order;
    guint spp, bps, nplanes, stride;
    guint64 stripno, stripindex, nstrips, nstrip_rows;
    gsize rowstride, offset;
    const guchar *p;

    spp = reader->samples_per_pixel;
    g_return_val_if_fail(channelno < spp, FALSE);
    bps = reader->bits_per_sample;
    rowstride = reader->rowstride;
    nplanes = (reader->planar_config == GWY_TIFF_PLANAR_CONFIG_SEPARATE ? spp : 1);
    nstrip_rows = reader->strip_rows;
    nstrips = gwy_tiff_round_up_div(reader->height, nstrip_rows);
    stripno = (nplanes > 1 ? channelno*nstrips : 0) + rowno/nstrip_rows;
    stripindex = rowno % nstrip_rows;
    stride = spp/nplanes;
    p = tiff->data + reader->offsets[stripno];
    if (reader->unpack_func) {
        g_assert(reader->unpacked);
        /* If we want a row from different stripe than current we unpack the stripe. */
        if (stripno != reader->which_unpacked) {
            if (stripno == nstrips-1 && reader->height % nstrip_rows)
                nstrip_rows = reader->height % nstrip_rows;
            g_assert(rowstride * nstrip_rows <= reader->unpacked_alloc_size);
            if (!reader->unpack_func(p, reader->bytecounts[stripno], reader->unpacked, rowstride * nstrip_rows))
                return FALSE;
            reader->which_unpacked = stripno;
        }
        /* Read from the unpacked buffer instead of the file data. */
        p = reader->unpacked;
    }
    offset = stripindex*rowstride + (nplanes > 1 ? 0 : bps*channelno/8);
    gwy_tiff_adjust_params_for_fractional_size(bps, nplanes, spp, channelno, &stride, &byteorder);
    gwy_convert_raw_data(p + offset, reader->width, stride, rawtype, byteorder, dest, q, z0);

    return TRUE;
}

static gboolean
gwy_tiff_read_image_row_tiled(const GwyTIFF *tiff,
                              GwyTIFFImageReader *reader,
                              guint channelno,
                              guint rowno,
                              gdouble q,
                              gdouble z0,
                              gdouble *dest)
{
    GwyRawDataType rawtype = reader->rawtype;
    GwyByteOrder byteorder = tiff->byte_order;
    guint bps, spp, nplanes, stride;
    guint64 nhtiles, nvtiles, theight, twidth, tsize, firsttileno, vtileno, vtileindex, rowstride, offset;
    guint64 len, toread, i;
    const guchar *p;

    spp = reader->samples_per_pixel;
    g_return_val_if_fail(channelno < spp, FALSE);
    bps = reader->bits_per_sample;
    rowstride = reader->rowstride;
    nplanes = (reader->planar_config == GWY_TIFF_PLANAR_CONFIG_SEPARATE ? spp : 1);
    theight = reader->tile_height;
    twidth = reader->tile_width;
    tsize = theight * rowstride;
    nhtiles = gwy_tiff_round_up_div(reader->width, twidth);
    nvtiles = gwy_tiff_round_up_div(reader->height, theight);
    vtileno = rowno/theight;
    firsttileno = ((nplanes > 1 ? channelno*nvtiles : 0) + vtileno)*nhtiles;
    vtileindex = rowno % theight;
    stride = spp/nplanes;
    toread = reader->width;
    gwy_tiff_adjust_params_for_fractional_size(bps, nplanes, spp, channelno, &stride, &byteorder);
    if (reader->unpack_func) {
        /* We unpack all tiles within a horizontal stripe (these tile stripes are indexed by vtileno) and thus
         * basically convert the reading to stripes. The resulting data layout is still messy though. We just have
         * concatenated tile data, not contiguous rows! Tiles should be padded, i.e. full size, but out dest buffer
         * is not. */
        g_assert(reader->unpacked);
        /* If we want a row from different stripe than current we unpack the stripe. */
        if (vtileno != reader->which_unpacked) {
            g_assert(tsize * nhtiles <= reader->unpacked_alloc_size);
            for (i = 0; i < nhtiles; i++) {
                p = tiff->data + reader->offsets[firsttileno + i];
                if (!reader->unpack_func(p, reader->bytecounts[firsttileno + i], reader->unpacked + i*tsize, tsize))
                    return FALSE;
            }
            reader->which_unpacked = vtileno;
        }
        /* Read from the unpacked buffer instead of the file data. */
        for (i = 0; i < nhtiles; i++) {
            p = reader->unpacked + i*tsize + vtileindex*rowstride;
            len = MIN(toread, twidth);
            gwy_convert_raw_data(p, len, stride, rawtype, byteorder, dest, q, z0);
            dest += len;
            toread -= len;
        }
    }
    else {
        for (i = 0; i < nhtiles; i++) {
            offset = reader->offsets[firsttileno + i] + vtileindex*rowstride + (nplanes > 1 ? 0 : bps*channelno/8);
            p = tiff->data + offset;
            len = MIN(toread, twidth);
            gwy_convert_raw_data(p, len, stride, rawtype, byteorder, dest, q, z0);
            dest += len;
            toread -= len;
        }
    }

    return TRUE;
}

/* If the file may be compressed (which needs to be explicitly allowed using gwy_tiff_allow_compressed()) this
 * function should to be called with rowno in a mononotonically increasing sequence.  Anything else can result in
 * repeated unpacking data from the beginning and quadratic time complexity. */
gboolean
gwy_tiff_read_image_row(const GwyTIFF *tiff,
                        GwyTIFFImageReader *reader,
                        guint channelno,
                        guint rowno,
                        gdouble q,
                        gdouble z0,
                        gdouble *dest)
{
    g_return_val_if_fail(tiff, FALSE);
    g_return_val_if_fail(reader, FALSE);
    g_return_val_if_fail(reader->dirno < tiff->dirs->len, FALSE);
    g_return_val_if_fail(rowno < reader->height, FALSE);
    g_return_val_if_fail(channelno < reader->samples_per_pixel, FALSE);
    if (reader->strip_rows) {
        g_return_val_if_fail(!reader->tile_width, FALSE);
        return gwy_tiff_read_image_row_striped(tiff, reader, channelno, rowno, q, z0, dest);
    }
    g_return_val_if_fail(reader->tile_width, FALSE);
    g_return_val_if_fail(reader->tile_height, FALSE);
    return gwy_tiff_read_image_row_tiled(tiff, reader, channelno, rowno, q, z0, dest);
}

gboolean
gwy_tiff_read_image_row_averaged(const GwyTIFF *tiff,
                                 GwyTIFFImageReader *reader,
                                 guint rowno,
                                 gdouble q,
                                 gdouble z0,
                                 gdouble *dest)
{
    gint ch, j, width, spp = reader->samples_per_pixel;
    gdouble *rowbuf;

    g_return_val_if_fail(spp >= 1, FALSE);

    q /= spp;
    if (!gwy_tiff_read_image_row(tiff, reader, 0, rowno, q, z0, dest))
        return FALSE;
    if (spp == 1)
        return TRUE;

    width = reader->width;
    if (!reader->rowbuf)
        reader->rowbuf = g_new(gdouble, width);

    rowbuf = reader->rowbuf;
    for (ch = 1; ch < spp; ch++) {
        if (!gwy_tiff_read_image_row(tiff, reader, ch, rowno, q, 0.0, rowbuf))
            return FALSE;
        for (j = 0; j < width; j++)
            dest[j] += rowbuf[j];
    }

    return TRUE;
}

GwyTIFFImageReader*
gwy_tiff_image_reader_free(GwyTIFFImageReader *reader)
{
    if (reader) {
        g_free(reader->offsets);
        g_free(reader->bytecounts);
        g_free(reader->unpacked);
        g_free(reader->rowbuf);
        g_free(reader);
    }
    return NULL;
}

guint
gwy_tiff_get_n_dirs(const GwyTIFF *tiff)
{
    if (!tiff->dirs)
        return 0;

    return tiff->dirs->len;
}

static gint
gwy_tiff_tag_compare(gconstpointer a, gconstpointer b)
{
    const GwyTIFFEntry *ta = (const GwyTIFFEntry*)a;
    const GwyTIFFEntry *tb = (const GwyTIFFEntry*)b;

    if (ta->tag < tb->tag)
        return -1;
    if (ta->tag > tb->tag)
        return 1;
    return 0;
}

void
gwy_tiff_sort_tag_array(GArray *tags)
{
    g_array_sort(tags, gwy_tiff_tag_compare);
}

static void
gwy_tiff_sort_tags(GwyTIFF *tiff)
{
    gsize i;

    for (i = 0; i < tiff->dirs->len; i++)
        gwy_tiff_sort_tag_array((GArray*)g_ptr_array_index(tiff->dirs, i));
}

GwyTIFF*
gwy_tiff_load(const gchar *filename,
              GError **error)
{
    GwyTIFF *tiff;

    tiff = g_new0(GwyTIFF, 1);
    if (gwy_tiff_load_impl(tiff, filename, error) && gwy_tiff_tags_valid(tiff, error)) {
        gwy_tiff_sort_tags(tiff);
        return tiff;
    }

    gwy_tiff_free(tiff);
    return NULL;
}

/* vim: set cin et columns=120 tw=118 ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
