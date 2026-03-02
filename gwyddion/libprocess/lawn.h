/*
 *  $Id: lawn.h 24510 2021-11-12 10:22:13Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti), Petr Klapetek, Radek Slesinger
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

#ifndef __GWY_LAWN_H__
#define __GWY_LAWN_H__

#include <libgwyddion/gwysiunit.h>
#include <libprocess/gwyprocessenums.h>
#include <libprocess/datafield.h>
#include <libprocess/dataline.h>

G_BEGIN_DECLS

#define GWY_TYPE_LAWN            (gwy_lawn_get_type())
#define GWY_LAWN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_LAWN, GwyLawn))
#define GWY_LAWN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_LAWN, GwyLawnClass))
#define GWY_IS_LAWN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_LAWN))
#define GWY_IS_LAWN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_LAWN))
#define GWY_LAWN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_LAWN, GwyLawnClass))

typedef struct _GwyLawn      GwyLawn;
typedef struct _GwyLawnClass GwyLawnClass;


struct _GwyLawn {
    GObject parent_instance;

    gint xres;
    gint yres;

    gdouble xreal;
    gdouble yreal;

    gdouble xoff;
    gdouble yoff;

    GwySIUnit *si_unit_xy;

    gpointer priv;
};

struct _GwyLawnClass {
    GObjectClass parent_class;

    void (*data_changed)(GwyLawn *lawn);
    /*< private >*/
    void (*reserved1)(void);
};

typedef gdouble (*GwyCurveReduceFunction)(gint ncurves, gint curvelength, const gdouble *curvedata, gpointer userdata);

#define gwy_lawn_duplicate(lawn) \
        (GWY_LAWN(gwy_serializable_duplicate(G_OBJECT(lawn))))
#define gwy_lawn_assign(dest, source) \
        gwy_serializable_clone_with_type(G_OBJECT(source), \
                                         G_OBJECT(dest), \
                                         GWY_TYPE_LAWN)


GType             gwy_lawn_get_type              (void)                       G_GNUC_CONST;
GwyLawn*          gwy_lawn_new                   (gint xres,
                                                  gint yres,
                                                  gdouble xreal,
                                                  gdouble yreal,
                                                  gint ncurves,
                                                  gint nsegments);
GwyLawn*          gwy_lawn_new_alike             (GwyLawn *model);
GwyLawn*          gwy_lawn_new_part              (GwyLawn *lawn,
                                                  gint xpos,
                                                  gint ypos,
                                                  gint xres,
                                                  gint yres,
                                                  gboolean keep_offsets);
void              gwy_lawn_data_changed          (GwyLawn *lawn);
/* TODO: gwy_lawn_copy */
void              gwy_lawn_copy                  (GwyLawn *src,
                                                  GwyLawn *dest,
                                                  gboolean nondata_too);
gint              gwy_lawn_get_xres              (GwyLawn *lawn);
gint              gwy_lawn_get_yres              (GwyLawn *lawn);
gdouble           gwy_lawn_get_xreal             (GwyLawn *lawn);
gdouble           gwy_lawn_get_yreal             (GwyLawn *lawn);
gdouble           gwy_lawn_get_xoffset           (GwyLawn *lawn);
gdouble           gwy_lawn_get_yoffset           (GwyLawn *lawn);
gint              gwy_lawn_get_n_curves          (GwyLawn *lawn);
void              gwy_lawn_set_xreal             (GwyLawn *lawn,
                                                  gdouble xreal);
void              gwy_lawn_set_yreal             (GwyLawn *lawn,
                                                  gdouble yreal);
void              gwy_lawn_set_xoffset           (GwyLawn *lawn,
                                                  gdouble xoffset);
void              gwy_lawn_set_yoffset           (GwyLawn *lawn,
                                                  gdouble yoffset);
gdouble           gwy_lawn_get_dx                (GwyLawn *lawn);
gdouble           gwy_lawn_get_dy                (GwyLawn *lawn);
GwySIUnit*        gwy_lawn_get_si_unit_xy        (GwyLawn *lawn);
GwySIUnit*        gwy_lawn_get_si_unit_curve     (GwyLawn *lawn,
                                                  gint n);
void              gwy_lawn_set_si_unit_xy        (GwyLawn *lawn,
                                                  GwySIUnit *si_unit);
void              gwy_lawn_set_si_unit_curve     (GwyLawn *lawn,
                                                  gint n,
                                                  GwySIUnit *si_unit);
void              gwy_lawn_copy_units            (GwyLawn *lawn,
                                                  GwyLawn *target);
void              gwy_lawn_set_curve_label       (GwyLawn *lawn,
                                                  gint n,
                                                  const gchar *label);
const gchar*      gwy_lawn_get_curve_label       (GwyLawn *lawn,
                                                  gint n);
GwySIValueFormat* gwy_lawn_get_value_format_xy   (GwyLawn *lawn,
                                                  GwySIUnitFormatStyle style,
                                                  GwySIValueFormat *format);
GwySIValueFormat* gwy_lawn_get_value_format_curve(GwyLawn *lawn,
                                                  gint n,
                                                  GwySIUnitFormatStyle style,
                                                  GwySIValueFormat *format);
gint              gwy_lawn_get_curve_length      (GwyLawn *lawn,
                                                  gint col,
                                                  gint row);
gdouble*          gwy_lawn_get_curve_data        (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint n,
                                                  gint *curvelength);
const gdouble*    gwy_lawn_get_curve_data_const  (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint n,
                                                  gint *curvelength);
void              gwy_lawn_set_curve_data        (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint n,
                                                  const gdouble *curvedata);
const gdouble*    gwy_lawn_get_curves_data_const (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint *curvelength);
void              gwy_lawn_set_curves            (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint curvelength,
                                                  const gdouble* curvesdata,
                                                  const gint *segments);
gint              gwy_lawn_get_n_segments        (GwyLawn *lawn);
const gint*       gwy_lawn_get_segments          (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  gint *nsegments);
void              gwy_lawn_set_segments          (GwyLawn *lawn,
                                                  gint nsegments,
                                                  const gint *segments);
const gchar*      gwy_lawn_get_segment_label     (GwyLawn *lawn,
                                                  gint segment);
void              gwy_lawn_set_segment_label     (GwyLawn *lawn,
                                                  gint segment,
                                                  const gchar *label);
void              gwy_lawn_curve_set_segments    (GwyLawn *lawn,
                                                  gint col,
                                                  gint row,
                                                  const gint *segments);
void              gwy_lawn_clear                 (GwyLawn *lawn);
void              gwy_lawn_area_reduce_to_plane  (GwyLawn *lawn,
                                                  GwyDataField *target,
                                                  GwyCurveReduceFunction func,
                                                  gpointer user_data,
                                                  gint col,
                                                  gint row,
                                                  gint width,
                                                  gint height,
                                                  gboolean keep_offsets);
void              gwy_lawn_reduce_to_plane       (GwyLawn *lawn,
                                                  GwyDataField *target,
                                                  GwyCurveReduceFunction func,
                                                  gpointer user_data);
GwyLawn*          gwy_lawn_new_rotated_90        (GwyLawn *lawn,
                                                  gboolean clockwise);
void              gwy_lawn_invert                (GwyLawn *lawn,
                                                  gboolean xflipped,
                                                  gboolean yflipped);

G_END_DECLS

#endif /* __GWY_LAWN_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
