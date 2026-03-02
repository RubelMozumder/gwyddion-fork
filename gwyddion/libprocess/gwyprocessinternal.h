/*
 *  $Id: gwyprocessinternal.h 26068 2023-12-20 12:25:29Z yeti-dn $
 *  Copyright (C) 2003-2019 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

/*< private_header >*/

#ifndef __GWYPROCESS_INTERNAL_H__
#define __GWYPROCESS_INTERNAL_H__

#include <libprocess/gwyprocessenums.h>
#include <libprocess/datafield.h>

/* Cache operations */
#define CVAL(datafield, b)  ((datafield)->cache[GWY_DATA_FIELD_CACHE_##b])
#define CBIT(b)             (1 << GWY_DATA_FIELD_CACHE_##b)
#define CTEST(datafield, b) ((datafield)->cached & CBIT(b))

G_BEGIN_DECLS

typedef void (*RowExtendFunc)(const gdouble *in,
                              gdouble *out,
                              guint pos,
                              guint width,
                              guint res,
                              guint extend_left,
                              guint extend_right,
                              gdouble value);

typedef void (*RectExtendFunc)(const gdouble *in,
                               guint inrowstride,
                               gdouble *out,
                               guint outrowstride,
                               guint xpos,
                               guint ypos,
                               guint width,
                               guint height,
                               guint xres,
                               guint yres,
                               guint extend_left,
                               guint extend_right,
                               guint extend_up,
                               guint extend_down,
                               gdouble value);

G_GNUC_INTERNAL
void _gwy_cdline_class_setup_presets(void);

G_GNUC_INTERNAL
void _gwy_grain_value_class_setup_presets(void);

G_GNUC_INTERNAL
void _gwy_calibration_class_setup_presets(void);

G_GNUC_INTERNAL
void _gwy_shape_fit_preset_class_setup_presets(void);

G_GNUC_INTERNAL
RowExtendFunc _gwy_get_row_extend_func(GwyExteriorType exterior);

G_GNUC_INTERNAL
RectExtendFunc _gwy_get_rect_extend_func(GwyExteriorType exterior);

G_GNUC_INTERNAL
gboolean _gwy_data_field_check_area(GwyDataField *data_field,
                                    gint col, gint row,
                                    gint width, gint height,
                                    gboolean allow_empty);

G_GNUC_INTERNAL
gboolean _gwy_data_field_check_mask(GwyDataField *data_field,
                                    GwyDataField **mask,
                                    GwyMaskingType *masking);

struct _GwySurfacePrivate {
    GwySIUnit *si_unit_xy;
    GwySIUnit *si_unit_z;
    GwyXYZ min;
    GwyXYZ max;
    guchar checksum[16];
    gboolean cached_ranges : 1;
    gboolean cached_checksum : 1;
};

typedef struct _GwySurfacePrivate Surface;

typedef struct {
    guint col;
    guint row;
    guint width;
    guint height;
} GwyDataFieldPart;

typedef struct {
    gint i;
    gint j;
} GridPoint;

typedef struct {
    guint size;
    guint len;
    GridPoint *points;
} PixelQueue;

G_GNUC_INTERNAL
guint _gwy_simple_dist_trans(gint *grain,
                             guint width,
                             guint height,
                             gboolean from_border,
                             GwyDistanceTransformType dtype,
                             PixelQueue *inqueue,
                             PixelQueue *outqueue);

typedef struct {
    guint size;
    guint len;
    gint *data;
} IntList;

G_GNUC_UNUSED
static inline IntList*
int_list_new(guint prealloc)
{
    IntList *list = g_slice_new0(IntList);
    prealloc = MAX(prealloc, 16);
    list->size = prealloc;
    list->data = g_new(gint, list->size);
    return list;
}

