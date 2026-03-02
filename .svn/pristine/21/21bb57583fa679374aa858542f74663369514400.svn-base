/*
   Library of mathematical morphology and tip estimation routines
   written by John Villarrubia, National Institute of Standards and
   Technology, an agency of the U.S. Department of Commerce,
   Gaithersburg, MD 20899, USA.

   The algorithms presented below are intended to be used for research
   purposes only and bear no warranty, either express or implied.
   Please note that within the United States, copyright protection,
   under Section 105 of the United States Code, Title 17, is not
   available for any work of the United States Government and/or for
   any works conceived by United States Government employees under this
   software release. User acknowledges that Villarrubia's actual work
   is in the public domain and is not subject to copyright. However, if
   User utilizes the aforementioned government-created algorithms in a
   manner which substantially alters User's work, User agrees to
   acknowledge this by reference to Villarrubia's papers, which are
   listed below.

    J. S. Villarubia: J. Res. Natl. Inst. Stand. Technol. 102  (1997) 425.
*/

/* Technical and formal modification by Petr Klapetek and David Necas (Yeti),
 * 2004 to fit better in Gwyddion */


#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"
#include "morph_lib.h"

/*static members forward declaration*/
static gint
itip_estimate_iter(const gint *const *image,
                   gint xres, gint yres, gint txres, gint tyres,
                   gint xc, gint yc, gint **tip0,
                   gint thresh, gboolean use_edges,
                   gint iter,
                   GwySetMessageFunc set_message,
                   GwySetFractionFunc set_fraction);

static gboolean
useit(gint x, gint y, const gint *const *image, gint sx, gint sy, gint delta);

static gint**
iopen(const gint *const *image, gint xres, gint yres,
      const gint *const *tip, gint txres, gint tyres,
      GwySetMessageFunc set_message, GwySetFractionFunc set_fraction);

static gint
itip_estimate_point(gint ixp, gint jxp, const gint *const *image,
                    gint xres, gint yres, gint txres, gint tyres,
                    gint xc, gint yc, gint **tip0, gint thresh,
                    gboolean use_edges);

/*end of forward declarations*/


/**
 * _gwy_morph_lib_iallocmatrix:
 * @yres: Rows number.
 * @xres: Columns number.
 *
 * Allocates an integer matrix of dimension [yres][xres] using an array
 * of pointers to rows. yres is the number of rows. xres is the number
 * of columns.
 *
 * Warning: The argument order is y, x.
 *
 * Returns: Alocated matrix.
 **/
gint**
_gwy_morph_lib_iallocmatrix(gint yres, gint xres)
{
    gint **mptr;                /* points to allocated matrix */
    gint *mem;
    gint i;                     /* counter */

    /* The actual storage */
    mem = g_new(gint, xres*yres);

    /* The pointers to rows */
    mptr = g_new(gint*, yres);
    for (i = 0; i < yres; i++)
        mptr[i] = mem + i*xres;

    return mptr;
}

static gint**
idupmatrix(gint **a, gint yres, gint xres)
{
    gint **b = _gwy_morph_lib_iallocmatrix(yres, xres);

    /* The storage is actually a single block. */
    gwy_assign(b[0], a[0], xres*yres);

    return b;
}

/**
 * _gwy_morph_lib_ifreematrix:
 * @mptr: Pointer to matrix.
 *
 * Frees memory allocated with allocmatrix.
 **/
void
_gwy_morph_lib_ifreematrix(gint **mptr)
{
    if (!mptr)
        return;

    g_free(mptr[0]);
    g_free(mptr);
}

#define idupmatrix_omp_if_threads(a,yres,xres) \
    (gwy_omp_num_threads() > 1 ? idupmatrix((a), (yres), (xres)) : (a))

/**
 * _gwy_morph_lib_ireflect:
 * @image: Integer array to be reflected.
 * @xres: Number of columns.
 * @yres: Number of rows.
 *
 * Perform reflection of integer array.
 *
 * Returns: Reflected array.
 **/
gint**
_gwy_morph_lib_ireflect(const gint *const *image, gint xres, gint yres)
{
    gint **result;
    gint i, j;                  /* index */

    /* create output array of appropriate size */
    result = _gwy_morph_lib_iallocmatrix(yres, xres);

    for (j = 0; j < yres; j++) { /* Loop over all points in output array */
        for (i = 0; i < xres; i++) {
            result[j][i] = -image[yres - 1 - j][xres - 1 - i];
        }
    }
    return result;
}

