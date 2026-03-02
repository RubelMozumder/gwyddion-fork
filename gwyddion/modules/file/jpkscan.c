/*
 *  $Id: jpkscan.c 26005 2023-11-09 13:17:31Z yeti-dn $
 *  Loader for JPK Image Scans.
 *  Copyright (C) 2005  JPK Instruments AG.
 *  Written by Sven Neumann <neumann@jpk.com>.
 *
 *  Rewritten to use GwyTIFF and spectra added by Yeti <yeti@gwyddion.net>.
 *  Copyright (C) 2009-2019 David Necas (Yeti).
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-jpk-image-scan">
 *   <comment>JPK image scan</comment>
 *   <magic priority="10">
 *     <match type="string" offset="0" value="MM\x00\x2a"/>
 *   </magic>
 *   <glob pattern="*.jpk"/>
 *   <glob pattern="*.JPK"/>
 *   <glob pattern="*.jpk-force"/>
 *   <glob pattern="*.JPK-FORCE"/>
 *   <glob pattern="*.jpk-force-map"/>
 *   <glob pattern="*.JPK-FORCE-MAP"/>
 *   <glob pattern="*.jpk-qi-image"/>
 *   <glob pattern="*.JPK-QI-IMAGE"/>
 *   <glob pattern="*.jpk-qi-data"/>
 *   <glob pattern="*.JPK-QI-DATA"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * JPK Instruments
 * .jpk, .jpk-qi-image, .jpk-force, .jpk-force-map, .jpk-qi-data
 * Read SPS Curvemap
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>
#include <libprocess/grains.h>
#include <libprocess/lawn.h>
#include <libgwydgets/gwygraphmodel.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include <app/wait.h>
#include "err.h"
#include "jpk.h"
#include "gwytiff.h"
#include "gwyzip.h"

#define MAGIC "PK\x03\x04"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define MAGIC_FORCE1 "segments/0"
#define MAGIC_FORCE1_SIZE (sizeof(MAGIC_FORCE1)-1)
#define MAGIC_FORCE2 "header.properties"
#define MAGIC_FORCE2_SIZE (sizeof(MAGIC_FORCE2)-1)

typedef enum {
    JPK_FORCE_UNKNOWN = 0,
    /* This is just some graphs (curves). */
    JPK_FORCE_CURVES,
    /* This includes coordinates that may or may not be on a regular grid. If they are not, we must load it as SPS.
     * Otherwise we can load it as a curve map. */
    JPK_FORCE_MAP,
    /* This is always on a fine grid and should be loaded as a curve map. */
    JPK_FORCE_QI,
} JPKForceFileType;

typedef struct {
    /* Segment header properties. */
    /* Concatenated data of all channels. */
    guint ndata;           /* Points per curve from settings.  The actual number of values measured can be smaller and
                              is stored in measured_ndata[] for each map point. */
    guint *measured_ndata;
    gdouble *data;
    const gchar **units;   /* For all channels */
    gchar *segment_style;  /* This is extend, retract, pause. */
    gchar *segment_type;   /* This is a more detailed type. */
    gchar *segment_name;
} JPKForceData;

typedef struct {
    const gchar *filename;

    GRegex *segment_regex;
    GRegex *index_regex;
    GRegex *index_segment_regex;
    GString *str;     /* General scratch buffer. */
    GString *sstr;    /* Inner scratch buffer for lookup_property(). */
    GString *qstr;    /* Inner scratch buffer for find_scaling_parameters(). */

    GHashTable *header_properties;
    GHashTable *shared_header_properties;
    JPKForceFileType type;
    guint nids;
    guint *ids;
    guint nsegs;
    guint npoints;      /* Number of positions (xy coordinates) */
    guint nchannels;
    gint height_cid;
    gchar **channel_names;
    gchar **pause_channels;    /* Scratch space for pause segment channels. */
    const gchar **default_cals;
    /* We have only data[nseg] (with all channels and points in one block). The most coarse index is map point, then
     * channel, then spectrum value. */
    JPKForceData *data;

    /* For maps/QI */
    guint xres;        /* Detected */
    guint yres;
    guint ilength;     /* Grid dimensions from the header */
    guint jlength;
    GwyXY *coordinates;
    GwyXY xyorigin;
    GwyXY xystep;
    gboolean *have_coordinates;
    guint *pointmap;   /* Image pixel index → file data point id (index). */
    guint imgid;       /* Next free image id. */

    /* The backend storage for all the hash tables.  We must keep buffers we created hashes from because the strings
     * point directly to the buffers. */
    GSList *buffers;
    GHashTable *last_hash;
} JPKForceFile;

static gboolean      module_register     (void);
static gint          jpkscan_detect      (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* jpkscan_load        (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static void          jpk_load_channel    (const GwyTIFF *tiff,
                                          GwyTIFFImageReader *reader,
                                          const gchar *filename,
                                          GwyContainer *container,
                                          GwyContainer *meta,
                                          guint idx,
                                          gdouble ulen,
                                          gdouble vlen);
static void          jpk_load_meta       (GwyTIFF *tiff,
                                          GwyContainer *container);
static void          jpk_load_meta_string(GwyTIFF *tiff,
                                          GwyContainer *container,
                                          guint tag,
                                          const gchar *name);
static void          jpk_load_meta_double(GwyTIFF *tiff,
                                          GwyContainer *container,
                                          guint tag,
                                          const gchar *unit,
                                          const gchar *name);
static void          meta_store_double   (GwyContainer *container,
                                          const gchar *name,
                                          gdouble value,
                                          const gchar *unit);

#ifdef HAVE_GWYZIP
static gint          jpkforce_detect              (const GwyFileDetectInfo *fileinfo,
                                                   gboolean only_name);
static GwyContainer* jpkforce_load                (const gchar *filename,
                                                   GwyRunType mode,
                                                   GError **error);
static void          read_embedded_image_file     (GwyContainer *container,
                                                   GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile);
static void          check_regular_grid           (JPKForceFile *jpkfile);
static guint         create_force_curves          (GwyContainer *container,
                                                   JPKForceFile *jpkfile);
static gboolean      read_curve_data              (GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile,
                                                   GError **error);
static guint         create_sps_data              (GwyContainer *container,
                                                   JPKForceFile *jpkfile,
                                                   GwySetFractionFunc set_fraction,
                                                   GError **error);
static guint         create_curve_map             (GwyContainer *container,
                                                   JPKForceFile *jpkfile,
                                                   GwySetFractionFunc set_fraction,
                                                   GError **error);
static gboolean      read_forcemap_data           (GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile,
                                                   GwySetFractionFunc set_fraction,
                                                   GError **error);
static gboolean      read_raw_data                (GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile,
                                                   JPKForceData *data,
                                                   GHashTable *hash,
                                                   const gchar *datatype,
                                                   guint ptid,
                                                   guint cid,
                                                   guint ndata,
                                                   GError **error);
static gboolean      read_computed_data           (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   JPKForceData *data,
                                                   const gchar *datatype,
                                                   guint ptid,
                                                   guint cid,
                                                   guint ndata,
                                                   GError **error);
static void          find_segment_settings        (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   guint sid);
static guint         find_segment_npoints         (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   GError **error);
static gchar*        find_sgement_name            (GHashTable *segment_properties,
                                                   GHashTable *shared_properties,
                                                   guint sid,
                                                   GString *str);
static gboolean      apply_default_channel_scaling(JPKForceFile *jpkfile,
                                                   JPKForceData *data,
                                                   GHashTable *header_properties,
                                                   guint cid,
                                                   gsize datablockoff);
static gboolean      find_scaling_parameters      (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   const gchar *subkey,
                                                   guint cid,
                                                   gdouble *multiplier,
                                                   gdouble *offset,
                                                   const gchar **unit,
                                                   gboolean ignore_missing);
static const gchar*  lookup_channel_property      (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   const gchar *subkey,
                                                   guint cid,
                                                   gboolean fail_if_not_found,
                                                   GError **error);
static const gchar*  lookup_property              (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   const gchar *key,
                                                   gboolean fail_if_not_found,
                                                   GError **error);
static gchar**       enumerate_channels_raw       (GHashTable *header_properties);
static gboolean      enumerate_channels           (JPKForceFile *jpkfile,
                                                   GHashTable *header_properties,
                                                   gboolean needslist,
                                                   GError **error);
static gboolean      analyse_segment_ids          (JPKForceFile *jpkfile,
                                                   GError **error);
static gboolean      analyse_map_segment_ids      (JPKForceFile *jpkfile,
                                                   GError **error);
static gboolean      scan_file_enumerate_segments (GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile,
                                                   GwySetMessageFunc set_fraction,
                                                   GError **error);
static GHashTable*   parse_header_properties      (GwyZipFile zipfile,
                                                   JPKForceFile *jpkfile,
                                                   GError **error);
static void          free_last_hash               (JPKForceFile *jpkfile);
static void          jpk_force_file_free          (JPKForceFile *jpkfile);
#endif

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    module_register,
    N_("Imports JPK image scans."),
    "Sven Neumann <neumann@jpk.com>, Yeti <yeti@gwyddion.net>",
    "0.16",
    "JPK Instruments AG, David Nečas (Yeti)",
    "2005-2007",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("jpkscan",
                           N_("JPK image scans (.jpk, .jpk-qi-image)"),
                           (GwyFileDetectFunc)&jpkscan_detect,
                           (GwyFileLoadFunc)&jpkscan_load,
                           NULL,
                           NULL);
#ifdef HAVE_GWYZIP
    gwy_file_func_register("jpkforce",
                           N_("JPK force curves (.jpk-force, .jpk-force-map, .jpk-qi-data)"),
                           (GwyFileDetectFunc)&jpkforce_detect,
                           (GwyFileLoadFunc)&jpkforce_load,
                           NULL,
                           NULL);
#endif

    return TRUE;
}

static gint
jpkscan_detect(const GwyFileDetectInfo *fileinfo, gboolean only_name)
{
    GwyTIFF *tiff;
    gdouble ulen, vlen;
    gchar *name = NULL;
    gint score = 0;
    guint byteorder = G_BIG_ENDIAN;
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;

    if (only_name)
        return score;

    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len, &version, &byteorder))
        return 0;

    if ((tiff = gwy_tiff_load(fileinfo->name, NULL))
        && gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_uLength, &ulen)
        && gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_vLength, &vlen)
        && ulen > 0.0
        && vlen > 0.0
        && (gwy_tiff_get_string0(tiff, JPK_TIFFTAG_ChannelFancyName, &name)
            || gwy_tiff_get_string0(tiff, JPK_TIFFTAG_Channel, &name)))
        score = 100;

    g_free(name);
    if (tiff)
        gwy_tiff_free(tiff);

    return score;
}

