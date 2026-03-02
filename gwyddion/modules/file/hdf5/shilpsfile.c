/*
 *  $Id: shilpsfile.c 26339 2024-05-15 09:03:58Z yeti-dn $
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
 * Shilps Sciences Lucent HDF5
 * .h5
 * Read SPS Volume Curvemap
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libprocess/correct.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "gwyhdf5.h"
#include "hdf5file.h"

/* Shilps Lucent */
enum {
    SHILPS_IMAGES  = 0,
    SHILPS_SPECTRA = 1,
    SHILPS_CURVES  = 2,
    SHILPS_VOLUME  = 3,
    SHILPS_XYZ     = 4,
    SHILPS_NDATA
};

typedef struct {
    gint start;
    gint end;
    gchar *name;
} ShilpsSegment;

static gint          shilps_detect                 (const GwyFileDetectInfo *fileinfo,
                                                    gboolean only_name);
static GwyContainer* shilps_load                   (const gchar *filename,
                                                    GwyRunType mode,
                                                    GError **error);
static void          shilps_filter_meta            (GwyHDF5File *ghfile);
static gint          shilps_read_images            (GwyContainer *data,
                                                    hid_t file_id,
                                                    GwyHDF5File *ghfile,
                                                    GError **error);
static GwyDataField* shilps_read_field             (hid_t file_id,
                                                    gint id,
                                                    gint xres,
                                                    gint yres,
                                                    gdouble xreal,
                                                    gdouble yreal,
                                                    const gchar *xyunitstr,
                                                    GString *str,
                                                    GError **error);
static gint          shilps_read_graphs            (GwyContainer *container,
                                                    hid_t file_id,
                                                    GwyHDF5File *ghfile,
                                                    GError **error);
static gboolean      shilps_read_graph_spec        (hid_t file_id,
                                                    gint id,
                                                    const gint *plot_map,
                                                    gint ngraphs,
                                                    GArray *spectra,
                                                    GArray *segments,
                                                    gboolean first_time,
                                                    GString *str,
                                                    GError **error);
static gint          shilps_read_spectra           (GwyContainer *container,
                                                    hid_t file_id,
                                                    GwyHDF5File *ghfile,
                                                    GError **error);
static GwySpectra*   shilps_read_curves_spec       (hid_t file_id,
                                                    gint id,
                                                    GString *str,
                                                    GError **error);
static gint          shilps_read_volumes           (GwyContainer *container,
                                                    hid_t file_id,
                                                    GwyHDF5File *ghfile,
                                                    GError **error);
static GwyBrick*     shilps_read_brick             (hid_t file_id,
                                                    gint id,
                                                    gint xres,
                                                    gint yres,
                                                    gint zres,
                                                    gdouble xreal,
                                                    gdouble yreal,
                                                    gdouble zreal,
                                                    const gchar *xyunitstr,
                                                    GString *str,
                                                    GError **error);
static gint          shilps_read_xyzs              (GwyContainer *container,
                                                    hid_t file_id,
                                                    GwyHDF5File *ghfile,
                                                    GError **error);
static GwySurface*   shilps_read_surface           (hid_t file_id,
                                                    gint id,
                                                    const gchar *xyunitstr,
                                                    GString *str,
                                                    GError **error);
static void          shilps_set_spectra_coordinates(GwyContainer *container,
                                                    gint nspec,
                                                    gint nxyz);


void
gwyhdf5_register_shilpsfile(void)
{
    gwy_file_func_register("shilpsfile",
                           N_("Shilps Sciences Lucent HDF5 files (.h5)"),
                           (GwyFileDetectFunc)&shilps_detect,
                           (GwyFileLoadFunc)&shilps_load,
                           NULL,
                           NULL);
}

static gint
shilps_detect(const GwyFileDetectInfo *fileinfo,
              gboolean only_name)
{
    hid_t file_id;
    gchar *company;
    gint score = 0;

    if ((file_id = gwyhdf5_quick_check(fileinfo, only_name)) < 0)
        return 0;

    if (gwyhdf5_get_str_attr(file_id, ".", "Company", &company, NULL)) {
        if (gwy_strequal(company, "Shilps Sciences"))
            score = 100;
        H5free_memory(company);
    }

    H5Fclose(file_id);

    return score;
}

