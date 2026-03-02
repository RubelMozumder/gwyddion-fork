/*
 *  $Id: gwyutils.h 26027 2023-11-15 10:53:53Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
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

#ifndef __GWY_UTILS_H__
#define __GWY_UTILS_H__

#include <glib-object.h>
#include <stdio.h>

G_BEGIN_DECLS

typedef enum {
    GWY_RAW_DATA_SINT8,
    GWY_RAW_DATA_UINT8,
    GWY_RAW_DATA_SINT16,
    GWY_RAW_DATA_UINT16,
    GWY_RAW_DATA_SINT32,
    GWY_RAW_DATA_UINT32,
    GWY_RAW_DATA_SINT64,
    GWY_RAW_DATA_UINT64,
    GWY_RAW_DATA_HALF,
    GWY_RAW_DATA_FLOAT,
    GWY_RAW_DATA_REAL,
    GWY_RAW_DATA_DOUBLE,
    GWY_RAW_DATA_BIT,
    GWY_RAW_DATA_SINT4,
    GWY_RAW_DATA_UINT4,
    GWY_RAW_DATA_SINT12,
    GWY_RAW_DATA_UINT12,
    GWY_RAW_DATA_SINT24,
    GWY_RAW_DATA_UINT24,
} GwyRawDataType;

typedef enum {
    GWY_BYTE_ORDER_NATIVE        = 0,
    GWY_BYTE_ORDER_LITTLE_ENDIAN = G_LITTLE_ENDIAN,
    GWY_BYTE_ORDER_BIG_ENDIAN    = G_BIG_ENDIAN,
    GWY_BYTE_ORDER_IMPLICIT      = 9999,
} GwyByteOrder;

typedef enum {
    GWY_PARSE_DOUBLES_EMPTY_OK   = 1 << 0,
    GWY_PARSE_DOUBLES_COMPLETELY = 1 << 1,
    GWY_PARSE_DOUBLES_FREE_FORM  = 1 << 2,
} GwyParseDoublesFlags;

typedef enum {
    GWY_PARSE_DOUBLES_EMPTY,
    GWY_PARSE_DOUBLES_TRUNCATED,
    GWY_PARSE_DOUBLES_MALFORMED,
    GWY_PARSE_DOUBLES_NONUNIFORM,
} GwyParseDoublesError;

typedef gboolean (*GwySetFractionFunc)(gdouble fraction);
typedef gboolean (*GwySetMessageFunc)(const gchar *message);

#define GWY_PARSE_DOUBLES_ERROR gwy_parse_doubles_error_quark()

GQuark       gwy_parse_doubles_error_quark(void);
void         gwy_hash_table_to_slist_cb   (gpointer unused_key,
                                           gpointer value,
                                           gpointer user_data);
void         gwy_hash_table_to_list_cb    (gpointer unused_key,
                                           gpointer value,
                                           gpointer user_data);
gchar*       gwy_strkill                  (gchar *s,
                                           const gchar *killchars);
gchar*       gwy_strreplace               (const gchar *haystack,
                                           const gchar *needle,
                                           const gchar *replacement,
                                           gsize maxrepl)                G_GNUC_MALLOC;
gint         gwy_strdiffpos               (const gchar *s1,
                                           const gchar *s2);
gboolean     gwy_strisident               (const gchar *s,
                                           const gchar *more,
                                           const gchar *startmore);
gboolean     gwy_ascii_strcase_equal      (gconstpointer v1,
                                           gconstpointer v2);
guint        gwy_ascii_strcase_hash       (gconstpointer v);
guint        gwy_stramong                 (const gchar *str,
                                           ...)                          G_GNUC_NULL_TERMINATED;
gpointer     gwy_memmem                   (gconstpointer haystack,
                                           gsize haystack_len,
                                           gconstpointer needle,
                                           gsize needle_len);
gboolean     gwy_file_get_contents        (const gchar *filename,
                                           guchar **buffer,
                                           gsize *size,
                                           GError **error);
gboolean     gwy_file_abandon_contents    (guchar *buffer,
                                           gsize size,
                                           GError **error);
gchar*       gwy_find_self_dir            (const gchar *dirname);
const gchar* gwy_get_user_dir             (void);
const gchar* gwy_get_home_dir             (void);
gchar*       gwy_canonicalize_path        (const gchar *path)            G_GNUC_MALLOC;
gboolean     gwy_filename_ignore          (const gchar *filename_sys);
gchar*       gwy_sgettext                 (const gchar *msgid);
gchar*       gwy_str_next_line            (gchar **buffer);
guint        gwy_str_fixed_font_width     (const gchar *str);
guint        gwy_gstring_replace          (GString *str,
                                           const gchar *old,
                                           const gchar *replacement,
                                           gint count);
void         gwy_gstring_to_native_eol    (GString *str);
const gchar* gwy_get_decimal_separator    (void);
void         gwy_append_doubles_to_gstring(GString *str,
                                           const gdouble *values,
                                           guint n,
                                           gint precision,
                                           const gchar *field_separator,
                                           gboolean force_decimal_dot);
gdouble*     gwy_parse_doubles            (const gchar *s,
                                           gdouble *values,
                                           GwyParseDoublesFlags flags,
                                           gint *nlines,
                                           gint *ncols,
                                           gchar **endptr,
                                           GError **error);
void         gwy_memcpy_byte_swap         (const guint8 *source,
                                           guint8 *dest,
                                           gsize item_size,
                                           gsize nitems,
                                           gsize byteswap);
void         gwy_convert_raw_data         (gconstpointer data,
                                           gsize nitems,
                                           gssize stride,
                                           GwyRawDataType datatype,
                                           GwyByteOrder byteorder,
                                           gdouble *target,
                                           gdouble scale,
                                           gdouble offset);
guint        gwy_raw_data_size            (GwyRawDataType datatype);
gchar*       gwy_convert_to_utf8          (const guchar *str,
                                           glong len,
                                           const gchar *codeset)         G_GNUC_MALLOC;
gchar*       gwy_utf16_to_utf8            (const gunichar2 *str,
                                           glong len,
                                           GwyByteOrder byteorder)       G_GNUC_MALLOC;
gchar*       gwy_utf8_strescape           (const gchar *source,
                                           const gchar *exceptions)      G_GNUC_MALLOC;
gboolean     gwy_assign_string            (gchar **target,
                                           const gchar *newvalue);
void         gwy_object_set_or_reset      (gpointer object,
                                           GType type,
                                           ...)                          G_GNUC_NULL_TERMINATED;
gboolean     gwy_set_member_object        (gpointer instance,
                                           gpointer member_object,
                                           GType expected_type,
                                           gpointer member_field,
                                           ...)                          G_GNUC_NULL_TERMINATED;
#ifdef G_OS_WIN32
FILE*        gwy_fopen                    (const gchar *filename,
                                           const gchar *mode);
gint         gwy_fprintf                  (FILE *file,
                                           gchar const *format,
                                           ...);
#else
#define gwy_fopen fopen
#define gwy_fprintf fprintf
#endif

G_END_DECLS

#endif /* __GWY_UTILS_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
