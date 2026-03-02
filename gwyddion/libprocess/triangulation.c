/*
 *  $Id: triangulation.c 21692 2018-11-26 13:29:49Z yeti-dn $
 *  Copyright (C) 2009-2018 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#include <string.h>
#include <math.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/triangulation.h>

/*
 * Some identities for planar triangulations
 * v ... number of vertices
 * h ... number of edges
 * t ... number of triangles
 * b ... number of boundary edges
 *
 *    t = h - (v - 1)
 *    b = 3*(v - 1) - h
 *
 * They are expressed using v and h because that's what we normally have
 * available: the number of points and the size of neigbours[] (where each
 * edge is counted twice).
 */

enum {
    UNDEF = GWY_TRIANGULATION_NONE,
    /* The work space and work queue can be resized, just choose some
     * reasonable initial more-or-less upper bound. */
    QUEUE = 128,
    /* Tunables */
    NEIGHBOURS = 8,   /* Must be at least 3. */
    LINE_13 = 1,
    LINE_02 = 2,
};

#define CELL_SIDE 6.0

#define get_point(points, point_size, i) \
    ((const GwyXY*)((const gchar*)(points) + (i)*(point_size)))

#define get_point_xyz(points, point_size, i) \
    ((const GwyXYZ*)((const gchar*)(points) + (i)*(point_size)))

#define get_vpoint(tri, i) \
    ((i) >= tri->npoints \
     ? (const GwyXY*)(tri->vpoints + ((i) - tri->npoints)) \
     : (const GwyXY*)((const gchar*)(tri->points) + (i)*(tri->point_size)))

#define get_vpoint_xyz(tri, i) \
    ((i) >= tri->npoints \
     ? (const GwyXYZ*)(tri->vpoints + ((i) - tri->npoints)) \
     : (const GwyXYZ*)((const gchar*)(tri->points) + (i)*(tri->point_size)))

/* Triangulation private data.  This is the representation of the final result.
 * The in-progress data is represented by Triangulator (together with a couple
 * of queues and caches). */
typedef struct {
    guint npoints;          /* Number of points */
    guint nsize;            /* Total size of neighbours[] */
    guint blen;             /* Number of points on the boundary. */
    guint nvpoints;         /* Number of Voronoi points in vpoints[] */
    guint vsize;            /* Total number of elements in voronoi[] */
    guint point_size;       /* Caller's point size, at least sizeof(GwyXY) */
    gconstpointer points;   /* Caller's points (Delaunay points) */
    GwyXY *vpoints;         /* Voronoi points (cell vertices). */
    gdouble *zvalues;       /* Z values of Voronoi points; only for NNA. */
    guint *nindex;          /* Position in neighbours[] where neighbours of
                               i-th point start. */
    guint *neighbours;      /* Compactified blocks of neighbours. */
    guint *boundary;        /* List of boundary points in CCW order. */
    guint *bindex;          /* Index of a point on the boundary, or UNDEF. */
    guint *vindex;          /* Block start positions in voronoi[]. */
    guint *voronoi;         /* Blocks of neighbours defining Voronoi triangles
                               (sections of Voronoi cells). */
} Triangulation;

/* List of points.  We transform the input points to this as we reorder them
 * anyway. */
typedef struct {
    guint npoints;          /* Number of points */
    GwyXY *points;          /* The points */
    guint *orig_index;      /* Map from our ids to original point numbers. */
} PointList;

/* Information about blocks of neighbours in Voronoi point merging.  The size
 * field is redundant. */
typedef struct {
    guint pos;
    guint len;
} VNeighbourBlock;

/* Information about blocks of neighbours in Triangulator. */
typedef struct {
    guint pos;
    guint len;
    guint size;
} NeighbourBlock;

/* Information about a boundary point.  For non-boundary points boht items
 * are UNDEF. */
typedef struct {
    guint prev;   /* Id of previous point on the convex hull. */
    guint next;   /* Id of next point on the convex hull. */
} BoundaryLink;

/* Triangulation state. */
typedef struct {
    NeighbourBlock *blocks;   /* Blocks of neighbours in neighbours[] */
    guint *neighbours;        /* Storage for indices of neighbours points */
    BoundaryLink *boundary;   /* Cycle of boundary points */
    guint npoints;            /* Point currently in the triangulation */
    guint nsize;              /* Allocated size of neighbours[] */
    guint nlen;               /* Used space in neighbours[] */
    gdouble eps;
    gdouble minq;
} Triangulator;

typedef struct {
    GwyXY centre;      /* Point in the side centre */
    GwyXY outernormal; /* Outer normal of the side */
    gdouble norm;      /* Scalar product of the vector from cente to the
                          opposite side with outernormal */
} TriangleSide;

typedef struct {
    TriangleSide sa, sb, sc;    /* Triangle sides, opposite to a, b, c */
    const GwyXYZ *a, *b, *c;    /* Vertices */
    gdouble da, db, dc;         /* Signed distances of a point from the side,
                                   depends on the point being considered. */
    guint ia, ib, ic;           /* Ids of the triangle vertices */
} Triangle;

typedef struct {
    guint *data;
    guint size;
    guint len;
    guint pos;
} UIntQueue;

/* NB: This struct does not necessarily represent convex tetragons.  We cache
 * information about any configurations consisting of one (splitting) line and
 * two points, one on each side of the line, forming two triangles. */
typedef struct {
    guint ids[4];      /* Vertex ids, first is the smallest, then ccw. */
} Tetragon;

typedef struct {
    GHashTable *map;   /* Map (ia, ib, ic, id) → index in cache + 1 */
    GSList *storage;   /* The storage, blocks of Tetragon. */
    Tetragon *currblock;    /* Current storage block for convenience. */
    guint currlen;     /* Number of cache items occupied in current block. */
    guint size;        /* Size of a storage block. */
} TetragonDecisionCache;

#ifdef DEBUG
static void dump_neighbours(const Triangulator *triangulator);
static void dump_triangulator(const Triangulator *triangulator);
static void dump_points_dat(gconstpointer points, guint npoints, guint point_size);
static void dump_points_(const Triangulator *triangulator,
                         guint npoints, gconstpointer points, gsize point_size);
static void dump_missing_points(const UIntQueue *queue, PointList *pointlist);
static void dump_points(const Triangulation *triangulation);
static void dump_voronoi(const Triangulation *triangulation);
#endif

#define GWY_TRIANGULATION_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE((obj), GWY_TYPE_TRIANGULATION, GwyTriangulationPrivate))

typedef Triangulation GwyTriangulationPrivate;

static void gwy_triangulation_finalize(GObject *object);

G_DEFINE_TYPE(GwyTriangulation, gwy_triangulation, G_TYPE_OBJECT)

static void
gwy_triangulation_class_init(GwyTriangulationClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GwyTriangulationPrivate));

    gobject_class->finalize = gwy_triangulation_finalize;
}

static void
gwy_triangulation_init(G_GNUC_UNUSED GwyTriangulation *object)
{
}

static void
gwy_triangulation_finalize(GObject *object)
{
    Triangulation *triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);

    g_free(triangulation->nindex);
    g_free(triangulation->neighbours);
    g_free(triangulation->boundary);
    g_free(triangulation->bindex);
    g_free(triangulation->vpoints);
    g_free(triangulation->zvalues);
    g_free(triangulation->vindex);
    g_free(triangulation->voronoi);
    /* Don't own that but leave no pointers behind. */
    triangulation->points = NULL;
}

/* Estimate how big block we want to allocate if we have @n neighbours.
 * Returns a multiple of NEIGHBOURS. */
static inline guint
block_size(guint n)
{
    guint size = MAX(n + 2, NEIGHBOURS);
    return (size + NEIGHBOURS-1)/NEIGHBOURS*NEIGHBOURS;
}

static inline void
block_clear(guint *block,
            guint len)
{
    memset(block, 0xff, len*sizeof(guint));
}

static inline guint
coords_to_grid_index(guint xres,
                     guint yres,
                     gdouble step,
                     gdouble x,
                     gdouble y)
{
    guint ix, iy;

    ix = (gint)floor(x/step);
    if (G_UNLIKELY(ix == xres))
        ix--;

    iy = (gint)floor(y/step);
    if (G_UNLIKELY(iy == yres))
        iy--;

    /* Go zig-zag through the cells */
    if (iy % 2)
        ix = xres-1 - ix;

    return iy*xres + ix;
}

static inline void
index_accumulate(guint *index_array,
                 guint n)
{
    guint i;

    for (i = 1; i <= n; i++)
        index_array[i] += index_array[i-1];
}

static inline void
index_rewind(guint *index_array,
             guint n)
{
    guint i;

    for (i = n; i; i--)
        index_array[i] = index_array[i-1];
    index_array[0] = 0;
}

/* Try to increase locality of the point list by sorting it to grid cells
 * and then taking the points cell-by-cell.  Also reduces the workind set size
 * by constructing a list of plain Points instead of whatever might the
 * caller's representation be. */
static void
build_compact_point_list(PointList *pointlist,
                         guint npoints,
                         gconstpointer points,
                         gsize point_size)
{
    const GwyXY *pt;
    gdouble xmin, xmax, ymin, ymax, xreal, yreal, step, xr, yr;
    guint i, xres, yres, ncells, ig, pos;
    guint *cell_index;

    pointlist->npoints = npoints;
    pointlist->points = g_new(GwyXY, npoints);
    pointlist->orig_index = g_new(guint, npoints);

    pt = get_point(points, point_size, 0);
    xmin = xmax = pt->x;
    ymin = ymax = pt->y;
    for (i = 1; i < npoints; i++) {
        pt = get_point(points, point_size, i);

        if (pt->x < xmin)
            xmin = pt->x;
        else if (pt->x > xmax)
            xmax = pt->x;

        if (pt->y < ymin)
            ymin = pt->y;
        else if (pt->y > ymax)
            ymax = pt->y;
    }

    xreal = xmax - xmin;
    yreal = ymax - ymin;
    xr = xreal/sqrt(npoints)*CELL_SIDE;
    yr = yreal/sqrt(npoints)*CELL_SIDE;

    if (xr <= yr) {
        xres = (guint)ceil(xreal/xr);
        step = xreal/xres;
        yres = (guint)ceil(yreal/step);
    }
    else {
        yres = (guint)ceil(yreal/yr);
        step = yreal/yres;
        xres = (guint)ceil(xreal/step);
    }

    ncells = xres*yres;
    cell_index = g_new0(guint, ncells + 1);

    for (i = 0; i < npoints; i++) {
        pt = get_point(points, point_size, i);
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        cell_index[ig]++;
    }

    index_accumulate(cell_index, xres*yres);
    index_rewind(cell_index, xres*yres);

    for (i = 0; i < npoints; i++) {
        pt = get_point(points, point_size, i);
        ig = coords_to_grid_index(xres, yres, step, pt->x - xmin, pt->y - ymin);
        pos = cell_index[ig];
        pointlist->orig_index[pos] = i;
        pointlist->points[pos] = *pt;
        cell_index[ig]++;
    }

    g_free(cell_index);
}

static inline void
free_point_list(PointList *pointlist)
{
    g_free(pointlist->orig_index);
    g_free(pointlist->points);
}

static inline void
uint_queue_init(UIntQueue *queue)
{
    queue->size = QUEUE;
    queue->data = g_new(guint, queue->size);
    queue->len = queue->pos = 0;
}

/* Add i unconditionally. */
static inline void
uint_queue_add_to_end(UIntQueue *queue, guint i)
{
    if (G_UNLIKELY(queue->len == queue->size)) {
        queue->size = MAX(2*queue->size, 16);
        queue->data = g_renew(guint, queue->data, queue->size);
    }

    queue->data[queue->len] = i;
    queue->len++;
}

/* Add i if it is not in the active partition [pos, len) yet. */
static inline void
uint_queue_push(UIntQueue *queue, guint i)
{
    guint j;

    for (j = queue->pos; j < queue->len; j++) {
        if (queue->data[j] == i)
            return;
    }
    uint_queue_add_to_end(queue, i);
}

static inline void
uint_queue_clear(UIntQueue *queue)
{
    queue->pos = queue->len = 0;
}

/* Prefill the queue with numbers from 0 to n-1. */
static inline void
uint_queue_identity_fill(UIntQueue *queue, guint n)
{
    guint i;

    if (G_UNLIKELY(queue->size < n)) {
        queue->size = n;
        queue->data = g_renew(guint, queue->data, queue->size);
    }

    queue->pos = 0;
    queue->len = n;
    for (i = 0; i < n; i++)
        queue->data[i] = i;
}

static inline gboolean
uint_queue_next(UIntQueue *queue, guint *i)
{
    if (queue->pos == queue->len)
        return FALSE;

    *i = queue->data[queue->pos++];

    /* If the queue becomes exhausted we can freely reset the position to the
     * beginning. */
    if (queue->pos == queue->len)
        uint_queue_clear(queue);

    return TRUE;
}

static inline void
uint_queue_free(UIntQueue *queue)
{
    g_free(queue->data);
}

/* Returns %TRUE if @pt lies on the right side of line from @a to @b. */
static inline gboolean
point_on_right_side(const GwyXY *a, const GwyXY *b, const GwyXY *pt)
{
    gdouble cx, cy, vx, vy;

    cx = pt->x - 0.5*(a->x + b->x);
    cy = pt->y - 0.5*(a->y + b->y);
    vx = b->x - a->x;
    vy = b->y - a->y;

    return cx*vy - cy*vx >= 0.0;
}

static inline gboolean
ccw_angle_convex(gdouble phi1, gdouble phi2)
{
    return fmod(phi2 - phi1 + 2*G_PI, 2*G_PI) <= G_PI;
}

static Triangulator*
triangulator_new_from_pointlist(const PointList *pointlist)
{
    Triangulator *triangulator;
    guint npoints = pointlist->npoints;

    triangulator = g_new0(Triangulator, 1);
    /* A reasonable estimate */
    triangulator->nsize = 2*NEIGHBOURS*npoints;
    triangulator->blocks = g_new(NeighbourBlock, npoints);
    triangulator->neighbours = g_new(guint, triangulator->nsize);
    block_clear(triangulator->neighbours, triangulator->nsize);
    triangulator->boundary = g_new(BoundaryLink, npoints);
    block_clear((guint*)triangulator->boundary, 2*npoints);

    return triangulator;
}

static void
triangulator_free(Triangulator *triangulator)
{
    if (triangulator) {
        g_free(triangulator->blocks);
        g_free(triangulator->neighbours);
        g_free(triangulator->boundary);
        g_free(triangulator);
    }
}

/* Enlarge the neighbours[] storage for a triangulator by a reasonable amount.
 * Low-level subroutine.  */
static inline void
triangulator_enlarge_neighbours(Triangulator *triangulator)
{
    guint size = triangulator->nsize;

    triangulator->nsize = block_size(((3*size/2) | 0xfffu) + 1);
    g_assert(triangulator->nsize > size);
    triangulator->neighbours = g_renew(guint,
                                       triangulator->neighbours,
                                       triangulator->nsize);
    block_clear(triangulator->neighbours + size,
                triangulator->nsize - size);
}

/* Increment the number of points in triangulator and create a new empty block
 * for the new point.  */
static inline void
triangulator_append_block(Triangulator *triangulator)
{
    NeighbourBlock *nb = triangulator->blocks + triangulator->npoints;

    nb->pos = triangulator->nlen;
    nb->len = 0;
    if (triangulator->nsize - nb->pos < NEIGHBOURS)
        triangulator_enlarge_neighbours(triangulator);
    nb->size = NEIGHBOURS;

    triangulator->nlen += nb->size;
    triangulator->npoints++;
}

/* Enlarge neighbour block @nb to size at least @len.
 * The slow path for wspace reintegration into triangulator->neighbours[].
 * It can just reclaim space in the next (unused) block, move the block
 * elsewhere and update @nb->pos, or even reallocate entire
 * triangulator->neighbours[] to make more space.  */
static void
enlarge_neighbour_block(Triangulator *triangulator,
                        NeighbourBlock *nb,
                        guint len)
{
    guint j, remaining, newsize;
    guint *neighbours;

    /* Try to find space after the end of the current block.  We know
     * the blocks sizes are multiples of NEIGHBOURS, so only check if
     * the first item is UNDEF. */
    neighbours = triangulator->neighbours + nb->pos;
    newsize = block_size(len + 2);
    remaining = triangulator->nsize - nb->pos;
    for (j = nb->size;
         j < remaining && neighbours[j] == UNDEF && j < newsize;
         j++)
        ;

    /* Gobble up the next block. */
    if (j >= len) {
        nb->size = j;
        triangulator->nlen = MAX(triangulator->nlen, nb->pos + j);
    }

    /* Must relocate the block elsewhere in triangulator->neighbours.
     * We just move it to the end. */
    if (triangulator->nlen + newsize > triangulator->nsize) {
        triangulator_enlarge_neighbours(triangulator);
        neighbours = triangulator->neighbours + nb->pos;
    }

    /* Copy the existing data to the new position. */
    gwy_assign(triangulator->neighbours + triangulator->nlen,
               neighbours,
               nb->len);
    /* Mark the original block unused. */
    block_clear(neighbours, nb->len);
    /* Update nb. */
    nb->pos = triangulator->nlen;
    nb->size = newsize;
    triangulator->nlen += newsize;
}

static inline guint
find_neighbour(const guint *neighbours,
               guint len,
               guint id)
{
    guint i;

    for (i = 0; i < len; i++) {
        if (neighbours[i] == id)
            return i;
    }
    return UNDEF;
}

static inline guint
next_neighbour(const guint *neighbours,
               guint len,
               guint i)
{
    i++;
    return neighbours[i == len ? 0 : i];
}

static inline guint
prev_neighbour(const guint *neighbours,
               guint len,
               guint i)
{
    return neighbours[i == 0 ? len-1 : i-1];
}

