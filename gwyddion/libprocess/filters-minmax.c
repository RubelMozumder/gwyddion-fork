/*
 *  $Id: filters-minmax.c 26068 2023-12-20 12:25:29Z yeti-dn $
 *  Copyright (C) 2003-2020 David Necas (Yeti), Petr Klapetek.
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

#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/arithmetic.h>
#include <libprocess/elliptic.h>
#include <libprocess/stats.h>
#include <libprocess/linestats.h>
#include <libprocess/grains.h>
#include <libprocess/filters.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

/* Data for one row.  To be used in conjuction with MinMaxPrecomputedReq. */
typedef struct {
    gdouble *storage;
    gdouble **each;
    gdouble **even;
} MinMaxPrecomputedRow;

typedef struct {
    guint sublen1;   /* Even length for the even-odd scheme. */
    guint sublen2;
    gboolean needed;
    gboolean even_even : 1;
    gboolean even_odd : 1;
} MinMaxPrecomputedLen;

/* Resolved set of required block lengths and the rules how to compute them. */
typedef struct {
    /* NB: The array sizes are maxlen_even+1 and maxlen_each+1 because maxlen is really the maximum length, inclusive.
     */
    MinMaxPrecomputedLen *each;
    MinMaxPrecomputedLen *even;
    guint maxlen_each;
    guint maxlen_even;
    guint nbuffers;   /* The actual number of row buffers (for storage size) */
} MinMaxPrecomputedReq;

typedef struct {
    guint row;
    guint col;
    guint len;
} MaskSegment;

typedef struct {
    MaskSegment *segments;
    guint nsegments;
} MaskRLE;

typedef struct {
    MaskRLE *mrle;
    MinMaxPrecomputedReq *req;
    MinMaxPrecomputedRow **prows;
    gdouble *extrowbuf;
    guint rowbuflen;
    guint kxres;
    guint kyres;
} MinMaxPrecomputed;

typedef void (*MinMaxPrecomputedRowFill)(const MinMaxPrecomputedReq *req,
                                         MinMaxPrecomputedRow *prow,
                                         const gdouble *x,
                                         guint rowlen);

static void find_required_lengths_recursive(MinMaxPrecomputedReq *req,
                                            guint blocklen,
                                            gboolean is_even);

/* K-th rank filter tree.  Number of levels with special storage; the rest is in ucount[][]. */
enum { MEDIAN_RADIX_TREE_BASE_LEVELS = 8 };

/* This needs somewhat less than 2n bits, i.e. n/4 bytes.  We actually never use the last element in each quadruple
 * – but having it enables making add/remove branch-free, using just shifts and increments/decrements. */
typedef struct {
    guint n;
    guint levels;
    guint8 *bits;           /* Bits: 1 = number is present in the set. */
    guint8 *bcount8;        /* Nibbles: total bits set in corresponding bits[] */
    guint8 *bcount32;       /* Bytes: sum of quadruple of bcount8[]. */
    guint8 *bcount128;      /* Bytes: sum of quadruple of bcount32[]. */
    guint16 *scount512;     /* Shorts: sum of quadruple of bcount128[]. */
    guint16 *scount2048;    /* Shorts: sum of quadruple of scount512[]. */
    guint16 *scount8192;    /* Shorts: sum of quadruple of scount2048[]. */
    guint16 *scount32768;   /* Shorts: sum of quadruple of scount8192[]. */
    guint32 **ucount;       /* Ints: sums of quadruples of lower levels. */
    guint len;              /* Plain number of values. */
} MedianRadixTree;

typedef struct {
    guint nup;
    guint ndown;
    guint nleft;
    guint nright;
    guint *up;
    guint *down;
    guint *left;
    guint *right;
} FilterKernelBoundaries;

typedef struct {
    guint f1;
    guint f2;
    guint k1;    /* counted from the beginning */
    guint k2;    /* counted from the end! */
    gdouble runningsum;
} TrimmedMeanState;

/**
 * gwy_data_field_area_filter_minimum:
 * @data_field: A data field to apply minimum filter to.
 * @size: Neighbourhood size for minimum search.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Filters a rectangular part of a data field with minimum filter.
 *
 * This operation is often called erosion filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_min_max() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_area_filter_minimum(GwyDataField *data_field,
                                   gint size,
                                   gint col,
                                   gint row,
                                   gint width,
                                   gint height)
{
    GwyDataField *buffer, *buffer2;
    gint d, i, j;
    gint ep, em;  /* positive and negative excess */
    gdouble *buf, *buf2;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return;
    g_return_if_fail(size > 0);
    if (size == 1)
        return;

    /* FIXME: does this silly case need an alternative implementation? */
    if (size/2 >= MIN(width, height)) {
        g_warning("Too large kernel size for too small area.");
        return;
    }

    buffer = gwy_data_field_new(width, height, 1.0, 1.0, FALSE);
    buffer2 = gwy_data_field_new(width, height, 1.0, 1.0, FALSE);
    buf = buffer->data;
    buf2 = buffer2->data;

    d = 1;
    gwy_data_field_area_copy(data_field, buffer, col, row, width, height, 0, 0);
    while (3*d < size) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(buf,buf2,width,height,d)
#endif
        for (i = 0; i < height; i++) {
            gint ii = i*width;
            gint im = MAX(i - d, 0)*width;
            gint ip = MIN(i + d, height-1)*width;

            for (j = 0; j < width; j++) {
                gint jm = MAX(j - d, 0);
                gint jp = MIN(j + d, width-1);
                gdouble v = MIN(buf[im + jm], buf[im + jp]);

                if (v > buf[im + j])
                    v = buf[im + j];
                if (v > buf[ii + jm])
                    v = buf[ii + jm];
                if (v > buf[ii + j])
                    v = buf[ii + j];
                if (v > buf[ip + j])
                    v = buf[ip + j];
                if (v > buf[ii + jp])
                    v = buf[ii + jp];
                if (v > buf[ip + jm])
                    v = buf[ip + jm];
                if (v > buf[ip + jp])
                    v = buf[ip + jp];

                buf2[ii + j] = v;
            }
        }
        /* XXX: This breaks the relation between buffer and buf */
        GWY_SWAP(gdouble*, buf, buf2);
        d *= 3;
    }

    /* Now we have to overlay the neighbourhoods carefully to get exactly @size-sized squares.  There are two cases:
     * 1. @size <= 2*d, it's enough to take four corner representants
     * 2. @size > 2*d, it's necessary to take all nine representants
     */
    ep = size/2;
    em = (size - 1)/2;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(buf,buf2,width,height,d,ep,em,size)
#endif
    for (i = 0; i < height; i++) {
        gint ii = i*width;
        gint im = (MAX(i - em, 0) + d/2)*width;
        gint ip = (MIN(i + ep, height-1) - d/2)*width;

        for (j = 0; j < width; j++) {
            gint jm = MAX(j - em, 0) + d/2;
            gint jp = MIN(j + ep, width-1) - d/2;
            gdouble v = MIN(buf[im + jm], buf[im + jp]);

            if (2*d < size) {
                if (v > buf[im + j])
                    v = buf[im + j];
                if (v > buf[ii + jm])
                    v = buf[ii + jm];
                if (v > buf[ii + j])
                    v = buf[ii + j];
                if (v > buf[ii + jp])
                    v = buf[ii + jp];
                if (v > buf[ip + j])
                    v = buf[ip + j];
            }
            if (v > buf[ip + jm])
                v = buf[ip + jm];
            if (v > buf[ip + jp])
                v = buf[ip + jp];

            buf2[ii + j] = v;
        }
    }
    buffer->data = buf;
    buffer2->data = buf2;

    gwy_data_field_area_copy(buffer2, data_field, 0, 0, width, height, col, row);

    g_object_unref(buffer2);
    g_object_unref(buffer);
}

/**
 * gwy_data_field_filter_minimum:
 * @data_field: A data field to apply minimum filter to.
 * @size: Neighbourhood size for minimum search.
 *
 * Filters a data field with minimum filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_min_max() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_filter_minimum(GwyDataField *data_field,
                              gint size)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_filter_minimum(data_field, size, 0, 0, data_field->xres, data_field->yres);
}

/**
 * gwy_data_field_area_filter_maximum:
 * @data_field: A data field to apply maximum filter to.
 * @size: Neighbourhood size for maximum search.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Filters a rectangular part of a data field with maximum filter.
 *
 * This operation is often called dilation filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_min_max() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_area_filter_maximum(GwyDataField *data_field,
                                   gint size,
                                   gint col,
                                   gint row,
                                   gint width,
                                   gint height)
{
    GwyDataField *buffer, *buffer2;
    gint d, i, j;
    gint ep, em;  /* positive and negative excess */
    gdouble *buf, *buf2;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return;
    g_return_if_fail(size > 0);
    if (size == 1)
        return;

    /* FIXME: does this silly case need an alternative implementation? */
    if (size/2 >= MIN(width, height)) {
        g_warning("Too large kernel size for too small area.");
        return;
    }

    buffer = gwy_data_field_new(width, height, 1.0, 1.0, FALSE);
    buffer2 = gwy_data_field_new(width, height, 1.0, 1.0, FALSE);
    buf = buffer->data;
    buf2 = buffer2->data;

    d = 1;
    gwy_data_field_area_copy(data_field, buffer, col, row, width, height, 0, 0);
    while (3*d < size) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(buf,buf2,width,height,d)
#endif
        for (i = 0; i < height; i++) {
            gint ii = i*width;
            gint im = MAX(i - d, 0)*width;
            gint ip = MIN(i + d, height-1)*width;

            for (j = 0; j < width; j++) {
                gint jm = MAX(j - d, 0);
                gint jp = MIN(j + d, width-1);
                gdouble v = MAX(buf[im + jm], buf[im + jp]);

                if (v < buf[im + j])
                    v = buf[im + j];
                if (v < buf[ii + jm])
                    v = buf[ii + jm];
                if (v < buf[ii + j])
                    v = buf[ii + j];
                if (v < buf[ip + j])
                    v = buf[ip + j];
                if (v < buf[ii + jp])
                    v = buf[ii + jp];
                if (v < buf[ip + jm])
                    v = buf[ip + jm];
                if (v < buf[ip + jp])
                    v = buf[ip + jp];

                buf2[ii + j] = v;
            }
        }
        /* XXX: This breaks the relation between buffer and buf */
        GWY_SWAP(gdouble*, buf, buf2);
        d *= 3;
    }

    /* Now we have to overlay the neighbourhoods carefully to get exactly @size-sized squares.  There are two cases:
     * 1. @size <= 2*d, it's enough to take four corner representants
     * 2. @size > 2*d, it's necessary to take all nine representants
     */
    ep = size/2;
    em = (size - 1)/2;

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(buf,buf2,width,height,d,ep,em,size)
#endif
    for (i = 0; i < height; i++) {
        gint ii = i*width;
        gint im = (MAX(i - em, 0) + d/2)*width;
        gint ip = (MIN(i + ep, height-1) - d/2)*width;

        for (j = 0; j < width; j++) {
            gint jm = MAX(j - em, 0) + d/2;
            gint jp = MIN(j + ep, width-1) - d/2;
            gdouble v = MAX(buf[im + jm], buf[im + jp]);

            if (2*d < size) {
                if (v < buf[im + j])
                    v = buf[im + j];
                if (v < buf[ii + jm])
                    v = buf[ii + jm];
                if (v < buf[ii + j])
                    v = buf[ii + j];
                if (v < buf[ii + jp])
                    v = buf[ii + jp];
                if (v < buf[ip + j])
                    v = buf[ip + j];
            }
            if (v < buf[ip + jm])
                v = buf[ip + jm];
            if (v < buf[ip + jp])
                v = buf[ip + jp];

            buf2[ii + j] = v;
        }
    }
    buffer->data = buf;
    buffer2->data = buf2;

    gwy_data_field_area_copy(buffer2, data_field, 0, 0, width, height, col, row);

    g_object_unref(buffer2);
    g_object_unref(buffer);
}