static GwyContainer*
jpkscan_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    GwyContainer *container = NULL;
    GwyContainer *meta = NULL;
    GwyTIFF *tiff;
    GwyTIFFImageReader *reader = NULL;
    GError *err = NULL;
    guint idx, photo;
    gdouble ulen, vlen;

    tiff = gwy_tiff_load(filename, error);
    if (!tiff)
        return NULL;

    /*  sanity check, grid dimensions must be present!  */
    if (!(gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_uLength, &ulen)
          && gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_vLength, &vlen))) {
        gwy_tiff_free(tiff);
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File does not contain grid dimensions."));
        return NULL;
    }

    sanitise_real_size(&ulen, "x size");
    sanitise_real_size(&vlen, "y size");

    container = gwy_container_new();
    meta = gwy_container_new();
    /* FIXME: I'm unable to meaningfully sort out the metadata to channels, so each one receives an identical copy of
     * the global metadata now. */
    jpk_load_meta(tiff, meta);

    gwy_debug("ulen: %g vlen: %g", ulen, vlen);

    for (idx = 0; idx < gwy_tiff_get_n_dirs(tiff); idx++) {
        reader = gwy_tiff_image_reader_free(reader);
        /* Request a reader, this ensures dimensions and stuff are defined. */
        reader = gwy_tiff_get_image_reader(tiff, idx, 1, &err);
        if (!reader) {
            /* 0th directory is usually thumbnail, do not complain about it. */
            if (idx > 0)
                g_warning("Ignoring directory %u: %s.", idx, err->message);
            g_clear_error(&err);
            continue;
        }

        if (!gwy_tiff_get_uint(tiff, idx, GWY_TIFFTAG_PHOTOMETRIC, &photo)) {
            g_warning("Could not get photometric tag, ignoring directory %u", idx);
            continue;
        }

        /*  we are only interested in 16bit and 32bit grayscale  */
        if (photo != GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK || photo != GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK
            || (reader->bits_per_sample != 16 && reader->bits_per_sample != 32))
            continue;

        jpk_load_channel(tiff, reader, filename, container, meta, idx, ulen, vlen);
    }

    gwy_tiff_image_reader_free(reader);
    gwy_tiff_free(tiff);
    g_object_unref(meta);

    if (!gwy_container_get_n_items(container)) {
        err_NO_DATA(error);
        GWY_OBJECT_UNREF(container);
    }

    return container;
}

/* FIXME: this function could use some sort of failure indication, if the file is damaged and no data field can be
 * loaded, suspicionless caller can return empty Container */
static void
jpk_load_channel(const GwyTIFF *tiff,
                 GwyTIFFImageReader *reader,
                 const gchar *filename,
                 GwyContainer *container,
                 GwyContainer *meta,
                 guint idx, gdouble ulen, gdouble vlen)
{
    GwyDataField *dfield;
    gdouble *data;
    gchar *channel;
    gchar *name = NULL;
    gchar *slot = NULL;
    gchar *unit = NULL;
    gboolean retrace = FALSE;
    gboolean reflect = FALSE;
    gdouble mult = 0.0;
    gdouble offset = 0.0;
    gint num_slots = 0;
    gint i, j, jj;

    gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_ChannelFancyName, &name);
    if (!name)
        gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_Channel, &name);
    g_return_if_fail(name != NULL);

    gwy_tiff_get_bool(tiff, idx, JPK_TIFFTAG_Channel_retrace, &retrace);
    channel = g_strdup_printf("%s%s", name, retrace ? " (retrace)" : "");
    g_free(name);
    gwy_debug("channel: %s", channel);

    gwy_tiff_get_sint(tiff, idx, JPK_TIFFTAG_NrOfSlots, &num_slots);
    g_return_if_fail(num_slots > 0);
    gwy_debug("num_slots: %d", num_slots);

    /* Locate the default slot */

    gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_DefaultSlot, &slot);
    g_return_if_fail(slot != NULL);
    gwy_debug("num_slots: %d, default slot: %s", num_slots, slot);

    for (i = 0; i < num_slots; i++) {
        gchar *string = NULL;

        if (gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_Slot_Name(i), &string)
            && string
            && gwy_strequal(string, slot)) {
            g_free(string);
            gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_Scaling_Type(i), &string);
            g_return_if_fail(gwy_strequal(string, "LinearScaling"));

            gwy_tiff_get_float(tiff, idx, JPK_TIFFTAG_Scaling_Multiply(i), &mult);
            gwy_tiff_get_float(tiff, idx, JPK_TIFFTAG_Scaling_Offset(i), &offset);
            gwy_debug("multipler: %g offset: %g", mult, offset);

            g_free(string);
            gwy_tiff_get_string(tiff, idx, JPK_TIFFTAG_Encoder_Unit(i), &unit);

            break;
        }
        g_free(string);
    }
    g_free(slot);

    /* Create a new data field */
    dfield = gwy_data_field_new(reader->width, reader->height, ulen, vlen, FALSE);

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    if (unit) {
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), unit);
        g_free(unit);
    }

    /* Read the scan data */
    gwy_tiff_get_bool(tiff, idx, JPK_TIFFTAG_Grid_Reflect, &reflect);
    data = gwy_data_field_get_data(dfield);

    for (j = 0; j < reader->height; j++) {
        jj = reflect ? j : reader->height-1 - j;
        gwy_tiff_read_image_row(tiff, reader, 0, j, mult, offset, data + jj*reader->width);
    }

    if (gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_x0, &offset))
        gwy_data_field_set_xoffset(dfield, offset);
    if (gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Grid_y0, &offset))
        gwy_data_field_set_yoffset(dfield, offset);

    /* Add the GwyDataField to the container */
    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(idx), dfield);
    gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(idx), channel);
    if (gwy_container_get_n_items(meta))
        gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(idx), gwy_container_duplicate(meta));
    gwy_file_channel_import_log_add(container, idx, NULL, filename);
}

static void
jpk_load_meta(GwyTIFF *tiff, GwyContainer *container)
{
    gchar *string;
    gdouble frequency;
    gdouble value;

    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_Name, "Name");
    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_Comment, "Comment");
    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_Sample, "Probe");
    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_AccountName, "Account");

    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_StartDate, "Time Start");
    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_EndDate, "Time End");

    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Grid_x0, "m", "Origin X");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Grid_y0, "m", "Origin Y");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Grid_uLength, "m", "Size X");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Grid_vLength, "m", "Size Y");

    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Scanrate_Dutycycle, NULL, "Duty Cycle");

    jpk_load_meta_string(tiff, container, JPK_TIFFTAG_Feedback_Mode, "Feedback Mode");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_iGain, "Hz", "Feedback IGain");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_pGain, NULL, "Feedback PGain");
    jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_Setpoint, "V", "Feedback Setpoint");

    /*  some values need special treatment  */

    if (gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Scanrate_Frequency, &frequency)
        && gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Scanrate_Dutycycle, &value)
        && value > 0.0) {
        meta_store_double(container, "Scan Rate", frequency/value, "Hz");
    }

    if (gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Feedback_iGain, &value))
        meta_store_double(container, "Feedback IGain", fabs(value), "Hz");

    if (gwy_tiff_get_float0(tiff, JPK_TIFFTAG_Feedback_pGain, &value))
        meta_store_double(container, "Feedback PGain", fabs(value), NULL);

    if (gwy_tiff_get_string0(tiff, JPK_TIFFTAG_Feedback_Mode, &string)) {
        if (gwy_strequal(string, "contact")) {
            jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_Baseline, "V", "Feedback Baseline");
        }
        else if (gwy_strequal(string, "intermittent")) {
            jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_Amplitude, "V", "Feedback Amplitude");
            jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_Frequency, "Hz", "Feedback Frequency");
            jpk_load_meta_double(tiff, container, JPK_TIFFTAG_Feedback_Phaseshift, "deg", "Feedback Phaseshift");
        }
        g_free(string);
    }
}

static void
jpk_load_meta_string(GwyTIFF *tiff,
                      GwyContainer *container, guint tag, const gchar *name)
{
    gchar *string;

    if (gwy_tiff_get_string0(tiff, tag, &string))
        gwy_container_set_string_by_name(container, name, string);
}

static void
jpk_load_meta_double(GwyTIFF *tiff,
                      GwyContainer *container,
                      guint tag, const gchar *unit, const gchar *name)
{
    gdouble value;

    if (gwy_tiff_get_float0(tiff, tag, &value))
        meta_store_double(container, name, value, unit);
}

static void
meta_store_double(GwyContainer *container,
                  const gchar *name, gdouble value, const gchar *unit)
{
    GwySIUnit *siunit = gwy_si_unit_new(unit);
    GwySIValueFormat *format = gwy_si_unit_get_format(siunit, GWY_SI_UNIT_FORMAT_MARKUP, value, NULL);

    gwy_container_set_string_by_name(container, name,
                                     g_strdup_printf("%5.3f %s", value/format->magnitude, format->units));
    g_object_unref(siunit);
    gwy_si_unit_value_format_free(format);
}

#ifdef HAVE_GWYZIP
static gint
jpkforce_detect(const GwyFileDetectInfo *fileinfo,
                gboolean only_name)
{
    GwyZipFile zipfile;
    guchar *content;
    gint score = 0;

    if (only_name)
        return 0;

    /* Generic ZIP file. */
    if (fileinfo->file_size < MAGIC_SIZE || memcmp(fileinfo->head, MAGIC, MAGIC_SIZE) != 0)
        return 0;

    /* It contains segments/0 (possibly under index) and header.properties (possibly also inside something). */
    if ((!gwy_memmem(fileinfo->head, fileinfo->buffer_len, MAGIC_FORCE1, MAGIC_FORCE1_SIZE)
         && gwy_memmem(fileinfo->tail, fileinfo->buffer_len, MAGIC_FORCE1, MAGIC_FORCE1_SIZE))
        || (!gwy_memmem(fileinfo->head, fileinfo->buffer_len, MAGIC_FORCE2, MAGIC_FORCE2_SIZE)
            && !gwy_memmem(fileinfo->head, fileinfo->buffer_len, MAGIC_FORCE2, MAGIC_FORCE2_SIZE)))
        return 0;

    /* Look inside if there is header.properties in the main directory. */
    if ((zipfile = gwyzip_open(fileinfo->name, NULL))) {
        if (gwyzip_locate_file(zipfile, "header.properties", 1, NULL)
            && (content = gwyzip_get_file_content(zipfile, NULL, NULL))) {
            if (g_strstr_len(content, 4096, "jpk-data-file"))
                score = 100;
            g_free(content);
        }
        gwyzip_close(zipfile);
    }

    return score;
}