static inline guint
find_next_neighbour(const guint *neighbours,
                    guint len,
                    guint id)
{
    guint i = find_neighbour(neighbours, len, id);
    return G_UNLIKELY(i == UNDEF) ? UNDEF : next_neighbour(neighbours, len, i);
}

static inline guint
find_prev_neighbour(const guint *neighbours,
                    guint len,
                    guint id)
{
    guint i = find_neighbour(neighbours, len, id);
    return G_UNLIKELY(i == UNDEF) ? UNDEF :  prev_neighbour(neighbours, len, i);
}

/* This assumes a counter-clockwise triangle */
static inline void
make_triangle_side(TriangleSide *side,
                   const GwyXYZ *from,
                   const GwyXYZ *to,
                   const GwyXYZ *opposite)
{
    side->centre.x = 0.5*(to->x + from->x);
    side->centre.y = 0.5*(to->y + from->y);
    side->outernormal.x = to->y - from->y;
    side->outernormal.y = from->x - to->x;
    side->norm = (opposite->x - side->centre.x)*side->outernormal.x
                 + (opposite->y - side->centre.y)*side->outernormal.y;
}

/* This assumes a counter-clockwise triangle */
static void
make_triangle(Triangle *triangle,
              gconstpointer points, gsize point_size)
{
    /* XXX: In the triangulation algoritm, the points are in fact only XY,
     * but the Z members are never accessed so the typecast is all right. */
    triangle->a = get_point_xyz(points, point_size, triangle->ia);
    triangle->b = get_point_xyz(points, point_size, triangle->ib);
    triangle->c = get_point_xyz(points, point_size, triangle->ic);

    make_triangle_side(&triangle->sa, triangle->b, triangle->c, triangle->a);
    make_triangle_side(&triangle->sb, triangle->c, triangle->a, triangle->b);
    make_triangle_side(&triangle->sc, triangle->a, triangle->b, triangle->c);
}

/* Positive for inside, negative for outside.  Normalized to the distance of
 * the opposite triangle point -- directly usable for interpolation. */
static inline gdouble
side_point_distance(const TriangleSide *side,
                    const GwyXY *pt)

{
    return ((pt->x - side->centre.x)*side->outernormal.x
            + (pt->y - side->centre.y)*side->outernormal.y)/side->norm;
}

static gboolean
triangle_contains_point(Triangle *triangle,
                        const GwyXY *pt)
{
    /* Do not terminate permaturely, the caller will typically examine da, db,
     * and dc to determine what to do next if the point is not inside. */
    triangle->da = side_point_distance(&triangle->sa, pt);
    triangle->db = side_point_distance(&triangle->sb, pt);
    triangle->dc = side_point_distance(&triangle->sc, pt);

    return triangle->da >= 0 && triangle->db >= 0 && triangle->dc >= 0;
}

/* Decide if a ccw oriented line a--b is on the boundary. */
static inline gboolean
line_is_on_boundary_ccw(const Triangulator *triangulator, guint from, guint to)
{
    return triangulator->boundary[from].next == to;
}

/* Initializes @triangle to any valid triangle containing point @hint. */
static void
make_valid_triangle(const guint *neighbours, guint len,
                    gconstpointer points, gsize point_size,
                    Triangle *triangle,
                    guint hint)
{
    const GwyXY *a = get_point(points, point_size, hint);
    const GwyXY *b, *c;
    gdouble phib, phic;
    guint i;

    triangle->ia = hint;
    for (i = 0; i < len; i++) {
        triangle->ib = neighbours[i];
        triangle->ic = next_neighbour(neighbours, len, i);

        b = get_point(points, point_size, triangle->ib);
        phib = atan2(b->y - a->y, b->x - a->x);
        c = get_point(points, point_size, triangle->ic);
        phic = atan2(c->y - a->y, c->x - a->x);

        if (ccw_angle_convex(phib, phic)) {
            make_triangle(triangle, points, point_size);
            return;
        }
    }

    g_assert_not_reached();
}

/* If TRUE is returned, then a neighbour on the other side was found and the
 * triangle has become clockwise.  If TRUE is returned, then @opposite is
 * unchanged and the triangle is kept counter-clockwise. */
static gboolean
find_opposite_point(const Triangulation *triangulation,
                    guint from, guint to, guint *opposite)
{
    const GwyXY *a, *b, *c;
    guint to_prev, from_next, pos, len;
    const guint *neighbours;

    pos = triangulation->nindex[from];
    len = triangulation->nindex[from + 1] - pos;
    neighbours = triangulation->neighbours + pos;
    to_prev = find_prev_neighbour(neighbours, len, to);

    pos = triangulation->nindex[to];
    len = triangulation->nindex[to + 1] - pos;
    neighbours = triangulation->neighbours + pos;
    from_next = find_next_neighbour(neighbours, len, from);

    /* Now there are some silly few-point special cases.  If @opposite is in
     * the centre of a triangle formed by @from, @to and the newly found point,
     * then we have an apparent match but it is not the point we are looking
     * for.  Check that the points really lies on the opposite side. */
    if (from_next != to_prev || from_next == *opposite)
        return FALSE;

    a = get_point(triangulation->points, triangulation->point_size, from);
    b = get_point(triangulation->points, triangulation->point_size, to);
    c = get_point(triangulation->points, triangulation->point_size, to_prev);
    /*
    g_assert(!point_on_right_side(a, b,
                                  get_point(points, point_size, *opposite)));
                                  */

    if (!point_on_right_side(a, b, c))
        return FALSE;

    *opposite = to_prev;
    return TRUE;
}

static inline gboolean
move_triangle_a(const Triangulation *triangulation, Triangle *triangle)
{
    if (find_opposite_point(triangulation,
                            triangle->ib, triangle->ic, &triangle->ia)) {
        GWY_SWAP(guint, triangle->ib, triangle->ic);
        GWY_SWAP(const GwyXYZ*, triangle->b, triangle->c);
        return TRUE;
    }
    return FALSE;
}

static inline gboolean
move_triangle_b(const Triangulation *triangulation, Triangle *triangle)
{
    if (find_opposite_point(triangulation,
                            triangle->ic, triangle->ia, &triangle->ib)) {
        GWY_SWAP(guint, triangle->ic, triangle->ia);
        GWY_SWAP(const GwyXYZ*, triangle->c, triangle->a);
        return TRUE;
    }
    return FALSE;
}

static inline gboolean
move_triangle_c(const Triangulation *triangulation, Triangle *triangle)
{
    if (find_opposite_point(triangulation,
                            triangle->ia, triangle->ib, &triangle->ic)) {
        GWY_SWAP(guint, triangle->ia, triangle->ib);
        GWY_SWAP(const GwyXYZ*, triangle->a, triangle->b);
        return TRUE;
    }
    return FALSE;
}

/* Calculate the intersection of dividing lines of the corner angles at a and b
 * in boundary point sequence p, a, b, n. */
static void
find_side_section(const GwyXY *p,
                  const GwyXY *a,
                  const GwyXY *b,
                  const GwyXY *n,
                  GwyXY *origin)
{
    GwyXY pa, ab, bn, mA, mB;
    gdouble norm, det, rhsa, rhsb;

    pa.x = a->x - p->x;
    pa.y = a->y - p->y;
    norm = hypot(pa.x, pa.y);
    pa.x /= norm;
    pa.y /= norm;

    ab.x = b->x - a->x;
    ab.y = b->y - a->y;
    norm = hypot(ab.x, ab.y);
    ab.x /= norm;
    ab.y /= norm;

    bn.x = n->x - b->x;
    bn.y = n->y - b->y;
    norm = hypot(bn.x, bn.y);
    bn.x /= norm;
    bn.y /= norm;

    mA.x = pa.x + ab.x;
    mA.y = pa.y + ab.y;
    mB.x = ab.x + bn.x;
    mB.y = ab.y + bn.y;
    det = mA.x*mB.y - mA.y*mB.x;

    rhsa = a->x*mA.x + a->y*mA.y;
    rhsb = b->x*mB.x + b->y*mB.y;

    origin->x = (rhsa*mB.y - rhsb*mA.y)/det;
    origin->y = (rhsb*mA.x - rhsa*mB.x)/det;
}

/* A number between [-1, 1] means in the side, smaller means back, larger means
 * forward. */
static gdouble
side_intersection_distance(const GwyXYZ *a, const GwyXYZ *b,
                           const GwyXY *pt)
{
    GwyXY c, v;

    c.x = pt->x - 0.5*(a->x + b->x);
    c.y = pt->y - 0.5*(a->y + b->y);
    v.x = b->x - a->x;
    v.y = b->y - a->y;

    return 2.0*(c.x*v.x + c.y*v.y)/(v.x*v.x + v.y*v.y);
}

/* Find the side nearest to @pt.  The search must start from a boundary side,
 * if the side is not boundary, FALSE is returned. */
static gboolean
find_nearest_side(const Triangulation *triangulation,
                  guint *pia, guint *pib,
                  const GwyXY *pt)
{
    guint ip, ia, ib, in, blen, iter, previa, previb, prevprevia, prevprevib;
    const guint *bindex, *boundary;
    const GwyXY *p, *a, *b, *n;
    GwyXY origin;
    gdouble phia, phib, phi;
    gboolean forw, back;

    ia = *pia;
    ib = *pib;
    bindex = triangulation->bindex;
    if (bindex[ia] == UNDEF || bindex[ib] == UNDEF)
        return FALSE;

    boundary = triangulation->boundary;
    blen = triangulation->blen;
    ip = boundary[(bindex[ia] + blen-1) % blen];
    in = boundary[(bindex[ib] + 1) % blen];

    p = get_point(triangulation->points, triangulation->point_size, ip);
    a = get_point(triangulation->points, triangulation->point_size, ia);
    b = get_point(triangulation->points, triangulation->point_size, ib);
    n = get_point(triangulation->points, triangulation->point_size, in);

    iter = 0;
    previa = previb = prevprevia = prevprevib = UNDEF;
    for (iter = 0; iter < blen; iter++) {
        find_side_section(p, a, b, n, &origin);

        phia = atan2(a->y - origin.y, a->x - origin.x);
        phib = atan2(b->y - origin.y, b->x - origin.x);
        phi = atan2(pt->y - origin.y, pt->x - origin.x);

        forw = ccw_angle_convex(phia, phi);
        back = ccw_angle_convex(phi, phib);

        if (forw && back && point_on_right_side(a, b, pt)) {
            *pia = ia;
            *pib = ib;
            return TRUE;
        }

        if (forw) {
            ip = ia;
            ia = ib;
            ib = in;
            in = boundary[(bindex[ib] + 1) % blen];
            p = a;
            a = b;
            b = n;
            n = get_point(triangulation->points, triangulation->point_size, in);
        }
        else {
            in = ib;
            ib = ia;
            ia = ip;
            ip = boundary[(bindex[ia] + blen-1) % blen];
            n = b;
            b = a;
            a = p;
            p = get_point(triangulation->points, triangulation->point_size, ip);
        }

        if (ia == prevprevia && ib == prevprevib) {
            /* Cannot decide between two sides.  Choose one at random. */
            *pia = ia;
            *pib = ib;
            return TRUE;
        }

        prevprevia = previa;
        prevprevib = previb;
        previa = ia;
        previb = ib;
    }

    return FALSE;
}

/* Ensures @triangle contains point @pt.  A relatively quick test if it already
 * contains the point.  If the right triangle is nearby, it is also found
 * reasonably fast. */
