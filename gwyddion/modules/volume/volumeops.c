/*
 *  $Id: volumeops.c 25241 2023-02-23 16:00:42Z klapetek $
 *  Copyright (C) 2003 David Necas (Yeti), Petr Klapetek.
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

#define VOLUMEOPS_RUN_MODES (GWY_RUN_IMMEDIATE)

static gboolean module_register(void);
static void     extract_preview(GwyContainer *data,
                                GwyRunType run);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Inverts value in volume data"),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti)",
    "2017",
};

GWY_MODULE_QUERY2(module_info, volumeops)

static gboolean
module_register(void)
{
    gwy_volume_func_register("extract_preview",
                             (GwyVolumeFunc)&extract_preview,
                             N_("/_Basic Operations/Extract _Preview"),
                             NULL,
                             VOLUMEOPS_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Extract volume data preview to an image"));

    return TRUE;
}

static void
extract_preview(GwyContainer *data, GwyRunType run)
{
    GwyDataField *dfield = NULL;
    GQuark quark;
    gint id, newid;
    gchar *title;

    g_return_if_fail(run & VOLUMEOPS_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK_ID, &id, 0);

    quark = gwy_app_get_brick_preview_key_for_id(id);
    dfield = gwy_data_field_duplicate(gwy_container_get_object(data, quark));
    title = gwy_app_get_brick_title(data, id);

    g_return_if_fail(GWY_IS_DATA_FIELD(dfield));

    newid = gwy_app_data_browser_add_data_field(dfield, data, TRUE);
    g_object_unref(dfield);

    quark = gwy_app_get_data_title_key_for_id(newid);
    gwy_container_set_string(data, quark, (guchar*)title);

    gwy_app_channel_log_add(data, -1, newid, "volume::extract_preview", NULL);
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