static GwyContainer*
shilps_load(const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GwyContainer *container, *meta;
    GwyHDF5File ghfile;
    hid_t file_id;
    G_GNUC_UNUSED herr_t status;
    H5O_info_t infobuf;
    guint i;
    gint nspec, nxyz;

    file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
    gwy_debug("file_id %d", (gint)file_id);
    status = H5Oget_info(file_id, &infobuf);
    if (!gwyhdf5_check_status(status, file_id, NULL, "H5Oget_info", error))
        return NULL;

    gwyhdf5_init(&ghfile);
    gwyhdf5_alloc_lists(&ghfile, SHILPS_NDATA);
    /* Having two things called spectra makes a bit of a mess in the corresponding function names. */
    ghfile.lists[SHILPS_IMAGES].idprefix = "/Session/Scan/Image";
    ghfile.lists[SHILPS_SPECTRA].idprefix = "/Session/Spectra/Graph";
    ghfile.lists[SHILPS_CURVES].idprefix = "/Session/Curves/Spectrum";
    ghfile.lists[SHILPS_VOLUME].idprefix = "/Session/Volume/Image3d";
    ghfile.lists[SHILPS_XYZ].idprefix = "/Session/XYZ/Scatter";
    for (i = 0; i < SHILPS_NDATA; i++)
        ghfile.lists[i].idwhat = H5O_TYPE_DATASET;
    g_array_append_val(ghfile.addr, infobuf.addr);

    status = H5Literate(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_scan_file, &ghfile);
    if (!gwyhdf5_check_status(status, file_id, &ghfile, "H5Literate", error))
        return NULL;

    status = H5Aiterate2(file_id, H5_INDEX_NAME, H5_ITER_NATIVE, NULL, gwyhdf5_process_attribute, &ghfile);
    if (!gwyhdf5_check_status(status, file_id, &ghfile, "H5Aiterate2", error))
        return NULL;

    meta = gwyhdf5_meta_slash_to_4dots(ghfile.meta);
    g_object_unref(ghfile.meta);
    ghfile.meta = meta;
    shilps_filter_meta(&ghfile);

    container = gwy_container_new();
    if (shilps_read_images(container, file_id, &ghfile, error) < 0
        || shilps_read_volumes(container, file_id, &ghfile, error) < 0
        || shilps_read_graphs(container, file_id, &ghfile, error) < 0
        || (nxyz = shilps_read_xyzs(container, file_id, &ghfile, error)) < 0
        || (nspec = shilps_read_spectra(container, file_id, &ghfile, error)) < 0)
        GWY_OBJECT_UNREF(container);

    status = H5Fclose(file_id);
    gwy_debug("status %d", status);
    gwyhdf5_free(&ghfile);

    shilps_set_spectra_coordinates(container, nspec, nxyz);

    /* All data types are optional so we might not have failed but also have not read anything. */
    if (container && !gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
    }

    return container;
}

static void
gather_to_remove(gpointer key, G_GNUC_UNUSED gpointer value, gpointer user_data)
{
    static const gchar *prefixes[] = {
        "Session::Scan::Image", "Session::Spectra::Graph", "Session::Curves::Spectrum", "Session::Volume::Image3d",
        "Session::XYZ:Scatter",
    };
    static guint lenghts[G_N_ELEMENTS(prefixes)] = { 0, };

    GArray *to_remove = (GArray*)user_data;
    GQuark quark = GPOINTER_TO_UINT(key);
    const gchar *s = g_quark_to_string(quark);
    guint i, len;

    if (!lenghts[0]) {
        for (i = 0; i < G_N_ELEMENTS(prefixes); i++)
            lenghts[i] = strlen(prefixes[i]);
    }
    for (i = 0; i < G_N_ELEMENTS(prefixes); i++) {
        len = lenghts[i];
        if (strncmp(s, prefixes[i], len) == 0 && g_ascii_isdigit(s[len])) {
            gwy_debug("filtering out metadata %s", s);
            g_array_append_val(to_remove, quark);
            return;
        }
    }
}

/* Filter out data-specific items. We could do a more refined filtering, keeping information pertaining to the
 * individual data pieces (creating different metadata for each data piece). But there is not much useful there to
 * keep anyway. */
static void
shilps_filter_meta(GwyHDF5File *ghfile)
{
    GwyContainer *meta = ghfile->meta;
    GArray *quarks = g_array_new(FALSE, FALSE, sizeof(GQuark));
    guint i;

    gwy_container_foreach(meta, NULL, gather_to_remove, quarks);
    for (i = 0; i < quarks->len; i++)
        gwy_container_remove(meta, g_array_index(quarks, GQuark, i));
    g_array_free(quarks, TRUE);
}

static gint
shilps_read_images(GwyContainer *container,
                   hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    const gchar grouppfx[] = "Session/Scan";
    GArray *images = ghfile->lists[SHILPS_IMAGES].idlist;
    GString *str = ghfile->buf;
    GwyDataField *dfield;
    gint xres, yres, power10;
    gdouble xreal, yreal;
    gchar *channel, *scancyc, *title, *xyunitstr;
    GwyContainer *meta;
    guint i, id;

    gwy_debug("nimages %u", images->len);
    if (!images->len)
        return 0;

    if (!gwyhdf5_get_int_attr(file_id, grouppfx, "X No", &xres, error)
        || !gwyhdf5_get_int_attr(file_id, grouppfx, "Y No", &yres, error)
        || !gwyhdf5_get_float_attr(file_id, grouppfx, "X Range", &xreal, error)
        || !gwyhdf5_get_float_attr(file_id, grouppfx, "Y Range", &yreal, error))
        return -1;

    gwy_debug("xres %d, yres %d", xres, yres);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres))
        return -1;

    gwy_debug("xreal %g, yreal %g", xreal, yreal);
    sanitise_real_size(&xreal, "x size");
    sanitise_real_size(&yreal, "y size");

    if (!gwyhdf5_get_str_attr_g(file_id, grouppfx, "Range Units", &xyunitstr, NULL))
        xyunitstr = g_strdup("µm");
    g_object_unref(gwy_si_unit_new_parse(xyunitstr, &power10));
    xreal *= pow10(power10);
    yreal *= pow10(power10);

    for (i = 0; i < images->len; i++) {
        id = g_array_index(images, gint, i);
        if (!(dfield = shilps_read_field(file_id, id, xres, yres, xreal, yreal, xyunitstr, str, error))) {
            g_free(xyunitstr);
            return -1;
        }

        gwy_container_pass_object(container, gwy_app_get_data_key_for_id(i), dfield);

        /* shilps_read_field() fills str->str with the correct prefix. */
        if (gwyhdf5_get_str_attr(file_id, str->str, "Channel", &channel, NULL)) {
            g_strstrip(channel);
            if (gwyhdf5_get_str_attr(file_id, str->str, "Scan cycle", &scancyc, NULL)) {
                g_strstrip(scancyc);
                title = g_strdup_printf("%s (%s)", channel, scancyc);
                gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), title);
                g_free(title);
                H5free_memory(scancyc);
            }
            else
                gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(i), channel);
            H5free_memory(channel);
        }
        else
            gwy_app_channel_title_fall_back(container, i);

        meta = gwy_container_duplicate(ghfile->meta);
        gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(i), meta);
    }
    g_free(xyunitstr);

    return images->len;
}