static gboolean
ensure_triangle(const Triangulation *triangulation,
                Triangle *triangle,
                const GwyXY *pt)
{
    gboolean moved;
    guint iter;

    iter = 0;
    while (!triangle_contains_point(triangle, pt)) {
        if (triangle->da <= triangle->db) {
            if (triangle->da <= triangle->dc)
                moved = move_triangle_a(triangulation, triangle);
            else
                moved = move_triangle_c(triangulation, triangle);
        }
        else {
            if (triangle->db <= triangle->dc)
                moved = move_triangle_b(triangulation, triangle);
            else
                moved = move_triangle_c(triangulation, triangle);
        }

        if (!moved)
            return FALSE;

        make_triangle(triangle,
                      triangulation->points, triangulation->point_size);
        if (G_UNLIKELY(iter++ == triangulation->npoints)) {
            triangle->ia = triangle->ib = triangle->ic = UNDEF;
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
map_to_orig_index(const Triangulator *triangulator,
                  const PointList *pointlist,
                  Triangulation *triangulation)
{
    const NeighbourBlock *nb;
    const guint *neighbours, *orig_index;
    guint *dest;
    guint npoints = triangulator->npoints;
    guint bsize, i, j, iorig, ifrom;

    orig_index = pointlist->orig_index;

    /* Construct the back-mapped neighbour index */
    g_assert(npoints == pointlist->npoints);
    triangulation->npoints = npoints;
    triangulation->nindex = g_renew(guint, triangulation->nindex, npoints+1);
    for (i = 0; i < npoints; i++) {
        iorig = orig_index[i];
        triangulation->nindex[iorig] = triangulator->blocks[i].len;
    }

    index_accumulate(triangulation->nindex, npoints);
    index_rewind(triangulation->nindex, npoints);

    /* Fill neighbours with back-mapped neighbour indices */
    triangulation->nsize = triangulation->nindex[npoints];
    triangulation->neighbours = g_renew(guint, triangulation->neighbours,
                                        triangulation->nsize);
    for (i = 0; i < npoints; i++) {
        iorig = orig_index[i];
        nb = triangulator->blocks + i;
        neighbours = triangulator->neighbours + nb->pos;
        dest = triangulation->neighbours + triangulation->nindex[iorig];
        for (j = 0; j < nb->len; j++)
            dest[j] = orig_index[neighbours[j]];
    }

    /* Now map back the boundary. */
    /* We promise GwyTriangulationData.index[size] always exists... */
    triangulation->bindex = g_renew(guint, triangulation->bindex, npoints + 1);
    triangulation->bindex[npoints] = UNDEF;
    block_clear(triangulation->bindex, npoints);
    /* If the triangulation is correct this formula holds, see the identities
     * near the start of the file.  */
    if (3*(npoints - 1) <= triangulation->nsize/2)
        return FALSE;
    bsize = 3*(npoints - 1) - triangulation->nsize/2;
    triangulation->boundary = g_renew(guint, triangulation->boundary, bsize);
    triangulation->blen = 0;

    /* To be consistent with the original implementation, number the boundary
     * points starting from the leftmost one. */
    for (i = 0; i < npoints; i++) {
        if (triangulator->boundary[i].next != UNDEF)
            break;
    }
    if (i == npoints)
        return FALSE;

    /* First simply find all the points and fill triangulation->boundary[]. */
    ifrom = i;
    triangulation->boundary[triangulation->blen++] = orig_index[i];
    while (TRUE) {
        i = triangulator->boundary[i].next;
        if (i == ifrom)
            break;
        /* Too many points on the boundary mean violated invariants. */
        if (triangulation->blen == bsize)
            return FALSE;

        triangulation->boundary[triangulation->blen++] = orig_index[i];
    }
    if (triangulation->blen != bsize)
        return FALSE;

    /* The rest is easy because triangulation->boundary[] is already using
     * original indices.  Go through the points and fill bindex[]. */
    for (i = 0; i < bsize; i++)
        triangulation->bindex[triangulation->boundary[i]] = i;

    return TRUE;
}

static guint
tetragon_hash(gconstpointer key)
{
    const Tetragon *tgon = (const Tetragon*)key;
    guint i, out;

    out = tgon->ids[0];
    for (i = 1; i < 4; i++) {
        out *= 1103515245u;
        out += 12345u;
        out ^= tgon->ids[i];
    }

    return out;
}

static gboolean
tetragon_equal(gconstpointer keya, gconstpointer keyb)
{
    const Tetragon *tgona = (const Tetragon*)keya;
    const Tetragon *tgonb = (const Tetragon*)keyb;

    return memcmp(tgona->ids, tgonb->ids, 4*sizeof(guint)) == 0;
}

/* Peform a cyclic permutation of point ids (a, b, c, d) in the tetragon to
 * ensure a is the smallest.  Return %TRUE if the meaning of dividing line
 * is unchanged by canonicalisation, %FALSE it it is flipped. */
static inline gboolean
tetragon_decision_canonicalize(Tetragon *tgon)
{
    guint i, imin = 0;

    for (i = 1; i < 4; i++) {
        if (tgon->ids[i] < tgon->ids[imin])
            imin = i;
    }

    if (!(imin & 1)) {
        if (imin == 2) {
            GWY_SWAP(guint, tgon->ids[0], tgon->ids[2]);
            GWY_SWAP(guint, tgon->ids[1], tgon->ids[3]);
        }
        return TRUE;
    }

    if (imin == 1) {
        i = tgon->ids[0];
        tgon->ids[0] = tgon->ids[1];
        tgon->ids[1] = tgon->ids[2];
        tgon->ids[2] = tgon->ids[3];
        tgon->ids[3] = i;
    }
    else {
        i = tgon->ids[3];
        tgon->ids[3] = tgon->ids[2];
        tgon->ids[2] = tgon->ids[1];
        tgon->ids[1] = tgon->ids[0];
        tgon->ids[0] = i;
    }
    return FALSE;
}

static inline void
tetragon_decision_cache_append_block(TetragonDecisionCache *cache)
{
    cache->currblock = g_new(Tetragon, cache->size);
    cache->storage = g_slist_prepend(cache->storage, cache->currblock);
    cache->currlen = 0;
}

static void
tetragon_decision_cache_init(TetragonDecisionCache *cache,
                             guint npoints)
{
    cache->map = g_hash_table_new(tetragon_hash, tetragon_equal);
    cache->size = npoints;
    cache->storage = NULL;
    tetragon_decision_cache_append_block(cache);
}

static void
tetragon_decision_cache_free(TetragonDecisionCache *cache)
{
    GSList *l;

    for (l = cache->storage; l; l = g_slist_next(l))
        g_free(l->data);
    g_slist_free(cache->storage);
    g_hash_table_destroy(cache->map);
}

/* Return the remembered decision for tetragon @tgon, as passed by the user.
 * Note @tgon is canonicalised so the returned value may no longer apply to it;
 * it applies to whatever was originally passed in it. */
static inline guint
tetragon_decision_lookup(TetragonDecisionCache *cache,
                         Tetragon *tgon)
{
    gboolean unflipped;
    guint result;

    unflipped = tetragon_decision_canonicalize(tgon);
    result = GPOINTER_TO_UINT(g_hash_table_lookup(cache->map, tgon));
    if (!result || unflipped)
        return result;
    return LINE_13 + LINE_02 - result;
}

/* Store a *canonicalised* decision to the cache.  Normally one only stores
 * decisions that have been looked up previously (unsuccessfully) so they are
 * automatically canonicalised.  Pass any vertex of the dividing line in
 * @any_vertex. */
static inline void
tetragon_decision_remember(TetragonDecisionCache *cache,
                           Tetragon *tgon,
                           guint any_vertex)
{
    guint result;

    if (G_UNLIKELY(cache->currlen == cache->size))
        tetragon_decision_cache_append_block(cache);

    result = (any_vertex == tgon->ids[0] || any_vertex == tgon->ids[2]
              ? LINE_02
              : LINE_13);
    //g_assert(!tetragon_decision_lookup(cache, tgon));
    cache->currblock[cache->currlen] = *tgon;
    g_hash_table_insert(cache->map, cache->currblock + cache->currlen,
                        GUINT_TO_POINTER(result));
    cache->currlen++;
}

/* Return TRUE if angle a--origin--b is convex (in ccw sense).  This includes
 * angles zero and π because they are indeed convex.  Use the function negated
 * for the complementary angle (@a and @b swapped) to exclude these two.  */
static inline gboolean
ccw_xy_angle_convex(const GwyXY *origin, const GwyXY *a, const GwyXY *b)
{
    gdouble dxa = a->x - origin->x;
    gdouble dya = a->y - origin->y;
    gdouble dxb = b->x - origin->x;
    gdouble dyb = b->y - origin->y;

    return dxa*dyb - dya*dxb >= 0.0;
}

static inline gboolean
ccw_id_angle_convex(const GwyXY *points, guint origin, guint ia, guint ib)
{
    return ccw_xy_angle_convex(points + origin, points + ia, points + ib);
}

static inline void
move_along_edge_forward(const Triangulator *triangulator, guint *ia, guint *ib)
{
    *ib = triangulator->boundary[*ib].next;
    *ia = triangulator->boundary[*ia].next;
}

static inline void
move_along_edge_back(const Triangulator *triangulator, guint *ia, guint *ib)
{
    *ib = triangulator->boundary[*ib].prev;
    *ia = triangulator->boundary[*ia].prev;
}

/* Find a triangle which has @ia as one of its vertices.  This can fail if
 * there is only a straight line through @ia. */
static gboolean
make_any_triangle_with_point(const Triangulator *triangulator,
                             guint ia, guint *ib, guint *ic)
{
    NeighbourBlock *nb = triangulator->blocks + ia;
    const guint *neighbours = triangulator->neighbours + nb->pos;
    guint i, len = nb->len;

    for (i = 0; i < len; i++) {
        *ic = neighbours[i];
        if (!line_is_on_boundary_ccw(triangulator, ia, *ic)) {
            *ib = prev_neighbour(neighbours, len, i);
            return TRUE;
        }
    }

    return FALSE;
}

/* Determine which line a point is closer to (in sense of bisection of the
 * angle betwen the two lines).
 * We do this by comparing (squared) cosines of the angles, calculated using
 * scalar products.  Signs need to be taken into account... */
static gboolean
closer_to_first_line(const GwyXY *origin, const GwyXY *a, const GwyXY *b,
                     const GwyXY *pt)
{
    gdouble px = pt->x - origin->x, py = pt->y - origin->y;
    gdouble ax = a->x - origin->x, ay = a->y - origin->y;
    gdouble bx = b->x - origin->x, by = b->y - origin->y;
    gdouble ap = ax*px + ay*py, bp = bx*px + by*py;
    gdouble a2, b2;

    if (ap >= 0.0) {
        /* Angle p--origin--a is < π/2, p--origin--b is > π/2. */
        if (G_LIKELY(bp <= 0.0))
            return TRUE;
        /* Both cosines are positive and squared cosine is a decreasing
         * function of angle.  This normally does not happen if a--origin--b is
         * outer ccw boundary (of convex hull or just a single triangle).  But
         * it can numerically.  */
        a2 = ax*ax + ay*ay;
        b2 = bx*bx + by*by;
        return ap*ap * b2 >= bp*bp * a2;
    }
    /* Angle p--origin--a is > π/2, p--origin--b is < π/2. */
    if (bp >= 0.0)
        return FALSE;
    /* Both cosines are negative and squared cosine is an increasing function
     * of angle.  */
    a2 = ax*ax + ay*ay;
    b2 = bx*bx + by*by;
    return ap*ap * b2 <= bp*bp * a2;
}

/* Only use this function when a--b is not a boundary line (as checked by
 * line_is_on_boundary_ccw()).  Then it should always find the opposite point.
 * So for this function %FALSE return value means failure. */
static gboolean
get_opposite_point(const Triangulator *triangulator, const GwyXY *points,
                   guint ia, guint ib, guint *opposite)
{
    NeighbourBlock *nb;
    const GwyXY *a, *b, *c;
    guint ib_prev, ia_next;
    const guint *neighbours;

    nb = triangulator->blocks + ia;
    neighbours = triangulator->neighbours + nb->pos;
    ib_prev = find_prev_neighbour(neighbours, nb->len, ib);
    if (G_UNLIKELY(ib_prev == UNDEF))
        return FALSE;

    nb = triangulator->blocks + ib;
    neighbours = triangulator->neighbours + nb->pos;
    ia_next = find_next_neighbour(neighbours, nb->len, ia);
    if (G_UNLIKELY(ia_next == UNDEF))
        return FALSE;

    /* Now there are some silly few-point special cases.  If @opposite is in
     * the centre of a triangle formed by @ia, @ib and the newly found point,
     * then we have an apparent match but it is not the point we are looking
     * for.  Check that the points really lies on the opposite side. */
    if (G_UNLIKELY(ia_next != ib_prev || ia_next == *opposite))
        return FALSE;

    a = points + ia;
    b = points + ib;
    c = points + ib_prev;
    if (G_UNLIKELY(!point_on_right_side(a, b, c)))
        return FALSE;

    *opposite = ib_prev;
    return TRUE;
}

/* Procedure find_triangle_for_new_point() is good for finding the triangle
 * containing a point lying inside the convex hull.  But it can get stuck when
 * the point is outside or just on the boundary because it always moves towards
 * the point, whreas in this case it has to move sidewise and even a bit
 * away.  Here we do not try to follow triangles, just move a--b along the
 * boundary until we find the closest edge.
 *
 * The function always returns %FALSE for convenience (it means outside point
 * in find_triangle_for_new_point()). */
static gboolean
find_edge_for_new_point(const Triangulator *triangulator,
                        const GwyXY *points,
                        guint *ia, guint *ib, guint *ic,
                        const GwyXY *pt)
{
    const GwyXY *a, *b, *t;
    const NeighbourBlock *nb;
    const guint *neighbours;
    guint tia, tib;

    /* Forward, checking a--b--t. */
    while (TRUE) {
        tia = *ia;
        tib = *ib;
        move_along_edge_forward(triangulator, &tia, &tib);
        g_assert(tia == *ib);
        a = points + *ia;
        b = points + *ib;
        t = points + tib;
        if (!closer_to_first_line(b, t, a, pt))
            break;
        *ib = tib;
        *ia = tia;
    }

    /* Back, checking t--a--b. */
    while (TRUE) {
        tia = *ia;
        tib = *ib;
        move_along_edge_back(triangulator, &tia, &tib);
        g_assert(tib == *ia);
        t = points + tia;
        a = points + *ia;
        b = points + *ib;
        if (!closer_to_first_line(a, t, b, pt))
            break;
        *ib = tib;
        *ia = tia;
    }

    /* Find the third point. */
    nb = triangulator->blocks + *ia;
    neighbours = triangulator->neighbours + nb->pos;
    tia = find_next_neighbour(neighbours, nb->len, *ib);

    /* XXX: Only for consistency check. */
    nb = triangulator->blocks + *ib;
    neighbours = triangulator->neighbours + nb->pos;
    tib = find_prev_neighbour(neighbours, nb->len, *ia);

    if (G_UNLIKELY(tia == UNDEF || tib == UNDEF || tia != tib))
        *ia = *ib = *ic = UNDEF;
    else
        *ic = tia;

    return FALSE;
}

/* Find the triangle containing @pt, filling it vertices in @ia, @ib and @ic.
 * Returns %TRUE if we found such triangle.  Return %FALSE if we did not, so
 * @pt is outside the current triangulation.  If we fail completely, the
 * output indices are set to UNDEF. */
static gboolean
find_triangle_for_new_point(const Triangulator *triangulator,
                            const GwyXY *points,
                            guint *ia, guint *ib, guint *ic,
                            const GwyXY *pt)
{
    gboolean right_of_ba, right_of_cb, right_of_ac;
    const GwyXY *a, *b, *c;
    guint iter, id, flip_pt;

    iter = 0;
    /* We cannot make more than triangulator->npoints moves.  If we do, abort
     * and fail. */
    for (iter = 0; iter < triangulator->npoints; iter++) {
        a = points + *ia;
        b = points + *ib;
        c = points + *ic;
        right_of_ba = point_on_right_side(b, a, pt);
        right_of_cb = point_on_right_side(c, b, pt);
        right_of_ac = point_on_right_side(a, c, pt);

        /* On the right side of all three sides → inside. */
        if (right_of_ba && right_of_cb && right_of_ac)
            return TRUE;

        /* On the right side of two sides, so flip around the remaining one. */
        if (right_of_ba && right_of_cb)
            flip_pt = 2;  /* b gets replaced */
        else if (right_of_cb && right_of_ac)
            flip_pt = 3;  /* c gets replaced */
        else if (right_of_ac && right_of_ba)
            flip_pt = 1;  /* a gets replaced */
        /* On the right side of only one side.  So we are not flipping around
         * that one but must choose from the other two based on which one the
         * point is farther from. */
        else if (right_of_ba)
            flip_pt = closer_to_first_line(c, a, b, pt) ? 2 : 1;
        else if (right_of_cb)
            flip_pt = closer_to_first_line(a, b, c, pt) ? 3 : 2;
        else if (right_of_ac)
            flip_pt = closer_to_first_line(b, c, a, pt) ? 1 : 3;
        else {
            g_assert_not_reached();
        }

        if (flip_pt == 1) {
            if (line_is_on_boundary_ccw(triangulator, *ib, *ic)) {
                return find_edge_for_new_point(triangulator, points,
                                               ib, ic, ia, pt);
            }
            id = *ia;
            if (!get_opposite_point(triangulator, points, *ib, *ic, &id))
                goto fail;

            /* New triangle is bdc */
            *ia = *ib;
            *ib = id;
        }
        else if (flip_pt == 2) {
            if (line_is_on_boundary_ccw(triangulator, *ic, *ia)) {
                return find_edge_for_new_point(triangulator, points,
                                               ic, ia, ib, pt);
            }
            id = *ib;
            if (!get_opposite_point(triangulator, points, *ic, *ia, &id))
                goto fail;

            /* New triangle is acd */
            *ib = *ic;
            *ic = id;
        }
        else if (flip_pt == 3) {
            if (line_is_on_boundary_ccw(triangulator, *ia, *ib)) {
                return find_edge_for_new_point(triangulator, points,
                                               ia, ib, ic, pt);
            }
            id = *ic;
            if (!get_opposite_point(triangulator, points, *ia, *ib, &id))
                goto fail;

            /* New triangle is adb */
            *ic = *ib;
            *ib = id;
        }
        else {
            g_assert_not_reached();
        }
    }

fail:
    *ia = *ib = *ic = UNDEF;
    return FALSE;
}

/* Find insert position i for point @ib in the neighbourhood of point @ia.
 * Point at i-1 should become the preceeding point of @ib and point i should
 * become its following point. */
static guint
find_insert_pos_in_nehgbourhood(const NeighbourBlock *nb,
                                guint ia,
                                const guint *neighbours,
                                const GwyXY *points,
                                const GwyXY *pt)
{
    const GwyXY *origin = points + ia, *neighpt, *neighptprev;
    gboolean prev_is_convex, next_is_convex;
    guint i;

    /* One or two points are always sorted.  So just append the point. */
    if (G_UNLIKELY(nb->len < 2))
        return nb->len;

    neighpt = points + neighbours[nb->len - 1];
    next_is_convex = ccw_xy_angle_convex(origin, pt, neighpt);
    for (i = 0; i < nb->len; i++) {
        prev_is_convex = next_is_convex;
        neighpt = points + neighbours[i];
        next_is_convex = ccw_xy_angle_convex(origin, pt, neighpt);
        /* The point is sorted if going forward the angle is positive and
         * going backward the angle is negative.  Do not insert points at
         * the beginning, append them at the end instead. */
        if (next_is_convex && !prev_is_convex)
            return i ? i : nb->len;
    }

    /* Now we are either screwed or we are outside the current convex hull
     * so there is a non-convex angle somewhere.  Find it. */
    neighpt = points + neighbours[nb->len - 1];
    for (i = 0; i < nb->len; i++) {
        neighptprev = neighpt;
        neighpt = points + neighbours[i];
        if (ccw_xy_angle_convex(origin, neighpt, neighptprev))
            return i ? i : nb->len;
    }

    /* We can try to handle this but generally we must fail.  The neighbourhood
     * consists of points all on one radial line from the origin. */
    return UNDEF;
}

/* Low-level function which manages physically inserting a new neighbour,
 * including reallocations, block relocations and stuff. */
static void
add_neighbour(Triangulator *triangulator, NeighbourBlock *nb,
              guint ipos, guint id)
{
    guint *neighbours;

    if (nb->len == nb->size)
        enlarge_neighbour_block(triangulator, nb, nb->len + 1);

    neighbours = triangulator->neighbours + nb->pos;
    if (ipos < nb->len) {
        memmove(neighbours + ipos + 1,
                neighbours + ipos,
                (nb->len - ipos)*sizeof(guint));
    }
    nb->len++;
    neighbours[ipos] = id;
}

/* Low-level function which manages physically removing a neighbour. */
static void
remove_neighbour(Triangulator *triangulator, NeighbourBlock *nb,
                 guint rpos)
{
    guint *neighbours;

    neighbours = triangulator->neighbours + nb->pos;
    if (rpos < nb->len-1) {
        memmove(neighbours + rpos,
                neighbours + rpos + 1,
                (nb->len-1 - rpos)*sizeof(guint));
    }
    nb->len--;
    neighbours[nb->len] = UNDEF;
}

/* Insert correctly sorted @ia into @ib's neighbourhood and @ib into @ia's
 * neighbourhood.  Fail if we are apparently unable to do it without breaking
 * invariants. */
static gboolean
connect_vertices(Triangulator *triangulator, const GwyXY *points,
                 guint ia, guint ib)
{
    NeighbourBlock *nba = triangulator->blocks + ia;
    NeighbourBlock *nbb = triangulator->blocks + ib;
    const guint *neighbours = triangulator->neighbours;
    guint apos, bpos;

    if ((apos = find_insert_pos_in_nehgbourhood(nba, ia, neighbours + nba->pos,
                                                points, points + ib)) == UNDEF)
        return FALSE;
    if ((bpos = find_insert_pos_in_nehgbourhood(nbb, ib, neighbours + nbb->pos,
                                                points, points + ia)) == UNDEF)
        return FALSE;

    add_neighbour(triangulator, nba, apos, ib);
    add_neighbour(triangulator, nbb, bpos, ia);
    return TRUE;
}

/* Remove sorted @ia from @ib's neighbourhood and @ib from @ia's neighbourhood.
 * Fail miserably if they are not found. */
static gboolean
disconnect_vertices(Triangulator *triangulator, guint ia, guint ib)
{
    NeighbourBlock *nba = triangulator->blocks + ia;
    NeighbourBlock *nbb = triangulator->blocks + ib;
    const guint *neighbours = triangulator->neighbours;
    guint apos, bpos;

    apos = find_neighbour(neighbours + nba->pos, nba->len, ib);
    g_return_val_if_fail(apos != UNDEF, FALSE);
    bpos = find_neighbour(neighbours + nbb->pos, nbb->len, ia);
    g_return_val_if_fail(bpos != UNDEF, FALSE);

    remove_neighbour(triangulator, nba, apos);
    remove_neighbour(triangulator, nbb, bpos);

    return TRUE;
}

/* Return %TRUE if side a--b is visible from point @i, assuming a--b is the a
 * ccw oriented side of a convex polygon (convex hull of the triangulation). */
static inline gboolean
boundary_side_visible(const GwyXY *points, guint ia, guint ib, guint i)
{
    const GwyXY *a = points + ia, *b = points + ib, *pt = points + i;
    gdouble cx, cy, ax, ay, bx, by;

    if (point_on_right_side(b, a, pt))
        return FALSE;

    /* If the point seems to be on the right side, be more strict. */
    ax = a->x - pt->x;
    ay = a->y - pt->y;
    bx = b->x - pt->x;
    by = b->y - pt->y;
    cx = 0.5*(a->x + b->x) - pt->x;
    cy = 0.5*(a->y + b->y) - pt->y;

    return fabs(ax*by - ay*bx) >= 1e-4*(cx*cx + cy*cy);
}

/* Assuming abc is a ccw triangle whose one boundary is the side of the convex
 * hull of the triangulation and the boundary is visible from point @i, rotate
 * abc cyclically until we make a--b such side (there can be two such sides).
 * Fail if we cannot make it so. */
static inline gboolean
make_ab_boundary_edge(const Triangulator *triangulator, const GwyXY *points,
                      guint *ia, guint *ib, guint *ic, guint i)
{
    /* There are two conditions a--b must satisfy: It is visible form @i and it
     * is actuall a boundary line. */
    if (boundary_side_visible(points, *ia, *ib, i)
        && line_is_on_boundary_ccw(triangulator, *ia, *ib))
        return TRUE;

    if (boundary_side_visible(points, *ib, *ic, i)
        && line_is_on_boundary_ccw(triangulator, *ib, *ic)) {
        i = *ia;
        *ia = *ib;
        *ib = *ic;
        *ic = i;
        return TRUE;
    }

    if (boundary_side_visible(points, *ic, *ia, i)
        && line_is_on_boundary_ccw(triangulator, *ic, *ia)) {
        i = *ib;
        *ib = *ia;
        *ia = *ic;
        *ic = i;
        return TRUE;
    }

    return FALSE;
}

/* Test for whether point @d lies inside the circumcircle of triangle abc.
 * The return value is positive for @d lying inside. */
static inline gdouble
point_inside_circumcircle_det(const GwyXY *a, const GwyXY *b, const GwyXY *c,
                              const GwyXY *d)
{
    gdouble adx = a->x - d->x, ady = a->y - d->y;
    gdouble bdx = b->x - d->x, bdy = b->y - d->y;
    gdouble cdx = c->x - d->x, cdy = c->y - d->y;
    gdouble ad2 = adx*adx + ady*ady;
    gdouble bd2 = bdx*bdx + bdy*bdy;
    gdouble cd2 = cdx*cdx + cdy*cdy;

    return ((adx*bdy - ady*bdx)*cd2
            + (cdx*ady - cdy*adx)*bd2
            + (bdx*cdy - bdy*cdx)*ad2);
}

/* Return %TRUE if the convex tetragon abcd can be divided by line b--d.
 * If the case seems ambiguous we also return %TRUE as there is no point
 * changing the dividing line to another ambiguous one. */
static inline gboolean
dividing_line_bd_is_fine(const GwyXY *points,
                         guint ia, guint ib, guint ic, guint id)
{
    const GwyXY *a = points + ia;
    const GwyXY *b = points + ib;
    const GwyXY *c = points + ic;
    const GwyXY *d = points + id;
    gdouble cdet = point_inside_circumcircle_det(d, a, b, c);
    gdouble adet = point_inside_circumcircle_det(b, c, d, a);

    return adet + cdet <= 0.0;
}

/* Remove line b--d from the triangulation.  Add line a--c. */
static inline gboolean
flip_dividing_line_to_ac(Triangulator *triangulator, const GwyXY *points,
                         guint ia, guint ib, guint ic, guint id)
{
    if (!disconnect_vertices(triangulator, ib, id))
        return FALSE;
    if (!connect_vertices(triangulator, points, ia, ic))
        return FALSE;

    return TRUE;
}

/* Quantity proportional to S³/(abc)² where a, b and c are the sides and S is
 * the triangle area.  The points need not be oriented in any manner as we
 * work with squared values.
 *
 * The maximum possible value is 1 for an equilateral triangle.
 * For right-angle triangles it ranges from 0 to 16/27 (isosceles).
 * For any narrow triangle it is small. */
static gdouble
triangle_quality(const GwyXY *a, const GwyXY *b, const GwyXY *c)
{
    gdouble abx = b->x - a->x, aby = b->y - a->y;
    gdouble bcx = c->x - b->x, bcy = c->y - b->y;
    gdouble cax = a->x - c->x, cay = a->y - c->y;
    gdouble ab2 = abx*abx + aby*aby;
    gdouble bc2 = bcx*bcx + bcy*bcy;
    gdouble ca2 = cax*cax + cay*cay;
    gdouble sa = abx*cay - aby*cax;
    gdouble sb = bcx*aby - bcy*abx;
    gdouble sc = cax*bcy - cay*bcx;
    gdouble s3 = fabs(sa*sb*sc);

    if (s3 == 0.0)
        return 0.0;

    return 64.0/27.0 * s3/(ab2*bc2*ca2);
}

static void
swap_point_list_points(PointList *pointlist, guint ia, guint ib)
{
    guint *orig_index = pointlist->orig_index;
    GwyXY *points = pointlist->points;

    GWY_SWAP(guint, orig_index[ia], orig_index[ib]);
    GWY_SWAP(GwyXY, points[ia], points[ib]);
}

static gboolean
create_first_triangle(PointList *pointlist, Triangulator *triangulator)
{
    guint i, n, ibest = 2;
    GwyXY *points = pointlist->points;
    gdouble quality, bestquality = 0.0;

    n = pointlist->npoints;
    triangulator_append_block(triangulator);
    if (n == 1)
        return TRUE;

    triangulator_append_block(triangulator);
    if (!connect_vertices(triangulator, points, 0, 1))
        return FALSE;
    if (n == 2) {
        triangulator->boundary[0].prev = triangulator->boundary[0].next = 1;
        triangulator->boundary[1].prev = triangulator->boundary[1].next = 0;
        return TRUE;
    }

    for (i = 2; i < n; i++) {
        quality = triangle_quality(points + 0, points + 1, points + i);
        /* Reject suspiciously large values. */
        if (quality > bestquality && quality <= 1.00001) {
            bestquality = quality;
            ibest = i;
            /* Immediately accept the triangle if it looks reasonable.  A right
             * angle triangle is probably reasonable. */
            if (quality > 0.4)
                break;
        }
    }

    if (bestquality <= 1e-3)
        return FALSE;

    /* Move the selected point to position 2. */
    if (ibest != 2)
        swap_point_list_points(pointlist, 2, ibest);

    triangulator_append_block(triangulator);
    if (!connect_vertices(triangulator, points, 0, 2))
        return FALSE;
    if (!connect_vertices(triangulator, points, 1, 2))
        return FALSE;

    /* Ensure we are starting with a ccw triangle as the boundary.  Using
     * ccw_id_angle_convex() should be safe here because we have specifically
     * chosen a nice triangle to start with.  */
    if (ccw_id_angle_convex(points, 0, 1, 2)) {
        for (i = 0; i < 3; i++) {
            triangulator->boundary[i].prev = (i + 2) % 3;
            triangulator->boundary[i].next = (i + 1) % 3;
        }
    }
    else {
        for (i = 0; i < 3; i++) {
            triangulator->boundary[i].prev = (i + 1) % 3;
            triangulator->boundary[i].next = (i + 2) % 3;
        }
    }

    return TRUE;
}

static inline void
reverse_uint_block(guint *array, guint len)
{
    guint *end = (array + len) - 1;

    while (array < end) {
        GWY_SWAP(guint, *array, *end);
        array++;
        end--;
    }
}

/* Rotate item at position @pos to position 0. */
static inline void
cyclically_rotate_uint_block(guint *array, guint len, guint pos)
{
    if (!pos)
        return;

    reverse_uint_block(array, len);
    reverse_uint_block(array, len - pos);
    reverse_uint_block(array + (len - pos), pos);
}

/* Rotate neighbourhood blocks so that the points are ordered not just
 * circularly, but the point with smallest angle actually comes first.
 * Some other code relies on this.
 * We can do this without trigonometry by finding the insert position for point
 * + (1, 0), i.e. some point directly along the x-axis.  The neighbour at this
 * position should be first in the neighbour block. */
static gboolean
reorder_neighbours_by_angle(Triangulator *triangulator, const GwyXY *points)
{
    NeighbourBlock *nb;
    guint *neighbours;
    guint i, j, n;
    GwyXY xline;
    gdouble r;

    n = triangulator->npoints;
    for (i = 0; i < n; i++) {
        nb = triangulator->blocks + i;
        neighbours = triangulator->neighbours + nb->pos;
        /* Find an x-vector of reasonable length. */
        r = fmax(fabs(points[i].x), fabs(points[i].y));
        for (j = 0; j < nb->len; j++) {
            r += fmax(fabs(points[neighbours[j]].x),
                      fabs(points[neighbours[j]].y));
        }
        r /= (nb->len + 1);

        /* Finding the insert position for point + (1, 0), i.e. some point
         * directly along the x-axis.  The neighbour at this position should be
         * first in the neighbour block. */
        xline.x = points[i].x + r;
        xline.y = points[i].y;

        j = find_insert_pos_in_nehgbourhood(nb, i, neighbours, points, &xline);
        /* This should not happen, but... */
        if (j == UNDEF)
            return FALSE;

        cyclically_rotate_uint_block(neighbours, nb->len, j);
    }

    return TRUE;
}

static inline gboolean
points_connected(const Triangulator *triangulator, guint ia, guint ib)
{
    const NeighbourBlock *nba, *nbb;
    const guint *neighbours;
    guint i, len;

    nba = triangulator->blocks + ia;
    nbb = triangulator->blocks + ib;
    if (nba->len <= nbb->len) {
        neighbours = triangulator->neighbours + nba->pos;
        len = nba->len;
    }
    else {
        neighbours = triangulator->neighbours + nbb->pos;
        len = nbb->len;
        ib = ia;
    }

    for (i = 0; i < len; i++) {
        if (neighbours[i] == ib)
            return TRUE;
    }
    return FALSE;
}

/* FIXME FIXME FIXME FIXME FIXME
 * We use the lies-on-line test incorrectly, finding points that are not
 * inside the segment but on the forward or backward continuation.  This
 * typically shows up as convex hull which jumps forward, back and forward
 * in very sharp angles. */
/* TODO: This is not a good condition.  Use some which makes points close to
 * one endpoint a bit more OK (i.e. not too close to the line) than points
 * around the centre which we always want to put on the line. */
static inline gdouble
point_closeness_to_line(const GwyXY *a, const GwyXY *b, const GwyXY *pt)
{
    gdouble ax = pt->x - a->x, ay = pt->y - a->y;
    gdouble bx = pt->x - b->x, by = pt->y - b->y;
    gdouble abx = b->x - a->x, aby = b->y - a->y;
    gdouble a2 = ax*ax + ay*ay, b2 = bx*bx + by*by, ab2 = abx*abx + aby*aby;
    gdouble ap = ax*abx + ay*aby, bp = bx*abx + by*aby;

    return (a2 - ap*ap/ab2 + b2 - bp*bp/ab2)/ab2;
}

/* XXX: For points outside the current convex hull and points completely inside
 * we can use a lenient condition.  However, if a--b is a boundary edge we risk
 * creating a slightly non-convex convex hull. */
static inline gboolean
point_lies_on_line(const GwyXY *points, guint ia, guint ib, guint i,
                   gdouble eps)
{
    return point_closeness_to_line(points + ia,
                                   points + ib,
                                   points + i) < eps;
}

/* Handle a point nominally inside a triangle, but as it lies on one side it
 * can still become a boundary point. */
static gboolean
handle_point_on_line_inside_triangle(Triangulator *triangulator,
                                     const GwyXY *points,
                                     guint ia, guint ib, guint ic,
                                     guint i,
                                     UIntQueue *queue,
                                     gboolean *inside)
{
    guint id;
    gdouble minq;

    if (line_is_on_boundary_ccw(triangulator, ia, ib)) {
        /* Split boundary line a--b, queue ccw ordered points (cw when viewed
         * from the newly added point).  Ensure a goes first and b goes last
         * because the boundary update needs it. */
        uint_queue_add_to_end(queue, ia);
        uint_queue_add_to_end(queue, ic);
        uint_queue_add_to_end(queue, ib);
        *inside = FALSE;
    }
    else {
        /* Find the opposite point to @c and connect the new point to all four
         * neighbours. */
        id = ic;
        if (!get_opposite_point(triangulator, points, ia, ib, &id))
            return FALSE;
        minq = triangulator->minq;
        if (triangle_quality(points + i, points + id, points + ib) < minq
            || triangle_quality(points + i, points + ia, points + id) < minq)
            return FALSE;
        /* TODO: This can still break topology by making pt--d to cross a--b.
         * Fail when it seems it might be the case. */
        uint_queue_add_to_end(queue, ia);
        uint_queue_add_to_end(queue, id);
        uint_queue_add_to_end(queue, ib);
        uint_queue_add_to_end(queue, ic);
    }
    /* In all cases the two lines pt--a an pt--b replace a--b. */
    disconnect_vertices(triangulator, ia, ib);
    return TRUE;
}

/* Find some initial neighbours for point @i it should be provisionally
 * connected to and fill them in the queue.  If @inside is returned as %FALSE
 * then the first and last point in @queue must be the first and last
 * *boundary* point (this occurs naturally except when splitting a boundary
 * line, then we connect to both boundary and non-boundary points at once).
 *
 * XXX: The function must not change anything in the triangulator if it returns
 * FALSE because the caller can then just postpone point @i and try another
 * point. */
static inline gboolean
find_provisional_neighbours(Triangulator *triangulator,
                            const GwyXY *points,
                            guint i,
                            UIntQueue *queue,
                            gboolean *inside)
{
    guint ia, ib, ic, tia, tib, bpos, j;
    gdouble q;

    uint_queue_clear(queue);

    /* Start from any valid triangle containing the last inserted point.
     * We count on the list with improved locality to make it a reasonable
     * start. */
    ia = i-1;
    if (!make_any_triangle_with_point(triangulator, ia, &ib, &ic))
        return FALSE;

    /* Traverse the triangulation until we find the enclosing or the
     * nearest (for outside points) triangle to the new point.  */
    *inside = find_triangle_for_new_point(triangulator, points, &ia, &ib, &ic,
                                          points + i);
    if (G_UNLIKELY(ia == UNDEF))
        return FALSE;

    if (*inside) {
        /* If the point lies on a line we split the line, put the point onto it
         * and connect also to the point on the opposite side (if there is any
         * such point).   */
        if (point_lies_on_line(points, ia, ib, i, triangulator->eps)) {
            return handle_point_on_line_inside_triangle(triangulator, points,
                                                        ia, ib, ic, i, queue,
                                                        inside);
        }
        if (point_lies_on_line(points, ib, ic, i, triangulator->eps)) {
            return handle_point_on_line_inside_triangle(triangulator, points,
                                                        ib, ic, ia, i, queue,
                                                        inside);
        }
        if (point_lies_on_line(points, ic, ia, i, triangulator->eps)) {
            return handle_point_on_line_inside_triangle(triangulator, points,
                                                        ic, ia, ib, i, queue,
                                                        inside);
        }
        /* Nothing untoward encountered. Simply add the new point to the mesh,
         * declaring the containing triangle vertices are its neighbours and
         * ensure reflectivity. */
        uint_queue_add_to_end(queue, ia);
        uint_queue_add_to_end(queue, ib);
        uint_queue_add_to_end(queue, ic);
        return TRUE;
    }

    /* When the point is outside, form new triangles by going along
     * the boundary line as far as we can ‘see’ the new point.  Make
     * all points along the way the neighbours of the new point. */
    if (!make_ab_boundary_edge(triangulator, points, &ia, &ib, &ic, i))
        return FALSE;

    /* Again, treat points lying directly on a line by splitting the line.  We
     * do not scan the boundary in such case. */
    if (point_lies_on_line(points, ia, ib, i, 3.0*triangulator->eps)) {
        uint_queue_add_to_end(queue, ia);
        uint_queue_add_to_end(queue, ic);
        uint_queue_add_to_end(queue, ib);
        disconnect_vertices(triangulator, ia, ib);
        return TRUE;
    }

    uint_queue_add_to_end(queue, ia);
    uint_queue_add_to_end(queue, ib);
    /* Forward. */
    tia = ia;
    tib = ib;
    move_along_edge_forward(triangulator, &tia, &tib);
    while (boundary_side_visible(points, tia, tib, i)) {
        uint_queue_add_to_end(queue, tib);
        move_along_edge_forward(triangulator, &tia, &tib);
    }

    /* Backward. */
    bpos = queue->len;
    tia = ia;
    tib = ib;
    move_along_edge_back(triangulator, &tia, &tib);
    while (boundary_side_visible(points, tia, tib, i)) {
        uint_queue_add_to_end(queue, tia);
        move_along_edge_back(triangulator, &tia, &tib);
    }

    /* Now order the points ccw along the boundary. */
    reverse_uint_block(queue->data, queue->len);
    reverse_uint_block(queue->data + (queue->len - bpos), bpos);

    for (j = 0; j < queue->len-1; j++) {
        q = triangle_quality(points + i,
                             points + queue->data[j],
                             points + queue->data[j + 1]);
        if (q < triangulator->minq)
            return FALSE;
    }

    return TRUE;
}

G_GNUC_UNUSED
static Triangulator*
triangulate(PointList *pointlist, GwySetFractionFunc set_fraction)
{
    Triangulator *triangulator;
    UIntQueue queue, todo;
    TetragonDecisionCache cache;
    PointList mypoints;
    GwyXY *points;
    guint npoints, i, iorig, niter, desperation;

    npoints = pointlist->npoints;
    triangulator = triangulator_new_from_pointlist(pointlist);
    /* The queue is used alternately for two different lists: points to
     * connect to the new point and points to update. */
    uint_queue_init(&queue);
    uint_queue_init(&todo);
    tetragon_decision_cache_init(&cache, npoints);

    /* Point list as created by the triangulation where we can skip some points
     * and schedule them for later.  All indices in neighbours, etc. refer to
     * this list.
     * XXX: It is obviously wasteful to copy the coordinates themselves. */
    mypoints.npoints = npoints;
    mypoints.points = g_new(GwyXY, npoints);
    mypoints.orig_index = g_new(guint, npoints);
    uint_queue_identity_fill(&todo, npoints);

    /* Create the first triangle.  If the points are all collinear we can
     * fail, at least for now...  The function is allowed to swap some points
     * in pointlist[] to make the first three points usable. */
    if (!create_first_triangle(pointlist, triangulator)) {
        /* In case we want to print something. */
        points = pointlist->points;
        goto fail;
    }

    points = mypoints.points;
    gwy_assign(points, pointlist->points, triangulator->npoints);
    gwy_assign(mypoints.orig_index, pointlist->orig_index,
               triangulator->npoints);
    todo.pos = triangulator->npoints;
    niter = 0;
    desperation = 0;

    /* If we fail adding a point, do not abort immediately.  Keep a growing
     * stack of problematic points at the end of pointlist, moving failed
     * points for later retry.  Only fail if all remaining points are in the
     * problematic list and we fail to add them again.  */
    triangulator->eps = 1e-9;
    triangulator->minq = 1e-4;
    while (uint_queue_next(&todo, &iorig)) {
        guint ia, ib, ic, id, j;
        NeighbourBlock *nb;
        gboolean inside;

        /* How much we are desperate? */
        niter++;
        if (niter > todo.len+1 - todo.pos) {
            desperation++;
            if (desperation > 10) {
                /* For consistent debug-print at the end. */
                uint_queue_add_to_end(&todo, iorig);
                break;
            }
            niter = 0;
            triangulator->eps *= 3.0;
            triangulator->minq *= 0.12;
        }

        i = triangulator->npoints;
        if (niter % 100 == 0 && set_fraction && !set_fraction(0.9*i/npoints))
            goto fail;

        /* Whatever was the original point index ioirg, it is point #i for us.
         * So points in the triangulator are always numbered sequentially. */
        mypoints.orig_index[i] = pointlist->orig_index[iorig];
        points[i] = pointlist->points[iorig];
        if (!find_provisional_neighbours(triangulator, points, i, &queue,
                                         &inside)) {
            /* Postpone the point.  This does not increment i, but niter is
             * still incremented so we will terminate eventually even if
             * nothing can be added. */
            uint_queue_add_to_end(&todo, iorig);
            continue;
        }

        /* Update outer boundary if the new point is outside/on the boundary.
         * This is the only boundary update because flipping can never change
         * the boundary. */
        if (!inside) {
            ia = queue.data[0];
            ib = queue.data[queue.len-1];
            g_assert(triangulator->boundary[ia].next != UNDEF);
            g_assert(triangulator->boundary[ib].prev != UNDEF);
            do {
                ic = triangulator->boundary[ia].next;
                triangulator->boundary[ia].next = UNDEF;
                triangulator->boundary[ic].prev = UNDEF;
                ia = ic;
            } while (ia != ib);

            ia = queue.data[0];
            triangulator->boundary[ia].next = i;
            triangulator->boundary[ib].prev = i;
            triangulator->boundary[i].prev = ia;
            triangulator->boundary[i].next = ib;
        }

        /* Create the connecting lines.  It could be done safely for the inside
         * case but not during the boundary scan for an outside point. */
        triangulator_append_block(triangulator);
        while (uint_queue_next(&queue, &id)) {
            if (!connect_vertices(triangulator, points, i, id))
                goto fail;
        }

        /* Queue the new point for neighbour update. */
        uint_queue_push(&queue, i);
        while (uint_queue_next(&queue, &ia)) {
            /* The block size can change and it can be even reallocated during
             * the cycle.  Iterate carefully! */
            nb = triangulator->blocks + ia;
            j = 0;
            while (j < nb->len) {
                guint *neighbours = triangulator->neighbours + nb->pos;
                guint cval;
                Tetragon tgon;

                ib = neighbours[j];
                ic = next_neighbour(neighbours, nb->len, j);
                /* If the next point is next on the boundary then the previous
                 * point must be previous on the boundary and the entire
                 * section is exterior. */
                if (line_is_on_boundary_ccw(triangulator, ia, ic)) {
                    if (!line_is_on_boundary_ccw(triangulator, ib, ia))
                        goto fail;
                    j++;
                    continue;
                }
                if (!points_connected(triangulator, ib, ic))
                    goto fail;
                /* If the line between the neighbours is a boundary line then
                 * there is nothing to flip. */
                if (line_is_on_boundary_ccw(triangulator, ib, ic)) {
                    j++;
                    continue;
                }

                id = ia;
                if (!get_opposite_point(triangulator, points, ib, ic, &id))
                    goto fail;
                /* Now we have the ccw oriented tetragon a--b--d--c--a with
                 * dividing line b--c. */
                tgon.ids[0] = ia;
                tgon.ids[1] = ib;
                tgon.ids[2] = id;
                tgon.ids[3] = ic;
                if ((cval = tetragon_decision_lookup(&cache, &tgon))) {
                    /* XXX: It must be in the cache with dividing line b--c! */
                    if (cval != LINE_13) {
                        g_warning("Neighbourhood found in cache with the "
                                  "other dividing line.");
                        goto fail;
                    }
                    j++;
                    continue;
                }
                /* If any of the angles at b or c are non-convex the tetragon
                 * is non-convex and can never be divided the other way.  Also,
                 * non-convex tetragons mess up the following test. */
                if (ccw_id_angle_convex(points, ib, ia, id)
                    || ccw_id_angle_convex(points, ic, id, ia)) {
                    tetragon_decision_remember(&cache, &tgon, ib);
                    j++;
                    continue;
                }
                /* If the dividing line is OK, sigh with relief (note the
                 * function uses abcd notation, but we have the points labelled
                 * differently. */
                if (dividing_line_bd_is_fine(points, ia, ib, id, ic)) {
                    tetragon_decision_remember(&cache, &tgon, ib);
                    j++;
                    continue;
                }

                /* Now the complicated part.  We have to flip (change dividing
                 * line from b--c to a--d). */
                if (!flip_dividing_line_to_ac(triangulator, points,
                                              ia, ib, id, ic))
                    goto fail;

                tetragon_decision_remember(&cache, &tgon, ia);
                /* We added a dividing line between a--b and a--c.  Therefore,
                 * we formed new neighbourhood segments j and j+1.  Do not
                 * increment j.  */
                /* XXX: It may not be necessary to push all three.  Not sure
                 * what the conditions are. */
                uint_queue_push(&queue, ib);
                uint_queue_push(&queue, ic);
                uint_queue_push(&queue, id);
            }
        }

        /* We successfully added a point.  Reset @niter so that it counts to
         * the number of remaning points again. */
        niter = 0;
    }

    if (triangulator->npoints < npoints)
        goto fail;
    if (!reorder_neighbours_by_angle(triangulator, points))
        goto fail;

    gwy_assign(pointlist->points, points, npoints);
    gwy_assign(pointlist->orig_index, mypoints.orig_index, npoints);
    free_point_list(&mypoints);
    uint_queue_free(&queue);
    uint_queue_free(&todo);
    tetragon_decision_cache_free(&cache);

    return triangulator;

fail:
    free_point_list(&mypoints);
    uint_queue_free(&queue);
    uint_queue_free(&todo);
    tetragon_decision_cache_free(&cache);
    triangulator_free(triangulator);

    return NULL;
}

/* Calculate circumcircle centre for a triangle that is counter-clockwise at
 * point @a.  Use the trick with shifting the origin to point @a to simplify
 * the formulas. */
static gboolean
circumcircle_centre(const GwyXY *a,
                    const GwyXY *b,
                    const GwyXY *c,
                    GwyXY *pt)
{
    GwyXY ca, ba;
    gdouble phib, phic, det, ba2, ca2;

    ba.x = b->x - a->x;
    ba.y = b->y - a->y;
    ca.x = c->x - a->x;
    ca.y = c->y - a->y;
    phib = atan2(ba.y, ba.x);
    phic = atan2(ca.y, ca.x);
    if (!ccw_angle_convex(phib, phic))
        return FALSE;

    ba2 = ba.x*ba.x + ba.y*ba.y;
    ca2 = ca.x*ca.x + ca.y*ca.y;
    det = 2*(ba.y*ca.x - ba.x*ca.y);
    /* XXX */
    if (!det)
        return FALSE;

    pt->x = a->x + (ba.y*ca2 - ca.y*ba2)/det;
    pt->y = a->y + (ca.x*ba2 - ba.x*ca2)/det;
    return TRUE;
}

static gboolean
add_point_id(const Triangulation *triangulation,
             guint i,
             guint ni,
             guint *vneighbours,
             guint toadd)
{
    guint pos, len, j;

    pos = triangulation->nindex[i];
    len = triangulation->nindex[i+1] - pos;
    j = find_neighbour(triangulation->neighbours + pos, len, ni);
    if (G_UNLIKELY(j == UNDEF || vneighbours[j] != UNDEF))
        return FALSE;
    vneighbours[j] = toadd;
    return TRUE;
}

static gboolean
add_common_neighbour(guint *vneighbours,
                     const guint *vindex,
                     guint ignore,
                     guint ia, guint ib,
                     guint addat)
{
    guint i, j, ni, nj;

    ia = vneighbours[ia];
    ib = vneighbours[ib];

    for (i = vindex[ia]; i < vindex[ia+1]; i++) {
        ni = vneighbours[i];
        if (ni == ignore || ni == UNDEF)
            continue;
        for (j = vindex[ib]; j < vindex[ib+1]; j++) {
            nj = vneighbours[j];
            if (nj == ni) {
                vneighbours[addat] = nj;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean
add_infinity_neighbour(guint *vneighbours,
                       const guint *vindex,
                       guint ignore,
                       guint ia,
                       guint addat)
{
    guint i, ni;

    ia = vneighbours[ia];

    for (i = vindex[ia]; i < vindex[ia+1]; i++) {
        ni = vneighbours[i];
        if (ni == ignore || ni == UNDEF)
            continue;
        if (vindex[ni+1] - vindex[ni] == 5) {
            vneighbours[addat] = ni;
            return TRUE;
        }
    }
    return FALSE;
}

static gdouble
point_distance2(const GwyXY *a, const GwyXY *b)
{
    gdouble x = a->x - b->x, y = a->y - b->y;
    return x*x + y*y;
}

/* Decide if two inner Voronoi points (have 6-neighbourhood) coincide and
 * should be merged.  Point @i must be inner.  Point @j we check if it is
 * actually inner. */
static inline gboolean
vpoints_too_close(const Triangulation *triangulation, guint i, guint j)
{
    static const guint possibilities[] = {
        1, 1,
        1, 3,
        1, 5,
        3, 3,
        3, 5,
        5, 5,
    };

    gconstpointer points = triangulation->points;
    gsize point_size = triangulation->point_size;
    const guint *voronoi = triangulation->voronoi;
    const GwyXY *pti, *ptj;
    guint m, k, ipos, jpos;
    gdouble d2, d2min = G_MAXDOUBLE;

    ipos = triangulation->vindex[i];
    jpos = triangulation->vindex[j];
    /* Never merge points at infinity. */
    if (triangulation->vindex[j+1] - jpos == 5)
        return FALSE;

    /* Find common Delaunay neighbours (they are at odd positions in the
     * neighbours).  Find minimum squared distance from the Voronoi points
     * @i and @j. */
    pti = triangulation->vpoints + (i - triangulation->npoints);
    ptj = triangulation->vpoints + (j - triangulation->npoints);
    for (m = 0; m < G_N_ELEMENTS(possibilities); m += 2) {
        k = voronoi[ipos + possibilities[m+1]];
        if (voronoi[jpos + possibilities[m]] != k)
            continue;

        d2 = point_distance2(get_point(points, point_size, k), pti);
        if (d2 < d2min)
            d2min = d2;
        d2 = point_distance2(get_point(points, point_size, k), ptj);
        if (d2 < d2min)
            d2min = d2;
    }

    d2 = point_distance2(pti, ptj);
    return d2 <= 1e-5*d2min;
}

static inline void
merge_with_add(guint *mergewith, guint j)
{
    guint k;

    for (k = 3; k; k--) {
        if (*mergewith == UNDEF) {
            *mergewith = j;
            return;
        }
        mergewith++;
    }
    g_assert_not_reached();
}

static inline void
merge_with_remove(guint *mergewith, guint j)
{
    g_return_if_fail(*mergewith != UNDEF);
    if (*mergewith == j) {
        if (mergewith[2] != UNDEF) {
            *mergewith = mergewith[2];
            mergewith[2] = UNDEF;
        }
        else if (mergewith[1] != UNDEF) {
            *mergewith = mergewith[1];
            mergewith[1] = UNDEF;
        }
        else
            *mergewith = UNDEF;
        return;
    }

    mergewith++;
    g_return_if_fail(*mergewith != UNDEF);
    if (*mergewith == j) {
        if (mergewith[1] != UNDEF) {
            *mergewith = mergewith[1];
            mergewith[1] = UNDEF;
        }
        else
            *mergewith = UNDEF;
        return;
    }

    mergewith++;
    g_return_if_fail(*mergewith == j);
    *mergewith = UNDEF;
}

static inline void
add_merge_friends_to_queue(UIntQueue *queue, const guint *mergewith)
{
    guint k;

    for (k = 3; k; k--) {
        if (*mergewith == UNDEF)
            return;
        uint_queue_push(queue, *mergewith);
        mergewith++;
    }
}

/* Remove links from and to point @i. */
static inline void
merge_point_done(guint *mergewith, guint npoints, guint i)
{
    guint j, k;

    for (k = 0; k < 3; k++) {
        if ((j = mergewith[3*(i - npoints) + k]) == UNDEF)
            return;

        merge_with_remove(mergewith + 3*(j - npoints), i);
        mergewith[3*(i - npoints) + k] = UNDEF;
    }
}

/* Replace all points in group[] with the first one.  The must form a single
 * subblock.  Fail when that is not the case or we do not find the point at
 * all.  */
static gboolean
replace_group_with_head(guint *neighbours, guint *plen,
                        const guint *group, guint glen)
{
    guint jfrom, jto, j, len = *plen;

    /* Figure out actual length, excluding UNDEFs at the end. */
    for (j = 0; j < len && neighbours[j] != UNDEF; j++)
        ;
    len = j;

    /* Subblock start. */
    for (jfrom = 0; jfrom < len; jfrom++) {
        if (find_neighbour(group, glen, neighbours[jfrom]) != UNDEF)
            break;
    }
    if (G_UNLIKELY(jfrom == len))
        return FALSE;

    /* Subblock end. */
    for (jto = (jfrom + 1) % len; jto != jfrom; jto = (jto + 1) % len) {
        if (find_neighbour(group, glen, neighbours[jto]) == UNDEF)
            break;
    }
    if (G_UNLIKELY(jto == jfrom))
        return FALSE;
    jto = (jto + len-1) % len;

    /* Try to wrap the start around the array beginning. */
    if (jfrom == 0) {
        for (j = len-1; j > jto; j--) {
            if (find_neighbour(group, glen, neighbours[j]) == UNDEF)
                break;
            jfrom = j;
        }
    }

    /* Check that there no other points elswehre in the neighbourhood that also
     * belong to the merge group.  That would screw up the topology.  */
    for (j = (jto + 1) % len; j != jfrom; j = (j + 1) % len) {
        if (find_neighbour(group, glen, neighbours[j]) != UNDEF)
            return FALSE;
    }

    neighbours[jfrom] = group[0];
    if (jto < jfrom) {
        /* j is the length of the preserved block. */
        j = jfrom - jto;
        memmove(neighbours, neighbours + jto+1, j*sizeof(guint));
        block_clear(neighbours + j, len - j);
        *plen = j;
    }
    else {
        /* j is the length of the removed block. */
        j = jto - jfrom;
        if (jto < len-1) {
            memmove(neighbours + jfrom+1, neighbours + jto+1,
                    (len-1 - jto)*sizeof(guint));
        }
        block_clear(neighbours + (len - j), j);
        *plen = len-j;
    }

    return TRUE;
}

/* For sorting of merged Voronoi point neighbours.  Use trigonometry here for
 * absolute comparisons. */
static gint
compare_vneighbours_ccw(gconstpointer pa, gconstpointer pb, gpointer user_data)
{
    const Triangulation *triangulation = (const Triangulation*)user_data;
    guint ia = *(const guint*)pa, ib = *(const guint*)pb;
    const GwyXY *origin, *a, *b;
    guint npoints = triangulation->npoints;
    gdouble phia, phib;

    /* The origin is always a Voronoi point. */
    origin = triangulation->vpoints + (triangulation->blen - npoints);

    a = get_vpoint(triangulation, ia);
    b = get_vpoint(triangulation, ib);

    phia = atan2(a->y - origin->y, a->x - origin->x);
    phib = atan2(b->y - origin->y, b->x - origin->x);
    if (phia < phib)
        return -1;
    if (phia > phib)
        return 1;
    return 0;
}

/* Each merging
 * - decreases the number of vpoints[] by 1,
 * - removes 3 lines, but each line is twice in voronoi[] (from both
 *   endpoints), so its size goes down by 6.
 * For points in infinity we would remove only 2 lines.  This seems hairy. */
static gboolean
merge_voronoi_points(Triangulation *triangulation)
{
    guint i, j, k, n, vpos, npoints, vsize, nvpoints, blen, mcount, ngroups,
          len, pos, tmplen, gid, gpos, glen;
    guint *mergewith, *vindex, *voronoi, *groups, *gindex, *pointinfo,
          *mneigbours;
    VNeighbourBlock *mblocks, *mb, *mbn;
    UIntQueue group, todo;
    gboolean ok = FALSE;
    GwyXY *vpoints;
    GwyXY pt;

    npoints = triangulation->npoints;
    vsize = triangulation->vsize;
    nvpoints = triangulation->nvpoints;
    blen = triangulation->blen;
    vindex = triangulation->vindex;
    voronoi = triangulation->voronoi;
    vpoints = triangulation->vpoints;

    /* Find which points we would like to merge.  Each Voronoi point three
     * Voronoi neigbours in the graph. */
    mergewith = g_new(guint, 3*(nvpoints - blen));
    block_clear(mergewith, 3*(nvpoints - blen));
    mcount = 0;
    for (i = npoints; i < npoints + nvpoints - blen; i++) {
        vpos = vindex[i];
        g_assert(vindex[i+1] - vpos == 6);

        /* Voronoi neighbours are at even positions, Delaunay at odd. */
        for (n = 0; n < 6; n += 2) {
            j = voronoi[vpos + n];
            if (j < i || !vpoints_too_close(triangulation, i, j))
                continue;

            merge_with_add(mergewith + 3*(i - npoints), j);
            merge_with_add(mergewith + 3*(j - npoints), i);
            mcount++;
        }
    }

    /* No merging necessary? Nice. */
    if (!mcount) {
        g_free(mergewith);
        return TRUE;
    }

    /* We do not know yet how many merge groups there will be, but at most
     * mcount if each merge is separate.  The entire list of merged points
     * is also longest in this case and has 2*@mcount items. */
    uint_queue_init(&group);
    uint_queue_init(&todo);
    gindex = g_new(guint, mcount + 1);
    groups = g_new(guint, 2*mcount);
    ngroups = len = 0;
    for (i = npoints; i < npoints + nvpoints - blen; i++) {
        if (mergewith[3*(i - npoints)] == UNDEF)
            continue;

        uint_queue_clear(&group);
        uint_queue_push(&group, i);
        add_merge_friends_to_queue(&group, mergewith + 3*(i - npoints));
        add_merge_friends_to_queue(&todo, mergewith + 3*(i - npoints));
        merge_point_done(mergewith, npoints, i);
        while (uint_queue_next(&todo, &j)) {
            add_merge_friends_to_queue(&group, mergewith + 3*(j - npoints));
            add_merge_friends_to_queue(&todo, mergewith + 3*(j - npoints));
            merge_point_done(mergewith, npoints, j);
        }

        gindex[ngroups] = len;
        gwy_assign(groups + len, group.data, group.len);
        gwy_guint_sort(group.len, groups + len);
        len += group.len;
        ngroups++;
    }
    gindex[ngroups] = len;

    g_free(mergewith);
    uint_queue_free(&todo);

    /* Now:
     * - All Voronoi points in a group get replaced by the point with the
     *   lowest id.  This point gets the union of all the neighbours (both
     *   Voronoi and Delaunay), sorted ccw.
     * - Any line to any of the point in the group is replaced with a line
     *   to the first point.
     * This would be hairy to do in one pass because of the mix of new and old
     * point ids when updating the links.  So in first pass we fix topology,
     * keeping point ids.  Removed points remain as ghosts with zero-sized
     * neighbourhoods and no links to them.  In the second pass we remap all
     * point ids. */

    /* Zero pointinfo means point not involved in merging; non-zero is group id
     * + 1.  We can detect if a point a group head by checking if its point id
     * appears first in the group. */
    pointinfo = g_new0(guint, nvpoints);
    for (i = 0; i < ngroups; i++) {
        for (j = gindex[i]; j < gindex[i+1]; j++)
            pointinfo[groups[j] - npoints] = i+1;
    }

    mblocks = g_new(VNeighbourBlock, npoints + nvpoints);
    mneigbours = g_new(guint, vsize); /* The size must decrease, actually. */

    /* Neighbourhoods of Delaunay points start as copies of what we have. */
    gwy_assign(mneigbours, voronoi, vindex[npoints]);
    for (i = 0; i < npoints; i++) {
        mblocks[i].pos = vindex[i];
        mblocks[i].len = vindex[i+1] - vindex[i];
    }
    len = vindex[npoints];

    /* The real fun is with Voronoi points. */
    uint_queue_clear(&group);
    for (i = npoints; i < npoints + nvpoints; i++) {
        mb = mblocks + i;
        mb->pos = len;
        pos = vindex[i];

        /* Non-merged point.  Some neighbourhoods might already have been
         * removed.  Copy the reduced block. */
        if (!pointinfo[i - npoints]) {
            for (j = pos; j < vindex[i+1]; j++) {
                if (voronoi[j] == UNDEF)
                    break;
            }
            j -= pos;
            gwy_assign(mneigbours + len, voronoi + pos, j);
            mb->len = j;
            len += j;
            continue;
        }

        gid = pointinfo[i - npoints] - 1;
        gpos = gindex[gid];
        /* Merged point that is not a group head becomes a ghost.  We already
         * removed it from everywhere when we were processing the group head. */
        if (i != groups[gpos])
            continue;

        /* Group head.  Gobble up the neighbours of all other points in the
         * group. */
        glen = gindex[gid+1] - gindex[gid];
        pt.x = pt.y = 0.0;
        for (j = gpos; j < gpos + glen; j++) {
            n = groups[j];
            pt.x += vpoints[n - npoints].x;
            pt.y += vpoints[n - npoints].y;
            for (k = vindex[n]; k < vindex[n+1] && voronoi[k] != UNDEF; k++) {
                /* Only add points that are not in the group itself. */
                if (find_neighbour(groups + gpos, glen, voronoi[k]) == UNDEF)
                    uint_queue_push(&group, voronoi[k]);
            }
        }
        vpoints[i - npoints].x = pt.x/glen;
        vpoints[i - npoints].y = pt.y/glen;

        /* Update links from the merged point.  Reuse of triangulation->blen
         * for the sorting to avoid extra data structures.  XXX: Dirty. */
        tmplen = triangulation->blen;
        triangulation->blen = i;
        g_qsort_with_data(group.data, group.len, sizeof(guint),
                          compare_vneighbours_ccw, triangulation);
        triangulation->blen = tmplen;

        gwy_assign(mneigbours + len, group.data, group.len);
        mb->len = group.len;
        len += group.len;

        /* Kill the neighbourhoods of all other points in the group.  They have
         * larger id so they are in voronoi[], not in mneigbours[]. */
        for (j = gpos + 1; j < gpos + glen; j++) {
            n = groups[j];
            mbn = mblocks + n;
            mbn->len = 0;
            block_clear(voronoi + vindex[n], vindex[n+1] - vindex[n]);
        }

        /* Update links to the group.  Points with smaller id must be
         * already updated in mneigbours, whereas points with larger id
         * must be still updated in voronoi. */
        while (uint_queue_next(&group, &n)) {
            if (n < i) {
                mbn = mblocks + n;
                if (!replace_group_with_head(mneigbours + mbn->pos, &mbn->len,
                                             groups + gpos, glen))
                    goto fail;
            }
            else {
                tmplen = vindex[n+1] - vindex[n];
                if (!replace_group_with_head(voronoi + vindex[n], &tmplen,
                                             groups + gpos, glen))
                    goto fail;
            }
        }
    }

    /* Now we only need to compactify the blocks (but we have the correct
     * sizes in mblocks) and remap Voronoi point ids (Delaunay point ids are
     * preserved). */
    j = npoints;
    for (i = 0; i < nvpoints; i++) {
        if (!pointinfo[i] || groups[gindex[pointinfo[i]-1]] == i + npoints)
            pointinfo[i] = j++;
        else
            pointinfo[i] = UNDEF;
    }
    tmplen = j;

    /* Delaunay points. */
    len = 0;
    for (i = 0; i < npoints; i++) {
        mb = mblocks + i;
        for (j = 0; j < mb->len; j++) {
            n = mneigbours[mb->pos + j];
            if (n < npoints)
                voronoi[len + j] = n;
            else {
                g_assert(pointinfo[n - npoints] != UNDEF);
                voronoi[len + j] = pointinfo[n - npoints];
            }
        }
        vindex[i] = len;
        len += mb->len;
    }

    /* Voronoi points.  Here we need to skip some and also remap coordinates. */
    for (i = npoints; i < npoints + nvpoints; i++) {
        /* The id in source data structs is i, but the id in destination data
         * structs is k. */
        k = pointinfo[i - npoints];
        if (k == UNDEF)
            continue;

        g_assert(k <= i);
        vpoints[k - npoints] = vpoints[i - npoints];
        mb = mblocks + i;
        for (j = 0; j < mb->len; j++) {
            n = mneigbours[mb->pos + j];
            if (n < npoints)
                voronoi[len + j] = n;
            else {
                g_assert(pointinfo[n - npoints] != UNDEF);
                voronoi[len + j] = pointinfo[n - npoints];
            }
        }
        vindex[k] = len;
        len += mb->len;
    }
    vindex[tmplen] = triangulation->vsize = len;
    triangulation->nvpoints = tmplen - npoints;

    ok = TRUE;

fail:
    uint_queue_free(&group);
    g_free(pointinfo);
    g_free(mblocks);
    g_free(mneigbours);
    g_free(groups);
    g_free(gindex);

    return ok;
}

static gboolean
delaunay_to_voronoi(Triangulation *triangulation)
{
    const GwyXY *a, *b, *c;
    GwyXY *vpoints;
    GwyXY pt;
    guint *voronoi, *vindex, *neighbours, *remaining;
    guint i, j, n, ni, next, prev, nvpoints, vsize, pos, len, vpos, vm1;
    guint blen, bnext, npoints;
    gdouble h, xmin, xmax, ymin, ymax, far_away;
    gconstpointer points = triangulation->points;
    gsize point_size = triangulation->point_size;

    blen = triangulation->blen;
    /* This is exact if counting also the vertices in infinities (the formula
     * is more understandably t+b).  See the identities at the begining of the
     * file. */
    npoints = triangulation->npoints;
    vm1 = npoints - 1;
    nvpoints = triangulation->nvpoints = 2*vm1;
    vpoints = triangulation->vpoints = g_renew(GwyXY, triangulation->vpoints,
                                               nvpoints);
    /* Inner Voronoi have 6 neighbours.  Voronoi points in infinity have only 5
     * neighbours but boundary Delaunay points will gain one neigbour more, so
     * this should be exact. */
    vsize = triangulation->vsize = 12*vm1 + triangulation->nsize;
    voronoi = triangulation->voronoi = g_renew(guint, triangulation->voronoi,
                                               vsize);
    block_clear(voronoi, vsize);

    /* We know exactly how many neighbours a Delaunay point will have so
     * prefill the index. */
    vindex = triangulation->vindex = g_renew(guint, triangulation->vindex,
                                             npoints + nvpoints + 1);
    vpos = 0;
    for (n = 0; n < npoints; n++) {
        vindex[n] = vpos;
        vpos += triangulation->nindex[n+1] - triangulation->nindex[n];
        /* Boundary points will gain one neighbour as there are two Voronoi
         * points in infinity. */
        if (triangulation->bindex[n] != UNDEF)
            vpos++;
    }
    /* We know the exact number of edges from original points. */
    if (G_UNLIKELY(vpos != 3*vm1 + triangulation->nsize/2))
        return FALSE;

    /* Now the Voronoi points.  In the first pass, create the points and
     * resolve Delaunay neighbours.  Mutual Voronoi point relations will be
     * resolved later.  */
    xmin = ymin = G_MAXDOUBLE;
    xmax = ymax = -G_MAXDOUBLE;
    for (i = 0; i < npoints; i++) {
        a = get_point(points, point_size, i);
        if ((bnext = triangulation->bindex[i]) != UNDEF)
            bnext = triangulation->boundary[(bnext + 1) % blen];
        pos = triangulation->nindex[i];
        len = triangulation->nindex[i+1] - pos;
        neighbours = triangulation->neighbours + pos;
        prev = neighbours[len - 1];
        for (j = 0; j < len; j++) {
            next = neighbours[j];
            if (prev > i && next > i && next != bnext) {
                b = get_point(points, point_size, prev);
                c = get_point(points, point_size, next);
                if (circumcircle_centre(a, b, c, &pt)) {
                    if (pt.x < xmin)
                        xmin = pt.x;
                    if (pt.x > xmax)
                        xmax = pt.x;
                    if (pt.y < ymin)
                        ymin = pt.y;
                    if (pt.y > ymax)
                        ymax = pt.y;
                    /* Add a new Voronoi point and make a, b and c its
                     * neighbours. */
                    if (G_UNLIKELY(n - npoints == nvpoints))
                        return FALSE;
                    vpoints[n - npoints] = pt;
                    vindex[n] = vpos;
                    voronoi[vpos + 1] = i;
                    voronoi[vpos + 3] = prev;
                    voronoi[vpos + 5] = next;
                    vpos += 6;   /* Make space for the Voronoi neighbours. */
                    /* Conversely, add it to the neighbourhood of a, b and c. */
                    if (!add_point_id(triangulation, i, prev,
                                      voronoi + vindex[i], n)
                        || !add_point_id(triangulation, prev, next,
                                         voronoi + vindex[prev], n)
                        || !add_point_id(triangulation, next, i,
                                         voronoi + vindex[next], n))
                        return FALSE;
                    n++;
                }
            }
            prev = next;
        }
    }
    /* Base the notion of what is sufficiently far away on the inner Voronoi
     * points.  They can be relatively far away too as the boundary triangles
     * tend to be quite flat. */
    far_away = 10.0*hypot(xmax - xmin, ymax - ymin);

    /* Compactify the two free positions in neighbourhoods of boundary points.
     * One is always at the end now because the new neighbourhood is one item
     * longer but we did not take this into account.  The two free positions
     * correspond to the infinity points of Voronoi grid and they should come
     * together.  Then we can really place the first infinity neighbour to the
     * position of the previous point in neighbours[] and the second infinity
     * point to the following position. */
    for (j = 0; j < blen; j++) {
        i = triangulation->boundary[j];
        pos = triangulation->vindex[i];
        len = triangulation->vindex[i+1] - pos;
        g_assert(len > 2);
        neighbours = triangulation->voronoi + pos;
        ni = find_neighbour(neighbours, len-1, UNDEF);
        g_assert(ni != UNDEF);
        for (i = len-1; i > ni+1; i--)
            neighbours[i] = neighbours[i-1];
        neighbours[i] = UNDEF;
    }

    /* Continuing the first pass for the boundary points. */
    remaining = g_new(guint, blen);
    for (j = 0; j < blen; j++) {
        i = triangulation->boundary[j];
        next = triangulation->boundary[(j + 1) % blen];
        a = get_point(points, point_size, i);
        b = get_point(points, point_size, next);
        /* The point in infinity is in fact somewhere far away from the
         * centre of a-b line in the direction of the outer normal. */
        pt.x = b->y - a->y;
        pt.y = a->x - b->x;
        h = far_away/hypot(pt.x, pt.y);
        pt.x = h*pt.x + 0.5*(a->x + b->x);
        pt.y = h*pt.y + 0.5*(a->y + b->y);
        /* Add a new Voronoi point and make a and b its neighbours. */
        if (G_UNLIKELY(n - npoints == nvpoints)) {
            g_free(remaining);
            return FALSE;
        }
        vpoints[n - npoints] = pt;
        vindex[n] = vpos;
        /* The neighbours need to be in the reverse order because we look from
         * the outside (infinity) now. */
        voronoi[vpos + 1] = next;
        voronoi[vpos + 3] = i;
        vpos += 5;   /* Make space for the Voronoi neighbours. */
        /* Conversely, add it to the neighbourhood of b and remember it for
         * adding to the neighbourhood of a.  We only know the position when
         * we have a point and the previous one so we would need another.  To
         * preserve mental sanity, just remember the id now and add it later.
         */
        if (!add_point_id(triangulation, next, i, voronoi + vindex[next], n)) {
            g_free(remaining);
            return FALSE;
        }
        remaining[j] = n;
        n++;
    }

    if (G_UNLIKELY(vpos != vsize || n != npoints + nvpoints)) {
        g_free(remaining);
        return FALSE;
    }
    vindex[n] = vpos;

    for (j = 0; j < blen; j++) {
        i = triangulation->boundary[j];
        ni = find_neighbour(voronoi + vindex[i], vindex[i+1] - vindex[i],
                            UNDEF);
        g_assert(ni != UNDEF);
        voronoi[vindex[i] + ni] = remaining[j];
    }
    g_free(remaining);

    /* Now we have created all the Voronoi points so we can add Voronoi
     * neighbours of Voronoi points. */
    for (i = npoints; i < npoints + nvpoints; i++) {
        vpos = vindex[i];
        if (vindex[i+1] - vpos == 5) {
            if (!add_common_neighbour(voronoi, vindex, i, vpos+1, vpos+3,
                                      vpos+2)
                || !add_infinity_neighbour(voronoi, vindex, i, vpos+1, vpos)
                || !add_infinity_neighbour(voronoi, vindex, i, vpos+3, vpos+4))
                return FALSE;
        }
        else {
            if (!add_common_neighbour(voronoi, vindex, i, vpos+1, vpos+3,
                                      vpos+2)
                || !add_common_neighbour(voronoi, vindex, i, vpos+3, vpos+5,
                                         vpos+4)
                || !add_common_neighbour(voronoi, vindex, i, vpos+5, vpos+1,
                                         vpos))
                return FALSE;
        }
    }

    return merge_voronoi_points(triangulation);
}

static void
make_triangulation_empty(Triangulation *triangulation)
{
    triangulation->npoints = 0;
    triangulation->points = NULL;
}

/**
 * gwy_triangulation_new:
 *
 * Creates a new triangulation.
 *
 * Returns: A new empty triangulation.
 *
 * Since: 2.18
 **/
GwyTriangulation*
gwy_triangulation_new(void)
{
    return g_object_new(GWY_TYPE_TRIANGULATION, NULL);
}

/**
 * gwy_triangulation_triangulate:
 * @triangulation: Triangulation.
 * @npoints: Number of points.
 * @points: Array of points.  They must be typecastable to
 *          #GwyXY for triangulation and to
 *          #GwyXYZ for interpolation.  However, they can be
 *          larger than that.  The actual struct size is indicated by
 *          @point_size.
 * @point_size: Size of point struct, in bytes.
 *
 * Finds Delaunay and Voronoi triangulations for a set of points in plane.
 *
 * The triangulation might not work in numerically unstable cases.  At present
 * this includes various ambiguous cases with neighbour points on straight
 * lines or circles.  Also, no points in the input set may coincide.
 *
 * It is possible to call this method successively on several different sets
 * of points to triangulate each separately.  Note that pointers in data
 * returned by methods such as gwy_triangulation_delaunay() become invalid
 * then.
 *
 * Returns: %TRUE on success, %FALSE on failure.  On failure the triangulation
 *          is empty.
 *
 * Since: 2.18
 **/
gboolean
gwy_triangulation_triangulate(GwyTriangulation *object,
                              guint npoints,
                              gconstpointer points,
                              gsize point_size)
{
    return gwy_triangulation_triangulate_iterative(object,
                                                   npoints, points, point_size,
                                                   NULL, NULL);
}

/**
 * gwy_triangulation_triangulate_iterative:
 * @triangulation: Triangulation.
 * @npoints: Number of points.
 * @points: Array of points.  They must be typecastable to #GwyXY for
 *          triangulation and to #GwyXYZ for interpolation.  However, they can
 *          be larger than that.  The actual struct size is indicated by
 *          @point_size.
 * @point_size: Size of point struct, in bytes.
 * @set_fraction: Function that sets fraction to output (or %NULL).
 * @set_message: Function that sets message to output (or %NULL).
 *
 * Finds Delaunay and Voronoi triangulations for a set of points in plane.
 *
 * See gwy_triangulation_triangulate() for discussion.  This function differs
 * only by the optional @set_fraction and @set_message arguments that permit
 * providing user feedback for large triangulations and cancelling the
 * procedure upon request.
 *
 * Returns: %TRUE on success, %FALSE on failure.  On failure the triangulation
 *          is empty.  Cancellation via @set_fraction or @set_message
 *          callback also counts as failure.
 *
 * Since: 2.44
 **/
gboolean
gwy_triangulation_triangulate_iterative(GwyTriangulation *object,
                                        guint npoints,
                                        gconstpointer points,
                                        gsize point_size,
                                        GwySetFractionFunc set_fraction,
                                        GwySetMessageFunc set_message)
{
    Triangulation *triangulation;
    Triangulator *triangulator = NULL;
    PointList pointlist;
    gboolean ok = FALSE;

    gwy_clear(&pointlist, 1);
    if (set_fraction)
        set_fraction(0.0);

    g_return_val_if_fail(GWY_IS_TRIANGULATION(object), FALSE);
    triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);
    make_triangulation_empty(triangulation);
    g_return_val_if_fail(point_size >= sizeof(GwyXY), FALSE);

    /* FIXME: This is not that good initial point order.  We still struggle
     * with points on the convex hull when it consists of straight lines with
     * many points on one line (or very close to it).  We probably need
     * a strategy which starts explicitly from the convex hull. */
    triangulation->point_size = point_size;
    triangulation->points = points;
    build_compact_point_list(&pointlist, npoints, points, point_size);
    if (set_message && !set_message(_("Triangulating...")))
        goto fail;

    if (!(triangulator = triangulate(&pointlist, set_fraction)))
        goto fail;
    if (set_fraction && !set_fraction(0.9))
        goto fail;

    if (!map_to_orig_index(triangulator, &pointlist, triangulation))
        goto fail;
    triangulator_free(triangulator);
    triangulator = NULL;
    if (set_fraction && !set_fraction(0.93))
        goto fail;

    if (!delaunay_to_voronoi(triangulation))
        goto fail;
    if (set_fraction && !set_fraction(1.0))
        goto fail;

    ok = TRUE;

fail:
    triangulator_free(triangulator);
    free_point_list(&pointlist);
    if (!ok)
        make_triangulation_empty(triangulation);

    return ok;
}

static gdouble
smooth_neighbours_nna(const Triangulation *triangulation, guint i,
                      const gdouble *zvalues)
{
    const GwyXY *vpt, *vpt2;
    const GwyXYZ *pt;
    gdouble a[10], b[4];
    gdouble dx, dy, dx2, dy2, z;
    guint j, n, npoints, point_size;
    gconstpointer points;
    const guint *vindex, *voronoi;

    point_size = triangulation->point_size;
    points = triangulation->points;
    vindex = triangulation->vindex;
    voronoi = triangulation->voronoi;
    npoints = triangulation->npoints;
    points = triangulation->points;
    vpt = triangulation->vpoints + (i - npoints);
    gwy_clear(a, 10);
    gwy_clear(b, 4);
    for (j = vindex[i]; j < vindex[i+1]; j++) {
        n = voronoi[j];
        if (n < npoints) {
            pt = get_point_xyz(points, point_size, n);
            dx = pt->x - vpt->x;
            dy = pt->y - vpt->y;
            z = pt->z;
        }
        else {
            vpt2 = triangulation->vpoints + (n - npoints);
            dx = vpt2->x - vpt->x;
            dy = vpt2->y - vpt->y;
            z = zvalues[n - npoints];
        }
        dx2 = dx*dx;
        dy2 = dy*dy;
        a[0] += 1.0;
        a[1] += dx;
        a[2] += dx2;
        a[3] += dy;
        a[4] += dx*dy;
        a[5] += dy2;
        a[6] += dx*dy;
        a[7] += dx2*dy;
        a[8] += dx*dy2;
        a[9] += dx2*dy2;
        b[0] += z;
        b[1] += z*dx;
        b[2] += z*dy;
        b[3] += z*dx*dy;
    }
    if (!gwy_math_choleski_decompose(4, a))
        return zvalues[i - npoints];

    gwy_math_choleski_solve(4, a, b);
    return 0.5*zvalues[i - npoints] + 0.5*b[0];
}

/* Assign Z values to the added Voronoi points. */
static void
calculate_voronoi_zvalues(Triangulation *triangulation)
{
    const guint *vindex, *voronoi;
    guint point_size, npoints, nvpoints, i, j, n;
    gconstpointer points;
    gdouble *zvalues, *averages;
    const GwyXYZ *pt;
    gdouble sw, s;

    npoints = triangulation->npoints;
    nvpoints = triangulation->nvpoints;
    vindex = triangulation->vindex;
    voronoi = triangulation->voronoi;
    point_size = triangulation->point_size;
    points = triangulation->points;
    zvalues = g_renew(gdouble, triangulation->zvalues, 2*nvpoints);
    averages = zvalues + nvpoints;
    triangulation->zvalues = zvalues;

    /* Assign initial values to Voronoi points.  Since a Voronoi point is by
     * definition in the same distance from all neighbour Delaunay vertices use
     * a plain average. */
    for (i = npoints; i < npoints + nvpoints; i++) {
        sw = s = 0.0;
        for (j = vindex[i]; j < vindex[i+1]; j++) {
            n = voronoi[j];
            if (n >= npoints)
                continue;

            pt = get_point_xyz(points, point_size, n);
            sw += 1.0;
            s += pt->z;
        }
        zvalues[i - npoints] = s/sw;
    }

    /* Now smooth them so that very close points receive essentially the same
     * value. */
    for (i = npoints; i < npoints + nvpoints; i++) {
        averages[i - npoints] = smooth_neighbours_nna(triangulation, i,
                                                      zvalues);
    }
    for (i = npoints; i < npoints + nvpoints; i++) {
        zvalues[i - npoints] = smooth_neighbours_nna(triangulation, i,
                                                     averages);
    }
    for (i = npoints; i < npoints + nvpoints; i++) {
        averages[i - npoints] = smooth_neighbours_nna(triangulation, i,
                                                      zvalues);
    }
    for (i = npoints; i < npoints + nvpoints; i++) {
        zvalues[i - npoints] = smooth_neighbours_nna(triangulation, i,
                                                     averages);
    }
}

/* If TRUE is returned, then a neighbour on the other side was found and the
 * triangle has become clockwise.  If TRUE is returned, then @opposite is
 * unchanged and the triangle is kept counter-clockwise. */
static gboolean
find_the_other_vneighbour(const Triangulation *triangulation,
                          guint from,
                          guint to,
                          guint *opposite)
{
    const GwyXY *a, *b, *c;
    guint to_prev, from_next, pos, len;
    const guint *neighbours;

    pos = triangulation->vindex[from];
    len = triangulation->vindex[from + 1] - pos;
    neighbours = triangulation->voronoi + pos;
    to_prev = find_prev_neighbour(neighbours, len, to);
    if (G_UNLIKELY(to_prev == UNDEF))
        return FALSE;

    pos = triangulation->vindex[to];
    len = triangulation->vindex[to + 1] - pos;
    neighbours = triangulation->voronoi + pos;
    from_next = find_next_neighbour(neighbours, len, from);
    if (G_UNLIKELY(from_next == UNDEF))
        return FALSE;

    /* Now there are some silly few-point special cases.  If @opposite is in
     * the centre of a triangle formed by @from, @to and the newly found point,
     * then we have an apparent match but it is not the point we are looking
     * for.  Check that the points really lies on the opposite side. */
    if (from_next != to_prev || from_next == *opposite)
        return FALSE;

    a = get_vpoint(triangulation, from);
    b = get_vpoint(triangulation, to);
    c = get_vpoint(triangulation, to_prev);
    if (!point_on_right_side(a, b, c))
        return FALSE;

    *opposite = to_prev;
    return TRUE;
}

static inline gboolean
move_vtriangle_a(const Triangulation *triangulation, Triangle *vtriangle)
{
    if (find_the_other_vneighbour(triangulation, vtriangle->ib, vtriangle->ic,
                                  &vtriangle->ia)) {
        GWY_SWAP(guint, vtriangle->ib, vtriangle->ic);
        GWY_SWAP(const GwyXYZ*, vtriangle->b, vtriangle->c);
        return TRUE;
    }
    return FALSE;
}

static inline gboolean
move_vtriangle_b(const Triangulation *triangulation, Triangle *vtriangle)
{
    if (find_the_other_vneighbour(triangulation, vtriangle->ic, vtriangle->ia,
                                  &vtriangle->ib)) {
        GWY_SWAP(guint, vtriangle->ic, vtriangle->ia);
        GWY_SWAP(const GwyXYZ*, vtriangle->c, vtriangle->a);
        return TRUE;
    }
    return FALSE;
}

static inline gboolean
move_vtriangle_c(const Triangulation *triangulation, Triangle *vtriangle)
{
    if (find_the_other_vneighbour(triangulation, vtriangle->ia, vtriangle->ib,
                                  &vtriangle->ic)) {
        GWY_SWAP(guint, vtriangle->ia, vtriangle->ib);
        GWY_SWAP(const GwyXYZ*, vtriangle->a, vtriangle->b);
        return TRUE;
    }
    return FALSE;
}

/* This assumes a counter-clockwise triangle */
static void
make_vtriangle(Triangle *triangle, const Triangulation *triangulation)
{
    /* XXX: In the triangulation algoritm, the points are in fact only XY,
     * but the Z members are never accessed so the typecast is all right. */
    triangle->a = get_vpoint_xyz(triangulation, triangle->ia);
    triangle->b = get_vpoint_xyz(triangulation, triangle->ib);
    triangle->c = get_vpoint_xyz(triangulation, triangle->ic);

    make_triangle_side(&triangle->sa, triangle->b, triangle->c, triangle->a);
    make_triangle_side(&triangle->sb, triangle->c, triangle->a, triangle->b);
    make_triangle_side(&triangle->sc, triangle->a, triangle->b, triangle->c);
}

/* Ensures @triangle contains point @pt.  A relatively quick test if it already
 * contains the point.  If the right triangle is nearby, it is also found
 * reasonably fast. */
static gboolean
ensure_vtriangle(const Triangulation *triangulation,
                 Triangle *vtriangle,
                 const GwyXY *pt)
{
    gboolean moved;
    guint iter;

    iter = 0;
    while (!triangle_contains_point(vtriangle, pt)) {
        if (vtriangle->da <= vtriangle->db) {
            if (vtriangle->da <= vtriangle->dc)
                moved = move_vtriangle_a(triangulation, vtriangle);
            else
                moved = move_vtriangle_c(triangulation, vtriangle);
        }
        else {
            if (vtriangle->db <= vtriangle->dc)
                moved = move_vtriangle_b(triangulation, vtriangle);
            else
                moved = move_vtriangle_c(triangulation, vtriangle);
        }

        if (!moved)
            return FALSE;

        make_vtriangle(vtriangle, triangulation);
        if (G_UNLIKELY(iter++ == triangulation->nvpoints)) {
            vtriangle->ia = vtriangle->ib = vtriangle->ic = UNDEF;
            return FALSE;
        }
    }

    return TRUE;
}

/* Initializes @vtriangle to any valid triangle containing point @hint. */
static void
make_valid_vtriangle(const Triangulation *triangulation,
                     Triangle *vtriangle,
                     guint hint)
{
    const GwyXY *a = get_vpoint(triangulation, hint);
    const GwyXY *b, *c;
    const guint *neighbours;
    gdouble phib, phic;
    guint i, len;

    vtriangle->ia = i = hint;
    neighbours = triangulation->voronoi + triangulation->vindex[i];
    len = triangulation->vindex[i+1] - triangulation->vindex[i];
    for (i = 0; i < len; i++) {
        vtriangle->ib = neighbours[i];
        vtriangle->ic = next_neighbour(neighbours, len, i);

        b = get_vpoint(triangulation, vtriangle->ib);
        phib = atan2(b->y - a->y, b->x - a->x);
        c = get_vpoint(triangulation, vtriangle->ic);
        phic = atan2(c->y - a->y, c->x - a->x);

        if (ccw_angle_convex(phib, phic)) {
            make_vtriangle(vtriangle, triangulation);
            return;
        }
    }

    g_assert_not_reached();
}

static gboolean
interpolate_round(Triangulation *triangulation,
                  Triangle *vtriangle,
                  const GwyXY *pt,
                  gdouble *value)
{
    const GwyXYZ *p = NULL;

    ensure_vtriangle(triangulation, vtriangle, pt);
    if (G_UNLIKELY(vtriangle->ia == UNDEF)) {
        *value = 0.0;
        return FALSE;
    }

    if (vtriangle->ia < triangulation->npoints)
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ia);
    else if (vtriangle->ib < triangulation->npoints)
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ib);
    else if (vtriangle->ic < triangulation->npoints)
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ic);

    if (p) {
        *value = p->z;
        return TRUE;
    }
    else {
        *value = 0.0;
        return FALSE;
    }
}

static inline gdouble
triangle_interpolate(gdouble da, gdouble za,
                     gdouble db, gdouble zb,
                     gdouble dc, gdouble zc)
{
    gdouble wsum = da + db + dc;

    return (da*za + db*zb + dc*zc)/wsum;
}

static gboolean
interpolate_nna(Triangulation *triangulation,
                Triangle *vtriangle,
                const GwyXY *pt,
                gdouble *value)
{
    const GwyXYZ *p = NULL;
    const gdouble *zvalues = triangulation->zvalues;
    guint npts = triangulation->npoints;
    gdouble za, zb, zc;

    ensure_vtriangle(triangulation, vtriangle, pt);
    if (G_UNLIKELY(vtriangle->ia == UNDEF)) {
        *value = 0.0;
        return FALSE;
    }

    if (vtriangle->ia < triangulation->npoints) {
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ia);
        za = p->z;
        zb = zvalues[vtriangle->ib - npts];
        zc = zvalues[vtriangle->ic - npts];
    }
    else if (vtriangle->ib < triangulation->npoints) {
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ib);
        za = zvalues[vtriangle->ia - npts];
        zb = p->z;
        zc = zvalues[vtriangle->ic - npts];
    }
    else if (vtriangle->ic < triangulation->npoints) {
        p = get_point_xyz(triangulation->points, triangulation->point_size,
                          vtriangle->ic);
        za = zvalues[vtriangle->ia - npts];
        zb = zvalues[vtriangle->ib - npts];
        zc = p->z;
    }
    else {
        *value = 0.0;
        return FALSE;
    }

    *value = triangle_interpolate(vtriangle->da, za,
                                  vtriangle->db, zb,
                                  vtriangle->dc, zc);
    return TRUE;
}

static inline gdouble
sinterpolate1_linear(gconstpointer points, gsize point_size,
                     guint ia, guint ib,
                     const GwyXY *pt)
{
    const GwyXYZ *a = get_point_xyz(points, point_size, ia);
    const GwyXYZ *b = get_point_xyz(points, point_size, ib);
    gdouble d = side_intersection_distance(a, b, pt);

    if (d <= -1.0)
        return a->z;
    else if (d >= 1.0)
        return b->z;

    return 0.5*((d + 1.0)*b->z + (1.0 - d)*a->z);
}

static inline gdouble
tinterpolate_linear(const Triangle *triangle)
{
    return triangle_interpolate(triangle->da, triangle->a->z,
                                triangle->db, triangle->b->z,
                                triangle->dc, triangle->c->z);
}

static inline gdouble
sinterpolate_linear(const Triangulation *triangulation,
                    const Triangle *triangle, const GwyXY *pt)
{
    guint ia, ib;

    ia = triangle->ia;
    ib = triangle->ib;
    if (find_nearest_side(triangulation, &ia, &ib, pt))
        goto success;

    ia = triangle->ib;
    ib = triangle->ic;
    if (find_nearest_side(triangulation, &ia, &ib, pt))
        goto success;

    ia = triangle->ic;
    ib = triangle->ia;
    if (find_nearest_side(triangulation, &ia, &ib, pt))
        goto success;

    g_assert_not_reached();
    return 0.0;

success:
    return sinterpolate1_linear(triangulation->points,
                                triangulation->point_size,
                                ia, ib, pt);
}

static gboolean
interpolate_linear(Triangulation *triangulation,
                   Triangle *triangle,
                   const GwyXY *pt,
                   gdouble *value)
{
    if (ensure_triangle(triangulation, triangle, pt))
        *value = tinterpolate_linear(triangle);
    else {
        if (G_UNLIKELY(triangle->ia == UNDEF))
            return FALSE;
        *value = sinterpolate_linear(triangulation, triangle, pt);
    }
    return TRUE;
}

/**
 * gwy_triangulation_interpolate:
 * @triangulation: Triangulation.
 * @interpolation: Interpolation to use.  Only @GWY_INTERPOLATION_ROUND and
 *                 @GWY_INTERPOLATION_LINEAR are implemented.  Is is an error
 *                 to pass any other interpolation type.
 * @dfield: Data field to fill with interpolated values.
 *
 * Regularizes XYZ data to a grid, represented by a data field.
 *
 * The area and resolution of the regular grid is given by the dimensions and
 * offsets of @dfield.
 *
 * Returns: %TRUE if the interpolation succeeds, %FALSE on failure, e.g. due to
 *          numerical errors.  In the latter case the contents of @dfield is
 *          undefined.
 *
 * Since: 2.18
 **/
gboolean
gwy_triangulation_interpolate(GwyTriangulation *object,
                              GwyInterpolationType interpolation,
                              GwyDataField *dfield)
{
    Triangulation *triangulation;
    guint xres, yres, i, j;
    gdouble qx, qy, xoff, yoff;
    gdouble *d;
    Triangle triangle;
    gboolean ok = FALSE;
    GwyXY pt;

    g_return_val_if_fail(GWY_IS_TRIANGULATION(object), FALSE);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(dfield), FALSE);
    triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);
    g_return_val_if_fail(triangulation->point_size >= sizeof(GwyXYZ), FALSE);
    g_return_val_if_fail(interpolation == GWY_INTERPOLATION_LINEAR
                         || interpolation == GWY_INTERPOLATION_NNA
                         || interpolation == GWY_INTERPOLATION_ROUND, FALSE);

    if (interpolation == GWY_INTERPOLATION_LINEAR)
        make_valid_triangle(triangulation->neighbours, triangulation->nindex[1],
                            triangulation->points, triangulation->point_size,
                            &triangle, 0);
    else
        make_valid_vtriangle(triangulation, &triangle, 0);

    if (interpolation == GWY_INTERPOLATION_NNA)
        calculate_voronoi_zvalues(triangulation);

    xres = dfield->xres;
    yres = dfield->yres;
    xoff = dfield->xoff;
    yoff = dfield->yoff;
    qx = dfield->xreal/dfield->xres;
    qy = dfield->yreal/dfield->yres;
    d = dfield->data;

    for (i = 0; i < yres; i++) {
        pt.y = yoff + qy*(i + 0.5);
        for (j = 0; j < xres; j++) {
            pt.x = xoff + qx*(j + 0.5);
            if (interpolation == GWY_INTERPOLATION_LINEAR)
                ok = interpolate_linear(triangulation, &triangle, &pt, d);
            else if (interpolation == GWY_INTERPOLATION_NNA)
                ok = interpolate_nna(triangulation, &triangle, &pt, d);
            else
                ok = interpolate_round(triangulation, &triangle, &pt, d);
            if (!ok)
                goto fail;
            d++;
        }
    }
    ok = TRUE;

