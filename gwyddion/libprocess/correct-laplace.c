/*
 *  $Id: correct-laplace.c 26340 2024-05-15 09:04:45Z yeti-dn $
 *  Copyright (C) 2004-2024 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/datafield.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/correct.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

/* Expected maximum number of coeff block types. */
enum { COEFF_BLOCK_NTYPES = 64 };

enum { NONE = G_MAXUINT };

enum { MIN_SIZE_FOR_OMP = 2000 };

typedef enum {
    UP,
    RIGHT,
    DOWN,
    LEFT,
    NDIRECTIONS
} LaplaceDirection;

/*
 * @len: The number of blocks.  (The allocated size is usually a bit larger.)
 * @n: Start of each neighbour in @k, size @len+1 (one extra item to get consistently the size of the last block).
 * @k: Indices of neighbours for laplacian calculation, allocated size is a multiple of @len but blocks are
 *     variable-size, given by @n.  These indices refer to these arrays, not the original grid.
 * @w: Unique coefficient blocks, a small array of size @wsize.
 * @iw: Indices of blocks in @w, size @len.
 * @nw: Number of unique coefficient blocks.
 * @wsize: Allocated size of @w (in doubles, not blocks).
 * @wlensize: Allocated size of @wlen.
 * @z: Values of the points, size @len.
 * @rhs: Right-hand-sides of the points, size @len.
 * @f: The difference between the second derivative and the value, size @len.
 * @v: Conjugate-gradients auxiliary vector, size @len.
 * @t: Conjugate-gradients auxiliary vector, size @len.
 * @gindex: Where the point is placed in the original data, size @len.  Not used during iteration.
 * @wlen: Lengths of blocks in @w.  Only used during construction because we take the length from @n (the @n and @w
 *        blocks must be of the same size).
 */
typedef struct {
    guint len;
    guint int_size;
    guint float_size;
    guint wsize;
    guint wlensize;
    guint nw;
    guint *n;
    guint *k;
    guint *iw;
    gdouble *w;
    gdouble *z;
    gdouble *rhs;
    gdouble *f;
    gdouble *v;
    gdouble *t;
    guint *gindex;
    guint *wlen;
} LaplaceIterators;

typedef struct {
    gboolean is_virtual : 1;
    gboolean is_boundary : 1;
    gboolean is_rhs : 1;

    guint bdist;     // Distance of the boundary line where ∂z/∂x = 0
    guint step;
    guint neighbour;
    guint neighbour2;

    gdouble rhs;     // Remember the exterior data used for rhs
    gdouble weight;  // Coefficient before (z_neighbour - z_0)
    gdouble weight2; // Coefficient before (z_neighbour2 - z_0)
} LaplaceNeighbour;

/**
 * gwy_data_field_correct_laplace_iteration:
 * @data_field: Data field to be corrected.
 * @mask_field: Mask of places to be corrected.
 * @buffer_field: Initialized to same size as mask and data.
 * @error: Maximum change within last step.
 * @corrfactor: Correction factor within step.
 *
 * Performs one interation of Laplace data correction.
 *
 * Tries to remove all the points in mask off the data by using iterative method similar to solving heat flux
 * equation.
 *
 * Use this function repeatedly until reasonable @error is reached.
 *
 * <warning>For almost all purposes this function was superseded by non-iterative gwy_data_field_laplace_solve() which
 * is simultaneously much faster and more accurate.</warning>
 **/
void
gwy_data_field_correct_laplace_iteration(GwyDataField *data_field,
                                         GwyDataField *mask_field,
                                         GwyDataField *buffer_field,
                                         gdouble corrfactor,
                                         gdouble *error)
{
    gint xres, yres, i, j;
    const gdouble *mask, *data;
    gdouble *buffer;
    gdouble cor, cf, err;

    g_return_if_fail(GWY_IS_DATA_FIELD(data_field));
    g_return_if_fail(GWY_IS_DATA_FIELD(mask_field));
    g_return_if_fail(GWY_IS_DATA_FIELD(buffer_field));
    g_return_if_fail(data_field->xres == mask_field->xres && data_field->yres == mask_field->yres);

    xres = data_field->xres;
    yres = data_field->yres;

    /* check buffer field */
    if (buffer_field->xres != xres || buffer_field->yres != yres)
        gwy_data_field_resample(buffer_field, xres, yres, GWY_INTERPOLATION_NONE);

    gwy_data_field_copy(data_field, buffer_field, FALSE);

    data = data_field->data;
    buffer = buffer_field->data;
    mask = mask_field->data;

    /* set boundary condition for masked boundary data */
    if (yres >= 2) {
        for (i = 0; i < xres; i++) {
            if (mask[i] > 0)
                buffer[i] = buffer[i + xres];
            if (mask[i + xres*(yres - 1)] > 0)
                buffer[i + xres*(yres - 1)] = buffer[i + xres*(yres - 2)];
        }
    }
    if (xres >= 2) {
        for (i = 0; i < yres; i++) {
            if (mask[xres*i] > 0)
                buffer[xres*i] = buffer[1 + xres*i];
            if (mask[xres - 1 + xres*i] > 0)
                buffer[xres - 1 + xres*i] = buffer[xres-2 + xres*i];
        }
    }

    /* iterate */
    err = 0.0;
    cf = corrfactor;
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            reduction(max:err) \
            private(i,j,cor) \
            shared(data,mask,buffer,xres,yres,cf)
#endif
    for (i = 1; i < yres - 1; i++) {
        for (j = 1; j < xres - 1; j++) {
            if (mask[i*xres + j] > 0) {
                cor = cf*((data[(i - 1)*xres + j] + data[(i + 1)*xres + j] - 2*data[i*xres + j])
                          + (data[i*xres + j - 1] + data[i*xres + j + 1] - 2*data[i*xres + j]));

                buffer[i*xres + j] += cor;
                cor = fabs(cor);
                if (cor > err)
                    err = cor;
            }
        }
    }

    gwy_data_field_copy(buffer_field, data_field, FALSE);
    gwy_data_field_invalidate(data_field);

    if (error)
        *error = err;
}

/***************************************************************************
 *
 * Efficient Laplace interpolation.
 *
 ***************************************************************************/

static gboolean
promote(const guint *levels, guint *buffer,
        guint xres, guint yres,
        guint level, guint step)
{
    guint nx = (xres + step-1)/step, ny = (yres + step-1)/step;
    guint vstep = xres*step;
    gboolean ok = FALSE;
    guint i, j;

    if (nx < 3 || ny < 3)
        return ok;

    for (i = 0; i < ny; i++) {
        for (j = 0; j < nx; j++) {
            guint k = (i*xres + j)*step;

            if (levels[k] == level
                && (!i || levels[k-vstep] == level)
                && (!j || levels[k-step] == level)
                && (j == nx-1 || levels[k+step] == level)
                && (i == ny-1 || levels[k+vstep] == level)) {
                buffer[k] = level+1;
                ok = TRUE;
            }
        }
    }

    return ok;
}

