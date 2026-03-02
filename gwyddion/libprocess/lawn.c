/*
 *  $Id: lawn.c 25662 2023-09-14 12:33:36Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti), Petr Klapetek, Radek Slesinger.
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
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/lawn.h>
#include <libprocess/interpolation.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

#define GWY_LAWN_TYPE_NAME "GwyLawn"

typedef struct {
    gint ncurves;
    gint *curvelengths; /* (xres * yres) */
    gdouble **curvedata; /* (xres * yres) * (ncurves * curvelengths[i]) */
    GwySIUnit **si_units_curves; /* ncurves */
    gchar **curvelabels;

    /* get_n_segments, get_segments, set_segments, set_data */
    /* new lawn - 1 segment */
    /* set curve data */
    gint nsegments;
    gint *segments;
    gchar **segmentlabels;
} GwyLawnPrivate;

enum {
    DATA_CHANGED,
    LAST_SIGNAL
};

static guint lawn_signals[LAST_SIGNAL] = { 0 };

static void        gwy_lawn_finalize         (GObject *object);
static void        gwy_lawn_serializable_init(GwySerializableIface *iface);
static GByteArray* gwy_lawn_serialize        (GObject *obj,
                                              GByteArray *buffer);
static gsize       gwy_lawn_get_size         (GObject *obj);
static GObject*    gwy_lawn_deserialize      (const guchar *buffer,
                                              gsize size,
                                              gsize *position);
static GObject*    gwy_lawn_duplicate_real   (GObject *object);
static void        gwy_lawn_clone_real       (GObject *source,
                                              GObject *copy);
static void        clone_string_array        (gchar ***ptarget,
                                              gint ntarget,
                                              gchar **source,
                                              gint nsource);

G_DEFINE_TYPE_EXTENDED(GwyLawn, gwy_lawn, G_TYPE_OBJECT, 0,
                       GWY_IMPLEMENT_SERIALIZABLE(gwy_lawn_serializable_init))

static void
gwy_lawn_serializable_init(GwySerializableIface *iface)
{
    iface->serialize = gwy_lawn_serialize;
    iface->deserialize = gwy_lawn_deserialize;
    iface->get_size = gwy_lawn_get_size;
    iface->duplicate = gwy_lawn_duplicate_real;
    iface->clone = gwy_lawn_clone_real;
}

static void
gwy_lawn_class_init(GwyLawnClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_lawn_finalize;

    g_type_class_add_private(klass, sizeof(GwyLawnPrivate));

    /**
     * GwyLawn::data-changed:
     * @gwylawn: The #GwyLawn which received the signal.
     *
     * The ::data-changed signal is never emitted by lawn itself.  It is intended as a means to notify others data
     * line users they should update themselves.
     */
    lawn_signals[DATA_CHANGED]
        = g_signal_new("data-changed",
                       G_OBJECT_CLASS_TYPE(gobject_class),
                       G_SIGNAL_RUN_FIRST,
                       G_STRUCT_OFFSET(GwyLawnClass, data_changed),
                       NULL, NULL,
                       g_cclosure_marshal_VOID__VOID,
                       G_TYPE_NONE, 0);
}

static void
gwy_lawn_init(GwyLawn *lawn)
{
    lawn->priv = G_TYPE_INSTANCE_GET_PRIVATE(lawn, GWY_TYPE_LAWN, GwyLawnPrivate);
}

static void
gwy_lawn_finalize(GObject *object)
{
    GwyLawnPrivate *priv;

    gint i;

    GwyLawn *lawn = (GwyLawn*)object;
    priv = (GwyLawnPrivate*)lawn->priv;

    GWY_OBJECT_UNREF(lawn->si_unit_xy);

    for (i = 0; i < lawn->xres * lawn->yres; i++)
        g_free(priv->curvedata[i]); // cond. j/f dep. uninit; inv free/del/reall

    g_free(priv->curvedata);
    for (i = 0; i < priv->ncurves; i++)
        GWY_OBJECT_UNREF(priv->si_units_curves[i]);
    g_free(priv->si_units_curves);
    g_free(priv->curvelengths);
    g_free(priv->segments);

    clone_string_array(&priv->curvelabels, priv->ncurves, NULL, 0);
    clone_string_array(&priv->segmentlabels, priv->nsegments, NULL, 0);

    G_OBJECT_CLASS(gwy_lawn_parent_class)->finalize(object);
}

/**
 * gwy_lawn_new:
 * @xres: X resolution, i.e., the number of samples in x direction
 * @yres: Y resolution, i.e., the number of samples in y direction
 * @xreal: Real physical dimension in x direction.
 * @yreal: Real physical dimension in y direction.
 * @ncurves: The number of curves at each sample.
 * @nsegments: The number of curve segments.
 *
 * Creates a new data lawn.
 *
 * Returns: A newly created data lawn.
 *
 * Since: 2.60
 **/
GwyLawn*
gwy_lawn_new(gint xres, gint yres, gdouble xreal, gdouble yreal, gint ncurves, gint nsegments)
{
    GwyLawn *lawn;
    GwyLawnPrivate *priv;
    gint npoints;

    gwy_debug("");
    lawn = g_object_new(GWY_TYPE_LAWN, NULL);
    priv = (GwyLawnPrivate*)lawn->priv;

    lawn->xres = xres;
    lawn->yres = yres;
    lawn->xreal = xreal;
    lawn->yreal = yreal;
    lawn->xoff = 0.0;
    lawn->yoff = 0.0;

    npoints = xres*yres;

    priv->ncurves = ncurves;
    priv->curvelengths = g_new0(gint, npoints);
    priv->curvedata = g_new0(gdouble*, npoints);
    priv->si_units_curves = g_new0(GwySIUnit*, ncurves);

    priv->nsegments = nsegments;
    priv->segments = g_new0(gint, npoints * 2 *nsegments);

    return lawn;
}

/**
 * gwy_lawn_new_alike:
 * @model: A data lawn to take resolutions, units, and labels from.
 *
 * Creates a new data lawn similar to an existing one.
 *
 * Use gwy_lawn_duplicate() if you want to copy a data lawn including
 * data.
 *
 * Returns: A newly created data lawn.
 *
 * Since: 2.60
 **/
GwyLawn*
gwy_lawn_new_alike(GwyLawn *model)
{
    GwyLawn *lawn;
    GwyLawnPrivate *lpriv, *mpriv;

    g_return_val_if_fail(GWY_IS_LAWN(model), NULL);
    mpriv = (GwyLawnPrivate*)model->priv;

    lawn = gwy_lawn_new(model->xres, model->yres, model->xreal, model->yreal, mpriv->ncurves, 0);
    lpriv = (GwyLawnPrivate*)lawn->priv;

    lawn->xoff = model->xoff;
    lawn->yoff = model->yoff;
    gwy_lawn_copy_units(model, lawn);
    clone_string_array(&lpriv->curvelabels, lpriv->ncurves, mpriv->curvelabels, mpriv->ncurves);

    return lawn;
}

/**
 * gwy_lawn_new_part:
 * @lawn: A data lawn to take data from
 * @xpos: x position where to start from
 * @ypos: y position where to start from
 * @xres: x resolution (width) to be extracted
 * @yres: y resolution (height) to be extracted
 * @keep_offsets: keep offsets of data during extraction
 *
 * Creates a new data lawn as a part of existing one.
 *
 * Use gwy_lawn_duplicate() if you want to copy a whole data lawn.
 *
 * Returns: A newly created data lawn.
 *
 * Since: 2.60
 **/
