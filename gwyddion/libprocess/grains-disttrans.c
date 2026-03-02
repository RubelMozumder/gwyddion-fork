/*
 *  $Id: grains-disttrans.c 25652 2023-09-12 13:37:48Z yeti-dn $
 *  Copyright (C) 2003-2017 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymath.h>
#include <libprocess/arithmetic.h>
#include <libprocess/grains.h>
#include "gwyprocessinternal.h"

enum {
    SEDINF = 0x7fffffffu,
    QUEUED = 0x80000000u,
};

typedef struct {
    gdouble distance;
    guint i;
    guint j;
} DistantPoint;

typedef struct {
    gdouble dist, ndist;
    gint j, i;
} ThinCandidate;

typedef gboolean (*ErodeFunc)(guint *grain,
                              gint width, gint height,
                              guint id,
                              const PixelQueue *inqueue,
                              PixelQueue *outqueue);


/* Euclidean distance transform */

static inline void
pixel_queue_add(PixelQueue *queue,
                gint i, gint j)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size = MAX(2*queue->size, 16);
        queue->points = g_renew(GridPoint, queue->points, queue->size);
    }

    queue->points[queue->len].i = i;
    queue->points[queue->len].j = j;
    queue->len++;
}

// Set squared distance for all points that have an 8-neighbour outside and add them to the queue.
static void
distance_transform_first_step(guint *distances,
                              guint xres, guint yres,
                              IntList *queue,
                              gboolean from_border)
{
    guint k = 0, j, i;

    queue->len = 0;
    for (i = 0; i < yres; i++) {
        gboolean first_row = (i == 0);
        gboolean last_row = (i == yres-1);

        for (j = 0; j < xres; j++, k++) {
            gboolean first_column = (j == 0);
            gboolean last_column = (j == xres-1);

            if (!distances[k])
                continue;

            if ((from_border && (first_row || first_column || last_row || last_column))
                || (!first_row && !distances[k-xres])
                || (!first_column && !distances[k-1])
                || (!last_column && !distances[k+1])
                || (!last_row && !distances[k+xres])) {
                distances[k] = 1;
                int_list_add(queue, k);
            }
            else if ((!first_row && !first_column && !distances[k-xres-1])
                     || (!first_row && !last_column && !distances[k-xres+1])
                     || (!last_row && !first_column && !distances[k+xres-1])
                     || (!last_row && !last_column && !distances[k+xres+1])) {
                distances[k] = 2;
                int_list_add(queue, k);
            }
        }
    }
}

static void
distance_transform_erode_sed(guint *distances, const guint *olddist,
                             guint xres, guint yres,
                             guint l,
                             const IntList *inqueue,
                             IntList *outqueue)
{
    guint hvsed2 = 2*l - 1, diag2 = 2*hvsed2;
    guint q;

    outqueue->len = 0;

    for (q = 0; q < inqueue->len; q++) {
        guint k = inqueue->data[q], kk = k-xres-1;
        guint i = k/xres, j = k % xres;
        gboolean first_row = (i == 0);
        gboolean last_row = (i == yres-1);
        gboolean first_column = (j == 0);
        gboolean last_column = (j == xres-1);
        guint d2hv = olddist[k] + hvsed2, d2d = olddist[k] + diag2;

        if (!first_row && !first_column && (distances[kk] & ~QUEUED) > d2d) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2d;
        }
        kk++;
        if (!first_row && (distances[kk] & ~QUEUED) > d2hv) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2hv;
        }
        kk++;
        if (!first_row && !last_column && (distances[kk] & ~QUEUED) > d2d) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2d;
        }
        kk += xres-2;
        if (!first_column && (distances[kk] & ~QUEUED) > d2hv) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2hv;
        }
        kk += 2;
        if (!last_column && (distances[kk] & ~QUEUED) > d2hv) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2hv;
        }
        kk += xres-2;
        if (!last_row && !first_column && (distances[kk] & ~QUEUED) > d2d) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2d;
        }
        kk++;
        if (!last_row && (distances[kk] & ~QUEUED) > d2hv) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2hv;
        }
        kk++;
        if (!last_row && !last_column && (distances[kk] & ~QUEUED) > d2d) {
            if (!(distances[kk] & QUEUED))
                int_list_add(outqueue, kk);
            distances[kk] = QUEUED | d2d;
        }
    }
}