static void
demote(const guint *levels, guint *buffer,
       guint xres, guint yres,
       guint level, guint step)
{
    guint nx = (xres + step-1)/step, ny = (yres + step-1)/step;
    guint vstep = xres*step;
    guint i, j;

    if (nx < 3 || ny < 3)
        return;

    for (i = 1; i < ny-1; i++) {
        for (j = 1; j < nx-1; j++) {
            guint k = (i*xres + j)*step;

            if (levels[k] == level
                && (levels[k-vstep-step] == level-1 || levels[k-vstep] == level-1 || levels[k-vstep+step] == level-1
                    || levels[k-step] == level-1 || levels[k+step] == level-1 || levels[k+vstep-step] == level-1
                    || levels[k+vstep] == level-1 || levels[k+vstep+step] == level-1)) {
                if (buffer[k-vstep-step] > level)
                    buffer[k-vstep-step] = level;
                if (buffer[k-vstep] > level)
                    buffer[k-vstep] = level;
                if (buffer[k-vstep+step] > level)
                    buffer[k-vstep+step] = level;
                if (buffer[k-step] > level)
                    buffer[k-step] = level;
                if (buffer[k+step] > level)
                    buffer[k+step] = level;
                if (buffer[k+vstep-step] > level)
                    buffer[k+vstep-step] = level;
                if (buffer[k+vstep] > level)
                    buffer[k+vstep] = level;
                if (buffer[k+vstep+step] > level)
                    buffer[k+vstep+step] = level;
            }
        }
    }
}

static gboolean
reduce(const guint *levels, guint *buffer,
       guint xres, guint yres,
       guint level, guint step)
{
    guint nx = (xres + step-1)/step, ny = (yres + step-1)/step;
    guint halfstep = step/2;
    guint vstep = xres*step, vhalfstep = xres*halfstep;
    gboolean ok = FALSE;
    gboolean right = (nx - 1)*step + halfstep < xres;
    gboolean down = (ny - 1)*step + halfstep < yres;
    guint i, j;

    g_return_val_if_fail(step % 2 == 0, FALSE);

    if (nx < 3 || ny < 3)
        return ok;

    for (i = 0; i < ny; i++) {
        for (j = 0; j < nx; j++) {
            guint k = (i*xres + j)*step;

            if (levels[k] == level
                && (!i || !j || levels[k-vstep-step] >= level)
                && (!i || levels[k-vstep] == level)
                && (!i || j == nx-1 || levels[k-vstep+step] >= level)
                && (!j || levels[k-step] == level)
                && (j == nx-1 || levels[k+step] == level)
                && (i == ny-1 || !j || levels[k+vstep-step] >= level)
                && (i == ny-1 || levels[k+vstep] == level)
                && (i == ny-1 || j == nx-1 || levels[k+vstep+step] >= level)) {
                buffer[k] = level+1;
                if (i && j)
                    buffer[k-vhalfstep-halfstep] = NONE;
                if (i)
                    buffer[k-vhalfstep] = NONE;
                if (i && (right || j < nx-1))
                    buffer[k-vhalfstep+halfstep] = NONE;
                if (j)
                    buffer[k-halfstep] = NONE;
                if (right || j < nx-1)
                    buffer[k+halfstep] = NONE;
                if ((down || i < ny-1) && j)
                    buffer[k+vhalfstep-halfstep] = NONE;
                if (down || i < ny-1)
                    buffer[k+vhalfstep] = NONE;
                if ((down || i < ny-1) && (right || j < nx-1))
                    buffer[k+vhalfstep+halfstep] = NONE;
                ok = TRUE;
            }
        }
    }

    return ok;
}

static void
remove_spikes(guint *levels,
              guint xres, guint yres,
              guint level, guint step)
{
    guint nx = (xres + step-1)/step, ny = (yres + step-1)/step;
    guint i, j;

    if (nx < 3 || ny < 3)
        return;

    for (i = 1; i < ny-1; i++) {
        for (j = 1; j < nx-1; j++) {
            guint k = (i*xres + j)*step;

            if (levels[k] == level) {
                guint su = (levels[k-xres*step] == NONE), sd = (levels[k+xres*step] == NONE),
                      sl = (levels[k-step] == NONE), sr = (levels[k+step] == NONE);

                if ((su & sd & ~sl & ~sr) || (~su & ~sd & sl & sr))
                    levels[k] = NONE;
            }
        }
    }
}

static guint
build_levels(guint *levels, guint *buffer,
             guint xres, guint yres)
{
    guint step = 1, level = 0;

    gwy_assign(buffer, levels, xres*yres);
    while (TRUE) {
        // Promote odd levels to one-greater even levels if they do not touch lower-values levels.
        level++;
        if (!promote(levels, buffer, xres, yres, level, step))
            break;

        // Ensure a dense representation near the boundary.
        if (level == 1) {
            gwy_assign(levels, buffer, xres*yres);
            demote(levels, buffer, xres, yres, level, step);
        }

        gwy_assign(levels, buffer, xres*yres);
        // Clear the space around even levels and promote them to one-greater odd levels if the do not touch lower
        // levels.
        level++;
        step *= 2;
        if (!reduce(levels, buffer, xres, yres, level, step))
            break;

        // Remove even levels that would have to be interpolated from two opposide sides (they appear when both sides
        // of it are promoted but not the point itself).
        if (level > 1)
            remove_spikes(buffer, xres, yres, level, step/2);

        gwy_assign(levels, buffer, xres*yres);
    }

    return level;
}

static guint
count_grid_points(const guint *levels,
                  guint xres, guint yres)
{
    guint k, npoints = 0;

    for (k = 0; k < xres*yres; k++) {
        if (levels[k] && levels[k] != NONE)
            npoints++;
    }

    return npoints;
}

static void
build_grid_index(const guint *levels,
                 guint xres, guint yres,
                 guint *gindex,
                 guint *revindex)
{
    guint k, n = 0;

    for (k = 0; k < xres*yres; k++) {
        if (levels[k] && levels[k] != NONE) {
            revindex[k] = n;
            gindex[n++] = k;
        }
        else
            revindex[k] = NONE;
    }
}

static void
laplace_iterators_setup(LaplaceIterators *iterators,
                        guint maxneighbours)
{
    gsize len = iterators->len;

    iterators->k = iterators->n + (len + 2);
    iterators->iw = iterators->k + maxneighbours*len;
    iterators->gindex = iterators->iw + len;

    iterators->rhs = iterators->z + len;
    iterators->f = iterators->rhs + len;
    iterators->v = iterators->f + len;
    iterators->t = iterators->v + len;

    iterators->n[0] = 0;
    iterators->nw = 0;
}

static void
laplace_iterators_resize(LaplaceIterators *iterators,
                         guint len,
                         guint maxneighbours)
{
    guint wsize = COEFF_BLOCK_NTYPES*maxneighbours;
    gsize int_size = (maxneighbours + 3)*len + 2;
    gsize float_size = 5*len;

    if (int_size > iterators->int_size) {
        if (G_UNLIKELY(iterators->int_size))
            g_warning("Laplace iterators need to be enlarged (int).");
        GWY_FREE(iterators->n);
        iterators->n = g_new(guint, int_size);
        iterators->int_size = int_size;
    }

    if (float_size > iterators->float_size) {
        if (G_UNLIKELY(iterators->float_size))
            g_warning("Laplace iterators need to be enlarged (float).");
        GWY_FREE(iterators->z);
        iterators->z = g_new(gdouble, float_size);
        iterators->float_size = float_size;
    }

    if (wsize > iterators->wsize) {
        iterators->w = g_renew(gdouble, iterators->w, wsize);
        iterators->wsize = wsize;
    }
    if (!iterators->wlen) {
        iterators->wlensize = COEFF_BLOCK_NTYPES;
        iterators->wlen = g_new(guint, iterators->wlensize);
    }

    iterators->len = len;
    laplace_iterators_setup(iterators, maxneighbours);
}

