/*
 *  $Id: ergofile.c 26349 2024-05-20 09:09:17Z yeti-dn $
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
 * Asylum Research Ergo HDF5
 * .h5,.aris
 * Read
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-asylum-research-aris-hdf5-spm">
 *   <comment>Asylum Research HDF5 SPM data</comment>
 *   <glob pattern="*.aris"/>
 *   <glob pattern="*.ARIS"/>
 * </mime-type>
 **/

#include "config.h"
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "gwyhdf5.h"
#include "hdf5file.h"

/* Asylym Research Ergo */
typedef struct {
    gchar *name;
    GwySIUnit *xyunit;
    GwySIUnit *zunit;
    gint xypower10;
    gint zpower10;
    gdouble realcoords[4];
} ErgoChannel;

typedef struct {
    GArray *channels;
    gint nframes;
} ErgoFile;

static gint          ergo_detect       (const GwyFileDetectInfo *fileinfo,
                                        gboolean only_name);
static GwyContainer* ergo_load         (const gchar *filename,
                                        GwyRunType mode,
                                        GError **error);
static void          ergo_attr_handler (GwyHDF5File *ghfile,
                                        hid_t loc_id,
                                        const char *attr_name);
static GwyContainer* ergo_read_channels(hid_t file_id,
                                        GwyHDF5File *ghfile,
                                        GError **error);
static GwyDataField* ergo_read_field   (hid_t file_id,
                                        guint r,
                                        ErgoChannel *c,
                                        gint frameid,
                                        const gint *yxres,
                                        GString *str,
                                        GError **error);

void
gwyhdf5_register_ergofile(void)
{
    gwy_file_func_register("ergofile",
                           N_("Asylum Research Ergo HDF5 files (.h5,.aris)"),
                           (GwyFileDetectFunc)&ergo_detect,
                           (GwyFileLoadFunc)&ergo_load,
                           NULL,
                           NULL);
}

static gint
ergo_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    hid_t file_id;
    gchar *format = NULL;
    gint version[3], dim = 3;
    gint score = 0;

    if ((file_id = gwyhdf5_quick_check(fileinfo, only_name)) < 0)
        return 0;

    if (gwyhdf5_get_str_attr(file_id, ".", "ARFormat", &format, NULL)) {
        if (gwyhdf5_get_ints_attr(file_id, ".", "ARVersion", 1, &dim, version, NULL))
            score = 100;
        H5free_memory(format);
    }

    H5Fclose(file_id);

    return score;
}

static GwyContainer*
ergo_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    GwyContainer *meta, *container = NULL;
    GwyHDF5File ghfile;
    ErgoFile efile;
    hid_t file_id;
    G_GNUC_UNUSED herr_t status;
    H5O_info_t infobuf;
    guint i;

    file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    gwy_debug("file_id %d", (gint)file_id);
    status = H5Oget_info(file_id, &infobuf);
    if (!gwyhdf5_check_status(status, file_id, NULL, "H5Oget_info", error))
        return NULL;

    gwyhdf5_init(&ghfile);
    ghfile.impl = &efile;
    ghfile.attr_handler = ergo_attr_handler;
    gwyhdf5_alloc_lists(&ghfile, 1);
    ghfile.lists[0].idprefix = "/DataSet/Resolution ";
    ghfile.lists[0].idwhat = H5O_TYPE_GROUP;
    g_array_append_val(ghfile.addr, infobuf.addr);

    gwy_clear(&efile, 1);
    efile.channels = g_array_new(FALSE, FALSE, sizeof(ErgoChannel));

    status = H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_scan_file, &ghfile);
    if (!gwyhdf5_check_status(status, file_id, &ghfile, "H5Literate", error)) {
        g_array_free(efile.channels, TRUE);
        return NULL;
    }
    status = H5Aiterate2(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_process_attribute, &ghfile);
    if (!gwyhdf5_check_status(status, file_id, &ghfile, "H5Aiterate2", error)) {
        g_array_free(efile.channels, TRUE);
        return NULL;
    }

    meta = gwyhdf5_meta_slash_to_4dots(ghfile.meta);
    g_object_unref(ghfile.meta);
    ghfile.meta = meta;

    if (gwyhdf5_get_int_attr(file_id, "DataSetInfo", "NumFrames", &efile.nframes, error)) {
        gwy_debug("nframes %d", efile.nframes);
        container = ergo_read_channels(file_id, &ghfile, error);
    }

    status = H5Fclose(file_id);
    gwy_debug("status %d", status);

    for (i = 0; i < efile.channels->len; i++) {
        ErgoChannel *c = &g_array_index(efile.channels, ErgoChannel, i);
        g_free(c->name);
        GWY_OBJECT_UNREF(c->xyunit);
        GWY_OBJECT_UNREF(c->zunit);
    }
    g_array_free(efile.channels, TRUE);
    gwyhdf5_free(&ghfile);

    return container;
}