GwyLawn*
gwy_lawn_new_part(GwyLawn *lawn,
                  gint xpos, gint ypos,
                  gint xres, gint yres,
                  gboolean keep_offsets)
{
    GwyLawn *part;
    GwyLawnPrivate *lpriv, *ppriv;
    gint row, col, lxres, lyres, idx_part, idx_lawn, idx_seg_part, idx_seg_lawn, nsegments;
    gdouble dx, dy;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    g_return_val_if_fail(xpos >= 0 && ypos >= 0 && xres >= 0 && yres >= 0, NULL);
    lxres = lawn->xres;
    lyres = lawn->yres;
    g_return_val_if_fail(xpos + xres <= lxres && ypos + yres <= lyres, NULL);

    dx = gwy_lawn_get_dx(lawn);
    dy = gwy_lawn_get_dy(lawn);

    nsegments = gwy_lawn_get_n_segments(lawn);

    lpriv = (GwyLawnPrivate*)lawn->priv;

    part = gwy_lawn_new(xres, yres, xres*dx, yres*dy, lpriv->ncurves, nsegments);
    ppriv = (GwyLawnPrivate*)part->priv;

    if (keep_offsets) {
        part->xoff = xpos*dx + lawn->xoff;
        part->yoff = ypos*dy + lawn->yoff;
    }

    gwy_lawn_copy_units(lawn, part);
    clone_string_array(&ppriv->curvelabels, ppriv->ncurves, lpriv->curvelabels, lpriv->ncurves);
    clone_string_array(&ppriv->segmentlabels, ppriv->nsegments, lpriv->segmentlabels, lpriv->nsegments);

    ppriv->segments = g_new(gint, xres*yres * 2 * nsegments);

    for (row = 0; row < yres; row++) {
        gwy_assign(ppriv->curvelengths + xres*row, lpriv->curvelengths + lxres*(ypos + row) + xpos, xres);

        for (col = 0; col < xres; col++) {
            idx_part = xres*row + col;
            idx_lawn = lxres*(ypos + row) + (xpos + col);

            ppriv->curvedata[idx_part] = g_new(gdouble, ppriv->curvelengths[idx_part] * ppriv->ncurves);
            gwy_assign(ppriv->curvedata[idx_part],
                       lpriv->curvedata[idx_lawn],
                       ppriv->curvelengths[idx_part] * ppriv->ncurves);

            idx_seg_part = idx_part * 2 * nsegments;
            idx_seg_lawn = idx_lawn * 2 * nsegments;
            gwy_assign(ppriv->segments + idx_seg_part, lpriv->segments + idx_seg_lawn, 2 * nsegments);
        }
    }

    return part;
}

static GByteArray*
gwy_lawn_serialize(GObject *obj,
                   GByteArray *buffer)
{
    GwyLawn *lawn;
    GwyLawnPrivate *priv;
    guint32 datasize;
    gpointer pxoff, pyoff, pxyunit, *pcurveunits;
    gint i, j;
    guint32 npoints, bsize, segdatasize;
    gdouble *alldata, *wrk;
    gboolean any_units;
    GByteArray *retval;

    lawn = GWY_LAWN(obj);

    pxoff = lawn->xoff ? &lawn->xoff : NULL;
    pyoff = lawn->yoff ? &lawn->yoff : NULL;
    pxyunit = unit_pointer_if_nonempty(lawn->si_unit_xy);

    priv = (GwyLawnPrivate*)lawn->priv;
    pcurveunits = g_new(gpointer, priv->ncurves);
    any_units = FALSE;
    for (i = 0; i < priv->ncurves; i++) {
        pcurveunits[i] = unit_pointer_if_nonempty(priv->si_units_curves[i]);
        if (pcurveunits[i])
            any_units = TRUE;
    }
    if (!any_units)
        GWY_FREE(pcurveunits);

    npoints = lawn->xres * lawn->yres;
    datasize = 0;

    for (i = 0; i < npoints; i++) {
        datasize += priv->ncurves * priv->curvelengths[i];
    }

    alldata = g_new(gdouble, datasize);
    wrk = alldata;

    for (i = 0; i < npoints; i++) {
        bsize = priv->curvelengths[i] * priv->ncurves;
        gwy_assign(wrk, priv->curvedata[i], bsize);
        wrk += bsize;
    }

    segdatasize = lawn->xres * lawn->yres * 2 * priv->nsegments;

    {
        GwySerializeSpec spec[] = {
            { 'i', "xres", &lawn->xres, NULL, },
            { 'i', "yres", &lawn->yres, NULL, },
            { 'd', "xreal", &lawn->xreal, NULL, },
            { 'd', "yreal", &lawn->yreal, NULL, },
            { 'd', "xoff", pxoff, NULL, },
            { 'd', "yoff", pyoff, NULL, },
            { 'o', "si_unit_xy", pxyunit, NULL, },

            { 'i', "ncurves", &(priv->ncurves), NULL, },
            { 'I', "curvelengths", &(priv->curvelengths), &npoints, },
            { 'D', "data", &alldata, &datasize, },
            { 'O', "si_units_curves", pcurveunits, &(priv->ncurves), },
            { 'S', "curve_labels", &(priv->curvelabels), &(priv->ncurves), },

            { 'i', "nsegments", &(priv->nsegments), NULL, },
            { 'I', "segments", &(priv->segments), &segdatasize, },
            { 'S', "segment_labels", &(priv->segmentlabels), &(priv->nsegments), },
        };

        /* Filter out non-existent arrays. */
        for (i = j = 0; i < G_N_ELEMENTS(spec); i++) {
            if (g_ascii_islower(spec[i].ctype) || (*spec[i].array_size && spec[i].value && *(gpointer*)spec[i].value))
                spec[j++] = spec[i];
        }

        retval = gwy_serialize_pack_object_struct(buffer, GWY_LAWN_TYPE_NAME, j, spec);
        g_free(pcurveunits);
        g_free(alldata);

        return retval;
    }
}

static gsize
gwy_lawn_get_size(GObject *obj)
{
    GwyLawn *lawn;
    GwyLawnPrivate *priv;
    guint32 datasize;
    gpointer pxoff, pyoff, pxyunit, *pcurveunits;
    gint i, j;
    gdouble *alldata;
    guint32 npoints, segdatasize;
    gboolean any_units;
    gsize retval;

    lawn = GWY_LAWN(obj);
    priv = (GwyLawnPrivate*)lawn->priv;

    pxoff = lawn->xoff ? &lawn->xoff : NULL;
    pyoff = lawn->yoff ? &lawn->yoff : NULL;
    pxyunit = unit_pointer_if_nonempty(lawn->si_unit_xy);

    pcurveunits = g_new(gpointer, priv->ncurves);
    any_units = FALSE;
    for (i = 0; i < priv->ncurves; i++) {
        pcurveunits[i] = unit_pointer_if_nonempty(priv->si_units_curves[i]);
        if (pcurveunits[i])
            any_units = TRUE;
    }
    if (!any_units)
        GWY_FREE(pcurveunits);

    npoints = lawn->xres * lawn->yres;
    datasize = 0;

    for (i = 0; i < npoints; i++) {
        datasize += priv->ncurves * priv->curvelengths[i];
    }

    alldata = g_new(gdouble, datasize);

    segdatasize = lawn->xres * lawn->yres * 2 * priv->nsegments;

    {
        GwySerializeSpec spec[] = {
            { 'i', "xres", &lawn->xres, NULL, },
            { 'i', "yres", &lawn->yres, NULL, },
            { 'd', "xreal", &lawn->xreal, NULL, },
            { 'd', "yreal", &lawn->yreal, NULL, },
            { 'd', "xoff", pxoff, NULL, },
            { 'd', "yoff", pyoff, NULL, },
            { 'o', "si_unit_xy", pxyunit, NULL, },

            { 'i', "ncurves", &(priv->ncurves), NULL, },
            { 'I', "curvelengths", &(priv->curvelengths), &npoints, },
            { 'D', "data", &alldata, &datasize, },
            { 'O', "si_units_curves", pcurveunits, &(priv->ncurves), },
            { 'S', "curve_labels", &(priv->curvelabels), &(priv->ncurves), },

            { 'i', "nsegments", &(priv->nsegments), NULL, },
            { 'I', "segments", &(priv->segments), &segdatasize, },
            { 'S', "segment_labels", &(priv->segmentlabels), &(priv->nsegments), },
        };

        /* Filter out non-existent arrays. */
        for (i = j = 0; i < G_N_ELEMENTS(spec); i++) {
            if (g_ascii_islower(spec[i].ctype) || (*spec[i].array_size && spec[i].value && *(gpointer*)spec[i].value))
                spec[j++] = spec[i];
        }

        retval = gwy_serialize_get_struct_size(GWY_LAWN_TYPE_NAME, j, spec);

        g_free(pcurveunits);
        g_free(alldata);

        return retval;
    }
}

