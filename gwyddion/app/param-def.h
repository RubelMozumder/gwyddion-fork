/*
 *  $Id: param-def.h 25448 2023-06-16 11:13:59Z yeti-dn $
 *  Copyright (C) 2021-2023 David Necas (Yeti).
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

#ifndef __GWY_PARAM_DEF_H__
#define __GWY_PARAM_DEF_H__

#include <glib-object.h>
#include <libgwyddion/gwyenum.h>
#include <libprocess/gwyprocessenums.h>
#include <app/gwyresultsexport.h>

G_BEGIN_DECLS

typedef enum {
    GWY_PARAM_STRING_EMPTY_IS_NULL = (1 << 0),
    GWY_PARAM_STRING_NULL_IS_EMPTY = (1 << 1),
    GWY_PARAM_STRING_DO_NOT_STRIP  = (1 << 2),
} GwyParamStringFlags;

typedef gchar* (*GwyRectifyStringFunc)(const gchar *s);

#define GWY_TYPE_PARAM_DEF            (gwy_param_def_get_type())
#define GWY_PARAM_DEF(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GWY_TYPE_PARAM_DEF, GwyParamDef))
#define GWY_PARAM_DEF_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GWY_TYPE_PARAM_DEF, GwyParamDefClass))
#define GWY_IS_PARAM_DEF(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GWY_TYPE_PARAM_DEF))
#define GWY_IS_PARAM_DEF_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GWY_TYPE_PARAM_DEF))
#define GWY_PARAM_DEF_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GWY_TYPE_PARAM_DEF, GwyParamDefClass))

typedef struct _GwyParamDef      GwyParamDef;
typedef struct _GwyParamDefClass GwyParamDefClass;

struct _GwyParamDef {
    GObject g_object;
    struct _GwyParamDefPrivate *priv;
};

struct _GwyParamDefClass {
    GObjectClass g_object_class;
};

GType        gwy_param_def_get_type              (void)                                G_GNUC_CONST;
GwyParamDef* gwy_param_def_new                   (void);
void         gwy_param_def_set_function_name     (GwyParamDef *pardef,
                                                  const gchar *name);
const gchar* gwy_param_def_get_function_name     (GwyParamDef *pardef)                 G_GNUC_PURE;
const gchar* gwy_param_def_get_param_name        (GwyParamDef *pardef,
                                                  gint id)                             G_GNUC_PURE;
const gchar* gwy_param_def_get_param_label       (GwyParamDef *pardef,
                                                  gint id)                             G_GNUC_PURE;
void         gwy_param_def_add_gwyenum           (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  const GwyEnum *values,
                                                  gint nvalues,
                                                  gint default_value);
void         gwy_param_def_add_gwyflags          (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  const GwyEnum *values,
                                                  gint nvalues,
                                                  guint default_value);
void         gwy_param_def_add_enum              (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GType enum_gtype,
                                                  gint default_value);
void         gwy_param_def_add_int               (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gint minimum,
                                                  gint maximum,
                                                  gint default_value);
void         gwy_param_def_add_graph_curve       (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_lawn_curve        (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_lawn_segment      (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_active_page       (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_boolean           (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gboolean default_value);
void         gwy_param_def_add_instant_updates   (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gboolean default_value);
void         gwy_param_def_add_randomize         (GwyParamDef *pardef,
                                                  gint id,
                                                  gint seed_id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gboolean default_value);
void         gwy_param_def_add_double            (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gdouble minimum,
                                                  gdouble maximum,
                                                  gdouble default_value);
void         gwy_param_def_add_angle             (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gboolean positive,
                                                  gint folding,
                                                  gdouble default_value);
void         gwy_param_def_add_percentage        (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gdouble default_value);
void         gwy_param_def_add_mask_color        (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_color             (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  gboolean has_alpha,
                                                  GwyRGBA default_value);
void         gwy_param_def_add_target_graph      (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_graph_id          (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_image_id          (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_volume_id         (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_xyz_id            (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_curve_map_id      (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_report_type       (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GwyResultsExportStyle style,
                                                  GwyResultsReportType default_value);
void         gwy_param_def_add_hold_selection    (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_seed              (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc);
void         gwy_param_def_add_string            (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GwyParamStringFlags flags,
                                                  GwyRectifyStringFunc rectify,
                                                  const gchar *default_value);
void         gwy_param_def_add_unit              (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  const gchar *default_value);
void         gwy_param_def_add_resource          (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  GwyInventory *inventory,
                                                  const gchar *default_value);
void         gwy_param_def_add_grain_groups      (GwyParamDef *pardef,
                                                  gint id,
                                                  const gchar *name,
                                                  const gchar *desc,
                                                  guint default_value);

G_END_DECLS

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