/**
 * gwy_morph_lib_idilation:
 * @image: Surface array.
 * @im_xsiz: Number of columns.
 * @im_ysiz: Number of rows.
 * @tip: Tip array.
 * @tip_xsiz: Number of columns.
 * @tip_ysiz: Number of rows.
 * @xc: Tip apex column coordinate.
 * @yc: Tip apex row coordinate.
 * @set_fraction: Function that sets fraction to output (or %NULL).
 * @set_message: Function that sets message to output (or %NULL).
 *
 * Performs dilation algorithm (for integer arrays).
 *
 * Returns: Dilated data (newly allocated).  May return %NULL if aborted.
 **/
static gint**
gwy_morph_lib_idilation(const gint *const *image, gint xres, gint yres,
                        const gint *const *tip, gint txres, gint tyres,
                        gint xc, gint yc,
                        GwySetMessageFunc set_message,
                        GwySetFractionFunc set_fraction)
{
    gint **result;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    if ((set_message && !set_message(_("Dilation...")))
        || (set_fraction && !set_fraction(0.0)))
        return NULL;

    /* create output array of appropriate size */
    result = _gwy_morph_lib_iallocmatrix(yres, xres);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(image,tip,result,xres,yres,txres,tyres,xc,yc,set_fraction,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j;

        /* Loop over all points in output array */
        for (i = ifrom; i < ito; i++) {
            /* Compute the allowed range of py. This may be different from
             * the full range of the tip due to edge overlaps. */
            gint pymin = MAX(i - yres + 1, -yc);
            gint pymax = MIN(tyres - yc - 1, i);
            for (j = 0; j < xres; j++) {
                /* Compute the allowed range of px. This may be different from
                * the full range of the tip due to edge overlaps. */
                gint pxmin = MAX(j - xres + 1, -xc);
                gint pxmax = MIN(txres - xc - 1, j);
                gint max, px, py;

                max = image[i - pymin][j - pxmin] + tip[pymin + yc][pxmin + xc];
                /* Loop over points in tip */
                for (px = pxmin; px <= pxmax; px++) {
                    for (py = pymin; py <= pymax; py++) {
                        gint temp = image[i - py][j - px] + tip[py + yc][px + xc];
                        max = MAX(temp, max);
                    }
                }
                result[i][j] = max;
            }

            if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito,
                                                  pcancelled))
                break;
        }
    }

    if (cancelled) {
        _gwy_morph_lib_ifreematrix(result);
        return NULL;
    }

    return result;
}

/**
 * _gwy_morph_lib_ierosion:
 * @surface: Surface array.
 * @surf_xsiz: Number of columns.
 * @surf_ysiz: Number of rows.
 * @tip: Tip array.
 * @txres: Number of columns.
 * @tyres: Number of rows.
 * @xc: Tip apex column coordinate.
 * @yc: Tip apex row coordinate.
 * @set_fraction: Function that sets fraction to output (or %NULL).
 * @set_message: Function that sets message to output (or %NULL).
 *
 * Performs erosion algorithm (for integer arrays).
 *
 * Returns: Eroded data (newly allocated). May return %NULL if aborted.
 **/
