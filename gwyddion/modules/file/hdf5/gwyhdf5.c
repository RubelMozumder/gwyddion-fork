/*
 *  $Id: gwyhdf5.c 26338 2024-05-15 09:01:36Z yeti-dn $
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
 * [FILE-MAGIC-MISSING]
 * This is a HDF5 file format helper, not a specific file format module.
 **/

#include "config.h"
#include <libgwyddion/gwyutils.h>
#include "gwyhdf5.h"

/*******************************************************************************************************************
 *
 * More or less general HDF5 utility functions
 *
 *******************************************************************************************************************/

#define MAGIC "\x89HDF\r\n\x1a\n"
#define MAGIC_SIZE (sizeof(MAGIC)-1)

#define EXTENSION ".h5"

static gboolean enumerate_indexed(GString *path, const gchar *prefix, GArray *array);

hid_t
gwyhdf5_quick_check(const GwyFileDetectInfo *fileinfo,
                    gboolean only_name)
{
    if (only_name)
        return -1;

    if (fileinfo->buffer_len <= MAGIC_SIZE || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return -1;

    return H5Fopen(fileinfo->name, H5F_ACC_RDONLY, H5P_DEFAULT);
}

void
gwyhdf5_init(GwyHDF5File *ghfile)
{
    gwy_clear(ghfile, 1);
    ghfile->meta = gwy_container_new();
    ghfile->path = g_string_new(NULL);
    ghfile->buf = g_string_new(NULL);
    ghfile->addr = g_array_new(FALSE, FALSE, sizeof(haddr_t));
    ghfile->datasets = g_array_new(FALSE, FALSE, sizeof(gchar*));
    ghfile->datascales = g_array_new(FALSE, FALSE, sizeof(gchar*));
}

void
gwyhdf5_alloc_lists(GwyHDF5File *ghfile, guint n)
{
    guint i;

    if (!n)
        return;

    ghfile->nlists = n;
    ghfile->lists = g_new(GatheredIds, n);
    for (i = 0; i < n; i++)
        ghfile->lists[i].idlist = g_array_new(FALSE, FALSE, sizeof(gint));
}

void
gwyhdf5_free(GwyHDF5File *ghfile)
{
    guint i;

    for (i = 0; i < ghfile->datasets->len; i++)
        g_free(g_array_index(ghfile->datasets, gchar*, i));
    g_array_free(ghfile->datasets, TRUE);
    for (i = 0; i < ghfile->datascales->len; i++)
        g_free(g_array_index(ghfile->datascales, gchar*, i));
    g_array_free(ghfile->datascales, TRUE);
    g_array_free(ghfile->addr, TRUE);
    if (ghfile->nlists) {
        for (i = 0; i < ghfile->nlists; i++)
            g_array_free(ghfile->lists[i].idlist, TRUE);
        g_free(ghfile->lists);
    }
    g_string_free(ghfile->path, TRUE);
    g_string_free(ghfile->buf, TRUE);
    GWY_OBJECT_UNREF(ghfile->meta);
}

/* NB: loc_id is ‘parent’ location and name is particlar item within it. */
herr_t
gwyhdf5_scan_file(hid_t loc_id,
                  const char *name,
                  G_GNUC_UNUSED const H5L_info_t *info,
                  void *user_data)
{
    GwyHDF5File *ghfile = (GwyHDF5File*)user_data;
    herr_t status, return_val = 0;
    H5O_info_t infobuf;
    GArray *addr = ghfile->addr;
    GString *path = ghfile->path;
    guint i, len = path->len;
    hid_t dataset;
    gchar *s;

    status = H5Oget_info_by_name(loc_id, name, &infobuf, H5P_DEFAULT);
    if (status < 0)
        return status;

    /* Detect loops. */
    for (i = 0; i < addr->len; i++) {
        if (g_array_index(addr, haddr_t, i) == infobuf.addr)
            return -1;
    }

    g_array_append_val(addr, infobuf.addr);
    g_string_append_c(path, '/');
    g_string_append(path, name);
    gwy_debug("path %s", path->str);
    if (infobuf.type == H5O_TYPE_GROUP) {
        return_val = H5Literate_by_name(loc_id, name, H5_INDEX_NAME, H5_ITER_NATIVE,
                                        NULL, gwyhdf5_scan_file, user_data, H5P_DEFAULT);
    }
    else if (infobuf.type == H5O_TYPE_DATASET) {
        s = g_strdup(path->str);
        dataset = H5Dopen(loc_id, name, H5P_DEFAULT);
        if (dataset >= 0) {
            if (H5DSis_scale(dataset) > 0)
                g_array_append_val(ghfile->datascales, s);
            else
                g_array_append_val(ghfile->datasets, s);
            H5Dclose(dataset);
        }
    }
    /* Nothing to do for other object types. */
    else if (infobuf.type == H5O_TYPE_NAMED_DATATYPE) {
    }
    else {
        gwy_debug("unknown type %d", infobuf.type);
    }

    for (i = 0; i < ghfile->nlists; i++) {
        GatheredIds *gathered = ghfile->lists + i;
        if (infobuf.type == gathered->idwhat)
            enumerate_indexed(path, gathered->idprefix, gathered->idlist);
    }

    if (infobuf.num_attrs > 0) {
        hid_t this_id = H5Oopen(loc_id, name, H5P_DEFAULT);

        H5Aiterate2(this_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_process_attribute, user_data);
        H5Oclose(this_id);
    }

    g_string_truncate(path, len);
    g_array_set_size(addr, addr->len-1);

    return return_val;
}

static void
format_int_values(GString *outbuf, const gint *values, gint nvalues)
{
    gint i;

    if (nvalues <= 0)
        return;
    g_string_printf(outbuf, "%d", values[0]);
    for (i = 1; i < nvalues; i++)
        g_string_append_printf(outbuf, "; %d", values[i]);
}

static void
format_float_values(GString *outbuf, const gdouble *values, gint nvalues)
{
    gint i;

    if (nvalues <= 0)
        return;
    g_string_printf(outbuf, "%.8g", values[0]);
    for (i = 1; i < nvalues; i++)
        g_string_append_printf(outbuf, "; %.8g", values[i]);
}

static herr_t
process_float_attribute(hid_t attr, hid_t attr_type, gint nitems,
                        GString *outbuf)
{
    herr_t status;

    if (nitems <= 0)
        return -1;

    if (nitems == 1) {
        gdouble v;

        gwy_debug("float");
        if ((status = H5Aread(attr, attr_type, &v)) >= 0)
            format_float_values(outbuf, &v, 1);
    }
    else {
        gdouble *v = g_new(gdouble, nitems);

        gwy_debug("float array");
        if ((status = H5Aread(attr, attr_type, v)) >= 0)
            format_float_values(outbuf, v, nitems);
        g_free(v);
    }

    return status;
}

static herr_t
process_integer_attribute(hid_t attr, hid_t attr_type, gint nitems,
                          GString *outbuf)
{
    herr_t status;

    if (nitems <= 0)
        return -1;

    if (nitems == 1) {
        gint v;

        gwy_debug("integer");
        if ((status = H5Aread(attr, attr_type, &v)) >= 0)
            format_int_values(outbuf, &v, 1);
    }
    else {
        gint *v = g_new(gint, nitems);

        gwy_debug("integer array");
        if ((status = H5Aread(attr, attr_type, v)) >= 0)
            format_int_values(outbuf, v, nitems);
        g_free(v);
    }

    return status;
}

/* Fixed strings are read differently, into preallocated buffers, but fortunately no one uses them. */
static herr_t
process_var_string_attribute(hid_t attr, hid_t attr_type, hid_t read_as_type, gint nitems,
                             GString *outbuf)
{
    herr_t status;
    gboolean close_str_type = FALSE;

    if (nitems <= 0)
        return -1;

    if (read_as_type < 0) {
        read_as_type = gwyhdf5_make_string_type_for_attr(attr_type);
        close_str_type = TRUE;
    }
    if (nitems == 1) {
        gchar *s = NULL;

        gwy_debug("string");
        if ((status = H5Aread(attr, read_as_type, &s)) >= 0) {
            g_strstrip(s);
            g_string_printf(outbuf, "%s", s);
        }
        H5free_memory(s);
    }
    else if (nitems > 0) {
        gchar **s = g_new0(gchar*, nitems);
        gint i;

        gwy_debug("string array");
        if ((status = H5Aread(attr, read_as_type, s)) >= 0) {
            g_strstrip(s[0]);
            g_string_assign(outbuf, s[0]);
            H5free_memory(s[0]);
            for (i = 1; i < nitems; i++) {
                g_strstrip(s[i]);
                g_string_append(outbuf, "; ");
                g_string_append(outbuf, s[i]);
                H5free_memory(s[i]);
            }
        }
        g_free(s);
    }
    if (close_str_type)
        H5Tclose(read_as_type);

    return status;
}

static herr_t
process_fixed_string_attribute(hid_t attr, hid_t attr_type, gint nitems,
                               GString *outbuf)
{
    hid_t str_type;
    herr_t status;
    hsize_t size;
    gchar *buf;
    gint i;

    if (nitems <= 0)
        return -1;

    if (nitems == 1) {
        gwy_debug("fixed-size string");
        g_string_set_size(outbuf, nitems);
        str_type = gwyhdf5_make_string_type_for_attr(attr_type);
        if ((status = H5Aread(attr, str_type, outbuf->str)) >= 0) {
            g_strstrip(outbuf->str);
            outbuf->len = strlen(outbuf->str);
        }
        H5Tclose(str_type);
    }
    else {
        gwy_debug("fixed-size string array");
        str_type = gwyhdf5_make_string_type_for_attr(attr_type);
        size = H5Tget_size(str_type);
        gwy_debug("string size (incl. nul) %u", (guint)size);

        buf = g_new(gchar, size*nitems);
        if ((status = H5Aread(attr, str_type, buf)) >= 0) {
            g_string_assign(outbuf, buf);
            for (i = 1; i < nitems; i++) {
                g_string_append(outbuf, "; ");
                g_string_append(outbuf, buf + i*size);
            }
        }
        g_free(buf);
        H5Tclose(str_type);
    }

    return status;
}

#if 0
/* XXX: This all requires HDF5 1.12. Currently that's way too new for our conservative taste. */
static herr_t
process_reference_attribute(hid_t attr, gint nitems,
                            GString *outbuf)
{
    hid_t ref_type = H5T_STD_REF;
    H5R_ref_t ref;
    herr_t status;
    hsize_t size;

    if (nitems <= 0)
        return -1;

    if (nitems > 1) {
        gwy_debug("Cannot handle reference arrays yet.");
        return -1;
    }

    gwy_debug("reference");
    if ((status = H5Aread(attr, ref_type, &ref)) >= 0) {
        size = H5Rget_obj_name(&ref, H5P_DEFAULT, NULL, 0);
        if (size > 0) {
            g_string_set_size(outbuf, size);
            /* Not passing size+1 makes it miss the last character. We have the +1 space so pass it. But make sure
             * the string is terminated afterwards. */
            H5Rget_obj_name(&ref, H5P_DEFAULT, outbuf->str, size+1);
            g_string_truncate(outbuf, size);
        }
        else
            status = -1;
        H5Rdestroy(&ref);
    }

    return status;
}
#endif

static herr_t
process_reference_attribute(hid_t attr, gint nitems,
                            GString *outbuf)
{
    hobj_ref_t objref;
    hdset_reg_ref_t dsrref;
    herr_t status;
    ssize_t size = -1;
    H5R_type_t reftype = H5R_BADTYPE;
    gpointer refptr = NULL;
    gchar buf[4];

    if (nitems <= 0)
        return -1;

    if (nitems > 1) {
        gwy_debug("Cannot handle reference arrays yet.");
        return -1;
    }

    gwy_debug("reference");
    gwy_clear(&objref, 1);
    if ((status = H5Aread(attr, H5T_STD_REF_OBJ, &objref)) >= 0) {
        /* It seems we cannot actually pass NULL for buf in the old API because there is an assertion HDAssert(name).
         * Pass a small buffer we are going to ignore. If the reference is not to an object, we can get still get
         * a good status above, but then we get zero size here. */
        size = H5Rget_name(attr, H5R_OBJECT, &objref, buf, sizeof(buf));
        gwy_debug("object reference size %ld", (glong)size);
        if (size < 0)
            return -1;
        if (size > 0) {
            reftype = H5R_OBJECT;
            refptr = &objref;
        }
    }
    if (size == 0 && (status = H5Aread(attr, H5T_STD_REF_DSETREG, &dsrref)) >= 0) {
        /* FIXME: Don't actually know what to do with a dataset region reference. */
        size = H5Rget_name(attr, H5R_DATASET_REGION, &dsrref, buf, sizeof(buf));
        gwy_debug("dataset region reference size %ld", (glong)size);
        if (size < 0)
            return -1;
        if (size > 0) {
            reftype = H5R_DATASET_REGION;
            refptr = &dsrref;
        }
    }

    if (refptr && reftype != H5R_BADTYPE) {
        g_assert(size > 0);
        g_string_set_size(outbuf, size);
        /* Not passing size+1 makes it miss the last character. We have the +1 space so pass it. But make sure
         * the string is terminated afterwards. */
        H5Rget_name(attr, reftype, refptr, outbuf->str, size+1);
        g_string_truncate(outbuf, size);
        gwy_debug("reference target <%s>", outbuf->str);
    }

    return status;
}

static herr_t
process_compound_attribute(hid_t space,
                           hid_t attr, hid_t attr_type,
                           gint imember,
                           GString *outbuf,
                           gboolean *handled)
{
    hid_t member_type, item_type;
    H5T_class_t member_class, item_class;
    gchar *member_name;
    gint nitems;
    herr_t status;
    hvl_t v;

    *handled = FALSE;
    nitems = H5Sget_simple_extent_npoints(space);

    /* Here we basically need to do it all over again with individual fields because we do not read compounds
     * as structs. */
    member_name = H5Tget_member_name(attr_type, imember);
    member_class = H5Tget_member_class(attr_type, imember);
    gwy_debug("compound[%d] \"%s\" class %d", imember, member_name, member_class);

    /* Simple types. */
    if (member_class == H5T_INTEGER) {
        member_type = H5Tcreate(H5T_COMPOUND, sizeof(gint));
        H5Tinsert(member_type, member_name, 0, H5T_NATIVE_INT);
        status = process_integer_attribute(attr, member_type, nitems, outbuf);
        H5free_memory(member_name);
        H5Tclose(member_type);
        *handled = TRUE;
        return status;
    }

    if (member_class == H5T_FLOAT) {
        member_type = H5Tcreate(H5T_COMPOUND, sizeof(gdouble));
        H5Tinsert(member_type, member_name, 0, H5T_NATIVE_DOUBLE);
        status = process_float_attribute(attr, member_type, nitems, outbuf);
        H5free_memory(member_name);
        H5Tclose(member_type);
        *handled = TRUE;
        return status;
    }

    if (member_class == H5T_STRING) {
        hid_t str_type, file_member_type = H5Tget_member_type(attr_type, imember);

        /* I do not see how this variant can be logically represented. Where would we read the fixed string
         * length? */
        if (!H5Tis_variable_str(file_member_type)) {
            H5free_memory(member_name);
            H5Tclose(file_member_type);
            return -1;
        }

        member_type = H5Tcreate(H5T_COMPOUND, sizeof(gpointer));
        str_type = gwyhdf5_make_string_type_for_attr(file_member_type);
        H5Tinsert(member_type, member_name, 0, str_type);
        status = process_var_string_attribute(attr, file_member_type, member_type, nitems, outbuf);
        H5free_memory(member_name);
        H5Tclose(str_type);
        H5Tclose(file_member_type);
        H5Tclose(member_type);
        *handled = TRUE;
        return status;
    }

    if (member_class != H5T_VLEN) {
        gwy_debug("cannot handle class %d", member_class);
        H5free_memory(member_name);
        return -1;
    }

    /* FIXME: We might want to support nested compounds. Some seem to love them (they are definitely preset in DATX),
     * even though getting the data out of them is PITA. */

    if (nitems != 1) {
        gwy_debug("cannot handle arrays of variable-length types");
        H5free_memory(member_name);
        return -1;
    }

    /* Variable length types are weird. They ‘super’ class is the class of the item. */
    member_type = H5Tget_member_type(attr_type, imember);
    item_type = H5Tget_super(member_type);
    item_class = H5Tget_class(item_type);
    gwy_debug("member type %ld", (glong)member_type);
    gwy_debug("member type item %ld", item_type);
    gwy_debug("member type item class %d", item_class);
    H5Tclose(member_type);
    H5Tclose(item_type);

    if (item_class == H5T_INTEGER)
        item_type = H5Tvlen_create(H5T_NATIVE_INT);
    else if (item_class == H5T_FLOAT)
        item_type = H5Tvlen_create(H5T_NATIVE_DOUBLE);
    else {
        gwy_debug("cannot handle variable-length types other than float and int");
        H5free_memory(member_name);
        return -1;
    }
    *handled = TRUE;
    member_type = H5Tcreate(H5T_COMPOUND, sizeof(hvl_t));
    H5Tinsert(member_type, member_name, 0, item_type);
    status = H5Aread(attr, member_type, &v);
    gwy_debug("read vlen: len %ld, data %p (status = %d)", (gulong)v.len, v.p, status);
    if (status >= 0) {
        if (item_class == H5T_INTEGER)
            format_int_values(outbuf, (gint*)v.p, v.len);
        else if (item_class == H5T_FLOAT)
            format_float_values(outbuf, (gdouble*)v.p, v.len);
        else {
            g_assert_not_reached();
        }
    }
    H5Dvlen_reclaim(member_type, space, H5P_DEFAULT, &v);
    H5free_memory(member_name);
    H5Tclose(item_type);

    return status;
}

herr_t
gwyhdf5_process_attribute(hid_t loc_id,
                          const char *attr_name,
                          G_GNUC_UNUSED const H5A_info_t *ainfo,
                          void *user_data)
{
    GwyHDF5File *ghfile = (GwyHDF5File*)user_data;
    GString *path = ghfile->path, *buf = ghfile->buf;
    G_GNUC_UNUSED H5T_cset_t cset = H5T_CSET_ERROR;
    guint len = path->len, attr_path_len;
    hid_t attr, attr_type, space;
    gboolean is_vlenstr = FALSE, handled, compound_handled_completely = FALSE;
    H5T_class_t type_class;
    gchar *member_name;
    gint nitems, j, nmembers;
    herr_t status;

    attr = H5Aopen(loc_id, attr_name, H5P_DEFAULT);
    attr_type = H5Aget_type(attr);
    space = H5Aget_space(attr);
    nitems = H5Sget_simple_extent_npoints(space);
    type_class = H5Tget_class(attr_type);
    if (type_class == H5T_STRING) {
        is_vlenstr = H5Tis_variable_str(attr_type);
        cset = H5Tget_cset(attr_type);
    }

    gwy_debug("attr %s, type class %d (is_vlenstr: %d, cset: %d)", attr_name, type_class, is_vlenstr, cset);
    g_string_append_c(path, '/');
    g_string_append(path, attr_name);
    status = -1;
    /* Try to read all attribute types used by Ergo; there are just a few. */
    if (type_class == H5T_INTEGER)
        status = process_integer_attribute(attr, H5T_NATIVE_INT, nitems, buf);
    else if (type_class == H5T_FLOAT)
        status = process_float_attribute(attr, H5T_NATIVE_DOUBLE, nitems, buf);
    else if (type_class == H5T_STRING && is_vlenstr)
        status = process_var_string_attribute(attr, attr_type, -1, nitems, buf);
    else if (type_class == H5T_STRING)
        status = process_fixed_string_attribute(attr, attr_type, nitems, buf);
    else if (type_class == H5T_REFERENCE)
        status = process_reference_attribute(attr, nitems, buf);
    else if (type_class == H5T_COMPOUND) {
        /* Here we basically need to do it all over again with individual fields because we do not read compounds
         * as structs. */
        attr_path_len = path->len;
        g_string_append_c(path, '/');
        nmembers = H5Tget_nmembers(attr_type);
        gwy_debug("compound, nmembers %d, nitems %d", nmembers, nitems);
        compound_handled_completely = TRUE;
        for (j = 0; j < nmembers; j++) {
            member_name = H5Tget_member_name(attr_type, j);
            g_string_append(path, member_name);
            status = process_compound_attribute(space, attr, attr_type, j, buf, &handled);

            if (!handled) {
                gwy_debug("unhandled compound member %s", member_name);
                compound_handled_completely = FALSE;
            }
            else {
                gwy_debug("compound %s reading status %d (value %s)", member_name, status, buf->str);
                if (status >= 0) {
                    gwy_debug("[%s] = <%s>", path->str, buf->str);
                    gwy_container_set_const_string_by_name(ghfile->meta, path->str+1, buf->str);
                }
            }

            H5free_memory(member_name);
            g_string_truncate(path, attr_path_len+1);
        }
        /* Do not satisfy status >= 0 below. That would try to add additional bogus item to ghfile->meta. */
        status = -1;
    }
    H5Sclose(space);
    H5Tclose(attr_type);
    H5Aclose(attr);

    if (status >= 0) {
        gwy_debug("[%s] = <%s>", path->str, buf->str);
        gwy_container_set_const_string_by_name(ghfile->meta, path->str+1, buf->str);
    }
    else if (type_class == H5T_COMPOUND) {
        /* Dealt with, one way or another. */
        if (!compound_handled_completely) {
            /* Can happen quite often in DATX (and possibly others). Just shut up about it. */
        }
    }
    else if (type_class == H5T_VLEN && gwy_strequal(attr_name, "DIMENSION_LIST")) {
        /* This is for dimension scales and we handle them using the high level H5DS function as neeed. The vlen lists
         * of references are not useful and PITA to read manually so just suppress warnings. */
    }
    else
        g_warning("Cannot handle attribute %d(%d)[%d]", type_class, is_vlenstr, nitems);

    if (ghfile->attr_handler)
        ghfile->attr_handler(ghfile, loc_id, attr_name);

    g_string_truncate(path, len);

    return 0;
}

hid_t
gwyhdf5_make_string_type_for_attr(hid_t attr_type)
{
    hid_t str_type;

    str_type = H5Tcopy(H5T_C_S1);
    if (H5Tis_variable_str(attr_type))
        H5Tset_size(str_type, H5T_VARIABLE);
    else
        H5Tset_size(str_type, H5Tget_size(attr_type)+1);

    H5Tset_strpad(str_type, H5T_STR_NULLTERM);
    /* This is kind of stupid because the character set is ‘whatever’. Unforutnately, when the attribute character set
     * is ASCII and we ask for UTF-8 it goes poorly. Even though the conversion is no-op HDF5 thinks it cannot do it
     * and fails. So we just count on ASCII and UTF-8 being the only character sets available – and both being fine
     * interpreted as UTF-8. */
    H5Tset_cset(str_type, H5Tget_cset(attr_type));

    return str_type;
}

/* expected_dims[] is non-const because we can either check or query the dimensions. Pass -1 as an expected_dims[]
 * item to query it. Pass NULL expected_dims if you do not care. */
hid_t
gwyhdf5_open_and_check_attr(hid_t file_id,
                            const gchar *obj_path,
                            const gchar *attr_name,
                            H5T_class_t expected_class,
                            gint expected_rank,
                            gint *expected_dims,
                            GError **error)
{
    hid_t attr, attr_type, space;
    H5T_class_t type_class;
    gint i, rank, status;
    hsize_t dims[3];

    gwy_debug("looking for %s in %s, class %d, rank %d",
              attr_name, obj_path, expected_class, expected_rank);
    if ((attr = H5Aopen_by_name(file_id, obj_path, attr_name, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
        err_MISSING_FIELD(error, attr_name);
        return -1;
    }

    attr_type = H5Aget_type(attr);
    type_class = H5Tget_class(attr_type);
    gwy_debug("found attr %d of type %d and class %d", (gint)attr, (gint)attr_type, type_class);
    if (type_class != expected_class) {
        H5Tclose(attr_type);
        H5Aclose(attr);
        err_UNSUPPORTED(error, attr_name);
        return -1;
    }

    if ((space = H5Aget_space(attr)) < 0) {
        err_HDF5(error, "H5Aget_space", space);
        H5Tclose(attr_type);
        H5Aclose(attr);
    }
    rank = H5Sget_simple_extent_ndims(space);
    gwy_debug("attr space is %d with rank %d", (gint)space, rank);
    if (rank > 3 || rank != expected_rank) {
        err_UNSUPPORTED(error, attr_name);
        goto fail;
    }

    if ((status = H5Sget_simple_extent_dims(space, dims, NULL)) < 0) {
        gwy_debug("cannot get space %d extent dims", (gint)space);
        err_HDF5(error, "H5Sget_simple_extent_dims", status);
        goto fail;
    }
    if (expected_dims) {
        for (i = 0; i < rank; i++) {
            gwy_debug("dims[%d]=%lu, expecting %d", i, (gulong)dims[i], expected_dims[i]);
            if (expected_dims[i] < 0)
                expected_dims[i] = dims[i];
            else if (dims[i] != (hsize_t)expected_dims[i]) {
                err_UNSUPPORTED(error, attr_name);
                goto fail;
            }
        }
    }

    H5Sclose(space);
    H5Tclose(attr_type);
    gwy_debug("attr %d seems OK", (gint)attr);
    return attr;

fail:
    H5Sclose(space);
    H5Tclose(attr_type);
    H5Aclose(attr);
    return -1;
}

gboolean
gwyhdf5_get_ints_attr(hid_t file_id,
                      const gchar *obj_path, const gchar *attr_name,
                      gint expected_rank, gint *expected_dims,
                      gint *v, GError **error)
{
    hid_t attr;
    gint status;

    if ((attr = gwyhdf5_open_and_check_attr(file_id, obj_path, attr_name, H5T_INTEGER,
                                            expected_rank, expected_dims, error)) < 0)
        return FALSE;

    status = H5Aread(attr, H5T_NATIVE_INT, v);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

gboolean
gwyhdf5_get_int_attr(hid_t file_id,
                     const gchar *obj_path, const gchar *attr_name,
                     gint *v, GError **error)
{
    return gwyhdf5_get_ints_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

gboolean
gwyhdf5_get_floats_attr(hid_t file_id,
                        const gchar *obj_path, const gchar *attr_name,
                        gint expected_rank, gint *expected_dims,
                        gdouble *v, GError **error)
{
    hid_t attr;
    gint status;

    if ((attr = gwyhdf5_open_and_check_attr(file_id, obj_path, attr_name, H5T_FLOAT,
                                            expected_rank, expected_dims, error)) < 0)
        return FALSE;

    status = H5Aread(attr, H5T_NATIVE_DOUBLE, v);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

gboolean
gwyhdf5_get_float_attr(hid_t file_id,
                       const gchar *obj_path, const gchar *attr_name,
                       gdouble *v, GError **error)
{
    return gwyhdf5_get_floats_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

gboolean
gwyhdf5_get_strs_attr(hid_t file_id,
                      const gchar *obj_path, const gchar *attr_name,
                      gint expected_rank, gint *expected_dims,
                      gchar **v, GError **error)
{
    hid_t attr, attr_type, str_type;
    gboolean is_vlenstr;
    gint status;

    if ((attr = gwyhdf5_open_and_check_attr(file_id, obj_path, attr_name, H5T_STRING,
                                            expected_rank, expected_dims, error)) < 0)
        return FALSE;

    attr_type = H5Aget_type(attr);
    if (attr_type < 0) {
        H5Aclose(attr);
        err_HDF5(error, "H5Aget_type", attr_type);
        return FALSE;
    }
    is_vlenstr = H5Tis_variable_str(attr_type);
    gwy_debug("attr %d is%s vlen string", (gint)attr, is_vlenstr ? "" : " not");
    if (!is_vlenstr) {
        H5Tclose(attr_type);
        H5Aclose(attr);
        /* XXX: Be more specific. */
        err_UNSUPPORTED(error, attr_name);
        return FALSE;
    }

    str_type = gwyhdf5_make_string_type_for_attr(attr_type);
    status = H5Aread(attr, str_type, v);
    H5Tclose(attr_type);
    H5Tclose(str_type);
    H5Aclose(attr);
    if (status < 0) {
        err_HDF5(error, "H5Aread", status);
        return FALSE;
    }
    return TRUE;
}

/* Get string attribute, but as a glib-allocated string so we do not have to track its origin and free it using
 * g_free() as any other string. */
gboolean
gwyhdf5_get_str_attr_g(hid_t file_id,
                       const gchar *obj_path, const gchar *attr_name,
                       gchar **v, GError **error)
{
    gchar *t;

    if (!gwyhdf5_get_strs_attr(file_id, obj_path, attr_name, 0, NULL, &t, error))
        return FALSE;

    *v = g_strdup(t);
    H5free_memory(t);
    return TRUE;
}

gboolean
gwyhdf5_get_str_attr(hid_t file_id,
                     const gchar *obj_path, const gchar *attr_name,
                     gchar **v, GError **error)
{
    return gwyhdf5_get_strs_attr(file_id, obj_path, attr_name, 0, NULL, v, error);
}

static gboolean
gwyhdf5_get_simple_space_dims(hid_t space, gint expected_ndims, gint *dims,
                              const gchar *name, GError **error)
{
    gint i, ndims;
    hsize_t hdims[4];

    g_return_val_if_fail(expected_ndims <= 4, FALSE);

    ndims = H5Sget_simple_extent_ndims(space);
    if (ndims != expected_ndims) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Dataset %s has %d dimensions instead of expected %d."), name, ndims, expected_ndims);
        return FALSE;
    }

    H5Sget_simple_extent_dims(space, hdims, NULL);
    for (i = 0; i < ndims; i++) {
        if (dims[i] >= 0) {
            if (dims[i] != hdims[i]) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Dataset %s dimension #%d is %ld, which does not match image resolution %d."),
                            name, i, (glong)hdims[i], dims[i]);
                return FALSE;
            }
        }
        else {
            gwy_debug("dims[%d] %ld (no expectation)", i, (glong)hdims[i]);
            dims[i] = hdims[i];
        }
    }
    return TRUE;
}

hid_t
gwyhdf5_open_and_check_dataset(hid_t file_id, const gchar *name,
                               gint expected_ndims, gint *dims,
                               GError **error)
{
    hid_t dataset, space;

    if ((dataset = H5Dopen(file_id, name, H5P_DEFAULT)) < 0) {
        err_HDF5(error, "H5Dopen", dataset);
        return -1;
    }
    gwy_debug("dataset %s is %ld", name, (glong)dataset);

    if ((space = H5Dget_space(dataset)) < 0) {
        err_HDF5(error, "H5Dget_space", space);
        H5Dclose(dataset);
        return -1;
    }
    if (!gwyhdf5_get_simple_space_dims(space, expected_ndims, dims, name, error)) {
        H5Sclose(space);
        H5Dclose(dataset);
        return -1;
    }
    H5Sclose(space);

    return dataset;
}

/* Gather all the numbers in paths of the form prefix#NUM. */
static gboolean
enumerate_indexed(GString *path, const gchar *prefix, GArray *array)
{
    guint i, len = strlen(prefix);
    const gchar *p;

    if (strncmp(path->str, prefix, len))
        return FALSE;

    p = path->str + len;
    for (i = 0; g_ascii_isdigit(p[i]); i++)
        ;
    if (!i || p[i])
        return FALSE;

    i = atol(p);
    if (i > G_MAXINT)
        return FALSE;

    g_array_append_val(array, i);
    gwy_debug("found indexed %s[%u]", prefix, i);
    return TRUE;
}

gboolean
gwyhdf5_check_status(herr_t status, hid_t file_id, GwyHDF5File *ghfile,
                     const gchar *func_name, GError **error)
{
    gwy_debug("status %d", status);
    if (status >= 0)
        return TRUE;

    err_HDF5(error, func_name, status);
    if (file_id >= 0)
        H5Fclose(file_id);
    if (ghfile)
        gwyhdf5_free(ghfile);
    return FALSE;
}

static void
transform_key(gpointer hkey, gpointer hvalue, gpointer user_data)
{
    const gchar *oldkey = g_quark_to_string(GPOINTER_TO_UINT(hkey));
    const gchar *value = g_value_get_string((GValue*)hvalue);
    GwyContainer *newmeta = (GwyContainer*)user_data;
    gchar *newkey = gwy_strreplace(oldkey, "/", "::", (gsize)-1);

    gwy_container_set_const_string_by_name(newmeta, newkey, value);
    g_free(newkey);
}

GwyContainer*
gwyhdf5_meta_slash_to_4dots(GwyContainer *meta)
{
    GwyContainer *newmeta = gwy_container_new();

    gwy_container_foreach(meta, NULL, transform_key, newmeta);
    return newmeta;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