static GwyContainer*
jpkforce_load(const gchar *filename,
              GwyRunType mode,
              GError **error)
{
    GwyContainer *container = NULL;
    JPKForceFile jpkfile;
    GwyZipFile zipfile;
    gboolean waiting = FALSE;

    gwy_debug("open file");
    if (!(zipfile = gwyzip_open(filename, error)))
        return NULL;

    gwy_clear(&jpkfile, 1);

    jpkfile.height_cid = -1;
    jpkfile.str = g_string_new(NULL);
    jpkfile.sstr = g_string_new(NULL);
    jpkfile.qstr = g_string_new(NULL);
    jpkfile.segment_regex = g_regex_new("^segments/([0-9]+)/(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
    jpkfile.index_regex = g_regex_new("^index/([0-9]+)/(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
    jpkfile.index_segment_regex = g_regex_new("^index/([0-9]+)/segments/([0-9]+)/(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
    jpkfile.filename = filename;

    gwy_debug("starting scanning");
    if (mode == GWY_RUN_INTERACTIVE) {
        g_string_printf(jpkfile.str, _("Scanning file (%u curves)..."), 0);
        gwy_app_wait_start(NULL, jpkfile.str->str);
        waiting = TRUE;
    }
    if (!scan_file_enumerate_segments(zipfile, &jpkfile, waiting ? gwy_app_wait_set_message : NULL, error))
        goto fail;

    if (jpkfile.type == JPK_FORCE_CURVES) {
        if (!analyse_segment_ids(&jpkfile, error))
            goto fail;
    }
    else if (jpkfile.type == JPK_FORCE_MAP) {
        /* The image file should be near the beginning so hopefully this locate-file operation does not take several
         * seconds. */
        if (gwyzip_locate_file(zipfile, "data-image.jpk-qi-image", TRUE, NULL))
            jpkfile.type = JPK_FORCE_QI;
        if (!analyse_map_segment_ids(&jpkfile, error))
            goto fail;
    }
    else {
        g_assert_not_reached();
    }

    if (!enumerate_channels(&jpkfile, jpkfile.shared_header_properties, FALSE, error))
        goto fail;

    jpkfile.data = g_new0(JPKForceData, jpkfile.nsegs);
    if (jpkfile.type == JPK_FORCE_CURVES) {
        if (!read_curve_data(zipfile, &jpkfile, error))
            goto fail;
        container = gwy_container_new();
        create_force_curves(container, &jpkfile);
    }
    else {
        jpkfile.coordinates = g_new(GwyXY, jpkfile.npoints);
        jpkfile.have_coordinates = g_new0(gboolean, jpkfile.npoints);
        if (waiting) {
            if (!gwy_app_wait_set_message(_("Reading files...")) || !gwy_app_wait_set_fraction(0.0)) {
                err_CANCELLED(error);
                goto fail;
            }
        }
        if (!read_forcemap_data(zipfile, &jpkfile, waiting ? gwy_app_wait_set_fraction : NULL, error))
            goto fail;

        check_regular_grid(&jpkfile);
        container = gwy_container_new();
        read_embedded_image_file(container, zipfile, &jpkfile);

        if (jpkfile.pointmap) {
            /* Regular grid with complete rows. */
            if (waiting) {
                if (!gwy_app_wait_set_message(_("Creating volume data...")) || !gwy_app_wait_set_fraction(0.0)) {
                    err_CANCELLED(error);
                    GWY_OBJECT_UNREF(container);
                    goto fail;
                }
            }
            if (!create_curve_map(container, &jpkfile, waiting ? gwy_app_wait_set_fraction : NULL, error)) {
                GWY_OBJECT_UNREF(container);
                goto fail;
            }
        }
        else {
            if (!create_sps_data(container, &jpkfile, waiting ? gwy_app_wait_set_fraction : NULL, error)) {
                GWY_OBJECT_UNREF(container);
                goto fail;
            }
        }
    }

    if (!gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        err_NO_DATA(error);
        goto fail;
    }

fail:
    gwyzip_close(zipfile);
    jpk_force_file_free(&jpkfile);
    if (waiting)
        gwy_app_wait_finish();

    return container;
}

/* Extract the embedded image to a temporary file and use jpkscan_load() to load it.  Do not complain if something
 * goes wrong.  We take the embedded image as a bonus if we can load it. */
static void
read_embedded_image_file(GwyContainer *container,
                         GwyZipFile zipfile, JPKForceFile *jpkfile)
{
    GwyContainer *embcontainer;
    guchar *content = NULL, *p;
    gchar *filename = NULL;
    gsize size;
    gssize bytes_written;
    gint *ids;
    guint i;
    gint fd;

    if (!gwyzip_locate_file(zipfile, "data-image.jpk-qi-image", TRUE, NULL))
        return;

    if (!(content = gwyzip_get_file_content(zipfile, &size, NULL)))
        return;

    fd = g_file_open_tmp("gwyddion-jpkscan-XXXXXX.jpk-qi-image", &filename, NULL);
    if (fd == -1)
        goto fail;

    p = content;
    while (size) {
        bytes_written = write(fd, p, size);
        if (bytes_written <= 0) {
            /* We might want to try again when we get zero written bytes or an error such as EAGAIN, EWOULDBLOCK or
             * EINTR.  But in this context, screw it. */
            close(fd);
            goto fail;
        }
        p += bytes_written;
        size -= bytes_written;
    }

    embcontainer = jpkscan_load(filename, GWY_RUN_NONINTERACTIVE, NULL);
    close(fd);
    if (embcontainer) {
        gwy_container_transfer(embcontainer, container, "/", "/", FALSE);
        g_object_unref(embcontainer);

        ids = gwy_app_data_browser_get_data_ids(container);
        for (i = 0; ids[i] != -1; i++) {
            if (ids[i] >= jpkfile->imgid)
                jpkfile->imgid = ids[i]+1;
        }
        g_free(ids);
    }

fail:
    if (filename) {
        g_unlink(filename);
        g_free(filename);
    }
    g_free(content);
}


static void
check_regular_grid(JPKForceFile *jpkfile)
{
    guint npoints = jpkfile->npoints;
    guint *pointmap;

    if (!npoints)
        return;

    pointmap = gwy_check_regular_2d_grid((gdouble*)jpkfile->coordinates, 2, npoints, -1.0,
                                         &jpkfile->xres, &jpkfile->yres,
                                         &jpkfile->xyorigin, &jpkfile->xystep);
    gwy_debug("first attempt %p", pointmap);
    if (pointmap) {
        jpkfile->pointmap = pointmap;
        return;
    }

    if (jpkfile->ilength < 2 || jpkfile->jlength < 2)
        return;
    if (jpkfile->ilength*jpkfile->jlength == npoints)
        return;
    if (npoints < jpkfile->jlength)
        return;

    /* For an incomplete measurement, try cutting it to full rows.  If we still do not get a regular grid just give
     * up. */
    npoints = (npoints/jpkfile->jlength)*jpkfile->jlength;
    pointmap = gwy_check_regular_2d_grid((gdouble*)jpkfile->coordinates, 2, npoints, -1.0,
                                         &jpkfile->xres, &jpkfile->yres,
                                         &jpkfile->xyorigin, &jpkfile->xystep);
    gwy_debug("second attempt %p", pointmap);
    if (pointmap) {
        jpkfile->pointmap = pointmap;
        jpkfile->npoints = npoints;
        return;
    }
}

static gboolean
err_IRREGULAR_NUMBERING(GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Non-uniform point and/or segment numbering is not supported."));
    return FALSE;
}

static gboolean
err_NONUNIFORM_CHANNELS(GError **error)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Non-uniform channel lists are not supported."));
    return FALSE;
}

static gboolean
err_DATA_FILE_NAME(GError **error, const gchar *expected, const gchar *found)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                _("Data file %s was found instead of expected %s."),
                found, expected);
    return FALSE;
}

static int
compare_uint2(gconstpointer a, gconstpointer b)
{
    const guint ia = *(const guint*)a;
    const guint ib = *(const guint*)b;
    const guint iaa = *((const guint*)a + 1);
    const guint ibb = *((const guint*)b + 1);

    if (ia < ib)
        return -1;
    if (ia > ib)
        return 1;
    if (iaa < ibb)
        return -1;
    if (iaa > ibb)
        return 1;
    return 0;
}

static gchar*
match_segment_or_index_filename(const gchar *filename, GRegex *regex, gint *id)
{
    GMatchInfo *info;
    gchar *s;

    if (!g_regex_match(regex, filename, 0, NULL))
        return NULL;

    g_regex_match(regex, filename, 0, &info);

    s = g_match_info_fetch(info, 1);
    *id = atoi(s);
    g_free(s);

    s = g_match_info_fetch(info, 2);
    g_match_info_free(info);
    return s;
}

static gchar*
match_map_segment_filename(const gchar *filename, GRegex *regex,
                           gint *id1, gint *id2)
{
    GMatchInfo *info;
    gchar *s;

    if (!g_regex_match(regex, filename, 0, NULL))
        return NULL;

    g_regex_match(regex, filename, 0, &info);

    s = g_match_info_fetch(info, 1);
    *id1 = atoi(s);
    g_free(s);

    s = g_match_info_fetch(info, 2);
    *id2 = atoi(s);
    g_free(s);

    s = g_match_info_fetch(info, 3);
    g_match_info_free(info);
    return s;
}

static guint
create_force_curves(GwyContainer *container, JPKForceFile *jpkfile)
{
    gint id, cid, height_cid = jpkfile->height_cid;
    guint ngraphs = 0, i, ndata;

    g_return_val_if_fail(height_cid >= 0 && height_cid < jpkfile->nchannels, 0);

    for (cid = 0; cid < jpkfile->nchannels; cid++) {
        GwyGraphModel *gmodel;
        GwySIUnit *xunit, *yunit;
        GQuark key;

        if (cid == height_cid)
            continue;

        gmodel = gwy_graph_model_new();
        i = 0;
        for (id = 0; id < jpkfile->nsegs; id++) {
            GwyGraphCurveModel *gcmodel;
            JPKForceData *data = jpkfile->data + id;
            const gdouble *xdata, *ydata;

            if (gwy_strequal(data->segment_style, "pause"))
                continue;

            ndata = data->ndata;
            xdata = data->data + height_cid * ndata;
            ydata = data->data + cid * ndata;
            gcmodel = gwy_graph_curve_model_new();
            gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, ndata);
            gwy_graph_curve_model_enforce_order(gcmodel);
            g_object_set(gcmodel,
                         "mode", GWY_GRAPH_CURVE_LINE,
                         "color", gwy_graph_get_preset_color(i++),
                         "description", data->segment_name,
                         NULL);
            gwy_graph_model_add_curve(gmodel, gcmodel);
            g_object_unref(gcmodel);
        }

        if (gwy_graph_model_get_n_curves(gmodel)) {
            xunit = gwy_si_unit_new(jpkfile->data[0].units[height_cid]);
            yunit = gwy_si_unit_new(jpkfile->data[0].units[cid]);
            g_object_set(gmodel,
                         "title", jpkfile->channel_names[cid],
                         "si-unit-x", xunit,
                         "si-unit-y", yunit,
                         "axis-label-bottom", jpkfile->default_cals[height_cid],
                         "axis-label-left", jpkfile->default_cals[cid],
                         NULL);
            g_object_unref(yunit);
            g_object_unref(xunit);

            key = gwy_app_get_graph_key_for_id(ngraphs++);
            gwy_container_set_object(container, key, gmodel);
        }
        g_object_unref(gmodel);
    }

    return ngraphs;
}