/**
 * _gwy_distance_transform_raw:
 * @distances: Array @xres*@yres with nonzeros within shapes.  If all non-zero values are %SEDINF you can pass %TRUE
 *             for @infinitised.
 * @workspace: Workspace of identical dimensions as @distances.
 * @xres: Width.
 * @yres: Height.
 * @inqueue: Pre-allocated queue used by the algorithm.
 * @outqueue: Second pre-allocated queue used by the algorithm.
 * @from_border: %TRUE to consider image edges to be grain boundaries.
 *
 * Performs distance transformation.
 *
 * When it finishes non-zero values in @distances are squared Euclidean distances from outside pixels (including
 * outside the field).
 *
 * Workspace objects @workspace, @inqueue and @outqueue do not carry any information.  They are allocated by the
 * caller to enable an efficient repeated use.
 **/
static void
distance_transform_raw(guint *distances, guint *workspace,
                       guint xres, guint yres,
                       IntList *inqueue, IntList *outqueue,
                       gboolean from_border)
{
    guint l, q;

    distance_transform_first_step(distances, xres, yres, inqueue, from_border);

    for (l = 2; inqueue->len; l++) {
        gint *qdata;

        for (q = 0; q < inqueue->len; q++) {
            guint k = inqueue->data[q];
            workspace[k] = distances[k];
        }
        distance_transform_erode_sed(distances, workspace, xres, yres, l, inqueue, outqueue);

        qdata = outqueue->data;
        for (q = outqueue->len; q; q--, qdata++)
            distances[*qdata] &= ~QUEUED;

        GWY_SWAP(IntList*, inqueue, outqueue);
    }
}

static void
gwy_data_field_grain_distance_transform_internal(GwyDataField *data_field,
                                                 gboolean from_border)
{
    IntList *inqueue, *outqueue;
    guint xres, yres, k, inisize;
    guint *distances, *workspace;
    gdouble *d;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    xres = data_field->xres;
    yres = data_field->yres;
    d = data_field->data;
    distances = g_new(guint, xres*yres);
    workspace = g_new(guint, xres*yres);

    for (k = 0; k < xres*yres; k++)
        distances[k] = (d[k] > 0.0) ? SEDINF : 0;

    inisize = (guint)(8*sqrt(xres*yres) + 16);
    inqueue = int_list_new(inisize);
    outqueue = int_list_new(inisize);

    distance_transform_raw(distances, workspace, xres, yres, inqueue, outqueue, from_border);

    int_list_free(inqueue);
    int_list_free(outqueue);

    for (k = 0; k < xres*yres; k++)
        d[k] = sqrt(distances[k]);
    gwy_data_field_invalidate(data_field);

    g_free(workspace);
    g_free(distances);
}

/**
 * gwy_data_field_grain_distance_transform:
 * @data_field: A data field with zeros in empty space and nonzeros in grains.
 *
 * Performs Euclidean distance transform of a data field with grains.
 *
 * Each non-zero value will be replaced with Euclidean distance to the grain boundary, measured in pixels.
 *
 * See also gwy_data_field_grain_simple_dist_trans() for simple distance transforms such as city-block or chessboard.
 *
 * Since: 2.36
 **/
void
gwy_data_field_grain_distance_transform(GwyDataField *data_field)
{
    gwy_data_field_grain_distance_transform_internal(data_field, TRUE);
}