static void
clean_deserialized_data(guint32 nsiunits, GwySIUnit **si_units_curves,
                        guint32 ncurvelabels, gchar **curvelabels,
                        guint32 *curvelengths,
                        gdouble *alldata,
                        guint32 *segments)
{
    gint i;

    if (si_units_curves) {
        for (i = 0; i < nsiunits; i++)
            GWY_OBJECT_UNREF(si_units_curves[i]);

        g_free(si_units_curves);
    }

    if (curvelabels) {
        for (i = 0; i < ncurvelabels; i++)
            g_free(curvelabels[i]);

        g_free(curvelabels);
    }

    g_free(curvelengths);
    g_free(alldata);
    g_free(segments);
}

static GObject*
gwy_lawn_deserialize(const guchar *buffer,
                     gsize size,
                     gsize *position)
{
    guint32 datasize = 0;
    gint xres, yres, i;
    gdouble xreal, yreal, xoff = 0.0, yoff = 0.0;
    GwySIUnit *si_unit_xy = NULL;
    GwyLawn *lawn;
    GwyLawnPrivate *priv;

    guint32 ncurves = 0, nsiunits = 0, ncurvelabels = 0, npoints = 0;
    guint32 nsegments = 0, nsegmentdata = 0, nsegmentlabels = 0;
    GwySIUnit **si_units_curves = NULL;
    gchar **curvelabels = NULL, **segmentlabels = NULL;
    guint32 *curvelengths = NULL, *segments = NULL;
    gdouble *alldata = NULL, *wrk;
    guint32 bsize, ds_expect;

    GwySerializeSpec spec[] = {
        { 'i', "xres", &xres, NULL, },
        { 'i', "yres", &yres, NULL, },
        { 'd', "xreal", &xreal, NULL, },
        { 'd', "yreal", &yreal, NULL, },
        { 'd', "xoff", &xoff, NULL, },
        { 'd', "yoff", &yoff, NULL, },
        { 'o', "si_unit_xy", &si_unit_xy, NULL, },

        { 'i', "ncurves", &ncurves, NULL, },
        { 'O', "si_units_curves", &si_units_curves, &nsiunits, },
        { 'S', "curve_labels", &curvelabels, &ncurvelabels, },
        { 'I', "curvelengths", &curvelengths, &npoints, },
        { 'D', "data", &alldata, &datasize, },

        { 'i', "nsegments", &nsegments, NULL, },
        { 'I', "segments", &segments, &nsegmentdata, },
        { 'S', "segment_labels", &segmentlabels, &nsegmentlabels, },
    };

    gwy_debug("");
    g_return_val_if_fail(buffer, NULL);

    if (!gwy_serialize_unpack_object_struct(buffer, size, position, GWY_LAWN_TYPE_NAME, G_N_ELEMENTS(spec), spec))
        goto fail;

    if (npoints != xres*yres) {
        g_critical("Serialized %s number of curves mismatch %u != %u", GWY_LAWN_TYPE_NAME, npoints, xres*yres);
        goto fail;
    }
    if (si_units_curves && nsiunits != ncurves) {
        g_critical("Serialized %s number of units mismatch %u != %u", GWY_LAWN_TYPE_NAME, nsiunits, ncurves);
        goto fail;
    }
    if (curvelabels && ncurvelabels != ncurves) {
        g_critical("Serialized %s number of curve labels mismatch %u != %u", GWY_LAWN_TYPE_NAME, ncurvelabels, ncurves);
        goto fail;
    }
    if (segments && nsegmentdata != xres*yres* 2*nsegments) {
        g_critical("Serialized %s segment data size mismatch %u != %u",
                   GWY_LAWN_TYPE_NAME, nsegmentdata, xres*yres* 2*nsegments);
        goto fail;
    }
    if (segmentlabels && nsegmentlabels != nsegments) {
        g_critical("Serialized %s number of segment labels mismatch %u != %u",
                   GWY_LAWN_TYPE_NAME, nsegmentlabels, nsegments);
        goto fail;
    }

    ds_expect = 0;
    for (i = 0; i < npoints; i++)
        ds_expect += ncurves * curvelengths[i];

    if (datasize != ds_expect) {
        g_critical("Serialized %s size mismatch %u != %u", GWY_LAWN_TYPE_NAME, datasize, ds_expect);
        goto fail;
    }

    lawn = gwy_lawn_new(xres, yres, xreal, yreal, ncurves, nsegments);
    gwy_lawn_set_xoffset(lawn, xoff);
    gwy_lawn_set_yoffset(lawn, yoff);

    if (si_unit_xy) {
        GWY_OBJECT_UNREF(lawn->si_unit_xy);
        lawn->si_unit_xy = si_unit_xy;
    }

    priv = (GwyLawnPrivate*)lawn->priv;

    g_free(priv->curvelengths);
    priv->curvelengths = curvelengths;
    if (si_units_curves) {
        for (i = 0; i < ncurves; i++) {
            _gwy_set_object_si_unit(si_units_curves[i], priv->si_units_curves + i);
            GWY_OBJECT_UNREF(si_units_curves[i]);
        }
    }
    /* These are NULL by default so we just assign to them from deserialised data. */
    priv->curvelabels = curvelabels;
    priv->segments = segments;
    priv->segmentlabels = segmentlabels;

    wrk = alldata;

    for (i = 0; i < npoints; i++) {
        bsize = curvelengths[i] * ncurves;

        if (curvelengths[i] > 0) {
            priv->curvedata[i] = g_new(gdouble, bsize);
            gwy_assign(priv->curvedata[i], wrk, bsize);
            wrk += bsize;
        }
        else {
            priv->curvedata[i] = NULL;
        }
    }

    return (GObject*)lawn;

fail:
    GWY_OBJECT_UNREF(si_unit_xy);
    clean_deserialized_data(nsiunits, si_units_curves, ncurvelabels, curvelabels, curvelengths, alldata, segments);
    return NULL;
}

static GObject*
gwy_lawn_duplicate_real(GObject *object)
{
    GwyLawn *lawn, *duplicate;
    GwyLawnPrivate *dpriv, *lpriv;
    gint i, npoints;

    g_return_val_if_fail(GWY_IS_LAWN(object), NULL);
    lawn = GWY_LAWN(object);
    duplicate = gwy_lawn_new_alike(lawn);

    dpriv = (GwyLawnPrivate*)duplicate->priv;
    lpriv = (GwyLawnPrivate*)lawn->priv;

    npoints = lawn->xres * lawn->yres;

    dpriv->curvelengths = g_new(gint, npoints);
    dpriv->curvedata = g_new0(gdouble*, npoints);

    for (i = 0; i < npoints; i++) {
        dpriv->curvelengths[i] = lpriv->curvelengths[i];

        if (dpriv->curvelengths[i] > 0) {
            dpriv->curvedata[i] = g_new(gdouble, dpriv->curvelengths[i] * dpriv->ncurves);
            gwy_assign(dpriv->curvedata[i], lpriv->curvedata[i], dpriv->curvelengths[i] * dpriv->ncurves);
        }
    }

    clone_string_array(&dpriv->segmentlabels, dpriv->nsegments, lpriv->segmentlabels, lpriv->nsegments);
    dpriv->nsegments = lpriv->nsegments;
    dpriv->segments = g_new(gint, npoints * 2*lpriv->nsegments);
    gwy_assign(dpriv->segments, lpriv->segments, npoints * 2*lpriv->nsegments);

    return (GObject*)duplicate;
}