static GwyDataField*
shilps_read_field(hid_t file_id, gint id,
                  gint xres, gint yres, gdouble xreal, gdouble yreal,
                  const gchar *xyunitstr,
                  GString *str, GError **error)
{
    GwyDataField *dfield = NULL;
    hid_t dataset;
    gint power10;
    gint yxres[2] = { yres, xres };
    gchar *zunitstr = NULL;
    herr_t status = -1;

    g_string_printf(str, "Session/Scan/Image%d", id);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 2, yxres, error)) < 0)
        return NULL;

    gwyhdf5_get_str_attr_g(file_id, str->str, "Units", &zunitstr, NULL);
    gwy_debug("Range units %s, data Units %s", xyunitstr, zunitstr);

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, TRUE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), xyunitstr);
    gwy_si_unit_set_from_string_parse(gwy_data_field_get_si_unit_z(dfield), zunitstr, &power10);
    g_free(zunitstr);

    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, gwy_data_field_get_data(dfield));
    H5Dclose(dataset);

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        GWY_OBJECT_UNREF(dfield);
    }
    else
        gwy_data_field_multiply(dfield, pow10(power10));

    return dfield;
}

/* This also reads spectra. In fact, this is probably the right SPS data type and the other should be some kind of
 * curve map. */
static gint
shilps_read_graphs(GwyContainer *container,
                   hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    const gchar grouppfx[] = "Session/Spectra";
    GArray *graphs = ghfile->lists[SHILPS_SPECTRA].idlist;
    GArray *segments;
    GString *str = ghfile->buf;
    GArray *spectra;
    GwySpectra *spec;
    guint i, id;
    hid_t attr;
    gint plots_dims[2] = { -1, 2 };
    gint *plot_map = NULL;
    gboolean ok = FALSE;
    gdouble *d;

    gwy_debug("ngraphs %u", graphs->len);
    if (!graphs->len)
        return 0;

    /* The plot map is a N × 2 array giving (abscissa, ordinate) pairs to plot. If we do not have it we fall back to
     * plotting everything as a function of the data point number. */
    if ((attr = gwyhdf5_open_and_check_attr(file_id, grouppfx, "Plots", H5T_INTEGER, 2, plots_dims, NULL)) >= 0) {
        H5Aclose(attr);
        gwy_debug("found Plots array %d x %d", plots_dims[0], plots_dims[1]);
        plot_map = g_new(gint, plots_dims[0]*plots_dims[1]);
        if (!gwyhdf5_get_ints_attr(file_id, grouppfx, "Plots", 2, plots_dims, plot_map, NULL))
            GWY_FREE(plot_map);
        for (i = 0; (gint)i < plots_dims[0]*plots_dims[1]; i++) {
            /* NB: The numbers are indexed from 1, like channel numbers. */
            if (plot_map[i] <= 0) {
                g_warning("Invalid Plots map index #%u: %d < 0", i, plot_map[i]);
                GWY_FREE(plot_map);
                break;
            }
        }
    }

    spectra = g_array_new(FALSE, TRUE, sizeof(GwySpectra*));
    segments = g_array_new(FALSE, FALSE, sizeof(ShilpsSegment));
    if (plot_map)
        g_array_set_size(spectra, plots_dims[0]);

    for (i = 0; i < graphs->len; i++) {
        id = g_array_index(graphs, gint, i);
        if (!shilps_read_graph_spec(file_id, id, plot_map, plot_map ? plots_dims[0] : -1, spectra, segments, !i,
                                    str, error))
            goto fail;
    }

    for (i = 0; i < spectra->len; i++) {
        spec = g_array_index(spectra, GwySpectra*, i);
        if (spec)
            gwy_container_set_object(container, gwy_app_get_spectra_key_for_id(i), spec);
    }
    ok = TRUE;

fail:
    for (i = 0; i < spectra->len; i++) {
        spec = g_array_index(spectra, GwySpectra*, i);
        if (spec) {
            if ((d = g_object_get_data(G_OBJECT(spec), "q"))) {
                g_object_set_data(G_OBJECT(spec), "q", NULL);
                g_free(d);
            }
            g_object_unref(spec);
        }
    }
    g_array_free(spectra, TRUE);
    for (i = 0; i < segments->len; i++)
        g_free(g_array_index(segments, ShilpsSegment, i).name);
    g_array_free(segments, TRUE);
    g_free(plot_map);

    return ok ? graphs->len : -1;
}