/* Init @queue with all Von Neumann-neighbourhood boundary pixels. */
static void
init_erosion_4(guint *grain,
               guint width, guint height,
               gboolean from_border,
               PixelQueue *queue)
{
    guint ifrom = from_border ? 0 : 1;
    guint iend = from_border ? height : height-1;
    guint jfrom = from_border ? 0 : 1;
    guint jend = from_border ? width : width-1;
    guint i, j, k;

    queue->len = 0;
    for (i = ifrom; i < iend; i++) {
        k = i*width + jfrom;
        for (j = jfrom; j < jend; j++, k++) {
            if (!grain[k])
                continue;

            if (!i || !j || j == width-1 || i == height-1
                || !grain[k - width] || !grain[k - 1] || !grain[k + 1] || !grain[k + width]) {
                grain[k] = 1;
                pixel_queue_add(queue, i, j);
            }
        }
    }
}

/* Init @queue with all Von Neumann-neighbourhood boundary pixels. */
static void
init_erosion_8(guint *grain,
               gint width, gint height,
               gboolean from_border,
               PixelQueue *queue)
{
    guint ifrom = from_border ? 0 : 1;
    guint iend = from_border ? height : height-1;
    guint jfrom = from_border ? 0 : 1;
    guint jend = from_border ? width : width-1;
    guint i, j, k;

    queue->len = 0;
    for (i = ifrom; i < iend; i++) {
        k = i*width + jfrom;
        for (j = jfrom; j < jend; j++, k++) {
            if (!grain[k])
                continue;

            if (!i || !j || j == width-1 || i == height-1
                || !grain[k - width - 1] || !grain[k - width] || !grain[k - width + 1]
                || !grain[k - 1] || !grain[k + 1]
                || !grain[k + width - 1] || !grain[k + width] || !grain[k + width + 1]) {
                grain[k] = 1;
                pixel_queue_add(queue, i, j);
            }
        }
    }
}

static gboolean
erode_4(guint *grain,
        gint width, gint height,
        guint id,
        const PixelQueue *inqueue,
        PixelQueue *outqueue)
{
    const GridPoint *ipt = inqueue->points;
    guint m;

    outqueue->len = 0;
    for (m = inqueue->len; m; m--, ipt++) {
        gint i = ipt->i, j = ipt->j, k = i*width + j;

        if (i && grain[k - width] == G_MAXUINT) {
            grain[k - width] = id+1;
            pixel_queue_add(outqueue, i-1, j);
        }
        if (j && grain[k - 1] == G_MAXUINT) {
            grain[k - 1] = id+1;
            pixel_queue_add(outqueue, i, j-1);
        }
        if (j < width-1 && grain[k + 1] == G_MAXUINT) {
            grain[k + 1] = id+1;
            pixel_queue_add(outqueue, i, j+1);
        }
        if (i < height-1 && grain[k + width] == G_MAXUINT) {
            grain[k + width] = id+1;
            pixel_queue_add(outqueue, i+1, j);
        }
    }

    return outqueue->len;
}

static gboolean
erode_8(guint *grain,
        gint width, gint height,
        guint id,
        const PixelQueue *inqueue,
        PixelQueue *outqueue)
{
    const GridPoint *ipt = inqueue->points;
    guint m;

    outqueue->len = 0;
    for (m = inqueue->len; m; m--, ipt++) {
        gint i = ipt->i, j = ipt->j, k = i*width + j;
        if (i && j && grain[k - width - 1] == G_MAXUINT) {
            grain[k - width - 1] = id+1;
            pixel_queue_add(outqueue, i-1, j-1);
        }
        if (i && grain[k - width] == G_MAXUINT) {
            grain[k - width] = id+1;
            pixel_queue_add(outqueue, i-1, j);
        }
        if (i && j < width-1 && grain[k - width + 1] == G_MAXUINT) {
            grain[k - width + 1] = id+1;
            pixel_queue_add(outqueue, i-1, j+1);
        }
        if (j && grain[k - 1] == G_MAXUINT) {
            grain[k - 1] = id+1;
            pixel_queue_add(outqueue, i, j-1);
        }
        if (j < width-1 && grain[k + 1] == G_MAXUINT) {
            grain[k + 1] = id+1;
            pixel_queue_add(outqueue, i, j+1);
        }
        if (i < height-1 && j && grain[k + width - 1] == G_MAXUINT) {
            grain[k + width - 1] = id+1;
            pixel_queue_add(outqueue, i+1, j-1);
        }
        if (i < height-1 && grain[k + width] == G_MAXUINT) {
            grain[k + width] = id+1;
            pixel_queue_add(outqueue, i+1, j);
        }
        if (i < height-1 && j < width-1 && grain[k + width + 1] == G_MAXUINT) {
            grain[k + width + 1] = id+1;
            pixel_queue_add(outqueue, i+1, j+1);
        }
    }

    return outqueue->len;
}