/* Expect the files in order.  We could read everything into memory first but that would be insane for QI. */
static gboolean
read_curve_data(GwyZipFile zipfile, JPKForceFile *jpkfile, GError **error)
{
    JPKForceData *data;
    GString *str = jpkfile->str;
    guint sid;

    if (!gwyzip_first_file(zipfile, error))
        return FALSE;

    if (jpkfile->shared_header_properties) {
        for (sid = 0; sid < jpkfile->nsegs; sid++)
            find_segment_settings(jpkfile, jpkfile->shared_header_properties, sid);
    }

    do {
        gchar *filename = NULL, *suffix = NULL;
        GHashTable *hash;
        guint cid, ndata, nchannels;
        gboolean ok, is_pause = FALSE;

        if (jpkfile->pause_channels) {
            g_strfreev(jpkfile->pause_channels);
            jpkfile->pause_channels = NULL;
        }

        if (!gwyzip_get_current_filename(zipfile, &filename, error))
            return FALSE;

        /* Find the header. */
        suffix = match_segment_or_index_filename(filename, jpkfile->segment_regex, &sid);
        g_free(filename);

        if (!suffix)
            continue;
        ok = gwy_strequal(suffix, "segment-header.properties");
        g_free(suffix);
        if (!ok)
            continue;

        g_assert(sid <= jpkfile->nsegs);
        data = jpkfile->data + sid;

        if (!(hash = parse_header_properties(zipfile, jpkfile, error)))
            return FALSE;

        find_segment_settings(jpkfile, hash, sid);
        is_pause = (data->segment_style && gwy_strequal(data->segment_style, "pause"));

        if (!enumerate_channels(jpkfile, hash, TRUE, error)) {
            /* XXX: Pause segments can have different channels.  Since we ignore them anyway, try not to fail when
             * they do not match the other segments. */
            if (!is_pause || !(jpkfile->pause_channels = enumerate_channels_raw(hash)))
                return FALSE;
            g_clear_error(error);
            nchannels = g_strv_length(jpkfile->pause_channels);
        }
        else
            nchannels = jpkfile->nchannels;

        /* A segment many not have numpoints if data were not collected. But for single curves this means a bad file
         * anyway.  */
        if (is_pause)
            ndata = 1;
        else {
            if (!(ndata = find_segment_npoints(jpkfile, hash, error)))
                return FALSE;

            gwy_debug("%u, npts = %u", sid, ndata);
            if (data->ndata && ndata != data->ndata) {
                /* Can this happen for non-maps? */
                gwy_debug("number of measured data differs from settings");
            }
        }
        /* Anyway, we have just a single curve set so let the segment header
         * override any shared settings. */
        data->ndata = ndata;
        data->data = g_new(gdouble, ndata*jpkfile->nchannels);
        data->units = g_new0(const gchar*, jpkfile->nchannels);

        /* Expect corresponding data files next. */
        for (cid = 0; cid < nchannels; cid++) {
            const gchar *datafilename, *datatype;

            if (!(datatype = lookup_channel_property(jpkfile, hash, "type", cid, TRUE, error)))
                return FALSE;

            /* Handle computed data.  There is no corresponding file. */
            if (gwy_stramong(datatype, "constant-data", "raster-data", NULL)) {
                if (is_pause)
                    continue;
                if (!read_computed_data(jpkfile, hash, data, datatype, 0, cid, ndata, error))
                    return FALSE;
                continue;
            }

            /* Otherwise we have actual data and expect a file name. */
            if (!gwyzip_next_file(zipfile, error))
                return FALSE;

            if (!(datafilename = lookup_channel_property(jpkfile, hash, "file.name", cid, TRUE, error)))
                return FALSE;

            g_string_printf(str, "segments/%u/%s", sid, datafilename);
            if (!gwyzip_get_current_filename(zipfile, &filename, error))
                return FALSE;
            gwy_debug("expecting file <%s>, found <%s>", str->str, filename);
            if (!gwy_strequal(filename, str->str)) {
                err_DATA_FILE_NAME(error, str->str, filename);
                g_free(filename);
                return FALSE;
            }
            g_free(filename);

            /* Read the data, unless it is a pause segment, then do not
             * bother. */
            if (!is_pause) {
                if (!read_raw_data(zipfile, jpkfile, data, hash, datatype, 0, cid, ndata, error))
                    return FALSE;
                apply_default_channel_scaling(jpkfile, data, hash, cid, cid*ndata);
            }
        }

        free_last_hash(jpkfile);

    } while (gwyzip_next_file(zipfile, NULL));

    return TRUE;
}

static gboolean
analyse_height_channel_range(JPKForceData *data, const guint *pointmap,
                             guint nchannels, guint npoints,
                             gint height_cid,
                             GwyDataField *min_field,
                             GwyDataField *range_field,
                             GwyDataField *mask)
{
    guint ij, k, ndata = data->ndata, measured_ndata, np;
    gdouble *dm = gwy_data_field_get_data(min_field);
    gdouble *dr = gwy_data_field_get_data(range_field);
    gdouble *m = gwy_data_field_get_data(mask);

    np = 0;
    /* ij indexes image points (so the fields are already unshuffled); we then analyse the curve at pointmap[ij] */
    for (ij = 0; ij < npoints; ij++) {
        gdouble zmin = G_MAXDOUBLE, zmax = -G_MAXDOUBLE;
        const gdouble *ptdata;

        ptdata = data->data + ndata*(height_cid + pointmap[ij]*nchannels);
        measured_ndata = data->measured_ndata[pointmap[ij]];
        if (measured_ndata >= 2) {
            for (k = 0; k < measured_ndata; k++) {
                gdouble z = ptdata[k];
                if (z < zmin)
                    zmin = z;
                if (z > zmax)
                    zmax = z;
            }
            dm[ij] = zmin;
            dr[ij] = zmax - zmin;
            m[ij] = 1.0;
            np++;
        }
        else {
            dm[ij] = dr[ij] = m[ij] = 0.0;
        }
    }
    if (!np)
        return FALSE;

    return TRUE;
}

/* Enforce interpolation to regular z because there is, in principle, no guarantee the z values in individual spectra
 * are compatible in any manner.  But we have shared z.  This can cause some information loss... */
static void
rasterise_spectrum_curve(GwyXY *data, guint ndata,
                         const gdouble *abscissa, gdouble *out,
                         guint nout, guint outstride)
{
    guint i, j;

    g_assert(ndata > 0);

    qsort(data, ndata, sizeof(GwyXY), gwy_compare_double);

    /* Fill the leading segment before the data start with the first value. This should not happen except for rounding
     * errors. */
    i = j = 0;
    while (i < nout && abscissa[i] <= data[j].x) {
        *out = data[j].y;
        out += outstride;
        i++;
    }

    /* Interpolate until we reach the end of available values or fill the entire output array. */
    while (i < nout && j < ndata-1) {
        /* Invariant: abscissa[i] >= data[j].x */
        if (abscissa[i] == data[j].x)
            *out = data[j].y;
        else {
            gdouble d = data[j+1].x - data[j].x;
            if (G_LIKELY(d > 0.0)) {
                gdouble t = (abscissa[i] - data[j].x)/d;
                *out = t*data[j+1].y + (1.0 - t)*data[j].y;
            }
            else
                *out = data[j].y;
        }
        out += outstride;
        i++;
        /* Possibly move forward in input, preserving the invariant. */
        while (i < nout && j < ndata-1 && abscissa[i] > data[j+1].x)
            j++;
    }

    /* There may be a trailing segment after the last input value because the curve shorter.  In this case we must
     * have j == ndata-1.  Filling with the last value seems reasonable. */
    while (i < nout) {
        *out = data[ndata-1].y;
        out += outstride;
        i++;
    }
}

static guint
create_curve_map(GwyContainer *container, JPKForceFile *jpkfile,
                 GwySetFractionFunc set_fraction,
                 GError **error)
{
    guint sid, cid, height_cid = jpkfile->height_cid;
    guint kpt, ij, nchannels = jpkfile->nchannels, nsegs = jpkfile->nsegs;
    guint xres = jpkfile->xres, yres = jpkfile->yres;
    guint blocklen;
    GwyLawn *lawn;
    const gdouble *segdata;
    GArray *curvesdata;
    gint *segments;

    g_return_val_if_fail(height_cid < jpkfile->nchannels, 0);

    lawn = gwy_lawn_new(xres, yres, jpkfile->xystep.x*xres, jpkfile->xystep.y*yres, nchannels, nsegs);
    gwy_si_unit_set_from_string(gwy_lawn_get_si_unit_xy(lawn), "m");
    gwy_lawn_set_xoffset(lawn, jpkfile->xyorigin.x);
    gwy_lawn_set_yoffset(lawn, jpkfile->xyorigin.y);

    curvesdata = g_array_new(FALSE, FALSE, sizeof(gdouble));
    segments = g_new(gint, 2*nsegs);

    /* Use the first force data set for units.
     * FIXME: Do we ensure the segments have uniform channel sets somewhere? We have just one set of channel names,
     * but each JPKForceData has its own set of channel units. */
    for (cid = 0; cid < nchannels; cid++) {
        gwy_si_unit_set_from_string(gwy_lawn_get_si_unit_curve(lawn, cid), jpkfile->data->units[cid]);
        gwy_lawn_set_curve_label(lawn, cid, jpkfile->channel_names[cid]);
    }
    for (sid = 0; sid < nsegs; sid++)
        gwy_lawn_set_segment_label(lawn, sid, jpkfile->data[sid].segment_name);

    for (ij = 0; ij < xres*yres; ij++) {
        g_array_set_size(curvesdata, 0);
        kpt = jpkfile->pointmap[ij];

        /* Gather data of all curves for the pixel. */
        for (cid = 0; cid < nchannels; cid++) {
            /* Gather data of all segments. */
            for (sid = 0; sid < nsegs; sid++) {
                JPKForceData *data = jpkfile->data + sid;

                segdata = data->data + data->ndata*(cid + kpt*nchannels);
                blocklen = data->measured_ndata[kpt];
                if (!cid) {
                    segments[2*sid] = curvesdata->len/2;
                    segments[2*sid + 1] = (curvesdata->len + blocklen)/2;
                }
                g_array_append_vals(curvesdata, segdata, blocklen);
            }
        }
        gwy_lawn_set_curves(lawn, ij % xres, ij/xres,
                            curvesdata->len/nchannels, &g_array_index(curvesdata, gdouble, 0),
                            segments);

        if (set_fraction && ij % 400 == 0) {
            if (!set_fraction(ij/(gdouble)(xres*yres))) {
                err_CANCELLED(error);
                GWY_OBJECT_UNREF(lawn);
                goto fail;
            }
        }
    }

    gwy_container_pass_object(container, gwy_app_get_lawn_key_for_id(0), lawn);

fail:
    g_free(segments);
    g_array_free(curvesdata, TRUE);

    return !!lawn;
}

