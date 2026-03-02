/*
 *  $Id: gwyhdf5.h 26224 2024-02-29 15:59:26Z yeti-dn $
 *  Copyright (C) 2020-2024 David Necas (Yeti), Petr Klapetek.
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

#ifndef __GWY_HDF5__
#define __GWY_HDF5__

#define H5_USE_18_API

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <hdf5.h>
#include <hdf5_hl.h>
#include <libgwyddion/gwymacros.h>
#include <libgwymodule/gwymodule-file.h>
#include "err.h"

typedef struct _GwyHDF5File GwyHDF5File;

typedef void (*AttrHandlerFunc)(GwyHDF5File *ghfile,
                                hid_t loc_id,
                                const char *attr_name);

typedef struct {
    GArray *idlist;
    const gchar *idprefix;
    H5O_type_t idwhat;
} GatheredIds;

struct _GwyHDF5File {
    GArray *addr;
    GString *path;
    GString *buf;
    GwyContainer *meta;

    /* Generic gathering of some numeric values. */
    guint nlists;
    GatheredIds *lists;

    /* Generic gathering of datasets in the file (items are string paths in the file). Array datasets does not include
     * datsets which are data scales; they are only in datascales. */
    GArray *datasets;
    GArray *datascales;

    /* File type implementation specifics. */
    AttrHandlerFunc attr_handler;
    gpointer impl;
};

static inline void
err_HDF5(GError **error, const gchar *where, glong code)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                _("HDF5 library error %ld in function %s."), code, where);
}

G_GNUC_INTERNAL hid_t         gwyhdf5_quick_check              (const GwyFileDetectInfo *fileinfo,
                                                                gboolean only_name);
G_GNUC_INTERNAL void          gwyhdf5_init                     (GwyHDF5File *ghfile);
G_GNUC_INTERNAL void          gwyhdf5_free                     (GwyHDF5File *ghfile);
G_GNUC_INTERNAL void          gwyhdf5_alloc_lists              (GwyHDF5File *ghfile,
                                                                guint n);
G_GNUC_INTERNAL hid_t         gwyhdf5_make_string_type_for_attr(hid_t attr_type);
G_GNUC_INTERNAL herr_t        gwyhdf5_scan_file                (hid_t loc_id,
                                                                const char *name,
                                                                const H5L_info_t *info,
                                                                void *user_data);
G_GNUC_INTERNAL herr_t        gwyhdf5_process_attribute        (hid_t loc_id,
                                                                const char *attr_name,
                                                                const H5A_info_t *ainfo,
                                                                void *user_data);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_ints_attr            (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gint expected_rank,
                                                                gint *expected_dims,
                                                                gint *v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_int_attr             (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gint *v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_floats_attr          (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gint expected_rank,
                                                                gint *expected_dims,
                                                                gdouble *v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_float_attr           (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gdouble *v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_strs_attr            (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gint expected_rank,
                                                                gint *expected_dims,
                                                                gchar **v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_str_attr             (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gchar **v,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_get_str_attr_g           (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                gchar **v,
                                                                GError **error);
G_GNUC_INTERNAL hid_t         gwyhdf5_open_and_check_attr      (hid_t file_id,
                                                                const gchar *obj_path,
                                                                const gchar *attr_name,
                                                                H5T_class_t expected_class,
                                                                gint expected_rank,
                                                                gint *expected_dims,
                                                                GError **error);
G_GNUC_INTERNAL hid_t         gwyhdf5_open_and_check_dataset   (hid_t file_id,
                                                                const gchar *name,
                                                                gint expected_ndims,
                                                                gint *dims,
                                                                GError **error);
G_GNUC_INTERNAL gboolean      gwyhdf5_enumerate_indexed        (GString *path,
                                                                const gchar *prefix,
                                                                GArray *array);
G_GNUC_INTERNAL gboolean      gwyhdf5_check_status             (herr_t status,
                                                                hid_t file_id,
                                                                GwyHDF5File *ghfile,
                                                                const gchar *func_name,
                                                                GError **error);
G_GNUC_INTERNAL GwyContainer* gwyhdf5_meta_slash_to_4dots      (GwyContainer *meta);

#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