/* Perform a cityblock, chessboard or octagonal distance transform of given type using provided queues. */
guint
_gwy_simple_dist_trans(gint *grain, guint width, guint height,
                       gboolean from_border, GwyDistanceTransformType dtype,
                       PixelQueue *inqueue, PixelQueue *outqueue)
{
    ErodeFunc erode = NULL;
    guint dist = 1;

    inqueue->len = outqueue->len = 0;

    if (dtype == GWY_DISTANCE_TRANSFORM_CONN4 || dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL48)
        init_erosion_4(grain, width, height, from_border, inqueue);
    else if (dtype == GWY_DISTANCE_TRANSFORM_CONN8 || dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL84)
        init_erosion_8(grain, width, height, from_border, inqueue);

    if (dtype == GWY_DISTANCE_TRANSFORM_CONN4 || dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL84)
        erode = erode_4;
    else if (dtype == GWY_DISTANCE_TRANSFORM_CONN8 || dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL48)
        erode = erode_8;

    g_return_val_if_fail(erode, 0);

    while (TRUE) {
        if (!erode(grain, width, height, dist, inqueue, outqueue))
            break;
        GWY_SWAP(PixelQueue*, inqueue, outqueue);
        dist++;

        if (dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL48 || dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL84)
            erode = (erode == erode_4) ? erode_8 : erode_4;
    }

    if (!from_border) {
        guint k;

        /* Fix single-pixel grains touching the image borders. */
        for (k = 0; k < width; k++) {
            if (grain[k] == G_MAXUINT)
                grain[k] = 1;
            if (grain[width*(height-1) + k] == G_MAXUINT)
                grain[width*(height-1) + k] = 1;
        }

        for (k = 0; k < height; k++) {
            if (grain[k*width] == G_MAXUINT)
                grain[k*width] = 1;
            if (grain[k*width + width-1] == G_MAXUINT)
                grain[k*width + width-1] = 1;
        }
    }

    return dist;
}

static void
average_octagonal_dt(GwyDataField *dfield, gboolean from_border)
{
    GwyDataField *tmp;

    tmp = gwy_data_field_duplicate(dfield);
    gwy_data_field_grain_simple_dist_trans(dfield, GWY_DISTANCE_TRANSFORM_OCTAGONAL48, from_border);
    gwy_data_field_grain_simple_dist_trans(tmp, GWY_DISTANCE_TRANSFORM_OCTAGONAL84, from_border);
    gwy_data_field_linear_combination(dfield, 0.5, dfield, 0.5, tmp, 0.0);
    g_object_unref(tmp);
}

/**
 * gwy_data_field_grain_simple_dist_trans:
 * @data_field: A data field with zeros in empty space and nonzeros in grains.
 * @dtype: Type of simple distance to use.
 * @from_border: %TRUE to consider image edges to be grain boundaries.
 *
 * Performs a distance transform of a data field with grains.
 *
 * Each non-zero value will be replaced with a distance to the grain boundary, measured in pixels.
 *
 * Note this function can calculate the true Euclidean distance transform only since 2.43.  Use
 * gwy_data_field_grain_distance_transform() for the EDT if you need compatibility with older versions.
 *
 * Since: 2.41
 **/