static guint
create_sps_data(GwyContainer *container, JPKForceFile *jpkfile,
                GwySetFractionFunc set_fraction,
                GError **error)
{
    guint sid, cid, height_cid = jpkfile->height_cid;
    guint nspec, ij, k, nchannels = jpkfile->nchannels;
    guint xres = jpkfile->xres, yres = jpkfile->yres;
    guint noutdata, ndata;
    GwyDataField *min_field, *range_field, *mask;
    gdouble *abscissa = NULL;
    gdouble progres_denom, zmin, zrange;
    GwyXY *curve = NULL;

    g_return_val_if_fail(height_cid < jpkfile->nchannels, 0);

    min_field = gwy_data_field_new(xres, yres, jpkfile->xystep.x*xres, jpkfile->xystep.y*yres, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(min_field), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(min_field), "m");
    range_field = gwy_data_field_new_alike(min_field, FALSE);
    mask = gwy_data_field_new_alike(min_field, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(mask), NULL);

    /* FIXME: Pessimistic.  But that is probably better than the opposite. */
    progres_denom = jpkfile->nsegs*(nchannels - 1.0)*xres*yres;

    nspec = 0;
    for (sid = 0; sid < jpkfile->nsegs; sid++) {
        JPKForceData *data = jpkfile->data + sid;
        gdouble hstep;

        if (gwy_strequal(data->segment_style, "pause"))
            continue;

        if (!analyse_height_channel_range(data, jpkfile->pointmap, nchannels, xres*yres, height_cid,
                                          min_field, range_field, mask)) {
            g_warning("No curves with reasonable number of points found for segment %u.", sid);
            continue;
        }

        ndata = data->ndata;
        hstep = gwy_data_field_area_get_median(range_field, mask, 0, 0, xres, yres)/ndata;
        gwy_data_field_area_get_min_max(range_field, mask, 0, 0, xres, yres, NULL, &zrange);
        noutdata = GWY_ROUND(zrange/hstep);
        noutdata = MIN(noutdata, 2*ndata);

        curve = g_renew(GwyXY, curve, noutdata);
        abscissa = g_renew(gdouble, abscissa, noutdata);
        for (k = 0; k < noutdata; k++)
            abscissa[k] = k/(noutdata-1.0) * zrange;

        for (cid = 0; cid < nchannels; cid++) {
            GwySpectra *spectra;
            GwyDataLine *sps;
            gchar *title;

            if (cid == height_cid)
                continue;

            spectra = gwy_spectra_new();
            /* ij indexes image points; we then extract the curve at pointmap[ij] */
            for (ij = 0; ij < xres*yres; ij++) {
                const gdouble *zdata, *wdata;
                gdouble *sdata;
                guint measured_ndata, kpt;

                kpt = jpkfile->pointmap[ij];
                zdata = data->data + ndata*(height_cid + kpt*nchannels);
                wdata = data->data + ndata*(cid + kpt*nchannels);
                measured_ndata = data->measured_ndata[kpt];

                if (measured_ndata < 3)
                    continue;

                zmin = min_field->data[ij];
                for (k = 0; k < measured_ndata; k++) {
                    curve[k].x = zdata[k] - zmin;
                    curve[k].y = wdata[k];
                }
                sps = gwy_data_line_new(measured_ndata, zrange*measured_ndata/(noutdata - 1.0), FALSE);
                gwy_data_line_set_offset(sps, zmin);
                gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_x(sps), data->units[height_cid]);
                gwy_si_unit_set_from_string(gwy_data_line_get_si_unit_y(sps), data->units[cid]);

                sdata = gwy_data_line_get_data(sps);
                rasterise_spectrum_curve(curve, measured_ndata, abscissa, sdata, measured_ndata, 1);

                gwy_spectra_add_spectrum(spectra, sps, jpkfile->coordinates[kpt].x, jpkfile->coordinates[kpt].y);
                g_object_unref(sps);

                if (set_fraction && ij % 1000 == 0) {
                    if (!set_fraction((nspec*xres*yres + ij)/progres_denom)) {
                        err_CANCELLED(error);
                        g_object_unref(spectra);
                        nspec = 0;
                        goto fail;
                    }
                }
            }

            gwy_si_unit_set_from_string(gwy_spectra_get_si_unit_xy(spectra), "m");
            gwy_container_pass_object(container, gwy_app_get_spectra_key_for_id(nspec), spectra);

            title = g_strdup_printf("%s [%s]", jpkfile->channel_names[cid], data->segment_name);
            gwy_spectra_set_title(spectra, title);
            g_free(title);

            nspec++;
        }
    }

fail:
    g_free(curve);
    g_free(abscissa);
    g_object_unref(min_field);
    g_object_unref(range_field);
    g_object_unref(mask);

    return nspec;
}

static inline const gchar*
lookup_either(GHashTable *hash, const gchar *key1, const gchar *key2)
{
    const gchar *s;

    if ((s = g_hash_table_lookup(hash, key1)))
        return s;
    if ((s = g_hash_table_lookup(hash, key2)))
        return s;
    return NULL;
}

