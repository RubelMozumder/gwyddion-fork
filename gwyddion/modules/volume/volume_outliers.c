/*
 *  $Id: volume_outliers.c 25241 2023-02-23 16:00:42Z klapetek $
 *  Copyright (C) 2018 David Necas (Yeti), Petr Klapetek.
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
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libprocess/brick.h>
#include <libprocess/correct.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-volume.h>
#include <app/gwyapp.h>
#include "libgwyddion/gwyomp.h"

#define VOLUME_OUTLIERS_RUN_MODES (GWY_RUN_IMMEDIATE)

static gboolean module_register(void);
static void     volume_outliers(GwyContainer *data,
                                GwyRunType run);
static void     brick_outliers (GwyBrick *brick);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Removes outliers from all XY planes"),
    "Petr Klapetek <klapetek@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2018",
};

GWY_MODULE_QUERY2(module_info, volume_outliers)

static gboolean
module_register(void)
{
    gwy_volume_func_register("volume_outliers",
                             (GwyVolumeFunc)&volume_outliers,
                             N_("/_Correct Data/_XY Plane Outliers"),
                             NULL,
                             VOLUME_OUTLIERS_RUN_MODES,
                             GWY_MENU_FLAG_VOLUME,
                             N_("Remove outliers from XY planes"));

    return TRUE;
}

static void
volume_outliers(GwyContainer *data, GwyRunType run)
{
    GwyBrick *brick = NULL;
    gint id, newid;

    g_return_if_fail(run & VOLUME_OUTLIERS_RUN_MODES);

    gwy_app_data_browser_get_current(GWY_APP_BRICK, &brick,
                                     GWY_APP_BRICK_ID, &id,
                                     0);
    g_return_if_fail(GWY_IS_BRICK(brick));

    brick = gwy_brick_duplicate(brick);
    brick_outliers(brick);
    newid = gwy_app_data_browser_add_brick(brick, NULL, data, TRUE);
    g_object_unref(brick);
    gwy_app_volume_log_add_volume(data, id, newid);
}

static void
brick_outliers(GwyBrick *brick)
{
    gint xres = gwy_brick_get_xres(brick);
    gint yres = gwy_brick_get_yres(brick);
    gint zres = gwy_brick_get_zres(brick);

#ifdef _OPENMP
#pragma omp parallel if(gwy_threads_are_enabled()) default(none) \
            shared(brick,xres,yres,zres)
#endif
    {
        GwyDataField *dfield = gwy_data_field_new(xres, yres, xres, yres,
                                                  FALSE);
        GwyDataField *mfield = gwy_data_field_new_alike(dfield, TRUE);
        gint kfrom = gwy_omp_chunk_start(zres), kto = gwy_omp_chunk_end(zres);
        gint k;

        for (k = kfrom; k < kto; k++) {
            gwy_brick_extract_xy_plane(brick, dfield, k);
            gwy_data_field_mask_outliers(dfield, mfield, 3);
            gwy_data_field_correct_average(dfield, mfield);
            gwy_brick_set_xy_plane(brick, dfield, k);
        }

        g_object_unref(dfield);
        g_object_unref(mfield);
    }
}

/* vim: set cin et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