/**
 * gwy_data_field_filter_maximum:
 * @data_field: A data field to apply maximum filter to.
 * @size: Neighbourhood size for maximum search.
 *
 * Filters a data field with maximum filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_min_max() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_filter_maximum(GwyDataField *data_field,
                              gint size)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_filter_maximum(data_field, size, 0, 0, data_field->xres, data_field->yres);
}

static inline gboolean
maybe_set_req(MinMaxPrecomputedLen *precomp)
{
    if (precomp->needed)
        return TRUE;

    precomp->needed = TRUE;
    return FALSE;
}

static inline void
fill_req_subs(MinMaxPrecomputedLen *precomp,
              guint sublen1, guint sublen2,
              gboolean even_odd, gboolean even_even)
{
    precomp->sublen1 = sublen1;
    precomp->sublen2 = sublen2;
    precomp->even_even = even_even;
    precomp->even_odd = even_odd;
    g_assert(!even_odd || !even_even);
    g_assert(!even_odd || sublen1 % 2 == 0);
    g_assert(!even_even || (sublen1 % 2 == 0 && sublen2 % 2 == 0));
}

static void
find_required_lengths_recursive(MinMaxPrecomputedReq *req,
                                guint blocklen, gboolean is_even)
{
    MinMaxPrecomputedLen *precomp;
    guint i, j, any = 0;

    g_assert(blocklen);

    if (is_even) {
        g_assert(blocklen % 2 == 0);

        precomp = req->even + blocklen;
        if (maybe_set_req(precomp))
            return;

        if (blocklen == 2) {
            /* Even(2) = Each(1) + Each(1) */
            fill_req_subs(precomp, 1, 1, FALSE, FALSE);
            find_required_lengths_recursive(req, 1, FALSE);
        }
        else if (blocklen % 4 == 0) {
            /* Even(4m) = Even(2m) + Even(2m) */
            fill_req_subs(precomp, blocklen/2, blocklen/2, FALSE, TRUE);
            find_required_lengths_recursive(req, blocklen/2, TRUE);
        }
        else if (blocklen % 4 == 2) {
            /* Even(4m+2) = Even(2m+2) + Even(2m) */
            fill_req_subs(precomp, blocklen/2 - 1, blocklen/2 + 1, FALSE, TRUE);
            find_required_lengths_recursive(req, blocklen/2 - 1, TRUE);
            find_required_lengths_recursive(req, blocklen/2 + 1, TRUE);
        }
        else {
            g_assert_not_reached();
        }
    }
    else {
        precomp = req->each + blocklen;
        if (maybe_set_req(precomp))
            return;

        if (blocklen == 1) {
            /* Even(1), this is always required.  There is no construction
             * rule, of course.*/
            req->each[1].needed = TRUE;
        }
        else if (blocklen % 2 == 0) {
            /* Try to find a split into two existing lengths. */
            for (i = 1, j = blocklen-1; i < (blocklen + 1)/2; i++, j--) {
                if (req->each[i].needed && req->each[j].needed) {
                    fill_req_subs(precomp, i, j, FALSE, FALSE);
                    return;
                }
            }

            /* Each(2m) = Each(m) + Each(m) */
            fill_req_subs(precomp, blocklen/2, blocklen/2, FALSE, FALSE);
            find_required_lengths_recursive(req, blocklen/2, FALSE);
        }
        else if (blocklen % 2 == 1) {
            /* Try to find a split into two existing lengths. */
            for (i = 1, j = blocklen-1; i < (blocklen + 1)/2; i++, j--) {
                if (req->each[i].needed && req->each[j].needed) {
                    fill_req_subs(precomp, i, j, FALSE, FALSE);
                    return;
                }
                if (req->even[i].needed && req->each[j].needed) {
                    fill_req_subs(precomp, i, j, TRUE, FALSE);
                    return;
                }
                if (req->each[i].needed && req->even[j].needed) {
                    fill_req_subs(precomp, j, i, TRUE, FALSE);
                    return;
                }
                if (req->each[i].needed)
                    any = i;
            }
            /* Or split to one existing and one new. */
            if (any) {
                fill_req_subs(precomp, any, blocklen - any, FALSE, FALSE);
                find_required_lengths_recursive(req, blocklen - any, FALSE);
                return;
            }

            if (blocklen % 4 == 1) {
                /* Each(4m+1) = Even(2m) + Each(2m+1), Each(2m+1) + Even(2m) */
                fill_req_subs(precomp, blocklen/2, blocklen/2 + 1, TRUE, FALSE);
                find_required_lengths_recursive(req, blocklen/2, TRUE);
                find_required_lengths_recursive(req, blocklen/2 + 1, FALSE);
            }
            else if (blocklen % 4 == 3) {
                /* Each(4m+3) = Even(2m+2) + Each(2m+1), Each(2m+1) + Even(2m+2) */
                fill_req_subs(precomp, blocklen/2 + 1, blocklen/2, TRUE, FALSE);
                find_required_lengths_recursive(req, blocklen/2 + 1, TRUE);
                find_required_lengths_recursive(req, blocklen/2, FALSE);
            }
            else {
                g_assert_not_reached();
            }
        }
        else {
            g_assert_not_reached();
        }
    }
}

static MinMaxPrecomputedReq*
find_required_lengths_for_set(const guint *blocklens, guint nlens)
{
    MinMaxPrecomputedReq *req = g_new(MinMaxPrecomputedReq, 1);
    guint *blens = g_new(guint, 2*nlens);
    guint i, n, maxlen;

    /* Find unique lengths and sort them in ascending order to blens[],
     * starting ar position nlens. */
    gwy_assign(blens, blocklens, nlens);
    gwy_guint_sort(nlens, blens);

    blens[nlens] = blens[0];
    for (i = n = 1; i < nlens; i++) {
        if (blens[i] > blens[i-1])
            blens[nlens + n++] = blens[i];
    }

    maxlen = blens[nlens + n-1];
    req->maxlen_each = maxlen;
    req->maxlen_even = maxlen;
    req->each = g_new0(MinMaxPrecomputedLen, maxlen+1);
    req->even = g_new0(MinMaxPrecomputedLen, maxlen+1);
    for (i = 0; i < n; i++)
        find_required_lengths_recursive(req, blens[nlens + i], FALSE);

    g_free(blens);

    for (i = maxlen; i; i--) {
        if (req->even[i].needed)
            break;
    }
    req->maxlen_even = i;

    req->nbuffers = 0;
    for (i = 1; i <= req->maxlen_each; i++) {
        if (req->each[i].needed)
            req->nbuffers++;
    }
    for (i = 2; i <= req->maxlen_even; i++) {
        if (req->even[i].needed)
            req->nbuffers++;
    }

    return req;
}

static void
min_max_precomputed_req_free(MinMaxPrecomputedReq *req)
{
    g_free(req->each);
    g_free(req->even);
    g_free(req);
}

/* Allocate data buffers for all lengths.  Do not allocate the Each(1) buffer, we use a direct pointer to the data row
 * for that. */
static MinMaxPrecomputedRow*
min_max_precomputed_row_alloc(const MinMaxPrecomputedReq *req,
                              guint rowlen)
{
    MinMaxPrecomputedRow *prow = g_new0(MinMaxPrecomputedRow, 1);
    gdouble *p;
    guint i;

    prow->storage = p = g_new(gdouble, rowlen*req->nbuffers);
    prow->each = g_new0(gdouble*, req->maxlen_each + 1);
    if (req->maxlen_even)
        prow->even = g_new0(gdouble*, req->maxlen_even + 1);

    for (i = 1; i <= req->maxlen_each; i++) {
        if (req->each[i].needed) {
            prow->each[i] = p;
            p += rowlen;
        }
    }
    for (i = 2; i <= req->maxlen_even; i++) {
        if (req->even[i].needed) {
            prow->even[i] = p;
            p += rowlen;
        }
    }

    return prow;
}

static void
compose_max_row_data_each(gdouble *target,
                          const gdouble *sub1,
                          guint sublen1,
                          const gdouble *sub2,
                          guint sublen2,
                          guint rowlen)
{
    guint i, n;

    g_return_if_fail(sublen1 + sublen2 <= rowlen);
    g_return_if_fail(target);
    g_return_if_fail(sub1);
    g_return_if_fail(sub2);

    sub2 += sublen1;
    n = rowlen - (sublen1 + sublen2);
    i = 0;
    while (i <= n) {
        target[i] = (*sub1 < *sub2) ? *sub2 : *sub1;
        i++;
        sub1++;
        sub2++;
    }
}

static void
compose_max_row_data_even_odd(gdouble *target,
                              const gdouble *even,
                              guint evenlen,
                              const gdouble *odd,
                              guint oddlen,
                              guint rowlen)
{
    const gdouble *even2, *odd2;
    guint i, n;

    g_return_if_fail(evenlen + oddlen <= rowlen);
    g_return_if_fail(evenlen % 2 == 0);
    g_return_if_fail(target);
    g_return_if_fail(even);
    g_return_if_fail(odd);

    odd2 = odd + 1;
    even2 = even + oddlen + 1;
    odd += evenlen;
    n = rowlen - (evenlen + oddlen);
    i = 0;
    while (i+1 <= n) {
        /* Now even points to an even position. */
        target[i] = (*even < *odd) ? *odd : *even;
        i++;
        even += 2;
        odd += 2;

        /* Now even2 points to an even position. */
        target[i] = (*even2 < *odd2) ? *odd2 : *even2;
        i++;
        even2 += 2;
        odd2 += 2;
    }

    if (i <= n) {
        /* Now even points to an even position. */
        target[i] = (*even < *odd) ? *odd : *even;
        i++;
    }
    if (i <= n) {
        /* Now even2 points to an even position. */
        target[i] = (*even2 < *odd2) ? *odd2 : *even2;
    }
}

static void
compose_max_row_data_even(gdouble *target,
                          const gdouble *sub1,
                          guint sublen1,
                          const gdouble *sub2,
                          guint sublen2,
                          guint rowlen)
{
    guint i, n;

    g_return_if_fail(sublen1 + sublen2 <= rowlen);
    g_return_if_fail(sublen1 % 2 == 0);
    g_return_if_fail(sublen2 % 2 == 0);
    g_return_if_fail(target);
    g_return_if_fail(sub1);
    g_return_if_fail(sub2);

    sub2 += sublen1;
    n = rowlen - (sublen1 + sublen2);
    i = 0;
    while (i <= n) {
        target[i] = (*sub1 < *sub2) ? *sub2 : *sub1;
        i += 2;
        sub1 += 2;
        sub2 += 2;
    }
}

static void
compose_max_row_data_two(gdouble *target,
                         const gdouble *one,
                         guint rowlen)
{
    guint i, n;

    g_return_if_fail(2 <= rowlen);
    g_return_if_fail(target);
    g_return_if_fail(one);

    n = rowlen - 2;
    i = 0;
    while (i <= n) {
        target[i] = (*one < *(one + 1)) ? *(one + 1) : *one;
        i += 2;
        one += 2;
    }
}

