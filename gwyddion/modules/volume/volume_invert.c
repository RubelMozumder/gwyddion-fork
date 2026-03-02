/*
 *  $Id: volume_invert.c 25847 2023-10-12 19:41:06Z klapetek $
 *  Copyright (C) 2013 David Necas (Yeti), Petr Klapetek.
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

#include "config.h"
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>

#define VOLUME_INVERT_RUN_MODES (GWY_RUN_IMMEDIATE)

static gboolean module_register(void);
static void     volume_invert  (GwyContainer *data,
                                GwyRunType run);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Inverts value in volume data"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2013",
};

GWY_MODULE_QUERY2(module_info, volume_invert)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_invert",
                             (GwyVolumeFunc)&volume_invert,
                             N_("/_Basic Operations/_Invert Value"),
                             GWY_STOCK_VOLUME_INVERT,
                             VOLUME_INVERT_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Invert value in volume data"));

    return TRUE;
}

static void
volume_invert(GwyContainer *data, GwyRunType run)
{
    GwyBrick *brick = NULL;
    GwyDataField *dfield = NULL;
    GQuark quark;
    gint id, newid;

    g_return_if_fail(run & VOLUME_INVERT_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);

    quark = gwy_app_get_brick_preview_key_for_id(id);
    dfield = gwy_data_field_duplicate(gwy_container_get_object(data, quark));
    brick = gwy_brick_duplicate(brick);

    g_return_if_fail(GWY_IS_BRICK(brick));
    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));

    gwy_data_field_invert(dfield, FALSE, FALSE, TRUE);
    gwy_brick_multiply(brick, -1.0);

    newid = gwy_app_data_browser_add_brick(brick, dfield, data, TRUE);
    gwy_app_set_brick_title(data, newid, _("Inverted"));
    gwy_app_sync_volume_items(data, data, id, newid, FALSE,
                              GWY_DATA_ITEM_GRADIENT,
                              0);

    g_object_unref(brick);
    g_object_unref(dfield);

    gwy_app_volume_log_add_volume(data, id, newid);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