static LaplaceIterators*
laplace_iterators_new(guint len, guint maxneighbours)
{
    LaplaceIterators *iterators = g_new0(LaplaceIterators, 1);
    laplace_iterators_resize(iterators, len, maxneighbours);
    return iterators;
}

static void
laplace_iterators_free(LaplaceIterators *iterators)
{
    g_free(iterators->z);
    g_free(iterators->n);
    g_free(iterators->w);
    g_free(iterators->wlen);
    g_free(iterators);
}

static void
analyse_neighbour_direction(const guint *levels,
                            const gdouble *data,
                            gint xres, gint yres,
                            gint xstep, gint ystep,
                            gint j, gint i,
                            const guint *revindex,
                            LaplaceNeighbour *nd)
{
    gint ineigh, jneigh, step;
    gint xorthostep, yorthostep;
    guint kk;

    gwy_clear(nd, 1);
    step = MAX(ABS(xstep), ABS(ystep));
    ineigh = i + ystep;
    jneigh = j + xstep;

    // 1 Primary neighbour.
    // 1.a Neumann boundary.
    // The upper and left boundaries are always aligned.
    if (ineigh < 0) {
        g_assert(i == 0);
        nd->is_boundary = TRUE;
        nd->step = step;
        return;
    }
    if (jneigh < 0) {
        g_assert(j == 0);
        nd->is_boundary = TRUE;
        nd->step = step;
        return;
    }
    // The other boundaries can be unaligned.
    if (ineigh >= yres) {
        nd->is_boundary = TRUE;
        nd->bdist = yres-1 - i;
        nd->step = step;
        return;
    }
    if (jneigh >= xres) {
        nd->is_boundary = TRUE;
        nd->bdist = xres-1 - j;
        nd->step = step;
        return;
    }

    kk = ineigh*xres + jneigh;

    // 1.b Dirichlet boundary.
    if (!levels[kk]) {
        g_assert(step == 1);
        nd->is_rhs = TRUE;
        nd->step = step;
        nd->rhs = data[kk];
        return;
    }

    // 1.c Interior.
    if (levels[kk] != NONE) {
        nd->neighbour = revindex[kk];
        nd->step = step;
        return;
    }

    // 2 Secondary neighbour.
    ineigh = i + 2*ystep;
    jneigh = j + 2*xstep;

    // 2.a Neumann boundary.
    // The upper and left boundaries are always aligned.
    if (ineigh < 0) {
        g_assert(i == 0);
        nd->is_boundary = TRUE;
        nd->step = 2*step;
        return;
    }
    if (jneigh < 0) {
        g_assert(j == 0);
        nd->is_boundary = TRUE;
        nd->step = 2*step;
        return;
    }
    // The other boundaries can be unaligned.
    if (ineigh >= yres) {
        nd->is_boundary = TRUE;
        nd->bdist = yres-1 - i;
        nd->step = 2*step;
        return;
    }
    if (jneigh >= xres) {
        nd->is_boundary = TRUE;
        nd->bdist = xres-1 - j;
        nd->step = 2*step;
        return;
    }

    kk = ineigh*xres + jneigh;
    g_assert(levels[kk]);    // Dirichlet boundary is always at step one.

    // 2.b Interior.
    if (levels[kk] != NONE) {
        nd->neighbour = revindex[kk];
        nd->step = 2*step;
        return;
    }

    // 3 Virtual neighbour.
    xorthostep = xstep ? 0 : ABS(ystep);
    yorthostep = ystep ? 0 : ABS(xstep);
    ineigh = i + 2*ystep - yorthostep;
    jneigh = j + 2*xstep - xorthostep;
    // The upper and left boundaries are always aligned.
    g_assert(ineigh >= 0);
    g_assert(jneigh >= 0);
    kk = ineigh*xres + jneigh;

    nd->is_virtual = TRUE;
    nd->neighbour = revindex[kk];
    nd->step = 2*step;  // The long distance; the short is always half of that.

    ineigh = i + 2*ystep + yorthostep;
    jneigh = j + 2*xstep + xorthostep;
    g_assert(ineigh < yres || jneigh < xres);
    if (ineigh < yres && jneigh < xres) {
        kk = ineigh*xres + jneigh;
        nd->neighbour2 = revindex[kk];
        g_assert(nd->neighbour2 != NONE);
    }
    else {
        nd->is_boundary = TRUE;
        if (ineigh >= yres)
            nd->bdist = yres-1 - i;
        else
            nd->bdist = xres-1 - j;
    }
}

static void
calculate_weights(LaplaceNeighbour *nd)
{
    LaplaceDirection virtual_dir = NDIRECTIONS;
    guint i, ii, ileft, iright, j, boundary_dir;
    gboolean virtual_is_boundary;

    // At most one virtual direction.
    for (j = 0; j < NDIRECTIONS; j++) {
        if (nd[j].is_virtual) {
            g_assert(virtual_dir == NDIRECTIONS);
            virtual_dir = j;
        }
    }

    // No virtual, no mixing of z_xx and z_yy.
    if (virtual_dir == NDIRECTIONS) {
        for (j = 0; j < NDIRECTIONS; j++) {
            guint jj = (j + 2) % NDIRECTIONS;
            gdouble s, xs;

            if (nd[j].is_boundary)
                continue;

            s = nd[j].step;
            xs = nd[jj].is_boundary ? 2*nd[jj].bdist : nd[jj].step;
            nd[j].weight = 2.0/(s + xs)/s;
        }
        return;
    }

    // Virtual.
    i = virtual_dir;
    iright = (i + 1) % NDIRECTIONS;
    ii = (i + 2) % NDIRECTIONS;
    ileft = (i + 3) % NDIRECTIONS;
    boundary_dir = NDIRECTIONS;
    virtual_is_boundary = nd[i].is_boundary;

    // At most one boundary direction, except the boundary direction itself.
    for (j = 0; j < NDIRECTIONS; j++) {
        if (j != virtual_dir && nd[j].is_boundary) {
            g_assert(boundary_dir == NDIRECTIONS);
            boundary_dir = j;
        }
    }
    g_assert(!virtual_is_boundary || (boundary_dir == iright || boundary_dir == ileft));
    if (boundary_dir == NDIRECTIONS) {
        gdouble s = nd[i].step, ss = nd[ii].step;
        gdouble sleft = nd[ileft].step, sright = nd[iright].step;
        gdouble w = 1.0 - 0.25*s/(s + ss);
        nd[i].weight = nd[i].weight2 = 1.0/(s + ss)/s;
        nd[ii].weight = 2.0/(s + ss)/ss;
        nd[ileft].weight = 2.0*w/(sleft + sright)/sleft;
        nd[iright].weight = 2.0*w/(sleft + sright)/sright;
    }
    else if (boundary_dir == ii) {
        gdouble s = nd[i].step;
        gdouble sleft = nd[ileft].step, sright = nd[iright].step;
        gdouble b = nd[boundary_dir].bdist;
        gdouble w = 1.0 - 0.25*s/(s + 2*b);
        nd[i].weight = nd[i].weight2 = 1.0/(s + 2*b)/s;
        nd[ileft].weight = 2.0*w/(sleft + sright)/sleft;
        nd[iright].weight = 2.0*w/(sleft + sright)/sright;
    }
    else {
        guint irem = (boundary_dir + 2) % NDIRECTIONS;
        gdouble s = nd[i].step, ss = nd[ii].step;
        gdouble srem = nd[irem].step;
        gdouble b = nd[boundary_dir].bdist;
        gdouble w = 1.0 - 0.25*(s + 4*b)/(s + ss);
        nd[i].weight = 2.0/(s + ss)/s;
        nd[ii].weight = 2.0/(s + ss)/ss;
        nd[irem].weight = 2.0*w/(srem + 2*b)/srem;
    }
}

