/*
 *  $Id: grains-values.c 25654 2023-09-12 15:02:12Z yeti-dn $
 *  Copyright (C) 2003-2018 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/linestats.h>
#include <libprocess/arithmetic.h>
#include <libprocess/correct.h>
#include <libprocess/grains.h>
#include "gwyprocessinternal.h"

#define ONE G_GUINT64_CONSTANT(1)

typedef struct {
    gdouble xa;
    gdouble ya;
    gdouble xb;
    gdouble yb;
    gdouble r2;
} Edge;

typedef struct {
    guint size;
    guint len;
    Edge *edges;
} EdgeQueue;

typedef struct {
    gdouble x;
    gdouble y;
    gdouble R2;
    guint size;   /* For candidate sorting. */
} InscribedDisc;

enum { NDIRECTIONS = 12 };

static const gdouble shift_directions[NDIRECTIONS*2] = {
    1.0, 0.0,
    0.9914448613738104, 0.1305261922200516,
    0.9659258262890683, 0.2588190451025207,
    0.9238795325112867, 0.3826834323650898,
    0.8660254037844387, 0.5,
    0.7933533402912352, 0.6087614290087207,
    0.7071067811865476, 0.7071067811865475,
    0.6087614290087207, 0.7933533402912352,
    0.5,                0.8660254037844386,
    0.3826834323650898, 0.9238795325112867,
    0.2588190451025207, 0.9659258262890683,
    0.1305261922200517, 0.9914448613738104,
};

/**
 * gwy_data_field_grains_get_distribution:
 * @data_field: Data field used for marking.  For some quantities its values are not used, but units and physical
 *              dimensions are always taken from it.
 * @grain_field: Data field (mask) of marked grains.  Note if you pass non-%NULL @grains all grain information is
 *               taken from it and @grain_field can be even %NULL then.
 * @distribution: Data line to store grain distribution to.
 * @grains: Grain numbers filled with gwy_data_field_number_grains() if you have it, or %NULL (the function then finds
 *          grain numbers itself which is not efficient for repeated use on the same grain field).
 * @ngrains: The number of grains as returned by gwy_data_field_number_grains().  Ignored in @grains is %NULL.
 * @quantity: The quantity to calculate.
 * @nstats: The number of samples to take on the distribution function.  If nonpositive, a suitable resolution is
 *          determined automatically.
 *
 * Computes distribution of requested grain characteristics.
 *
 * Puts number of grains vs. grain value data into @distribution, units, scales and offsets of @distribution are
 * updated accordingly.
 *
 * Note the @i-th bin is [@i*@dx+@off,(@i+1)*@dx+@off] so the central value you probably want to use for plotting is
 * (@i+0.5)*@dx+@off (where @dx is the @distribution data line pixel size, @off is its offset).
 *
 * Returns: A data line with the distribution: @distribution itself if it was not %NULL, otherwise a newly created
 *          #GwyDataLine caller must destroy.  If there are no grains, %NULL is returned and @distribution is not
 *          changed.
 **/
GwyDataLine*
gwy_data_field_grains_get_distribution(GwyDataField *data_field,
                                       GwyDataField *grain_field,
                                       GwyDataLine *distribution,
                                       gint ngrains,
                                       const gint *grains,
                                       GwyGrainQuantity quantity,
                                       gint nstats)
{
    GwyDataLine *values;
    gint *mygrains = NULL;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), FALSE);
    g_return_val_if_fail(grains || GWY_IS_DATA_FIELD(grain_field), FALSE);
    g_return_val_if_fail(!grain_field || (grain_field->xres == data_field->xres
                                          && grain_field->yres == data_field->yres), FALSE);
    g_return_val_if_fail(!distribution || GWY_IS_DATA_LINE(distribution), FALSE);

    /* Calculate raw statistics */
    if (!grains) {
        grains = mygrains = g_new0(gint, grain_field->xres*grain_field->yres);
        ngrains = gwy_data_field_number_grains(grain_field, mygrains);
    }
    if (!ngrains) {
        g_free(mygrains);
        return NULL;
    }

    values = gwy_data_line_new(ngrains + 1, 1.0, FALSE);
    gwy_data_field_grains_get_values(data_field, values->data, ngrains, grains, quantity);
    g_free(mygrains);

    values->res--;
    values->data[0] = values->data[values->res];

    if (!distribution)
        distribution = gwy_data_line_new(1, 1.0, FALSE);

    gwy_data_line_distribution(values, distribution, 0.0, 0.0, FALSE, nstats);

    g_object_unref(values);

    return distribution;
}

/* See stats.c for description, this function calculates twice `contribution of one corner' (the twice is to move
 * multiplications from inner loops) */
static inline gdouble
square_area2w_1c(gdouble z1, gdouble z2, gdouble z4, gdouble c,
                 gdouble x, gdouble y)
{
    return sqrt(1.0 + (z1 - z2)*(z1 - z2)/x + (z1 + z2 - c)*(z1 + z2 - c)/y)
            + sqrt(1.0 + (z1 - z4)*(z1 - z4)/y + (z1 + z4 - c)*(z1 + z4 - c)/x);
}

/**
 * find_grain_convex_hull:
 * @xres: The number of columns in @grains.
 * @yres: The number of rows in @grains.
 * @grains: Grain numbers filled with gwy_data_field_number_grains().
 * @pos: Position of the top-left vertex of grain's convex hull.
 * @vertices: Array to fill with vertices.
 *
 * Finds vertices of a grain's convex hull.
 *
 * The grain is identified by @pos which must lie in a grain.
 *
 * The positions are returned as indices to vertex grid.  NB: The size of the grid is (@xres + 1)*(@yres + 1), not
 * @xres*@yres.
 *
 * The method is a bit naive, some atan2() calculations could be easily saved.
 **/
static void
find_grain_convex_hull(gint xres, gint yres,
                       const gint *grains,
                       gint pos,
                       GArray *vertices)
{
    enum { RIGHT = 0, DOWN, LEFT, UP } newdir, dir;
    const GridPoint *cur, *mid, *prev;
    GridPoint v;
    gdouble phi, phim;
    gint initpos, gno;

    g_return_if_fail(grains[pos]);

    g_array_set_size(vertices, 0);
    initpos = pos;
    gno = grains[pos];
    v.i = pos/xres;
    v.j = pos % xres;
    g_array_append_val(vertices, v);
    newdir = RIGHT;

    do {
        dir = newdir;
        if (dir == RIGHT) {
            v.j++;
            if (v.i > 0 && v.j < xres && grains[(v.i-1)*xres + v.j] == gno)
                newdir = UP;
            else if (v.j < xres && grains[v.i*xres + v.j] == gno)
                newdir = RIGHT;
            else
                newdir = DOWN;
        }
        else if (dir == DOWN) {
            v.i++;
            if (v.j < xres && v.i < yres && grains[v.i*xres + v.j] == gno)
                newdir = RIGHT;
            else if (v.i < yres && grains[v.i*xres + v.j-1] == gno)
                newdir = DOWN;
            else
                newdir = LEFT;
        }
        else if (dir == LEFT) {
            v.j--;
            if (v.i < yres && v.j > 0 && grains[v.i*xres + v.j-1] == gno)
                newdir = DOWN;
            else if (v.j > 0 && grains[(v.i-1)*xres + v.j-1] == gno)
                newdir = LEFT;
            else
                newdir = UP;
        }
        else if (dir == UP) {
            v.i--;
            if (v.j > 0 && v.i > 0 && grains[(v.i-1)*xres + v.j-1] == gno)
                newdir = LEFT;
            else if (v.i > 0 && grains[(v.i-1)*xres + v.j] == gno)
                newdir = UP;
            else
                newdir = RIGHT;
        }
        else {
            g_assert_not_reached();
        }

        /* When we turn right, the previous point is a potential vertex, and it can also supersed previous vertices. */
        if (newdir == (dir + 1) % 4) {
            g_array_append_val(vertices, v);
            while (vertices->len > 2) {
                cur = &g_array_index(vertices, GridPoint, vertices->len-1);
                mid = &g_array_index(vertices, GridPoint, vertices->len-2);
                prev = &g_array_index(vertices, GridPoint, vertices->len-3);
                phi = atan2(cur->i - mid->i, cur->j - mid->j);
                phim = atan2(mid->i - prev->i, mid->j - prev->j);
                phi = gwy_canonicalize_angle(phi - phim, TRUE, TRUE);
                /* This should be fairly safe as (a) not real harm is done when we have an occasional extra vertex (b)
                 * the greatest possible angle is π/2 */
                if (phi > 1e-12 && phi < G_PI)
                    break;

                /* Get rid of mid, it is in a locally concave part */
                g_array_index(vertices, GridPoint, vertices->len-2) = *cur;
                g_array_set_size(vertices, vertices->len-1);
            }
        }
    } while (v.i*xres + v.j != initpos);

    /* The last point is duplicated first point */
    g_array_set_size(vertices, vertices->len-1);
}

/**
 * grain_maximum_bound:
 * @vertices: Convex hull vertex list.
 * @qx: Scale (pixel size) in x-direction.
 * @qy: Scale (pixel size) in y-direction.
 * @vx: Location to store vector x component to.
 * @vy: Location to store vector y component to.
 *
 * Given a list of integer convex hull vertices, return the vector between the two most distance vertices.
 *
 * FIXME: This is a blatantly naive O(n^2) algorithm.
 **/
static void
grain_maximum_bound(GArray *vertices,
                    gdouble qx, gdouble qy,
                    gdouble *vx, gdouble *vy)
{
    const GridPoint *a, *x;
    gdouble vm, v, dx, dy;
    guint g1, g2;

    vm = -G_MAXDOUBLE;
    for (g1 = 0; g1 < vertices->len; g1++) {
        a = &g_array_index(vertices, GridPoint, g1);
        for (g2 = g1 + 1; g2 < vertices->len; g2++) {
            x = &g_array_index(vertices, GridPoint, g2);
            dx = qx*(x->j - a->j);
            dy = qy*(x->i - a->i);
            v = dx*dx + dy*dy;
            if (v > vm) {
                vm = v;
                *vx = dx;
                *vy = dy;
            }
        }
    }
}

/**
 * grain_minimum_bound:
 * @vertices: Convex hull vertex list.
 * @qx: Scale (pixel size) in x-direction.
 * @qy: Scale (pixel size) in y-direction.
 * @vx: Location to store vector x component to.
 * @vy: Location to store vector y component to.
 *
 * Given a list of integer convex hull vertices, return the vector corresponding to the minimum linear projection.
 *
 * FIXME: This is a blatantly naive O(n^2) algorithm.
 **/