gint**
_gwy_morph_lib_ierosion(const gint *const *image, gint xres, gint yres,
                        const gint *const *tip, gint txres, gint tyres,
                        gint xc, gint yc,
                        GwySetMessageFunc set_message,
                        GwySetFractionFunc set_fraction)
{
    gint **result;
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    if ((set_message && !set_message(_("Erosion...")))
        || (set_fraction && !set_fraction(0.0)))
        return NULL;

    /* create output array of appropriate size */
    result = _gwy_morph_lib_iallocmatrix(yres, xres);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(image,tip,result,xres,yres,txres,tyres,xc,yc,set_fraction,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres), ito = gwy_omp_chunk_end(yres);
        gint i, j;

        /* Loop over all points in output array */
        for (i = ifrom; i < ito; i++) {
            /* Compute the allowed range of py. This may be different from
             * the full range of the tip due to edge overlaps. */
            gint pymin = MAX(-i, -yc);
            gint pymax = MIN(tyres - yc, yres - i) - 1;

            for (j = 0; j < xres; j++) {
                /* Compute the allowed range of px. This may be different from
                 * the full range of the tip due to edge overlaps. */
                gint pxmin = MAX(-xc, -j);
                gint pxmax = MIN(txres - xc, xres - j) - 1;
                gint min, px, py;

                min = image[i + pymin][j + pxmin] - tip[pymin + yc][pxmin + xc];
                /* Loop over points in tip */
                for (py = pymin; py <= pymax; py++) {
                    for (px = pxmin; px <= pxmax; px++) {
                        gint temp = image[i + py][j + px] - tip[py + yc][px + xc];
                        if (min > temp)
                            min = temp;
                    }
                }
                result[i][j] = min;
            }

            if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito,
                                                  pcancelled))
                break;
        }
    }

    if (cancelled) {
        _gwy_morph_lib_ifreematrix(result);
        return NULL;
    }

    return result;
}

/**
 * _gwy_morph_lib_icmap:
 * @image: Image array.
 * @xres: Number of columns.
 * @yres: Number of rows.
 * @tip: Tip array.
 * @txres: Number of columns.
 * @tyres: Number of rows.
 * @rsurf: Eroded surface array.
 * @xc: Tip apex column coordinate.
 * @yc: Tip apex row coordinate.
 * @set_fraction: Function to output computation fraction (or %NULL).
 * @set_message: Function to output computation state message (or %NULL).
 *
 * Performs the certainty map algorithm.
 *
 * Returns: Certainty map (newly allocated).  May return %NULL if aborted.
 **/
gint**
_gwy_morph_lib_icmap(const gint *const *image, gint xres, gint yres,
                     const gint *const *tip, gint txres, gint tyres,
                     const gint *const *rsurf,
                     gint xc, gint yc,
                     GwySetMessageFunc set_message,
                     GwySetFractionFunc set_fraction)
{
    gint **cmap;
    gint k, rxc, ryc;              /* center coordinates of reflected tip */
    gboolean cancelled = FALSE, *pcancelled = &cancelled;

    if ((set_message && !set_message(_("Certainty map...")))
         || (set_fraction && !set_fraction(0.0)))
        return NULL;

    rxc = txres - 1 - xc;
    ryc = tyres - 1 - yc;

    /* create output array of appropriate size */
    cmap = _gwy_morph_lib_iallocmatrix(yres, xres);
    for (k = 0; k < yres; k++)
        gwy_clear(cmap[k], xres);

    /*
       Loop over all pixels in the interior of the image. We skip
       pixels near the edge. Since it is possible there are unseen
       touches over the edge, we must conservatively leave these cmap
       entries at 0.
     */
#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            shared(image,tip,rsurf,cmap,xres,yres,txres,tyres,rxc,ryc,set_fraction,pcancelled)
#endif
    {
        gint ifrom = gwy_omp_chunk_start(yres+1 - tyres) + ryc;
        gint ito = gwy_omp_chunk_end(yres+1 - tyres) + ryc;
        gint i, j;

        for (i = ifrom; i < ito; i++) {
            for (j = rxc; j <= xres + rxc - txres; j++) {
                gint tjmin = MAX(0, rxc - j);
                gint tjmax = MIN(txres - 1, xres - 1 + rxc - j);
                gint timin = MAX(0, ryc - i);
                gint timax = MIN(tyres - 1, yres - 1 + ryc - i);
                /* GCC, could you please explain to me how do you think count
                 * can become nonzero without setting x and y? */
                gint x = 0, y = 0; /* silence GCC */
                gint ti, tj, count = 0;

                for (ti = timin; ti <= timax && count < 2; ti++) {
                    for (tj = tjmin; tj <= tjmax && count < 2; tj++) {
                        if (image[i][j] - tip[tyres-1-ti][txres-1-tj]
                            == rsurf[ti+i-ryc][tj+j-rxc]) {
                            count++;        /* increment count */
                            x = tj + j - rxc;    /* remember coordinates */
                            y = ti + i - ryc;
                        }
                    }
                }
                /* One contact = good recon. */
                /* This is OK with parallelisation because if we write from
                 * multiple threads to the same location, we write the same
                 * value. */
                if (count == 1)
                    cmap[y][x] = 1;
            }

            if (gwy_omp_set_fraction_check_cancel(set_fraction, i, ifrom, ito,
                                                  pcancelled))
                break;
        }
    }

    if (cancelled) {
        _gwy_morph_lib_ifreematrix(cmap);
        return NULL;
    }

    return cmap;
}