static guint
add_unique_coeff_block(LaplaceIterators *iterators,
                       const gdouble *w, guint wlen)
{
    guint i, j, k;
    gboolean matching = FALSE;

    /* Try to find the w block among existing ones.  Of course, we only need to check blocks of the same size. */
    for (i = j = 0; i < iterators->nw; i++) {
        if (iterators->wlen[i] == wlen) {
            matching = TRUE;
            for (k = 0; k < wlen; k++) {
                /* Do not allow rounding errors to disrupt the matching. The weights are simple fractions of unity so
                 * we can use a fixed epsilon here. */
                if (fabs(iterators->w[j+k] - w[k]) > 1e-14) {
                    matching = FALSE;
                    break;
                }
            }
            if (matching)
                return j;
        }
        j += iterators->wlen[i];
    }

    /* Add w as a new block. */
    if (G_UNLIKELY(j + wlen > iterators->wsize)) {
        iterators->wsize += MAX(wlen, 64);
        iterators->w = g_renew(gdouble, iterators->w, iterators->wsize);
        g_warning("Resizing w[] to %u.", iterators->wsize);
    }
    if (G_UNLIKELY(iterators->nw == iterators->wlensize)) {
        iterators->wlensize += 8;
        iterators->wlen = g_renew(guint, iterators->wlen, iterators->wlensize);
        g_warning("Resizing wlen[] to %u.", iterators->wlensize);
    }
    gwy_assign(iterators->w + j, w, wlen);
    iterators->wlen[iterators->nw++] = wlen;

    return j;
}

static void
build_iterator(LaplaceNeighbour *nd,
               LaplaceIterators *iterators,
               guint ipt,
               gdouble *nrhs,
               gdouble *rhssum)
{
    guint i, nneigh, start, npt = 0;
    gdouble ws = 0.0, rs = 0.0;
    gboolean sorted = FALSE;
    guint *k;
    gdouble w[5];

    // Figure out how many neighbours we have and sum the weights.
    for (i = 0; i < NDIRECTIONS; i++) {
        if (nd[i].weight) {
            ws += nd[i].weight;
            if (nd[i].is_rhs) {
                g_assert(!nd[i].is_virtual);
                g_assert(!nd[i].is_boundary);
                rs += nd[i].rhs;
                *nrhs += nd[i].weight;
            }
            else
                npt++;

            if (nd[i].weight2) {
                g_assert(nd[i].is_virtual);
                ws += nd[i].weight2;
                npt++;
            }
        }
    }
    g_assert(npt > 0 && npt <= 5);

    start = iterators->n[ipt];
    iterators->n[ipt+1] = start + npt;
    if (rs) {
        *rhssum += rs;
        iterators->rhs[ipt] = rs/ws;
    }
    else
        iterators->rhs[ipt] = 0.0;

    // Create the iterators.
    k = iterators->k + start;
    for (i = nneigh = 0; i < NDIRECTIONS; i++) {
        if (!nd[i].is_rhs && nd[i].weight) {
            w[nneigh] = nd[i].weight/ws;
            k[nneigh] = nd[i].neighbour;
            nneigh++;
            if (nd[i].weight2) {
                w[nneigh] = nd[i].weight2/ws;
                k[nneigh] = nd[i].neighbour2;
                nneigh++;
            }
        }
    }

    // Sort the segments by k.
    do {
        sorted = TRUE;
        for (i = 1; i < npt; i++) {
            if (k[i-1] > k[i]) {
                GWY_SWAP(guint, k[i-1], k[i]);
                GWY_SWAP(gdouble, w[i-1], w[i]);
                sorted = FALSE;
            }
        }
    } while (!sorted);

    iterators->iw[ipt] = add_unique_coeff_block(iterators, w, nneigh);
}

static void
build_sparse_iterators(LaplaceIterators *iterators,
                       guint *revindex,
                       const guint *levels,
                       const gdouble *data,
                       guint xres, guint yres)
{
    LaplaceNeighbour nd[NDIRECTIONS];
    guint len = count_grid_points(levels, xres, yres);
    gdouble rhssum = 0.0, nrhs = 0.0;
    const guint *gindex;
    guint ipt;

    laplace_iterators_resize(iterators, len, 5);
    build_grid_index(levels, xres, yres, iterators->gindex, revindex);

    gindex = iterators->gindex;

    for (ipt = 0; ipt < len; ipt++) {
        guint k = gindex[ipt], i = k/xres, j = k % xres;
        gint step = 1 << ((levels[k] - 1)/2);

        analyse_neighbour_direction(levels, data, xres, yres, 0, -step, j, i, revindex, nd + UP);
        analyse_neighbour_direction(levels, data, xres, yres, step, 0, j, i, revindex, nd + RIGHT);
        analyse_neighbour_direction(levels, data, xres, yres, 0, step, j, i, revindex, nd + DOWN);
        analyse_neighbour_direction(levels, data, xres, yres, -step, 0, j, i, revindex, nd + LEFT);

        calculate_weights(nd);
        build_iterator(nd, iterators, ipt, &nrhs, &rhssum);
    }

    // Initialise with the mean value of right hand sides, including multiplicity.
    rhssum /= nrhs;
    for (ipt = 0; ipt < len; ipt++)
        iterators->z[ipt] = rhssum;
}

static void
build_dense_iterators(LaplaceIterators *iterators,
                      guint *revindex,
                      const guint *levels,
                      const gdouble *data,
                      guint xres, guint yres)
{
    guint len = count_grid_points(levels, xres, yres);
    const guint *gindex;
    guint ipt;

    laplace_iterators_resize(iterators, len, 4);
    build_grid_index(levels, xres, yres, iterators->gindex, revindex);

    gindex = iterators->gindex;
    for (ipt = 0; ipt < len; ipt++) {
        guint pos = gindex[ipt], i = pos/xres, j = pos % xres;
        guint nneigh = 0, ws = 0, kk;
        gdouble rs = 0.0;
        guint *k = iterators->k + iterators->n[ipt];
        gdouble w[4];

        if (i) {
            kk = pos-xres;
            ws++;
            if (levels[kk]) {
                k[nneigh] = revindex[kk];
                nneigh++;
            }
            else
                rs += data[kk];
        }
        if (j) {
            kk = pos-1;
            ws++;
            if (levels[kk]) {
                k[nneigh] = revindex[kk];
                nneigh++;
            }
            else
                rs += data[kk];
        }
        if (j < xres-1) {
            kk = pos+1;
            ws++;
            if (levels[kk]) {
                k[nneigh] = revindex[kk];
                nneigh++;
            }
            else
                rs += data[kk];
        }
        if (i < yres-1) {
            kk = pos+xres;
            ws++;
            if (levels[kk]) {
                k[nneigh] = revindex[kk];
                nneigh++;
            }
            else
                rs += data[kk];
        }

        iterators->z[ipt] = data[pos];
        iterators->rhs[ipt] = rs/ws;
        iterators->n[ipt+1] = iterators->n[ipt] + nneigh;
        for (j = 0; j < nneigh; j++)
            w[j] = 1.0/ws;
        iterators->iw[ipt] = add_unique_coeff_block(iterators, w, nneigh);
    }
}