static void
gwy_lawn_clone_real(GObject *source, GObject *copy)
{
    GwyLawn *lawn, *clone;
    GwyLawnPrivate *lpriv, *cpriv;
    gint i, npoints;

    g_return_if_fail(GWY_IS_LAWN(source));
    g_return_if_fail(GWY_IS_LAWN(copy));

    lawn = GWY_LAWN(source);
    clone = GWY_LAWN(copy);

    npoints = lawn->xres * lawn->yres;

    lpriv = (GwyLawnPrivate*)lawn->priv;
    cpriv = (GwyLawnPrivate*)clone->priv;

    if (clone->xres != lawn->xres || clone->yres != lawn->yres) {
        clone->xres = lawn->xres;
        clone->yres = lawn->yres;

        g_free(cpriv->curvelengths);
        cpriv->curvelengths = g_new(gint, npoints);
        g_free(cpriv->curvedata);
        cpriv->curvedata = g_new(gdouble*, npoints);
    }

    clone->xreal = lawn->xreal;
    clone->yreal = lawn->yreal;
    clone->xoff = lawn->xoff;
    clone->yoff = lawn->yoff;

    clone_string_array(&cpriv->curvelabels, cpriv->ncurves, lpriv->curvelabels, lpriv->ncurves);
    clone_string_array(&cpriv->segmentlabels, cpriv->nsegments, lpriv->segmentlabels, lpriv->nsegments);

    if (cpriv->ncurves != lpriv->ncurves) {
        for (i = 0; i < cpriv->ncurves; i++)
            GWY_OBJECT_UNREF(cpriv->si_units_curves[i]);
        cpriv->si_units_curves = g_renew(GwySIUnit*, cpriv->si_units_curves, lpriv->ncurves);
        cpriv->ncurves = lpriv->ncurves;
    }
    gwy_lawn_copy_units(lawn, clone);

    for (i = 0; i < npoints; i++) {
        if (cpriv->curvelengths[i] != lpriv->curvelengths[i]) {
            g_free(cpriv->curvedata[i]);
            cpriv->curvedata[i] = g_new(gdouble, lpriv->curvelengths[i] * cpriv->ncurves);
            cpriv->curvelengths[i] = lpriv->curvelengths[i];
        }
        gwy_assign(cpriv->curvedata[i], lpriv->curvedata[i], cpriv->curvelengths[i] * cpriv->ncurves);
    }

    if (cpriv->nsegments != lpriv->nsegments) {
        g_free(cpriv->segments);
        cpriv->segments = g_new(gint, npoints * 2*lpriv->nsegments);
        cpriv->nsegments = lpriv->nsegments;
    }
    gwy_assign(cpriv->segments, lpriv->segments, npoints * 2*lpriv->nsegments);
}

/**
 * gwy_lawn_data_changed:
 * @lawn: A data lawn.
 *
 * Emits signal "data_changed" on a data lawn.
 *
 * Since: 2.60
 **/
void
gwy_lawn_data_changed(GwyLawn *lawn)
{
    g_signal_emit(lawn, lawn_signals[DATA_CHANGED], 0);
}

/**
 * gwy_lawn_copy:
 * @src: Source lawn.
 * @dest: Destination lawn.
 * @nondata_too: Whether non-data (units, labels, segment information) should be copied too.
 *
 * Copies the contents of an already allocated lawn to a lawn of the same size.
 **/
void
gwy_lawn_copy(GwyLawn *src,
              GwyLawn *dest,
              gboolean nondata_too)
{
    GwyLawnPrivate *spriv, *dpriv;
    gint i, npoints;

    g_return_if_fail(GWY_IS_LAWN(src));
    g_return_if_fail(GWY_IS_LAWN(dest));
    g_return_if_fail(src->xres == dest->xres && src->yres == dest->yres);

    if (src == dest)
        return;

    dest->xreal = src->xreal;
    dest->yreal = src->yreal;

    spriv = (GwyLawnPrivate*)src->priv;
    dpriv = (GwyLawnPrivate*)dest->priv;
    g_return_if_fail(dpriv->ncurves != spriv->ncurves);

    npoints = src->xres * src->yres;

    if (dpriv->curvelengths)
        g_free(dpriv->curvelengths);

    dpriv->curvelengths = g_new(gint, npoints);
    gwy_assign(dpriv->curvelengths, spriv->curvelengths, npoints);

    if (dpriv->curvedata) {
        for (i = 0; i < npoints; i++)
            g_free(dpriv->curvedata[i]);
        g_free(dpriv->curvedata);
    }

    dpriv->curvedata = g_new0(double*, npoints);

    for (i = 0; i < npoints; i++) {
        if (dpriv->curvelengths[i] > 0) {
            dpriv->curvedata[i] = g_new(double, dpriv->curvelengths[i]);
            gwy_assign(dpriv->curvedata[i], spriv->curvedata[i], spriv->curvelengths[i] * spriv->ncurves);
        }
    }

    if (!nondata_too)
        return;

    gwy_lawn_copy_units(src, dest);
    clone_string_array(&dpriv->curvelabels, dpriv->ncurves, spriv->curvelabels, spriv->ncurves);
    clone_string_array(&dpriv->segmentlabels, dpriv->nsegments, spriv->segmentlabels, spriv->nsegments);

    dpriv->nsegments = spriv->nsegments;

    if (dpriv->segments)
        g_free(dpriv->segments);

    dpriv->segments = g_new(gint, npoints * 2 * dpriv->nsegments);
    gwy_assign(dpriv->segments, spriv->segments, npoints * spriv->nsegments * 2);
}

/**
 * gwy_lawn_get_xres:
 * @lawn: A data lawn.
 *
 * Gets the x resolution of a data lawn.
 *
 * Returns: Resolution (number of data points).
 *
 * Since: 2.60
 **/
gint
gwy_lawn_get_xres(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0);
    return lawn->xres;
}

/**
 * gwy_lawn_get_yres:
 * @lawn: A data lawn.
 *
 * Gets the y resolution of a data lawn.
 *
 * Returns: Resolution (number of data points).
 *
 * Since: 2.60
 **/
gint
gwy_lawn_get_yres(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0);
    return lawn->yres;
}

/**
 * gwy_lawn_get_xreal:
 * @lawn: A data lawn.
 *
 * Gets the physical size of a data lawn in the x direction.
 *
 * Returns: Real size of a data lawn the x direction.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_xreal(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->xreal;
}

/**
 * gwy_lawn_get_yreal:
 * @lawn: A data lawn.
 *
 * Gets the physical size of a data lawn in the y direction.
 *
 * Returns: Real size of a data lawn the y direction.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_yreal(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->yreal;
}

/**
 * gwy_lawn_get_xoffset:
 * @lawn: A data lawn.
 *
 * Gets the offset of data lawn origin in x direction.
 *
 * Returns: Offset value.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_xoffset(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->xoff;
}

/**
 * gwy_lawn_get_yoffset:
 * @lawn: A data lawn.
 *
 * Gets the offset of data lawn origin in y direction.
 *
 * Returns: Offset value.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_yoffset(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->yoff;
}

/**
 * gwy_lawn_get_n_curves:
 * @lawn: A data lawn.
 *
 * Gets the number of curves at each sample.
 *
 * Returns: Number of curves.
 *
 * Since: 2.60
 **/
gint
gwy_lawn_get_n_curves(GwyLawn *lawn)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    priv = (GwyLawnPrivate*)lawn->priv;

    return priv->ncurves;
}

