/*
 *  $Id: gwymoduleutils-synth.h 25112 2022-10-19 15:11:05Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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

#ifndef __GWY_MODULEUTILS_SYNTH_H__
#define __GWY_MODULEUTILS_SYNTH_H__

#include <gtk/gtk.h>
#include <libgwyddion/gwycontainer.h>
#include <libprocess/datafield.h>
#include <app/datachooser.h>
#include <app/param-def.h>
#include <app/params.h>
#include <app/param-table.h>

G_BEGIN_DECLS

typedef enum {
    GWY_RESPONSE_SYNTH_TAKE_DIMS = 200,
    GWY_RESPONSE_SYNTH_INIT_Z    = 201,
} GwySynthResponseType;

typedef enum {
    GWY_DIMS_PARAM_XRES           = 0,
    GWY_DIMS_PARAM_YRES           = 1,
    GWY_DIMS_PARAM_SQUARE_IMAGE   = 2,
    GWY_DIMS_PARAM_XREAL          = 3,
    GWY_DIMS_PARAM_YREAL          = 4,
    GWY_DIMS_PARAM_SQUARE_PIXELS  = 5,
    GWY_DIMS_PARAM_XYUNIT         = 6,
    GWY_DIMS_PARAM_ZUNIT          = 7,
    GWY_DIMS_PARAM_REPLACE        = 8,
    GWY_DIMS_PARAM_INITIALIZE     = 9,
    GWY_DIMS_BUTTON_TAKE          = 10,
    GWY_DIMS_HEADER_PIXEL         = 11,
    GWY_DIMS_HEADER_PHYSICAL      = 12,
    GWY_DIMS_HEADER_UNITS         = 13,
    GWY_DIMS_HEADER_CURRENT_IMAGE = 14,
} GwySynthDimsParam;

typedef enum {
    GWY_SYNTH_FIXED_XYUNIT  = (1 << 0),
    GWY_SYNTH_FIXED_ZUNIT   = (1 << 1),
    GWY_SYNTH_FIXED_UNITS   = (GWY_SYNTH_FIXED_XYUNIT | GWY_SYNTH_FIXED_ZUNIT),
    GWY_SYNTH_NO_INITIALIZE = (1 << 2),
} GwySynthDimsFlags;

typedef enum {
    GWY_SYNTH_UPDATE_CANCELLED  = -1,
    GWY_SYNTH_UPDATE_NOTHING    = 0,
    GWY_SYNTH_UPDATE_DO_PREVIEW = 1,
} GwySynthUpdateType;

void               gwy_synth_define_dimensions_params              (GwyParamDef *paramdef,
                                                                    gint first_id);
void               gwy_synth_sanitise_params                       (GwyParams *params,
                                                                    gint first_id,
                                                                    GwyDataField *template_);
void               gwy_synth_append_dimensions_to_param_table      (GwyParamTable *partable,
                                                                    GwySynthDimsFlags flags);
void               gwy_synth_use_dimensions_template               (GwyParamTable *partable);
void               gwy_synth_update_value_unitstrs                 (GwyParamTable *partable,
                                                                    const gint *ids,
                                                                    guint nids);
void               gwy_synth_update_lateral_alts                   (GwyParamTable *partable,
                                                                    const gint *ids,
                                                                    guint nids);
void               gwy_synth_update_like_current_button_sensitivity(GwyParamTable *partable,
                                                                    gint id);
gboolean           gwy_synth_handle_param_changed                  (GwyParamTable *partable,
                                                                    gint id);
GwyAppDataId       gwy_synth_add_result_to_file                    (GwyDataField *result,
                                                                    GwyContainer *data,
                                                                    gint id,
                                                                    GwyParams *params);
GwyDataField*      gwy_synth_make_result_data_field                (GwyDataField *data_field,
                                                                    GwyParams *params,
                                                                    gboolean always_use_template);
GwyDataField*      gwy_synth_make_preview_data_field               (GwyDataField *data_field,
                                                                    gint size);
GwySynthUpdateType gwy_synth_update_progress                       (GTimer *timer,
                                                                    gdouble preview_time,
                                                                    gulong i,
                                                                    gulong niters);

G_END_DECLS

#endif /* __GWY_MODULEUTILS_SYNTH_H__ */

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