static void
calculate_f(LaplaceIterators *iterators)
{
    const guint *n = iterators->n, *k = iterators->k, *iw = iterators->iw;
    const gdouble *z = iterators->z, *rhs = iterators->rhs, *w = iterators->w;
    gdouble *f = iterators->f;
    guint ipt, len = iterators->len;

#ifdef _OPENMP
#pragma omp parallel for if(len >= MIN_SIZE_FOR_OMP && gwy_threads_are_enabled()) default(none) \
            private(ipt) \
            shared(n,k,iw,z,rhs,f,w,len)
#endif
    for (ipt = 0; ipt < len; ipt++) {
        guint nn = n[ipt+1] - n[ipt];
        const guint *kk = k + n[ipt];
        const gdouble *ww = w + iw[ipt];
        gdouble lhs = 0.0;
        guint j;

        for (j = 0; j < nn; j++)
            lhs += z[kk[j]]*ww[j];
        f[ipt] = (z[ipt] - lhs) - rhs[ipt];
    }
}

static void
iterate_simple(LaplaceIterators *iterators)
{
    const gdouble *f = iterators->f;
    gdouble *z = iterators->z;
    guint ipt, len = iterators->len;

    /* Too trivial to parallelise. */
    for (ipt = 0; ipt < len; ipt++)
        z[ipt] -= 0.8*f[ipt];
}

static void
matrix_multiply(LaplaceIterators *iterators, gdouble *pphi, gdouble *pS)
{
    const guint *n = iterators->n, *k = iterators->k, *iw = iterators->iw;
    const gdouble *v = iterators->v, *f = iterators->f, *w = iterators->w;
    gdouble *t = iterators->t;
    gdouble phi = 0.0, S = 0.0;
    guint ipt, len = iterators->len;

#ifdef _OPENMP
#pragma omp parallel for if(len >= MIN_SIZE_FOR_OMP && gwy_threads_are_enabled()) default(none) \
            reduction(+:phi,S) \
            private(ipt) \
            shared(n,iw,v,t,f,w,k,len)
#endif
    for (ipt = 0; ipt < len; ipt++) {
        guint nn = n[ipt+1] - n[ipt];
        const guint *kk = k + n[ipt];
        const gdouble *ww = w + iw[ipt];
        gdouble s = 0.0;
        guint j;

        for (j = 0; j < nn; j++)
            s += v[kk[j]]*ww[j];
        t[ipt] = v[ipt] - s;
        S += v[ipt]*t[ipt];
        phi += v[ipt]*f[ipt];
    }

    *pphi = phi;
    *pS = S;
}

static gboolean
iterate_conj_grad(LaplaceIterators *iterators)
{
    gdouble *z = iterators->z, *v = iterators->v, *t = iterators->t, *f = iterators->f;
    gdouble S, phi, phiS;
    guint ipt, len = iterators->len;

    // Temporary quantities: t = A.v, S = v.t, φ = v.f
    matrix_multiply(iterators, &phi, &S);

    if (S <= 1e-20*phi)
        return TRUE;

    // New value and f = A.z-b
    // And new v
    phiS = phi/S;
    phi = 0.0;
#ifdef _OPENMP
#pragma omp parallel for if(len >= MIN_SIZE_FOR_OMP && gwy_threads_are_enabled()) default(none) \
            reduction(+:phi) \
            private(ipt) \
            shared(v,t,f,z,len,phiS)
#endif
    for (ipt = 0; ipt < len; ipt++) {
        z[ipt] -= phiS*v[ipt];
        f[ipt] -= phiS*t[ipt];
        phi += t[ipt]*f[ipt];
    }

    phiS = phi/S;
    /* Too trivial to parallelise. */
    for (ipt = 0; ipt < len; ipt++)
        v[ipt] = f[ipt] - phiS*v[ipt];

    return FALSE;
}

static void
move_result_to_data(const LaplaceIterators *iterators, gdouble *data)
{
    guint ipt;

    for (ipt = 0; ipt < iterators->len; ipt++)
        data[iterators->gindex[ipt]] = iterators->z[ipt];
}

