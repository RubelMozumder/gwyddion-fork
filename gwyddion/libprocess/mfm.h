/*
 *  $Id: mfm.h 22203 2019-07-09 10:34:42Z klapetek $
 *  Copyright (C) 2016-2018 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_PROCESS_MFM_H__
#define __GWY_PROCESS_MFM_H__

#include <libprocess/datafield.h>
#include <libprocess/gwyprocesstypes.h>

G_BEGIN_DECLS

typedef enum {
    GWY_MFM_PROBE_CHARGE = 0,
    GWY_MFM_PROBE_BAR    = 1,
} GwyMFMProbeType;

typedef enum {
    GWY_MFM_COMPONENT_HX       = 0,
    GWY_MFM_COMPONENT_HY       = 1,
    GWY_MFM_COMPONENT_HZ       = 2,
    GWY_MFM_COMPONENT_DHZ_DZ   = 3,
    GWY_MFM_COMPONENT_D2HZ_DZ2 = 4,
} GwyMFMComponentType;

typedef enum {
    GWY_MFM_GRADIENT_FORCE           = 0,
    GWY_MFM_GRADIENT_MFM             = 1,
    GWY_MFM_GRADIENT_MFM_AREA        = 2,
} GwyMFMGradientType;


void    gwy_data_field_mfm_perpendicular_stray_field (GwyDataField *mfield,
                                                      GwyDataField *out,
                                                      gdouble height,
                                                      gdouble thickness,
                                                      gdouble sigma,
                                                      gboolean walls,
                                                      gdouble wall_delta);

void    gwy_data_field_mfm_perpendicular_stray_field_angle_correction(GwyDataField *field,
                                                      gdouble angle,
                                                      GwyOrientation orientation);

void    gwy_data_field_mfm_perpendicular_medium_force(GwyDataField *hz,
                                                      GwyDataField *fz,
                                                      GwyMFMProbeType type,
                                                      gdouble mtip,
                                                      gdouble bx,
                                                      gdouble by,
                                                      gdouble length);
void    gwy_data_field_mfm_shift_z                   (GwyDataField *dfield,
                                                      GwyDataField *out,
                                                      gdouble zdiff);
gdouble gwy_data_field_mfm_find_shift_z              (GwyDataField *dfield,
                                                      GwyDataField *shifted,
                                                      gdouble zdiffmin,
                                                      gdouble zdiffmax);
void    gwy_data_field_mfm_parallel_medium           (GwyDataField *hfield,
                                                      gdouble height,
                                                      gdouble size_a,
                                                      gdouble size_b,
                                                      gdouble size_c,
                                                      gdouble magnetisation,
                                                      gdouble thickness,
                                                      GwyMFMComponentType component);
void    gwy_data_field_mfm_current_line              (GwyDataField *hfield,
                                                      gdouble height,
                                                      gdouble width,
                                                      gdouble position,
                                                      gdouble current,
                                                      GwyMFMComponentType component);

G_END_DECLS

#endif /* __GWY_PROCESS_MFM__ */

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