fail:
    gwy_data_field_invalidate(dfield);

    return ok;
}

/**
 * gwy_triangulation_data_free:
 * @triangulation_data: Raw triangulation data.
 *
 * Frees raw triangulation data.
 *
 * This function should be used to free triangulation data returned by
 * gwy_triangulation_delaunay() and similar.  It does not free the array
 * members as they are owned by the triangulation object.
 *
 * Since: 2.18
 **/
void
gwy_triangulation_data_free(GwyTriangulationData *triangulation_data)
{
    g_free(triangulation_data);
    /* The rest is owned by the object. */
}

/**
 * gwy_triangulation_delaunay:
 * @triangulation: Triangulation.
 *
 * Obtains the Delaunay triangulation data.
 *
 * Notes to the fields in the returned struct:
 *
 * @npoints equals to the number of points passed to
 * gwy_triangulation_triangulate().
 *
 * Returns: Newly clreated #GwyTriangulationData that must be freed with
 *          gwy_triangulation_data_free() when no longer used.  The data within
 *          is owned by @triangulation, see #GwyTriangulationData.
 *
 * Since: 2.18
 **/
GwyTriangulationData*
gwy_triangulation_delaunay(GwyTriangulation *object)
{
    Triangulation *triangulation;
    GwyTriangulationData *data = NULL;

    g_return_val_if_fail(GWY_IS_TRIANGULATION(object), NULL);
    triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);
    if (triangulation->npoints) {
        data = g_new(GwyTriangulationData, 1);
        data->npoints = triangulation->npoints;
        data->size = triangulation->nsize;
        data->index = triangulation->nindex;
        data->neighbours = triangulation->neighbours;
    }

    return data;
}