/* XXX: This is what we do if we don't know which column should be the abscissa. Probably can happen only for
 * early stage (non-public) files or when something goes awry. Normal files should have the plot map.
 *
 * We give up and show point index on abscissa. The good part is that we do not have any consistency requirement on
 * the channels in individual points and can basically merge anything. */
static void
shilps_read_spec_fallback(hid_t file_id, GArray *spectra,
                          const gdouble *values, gint nrow, gint ncol,
                          gdouble x, gdouble y,
                          GString *str)
{
    GString *attrname;
    gint i, j;

    attrname = g_string_new(NULL);
    for (i = 0; i < ncol; i++) {
        gchar *name = NULL, *unit = NULL;
        GwySpectra *spec;
        GwyDataLine *line;
        gint power10;
        gdouble *d;
        gdouble q;

        /* Skip unnamed spectra as we have to way to identify them. */
        g_string_printf(attrname, "Column%u_Channel", i+1);
        if (!gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &name, NULL))
            continue;
        g_string_printf(attrname, "Column%u_Unit", i+1);
        gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &unit, NULL);

        gwy_debug("col[%u] name=%s, unit=%s", i, name, unit);
        for (j = 0; j < spectra->len; j++) {
            spec = g_array_index(spectra, GwySpectra*, j);
            if (gwy_strequal(gwy_spectra_get_title(spec), name))
                break;
        }
        if (j == spectra->len) {
            spec = gwy_spectra_new();
            if (name) {
                gwy_spectra_set_title(spec, name);
                gwy_spectra_set_spectrum_y_label(spec, name);
            }
            gwy_spectra_set_spectrum_x_label(spec, "index");
            gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spec), "m");
            g_array_append_val(spectra, spec);
        }

        line = gwy_data_line_new(nrow, nrow, FALSE);
        gwy_si_unit_set_from_string_parse(gwy_data_line_get_si_unit_y(line), unit, &power10);
        q = pow10(power10);

        d = gwy_data_line_get_data(line);
        for (j = 0; j < nrow; j++)
            d[j] = q*values[j*ncol + i];

        gwy_spectra_add_spectrum(spec, line, x, y);
        g_object_unref(line);

        g_free(name);
        g_free(unit);
    }
    g_string_free(attrname, TRUE);
}

static void
regularise_curve_sampling(const gdouble *xdata, const gdouble *ydata, gint n, gint stride,
                          gdouble qx, gdouble qy,
                          GwyDataLine *line)
{
    GwyDataLine *weight;
    gdouble xmin = G_MAXDOUBLE, xmax = -G_MAXDOUBLE;
    gdouble x, dx, s = 0.0;
    gdouble *d, *w;
    gint i, ix, res;

    for (i = 0; i < n; i++) {
        xmin = fmin(xmin, xdata[i*stride]);
        xmax = fmax(xmax, xdata[i*stride]);
    }
    xmin *= qx;
    xmax *= qx;

    if (!(xmax > xmin)) {
        res = 2;
        gwy_data_line_resample(line, res, GWY_INTERPOLATION_NONE);
        gwy_data_line_set_offset(line, xmin);
        gwy_data_line_set_real(line, xmin > 0.0 ? 0.1*xmin : 1.0);
        for (i = 0; i < n; i++)
            s += ydata[i*stride];
        d = gwy_data_line_get_data(line);
        d[0] = d[1] = qy*s/n;
        return;
    }

    res = n;
    dx = (xmax - xmin)/res;
    gwy_data_line_resample(line, res, GWY_INTERPOLATION_NONE);
    gwy_data_line_clear(line);
    gwy_data_line_set_real(line, xmax - xmin);
    gwy_data_line_set_offset(line, xmin);
    d = gwy_data_line_get_data(line);
    weight = gwy_data_line_new_alike(line, TRUE);
    w = gwy_data_line_get_data(weight);

    for (i = 0; i < n; i++) {
        x = (qx*xdata[i*stride] + 0.5*dx - xmin)/dx;
        ix = (gint)floor(x);
        ix = CLAMP(ix, 0, res-1);
        d[ix] += ydata[i*stride];
        w[ix]++;
    }

    for (i = 0; i < res; i++) {
        if (w[i] > 0.0) {
            d[i] *= qy/w[i];
            w[i] = 0.0;
        }
        else
            w[i] = 1.0;
    }

    gwy_data_line_correct_laplace(line, weight);

    g_object_unref(weight);
}