static gboolean
read_forcemap_data(GwyZipFile zipfile, JPKForceFile *jpkfile,
                   GwySetFractionFunc set_fraction, GError **error)
{
    GHashTable *hash;
    JPKForceData *data;
    GString *str = jpkfile->str;
    const gchar *s;
    guint sid, ptid;

    if (!gwyzip_first_file(zipfile, error))
        return FALSE;

    /* FIXME: Which dimension is i and which is j? */
    hash = jpkfile->header_properties;
    if ((s = lookup_either(hash,
                           "quantitative-imaging-map.position-pattern.grid.ilength",
                           "force-scan-map.position-pattern.grid.ilength"))) {
        jpkfile->ilength = atoi(s);
        gwy_debug("ilength from header %u", jpkfile->ilength);
    }
    if ((s = lookup_either(hash,
                           "quantitative-imaging-map.position-pattern.grid.jlength",
                           "force-scan-map.position-pattern.grid.jlength"))) {
        jpkfile->jlength = atoi(s);
        gwy_debug("jlength from header %u", jpkfile->jlength);
    }

    /* XXX: Cannot continue without knowing the number of points from settings.  Would like avoid allocating all curve
     * data one by one. Though we might be forced to do that at the end? */
    if (!jpkfile->shared_header_properties) {
        err_MISSING_FIELD(error, "num-points");
        return FALSE;
    }

    for (sid = 0; sid < jpkfile->nsegs; sid++) {
        data = jpkfile->data + sid;
        find_segment_settings(jpkfile, jpkfile->shared_header_properties, sid);
        if (!data->ndata) {
            err_MISSING_FIELD(error, "num-points");
        }
        /* NB: We cannot allocate anything here.  Must, unfortunately, wait for enumerate_channels() to be run for the
         * first time. */
        data->measured_ndata = g_new0(guint, jpkfile->npoints);
    }

    do {
        gchar *filename = NULL, *suffix = NULL;
        guint cid, ndata;
        gsize datablockoff;
        gboolean ok;

        if (!gwyzip_get_current_filename(zipfile, &filename, error))
            return FALSE;

        /* The point header comes after the segment data.  But that is not of much help because there may be missing
         * segments.  */
        suffix = match_segment_or_index_filename(filename, jpkfile->index_regex, &ptid);
        if (suffix && gwy_strequal(suffix, "header.properties")) {
            g_free(filename);
            g_free(suffix);

            g_assert(ptid <= jpkfile->npoints);

            hash = parse_header_properties(zipfile, jpkfile, error);
            if (!hash)
                return FALSE;
            if (!(s = lookup_either(hash,
                                    "quantitative-imaging-series.header.position.x",
                                    "force-scan-series.header.position.x"))) {
                err_MISSING_FIELD(error, "position.x");
                return FALSE;
            }
            jpkfile->coordinates[ptid].x = g_ascii_strtod(s, NULL);
            if (!(s = lookup_either(hash,
                                    "quantitative-imaging-series.header.position.y",
                                    "force-scan-series.header.position.y"))) {
                err_MISSING_FIELD(error, "position.y");
                return FALSE;
            }
            jpkfile->coordinates[ptid].y = g_ascii_strtod(s, NULL);
            jpkfile->have_coordinates[ptid] = TRUE;
            free_last_hash(jpkfile);
            continue;
        }
        g_free(suffix);

        /* Find the header. */
        suffix = match_map_segment_filename(filename, jpkfile->index_segment_regex, &ptid, &sid);
        g_free(filename);

        if (!suffix)
            continue;
        ok = gwy_strequal(suffix, "segment-header.properties");
        g_free(suffix);
        /* This should only happen with the directory entry, not any actual file. */
        if (!ok)
            continue;

        g_assert(sid <= jpkfile->nsegs);
        g_assert(ptid <= jpkfile->npoints);

        data = jpkfile->data + sid;
        if (set_fraction && ptid % 1000 == 0) {
            if (!set_fraction((gdouble)ptid/jpkfile->npoints)) {
                err_CANCELLED(error);
                return FALSE;
            }
        }

        hash = parse_header_properties(zipfile, jpkfile, error);
        if (!hash
            || !enumerate_channels(jpkfile, hash, TRUE, error)
            || !(ndata = find_segment_npoints(jpkfile, hash, error)))
            return FALSE;

        if (!data->data) {
            data->data = g_new(gdouble, data->ndata*jpkfile->nchannels*jpkfile->npoints);
            data->units = g_new0(const gchar*, jpkfile->nchannels);
        }

        //gwy_debug("%u, %u, npts = %u", ptid, sid, ndata);
        data->measured_ndata[ptid] = ndata;
        find_segment_settings(jpkfile, hash, sid);

        /* Expect corresponding data files next. */
        for (cid = 0; cid < jpkfile->nchannels; cid++) {
            const gchar *datafilename, *datatype;

            if (!(datatype = lookup_channel_property(jpkfile, hash, "type", cid, TRUE, error)))
                return FALSE;

            //gwy_debug("data.type %s", datatype);
            /* Handle computed data.  There is no corresponding file. */
            if (gwy_stramong(datatype, "constant-data", "raster-data", NULL)) {
                if (!read_computed_data(jpkfile, hash, data, datatype, ptid, cid, ndata, error))
                    return FALSE;
                continue;
            }

            /* Otherwise we have actual data and expect a file name. */
            if (!gwyzip_next_file(zipfile, error))
                return FALSE;

            if (!(datafilename = lookup_channel_property(jpkfile, hash, "file.name", cid, TRUE, error)))
                return FALSE;

            g_string_printf(str, "index/%u/segments/%u/%s",
                            ptid, sid, datafilename);
            if (!gwyzip_get_current_filename(zipfile, &filename, error))
                return FALSE;
            //gwy_debug("expecting file <%s>, found <%s>", str->str, filename);
            if (!gwy_strequal(filename, str->str)) {
                err_DATA_FILE_NAME(error, str->str, filename);
                g_free(filename);
                return FALSE;
            }
            g_free(filename);

            /* Read the data. */
            if (!read_raw_data(zipfile, jpkfile, data, hash, datatype, ptid, cid, ndata, error))
                return FALSE;

            datablockoff = (ptid*jpkfile->nchannels + cid)*data->ndata;
            apply_default_channel_scaling(jpkfile, data, hash, cid, datablockoff);
        }

        free_last_hash(jpkfile);

    } while (gwyzip_next_file(zipfile, NULL));

    for (ptid = 0; ptid < jpkfile->npoints; ptid++) {
        if (!jpkfile->have_coordinates[ptid]) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Header properties file for index %u is missing."), ptid);
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean
read_raw_data(GwyZipFile zipfile, JPKForceFile *jpkfile,
              JPKForceData *data, GHashTable *hash, const gchar *datatype,
              guint ptid, guint cid, guint ndata,
              GError **error)
{
    GwyRawDataType rawtype;
    const gchar *encoder = "";
    gsize size, datablockoff;
    guchar *bytes;
    gdouble q, off;
    gboolean is_float;

    if (gwy_stramong(datatype, "float-data", "float", NULL))
        rawtype = GWY_RAW_DATA_FLOAT;
    else if (gwy_stramong(datatype, "double-data", "double", NULL))
        rawtype = GWY_RAW_DATA_DOUBLE;
    else if (gwy_stramong(datatype, "short-data", "memory-short-data", "short", NULL)) {
        if (!(encoder = lookup_channel_property(jpkfile, hash, "encoder.type", cid, TRUE, error)))
            return FALSE;
        if (gwy_stramong(encoder, "unsignedshort", "unsignedshort-limited", NULL))
            rawtype = GWY_RAW_DATA_UINT16;
        else if (gwy_stramong(encoder, "signedshort", "signedshort-limited", NULL))
            rawtype = GWY_RAW_DATA_SINT16;
        else {
            err_UNSUPPORTED(error, "data.encoder.type");
            return FALSE;
        }
    }
    else if (gwy_stramong(datatype, "integer-data", "memory-integer-data", NULL)) {
        if (!(encoder = lookup_channel_property(jpkfile, hash, "encoder.type", cid, TRUE, error)))
            return FALSE;
        if (gwy_stramong(encoder, "unsignedinteger", "unsignedinteger-limited", NULL))
            rawtype = GWY_RAW_DATA_UINT32;
        else if (gwy_stramong(encoder, "signedinteger", "signedinteger-limited", NULL))
            rawtype = GWY_RAW_DATA_SINT32;
        else {
            err_UNSUPPORTED(error, "data.encoder.type");
            return FALSE;
        }
    }
    else if (gwy_stramong(datatype, "long-data", "memory-long-data", "long", NULL)) {
        if (!(encoder = lookup_channel_property(jpkfile, hash, "encoder.type", cid, TRUE, error)))
            return FALSE;
        if (gwy_stramong(encoder, "unsignedlong", "unsignedlong-limited", NULL))
            rawtype = GWY_RAW_DATA_UINT64;
        else if (gwy_stramong(encoder, "signedlong", "signedlong-limited", NULL))
            rawtype = GWY_RAW_DATA_SINT64;
        else {
            err_UNSUPPORTED(error, "data.encoder.type");
            return FALSE;
        }
    }
    else {
        err_UNSUPPORTED(error, "data.type");
        return FALSE;
    }

    if (!(bytes = gwyzip_get_file_content(zipfile, &size, error)))
        return FALSE;

    if (err_SIZE_MISMATCH(error, ndata*gwy_raw_data_size(rawtype), size, TRUE)) {
        g_free(bytes);
        return FALSE;
    }

    /* Apply the encoder conversion factors.  These convert raw data to some sensor physical values, typically Volts.
     * Conversions to values we actually want to display are done later.  */
    /* Apparently floating point data do not need encoder (makes sense but the file spec is unclear in this regard).
     * */
    is_float = (rawtype == GWY_RAW_DATA_DOUBLE || rawtype == GWY_RAW_DATA_FLOAT);
    find_scaling_parameters(jpkfile, hash, "encoder", cid, &q, &off, data->units + cid, is_float);
    /* Use allocate ndata from settings, not actual ndata for segment here! */
    datablockoff = (ptid*jpkfile->nchannels + cid)*data->ndata;
    gwy_convert_raw_data(bytes, ndata, 1, rawtype, GWY_BYTE_ORDER_BIG_ENDIAN, data->data + datablockoff, q, off);
    g_free(bytes);
    //gwy_debug("read %u (%s,%s) data points", ndata, datatype, encoder);
    return TRUE;
}

static gboolean
read_computed_data(JPKForceFile *jpkfile, GHashTable *header_properties,
                   JPKForceData *data, const gchar *datatype,
                   guint ptid, guint cid, guint ndata,
                   GError **error)
{
    gsize datablockoff;
    const gchar *s;
    gdouble value, start, step;
    gdouble *d;
    guint j;

    /* Use allocate ndata from settings, not actual ndata for segment here! */
    datablockoff = (ptid*jpkfile->nchannels + cid)*data->ndata;
    d = data->data + datablockoff;
    /* XXX: I invented this to have a non-NULL string there. */
    if (!jpkfile->default_cals[cid])
        jpkfile->default_cals[cid] = "computed";

    if (gwy_strequal(datatype, "constant-data")) {
        if (!(s = lookup_channel_property(jpkfile, header_properties, "value", cid, TRUE, error)))
            return FALSE;
        value = g_ascii_strtod(s, NULL);

        for (j = 0; j < ndata; j++)
            d[j] = value;
        return TRUE;
    }

    if (gwy_strequal(datatype, "raster-data")) {
        if (!(s = lookup_channel_property(jpkfile, header_properties, "start", cid, TRUE, error)))
            return FALSE;
        start = g_ascii_strtod(s, NULL);
        if (!(s = lookup_channel_property(jpkfile, header_properties, "step", cid, TRUE, error)))
            return FALSE;
        step = g_ascii_strtod(s, NULL);

        for (j = 0; j < ndata; j++)
            d[j] = start + j*step;
        return TRUE;
    }

    g_assert_not_reached();
    return FALSE;
}

static void
find_segment_settings(JPKForceFile *jpkfile,
                      GHashTable *header_properties, guint sid)
{
    GHashTable *shared_properties = jpkfile->shared_header_properties;
    JPKForceData *data = jpkfile->data + sid;
    GString *str = jpkfile->str;
    const gchar *s;

    g_free(data->segment_name);
    data->segment_name = find_sgement_name(header_properties, shared_properties, sid, str);
    //gwy_debug("segment %u name: %s", sid, data->segment_name);
    /* FIXME: Should we fail when segment_name is NULL? */

    g_free(data->segment_style);
    data->segment_style = g_hash_table_lookup(header_properties,
                                              "force-segment-header.settings.segment-settings.style");
    if (!data->segment_style && shared_properties) {
        g_string_printf(str, "force-segment-header-info.%u.settings.segment-settings.style", sid);
        data->segment_style = g_hash_table_lookup(shared_properties, str->str);
    }
    data->segment_style = g_strdup(data->segment_style);
    //gwy_debug("segment %u style: %s", sid, data->segment_style);

    if (!data->ndata && shared_properties) {
        g_string_printf(str, "force-segment-header-info.%u.settings.segment-settings.num-points", sid);
        if ((s = g_hash_table_lookup(shared_properties, str->str)))
            data->ndata = atoi(s);
    }
    //gwy_debug("segment %u num-points: %u", sid, data->ndata);

    g_free(data->segment_type);
    data->segment_type = g_hash_table_lookup(header_properties, "force-segment-header.settings.segment-settings.type");
    if (!data->segment_type && shared_properties) {
        g_string_printf(str, "force-segment-header-info.%u.settings.segment-settings.type", sid);
        data->segment_type = g_hash_table_lookup(shared_properties, str->str);
    }
    data->segment_type = g_strdup(data->segment_type);
    //gwy_debug("segment %u type: %s", sid, data->segment_type);
}

static guint
find_segment_npoints(JPKForceFile *jpkfile,
                     GHashTable *header_properties, GError **error)
{
    guint cid, npts = 0;
    const gchar *s;

    for (cid = 0; cid < jpkfile->nchannels; cid++) {
        if (!(s = lookup_channel_property(jpkfile, header_properties, "num-points", cid, TRUE, error)))
            return 0;
        if (cid) {
            if (atoi(s) != npts) {
                err_INVALID(error, jpkfile->str->str);
                return 0;
            }
        }
        else {
            npts = atoi(s);
            if (err_DIMENSION(error, npts))
                return FALSE;
        }
    }

    return npts;
}

static const gchar*
lookup_similar(GHashTable *hash, GString *str, guint len, const gchar *newend)
{
    g_string_truncate(str, len);
    g_string_append(str, newend);
    return g_hash_table_lookup(hash, str->str);
}

static gchar*
find_sgement_name(GHashTable *segment_properties, GHashTable *shared_properties,
                  guint sid, GString *str)
{
    GHashTable *hash;
    const gchar *t, *name, *prefix, *suffix;
    guint len;
    gchar *s;

    /* Figure out the correct leading part of the path. */
    hash = segment_properties;
    g_string_assign(str, "force-segment-header.settings.segment-settings.identifier.");
    len = str->len;
    name = lookup_similar(hash, str, len, "name");
    if (!name && shared_properties) {
        hash = shared_properties;
        g_string_printf(str, "force-segment-header-info.%u.settings.segment-settings.identifier.", sid);
        len = str->len;
        name = lookup_similar(hash, str, len, "name");
    }
    if (!name)
        return NULL;

    /* Use this leading part for all other keys. */
    t = lookup_similar(hash, str, len, "type");
    if (!t) {
        g_warning("Missing identifier type.");
        return g_strdup(name);
    }

    if (gwy_strequal(t, "standard")) {
        s = g_strdup(name);
        s[0] = g_ascii_toupper(s[0]);
        return s;
    }
    if (gwy_strequal(t, "ExtendedStandard")) {
        prefix = lookup_similar(hash, str, len, "prefix");
        suffix = lookup_similar(hash, str, len, "suffix");
        if (prefix && suffix)
            return g_strconcat(prefix, name, suffix, NULL);
        g_warning("Prefix or suffix missing for ExtendedStandard identifier.");
        return g_strdup(name);
    }
    if (gwy_strequal(t, "user"))
        return g_strdup(name);

    g_warning("Unknown identifier type %s.", t);
    return g_strdup(name);
}

/* FIXME: We might not want to do this because apparently it is not guaranteed the default for force is force etc. */
static gboolean
apply_default_channel_scaling(JPKForceFile *jpkfile,
                              JPKForceData *data,
                              GHashTable *header_properties,
                              guint cid,
                              gsize datablockoff)
{
    const gchar *default_cal;
    gdouble q, off;
    gdouble *d;
    gchar *key;
    guint j, ndata;

    if (!(default_cal = jpkfile->default_cals[cid])) {
        default_cal = lookup_channel_property(jpkfile, header_properties,
                                              "conversion-set.conversions.default",
                                              cid, FALSE, NULL);
        if (!default_cal) {
            g_warning("Cannot find the default conversion.");
            return FALSE;
        }
        else
            jpkfile->default_cals[cid] = default_cal;
    }

    key = g_strconcat("conversion-set.conversion.", default_cal, NULL);
    if (!find_scaling_parameters(jpkfile, header_properties, key, cid, &q, &off, data->units + cid, FALSE)) {
        g_free(key);
        return FALSE;
    }
    g_free(key);

    ndata = data->ndata;
    d = data->data + datablockoff;
    for (j = 0; j < ndata; j++)
        d[j] = q*d[j] + off;

    return TRUE;
}

static const gchar*
lookup_scaling_property(JPKForceFile *jpkfile,
                        GHashTable *hash,
                        const gchar *subkey,
                        guint len,
                        guint cid,
                        const gchar *expected_value,
                        gboolean ignore_missing)
{
    GString *key = jpkfile->qstr;
    const gchar *s;

    g_string_truncate(key, len);
    g_string_append(key, subkey);
    s = lookup_channel_property(jpkfile, hash, key->str, cid, FALSE, NULL);
    if (!s) {
        if (!ignore_missing)
            g_warning("Cannot find %s.", key->str);
        return NULL;
    }
    if (expected_value && !gwy_strequal(s, expected_value)) {
        g_warning("Value of %s is not %s.", key->str, expected_value);
        return NULL;
    }
    return s;
}

/* Subkey is typically something like "data.encoder" for conversion from integral data; or
 * "conversion-set.conversion.force" for calibrations.
 * Note calibrations can be nested, we it can refer recursively to "base-calibration-slot" and we have to perform that
 * calibration first. */
static gboolean
find_scaling_parameters(JPKForceFile *jpkfile,
                        GHashTable *hash,
                        const gchar *subkey,
                        guint cid,
                        gdouble *multiplier,
                        gdouble *offset,
                        const gchar **unit,
                        gboolean ignore_missing)
{
    /* There seem to be different unit styles.  Documentation says just "unit" but I see "unit.type" and "unit.unit"
     * for the actual unit.  Try both. */
    static const gchar *unit_keys[] = { "unit.unit", "unit" };

    gdouble base_multipler, base_offset;
    const gchar *base_unit;  /* we ignore that; they do not specify factors but directly units of the results. */
    /* NB: This function can recurse.  Must avoid overwriting! */
    GString *key = jpkfile->qstr;
    const gchar *s, *bcs;
    gchar *bcskey;
    guint len, j;

    *multiplier = 1.0;
    *offset = 0.0;
    /* Do not set the unit unless some unit is found. */

    g_string_assign(key, subkey);
    g_string_append_c(key, '.');
    len = key->len;

    /* If the scaling has defined=false, it means there is no scaling to perform.  This occurs for the base
     * calibration.  In principle, we should already know we are at the base calibration by looking at
     * "conversions.base" but we do not bother at present. */
    g_string_append(key, "defined");
    if ((s = lookup_channel_property(jpkfile, hash, key->str, cid, FALSE, NULL)) && gwy_strequal(s, "false"))
        return TRUE;

    g_string_truncate(key, len);
    g_string_append(key, "scaling.");
    len = key->len;

    if (!lookup_scaling_property(jpkfile, hash, "type", len, cid, "linear", ignore_missing))
        return FALSE;
    if (!lookup_scaling_property(jpkfile, hash, "style", len, cid, "offsetmultiplier", ignore_missing))
        return FALSE;
    if ((s = lookup_scaling_property(jpkfile, hash, "offset", len, cid, NULL, ignore_missing)))
        *offset = g_ascii_strtod(s, NULL);
    if ((s = lookup_scaling_property(jpkfile, hash, "multiplier", len, cid, NULL, ignore_missing)))
        *multiplier = g_ascii_strtod(s, NULL);

    for (j = 0; j < G_N_ELEMENTS(unit_keys); j++) {
        g_string_truncate(key, len);
        g_string_append(key, unit_keys[j]);
        s = lookup_channel_property(jpkfile, hash, key->str, cid, FALSE, NULL);
        if (s) {
            *unit = s;
            break;
        }
    }
    if (!*unit)
        g_warning("Cannot find scaling unit.");

    /* If there is no base calibration slot we have the final calibration parameters. */
    g_string_assign(key, subkey);
    len = key->len;
    g_string_append(key, ".base-calibration-slot");
    bcs = lookup_channel_property(jpkfile, hash, key->str, cid, FALSE, NULL);
    if (!bcs)
        return TRUE;

    /* Otherwise we have to recurse.  First assume the calibration slot name is the same as the calibration name (yes,
     * there seems another level of indirection). */
    if (!(s = strrchr(subkey, '.'))) {
        g_warning("Cannot form base calibration name becaue there is no dot in the original name.");
        return FALSE;
    }
    g_string_truncate(key, s+1 - subkey);
    g_string_append(key, bcs);
    bcskey = g_strdup(key->str);
    if (find_scaling_parameters(jpkfile, hash, bcskey, cid, &base_multipler, &base_offset, &base_unit, FALSE)) {
        g_free(bcskey);
        *multiplier *= base_multipler;
        *offset += *multiplier * base_offset;
        /* Ignore base unit. */
        return TRUE;
    }

    /* XXX: The name does not necessarily have to be the same.  We should look for base calibration with
     * "calibration-slot" equal to @bcskey, but that requires scanning the entire dictionary (XXX: maybe not, there is
     * "conversions.list" field listing all the conversions – but whether the names are slot or conversion names, no
     * one knows. */
    g_warning("Cannot figure out base calibration (trying %s).", bcskey);
    g_free(bcskey);
    return FALSE;
}

static const gchar*
lookup_channel_property(JPKForceFile *jpkfile,
                        GHashTable *header_properties, const gchar *subkey,
                        guint i,
                        gboolean fail_if_not_found, GError **error)
{
    GError *err = NULL;
    GString *str = jpkfile->str;
    const gchar *retval;
    guint len;

    g_return_val_if_fail(i < jpkfile->nchannels, NULL);
    g_string_assign(str, "channel.");
    if (jpkfile->pause_channels)
        g_string_append(str, jpkfile->pause_channels[i]);
    else
        g_string_append(str, jpkfile->channel_names[i]);
    g_string_append_c(str, '.');

    /* Some things are found under "data" in documentation but under "lcd-info" in real files.  Some may be only in
     * one location but we simply try both for all keys. */
    len = str->len;
    g_string_append(str, "lcd-info.");
    g_string_append(str, subkey);
    if ((retval = lookup_property(jpkfile, header_properties, str->str,
                                  fail_if_not_found,
                                  fail_if_not_found ? &err : NULL)))
        return retval;

    g_string_truncate(str, len);
    g_string_append(str, "data.");
    g_string_append(str, subkey);
    if ((retval = lookup_property(jpkfile, header_properties, str->str, FALSE, NULL))) {
        if (fail_if_not_found)
            g_clear_error(&err);
        return retval;
    }

    if (fail_if_not_found) {
        /* @err cannot be set otherwise. */
        g_propagate_error(error, err);
    }
    return NULL;
}

/* Look up a property in provided @header_properties and, failing that, in the shared properties. */
static const gchar*
lookup_property(JPKForceFile *jpkfile,
                GHashTable *header_properties, const gchar *key,
                gboolean fail_if_not_found, GError **error)
{
    GString *sstr = jpkfile->sstr;
    const gchar *s, *dot;
    guint len;

    /* Direct lookup. */
    if ((s = g_hash_table_lookup(header_properties, key)))
        return s;

    /* If there are shared properties and a *-reference we have a second chance. */
    if (jpkfile->shared_header_properties) {
        g_string_assign(sstr, key);
        while ((dot = strrchr(sstr->str, '.'))) {
            len = dot - sstr->str;
            g_string_truncate(sstr, len+1);
            g_string_append_c(sstr, '*');
            if ((s = g_hash_table_lookup(header_properties, sstr->str)))
                break;
            g_string_truncate(sstr, len);
        }
    }

    /* Not found or we have zero prefix. */
    if (!s || !len)
        goto fail;

    /* Try to look it up in the shared properties.  The part just before .* is the beginning of the property name in
     * the shared properties.  */
    g_string_truncate(sstr, len);
    if ((dot = strrchr(sstr->str, '.')))
        g_string_erase(sstr, 0, dot+1 - sstr->str);
    g_string_append_c(sstr, '.');
    g_string_append(sstr, s);
    g_string_append(sstr, key + len);
    //gwy_debug("shared properties key <%s>", sstr->str);

    if ((s = g_hash_table_lookup(jpkfile->shared_header_properties, sstr->str)))
        return s;

fail:
    if (fail_if_not_found)
        err_MISSING_FIELD(error, key);
    return NULL;
}

static gchar**
enumerate_channels_raw(GHashTable *header_properties)
{
    const gchar *s;

    if (!header_properties || !(s = g_hash_table_lookup(header_properties, "channels.list")))
        return NULL;

    return g_strsplit(s, " ", -1);
}

static gboolean
enumerate_channels(JPKForceFile *jpkfile, GHashTable *header_properties,
                   gboolean needslist, GError **error)
{
    const gchar *s, *ss;
    gchar **fields;
    guint i, n, len;

    if (!header_properties || !(s = g_hash_table_lookup(header_properties, "channels.list"))) {
        if (!needslist || jpkfile->channel_names)
            return TRUE;
        err_MISSING_FIELD(error, "channels.list");
        return FALSE;
    }

    /* If we already have some channel list, check if it matches. */
    gwy_debug("channel list <%s>", s);
    if (jpkfile->channel_names) {
        n = jpkfile->nchannels;
        for (i = 0; i < n-1; i++) {
            ss = jpkfile->channel_names[i];
            len = strlen(ss);
            if (memcmp(s, ss, len) != 0 || s[len] != ' ')
                return err_NONUNIFORM_CHANNELS(error);
            s += len+1;
        }
        ss = jpkfile->channel_names[i];
        if (!gwy_strequal(s, ss))
            return err_NONUNIFORM_CHANNELS(error);
        /* There is a perfect match. */
        return TRUE;
    }

    /* There is no channel yet list so construct it from what we found. */
    fields = g_strsplit(s, " ", -1);
    n = g_strv_length(fields);
    if (!n) {
        g_free(fields);
        err_NO_DATA(error);
        return FALSE;
    }

    jpkfile->nchannels = n;
    jpkfile->channel_names = g_new(gchar*, n);
    jpkfile->default_cals = g_new0(const gchar*, n);
    for (i = 0; i < n; i++) {
        jpkfile->channel_names[i] = fields[i];
        gwy_debug("channel[%u] = <%s>", i, fields[i]);
        if (gwy_strequal(jpkfile->channel_names[i], "height"))
            jpkfile->height_cid = i;
    }
    g_free(fields);

    if (jpkfile->height_cid < 0) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Cannot find any height channel."));
        return FALSE;
    }

    return TRUE;
}

static gboolean
analyse_segment_ids(JPKForceFile *jpkfile, GError **error)
{
    guint i, nids = jpkfile->nids;

    g_assert(jpkfile->type == JPK_FORCE_CURVES);
    for (i = 0; i < nids; i++) {
        if (jpkfile->ids[i] != i) {
            return err_IRREGULAR_NUMBERING(error);
        }
    }

    jpkfile->nsegs = nids;
    jpkfile->npoints = 1;
    return TRUE;
}

static void
add_id_to_array(gpointer hkey, G_GNUC_UNUSED gpointer hval, gpointer user_data)
{
    guint id = GPOINTER_TO_UINT(hkey);
    GArray *array = (GArray*)user_data;

    g_array_append_val(array, id);
}

static gboolean
analyse_map_segment_ids(JPKForceFile *jpkfile, GError **error)
{
    GHashTable *idhash;
    GArray *idlist;
    guint *allids;
    guint i, j, k, kk, nids = jpkfile->nids;
    guint nsegs, npoints, idx;

    g_assert(jpkfile->type == JPK_FORCE_MAP || jpkfile->type == JPK_FORCE_QI);
    gwy_debug("nids %u", nids);

    idhash = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (i = 0; i < nids; i++) {
        idx = jpkfile->ids[2*i + 1];
        g_hash_table_insert(idhash, GUINT_TO_POINTER(idx), GUINT_TO_POINTER(TRUE));
    }
    idlist = g_array_new(FALSE, FALSE, sizeof(guint));
    g_hash_table_foreach(idhash, add_id_to_array, idlist);
    nsegs = idlist->len;
    gwy_guint_sort(nsegs, (guint*)idlist->data);

    gwy_debug("segment ids (%u)", nsegs);
    for (i = 0; i < nsegs; i++) {
        if (g_array_index(idlist, guint, i) != i) {
            g_array_free(idlist, TRUE);
            g_hash_table_destroy(idhash);
            return err_IRREGULAR_NUMBERING(error);
        }
    }

    g_hash_table_steal_all(idhash);
    for (i = 0; i < nids; i++) {
        idx = jpkfile->ids[2*i];
        g_hash_table_insert(idhash, GUINT_TO_POINTER(idx), GUINT_TO_POINTER(TRUE));
    }
    g_array_set_size(idlist, 0);
    g_hash_table_foreach(idhash, add_id_to_array, idlist);
    npoints = idlist->len;
    gwy_guint_sort(npoints, (guint*)idlist->data);

    gwy_debug("point ids (%u)", npoints);
    for (i = 0; i < npoints; i++) {
        if (g_array_index(idlist, guint, i) != i) {
            g_array_free(idlist, TRUE);
            g_hash_table_destroy(idhash);
            return err_IRREGULAR_NUMBERING(error);
        }
    }
    g_array_free(idlist, TRUE);
    g_hash_table_destroy(idhash);

    /* There can be some missing spectra.  But if there is too large disparity between nsegs*npoints and the number of
     * curves then something is amiss. We do not want to try allocating a huge chunk of memory in result... */
    if (nids/npoints > nsegs+1)
        return err_IRREGULAR_NUMBERING(error);

    jpkfile->nsegs = nsegs;
    jpkfile->npoints = npoints;

    gwy_debug("expecting missing %u curves", nsegs*npoints - nids);
    if (nids == nsegs*npoints)
        return TRUE;

    /* Some curves are missing.  Insert markers to the ids[] array so that we have it formally complete. */
    allids = g_new(guint, 2*nids*npoints);
    kk = k = 0;
    for (i = 0; i < npoints; i++) {
        for (j = 0; j < nsegs; j++) {
            k = i*nsegs + j;
            if (jpkfile->ids[2*kk] != i || jpkfile->ids[2*kk + 1] != j) {
                allids[2*k] = allids[2*k + 1] = G_MAXUINT;
            }
            else {
                allids[2*k] = i;
                allids[2*k + 1] = j;
                kk++;
            }
        }
    }
    gwy_debug("%u missing curves", k+1 - kk);
    GWY_SWAP(guint*, jpkfile->ids, allids);

    return TRUE;
}

/*
 * We want to avoid:
 * - gwyzip_locate_file() on files that can be at the end; for instance shared header properties
 * - scanning the file twice to figure out what kind of data we are dealing with
 * Either takes a *long* time.
 *
 * So here we gather info about curve segments, read any special file we come across along the way and decide the file
 * type, all in a single pass.
 */
static gboolean
scan_file_enumerate_segments(GwyZipFile zipfile, JPKForceFile *jpkfile,
                             GwySetMessageFunc set_message,
                             GError **error)
{
    GRegex *seg_regex = jpkfile->segment_regex,
           *map_regex = jpkfile->index_segment_regex;
    GHashTable *hash;
    GArray *ids = NULL;

    gwy_debug("file");
    if (!gwyzip_first_file(zipfile, error))
        return FALSE;

    do {
        gchar *filename = NULL, *suffix = NULL;
        guint id, id2[2];

        if (!gwyzip_get_current_filename(zipfile, &filename, error)) {
            if (ids)
                g_array_free(ids, TRUE);
            return FALSE;
        }

        if (gwy_strequal(filename, "header.properties")) {
            /* If we encounter main header.properties read it. */
            if (jpkfile->header_properties)
                g_warning("%s found twice, using the first one", filename);
            else {
                if (!(hash = parse_header_properties(zipfile, jpkfile, error))) {
                    if (ids)
                        g_array_free(ids, TRUE);
                    g_free(filename);
                    return FALSE;
                }
                jpkfile->header_properties = hash;
                jpkfile->last_hash = NULL;   /* Take ownership. */
            }
        }
        else if (gwy_strequal(filename, "shared-data/header.properties")) {
            /* If we encounter shared header.properties read it. */
            if (jpkfile->shared_header_properties)
                g_warning("%s found twice, using the first one", filename);
            else {
                if (!(hash = parse_header_properties(zipfile, jpkfile, error))) {
                    if (ids)
                        g_array_free(ids, TRUE);
                    g_free(filename);
                    return FALSE;
                }
                jpkfile->shared_header_properties = hash;
                jpkfile->last_hash = NULL;   /* Take ownership. */
            }
        }
        else if (jpkfile->type == JPK_FORCE_MAP) {
            /* File type known (MAP vs QI resolved later), try to get ids. */
            if ((suffix = match_map_segment_filename(filename, map_regex, id2 + 0, id2 + 1))) {
                if (gwy_strequal(suffix, "segment-header.properties")) {
                    g_array_append_val(ids, id2);
                    if (set_message && ids->len % 10000 == 0) {
                        g_string_printf(jpkfile->str, _("Scanning file (%u curves)..."), ids->len);
                        if (!set_message(jpkfile->str->str)) {
                            g_free(suffix);
                            g_free(filename);
                            err_CANCELLED(error);
                            g_array_free(ids, TRUE);
                            return FALSE;
                        }
                    }
                }
                g_free(suffix);
            }
        }
        else if (jpkfile->type == JPK_FORCE_CURVES) {
            /* File type known, try to get id. */
            if ((suffix = match_segment_or_index_filename(filename, seg_regex, &id))) {
                if (gwy_strequal(suffix, "segment-header.properties")) {
                    g_array_append_val(ids, id);
                    gwy_debug("segment: %s -> %u", filename, id);
                }
                g_free(suffix);
            }
        }
        else {
            /* Try to decide the file type. */
            if ((suffix = match_map_segment_filename(filename, map_regex, id2 + 0, id2 + 1))) {
                if (gwy_strequal(suffix, "segment-header.properties")) {
                    jpkfile->type = JPK_FORCE_MAP;
                    ids = g_array_new(FALSE, FALSE, 2*sizeof(gint));
                    g_array_append_val(ids, id2);
                }
                g_free(suffix);
            }
            else if ((suffix = match_segment_or_index_filename(filename, seg_regex, &id))) {
                if (gwy_strequal(suffix, "segment-header.properties")) {
                    jpkfile->type = JPK_FORCE_CURVES;
                    ids = g_array_new(FALSE, FALSE, sizeof(guint));
                    g_array_append_val(ids, id);
                    gwy_debug("segment: %s -> %u", filename, id);
                }
                g_free(suffix);
            }
        }
        g_free(filename);
    } while (gwyzip_next_file(zipfile, NULL));

    if (!ids) {
        err_NO_DATA(error);
        return FALSE;
    }
    g_assert(jpkfile->type);

    if (!jpkfile->header_properties) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("File %s is missing in the zip file."), "header.properties");
        g_array_free(ids, TRUE);
        return FALSE;
    }

    if (jpkfile->type == JPK_FORCE_MAP)
        g_array_sort(ids, compare_uint2);
    else
        gwy_guint_sort(ids->len, (guint*)ids->data);

    jpkfile->nids = ids->len;
    gwy_debug("total nids: %u", jpkfile->nids);
    jpkfile->ids = (guint*)g_array_free(ids, FALSE);
    return TRUE;
}