/**
 * gwy_triangulation_boundary:
 * @triangulation: Triangulation.
 *
 * Obtains the boundary, i.e. convex hull, of Delaunay triangulation.
 *
 * Notes to the fields in the returned struct:
 *
 * @npoints equals to the number of points passed to
 * gwy_triangulation_triangulate().
 *
 * @size is the boundary length.
 *
 * @index[] contains point indices in the boundary for points on the boundary;
 * and %GWY_TRIANGULATION_NONE for points not on the boundary.
 *
 * @neighbours[] lists sequentially the boundary points.
 *
 * Returns: Newly clreated #GwyTriangulationData that must be freed with
 *          gwy_triangulation_data_free() when no longer used.  The data within
 *          is owned by @triangulation, see #GwyTriangulationData.
 *
 * Since: 2.18
 **/
GwyTriangulationData*
gwy_triangulation_boundary(GwyTriangulation *object)
{
    Triangulation *triangulation;
    GwyTriangulationData *data = NULL;

    g_return_val_if_fail(GWY_IS_TRIANGULATION(object), NULL);
    triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);
    if (triangulation->npoints) {
        data = g_new(GwyTriangulationData, 1);
        data->npoints = triangulation->npoints;
        data->size = triangulation->blen;
        data->index = triangulation->bindex;
        data->neighbours = triangulation->boundary;
    }

    return data;
}