/* This is the usual code path. Plot the specified (absiccsa,ordinate) pairs and ignore anything else. */
static gboolean
shilps_read_spec_plotmap(hid_t file_id, GArray *spectra, GArray *segments,
                         const gdouble *values, gint nrow, gint ncol,
                         gdouble x, gdouble y,
                         const gint *plot_map, gint ngraphs,
                         GString *str,
                         G_GNUC_UNUSED GError **error)
{
    GString *attrname;
    gdouble qx, qy;
    gint start, end, i, j, k, nseg = segments->len ? segments->len : 1;

    attrname = g_string_new(NULL);
    for (i = 0; i < ngraphs; i++) {
        gint ia = plot_map[2*i], io = plot_map[2*i + 1];
        gchar *namea = NULL, *unita = NULL, *nameo = NULL, *unito = NULL;
        GwySpectra *spec;
        GwyDataLine *line;
        gint power10a, power10o;
        gdouble *d;
        gchar *title;

        if (ia > ncol || io > ncol) {
            g_warning("Too few spectrum columns (%d) for plot index %d", ncol, MAX(ia, io));
            continue;
        }

        for (j = 0; j < nseg; j++) {
            k = i*nseg + j;
            if (!(spec = g_array_index(spectra, GwySpectra*, k))) {
                g_string_printf(attrname, "Column%u_Channel", ia);
                gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &namea, NULL);
                g_string_printf(attrname, "Column%u_Unit", ia);
                gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &unita, NULL);
                g_string_printf(attrname, "Column%u_Channel", io);
                gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &nameo, NULL);
                g_string_printf(attrname, "Column%u_Unit", io);
                gwyhdf5_get_str_attr_g(file_id, str->str, attrname->str, &unito, NULL);

                gwy_debug("creating spectrum %d (%s vs. %s) from (%d, %d)", i, namea, nameo, ia, io);
                spec = gwy_spectra_new();
                title = g_strdup_printf("%s-%s%s%s",
                                        nameo ? nameo : _("Unknown"),
                                        namea ? namea : _("Unknown"),
                                        segments->len ? " " : "",
                                        segments->len ? g_array_index(segments, ShilpsSegment, j).name : "");
                gwy_spectra_set_title(spec, title);
                g_free(title);
                if (namea)
                    gwy_spectra_set_spectrum_x_label(spec, namea);
                if (nameo)
                    gwy_spectra_set_spectrum_y_label(spec, nameo);

                gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spec), "m");
                g_array_index(spectra, GwySpectra*, k) = spec;

                line = gwy_data_line_new(1, 1, FALSE);
                gwy_si_unit_set_from_string_parse(gwy_data_line_get_si_unit_x(line), unita, &power10a);
                gwy_si_unit_set_from_string_parse(gwy_data_line_get_si_unit_y(line), unito, &power10o);

                d = g_new(gdouble, 2);
                d[0] = qx = pow10(power10a);
                d[1] = qy = pow10(power10o);
                g_object_set_data(G_OBJECT(spec), "q", d);

                g_free(namea);
                g_free(nameo);
                g_free(unita);
                g_free(unito);
            }
            else {
                line = gwy_data_line_new_alike(gwy_spectra_get_spectrum(spec, 0), FALSE);
                d = (gdouble*)g_object_get_data(G_OBJECT(spec), "q");
                qx = d[0];
                qy = d[1];
            }

            if (segments->len) {
                start = g_array_index(segments, ShilpsSegment, j).start;
                end = g_array_index(segments, ShilpsSegment, j).end;
                start = CLAMP(start, 0, nrow-1);
                end = CLAMP(end, 1, nrow);
                regularise_curve_sampling(values + ia-1 + start*ncol, values + io-1 + start*ncol,
                                          end - start, ncol, qx, qy, line);
            }
            else
                regularise_curve_sampling(values + ia-1, values + io-1, nrow, ncol, qx, qy, line);
            gwy_spectra_add_spectrum(spec, line, x, y);
            g_object_unref(line);
        }
    }
    g_string_free(attrname, TRUE);

    return TRUE;
}

/* Read and parse curve segmentation given as an array of Name:start strings.
 * NB: We do not fill the last item of segmentation[] because we do not know the number of data points. The caller
 * has to do it afterwards. */
static gboolean
parse_curve_segmentation(hid_t file_id, const gchar *obj_path, const gchar *attr_name,
                         GArray *segments, GArray *tmpl,
                         GError **error)
{
    ShilpsSegment seg;
    hid_t attr;
    gchar **segnames = NULL;
    gchar *colon;
    gint i, nseg = -1;
    gboolean ok = FALSE;

    if ((attr = gwyhdf5_open_and_check_attr(file_id, obj_path, attr_name, H5T_STRING, 1, &nseg, error)) < 0)
        return FALSE;
    H5Aclose(attr);

    gwy_debug("nseg %d, tmpl nseg %d", nseg, tmpl ? (gint)tmpl->len : -1);
    if (tmpl && (guint)nseg != tmpl->len) {
        err_INCONSISTENT_SPECTRA(error);
        return FALSE;
    }

    segnames = g_new0(gchar*, nseg);
    if (!gwyhdf5_get_strs_attr(file_id, obj_path, attr_name, 1, &nseg, segnames, error)) {
        g_free(segnames);
        return FALSE;
    }

    for (i = 0; i < nseg; i++) {
        gwy_debug("segment spec[%d] %s", i, segnames[i]);
        if (!(colon = strchr(segnames[i], ':'))) {
            err_INVALID(error, attr_name);
            goto end;
        }
    }

    for (i = 0; i < nseg; i++) {
        colon = strchr(segnames[i], ':');
        *colon = '\0';
        if (tmpl && !gwy_strequal(segnames[i], g_array_index(tmpl, ShilpsSegment, i).name)) {
            err_INCONSISTENT_SPECTRA(error);
            goto end;
        }
        seg.name = segnames[i];
        seg.start = atoi(colon+1);
        if (i)
            g_array_index(segments, ShilpsSegment, i-1).end = seg.start;
        g_array_append_val(segments, seg);
        gwy_debug("segment spec[%d] %s, starts at %d", i, seg.name, seg.start);
    }

    /* Reallocate string using GLib to unify freeing. */
    for (i = 0; i < nseg; i++)
        g_array_index(segments, ShilpsSegment, i).name = g_strdup(g_array_index(segments, ShilpsSegment, i).name);
    ok = TRUE;

end:
    /* No need to free anything inside because if !ok we have not allocated it anew. */
    if (!ok)
        g_array_set_size(segments, 0);
    for (i = 0; i < nseg; i++)
        H5free_memory(segnames[i]);
    g_free(segnames);

    return ok;
}