static GHashTable*
parse_header_properties(GwyZipFile zipfile, JPKForceFile *jpkfile,
                        GError **error)
{
    GwyTextHeaderParser parser;
    GHashTable *hash;
    guchar *contents;
    gsize size;

    if (!(contents = gwyzip_get_file_content(zipfile, &size, error)))
        return NULL;

    jpkfile->buffers = g_slist_prepend(jpkfile->buffers, contents);

    gwy_clear(&parser, 1);
    parser.comment_prefix = "#";
    parser.key_value_separator = "=";
    hash = gwy_text_header_parse((gchar*)contents, &parser, NULL, error);
    if (hash && jpkfile->last_hash) {
        g_warning("Overwriting last_hash, memory leak is imminent.");
    }
    jpkfile->last_hash = hash;
//#ifdef DEBUG
#if 0
    if (hash) {
        gchar *filename;
        if (gwyzip_get_current_filename(zipfile, &filename, NULL)) {
            gwy_debug("%s has %u entries", filename, g_hash_table_size(hash));
            g_free(filename);
        }
        else {
            gwy_debug("UNKNOWNFILE? has %u entries", g_hash_table_size(hash));
        }
    }
#endif

    return hash;
}

static void
free_last_hash(JPKForceFile *jpkfile)
{
    g_hash_table_destroy(jpkfile->last_hash);
    jpkfile->last_hash = NULL;

    g_free(jpkfile->buffers->data);
    jpkfile->buffers = g_slist_delete_link(jpkfile->buffers, jpkfile->buffers);
}

