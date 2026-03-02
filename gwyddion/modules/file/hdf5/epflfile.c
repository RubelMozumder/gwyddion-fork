/*
 *  $Id: epflfile.c 26339 2024-05-15 09:03:58Z yeti-dn $
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
 * EPFL HDF5
 * .h5
 * Read
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "gwyhdf5.h"
#include "hdf5file.h"

/* The file group structure goes like this
 *
 * measurements
 * +--electron microscopy measurements
 *    +--sem
 * +--fluorescence measurements
 *    +--epi
 * +--spm measurements
 *    +--afm
 *       +--electrical properties
 *       +--mechanical properties
 *       +--magnetic properties
 *    +--sicm
 *       +--mechanical properties
 *    +--snom
 *    +--stm
 *
 * But we do not really care. Each dataset has the important information (name, units, some other metadata) given
 * relative to it. So we can simply iterate of all datasets we find as in the generic loader – just be a bit smarter
 * about it. And also avoid creating images from the odd auxiliary datasets (coming from USID).
 */

/* This is basically a USID thing. They are always two dimensions of everything. The first number is the actual number
 * of dimensions (as everything is flattened). The second is the number of points.
 *
 * Execpt for data, where the first is (flattened) position dimension and the second (flattened) spectroscopic
 * dimension. */
typedef struct {
    gint dims_data[2];
    gint dims_pos[2];
    gint dims_spec[2];
} USIDDimensionInfo;

typedef struct {
    GwyContainer *meta;
    gchar *path;
    hid_t file_id;
} GroupMetaData;

static gint          epfl_detect             (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name);
static GwyContainer* epfl_load               (const gchar *filename,
                                              GwyRunType mode,
                                              GError **error);
static void          epfl_create_image       (hid_t file_id,
                                              const gchar *dataset_path,
                                              GwyHDF5File *ghfile,
                                              GwyContainer *container,
                                              gint *id);
static GwyContainer* epfl_gather_meta        (hid_t file_id,
                                              GwyHDF5File *ghfile,
                                              GString *key,
                                              guint klen);
static gboolean      read_usid_dimension_info(hid_t file_id,
                                              const gchar *dataset_path,
                                              GwyHDF5File *ghfile,
                                              USIDDimensionInfo *info);

void
gwyhdf5_register_epflfile(void)
{
    gwy_file_func_register("epflfile",
                           N_("EPFL HDF5 files (.h5)"),
                           (GwyFileDetectFunc)&epfl_detect,
                           (GwyFileLoadFunc)&epfl_load,
                           NULL,
                           NULL);
}

static gint
epfl_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    static const gchar *groups[] = {
        "electron microscopy measurements", "fluorescence measurements", "spm measurements",
    };

    hid_t file_id, group1_id, group2_id;
    gint score = 0;
    guint i;

    if ((file_id = gwyhdf5_quick_check(fileinfo, only_name)) < 0)
        return 0;

    /* The files do not have any of the typical top-level attributes we then use to recognise the data formats.
     * Look for some groups. They should be always present, even if they are empty. */
    if ((group1_id = H5Gopen(file_id, "measurements", H5P_DEFAULT)) > 0) {
        score += 25;
        for (i = 0; i < G_N_ELEMENTS(groups); i++) {
            if ((group2_id = H5Gopen(group1_id, groups[i], H5P_DEFAULT)) > 0) {
                score += 25;
                H5Gclose(group2_id);
            }
        }
        H5Gclose(group1_id);
    }

    H5Fclose(file_id);

    return score;
}

static GwyContainer*
epfl_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *container = NULL;
    GwyHDF5File ghfile;
    hid_t file_id;
    herr_t status;
    H5O_info_t infobuf;
    gint id;
    guint i;

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
    /* Other file formats also do H5Aiterate2() but here we do not seem to get anything useful. */

    container = gwy_container_new();
    id = 0;
    for (i = 0; i < ghfile.datasets->len; i++)
        epfl_create_image(file_id, g_array_index(ghfile.datasets, gchar*, i), &ghfile, container, &id);

    status = H5Fclose(file_id);
    gwy_debug("status %d", status);

    gwyhdf5_free(&ghfile);

    if (!gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
    }

    return container;
}