void
gwy_data_field_grain_simple_dist_trans(GwyDataField *data_field,
                                       GwyDistanceTransformType dtype,
                                       gboolean from_border)
{
    guint *grains = NULL;
    PixelQueue *inqueue, *outqueue;
    gdouble *d;
    guint xres, yres, k;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));

    if (dtype == GWY_DISTANCE_TRANSFORM_EUCLIDEAN) {
        gwy_data_field_grain_distance_transform_internal(data_field, from_border);
        return;
    }
    if (dtype == GWY_DISTANCE_TRANSFORM_OCTAGONAL) {
        average_octagonal_dt(data_field, from_border);
        return;
    }

    g_return_if_fail(dtype <= GWY_DISTANCE_TRANSFORM_OCTAGONAL84);

    xres = data_field->xres;
    yres = data_field->yres;
    d = data_field->data;
    inqueue = g_slice_new0(PixelQueue);
    outqueue = g_slice_new0(PixelQueue);
    grains = g_new(guint, xres*yres);
    for (k = 0; k < xres*yres; k++)
        grains[k] = (d[k] > 0.0) ? G_MAXUINT : 0;

    _gwy_simple_dist_trans(grains, xres, yres, from_border, dtype, inqueue, outqueue);

    for (k = 0; k < xres*yres; k++)
        d[k] = grains[k];
    gwy_data_field_invalidate(data_field);

    g_free(grains);
    g_free(inqueue->points);
    g_free(outqueue->points);
    g_slice_free(PixelQueue, inqueue);
    g_slice_free(PixelQueue, outqueue);
}

/**
 * gwy_data_field_grains_shrink:
 * @data_field: A data field with zeros in empty space and nonzeros in grains.
 * @amount: How much the grains should be reduced, in pixels.  It is inclusive, i.e. pixels that are @amount far from
 *          the border will be removed.
 * @dtype: Type of simple distance to use.
 * @from_border: %TRUE to consider image edges to be grain boundaries. %FALSE to reduce grains touching field
 *               boundaries only along the boundaries.
 *
 * Erodes a data field containing mask by specified amount using a distance measure.
 *
 * Non-zero pixels in @data_field will be replaced with zeros if they are not farther than @amount from the grain
 * boundary as defined by @dtype.
 *
 * Since: 2.43
 **/
void
gwy_data_field_grains_shrink(GwyDataField *data_field,
                             gdouble amount,
                             GwyDistanceTransformType dtype,
                             gboolean from_border)
{
    GwyDataField *edt;
    guint xres, yres, k;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(dtype <= GWY_DISTANCE_TRANSFORM_EUCLIDEAN);

    if (amount < 0.5)
        return;

    xres = data_field->xres;
    yres = data_field->yres;
    edt = gwy_data_field_duplicate(data_field);
    gwy_data_field_grain_simple_dist_trans(edt, dtype, from_border);
    for (k = 0; k < xres*yres; k++) {
        if (edt->data[k] <= amount + 1e-9)
            data_field->data[k] = 0.0;
    }

    g_object_unref(edt);
    gwy_data_field_invalidate(data_field);
}

static gint
compare_distant_points(gconstpointer pa, gconstpointer pb)
{
    const DistantPoint *a = (const DistantPoint*)pa;
    const DistantPoint *b = (const DistantPoint*)pb;

    if (a->distance < b->distance)
        return -1;
    if (a->distance > b->distance)
        return 1;
    if (a->i < b->i)
        return -1;
    if (a->i > b->i)
        return 1;
    if (a->j < b->j)
        return -1;
    if (a->j > b->j)
        return 1;
    return 0;
}