/**
 * gwy_triangulation_voronoi:
 * @triangulation: Triangulation.
 * @nvpoints: Location to store the number of new Voronoi triangulation points,
 *            or %NULL.
 * @vpoints: Location to store pointer to the Voronoi triangulation points,
 *           or %NULL.
 *
 * Obtains the Voronoi triangulation data.
 *
 * Notes to the fields in the returned struct:
 *
 * @npoints equals to the number of Delaunay triangulation points passed to
 * gwy_triangulation_triangulate() plus the number of points in the Voronoi
 * triangulation, @nvpoints.  Voronoi triangulation points are the vertices of
 * Voronoi cells.  So each triangle has one original (Delaunay) point and two
 * cell vertices (Voronoi points).
 *
 * @index[] is the usual index of blocks in @neighbours, however,
 * point indices smaller than the number of Delaunay points correspond to the
 * Delaunay points, point indices equal or larger correspond to points in
 * @vpoints (it is necessary to subtract the number of original points to
 * obtain the real position in @vpoints).
 *
 * @neighbours[] contains the neighbour blocks, with above caveats about
 * point numbering.
 *
 * Returns: Newly clreated #GwyTriangulationData that must be freed with
 *          gwy_triangulation_data_free() when no longer used.  The data within
 *          is owned by @triangulation, see #GwyTriangulationData.
 *
 * Since: 2.18
 **/
