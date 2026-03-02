/*
 *  $Id: maskops.c 25259 2023-03-06 11:19:12Z yeti-dn $
 *  Copyright (C) 2003-2021 David Necas (Yeti), Petr Klapetek.
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
#include <libprocess/filters.h>
#include <libprocess/grains.h>
#include <libgwydgets/gwystock.h>
#include <libgwymodule/gwymodule-process.h>
#include <app/gwymoduleutils.h>
#include <app/gwyapp.h>

#define RUN_MODES GWY_RUN_IMMEDIATE

static gboolean module_register      (void);
static void     mask_remove          (GwyContainer *data,
                                      GwyRunType runtype);
static void     mask_invert          (GwyContainer *data,
                                      GwyRunType runtype);
static void     mask_extract         (GwyContainer *data,
                                      GwyRunType runtype);
static void     extract_grain_numbers(GwyContainer *data,
                                      GwyRunType runtype);
static void     remove_touching      (GwyContainer *data,
                                      GwyRunType runtype);
static void     mask_thin            (GwyContainer *data,
                                      GwyRunType runtype);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Basic operations with mask: inversion, removal, extraction."),
    "Yeti <yeti@gwyddion.net>",
    "1.7",
    "David Nečas (Yeti) & Petr Klapetek",
    "2004",
};

GWY_MODULE_QUERY2(module_info, maskops)

static gboolean
module_register(void)
{
    gwy_process_func_register("mask_remove",
                              (GwyProcessFunc)&mask_remove,
                              N_("/_Mask/_Remove Mask"),
                              GWY_STOCK_MASK_REMOVE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Remove mask from data"));
    gwy_process_func_register("mask_invert",
                              (GwyProcessFunc)&mask_invert,
                              N_("/_Mask/_Invert Mask"),
                              GWY_STOCK_MASK_INVERT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Invert mask"));
    gwy_process_func_register("mask_extract",
                              (GwyProcessFunc)&mask_extract,
                              N_("/_Mask/_Extract Mask"),
                              GWY_STOCK_MASK_EXTRACT,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Extract mask to a new image"));
    gwy_process_func_register("grain_extract_numbers",
                              (GwyProcessFunc)&extract_grain_numbers,
                              N_("/_Grains/_Extract Numbers"),
                              NULL,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Extract grain numbers to a new image"));
    gwy_process_func_register("grain_rem_touching",
                              (GwyProcessFunc)&remove_touching,
                              N_("/_Grains/_Remove Edge-Touching"),
                              GWY_STOCK_GRAINS_EDGE_REMOVE,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA | GWY_MENU_FLAG_DATA_MASK,
                              N_("Remove grains touching image edges"));
    gwy_process_func_register("mask_thin",
                              (GwyProcessFunc)&mask_thin,
                              N_("/_Mask/Thi_n"),
                              GWY_STOCK_MASK_THIN,
                              RUN_MODES,
                              GWY_MENU_FLAG_DATA_MASK | GWY_MENU_FLAG_DATA,
                              N_("Thin mask"));

    return TRUE;
}

static void
mask_invert(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *mfield;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mfield && mquark);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    gwy_data_field_grains_invert(mfield);
    gwy_data_field_data_changed(mfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
mask_remove(GwyContainer *data, GwyRunType runtype)
{
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mquark);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    gwy_container_remove(data, mquark);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
mask_extract(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *mfield;
    gint id, newid;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mfield);

    mfield = gwy_data_field_duplicate(mfield);
    gwy_data_field_clamp(mfield, 0.0, 1.0);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mfield), NULL);

    newid = gwy_app_data_browser_add_data_field(mfield, data, TRUE);
    g_object_unref(mfield);
    gwy_app_set_data_field_title(data, newid, _("Mask"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_channel_log_add_proc(data, id, newid);
}

static void
extract_grain_numbers(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *mfield;
    gint *grains;
    gint id, newid, n, i;
    gdouble *d;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mfield);

    n = gwy_data_field_get_xres(mfield)*gwy_data_field_get_yres(mfield);
    grains = g_new0(gint, n);
    gwy_data_field_number_grains(mfield, grains);
    mfield = gwy_data_field_duplicate(mfield);
    d = gwy_data_field_get_data(mfield);
    for (i = 0; i < n; i++)
        d[i] = grains[i];
    g_free(grains);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mfield), NULL);

    newid = gwy_app_data_browser_add_data_field(mfield, data, TRUE);
    g_object_unref(mfield);
    gwy_app_set_data_field_title(data, newid, _("Grain Numbers"));
    gwy_app_sync_data_items(data, data, id, newid, FALSE,
                            GWY_DATA_ITEM_GRADIENT,
                            GWY_DATA_ITEM_REAL_SQUARE,
                            0);
    gwy_app_channel_log_add_proc(data, id, newid);
}

static void
remove_touching(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *mfield;
    GQuark mquark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mfield,
                                     GWY_APP_MASK_FIELD_KEY, &mquark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mfield);

    gwy_app_undo_qcheckpointv(data, 1, &mquark);
    gwy_data_field_grains_remove_touching_border(mfield);
    gwy_data_field_data_changed(mfield);
    gwy_app_channel_log_add_proc(data, id, id);
}

static void
mask_thin(GwyContainer *data, GwyRunType runtype)
{
    GwyDataField *mask;
    GQuark quark;
    gint id;

    g_return_if_fail(runtype & RUN_MODES);
    gwy_app_data_browser_get_current(GWY_APP_MASK_FIELD, &mask,
                                     GWY_APP_MASK_FIELD_KEY, &quark,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    g_return_if_fail(mask);

    gwy_app_undo_qcheckpointv(data, 1, &quark);
    gwy_data_field_grains_thin(mask);
    gwy_data_field_data_changed(mask);
    gwy_app_channel_log_add_proc(data, id, id);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