static gboolean
shilps_read_graph_spec(hid_t file_id, gint id, const gint *plot_map, gint ngraphs,
                       GArray *spectra, GArray *segments, gboolean first_time,
                       GString *str, GError **error)
{
    gint res[2] = { -1, -1 };
    guint ncol, nrow;
    gdouble x, y;
    gdouble *values = NULL;
    GArray *this_segments = NULL;
    hid_t dataset;
    gint status;
    guint i;
    gboolean ok = FALSE;

    g_string_printf(str, "Session/Spectra/Graph%d", id);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 2, res, error)) < 0)
        return FALSE;

    nrow = res[0];
    ncol = res[1];
    gwy_debug("[%u]res rows=%u, cols=%u", id, nrow, ncol);
    if (err_DIMENSION(error, nrow) || err_DIMENSION(error, ncol))
        goto fail;

    if (!gwyhdf5_get_float_attr(file_id, str->str, "Point_X", &x, error)
        || !gwyhdf5_get_float_attr(file_id, str->str, "Point_Y", &y, error))
        goto fail;
    /* Micrometers. */
    x *= 1e-6;
    y *= 1e-6;
    gwy_debug("spectrum %d at (%g,%g)", id, x, y);

    if (first_time) {
        parse_curve_segmentation(file_id, str->str, "CurveSegments", segments, NULL, NULL);
        if (segments->len && plot_map)
            g_array_set_size(spectra, segments->len*spectra->len);
    }
    else {
        this_segments = g_array_new(FALSE, FALSE, sizeof(ShilpsSegment));
        parse_curve_segmentation(file_id, str->str, "CurveSegments", this_segments, segments, NULL);
        segments = this_segments;
    }
    if (segments->len)
        g_array_index(segments, ShilpsSegment, segments->len-1).end = nrow;

    values = g_new(gdouble, nrow*ncol);
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values);
    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        goto fail;
    }

    if (plot_map) {
        ok = shilps_read_spec_plotmap(file_id, spectra, segments, values, nrow, ncol, x, y, plot_map, ngraphs,
                                      str, error);
    }
    else {
        shilps_read_spec_fallback(file_id, spectra, values, nrow, ncol, x, y, str);
        ok = TRUE;
    }

fail:
    H5Dclose(dataset);
    g_free(values);
    if (this_segments) {
        for (i = 0; i < this_segments->len; i++)
            g_free(g_array_index(this_segments, ShilpsSegment, i).name);
        g_array_free(this_segments, TRUE);
    }

    return ok;
}

static gint
shilps_read_spectra(GwyContainer *container,
                    hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    //const gchar grouppfx[] = "Session/Curves";
    GArray *spectra = ghfile->lists[SHILPS_CURVES].idlist;
    GString *str = ghfile->buf;
    GwySpectra *spec;
    gchar *channel;
    guint i, id;

    gwy_debug("nspectra %u", spectra->len);
    if (!spectra->len)
        return 0;

    for (i = 0; i < spectra->len; i++) {
        id = g_array_index(spectra, gint, i);
        if (!(spec = shilps_read_curves_spec(file_id, id, str, error))) {
            return -1;
        }

        /* shilps_read_curves_spec() fills str->str with the correct prefix. */
        if (gwyhdf5_get_str_attr(file_id, str->str, "Channel", &channel, NULL)) {
            g_strstrip(channel);
            gwy_spectra_set_title(spec, channel);
            H5free_memory(channel);
        }

        gwy_container_pass_object(container, gwy_app_get_spectra_key_for_id(i), spec);
    }

    return spectra->len;
}

static GwySpectra*
shilps_read_curves_spec(hid_t file_id, gint id,
                        GString *str, GError **error)
{
    GwySpectra *spec = NULL;
    GwyDataLine *dline;
    hid_t dataset;
    gdouble *ydata, *cdata;
    gint i, npts, ncurves, power10;
    GwySIUnit *yunit;
    gint res[2] = { -1, -1 };
    gchar *yunitstr = NULL;
    gdouble q;
    herr_t status = -1;

    g_string_printf(str, "Session/Curves/Spectrum%d", id);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 2, res, error)) < 0)
        return NULL;
    gwy_debug("curve data dims ncurves=%d npts=%d", res[0], res[1]);
    ncurves = res[0];
    npts = res[1];
    if (err_DIMENSION(error, npts)) {
        H5Dclose(dataset);
        return NULL;
    }

    gwyhdf5_get_str_attr_g(file_id, str->str, "Units", &yunitstr, NULL);
    gwy_debug("data Units %s", yunitstr);

    /* We set units and point coordinates later using XYZ data. */
    yunit = gwy_si_unit_new_parse(yunitstr, &power10);
    g_free(yunitstr);
    q = pow10(power10);
    spec = gwy_spectra_new();

    status = 0;
    ydata = g_new(gdouble, npts*ncurves);
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, ydata);
    H5Dclose(dataset);

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        GWY_OBJECT_UNREF(spec);
    }
    else {
        for (i = 0; i < ncurves; i++) {
            dline = gwy_data_line_new(npts, npts, FALSE);
            cdata = gwy_data_line_get_data(dline);
            gwy_assign(cdata, ydata + i*npts, npts);
            gwy_data_line_multiply(dline, q);
            gwy_si_unit_assign(gwy_data_line_get_si_unit_y(dline), yunit);
            gwy_spectra_add_spectrum(spec, dline, i, 0.0);
            g_object_unref(dline);
        }
    }
    g_free(ydata);
    GWY_OBJECT_UNREF(yunit);

    return spec;
}