static void
interpolate(guint *levels, gdouble *data, guint xres, guint yres, guint step)
{
    guint nx = (xres + step-1)/step, ny = (yres + step-1)/step, vstep = xres*step;
    guint i, j;

    if (nx < 3 || ny < 3)
        return;

    // Six-point interpolation
    for (i = 0; i < ny; i++) {
        if (i % 2 == 0) {
            // Interpolated point horizontally in between two other points.
            for (j = 1; j < nx; j += 2) {
                guint k = (i*xres + j)*step;
                if (levels[k] != NONE)
                    continue;

                if (i >= 2 && i < ny-2 && j < nx-1) {
                    data[k] = (0.375*(data[k-step] + data[k+step])
                               + 0.0625*(data[k-2*vstep-step] + data[k-2*vstep+step]
                                         + data[k+2*vstep-step] + data[k+2*vstep+step]));
                    levels[k] = (levels[k-step] + levels[k+step])/2;
                }
                else if (j < nx-1 && i < ny-2) {
                    // Upper boundary is aligned.
                    data[k] = (0.375*(data[k-step] + data[k+step])
                               + 0.125*(data[k+2*vstep-step] + data[k+2*vstep+step]));
                    levels[k] = (levels[k-step] + levels[k+step])/2;
                }
                else if (j < nx-1 && i >= 2) {
                    // Lower boundary can be unaligned.
                    guint bdist = yres-1 - i*step;
                    guint a = 4*bdist + 3*step, b = step, d = 8*(bdist + step);
                    data[k] = (a*(data[k-step] + data[k+step]) + b*(data[k-2*vstep-step] + data[k-2*vstep+step]))/d;
                    levels[k] = (levels[k-step] + levels[k+step])/2;
                }
                else if (i >= 2 && i < ny-2) {
                    // Right boundary can be unaligned.
                    guint bdist = xres-1 - j*step;
                    guint a = 6*step - 4*bdist, b = 2*bdist + step, d = 8*step;
                    data[k] = (a*data[k-step] + b*(data[k-2*vstep-step] + data[k+2*vstep-step]))/d;
                    levels[k] = levels[k-step];
                }
                else if (i < ny-2) {
                    // Upper boundary is aligned, right boundary can be
                    // unaligned.
                    guint bdist = xres-1 - j*step;
                    guint a = 3*step - 2*bdist, b = 2*bdist + step, d = 4*step;
                    data[k] = (a*data[k-step] + b*data[k-step+2*vstep])/d;
                    levels[k] = levels[k-step];
                }
                else if (i >= 2) {
                    // Lower and right boundaries can be both unaligned.
                    guint xbdist = xres-1 - j*step;
                    guint ybdist = yres-1 - i*step;
                    guint a = 3*step + 4*ybdist - 2*xbdist, b = 2*xbdist + step;
                    data[k] = (a*data[k-step] + b*data[k-2*vstep])/(a + b);
                    levels[k] = levels[k-step];
                }
                else {
                    g_assert_not_reached();
                }
            }
        }
        else {
            // Interpolated point vertically in between two other points.
            for (j = 0; j < nx; j += 2) {
                guint k = (i*xres + j)*step;
                if (levels[k] != NONE)
                    continue;

                if (j >= 2 && j < nx-2 && i < ny-1) {
                    data[k] = (0.375*(data[k-vstep] + data[k+vstep])
                               + 0.0625*(data[k-vstep-2*step] + data[k-vstep+2*step]
                                         + data[k+vstep-2*step] + data[k+vstep+2*step]));
                    levels[k] = (levels[k-vstep] + levels[k+vstep])/2;
                }
                else if (j < nx-2 && i < ny-1) {
                    // Left boundary is aligned.
                    data[k] = (0.375*(data[k-vstep] + data[k+vstep])
                               + 0.125*(data[k-vstep+2*step] + data[k+vstep+2*step]));
                    levels[k] = (levels[k-vstep] + levels[k+vstep])/2;
                }
                else if (j >= 2 && i < ny-1) {
                    // Right boundary can be unaligned.
                    guint bdist = xres-1 - j*step;
                    guint a = 4*bdist + 3*step, b = step, d = 8*(bdist + step);
                    data[k] = (a*(data[k-vstep] + data[k+vstep]) + b*(data[k-vstep-2*step] + data[k+vstep-2*step]))/d;
                    levels[k] = (levels[k-vstep] + levels[k+vstep])/2;
                }
                else if (j >= 2 && j < nx-2) {
                    // Lower boundary can be unaligned.
                    guint bdist = yres-1 - i*step;
                    guint a = 6*step - 4*bdist, b = 2*bdist + step, d = 8*step;
                    data[k] = (a*data[k-vstep] + b*(data[k-vstep-2*step] + data[k-vstep+2*step]))/d;
                    levels[k] = levels[k-vstep];
                }
                else if (j < nx-2) {
                    // Left boundary is aligned, lower boundary can be
                    // unaligned.
                    guint bdist = yres-1 - i*step;
                    guint a = 3*step - 2*bdist, b = 2*bdist + step, d = 4*step;
                    data[k] = (a*data[k-vstep] + b*data[k+2*step-vstep])/d;
                    levels[k] = levels[k-vstep];
                }
                else if (j >= 2) {
                    // Lower and right boundaries can be both unaligned.
                    guint xbdist = xres-1 - j*step;
                    guint ybdist = yres-1 - i*step;
                    guint a = 3*step + 4*xbdist - 2*ybdist, b = 2*ybdist + step;
                    data[k] = (a*data[k-vstep] + b*data[k-2*step])/(a + b);
                    levels[k] = levels[k-vstep];
                }
                else {
                    g_assert_not_reached();
                }
            }
        }
    }

    // Four-point interpolation
    for (i = 1; i < ny; i += 2) {
        for (j = 1; j < nx; j += 2) {
            guint k = (i*xres + j)*step;
            if (levels[k] != NONE)
                continue;

            if (i < ny-1 && j < nx-1) {
                data[k] = 0.25*(data[k-vstep] + data[k+vstep] + data[k-step] + data[k+step]);
                levels[k] = (levels[k-vstep] + levels[k+vstep] + levels[k-step] + levels[k+step])/4;
            }
            else if (i < ny-1) {
                // Right boundary can be unaligned.
                guint bdist = xres-1 - j*step;
                guint a = 2*bdist + step, b = 2*step, d = 4*(bdist + step);
                data[k] = (a*(data[k-vstep] + data[k+vstep]) + b*data[k-step])/d;
                levels[k] = (levels[k-vstep] + levels[k+vstep])/2;
            }
            else if (j < nx-1) {
                // Lower boundary can be unaligned.
                guint bdist = yres-1 - i*step;
                guint a = 2*bdist + step, b = 2*step, d = 4*(bdist + step);
                data[k] = (a*(data[k-step] + data[k+step]) + b*data[k-vstep])/d;
                levels[k] = (levels[k-step] + levels[k+step])/2;
            }
            else {
                // Right and lower boundary can be unaligned both.
                guint xbdist = xres-1 - j*step;
                guint ybdist = yres-1 - i*step;
                guint a = 2*ybdist + step, b = 2*xbdist + step;
                data[k] = (a*data[k-step] + b*data[k-vstep])/(a + b);
                levels[k] = (levels[k-step] + levels[k-vstep])/2;
            }
        }
    }
}

static void
reconstruct(guint *levels, gdouble *data, guint xres, guint yres, guint level)
{
    guint step = 1 << ((level - 1)/2);

    while (step) {
        interpolate(levels, data, xres, yres, step);
        step /= 2;
    }
}

static void
init_data_simple(gdouble *data, guint *levels, guint xres, guint yres)
{
    guint i, j, kk, level = 1;
    gboolean finished = FALSE;

    for (kk = 0; kk < xres*yres; kk++)
        levels[kk] = !!levels[kk];

    while (!finished) {
        finished = TRUE;
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres; j++) {
                guint k = i*xres + j, n = 0;
                gdouble s = 0;

                if (levels[k] != level)
                    continue;

                if (i && levels[k-xres] < level) {
                    s += data[k-xres];
                    n++;
                }
                if (j && levels[k-1] < level) {
                    s += data[k-1];
                    n++;
                }
                if (j+1 < xres && levels[k+1] < level) {
                    s += data[k+1];
                    n++;
                }
                if (i+1 < yres && levels[k+xres] < level) {
                    s += data[k+xres];
                    n++;
                }

                if (n) {
                    data[k] = s/n;
                }
                else {
                    levels[k] = level+1;
                    finished = FALSE;
                }
            }
        }
        level++;
    }
}

static void
laplace_sparse(LaplaceIterators *iterators,
               guint *revindex,
               gdouble *data, guint *levels, guint xres, guint yres,
               guint nconjgrad, guint nsimple)
{
    // Revindex is filled later, use is as a temporary xres*yres-sized buffer.
    guint maxlevel = build_levels(levels, revindex, xres, yres);
    gboolean finished = FALSE;
    guint iter;

    if (maxlevel < 3) {
        // If the grain is nowhere thick just init the interior using boundary conditions and continue with dense
        // iteration.  Note for single-pixel grains init_data_simple() already produces the solution.
        init_data_simple(data, levels, xres, yres);
        return;
    }

    build_sparse_iterators(iterators, revindex, levels, data, xres, yres);
    calculate_f(iterators);
    gwy_assign(iterators->v, iterators->f, iterators->len);

    for (iter = 0; iter < nconjgrad; iter++) {
        if ((finished = iterate_conj_grad(iterators)))
            break;
    }
    if (!finished) {
        for (iter = 0; iter < nsimple; iter++) {
            calculate_f(iterators);
            iterate_simple(iterators);
        }
    }
    move_result_to_data(iterators, data);
    reconstruct(levels, data, xres, yres, maxlevel);
}

static void
laplace_dense(LaplaceIterators *iterators,
              guint *revindex,
              gdouble *data, guint *levels, guint xres, guint yres,
              guint nconjgrad, guint nsimple)
{
    gboolean finished = FALSE;
    guint iter;

    build_dense_iterators(iterators, revindex, levels, data, xres, yres);
    calculate_f(iterators);
    gwy_assign(iterators->v, iterators->f, iterators->len);
    for (iter = 0; iter < nconjgrad; iter++) {
        if ((finished = iterate_conj_grad(iterators)))
            break;
    }
    if (!finished) {
        for (iter = 0; iter < nsimple; iter++) {
            calculate_f(iterators);
            iterate_simple(iterators);
        }
    }
    move_result_to_data(iterators, data);
}