static gint**
iopen(const gint *const *image, gint xres, gint yres,
      const gint *const *tip, gint txres, gint tyres,
      GwySetMessageFunc set_message,
      GwySetFractionFunc set_fraction)
{
    gint **result, **eros;

    eros = _gwy_morph_lib_ierosion(image, xres, yres, tip,
                                   txres, tyres,
                                   txres/2, tyres/2,
                                   set_message, set_fraction);
    if (!eros)
        return NULL;

    result = gwy_morph_lib_idilation((const gint* const*)eros, xres, yres,
                                     tip, txres, tyres,
                                     txres/2, tyres/2,
                                     set_message, set_fraction);
    _gwy_morph_lib_ifreematrix(eros);  /* free intermediate result */

    return result;
}

/**
 * _gwy_morph_lib_itip_estimate:
 * @image: Surface data.
 * @im_xsiz: Number of columns.
 * @im_ysiz: Number of rows.
 * @tip_xsiz: Tip number of columns.
 * @tip_ysiz: Tip numbe rof rows.
 * @xc: Tip apex column coordinate.
 * @yc: Tip apex row coordinate.
 * @tip0: Tip data to be refined.
 * @thresh: Threshold.
 * @use_edges: Whether to use also image edges.
 * @set_fraction: Function to output computation fraction (or %NULL).
 * @set_message: Functon to output computation state message (or %NULL).
 *
 * Performs tip estimation algorithm.
 *
 * Returns: The number of locations that produced refinement, -1 if aborted.
 **/
gint
_gwy_morph_lib_itip_estimate(const gint *const *image,
                             gint xres, gint yres,
                             gint txres, gint tyres, gint xc,
                             gint yc, gint **tip0,
                             gint thresh, gboolean use_edges,
                             GwySetMessageFunc set_message,
                             GwySetFractionFunc set_fraction)
{
    gint iter = 0;
    gint count = 1;
    gint sumcount = 0;
    gchar *s;

    while (count) {
        iter++;
        count = itip_estimate_iter(image, xres, yres,
                                   txres, tyres, xc, yc, tip0,
                                   thresh, use_edges, iter,
                                   set_message, set_fraction);
        if (count == -1)
            return count;
        s = g_strdup_printf(ngettext("One image location produced refinement",
                                     "%d image locations produced refinement",
                                     count),
                            count);
        if (set_message && !set_message(s)) {
            g_free(s);
            return -1;
        }
        g_free(s);
        sumcount += count;
    }

    return sumcount;
}

static inline void
gwy_omp_if_threads_min_imatrix(G_GNUC_UNUSED gint **a,
                               G_GNUC_UNUSED gint **ta,
                               G_GNUC_UNUSED gint yres,
                               G_GNUC_UNUSED gint xres)
{
#ifdef _OPENMP
    gint i, j;

    if (ta == a)
       return;

#pragma omp critical
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            if (ta[i][j] < a[i][j])
                a[i][j] = ta[i][j];
        }
    }
    _gwy_morph_lib_ifreematrix(ta);
#endif
}