GwyTriangulationData*
gwy_triangulation_voronoi(GwyTriangulation *object,
                          guint *nvpoints,
                          const GwyXY **vpoints)
{
    Triangulation *triangulation;
    GwyTriangulationData *data = NULL;

    g_return_val_if_fail(GWY_IS_TRIANGULATION(object), NULL);
    triangulation = GWY_TRIANGULATION_GET_PRIVATE(object);
    if (triangulation->npoints) {
        data = g_new(GwyTriangulationData, 1);
        /* This is the size of index[] which is the sum of original and
         * Voronoi triangulation points. */
        data->npoints = triangulation->nvpoints + triangulation->npoints;
        data->size = triangulation->vsize;
        data->index = triangulation->vindex;
        data->neighbours = triangulation->voronoi;
        if (nvpoints)
            *nvpoints = triangulation->nvpoints;
        if (vpoints)
            *vpoints = triangulation->vpoints;
    }
    else {
        if (nvpoints)
            *nvpoints = 0;
        if (vpoints)
            *vpoints = NULL;
    }

    return data;
}

#ifdef DEBUG
G_GNUC_UNUSED
static gint
compare_uints(gconstpointer pa, gconstpointer pb)
{
    const guint *a = (const guint *)pa;
    const guint *b = (const guint *)pb;

    if (*a < *b)
        return -1;
    if (*a > *b)
        return 1;
    return 0;
}