static GwyContainer*
ergo_read_channels(hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    ErgoFile *efile = (ErgoFile*)ghfile->impl;
    GwyContainer *container = NULL;
    GArray *channels = efile->channels;
    GArray *resolutions = ghfile->lists[0].idlist;
    GString *str = ghfile->buf;
    GwyDataField *dfield, *mask;
    GError *err = NULL;
    gint expected2[2] = { 2, 2 }, yxres[2];
    gchar *s, *s2[2];
    gint frameid, id = 0;
    guint i, ri, r;

    for (ri = 0; ri < resolutions->len; ri++) {
        r = g_array_index(resolutions, guint, ri);
        for (i = 0; i < channels->len; i++) {
            ErgoChannel *c = &g_array_index(channels, ErgoChannel, i);

            g_string_printf(str, "DataSetInfo/Global/Channels/%s/ImageDims", c->name);

            if (!gwyhdf5_get_str_attr(file_id, str->str, "DataUnits", &s, error))
                goto fail;
            gwy_debug("zunit of %s is %s", c->name, s);
            c->zunit = gwy_si_unit_new_parse(s, &c->zpower10);
            H5free_memory(s);

            if (!gwyhdf5_get_strs_attr(file_id, str->str, "DimUnits", 1, expected2, s2, error))
                goto fail;
            gwy_debug("xyunits of %s are %s and %s", c->name, s2[0], s2[1]);
            if (!gwy_strequal(s2[0], s2[1]))
                g_warning("X and Y units differ, using X");
            c->xyunit = gwy_si_unit_new_parse(s2[1], &c->xypower10);
            H5free_memory(s2[0]);
            H5free_memory(s2[1]);

            /* NB: In all dimensions y is first, then x. */
            if (!gwyhdf5_get_floats_attr(file_id, str->str, "DimScaling", 2, expected2, c->realcoords, error))
                goto fail;
            gwy_debug("dims of %s are [%g, %g], [%g, %g]",
                      c->name, c->realcoords[2], c->realcoords[3], c->realcoords[0], c->realcoords[1]);

            g_string_append_printf(str, "/Resolution %d", r);
            if (!gwyhdf5_get_ints_attr(file_id, str->str, "DimExtents", 1, expected2, yxres, error))
                goto fail;
            gwy_debug("resid %u res %dx%d", r, yxres[1], yxres[0]);

            for (frameid = 0; frameid < efile->nframes; frameid++) {
                if (!(dfield = ergo_read_field(file_id, r, c, frameid, yxres, str, &err))) {
                    /* Sometimes this happens? Perhaps interrupted measurement? */
                    g_warning("Cannot read %s: %s", str->str, err->message);
                    g_clear_error(&err);
                    continue;
                }

                if (!container)
                    container = gwy_container_new();

                mask = gwy_app_channel_mask_of_nans(dfield, TRUE);
                gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), dfield);
                if (mask)
                    gwy_container_pass_object(container, gwy_app_get_mask_key_for_id(id), mask);
                gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(id), c->name);
                gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(id),
                                          gwy_container_duplicate(ghfile->meta));
                id++;
            }
        }
    }

    if (container)
        return container;

    err_NO_DATA(error);

fail:
    GWY_OBJECT_UNREF(container);
    return NULL;
}