static void
epfl_create_image(hid_t file_id,
                  const gchar *dataset_path,
                  GwyHDF5File *ghfile,
                  GwyContainer *container,
                  gint *id)
{
    GwyContainer *meta, *hash = ghfile->meta;
    gint dims[2] = { -1, -1 };
    const gchar *name;
    const guchar *s;
    GwyDataField *field;
    GString *key;
    gchar *end;
    hid_t dataset;
    herr_t status;
    gint power10;
    gdouble u;
    guint klen;

    if ((name = strrchr(dataset_path, '/')))
        name++;
    else
        name = dataset_path;
    gwy_debug("<%s> <%s>", dataset_path, name);
    if (gwy_stramong(name,
                     "Position_Indices", "Position_Values", "Spectroscopic_Indices", "Spectroscopic_Values", NULL))
        return;

    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, dataset_path, 2, dims, NULL)) < 0)
        return;
    gwy_debug("dims %d %d", dims[0], dims[1]);

    key = g_string_new(NULL);

    /* Skip the initial slash. */
    g_string_assign(key, dataset_path+1);
    g_string_append_c(key, '/');
    klen = key->len;

    if (!(meta = epfl_gather_meta(file_id, ghfile, key, klen))) {
        g_string_free(key, TRUE);
        H5Dclose(dataset);
        return;
    }

    /* FIXME: This is wrong for USID-style dimensions. */
    if (err_DIMENSION(NULL, dims[0]) || err_DIMENSION(NULL, dims[1])) {
        g_string_free(key, TRUE);
        g_object_unref(meta);
        H5Dclose(dataset);
        return;
    }

    field = gwy_data_field_new(dims[1], dims[0], dims[1], dims[0], FALSE);
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, gwy_data_field_get_data(field));
    gwy_debug("status %d", status);
    H5Dclose(dataset);
    if (status < 0) {
        g_string_free(key, TRUE);
        g_object_unref(field);
        return;
    }

    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(*id), field);

    /* The two labels are usually the same. */
    s = name;
    g_string_truncate(key, klen);
    g_string_append(key, "quantity");
    gwy_container_gis_string_by_name(hash, key->str, &s);
    gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(*id), s);

    /* Value units. */
    g_string_truncate(key, klen);
    g_string_append(key, "units");
    if (gwy_container_gis_string_by_name(hash, key->str, &s)) {
        gwy_debug("found units <%s>", s);
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(field), s, &power10);
        if (power10)
            gwy_data_field_multiply(field, pow10(power10));
    }

    /* Lateral dimensions – take from metadata. */
    if (gwy_container_gis_string_by_name(meta, "Width", &s)) {
        gwy_debug("found Width <%s>", s);
        u = g_ascii_strtod(s, &end);
        sanitise_real_size(&u, "x size");
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(field), end, &power10);
        u *= pow10(power10);
        gwy_data_field_set_xreal(field, u);
    }
    if (gwy_container_gis_string_by_name(meta, "Height", &s)) {
        gwy_debug("found Height <%s>", s);
        u = g_ascii_strtod(s, &end);
        sanitise_real_size(&u, "y size");
        gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_xy(field), end, &power10);
        u *= pow10(power10);
        gwy_data_field_set_yreal(field, u);
    }

    if (gwy_container_get_n_items(meta))
        gwy_container_set_object(container, gwy_app_get_data_meta_key_for_id(*id), meta);
    g_object_unref(meta);

    g_string_free(key, TRUE);

    (*id)++;
}

static herr_t
scan_meta_group(G_GNUC_UNUSED hid_t loc_id,
                const char *name,
                G_GNUC_UNUSED const H5A_info_t *ainfo,
                void *user_data)
{
    GroupMetaData *gmd = (GroupMetaData*)user_data;
    gchar *s = NULL, *s_utf8;

    /* Do not overwrite existing metadata. This should generally not happen, but we go from the innermost group
     * to the outermost. So if it does happen keep the information closest to the data. */
    gwy_debug("attr %s", name);
    if (gwy_container_contains_by_name(gmd->meta, name)) {
        gwy_debug("already exists");
        return 0;
    }

    if (!gwyhdf5_get_str_attr_g(gmd->file_id, gmd->path, name, &s, NULL))
        return 0;

    /* XXX: It seems the nominally UTF-8 strings may actually be ISO-8895-1. Try both. */
    if (g_utf8_validate(s, -1, NULL))
        s_utf8 = s;
    else {
        s_utf8 = gwy_convert_to_utf8(s, -1, "ISO-8859-1");
        g_free(s);
        if (!s_utf8)
            return 0;
    }
    g_strdelimit(s_utf8, "\n\r\t\v", ' ');

    gwy_debug("value %s", s_utf8);
    gwy_container_set_string_by_name(gmd->meta, name, s_utf8);

    return 0;
}