G_GNUC_UNUSED
static inline void
int_list_add(IntList *list, gint i)
{
    if (G_UNLIKELY(list->len == list->size)) {
        list->size = MAX(2*list->size, 16);
        list->data = g_renew(gint, list->data, list->size);
    }

    list->data[list->len] = i;
    list->len++;
}

G_GNUC_UNUSED
static inline void
int_list_add_unique(IntList **plist, gint i)
{
    IntList *list;
    guint j;

    if (!*plist)
        *plist = int_list_new(0);

    list = *plist;
    for (j = 0; j < list->len; j++) {
        if (list->data[j] == i)
            return;
    }
    int_list_add(list, i);
}

G_GNUC_UNUSED
static inline void
int_list_free(IntList *list)
{
    g_free(list->data);
    g_slice_free(IntList, list);
}

/* Symmetrically means that for even @extsize-@size it holds
 * @extend_begining=@extend_end while for an odd difference it holds
 * @extend_begining+1=@extend_end, i.e. it's extended one pixel more at the
 * end. */
G_GNUC_UNUSED
static void
make_symmetrical_extension(guint size, guint extsize,
                           guint *extend_begining, guint *extend_end)
{
    guint extend = extsize - size;

    *extend_begining = extend/2;
    *extend_end = extend - *extend_begining;
}

G_GNUC_UNUSED
static inline void
extend_kernel_row(const gdouble *kernel, guint klen,
                  gdouble *extended, guint size)
{
    guint llen = klen/2, rlen = klen - llen;
    gwy_assign(extended, kernel + llen, rlen);
    gwy_clear(extended + rlen, size - klen);
    gwy_assign(extended + size - llen, kernel, llen);
}

G_GNUC_UNUSED
static void
extend_kernel_rect(const gdouble *kernel,
                   guint kxlen, guint kylen,
                   gdouble *extended,
                   guint xsize, guint ysize, guint rowstride)
{
    guint ulen = kylen/2, dlen = kylen - ulen;
    guint i;

    for (i = 0; i < dlen; i++) {
        extend_kernel_row(kernel + (i + ulen)*kxlen, kxlen,
                          extended + i*rowstride, xsize);
    }
    gwy_clear(extended + dlen*rowstride, (ysize - kylen)*rowstride);
    for (i = 0; i < ulen; i++) {
        extend_kernel_row(kernel + i*kxlen, kxlen,
                          extended + (ysize - ulen + i)*rowstride, xsize);
    }
}

#define unit_pointer_if_nonempty(unit) \
    (((unit) && !gwy_si_unit_equal_string((unit), NULL)) ? &(unit) : NULL)

G_GNUC_INTERNAL
void _gwy_copy_si_unit(GwySIUnit *source, GwySIUnit **dest);

G_GNUC_INTERNAL
void _gwy_set_object_si_unit(GwySIUnit *unit, GwySIUnit **objmember);

/* Raw, non-checking variants for the derivatives. */
G_GNUC_UNUSED
static inline gdouble
_gwy_data_field_xder(GwyDataField *dfield, gint col, gint row)
{
    gint xres = dfield->xres;
    gdouble *p = dfield->data + row*xres + col;
    gdouble dx = dfield->xreal/xres;

    if (col == 0)
        return (*(p+1) - *p)/dx;
    if (col == xres-1)
        return (*p - *(p-1))/dx;
    return (*(p+1) - *(p-1))/dx/2;
}

G_GNUC_UNUSED
static inline gdouble
_gwy_data_field_yder(GwyDataField *dfield, gint col, gint row)
{
    gint xres = dfield->xres, yres = dfield->yres;
    gdouble *p = dfield->data + row*xres + col;
    gdouble dy = dfield->yreal/yres;

    if (row == 0)
        return (*p - *(p+xres))/dy;
    if (row == yres-1)
        return (*(p-xres) - *p)/dy;
    return (*(p-xres) - *(p+xres))/dy/2;
}

G_END_DECLS

#endif /* __GWYPROCESS_INTERNAL_H__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
