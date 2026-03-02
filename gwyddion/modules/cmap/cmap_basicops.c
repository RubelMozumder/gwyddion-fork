/*
 *  $Id: cmap_basicops.c 24272 2021-10-07 09:42:24Z yeti-dn $
 *  Copyright (C) 2021 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-cmap.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register           (void);
static void     flip_horizontally         (GwyContainer *data,
                                           GwyRunType runtype);
static void     flip_vertically           (GwyContainer *data,
                                           GwyRunType runtype);
#if 0
static void     flip_diagonally           (GwyContainer *data,
                                           GwyRunType runtype);
#endif
static void     rotate_clockwise_90       (GwyContainer *data,
                                           GwyRunType runtype);
static void     rotate_counterclockwise_90(GwyContainer *data,
                                           GwyRunType runtype);
static void     rotate_180                (GwyContainer *data,
                                           GwyRunType runtype);
static void     null_offsets              (GwyContainer *data,
                                           GwyRunType runtype);
static void     remove_segments           (GwyContainer *data,
                                           GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Basic operations like flipping and rotation by multiples of 90 degrees."),
    "Yeti <yeti@gwyddion.net>",
    "1.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2021",
};

GWY_MODULE_QUERY2(module_info, cmap_basicops)

static gboolean
module_register(void)
{
    gwy_curve_map_func_register("cmap_flip_horizontally",
                                (GwyCurveMapFunc)&flip_horizontally,
                                N_("/_Basic Operations/Flip _Horizontally"),
                                GWY_STOCK_FLIP_HORIZONTALLY,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Flip data horizontally"));
    gwy_curve_map_func_register("cmap_flip_vertically",
                                (GwyCurveMapFunc)&flip_vertically,
                                N_("/_Basic Operations/Flip _Vertically"),
                                GWY_STOCK_FLIP_VERTICALLY,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Flip data vertically"));
    gwy_curve_map_func_register("cmap_rotate_180",
                                (GwyCurveMapFunc)&rotate_180,
                                N_("/_Basic Operations/Flip _Both"),
                                GWY_STOCK_ROTATE_180,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Flip data both horizontally and vertically"));
#if 0
    gwy_curve_map_func_register("cmap_flip_diagonally",
                                (GwyCurveMapFunc)&flip_diagonally,
                                N_("/_Basic Operations/Flip Dia_gonally"),
                                GWY_STOCK_FLIP_DIAGONALLY,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Flip data diagonally"));
#endif
    gwy_curve_map_func_register("cmap_rotate_90_cw",
                                (GwyCurveMapFunc)&rotate_clockwise_90,
                                N_("/_Basic Operations/Rotate C_lockwise"),
                                GWY_STOCK_ROTATE_90_CW,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Rotate data 90 degrees clockwise"));
    gwy_curve_map_func_register("cmap_rotate_90_ccw",
                                (GwyCurveMapFunc)&rotate_counterclockwise_90,
                                N_("/_Basic Operations/Rotate _Counterclockwise"),
                                GWY_STOCK_ROTATE_90_CCW,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Rotate data 90 degrees counterclockwise"));
    gwy_curve_map_func_register("cmap_null_offsets",
                                (GwyCurveMapFunc)&null_offsets,
                                N_("/_Basic Operations/_Null Offsets"),
                                GWY_STOCK_NULL_OFFSETS,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Null horizontal offsets, moving the origin to the upper left corner"));
    gwy_curve_map_func_register("cmap_remove_segments",
                                (GwyCurveMapFunc)&remove_segments,
                                N_("/_Basic Operations/Remove _Segments"),
                                NULL,
                                RUN_MODES,
                                GWY_MENU_FLAG_CURVE_MAP,
                                N_("Remove curve segmentation"));

    return TRUE;
}

static inline void
basicops_common(GwyContainer *data, GwyLawn **lawn, GwyDataField **field, GQuark *quarks, gint *id)
{
    gwy_app_data_browser_get_current(GWY_APP_LAWN, lawn,
                                     GWY_APP_LAWN_KEY, quarks + 0,
                                     GWY_APP_LAWN_ID, id,
                                     0);
    quarks[1] = gwy_app_get_lawn_preview_key_for_id(*id);
    *field = gwy_container_get_object(data, quarks[1]);
    gwy_app_undo_qcheckpointv(data, 2, quarks);
}

static void
flip_horizontally(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    gwy_lawn_invert(lawn, TRUE, FALSE);
    gwy_data_field_invert(preview, FALSE, TRUE, FALSE);
    gwy_data_field_data_changed(preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
}

static void
flip_vertically(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    gwy_lawn_invert(lawn, FALSE, TRUE);
    gwy_data_field_invert(preview, TRUE, FALSE, FALSE);
    gwy_data_field_data_changed(preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
}

static void
rotate_180(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    gwy_lawn_invert(lawn, TRUE, TRUE);
    gwy_data_field_invert(preview, TRUE, TRUE, FALSE);
    gwy_data_field_data_changed(preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
}

#if 0
static void
flip_diagonally(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *dfields[3], *newfield;
    GQuark quarks[3];
    gint id, n, i;

    g_return_if_fail(runtype & RUN_MODES);
    n = get_fields_and_quarks(dfields, quarks, &id);
    gwy_app_undo_qcheckpointv(data, n, quarks);
    for (i = 0; i < n; i++) {
        newfield = gwy_data_field_new_alike(dfields[i], FALSE);
        gwy_data_field_flip_xy(dfields[i], newfield, FALSE);
        gwy_container_set_object(data, quarks[i], newfield);
        g_object_unref(newfield);
    }
    gwy_app_data_clear_selections(data, id);
    gwy_app_channel_log_add_proc(data, id, id);
}
#endif

static void
rotate_clockwise_90(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    lawn = gwy_lawn_new_rotated_90(lawn, TRUE);
    preview = gwy_data_field_new_rotated_90(preview, TRUE);
    gwy_container_set_object(data, quarks[0], lawn);
    gwy_container_set_object(data, quarks[1], preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
    g_object_unref(lawn);
    g_object_unref(preview);
}

static void
rotate_counterclockwise_90(GwyContainer *data, GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    lawn = gwy_lawn_new_rotated_90(lawn, FALSE);
    preview = gwy_data_field_new_rotated_90(preview, FALSE);
    gwy_container_set_object(data, quarks[0], lawn);
    gwy_container_set_object(data, quarks[1], preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
    g_object_unref(lawn);
    g_object_unref(preview);
}

static void
null_offsets(GwyContainer *data,
             GwyRunType runtype)
{
    GwyLawn *lawn;
    GwyDataField *preview;
    GQuark quarks[2];
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    basicops_common(data, &lawn, &preview, quarks, &id);
    gwy_lawn_set_xoffset(lawn, 0.0);
    gwy_lawn_set_yoffset(lawn, 0.0);
    gwy_data_field_set_xoffset(preview, 0.0);
    gwy_data_field_set_yoffset(preview, 0.0);
    gwy_data_field_data_changed(preview);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
}

static void
remove_segments(GwyContainer *data,
                GwyRunType runtype)
{
    GwyLawn *lawn;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_LAWN, &lawn,
                                     GWY_APP_LAWN_ID, &id,
                                     0);
    g_return_if_fail(lawn);
    // FIXME: This saves the entire data to undo.
    //gwy_app_undo_qcheckpointv(data, 2, quarks);
    gwy_lawn_set_segments(lawn, 0, NULL);
    gwy_lawn_data_changed(lawn);
    gwy_app_curve_map_log_add_curve_map(data, id, id);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