static void
grain_minimum_bound(GArray *vertices,
                    gdouble qx, gdouble qy,
                    gdouble *vx, gdouble *vy)
{
    const GridPoint *a, *b, *x;
    gdouble vm, vm1, v, s, b2, bx, by, dx, dy, vx1, vy1;
    guint g1, g1p, g2;

    g_return_if_fail(vertices->len >= 3);

    vm = G_MAXDOUBLE;
    for (g1 = 0; g1 < vertices->len; g1++) {
        a = &g_array_index(vertices, GridPoint, g1);
        g1p = (g1 + 1) % vertices->len;
        b = &g_array_index(vertices, GridPoint, g1p);
        bx = qx*(b->j - a->j);
        by = qy*(b->i - a->i);
        b2 = bx*bx + by*by;
        vm1 = vx1 = vy1 = -G_MAXDOUBLE;
        for (g2 = 0; g2 < vertices->len; g2++) {
            x = &g_array_index(vertices, GridPoint, g2);
            dx = qx*(x->j - a->j);
            dy = qy*(x->i - a->i);
            s = (dx*bx + dy*by)/b2;
            dx -= s*bx;
            dy -= s*by;
            v = dx*dx + dy*dy;
            if (v > vm1) {
                vm1 = v;
                vx1 = dx;
                vy1 = dy;
            }
        }
        if (vm1 < vm) {
            vm = vm1;
            *vx = vx1;
            *vy = vy1;
        }
    }
}

static gdouble
grain_convex_hull_area(GArray *vertices, gdouble dx, gdouble dy)
{
    const GridPoint *a = &g_array_index(vertices, GridPoint, 0),
                    *b = &g_array_index(vertices, GridPoint, 1),
                    *c = &g_array_index(vertices, GridPoint, 2);
    gdouble s = 0.0;
    guint i;

    g_return_val_if_fail(vertices->len >= 4, 0.0);

    for (i = 2; i < vertices->len; i++) {
        gdouble bx = b->j - a->j, by = b->i - a->i,
                cx = c->j - a->j, cy = c->i - a->i;
        s += 0.5*(bx*cy - by*cx);
        b = c;
        c++;
    }

    return dx*dy*s;
}

static void
grain_convex_hull_centre(GArray *vertices,
                         gdouble dx, gdouble dy,
                         gdouble *centrex, gdouble *centrey)
{
    const GridPoint *a = &g_array_index(vertices, GridPoint, 0),
                    *b = &g_array_index(vertices, GridPoint, 1),
                    *c = &g_array_index(vertices, GridPoint, 2);
    gdouble s = 0.0, xc = 0.0, yc = 0.0;
    guint i;

    g_return_if_fail(vertices->len >= 4);

    for (i = 2; i < vertices->len; i++) {
        gdouble bx = b->j - a->j, by = b->i - a->i, cx = c->j - a->j, cy = c->i - a->i;
        gdouble s1 = bx*cy - by*cx;
        xc += s1*(a->j + b->j + c->j);
        yc += s1*(a->i + b->i + c->i);
        s += s1;
        b = c;
        c++;
    }
    *centrex = xc*dx/(3.0*s);
    *centrey = yc*dy/(3.0*s);
}

static gdouble
minimize_circle_radius(InscribedDisc *circle, GArray *vertices,
                       gdouble dx, gdouble dy)
{
    const GridPoint *v = (const GridPoint*)vertices->data;
    gdouble x = circle->x, y = circle->y, r2best = 0.0;
    guint n = vertices->len;

    while (n--) {
        gdouble deltax = dx*v->j - x, deltay = dy*v->i - y;
        gdouble r2 = deltax*deltax + deltay*deltay;

        if (r2 > r2best)
            r2best = r2;

        v++;
    }

    return r2best;
}

static void
improve_circumscribed_circle(InscribedDisc *circle, GArray *vertices,
                             gdouble dx, gdouble dy)
{
    gdouble eps = 1.0, improvement, qgeom = sqrt(dx*dy);

    do {
        InscribedDisc best = *circle;
        guint i;

        improvement = 0.0;
        for (i = 0; i < NDIRECTIONS; i++) {
            InscribedDisc cand;
            gdouble sx = eps*qgeom*shift_directions[2*i], sy = eps*qgeom*shift_directions[2*i + 1];

            cand.size = circle->size;

            cand.x = circle->x + sx;
            cand.y = circle->y + sy;
            if ((cand.R2 = minimize_circle_radius(&cand, vertices, dx, dy)) < best.R2)
                best = cand;

            cand.x = circle->x - sy;
            cand.y = circle->y + sx;
            if ((cand.R2 = minimize_circle_radius(&cand, vertices, dx, dy)) < best.R2)
                best = cand;

            cand.x = circle->x - sx;
            cand.y = circle->y - sy;
            if ((cand.R2 = minimize_circle_radius(&cand, vertices, dx, dy)) < best.R2)
                best = cand;

            cand.x = circle->x + sy;
            cand.y = circle->y - sx;
            if ((cand.R2 = minimize_circle_radius(&cand, vertices, dx, dy)) < best.R2)
                best = cand;
        }
        if (best.R2 < circle->R2) {
            improvement = (best.R2 - circle->R2)/(dx*dy);
            *circle = best;
        }
        else {
            eps *= 0.5;
        }
    } while (eps > 1e-3 || improvement > 1e-3);
}

static guint*
grain_maybe_realloc(guint *grain, guint w, guint h, guint *grainsize)
{
    if (w*h > *grainsize) {
        g_free(grain);
        *grainsize = w*h;
        grain = g_new(guint, *grainsize);
    }
    return grain;
}

static guint*
extract_upsampled_square_pixel_grain(const guint *grains, guint xres, guint gno,
                                     const gint *bbox,
                                     guint *grain, guint *grainsize,
                                     guint *widthup, guint *heightup,
                                     gdouble dx, gdouble dy)
{
    gint col = bbox[0], row = bbox[1], w = bbox[2], h = bbox[3];
    guint w2 = 2*w, h2 = 2*h;
    guint i, j;

    /* Do not bother with nearly square pixels and upsample also 2×2. */
    if (fabs(log(dy/dx)) < 0.05) {
        grain = grain_maybe_realloc(grain, w2, h2, grainsize);
        for (i = 0; i < h; i++) {
            guint k2 = w2*(2*i);
            guint k = (i + row)*xres + col;
            for (j = 0; j < w; j++, k++, k2 += 2) {
                guint v = (grains[k] == gno) ? G_MAXUINT : 0;
                grain[k2] = v;
                grain[k2+1] = v;
                grain[k2 + w2] = v;
                grain[k2 + w2+1] = v;
            }
        }
    }
    else if (dy < dx) {
        /* Horizontal upsampling, precalculate index map to use in each row. */
        guint *indices;
        w2 = GWY_ROUND(dx/dy*w2);
        grain = grain_maybe_realloc(grain, w2, h2, grainsize);
        indices = (guint*)g_slice_alloc(w2*sizeof(guint));
        for (j = 0; j < w2; j++) {
            gint jj = (gint)floor(0.5*j*dy/dx);
            indices[j] = CLAMP(jj, 0, (gint)w-1);
        }
        for (i = 0; i < h; i++) {
            guint k = (i + row)*xres + col;
            guint k2 = w2*(2*i);
            for (j = 0; j < w2; j++) {
                guint v = (grains[k + indices[j]] == gno) ? G_MAXUINT : 0;
                grain[k2 + j] = v;
                grain[k2 + w2 + j] = v;
            }
        }
        g_slice_free1(w2*sizeof(guint), indices);
    }
    else {
        /* Vertical upsampling, rows are 2× scaled copies but uneven. */
        h2 = GWY_ROUND(dy/dx*h2);
        grain = grain_maybe_realloc(grain, w2, h2, grainsize);
        for (i = 0; i < h2; i++) {
            guint k, k2 = i*w2;
            gint ii = (gint)floor(0.5*i*dx/dy);
            ii = CLAMP(ii, 0, (gint)h-1);
            k = (ii + row)*xres + col;
            for (j = 0; j < w; j++) {
                guint v = (grains[k + j] == gno) ? G_MAXUINT : 0;
                grain[k2 + 2*j] = v;
                grain[k2 + 2*j + 1] = v;
            }
        }
    }

    *widthup = w2;
    *heightup = h2;
    return grain;
}

static gint
compare_candidates(gconstpointer a,
                   gconstpointer b)
{
    const InscribedDisc *da = (const InscribedDisc*)a;
    const InscribedDisc *db = (const InscribedDisc*)b;

    if (da->size > db->size)
        return -1;
    if (da->size < db->size)
        return 1;

    if (da->R2 < db->R2)
        return -1;
    if (da->R2 > db->R2)
        return 1;

    return 0;
}

static void
find_disc_centre_candidates(GArray *candidates,
                            PixelQueue *inqueue,
                            const guint *grain,
                            guint width, guint height,
                            gdouble dx, gdouble dy,
                            gdouble centrex, gdouble centrey)
{
    guint m;

    g_array_set_size(candidates, 0);
    for (m = 0; m < inqueue->len; m++) {
        GridPoint *mpt = inqueue->points + m;
        guint i = mpt->i, j = mpt->j, k = i*width + j, size = 8*grain[k], w;
        InscribedDisc cand;

        if (i && j && (w = grain[k - width-1]) != G_MAXUINT)
            size += w;
        if (i && (w = grain[k - width]) != G_MAXUINT)
            size += 2*w;
        if (i && j < width-1 && (w = grain[k - width+1]) != G_MAXUINT)
            size += w;
        if (j && (w = grain[k-1]) != G_MAXUINT)
            size += 2*w;
        if (j < width-1 && (w = grain[k+1]) != G_MAXUINT)
            size += 2*w;
        if (i < height-1 && j && (w = grain[k + width-1]) != G_MAXUINT)
            size += w;
        if (i < height-1 && (w = grain[k + width]) != G_MAXUINT)
            size += 2*w;
        if (i < height-1 && j < width-1 && (w = grain[k + width+1]) != G_MAXUINT)
            size += w;

        cand.x = (mpt->j + 0.5)*dx;
        cand.y = (mpt->i + 0.5)*dy;
        cand.size = size;
        /* Use R2 temporarily for distance from the entire grain centre; this is only for sorting below. */
        cand.R2 = ((cand.x - centrex)*(cand.x - centrex) + (cand.y - centrey)*(cand.y - centrey));
        g_array_append_val(candidates, cand);
    }
    g_array_sort(candidates, &compare_candidates);
}

static inline void
edge_list_add(EdgeQueue *queue,
              gdouble xa, gdouble ya,
              gdouble xb, gdouble yb)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size = MAX(2*queue->size, 16);
        queue->edges = g_renew(Edge, queue->edges, queue->size);
    }

    queue->edges[queue->len].xa = xa;
    queue->edges[queue->len].ya = ya;
    queue->edges[queue->len].xb = xb;
    queue->edges[queue->len].yb = yb;
    queue->len++;
}