static gint
itip_estimate_iter(const gint *const *image, gint xres, gint yres,
                   gint txres, gint tyres, gint xc, gint yc, gint **tip0,
                   gint thresh, gboolean use_edges,
                   gint iter,
                   GwySetMessageFunc set_message,
                   GwySetFractionFunc set_fraction)
{
    gboolean cancelled = FALSE, *pcancelled = &cancelled;
    gchar *s;
    gint **open;
    gint count = 0;          /* counts places where tip estimate is improved */
    gint next_row = 0;

    open = iopen(image, xres, yres,
                 (const gint* const*)tip0, txres, tyres,
                 set_message, set_fraction);
    if (!open)
        return -1;

    s = g_strdup_printf(_("Iterating estimate (iteration %d)..."), iter);
    if (set_message && !set_message(s)) {
        g_free(s);
        return -1;
    }
    g_free(s);

#ifdef _OPENMP
#pragma omp parallel if (gwy_threads_are_enabled()) default(none) \
            reduction(+:count) \
            shared(image,open,tip0,xres,yres,txres,tyres,xc,yc,thresh,use_edges,next_row,set_fraction,pcancelled)
#endif
    {
        gint **ttip0 = idupmatrix_omp_if_threads(tip0, tyres, txres);
        gint j, nj = yres+1 - tyres;
        gint jxp, ixp;           /* index into the image (x') */

        /* Acquire unique row number to process.  These are essentially tasks,
         * but with all the allocation and cancellation around it seems simpler
         * to just express it explicitly.  We need to split the work with
         * row granularity because different blocks take different time
         * depending on if anything useful is found. */
        while ((j = gwy_omp_atomic_increment_int(&next_row)) < nj) {
            jxp = j + tyres - 1 - yc;
            for (ixp = txres - 1 - xc; ixp <= xres - 1 - xc; ixp++) {
                if (image[jxp][ixp] - open[jxp][ixp] > thresh) {
                    if (itip_estimate_point(ixp, jxp, image,
                                            xres, yres, txres, tyres,
                                            xc, yc, tip0, thresh, use_edges)) {
                        count++;
                    }
                }
            }
            if (gwy_omp_set_fraction_check_cancel(set_fraction, j, 0, nj,
                                                  pcancelled))
                break;
        }
        gwy_omp_if_threads_min_imatrix(tip0, ttip0, tyres, txres);
    }
    _gwy_morph_lib_ifreematrix(open);

    if (cancelled)
        return -1;
    if (!count)
        return 0;

    /* XXX: The count is inflated if paralellising.  Return an estimate based
     * on assumption of random independent improvements, making sure we return
     * a positive number when we started with one. */
    count /= GWY_ROUND(sqrt(gwy_omp_max_threads()));
    count = MAX(count, 1);
    return count;
}

/**
 * _gwy_morph_lib_itip_estimate0:
 * @image: Surface data.
 * @xres: Number of columns.
 * @yres: Number of rows.
 * @txres: Tip number of columns.
 * @tyres: Tip numbe rof rows.
 * @xc: Tip apex column coordinate.
 * @yc: Tip apex row coordinate.
 * @tip0: Tip data to be refined.
 * @thresh: Threshold.
 * @use_edges: Whether to use also image edges.
 * @set_fraction: Function to output computation fraction (or %NULL).
 * @set_message: Functon to output computation state message (or %NULL).
 *
 * Performs partial tip estimation algorithm.
 *
 * Returns: The number of locations that produced refinement, -1 if aborted.
 **/
gint
_gwy_morph_lib_itip_estimate0(const gint *const *image, gint xres, gint yres,
                              gint txres, gint tyres,
                              gint xc, gint yc,
                              gint **tip0, gint thresh,
                              gboolean use_edges,
                              GwySetMessageFunc set_message,
                              GwySetFractionFunc set_fraction)
{
    IntList *x, *y;  /* point to coordinates of image maxima */
    gint i, j, count, sumcount, iter = 0;
    gint delta;      /* defines what is meant by near neighborhood for purposes
                        of point selection. */
    gint maxcount = 20;
    GString *str;

    x = int_list_new(200);
    y = int_list_new(200);
    str = g_string_new(NULL);

    delta = MAX(MAX(txres, tyres)/10, 1);

    if (set_message && !set_message(_("Searching for local maxima..."))) {
        sumcount = -1;
        goto finalise;
    }

    /* Create a list of coordinates to use */
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i,j) \
            shared(image,txres,tyres,xres,yres,xc,yc,delta,x,y)