/* Precomputes maxima for row.  Minimum is always computed from given index blocklen values *forwards*, i.e. the block
 * is not symmetrical it starts at the given index. */
static void
max_precomputed_row_fill(const MinMaxPrecomputedReq *req,
                         MinMaxPrecomputedRow *prow,
                         const gdouble *x,
                         guint rowlen)
{
    guint blen;

    /* The row itself, AKA Each(1). */
    gwy_assign(prow->each[1], x, rowlen);

    for (blen = 2; blen <= req->maxlen_each; blen++) {
        const MinMaxPrecomputedLen *precomp;

        precomp = req->each + blen;
        if (precomp->needed) {
            g_assert(!precomp->even_even);
            if (precomp->even_odd) {
                compose_max_row_data_even_odd(prow->each[blen],
                                              prow->even[precomp->sublen1], precomp->sublen1,
                                              prow->each[precomp->sublen2], precomp->sublen2,
                                              rowlen);
            }
            else {
                compose_max_row_data_each(prow->each[blen],
                                          prow->each[precomp->sublen1], precomp->sublen1,
                                          prow->each[precomp->sublen2], precomp->sublen2,
                                          rowlen);
            }
        }

        if (blen > req->maxlen_even)
            continue;

        precomp = req->even + blen;
        if (precomp->needed) {
            g_assert(!precomp->even_odd);
            if (precomp->even_even) {
                compose_max_row_data_even(prow->even[blen],
                                          prow->even[precomp->sublen1], precomp->sublen1,
                                          prow->even[precomp->sublen2], precomp->sublen2,
                                          rowlen);
            }
            else {
                g_assert(blen == 2);
                g_assert(precomp->sublen1 == 1);
                compose_max_row_data_two(prow->even[blen], prow->each[precomp->sublen1], rowlen);
            }
        }
    }
}

static void
compose_min_row_data_each(gdouble *target,
                          const gdouble *sub1,
                          guint sublen1,
                          const gdouble *sub2,
                          guint sublen2,
                          guint rowlen)
{
    guint i, n;

    g_return_if_fail(sublen1 + sublen2 <= rowlen);
    g_return_if_fail(target);
    g_return_if_fail(sub1);
    g_return_if_fail(sub2);

    sub2 += sublen1;
    n = rowlen - (sublen1 + sublen2);
    i = 0;
    while (i <= n) {
        target[i] = (*sub1 > *sub2) ? *sub2 : *sub1;
        i++;
        sub1++;
        sub2++;
    }
}

static void
compose_min_row_data_even_odd(gdouble *target,
                              const gdouble *even,
                              guint evenlen,
                              const gdouble *odd,
                              guint oddlen,
                              guint rowlen)
{
    const gdouble *even2, *odd2;
    guint i, n;

    g_return_if_fail(evenlen + oddlen <= rowlen);
    g_return_if_fail(evenlen % 2 == 0);
    g_return_if_fail(target);
    g_return_if_fail(even);
    g_return_if_fail(odd);

    odd2 = odd + 1;
    even2 = even + oddlen + 1;
    odd += evenlen;
    n = rowlen - (evenlen + oddlen);
    i = 0;
    while (i+1 <= n) {
        /* Now even points to an even position. */
        target[i] = (*even > *odd) ? *odd : *even;
        i++;
        even += 2;
        odd += 2;

        /* Now even2 points to an even position. */
        target[i] = (*even2 > *odd2) ? *odd2 : *even2;
        i++;
        even2 += 2;
        odd2 += 2;
    }

    if (i <= n) {
        /* Now even points to an even position. */
        target[i] = (*even > *odd) ? *odd : *even;
        i++;
    }
    if (i <= n) {
        /* Now even2 points to an even position. */
        target[i] = (*even2 > *odd2) ? *odd2 : *even2;
    }
}

static void
compose_min_row_data_even(gdouble *target,
                          const gdouble *sub1,
                          guint sublen1,
                          const gdouble *sub2,
                          guint sublen2,
                          guint rowlen)
{
    guint i, n;

    g_return_if_fail(sublen1 + sublen2 <= rowlen);
    g_return_if_fail(sublen1 % 2 == 0);
    g_return_if_fail(sublen2 % 2 == 0);
    g_return_if_fail(target);
    g_return_if_fail(sub1);
    g_return_if_fail(sub2);

    sub2 += sublen1;
    n = rowlen - (sublen1 + sublen2);
    i = 0;
    while (i <= n) {
        target[i] = (*sub1 > *sub2) ? *sub2 : *sub1;
        i += 2;
        sub1 += 2;
        sub2 += 2;
    }
}

static void
compose_min_row_data_two(gdouble *target,
                         const gdouble *one,
                         guint rowlen)
{
    guint i, n;

    g_return_if_fail(2 <= rowlen);
    g_return_if_fail(target);
    g_return_if_fail(one);

    n = rowlen - 2;
    i = 0;
    while (i <= n) {
        target[i] = (*one > *(one + 1)) ? *(one + 1) : *one;
        i += 2;
        one += 2;
    }
}

/* Precomputes minima for row.  Minimum is always computed from given index blocklen values *forwards*, i.e. the block
 * is not symmetrical it starts at the given index. */
static void
min_precomputed_row_fill(const MinMaxPrecomputedReq *req,
                         MinMaxPrecomputedRow *prow,
                         const gdouble *x,
                         guint rowlen)
{
    guint blen;

    /* The row itself, AKA Each(1). */
    gwy_assign(prow->each[1], x, rowlen);

    for (blen = 2; blen <= req->maxlen_each; blen++) {
        const MinMaxPrecomputedLen *precomp;

        precomp = req->each + blen;
        if (precomp->needed) {
            g_assert(!precomp->even_even);
            if (precomp->even_odd) {
                compose_min_row_data_even_odd(prow->each[blen],
                                              prow->even[precomp->sublen1], precomp->sublen1,
                                              prow->each[precomp->sublen2], precomp->sublen2,
                                              rowlen);
            }
            else {
                compose_min_row_data_each(prow->each[blen],
                                          prow->each[precomp->sublen1], precomp->sublen1,
                                          prow->each[precomp->sublen2], precomp->sublen2,
                                          rowlen);
            }
        }

        if (blen > req->maxlen_even)
            continue;

        precomp = req->even + blen;
        if (precomp->needed) {
            g_assert(!precomp->even_odd);
            if (precomp->even_even) {
                compose_min_row_data_even(prow->even[blen],
                                          prow->even[precomp->sublen1], precomp->sublen1,
                                          prow->even[precomp->sublen2], precomp->sublen2,
                                          rowlen);
            }
            else {
                g_assert(blen == 2);
                compose_min_row_data_two(prow->even[blen], prow->each[precomp->sublen1], rowlen);
            }
        }
    }
}

static void
min_max_precomputed_row_copy(MinMaxPrecomputedRow *target,
                             const MinMaxPrecomputedRow *source,
                             const MinMaxPrecomputedReq *req,
                             guint rowlen)
{
    gwy_assign(target->storage, source->storage, rowlen*req->nbuffers);
}

static void
min_max_precomputed_row_free(MinMaxPrecomputedRow *prow)
{
    g_free(prow->each);
    g_free(prow->even);
    g_free(prow->storage);
    g_free(prow);
}

static MaskRLE*
run_length_encode_mask(GwyDataField *mask)
{
    GArray *segments = g_array_new(FALSE, FALSE, sizeof(MaskSegment));
    MaskRLE *mrle = g_new0(MaskRLE, 1);
    const gdouble *data = mask->data;
    guint xres = mask->xres, yres = mask->yres;
    guint i, j, l;

    for (i = 0; i < yres; i++) {
        j = l = 0;
        while (j + l < xres) {
            if (*(data++))
                l++;
            else {
                if (l) {
                    MaskSegment seg = { i, j, l };
                    g_array_append_val(segments, seg);
                    j += l;
                    l = 0;
                }
                j++;
            }
        }
        if (l) {
            MaskSegment seg = { i, j, l };
            g_array_append_val(segments, seg);
        }
    }

    mrle->nsegments = segments->len;
    mrle->segments = (MaskSegment*)g_array_free(segments, FALSE);
    return mrle;
}

static void
mask_rle_free(MaskRLE *mrle)
{
    g_free(mrle->segments);
    g_free(mrle);
}

/* Analyse the set of segments and make a composition plan. */
static MinMaxPrecomputedReq*
find_required_lengths_for_rle(const MaskRLE *mrle)
{
    MinMaxPrecomputedReq *req;
    guint *lengths = g_new(guint, mrle->nsegments);
    guint i;

    for (i = 0; i < mrle->nsegments; i++)
        lengths[i] = mrle->segments[i].len;
    req = find_required_lengths_for_set(lengths, mrle->nsegments);
    g_free(lengths);

    return req;
}

static inline void
fill_block(gdouble *data, guint len, gdouble value)
{
    while (len--)
        *(data++) = value;
}

static inline void
row_extend_base(const gdouble *in, gdouble *out,
                guint *pos, guint *width, guint res,
                guint *extend_left, guint *extend_right)
{
    guint e2r, e2l;

    /* Expand the ROI to the right as far as possible */
    e2r = MIN(*extend_right, res - (*pos + *width));
    *width += e2r;
    *extend_right -= e2r;

    /* Expand the ROI to the left as far as possible */
    e2l = MIN(*extend_left, *pos);
    *width += e2l;
    *extend_left -= e2l;
    *pos -= e2l;

    /* Direct copy of the ROI */
    gwy_assign(out + *extend_left, in + *pos, *width);
}

static void
row_extend_border(const gdouble *in, gdouble *out,
                  guint pos, guint width, guint res,
                  guint extend_left, guint extend_right,
                  G_GNUC_UNUSED gdouble value)
{
    row_extend_base(in, out, &pos, &width, res, &extend_left, &extend_right);
    /* Forward-extend */
    fill_block(out + extend_left + width, extend_right, in[res-1]);
    /* Backward-extend */
    fill_block(out, extend_left, in[0]);
}

static void
mask_rle_execute_min_max(const MaskRLE *mrle, MinMaxPrecomputedRow **prows,
                         gdouble *outbuf, guint width, gboolean maximum)
{
    const MaskSegment *seg = mrle->segments;
    guint n = mrle->nsegments, i, j;
    const gdouble *segdata = prows[seg->row]->each[seg->len] + seg->col;

    gwy_assign(outbuf, segdata, width);
    seg++;
    for (i = n-1; i; i--, seg++) {
        segdata = prows[seg->row]->each[seg->len] + seg->col;
        if (maximum) {
            for (j = 0; j < width; j++) {
                if (outbuf[j] < segdata[j])
                    outbuf[j] = segdata[j];
            }
        }
        else {
            for (j = 0; j < width; j++) {
                if (outbuf[j] > segdata[j])
                    outbuf[j] = segdata[j];
            }
        }
    }
}

static gboolean
gwy_data_field_area_rle_analyse(GwyDataField *kernel,
                                gint width,
                                MinMaxPrecomputed *mmp)
{
    guint kxres, kyres, i;

    kxres = kernel->xres;
    kyres = kernel->yres;
    mmp->rowbuflen = width + kxres-1;
    mmp->kxres = kxres;
    mmp->kyres = kyres;

    /* Run-length encode the mask, i.e. transform it to a set of segments and their positions. */
    mmp->mrle = run_length_encode_mask(kernel);
    if (!mmp->mrle->nsegments) {
        mask_rle_free(mmp->mrle);
        return FALSE;
    }

    mmp->req = find_required_lengths_for_rle(mmp->mrle);

    /* Create the row buffers for running extrema of various lengths. */
    mmp->prows = g_new(MinMaxPrecomputedRow*, kyres);
    for (i = 0; i < kyres; i++)
        mmp->prows[i] = min_max_precomputed_row_alloc(mmp->req, mmp->rowbuflen);

    mmp->extrowbuf = g_new(gdouble, mmp->rowbuflen);

    return TRUE;
}