static void
find_all_edges(EdgeQueue *edges,
               const gint *grains, guint xres,
               guint gno, const gint *bbox,
               gdouble dx, gdouble dy)
{
    guint col = bbox[0], row = bbox[1], w = bbox[2], h = bbox[3];
    guint i, j;
    guint *vertices;

    edges->len = 0;

    vertices = g_slice_alloc((w + 1)*sizeof(guint));
    for (j = 0; j <= w; j++)
        vertices[j] = G_MAXUINT;

    for (i = 0; i <= h; i++) {
        guint k = (i + row)*xres + col;
        guint vertex = G_MAXUINT;

        for (j = 0; j <= w; j++, k++) {
            /*
             * 1 2
             * 3 4
             */
            guint g0 = i && j && grains[k - xres - 1] == gno;
            guint g1 = i && j < w && grains[k - xres] == gno;
            guint g2 = i < h && j && grains[k - 1] == gno;
            guint g3 = i < h && j < w && grains[k] == gno;
            guint g = g0 | (g1 << 1) | (g2 << 2) | (g3 << 3);

            if (g == 8 || g == 7) {
                vertex = j;
                vertices[j] = i;
            }
            else if (g == 2 || g == 13) {
                edge_list_add(edges, dx*j, dy*vertices[j], dx*j, dy*i);
                vertex = j;
                vertices[j] = G_MAXUINT;
            }
            else if (g == 4 || g == 11) {
                edge_list_add(edges, dx*vertex, dy*i, dx*j, dy*i);
                vertex = G_MAXUINT;
                vertices[j] = i;
            }
            else if (g == 1 || g == 14) {
                edge_list_add(edges, dx*vertex, dy*i, dx*j, dy*i);
                edge_list_add(edges, dx*j, dy*vertices[j], dx*j, dy*i);
                vertex = G_MAXUINT;
                vertices[j] = G_MAXUINT;
            }
            else if (g == 6 || g == 9) {
                edge_list_add(edges, dx*vertex, dy*i, dx*j, dy*i);
                edge_list_add(edges, dx*j, dy*vertices[j], dx*j, dy*i);
                vertex = j;
                vertices[j] = i;
            }
        }
    }

    g_slice_free1((w + 1)*sizeof(guint), vertices);
}

static gdouble
maximize_disc_radius(InscribedDisc *disc, Edge *edges, guint n)
{
    gdouble x = disc->x, y = disc->y, r2best = HUGE_VAL;

    while (n--) {
        gdouble rax = edges->xa - x, ray = edges->ya - y,
                rbx = edges->xb - x, rby = edges->yb - y,
                deltax = edges->xb - edges->xa, deltay = edges->yb - edges->ya;
        gdouble ca = -(deltax*rax + deltay*ray),
                cb = deltax*rbx + deltay*rby;

        if (ca <= 0.0)
            edges->r2 = rax*rax + ray*ray;
        else if (cb <= 0.0)
            edges->r2 = rbx*rbx + rby*rby;
        else {
            gdouble tx = cb*rax + ca*rbx, ty = cb*ray + ca*rby, D = ca + cb;
            edges->r2 = (tx*tx + ty*ty)/(D*D);
        }

        if (edges->r2 < r2best)
            r2best = edges->r2;
        edges++;
    }

    return r2best;
}

static guint
filter_relevant_edges(EdgeQueue *edges, gdouble r2, gdouble eps)
{
    Edge *edge = edges->edges, *enear = edges->edges;
    gdouble limit = sqrt(r2) + 4.0*eps + 0.5;
    guint i;

    limit *= limit;
    for (i = edges->len; i; i--, edge++) {
        if (edge->r2 <= limit) {
            if (edge != enear)
                GWY_SWAP(Edge, *edge, *enear);
            enear++;
        }
    }

    return enear - edges->edges;
}

static void
improve_inscribed_disc(InscribedDisc *disc, EdgeQueue *edges, guint dist)
{
    gdouble eps = 0.5 + 0.25*(dist > 4) + 0.25*(dist > 16), improvement;
    guint nsuccessiveimprovements = 0;

    do {
        InscribedDisc best;
        guint i, nr;

        disc->R2 = maximize_disc_radius(disc, edges->edges, edges->len);
        eps = MIN(eps, 0.5*sqrt(disc->R2));
        best = *disc;
        nr = filter_relevant_edges(edges, best.R2, eps);

        improvement = 0.0;
        for (i = 0; i < NDIRECTIONS; i++) {
            InscribedDisc cand;
            gdouble sx = eps*shift_directions[2*i], sy = eps*shift_directions[2*i + 1];

            cand.size = disc->size;

            cand.x = disc->x + sx;
            cand.y = disc->y + sy;
            if ((cand.R2 = maximize_disc_radius(&cand, edges->edges, nr)) > best.R2)
                best = cand;

            cand.x = disc->x - sy;
            cand.y = disc->y + sx;
            if ((cand.R2 = maximize_disc_radius(&cand, edges->edges, nr)) > best.R2)
                best = cand;

            cand.x = disc->x - sx;
            cand.y = disc->y - sy;
            if ((cand.R2 = maximize_disc_radius(&cand, edges->edges, nr)) > best.R2)
                best = cand;

            cand.x = disc->x + sy;
            cand.y = disc->y - sx;
            if ((cand.R2 = maximize_disc_radius(&cand, edges->edges, nr)) > best.R2)
                best = cand;
        }

        if (best.R2 > disc->R2) {
            improvement = sqrt(best.R2) - sqrt(disc->R2);
            *disc = best;
            /* This scales up *each* successive improvement after 3 so eps can grow very quickly. */
            if (nsuccessiveimprovements++ > 2)
                eps *= 1.5;
        }
        else {
            eps *= 0.5;
            nsuccessiveimprovements = 0;
        }
    } while (eps > 1e-3 || improvement > 1e-3);
}

/**
 * gwy_data_field_grains_get_values:
 * @data_field: Data field used for marking.  For some quantities its values are not used, but its dimensions
 *              determine the dimensions of @grains.
 * @values: Array of size @ngrains+1 to put grain values to.  It can be %NULL to allocate and return a new array.
 * @grains: Grain numbers filled with gwy_data_field_number_grains().
 * @ngrains: The number of grains as returned by gwy_data_field_number_grains().
 * @quantity: The quantity to calculate.
 *
 * Calculates characteristics of grains.
 *
 * This is a bit low-level function, see also gwy_data_field_grains_get_distribution().
 *
 * The array @values will be filled with the requested grain value for each individual grain (0th item of @values
 * which does not correspond to any grain will be overwritten with an arbitrary value and should be ignored).
 *
 * The grain numbers serve as indices in @values.  Therefore as long as the same @grains is used, the same position in
 * @values corresponds to the same particular grain.  This enables one for instance to calculate grain sizes and grain
 * heights and then correlate them.
 *
 * Returns: @values itself if it was not %NULL, otherwise a newly allocated array that caller has to free.
 **/
gdouble*
gwy_data_field_grains_get_values(GwyDataField *data_field,
                                 gdouble *values,
                                 gint ngrains,
                                 const gint *grains,
                                 GwyGrainQuantity quantity)
{
    gdouble *allvalues[1];

    if (!values)
        values = g_new(gdouble, ngrains + 1);

    allvalues[0] = values;
    gwy_data_field_grains_get_quantities(data_field, allvalues, &quantity, 1, ngrains, grains);
    return values;
}

static gdouble*
ensure_buffer(GwyGrainQuantity quantity,
              gdouble **quantity_data,
              guint ngrains,
              gdouble fillvalue,
              GList **buffers)
{
    gdouble *buf, *b;
    guint gno;

    if (quantity_data[quantity]) {
        buf = quantity_data[quantity];
        if (!fillvalue)
            gwy_clear(buf, ngrains + 1);
    }
    else {
        buf = fillvalue ? g_new(gdouble, ngrains + 1) : g_new0(gdouble, ngrains + 1);
        *buffers = g_list_prepend(*buffers, buf);
    }
    if (fillvalue) {
        for (gno = ngrains+1, b = buf; gno; gno--)
            *(b++) = fillvalue;
    }

    return buf;
}

/* Note all coordinates are pixel-wise, not real.  For linear and quadratic, the origin is always the grain centre. */
static void
calculate_grain_aux(GwyDataField *data_field,
                    const gint *grains,
                    guint ngrains,
                    gint *sizes, gint *boundpos,
                    gdouble *min, gdouble *max,
                    gdouble *xvalue, gdouble *yvalue, gdouble *zvalue,
                    gdouble *linear, gdouble *quadratic)
{
    guint xres, yres, i, j, k, n, gno, nn;
    gdouble z;
    const gdouble *d;
    const gint *g;
    gdouble *t;

    xres = data_field->xres;
    yres = data_field->yres;
    nn = xres*yres;

    if (sizes) {
        for (k = nn, g = grains; k; k--, g++) {
            gno = *g;
            sizes[gno]++;
        }
    }
    if (boundpos) {
        for (k = 0, g = grains; k < nn; k++, g++) {
            gno = *g;
            if (boundpos[gno] == -1)
                boundpos[gno] = k;
        }
    }
    if (min) {
        for (k = nn, g = grains, d = data_field->data; k; k--, g++, d++) {
            gno = *g;
            z = *d;
            if (z < min[gno])
                min[gno] = z;
        }
    }
    if (max) {
        for (k = nn, g = grains, d = data_field->data; k; k--, g++, d++) {
            gno = *g;
            z = *d;
            if (z > max[gno])
                max[gno] = z;
        }
    }
    if (zvalue) {
        g_assert(sizes);
        for (k = nn, g = grains, d = data_field->data; k; k--, g++, d++) {
            gno = *g;
            z = *d;
            zvalue[gno] += z;
        }
        for (gno = 0; gno <= ngrains; gno++) {
            n = sizes[gno];
            zvalue[gno] /= n;
        }
    }
    if (xvalue) {
        g_assert(sizes);
        g = grains;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, g++) {
                gno = *g;
                xvalue[gno] += j;
            }
        }
        for (gno = 0; gno <= ngrains; gno++) {
            n = sizes[gno];
            xvalue[gno] /= n;
        }
    }
    if (yvalue) {
        g_assert(sizes);
        g = grains;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, g++) {
                gno = *g;
                yvalue[gno] += i;
            }
        }
        for (gno = 0; gno <= ngrains; gno++) {
            n = sizes[gno];
            yvalue[gno] /= n;
        }
    }
    if (linear) {
        g_assert(xvalue && yvalue);
        g = grains;
        d = data_field->data;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, g++, d++) {
                gdouble x, y;

                gno = *g;
                t = linear + 5*gno;
                x = j - xvalue[gno];
                y = i - yvalue[gno];
                z = *d;
                *(t++) += x*x;
                *(t++) += x*y;
                *(t++) += y*y;
                *(t++) += x*z;
                *t += y*z;
            }
        }
    }
    if (quadratic) {
        g_assert(xvalue && yvalue);
        g = grains;
        d = data_field->data;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, g++, d++) {
                gdouble x, y, xx, yy, xy;

                gno = *g;
                t = quadratic + 12*gno;
                x = j - xvalue[gno];
                y = i - yvalue[gno];
                xx = x*x;
                xy = x*y;
                yy = y*y;
                z = *d;
                *(t++) += xx*x;
                *(t++) += xx*y;
                *(t++) += x*yy;
                *(t++) += y*yy;
                *(t++) += xx*xx;
                *(t++) += xx*xy;
                *(t++) += xx*yy;
                *(t++) += xy*yy;
                *(t++) += yy*yy;
                *(t++) += xx*z;
                *(t++) += xy*z;
                *t += yy*z;
            }
        }
    }
}