static void
grow_without_merging(GwyDataField *dfield, GwyDataField *edt, gdouble amount)
{
    GArray *array;
    guint xres, yres, i, j, k, m;
    gdouble *d, *e;
    gint *grains;

    xres = dfield->xres;
    yres = dfield->yres;
    d = dfield->data;
    e = edt->data;
    grains = g_new0(gint, xres*yres);
    if (!gwy_data_field_number_grains(dfield, grains)) {
        g_free(grains);
        return;
    }

    array = g_array_sized_new(FALSE, FALSE, sizeof(DistantPoint), 1000);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gdouble eij = e[i*xres + j];
            if (eij > 0.0 && eij <= amount) {
                DistantPoint dp = { eij, i, j };
                g_array_append_val(array, dp);
            }
        }
    }
    g_array_sort(array, compare_distant_points);

    for (m = 0; m < array->len; m++) {
        const DistantPoint *dp = &g_array_index(array, DistantPoint, m);
        gint g1, g2, g3, g4, gno;

        k = dp->i*xres + dp->j;
        g1 = dp->i > 0      ? grains[k-xres] : 0;
        g2 = dp->j > 0      ? grains[k-1]    : 0;
        g3 = dp->j < xres-1 ? grains[k+1]    : 0;
        g4 = dp->i < yres-1 ? grains[k+xres] : 0;
        /* If all are equal or zeros then bitwise or gives us the nonzero value sought. */
        gno = g1 | g2 | g3 | g4;
        if ((!g1 || g1 == gno) && (!g2 || g2 == gno) && (!g3 || g3 == gno) && (!g4 || g4 == gno)) {
            grains[k] = gno;
            d[k] = 1.0;
        }
    }

    g_array_free(array, TRUE);
    g_free(grains);
}

/**
 * gwy_data_field_grains_grow:
 * @data_field: A data field with zeros in empty space and nonzeros in grains.
 * @amount: How much the grains should be expanded, in pixels.  It is inclusive, i.e. exterior pixels that are @amount
 *          far from the border will be filled.
 * @dtype: Type of simple distance to use.
 * @prevent_merging: %TRUE to prevent grain merging, i.e. the growth stops where two grains would merge.  %FALSE to
 *                   simply expand the grains, without regard to grain connectivity.
 *
 * Dilates a data field containing mask by specified amount using a distance measure.
 *
 * Non-positive pixels in @data_field will be replaced with ones if they are not farther than @amount from the grain
 * boundary as defined by @dtype.
 *
 * Since: 2.43
 **/
void
gwy_data_field_grains_grow(GwyDataField *data_field,
                           gdouble amount,
                           GwyDistanceTransformType dtype,
                           gboolean prevent_merging)
{
    GwyDataField *edt;
    guint xres, yres, k;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(dtype <= GWY_DISTANCE_TRANSFORM_EUCLIDEAN);

    if (amount < 0.5)
        return;

    amount += 1e-9;
    xres = data_field->xres;
    yres = data_field->yres;
    edt = gwy_data_field_duplicate(data_field);
    gwy_data_field_grains_invert(edt);
    gwy_data_field_grain_simple_dist_trans(edt, dtype, FALSE);
    if (prevent_merging)
        grow_without_merging(data_field, edt, amount);
    else {
        for (k = 0; k < xres*yres; k++) {
            if (edt->data[k] <= amount)
                data_field->data[k] = 1.0;
        }
    }
    g_object_unref(edt);
    gwy_data_field_invalidate(data_field);
}

static gint
compare_candidate(gconstpointer pa, gconstpointer pb)
{
    const ThinCandidate *a = (const ThinCandidate*)pa;
    const ThinCandidate *b = (const ThinCandidate*)pb;

    /* Take pixels with lowest Euclidean distances first. */
    if (a->dist < b->dist)
        return -1;
    if (a->dist > b->dist)
        return 1;

    /* If equal, take pixels with largest Euclidean distance *of their neighbours* first.  This essentially mean flat
     * edges go before corners, preserving useful branches. */
    if (a->ndist > b->ndist)
        return -1;
    if (a->ndist < b->ndist)
        return 1;

    /* When desperate, sort bottom and right coordinates first so that we try to remove them first.  Anyway we must
     * impose some rule to make the sort stable. */
    if (a->i > b->i)
        return -1;
    if (a->i < b->i)
        return 1;
    if (a->j > b->j)
        return -1;
    if (a->j < b->j)
        return 1;

    return 0;
}