/**
 * gwy_lawn_set_xoffset:
 * @lawn: A data lawn.
 * @xoffset: New offset value.
 *
 * Sets the offset of a data lawn origin in the x direction.
 *
 * Note offsets don't affect any calculation.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_xoffset(GwyLawn *lawn, gdouble xoffset)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    lawn->xoff = xoffset;
}

/**
 * gwy_lawn_set_yoffset:
 * @lawn: A data lawn.
 * @yoffset: New offset value.
 *
 * Sets the offset of a data lawn origin in the y direction.
 *
 * Note offsets don't affect any calculation.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_yoffset(GwyLawn *lawn, gdouble yoffset)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    lawn->yoff = yoffset;
}

/**
 * gwy_lawn_set_xreal:
 * @lawn: A data lawn.
 * @xreal: New real x dimensions value
 *
 * Sets the real x dimension of a lawn.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_xreal(GwyLawn *lawn, gdouble xreal)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(xreal > 0.0);
    lawn->xreal = xreal;
}

/**
 * gwy_lawn_set_yreal:
 * @lawn: A data lawn.
 * @yreal: New real y dimensions value
 *
 * Sets the real y dimension of a lawn.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_yreal(GwyLawn *lawn, gdouble yreal)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(yreal > 0.0);
    lawn->yreal = yreal;
}

/**
 * gwy_lawn_get_dx:
 * @lawn: A data lawn.
 *
 * Gets the horizontal (X) pixel size of a lawn in real units.
 *
 * The result is the same as gwy_lawn_get_xreal(lawn)/gwy_lawn_get_xres(lawn).
 *
 * Returns: Horizontal pixel size.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_dx(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->xreal/lawn->xres;
}

/**
 * gwy_lawn_get_dy:
 * @lawn: A data lawn.
 *
 * Gets the vertical (Y) pixel size of a lawn in real units.
 *
 * The result is the same as gwy_lawn_get_yreal(lawn)/gwy_lawn_get_yres(lawn).
 *
 * Returns: Vertical pixel size.
 *
 * Since: 2.60
 **/
gdouble
gwy_lawn_get_dy(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0.0);
    return lawn->yreal/lawn->yres;
}

/**
 * gwy_lawn_get_si_unit_xy:
 * @lawn: A data lawn.
 *
 * Returns x- and y-direction SI unit of a data lawn.
 *
 * Returns: SI unit corresponding to the lateral (X) dimension of the data lawn.  Its reference count is not
 *          incremented.
 *
 * Since: 2.60
 **/
GwySIUnit*
gwy_lawn_get_si_unit_xy(GwyLawn *lawn)
{
    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    if (!lawn->si_unit_xy)
        lawn->si_unit_xy = gwy_si_unit_new(NULL);

    return lawn->si_unit_xy;
}

/**
 * gwy_lawn_get_si_unit_curve:
 * @lawn: A data lawn.
 * @n: Index of a curve in @lawn.
 *
 * Returns value SI unit of the n-th curve of a data lawn.
 *
 * Returns: SI unit corresponding to the "value" of the data lawn.  Its reference count is not incremented.
 *
 * Since: 2.60
 **/
GwySIUnit*
gwy_lawn_get_si_unit_curve(GwyLawn *lawn, gint n)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);
    g_return_val_if_fail(n >= 0, NULL);

    priv = (GwyLawnPrivate*)lawn->priv;
    g_return_val_if_fail(n < priv->ncurves, NULL);
    g_return_val_if_fail(priv->si_units_curves, NULL);

    if (!priv->si_units_curves[n])
        priv->si_units_curves[n] = gwy_si_unit_new(NULL);

    return priv->si_units_curves[n];
}

/**
 * gwy_lawn_set_si_unit_xy:
 * @lawn: A data lawn.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding to the lateral (X, Y) dimensions of a data lawn.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_si_unit_xy(GwyLawn *lawn,
                        GwySIUnit *si_unit)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    _gwy_set_object_si_unit(si_unit, &lawn->si_unit_xy);
}

/**
 * gwy_lawn_set_si_unit_curve:
 * @lawn: A data lawn.
 * @n: Index of a curve in @lawn.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding of the n-th curve of a data lawn.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_si_unit_curve(GwyLawn *lawn, gint n, GwySIUnit *si_unit)
{
    GwyLawnPrivate *priv;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(n >= 0);

    priv = (GwyLawnPrivate*)lawn->priv;
    g_return_if_fail(n < priv->ncurves);
    g_return_if_fail(priv->si_units_curves);

    _gwy_set_object_si_unit(si_unit, &(priv->si_units_curves[n]));
}

/**
 * gwy_lawn_copy_units:
 * @lawn: A data lawn.
 * @target: Target data lawn.
 *
 * Sets lateral and curve units of a data lawn to match another data lawn.
 *
 * Since: 2.60
 **/
void
gwy_lawn_copy_units(GwyLawn *lawn, GwyLawn *target)
{
    GwyLawnPrivate *lpriv, *tpriv;
    gint i;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(GWY_IS_LAWN(target));

    lpriv = (GwyLawnPrivate*)lawn->priv;
    tpriv = (GwyLawnPrivate*)target->priv;

    g_return_if_fail(lpriv->ncurves == tpriv->ncurves);
    g_return_if_fail(lpriv->si_units_curves);
    g_return_if_fail(tpriv->si_units_curves);

    _gwy_copy_si_unit(lawn->si_unit_xy, &target->si_unit_xy);
    for (i = 0; i < lpriv->ncurves; i++)
        _gwy_copy_si_unit(lpriv->si_units_curves[i], &(tpriv->si_units_curves[i]));
}

/**
 * gwy_lawn_set_curve_label:
 * @lawn: A data lawn.
 * @n: Index of a curve in @lawn.
 * @label: New curve label.
 *
 * Sets the label of a curve in data lawn.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_curve_label(GwyLawn *lawn, gint n, const gchar *label)
{
    GwyLawnPrivate *priv;
    gint i;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(n >= 0);
    g_return_if_fail(label);

    priv = (GwyLawnPrivate*)lawn->priv;
    g_return_if_fail(n < priv->ncurves);

    if (!priv->curvelabels) {
        priv->curvelabels = g_new(gchar*, priv->ncurves);
        for (i = 0; i < priv->ncurves; i++)
            priv->curvelabels[i] = g_strdup(_("Unknown"));
    }
    gwy_assign_string(priv->curvelabels + n, label);
}

/**
 * gwy_lawn_get_curve_label:
 * @lawn: A data lawn.
 * @n: Index of a curve in @lawn.
 *
 * Gets the label of a curve in data lawn.
 *
 * Returns: Curve label, as a string owned by @lawn.  It may be %NULL.
 *
 * Since: 2.60
 **/
const gchar*
gwy_lawn_get_curve_label(GwyLawn *lawn, gint n)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);
    g_return_val_if_fail(n >= 0, NULL);

    priv = (GwyLawnPrivate*)lawn->priv;
    g_return_val_if_fail(n < priv->ncurves, NULL);
    if (!priv->curvelabels)
        return NULL;

    return priv->curvelabels[n];
}

/**
 * gwy_lawn_get_value_format_xy:
 * @lawn: A data lawn.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying coordinates of a data lawn.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.60
 **/
GwySIValueFormat*
gwy_lawn_get_value_format_xy(GwyLawn *lawn,
                             GwySIUnitFormatStyle style,
                             GwySIValueFormat *format)
{
    gdouble max, unit;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    max = MAX(lawn->xreal, lawn->yreal);
    unit = MIN(lawn->xreal/lawn->xres, lawn->yreal/lawn->yres);
    return gwy_si_unit_get_format_with_resolution(gwy_lawn_get_si_unit_xy(lawn), style, max, unit, format);
}

/**
 * gwy_lawn_get_value_format_w:
 * @lawn: A data lawn.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying values of a data lawn.
 *
 * Note this functions searches for minimum and maximum value in @lawn, therefore it's relatively slow.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.60
 **/