static void
integrate_grain_volume0(const gdouble *d, const gint *grains,
                        gint xres, gint yres,
                        gdouble *volume, guint ngrains,
                        gdouble pixelarea)
{
    gint i, j, gno;

    gwy_clear(volume, ngrains + 1);
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            gint ix, ipx, imx, jp, jm;
            gdouble v;

            ix = i*xres;
            if (!(gno = grains[ix + j]))
                continue;

            imx = (i > 0) ? ix-xres : ix;
            ipx = (i < yres-1) ? ix+xres : ix;
            jm = (j > 0) ? j-1 : j;
            jp = (j < xres-1) ? j+1 : j;

            v = (52.0*d[ix + j] + 10.0*(d[imx + j] + d[ix + jm] + d[ix + jp] + d[ipx + j])
                 + (d[imx + jm] + d[imx + jp] + d[ipx + jm] + d[ipx + jp]));

            volume[gno] += v;
        }
    }
    for (gno = 0; gno <= ngrains; gno++)
        volume[gno] *= pixelarea/96.0;
}

/* This returns the signed distance of the median line (in coordinate system rotate to idir direction). */
static gdouble
median_cut_for_direction(guint idir,
                         guint gno, const guint *csizes, const GwyXY *coords,
                         gdouble *rotcoords)
{
    gdouble ca, sa;
    guint k, kfrom, kto;
    guint ranks[2];
    gdouble mm[2];

    if (idir < NDIRECTIONS) {
        ca = shift_directions[2*idir];
        sa = shift_directions[2*idir + 1];
    }
    else {
        sa = -shift_directions[2*(idir - NDIRECTIONS)];
        ca = shift_directions[2*(idir - NDIRECTIONS) + 1];
    }

    kfrom = 4*csizes[gno - 1];
    kto = 4*csizes[gno];
    for (k = kfrom; k < kto; k++)
        rotcoords[k - kfrom] = sa*coords[k].x - ca*coords[k].y;

    /* The length is a multiple of 4, i.e. even.  So we calculate the average of the two median candidates. */
    ranks[0] = (kto - kfrom)/2 - 1;
    ranks[1] = (kto - kfrom)/2;
    gwy_math_kth_ranks(kto - kfrom, rotcoords, 2, ranks, mm);

    return 0.5*(mm[0] + mm[1]);
}

static gdouble
martin_intersection_length(guint idir, gdouble med,
                           gdouble qh, gdouble qv, gdouble qdiag,
                           guint gno, const gint *bbox,
                           const gdouble *xvalue, const gdouble *yvalue,
                           gint xres, const gint *grains)
{
    gint jmin, jmax, imin, imax, i, j, len;
    gdouble ca, sa, xc, yc, deltax, deltay, x, y;

    bbox += 4*gno;
    jmin = bbox[0];
    imin = bbox[1];
    jmax = bbox[0] + bbox[2]-1;
    imax = bbox[1] + bbox[3]-1;

    if (idir < NDIRECTIONS) {
        ca = shift_directions[2*idir];
        sa = shift_directions[2*idir + 1];
    }
    else {
        sa = -shift_directions[2*(idir - NDIRECTIONS)];
        ca = shift_directions[2*(idir - NDIRECTIONS) + 1];
    }

    /* Start from a point on the median line and go along it to either direction until we get outside of the bounding
     * box.  The +0.5 actually belongs to the floor() function but we can do it here.  */
    xc = xvalue[gno] + med*sa/qh + 0.5;
    yc = yvalue[gno] - med*ca/qv + 0.5;
    /* The step in real coordinates is qdiag. */
    deltax = 0.061803398875*qdiag * ca/qh;
    deltay = 0.061803398875*qdiag * sa/qv;
    len = 0;

    x = xc;
    y = yc;
    while (TRUE) {
        j = (gint)floor(x);
        if (j < jmin || j > jmax)
            break;

        i = (gint)floor(y);
        if (i < imin || i > imax)
            break;

        if (grains[i*xres + j] == gno)
            len++;

        x += deltax;
        y += deltay;
    }

    x = xc - deltax;
    y = yc - deltay;
    while (TRUE) {
        j = (gint)floor(x);
        if (j < jmin || j > jmax)
            break;

        i = (gint)floor(y);
        if (i < imin || i > imax)
            break;

        if (grains[i*xres + j] == gno)
            len++;

        x -= deltax;
        y -= deltay;
    }

    return len*0.061803398875*qdiag;
}

static gdouble
refine_diameter_direction(const gdouble *diams, guint k, gboolean is_max)
{
    gint sign = is_max ? 1 : -1;
    gdouble mvals[3];
    gdouble x, phi;

    mvals[0] = sign*diams[(k + 2*NDIRECTIONS-1) % (2*NDIRECTIONS)];
    mvals[1] = sign*diams[k];
    mvals[2] = sign*diams[(k + 1) % (2*NDIRECTIONS)];
    gwy_math_refine_maximum_1d(mvals, &x);
    phi = gwy_canonicalize_angle(G_PI - 0.5*G_PI*(k + x)/NDIRECTIONS, FALSE, FALSE);

    return phi;
}

/**
 * gwy_data_field_grains_get_quantities:
 * @data_field: Data field used for marking.  For some quantities its values are not used, but its dimensions
 *              determine the dimensions of @grains.
 * @values: Array of @nquantities pointers to blocks of length @ngrains+1 to put the calculated grain values to.  Each
 *          block corresponds to one requested quantity.  %NULL can be passed to allocate and return a new array.
 * @quantities: Array of @nquantities items that specify the requested #GwyGrainQuantity to put to corresponding items
 *              in @values. Quantities can repeat.
 * @nquantities: The number of requested different grain values.
 * @grains: Grain numbers filled with gwy_data_field_number_grains().
 * @ngrains: The number of grains as returned by gwy_data_field_number_grains().
 *
 * Calculates multiple characteristics of grains simultaneously.
 *
 * See gwy_data_field_grains_get_values() for some discussion.  This function is more efficient if several grain
 * quantities need to be calculated since gwy_data_field_grains_get_values() can do lot of repeated work in such case.
 *
 * Returns: @values itself if it was not %NULL, otherwise a newly allocated array that caller has to free with
 *          g_free(), including the contained arrays.
 *
 * Since: 2.22
 **/