static int
compare_segment(const void *pa, const void *pb)
{
    const MaskSegment *a = (const MaskSegment*)pa, *b = (const MaskSegment*)pb;

    if (a->row < b->row)
        return -1;
    if (a->row > b->row)
        return 1;
    if (a->col < b->col)
        return -1;
    if (a->col > b->col)
        return 1;
    return 0;
}

/* Rotate the RLE data by pi.  The set of block lengths does not change. Therefore, the decompositions do not change
 * either.  The only thing that changes is the positions of the RLE segments. */
static void
gwy_data_field_area_rle_flip(MaskRLE *mrle, guint kxres, guint kyres)
{
    guint i;

    for (i = 0; i < mrle->nsegments; i++) {
        MaskSegment *seg = mrle->segments + i;
        seg->col = kxres - seg->col - seg->len;
        seg->row = kyres-1 - seg->row;
    }

    qsort(mrle->segments, mrle->nsegments, sizeof(MaskSegment), compare_segment);
}

static void
gwy_data_field_area_rle_free(MinMaxPrecomputed *mmp)
{
    guint i;

    g_free(mmp->extrowbuf);
    for (i = 0; i < mmp->kyres; i++)
        min_max_precomputed_row_free(mmp->prows[i]);
    g_free(mmp->prows);
    min_max_precomputed_req_free(mmp->req);
    mask_rle_free(mmp->mrle);
}

static void
gwy_data_field_area_min_max_execute(GwyDataField *dfield,
                                    gdouble *outbuf,
                                    MinMaxPrecomputed *mmp,
                                    gboolean maximum,
                                    gint col, gint row,
                                    gint width, gint height)
{
    MaskRLE *mrle = mmp->mrle;
    MinMaxPrecomputedReq *req = mmp->req;
    MinMaxPrecomputedRow **prows = mmp->prows, *prow;
    MinMaxPrecomputedRowFill precomp_row_fill;
    guint xres, yres, i, ii;
    guint extend_up, extend_down, extend_left, extend_right;
    gdouble *extrowbuf = mmp->extrowbuf;
    const gdouble *d = dfield->data;
    guint rowbuflen = mmp->rowbuflen;

    xres = dfield->xres;
    yres = dfield->yres;
    precomp_row_fill = (maximum ? max_precomputed_row_fill : min_precomputed_row_fill);

    /* Initialise the buffers for the zeroth row of the area.  For the maximum operation we even-sized kernels to the
     * other direction to obtain morphological operation according to definitions. */
    if (maximum) {
        extend_up = mmp->kyres/2;
        extend_down = (mmp->kyres - 1)/2;
        extend_left = mmp->kxres/2;
        extend_right = (mmp->kxres - 1)/2;
    }
    else {
        extend_up = (mmp->kyres - 1)/2;
        extend_down = mmp->kyres/2;
        extend_left = (mmp->kxres - 1)/2;
        extend_right = mmp->kxres/2;
    }

    for (i = 0; i <= extend_down; i++) {
        ii = i + extend_up;
        if (row + i < yres) {
            row_extend_border(d + xres*(row + i), extrowbuf, col, width, xres, extend_left, extend_right, 0.0);
            precomp_row_fill(req, prows[ii], extrowbuf, rowbuflen);
        }
        else {
            min_max_precomputed_row_copy(prows[ii], prows[ii-1], req, rowbuflen);
        }
    }
    for (i = 1; i <= extend_up; i++) {
        ii = extend_up - i;
        if (i <= (guint)row) {
            row_extend_border(d + xres*(row - i), extrowbuf, col, width, xres, extend_left, extend_right, 0.0);
            precomp_row_fill(req, prows[ii], extrowbuf, rowbuflen);
        }
        else {
            min_max_precomputed_row_copy(prows[ii], prows[ii+1], req, rowbuflen);
        }
    }

    /* Go through the rows and extract the minima or maxima from the precomputed segment data. */
    i = 0;
    while (TRUE) {
        mask_rle_execute_min_max(mrle, prows, outbuf + i*width, width, maximum);
        i++;
        if (i == (guint)height)
            break;

        /* Rotate physically prows[] so that the current row is at the zeroth position.  We could use cyclic buffers
         * but then all subordinate functions would know how to handle them and calculating mod() for all indexing is
         * inefficient anyway. */
        prow = prows[0];
        for (ii = 0; ii < mmp->kyres-1; ii++)
            prows[ii] = prows[ii+1];
        prows[mmp->kyres-1] = prow;

        /* Precompute the new row at the bottom. */
        ii = row + i + extend_down;
        if (ii < yres) {
            row_extend_border(d + xres*ii, extrowbuf, col, width, xres, extend_left, extend_right, 0.0);
            precomp_row_fill(req, prow, extrowbuf, rowbuflen);
        }
        else {
            g_assert(mmp->kyres >= 2);
            min_max_precomputed_row_copy(prow, prows[mmp->kyres-2], req, rowbuflen);
        }
    }
}

static gboolean
kernel_is_nonempty(GwyDataField *dfield)
{
    guint i, n = dfield->xres * dfield->yres;
    gdouble *d = dfield->data;

    for (i = 0; i < n; i++, d++) {
        if (*d)
            return TRUE;
    }
    return FALSE;
}

/* NB: The kernel passed to this function should be non-empty. */
static void
gwy_data_field_area_filter_min_max_real(GwyDataField *data_field,
                                        GwyDataField *kernel,
                                        gdouble *outbuf,
                                        GwyMinMaxFilterType filtertype,
                                        gint col, gint row,
                                        gint width, gint height)
{
    MinMaxPrecomputed mmp;
    gint i, j, xres, yres, kxres, kyres;

    if (width == 0 || height == 0)
        return;

    xres = data_field->xres;
    yres = data_field->yres;
    kxres = kernel->xres;
    kyres = kernel->yres;

    if (filtertype == GWY_MIN_MAX_FILTER_MINIMUM || filtertype == GWY_MIN_MAX_FILTER_MAXIMUM) {
        gboolean is_max = (filtertype == GWY_MIN_MAX_FILTER_MAXIMUM);

        gwy_data_field_area_rle_analyse(kernel, width, &mmp);
        if (is_max)
            gwy_data_field_area_rle_flip(mmp.mrle, kxres, kyres);
        gwy_data_field_area_min_max_execute(data_field, outbuf, &mmp, is_max, col, row, width, height);
        gwy_data_field_area_rle_free(&mmp);
    }
    else if (filtertype == GWY_MIN_MAX_FILTER_RANGE || filtertype == GWY_MIN_MAX_FILTER_NORMALIZATION) {
        gdouble *outbuf2 = g_new(gdouble, width*height);
        gdouble *d = data_field->data + row*xres + col;

        gwy_data_field_area_rle_analyse(kernel, width, &mmp);
        gwy_data_field_area_min_max_execute(data_field, outbuf2, &mmp, FALSE, col, row, width, height);

        gwy_data_field_area_rle_flip(mmp.mrle, kxres, kyres);
        gwy_data_field_area_min_max_execute(data_field, outbuf, &mmp, TRUE, col, row, width, height);
        gwy_data_field_area_rle_free(&mmp);

        if (filtertype == GWY_MIN_MAX_FILTER_RANGE) {
            for (i = 0; i < width*height; i++)
                outbuf[i] -= outbuf2[i];
        }
        else {
            for (i = 0; i < height; i++) {
                for (j = 0; j < width; j++) {
                    gdouble min = outbuf2[i*width + j];
                    gdouble max = outbuf[i*width + j];

                    if (G_UNLIKELY(min == max))
                        outbuf[i*width + j] = 0.5;
                    else
                        outbuf[i*width + j] = (d[i*xres + j] - min)/(max - min);
                }
            }
        }
        g_free(outbuf2);
    }
    else if (filtertype == GWY_MIN_MAX_FILTER_OPENING || filtertype == GWY_MIN_MAX_FILTER_CLOSING) {
        gboolean is_closing = (filtertype == GWY_MIN_MAX_FILTER_CLOSING);
        /* To limit the area of application but keep the influence of surrouding pixels as if we did erosion and
         * dilation on the entire field, we must perform the first operation in an extended area. */
        gint extcol = MAX(0, col - kxres/2);
        gint extrow = MAX(0, row - kyres/2);
        gint extwidth = MIN(xres, col + width + kxres/2) - extcol;
        gint extheight = MIN(yres, row + height + kyres/2) - extrow;
        GwyDataField *tmpfield;

        gwy_data_field_area_rle_analyse(kernel, extwidth, &mmp);
        if (is_closing)
            gwy_data_field_area_rle_flip(mmp.mrle, kxres, kyres);
        tmpfield = gwy_data_field_new(extwidth, extheight, extwidth, extheight, FALSE);
        gwy_data_field_area_min_max_execute(data_field, tmpfield->data, &mmp, is_closing,
                                            extcol, extrow, extwidth, extheight);

        if (extcol == col && extrow == row && extwidth == width && extheight == height) {
            /* Avoid repeating the analysis for full-field application. */
            gwy_data_field_area_rle_flip(mmp.mrle, kxres, kyres);
        }
        else {
            gwy_data_field_area_rle_free(&mmp);
            gwy_data_field_area_rle_analyse(kernel, width, &mmp);
            if (!is_closing)
                gwy_data_field_area_rle_flip(mmp.mrle, kxres, kyres);
        }
        gwy_data_field_area_min_max_execute(tmpfield, outbuf, &mmp, !is_closing,
                                            col - extcol, row - extrow, width, height);
        gwy_data_field_area_rle_free(&mmp);
        g_object_unref(tmpfield);
    }
    else {
        g_return_if_reached();
    }
}

/**
 * gwy_data_field_area_filter_min_max:
 * @data_field: A data field to apply the filter to.
 * @kernel: Data field defining the flat structuring element.
 * @filtertype: The type of filter to apply.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Applies a morphological operation with a flat structuring element to a part of a data field.
 *
 * Morphological operations with flat structuring elements can be expressed using minimum (erosion) and maximum
 * (dilation) filters that are the basic operations this function can perform.
 *
 * The kernel field is a mask that defines the shape of the flat structuring element.  It is reflected for all maximum
 * operations (dilation).  For symmetrical kernels this does not matter.  You can use
 * gwy_data_field_elliptic_area_fill() to create a true circular (or elliptical) kernel.
 *
 * The kernel is implicitly centered, i.e. it will be applied symmetrically to avoid unexpected data movement.
 * Even-sized kernels (generally not recommended) will extend farther towards the top left image corner for minimum
 * (erosion) and towards the bottom right corner for maximum (dilation) operations due to the reflection.  If you need
 * off-center structuring elements you can add empty rows or columns to one side of the kernel to counteract the
 * symmetrisation.
 *
 * The operation is linear-time in kernel size for any convex kernel.  Note gwy_data_field_area_filter_minimum() and
 * gwy_data_field_area_filter_maximum(), which are limited to square structuring elements, are much faster for large
 * sizes of the squares.
 *
 * The exterior is always handled as %GWY_EXTERIOR_BORDER_EXTEND.
 *
 * Since: 2.43
 **/