// Extract grain data from full-sized @grains and @data to workspace-sized @levels and @z.
// The level is simply initialised to 1 for inside grain and 0 for outside.
static void
extract_grain(const gint *grains,
              const gdouble *data,
              guint xres,
              const GwyDataFieldPart *fpart,
              gint grain_id,
              guint *levels,
              gdouble *z)
{
    guint i, j;
    const gint *grow;
    guint *lrow;

    for (i = 0; i < fpart->height; i++) {
        gwy_assign(z + i*fpart->width, data + (i + fpart->row)*xres + fpart->col, fpart->width);
        grow = grains + (i + fpart->row)*xres + fpart->col;
        lrow = levels + i*fpart->width;
        for (j = fpart->width; j; j--, lrow++, grow++)
            *lrow = (*grow == grain_id);
    }
}

// Put interpolated grain data @z back to @data.
static void
insert_grain(const gint *grains,
             gdouble *data,
             guint xres,
             const GwyDataFieldPart *fpart,
             gint grain_id,
             const gdouble *z)
{
    guint i, j;

    for (i = 0; i < fpart->height; i++) {
        const gdouble *zrow = z + i*fpart->width;
        const gint *grow = grains + (i + fpart->row)*xres + fpart->col;
        gdouble *drow = data + (i + fpart->row)*xres + fpart->col;
        for (j = fpart->width; j; j--, zrow++, drow++, grow++) {
            if (*grow == grain_id)
                *drow = *zrow;
        }
    }
}

static void
enlarge_field_part(GwyDataFieldPart *fpart, guint xres, guint yres)
{
    if (fpart->col) {
        fpart->col--;
        fpart->width++;
    }
    if (fpart->col + fpart->width < xres)
        fpart->width++;

    if (fpart->row) {
        fpart->row--;
        fpart->height++;
    }
    if (fpart->row + fpart->height < yres)
        fpart->height++;
}

/*
 * Find the largest
 * - grain size in the terms of pixels: this is the number of iterators for dense iteration
 * - grain size in the terms of extended bounding box (i.e. bounding box including one more line of pixels to each
 *   side, if possible): this is the size of levels, revindex and data arrays.
 */
static void
find_largest_sizes(guint xres, guint yres,
                   const GwyDataFieldPart *bboxes,
                   const guint *sizes,
                   guint gfrom, guint gto,
                   guint *size,
                   guint *bboxsize)
{
    guint gno, bs;
    GwyDataFieldPart bbox;

    *size = *bboxsize = 0;
    for (gno = gfrom; gno <= gto; gno++) {
        if (sizes[gno] > *size)
            *size = sizes[gno];

        bbox = bboxes[gno];
        enlarge_field_part(&bbox, xres, yres);
        bs = bbox.width * bbox.height;
        if (bs > *bboxsize)
            *bboxsize = bs;
    }
}

static void
handle_1x1_grain(gint width, gint height, gdouble *z, const gint *levels)
{
    gint i, j, k, n;
    gdouble s;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            k = i*width + j;
            if (!levels[k])
                continue;

            s = 0.0;
            n = 0;
            if (i) {
                s += z[k-width];
                n++;
            }
            if (j) {
                s += z[k-1];
                n++;
            }
            if (j < width-1) {
                s += z[k+1];
                n++;
            }
            if (i < height-1) {
                s += z[k+width];
                n++;
            }
            z[k] = s/n;
            return;
        }
    }
    g_assert_not_reached();
}

static gboolean
handle_thin_grain(gint width, gint height, gdouble *z)
{
    GwyOrientation orientation = (height-2 == 1 ? GWY_ORIENTATION_HORIZONTAL : GWY_ORIENTATION_VERTICAL);
    gint n = (orientation == GWY_ORIENTATION_VERTICAL ? height-2 : width-2);
    gdouble *storage = g_new(gdouble, 4*n - 2);
    gdouble *d = storage, *a = d + n, *b = a + n-1, *rhs = b + n-1;
    gboolean ok = FALSE;
    gint i;

    for (i = 0; i < n-1; i++) {
        d[i] = 4.0;
        a[i] = b[i] = -1.0;
    }
    d[n-1] = 4.0;

    if (orientation == GWY_ORIENTATION_VERTICAL) {
        /* We know width == 3 */
        g_assert(width == 3);
        rhs[0] = z[1] + z[3] + z[5];
        for (i = 1; i < n-1; i++)
            rhs[i] = z[3*i + 3] + z[3*i + 5];
        rhs[n-1] = z[3*n] + z[3*n + 2] + z[3*n + 4];
    }
    else {
        rhs[0] = z[1] + z[width] + z[2*width + 1];
        for (i = 1; i < n-1; i++)
            rhs[i] = z[i+1] + z[2*width + i+1];
        rhs[n-1] = z[n] + z[2*width - 1] + z[2*width + n];
    }

    if (!gwy_math_tridiag_solve_rewrite(n, d, a, b, rhs))
        goto end;

    ok = TRUE;
    if (orientation == GWY_ORIENTATION_VERTICAL) {
        for (i = 0; i < n; i++)
            z[3*i + 4] = rhs[i];
    }
    else
        gwy_assign(z + width+1, rhs, n);

end:
    g_free(storage);

    return ok;
}

static void
handle_3px_grain(gdouble *z, const gint *levels)
{
    if (!levels[5]) {
        z[10] = (2*(z[11] + z[14]) + z[5] + 0.5*(z[2] + z[7] + z[8] + z[13]))/7.0;
        z[6] = 0.25*(z[2] + z[5] + z[7] + z[10]);
        z[9] = 0.25*(z[5] + z[8] + z[10] + z[13]);
    }
    else if (!levels[6]) {
        z[9] = (2*(z[8] + z[13]) + z[6] + 0.5*(z[1] + z[4] + z[11] + z[14]))/7.0;
        z[5] = 0.25*(z[1] + z[4] + z[6] + z[9]);
        z[10] = 0.25*(z[6] + z[9] + z[11] + z[14]);
    }
    else if (!levels[9]) {
        z[6] = (2*(z[2] + z[7]) + z[9] + 0.5*(z[1] + z[4] + z[11] + z[14]))/7.0;
        z[5] = 0.25*(z[1] + z[4] + z[6] + z[9]);
        z[10] = 0.25*(z[6] + z[9] + z[11] + z[14]);
    }
    else {
        z[5] = (2*(z[1] + z[4]) + z[10] + 0.5*(z[2] + z[7] + z[8] + z[13]))/7.0;
        z[6] = 0.25*(z[2] + z[5] + z[7] + z[10]);
        z[9] = 0.25*(z[5] + z[8] + z[10] + z[13]);
    }
}

static gboolean
handle_tiny_grain(gint width, gint height, gdouble *z, const gint *levels,
                  gint size, const GwyDataFieldPart *origbbox)
{
    /* Handle 1×1 grains directly, even if they touch the boundaries. */
    if (origbbox->width == 1 && origbbox->height == 1) {
        handle_1x1_grain(width, height, z, levels);
        return TRUE;
    }
    /* Otherwise consider only grains fully inside the image (not touching the boundaries). The special-casing gets
     * messy on the boundaries and tiny boundary grains should form a negligible subset of all grains. */
    if (height != origbbox->height + 2 || width != origbbox->width + 2)
        return FALSE;

    /* Thin grains 1×N or M×1. */
    if (origbbox->height == 1 || origbbox->width == 1)
        return handle_thin_grain(width, height, z);

    /* L-shaped three-pixel grains (these are the only non-linear). */
    if (size == 3) {
        g_assert(width == 4 && height == 4);
        handle_3px_grain(z, levels);
        return TRUE;
    }

    return FALSE;
}