gdouble**
gwy_data_field_grains_get_quantities(GwyDataField *data_field,
                                     gdouble **values,
                                     const GwyGrainQuantity *quantities,
                                     guint nquantities,
                                     guint ngrains,
                                     const gint *grains)
{
    /* The number of built-in quantities. */
    enum { NQ = 49 };
    enum {
        NEED_SIZES = 1 << 0,
        NEED_BOUNDPOS = 1 << 1,
        NEED_MIN = 1 << 2,
        NEED_MAX = 1 << 3,
        NEED_XVALUE = (1 << 4) | NEED_SIZES,
        NEED_YVALUE = (1 << 5) | NEED_SIZES,
        NEED_CENTRE = NEED_XVALUE | NEED_YVALUE,
        NEED_ZVALUE = (1 << 6) | NEED_SIZES,
        NEED_LINEAR = (1 << 7) | NEED_ZVALUE | NEED_CENTRE,
        NEED_QUADRATIC = (1 << 8) | NEED_LINEAR,
        NEED_BBOX = (1 << 9),
        INVALID = G_MAXUINT
    };
    static const guint need_aux[NQ] = {
        NEED_SIZES,                   /* projected area */
        NEED_SIZES,                   /* equiv square side */
        NEED_SIZES,                   /* equiv disc radius */
        0,                            /* surface area */
        NEED_MAX,                     /* maximum */
        NEED_MIN,                     /* minimum */
        NEED_ZVALUE,                  /* mean */
        NEED_SIZES,                   /* median */
        NEED_SIZES,                   /* pixel area */
        NEED_MIN | NEED_MAX,          /* half-height area */
        0,                            /* flat boundary length */
        NEED_ZVALUE,                  /* rms */
        NEED_BOUNDPOS,                /* min bounding size */
        NEED_BOUNDPOS,                /* min bounding direction */
        NEED_BOUNDPOS,                /* max bounding size */
        NEED_BOUNDPOS,                /* max bounding direction */
        NEED_XVALUE,                  /* centre x */
        NEED_YVALUE,                  /* centre y */
        0,                            /* volume, 0-based */
        NEED_MIN | NEED_SIZES,        /* volume, min-based */
        NEED_BBOX | NEED_SIZES,       /* volume, Laplace-based */
        INVALID,
        INVALID,
        NEED_LINEAR,                  /* slope theta */
        NEED_LINEAR,                  /* slope phi */
        0,                            /* boundary minimum */
        0,                            /* boundary maximum */
        NEED_QUADRATIC,               /* curvature centre x */
        NEED_QUADRATIC,               /* curvature centre y */
        NEED_QUADRATIC,               /* curvature centre z */
        NEED_QUADRATIC,               /* curvature invrad 1 */
        NEED_QUADRATIC,               /* curvature invrad 2 */
        NEED_QUADRATIC,               /* curvature direction 1 */
        NEED_QUADRATIC,               /* curvature direction 2 */
        NEED_CENTRE | NEED_BBOX,      /* inscribed disc radius */
        NEED_CENTRE | NEED_BBOX,      /* inscribed disc centre x */
        NEED_CENTRE | NEED_BBOX,      /* inscribed disc centre y */
        NEED_BOUNDPOS,                /* convex hull area */
        NEED_BOUNDPOS,                /* circumcircle radius */
        NEED_BOUNDPOS,                /* circumcircle centre x */
        NEED_BOUNDPOS,                /* circumcircle centre y */
        NEED_CENTRE,                  /* mean radius */
        NEED_LINEAR,                  /* equiv ellipse major axis */
        NEED_LINEAR,                  /* equiv ellipse minor axis */
        NEED_LINEAR,                  /* equiv ellipse major axis angle */
        NEED_CENTRE | NEED_BBOX,      /* minimum Martin diameter */
        NEED_CENTRE | NEED_BBOX,      /* minimum Martin diameter angle */
        NEED_CENTRE | NEED_BBOX,      /* maximum Martin diameter */
        NEED_CENTRE | NEED_BBOX,      /* maximum Martin diameter angle */
    };

    gdouble *quantity_data[NQ];
    gboolean seen[NQ];
    GList *l, *buffers = NULL;
    guint *sizes = NULL;
    gint *boundpos = NULL, *bbox = NULL;
    gdouble *xvalue = NULL, *yvalue = NULL, *zvalue = NULL, *min = NULL, *max = NULL, *linear = NULL, *quadratic = NULL;
    const gdouble *d;
    gdouble *p;
    gdouble qh, qv, qarea, qdiag, qgeom;
    guint xres, yres, i, j, k, nn, gno;

    g_return_val_if_fail(GWY_IS_DATA_FIELD(data_field), NULL);
    g_return_val_if_fail(grains, NULL);
    if (!nquantities)
        return values;
    g_return_val_if_fail(quantities, NULL);

    if (!values) {
        values = g_new(gdouble*, nquantities);
        for (i = 0; i < nquantities; i++)
            values[i] = g_new0(gdouble, ngrains + 1);
    }
    else {
        for (i = 0; i < nquantities; i++)
            gwy_clear(values[i], ngrains + 1);
    }

    xres = data_field->xres;
    yres = data_field->yres;
    nn = xres*yres;
    gwy_debug("ngrains: %d, nn: %d", ngrains, nn);

    /* Figure out which quantities are requested. */
    gwy_clear(quantity_data, NQ);
    for (i = 0; i < nquantities; i++) {
        GwyGrainQuantity quantity = quantities[i];

        if ((guint)quantity >= NQ || need_aux[quantity] == INVALID) {
            g_warning("Invalid built-in grain quantity number %u.", quantity);
            continue;
        }
        /* Take the first if the same quantity is requested multiple times. We will deal with this later. */
        if (!quantity_data[quantity])
            quantity_data[quantity] = values[i];
    }

    /* Figure out the auxiliary data to calculate.  Do this after we gathered all quantities as some auxiliary data
     * are in fact quantities too. */
    for (i = 0; i < nquantities; i++) {
        GwyGrainQuantity quantity = quantities[i];
        guint need;

        if ((guint)quantity >= NQ || need_aux[quantity] == INVALID)
            continue;

        need = need_aux[quantity];
        /* Integer data */
        if ((need & NEED_SIZES) && !sizes) {
            sizes = g_new0(guint, ngrains + 1);
            buffers = g_list_prepend(buffers, sizes);
        }
        if ((need & NEED_BOUNDPOS) && !boundpos) {
            boundpos = g_new(gint, ngrains + 1);
            buffers = g_list_prepend(buffers, boundpos);
            for (gno = 0; gno <= ngrains; gno++)
                boundpos[gno] = -1;
        }
        if ((need & NEED_BBOX) && !bbox) {
            bbox = gwy_data_field_get_grain_bounding_boxes(data_field, ngrains, grains, NULL);
            buffers = g_list_prepend(buffers, bbox);
        }
        /* Floating point data that coincide with some quantity.  An array is allocated only if the corresponding
         * quantity is not requested. Otherwise we use the supplied array. */
        if (need & NEED_MIN)
            min = ensure_buffer(GWY_GRAIN_VALUE_MINIMUM, quantity_data, ngrains, G_MAXDOUBLE, &buffers);
        if (need & NEED_MAX)
            max = ensure_buffer(GWY_GRAIN_VALUE_MAXIMUM, quantity_data, ngrains, -G_MAXDOUBLE, &buffers);
        if (need & NEED_XVALUE)
            xvalue = ensure_buffer(GWY_GRAIN_VALUE_CENTER_X, quantity_data, ngrains, 0.0, &buffers);
        if (need & NEED_YVALUE)
            yvalue = ensure_buffer(GWY_GRAIN_VALUE_CENTER_Y, quantity_data, ngrains, 0.0, &buffers);
        if (need & NEED_ZVALUE)
            zvalue = ensure_buffer(GWY_GRAIN_VALUE_MEAN, quantity_data, ngrains, 0.0, &buffers);
        /* Complex floating point data */
        if ((need & NEED_LINEAR) && !linear) {
            linear = g_new0(gdouble, 5*(ngrains + 1));
            buffers = g_list_prepend(buffers, linear);
        }
        if ((need & NEED_QUADRATIC) && !quadratic) {
            quadratic = g_new0(gdouble, 12*(ngrains + 1));
            buffers = g_list_prepend(buffers, quadratic);
        }
    }

    /* Calculate auxiliary quantities (in pixel lateral coordinates) */
    calculate_grain_aux(data_field, grains, ngrains, sizes, boundpos,
                        min, max, xvalue, yvalue, zvalue, linear, quadratic);

    d = data_field->data;
    qh = gwy_data_field_get_dx(data_field);
    qv = gwy_data_field_get_dy(data_field);
    qdiag = hypot(qh, qv);
    qarea = qh*qv;
    qgeom = sqrt(qarea);

    /* Calculate specific requested quantities */
    if ((p = quantity_data[GWY_GRAIN_VALUE_PIXEL_AREA])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = sizes[gno];
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_PROJECTED_AREA])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = qarea*sizes[gno];
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_EQUIV_SQUARE_SIDE])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = sqrt(qarea*sizes[gno]);
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_EQUIV_DISC_RADIUS])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = sqrt(qarea/G_PI*sizes[gno]);
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_SURFACE_AREA])) {
        gdouble qh2 = qh*qh, qv2 = qv*qv;

        gwy_clear(p, ngrains + 1);
        /* Every contribution is calculated twice -- for each pixel (vertex) participating to a particular triangle */
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++) {
                gint ix, ipx, imx, jp, jm;
                gdouble c;

                ix = i*xres;
                if (!(gno = grains[ix + j]))
                    continue;

                imx = (i > 0) ? ix-xres : ix;
                ipx = (i < yres-1) ? ix+xres : ix;
                jm = (j > 0) ? j-1 : j;
                jp = (j < xres-1) ? j+1 : j;

                c = (d[ix + j] + d[ix + jm] + d[imx + jm] + d[imx + j])/2.0;
                p[gno] += square_area2w_1c(d[ix + j], d[ix + jm], d[imx + j], c, qh2, qv2);

                c = (d[ix + j] + d[ix + jp] + d[imx + jp] + d[imx + j])/2.0;
                p[gno] += square_area2w_1c(d[ix + j], d[ix + jp], d[imx + j], c, qh2, qv2);

                c = (d[ix + j] + d[ix + jm] + d[ipx + jm] + d[ipx + j])/2.0;
                p[gno] += square_area2w_1c(d[ix + j], d[ix + jm], d[ipx + j], c, qh2, qv2);

                c = (d[ix + j] + d[ix + jp] + d[ipx + jp] + d[ipx + j])/2.0;
                p[gno] += square_area2w_1c(d[ix + j], d[ix + jp], d[ipx + j], c, qh2, qv2);
            }
        }
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] *= qarea/8.0;
    }
    /* GWY_GRAIN_VALUE_MINIMUM is calculated directly. */
    /* GWY_GRAIN_VALUE_MAXIMUM is calculated directly. */
    /* GWY_GRAIN_VALUE_MEAN is calculated directly. */
    if ((p = quantity_data[GWY_GRAIN_VALUE_MEDIAN])) {
        guint *csizes = g_new0(guint, ngrains + 1);
        guint *pos = g_new0(guint, ngrains + 1);
        gdouble *tmp;

        /* Find cumulative sizes (we care only about grains, ignore the outside-grains area) */
        csizes[0] = 0;
        csizes[1] = sizes[1];
        for (gno = 2; gno <= ngrains; gno++)
            csizes[gno] = sizes[gno] + csizes[gno-1];

        tmp = g_new(gdouble, csizes[ngrains]);
        /* Find where each grain starts in tmp sorted by grain # */
        for (gno = 1; gno <= ngrains; gno++)
            pos[gno] = csizes[gno-1];
        /* Sort values by grain # to tmp */
        for (k = 0; k < nn; k++) {
            if ((gno = grains[k])) {
                tmp[pos[gno]] = d[k];
                pos[gno]++;
            }
        }
        /* Find medians of each block */
        for (gno = 1; gno <= ngrains; gno++)
            p[gno] = gwy_math_median(csizes[gno] - csizes[gno-1], tmp + csizes[gno-1]);
        /* Finalize */
        g_free(csizes);
        g_free(pos);
        g_free(tmp);
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_HALF_HEIGHT_AREA])) {
        gdouble *zhalf;
        guint *zhsizes;

        /* Find the grain half-heights, i.e. (z_min + z_max)/2, first */
        zhalf = g_new(gdouble, ngrains + 1);
        for (gno = 0; gno <= ngrains; gno++)
            zhalf[gno] = (min[gno] + max[gno])/2.0;
        /* Calculate the area of pixels above the half-heights */
        zhsizes = g_new0(gint, ngrains + 1);
        for (k = 0; k < nn; k++) {
            gno = grains[k];
            if (d[k] >= zhalf[gno])
                zhsizes[gno]++;
        }
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = qarea*zhsizes[gno];
        /* Finalize */
        g_free(zhalf);
        g_free(zhsizes);
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_RMS])) {
        gwy_clear(p, ngrains + 1);
        for (k = 0; k < nn; k++) {
            gno = grains[k];
            p[gno] += (d[k] - zvalue[gno])*(d[k] - zvalue[gno]);
        }
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = sqrt(p[gno]/sizes[gno]);
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_FLAT_BOUNDARY_LENGTH])) {
        gwy_clear(p, ngrains + 1);
        /* Note the cycles go to xres and yres inclusive as we calculate the boundary, not pixel interiors. */
        for (i = 0; i <= yres; i++) {
            for (j = 0; j <= xres; j++) {
                gint g1, g2, g3, g4, f;

                /* Hope compiler will optimize this mess... */
                g1 = (i > 0 && j > 0) ? grains[i*xres + j - xres - 1] : 0;
                g2 = (i > 0 && j < xres) ? grains[i*xres + j - xres] : 0;
                g3 = (i < yres && j > 0) ? grains[i*xres + j - 1] : 0;
                g4 = (i < yres && j < xres) ? grains[i*xres + j] : 0;
                f = (g1 > 0) + (g2 > 0) + (g3 > 0) + (g4 > 0);
                if (f == 0 || f == 4)
                    continue;

                if (f == 1 || f == 3) {
                    /* Try to avoid too many if-thens by using the fact they are all either zero or an identical value
                     */
                    p[g1 | g2 | g3 | g4] += qdiag/2.0;
                }
                else if (g1 && g4) {
                    /* This works for both g1 == g4 and g1 != g4 */
                    p[g1] += qdiag/2.0;
                    p[g4] += qdiag/2.0;
                }
                else if (g2 && g3) {
                    /* This works for both g2 == g3 and g2 != g3 */
                    p[g2] += qdiag/2.0;
                    p[g3] += qdiag/2.0;
                }
                else if (g1 == g2)
                    p[g1 | g3] += qh;
                else if (g1 == g3)
                    p[g1 | g2] += qv;
                else {
                    g_assert_not_reached();
                }
            }
        }
    }
    if (quantity_data[GWY_GRAIN_VALUE_BOUNDARY_MINIMUM] || quantity_data[GWY_GRAIN_VALUE_BOUNDARY_MAXIMUM]) {
        gdouble *pmin = quantity_data[GWY_GRAIN_VALUE_BOUNDARY_MINIMUM];
        gdouble *pmax = quantity_data[GWY_GRAIN_VALUE_BOUNDARY_MAXIMUM];

        if (pmin) {
            for (gno = 0; gno <= ngrains; gno++)
                pmin[gno] = G_MAXDOUBLE;
        }
        if (pmax) {
            for (gno = 0; gno <= ngrains; gno++)
                pmax[gno] = -G_MAXDOUBLE;
        }

        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++) {
                gdouble z;

                /* Processing of the none-grain boundary is waste of time. */
                if (!(gno = grains[i*xres + j]))
                    continue;

                if (i && j && i < yres-1 && j < xres - 1
                    && grains[(i - 1)*xres + j] == gno
                    && grains[i*xres + j - 1] == gno
                    && grains[i*xres + j + 1] == gno
                    && grains[(i + 1)*xres + j] == gno)
                    continue;

                z = d[i*xres + j];
                if (pmin && z < pmin[gno])
                    pmin[gno] = z;
                if (pmax && z > pmax[gno])
                    pmax[gno] = z;
            }
        }
    }
    if (quantity_data[GWY_GRAIN_VALUE_MINIMUM_BOUND_SIZE]
        || quantity_data[GWY_GRAIN_VALUE_MINIMUM_BOUND_ANGLE]
        || quantity_data[GWY_GRAIN_VALUE_MAXIMUM_BOUND_SIZE]
        || quantity_data[GWY_GRAIN_VALUE_MAXIMUM_BOUND_ANGLE]
        || quantity_data[GWY_GRAIN_VALUE_CONVEX_HULL_AREA]
        || quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_R]
        || quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_X]
        || quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_Y]) {
        gdouble *psmin = quantity_data[GWY_GRAIN_VALUE_MINIMUM_BOUND_SIZE];
        gdouble *psmax = quantity_data[GWY_GRAIN_VALUE_MAXIMUM_BOUND_SIZE];
        gdouble *pamin = quantity_data[GWY_GRAIN_VALUE_MINIMUM_BOUND_ANGLE];
        gdouble *pamax = quantity_data[GWY_GRAIN_VALUE_MAXIMUM_BOUND_ANGLE];
        gdouble *achull = quantity_data[GWY_GRAIN_VALUE_CONVEX_HULL_AREA];
        gdouble *circcr = quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_R];
        gdouble *circcx = quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_X];
        gdouble *circcy = quantity_data[GWY_GRAIN_VALUE_CIRCUMCIRCLE_Y];
        GArray *vertices;

        /* Find the complete convex hulls */
        vertices = g_array_new(FALSE, FALSE, sizeof(GridPoint));
        for (gno = 1; gno <= ngrains; gno++) {
            gdouble dx = qh, dy = qv;

            find_grain_convex_hull(xres, yres, grains, boundpos[gno], vertices);
            if (psmin || pamin) {
                grain_minimum_bound(vertices, qh, qv, &dx, &dy);
                if (psmin)
                    psmin[gno] = hypot(dx, dy);
                if (pamin) {
                    pamin[gno] = gwy_canonicalize_angle(atan2(-dy, dx), FALSE, FALSE);
                }
            }
            if (psmax || pamax) {
                grain_maximum_bound(vertices, qh, qv, &dx, &dy);
                if (psmax)
                    psmax[gno] = hypot(dx, dy);
                if (pamax) {
                    pamax[gno] = gwy_canonicalize_angle(atan2(-dy, dx), FALSE, FALSE);
                }
            }
            if (achull) {
                achull[gno] = grain_convex_hull_area(vertices, qh, qv);
            }
            if (circcr || circcx || circcy) {
                InscribedDisc circle = { 0.0, 0.0, 0.0, 0 };

                grain_convex_hull_centre(vertices, qh, qv, &circle.x, &circle.y);
                circle.R2 = minimize_circle_radius(&circle, vertices, qh, qv);
                improve_circumscribed_circle(&circle, vertices, qh, qv);

                if (circcr)
                    circcr[gno] = sqrt(circle.R2);
                if (circcx)
                    circcx[gno] = circle.x + data_field->xoff;
                if (circcy)
                    circcy[gno] = circle.y + data_field->yoff;
            }
        }
        /* Finalize */
        g_array_free(vertices, TRUE);
    }
    /* XXX: This must go before GWY_GRAIN_VALUE_CENTER_X and GWY_GRAIN_VALUE_CENTER_Y because we want them as pixel
     * quantities. */
    if (quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_R]
        || quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_X]
        || quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_Y]) {
        gdouble *inscdr = quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_R];
        gdouble *inscdx = quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_X];
        gdouble *inscdy = quantity_data[GWY_GRAIN_VALUE_INSCRIBED_DISC_Y];
        guint *grain = NULL;
        guint grainsize = 0;
        PixelQueue *inqueue = g_slice_new0(PixelQueue);
        PixelQueue *outqueue = g_slice_new0(PixelQueue);
        GArray *candidates = g_array_new(FALSE, FALSE, sizeof(InscribedDisc));
        EdgeQueue edges = { 0, 0, NULL };
        InscribedDisc *cand;

        /*
         * For each grain:
         *    Extract it, find all boundary pixels.
         *    Use (octagnoal) erosion to find disc centre candidate(s).
         *    For each candidate:
         *       Find maximum disc that fits with this centre.
         *       By expanding/moving try to find a larger disc until we cannot
         *       improve it.
         */
        for (gno = 1; gno <= ngrains; gno++) {
            guint width, height, dist;
            gdouble dx, dy, centrex, centrey;
            guint w = bbox[4*gno + 2], h = bbox[4*gno + 3];
            gdouble xoff = qh*bbox[4*gno] + data_field->xoff, yoff = qv*bbox[4*gno + 1] + data_field->yoff;
            guint ncand;

            /* If the grain is rectangular, calculate the disc directly. Large rectangular grains are rare but the
             * point is to catch grains with width of height of 1 here. */
            if (sizes[gno] == w*h) {
                dx = 0.5*w*qh;
                dy = 0.5*h*qv;
                if (inscdr)
                    inscdr[gno] = 0.999*MIN(dx, dy);
                if (inscdx)
                    inscdx[gno] = dx + xoff;
                if (inscdy)
                    inscdy[gno] = dy + yoff;
                continue;
            }

            /* Upsampling twice combined with octagonal erosion has the nice property that we get candidate pixels in
             * places such as corners or junctions of one-pixel thin lines. */
            grain = extract_upsampled_square_pixel_grain(grains, xres, gno, bbox + 4*gno, grain, &grainsize,
                                                         &width, &height, qh, qv);
            /* Size of upsamples pixel in original pixel coordinates.  Normally equal to 1/2 and always approximately
             * 1:1. */
            dx = w*(qh/qgeom)/width;
            dy = h*(qv/qgeom)/height;
            /* Grain centre in squeezed pixel coordinates within the bbox. */
            centrex = (xvalue[gno] + 0.5)*(qh/qgeom);
            centrey = (yvalue[gno] + 0.5)*(qv/qgeom);

            dist = _gwy_simple_dist_trans(grain, width, height, TRUE, GWY_DISTANCE_TRANSFORM_OCTAGONAL48,
                                          inqueue, outqueue);
            if (dist % 2 == 0) {
                GWY_SWAP(PixelQueue*, inqueue, outqueue);
            }
#if 0
            for (i = 0; i < height; i++) {
                for (j = 0; j < width; j++) {
                    if (!grain[i*width + j])
                        g_printerr("..");
                    else
                        g_printerr("%02u", grain[i*width + j]);
                    g_printerr("%c", j == width-1 ? '\n' : ' ');
                }
            }
#endif
            /* Now inqueue is always non-empty and contains max-distance pixels of the upscaled grain. */
            find_disc_centre_candidates(candidates, inqueue, grain, width, height, dx, dy, centrex, centrey);
            find_all_edges(&edges, grains, xres, gno, bbox + 4*gno, qh/qgeom, qv/qgeom);

            /* Try a few first candidates for the inscribed disc centre. */
            ncand = MIN(15, candidates->len);
            for (i = 0; i < ncand; i++) {
                cand = &g_array_index(candidates, InscribedDisc, i);
                improve_inscribed_disc(cand, &edges, dist);
            }

            cand = &g_array_index(candidates, InscribedDisc, 0);
            for (i = 1; i < ncand; i++) {
                if (g_array_index(candidates, InscribedDisc, i).R2 > cand->R2)
                    cand = &g_array_index(candidates, InscribedDisc, i);
            }

            if (inscdr)
                inscdr[gno] = sqrt(cand->R2 * qarea);
            if (inscdx)
                inscdx[gno] = cand->x*qgeom + xoff;
            if (inscdy)
                inscdy[gno] = cand->y*qgeom + yoff;
        }

        g_free(grain);
        g_free(inqueue->points);
        g_free(outqueue->points);
        g_slice_free(PixelQueue, inqueue);
        g_slice_free(PixelQueue, outqueue);
        g_free(edges.edges);
        g_array_free(candidates, TRUE);
    }
    /* XXX: This must go before GWY_GRAIN_VALUE_CENTER_X and GWY_GRAIN_VALUE_CENTER_Y because we want them as pixel
     * quantities. */
    if ((p = quantity_data[GWY_GRAIN_VALUE_MEAN_RADIUS])) {
        guint *blen = g_new0(guint, ngrains + 1);

        k = 0;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, k++) {
                gdouble xc, yc;

                if (!(gno = grains[k]))
                    continue;

                xc = xvalue[gno];
                yc = yvalue[gno];
                if (!i || !grains[k - xres]) {
                    p[gno] += hypot(qh*(j+0.5 - xc), qv*(i - yc));
                    p[gno] += hypot(qh*(j+1 - xc), qv*(i - yc));
                    blen[gno] += 2;
                }
                if (!j || !grains[k-1]) {
                    p[gno] += hypot(qh*(j - xc), qv*(i - yc));
                    p[gno] += hypot(qh*(j - xc), qv*(i+0.5 - yc));
                    blen[gno] += 2;
                }
                if (j == xres-1 || !grains[k+1]) {
                    p[gno] += hypot(qh*(j+1 - xc), qv*(i+0.5 - yc));
                    p[gno] += hypot(qh*(j+1 - xc), qv*(i+1 - yc));
                    blen[gno] += 2;
                }
                if (i == yres-1 || !grains[k + xres]) {
                    p[gno] += hypot(qh*(j - xc), qv*(i+1 - yc));
                    p[gno] += hypot(qh*(j+0.5 - xc), qv*(i+1 - yc));
                    blen[gno] += 2;
                }
            }
        }

        for (gno = 1; gno <= ngrains; gno++)
            p[gno] /= blen[gno];

        g_free(blen);
    }
    /* XXX: This must go before GWY_GRAIN_VALUE_CENTER_X and GWY_GRAIN_VALUE_CENTER_Y because we want them as pixel
     * quantities. */
    if (quantity_data[GWY_GRAIN_VALUE_MINIMUM_MARTIN_DIAMETER]
        || quantity_data[GWY_GRAIN_VALUE_MINIMUM_MARTIN_ANGLE]
        || quantity_data[GWY_GRAIN_VALUE_MAXIMUM_MARTIN_DIAMETER]
        || quantity_data[GWY_GRAIN_VALUE_MAXIMUM_MARTIN_ANGLE]) {
        gdouble *mmin = quantity_data[GWY_GRAIN_VALUE_MINIMUM_MARTIN_DIAMETER];
        gdouble *mmax = quantity_data[GWY_GRAIN_VALUE_MAXIMUM_MARTIN_DIAMETER];
        gdouble *phimin = quantity_data[GWY_GRAIN_VALUE_MINIMUM_MARTIN_ANGLE];
        gdouble *phimax = quantity_data[GWY_GRAIN_VALUE_MAXIMUM_MARTIN_ANGLE];
        guint *csizes = g_new0(guint, ngrains + 1);
        guint *pos = g_new0(guint, ngrains + 1);
        guint maxsize, t;
        GwyXY *coords;
        gdouble *rotcoords, *diams;
        gdouble x, y;

        /*
         * For each grain:
         *    Extract all coordinates, real and grain-centered.  Extract four
         *    points from each grain (this gives good median lines even for
         *    small L-shaped grains).
         *    For each selected direction:
         *      Rotate coordinates, find the ortohognal median line (using the
         *      ortogonal coordinate).
         *      Go along supersampled median line, count how many times we hit
         *      the grain (again, must use real-space angles).
         *    Find minimum and/or maximum.
         */
        csizes[0] = 0;
        csizes[1] = maxsize = sizes[1];
        for (gno = 2; gno <= ngrains; gno++) {
            csizes[gno] = sizes[gno] + csizes[gno-1];
            if (sizes[gno] > maxsize)
                maxsize = sizes[gno];
        }
        for (gno = 1; gno <= ngrains; gno++)
            pos[gno] = 4*csizes[gno-1];

        /* Extract coordinates */
        coords = g_new(GwyXY, 4*csizes[ngrains]);
        k = 0;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++, k++) {
                if ((gno = grains[k])) {
                    x = qh*(j - xvalue[gno]);
                    y = qv*(i - yvalue[gno]);
                    t = pos[gno];
                    coords[t].x = x - 0.25*qh;
                    coords[t].y = y - 0.25*qv;
                    t++;
                    coords[t].x = x + 0.25*qh;
                    coords[t].y = y - 0.25*qv;
                    t++;
                    coords[t].x = x - 0.25*qh;
                    coords[t].y = y + 0.25*qv;
                    t++;
                    coords[t].x = x + 0.25*qh;
                    coords[t].y = y + 0.25*qv;
                    pos[gno] = t+1;
                }
            }
        }
        g_free(pos);
        /* Find median lines by direction of each block */
        rotcoords = g_new(gdouble, 4*maxsize);
        diams = g_new(gdouble, 2*NDIRECTIONS);
        for (gno = 1; gno <= ngrains; gno++) {
            for (t = 0; t < 2*NDIRECTIONS; t++) {
                gdouble med = median_cut_for_direction(t, gno, csizes, coords, rotcoords);
                diams[t] = martin_intersection_length(t, med, qh, qv, qdiag, gno, bbox, xvalue, yvalue, xres, grains);
            }

            /* Find the minima/maxima. */
            if (mmin || phimin) {
                k = 0;
                for (t = 1; t < 2*NDIRECTIONS; t++) {
                    if (diams[t] < diams[k])
                        k = t;
                }
                if (mmin)
                    mmin[gno] = diams[k];
                if (phimin)
                    phimin[gno] = refine_diameter_direction(diams, k, FALSE);
            }
            if (mmax || phimax) {
                k = 0;
                for (t = 1; t < 2*NDIRECTIONS; t++) {
                    if (diams[t] > diams[k])
                        k = t;
                }
                if (mmax)
                    mmax[gno] = diams[k];
                if (phimax)
                    phimax[gno] = refine_diameter_direction(diams, k, TRUE);
            }
        }

        /* Finalize */
        g_free(csizes);
        g_free(coords);
        g_free(rotcoords);
        g_free(diams);
    }
    if (quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MAJOR]
        || quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MINOR]
        || quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_ANGLE]) {
        gdouble *amaj = quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MAJOR];
        gdouble *amin = quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MINOR];
        gdouble *phi = quantity_data[GWY_GRAIN_VALUE_EQUIV_ELLIPSE_ANGLE];

        for (gno = 1; gno <= ngrains; gno++) {
            guint n = sizes[gno];
            gdouble *lin = linear + 5*gno;
            gdouble Jxx = qh*qh*(lin[0] + n/12.0)*qarea;
            gdouble Jxy = qh*qv*lin[1]*qarea;
            gdouble Jyy = qv*qv*(lin[2] + n/12.0)*qarea;

            if (phi) {
                gdouble Jeps = 1e-9*MAX(Jxx, Jyy);

                if (fabs(Jxx - Jyy) > Jeps || fabs(Jxy) > Jeps)
                    phi[gno] = 0.5*atan2(-2.0*Jxy, Jxx - Jyy);
                else
                    phi[gno] = 0.0;
            }

            if (amaj || amin) {
                gdouble u = Jxx + Jyy, v = hypot(2.0*Jxy, Jxx - Jyy), w = sqrt(G_PI*sqrt(Jxx*Jyy - Jxy*Jxy));

                if (amaj)
                    amaj[gno] = sqrt((u + v)/w);
                if (amin)
                    amin[gno] = sqrt((u - v)/w);
            }
        }

    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_CENTER_X])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = qh*(p[gno] + 0.5) + data_field->xoff;
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_CENTER_Y])) {
        for (gno = 0; gno <= ngrains; gno++)
            p[gno] = qv*(p[gno] + 0.5) + data_field->yoff;
    }
    if (quantity_data[GWY_GRAIN_VALUE_VOLUME_0]
        || quantity_data[GWY_GRAIN_VALUE_VOLUME_MIN]) {
        gdouble *pv0 = quantity_data[GWY_GRAIN_VALUE_VOLUME_0];
        gdouble *pvm = quantity_data[GWY_GRAIN_VALUE_VOLUME_MIN];

        if (pv0) {
            integrate_grain_volume0(d, grains, xres, yres, pv0, ngrains, qarea);
            if (pvm) {
                for (gno = 0; gno <= ngrains; gno++)
                    pvm[gno] = pv0[gno] - qarea*min[gno]*sizes[gno];
            }
        }
        else {
            g_assert(pvm);
            integrate_grain_volume0(d, grains, xres, yres, pvm, ngrains, qarea);
            for (gno = 0; gno <= ngrains; gno++)
                pvm[gno] -= qarea*min[gno]*sizes[gno];
        }
    }
    if ((p = quantity_data[GWY_GRAIN_VALUE_VOLUME_LAPLACE])) {
        /* Fail gracefully when there is one big `grain' over all data. */
        if (ngrains == 1 && sizes[1] == xres*yres)
            p[1] = 0.0;
        else {
            GwyDataField *difference = gwy_data_field_duplicate(data_field);
            GwyDataField *mask = gwy_data_field_new_alike(data_field, FALSE);
            gdouble *m = mask->data;

            for (k = 0; k < nn; k++)
                m[k] = grains[k];

            gwy_data_field_laplace_solve(difference, mask, -1, 0.4);
            g_object_unref(mask);
            gwy_data_field_subtract_fields(difference, data_field, difference);
            integrate_grain_volume0(difference->data, grains, xres, yres, p, ngrains, qarea);
            g_object_unref(difference);
        }
    }
    if (quantity_data[GWY_GRAIN_VALUE_SLOPE_THETA] || quantity_data[GWY_GRAIN_VALUE_SLOPE_PHI]) {
        gdouble *ptheta = quantity_data[GWY_GRAIN_VALUE_SLOPE_THETA];
        gdouble *pphi = quantity_data[GWY_GRAIN_VALUE_SLOPE_PHI];

        for (gno = 1; gno <= ngrains; gno++) {
            gdouble xx, yy, xy, xz, yz, det, bx, by;
            gdouble *lin = linear + 5*gno;

            xx = lin[0];
            xy = lin[1];
            yy = lin[2];
            xz = lin[3];
            yz = lin[4];
            det = xx*yy - xy*xy;
            if (det) {
                bx = (xz*yy - xy*yz)/(qh*det);
                by = (yz*xx - xy*xz)/(qv*det);
                if (ptheta)
                    ptheta[gno] = atan(hypot(bx, by));
                if (pphi)
                    pphi[gno] = atan2(by, -bx);
            }
            else {
                if (ptheta)
                    ptheta[gno] = 0.0;
                if (pphi)
                    pphi[gno] = 0.0;
            }
        }
    }
    if (quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_X]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_Y]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_Z]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE1]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE2]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE_ANGLE1]
        || quantity_data[GWY_GRAIN_VALUE_CURVATURE_ANGLE2]) {
        gdouble *px = quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_X];
        gdouble *py = quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_Y];
        gdouble *pz = quantity_data[GWY_GRAIN_VALUE_CURVATURE_CENTER_Z];
        gdouble *pk1 = quantity_data[GWY_GRAIN_VALUE_CURVATURE1];
        gdouble *pk2 = quantity_data[GWY_GRAIN_VALUE_CURVATURE2];
        gdouble *pa1 = quantity_data[GWY_GRAIN_VALUE_CURVATURE_ANGLE1];
        gdouble *pa2 = quantity_data[GWY_GRAIN_VALUE_CURVATURE_ANGLE2];
        gdouble mx = sqrt(qh/qv), my = sqrt(qv/qh);

        for (gno = 1; gno <= ngrains; gno++) {
            /* a:
             *  0 [<1>
             *  1  <x>   <x²>
             *  3  <y>   <xy>   <y²>
             *  6  <x²>  <x³>   <x²y>  <x⁴>
             * 10  <xy>  <x²y>  <xy²>  <x³y>   <x²y²>
             * 15  <y²>  <xy²>  <y³>   <x²y²>  <xy³>   <y⁴>]
             * b: [<z>  <xz>  <yz>  <x²z>  <xyz>  <y²z>]
             */
            gdouble a[21], b[6];
            gdouble *lin = linear + 5*gno, *quad = quadratic + 12*gno;
            guint n = sizes[gno];

            if (n >= 6) {
                a[0] = n;
                a[1] = a[3] = 0.0;
                a[2] = a[6] = lin[0];
                a[4] = a[10] = lin[1];
                a[5] = a[15] = lin[2];
                a[7] = quad[0];
                a[8] = a[11] = quad[1];
                a[9] = quad[4];
                a[12] = a[16] = quad[2];
                a[13] = quad[5];
                a[14] = a[18] = quad[6];
                a[17] = quad[3];
                a[19] = quad[7];
                a[20] = quad[8];
                if (gwy_math_choleski_decompose(6, a)) {
                    b[0] = n*zvalue[gno];
                    b[1] = lin[3];
                    b[2] = lin[4];
                    b[3] = quad[9];
                    b[4] = quad[10];
                    b[5] = quad[11];
                    gwy_math_choleski_solve(6, a, b);
                    /* Get pixel aspect ratio right while keeping pixel size around 1. */
                    b[1] /= mx;
                    b[2] /= my;
                    b[3] /= mx*mx;
                    b[5] /= my*my;
                }
                else
                    n = 0;
            }

            /* Recycle a[] for the curvature parameters. */
            if (n >= 6)
                gwy_math_curvature(b, a+0, a+1, a+2, a+3, a+4, a+5, a+6);
            else {
                a[0] = a[1] = a[2] = a[4] = a[5] = 0.0;
                a[3] = G_PI/2.0;
                a[6] = zvalue[gno];
            }
            if (pk1)
                pk1[gno] = a[0]/(qgeom*qgeom);
            if (pk2)
                pk2[gno] = a[1]/(qgeom*qgeom);
            if (pa1)
                pa1[gno] = a[2];
            if (pa2)
                pa2[gno] = a[3];
            if (px)
                px[gno] = qgeom*a[4] + xvalue[gno];
            if (py)
                py[gno] = qgeom*a[5] + yvalue[gno];
            if (pz)
                pz[gno] = a[6];
        }
    }

    /* Copy quantity values to all other instances of the same quantity in @values. */
    gwy_clear(seen, NQ);
    for (i = 0; i < nquantities; i++) {
        GwyGrainQuantity quantity = quantities[i];

        if ((guint)quantity >= NQ || need_aux[quantity] == INVALID)
            continue;

        if (seen[quantity])
            gwy_assign(values[i], quantity_data[quantity], ngrains + 1);
        seen[quantity] = TRUE;
    }

    /* Finalize */
    for (l = buffers; l; l = g_list_next(l))
        g_free(l->data);
    g_list_free(buffers);

    return values;
}