static GwyContainer*
epfl_gather_meta(hid_t file_id, GwyHDF5File *ghfile,
                 GString *key, guint klen)
{
    /* There is a lot of rubbish. So use a whitelist. */
    static const gchar *dataset_attributes[] = {
        "platform", "machine_id", "labels", "timestamp", "quantity",
    };
    /* Group attribute canaries. We use these for checking it this looks like group from which the attributes should
     * be used as metadata. But we then take all. */
    static const gchar *group_attributes[] = {
        "Width", "Height", "Date", "Mode", "Lines", "Scan Rate", "X Offset", "Y Offset",
    };
    GwyContainer *meta = gwy_container_new();
    const guchar *s;
    GroupMetaData gmd;
    hid_t group;
    gchar *t;
    guint i;

    /* Metadata (of the dataset itself). */
    for (i = 0; i < G_N_ELEMENTS(dataset_attributes); i++) {
        g_string_truncate(key, klen);
        g_string_append(key, dataset_attributes[i]);
        if (gwy_container_gis_string_by_name(ghfile->meta, key->str, &s))
            gwy_container_set_const_string_by_name(meta, key->str + klen, s);
    }

    /* Metadata (from a parent group). We cannot be sure how deep we are with respect to the parent group, so just
     * go up and see. */
    while ((t = strrchr(key->str, '/'))) {
        klen = t - key->str;
        g_string_truncate(key, klen+1);
        gwy_debug("looking for parent attributes in <%.*s>", klen, key->str);
        for (i = 0; i < G_N_ELEMENTS(group_attributes); i++) {
            g_string_truncate(key, klen+1);
            g_string_append(key, group_attributes[i]);
            if (gwy_container_gis_string_by_name(ghfile->meta, key->str, &s)) {
                g_string_truncate(key, klen);
                gwy_debug("found canary attribute <%s> in parent group <%s>", group_attributes[i], key->str);
                gmd.meta = meta;
                gmd.file_id = file_id;

                gmd.path = g_strconcat("/", key->str, NULL);
                gwy_debug("path <%s>", gmd.path);
                group = H5Oopen(file_id, gmd.path, H5P_DEFAULT);
                gwy_debug("group %lu", group);
                if (group >= 0)
                    H5Aiterate2(group, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, scan_meta_group, &gmd);

                H5Oclose(group);
                g_free(gmd.path);
            }
        }
        g_string_truncate(key, klen);
    }

    return meta;
}

/* This is basically a general USID thing. Reading dataset dimensions is not that useful there because the values are
 * serialised, but it is good to gather it anyway.
 *
 * Aside from physical size consistency between indices and values we make no judgement calls here. */
G_GNUC_UNUSED
static gboolean
read_usid_dimension_info(hid_t file_id,
                         const gchar *dataset_path,
                         GwyHDF5File *ghfile,
                         USIDDimensionInfo *info)
{
    GwyContainer *hash = ghfile->meta;
    GString *key = g_string_new(NULL);
    gboolean ok = FALSE;
    const guchar *s;
    hid_t dataset;

    /* Data. */
    gwy_debug("gathering info about %s", dataset_path);
    info->dims_data[0] = -1;
    info->dims_data[1] = -1;
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, dataset_path, 2, info->dims_data, NULL)) < 0)
        goto end;
    H5Dclose(dataset);
    gwy_debug("dimensions %d x %d", info->dims_data[0], info->dims_data[1]);

    /* Position indices. */
    g_string_assign(key, dataset_path+1);
    g_string_append(key, "/Position_Indices");
    gwy_debug("looking for %s", key->str);
    if (!gwy_container_gis_string_by_name(hash, key->str, &s))
        goto end;

    gwy_debug("found %s, checking dimensions", s);
    g_string_append(key, s+1);
    info->dims_pos[0] = -1;
    info->dims_pos[1] = -1;
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, s, 2, info->dims_pos, NULL)) < 0)
        goto end;
    H5Dclose(dataset);
    gwy_debug("dimensions %d x %d", info->dims_pos[0], info->dims_pos[1]);

    /* Position values must have exactly the same shape as the indices.
     * It is khis is kind of bonkers to overspecify it like that. */
    g_string_assign(key, dataset_path+1);
    g_string_append(key, "/Position_Values");
    gwy_debug("looking for %s", key->str);
    if (!gwy_container_gis_string_by_name(hash, key->str, &s))
        goto end;

    gwy_debug("found %s, checking dimensions", s);
    g_string_append(key, s+1);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, s, 2, info->dims_pos, NULL)) < 0)
        goto end;
    H5Dclose(dataset);

    /* Spectroscopic indices. */
    g_string_assign(key, dataset_path+1);
    g_string_append(key, "/Spectroscopic_Indices");
    gwy_debug("looking for %s", key->str);
    if (!gwy_container_gis_string_by_name(hash, key->str, &s))
        goto end;

    gwy_debug("found %s, checking dimensions", s);
    g_string_append(key, s+1);
    info->dims_spec[0] = -1;
    info->dims_spec[1] = -1;
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, s, 2, info->dims_spec, NULL)) < 0)
        goto end;
    H5Dclose(dataset);
    gwy_debug("dimensions %d x %d", info->dims_spec[0], info->dims_spec[1]);

    /* Spectroscopic values must have exactly the same shape as the indices.
     * It is khis is kind of bonkers to overspecify it like that. */
    g_string_assign(key, dataset_path+1);
    g_string_append(key, "/Spectroscopic_Values");
    gwy_debug("looking for %s", key->str);
    if (!gwy_container_gis_string_by_name(hash, key->str, &s))
        goto end;

    gwy_debug("found %s, checking dimensions", s);
    g_string_append(key, s+1);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, s, 2, info->dims_spec, NULL)) < 0)
        goto end;
    H5Dclose(dataset);

    if (info->dims_data[1] != info->dims_pos[1]) {
        gwy_debug("mismatch between position and data size");
        goto end;
    }
    if (info->dims_data[0] != info->dims_spec[1]) {
        gwy_debug("mismatch between spectroscopic and data size");
        goto end;
    }

    ok = TRUE;

end:
    g_string_free(key, TRUE);

    return ok;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