static gint
shilps_read_volumes(GwyContainer *container,
                    hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    const gchar grouppfx[] = "Session/Volume";
    GArray *volumes = ghfile->lists[SHILPS_VOLUME].idlist;
    GString *str = ghfile->buf;
    GwyBrick *brick;
    gint xres, yres, zres, power10;
    gdouble xreal, yreal, zreal;
    gchar *channel, *xyunitstr;
    GwyContainer *meta;
    guint i, id;

    gwy_debug("nvolumes %u", volumes->len);
    if (!volumes->len)
        return 0;

    if (!gwyhdf5_get_int_attr(file_id, grouppfx, "X No", &xres, error)
        || !gwyhdf5_get_int_attr(file_id, grouppfx, "Y No", &yres, error)
        || !gwyhdf5_get_int_attr(file_id, grouppfx, "Z No", &zres, error)
        || !gwyhdf5_get_float_attr(file_id, grouppfx, "X Range", &xreal, error)
        || !gwyhdf5_get_float_attr(file_id, grouppfx, "Y Range", &yreal, error)
        || !gwyhdf5_get_float_attr(file_id, grouppfx, "Z Range", &zreal, error))
        return -1;

    gwy_debug("xres %d, yres %d, zres %d", xres, yres, zres);
    if (err_DIMENSION(error, xres) || err_DIMENSION(error, yres) || err_DIMENSION(error, zres))
        return -1;

    gwy_debug("xreal %g, yreal %g, zreal %g", xreal, yreal, zreal);
    sanitise_real_size(&xreal, "x size");
    sanitise_real_size(&yreal, "y size");
    sanitise_real_size(&zreal, "z size");

    if (!gwyhdf5_get_str_attr_g(file_id, grouppfx, "Range Units", &xyunitstr, NULL))
        xyunitstr = g_strdup("µm");
    g_object_unref(gwy_si_unit_new_parse(xyunitstr, &power10));
    xreal *= pow10(power10);
    yreal *= pow10(power10);
    zreal *= pow10(power10);

    for (i = 0; i < volumes->len; i++) {
        id = g_array_index(volumes, gint, i);
        if (!(brick = shilps_read_brick(file_id, id, xres, yres, zres, xreal, yreal, zreal, xyunitstr, str, error))) {
            g_free(xyunitstr);
            return -1;
        }

        gwy_container_pass_object(container, gwy_app_get_brick_key_for_id(i), brick);

        /* shilps_read_brick() fills str->str with the correct prefix. */
        if (gwyhdf5_get_str_attr(file_id, str->str, "Channel", &channel, NULL)) {
            g_strstrip(channel);
            gwy_container_set_const_string(container, gwy_app_get_brick_title_key_for_id(i), channel);
            H5free_memory(channel);
        }

        meta = gwy_container_duplicate(ghfile->meta);
        gwy_container_pass_object(container, gwy_app_get_brick_meta_key_for_id(i), meta);
    }
    g_free(xyunitstr);

    return volumes->len;
}

static GwyBrick*
shilps_read_brick(hid_t file_id, gint id,
                  gint xres, gint yres, gint zres, gdouble xreal, gdouble yreal, gdouble zreal,
                  const gchar *xyunitstr,
                  GString *str, GError **error)
{
    GwyBrick *brick = NULL, *transposed;
    hid_t dataset;
    gint power10;
    /* The order seems a bit odd, but y is really the slowest varying index and z is the fast varying index within one
     * pixel. Unfortnately, we store volume data by plane so we have to transpose. */
    gint yxzres[3] = { yres, xres, zres };
    gchar *wunitstr = NULL;
    herr_t status = -1;

    g_string_printf(str, "Session/Volume/Image3d%d", id);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 3, yxzres, error)) < 0)
        return NULL;

    gwyhdf5_get_str_attr_g(file_id, str->str, "Units", &wunitstr, NULL);
    gwy_debug("Range units %s, data Units %s", xyunitstr, wunitstr);

    transposed = gwy_brick_new(zres, xres, yres, zreal, xreal, yreal, TRUE);
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, gwy_brick_get_data(transposed));
    H5Dclose(dataset);

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
    }
    else {
        brick = gwy_brick_new(xres, yres, zres, xreal, yreal, zreal, FALSE);
        gwy_brick_transpose(transposed, brick, GWY_BRICK_TRANSPOSE_ZXY, FALSE, FALSE, FALSE);
        gwy_si_unit_set_from_string(gwy_brick_get_si_unit_x(brick), xyunitstr);
        gwy_si_unit_set_from_string(gwy_brick_get_si_unit_y(brick), xyunitstr);
        gwy_si_unit_set_from_string(gwy_brick_get_si_unit_z(brick), xyunitstr);
        gwy_si_unit_set_from_string_parse(gwy_brick_get_si_unit_w(brick), wunitstr, &power10);
        gwy_brick_multiply(brick, pow10(power10));
    }
    g_object_unref(transposed);
    g_free(wunitstr);

    return brick;
}