void
gwy_data_field_area_filter_min_max(GwyDataField *data_field,
                                   GwyDataField *kernel,
                                   GwyMinMaxFilterType filtertype,
                                   gint col, gint row,
                                   gint width, gint height)
{
    GwyDataField *redkernel, *buf;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return;
    g_return_if_fail(GWY_IS_DATA_FIELD(kernel));
    redkernel = gwy_data_field_duplicate(kernel);
    gwy_data_field_grains_autocrop(redkernel, TRUE, NULL, NULL, NULL, NULL);
    buf = gwy_data_field_new(width, height, width, height, FALSE);

    /* XXX: We do all the kernel analysis repeatedly. */
    if (kernel_is_nonempty(redkernel)) {
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(data_field,redkernel,buf,filtertype,col,row,width,height)
#endif
        {
            gint i = gwy_omp_chunk_start(height);
            gint ifrom = row + i, ito = row + gwy_omp_chunk_end(height);
            gdouble *b = buf->data + i*width;

            gwy_data_field_area_filter_min_max_real(data_field, redkernel, b, filtertype,
                                                    col, ifrom, width, ito - ifrom);

        }
    }
    g_object_unref(redkernel);
    gwy_data_field_area_copy(buf, data_field, 0, 0, width, height, col, row);
    g_object_unref(buf);
}

/**
 * gwy_data_field_area_filter_disc_asf:
 * @data_field: A data field to apply the filter to.
 * @radius: Maximum radius of the circular structuring element, in pixels. For radius 0 and smaller the filter is
 *          no-op.
 * @closing: %TRUE requests an opening-closing filter (i.e. ending with closing), %FALSE requests a closing-opening
 *           filter (i.e. ending with opening).
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Applies an alternating sequential morphological filter with a flat disc structuring element to a part of a data
 * field.
 *
 * Alternating sequential filter is a filter consisting of repeated opening and closing (or closing and opening) with
 * progressively larger structuring elements.  This function performs such filtering for sequence of structuring
 * elements consisting of true Euclidean discs with increasing radii.  The largest disc in the sequence fits into
 * a (2@size + 1) × (2@size + 1) square.
 *
 * Since: 2.43
 **/
void
gwy_data_field_area_filter_disc_asf(GwyDataField *data_field,
                                    gint radius,
                                    gboolean closing,
                                    gint col,
                                    gint row,
                                    gint width,
                                    gint height)
{
    GwyMinMaxFilterType filtertype1, filtertype2;
    GwyDataField *buf;
    gint r;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return;

    if (closing) {
        filtertype1 = GWY_MIN_MAX_FILTER_OPENING;
        filtertype2 = GWY_MIN_MAX_FILTER_CLOSING;
    }
    else {
        filtertype1 = GWY_MIN_MAX_FILTER_CLOSING;
        filtertype2 = GWY_MIN_MAX_FILTER_OPENING;
    }

    buf = gwy_data_field_new(width, height, width, height, FALSE);

    /* XXX: We do all the kernel analysis repeatedly. */
    for (r = 1; r <= radius; r++) {
        gint size = 2*r + 1;
        GwyDataField *kernel = gwy_data_field_new(size, size, size, size, TRUE);
        gwy_data_field_elliptic_area_fill(kernel, 0, 0, size, size, 1.0);
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(data_field,kernel,buf,filtertype1,filtertype2,col,row,width,height)
#endif
        {
            gint i = gwy_omp_chunk_start(height);
            gint ifrom = row + i, ito = row + gwy_omp_chunk_end(height);
            gdouble *b = buf->data + i*width;

            gwy_data_field_area_filter_min_max_real(data_field, kernel, b, filtertype1, col, ifrom, width, ito - ifrom);
        }
        gwy_data_field_area_copy(buf, data_field, 0, 0, width, height, col, row);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(data_field,kernel,buf, filtertype1,filtertype2,col,row,width,height)
#endif
        {
            gint i = gwy_omp_chunk_start(height);
            gint ifrom = row + i, ito = row + gwy_omp_chunk_end(height);
            gdouble *b = buf->data + i*width;

            gwy_data_field_area_filter_min_max_real(data_field, kernel, b, filtertype2, col, ifrom, width, ito - ifrom);
        }
        gwy_data_field_area_copy(buf, data_field, 0, 0, width, height, col, row);
        g_object_unref(kernel);
    }
    g_object_unref(buf);
}

/**
 * gwy_data_field_area_filter_median:
 * @data_field: A data field to apply the filter to.
 * @size: Size of area to take median of.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 *
 * Filters a rectangular part of a data field with median filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_kth_rank() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_area_filter_median(GwyDataField *data_field,
                                  gint size,
                                  gint col, gint row,
                                  gint width, gint height)
{

    gint xres, i, j;
    gdouble *buffer, *data;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return;
    g_return_if_fail(size > 0);

    buffer = g_new(gdouble, width*height);
    xres = data_field->xres;
    data = data_field->data + xres*row + col;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(data,buffer,xres,width,height,size)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(height);
        gint ito = gwy_omp_chunk_end(height);
        gdouble *kernel = g_new(gdouble, size*size);
        gint k;

        for (i = ifrom; i < ito; i++) {
            gint yfrom = MAX(0, i - (size-1)/2);
            gint yto = MIN(height-1, i + size/2);

            for (j = 0; j < width; j++) {
                gint xfrom = MAX(0, j - (size-1)/2);
                gint xto = MIN(width-1, j + size/2);
                gint len = xto - xfrom + 1;
                for (k = yfrom; k <= yto; k++) {
                    gwy_assign(kernel + len*(k - yfrom), data + k*xres + xfrom, len);
                }
                buffer[i*width + j] = gwy_math_median(len*(yto - yfrom + 1), kernel);
            }
        }
        g_free(kernel);
    }

    for (i = 0; i < height; i++)
        gwy_assign(data + i*xres, buffer + i*width, width);
    g_free(buffer);
    gwy_data_field_invalidate(data_field);
}

/**
 * gwy_data_field_filter_median:
 * @data_field: A data field to apply the filter to.
 * @size: Size of area to take median of.
 *
 * Filters a data field with median filter.
 *
 * This method uses a simple square kernel.  Use the general function gwy_data_field_area_filter_kth_rank() to perform
 * filtering with a different kernel, for instance circular.
 **/
void
gwy_data_field_filter_median(GwyDataField *data_field,
                             gint size)
{
    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    gwy_data_field_area_filter_median(data_field, size, 0, 0, data_field->xres, data_field->yres);
}