/**
 * gwy_grain_quantity_needs_same_units:
 * @quantity: A grain quantity.
 *
 * Tests whether a grain quantity is defined only when lateral and value units match.
 *
 * Returns: %TRUE if @quantity is meaningless when lateral and value units differ, %FALSE if it is always defined.
 *
 * Since: 2.7
 **/
gboolean
gwy_grain_quantity_needs_same_units(GwyGrainQuantity quantity)
{
    enum {
        no_same_units = ((ONE << GWY_GRAIN_VALUE_PROJECTED_AREA)
                         | (ONE << GWY_GRAIN_VALUE_EQUIV_SQUARE_SIDE)
                         | (ONE << GWY_GRAIN_VALUE_EQUIV_DISC_RADIUS)
                         | (ONE << GWY_GRAIN_VALUE_MAXIMUM)
                         | (ONE << GWY_GRAIN_VALUE_MINIMUM)
                         | (ONE << GWY_GRAIN_VALUE_MEAN)
                         | (ONE << GWY_GRAIN_VALUE_MEDIAN)
                         | (ONE << GWY_GRAIN_VALUE_HALF_HEIGHT_AREA)
                         | (ONE << GWY_GRAIN_VALUE_RMS)
                         | (ONE << GWY_GRAIN_VALUE_FLAT_BOUNDARY_LENGTH)
                         | (ONE << GWY_GRAIN_VALUE_MINIMUM_BOUND_SIZE)
                         | (ONE << GWY_GRAIN_VALUE_MINIMUM_BOUND_ANGLE)
                         | (ONE << GWY_GRAIN_VALUE_MAXIMUM_BOUND_SIZE)
                         | (ONE << GWY_GRAIN_VALUE_MAXIMUM_BOUND_ANGLE)
                         | (ONE << GWY_GRAIN_VALUE_CENTER_X)
                         | (ONE << GWY_GRAIN_VALUE_CENTER_Y)
                         | (ONE << GWY_GRAIN_VALUE_VOLUME_0)
                         | (ONE << GWY_GRAIN_VALUE_VOLUME_MIN)
                         | (ONE << GWY_GRAIN_VALUE_VOLUME_LAPLACE)
                         | (ONE << GWY_GRAIN_VALUE_SLOPE_PHI)
                         | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_X)
                         | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_Y)
                         | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_Z)
                         | (ONE << GWY_GRAIN_VALUE_CURVATURE_ANGLE1)
                         | (ONE << GWY_GRAIN_VALUE_CURVATURE_ANGLE2)
                         | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_R)
                         | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_X)
                         | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_Y)
                         | (ONE << GWY_GRAIN_VALUE_CONVEX_HULL_AREA)
                         | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_R)
                         | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_X)
                         | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_Y)
                         | (ONE << GWY_GRAIN_VALUE_MEAN_RADIUS)
                         | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MAJOR)
                         | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MINOR)
                         | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_ANGLE)
                         | (ONE << GWY_GRAIN_VALUE_MINIMUM_MARTIN_DIAMETER)
                         | (ONE << GWY_GRAIN_VALUE_MINIMUM_MARTIN_ANGLE)
                         | (ONE << GWY_GRAIN_VALUE_MAXIMUM_MARTIN_DIAMETER)
                         | (ONE << GWY_GRAIN_VALUE_MAXIMUM_MARTIN_ANGLE)),
        same_units = ((ONE << GWY_GRAIN_VALUE_SLOPE_THETA)
                      | (ONE << GWY_GRAIN_VALUE_SURFACE_AREA)
                      | (ONE << GWY_GRAIN_VALUE_CURVATURE1)
                      | (ONE << GWY_GRAIN_VALUE_CURVATURE2))
    };

    if ((ONE << quantity) & no_same_units)
        return FALSE;
    if ((ONE << quantity) & same_units)
        return TRUE;
    g_return_val_if_reached(FALSE);
}

