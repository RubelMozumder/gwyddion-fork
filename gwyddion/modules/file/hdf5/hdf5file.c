/*
 *  $Id: hdf5file.c 26225 2024-02-29 16:49:48Z yeti-dn $
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

/**
 * [FILE-MAGIC-USERGUIDE]
 * Generic HDF5 files
 * .h5
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Matlab MAT 7.x files
 * .mat
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

/*
 * HDF5 changes its APIs between versions incompatibly.  Forward compatibility is mostly preserved, but not
 * guaranteed.  To prevent breakage we need to know which specific version of the API we use and tell the library to
 * provide this one through compatibility macros.
 *
 * Therefore, this file must be compiled with -DH5_USE_18_API
 */
#include "config.h"
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "gwyhdf5.h"
#include "hdf5file.h"

#define MAGIC_MAT70 "MATLAB 7.0 MAT-file"
#define MAGIC_MAT70_SIZE (sizeof(MAGIC_MAT70)-1)
#define MAGIC_MAT73 "MATLAB 7.3 MAT-file"
#define MAGIC_MAT73_SIZE (sizeof(MAGIC_MAT73)-1)

enum {
    MAT7x_HEADER_SIZE = 512,
};

static gboolean module_register(void);


/* Matlab MAT 7 (only detection differs). */
static gint          mat7x_detect             (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name);

/* Generic */
static gint          ghdf5_detect             (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name);
static GwyContainer* ghdf5_load               (const gchar *filename,
                                               GwyRunType mode,
                                               GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports files based on Hierarchical Data Format (HDF), version 5."),
    "Yeti <yeti@gwyddion.net>",
    "2.0",
    "David Nečas (Yeti) & Petr Klapetek",
    "2020",
};

GWY_MODULE_QUERY2(module_info, hdf5file)

static gboolean
module_register(void)
{
    if (H5open() < 0) {
        g_warning("H5open() failed.");
        return FALSE;
    }
    /* Make libhdf5 print noisy errors to console when DEBUG is on. */
#ifndef DEBUG
    H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
#endif

    gwy_file_func_register("hdf5generic",
                           N_("Generic HDF5 files (.h5)"),
                           (GwyFileDetectFunc)&ghdf5_detect,
                           (GwyFileLoadFunc)&ghdf5_load,
                           NULL,
                           NULL);
    /* Read MAT 7.x files using the generic HDF5 reader. None of the Matlab specific stuff is meaningful for us
     * anyway. */
    gwy_file_func_register("mat7xfile",
                           N_("Matlab 7.x HDF5 MAT files (.mat)"),
                           (GwyFileDetectFunc)&mat7x_detect,
                           (GwyFileLoadFunc)&ghdf5_load,
                           NULL,
                           NULL);

    gwyhdf5_register_datxfile();
    gwyhdf5_register_epflfile();
    gwyhdf5_register_ergofile();
    gwyhdf5_register_nsidfile();
    gwyhdf5_register_shilpsfile();

    return TRUE;
}

/*******************************************************************************************************************
 *
 * Matlab 7.x
 *
 *******************************************************************************************************************/

static gint
mat7x_detect(const GwyFileDetectInfo *fileinfo,
             gboolean only_name)
{
    GwyFileDetectInfo subinfo;
    hid_t file_id;

    if (fileinfo->buffer_len <= MAT7x_HEADER_SIZE)
        return 0;

    if (memcmp(fileinfo->head, MAGIC_MAT70, MAGIC_MAT70_SIZE) && memcmp(fileinfo->head, MAGIC_MAT73, MAGIC_MAT73_SIZE))
        return 0;

    subinfo = *fileinfo;
    subinfo.buffer_len = fileinfo->buffer_len - MAT7x_HEADER_SIZE;
    subinfo.head = fileinfo->head + MAT7x_HEADER_SIZE;
    subinfo.tail = fileinfo->tail + MAT7x_HEADER_SIZE;

    if ((file_id = gwyhdf5_quick_check(&subinfo, only_name)) < 0)
        return 0;

    H5Fclose(file_id);

    return 75;
}

/*******************************************************************************************************************
 *
 * Plain/generic HDF5
 *
 *******************************************************************************************************************/

static gint
ghdf5_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    hid_t file_id;

    if ((file_id = gwyhdf5_quick_check(fileinfo, only_name)) < 0)
        return 0;

    H5Fclose(file_id);

    /* Return a moderate score. We have no idea if we can actually read anything from the file. */
    return 50;
}

static GwyContainer*
ghdf5_load(const gchar *filename,
           G_GNUC_UNUSED GwyRunType mode,
           GError **error)
{
    GwyContainer *container = NULL;
    GwyHDF5File ghfile;
    hid_t file_id, dataset;
    herr_t status;
    H5O_info_t infobuf;
    guint i;
    gint id;

    file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    gwy_debug("file_id %d", (gint)file_id);
    status = H5Oget_info(file_id, &infobuf);
    if (!gwyhdf5_check_status(status, file_id, NULL, "H5Oget_info", error))
        return NULL;

    gwyhdf5_init(&ghfile);
    g_array_append_val(ghfile.addr, infobuf.addr);
    status = H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_scan_file, &ghfile);
    if (!gwyhdf5_check_status(status, file_id, &ghfile, "H5Literate", error))
        return NULL;

    /* Read generic simple 2D data, i.e. images.
     * TODO: May want to read also other data types. */
    id = 0;
    for (i = 0; i < ghfile.datasets->len; i++) {
        gchar *name = g_array_index(ghfile.datasets, gchar*, i);
        gint dims[2] = { -1, -1 };
        GwyDataField *field;

        if ((dataset = gwyhdf5_open_and_check_dataset(file_id, name, 2, dims, NULL)) < 0)
            continue;
        if (err_DIMENSION(NULL, dims[0]) || err_DIMENSION(NULL, dims[1])) {
            H5Dclose(dataset);
            continue;
        }
        field = gwy_data_field_new(dims[1], dims[0], dims[1], dims[0], FALSE);
        status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, gwy_data_field_get_data(field));
        gwy_debug("status %d", status);
        H5Dclose(dataset);
        if (status < 0) {
            g_object_unref(field);
            continue;
        }

        if (!container)
            container = gwy_container_new();

        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), field);
        id++;
    }

    status = H5Fclose(file_id);
    gwy_debug("status %d", status);

    gwyhdf5_free(&ghfile);

    if (!container)
        err_NO_DATA(error);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