#endif
    for (j = tyres - 1 - yc; j <= yres - 1 - yc; j++) {
        for (i = txres - 1 - xc; i <= xres - 1 - xc; i++) {
            if (useit(i, j, image, xres, yres, delta)) {
#ifdef _OPENMP
#pragma omp critical
#endif
                {
                    int_list_add(x, i);
                    int_list_add(y, j);
                }
            }
        }
    }
    g_string_printf(str, ngettext("Found one internal local maximum",
                                  "Found %d internal local maxima",
                                  (gint)x->len),
                    (gint)x->len);
    if ((set_message && !set_message(str->str))
        || (set_fraction && !set_fraction(0.0))) {
        sumcount = -1;
        goto finalise;
    }

    /* Now refine tip at these coordinates recursively until no more change */
    sumcount = 0;
    do {
        count = 0;
        iter++;
        g_string_printf(str, _("Iterating estimate (iteration %d)..."), iter);
        if (set_message && !set_message(str->str)) {
            sumcount = -1;
            goto finalise;
        }

        for (i = 0; i < x->len; i++) {
            if (itip_estimate_point(x->data[i], y->data[i],
                                    image, xres, yres,
                                    txres, tyres, xc, yc, tip0, thresh,
                                    use_edges)) {
                count++;
                sumcount++;
            }
            if (set_fraction && !set_fraction((i + 1.0)/x->len)) {
                sumcount = -1;
                goto finalise;
            }
        }
        g_string_printf(str,
                        ngettext("One image location produced refinement",
                                 "%d image locations produced refinement",
                                 count),
                        count);
        if (set_message && !set_message(str->str)) {
            sumcount = -1;
            goto finalise;
        }
    } while (count && count > maxcount);

    /* free temporary space */
finalise:
    g_string_free(str, TRUE);
    int_list_free(x);
    int_list_free(y);
    return sumcount;
}

/*
   The following is a routine that determines whether a selected point
   at coordinates x,y within an image is deemed to be suitable for
   image refinement. In this implementation, the algorithm simply
   decides to use the point if it is a local maximum of the image.
   It defines a local maximum as a point with height greater than any
   of its near neighbors, provided there are not too many near neighbors
   with values equal to the maximum (which indicates a flat).
*/
static gboolean
useit(gint x, gint y, const gint *const *image, gint sx, gint sy, gint delta)
{
    gint xmin, xmax, ymin, ymax;        /* actual interval to search */
    gint i, j;
    gint max = image[y][x];    /* value of maximum height in the neighborhood */
    gint count = 0;             /* counts # spots where pixel = max value */

    xmin = MAX(x - delta, 0);
    xmax = MIN(x + delta, sx - 1);
    ymin = MAX(y - delta, 0);
    ymax = MIN(y + delta, sy - 1);

    for (j = ymin; j <= ymax; j++) {
        for (i = xmin; i <= xmax; i++) {
            max = image[j][i] >= max ? (count++, image[j][i]) : max;
        }
    }

    /* If the point equals the maximum value in the neighborhood we use it,
       unless there are too many points in the neighborhood with the same
       property--i.e. the neighborhood is flat */
    /*if (max == image[y][x] && count <= (((2*delta + 1) ^ 2)/5))*/
    if (max == image[y][x] && count <= (2*delta + 1)*(2*delta + 1)/5)
        return TRUE;
    return FALSE;
}

/*
   The following routine does the same thing as itip_estimate_iter, except
   that instead of looping through all i,j contained within the image, it
   computes the tip shape as deduced from a single i,j coordinate. For what
   is this useful? The order of evaluation of the points can affect the
   execution speed. That is because the image at some locations puts great
   constraints on the tip shape. If the tip shape is refined by considering
   these points first, time is saved later. The following routine, since it
   performs the calculation at a single point, allows the user to select
   the order in which image coordinates are considered. In using the
   routine in this mode, the fact that the refined tip replaces tip0 means
   results of one step automatically become the starting point for the next
   step. The routine returns an integer which is the number of pixels
   within the starting tip estimate which were updated.

   To compile codes which does not use parts of the image within a tipsize
   of the edge, set USE_EDGES to 0 on the next line.
*/
/* XXX: This function only ever monotonically decreases the values in tip0[].
 * So it can be run in parallel on independent tip0[]s and then combine the
 * results by taking the minimum.  The only problem is counting.  We will
 * definitely provide more refinements when we split the work, because the
 * refinements become independent.  This is OK in the full run, where we only
 * care if any refinement was made -- although we would still report the wrong
 * number to the user -- but cannot be done in the partial estimate, where
 * the count is actually used. */