GwySIValueFormat*
gwy_lawn_get_value_format_curve(GwyLawn *lawn,
                                gint n,
                                GwySIUnitFormatStyle style,
                                GwySIValueFormat *format)
{
    GwyLawnPrivate *priv;
    gdouble max, min;
    gint i, j, idx;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);
    priv = (GwyLawnPrivate*)lawn->priv;

    g_return_val_if_fail((n >= 0) && (n < priv->ncurves), NULL);

    max = -G_MAXDOUBLE;
    min = G_MAXDOUBLE;

    for (i = 0; i < lawn->xres * lawn->yres; i++) {
        if (priv->curvedata[i]) {
            for (j = 0; j < priv->curvelengths[j]; j++) {
                idx = n * lawn->xres + j;

                if (priv->curvedata[i][idx] > max)
                    max = priv->curvedata[i][idx];

                if (priv->curvedata[i][idx] < min)
                    min = priv->curvedata[i][idx];
            }
        }
    }

    if (max == min) {
        max = ABS(max);
        min = 0.0;
    }

    return gwy_si_unit_get_format(gwy_lawn_get_si_unit_curve(lawn, n), style, max - min, format);
}

/**
 * gwy_lawn_get_curve_length:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 *
 * Gets the length of the curves at given position in a data lawn.
 *
 * Returns: The curves length at given index.
 *
 * Since: 2.60
 **/
gint
gwy_lawn_get_curve_length(GwyLawn *lawn, gint col, gint row)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0);

    priv = (GwyLawnPrivate*)lawn->priv;
    g_return_val_if_fail(priv->curvelengths, 0);

    return priv->curvelengths[row * lawn->xres + col];
}

/**
 * gwy_lawn_get_curve_data:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @n: Index of a curve in @lawn.
 * @curvelength: Location to store the length of the curve, or %NULL.
 *
 * Gets the data array of the n-th curve at given position in a data lawn.
 *
 * The returned buffer is not guaranteed to be valid through whole data
 * lawn life time.
 *
 * Returns: The n-th curve data at given index.
 *
 * Since: 2.60
 **/
gdouble*
gwy_lawn_get_curve_data(GwyLawn *lawn, gint col, gint row, gint n, gint *curvelength)
{
    /* TODO: unify with gwy_lawn_get_curve_data_const */
    GwyLawnPrivate *priv;
    gint idx, ndata;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    g_return_val_if_fail(col >= 0 && col < lawn->xres && row >= 0 && row < lawn->yres, NULL);
    /* TODO: also check n */

    priv = lawn->priv;
    g_return_val_if_fail(n < priv->ncurves, NULL);

    idx = row * lawn->xres + col;
    ndata = priv->curvelengths[idx];

    if (curvelength)
        *curvelength = ndata;

    if (ndata == 0)
        return NULL;
    else
        return priv->curvedata[idx] + n*ndata;
}

/**
 * gwy_lawn_get_curve_data_const:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @n: Index of a curve in @lawn.
 * @curvelength: To store the length of the curve (optional).
 *
 * Gets the data array of the n-th curve at given position in a data lawn.
 *
 * Returns: The @n-th curve data at given index.  The returned data are owned by @lawn and must not be modified nor
 *          freed.
 *
 * Since: 2.60
 **/
const gdouble*
gwy_lawn_get_curve_data_const(GwyLawn *lawn, gint col, gint row, gint n, gint *curvelength)
{
    GwyLawnPrivate *priv;
    gint idx, ndata;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    g_return_val_if_fail(col >= 0 && col < lawn->xres && row >= 0 && row < lawn->yres, NULL);
    /* TODO: also check n */

    priv = lawn->priv;
    g_return_val_if_fail(n < priv->ncurves, NULL);

    idx = row * lawn->xres + col;
    ndata = priv->curvelengths[idx];

    if (curvelength)
        *curvelength = ndata;

    if (ndata == 0)
        return NULL;
    else
        return (const gdouble*)(priv->curvedata[idx] + n*ndata);
}

/**
 * gwy_lawn_set_curve_data:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @n: Index of a curve in @lawn.
 * @curvedata: New data of the @n-th curve.
 *
 * Sets the data of a single curve in a data lawn.
 *
 * The number of points remains the same since all curves at give pixel must have the same number of points.  If you
 * want to change the number of points set the data of all curves together using gwy_lawn_set_curves().
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_curve_data(GwyLawn *lawn, gint col, gint row, gint n, const gdouble *curvedata)
{
    GwyLawnPrivate *priv;
    gint idx;

    g_return_if_fail(GWY_IS_LAWN(lawn));

    g_return_if_fail(col >= 0 && col < lawn->xres && row >= 0 && row < lawn->yres);
    /* TODO: also check n */

    priv = lawn->priv;
    g_return_if_fail(n < priv->ncurves);

    idx = row * lawn->xres + col;

    gwy_assign(priv->curvedata[idx] + n * priv->curvelengths[idx], curvedata, priv->curvelengths[idx]);
}

/**
 * gwy_lawn_get_curves_data_const:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @curvelength: To store the length of the curves (optional).
 *
 * Gets the data array of all curves at given position in a data lawn.
 *
 * Returns: The curves data at given index.
 *
 * Since: 2.60
 **/
const gdouble*
gwy_lawn_get_curves_data_const(GwyLawn *lawn, gint col, gint row, gint *curvelength)
{
    GwyLawnPrivate *priv;
    gint idx, ndata;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);
    g_return_val_if_fail(col >= 0 && col < lawn->xres && row >= 0 && row < lawn->yres, NULL);

    priv = lawn->priv;
    idx = row * lawn->xres + col;
    ndata = priv->curvelengths[idx];

    if (curvelength)
        *curvelength = ndata;

    if (ndata == 0)
        return NULL;
    else
        return (const gdouble*)priv->curvedata[idx];
}

/**
 * gwy_lawn_set_curves:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @curvelength: Length of each of the curves.
 * @curvesdata: Curve data array.
 * @segments: Segmentation for the curve.  May be %NULL to leave the current segmentation unchanged.
 *
 * Sets data for all curves at given position in a data lawn.
 *
 * Note that passing %NULL @segments can result in segmentation which is not valid for @curvelength.  In such case you
 * need to correct the segmentation afterwards.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_curves(GwyLawn *lawn,
                    gint col, gint row,
                    gint curvelength, const gdouble* curvesdata,
                    const gint* segments)
{
    GwyLawnPrivate *priv;
    gint idx_lawn, idx_seg, datasize;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(col >= 0 && col < lawn->xres && row >= 0 && row < lawn->yres);

    priv = lawn->priv;

    idx_lawn = row * lawn->xres + col;
    datasize = priv->ncurves * curvelength;

    g_free(priv->curvedata[idx_lawn]);
    priv->curvedata[idx_lawn] = g_new(gdouble, datasize);
    gwy_assign(priv->curvedata[idx_lawn], curvesdata, datasize);
    priv->curvelengths[idx_lawn] = curvelength;

    idx_seg = idx_lawn * 2 * priv->nsegments;

    if (segments && priv->segments)
        gwy_assign(priv->segments + idx_seg, segments, 2 * priv->nsegments);
}

/**
 * gwy_lawn_get_n_segments:
 * @lawn: A data lawn.
 *
 * Gets the number of segments marked in curves in a data lawn.
 *
 * All curves have the same number of segments, even empty curves.  Empty curves simply have the corresponding number
 * of trivial zero-length segments.
 *
 * Returns: The number of segments.  Zero is returned if no segments are marked.
 *
 * Since: 2.60
 **/
gint
gwy_lawn_get_n_segments(GwyLawn *lawn)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), 0);

    priv = lawn->priv;

    return priv->nsegments;
}

/**
 * gwy_lawn_get_segments:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @nsegments: Location where to store the number of segments, or %NULL.
 *
 * Gets the segmentation of a curve in a data lawn.
 *
 * Returns: The segmentation for the curve, as an array owned by @lawn which must not be modified nor freed.
 *
 * See gwy_lawn_set_segments() for the array format.
 *
 * Since: 2.60
 **/
const gint*
gwy_lawn_get_segments(GwyLawn *lawn, gint col, gint row, gint *nsegments)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    priv = lawn->priv;

    if (nsegments)
        *nsegments = priv->nsegments;

    return priv->segments + 2*priv->nsegments*(row*lawn->xres + col);
}