static void
jpk_force_file_free(JPKForceFile *jpkfile)
{
    GSList *l;
    guint i;

    g_free(jpkfile->ids);
    g_free(jpkfile->coordinates);
    g_free(jpkfile->pointmap);
    g_free(jpkfile->have_coordinates);

    for (i = 0; i < jpkfile->nchannels; i++)
        g_free(jpkfile->channel_names[i]);
    if (jpkfile->pause_channels)
        g_strfreev(jpkfile->pause_channels);
    g_free(jpkfile->channel_names);
    g_free(jpkfile->default_cals);

    if (jpkfile->segment_regex)
        g_regex_unref(jpkfile->segment_regex);
    if (jpkfile->index_regex)
        g_regex_unref(jpkfile->index_regex);
    if (jpkfile->index_segment_regex)
        g_regex_unref(jpkfile->index_segment_regex);

    if (jpkfile->str)
        g_string_free(jpkfile->str, TRUE);
    if (jpkfile->sstr)
        g_string_free(jpkfile->sstr, TRUE);
    if (jpkfile->qstr)
        g_string_free(jpkfile->qstr, TRUE);

    if (jpkfile->header_properties)
        g_hash_table_destroy(jpkfile->header_properties);

    if (jpkfile->shared_header_properties)
        g_hash_table_destroy(jpkfile->shared_header_properties);

    if (jpkfile->data) {
        for (i = 0; i < jpkfile->nsegs; i++) {
            g_free(jpkfile->data[i].data);
            g_free(jpkfile->data[i].segment_name);
            g_free(jpkfile->data[i].segment_style);
            g_free(jpkfile->data[i].segment_type);
            g_free(jpkfile->data[i].units);
            g_free(jpkfile->data[i].measured_ndata);
        }
        g_free(jpkfile->data);
    }

    while ((l = jpkfile->buffers)) {
        jpkfile->buffers = g_slist_next(l);
        g_free(l->data);
        g_slist_free_1(l);
    }
}
#endif

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