static void
median_radix_tree_alloc(MedianRadixTree *mrtree, guint n)
{
    guint size, i;

    gwy_clear(mrtree, 1);
    mrtree->n = n;

    size = n/8 + (n % 8 ? 1 : 0);
    mrtree->bits = g_new0(guint8, size);

    size = (size + 1)/2;
    mrtree->bcount8 = g_new0(guint8, size);
    mrtree->levels = 2;

    size = (size + 1)/2;
    if (size == 1)
        return;
    mrtree->bcount32 = g_new0(guint8, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->bcount128 = g_new0(guint8, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->scount512 = g_new0(guint16, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->scount2048 = g_new0(guint16, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->scount8192 = g_new0(guint16, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->scount32768 = g_new0(guint16, size);
    mrtree->levels++;

    size = (size + 3)/4;
    if (size == 1)
        return;
    mrtree->ucount = g_new0(guint32*, (CHAR_BIT*sizeof(guint) - 15)/2);
    i = 0;
    do {
        mrtree->ucount[i] = g_new0(guint32, size);
        mrtree->levels++;

        size = (size + 3)/4;
        i++;
    } while (size > 1);
}

static void
median_radix_tree_free(MedianRadixTree *mrtree)
{
    if (mrtree->ucount) {
        guint i;

        for (i = 0; i < mrtree->levels - MEDIAN_RADIX_TREE_BASE_LEVELS; i++)
            g_free(mrtree->ucount[i]);
        g_free(mrtree->ucount);
    }

    g_free(mrtree->scount32768);
    g_free(mrtree->scount8192);
    g_free(mrtree->scount2048);
    g_free(mrtree->scount512);
    g_free(mrtree->bcount128);
    g_free(mrtree->bcount32);
    g_free(mrtree->bcount8);
    g_free(mrtree->bits);
}

/* The compiler should be clever enough to avoid repeated comparison of levels with the constants. GCC seems to be. */
static void
median_radix_tree_add(MedianRadixTree *mrtree, guint v)
{
    guint j, levels = mrtree->levels;

    mrtree->len++;

    j = (v & 0x7);
    v >>= 3;
    mrtree->bits[v] |= (1 << j);

    j = (v & 0x1) ? 0x10 : 0x01;
    mrtree->bcount8[v >> 1] += j;
    if (levels == 2)
        return;

    v >>= 2;
    mrtree->bcount32[v]++;
    if (levels == 3)
        return;

    v >>= 2;
    mrtree->bcount128[v]++;
    if (levels == 4)
        return;

    v >>= 2;
    mrtree->scount512[v]++;
    if (levels == 5)
        return;

    v >>= 2;
    mrtree->scount2048[v]++;
    if (levels == 6)
        return;

    v >>= 2;
    mrtree->scount8192[v]++;
    if (levels == 7)
        return;

    v >>= 2;
    mrtree->scount32768[v]++;
    if (levels == 8)
        return;

    for (j = 0; j < levels - MEDIAN_RADIX_TREE_BASE_LEVELS; j++) {
        v >>= 2;
        mrtree->ucount[j][v]++;
    }
}

static void
median_radix_tree_remove(MedianRadixTree *mrtree, guint v)
{
    guint j, levels = mrtree->levels;

    mrtree->len--;

    j = (v & 0x7);
    v >>= 3;
    mrtree->bits[v] &= ~(1 << j);

    j = (v & 0x1) ? 0x10 : 0x01;
    mrtree->bcount8[v >> 1] -= j;
    if (levels == 2)
        return;

    v >>= 2;
    mrtree->bcount32[v]--;
    if (levels == 3)
        return;

    v >>= 2;
    mrtree->bcount128[v]--;
    if (levels == 4)
        return;

    v >>= 2;
    mrtree->scount512[v]--;
    if (levels == 5)
        return;

    v >>= 2;
    mrtree->scount2048[v]--;
    if (levels == 6)
        return;

    v >>= 2;
    mrtree->scount8192[v]--;
    if (levels == 7)
        return;

    v >>= 2;
    mrtree->scount32768[v]--;
    if (levels == 8)
        return;

    for (j = 0; j < levels - MEDIAN_RADIX_TREE_BASE_LEVELS; j++) {
        v >>= 2;
        mrtree->ucount[j][v]--;
    }
}

static inline guint
median_radix_tree_advance32(const guint32 *ucount, guint v, guint *k)
{
    guint s, t;

    ucount += v;
    s = ucount[0];
    if (s <= *k) {
        t = s;
        s += ucount[1];
        if (s <= *k) {
            t = s;
            s += ucount[2];
            if (s <= *k) {
                v += 3;
                *k -= s;
            }
            else {
                v += 2;
                *k -= t;
            }
        }
        else {
            v += 1;
            *k -= t;
        }
    }

    return v << 2;
}

static inline guint
median_radix_tree_advance16(const guint16 *scount, guint v, guint *k)
{
    guint s, t;

    scount += v;
    s = scount[0];
    if (s <= *k) {
        t = s;
        s += scount[1];
        if (s <= *k) {
            t = s;
            s += scount[2];
            if (s <= *k) {
                v += 3;
                *k -= s;
            }
            else {
                v += 2;
                *k -= t;
            }
        }
        else {
            v += 1;
            *k -= t;
        }
    }

    return v << 2;
}

static inline guint
median_radix_tree_advance8(const guint8 *bcount, guint v, guint *k)
{
    guint s, t;

    bcount += v;
    s = bcount[0];
    if (s <= *k) {
        t = s;
        s += bcount[1];
        if (s <= *k) {
            t = s;
            s += bcount[2];
            if (s <= *k) {
                v += 3;
                *k -= s;
            }
            else {
                v += 2;
                *k -= t;
            }
        }
        else {
            v += 1;
            *k -= t;
        }
    }

    return v << 2;
}

/* This can be relatively complicated compared to add/remove because we run add/remove twenty or hundred times per one
 * k-th rank invocation. */
static guint
median_radix_tree_kth_rank(const MedianRadixTree *mrtree, guint k)
{
    guint v, j, s, t, levels = mrtree->levels;
    const guint8 *bcount8;

    v = 0;
    if (levels > MEDIAN_RADIX_TREE_BASE_LEVELS) {
        for (j = levels - MEDIAN_RADIX_TREE_BASE_LEVELS; j; j--)
            v = median_radix_tree_advance32(mrtree->ucount[j-1], v, &k);
    }

    if (levels >= 8)
        v = median_radix_tree_advance16(mrtree->scount32768, v, &k);
    if (levels >= 7)
        v = median_radix_tree_advance16(mrtree->scount8192, v, &k);
    if (levels >= 6)
        v = median_radix_tree_advance16(mrtree->scount2048, v, &k);
    if (levels >= 5)
        v = median_radix_tree_advance16(mrtree->scount512, v, &k);
    if (levels >= 4)
        v = median_radix_tree_advance8(mrtree->bcount128, v, &k);
    if (levels >= 3)
        v = median_radix_tree_advance8(mrtree->bcount32, v, &k);

    bcount8 = mrtree->bcount8 + v/2;
    s = (bcount8[0] & 0x0f);
    if (s <= k) {
        t = s;
        s += (bcount8[0] >> 4);
        if (s <= k) {
            t = s;
            s += (bcount8[1] & 0x0f);
            if (s <= k) {
                v += 3;
                k -= s;
            }
            else {
                v += 2;
                k -= t;
            }
        }
        else {
            v += 1;
            k -= t;
        }
    }

    t = mrtree->bits[v];
    for (j = 0; j < 8; j++) {
        if (t & 1) {
            if (!k)
                break;
            k--;
        }
        t >>= 1;
    }

    return (v << 3) + j;
}

/* Return k-th value from the end, not the beginning. */
static inline guint
median_radix_tree_kth_erank(const MedianRadixTree *mrtree, guint k)
{
    return median_radix_tree_kth_rank(mrtree, mrtree->len-1 - k);
}

static void
filter_kernel_boundaries_create(GwyDataField *kernel,
                                FilterKernelBoundaries *kbound,
                                guint xres)
{
    GArray *up, *down, *left, *right;
    gdouble *data = kernel->data;
    guint kxres = kernel->xres, kyres = kernel->yres;
    guint i, j, k, kk;

    up = g_array_new(FALSE, FALSE, sizeof(guint));
    down = g_array_new(FALSE, FALSE, sizeof(guint));
    left = g_array_new(FALSE, FALSE, sizeof(guint));
    right = g_array_new(FALSE, FALSE, sizeof(guint));

    k = 0;
    for (i = 0; i < kyres; i++) {
        kk = i*xres;
        for (j = 0; j < kxres; j++, k++, kk++) {
            if (data[k] <= 0.0)
                continue;

            /* Use xres from the image, not the kernel.  This means the indices are directly usable with the image. */
            if (!i || data[k-kxres] <= 0.0)
                g_array_append_val(up, kk);
            if (!j || data[k-1] <= 0.0)
                g_array_append_val(left, kk);
            if (j == kxres-1 || data[k+1] <= 0.0)
                g_array_append_val(right, kk);
            if (i == kyres-1 || data[k+kxres] <= 0.0)
                g_array_append_val(down, kk);
        }
    }

    kbound->up = g_new(guint, up->len + left->len + right->len + down->len);
    gwy_assign(kbound->up, up->data, up->len);
    kbound->nup = up->len;
    k = kbound->nup;

    kbound->down = kbound->up + k;
    gwy_assign(kbound->down, down->data, down->len);
    kbound->ndown = down->len;
    k += kbound->ndown;

    kbound->left = kbound->up + k;
    gwy_assign(kbound->left, left->data, left->len);
    kbound->nleft = left->len;
    k += kbound->nleft;

    kbound->right = kbound->up + k;
    gwy_assign(kbound->right, right->data, right->len);
    kbound->nright = right->len;

    g_assert(kbound->nup == kbound->ndown);
    g_assert(kbound->nleft == kbound->nright);

    g_array_free(up, TRUE);
    g_array_free(down, TRUE);
    g_array_free(left, TRUE);
    g_array_free(right, TRUE);
}

static void
filter_kernel_boundaries_free(FilterKernelBoundaries *kbound)
{
    g_free(kbound->up);
}

/* We do signed arithmetic with the dimensions here */
static GwyDataField*
crop_extend_field_for_kernel(GwyDataField *dfield,
                             gint col, gint row, gint w, gint h,
                             GwyDataField *kernel)
{
    GwyDataField *tmp;
    gint xres = dfield->xres, yres = dfield->yres;
    gint kxres = kernel->xres, kyres = kernel->yres;
    gint extcol, extrow, extw, exth, extxend, extyend;

    /* Extfield area origin in @dfield (can stick outside). */
    extcol = col - kxres/2;
    extrow = row - kyres/2;
    /* Extfield dimensions. */
    extw = w + kxres-1;
    exth = h + kyres-1;
    /* Extfield area bottom right corner+1 in @dfield (can stick outside). */
    extxend = extcol + extw;
    extyend = extrow + exth;

    /* Crop-only; also reduces to plain duplicate() for exact size match. */
    if (extcol >= 0 && extrow >= 0 && extxend <= xres && extyend <= yres)
        return gwy_data_field_area_extract(dfield, extcol, extrow, extw, exth);

    /* Extend-only */
    if (extcol <= 0 && extrow <= 0 && extxend >= xres && extyend >= yres) {
        return gwy_data_field_extend(dfield, -extcol, extxend-xres, -extrow, extyend-yres,
                                     GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);
    }

    /* When we both crop and extend, crop first, then border-extend the parts we need to extend. */
    tmp = gwy_data_field_area_extract(dfield, MAX(extcol, 0), MAX(extrow, 0),
                                      MIN(extxend, xres) - MAX(extcol, 0),
                                      MIN(extyend, yres) - MAX(extrow, 0));
    dfield = gwy_data_field_extend(tmp, MAX(-extcol, 0), MAX(extxend-xres, 0),
                                   MAX(-extrow, 0), MAX(extyend-yres, 0),
                                   GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);
    g_object_unref(tmp);

    return dfield;
}

static void
area_rank_filter_kernel_radixtree(GwyDataField *dfield,
                                  guint col, guint row, guint w, guint h,
                                  GwyDataField *kernel, const FilterKernelBoundaries *kbound, guint k,
                                  GwySetFractionFunc set_fraction,
                                  gboolean *ok)
{
    GwyDataField *extfield;
    guint xres = dfield->xres;
    guint kxres = kernel->xres, kyres = kernel->yres;
    guint i, j, n, kn, extxres, extyres, extn, workdone;
    MedianRadixTree mrtree;
    guint *revindex, *ranks, *rr, *filtered;
    gdouble *d, *e, *kd;
    gboolean cancelled = FALSE;

    if (w == 0 || h == 0)
        return;

    /* Figure out the field area we need to execute the filter. */
    extfield = crop_extend_field_for_kernel(dfield, col, row, w, h, kernel);
    extxres = extfield->xres;
    extyres = extfield->yres;
    extn = extxres * extyres;

    /* Perform rank-transform of the data. */
    revindex = g_new(guint, extn);
    for (i = 0; i < extn; i++)
        revindex[i] = i;
    gwy_math_sort_with_index(extn, extfield->data, revindex);

    ranks = g_new(guint, extn);
    for (i = 0; i < extn; i++)
        ranks[revindex[i]] = i;

    /* Initialise the tree with ranks corresponding to kernel in top left corner. */
    median_radix_tree_alloc(&mrtree, extn);
    kd = kernel->data;
    kn = 0;
    for (i = 0; i < kyres; i++) {
        for (j = 0; j < kxres; j++) {
            if (kd[i*kxres + j] > 0.0) {
                median_radix_tree_add(&mrtree, ranks[i*extxres + j]);
                kn++;
            }
        }
    }

    /* Go vertically in the inner loop because then we tend to access ranks[] in contiguous blocks. Going hozirontally
     * would basically ensure we always access one value in each row. Recycle the revindex[] array as filtered[]. */
    filtered = revindex;
    workdone = 0;
    j = 0;
    while (!cancelled) {
        guint nup = kbound->nup, ndown = kbound->ndown, nleft = kbound->nleft, nright = kbound->nright;
        const guint *up = kbound->up, *down = kbound->down, *left = kbound->left, *right = kbound->right;

        if (j % 2 == 0) {
            /* Downward pass. */
            i = 0;
            while (i < h-1) {
                filtered[i*w + j] = median_radix_tree_kth_rank(&mrtree, k);
                rr = ranks + i*extxres + j;
                for (n = 0; n < nup; n++)
                    median_radix_tree_remove(&mrtree, rr[up[n]]);
                i++;
                rr = ranks + i*extxres + j;
                for (n = 0; n < ndown; n++)
                    median_radix_tree_add(&mrtree, rr[down[n]]);
            }
        }
        else {
            /* Upward pass. */
            i = h-1;
            while (i) {
                filtered[i*w + j] = median_radix_tree_kth_rank(&mrtree, k);
                rr = ranks + i*extxres + j;
                for (n = 0; n < ndown; n++)
                    median_radix_tree_remove(&mrtree, rr[down[n]]);
                i--;
                rr = ranks + i*extxres + j;
                for (n = 0; n < nup; n++)
                    median_radix_tree_add(&mrtree, rr[up[n]]);
            }
        }
        filtered[i*w + j] = median_radix_tree_kth_rank(&mrtree, k);

        /* Move right. */
        if (j == w-1)
            break;

        rr = ranks + i*extxres + j;
        for (n = 0; n < nleft; n++)
            median_radix_tree_remove(&mrtree, rr[left[n]]);
        j++;
        rr = ranks + i*extxres + j;
        for (n = 0; n < nright; n++)
            median_radix_tree_add(&mrtree, rr[right[n]]);

        workdone += kn*h;
        /* Check for cancellation. The caller must ensure set_fraction != NULL only in the master thread because
         * set_fraction() can make GTK+ calls. However, this slows down only the master thread, harming scaling. */
        if (set_fraction && workdone >= 1000000) {
            if (!set_fraction(j/(gdouble)w))
                gwy_omp_atomic_write_boolean(ok, FALSE);
            workdone = 0;
        }
        /* In all threads, including the master for consistency, we read back the ok flag and cancel if requested. */
        cancelled = !gwy_omp_atomic_read_boolean(ok);
    }

    if (!cancelled) {
        d = dfield->data + row*xres + col;
        e = extfield->data;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++)
                d[i*xres + j] = e[filtered[i*w + j]];
        }
    }

    g_free(ranks);
    g_free(filtered);  /* == revindex */
    g_object_unref(extfield);
    median_radix_tree_free(&mrtree);
    gwy_data_field_invalidate(dfield);
}

static void
area_rank_filter_kernel_direct(GwyDataField *dfield,
                               guint col, guint row, guint w, guint h,
                               GwyDataField *kernel, guint k,
                               guint ksize)
{
    GwyDataField *extfield;
    guint xres = dfield->xres;
    guint kxres = kernel->xres, kyres = kernel->yres;
    guint i, j, n, extxres;
    guint *kindex;
    gdouble *d, *ed, *kd;

    /* Figure out the field area we need to execute the filter. */
    extfield = crop_extend_field_for_kernel(dfield, col, row, w, h, kernel);
    extxres = extfield->xres;

    kindex = g_new(guint, ksize);
    ksize = 0;
    kd = kernel->data;
    for (i = 0; i < kyres; i++) {
        for (j = 0; j < kxres; j++) {
            if (kd[i*kxres + j] > 0.0)
                kindex[ksize++] = i*extxres + j;
        }
    }

    d = dfield->data + row*xres + col;
    ed = extfield->data;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            private(i,j,n) \
            shared(d,ed,kindex,ksize,extxres,xres,k,h,w)
#endif
    {
        gdouble *buf = g_new(gdouble, ksize);
        guint ifrom = gwy_omp_chunk_start(h), ito = gwy_omp_chunk_end(h);

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < w; j++) {
                gdouble *e = ed + i*extxres + j;
                for (n = 0; n < ksize; n++)
                    buf[n] = e[kindex[n]];
                d[i*xres + j] = gwy_math_kth_rank(ksize, buf, k);
            }
        }

        g_free(buf);
    }
    gwy_data_field_invalidate(dfield);

    g_free(kindex);
    g_object_unref(extfield);
}

/**
 * gwy_data_field_area_filter_kth_rank:
 * @data_field: A data field to apply the filter to.
 * @kernel: Data field defining the kernel shape.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @k: Rank of the value to store as the output (from lowest to highest).
 * @set_fraction: Function that sets fraction to output (or %NULL).
 *
 * Applies a @k-th rank filter to a part of a data field.
 *
 * Pass half the number of non-zero values in @kernel as @k for a median filter.
 *
 * The kernel field is a mask that defines the shape of the kernel.  You can use gwy_data_field_elliptic_area_fill()
 * to create a true circular (or elliptical) kernel.  The kernel must be non-empty.
 *
 * The kernel is implicitly centered, i.e. it will be applied symmetrically to avoid unexpected data movement.
 *
 * The exterior is always handled as %GWY_EXTERIOR_BORDER_EXTEND.
 *
 * If the operation is aborted the contents of @data_field is untouched.
 *
 * Returns: %TRUE if the operation was not aborted via @set_fraction returning %FALSE; %FALSE if it was aborted.
 *
 * Since: 2.51
 **/
gboolean
gwy_data_field_area_filter_kth_rank(GwyDataField *data_field,
                                    GwyDataField *kernel,
                                    gint col, gint row,
                                    gint width, gint height,
                                    gint k,
                                    GwySetFractionFunc set_fraction)
{
    FilterKernelBoundaries kbound;
    GwyDataField *redkernel;
    gdouble *kd;
    guint i, n, extw;
    gboolean ok;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return FALSE;
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel), FALSE);

    kd = kernel->data;
    n = 0;
    for (i = 0; i < kernel->xres * kernel->yres; i++) {
        if (kd[i] > 0.0)
            n++;
    }
    /* This also fails for an empty kernel. */
    g_return_val_if_fail(k >= 0 && k < n, FALSE);

    /* Catch some simpler cases (currently cannot be aborted). */
    if (k == 0) {
        gwy_data_field_area_filter_min_max(data_field, kernel, GWY_MIN_MAX_FILTER_MINIMUM, col, row, width, height);
        return TRUE;
    }
    if (k == n-1) {
        gwy_data_field_area_filter_min_max(data_field, kernel, GWY_MIN_MAX_FILTER_MAXIMUM, col, row, width, height);
        return TRUE;
    }

    redkernel = gwy_data_field_duplicate(kernel);
    /* FIXME: It seems there is no way to counteract the autocrop so off-centre kernels do not work! */
    gwy_data_field_grains_autocrop(redkernel, TRUE, NULL, NULL, NULL, NULL);
    if (n <= 25) {
        /* The filter should be fast for tiny kernels. */
        area_rank_filter_kernel_direct(data_field, col, row, width, height, redkernel, k, n);
        g_object_unref(redkernel);
        return TRUE;
    }

    /* Extfield area dimensions. Must match crop_extend_field_for_kernel()! */
    extw = width + redkernel->xres-1;
    /* Analyse kernel boundaries. */
    filter_kernel_boundaries_create(redkernel, &kbound, extw);

    ok = TRUE;
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
        shared(data_field,col,row,width,height,redkernel,kbound,k,ok,set_fraction)