/**
 * gwy_data_field_laplace_solve:
 * @field: A two-dimensional data field.
 * @mask: A two-dimensional data field containing mask defining the areas to interpolate.
 * @grain_id: The id number of the grain to replace with the solution of Laplace equation, from 1 to @ngrains (see
 *            gwy_data_field_grain_numbers()).  Passing 0 means to replace the entire empty space outside grains while
 *            passing a negative value means to replace the entire masked area.
 * @qprec: Speed-accuracy tuning parameter.  Pass 1.0 for the default that is fast and sufficiently precise.
 *
 * Replaces masked areas by the solution of Laplace equation.
 *
 * The boundary conditions on mask boundaries are Dirichlet with values given by pixels on the outer boundary of the
 * masked area.  Boundary conditions at field edges are Neumann conditions ∂z/∂n=0 where n denotes the normal to the
 * edge.  If entire area of @field is to be replaced the problem is underspecified; @field will be filled with zeros.
 *
 * For the default value of @qprec the the result should be good enough for any image processing purposes with the
 * typical local error of order 10⁻⁵ for very large grains and possibly much smaller for small grains.  You can lower
 * @qprec down to about 0.3 or even 0.2 if speed is crucial and some precision can be sacrificed.  Below that the
 * result just starts becoming somewhat worse for not much speed increase.  Conversely, you may wish to increase
 * @qprec up to 3 or even 5 if accuracy is important and you can afford the increased computation time.
 *
 * Since: 2.47
 **/
void
gwy_data_field_laplace_solve(GwyDataField *field,
                             GwyDataField *mask,
                             gint grain_id,
                             gdouble qprec)
{
    GwyDataField *ourmask;
    guint xres, yres, maxsize, maxbboxsize;
    gint ngrains, gfrom, gto;
    GwyDataFieldPart *bboxes;
    LaplaceIterators *iterators;
    guint *levels, *revindex;
    gint *grains;
    guint *sizes;
    gdouble *z;

    g_return_if_fail(GWY_IS_DATA_FIELD(mask));
    g_return_if_fail(GWY_IS_DATA_FIELD(field));
    g_return_if_fail(mask->xres == field->xres);
    g_return_if_fail(mask->yres == field->yres);

    // To fill the entire empty space we need to divide it to grains too so work with the inverted mask.
    if (grain_id == 0) {
        ourmask = gwy_data_field_duplicate(mask);
        gwy_data_field_grains_invert(ourmask);
        grain_id = -1;
    }
    else
        ourmask = g_object_ref((gpointer)mask);

    xres = field->xres;
    yres = field->yres;
    grains = g_new0(gint, xres*yres);
    ngrains = gwy_data_field_number_grains(ourmask, grains);
    if (grain_id > ngrains) {
        g_free(grains);
        g_return_if_fail(grain_id <= ngrains);
    }

    bboxes = (GwyDataFieldPart*)gwy_data_field_get_grain_bounding_boxes(ourmask, ngrains, grains, NULL);
    sizes = (guint*)gwy_data_field_get_grain_sizes(ourmask, ngrains, grains, NULL);

    // The underspecified case.
    if (ngrains == 1 && sizes[1] == xres*yres) {
        gwy_data_field_clear(field);
        g_object_unref(ourmask);
        g_free(sizes);
        g_free(bboxes);
        g_free(grains);
        return;
    }

    gfrom = (grain_id < 0) ? 1 : grain_id;
    gto = (grain_id < 0) ? ngrains : grain_id;

    // Allocate everything at the maximum size to avoid reallocations.
    find_largest_sizes(xres, yres, bboxes, sizes, gfrom, gto, &maxsize, &maxbboxsize);

    levels = g_new(guint, maxbboxsize);
    revindex = g_new(guint, maxbboxsize);
    z = g_new(gdouble, maxbboxsize);
    iterators = laplace_iterators_new(maxsize, 5);
    for (grain_id = gfrom; grain_id <= gto; grain_id++) {
        GwyDataFieldPart bbox = bboxes[grain_id], origbbox = bbox;
        enlarge_field_part(&bbox, xres, yres);
        extract_grain(grains, field->data, xres, &bbox, grain_id, levels, z);
        if (!handle_tiny_grain(bbox.width, bbox.height, z, levels, sizes[grain_id], &origbbox)) {
            laplace_sparse(iterators, revindex, z, levels, bbox.width, bbox.height, 60*qprec, 20*qprec);
            laplace_dense(iterators, revindex, z, levels, bbox.width, bbox.height, 60*qprec, 30*qprec);
        }
        insert_grain(grains, field->data, xres, &bbox, grain_id, z);
    }

    laplace_iterators_free(iterators);
    g_free(z);
    g_free(levels);
    g_free(revindex);
    g_free(sizes);
    g_free(bboxes);
    g_free(grains);

    g_object_unref(ourmask);
    gwy_data_field_invalidate(field);
}

static void
interpolate_segment(GwyDataLine *data_line, gint from, gint to)
{
    gint i, res = data_line->res;
    gdouble *d = data_line->data;
    gdouble zl, zr;

    g_assert(to < res-1 || from > 0);

    if (from == 0) {
        zr = d[to+1];
        for (i = from; i <= to; i++)
            d[i] = zr;
    }
    else if (to == res-1) {
        zl = d[from-1];
        for (i = from; i <= to; i++)
            d[i] = zl;
    }
    else {
        zl = d[from-1]/(to - from + 2);
        zr = d[to+1]/(to - from + 2);
        for (i = from; i <= to; i++)
            d[i] = zr*(i+1 - from) + zl*(to+1 - i);
    }
}

/**
 * gwy_data_line_correct_laplace:
 * @data_line: A data line.
 * @mask_line: Mask of places to be corrected.
 *
 * Fills missing values in a data line using Laplace data correction.
 *
 * Both data lines must have the same number of values.
 *
 * For one-dimensional data the missing data interpolation is explicit. Interior missing segments are filled with
 * linear dependence between the edge points.  Missing segments with one end open are filled with the edge value.
 *
 * Returns: %TRUE if the line contained any data at all.  If there are no data the %FALSE is returned and @data_line
 *          is filled with zeros.
 *
 * Since: 2.45
 **/
gboolean
gwy_data_line_correct_laplace(GwyDataLine *data_line,
                              GwyDataLine *mask_line)
{
    gint start = -1, i, res;
    const gdouble *m;

    g_return_val_if_fail(GWY_IS_DATA_LINE(data_line), FALSE);
    g_return_val_if_fail(GWY_IS_DATA_LINE(mask_line), FALSE);
    g_return_val_if_fail(data_line->res == mask_line->res, FALSE);

    res = data_line->res;
    m = mask_line->data;
    for (i = 0; i < res; i++) {
        if (start == -1) {
            if (m[i] > 0.0)
                start = i;
        }
        else {
            if (!(m[i] > 0.0)) {
                interpolate_segment(data_line, start, i-1);
                start = -1;
            }
        }
    }

    if (start == 0) {
        gwy_data_line_clear(data_line);
        return FALSE;
    }

    if (start != -1)
        interpolate_segment(data_line, start, res-1);

    return TRUE;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