/**
 * gwy_lawn_set_segments:
 * @lawn: A data lawn.
 * @nsegments: New number of segments.
 * @segments: The new segmentation for entire @lawn.
 *
 * Sets the segmentation of all curves in a data lawn.
 *
 * This is the only function which allows changing the number of segments.  If the number of segments changes all the
 * labels are reset.
 *
 * See gwy_lawn_curve_set_segments() for the single-curve @segments array format.  When setting the data for an entire
 * @lawn the array must contain this data for all pixels, concatenated.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_segments(GwyLawn *lawn, gint nsegments, const gint *segments)
{
    GwyLawnPrivate *priv;
    gint npoints;

    g_return_if_fail(GWY_IS_LAWN(lawn));

    priv = lawn->priv;
    npoints = lawn->xres * lawn->yres;
    clone_string_array(&priv->segmentlabels, priv->nsegments, NULL, 0);
    g_free(priv->segments);
    priv->segments = g_memdup(segments, npoints * 2*nsegments * sizeof(gint));
    priv->nsegments = nsegments;
}

/**
 * gwy_lawn_get_segment_label:
 * @lawn: A data lawn.
 * @segment: Index of a curve segment in @lawn.
 *
 * Gets the label of a curve segment in data lawn.
 *
 * Returns: Segment label, as a string owned by @lawn.  It may be %NULL.
 *
 * Since: 2.60
 **/
const gchar*
gwy_lawn_get_segment_label(GwyLawn *lawn, gint segment)
{
    GwyLawnPrivate *priv;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);
    g_return_val_if_fail(segment >= 0, NULL);

    priv = lawn->priv;
    g_return_val_if_fail(segment < priv->nsegments, NULL);

    if (!priv->segmentlabels)
        return NULL;
    return priv->segmentlabels[segment];
}

/**
 * gwy_lawn_set_segment_label:
 * @lawn: A data lawn.
 * @segment: Index of a curve segment in @lawn.
 * @label: New segment label.
 *
 * Sets the label of a curve segment in data lawn.
 *
 * Since all curves in @lawn are segmented into the same segments the segments share labels.  The first segment label
 * corresponds to the first segment in all the curves.
 *
 * Since: 2.60
 **/
void
gwy_lawn_set_segment_label(GwyLawn *lawn, gint segment, const gchar *label)
{
    GwyLawnPrivate *priv;
    gint i;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(segment >= 0);
    g_return_if_fail(label);

    priv = lawn->priv;

    g_return_if_fail(segment < priv->nsegments);
    if (!priv->segmentlabels) {
        priv->segmentlabels = g_new(gchar*, priv->nsegments);
        for (i = 0; i < priv->nsegments; i++)
            priv->segmentlabels[i] = g_strdup_printf("%s %d", _("Segment"), i+1);
    }
    gwy_assign_string(priv->segmentlabels + segment, label);
}

/**
 * gwy_lawn_curve_set_segments:
 * @lawn: A data lawn.
 * @col: Position in the lawn (column index).
 * @row: Position in the lawn (row index).
 * @segments: The new segmentation for the curve.
 *
 * Sets the segmentation of a curve in data lawn.
 *
 * All curves have the same number of segments, equal to the value returned by gwy_lawn_get_n_segments().
 *
 * The array @segments contains twice as much elements as there are segments.  They are organised [@start₀, @end₀,
 * @start₁, @end₁, etc.].  A segment starts at the postion @start and has length @end-@start.  In other words, @start
 * is inclusive, but @end is exclusive.  If a segment is zero-length even @start can correspond to non-existent data
 * (the typical case is an empty curve).
 *
 * Since: 2.60
 **/
void
gwy_lawn_curve_set_segments(GwyLawn *lawn, gint col, gint row, const gint *segments)
{
    GwyLawnPrivate *priv;
    gint idx_seg;

    g_return_if_fail(GWY_IS_LAWN(lawn));

    priv = lawn->priv;

    idx_seg = (row * lawn->xres + col) * 2 * priv->nsegments;
    gwy_assign(priv->segments + idx_seg, segments, 2 * priv->nsegments);
}

/**
 * gwy_lawn_clear:
 * @lawn: A data lawn.
 *
 * Removes curve data at all samples (curve number, units, and labels preserved).
 *
 * Since: 2.60
 **/
void
gwy_lawn_clear(GwyLawn *lawn)
{
    GwyLawnPrivate *priv;
    gint i;

    g_return_if_fail(GWY_IS_LAWN(lawn));

    priv = lawn->priv;
    for (i = 0; i < lawn->xres * lawn->yres; i++) {
        GWY_FREE(priv->curvedata[i]);
    }
}

/**
 * gwy_lawn_area_reduce_to_plane:
 * @lawn: A data lawn.
 * @target: Datafield to be filled by summed plane. Its dimensions should be (width, height).
 * @func: Function to reduce the curves data array to a single value.
 * @user_data: Data passed to @func.
 * @col: Column where to start (pixel coordinates).
 * @row: Row where to start (pixel coordinates).
 * @width: Pixel width of summed plane.
 * @height: Pixel height of summed plane.
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Reduces curves data at each point of a rectangular region to a value,
 * storing the results in a data field.
 *
 * Since: 2.60
 **/
void
gwy_lawn_area_reduce_to_plane(GwyLawn *lawn, GwyDataField *target,
                              GwyCurveReduceFunction func, gpointer userdata,
                              gint istart, gint jstart,
                              gint width, gint height,
                              gboolean keep_offsets)
{
    GwyLawnPrivate *priv;
    gint col, row, idx_lawn, idx_target;
    gdouble *tdata;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    g_return_if_fail(istart + width <= lawn->xres && jstart + height <= lawn->yres);
    priv = lawn->priv;
    g_return_if_fail(priv);

    g_return_if_fail(GWY_IS_DATA_FIELD(target));
    g_return_if_fail((width == target->xres) && (height == target->yres));

    gwy_data_field_set_xreal(target, gwy_lawn_get_xreal(lawn));
    gwy_data_field_set_yreal(target, gwy_lawn_get_yreal(lawn));

    if (keep_offsets) {
        gwy_data_field_set_xoffset(target, gwy_lawn_get_xoffset(lawn));
        gwy_data_field_set_yoffset(target, gwy_lawn_get_yoffset(lawn));
    }

    tdata = gwy_data_field_get_data(target);
    g_return_if_fail(tdata);

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            idx_target = row * target->xres + col;
            idx_lawn = (row + istart) * lawn->xres + (col + jstart);
            tdata[idx_target] = (*func)(priv->ncurves, priv->curvelengths[idx_lawn], priv->curvedata[idx_lawn],
                                        userdata);
        }
    }

    _gwy_copy_si_unit(lawn->si_unit_xy, &target->si_unit_xy);
    /* should we care about si_unit_z? then probably func should tell */

    gwy_data_field_invalidate(target);
}

/**
 * gwy_lawn_reduce_to_plane:
 * @lawn: A data lawn.
 * @target: Datafield to be filled by the summary data. It should have the same dimensions as lawn.
 * @func: Function to reduce the curves data array to a single value.
 * @user_data: Data passed to @func.
 *
 * Sums all z-profiles of a data lawn to a data field.
 *
 * Reduces curves data at each point of the lawn to a value,
 * storing the results in a data field.
 *
 * Since: 2.60
 **/
void
gwy_lawn_reduce_to_plane(GwyLawn *lawn, GwyDataField *target,
                         GwyCurveReduceFunction func, gpointer userdata)
{
    g_return_if_fail(GWY_IS_LAWN(lawn));
    /* check dimensions */

    gwy_lawn_area_reduce_to_plane(lawn, target,
                                  func, userdata,
                                  0, 0,
                                  lawn->xres, lawn->yres,
                                  TRUE);
}

/**
 * gwy_lawn_new_rotated_90:
 * @lawn: A data lawn.
 * @clockwise: %TRUE to rotate clocwise, %FALSE to rotate anti-clockwise.
 *
 * Creates a new data lawn by rotating a data lawn by 90 degrees.
 *
 * Returns: A newly created data lawn.
 *
 * Since: 2.60
 **/
GwyLawn*
gwy_lawn_new_rotated_90(GwyLawn *lawn, gboolean clockwise)
{
    GwyLawn *rot;
    GwyLawnPrivate *lpriv, *rpriv;
    gint row, col, idx_lawn, idx_rot, idx_lseg, idx_rseg, bsize;

    g_return_val_if_fail(GWY_IS_LAWN(lawn), NULL);

    lpriv = (GwyLawnPrivate*)lawn->priv;

    rot = gwy_lawn_new(lawn->yres, lawn->xres, lawn->yreal, lawn->xreal, lpriv->ncurves, lpriv->nsegments);
    rpriv = (GwyLawnPrivate*)rot->priv;

    rot->xoff = lawn->yoff;
    rot->yoff = lawn->xoff;

    clone_string_array(&rpriv->curvelabels, rpriv->ncurves, lpriv->curvelabels, lpriv->ncurves);
    clone_string_array(&rpriv->segmentlabels, rpriv->nsegments, lpriv->segmentlabels, lpriv->nsegments);
    gwy_lawn_copy_units(lawn, rot);

    if (clockwise) {
        for (row = 0; row < lawn->yres; row++) {
            for (col = 0; col < lawn->xres; col++) {
                idx_lawn = row * lawn->xres + col;
                idx_rot = (rot->yres - 1 - col) * rot->xres + row;

                bsize = lpriv->ncurves * lpriv->curvelengths[idx_lawn];
                rpriv->curvedata[idx_rot] = g_new(gdouble, bsize);
                gwy_assign(rpriv->curvedata[idx_rot], lpriv->curvedata[idx_lawn], bsize);

                idx_lseg = idx_lawn * 2 * lpriv->nsegments;
                idx_rseg = idx_rot * 2 * rpriv->nsegments;
                gwy_assign(rpriv->segments + idx_rseg, lpriv->segments + idx_lseg, 2 * lpriv->nsegments);
            }
        }
    }
    else {
        for (row = 0; row < lawn->yres; row++) {
            for (col = 0; col < lawn->xres; col++) {
                idx_lawn = row * lawn->xres + col;
                idx_rot = col * rot->xres + (rot->xres - 1 - row);

                bsize = lpriv->ncurves * lpriv->curvelengths[idx_lawn];
                rpriv->curvedata[idx_rot] = g_new(gdouble, bsize);
                gwy_assign(rpriv->curvedata[idx_rot], lpriv->curvedata[idx_lawn], bsize);

                idx_lseg = idx_lawn * 2 * lpriv->nsegments;
                idx_rseg = idx_rot * 2 * rpriv->nsegments;
                gwy_assign(rpriv->segments + idx_rseg, lpriv->segments + idx_lseg, 2 * lpriv->nsegments);
            }
        }
    }

    return rot;
}


/**
 * gwy_lawn_invert:
 * @lawn: A data lawn.
 * @xflipped: %TRUE to reflect X, i.e. rows.
 * @yflipped: %TRUE to reflect Y, i.e. columns.
 *
 * Flips a data lawn in place.
 *
 * Since real sizes cannot go backward, flipping an axis results in the corresponding offset being reset (the real
 * dimension stays positive).
 *
 * Note that the axis parameter convention is different from the confusing one of gwy_data_field_invert().  Here
 * parameters simply correspond to directions that should be flipped.
 *
 * Since: 2.60
 **/
void
gwy_lawn_invert(GwyLawn *lawn,
                gboolean xflipped, gboolean yflipped)
{
    GwyLawnPrivate *priv;
    gint xres, yres, i, j, k, slen;
    gdouble **curvedata;
    gint *curvelengths, *segments;
    gint idx1, idx2;

    g_return_if_fail(GWY_IS_LAWN(lawn));
    xres = lawn->xres;
    yres = lawn->yres;
    priv = lawn->priv;
    curvedata = priv->curvedata;
    curvelengths = priv->curvelengths;
    segments = priv->segments;
    slen = priv->nsegments * 2;

    if (!xflipped && !yflipped) {
        /* Do nothing. */
    }
    else if (xflipped && !yflipped) {
        for (i = 0; i < yres; i++) {
            for (j = 0; j < xres/2; j++) {
                idx1 = i*xres + j;
                idx2 = i*xres + (xres - 1 - j);
                GWY_SWAP(gint, curvelengths[idx1], curvelengths[idx2]);
                GWY_SWAP(gdouble*, curvedata[idx1], curvedata[idx2]);
                for (k = 0; k < slen; k++)
                    GWY_SWAP(gint, segments[idx1*slen + k], segments[idx2*slen + k]);
            }
        }
    }
    else if (!xflipped && yflipped) {
        for (i = 0; i < yres/2; i++) {
            for (j = 0; j < xres; j++) {
                idx1 = i*xres + j;
                idx2 = (yres - 1 - i)*xres + j;
                GWY_SWAP(gint, curvelengths[idx1], curvelengths[idx2]);
                GWY_SWAP(gdouble*, curvedata[idx1], curvedata[idx2]);
                for (k = 0; k < slen; k++)
                    GWY_SWAP(gint, segments[idx1*slen + k], segments[idx2*slen + k]);
            }
        }
    }
    else if (xflipped && yflipped) {
        for (i = 0; i < yres/2; i++) {
            for (j = 0; j < xres; j++) {
                idx1 = i*xres + j;
                idx2 = (yres - 1 - i)*xres + (xres - 1 - j);
                GWY_SWAP(gint, curvelengths[idx1], curvelengths[idx2]);
                GWY_SWAP(gdouble*, curvedata[idx1], curvedata[idx2]);
                for (k = 0; k < slen; k++)
                    GWY_SWAP(gint, segments[idx1*slen + k], segments[idx2*slen + k]);
            }
        }
    }
    else {
        g_assert_not_reached();
    }

    lawn->xoff = xflipped ? 0.0 : lawn->xoff;
    lawn->yoff = yflipped ? 0.0 : lawn->yoff;
}

/* Make sure two string arrays are identical.  Do not do too much extra work when they already are.  The item counts
 * ntarget and nsource are potential counts; either array can either be of the specified size or NULL.   With NULL
 * source this is also a convenient way of freeing the target. */
static void
clone_string_array(gchar ***ptarget, gint ntarget, gchar **source, gint nsource)
{
    gchar **target = *ptarget;
    gint i;

    if (!source)
        nsource = 0;
    if (!target)
        ntarget = 0;

    for (i = nsource; i < ntarget; i++)
        g_free(target[i]);

    if (!nsource) {
        GWY_FREE(*ptarget);
        return;
    }

    if (nsource != ntarget) {
        target = *ptarget = g_renew(gchar*, target, nsource);
        gwy_clear(target + ntarget, nsource - ntarget);
    }

    for (i = 0; i < nsource; i++)
        gwy_assign_string(target + i, source[i]);
}

/************************** Documentation ****************************/

/**
 * SECTION:lawn
 * @title: GwyLawn
 * @short_description: Three-dimensional data representation
 *
 * #GwyLawn represents 3D data arrays in Gwyddion. It is typically useful for different volume data obtained from
 * SPMs, like in force volume measurements.
 **/

/**
 * GwyLawn:
 *
 * The #GwyLawn struct contains private data only and should be accessed using the functions below.
 *
 * Since: 2.60
 **/

/**
 * gwy_lawn_duplicate:
 * @lawn: A data lawn to duplicate.
 *
 * Convenience macro doing gwy_serializable_duplicate() with all the necessary typecasting.
 *
 * Since: 2.60
 **/

/**
 * gwy_lawn_assign:
 * @dest: Target data lawn.
 * @source: Source data lawn.
 *
 * Convenience macro making one data lawn identical to another.
 *
 * This is just a gwy_serializable_clone() wrapper with all the necessary typecasting.
 *
 * Since: 2.60
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