#endif
    {
        gint ifrom = row + gwy_omp_chunk_start(height);
        gint ito = row + gwy_omp_chunk_end(height);
        GwySetFractionFunc tset_fraction = NULL;

#ifdef _OPENMP
#pragma omp master
        tset_fraction = set_fraction;
#endif

        area_rank_filter_kernel_radixtree(data_field, col, ifrom, width, ito - ifrom, redkernel, &kbound, k,
                                          tset_fraction, &ok);
    }
    g_object_unref(redkernel);
    filter_kernel_boundaries_free(&kbound);

    return ok;
}

/**
 * gwy_data_line_part_filter_kth_rank:
 * @data_line: A data line.
 * @klen: Kernel size. The kernel is symmetrical segment of @klen pixels.
 * @from: The index in @data_line to start from (inclusive).
 * @to: The index in @data_line to stop (noninclusive).
 * @k: Rank of the value to store as the output (from lowest to highest). It must be between 0 and @klen-1
 *     (inclusive).
 *
 * Applies a @k-th rank filter to a part of a data line.
 *
 * The exterior is always handled as %GWY_EXTERIOR_BORDER_EXTEND.
 *
 * Since: 2.63
 **/
void
gwy_data_line_part_filter_kth_rank(GwyDataLine *data_line,
                                   gint klen,
                                   gint from,
                                   gint to,
                                   gint k)
{
    MedianRadixTree mrtree;
    gint extfrom = from - (klen-1 - klen/2), extto = to + klen/2, extres = extto - extfrom;
    gdouble *data, *datacopy;
    guint *revindex, *ranks;
    gint i, j, res;

    g_return_if_fail(GWY_IS_DATA_LINE(data_line));
    g_return_if_fail(klen > 0);
    g_return_if_fail(k >= 0 && k < klen);

    if (klen == 1)
        return;

    res = data_line->res;
    data = data_line->data;
    datacopy = g_new(gdouble, extres);
    j = 0;

    /* Crop-extend the line segment. */
    for (i = extfrom; i < 0; i++)
        datacopy[j++] = data[0];
    gwy_assign(datacopy + j, data + MAX(0, extfrom), MIN(res, extto) - MAX(0, extfrom));
    j += MIN(res, extto) - MAX(0, extfrom);
    while (j < extres)
        datacopy[j++] = data[res-1];

    /* TODO: There are other cases we might want to handle specially:
     * k == 0 (minimum), and generally small k (use direct method)
     * k == klen-1 (maximum), and generally small klen-1 - k (use direct method)
     * small klen (use direct method)
     *
     * The threshold is given by doing at least log(extres) work per pixel just because of sorting. The radix tree
     * work is only log(klen) (or actually a fixed constant here), so that is not a major cost.
     **/

    revindex = g_new(guint, extres);
    ranks = g_new(guint, extres);
    for (i = 0; i < extres; i++)
        revindex[i] = i;
    gwy_math_sort_with_index(extres, datacopy, revindex);
    for (i = 0; i < extres; i++)
        ranks[revindex[i]] = i;
    g_free(revindex);

    median_radix_tree_alloc(&mrtree, extres);
    for (i = 0; i < klen; i++)
        median_radix_tree_add(&mrtree, ranks[i]);

    i = 0;
    while (TRUE) {
        j = median_radix_tree_kth_rank(&mrtree, k);
        data[from + i] = datacopy[j];

        if (i == extres - klen)
            break;

        median_radix_tree_remove(&mrtree, ranks[i]);
        median_radix_tree_add(&mrtree, ranks[i+klen]);
        i++;
    }

    median_radix_tree_free(&mrtree);

    g_free(ranks);
    g_free(datacopy);
}

/* Add a value, updating integer value interval boundaries f1 and f2 and the running sum. */
static void
median_radix_tree_add_tmean(MedianRadixTree *mrtree,
                            TrimmedMeanState *tmstate,
                            const gdouble *values,
                            guint v)
{
    median_radix_tree_add(mrtree, v);
    if (v > tmstate->f2) {
        tmstate->f2 = median_radix_tree_kth_erank(mrtree, tmstate->k2);
        tmstate->runningsum += values[tmstate->f2];
    }
    else if (v < tmstate->f1) {
        tmstate->f1 = median_radix_tree_kth_rank(mrtree, tmstate->k1);
        tmstate->runningsum += values[tmstate->f1];
    }
    else
        tmstate->runningsum += values[v];
}

/* Remove a value, updating integer value interval boundaries f1 and f2 and the running sum. */
static void
median_radix_tree_remove_tmean(MedianRadixTree *mrtree,
                               TrimmedMeanState *tmstate,
                               const gdouble *values,
                               guint v)
{
    if (v >= tmstate->f2) {
        tmstate->runningsum -= values[tmstate->f2];
        median_radix_tree_remove(mrtree, v);
        tmstate->f2 = median_radix_tree_kth_erank(mrtree, tmstate->k2);
    }
    else if (v <= tmstate->f1) {
        tmstate->runningsum -= values[tmstate->f1];
        median_radix_tree_remove(mrtree, v);
        tmstate->f1 = median_radix_tree_kth_rank(mrtree, tmstate->k1);
    }
    else {
        tmstate->runningsum -= values[v];
        median_radix_tree_remove(mrtree, v);
    }
}

/* Assume strictly k1 < n-1 - k2.  This allows assuming strictly f1 < f2 all the time. Values within the rank interval
 * [k1, n-1 - k2] (inclusive) are used for averaging.
 *
 * This filter algorithm is written for the case of small k1 and k2, i.e. trimming of the tails.  If the filter output
 * is calculated just from a small interval of ranks, it would be more efficient to calculate the average value
 * directly from the tree (and updating f1 and f2 just once per output pixel). */