G_GNUC_UNUSED
static void
dump_neighbours(const Triangulator *triangulator)
{
    static GArray *blockstarts = NULL;               /* Threads: debug only */

    guint i, j, vundef[2];
    guint *a;

    if (!blockstarts)
        blockstarts = g_array_new(FALSE, FALSE, 2*sizeof(guint));

    g_array_set_size(blockstarts, triangulator->npoints);
    a = (guint*)blockstarts->data;
    for (j = 0; j < triangulator->npoints; j++) {
        a[2*j] = triangulator->blocks[j].pos;
        a[2*j + 1] = j;
    }
    g_array_sort(blockstarts, compare_uints);
    vundef[0] = vundef[1] = UNDEF;
    g_array_append_val(blockstarts, vundef);

    a = (guint*)blockstarts->data;
    j = 0;
    for (i = 0; i < triangulator->nlen; i++) {
        if (i == a[2*j]) {
            g_print("(%u)", a[2*j + 1]);
            j++;
        }
        if (triangulator->neighbours[i] == UNDEF)
            g_print(".");
        else
            g_print("%u", triangulator->neighbours[i]);
        g_print(" ");
    }
    g_print("\n");

    for (i = 0; i < triangulator->npoints; i++) {
        if (triangulator->boundary[i].next != UNDEF)
            break;
    }
    if (i < triangulator->npoints) {
        j = i;
        g_print("%u", i);
        do {
            j = triangulator->boundary[j].next;
            g_print("--%u", j);
        } while (j != i);
        g_print("\n");
    }
}

G_GNUC_UNUSED
static void
dump_triangulator(const Triangulator *triangulator)
{
    NeighbourBlock *nb;
    guint i, j;

    for (i = 0; i < triangulator->npoints; i++) {
        nb = triangulator->blocks + i;
        g_print("%u:", i);
        for (j = 0; j < nb->len; j++)
            g_print(" %u", triangulator->neighbours[nb->pos + j]);
        g_print("\n");
    }
}

G_GNUC_UNUSED
static void
dump_points_dat(gconstpointer points, guint npoints, guint point_size)
{
    guint i;
    FILE *fh;

    fh = gwy_fopen("points.dat", "w");
    for (i = 0; i < npoints; i++) {
        const GwyXY *pt = get_point(points, point_size, i);
        gwy_fprintf(fh, "%u %g %g\n", i, pt->x, pt->y);
    }
    fclose(fh);
}

G_GNUC_UNUSED
static void
dump_missing_points(const UIntQueue *queue, PointList *pointlist)
{
    guint i;
    FILE *fh;

    fh = gwy_fopen("xpoints.dat", "w");
    for (i = queue->pos; i < queue->len; i++) {
        guint iorig = queue->data[i];
        guint iorigorig = pointlist->orig_index[iorig];
        const GwyXY *pt = get_point(pointlist->points, sizeof(GwyXY), iorig);
        gwy_fprintf(fh, "%u %g %g\n", iorigorig, pt->x, pt->y);
    }
    fclose(fh);
}

G_GNUC_UNUSED
static void
dump_points_(const Triangulator *triangulator,
             guint npoints, gconstpointer points, gsize point_size)
{
    NeighbourBlock *nb;
    guint i, j, ni;
    const guint *neighbours;
    FILE *fh;

    dump_points_dat(points, npoints, point_size);

    fh = gwy_fopen("delaunay.dat", "w");
    for (i = 0; i < triangulator->npoints; i++) {
        nb = triangulator->blocks + i;
        neighbours = triangulator->neighbours + nb->pos;
        for (j = 0; j < nb->len; j++) {
            ni = neighbours[j];
            if (TRUE || ni > i) {
                const GwyXY *pt1 = get_point(points, point_size, i);
                const GwyXY *pt2 = get_point(points, point_size, ni);
                gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                            pt1->x, pt1->y, pt2->x, pt2->y);
            }
        }
    }
    fclose(fh);

    fh = gwy_fopen("delaunayb.dat", "w");
    for (i = 0; i < triangulator->npoints; i++) {
        ni = triangulator->boundary[i].prev;
        if (ni != UNDEF) {
            const GwyXY *pt1 = get_point(points, point_size, i);
            const GwyXY *pt2 = get_point(points, point_size, ni);
            gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                        pt1->x, pt1->y, pt2->x, pt2->y);
        }
    }
    fclose(fh);
}

G_GNUC_UNUSED
static void
dump_points(const Triangulation *triangulation)
{
    gconstpointer points = triangulation->points;
    gsize point_size = triangulation->point_size;
    guint npoints = triangulation->npoints;
    guint i, j, ni, pos, len;
    const guint *neighbours;
    FILE *fh;

    dump_points_dat(points, npoints, point_size);

    fh = gwy_fopen("delaunay.dat", "w");
    for (i = 0; i < npoints; i++) {
        pos = triangulation->nindex[i];
        len = triangulation->nindex[i+1] - pos;
        neighbours = triangulation->neighbours + pos;
        for (j = 0; j < len; j++) {
            ni = neighbours[j];
            if (ni > i) {
                const GwyXY *pt1 = get_point(points, point_size, i);
                const GwyXY *pt2 = get_point(points, point_size, ni);
                gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                            pt1->x, pt1->y, pt2->x, pt2->y);
            }
        }
    }
    fclose(fh);

    fh = gwy_fopen("delaunayb.dat", "w");
    for (j = 0; j < triangulation->blen; j++) {
        i = triangulation->boundary[j];
        ni = triangulation->boundary[(j + 1) % triangulation->blen];
        {
            const GwyXY *pt1 = get_point(points, point_size, i);
            const GwyXY *pt2 = get_point(points, point_size, ni);
            gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                        pt1->x, pt1->y, pt2->x, pt2->y);
        }
    }
    fclose(fh);
}

G_GNUC_UNUSED
static void
dump_voronoi(const Triangulation *triangulation)
{
    gconstpointer points = triangulation->points;
    gsize point_size = triangulation->point_size;
    guint i, j, ni, pos, len, npts;
    const guint *neighbours;
    FILE *fh;

    npts = triangulation->npoints;

    fh = gwy_fopen("vpoints.dat", "w");
    for (i = 0; i < triangulation->nvpoints; i++) {
        const GwyXY *pt = triangulation->vpoints + i;
        gwy_fprintf(fh, "%u %g %g\n", i, pt->x, pt->y);
    }
    fclose(fh);

    fh = gwy_fopen("voronoid.dat", "w");
    for (i = 0; i < triangulation->nvpoints; i++) {
        pos = triangulation->vindex[i + npts];
        len = triangulation->vindex[i+1 + npts] - pos;
        neighbours = triangulation->voronoi + pos;
        for (j = 0; j < len; j++) {
            ni = neighbours[j];
            if (ni < npts) {
                const GwyXY *pt1 = triangulation->vpoints + i;
                const GwyXY *pt2 = get_point(points, point_size, ni);
                gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                            pt1->x, pt1->y, pt2->x, pt2->y);
            }
        }
    }
    fclose(fh);

    fh = gwy_fopen("voronoiv.dat", "w");
    for (i = 0; i < triangulation->nvpoints; i++) {
        pos = triangulation->vindex[i + npts];
        len = triangulation->vindex[i+1 + npts] - pos;
        neighbours = triangulation->voronoi + pos;
        for (j = 0; j < len; j++) {
            ni = neighbours[j];
            if (ni >= npts) {
                ni -= npts;
                if (ni > i) {
                    const GwyXY *pt1 = triangulation->vpoints + i;
                    const GwyXY *pt2 = triangulation->vpoints + ni;
                    gwy_fprintf(fh, "%g %g\n%g %g\n\n",
                                pt1->x, pt1->y, pt2->x, pt2->y);
                }
            }
        }
    }
    fclose(fh);
}
#endif

/************************** Documentation ****************************/

/**
 * SECTION:triangulation
 * @title: triangulation
 * @short_description: Delaunay and Voronoi triangulation and interpolation
 **/

/**
 * GWY_TRIANGULATION_NONE:
 *
 * Point index value representing no point.
 *
 * Since: 2.18
 **/

/**
 * GwyTriangluationPointXY:
 * @x: X-coordinate.
 * @y: Y-coordinate.
 *
 * Representation of a point in plane for triangulation.
 *
 * Note this is an alias for #GwyXY since 2.45.
 *
 * Since: 2.18
 **/

/**
 * GwyTriangulationPointXYZ:
 * @x: X-coordinate.
 * @y: Y-coordinate.
 * @z: Z-coordinate, i.e. the value in point (@x,@y).
 *
 * Representation of a point in plane with associated value for interpolation.
 *
 * Note this is an alias for #GwyXYZ since 2.45.
 *
 * Since: 2.18
 **/

/**
 * GwyTriangulationData:
 * @npoints: Number of points in the set, also detrmines the size of @index.
 * @size: The length of @neighbours.
 * @index: Array of size @npoints+1 defining the blocks of neighbours in
 *         @neighbours.  The block for point @i starts at @index[@i] and ends
 *         one element before @index[@i+1].  Hence the last of @index is equal
 *         to @size.
 * @neighbours: Neighbours of each point, represented as indices into some
 *              array (which array, that depends on what kind of data it is).
 *              The points in each block are sorted counter-clockwise.
 *
 * Representation of raw triangulation data.
 *
 * Members @index and @neighbours are owned by the #GwyTriangulation object
 * that provided this data and remain valid only until this object is destroyed
 * or used to perform another triangulation.
 *
 * The exact interpretation of individual parts depends on what kind of
 * triangulation data it is and may differ a bit from the general description
 * provided here.  See the descriptions of individual methods returning
 * #GwyTriangulationData.
 *
 * Since: 2.18
 **/

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