static gint
itip_estimate_point(gint ixp, gint jxp, const gint *const *image,
                    gint xres, gint yres, gint txres, gint tyres,
                    gint xc, gint yc, gint **tip0, gint thresh,
                    gboolean use_edges)
{
    gint ix, jx;              /* index into the output tip array (x) */
    gint id, jd;              /* index into p' (d) */
    gint temp, imagep, dil;   /* various intermediate results */
    gint count = 0;           /* counts places where tip estimate is improved */
    gint interior;            /* =1 if no edge effects to worry about */
    gint apexstate, xstate, inside = 1;  /* Point is inside image */
    gint outside = 0;         /* point is outside image */

    interior = (jxp >= tyres - 1
                && jxp <= yres - tyres
                && ixp >= txres - 1
                && ixp <= xres - txres);

    if (interior) {
        for (jx = 0; jx < tyres; jx++) {
            for (ix = 0; ix < txres; ix++) {
                /* First handle the large middle area where we don't have to
                   be concerned with edge problems. Because edges are far
                   away, we can leave out the overhead of checking for them
                   in this section. */
                imagep = image[jxp][ixp];
                dil = -G_MAXINT;        /* initialize maximum to -infinity */
                for (jd = 0; jd < tyres; jd++) {
                    for (id = 0; id < txres; id++) {
                        if (imagep - image[jxp + yc - jd][ixp + xc - id]
                            > tip0[jd][id])
                            continue;
                        temp = image[jx + jxp - jd][ix + ixp - id]
                               + tip0[jd][id] - imagep;
                        dil = MAX(dil, temp);
                    }           /* end for id */
                }               /* end for jd */
                if (dil == -G_MAXINT)
                    continue;

                /* Essentially: tip = MIN(tip, dil+thresh), just with
                 * counting. */
                if (dil < tip0[jx][ix] - thresh) {
                    count++;
                    tip0[jx][ix] = dil+thresh;
                }
            }                   /* end for ix */
        }                       /* end for jx */
        return count;
    }                           /* endif */

    if (use_edges) {
        /* Now handle the edges */
        for (jx = 0; jx < tyres; jx++) {
            for (ix = 0; ix < txres; ix++) {
                imagep = image[jxp][ixp];
                dil = -G_MAXINT;    /* initialize maximum to -infinity */
                for (jd = 0; jd <= tyres - 1 && dil < G_MAXINT; jd++) {
                    for (id = 0; id <= txres - 1; id++) {
                        /* Determine whether the tip apex at (xc,yc) lies
                         * within the domain of the translated image, and
                         * if so, if it is inside (i.e. below or on the
                         * surface of) the image. */
                        apexstate = outside;        /* initialize */
                        if (jxp + yc - jd < 0
                            || jxp + yc - jd >= yres
                            || ixp + xc - id < 0
                            || ixp + xc - id >= xres)
                            apexstate = inside;
                        else if (imagep - image[jxp + yc - jd][ixp + xc - id]
                                 <= tip0[jd][id])
                            apexstate = inside;
                        /* Determine whether the point (ix,jx) under
                         * consideration lies within the domain of the
                         * translated image */
                        if (jxp + jx - jd < 0
                            || jxp + jx - jd >= yres
                            || ixp + ix - id < 0
                            || ixp + ix - id >= xres)
                            xstate = outside;
                        else
                            xstate = inside;

                        /* There are 3 actions we might take, depending upon
                           which of 4 states (2 apexstate possibilities times 2
                           xstate ones) we are in. */

                        /* If apex is outside and x is either in or out no
                         * change is made for this (id,jd) */
                        if (apexstate == outside)
                            continue;

                        /* If apex is inside and x is outside
                           worst case is translated image value -> G_MAXDOUBLE.
                           This would result in no change for ANY (id,jd).
                           We therefore abort the loop and go to next (ix,jx)
                           value */
                        if (xstate == outside)
                            goto nextx;

                        /* The only remaining possibility is x and apex both
                         * inside. This is the same case we treated in the
                         * interior. */
                        temp = image[jx + jxp - jd][ix + ixp - id]
                               + tip0[jd][id] - imagep;
                        dil = MAX(dil, temp);
                    }               /* end for id */
                }                   /* end for jd */
                if (dil == -G_MAXINT)
                    continue;

                /* Essentially: tip = MIN(tip, dil+thresh), just with
                 * counting. */
                if (dil < tip0[jx][ix] - thresh) {
                    count++;
                    tip0[jx][ix] = dil+thresh;
                }
nextx:;
            }                       /* end for ix */
        }                           /* end for jx */
    }

    return count;
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */

