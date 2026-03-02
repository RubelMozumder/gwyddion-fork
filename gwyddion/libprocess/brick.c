/*
 *  $Id: brick.c 25852 2023-10-13 17:01:32Z yeti-dn $
 *  Copyright (C) 2012-2018 David Necas (Yeti), Petr Klapetek.
 *  E-mail: yeti@gwyddion.net, klapetek@gwyddion.net.
 *
 *  Originally based on Yeti's implementation for Gwyddion 3 branch,
 *  backported and modified for test use in Gwyddion 2 branch.
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
#include <libprocess/brick.h>
#include <libprocess/interpolation.h>
#include "libgwyddion/gwyomp.h"
#include "gwyprocessinternal.h"

#define GWY_BRICK_TYPE_NAME "GwyBrick"

typedef struct {
    GwyDataLine *zcalibration;
} GwyBrickPrivate;

enum {
    DATA_CHANGED,
    LAST_SIGNAL
};

static void        gwy_brick_finalize         (GObject *object);
static void        gwy_brick_serializable_init(GwySerializableIface *iface);
static GByteArray* gwy_brick_serialize        (GObject *obj,
                                               GByteArray *buffer);
static gsize       gwy_brick_get_size         (GObject *obj);
static GObject*    gwy_brick_deserialize      (const guchar *buffer,
                                               gsize size,
                                               gsize *position);
static GObject*    gwy_brick_duplicate_real   (GObject *object);
static void        gwy_brick_clone_real       (GObject *source,
                                               GObject *copy);

static guint brick_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_EXTENDED(GwyBrick, gwy_brick, G_TYPE_OBJECT, 0,
                       GWY_IMPLEMENT_SERIALIZABLE(gwy_brick_serializable_init))

static void
gwy_brick_serializable_init(GwySerializableIface *iface)
{
    iface->serialize = gwy_brick_serialize;
    iface->deserialize = gwy_brick_deserialize;
    iface->get_size = gwy_brick_get_size;
    iface->duplicate = gwy_brick_duplicate_real;
    iface->clone = gwy_brick_clone_real;
}

static void
gwy_brick_class_init(GwyBrickClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gwy_brick_finalize;

    g_type_class_add_private(klass, sizeof(GwyBrickPrivate));

    /**
     * GwyBrick::data-changed:
     * @gwydataline: The #GwyBrick which received the signal.
     *
     * The ::data-changed signal is never emitted by data line itself.  It is intended as a means to notify others
     * data line users they should update themselves.
     */
    brick_signals[DATA_CHANGED] = g_signal_new("data-changed",
                                               G_OBJECT_CLASS_TYPE(gobject_class),
                                               G_SIGNAL_RUN_FIRST,
                                               G_STRUCT_OFFSET(GwyBrickClass, data_changed),
                                               NULL, NULL,
                                               g_cclosure_marshal_VOID__VOID,
                                               G_TYPE_NONE, 0);
}

static void
gwy_brick_init(GwyBrick *brick)
{
    brick->priv = G_TYPE_INSTANCE_GET_PRIVATE(brick, GWY_TYPE_BRICK, GwyBrickPrivate);
}

static void
gwy_brick_finalize(GObject *object)
{
    GwyBrick *brick = (GwyBrick*)object;
    GwyBrickPrivate *priv = brick->priv;

    GWY_OBJECT_UNREF(priv->zcalibration);
    GWY_OBJECT_UNREF(brick->si_unit_x);
    GWY_OBJECT_UNREF(brick->si_unit_y);
    GWY_OBJECT_UNREF(brick->si_unit_z);
    GWY_OBJECT_UNREF(brick->si_unit_w);
    g_free(brick->data);

    G_OBJECT_CLASS(gwy_brick_parent_class)->finalize(object);
}

/**
 * gwy_brick_new:
 * @xres: X resolution, i.e., the number of samples in x direction
 * @yres: Y resolution, i.e., the number of samples in y direction
 * @zres: Z resolution, i.e., the number of samples in z direction
 * @xreal: Real physical dimension in x direction.
 * @yreal: Real physical dimension in y direction.
 * @zreal: Real physical dimension in z direction.
 * @nullme: Whether the data brick should be initialized to zeroes. If %FALSE, the data will not be initialized.
 *
 * Creates a new data brick.
 *
 * Returns: A newly created data brick.
 *
 * Since: 2.31
 **/
GwyBrick*
gwy_brick_new(gint xres, gint yres, gint zres, gdouble xreal, gdouble yreal, gdouble zreal, gboolean nullme)
{
    GwyBrick *brick;

    gwy_debug("");
    brick = g_object_new(GWY_TYPE_BRICK, NULL);

    brick->xres = xres;
    brick->yres = yres;
    brick->zres = zres;
    brick->xreal = xreal;
    brick->yreal = yreal;
    brick->zreal = zreal;

    if (nullme)
        brick->data = g_new0(gdouble, brick->xres * brick->yres * brick->zres);
    else
        brick->data = g_new(gdouble, brick->xres * brick->yres * brick->zres);

    return brick;
}

/**
 * gwy_brick_new_alike:
 * @model: A data brick to take resolutions and units from.
 * @nullme: Whether the data brick should be initialized to zeroes. If %FALSE, the data will not be initialized.
 *
 * Creates a new data brick similar to an existing one.
 *
 * Use gwy_brick_duplicate() if you want to copy a data brick including
 * data.
 *
 * Returns: A newly created data brick.
 *
 * Since: 2.31
 **/
GwyBrick*
gwy_brick_new_alike(GwyBrick *model,
                    gboolean nullme)
{
    GwyBrick *brick;
    GwyBrickPrivate *priv, *new_priv;

    g_return_val_if_fail(GWY_IS_BRICK(model), NULL);
    brick = g_object_new(GWY_TYPE_BRICK, NULL);

    brick->xres = model->xres;
    brick->yres = model->yres;
    brick->zres = model->zres;
    brick->xreal = model->xreal;
    brick->yreal = model->yreal;
    brick->zreal = model->zreal;
    brick->xoff = model->xoff;
    brick->yoff = model->yoff;
    brick->zoff = model->zoff;
    if (nullme)
        brick->data = g_new0(gdouble, brick->xres * brick->yres * brick->zres);
    else
        brick->data = g_new(gdouble, brick->xres * brick->yres * brick->zres);

    gwy_brick_copy_units(model, brick);

    priv = model->priv;
    new_priv = brick->priv;
    if (priv->zcalibration)
        new_priv->zcalibration = gwy_data_line_duplicate(priv->zcalibration);

    return brick;
}

/**
 * gwy_brick_new_part:
 * @brick: A data brick to take data from
 * @xpos: x position where to start from
 * @ypos: y position where to start from
 * @zpos: z position where to start from
 * @xres: x resolution (width) to be extracted
 * @yres: y resolution (height) to be extracted
 * @zres: z resolution (depth) to be extracted
 * @keep_offsets: keep offsets of data during extraction
 *
 * Creates a new data brick as a part of existing one.
 *
 * Use gwy_brick_duplicate() if you want to copy a whole data brick.
 *
 * Returns: A newly created data brick.
 *
 * Since: 2.32
 **/
GwyBrick*
gwy_brick_new_part(const GwyBrick *brick,
                   gint xpos, gint ypos, gint zpos,
                   gint xres, gint yres, gint zres,
                   gboolean keep_offsets)
{
    GwyBrick *part;
    GwyBrickPrivate *priv, *new_priv;
    gint row, lev, bxres, byres, bzres;
    gdouble dx, dy, dz;
    gdouble *bdata, *pdata;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    g_return_val_if_fail(xpos >= 0 && ypos >= 0 && zpos >= 0 && xres >= 0 && yres >= 0 && zres >= 0, NULL);
    bxres = brick->xres;
    byres = brick->yres;
    bzres = brick->zres;
    g_return_val_if_fail(xpos + xres <= bxres && ypos + yres <= byres && zpos + zres <= bzres, NULL);

    dx = gwy_brick_get_dx((GwyBrick*)brick);
    dy = gwy_brick_get_dy((GwyBrick*)brick);
    dz = gwy_brick_get_dz((GwyBrick*)brick);
    part = gwy_brick_new(xres, yres, zres, xres*dx, yres*dy, zres*dz, FALSE);

    pdata = part->data;
    bdata = brick->data;

    for (lev = 0; lev < zres; lev++) {
        for (row = 0; row < yres; row++) {
            gwy_assign(pdata + xres*row + xres*yres*lev,
                       bdata + xpos + bxres*(row + ypos) + bxres*byres*(lev + zpos),
                       xres);
        }
    }

    gwy_brick_copy_units(brick, part);

    priv = brick->priv;
    if (priv->zcalibration) {
        new_priv = part->priv;
        new_priv->zcalibration = gwy_data_line_part_extract(priv->zcalibration, zpos, zres);
    }

    if (keep_offsets) {
        part->xoff = xpos*dx + brick->xoff;
        part->yoff = ypos*dy + brick->yoff;
        part->zoff = zpos*dz + brick->zoff;
    }

    return part;

}

static GByteArray*
gwy_brick_serialize(GObject *obj,
                    GByteArray *buffer)
{
    GwyBrick *brick;
    GwyBrickPrivate *priv;
    guint32 datasize;
    guint32 num_items = 1;
    gpointer pxoff, pyoff, pzoff, pxunit, pyunit, pzunit, pwunit;
    gpointer *calibrations = NULL;

    brick = GWY_BRICK(obj);

    pxoff = brick->xoff ? &brick->xoff : NULL;
    pyoff = brick->yoff ? &brick->yoff : NULL;
    pzoff = brick->zoff ? &brick->zoff : NULL;
    pxunit = unit_pointer_if_nonempty(brick->si_unit_x);
    pyunit = unit_pointer_if_nonempty(brick->si_unit_y);
    pzunit = unit_pointer_if_nonempty(brick->si_unit_z);
    pwunit = unit_pointer_if_nonempty(brick->si_unit_w);
    datasize = brick->xres * brick->yres * brick->zres;
    priv = (GwyBrickPrivate *)brick->priv;

    g_return_val_if_fail(!priv->zcalibration || GWY_IS_DATA_LINE(priv->zcalibration), NULL);
    if (!priv->zcalibration)
        num_items = 0;
    else {
        calibrations = g_new(gpointer, 1);
        *calibrations = priv->zcalibration;
    }

    {
        GwySerializeSpec spec[] = {
            { 'i', "xres", &brick->xres, NULL, },
            { 'i', "yres", &brick->yres, NULL, },
            { 'i', "zres", &brick->zres, NULL, },
            { 'd', "xreal", &brick->xreal, NULL, },
            { 'd', "yreal", &brick->yreal, NULL, },
            { 'd', "zreal", &brick->zreal, NULL, },
            { 'd', "xoff", pxoff, NULL, },
            { 'd', "yoff", pyoff, NULL, },
            { 'd', "zoff", pzoff, NULL, },
            { 'o', "si_unit_x", pxunit, NULL, },
            { 'o', "si_unit_y", pyunit, NULL, },
            { 'o', "si_unit_z", pzunit, NULL, },
            { 'o', "si_unit_w", pwunit, NULL, },
            { 'D', "data", &brick->data, &datasize, },
            { 'O', "calibration", &calibrations, &num_items, },
        };
        gsize nspec = G_N_ELEMENTS(spec) - (num_items ? 0 : 1);
        GByteArray *retval;

        retval = gwy_serialize_pack_object_struct(buffer, GWY_BRICK_TYPE_NAME, nspec, spec);
        g_free(calibrations);

        return retval;
    }
}

static gsize
gwy_brick_get_size(GObject *obj)
{
    GwyBrick *brick;
    GwyBrickPrivate *priv;
    guint32 datasize;
    guint32 num_items = 1;
    gpointer pxoff, pyoff, pzoff, pxunit, pyunit, pzunit, pwunit;
    gpointer *calibrations = NULL;

    brick = GWY_BRICK(obj);

    pxoff = brick->xoff ? &brick->xoff : NULL;
    pyoff = brick->yoff ? &brick->yoff : NULL;
    pzoff = brick->zoff ? &brick->zoff : NULL;
    pxunit = unit_pointer_if_nonempty(brick->si_unit_x);
    pyunit = unit_pointer_if_nonempty(brick->si_unit_y);
    pzunit = unit_pointer_if_nonempty(brick->si_unit_z);
    pwunit = unit_pointer_if_nonempty(brick->si_unit_w);

    datasize = brick->xres * brick->yres * brick->zres;
    priv = (GwyBrickPrivate *)brick->priv;

    if (!priv->zcalibration)
        num_items = 0;
    else {
        calibrations = g_new(gpointer, 1);
        *calibrations = priv->zcalibration;
    }

    {
        GwySerializeSpec spec[] = {
            { 'i', "xres", &brick->xres, NULL, },
            { 'i', "yres", &brick->yres, NULL, },
            { 'i', "zres", &brick->zres, NULL, },
            { 'd', "xreal", &brick->xreal, NULL, },
            { 'd', "yreal", &brick->yreal, NULL, },
            { 'd', "zreal", &brick->zreal, NULL, },
            { 'd', "xoff", pxoff, NULL, },
            { 'd', "yoff", pyoff, NULL, },
            { 'd', "zoff", pzoff, NULL, },
            { 'o', "si_unit_x", pxunit, NULL, },
            { 'o', "si_unit_y", pyunit, NULL, },
            { 'o', "si_unit_z", pzunit, NULL, },
            { 'o', "si_unit_w", pwunit, NULL, },
            { 'D', "data", &brick->data, &datasize, },
            { 'O', "calibration", &calibrations, &num_items, },
        };
        gsize nspec = G_N_ELEMENTS(spec) - (num_items ? 0 : 1);
        gsize retval;

        retval = gwy_serialize_get_struct_size(GWY_BRICK_TYPE_NAME, nspec, spec);
        g_free(calibrations);

        return retval;
    }
}

static GObject*
gwy_brick_deserialize(const guchar *buffer,
                      gsize size,
                      gsize *position)
{
    guint32 datasize = 0;
    gint xres, yres, zres, i;
    gdouble xreal, yreal, zreal, xoff = 0.0, yoff = 0.0, zoff = 0.0;
    gdouble *data = NULL;
    GwySIUnit *si_unit_x = NULL, *si_unit_y = NULL, *si_unit_z = NULL, *si_unit_w = NULL;
    GwyBrick *brick;
    GwyBrickPrivate *priv;
    GwyDataLine **calibrations = NULL;
    guint32 num_items = 0;

    GwySerializeSpec spec[] = {
        { 'i', "xres", &xres, NULL, },
        { 'i', "yres", &yres, NULL, },
        { 'i', "zres", &zres, NULL, },
        { 'd', "xreal", &xreal, NULL, },
        { 'd', "yreal", &yreal, NULL, },
        { 'd', "zreal", &zreal, NULL, },
        { 'd', "xoff", &xoff, NULL, },
        { 'd', "yoff", &yoff, NULL, },
        { 'd', "zoff", &zoff, NULL, },
        { 'o', "si_unit_x", &si_unit_x, NULL, },
        { 'o', "si_unit_y", &si_unit_y, NULL, },
        { 'o', "si_unit_z", &si_unit_z, NULL, },
        { 'o', "si_unit_w", &si_unit_w, NULL, },
        { 'D', "data", &data, &datasize, },
        { 'O', "calibration", &calibrations, &num_items, },
    };

    gwy_debug("");
    g_return_val_if_fail(buffer, NULL);

    if (!gwy_serialize_unpack_object_struct(buffer, size, position, GWY_BRICK_TYPE_NAME, G_N_ELEMENTS(spec), spec)) {
        g_free(data);
        GWY_OBJECT_UNREF(si_unit_x);
        GWY_OBJECT_UNREF(si_unit_y);
        GWY_OBJECT_UNREF(si_unit_z);
        GWY_OBJECT_UNREF(si_unit_w);
        GWY_OBJECT_UNREF(calibrations);

        return NULL;
    }
    if (datasize != (guint)(xres * yres * zres)) {
        g_critical("Serialized %s size mismatch %u != %u", GWY_BRICK_TYPE_NAME, datasize, xres*yres*zres);
        g_free(data);
        GWY_OBJECT_UNREF(si_unit_x);
        GWY_OBJECT_UNREF(si_unit_y);
        GWY_OBJECT_UNREF(si_unit_z);
        GWY_OBJECT_UNREF(si_unit_w);
        GWY_OBJECT_UNREF(calibrations);

        return NULL;
    }

    /* don't allocate large amount of memory just to immediately free it */
    brick = gwy_brick_new(1, 1, 1, xreal, yres, zreal, FALSE);
    g_free(brick->data);
    brick->xres = xres;
    brick->yres = yres;
    brick->zres = zres;

    brick->xreal = xreal;
    brick->yreal = yreal;
    brick->zreal = zreal;

    brick->xoff = xoff;
    brick->yoff = yoff;
    brick->zoff = zoff;

    brick->data = data;
    if (si_unit_x) {
        GWY_OBJECT_UNREF(brick->si_unit_x);
        brick->si_unit_x = si_unit_x;
    }
    if (si_unit_y) {
        GWY_OBJECT_UNREF(brick->si_unit_y);
        brick->si_unit_y = si_unit_y;
    }
    if (si_unit_z) {
        GWY_OBJECT_UNREF(brick->si_unit_z);
        brick->si_unit_z = si_unit_z;
    }
    if (si_unit_w) {
        GWY_OBJECT_UNREF(brick->si_unit_w);
        brick->si_unit_w = si_unit_w;
    }
    if (num_items > 0) {
        priv = (GwyBrickPrivate *)brick->priv;
        priv->zcalibration = calibrations[0];
        g_object_ref(priv->zcalibration);
    }

    for (i = 0; i < num_items; i++)
        GWY_OBJECT_UNREF(calibrations[i]);

    return (GObject*)brick;
}

static GObject*
gwy_brick_duplicate_real(GObject *object)
{
    GwyBrick *brick, *duplicate;

    g_return_val_if_fail(GWY_IS_BRICK(object), NULL);
    brick = GWY_BRICK(object);
    duplicate = gwy_brick_new_alike(brick, FALSE);
    gwy_assign(duplicate->data, brick->data, brick->xres * brick->yres * brick->zres);

    return (GObject*)duplicate;
}

static void
gwy_brick_clone_real(GObject *source, GObject *copy)
{
    GwyBrick *brick, *clone;

    g_return_if_fail(GWY_IS_BRICK(source));
    g_return_if_fail(GWY_IS_BRICK(copy));

    brick = GWY_BRICK(source);
    clone = GWY_BRICK(copy);

    if (clone->xres != brick->xres || clone->yres != brick->yres || clone->zres != brick->zres) {
        clone->xres = brick->xres;
        clone->yres = brick->yres;
        clone->zres = brick->zres;
        clone->data = g_renew(gdouble, clone->data, clone->xres * clone->yres * clone->zres);
    }
    clone->xreal = brick->xreal;
    clone->yreal = brick->yreal;
    clone->zreal = brick->zreal;
    clone->xoff = brick->xoff;
    clone->yoff = brick->yoff;
    clone->zoff = brick->zoff;

    gwy_assign(clone->data, brick->data, brick->xres * brick->yres * brick->zres);

    gwy_brick_copy_units(brick, clone);
    gwy_brick_copy_zcalibration(brick, clone);
}

/**
 * gwy_brick_data_changed:
 * @brick: A data brick.
 *
 * Emits signal "data_changed" on a data brick.
 *
 * Since: 2.31
 **/
void
gwy_brick_data_changed(GwyBrick *brick)
{
    g_signal_emit(brick, brick_signals[DATA_CHANGED], 0);
}

/**
 * gwy_brick_resample:
 * @brick: A data brick.
 * @xres: Desired x resolution.
 * @yres: Desired y resolution.
 * @zres: Desired z resolution.
 * @interpolation: Interpolation method to use.
 *
 * Resamples a data brick.
 *
 * In other words changes the size of three dimensional field related with data brick. The original values are used
 * for resampling using a requested interpolation alorithm.
 *
 * Since: 2.31
 **/
void
gwy_brick_resample(GwyBrick *brick,
                   gint xres,
                   gint yres,
                   gint zres,
                   GwyInterpolationType interpolation)
{
    gdouble *bdata, *srcdata;
    gint row, col, lev;
    gint srcxres, srcyres, srczres;
    gdouble xratio, yratio, zratio;

    g_return_if_fail(GWY_IS_BRICK(brick));
    srcxres = brick->xres;
    srcyres = brick->yres;
    srczres = brick->zres;
    if ((xres == srcxres) && (yres == srcyres) && (zres == srczres))
        return;
    g_return_if_fail(xres > 1 && yres > 1 && zres > 1);

    if (interpolation == GWY_INTERPOLATION_NONE) {
        brick->xres = xres;
        brick->yres = yres;
        brick->zres = zres;
        brick->data = g_renew(gdouble, brick->data, xres*yres*zres);
        return;
    }

    bdata = g_new(gdouble, xres*yres*zres);

    xratio = (gdouble)srcxres/xres;
    yratio = (gdouble)srcyres/yres;
    zratio = (gdouble)srczres/zres;

    if (interpolation != GWY_INTERPOLATION_ROUND) {
        g_warning("Only the ROUND interpolation method is implemented.");
        interpolation = GWY_INTERPOLATION_ROUND;
    }

    if (interpolation == GWY_INTERPOLATION_ROUND) {
        srcdata = brick->data;
        for (lev = 0; lev < zres; lev++) {
            gint srclev = MIN((gint)(zratio*lev + 0.5), brick->zres-1);
            for (row = 0; row < yres; row++) {
                gint srcrow = MIN((gint)(yratio*row + 0.5), brick->yres-1);
                for (col = 0; col < xres; col++) {
                    gint srccol = MIN((gint)(xratio*col + 0.5), brick->xres-1);
                    bdata[col + xres*(row + yres*lev)] = srcdata[srccol + srcxres*(srcrow + srcyres*srclev)];
                }
            }
        }
    }

    g_free(brick->data);
    brick->data = bdata;
    brick->xres = xres;
    brick->yres = yres;
    brick->zres = zres;
}


/**
 * gwy_brick_copy:
 * @src: Source brick.
 * @dest: Destination brick.
 * @nondata_too: Whether non-data (units) should be copied too.
 *
 * Copies the contents of an already allocated brick to a brick of the same size.
 **/
void
gwy_brick_copy(GwyBrick *src,
               GwyBrick *dest,
               gboolean nondata_too)
{
    g_return_if_fail(GWY_IS_BRICK(src));
    g_return_if_fail(GWY_IS_BRICK(dest));
    g_return_if_fail(src->xres == dest->xres && src->yres == dest->yres && src->zres == dest->zres);

    if (src == dest)
        return;

    gwy_assign(dest->data, src->data, src->xres*src->yres*src->zres);

    dest->xreal = src->xreal;
    dest->yreal = src->yreal;
    dest->zreal = src->zreal;

    if (!nondata_too)
        return;

    gwy_brick_copy_units(src, dest);
}

/**
 * gwy_brick_get_dval:
 * @brick: A data brick.
 * @x: Position in data brick in range [0, x resolution]. If the value is outside this range, the nearest border value
 *     is returned.
 * @y: Position in data brick in range [0, y resolution]. If the value is outside this range, the nearest border value
 *     is returned.
 * @z: Position in data brick in range [0, z resolution]. If the value is outside this range, the nearest border value
 *     is returned.
 * @interpolation: Interpolation method to use.
 *
 * Gets interpolated value at arbitrary data brick point indexed by pixel coordinates.
 *
 * Note that pixel values are centered in intervals [@i, @i+1]. See also gwy_brick_get_dval_real() that does the same,
 * but takes real coordinates.
 *
 * Returns: Value interpolated in the data brick.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_dval(GwyBrick *a,
                   gdouble x, gdouble y, gdouble z,
                   gint interpolation)
{
    g_return_val_if_fail(GWY_IS_BRICK(a), 0.0);

    if (G_UNLIKELY(interpolation == GWY_INTERPOLATION_NONE))
        return 0.0;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (z < 0)
        z = 0;

    if (interpolation != GWY_INTERPOLATION_ROUND) {
        g_warning("Only the ROUND interpolation method is implemented.");
        interpolation = GWY_INTERPOLATION_ROUND;
    }

    switch (interpolation) {
        case GWY_INTERPOLATION_ROUND:
        return (a->data[MIN((gint)(x + 0.5), a->xres-1)
                        + a->xres*MIN((gint)(y + 0.5), a->yres-1)
                        + a->xres*a->yres*MIN((gint)(z + 0.5), a->zres-1)]);
        break;
    }
    return 0.0;
}

/**
 * gwy_brick_get_dval_real:
 * @brick: A data brick.
 * @x: Position in data brick in range [0, x resolution].  If the value is outside this range, the nearest border
 *     value is returned.
 * @y: Position in data brick in range [0, y resolution].  If the value is outside this range, the nearest border
 *     value is returned.
 * @z: Position in data brick in range [0, z resolution].  If the value is outside this range, the nearest border
 *     value is returned.
 * @interpolation: Interpolation method to use.
 *
 * Gets interpolated value at arbitrary data brick point indexed by pixel coordinates.
 *
 * Note pixel values are centered in intervals [@j, @j+1]. See also gwy_brick_get_dval() that does the same, but takes
 * pixel coordinates.
 *
 * Returns: Value interpolated in the data brick.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_dval_real(GwyBrick *a, gdouble x, gdouble y, gdouble z, gint interpolation)
{
    gdouble xratio, yratio, zratio;

    g_return_val_if_fail(GWY_IS_BRICK(a), 0.0);

    if (G_UNLIKELY(interpolation == GWY_INTERPOLATION_NONE))
        return 0.0;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (z < 0)
        z = 0;

    xratio = a->xres/a->xreal;
    yratio = a->yres/a->yreal;
    zratio = a->zres/a->zreal;

    if (interpolation != GWY_INTERPOLATION_ROUND) {
        g_warning("Only the ROUND interpolation method is implemented.");
        interpolation = GWY_INTERPOLATION_ROUND;
    }

    switch (interpolation) {
        case GWY_INTERPOLATION_ROUND:
        return a->data[MIN((gint)(x*xratio + 0.5), a->xres-1)
                       + a->xres*MIN((gint)(y*yratio + 0.5), a->yres-1)
                       + a->xres*a->yres*MIN((gint)(z*zratio + 0.5), a->zres-1)];
        break;
    }
    return 0.0;
}

/**
 * gwy_brick_get_data:
 * @brick: A data brick.
 *
 * Gets the raw data buffer of a data brick.
 *
 * The returned buffer is not guaranteed to be valid through whole data
 * brick life time.  Some function may change it, most notably
 * gwy_brick_resample().
 *
 * This function invalidates any cached information, use
 * gwy_brick_get_data_const() if you are not going to change the data.
 *
 * Returns: The data as an array of doubles of length @xres*@yres*@zres.
 *
 * Since: 2.31
 **/
gdouble*
gwy_brick_get_data(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);
    return brick->data;
}

/**
 * gwy_brick_get_data_const:
 * @brick: A data brick.
 *
 * Gets the raw data buffer of a data brick, read-only.
 *
 * The returned buffer is not guaranteed to be valid through whole data brick life time.  Some function may change it,
 * most notably gwy_brick_resample().
 *
 * Use gwy_brick_get_data() if you want to change the data.
 *
 * Returns: The data as an array of doubles of length @xres*@yres*@zres.
 *
 * Since: 2.31
 **/
const gdouble*
gwy_brick_get_data_const(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);
    return (const gdouble*)brick->data;
}

/**
 * gwy_brick_get_xres:
 * @brick: A data brick.
 *
 * Gets the x resolution of a data brick.
 *
 * Returns: Resolution (number of data points).
 *
 * Since: 2.31
 **/
gint
gwy_brick_get_xres(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0);
    return brick->xres;
}

/**
 * gwy_brick_get_yres:
 * @brick: A data brick.
 *
 * Gets the y resolution of a data brick.
 *
 * Returns: Resolution (number of data points).
 *
 * Since: 2.31
 **/
gint
gwy_brick_get_yres(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0);
    return brick->yres;
}
/**
 * gwy_brick_get_zres:
 * @brick: A data line.
 *
 * Gets the z resolution of a data brick.
 *
 * Returns: Resolution (number of data points).
 *
 * Since: 2.31
 **/
gint
gwy_brick_get_zres(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0);
    return brick->zres;
}

/**
 * gwy_brick_get_xreal:
 * @brick: A data brick.
 *
 * Gets the physical size of a data brick in the x direction.
 *
 * Returns: Real size of a data brick the x direction.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_xreal(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->xreal;
}

/**
 * gwy_brick_get_yreal:
 * @brick: A data brick.
 *
 * Gets the physical size of a data brick in the y direction.
 *
 * Returns: Real size of a data brick the y direction.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_yreal(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->yreal;
}
/**
 * gwy_brick_get_zreal:
 * @brick: A data brick.
 *
 * Gets the physical size of a data brick in the z direction.
 *
 * Returns: Real size of a data brick the z direction.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_zreal(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->zreal;
}

/**
 * gwy_brick_get_xoffset:
 * @brick: A data brick.
 *
 * Gets the offset of data brick origin in x direction.
 *
 * Returns: Offset value.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_xoffset(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->xoff;
}

/**
 * gwy_brick_get_yoffset:
 * @brick: A data brick.
 *
 * Gets the offset of data brick origin in y direction.
 *
 * Returns: Offset value.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_yoffset(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->yoff;
}

/**
 * gwy_brick_get_zoffset:
 * @brick: A data brick.
 *
 * Gets the offset of data brick origin in z direction.
 *
 * Returns: Offset value.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_zoffset(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->zoff;
}

/**
 * gwy_brick_set_xoffset:
 * @brick: A data brick.
 * @xoffset: New offset value.
 *
 * Sets the offset of a data brick origin in the x direction.
 *
 * Note offsets don't affect any calculation, nor functions like gwy_brick_rtoi().
 *
 * Since: 2.31
 **/
void
gwy_brick_set_xoffset(GwyBrick *brick,
                      gdouble xoffset)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    brick->xoff = xoffset;
}

/**
 * gwy_brick_set_yoffset:
 * @brick: A data brick.
 * @yoffset: New offset value.
 *
 * Sets the offset of a data brick origin in the y direction.
 *
 * Note offsets don't affect any calculation, nor functions like gwy_brick_rtoi().
 *
 * Since: 2.31
 **/
void
gwy_brick_set_yoffset(GwyBrick *brick,
                      gdouble yoffset)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    brick->yoff = yoffset;
}
/**
 * gwy_brick_set_zoffset:
 * @brick: A data brick.
 * @zoffset: New offset value.
 *
 * Sets the offset of a data brick origin in the z direction.
 *
 * Note offsets don't affect any calculation, nor functions like gwy_brick_rtoi().
 *
 * Since: 2.31
 **/
void
gwy_brick_set_zoffset(GwyBrick *brick,
                      gdouble zoffset)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    brick->zoff = zoffset;
}

/**
 * gwy_brick_set_xreal:
 * @brick: A data brick.
 * @xreal: New real x dimensions value
 *
 * Sets the real x dimension of a brick.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_xreal(GwyBrick *brick, gdouble xreal)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(xreal > 0.0);
    brick->xreal = xreal;
}

/**
 * gwy_brick_set_yreal:
 * @brick: A data brick.
 * @yreal: New real y dimensions value
 *
 * Sets the real y dimension of a brick.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_yreal(GwyBrick *brick, gdouble yreal)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(yreal > 0.0);
    brick->yreal = yreal;
}

/**
 * gwy_brick_set_zreal:
 * @brick: A data brick.
 * @zreal: New real z dimensions value
 *
 * Sets the real z dimension of a brick.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_zreal(GwyBrick *brick, gdouble zreal)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(zreal > 0.0);
    brick->zreal = zreal;
}

/**
 * gwy_brick_get_dx:
 * @brick: A data brick.
 *
 * Gets the horizontal (X) voxel size of a brick in real units.
 *
 * The result is the same as gwy_brick_get_xreal(brick)/gwy_brick_get_xres(brick).
 *
 * Returns: Horizontal voxel size.
 *
 * Since: 2.52
 **/
gdouble
gwy_brick_get_dx(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->xreal/brick->xres;
}

/**
 * gwy_brick_get_dy:
 * @brick: A data brick.
 *
 * Gets the vertical (Y) voxel size of a brick in real units.
 *
 * The result is the same as gwy_brick_get_yreal(brick)/gwy_brick_get_yres(brick).
 *
 * Returns: Vertical voxel size.
 *
 * Since: 2.52
 **/
gdouble
gwy_brick_get_dy(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->yreal/brick->yres;
}

/**
 * gwy_brick_get_dz:
 * @brick: A data brick.
 *
 * Gets the level-wise (Z) voxel size of a brick in real units.
 *
 * The result is the same as gwy_brick_get_zreal(brick)/gwy_brick_get_zres(brick).
 *
 * Note that it cannot -- and hence does not -- take into account any attached Z calibration.
 *
 * Returns: Level-wise voxel size.
 *
 * Since: 2.52
 **/
gdouble
gwy_brick_get_dz(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    return brick->zreal/brick->zres;
}

/**
 * gwy_brick_get_si_unit_x:
 * @brick: A data brick.
 *
 * Returns x direction SI unit of a data brick.
 *
 * Returns: SI unit corresponding to the lateral (X) dimension of the data brick.  Its reference count is not
 *          incremented.
 *
 * Since: 2.31
 **/
GwySIUnit*
gwy_brick_get_si_unit_x(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    if (!brick->si_unit_x)
        brick->si_unit_x = gwy_si_unit_new(NULL);

    return brick->si_unit_x;
}

/**
 * gwy_brick_get_si_unit_y:
 * @brick: A data brick.
 *
 * Returns y direction SI unit of a data brick.
 *
 * Returns: SI unit corresponding to the lateral (Y) dimension of the data brick.  Its reference count is not
 *          incremented.
 *
 * Since: 2.31
 **/
GwySIUnit*
gwy_brick_get_si_unit_y(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    if (!brick->si_unit_y)
        brick->si_unit_y = gwy_si_unit_new(NULL);

    return brick->si_unit_y;
}

/**
 * gwy_brick_get_si_unit_z:
 * @brick: A data brick.
 *
 * Returns z direction SI unit of a data brick.
 *
 * Returns: SI unit corresponding to the "height" (Z) dimension of the data brick.  Its reference count is not
 *          incremented.
 *
 * Since: 2.31
 **/
GwySIUnit*
gwy_brick_get_si_unit_z(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    if (!brick->si_unit_z)
        brick->si_unit_z = gwy_si_unit_new(NULL);

    return brick->si_unit_z;
}

/**
 * gwy_brick_get_si_unit_w:
 * @brick: A data brick.
 *
 * Returns value SI unit of a data brick.
 *
 * Returns: SI unit corresponding to the "value" of the data brick.  Its reference count is not incremented.
 *
 * Since: 2.31
 **/
GwySIUnit*
gwy_brick_get_si_unit_w(GwyBrick *brick)
{
    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    if (!brick->si_unit_w)
        brick->si_unit_w = gwy_si_unit_new(NULL);

    return brick->si_unit_w;
}

/**
 * gwy_brick_set_si_unit_x:
 * @brick: A data brick.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding to the lateral (X) dimension of a data brick.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_si_unit_x(GwyBrick *brick,
                        GwySIUnit *si_unit)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    _gwy_set_object_si_unit(si_unit, &brick->si_unit_x);
}

/**
 * gwy_brick_set_si_unit_y:
 * @brick: A data brick.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding to the lateral (Y) dimension of a data brick.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_si_unit_y(GwyBrick *brick,
                        GwySIUnit *si_unit)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    _gwy_set_object_si_unit(si_unit, &brick->si_unit_y);
}

/**
 * gwy_brick_set_si_unit_z:
 * @brick: A data brick.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding to the "height" (Z) dimension of a data brick.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_si_unit_z(GwyBrick *brick,
                        GwySIUnit *si_unit)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    _gwy_set_object_si_unit(si_unit, &brick->si_unit_z);
}

/**
 * gwy_brick_set_si_unit_w:
 * @brick: A data brick.
 * @si_unit: SI unit to be set.
 *
 * Sets the SI unit corresponding to the "value"  of a data brick.
 *
 * It does not assume a reference on @si_unit, instead it adds its own reference.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_si_unit_w(GwyBrick *brick,
                        GwySIUnit *si_unit)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    _gwy_set_object_si_unit(si_unit, &brick->si_unit_w);
}

/**
 * gwy_brick_copy_units:
 * @brick: A data brick.
 * @target: Target data brick.
 *
 * Sets lateral and value units of a data brick to match another data brick.
 *
 * Since: 2.49
 **/
void
gwy_brick_copy_units(const GwyBrick *brick, GwyBrick *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_BRICK(target));

    _gwy_copy_si_unit(brick->si_unit_x, &target->si_unit_x);
    _gwy_copy_si_unit(brick->si_unit_y, &target->si_unit_y);
    _gwy_copy_si_unit(brick->si_unit_z, &target->si_unit_z);
    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_w);
}

/**
 * gwy_brick_get_value_format_x:
 * @brick: A data brick.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying coordinates of a data brick.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.31
 **/
GwySIValueFormat*
gwy_brick_get_value_format_x(GwyBrick *brick,
                             GwySIUnitFormatStyle style,
                             GwySIValueFormat *format)
{
    gdouble max, unit;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    max = brick->xreal;
    unit = brick->xreal/brick->xres;
    return gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_x(brick), style, max, unit, format);
}

/**
 * gwy_brick_get_value_format_y:
 * @brick: A data brick.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying values of a data brick.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.31
 **/
GwySIValueFormat*
gwy_brick_get_value_format_y(GwyBrick *brick,
                             GwySIUnitFormatStyle style,
                             GwySIValueFormat *format)
{
    gdouble max, unit;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    max = brick->yreal;
    unit = brick->yreal/brick->yres;
    return gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_y(brick), style, max, unit, format);
}

/**
 * gwy_brick_get_value_format_z:
 * @brick: A data brick.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying values of a data brick.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.31
 **/
GwySIValueFormat*
gwy_brick_get_value_format_z(GwyBrick *brick,
                             GwySIUnitFormatStyle style,
                             GwySIValueFormat *format)
{
    gdouble max, unit;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    max = brick->zreal;
    unit = brick->zreal/brick->zres;
    return gwy_si_unit_get_format_with_resolution(gwy_brick_get_si_unit_z(brick), style, max, unit, format);
}

/**
 * gwy_brick_get_min:
 * @brick: A data brick.
 *
 * Find the minimum value in a data brick.
 *
 * Returns: The minimum value within the brick.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_min(GwyBrick *brick)
{
    gint i, n;
    gdouble min = G_MAXDOUBLE;

    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    n = brick->xres * brick->yres * brick->zres;
    for (i = 0; i < n; i++) {
        if (brick->data[i] < min)
            min = brick->data[i];
    }
    return min;
}

/**
 * gwy_brick_get_max:
 * @brick: A data brick.
 *
 * Find the maximum value in a data brick.
 *
 * Returns: The maximum value within the brick.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_max(GwyBrick *brick)
{
    gint i, n;
    gdouble max = -G_MAXDOUBLE;

    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    n = brick->xres * brick->yres * brick->zres;
    for (i = 0; i < n; i++) {
        if (brick->data[i] > max)
            max = brick->data[i];
    }
    return max;
}

/**
 * gwy_brick_get_value_format_w:
 * @brick: A data brick.
 * @style: Unit format style.
 * @format: A SI value format to modify, or %NULL to allocate a new one.
 *
 * Finds value format good for displaying values of a data brick.
 *
 * Note this functions searches for minimum and maximum value in @brick, therefore it's relatively slow.
 *
 * Returns: The value format.  If @format is %NULL, a newly allocated format is returned, otherwise (modified) @format
 *          itself is returned.
 *
 * Since: 2.31
 **/
GwySIValueFormat*
gwy_brick_get_value_format_w(GwyBrick *brick,
                             GwySIUnitFormatStyle style,
                             GwySIValueFormat *format)
{
    gdouble max, min;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    max = gwy_brick_get_max(brick);
    min = gwy_brick_get_min(brick);
    if (max == min) {
        max = ABS(max);
        min = 0.0;
    }

    return gwy_si_unit_get_format(gwy_brick_get_si_unit_w(brick), style, max - min, format);
}

/**
 * gwy_brick_itor:
 * @brick: A data brick.
 * @pixpos: Pixel coordinate.
 *
 * Transforms pixel coordinate to real (physical) coordinate in x direction.
 *
 * That is it maps range [0..x resolution] to range [0..x real-size].  It is not suitable for conversion of matrix
 * indices to physical coordinates, you have to use gwy_brick_itor(@brick, @pixpos + 0.5) for that.
 *
 * Returns: @pixpos in real coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_itor(GwyBrick *brick, gdouble pixpos)
{
    return pixpos * brick->xreal/brick->xres;
}

/**
 * gwy_brick_jtor:
 * @brick: A data brick.
 * @pixpos: Pixel coordinate.
 *
 * Transforms pixel coordinate to real (physical) coordinate in y direction.
 *
 * That is it maps range [0..y resolution] to range [0..y real-size].  It is not suitable for conversion of matrix
 * indices to physical coordinates, you have to use gwy_brick_itor(@brick, @pixpos + 0.5) for that.
 *
 * Returns: @pixpos in real coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_jtor(GwyBrick *brick, gdouble pixpos)
{
    return pixpos * brick->yreal/brick->yres;
}

/**
 * gwy_brick_ktor:
 * @brick: A data brick.
 * @pixpos: Pixel coordinate.
 *
 * Transforms pixel coordinate to real (physical) coordinate in z direction.
 *
 * That is it maps range [0..z resolution] to range [0..z real-size].  It is not suitable for conversion of matrix
 * indices to physical coordinates, you have to use gwy_brick_itor(@brick, @pixpos + 0.5) for that.
 *
 * Returns: @pixpos in real coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_ktor(GwyBrick *brick, gdouble pixpos)
{
    return pixpos * brick->zreal/brick->zres;
}

/**
 * gwy_brick_rtoi:
 * @brick: A data brick.
 * @realpos: Real coordinate.
 *
 * Transforms real (physical) coordinate to pixel coordinate in x axis.
 *
 * That is it maps range [0..x real-size] to range [0..x resolution].
 *
 * Returns: @realpos in pixel coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_rtoi(GwyBrick *brick, gdouble realpos)
{
    return realpos * brick->xres/brick->xreal;
}

/**
 * gwy_brick_rtoj:
 * @brick: A data brick.
 * @realpos: Real coordinate.
 *
 * Transforms real (physical) coordinate to pixel coordinate in y axis.
 *
 * That is it maps range [0..y real-size] to range [0..y resolution].
 *
 * Returns: @realpos in pixel coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_rtoj(GwyBrick *brick, gdouble realpos)
{
    return realpos * brick->yres/brick->yreal;
}

/**
 * gwy_brick_rtok:
 * @brick: A data brick.
 * @realpos: Real coordinate.
 *
 * Transforms real (physical) coordinate to pixel coordinate in z axis.
 *
 * That is it maps range [0..z real-size] to range [0..z resolution].
 *
 * Returns: @realpos in pixel coordinates.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_rtok(GwyBrick *brick, gdouble realpos)
{
    return realpos * brick->zres/brick->zreal;
}

/**
 * gwy_brick_ktor_cal:
 * @brick: A data brick.
 * @pixpos: Pixel coordinate.
 *
 * Transforms pixel coordinate to real (physical) coordinate in z direction, taking into account calibration.
 *
 * Unlike gwy_brick_ktor(), this function takes into account the @z calibration and, if calibration is not present,
 * the @z axis offset. Since the calibration is available only for discrete pixel coordinates, the values are
 * interpolated between and clamped if outside the range.
 *
 * The values in the calibration are assumed to correspond to pixel centres. This convention is also kept when no
 * calibration is present.
 *
 * Returns: @pixpos in real coordinates.
 *
 * Since: 2.42
 **/
gdouble
gwy_brick_ktor_cal(GwyBrick *brick,
                   gdouble pixpos)
{
    GwyBrickPrivate *priv = (GwyBrickPrivate*)brick->priv;
    GwyDataLine *calibration;
    const gdouble *cdata;
    gint i;

    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    calibration = priv->zcalibration;

    if (!calibration)
        return (pixpos + 0.5)*brick->zreal/brick->zres + brick->zoff;

    i = (gint)floor(pixpos);
    cdata = calibration->data;
    if (i < 0)
        return cdata[0];
    if (i >= calibration->res-1)
        return cdata[calibration->res-1];

    pixpos -= i;
    return cdata[i]*(1.0 - pixpos) + cdata[i+1]*pixpos;
}

/**
 * gwy_brick_rtok_cal:
 * @brick: A data brick.
 * @realpos: Real coordinate.
 *
 * Transforms real (physical) coordinate to pixel coordinate in z axis, taking into account calibration.
 *
 * Unlike gwy_brick_rtok(), this function takes into account the @z calibration and, if calibration is not present,
 * the @z axis offset. Since the calibration is available only for discrete pixel coordinates, the values are
 * interpolated between and clamped if outside the range.
 *
 * The values in the calibration are assumed to correspond to pixel centres. This convention is also kept when no
 * calibration is present.
 *
 * Returns: @realpos in pixel coordinates.
 *
 * Since: 2.42
 **/
gdouble
gwy_brick_rtok_cal(GwyBrick *brick,
                   gdouble realpos)
{
    GwyBrickPrivate *priv = (GwyBrickPrivate*)brick->priv;
    GwyDataLine *calibration;
    const gdouble *cdata;
    gdouble t;
    gint i, j, k;

    g_return_val_if_fail(GWY_IS_BRICK(brick), 0.0);
    calibration = priv->zcalibration;

    if (!calibration)
        return (realpos - brick->zoff)/brick->zreal*brick->zres - 0.5;

    cdata = calibration->data;
    i = 0;
    j = calibration->res-1;
    if (cdata[i] <= cdata[j]) {
        /* The normal increasing case. */
        if (realpos <= cdata[i])
            return i;
        if (realpos >= cdata[j])
            return j;

        /* Now cdata[i] < realpos < cdata[j]. Use bisection. */
        do {
            k = (i + j)/2;
            if (realpos < cdata[k])
                j = k;
            else
                i = k;
        } while (j > i+1);

        g_assert(j == i+1);

        if (cdata[j] <= cdata[i])
            return i;

        t = (realpos - cdata[i])/(cdata[j] - cdata[i]);
        return i + GWY_CLAMP(t, 0.0, 1.0);
    }
    else {
        /* The decreasing case. XXX: We probably do not actually support this and other things can break. */
        if (realpos <= cdata[j])
            return j;
        if (realpos >= cdata[i])
            return i;

        /* Now cdata[i] < realpos < cdata[j]. Use bisection. */
        do {
            k = (i + j)/2;
            if (realpos < cdata[k])
                i = k;
            else
                j = k;
        } while (j > i+1);

        g_assert(j == i+1);

        if (cdata[i] <= cdata[j])
            return i;

        t = (realpos - cdata[j])/(cdata[i] - cdata[j]);
        return j - GWY_CLAMP(t, 0.0, 1.0);
    }
}

static inline gboolean
_gwy_brick_check_inside(const GwyBrick *brick, gint col, gint row, gint lev)
{
    return col >= 0 && col < brick->xres && row >= 0 && row < brick->yres && lev >= 0 && lev < brick->zres;
}

/**
 * gwy_brick_get_val:
 * @brick: A data brick.
 * @col: Position in the brick (column index).
 * @row: Position in the brick (row index).
 * @lev: Position in the brick (level index).
 *
 * Gets value at given position in a data brick.
 *
 * Do not access data with this function inside inner loops, it's slow. Get raw data buffer with
 * gwy_brick_get_data_const() and access it directly instead.
 *
 * Returns: Value at given index.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_val(GwyBrick *brick,
                  gint col,
                  gint row,
                  gint lev)
{
    g_return_val_if_fail(_gwy_brick_check_inside(brick, col, row, lev), 0.0);
    return brick->data[col + brick->xres*row + brick->xres*brick->yres*lev];
}

/**
 * gwy_brick_set_val:
 * @brick: A data brick.
 * @col: Position in the brick (column index).
 * @row: Position in the brick (row index).
 * @lev: Position in the brick (level index).
 * @value: Value to be set.
 *
 * Sets value at given position in a data brick.
 *
 * Do not access data with this function inside inner loops, it's slow. Get raw data buffer with
 * gwy_brick_get_data_const() and access it directly instead.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_val(GwyBrick *brick,
                  gint col,
                  gint row,
                  gint lev,
                  gdouble value)
{
    g_return_if_fail(_gwy_brick_check_inside(brick, col, row, lev));
    brick->data[col + brick->xres*row + brick->xres*brick->yres*lev] = value;
}

/**
 * gwy_brick_get_val_real:
 * @brick: A data brick.
 * @x: Position in the brick (x direction).
 * @y: Position in the brick (y direction).
 * @z: Position in the brick (z direction).
 *
 * Gets value at given position in a data brick, in real coordinates.
 *
 * Do not access data with this function inside inner loops, it's slow.
 * Get raw data buffer with gwy_brick_get_data_const() and access it
 * directly instead.
 *
 * Returns: Value at given index.
 *
 * Since: 2.31
 **/
gdouble
gwy_brick_get_val_real(GwyBrick *brick,
                       gdouble x,
                       gdouble y,
                       gdouble z)
{
    gint col = gwy_brick_rtoi(brick, x);
    gint row = gwy_brick_rtoj(brick, y);
    gint lev = gwy_brick_rtok(brick, z);

    g_return_val_if_fail(_gwy_brick_check_inside(brick, col, row, lev), 0.0);

    return brick->data[col + brick->xres*row + brick->xres*brick->yres*lev];
}

/**
 * gwy_brick_set_val_real:
 * @brick: A data brick.
 * @x: Position in the brick (x direction).
 * @y: Position in the brick (y direction).
 * @z: Position in the brick (z direction).
 * @value: Value to be set.
 *
 * Sets value at given position in a data brick.
 *
 * Do not access data with this function inside inner loops, it's slow.
 * Get raw data buffer with gwy_brick_get_data_const() and access it
 * directly instead.
 *
 * Since: 2.31
 **/
void
gwy_brick_set_val_real(GwyBrick *brick,
                       gdouble x,
                       gdouble y,
                       gdouble z,
                       gdouble value)
{
    gint col = gwy_brick_rtoi(brick, x);
    gint row = gwy_brick_rtoj(brick, y);
    gint lev = gwy_brick_rtok(brick, z);

    g_return_if_fail(_gwy_brick_check_inside(brick, col, row, lev));

    brick->data[col + brick->xres*row + brick->xres*brick->yres*lev] = value;
}

/**
 * gwy_brick_fill:
 * @brick: A data brick.
 * @value: Value to fill data brick with.
 *
 * Fills a data brick with specified value.
 *
 * Since: 2.31
 **/
void
gwy_brick_fill(GwyBrick *brick,
               gdouble value)
{
    gint i;

    g_return_if_fail(GWY_IS_BRICK(brick));
    for (i = 0; i < (brick->xres*brick->yres*brick->zres); i++)
        brick->data[i] = value;
}

/**
 * gwy_brick_clear:
 * @brick: A data brick.
 *
 * Fills a data brick with zeroes.
 *
 * Since: 2.31
 **/
void
gwy_brick_clear(GwyBrick *brick)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_clear(brick->data, brick->xres*brick->yres*brick->zres);
}

/**
 * gwy_brick_add:
 * @brick: A data brick.
 * @value: Value to be added.
 *
 * Adds a specified value to all values in a data brick.
 *
 * Since: 2.31
 **/
void
gwy_brick_add(GwyBrick *brick,
              gdouble value)
{
    gint i, n;

    g_return_if_fail(GWY_IS_BRICK(brick));
    n = brick->xres * brick->yres * brick->zres;
    for (i = 0; i < n; i++)
        brick->data[i] += value;
}

/**
 * gwy_brick_multiply:
 * @brick: A data brick.
 * @value: Value to multiply data brick with.
 *
 * Multiplies all values in a data brick with a specified value.
 *
 * Since: 2.31
 **/
void
gwy_brick_multiply(GwyBrick *brick,
                   gdouble value)
{
    gint i, n;

    g_return_if_fail(GWY_IS_BRICK(brick));
    n = brick->xres * brick->yres * brick->zres;
    for (i = 0; i < n; i++)
        brick->data[i] *= value;
}

static gboolean
gwy_brick_extract_field_common(const GwyBrick *brick,
                               GwyDataField *target,
                               gint istart, gint jstart, gint kstart,
                               gint width, gint height, gint depth,
                               gboolean keep_offsets)
{
    gint xres, yres, zres;
    gdouble dx, dy, dz;

    g_return_val_if_fail(GWY_IS_BRICK(brick), FALSE);
    g_return_val_if_fail(GWY_IS_DATA_FIELD(target), FALSE);
    g_return_val_if_fail((width == -1 && height > 0 && depth > 0)
                         || (width > 0 && height == -1 && depth > 0)
                         || (width > 0 && height > 0 && depth == -1), FALSE);
    g_return_val_if_fail(_gwy_brick_check_inside(brick, istart, jstart, kstart), FALSE);

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    dx = brick->xreal/xres;
    dy = brick->yreal/yres;
    dz = brick->zreal/zres;

    if (width == -1 && height > 0 && depth > 0) {
        g_return_val_if_fail(jstart + height <= yres, FALSE);
        g_return_val_if_fail(kstart + depth <= zres, FALSE);
        gwy_data_field_resample(target, height, depth, GWY_INTERPOLATION_NONE);
        target->xreal = dy*height;
        target->yreal = dz*depth;
        if (keep_offsets) {
            target->xoff = dy*jstart + brick->yoff;
            target->yoff = dz*kstart + brick->zoff;
        }
        _gwy_copy_si_unit(brick->si_unit_y, &target->si_unit_xy);
    }
    else if (width > 0 && height == -1 && depth > 0) {
        g_return_val_if_fail(istart + width <= xres, FALSE);
        g_return_val_if_fail(kstart + depth <= zres, FALSE);
        gwy_data_field_resample(target, width, depth, GWY_INTERPOLATION_NONE);
        target->xreal = dx*width;
        target->yreal = dz*depth;
        if (keep_offsets) {
            target->xoff = dx*istart + brick->xoff;
            target->yoff = dz*kstart + brick->zoff;
        }
        _gwy_copy_si_unit(brick->si_unit_x, &target->si_unit_xy);
    }
    else if (width > 0 && height > 0 && depth == -1) {
        g_return_val_if_fail(istart + width <= xres, FALSE);
        g_return_val_if_fail(jstart + height <= yres, FALSE);
        gwy_data_field_resample(target, width, height, GWY_INTERPOLATION_NONE);
        target->xreal = dx*width;
        target->yreal = dy*height;
        if (keep_offsets) {
            target->xoff = dx*istart + brick->xoff;
            target->yoff = dy*jstart + brick->yoff;
        }
        _gwy_copy_si_unit(brick->si_unit_x, &target->si_unit_xy);
    }
    else {
        g_assert_not_reached();
    }

    if (!keep_offsets)
        target->xoff = target->yoff = 0.0;

    return TRUE;
}

/**
 * gwy_brick_extract_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by extracted plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of extracted plane. If @width is -1, the yz plane will be extracted.
 * @height: Pixel height of extracted plane.  If @height is -1, the xz plane will be extracted
 * @depth: Pixel depth of extracted plane. If @depth is -1, the xy plane will be extracted
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Extracts a plane from a data brick.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation.
 *
 * Use gwy_brick_extract_xy_plane() to simply extract one entire XY plane.
 *
 * Since: 2.31
 **/
void
gwy_brick_extract_plane(const GwyBrick *brick,
                        GwyDataField *target,
                        gint istart, gint jstart, gint kstart,
                        gint width, gint height, gint depth,
                        gboolean keep_offsets)
{
    gint col, row, lev, xres, yres;
    const gdouble *bdata, *b;
    gdouble *ddata, *d;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    bdata = brick->data;
    ddata = target->data;

    if (width == -1 && height > 0 && depth > 0) {
        col = istart;
        d = ddata;
        for (lev = 0; lev < depth; lev++) {
            b = bdata + col + xres*jstart + xres*yres*(lev + kstart);
            for (row = 0; row < height; row++, d++, b += xres)
                *d = *b;
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        row = jstart;
        for (lev = 0; lev < depth; lev++) {
            gwy_assign(ddata + lev*width,
                       bdata + istart + xres*row + xres*yres*(lev + kstart),
                       width);
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        lev = kstart;
        for (row = 0; row < height; row++) {
            gwy_assign(ddata + row*width,
                       bdata + istart + xres*(row + jstart) + xres*yres*(lev),
                       width);
        }
    }

    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_z);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_extract_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by extracted plane. It will be resized if necessary.
 * @lev: Position in the brick (level index).
 *
 * Extracts one full XY plane of a data brick to a data field.
 *
 * Since: 2.52
 **/
void
gwy_brick_extract_xy_plane(const GwyBrick *brick,
                           GwyDataField *target,
                           gint lev)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_extract_plane(brick, target, 0, 0, lev, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_sum_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by summed plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summed plane. If @width is -1, the yz planes will be summed.
 * @height: Pixel height of summed plane.  If @height is -1, the xz planes will be summed
 * @depth: Pixel depth of summed plane. If @depth is -1, the xy planes will be summed
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Sums planes in certain direction
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example).
 *
 * Since: 2.31
 **/
void
gwy_brick_sum_plane(const GwyBrick *brick,
                    GwyDataField *target,
                    gint istart, gint jstart, gint kstart,
                    gint width, gint height, gint depth,
                    gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *bdata, *brow;
    gdouble *ddata, *drow;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    gwy_data_field_clear(target);
    ddata = target->data;

    if (width == -1 && height > 0 && depth > 0) {
        /* Sum in planes along x scan lines.  Here the good memory access strategy is trivial; just accumulate each
         * scan line. Target locations never collide, so parallelisation is safe. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,lev,row,col) \
            shared(bdata,ddata,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble s = 0.0;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                for (col = 0; col < xres; col++)
                    s += brow[col];
                ddata[row + lev*height] = s;
            }
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        /* Sum in planes, but in y direction.  This means for each brick plane we have one ‘active’ row of the output
         * field where we accumulate all the x scan lines in the brick plane. Target locations with different lev do
         * not collide, so outer for-cycle parallelisation is safe. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,lev,row,col) \
            shared(bdata,ddata,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                for (col = 0; col < width; col++)
                    drow[col] += brow[col];
            }
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        /* Sum along z profiles.  This means for each brick plane iterate synchronously through this plane and the
         * output field, and accumulate. Target locations always collide. */
        /* We have many parallelisation options:
         * 1. Switch loop order and make iteration by row the outermost loop. This removes collisions, results in
         *    still mostly linear memory access and can be handled by simple parallel for.
         *    Verdict: Somewhat faster than serial, maybe 10 %.
         *
         * 2. Use generic parallel, query the number of threads and thread id, form blocks explicitly and execute them
         *    (hiding the ugly parts in helper functions in gwyprocessinternal).
         *    Verdict: The fastest, but just barely, about 1/3 faster, requires code modification (explicit split to
         *    blocks).
         *
         * 3. Write it using tasks, except tasks should be larger than a single addition, so we would need explicit
         *    blocks.  We can make the row cycle to produce tasks and declare dependencies between the arrays – this
         *    seems complicated.
         *    Verdict: ??? too complicated to try.
         *
         * 4. Add parallel for pragma to the middle loop instead of the outermost one.  This removes collisions, but
         *    probably splits the work to quite small pieces.
         *    Verdict: The second fastest, about 1/3 faster and trivial to implement.
         *
         * 5. Add taskloop pragma to the middle loop.  Here we can control granularity with grainsize.
         *    Verdict: ??? too complicated to try.
         *
         * 6. Use atomic construct –  atomic update.  Isn't it too expensive for plain summation?
         *    Verdict: Yes, it is very slow.
         *
         * 7. In OpenMP 4.5+ we can use reduction(+:array[:size]) on ddata. However, we must ensure the compiler sees
         *    us accessing ddata, not drow!
         *    Verdict: Slower than serial.
         *
         * 8. Probably more...
         *
         * The winner is variant 4, but the speedup is nothing to write home about.  Memory bandwidth limited? */
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,row,col) \
            shared(bdata,ddata,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                for (col = 0; col < width; col++)
                    drow[col] += brow[col];
            }
        }
    }

    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_z);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_sum_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Sums all z-profiles of a data brick to a data field.
 *
 * The result is an xy plane and can be alternatively imagined as the sum of all xy planes.
 *
 * Since: 2.52
 **/
void
gwy_brick_sum_xy_plane(const GwyBrick *brick,
                        GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_sum_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_min_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the minima plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized.
 * @depth: Pixel depth of summarized plane. If @depth is -1, the xy planes will be summarized
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Finds minima of profiles in certain direction.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example).
 *
 * Since: 2.32
 **/
void
gwy_brick_min_plane(const GwyBrick *brick,
                    GwyDataField *target,
                    gint istart, gint jstart, gint kstart,
                    gint width, gint height, gint depth,
                    gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *bdata, *brow;
    gdouble *ddata, *drow;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = target->data;

    /* See gwy_brick_sum_plane() for explanation. */
    if (width == -1 && height > 0 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,lev,row,col) \
            shared(bdata,ddata,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble m = G_MAXDOUBLE;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                for (col = 0; col < xres; col++) {
                    if (brow[col] < m)
                        m = brow[col];
                }
                ddata[row + lev*height] = m;
            }
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        gwy_data_field_fill(target, G_MAXDOUBLE);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,lev,row,col) \
            shared(bdata,ddata,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] < drow[col])
                        drow[col] = brow[col];
                }
            }
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        gwy_data_field_fill(target, G_MAXDOUBLE);
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,row,col) \
            shared(bdata,ddata,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] < drow[col])
                        drow[col] = brow[col];
                }
            }
        }
    }

    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_z);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_min_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Computes the minima along z-axis of a data brick to a data field.
 *
 * The result is an xy plane.
 *
 * Since: 2.52
 **/
void
gwy_brick_min_xy_plane(const GwyBrick *brick,
                       GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_min_plane(brick, target,
                        0, 0, 0, brick->xres, brick->yres, -1,
                        TRUE);
}

/**
 * gwy_brick_max_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the maxima plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized.
 * @depth: Pixel depth of extracted plane. If @depth is -1, the xy planes will be summarized
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Finds minima of profiles in certain direction.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example).
 *
 * Since: 2.32
 **/
void
gwy_brick_max_plane(const GwyBrick *brick,
                    GwyDataField *target,
                    gint istart, gint jstart, gint kstart,
                    gint width, gint height, gint depth,
                    gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *bdata, *brow;
    gdouble *ddata, *drow;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = target->data;

    /* See gwy_brick_sum_plane() for explanation. */
    if (width == -1 && height > 0 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,lev,row,col) \
            shared(bdata,ddata,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble m = G_MINDOUBLE;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                for (col = 0; col < xres; col++) {
                    if (brow[col] > m)
                        m = brow[col];
                }
                ddata[row + lev*height] = m;
            }
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        gwy_data_field_fill(target, G_MINDOUBLE);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,lev,row,col) \
            shared(bdata,ddata,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] > drow[col])
                        drow[col] = brow[col];
                }
            }
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        gwy_data_field_fill(target, G_MINDOUBLE);
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,row,col) \
            shared(bdata,ddata,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] > drow[col])
                        drow[col] = brow[col];
                }
            }
        }
    }

    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_z);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_max_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Computes the maxima along z-axis of a data brick to a data field.
 *
 * The result is an xy plane.
 *
 * Since: 2.52
 **/
void
gwy_brick_max_xy_plane(const GwyBrick *brick,
                       GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_max_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

static void
convert_positions_to_coordinates(const GwyBrick *brick,
                                 GwyDataField *target,
                                 const gint *pos,
                                 gint width, gint height, gint depth)
{
    GwyDataLine *calibration;
    const gdouble *caldata;
    gdouble *data;
    gdouble q, off;
    gint i, n;

    n = target->xres*target->yres;
    data = target->data;

    if (width == -1 && height > 0 && depth > 0) {
        q = brick->xreal/brick->xres;
        off = brick->xoff + 0.5*q;
        _gwy_copy_si_unit(brick->si_unit_x, &target->si_unit_z);
    }
    else if (width > 0 && height == -1 && depth > 0) {
        q = brick->yreal/brick->yres;
        off = brick->yoff + 0.5*q;
        _gwy_copy_si_unit(brick->si_unit_y, &target->si_unit_z);
    }
    else if (width > 0 && height > 0 && depth == -1) {
        calibration = gwy_brick_get_zcalibration(brick);
        if (calibration) {
            caldata = calibration->data;
            _gwy_copy_si_unit(calibration->si_unit_y, &target->si_unit_z);
            for (i = 0; i < n; i++)
                data[i] = caldata[i];
            return;
        }
        q = brick->zreal/brick->zres;
        off = brick->zoff + 0.5*q;
        _gwy_copy_si_unit(brick->si_unit_z, &target->si_unit_z);
    }
    else {
        g_return_if_reached();
    }

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(data,pos,n,q,off)
#endif
    for (i = 0; i < n; i++)
        data[i] = pos[i]*q + off;
}

/**
 * gwy_brick_minpos_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the minima positions plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized.
 * @depth: Pixel depth of summarized plane. If @depth is -1, the xy planes will be summarized
 * @keep_offsets: Keep the physical offsets in summarized field.
 *
 * Finds minima coordinates in profiles in certain direction.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example)
 *
 * Since: 2.32
 **/
void
gwy_brick_minpos_plane(const GwyBrick *brick,
                       GwyDataField *target,
                       gint istart, gint jstart, gint kstart,
                       gint width, gint height, gint depth,
                       gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *bdata, *brow;
    gdouble *ddata, *drow;
    gint *pos;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = target->data;
    pos = g_new0(gint, target->xres*target->yres);

    /* See gwy_brick_sum_plane() for explanation. */
    if (width == -1 && height > 0 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,lev,row,col) \
            shared(bdata,ddata,pos,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble m = G_MAXDOUBLE;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                for (col = 0; col < xres; col++) {
                    if (brow[col] < m) {
                        m = brow[col];
                        pos[lev*height + row] = col;
                    }
                }
            }
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        gwy_data_field_fill(target, G_MAXDOUBLE);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,lev,row,col) \
            shared(bdata,ddata,pos,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] < drow[col]) {
                        drow[col] = brow[col];
                        pos[lev*width + col] = row;
                    }
                }
            }
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        gwy_data_field_fill(target, G_MAXDOUBLE);
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,row,col) \
            shared(bdata,ddata,pos,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] < drow[col]) {
                        drow[col] = brow[col];
                        pos[row*width + col] = lev;
                    }
                }
            }
        }
    }

    convert_positions_to_coordinates(brick, target, pos, width, height, depth);
    g_free(pos);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_minpos_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Computes the location of minima along z-axis of a data brick to a data field.
 *
 * The result is an xy plane.
 *
 * Since: 2.52
 **/
void
gwy_brick_minpos_xy_plane(const GwyBrick *brick,
                          GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_minpos_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_maxpos_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the maxima positions plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized
 * @depth: Pixel depth of summarized plane. If @depth is -1, the xy planes will be summarized
 * @keep_offsets: Keep the physical offsets in summarized field.  Not implemented.
 *
 * Finds maxima coordinates of profiles in certain direction.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example).
 *
 * Since: 2.32
 **/
void
gwy_brick_maxpos_plane(const GwyBrick *brick,
                       GwyDataField *target,
                       gint istart,
                       gint jstart,
                       gint kstart,
                       gint width,
                       gint height,
                       gint depth,
                       gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *bdata, *brow;
    gdouble *ddata, *drow;
    gint *pos;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = target->data;
    pos = g_new0(gint, target->xres*target->yres);

    /* See gwy_brick_sum_plane() for explanation. */
    if (width == -1 && height > 0 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,lev,row,col) \
            shared(bdata,ddata,pos,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble m = G_MINDOUBLE;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                for (col = 0; col < xres; col++) {
                    if (brow[col] > m) {
                        m = brow[col];
                        pos[lev*height + row] = col;
                    }
                }
            }
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        gwy_data_field_fill(target, G_MINDOUBLE);
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,lev,row,col) \
            shared(bdata,ddata,pos,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] > drow[col]) {
                        drow[col] = brow[col];
                        pos[lev*width + col] = row;
                    }
                }
            }
        }
    }
    else if (width > 0 && height > 0 && depth == -1) {
        gwy_data_field_fill(target, G_MINDOUBLE);
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(brow,drow,row,col) \
            shared(bdata,ddata,pos,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                for (col = 0; col < width; col++) {
                    if (brow[col] > drow[col]) {
                        drow[col] = brow[col];
                        pos[row*width + col] = lev;
                    }
                }
            }
        }
    }

    convert_positions_to_coordinates(brick, target, pos, width, height, depth);
    g_free(pos);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_maxpos_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Computes the location of maxima along z-axis of a data brick to a data field.
 *
 * The result is an xy plane.
 *
 * Since: 2.52
 **/
void
gwy_brick_maxpos_xy_plane(const GwyBrick *brick,
                          GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_maxpos_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_mean_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the mean plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized.
 * @depth: Pixel depth of summarized plane. If @depth is -1, the xy planes will be summarized.
 * @keep_offsets: Keep the physical offsets in summarized field.
 *
 * Finds mean of planes in certain direction.
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation. In contrast to
 * gwy_brick_extract_plane, the appropriate start coordinate (e.g. @istart if @width = -1) is not used for single
 * plane extraction, but the planes are accumulated in whole range (0..xres for given example).
 *
 * Since: 2.32
 **/
void
gwy_brick_mean_plane(const GwyBrick *brick,
                     GwyDataField *target,
                     gint istart, gint jstart, gint kstart,
                     gint width, gint height, gint depth,
                     gboolean keep_offsets)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_FIELD(target));
    gwy_brick_sum_plane(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets);

    if (width == -1 && height > 0 && depth > 0)
        gwy_data_field_multiply(target, 1.0/brick->xres);
    else if (width > 0 && height == -1 && depth > 0)
        gwy_data_field_multiply(target, 1.0/brick->yres);
    else if (width > 0 && height > 0 && depth == -1)
        gwy_data_field_multiply(target, 1.0/brick->zres);
}

/**
 * gwy_brick_mean_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Calculates mean values of all z-profiles of a data brick to a data field.
 *
 * The result is an xy plane and can be alternatively imagined as the average
 * of all xy planes.
 *
 * Since: 2.52
 **/
void
gwy_brick_mean_xy_plane(const GwyBrick *brick,
                        GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_mean_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_rms_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the rms plane. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of summarized plane. If @width is -1, the yz planes will be summarized.
 * @height: Pixel height of summarized plane.  If @height is -1, the xz planes will be summarized.
 * @depth: Pixel depth of summarized plane. If @depth is -1, the xy planes will be summarized.
 * @keep_offsets: Keep the physical offsets in extracted field.
 *
 * Finds rms of planes in certain direction and extract the result (GwyDataField). One value of set (@width, @height,
 * @depth) needs to be -1, determining the plane orientation. In contrast to gwy_brick_extract_plane, the appropriate
 * start coordinate (e.g. @istart if @width = -1) is not used for single plane extraction, but the planes are
 * accumulated in whole range (0..xres for given example)
 *
 * Since: 2.32
 **/
void
gwy_brick_rms_plane(const GwyBrick *brick,
                    GwyDataField *target,
                    gint istart,
                    gint jstart,
                    gint kstart,
                    gint width,
                    gint height,
                    gint depth,
                    gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres, i, n;
    const gdouble *bdata, *brow, *mdata, *mrow;
    gdouble *ddata, *drow;
    GwyDataField *meanfield;
    gdouble q = 1.0;

    if (!gwy_brick_extract_field_common(brick, target, istart, jstart, kstart, width, height, depth, keep_offsets))
        return;

    meanfield = gwy_data_field_new(1, 1, 1.0, 1.0, FALSE);
    gwy_brick_mean_plane(brick, meanfield, istart, jstart, kstart, width, height, depth, keep_offsets);
    gwy_data_field_clear(target);
    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = target->data;
    mdata = meanfield->data;
    n = target->xres*target->yres;

    /* See gwy_brick_sum_plane() for explanation. */
    if (width == -1 && height > 0 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(brow,mrow,lev,row,col) \
        shared(bdata,ddata,mdata,xres,yres,jstart,kstart,height,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < height; row++) {
                gdouble s = 0.0;

                brow = bdata + xres*(row + jstart) + xres*yres*(lev + kstart);
                mrow = mdata + row + lev*height;
                for (col = 0; col < xres; col++) {
                    gdouble v = brow[col] - mrow[col];
                    s += v*v;
                }
                ddata[row + lev*height] = s;
            }
        }
        q = 1.0/xres;
    }
    else if (width > 0 && height == -1 && depth > 0) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(brow,drow,mrow,lev,row,col) \
        shared(bdata,ddata,mdata,xres,yres,istart,kstart,width,depth)
#endif
        for (lev = 0; lev < depth; lev++) {
            for (row = 0; row < yres; row++) {
                brow = bdata + istart + xres*row + xres*yres*(lev + kstart);
                drow = ddata + lev*width;
                mrow = mdata + lev*width;
                for (col = 0; col < width; col++) {
                    gdouble v = brow[col] - mrow[col];
                    drow[col] += v*v;
                }
            }
        }
        q = 1.0/yres;
    }
    else if (width > 0 && height > 0 && depth == -1) {
        for (lev = 0; lev < zres; lev++) {
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(brow,drow,mrow,row,col) \
        shared(bdata,ddata,mdata,xres,yres,istart,jstart,height,width,lev)
#endif
            for (row = 0; row < height; row++) {
                brow = bdata + istart + xres*(row + jstart) + xres*yres*lev;
                drow = ddata + row*width;
                mrow = mdata + row*width;
                for (col = 0; col < width; col++) {
                    gdouble v = brow[col] - mrow[col];
                    drow[col] += v*v;
                }
            }
        }
        q = 1.0/zres;
    }
    g_object_unref(meanfield);

#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(ddata,n,q)
#endif
    for (i = 0; i < n; i++)
        ddata[i] = sqrt(q*ddata[i]);

    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_z);
    gwy_data_field_invalidate(target);
}

/**
 * gwy_brick_rms_xy_plane:
 * @brick: A data brick.
 * @target: Datafield to be filled by the summary data. It will be resized if necessary.
 *
 * Calculates rms values of all z-profiles of a data brick to a data field.
 *
 * The result is an xy plane and can be alternatively imagined as the variation
 * among xy planes.
 *
 * Since: 2.52
 **/
void
gwy_brick_rms_xy_plane(const GwyBrick *brick,
                       GwyDataField *target)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_rms_plane(brick, target, 0, 0, 0, brick->xres, brick->yres, -1, TRUE);
}

/**
 * gwy_brick_extract_line:
 * @brick: A data brick.
 * @target: Dataline to be filled by extracted line. It will be resized if necessary.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @iend: Column where to end, exclusive (pixel coordinates).
 * @jend: Row where to end, exclusive (pixel coordinates).
 * @kend: Level where to end, exclusive (pixel coordinates).
 * @keep_offsets: Keep physical offsets in extracted line.
 *
 * Extracts a line (GwyDataLine) from the brick.
 *
 * Only line orientations parallel to coordinate axes are supported now, i.e. two of the start coordinates need to be
 * same as end ones.
 *
 * Since: 2.31
 **/
void
gwy_brick_extract_line(const GwyBrick *brick, GwyDataLine *target,
                       gint istart, gint jstart, gint kstart,
                       gint iend, gint jend, gint kend,
                       gboolean keep_offsets)
{
    gint col, row, lev, xres, yres, zres;
    gdouble *bdata, *ddata;
    GwySIUnit *si_unit = NULL;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_LINE(target));
    g_return_if_fail(_gwy_brick_check_inside(brick, istart, jstart, kstart));

    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    g_return_if_fail(iend >= 0 && iend <= xres
                     && jend >= 0 && jend <= yres
                     && kend >= 0 && kend <= zres);

    bdata = brick->data;

    if ((jstart == jend) && (kstart == kend)) {
        gwy_data_line_resample(target, ABS(iend - istart), GWY_INTERPOLATION_NONE);
        ddata = gwy_data_line_get_data(target);
        si_unit = brick->si_unit_x;

        row = jstart;
        lev = kstart;
        if (iend >= istart) {
            for (col = 0; col < (iend - istart); col++)
                ddata[col] = bdata[col + istart + xres*row + xres*yres*lev];
        }
        else {
            for (col = 0; col < (istart - iend); col++)
                ddata[col] = bdata[iend - col - 1 + xres*row + xres*yres*lev];
            GWY_SWAP(gint, istart, iend);
        }
        target->off = keep_offsets ? istart*brick->xreal/xres : 0.0;
        target->real = (iend - istart)*brick->xreal/xres;
    }
    else if ((istart == iend) && (kstart == kend)) {
        gwy_data_line_resample(target, ABS(jend - jstart), GWY_INTERPOLATION_NONE);
        ddata = gwy_data_line_get_data(target);
        si_unit = brick->si_unit_y;

        col = istart;
        lev = kstart;
        if (jend >= jstart) {
            for (row = 0; row < (jend - jstart); row++)
                ddata[row] = bdata[col + xres*(row + jstart) + xres*yres*lev];
        }
        else {
            for (row = 0; row < (jstart - jend); row++)
                ddata[row] = bdata[col + xres*(jstart - row - 1) + xres*yres*lev];
            GWY_SWAP(gint, jstart, jend);
        }
        target->off = keep_offsets ? jstart*brick->yreal/yres : 0.0;
        target->real = (jend - jstart)*brick->yreal/yres;
    }
    else if ((istart == iend) && (jstart == jend)) {
        gwy_data_line_resample(target, ABS(kend - kstart),
                               GWY_INTERPOLATION_NONE);
        ddata = gwy_data_line_get_data(target);
        si_unit = brick->si_unit_x;

        col = istart;
        row = jstart;
        if (kend >= kstart) {
            for (lev = 0; lev < (kend - kstart); lev++)
                ddata[lev] = bdata[col + xres*row + xres*yres*(lev + kstart)];
        }
        else {
            for (lev = 0; lev < (kstart - kend); lev++)
                ddata[lev] = bdata[col + xres*row + xres*yres*(kend - lev - 1)];
            GWY_SWAP(gint, kstart, kend);
        }
        target->off = keep_offsets ? kstart*brick->zreal/zres : 0.0;
        target->real = (kend - kstart)*brick->zreal/zres;
    }
    else {
        g_return_if_reached();
    }

    _gwy_copy_si_unit(si_unit, &target->si_unit_x);
    _gwy_copy_si_unit(brick->si_unit_w, &target->si_unit_y);
}

/**
 * gwy_brick_extract_z_line:
 * @brick: A data brick.
 * @target: Dataline to be filled by extracted line. It will be resized if necessary.
 * @i: Column index.
 * @j: Row index.
 *
 * Extracts one full Z profile of a data brick to a data line.
 *
 * Since: 2.52
 **/
void
gwy_brick_extract_z_line(const GwyBrick *brick,
                         GwyDataLine *target,
                         gint i, gint j)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_extract_line(brick, target, i, j, 0, i, j, brick->zres, TRUE);
}

/**
 * gwy_brick_get_zcalibration:
 * @brick: A data brick.
 *
 * Gets the z-axis non-linear calibration of a data brick.
 *
 * Returns: Z Calibration (non-linear Z-axis values as ordinates).
 *
 * Since: 2.32
 **/
GwyDataLine*
gwy_brick_get_zcalibration(const GwyBrick *brick)
{
    GwyBrickPrivate *priv;

    g_return_val_if_fail(GWY_IS_BRICK(brick), NULL);

    priv = (GwyBrickPrivate*)brick->priv;

    return priv->zcalibration;
}

/**
 * gwy_brick_set_zcalibration:
 * @brick: A data brick.
 * @calibration: GwyDataLine pointer with z-axis non-linear calibration of a data brick (values are stored as
 *               ordinates).  It can also be %NULL to unset the calibration.
 *
 * Sets the z-axis non-linear calibration of a data brick.
 *
 * Since: 2.32
 **/
void
gwy_brick_set_zcalibration(GwyBrick *brick, GwyDataLine *calibration)
{
    GwyDataLine *oldcal;
    GwyBrickPrivate *priv;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(!calibration || GWY_IS_DATA_LINE(calibration));

    priv = (GwyBrickPrivate*)brick->priv;
    oldcal = priv->zcalibration;
    if (oldcal == calibration)
        return;

    if (calibration)
        g_object_ref(calibration);

    priv->zcalibration = calibration;
    GWY_OBJECT_UNREF(oldcal);
}

/**
 * gwy_brick_copy_zcalibration:
 * @brick: A data brick.
 * @target: Target data brick.
 *
 * Copies non-linear z-axis calibration between two data bricks.
 *
 * Both bricks must have the same Z resolution.  For a meaningful usage, they should also have the same Z real sizes
 * and units (uncalibrated).
 *
 * If @brick has no z-axis calibration, existing @targets' calibration is
 * deleted.
 *
 * Since: 2.52
 **/
void
gwy_brick_copy_zcalibration(const GwyBrick *brick,
                            GwyBrick *target)
{
    GwyBrickPrivate *priv, *tpriv;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_BRICK(target));
    if (target == brick)
        return;

    g_return_if_fail(target->zres == brick->zres);
    priv = (GwyBrickPrivate*)brick->priv;
    tpriv = (GwyBrickPrivate*)target->priv;
    if (priv->zcalibration && tpriv->zcalibration)
        gwy_data_line_assign(G_OBJECT(tpriv->zcalibration), G_OBJECT(priv->zcalibration));
    else if (priv->zcalibration && !tpriv->zcalibration)
        tpriv->zcalibration = gwy_data_line_duplicate(priv->zcalibration);
    else if (!priv->zcalibration && tpriv->zcalibration)
        gwy_object_unref(tpriv->zcalibration);
}

/* All dimensions are in the destination; source sizes are always with x and
 * y swapped. */
static inline void
copy_2d_block_swap_x_y(const gdouble *src, gdouble *dest,
                       guint xres, G_GNUC_UNUSED guint yres,
                       guint width, guint height)
{
    const gdouble *s;
    gdouble *d;
    guint i, j;

    for (i = 0; i < width; i++) { /* src y */
        s = src + i*yres;
        d = dest + i;
        for (j = height; j; j--, s++, d += xres) /* src x */
            *d = *s;
    }
}

/* All dimensions are in the destination; source sizes are always with x and z swapped. */
static inline void
copy_3d_block_swap_x_z(const gdouble *src, gdouble *dest,
                       guint xres, guint yres, guint zres,
                       guint width, guint height, guint depth)
{
    const gdouble *s;
    gdouble *d;
    guint i, j, k, n;

    n = xres*yres;
    for (i = 0; i < width; i++) { /* src z */
        for (j = 0; j < height; j++) { /* src y */
            s = src + (i*yres + j)*zres;
            d = dest + j*xres + i;
            for (k = depth; k; k--, s++, d += n) /* src x */
                *d = *s;
        }
    }
}

/* Rotates x → y → z → x.  All dimensions are in the destination; source sizes are always with z → y → x → z rotated. */
static inline void
copy_3d_block_swap_yzx(const gdouble *src, gdouble *dest,
                       guint xres, guint yres, guint zres,
                       guint width, guint height, guint depth)
{
    const gdouble *s;
    gdouble *d;
    guint i, j, k;

    for (i = 0; i < width; i++) { /* src z */
        for (j = 0; j < depth; j++) { /* src y */
            s = src + (i*zres + j)*yres;
            d = dest + j*xres*yres + i;
            for (k = height; k; k--, s++, d += xres) /* src x */
                *d = *s;
        }
    }
}

/* Rotates x → z → y → x.  All dimensions are in the destination; source sizes are always with z → x → y → z rotated. */
static inline void
copy_3d_block_swap_zxy(const gdouble *src, gdouble *dest,
                       guint xres, guint yres, guint zres,
                       guint width, guint height, guint depth)
{
    const gdouble *s;
    gdouble *d;
    guint i, j, k, n;

    n = xres*yres;
    for (i = 0; i < height; i++) { /* src z */
        for (j = 0; j < width; j++) { /* src y */
            s = src + (i*xres + j)*zres;
            d = dest + i*xres + j;
            for (k = depth; k; k--, s++, d += n) /* src x */
                *d = *s;
        }
    }
}

/**
 * gwy_brick_transpose:
 * @brick: A data brick.
 * @target: Destination data brick.  It will be resized as needed.
 * @type: Basic transposition type (which axes are swapped).
 * @xflipped: %TRUE to reflect X, i.e. rows within XY planes.
 * @yflipped: %TRUE to reflect Y, i.e. columns within XY planes.
 * @zflipped: %TRUE to reflect Z, i.e. the XY plane order.
 *
 * Transposes a data brick, exchanging and/or flipping axes.
 *
 * Real dimensions and units are updated.  Since real sizes cannot go backward, flipping an axis results in the
 * corresponding offset being reset (the real dimension stays positive).  If the Z axis is preserved its calibration
 * is copied to the target; otherwise the target will have no Z axis calibration.
 *
 * Since: 2.51
 **/
void
gwy_brick_transpose(GwyBrick *brick,
                    GwyBrick *target,
                    GwyBrickTransposeType type,
                    gboolean xflipped,
                    gboolean yflipped,
                    gboolean zflipped)
{
    enum {
        BLOCK_SWAP_N2D = 64,
        BLOCK_SWAP_N3D = 12,
    };

    guint oldxres, oldyres, oldzres, xres, yres, zres;
    gdouble xreal, yreal, zreal, newxreal, newyreal, newzreal;
    gdouble xoff, yoff, zoff, newxoff, newyoff, newzoff;
    GwyDataLine *zcal;
    guint i, j, k, sizex, sizey, sizez;
    guint tids[3];
    const gdouble *bdata;
    GwySIUnit *oldunit[4], *unit[4];
    gdouble *rdata;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_BRICK(target));

    xres = oldxres = brick->xres;
    yres = oldyres = brick->yres;
    zres = oldzres = brick->zres;

    newxreal = xreal = brick->xreal;
    newyreal = yreal = brick->yreal;
    newzreal = zreal = brick->zreal;

    newxoff = xoff = brick->xoff;
    newyoff = yoff = brick->yoff;
    newzoff = zoff = brick->zoff;

    tids[0] = 0;
    tids[1] = 1;
    tids[2] = 2;

    /* First fix the dimensions. */
    if (type == GWY_BRICK_TRANSPOSE_XYZ) {
        /* Identity. */
    }
    else if (type == GWY_BRICK_TRANSPOSE_XZY) {
        /* Y <-> Z */
        GWY_SWAP(guint, yres, zres);
        GWY_SWAP(gdouble, newyreal, newzreal);
        GWY_SWAP(gdouble, newyoff, newzoff);
        tids[1] = 2;
        tids[2] = 1;
    }
    else if (type == GWY_BRICK_TRANSPOSE_YXZ) {
        /* X <-> Y */
        GWY_SWAP(guint, xres, yres);
        GWY_SWAP(gdouble, newxreal, newyreal);
        GWY_SWAP(gdouble, newxoff, newyoff);
        tids[0] = 1;
        tids[1] = 0;
    }
    else if (type == GWY_BRICK_TRANSPOSE_ZYX) {
        /* Z <-> X */
        GWY_SWAP(guint, zres, xres);
        GWY_SWAP(gdouble, newzreal, newxreal);
        GWY_SWAP(gdouble, newzoff, newxoff);
        tids[0] = 2;
        tids[2] = 0;
    }
    else if (type == GWY_BRICK_TRANSPOSE_YZX) {
        /* X -> Y -> Z -> X */
        xres = oldzres;
        yres = oldxres;
        zres = oldyres;
        newxreal = zreal;
        newyreal = xreal;
        newzreal = yreal;
        newxoff = zoff;
        newyoff = xoff;
        newzoff = yoff;
        tids[0] = 1;
        tids[1] = 2;
        tids[2] = 0;
    }
    else if (type == GWY_BRICK_TRANSPOSE_ZXY) {
        /* X -> Z -> Y -> X */
        xres = oldyres;
        yres = oldzres;
        zres = oldxres;
        newxreal = yreal;
        newyreal = zreal;
        newzreal = xreal;
        newxoff = yoff;
        newyoff = zoff;
        newzoff = xoff;
        tids[0] = 2;
        tids[1] = 0;
        tids[2] = 1;
    }
    else {
        g_assert_not_reached();
    }

    bdata = brick->data;
    gwy_brick_resample(target, xres, yres, zres, GWY_INTERPOLATION_NONE);
    target->xreal = xreal;
    target->yreal = yreal;
    target->zreal = zreal;
    rdata = target->data;

    /* There are 48 different combinations, which is way too much.  Implement the transformation in two steps:
     * 1. Reshaping, without regard of axis inversion (6 types, one trivial). */
    if (type == GWY_BRICK_TRANSPOSE_XYZ) {
        /* Identity. */
        gwy_assign(rdata, bdata, xres*yres*zres);
    }
    else if (type == GWY_BRICK_TRANSPOSE_XZY) {
        /* Y <-> Z */
        /* Copy entire rows. */
        for (i = 0; i < zres; i++) {
            for (j = 0; j < yres; j++)
                gwy_assign(rdata, bdata + (j*zres + i)*xres, xres);
        }
    }
    else if (type == GWY_BRICK_TRANSPOSE_YXZ) {
        /* X <-> Y */
        /* Transpose entire XY planes. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(i,j,k,sizex,sizey) \
        shared(bdata,rdata,xres,yres,zres)
#endif
        for (i = 0; i < zres; i++) {
            for (j = 0; j < yres; j += BLOCK_SWAP_N2D) {
                sizey = MIN(BLOCK_SWAP_N2D, yres - j);
                for (k = 0; k < xres; k += BLOCK_SWAP_N2D) {
                    sizex = MIN(BLOCK_SWAP_N2D, xres - k);
                    copy_2d_block_swap_x_y(bdata + (i*xres + k)*yres + j,
                                           rdata + (i*yres + j)*xres + k,
                                           xres, yres, sizex, sizey);
                }
            }
        }
    }
    else if (type == GWY_BRICK_TRANSPOSE_ZYX) {
        /* Z <-> X */
        /* Work by small blocks. This seems to be a good cycle nesting. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(i,j,k,sizex,sizey,sizez) \
        shared(bdata,rdata,xres,yres,zres)
#endif
        for (i = 0; i < zres; i += BLOCK_SWAP_N3D) {
            sizez = MIN(BLOCK_SWAP_N3D, zres - i);
            for (j = 0; j < yres; j += BLOCK_SWAP_N3D) {
                sizey = MIN(BLOCK_SWAP_N3D, yres - j);
                for (k = 0; k < xres; k += BLOCK_SWAP_N3D) {
                    sizex = MIN(BLOCK_SWAP_N3D, xres - k);
                    copy_3d_block_swap_x_z(bdata + (k*yres + j)*zres + i,
                                           rdata + (i*yres + j)*xres + k,
                                           xres, yres, zres,
                                           sizex, sizey, sizez);
                }
            }
        }
    }
    else if (type == GWY_BRICK_TRANSPOSE_YZX) {
        /* X -> Y -> Z -> X */
        /* Work by small blocks. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(i,j,k,sizex,sizey,sizez) \
        shared(bdata,rdata,xres,yres,zres)
#endif
        for (k = 0; k < xres; k += BLOCK_SWAP_N3D) {
            sizex = MIN(BLOCK_SWAP_N3D, xres - k);
            for (i = 0; i < zres; i += BLOCK_SWAP_N3D) {
                sizez = MIN(BLOCK_SWAP_N3D, zres - i);
                for (j = 0; j < yres; j += BLOCK_SWAP_N3D) {
                    sizey = MIN(BLOCK_SWAP_N3D, yres - j);
                    copy_3d_block_swap_yzx(bdata + (k*zres + i)*yres + j,
                                           rdata + (i*yres + j)*xres + k,
                                           xres, yres, zres,
                                           sizex, sizey, sizez);
                }
            }
        }
    }
    else if (type == GWY_BRICK_TRANSPOSE_ZXY) {
        /* X -> Z -> Y -> X */
        /* Work by small blocks. */
#ifdef _OPENMP
#pragma omp parallel for if(gwy_threads_are_enabled()) default(none) \
        private(i,j,k,sizex,sizey,sizez) \
        shared(bdata,rdata,xres,yres,zres)
#endif
        for (j = 0; j < yres; j += BLOCK_SWAP_N3D) {
            sizey = MIN(BLOCK_SWAP_N3D, yres - j);
            for (k = 0; k < xres; k += BLOCK_SWAP_N3D) {
                sizex = MIN(BLOCK_SWAP_N3D, xres - k);
                for (i = 0; i < zres; i += BLOCK_SWAP_N3D) {
                    sizez = MIN(BLOCK_SWAP_N3D, zres - i);
                    copy_3d_block_swap_zxy(bdata + (j*xres + k)*zres + i,
                                           rdata + (i*yres + j)*xres + k,
                                           xres, yres, zres,
                                           sizex, sizey, sizez);
                }
            }
        }
    }
    else {
        g_assert_not_reached();
    }

    /* 2. Reverse along axes (8 types, one trivial). */
    target->xoff = newxoff;
    target->yoff = newyoff;
    target->zoff = newzoff;
    /* The target can only have a calibration if Z axis was preserved. */
    if (tids[2] == 2 && (zcal = gwy_brick_get_zcalibration(brick))) {
        zcal = gwy_data_line_duplicate(zcal);
        gwy_brick_set_zcalibration(target, zcal);
        g_object_unref(zcal);
    }
    /* This resets offsets and flips the calibration as needed. */
    gwy_brick_invert(target, xflipped, yflipped, zflipped, FALSE);

    /* Set units. */
    oldunit[0] = gwy_brick_get_si_unit_x(brick);
    oldunit[1] = gwy_brick_get_si_unit_y(brick);
    oldunit[2] = gwy_brick_get_si_unit_z(brick);
    oldunit[3] = gwy_brick_get_si_unit_w(brick);
    unit[0] = gwy_brick_get_si_unit_x(target);
    unit[1] = gwy_brick_get_si_unit_y(target);
    unit[2] = gwy_brick_get_si_unit_z(target);
    unit[3] = gwy_brick_get_si_unit_w(target);
    gwy_si_unit_assign(unit[tids[0]], oldunit[0]);
    gwy_si_unit_assign(unit[tids[1]], oldunit[1]);
    gwy_si_unit_assign(unit[tids[2]], oldunit[2]);
    gwy_si_unit_assign(unit[3], oldunit[3]);
}

/**
 * gwy_brick_invert:
 * @brick: A data brick.
 * @xflipped: %TRUE to reflect X, i.e. rows within XY planes.
 * @yflipped: %TRUE to reflect Y, i.e. columns within XY planes.
 * @zflipped: %TRUE to reflect Z, i.e. the XY plane order.
 * @wflipped: %TRUE to invert values.
 *
 * Reflects and/or inverts a data brick in place.
 *
 * In the case of value reflection, it's inverted about the mean value.
 *
 * Since real sizes cannot go backward, flipping an axis results in the corresponding offset being reset (the real
 * dimension stays positive).
 *
 * Note that the axis parameter convention is different from the confusing one of gwy_data_field_invert().  Here
 * parameters simply correspond to directions that should be flipped.
 *
 * Since: 2.59
 **/
void
gwy_brick_invert(GwyBrick *brick,
                 gboolean xflipped, gboolean yflipped, gboolean zflipped,
                 gboolean wflipped)
{
    GwyBrickPrivate *priv;
    gint xres, yres, zres, n, i, j, k;
    gdouble *data, *r1, *r2;
    gdouble avg;

    g_return_if_fail(GWY_IS_BRICK(brick));
    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    data = brick->data;
    priv = brick->priv;

    if (!xflipped && !yflipped && !zflipped) {
        /* Do nothing. */
    }
    else if (xflipped && yflipped && zflipped) {
        n = xres*yres*zres;
        r1 = data;
        r2 = data + n-1;
        for (i = n/2; i; i--, r1++, r2--)
            GWY_SWAP(gdouble, *r1, *r2);
    }
    else if (!xflipped && !yflipped && zflipped) {
        n = xres*yres;
        for (i = 0; i < zres/2; i++) {
            r1 = data + i*n;
            r2 = data + (zres-1 - i)*n;
            for (j = n; j; j--, r1++, r2++)
                GWY_SWAP(gdouble, *r1, *r2);
        }
    }
    else if (xflipped && !yflipped && !zflipped) {
        n = yres*zres;
        for (i = 0; i < n; i++) {
            r1 = data + i*xres;
            r2 = r1 + xres-1;
            for (j = xres/2; j; j--, r1++, r2--)
                GWY_SWAP(gdouble, *r1, *r2);
        }
    }
    else if (!xflipped && yflipped && !zflipped) {
        for (i = 0; i < zres; i++) {
            for (j = 0; j < yres/2; j++) {
                r1 = data + (i*yres + j)*xres;
                r2 = data + (i*yres + yres-1 - j)*xres;
                for (k = xres; k; k--, r1++, r2++)
                    GWY_SWAP(gdouble, *r1, *r2);
            }
        }
    }
    else if (xflipped && yflipped && !zflipped) {
        n = xres*yres;
        for (i = 0; i < zres; i++) {
            r1 = data + i*n;
            r2 = r1 + n-1;
            for (j = n/2; j; j--, r1++, r2--)
                GWY_SWAP(gdouble, *r1, *r2);
        }
    }
    else if (xflipped && !yflipped && zflipped) {
        for (i = 0; i < zres/2; i++) {
            for (j = 0; j < yres; j++) {
                r1 = data + (i*yres + j)*xres;
                r2 = data + ((zres-1 - i)*yres + j)*xres + xres-1;
                for (k = xres; k; k--, r1++, r2--)
                    GWY_SWAP(gdouble, *r1, *r2);
            }
        }
    }
    else if (!xflipped && yflipped && zflipped) {
        for (i = 0; i < zres/2; i++) {
            for (j = 0; j < yres; j++) {
                r1 = data + (i*yres + j)*xres;
                r2 = data + ((zres-1 - i)*yres + (yres-1 - j))*xres;
                for (k = xres; k; k--, r1++, r2++)
                    GWY_SWAP(gdouble, *r1, *r2);
            }
        }
    }
    else {
        g_assert_not_reached();
    }

    if (wflipped) {
        avg = data[0];
        n = xres*yres*zres;
        for (i = 1; i < n; i++)
            avg += data[i];
        avg /= n;
        for (i = 0; i < n; i++)
            data[i] = 2.0*avg - data[i];
    }

    brick->xoff = xflipped ? 0.0 : brick->xoff;
    brick->yoff = yflipped ? 0.0 : brick->yoff;
    brick->zoff = zflipped ? 0.0 : brick->zoff;

    if (zflipped && priv->zcalibration)
        gwy_data_line_invert(priv->zcalibration, TRUE, FALSE);
}

/**
 * gwy_brick_set_plane:
 * @brick: A data brick.
 * @plane: Datafield to be inserted into brick. It must have the appropriate size.
 * @istart: Column where to start (pixel coordinates).
 * @jstart: Row where to start (pixel coordinates).
 * @kstart: Level where to start (pixel coordinates).
 * @width: Pixel width of the inserted plane. If @width is -1, the yz plane will be filled.
 * @height: Pixel height of insered plane. If @height is -1, the xz plane will be filled.
 * @depth: Pixel depth of inserted plane. If @depth is -1, the xy plane will be filled.
 *
 * Fill a single plane in the brick by a two-dimensional data (GwyDataField).
 *
 * One value of set (@width, @height, @depth) needs to be -1, determining the plane orientation.
 *
 * Since: 2.51
 **/
void
gwy_brick_set_plane(GwyBrick *brick,
                    GwyDataField *plane,
                    gint istart, gint jstart, gint kstart,
                    gint width, gint height, gint depth)
{
    gint col, row, lev, xres, yres, zres;
    const gdouble *ddata, *d;
    gdouble *bdata, *b;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_FIELD(plane));
    g_return_if_fail((width == -1 && height > 0 && depth > 0)
                     || (width > 0 && height == -1 && depth > 0)
                     || (width > 0 && height > 0 && depth == -1));
    g_return_if_fail(_gwy_brick_check_inside(brick, istart, jstart, kstart));
    xres = brick->xres;
    yres = brick->yres;
    zres = brick->zres;
    bdata = brick->data;
    ddata = plane->data;

    if (width == -1 && height > 0 && depth > 0) {
        g_return_if_fail(plane->xres == height);
        g_return_if_fail(plane->yres == depth);
        g_return_if_fail(jstart + height <= yres);
        g_return_if_fail(kstart + depth <= zres);
        col = istart;
        d = ddata;
        for (lev = 0; lev < depth; lev++) {
            b = bdata + col + xres*jstart + xres*yres*(lev + kstart);
            for (row = 0; row < height; row++, d++, b += xres)
                *b = *d;
        }
    }
    else if (width > 0 && height == -1 && depth > 0) {
        g_return_if_fail(plane->xres == width);
        g_return_if_fail(plane->yres == depth);
        g_return_if_fail(istart + width <= xres);
        g_return_if_fail(kstart + depth <= zres);
        row = jstart;
        for (lev = 0; lev < depth; lev++)
            gwy_assign(bdata + istart + xres*row + xres*yres*(lev + kstart), ddata + lev*width, width);
    }
    else if (width > 0 && height > 0 && depth == -1) {
        g_return_if_fail(plane->xres == width);
        g_return_if_fail(plane->yres == height);
        g_return_if_fail(istart + width <= xres);
        g_return_if_fail(jstart + height <= yres);
        lev = kstart;
        for (row = 0; row < height; row++)
            gwy_assign(bdata + istart + xres*(row + jstart) + xres*yres*(lev), ddata + row*width, width);
    }
    else {
        g_return_if_reached();
    }
}

/**
 * gwy_brick_set_xy_plane:
 * @brick: A data brick.
 * @plane: Datafield to be inserted into brick. It must have dimensions @xres by @yres.
 * @lev: Position in the brick (level index).
 *
 * Sets one full single XY plane in a data brick from data field values.
 *
 * Since: 2.52
 **/
void
gwy_brick_set_xy_plane(GwyBrick *brick,
                       GwyDataField *plane,
                       gint lev)
{
    g_return_if_fail(GWY_IS_BRICK(brick));
    gwy_brick_set_plane(brick, plane, 0, 0, lev, brick->xres, brick->yres, -1);
}

/**
 * gwy_brick_add_to_xy_planes:
 * @brick: A data brick.
 * @plane: Datafield to be added to all brick XY planes.  It must have dimensions @xres by @yres.
 *
 * Adds a data field to all brick XY planes.
 *
 * Since: 2.55
 **/
void
gwy_brick_add_to_xy_planes(GwyBrick *brick,
                           GwyDataField *plane)
{
    gint n, lev;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_FIELD(plane));
    g_return_if_fail(plane->xres == brick->xres);
    g_return_if_fail(plane->yres == brick->yres);

    n = brick->xres * brick->yres;
    for (lev = 0; lev < brick->zres; lev++) {
        gdouble *d = brick->data + n*lev;
        const gdouble *p = plane->data;
        gint k;

        for (k = 0; k < n; k++)
            d[k] += p[k];
    }
}

/**
 * gwy_brick_add_to_z_lines:
 * @brick: A data brick.
 * @line: Data line to add to each Z lines.  It must have dimension @zres.
 *
 * Adds a data line to all brick Z lines.
 *
 * Since: 2.55
 **/
void
gwy_brick_add_to_z_lines(GwyBrick *brick,
                         GwyDataLine *line)
{
    gint n, lev;

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_LINE(line));
    g_return_if_fail(line->res == brick->zres);

    n = brick->xres * brick->yres;
    for (lev = 0; lev < brick->zres; lev++) {
        gdouble *d = brick->data + n*lev;
        gdouble v = line->data[lev];
        gint k;

        for (k = 0; k < n; k++)
            d[k] += v;
    }
}

/************************** Documentation ****************************/

/**
 * SECTION:brick
 * @title: GwyBrick
 * @short_description: Three-dimensional data representation
 *
 * #GwyBrick represents 3D data arrays in Gwyddion. It is typically useful for different volume data obtained from
 * SPMs, like in force volume measurements.
 **/

/**
 * GwyBrick:
 *
 * The #GwyBrick struct contains private data only and should be accessed using the functions below.
 *
 * Since: 2.31
 **/

/**
 * gwy_brick_duplicate:
 * @brick: A data brick to duplicate.
 *
 * Convenience macro doing gwy_serializable_duplicate() with all the necessary typecasting.
 *
 * Since: 2.31
 **/

/**
 * gwy_brick_assign:
 * @dest: Target data brick.
 * @source: Source data brick.
 *
 * Convenience macro making one data brick identical to another.
 *
 * This is just a gwy_serializable_clone() wrapper with all the necessary typecasting.
 *
 * Since: 2.52
 **/

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