/**
 * gwy_data_field_grains_thin:
 * @data_field: A data field with zeros in empty space and nonzeros in grains.
 *
 * Performs thinning of a data field containing mask.
 *
 * The result of thinning is a ‘skeleton’ mask consisting of single-pixel thin lines.
 *
 * Since: 2.48
 **/
void
gwy_data_field_grains_thin(GwyDataField *mask)
{
    /* TRUE means removing the central pixel in a 3x3 pixel configuration does not break any currently connected
     * parts. */
    static const gboolean ok_to_remove[0x100] = {
        FALSE, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        FALSE, TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,
        FALSE, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
        TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,
    };

    GwyDataField *dfield;
    gint i, j, k, xres, yres, ncand;
    gdouble *d, *m;
    ThinCandidate *candidates;

    g_return_if_fail(GWY_IS_DATA_FIELD(mask));

    xres = mask->xres;
    yres = mask->yres;
    dfield = gwy_data_field_duplicate(mask);
    gwy_data_field_copy(mask, dfield, FALSE);
    gwy_data_field_grain_distance_transform(dfield);
    d = dfield->data;
    m = mask->data;

    ncand = 0;
    for (k = 0; k < xres*yres; k++) {
        if (d[k] > 0.0)
            ncand++;
    }

    if (ncand < 2) {
        /* There are no mask pixels or just a single pixel.  In either case we do not have to do anything. */
        g_object_unref(dfield);
        return;
    }

    candidates = g_new(ThinCandidate, ncand);
    k = 0;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            if (d[i*xres + j] > 0.0) {
                gdouble nd, ndist = 0.0, maxndist = 0.0;
                candidates[k].i = i;
                candidates[k].j = j;
                candidates[k].dist = d[i*xres + j];

                if (i && j) {
                    nd = d[(i-1)*xres + (j-1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (i) {
                    nd = d[(i-1)*xres + j];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (i && j < xres-1) {
                    nd = d[(i-1)*xres + (j+1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (j < xres-1) {
                    nd = d[i*xres + (j+1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (i < yres-1 && j < xres-1) {
                    nd = d[(i+1)*xres + (j+1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (i < yres-1) {
                    nd = d[(i+1)*xres + j];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (i < yres-1 && j) {
                    nd = d[(i+1)*xres + (j-1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }
                if (j) {
                    nd = d[i*xres + (j-1)];
                    ndist += nd;
                    if (nd > maxndist)
                        maxndist = nd;
                }

                /* If the point is farther from the border than any neighbour we never remove it. */
                if (candidates[k].dist < 0.999*maxndist) {
                    candidates[k].ndist = ndist;
                    k++;
                }
            }
        }
    }
    ncand = k;

    if (ncand) {
        qsort(candidates, ncand, sizeof(ThinCandidate), &compare_candidate);

        for (k = 0; k < ncand; k++) {
            guint b = 0;

            i = candidates[k].i;
            j = candidates[k].j;
            if (i && j && d[(i-1)*xres + (j-1)] > 0.0)
                b |= 1;
            if (i && d[(i-1)*xres + j] > 0.0)
                b |= 2;
            if (i && j < xres-1 && d[(i-1)*xres + (j+1)] > 0.0)
                b |= 4;
            if (j < xres-1 && d[i*xres + (j+1)] > 0.0)
                b |= 8;
            if (i < yres-1 && j < xres-1 && d[(i+1)*xres + (j+1)] > 0.0)
                b |= 16;
            if (i < yres-1 && d[(i+1)*xres + j] > 0.0)
                b |= 32;
            if (i < yres-1 && j && d[(i+1)*xres + (j-1)] > 0.0)
                b |= 64;
            if (j && d[i*xres + (j-1)] > 0.0)
                b |= 128;

            if (ok_to_remove[b]) {
                d[i*xres + j] = 0.0;
                m[i*xres + j] = 0.0;
            }
        }
    }

    g_free(candidates);
    g_object_unref(dfield);
    gwy_data_field_invalidate(mask);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