static void
area_tmean_filter_kernel_radixtree(GwyDataField *dfield,
                                   guint col, guint row, guint w, guint h,
                                   GwyDataField *kernel, const FilterKernelBoundaries *kbound,
                                   guint nlowest, guint nhighest,
                                   GwySetFractionFunc set_fraction,
                                   gboolean *ok)
{
    GwyDataField *extfield;
    guint xres = dfield->xres;
    guint kxres = kernel->xres, kyres = kernel->yres;
    guint i, j, n, kn, extxres, extyres, extn, workdone;
    MedianRadixTree mrtree;
    TrimmedMeanState tmstate;
    guint *revindex, *ranks, *rr;
    gdouble *e, *kd, *out;
    gboolean cancelled = FALSE;

    if (w == 0 || h == 0)
        return;

    /* Figure out the field area we need to execute the filter. */
    extfield = crop_extend_field_for_kernel(dfield, col, row, w, h, kernel);
    extxres = extfield->xres;
    extyres = extfield->yres;
    extn = extxres * extyres;
    e = extfield->data;

    /* Perform rank-transform of the data. */
    revindex = g_new(guint, extn);
    for (i = 0; i < extn; i++)
        revindex[i] = i;
    gwy_math_sort_with_index(extn, extfield->data, revindex);

    ranks = g_new(guint, extn);
    for (i = 0; i < extn; i++)
        ranks[revindex[i]] = i;
    g_free(revindex);

    /* Initialise the tree with ranks corresponding to kernel in top left corner. */
    median_radix_tree_alloc(&mrtree, extn);
    kd = kernel->data;
    kn = 0;
    for (i = 0; i < kyres; i++) {
        for (j = 0; j < kxres; j++) {
            if (kd[i*kxres + j] > 0.0) {
                median_radix_tree_add(&mrtree, ranks[i*extxres + j]);
                kn++;
            }
        }
    }

    /* Initialise the running sum value. */
    tmstate.k1 = nlowest;
    tmstate.k2 = nhighest;
    tmstate.f1 = median_radix_tree_kth_rank(&mrtree, tmstate.k1);
    tmstate.f2 = median_radix_tree_kth_erank(&mrtree, tmstate.k2);
    tmstate.runningsum = 0.0;
    out = g_new(gdouble, w*h);
    for (i = 0; i < kyres; i++) {
        rr = ranks + i*extxres;
        for (j = 0; j < kxres; j++) {
            if (kd[i*kxres + j] > 0.0) {
                guint v = rr[j];
                if (v >= tmstate.f1 && v <= tmstate.f2)
                    tmstate.runningsum += e[v];
            }
        }
    }
    out[0] = tmstate.runningsum;

    /* Go vertically in the inner loop because then we tend to access ranks[] in contiguous blocks.
     *
     * We must not cause violation of f1 < f2, which would occur if we removed to many values.  So instead of doiing
     * add/remove in block, we always add one, remove one, add one, remove one, etc.  And keep tmstate consistent
     * during the procedure. */
    workdone = 0;
    j = 0;
    while (!cancelled) {
        guint nup = kbound->nup, ndown = kbound->ndown, nright = kbound->nright;
        const guint *up = kbound->up, *down = kbound->down, *left = kbound->left, *right = kbound->right;

        if (j % 2 == 0) {
            /* Downward pass. */
            i = 0;
            while (i < h-1) {
                rr = ranks + i*extxres + j;
                for (n = 0; n < ndown; n++) {
                    median_radix_tree_add_tmean(&mrtree, &tmstate, e, rr[down[n] + extxres]);
                    median_radix_tree_remove_tmean(&mrtree, &tmstate, e, rr[up[n]]);
                }
                i++;
                out[i*w + j] = tmstate.runningsum;
            }
        }
        else {
            /* Upward pass. */
            i = h-1;
            while (i) {
                rr = ranks + i*extxres + j;
                for (n = 0; n < nup; n++) {
                    median_radix_tree_add_tmean(&mrtree, &tmstate, e, *(rr + up[n] - extxres));
                    median_radix_tree_remove_tmean(&mrtree, &tmstate, e, rr[down[n]]);
                }
                i--;
                out[i*w + j] = tmstate.runningsum;
            }
        }

        /* Move right. */
        if (j == w-1)
            break;

        rr = ranks + i*extxres + j;
        for (n = 0; n < nright; n++) {
            median_radix_tree_add_tmean(&mrtree, &tmstate, e, rr[right[n] + 1]);
            median_radix_tree_remove_tmean(&mrtree, &tmstate, e, rr[left[n]]);
        }
        j++;
        out[i*w + j] = tmstate.runningsum;

        workdone += kn*h;
        /* Check for cancellation. The caller must ensure set_fraction != NULL only in the master thread because
         * set_fraction() can make GTK+ calls. However, this slows down only the master thread, harming scaling. */
        if (set_fraction && workdone >= 1000000) {
            if (!set_fraction(j/(gdouble)w))
                gwy_omp_atomic_write_boolean(ok, FALSE);
            workdone = 0;
        }
        /* In all threads, including the master for consistency, we read back the ok flag and cancel if requested. */
        cancelled = !gwy_omp_atomic_read_boolean(ok);
    }

    if (!cancelled) {
        for (i = 0; i < h; i++)
            gwy_assign(dfield->data + (row + i)*xres + col, out + i*w, w);
        gwy_data_field_area_multiply(dfield, col, row, w, h, 1.0/(kn - nlowest - nhighest));
    }

    g_free(ranks);
    g_free(out);
    g_object_unref(extfield);
    median_radix_tree_free(&mrtree);
    gwy_data_field_invalidate(dfield);
}

static void
area_tmean_filter_kernel_direct(GwyDataField *dfield,
                                guint col, guint row, guint w, guint h,
                                GwyDataField *kernel,
                                guint nlowest, guint nhighest,
                                guint ksize)
{
    GwyDataField *extfield;
    guint xres = dfield->xres;
    guint kxres = kernel->xres, kyres = kernel->yres;
    guint i, j, n, extxres;
    guint *kindex;
    gdouble *d, *ed, *kd;

    /* Figure out the field area we need to execute the filter. */
    extfield = crop_extend_field_for_kernel(dfield, col, row, w, h, kernel);
    extxres = extfield->xres;

    kindex = g_new(guint, ksize);
    ksize = 0;
    kd = kernel->data;
    for (i = 0; i < kyres; i++) {
        for (j = 0; j < kxres; j++) {
            if (kd[i*kxres + j] > 0.0)
                kindex[ksize++] = i*extxres + j;
        }
    }

    d = dfield->data + row*xres + col;
    ed = extfield->data;

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            private(i,j,n) \
            shared(d,ed,kindex,ksize,extxres,xres,nlowest,nhighest,h,w)
#endif
    {
        gdouble *buf = g_new(gdouble, ksize);
        guint ifrom = gwy_omp_chunk_start(h), ito = gwy_omp_chunk_end(h);

        for (i = ifrom; i < ito; i++) {
            for (j = 0; j < w; j++) {
                gdouble *e = ed + i*extxres + j;
                for (n = 0; n < ksize; n++)
                    buf[n] = e[kindex[n]];
                d[i*xres + j] = gwy_math_trimmed_mean(ksize, buf, nlowest, nhighest);
            }
        }

        g_free(buf);
    }
    gwy_data_field_invalidate(dfield);

    g_free(kindex);
    g_object_unref(extfield);
}

/**
 * gwy_data_field_area_filter_trimmed_mean:
 * @data_field: A data field to apply the filter to.
 * @kernel: Data field defining the kernel shape.
 * @col: Upper-left column coordinate.
 * @row: Upper-left row coordinate.
 * @width: Area width (number of columns).
 * @height: Area height (number of rows).
 * @nlowest: The number of lowest values to discard.
 * @nhighest: The number of highest values to discard.
 * @set_fraction: Function that sets fraction to output (or %NULL).
 *
 * Applies a trimmed mean filter to a part of a data field.
 *
 * At least one value must remain after the trimming, i.e. @nlowest + @nhighest must be smaller than the number of
 * non-zero values in @kernel.  Usually one passes the same number as both @nlowest and @nhighest, but it is not
 * a requirement.
 *
 * The kernel field is a mask that defines the shape of the kernel.  You can use gwy_data_field_elliptic_area_fill()
 * to create a true circular (or elliptical) kernel.  The kernel must be non-empty.
 *
 * The kernel is implicitly centered, i.e. it will be applied symmetrically to avoid unexpected data movement.
 * Even-sized kernels (generally not recommended) will extend farther towards the top left image corner for minimum
 * (erosion) and towards the bottom right corner for maximum (dilation) operations due to the reflection.  If you need
 * off-center structuring elements you can add empty rows or columns to one side of the kernel to counteract the
 * symmetrisation.
 *
 * The exterior is always handled as %GWY_EXTERIOR_BORDER_EXTEND.
 *
 * If the operation is aborted the contents of @data_field is untouched.
 *
 * Returns: %TRUE if the operation was not aborted via @set_fraction returning %FALSE; %FALSE if it was aborted.
 *
 * Since: 2.53
 **/
gboolean
gwy_data_field_area_filter_trimmed_mean(GwyDataField *data_field,
                                        GwyDataField *kernel,
                                        gint col, gint row,
                                        gint width, gint height,
                                        gint nlowest, gint nhighest,
                                        GwySetFractionFunc set_fraction)
{
    FilterKernelBoundaries kbound;
    GwyDataField *redkernel;
    gdouble *kd;
    guint i, n, extw;
    gboolean ok;

    if (!_gwy_data_field_check_area(data_field, col, row, width, height, TRUE))
        return FALSE;
    g_return_val_if_fail(GWY_IS_DATA_FIELD(kernel), FALSE);

    kd = kernel->data;
    n = 0;
    for (i = 0; i < kernel->xres * kernel->yres; i++) {
        if (kd[i] > 0.0)
            n++;
    }
    /* This also fails for an empty kernel. */
    g_return_val_if_fail(nlowest + nhighest < n, FALSE);

    /* Catch some simpler cases.
     *
     * NB: This one is important to catch in order to satisfy that the trimmed mean filter gets strictly k1 < k2! */
    if (nlowest + nhighest + 1 == n) {
        return gwy_data_field_area_filter_kth_rank(data_field, kernel, col, row, width, height, nlowest, set_fraction);
    }

    redkernel = gwy_data_field_duplicate(kernel);
    gwy_data_field_grains_autocrop(redkernel, TRUE, NULL, NULL, NULL, NULL);
    gwy_data_field_threshold(redkernel, G_MINDOUBLE, 0.0, 1.0/n);

    /* Catch more simpler cases. */
    if (!nhighest && !nlowest) {
        /* Preserve units updated by ext_convolve(). */
        gwy_data_field_copy_units(data_field, redkernel);
        gwy_data_field_area_ext_convolve(data_field, col, row, width, height, data_field, redkernel,
                                         GWY_EXTERIOR_BORDER_EXTEND, 0.0, FALSE);
        gwy_data_field_copy_units(redkernel, data_field);
        g_object_unref(redkernel);
        return TRUE;
    }

    if (n <= 40) {
        /* The filter should be fast for tiny kernels. */
        area_tmean_filter_kernel_direct(data_field, col, row, width, height, redkernel, nlowest, nhighest, n);
        g_object_unref(redkernel);
        return TRUE;
    }

    /* Extfield area dimensions. Must match crop_extend_field_for_kernel()! */
    extw = width + redkernel->xres-1;
    /* Analyse kernel boundaries. */
    filter_kernel_boundaries_create(redkernel, &kbound, extw);

    ok = TRUE;
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
        shared(data_field,col,row,width,height,redkernel,kbound,n,nlowest,nhighest,ok,set_fraction)
#endif
    {
        gint ifrom = row + gwy_omp_chunk_start(height);
        gint ito = row + gwy_omp_chunk_end(height);
        GwySetFractionFunc tset_fraction = NULL;

#ifdef _OPENMP
#pragma omp master
        tset_fraction = set_fraction;
#endif

        area_tmean_filter_kernel_radixtree(data_field, col, ifrom, width, ito - ifrom,
                                           redkernel, &kbound, nlowest, nhighest, tset_fraction, &ok);
    }

    filter_kernel_boundaries_free(&kbound);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