static GwyDataField*
ergo_read_field(hid_t file_id,
                guint r, ErgoChannel *c, gint frameid, const gint *yxres,
                GString *str, GError **error)
{
    GwyDataField *dfield;
    hid_t dataset;
    gdouble q, xreal, yreal, xoff, yoff;
    herr_t status;

    g_string_printf(str, "DataSet/Resolution %u/Frame %d/%s/Image", r, frameid, c->name);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 2, (gint*)yxres, error)) < 0)
        return NULL;

    q = pow10(c->xypower10);

    /* NB: In all dimensions y is first, then x. */
    xreal = c->realcoords[3] - c->realcoords[2];
    sanitise_real_size(&xreal, "x size");
    xoff = MIN(c->realcoords[2], c->realcoords[3]);

    yreal = c->realcoords[1] - c->realcoords[0];
    sanitise_real_size(&yreal, "y size");
    yoff = MIN(c->realcoords[0], c->realcoords[1]);

    dfield = gwy_data_field_new(yxres[1], yxres[0], q*xreal, q*yreal, FALSE);
    gwy_data_field_set_xoffset(dfield, q*xoff);
    gwy_data_field_set_yoffset(dfield, q*yoff);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), c->xyunit);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), c->zunit);

    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, gwy_data_field_get_data(dfield));
    H5Dclose(dataset);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    if (c->zpower10)
        gwy_data_field_multiply(dfield, pow10(c->zpower10));

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        GWY_OBJECT_UNREF(dfield);
    }
    return dfield;
}

static void
append_channel_name(GArray *channels, const gchar *name)
{
    ErgoChannel c;

    gwy_debug("found channel %s", name);
    gwy_clear(&c, 1);
    c.name = g_strdup(name);
    g_strstrip(c.name);
    g_array_append_val(channels, c);
}

/* XXX: Handle /DataSetInfo/ChannelNames which do not have unique paths and we just build them during the scan */
static void
ergo_attr_handler(GwyHDF5File *ghfile, hid_t loc_id, const char *attr_name)
{
    ErgoFile *efile = (ErgoFile*)ghfile->impl;
    G_GNUC_UNUSED H5T_cset_t cset = H5T_CSET_ERROR;
    H5T_class_t type_class;
    hid_t attr, attr_type, str_type, space;
    gboolean is_vlenstr = FALSE;
    gint nitems, i;
    herr_t status;

    if (!gwy_strequal(ghfile->path->str, "/DataSetInfo/ChannelNames"))
        return;

    gwy_debug("handling /DataSetInfo/ChannelNames");
    attr = H5Aopen(loc_id, attr_name, H5P_DEFAULT);
    attr_type = H5Aget_type(attr);
    space = H5Aget_space(attr);
    nitems = H5Sget_simple_extent_npoints(space);
    type_class = H5Tget_class(attr_type);

    if (gwy_strequal(ghfile->path->str, "/DataSetInfo/ChannelNames")) {
        if (type_class == H5T_STRING) {
            is_vlenstr = H5Tis_variable_str(attr_type);
            cset = H5Tget_cset(attr_type);
        }

        if (type_class == H5T_STRING && is_vlenstr) {
            if (nitems == 1) {
                gchar *s;

                str_type = gwyhdf5_make_string_type_for_attr(attr_type);
                if ((status = H5Aread(attr, str_type, &s)) >= 0) {
                    append_channel_name(efile->channels, s);
                    H5free_memory(s);
                }
                H5Tclose(str_type);
            }
            else if (nitems > 0) {
                gchar **s = g_new(gchar*, nitems);

                str_type = gwyhdf5_make_string_type_for_attr(attr_type);
                if ((status = H5Aread(attr, str_type, s)) >= 0) {
                    for (i = 0; i < nitems; i++) {
                        append_channel_name(efile->channels, s[i]);
                        H5free_memory(s[i]);
                    }
                }
                H5Tclose(str_type);
                g_free(s);
            }
        }
    }

    H5Sclose(space);
    H5Tclose(attr_type);
    H5Aclose(attr);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