static gint
shilps_read_xyzs(GwyContainer *container,
                 hid_t file_id, GwyHDF5File *ghfile, GError **error)
{
    const gchar grouppfx[] = "Session/XYZ";
    GArray *xyzs = ghfile->lists[SHILPS_XYZ].idlist;
    GString *str = ghfile->buf;
    GwySurface *surface;
    gchar *channel, *xyunitstr;
    GwyContainer *meta;
    guint i, id;

    gwy_debug("nxyzs %u", xyzs->len);
    if (!xyzs->len)
        return 0;

    /* XXX: In other groups it is called Range units. */
    if (!gwyhdf5_get_str_attr_g(file_id, grouppfx, "Units", &xyunitstr, NULL))
        xyunitstr = g_strdup("µm");

    for (i = 0; i < xyzs->len; i++) {
        id = g_array_index(xyzs, gint, i);
        if (!(surface = shilps_read_surface(file_id, id, xyunitstr, str, error))) {
            g_free(xyunitstr);
            return -1;
        }

        gwy_container_pass_object(container, gwy_app_get_surface_key_for_id(i), surface);

        /* shilps_read_surface() fills str->str with the correct prefix. */
        if (gwyhdf5_get_str_attr(file_id, str->str, "Channel", &channel, NULL)) {
            g_strstrip(channel);
            gwy_container_set_const_string(container, gwy_app_get_surface_title_key_for_id(i), channel);
            H5free_memory(channel);
        }
        else
            gwy_app_xyz_title_fall_back(container, i);

        meta = gwy_container_duplicate(ghfile->meta);
        gwy_container_pass_object(container, gwy_app_get_surface_meta_key_for_id(i), meta);
    }
    g_free(xyunitstr);

    return xyzs->len;
}

static GwySurface*
shilps_read_surface(hid_t file_id, gint id,
                    const gchar *xyunitstr,
                    GString *str, GError **error)
{
    GwySurface *surface = NULL;
    hid_t dataset;
    GwyXYZ *data;
    gint i, npts, power10xy, power10z;
    gint n3[2] = { -1, 3 };
    gchar *zunitstr = NULL;
    gdouble qxy, qz;
    herr_t status = -1;

    g_string_printf(str, "Session/XYZ/Scatter%d", id);
    if ((dataset = gwyhdf5_open_and_check_dataset(file_id, str->str, 2, n3, error)) < 0)
        return NULL;
    gwy_debug("xyz data dims %d %d", n3[0], n3[1]);
    npts = n3[0];
    /* In principle, we could also create empty surfaces even though they are weird. Let's just hope valid files do
     * not contain empty XYZ data sets. */
    g_return_val_if_fail(npts > 0, NULL);

    gwyhdf5_get_str_attr_g(file_id, str->str, "Units", &zunitstr, NULL);
    gwy_debug("Range units %s, data Units %s", xyunitstr, zunitstr);

    surface = gwy_surface_new_sized(npts);
    gwy_si_unit_set_from_string_parse(gwy_surface_get_si_unit_xy(surface), xyunitstr, &power10xy);
    gwy_si_unit_set_from_string_parse(gwy_surface_get_si_unit_z(surface), zunitstr, &power10z);
    g_free(zunitstr);

    status = 0;
    data = gwy_surface_get_data(surface);
    /* NB: Here we are silently typecasting array of GwyXYZ to doubles. */
    status = H5Dread(dataset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    H5Dclose(dataset);

    if (status < 0) {
        err_HDF5(error, "H5Dread", status);
        GWY_OBJECT_UNREF(surface);
    }
    else {
        qxy = pow10(power10xy);
        qz = pow10(power10z);
        for (i = 0; i < npts; i++) {
            data[i].x *= qxy;
            data[i].y *= qxy;
            data[i].z *= qz;
        }
    }

    return surface;
}

static void
shilps_set_spectra_coordinates(GwyContainer *container, gint nspec, gint nxyz)
{
    GwySpectra *spec;
    GwySurface *surface;
    const GwyXYZ *xyz;
    gint i, j, n, k;

    for (i = 0; i < nspec; i++) {
        spec = gwy_container_get_object(container, gwy_app_get_spectra_key_for_id(i));
        n = gwy_spectra_get_n_spectra(spec);
        for (j = 0; j < nxyz; j++) {
            surface = gwy_container_get_object(container, gwy_app_get_surface_key_for_id(j));
            if (gwy_surface_get_npoints(surface) == n) {
                gwy_debug("XYZ data #%d has the right number of points for spectra #%d", j, i);
                gwy_si_unit_assign(gwy_spectra_get_si_unit_xy(spec), gwy_surface_get_si_unit_xy(surface));
                xyz = gwy_surface_get_data_const(surface);
                for (k = 0; k < n; k++)
                    gwy_spectra_setpos(spec, k, xyz[k].x, xyz[k].y);
                break;
            }
        }
        if (j == nxyz) {
            gwy_debug("cannot find good XYZ data for spectra #%d; removing it", i);
            gwy_container_remove(container, gwy_app_get_spectra_key_for_id(i));
        }
    }
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