/**
 * gwy_grain_quantity_get_units:
 * @quantity: A grain quantity.
 * @siunitxy: Lateral SI unit of data.
 * @siunitz: Value SI unit of data.
 * @result: An SI unit to set to the units of @quantity. It can be %NULL, a new SI unit is created then and returned.
 *
 * Calculates the units of a grain quantity.
 *
 * Returns: When @result is %NULL, a newly creates SI unit that has to be dereferenced when no longer used later.
 *          Otherwise @result itself is simply returned, its reference count is NOT increased.
 *
 * Since: 2.7
 **/
GwySIUnit*
gwy_grain_quantity_get_units(GwyGrainQuantity quantity,
                             GwySIUnit *siunitxy,
                             GwySIUnit *siunitz,
                             GwySIUnit *result)
{
    enum {
        coord_units = ((ONE << GWY_GRAIN_VALUE_EQUIV_SQUARE_SIDE)
                       | (ONE << GWY_GRAIN_VALUE_EQUIV_DISC_RADIUS)
                       | (ONE << GWY_GRAIN_VALUE_FLAT_BOUNDARY_LENGTH)
                       | (ONE << GWY_GRAIN_VALUE_MINIMUM_BOUND_SIZE)
                       | (ONE << GWY_GRAIN_VALUE_MAXIMUM_BOUND_SIZE)
                       | (ONE << GWY_GRAIN_VALUE_CENTER_X)
                       | (ONE << GWY_GRAIN_VALUE_CENTER_Y)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_X)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_Y)
                       | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_R)
                       | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_X)
                       | (ONE << GWY_GRAIN_VALUE_INSCRIBED_DISC_Y)
                       | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_R)
                       | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_X)
                       | (ONE << GWY_GRAIN_VALUE_CIRCUMCIRCLE_Y)
                       | (ONE << GWY_GRAIN_VALUE_MEAN_RADIUS)
                       | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MAJOR)
                       | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_MINOR)
                       | (ONE << GWY_GRAIN_VALUE_MINIMUM_MARTIN_DIAMETER)
                       | (ONE << GWY_GRAIN_VALUE_MAXIMUM_MARTIN_DIAMETER)),
        icoord_units = ((ONE << GWY_GRAIN_VALUE_CURVATURE1)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE2)),
        value_units = ((ONE << GWY_GRAIN_VALUE_MAXIMUM)
                       | (ONE << GWY_GRAIN_VALUE_MINIMUM)
                       | (ONE << GWY_GRAIN_VALUE_MEAN)
                       | (ONE << GWY_GRAIN_VALUE_MEDIAN)
                       | (ONE << GWY_GRAIN_VALUE_RMS)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE_CENTER_Z)),
        area_units = ((ONE << GWY_GRAIN_VALUE_PROJECTED_AREA)
                      | (ONE << GWY_GRAIN_VALUE_HALF_HEIGHT_AREA)
                      | (ONE << GWY_GRAIN_VALUE_SURFACE_AREA)
                      | (ONE << GWY_GRAIN_VALUE_CONVEX_HULL_AREA)),
        volume_units = ((ONE << GWY_GRAIN_VALUE_VOLUME_0)
                        | (ONE << GWY_GRAIN_VALUE_VOLUME_MIN)
                        | (ONE << GWY_GRAIN_VALUE_VOLUME_LAPLACE)),
        angle_units = ((ONE << GWY_GRAIN_VALUE_MINIMUM_BOUND_ANGLE)
                       | (ONE << GWY_GRAIN_VALUE_MAXIMUM_BOUND_ANGLE)
                       | (ONE << GWY_GRAIN_VALUE_SLOPE_PHI)
                       | (ONE << GWY_GRAIN_VALUE_SLOPE_THETA)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE_ANGLE1)
                       | (ONE << GWY_GRAIN_VALUE_CURVATURE_ANGLE2)
                       | (ONE << GWY_GRAIN_VALUE_EQUIV_ELLIPSE_ANGLE)
                       | (ONE << GWY_GRAIN_VALUE_MINIMUM_MARTIN_ANGLE)
                       | (ONE << GWY_GRAIN_VALUE_MAXIMUM_MARTIN_ANGLE))
    };

    g_return_val_if_fail(GWY_IS_SI_UNIT(siunitxy), result);
    g_return_val_if_fail(GWY_IS_SI_UNIT(siunitz), result);

    if ((ONE << quantity) & coord_units)
        return gwy_si_unit_power(siunitxy, 1, result);
    if ((ONE << quantity) & icoord_units)
        return gwy_si_unit_power(siunitxy, -1, result);
    if ((ONE << quantity) & value_units)
        return gwy_si_unit_power(siunitz, 1, result);
    if ((ONE << quantity) & area_units)
        return gwy_si_unit_power(siunitxy, 2, result);
    if ((ONE << quantity) & volume_units)
        return gwy_si_unit_power_multiply(siunitxy, 2, siunitz, 1, result);
    if ((ONE << quantity) & angle_units) {
        if (!result)
            return gwy_si_unit_new(NULL);
        gwy_si_unit_set_from_string(result, NULL);
        return result;
    }

    g_return_val_if_reached(result);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
