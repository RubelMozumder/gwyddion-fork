/*
 *  $Id: rhk-sm4.c 25898 2023-10-22 05:28:26Z yeti-dn $
 *  Copyright (C) 2009-2021 David Necas (Yeti).
 *  E-mail: yeti@gwyddion.net.
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
 * <mime-type type="application/x-rhk-sm4-spm">
 *   <comment>RHK SM4 SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="2" value="S\0T\0i\0M\0a\0g\0e\0 \0\060\0\060\0\065\0.\0"/>
 *   </magic>
 *   <glob pattern="*.sm4"/>
 *   <glob pattern="*.SM4"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # RHK SM4
 * # The same as SM3, but with different numbers.
 * 2 lestring16 STiMage\ 005. RHK Technology SM4 data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * RHK Instruments SM4
 * .sm4
 * Read SPS:Limited[1]
 * [1] Spectra curves are imported as graphs, positional information is lost.
 **/

#include "config.h"
#include <string.h>

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/datafield.h>
#include <libgwymodule/gwymodule-file.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphcurvemodel.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>
#include "gwyzlib.h"
#include "err.h"
#include "get.h"

/* Ugly Microsoft UTF-16...
 * It reads: `STiMage 004.NNN N', but we do not check the NNN N */
static const guchar MAGIC[] = {
    0x53, 0x00, 0x54, 0x00, 0x69, 0x00, 0x4d, 0x00, 0x61, 0x00, 0x67, 0x00,
    0x65, 0x00, 0x20, 0x00, 0x30, 0x00, 0x30, 0x00, 0x35, 0x00, 0x2e, 0x00,
};

#define EXTENSION ".sm4"

enum {
    MAGIC_OFFSET = 2,
    MAGIC_SIZE = G_N_ELEMENTS(MAGIC),
    MAGIC_TOTAL_SIZE = 36,   /* including the version part we do not check */
    HEADER_SIZE = MAGIC_OFFSET + MAGIC_TOTAL_SIZE + 5*4,
    OBJECT_SIZE = 3*4,
    GUID_SIZE = 16,
    PAGE_INDEX_HEADER_SIZE = 4*4,
    PAGE_INDEX_ARRAY_SIZE = GUID_SIZE + 4*4,
    PAGE_HEADER_SIZE = 170,
    PRM_HEADER_SIZE = 12,
};

typedef enum {
    RHK_DATA_IMAGE          = 0,
    RHK_DATA_LINE           = 1,
    RHK_DATA_XY_DATA        = 2,
    RHK_DATA_ANNOTATED_LINE = 3,
    RHK_DATA_TEXT           = 4,
    RHK_DATA_ANNOTATED_TEXT = 5,
    RHK_DATA_SEQUENTIAL     = 6,    /* Only in RHKPageIndex */
    RHK_DATA_MOVIE          = 7,    /* New in R9, cannot import it anyway. */
} RHKDataType;

typedef enum {
    RHK_OBJECT_UNDEFINED            = 0,
    RHK_OBJECT_PAGE_INDEX_HEADER    = 1,
    RHK_OBJECT_PAGE_INDEX_ARRAY     = 2,
    RHK_OBJECT_PAGE_HEADER          = 3,
    RHK_OBJECT_PAGE_DATA            = 4,
    RHK_OBJECT_IMAGE_DRIFT_HEADER   = 5,
    RHK_OBJECT_IMAGE_DRIFT          = 6,
    RHK_OBJECT_SPEC_DRIFT_HEADER    = 7,
    RHK_OBJECT_SPEC_DRIFT_DATA      = 8,
    RHK_OBJECT_COLOR_INFO           = 9,
    RHK_OBJECT_STRING_DATA          = 10,
    RHK_OBJECT_TIP_TRACK_HEADER     = 11,
    RHK_OBJECT_TIP_TRACK_DATA       = 12,
    RHK_OBJECT_PRM                  = 13,
    RHK_OBJECT_THUMBNAIL            = 14,
    RHK_OBJECT_PRM_HEADER           = 15,
    RHK_OBJECT_THUMBNAIL_HEADER     = 16,
    RHK_OBJECT_API_INFO             = 17,
    RHK_OBJECT_HISTORY_INFO         = 18,
    RHK_OBJECT_PIEZO_SENSITIVITY    = 19,
    RHK_OBJECT_FREQUENCY_SWEEP_DATA = 20,
    RHK_OBJECT_SCAN_PROCESSOR_INFO  = 21,
    RHK_OBJECT_PLL_INFO             = 22,
    RHK_OBJECT_CH1_DRIVE_INFO       = 23,
    RHK_OBJECT_CH2_DRIVE_INFO       = 24,
    RHK_OBJECT_LOCKIN0_INFO         = 25,
    RHK_OBJECT_LOCKIN1_INFO         = 26,
    RHK_OBJECT_ZPI_INFO             = 27,
    RHK_OBJECT_KPI_INFO             = 28,
    RHK_OBJECT_AUX_PI_INFO          = 29,
    RHK_OBJECT_LOWPASS_FILTER0_INFO = 30,
    RHK_OBJECT_LOWPASS_FILTER1_INFO = 31,
    /* There is also object type 32 in Rev9. */
    /* Our types */
    RHK_OBJECT_FILE_HEADER          = -42,
    RHK_OBJECT_PAGE_INDEX           = -43,
} RHKObjectType;

typedef enum {
    RHK_SOURCE_RAW        = 0,
    RHK_SOURCE_PROCESSED  = 1,
    RHK_SOURCE_CALCULATED = 2,
    RHK_SOURCE_IMPORTED   = 3,
} RHKSourceType;

typedef enum {
    RHK_IMAGE_NORMAL         = 0,
    RHK_IMAGE_AUTOCORRELATED = 1,
} RHKImageType;

typedef enum {
    RHK_PAGE_UNDEFINED                   = 0,
    RHK_PAGE_TOPOGRAPHIC                 = 1,
    RHK_PAGE_CURRENT                     = 2,
    RHK_PAGE_AUX                         = 3,
    RHK_PAGE_FORCE                       = 4,
    RHK_PAGE_SIGNAL                      = 5,
    RHK_PAGE_FFT                         = 6,
    RHK_PAGE_NOISE_POWER_SPECTRUM        = 7,
    RHK_PAGE_LINE_TEST                   = 8,
    RHK_PAGE_OSCILLOSCOPE                = 9,
    RHK_PAGE_IV_SPECTRA                  = 10,
    RHK_PAGE_IV_4x4                      = 11,
    RHK_PAGE_IV_8x8                      = 12,
    RHK_PAGE_IV_16x16                    = 13,
    RHK_PAGE_IV_32x32                    = 14,
    RHK_PAGE_IV_CENTER                   = 15,
    RHK_PAGE_INTERACTIVE_SPECTRA         = 16,
    RHK_PAGE_AUTOCORRELATION             = 17,
    RHK_PAGE_IZ_SPECTRA                  = 18,
    RHK_PAGE_4_GAIN_TOPOGRAPHY           = 19,
    RHK_PAGE_8_GAIN_TOPOGRAPHY           = 20,
    RHK_PAGE_4_GAIN_CURRENT              = 21,
    RHK_PAGE_8_GAIN_CURRENT              = 22,
    RHK_PAGE_IV_64x64                    = 23,
    RHK_PAGE_AUTOCORRELATION_SPECTRUM    = 24,
    RHK_PAGE_COUNTER                     = 25,
    RHK_PAGE_MULTICHANNEL_ANALYSER       = 26,
    RHK_PAGE_AFM_100                     = 27,
    RHK_PAGE_CITS                        = 28,
    RHK_PAGE_GPIB                        = 29,
    RHK_PAGE_VIDEO_CHANNEL               = 30,
    RHK_PAGE_IMAGE_OUT_SPECTRA           = 31,
    RHK_PAGE_I_DATALOG                   = 32,
    RHK_PAGE_I_ECSET                     = 33,
    RHK_PAGE_I_ECDATA                    = 34,
    RHK_PAGE_I_DSP_AD                    = 35,
    RHK_PAGE_DISCRETE_SPECTROSCOPY_PP    = 36,
    RHK_PAGE_IMAGE_DISCRETE_SPECTROSCOPY = 37,
    RHK_PAGE_RAMP_SPECTROSCOPY_RP        = 38,
    RHK_PAGE_DISCRETE_SPECTROSCOPY_RP    = 39,
} RHKPageType;

typedef enum {
    RHK_LINE_NOT_A_LINE                     = 0,
    RHK_LINE_HISTOGRAM                      = 1,
    RHK_LINE_CROSS_SECTION                  = 2,
    RHK_LINE_LINE_TEST                      = 3,
    RHK_LINE_OSCILLOSCOPE                   = 4,
    RHK_LINE_NOISE_POWER_SPECTRUM           = 6,
    RHK_LINE_IV_SPECTRUM                    = 7,
    RHK_LINE_IZ_SPECTRUM                    = 8,
    RHK_LINE_IMAGE_X_AVERAGE                = 9,
    RHK_LINE_IMAGE_Y_AVERAGE                = 10,
    RHK_LINE_NOISE_AUTOCORRELATION_SPECTRUM = 11,
    RHK_LINE_MULTICHANNEL_ANALYSER_DATA     = 12,
    RHK_LINE_RENORMALIZED_IV                = 13,
    RHK_LINE_IMAGE_HISTOGRAM_SPECTRA        = 14,
    RHK_LINE_IMAGE_CROSS_SECTION            = 15,
    RHK_LINE_IMAGE_AVERAGE                  = 16,
    RHK_LINE_IMAGE_CROSS_SECTION_G          = 17,
    RHK_LINE_IMAGE_OUT_SPECTRA              = 18,
    RHK_LINE_DATALOG_SPECTRUM               = 19,
    RHK_LINE_GXY                            = 20,
    RHK_LINE_ELECTROCHEMISTRY               = 21,
    RHK_LINE_DISCRETE_SPECTROSCOPY          = 22,
    RHK_LINE_DSCOPE_DATALOGGING             = 23,
    RHK_LINE_TIME_SPECTROSCOPY              = 24,
    RHK_LINE_ZOOM_FFT                       = 25,
    RHK_LINE_FREQUENCY_SWEEP                = 26,
    RHK_LINE_PHASE_ROTATE                   = 27,
    RHK_LINE_FIBER_SWEEP                    = 28,
} RHKLineType;

typedef enum {
    RHK_SCAN_RIGHT = 0,
    RHK_SCAN_LEFT  = 1,
    RHK_SCAN_UP    = 2,
    RHK_SCAN_DOWN  = 3
} RHKScanType;

typedef enum {
    RHK_STRING_LABEL,
    RHK_STRING_SYSTEM_TEXT,
    RHK_STRING_SESSION_TEXT,
    RHK_STRING_USER_TEXT,
    RHK_STRING_PATH,
    RHK_STRING_DATE,
    RHK_STRING_TIME,
    RHK_STRING_X_UNITS,
    RHK_STRING_Y_UNITS,
    RHK_STRING_Z_UNITS,
    RHK_STRING_X_LABEL,
    RHK_STRING_Y_LABEL,
    RHK_STRING_STATUS_CHANNEL_TEXT,
    RHK_STRING_COMPLETED_LINE_COUNT,
    RHK_STRING_OVERSAMPLING_COUNT,
    RHK_STRING_SLICED_VOLTAGE,
    RHK_STRING_PLL_PRO_STATUS,
    RHK_STRING_NSTRINGS
} RHKStringType;

typedef enum {
    RHK_PIEZO_TUBE_X_UNIT,
    RHK_PIEZO_TUBE_Y_UNIT,
    RHK_PIEZO_TUBE_Z_UNIT,
    RHK_PIEZO_TUBE_Z_OFFSET_UNIT,
    RHK_PIEZO_SCAN_X_UNIT,
    RHK_PIEZO_SCAN_Y_UNIT,
    RHK_PIEZO_SCAN_Z_UNIT,
    RHK_PIEZO_ACTUATOR_UNIT,
    RHK_PIEZO_TUBE_CALIBRATION,
    RHK_PIEZO_SCAN_CALIBRATION,
    RHK_PIEZO_ACTUATOR_CALIBRATION,
    RHK_PIEZO_NSTRINGS
} RHKPiezoStringType;

typedef enum {
    RHK_DRIFT_DISABLED = 0,
    RHK_DRIFT_EACH_SPECTRA = 1,
    RHK_DRIFT_EACH_LOCATION = 2
} RHKDriftOptionType;

typedef struct {
    guint64 start_time;
    RHKDriftOptionType drift_opt;
    guint nstrings;
    gchar **strings;
} RHKSpecDriftHeader;

typedef struct {
    gdouble tube_x;
    gdouble tube_y;
    gdouble tube_z;
    gdouble tube_z_offset;
    gdouble scan_x;
    gdouble scan_y;
    gdouble scan_z;
    gdouble actuator;
    guint string_count;
    gchar *strings[RHK_PIEZO_NSTRINGS];
} RHKPiezoSensitivity;

typedef struct {
    gdouble ftime;
    gdouble x_coord;
    gdouble y_coord;
    gdouble dx;
    gdouble dy;
    gdouble cumulative_dx;
    gdouble cumulative_dy;
} RHKSpecInfo;

typedef struct {
    RHKObjectType type;
    guint offset;
    guint size;
} RHKObject;

typedef struct {
    guint page_count;
    guint object_count;
    guint reserved1;
    guint reserved2;
    RHKObject *objects;
} RHKPageIndexHeader;

typedef struct {
    guint field_size;
    guint string_count;
    RHKPageType page_type;
    guint data_sub_source;
    RHKLineType line_type;
    gint x_coord;
    gint y_coord;
    guint x_size;
    guint y_size;
    RHKImageType image_type;
    RHKScanType scan_dir;
    guint group_id;
    guint data_size;
    guint min_z_value;
    guint max_z_value;
    gdouble x_scale;
    gdouble y_scale;
    gdouble z_scale;
    gdouble xy_scale;
    gdouble x_offset;
    gdouble y_offset;
    gdouble z_offset;
    gdouble period;
    gdouble bias;
    gdouble current;
    gdouble angle;
    guint color_info_count;
    guint grid_x_size;
    guint grid_y_size;
    guint object_count;
    guint reserved[16];
    const guchar *data;
    gchar *strings[RHK_STRING_NSTRINGS];
    RHKObject *objects;
    RHKSpecDriftHeader *drift_header;
    RHKSpecInfo *spec_info;
    RHKPiezoSensitivity *piezo_sensitivity;
} RHKPage;

typedef struct {
    guchar id[GUID_SIZE];
    RHKDataType data_type;
    RHKSourceType source;
    guint object_count;
    guint minor_version;
    RHKObject *objects;
    RHKPage page;
} RHKPageIndex;

typedef struct {
    guint page_count;
    guint object_count;
    guint object_field_size;
    guint reserved1;
    guint reserved2;
    RHKObject *objects;
    RHKPageIndexHeader page_index_header;
    RHKPageIndex *page_indices;
} RHKFile;

static gboolean             module_register                (void);
static gint                 rhk_sm4_detect                 (const GwyFileDetectInfo *fileinfo,
                                                            gboolean only_name);
static GwyContainer*        rhk_sm4_load                   (const gchar *filename,
                                                            GwyRunType mode,
                                                            GError **error);
static gboolean             rhk_sm4_read_page_index_header (RHKPageIndexHeader *header,
                                                            const RHKObject *obj,
                                                            const guchar *buffer,
                                                            gsize size,
                                                            GError **error);
static gboolean             rhk_sm4_read_page_index        (RHKPageIndex *header,
                                                            const RHKObject *obj,
                                                            const guchar *buffer,
                                                            gsize size,
                                                            GError **error);
static gboolean             rhk_sm4_read_page_header       (RHKPage *page,
                                                            const RHKObject *obj,
                                                            RHKDataType data_type,
                                                            const guchar *buffer,
                                                            gsize size,
                                                            GError **error);
static gboolean             rhk_sm4_read_page_data         (RHKPage *page,
                                                            const RHKObject *obj,
                                                            const guchar *buffer,
                                                            GError **error);
static gboolean             rhk_sm4_read_string_data       (RHKPage *page,
                                                            const RHKObject *obj,
                                                            guint count,
                                                            const guchar *buffer);
static RHKSpecDriftHeader*  rhk_sm4_read_drift_header      (const RHKObject *obj,
                                                            const guchar *buffer);
static RHKPiezoSensitivity* rhk_sm4_read_piezo_sensitivity (const RHKObject *obj,
                                                            const guchar *buffer);
static RHKSpecInfo*         rhk_sm4_read_spec_info         (const RHKObject *obj,
                                                            const guchar *buffer,
                                                            gsize size,
                                                            guint nspec);
static RHKObject*           rhk_sm4_read_objects           (const guchar *buffer,
                                                            const guchar *p,
                                                            gsize size,
                                                            guint count,
                                                            RHKObjectType intype,
                                                            GError **error);
static RHKObject*           rhk_sm4_find_object            (RHKObject *objects,
                                                            guint count,
                                                            RHKObjectType type,
                                                            RHKObjectType parenttype,
                                                            GError **error);
static const gchar*         rhk_sm4_describe_object        (RHKObjectType type);
static void                 rhk_sm4_free                   (RHKFile *rhkfile);
static GwyDataField*        rhk_sm4_page_to_data_field     (const RHKPage *page);
static GwyGraphModel*       rhk_sm4_page_to_graph_model    (const RHKPage *page);
static GwyContainer*        rhk_sm4_get_metadata           (const RHKPageIndex *pi,
                                                            const RHKPage *page,
                                                            GwyContainer *basemeta);
static void                 rhk_sm4_add_ppl_pro_status_meta(const RHKPage *page,
                                                            GwyContainer *meta);
static GwyContainer*        rhk_sm4_read_prm               (const RHKObject *prmheader,
                                                            const RHKObject *prm,
                                                            const guchar *buffer);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports RHK Technology SM4 data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.8",
    "David Nečas (Yeti)",
    "2009",
};

static const GwyEnum scan_directions[] = {
    { "Right", RHK_SCAN_RIGHT, },
    { "Left",  RHK_SCAN_LEFT,  },
    { "Up",    RHK_SCAN_UP,    },
    { "Down",  RHK_SCAN_DOWN,  },
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    gwy_file_func_register("rhk-sm4",
                           N_("RHK SM4 files (.sm4)"),
                           (GwyFileDetectFunc)&rhk_sm4_detect,
                           (GwyFileLoadFunc)&rhk_sm4_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
rhk_sm4_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len > MAGIC_TOTAL_SIZE
        && memcmp(fileinfo->head + MAGIC_OFFSET, MAGIC, MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static GwyContainer*
rhk_sm4_load(const gchar *filename,
             G_GNUC_UNUSED GwyRunType mode,
             GError **error)
{
    RHKFile rhkfile;
    RHKObject *obj, o;
    GwyContainer *meta, *prmmeta = NULL, *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    GError *err = NULL;
    const guchar *p;
    GString *key = NULL;
    guint i, imageid = 0, graphid = 0;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&rhkfile, 1);
    if (size < HEADER_SIZE) {
        err_TOO_SHORT(error);
        goto fail;
    }

    /* File header */
    p = buffer + MAGIC_OFFSET + MAGIC_TOTAL_SIZE;
    rhkfile.page_count = gwy_get_guint32_le(&p);
    rhkfile.object_count = gwy_get_guint32_le(&p);
    rhkfile.object_field_size = gwy_get_guint32_le(&p);
    gwy_debug("page_count: %u, object_count: %u, object_field_size: %u",
              rhkfile.page_count, rhkfile.object_count, rhkfile.object_field_size);
    if (rhkfile.object_field_size != OBJECT_SIZE)
        g_warning("Object field size %u differs from %u", rhkfile.object_field_size, OBJECT_SIZE);
    rhkfile.reserved1 = gwy_get_guint32_le(&p);
    rhkfile.reserved2 = gwy_get_guint32_le(&p);

    /* Header objects */
    if (!(rhkfile.objects = rhk_sm4_read_objects(buffer, p, size, rhkfile.object_count, RHK_OBJECT_FILE_HEADER, error)))
        goto fail;

    /* Find page index header */
    if (!(obj = rhk_sm4_find_object(rhkfile.objects, rhkfile.object_count,
                                    RHK_OBJECT_PAGE_INDEX_HEADER, RHK_OBJECT_FILE_HEADER, error))
        || !rhk_sm4_read_page_index_header(&rhkfile.page_index_header, obj, buffer, size, error))
        goto fail;

    /* There, find the page index array.  That's a single object in the object
     * list but it contains a page_count-long sequence of page indices. */
    rhkfile.page_indices = g_new0(RHKPageIndex,
                                  rhkfile.page_index_header.page_count);
    if (!(obj = rhk_sm4_find_object(rhkfile.page_index_header.objects, rhkfile.page_index_header.object_count,
                                    RHK_OBJECT_PAGE_INDEX_ARRAY, RHK_OBJECT_PAGE_INDEX_HEADER, error)))
        goto fail;

    o = *obj;
    for (i = 0; i < rhkfile.page_index_header.page_count; i++) {
        if (!rhk_sm4_read_page_index(rhkfile.page_indices + i, &o, buffer, size, error))
            goto fail;

        /* Carefully move to the next page index */
        o.offset += o.size + OBJECT_SIZE*rhkfile.page_indices[i].object_count;
    }

    container = gwy_container_new();
    key = g_string_new(NULL);

    /* PRM metadata */
    if ((obj = rhk_sm4_find_object(rhkfile.objects, rhkfile.object_count,
                                   RHK_OBJECT_PRM_HEADER, RHK_OBJECT_FILE_HEADER, NULL))) {
        RHKObject *obj2;
        if ((obj2 = rhk_sm4_find_object(rhkfile.objects, rhkfile.object_count,
                                        RHK_OBJECT_PRM, RHK_OBJECT_FILE_HEADER, NULL))) {
            prmmeta = rhk_sm4_read_prm(obj, obj2, buffer);
        }
    }

    /* Read pages */
    for (i = 0; i < rhkfile.page_index_header.page_count; i++) {
        RHKPageIndex *pi = rhkfile.page_indices + i;
        RHKPage *page = &pi->page;

        /* Page must contain header */
        if (!(obj = rhk_sm4_find_object(pi->objects, pi->object_count,
                                        RHK_OBJECT_PAGE_HEADER, RHK_OBJECT_PAGE_INDEX, error))
            || !rhk_sm4_read_page_header(page, obj, pi->data_type, buffer, size, error))
            goto fail;

        /* Page must contain data */
        if (!(obj = rhk_sm4_find_object(pi->objects, pi->object_count,
                                        RHK_OBJECT_PAGE_DATA, RHK_OBJECT_PAGE_INDEX, error))
            || !rhk_sm4_read_page_data(page, obj, buffer, error))
            goto fail;

        /* Page may contain strings */
        if (!(obj = rhk_sm4_find_object(page->objects, page->object_count,
                                        RHK_OBJECT_STRING_DATA, RHK_OBJECT_PAGE_HEADER, NULL))
            || !rhk_sm4_read_string_data(page, obj, pi->page.string_count, buffer)) {
            g_warning("Failed to read string data in page %u", i);
        }
        /* Page may contain piezo sensitivity */
        if ((obj = rhk_sm4_find_object(page->objects, page->object_count,
                                       RHK_OBJECT_PIEZO_SENSITIVITY, RHK_OBJECT_PAGE_HEADER, NULL)))
            page->piezo_sensitivity = rhk_sm4_read_piezo_sensitivity(obj, buffer);

        /* Read the data */
        if (pi->data_type == RHK_DATA_IMAGE) {
            GwyDataField *dfield = rhk_sm4_page_to_data_field(page);
            GQuark quark = gwy_app_get_data_key_for_id(imageid);
            const gchar *scandir, *name;
            gchar *title;

            gwy_container_pass_object(container, quark, dfield);

            if ((name = page->strings[RHK_STRING_LABEL])) {
                scandir = gwy_enum_to_string(page->scan_dir, scan_directions, G_N_ELEMENTS(scan_directions));
                g_string_assign(key, g_quark_to_string(quark));
                g_string_append(key, "/title");
                if (scandir && *scandir)
                    title = g_strdup_printf("%s [%s]", name, scandir);
                else
                    title = g_strdup(name);
                gwy_container_set_string_by_name(container, key->str, title);
            }
            /* XXX: Maybe we should preferably show topograhical images?
             * Suggested by Andrés Muñiz Piniella.
            if (page->page_type == RHK_PAGE_TOPOGRAPHIC) {
                g_string_assign(key, g_quark_to_string(quark));
                g_string_append(key, "/visible");
                gwy_container_set_boolean_by_name(container, key->str, TRUE);
            }
            */

            meta = rhk_sm4_get_metadata(pi, page, prmmeta);
            g_string_printf(key, "/%u/meta", imageid);
            gwy_container_pass_object_by_name(container, key->str, meta);

            gwy_file_channel_import_log_add(container, imageid, NULL, filename);

            imageid++;
        }
        else if (pi->data_type == RHK_DATA_LINE) {
            GwyGraphModel *gmodel;

            gwy_debug("page_type %u", page->page_type);
            gwy_debug("line_type %u", page->line_type);
            gwy_debug("page_sizes %u %u", page->x_size, page->y_size);
            /* Page may contain drift header */
            if ((obj = rhk_sm4_find_object(page->objects, page->object_count,
                                           RHK_OBJECT_SPEC_DRIFT_HEADER, RHK_OBJECT_PAGE_HEADER, NULL)))
                page->drift_header = rhk_sm4_read_drift_header(obj, buffer);
            if ((obj = rhk_sm4_find_object(page->objects, page->object_count,
                                           RHK_OBJECT_SPEC_DRIFT_DATA, RHK_OBJECT_PAGE_HEADER, NULL)))
                page->spec_info = rhk_sm4_read_spec_info(obj, buffer, size, page->y_size);
            if ((gmodel = rhk_sm4_page_to_graph_model(page))) {
                graphid++;
                gwy_container_pass_object(container, gwy_app_get_graph_key_for_id(graphid), gmodel);
            }
        }
    }

    if (!imageid && !graphid)
        err_NO_DATA(error);

fail:
    gwy_file_abandon_contents(buffer, size, NULL);
    rhk_sm4_free(&rhkfile);
    GWY_OBJECT_UNREF(prmmeta);
    if (!imageid && !graphid) {
        GWY_OBJECT_UNREF(container);
    }
    if (key)
        g_string_free(key, TRUE);

    return container;
}

static inline void
err_OBJECT_TRUNCATED(GError **error, RHKObjectType type)
{
    err_TRUNCATED_PART(error, rhk_sm4_describe_object(type));
}

static gboolean
rhk_sm4_read_page_index_header(RHKPageIndexHeader *header,
                               const RHKObject *obj,
                               const guchar *buffer,
                               gsize size,
                               GError **error)
{
    const guchar *p;

    if (obj->size < PAGE_INDEX_HEADER_SIZE) {
        err_OBJECT_TRUNCATED(error, RHK_OBJECT_PAGE_INDEX_HEADER);
        return FALSE;
    }

    p = buffer + obj->offset;
    header->page_count = gwy_get_guint32_le(&p);
    header->object_count = gwy_get_guint32_le(&p);
    gwy_debug("page_count: %u, object_count: %u",
              header->page_count, header->object_count);
    header->reserved1 = gwy_get_guint32_le(&p);
    header->reserved2 = gwy_get_guint32_le(&p);

    if (!(header->objects = rhk_sm4_read_objects(buffer, p, size, header->object_count, RHK_OBJECT_PAGE_INDEX_HEADER,
                                                 error)))
        return FALSE;

    return TRUE;
}

static gboolean
rhk_sm4_read_page_index(RHKPageIndex *header, const RHKObject *obj,
                        const guchar *buffer, gsize size, GError **error)
{
    const guchar *p;

    if (obj->size < PAGE_INDEX_ARRAY_SIZE) {
        err_OBJECT_TRUNCATED(error, RHK_OBJECT_PAGE_INDEX_ARRAY);
        return FALSE;
    }

    p = buffer + obj->offset;
    memcpy(header->id, p, sizeof(header->id));
    p += sizeof(header->id);
    header->data_type = gwy_get_guint32_le(&p);
    header->source = gwy_get_guint32_le(&p);
    header->object_count = gwy_get_guint32_le(&p);
    header->minor_version = gwy_get_guint32_le(&p);
    gwy_debug("data_type: %u, source: %u, object_count: %u, minorv: %u",
              header->data_type, header->source, header->object_count, header->minor_version);

    if (!(header->objects = rhk_sm4_read_objects(buffer, p, size, header->object_count, RHK_OBJECT_PAGE_INDEX_ARRAY,
                                                 error)))
        return FALSE;

    return TRUE;
}

static gboolean
rhk_sm4_read_page_header(RHKPage *page, const RHKObject *obj, RHKDataType data_type,
                         const guchar *buffer, gsize size, GError **error)
{
    const guchar *p;
    guint i;

    if (obj->size < PAGE_HEADER_SIZE) {
        err_OBJECT_TRUNCATED(error, RHK_OBJECT_PAGE_HEADER);
        return FALSE;
    }

    p = buffer + obj->offset;
    page->field_size = gwy_get_guint16_le(&p);
    if (obj->size < page->field_size) {
        err_OBJECT_TRUNCATED(error, RHK_OBJECT_PAGE_HEADER);
        return FALSE;
    }

    page->string_count = gwy_get_guint16_le(&p);
    gwy_debug("string_count = %u", page->string_count);
    page->page_type = gwy_get_guint32_le(&p);
    gwy_debug("page_type = %u", page->page_type);
    page->data_sub_source = gwy_get_guint32_le(&p);
    page->line_type = gwy_get_guint32_le(&p);
    page->x_coord = gwy_get_gint32_le(&p);
    page->y_coord = gwy_get_gint32_le(&p);
    gwy_debug("x_coord = %u, y_coord = %u", page->x_coord, page->y_coord);
    page->x_size = gwy_get_guint32_le(&p);
    page->y_size = gwy_get_guint32_le(&p);
    gwy_debug("x_size = %u, y_size = %u", page->x_size, page->y_size);
    /* Non-image data can have y_size=1 and huge x_size.  This prevents using
     * err_DIMENSION() and we must check the actual size for sanity. */
    if (data_type == RHK_DATA_IMAGE && (err_DIMENSION(error, page->x_size) || err_DIMENSION(error, page->y_size)))
        return FALSE;
    if (page->x_size > 0x80000000u/page->y_size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Invalid field dimension: %d."),
                    MAX((gint)page->x_size, (gint)page->y_size));
        return FALSE;
    }
    page->image_type = gwy_get_guint32_le(&p);
    gwy_debug("image_type = %u", page->image_type);
    page->scan_dir = gwy_get_guint32_le(&p);
    gwy_debug("scan_dir = %u", page->scan_dir);
    page->group_id = gwy_get_guint32_le(&p);
    gwy_debug("group_id = 0x%08x", page->group_id);
    page->data_size = gwy_get_guint32_le(&p);
    gwy_debug("data_size = %u", page->data_size);
    page->min_z_value = gwy_get_gint32_le(&p);
    page->max_z_value = gwy_get_gint32_le(&p);
    gwy_debug("min,max_z_value = %d %d", page->min_z_value, page->max_z_value);
    page->x_scale = gwy_get_gfloat_le(&p);
    page->y_scale = gwy_get_gfloat_le(&p);
    page->z_scale = gwy_get_gfloat_le(&p);
    gwy_debug("x,y,z_scale = %g %g %g", page->x_scale, page->y_scale, page->z_scale);
    /* Use negated positive conditions to catch NaNs */
    /* Must not take the absolute value here, spectra may have valid negative
     * scales. */
    if (!(page->x_scale != 0.0)) {
        g_warning("Real x scale is 0.0, fixing to 1.0");
        page->x_scale = 1.0;
    }
    if (!(page->y_scale != 0.0)) {
        if (data_type == RHK_DATA_IMAGE)
            g_warning("Real y scale is 0.0, fixing to 1.0");
        page->y_scale = 1.0;
    }
    page->xy_scale = gwy_get_gfloat_le(&p);
    page->x_offset = gwy_get_gfloat_le(&p);
    page->y_offset = gwy_get_gfloat_le(&p);
    page->z_offset = gwy_get_gfloat_le(&p);
    gwy_debug("x,y,z_offset = %g %g %g", page->x_offset, page->y_offset, page->z_offset);
    page->period = gwy_get_gfloat_le(&p);
    page->bias = gwy_get_gfloat_le(&p);
    page->current = gwy_get_gfloat_le(&p);
    page->angle = gwy_get_gfloat_le(&p);
    gwy_debug("period = %g, bias = %g, current = %g, angle = %g", page->period, page->bias, page->current, page->angle);
    page->color_info_count = gwy_get_guint32_le(&p);
    gwy_debug("color_info_count = %u", page->color_info_count);
    page->grid_x_size = gwy_get_guint32_le(&p);
    page->grid_y_size = gwy_get_guint32_le(&p);
    gwy_debug("gird_x,y = %u %u", page->grid_x_size, page->grid_y_size);
    page->object_count = gwy_get_guint32_le(&p);
    for (i = 0; i < G_N_ELEMENTS(page->reserved); i++)
        page->reserved[i] = gwy_get_guint32_le(&p);

    if (!(page->objects = rhk_sm4_read_objects(buffer, p, size, page->object_count, RHK_OBJECT_PAGE_HEADER, error)))
        return FALSE;

    return TRUE;
}

static gboolean
rhk_sm4_read_page_data(RHKPage *page, const RHKObject *obj,
                       const guchar *buffer, GError **error)
{
    gsize expected_size;

    expected_size = 4 * page->x_size * page->y_size;
    if (err_SIZE_MISMATCH(error, expected_size, obj->size, TRUE))
        return FALSE;

    page->data = buffer + obj->offset;

    return TRUE;
}

static gchar*
rhk_sm4_read_string(const guchar **p, const guchar *end)
{
    gchar *s;
    guint len;

    if (end - *p < sizeof(guint16))
        return NULL;

    len = gwy_get_guint16_le(p);
    if (len > (end - *p)/sizeof(gunichar2))
        return NULL;

    s = gwy_utf16_to_utf8((const gunichar2*)*p, len, GWY_BYTE_ORDER_LITTLE_ENDIAN);
    *p += len*sizeof(gunichar2);

    return s;
}

static gboolean
rhk_sm4_read_string_data(RHKPage *page, const RHKObject *obj,
                         guint count, const guchar *buffer)
{
    const guchar *p = buffer + obj->offset, *end = p + obj->size;
    guint i;

    gwy_debug("count: %u, known strings: %u", count, RHK_STRING_NSTRINGS);
    count = MIN(count, RHK_STRING_NSTRINGS);
    for (i = 0; i < count; i++) {
        if (!(page->strings[i] = rhk_sm4_read_string(&p, end)))
            return FALSE;
        gwy_debug("string[%u]: <%s>", i, page->strings[i]);
    }

    return TRUE;
}

static RHKSpecDriftHeader*
rhk_sm4_read_drift_header(const RHKObject *obj, const guchar *buffer)
{
    RHKSpecDriftHeader *drift_header = NULL;
    const guchar *p = buffer + obj->offset, *end = p + obj->size;
    guint i, nstrings;

    if (obj->size < 16)
        return drift_header;

    drift_header = g_new0(RHKSpecDriftHeader, 1);
    drift_header->start_time = gwy_get_guint64_le(&p);
    drift_header->drift_opt = gwy_get_gint16_le(&p);
    drift_header->nstrings = nstrings = gwy_get_guint16_le(&p);
    gwy_debug("nstrings = %u", nstrings);
    drift_header->strings = g_new0(gchar*, nstrings+1);
    for (i = 0; i < nstrings; i++) {
        if (!(drift_header->strings[i] = rhk_sm4_read_string(&p, end))) {
            g_strfreev(drift_header->strings);
            g_free(drift_header);
            return NULL;
        }
        gwy_debug("string[%u] = <%s>", i, drift_header->strings[i]);
    }

    return drift_header;
}

static RHKPiezoSensitivity*
rhk_sm4_read_piezo_sensitivity(const RHKObject *obj, const guchar *buffer)
{
    RHKPiezoSensitivity *piezo_sensitivity = NULL;
    const guchar *p = buffer + obj->offset, *end = p + obj->size;
    guint i, nstrings;

    if (obj->size < 8*sizeof(gdouble) + sizeof(guint16))
        return piezo_sensitivity;

    piezo_sensitivity = g_new0(RHKPiezoSensitivity, 1);
    piezo_sensitivity->tube_x = gwy_get_gdouble_le(&p);
    piezo_sensitivity->tube_y = gwy_get_gdouble_le(&p);
    piezo_sensitivity->tube_z = gwy_get_gdouble_le(&p);
    gwy_debug("tube x %g, y %g, z %g",
              piezo_sensitivity->tube_x, piezo_sensitivity->tube_y, piezo_sensitivity->tube_z);
    piezo_sensitivity->tube_z_offset = gwy_get_gdouble_le(&p);
    piezo_sensitivity->scan_x = gwy_get_gdouble_le(&p);
    piezo_sensitivity->scan_y = gwy_get_gdouble_le(&p);
    piezo_sensitivity->scan_z = gwy_get_gdouble_le(&p);
    gwy_debug("scan x %g, y %g, z %g",
              piezo_sensitivity->scan_x, piezo_sensitivity->scan_y, piezo_sensitivity->scan_z);
    piezo_sensitivity->actuator = gwy_get_gdouble_le(&p);
    piezo_sensitivity->string_count = gwy_get_guint32_le(&p);
    gwy_debug("string_count = %u", piezo_sensitivity->string_count);
    nstrings = MIN(piezo_sensitivity->string_count, RHK_PIEZO_NSTRINGS);
    /* XXX: This never reads anything because obj->size is always just 68, i.e. only large
     * enough to hold the doubles and string count.  Even if there seem to be actually some
     * strings.  Other objects report their size correctly.  Why? */
    for (i = 0; i < nstrings; i++) {
        if (!(piezo_sensitivity->strings[i] = rhk_sm4_read_string(&p, end))) {
            while (i--)
                g_free(piezo_sensitivity->strings[i]);
            g_free(piezo_sensitivity);
            return NULL;
        }
        gwy_debug("string[%u] = <%s>", i, piezo_sensitivity->strings[i]);
    }

    return piezo_sensitivity;
}

static RHKSpecInfo*
rhk_sm4_read_spec_info(const RHKObject *obj,
                       const guchar *buffer, gsize size, guint nspec)
{
    enum { SPEC_INFO_SIZE = 28 };

    const guchar *p = buffer + obj->offset;
    RHKSpecInfo *spec_infos = NULL;
    guint i;

    if (obj->size != SPEC_INFO_SIZE)
        return spec_infos;
    if (obj->offset + nspec*SPEC_INFO_SIZE >= size)
        return spec_infos;

    spec_infos = g_new(RHKSpecInfo, nspec);
    for (i = 0; i < nspec; i++) {
        RHKSpecInfo *spec_info = spec_infos + i;
        spec_info->ftime = gwy_get_gfloat_le(&p);
        spec_info->x_coord = gwy_get_gfloat_le(&p);
        spec_info->y_coord = gwy_get_gfloat_le(&p);
        gwy_debug("[%u] x_coord = %g, y_coord = %g", i, spec_info->x_coord, spec_info->y_coord);
        spec_info->dx = gwy_get_gfloat_le(&p);
        spec_info->dy = gwy_get_gfloat_le(&p);
        spec_info->cumulative_dx = gwy_get_gfloat_le(&p);
        spec_info->cumulative_dy = gwy_get_gfloat_le(&p);
    }
    return spec_infos;
}

/* FIXME: Some of the objects read are of type 0 and size 0, but maybe that's right and they allow empty object slots
 */
static RHKObject*
rhk_sm4_read_objects(const guchar *buffer, const guchar *p, gsize size, guint count,
                     RHKObjectType intype, GError **error)
{
    RHKObject *objects, *obj;
    guint i;

    if ((p - buffer) + count*OBJECT_SIZE >= size) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Object list in %s is truncated."),
                    rhk_sm4_describe_object(intype));
        return NULL;
    }

    objects = g_new(RHKObject, count);
    for (i = 0; i < count; i++) {
        obj = objects + i;
        obj->type = gwy_get_guint32_le(&p);
        obj->offset = gwy_get_guint32_le(&p);
        obj->size = gwy_get_guint32_le(&p);
        gwy_debug("object of type %u (%s) at %u, size %u",
                  obj->type, rhk_sm4_describe_object(obj->type), obj->offset, obj->size);
        if ((gsize)obj->size + obj->offset > size) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Object of type %s is truncated."),
                        rhk_sm4_describe_object(obj->type));
            g_free(objects);
            return NULL;
        }
    }

    return objects;
}

static RHKObject*
rhk_sm4_find_object(RHKObject *objects, guint count,
                    RHKObjectType type, RHKObjectType parenttype,
                    GError **error)
{
    guint i;

    for (i = 0; i < count; i++) {
        RHKObject *obj = objects + i;

        if (obj->type == type)
            return obj;
    }

    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Cannot find object %s in %s."),
                rhk_sm4_describe_object(type), rhk_sm4_describe_object(parenttype));
    return NULL;
}

static const gchar*
rhk_sm4_describe_object(RHKObjectType type)
{
    static const GwyEnum types[] = {
        { "Undefined",          RHK_OBJECT_UNDEFINED,            },
        { "PageIndexHeader",    RHK_OBJECT_PAGE_INDEX_HEADER,    },
        { "PageIndexArray",     RHK_OBJECT_PAGE_INDEX_ARRAY,     },
        { "PageHeader",         RHK_OBJECT_PAGE_HEADER,          },
        { "PageData",           RHK_OBJECT_PAGE_DATA,            },
        { "ImageDriftHeader",   RHK_OBJECT_IMAGE_DRIFT_HEADER,   },
        { "ImageDrift",         RHK_OBJECT_IMAGE_DRIFT,          },
        { "SpecDriftHeader",    RHK_OBJECT_SPEC_DRIFT_HEADER,    },
        { "SpecDriftData",      RHK_OBJECT_SPEC_DRIFT_DATA,      },
        { "ColorInfo",          RHK_OBJECT_COLOR_INFO,           },
        { "StringData",         RHK_OBJECT_STRING_DATA,          },
        { "TipTrackHeader",     RHK_OBJECT_TIP_TRACK_HEADER,     },
        { "TipTrackData",       RHK_OBJECT_TIP_TRACK_DATA,       },
        { "PRM",                RHK_OBJECT_PRM,                  },
        { "Thumbnail",          RHK_OBJECT_THUMBNAIL,            },
        { "PRMHeader",          RHK_OBJECT_PRM_HEADER,           },
        { "ThumbnailHeader",    RHK_OBJECT_THUMBNAIL_HEADER,     },
        { "APIInfo",            RHK_OBJECT_API_INFO,             },
        { "HistoryInfo",        RHK_OBJECT_HISTORY_INFO,         },
        { "PiezoSensitivity",   RHK_OBJECT_PIEZO_SENSITIVITY,    },
        { "FrequencySweepData", RHK_OBJECT_FREQUENCY_SWEEP_DATA, },
        { "ScanProcessorInfo",  RHK_OBJECT_SCAN_PROCESSOR_INFO,  },
        { "PLLInfo",            RHK_OBJECT_PLL_INFO,             },
        { "Ch1DriveInfo",       RHK_OBJECT_CH1_DRIVE_INFO,       },
        { "Ch2DriveInfo",       RHK_OBJECT_CH2_DRIVE_INFO,       },
        { "Lockin0Info",        RHK_OBJECT_LOCKIN0_INFO,         },
        { "Lockin1Info",        RHK_OBJECT_LOCKIN1_INFO,         },
        { "ZPIInfo",            RHK_OBJECT_ZPI_INFO,             },
        { "KPIInfo",            RHK_OBJECT_KPI_INFO,             },
        { "AuxPIInfo",          RHK_OBJECT_AUX_PI_INFO,          },
        { "LowpassFilter0Info", RHK_OBJECT_LOWPASS_FILTER0_INFO, },
        { "LowpassFilter1Info", RHK_OBJECT_LOWPASS_FILTER1_INFO, },
        /* Our types */
        { "FileHeader",         RHK_OBJECT_FILE_HEADER,          },
        { "PageIndex",          RHK_OBJECT_PAGE_INDEX,           },
    };

    const gchar *retval;

    retval = gwy_enum_to_string(type, types, G_N_ELEMENTS(types));
    if (!retval || !*retval)
        return "Unknown";

    return retval;
};

static void
rhk_sm4_free(RHKFile *rhkfile)
{
    RHKPage *page;
    guint i, j;

    g_free(rhkfile->objects);
    g_free(rhkfile->page_index_header.objects);
    if (rhkfile->page_indices) {
        for (i = 0; i < rhkfile->page_index_header.page_count; i++) {
            g_free(rhkfile->page_indices[i].objects);
            page = &rhkfile->page_indices[i].page;
            for (j = 0; j < RHK_STRING_NSTRINGS; j++)
                g_free(page->strings[j]);
            if (page->drift_header) {
                g_strfreev(page->drift_header->strings);
                g_free(page->drift_header);
            }
            if (page->piezo_sensitivity) {
                for (j = 0; j < RHK_PIEZO_NSTRINGS; j++)
                    g_free(page->piezo_sensitivity->strings[j]);
                g_free(page->piezo_sensitivity);
            }
            g_free(page->spec_info);
            g_free(page->objects);
        }
        g_free(rhkfile->page_indices);
    }
}

static GwyDataField*
rhk_sm4_page_to_data_field(const RHKPage *page)
{
    GwyDataField *dfield;
    GwySIUnit *siunit;
    const gchar *unit;
    const gint32 *pdata;
    gint xres, yres, i, j;
    gdouble *data;

    xres = page->x_size;
    yres = page->y_size;
    dfield = gwy_data_field_new(xres, yres, xres*fabs(page->x_scale), yres*fabs(page->y_scale), FALSE);
    data = gwy_data_field_get_data(dfield);
    pdata = (const gint32*)page->data;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++)
            data[i*xres + xres-1 - j] = GINT32_FROM_LE(pdata[i*xres + j])*page->z_scale + page->z_offset;
    }

    /* Correct flipping of up images */
    if (page->y_scale > 0.0)
        gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);

    /* XY units */
    if (page->strings[RHK_STRING_X_UNITS]
        && page->strings[RHK_STRING_Y_UNITS]) {
        if (!gwy_strequal(page->strings[RHK_STRING_X_UNITS], page->strings[RHK_STRING_Y_UNITS]))
            g_warning("X and Y units differ, using X");
        unit = page->strings[RHK_STRING_X_UNITS];
    }
    else if (page->strings[RHK_STRING_X_UNITS])
        unit = page->strings[RHK_STRING_X_UNITS];
    else if (page->strings[RHK_STRING_Y_UNITS])
        unit = page->strings[RHK_STRING_Y_UNITS];
    else
        unit = NULL;

    siunit = gwy_data_field_get_si_unit_xy(dfield);
    gwy_si_unit_set_from_string(siunit, unit);

    /* Z units */
    if (page->strings[RHK_STRING_Z_UNITS])
        unit = page->strings[RHK_STRING_Z_UNITS];
    else
        unit = NULL;
    /* Fix some silly units */
    if (unit && gwy_strequal(unit, "N/sec"))
        unit = "s^-1";
    else if (unit && gwy_stramong(unit, "Vrms", "Vp", NULL))
        unit = "V";

    siunit = gwy_data_field_get_si_unit_z(dfield);
    gwy_si_unit_set_from_string(siunit, unit);

    return dfield;
}

static GwyGraphModel*
rhk_sm4_page_to_graph_model(const RHKPage *page)
{
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GwySIUnit *siunit;
    const gint32 *pdata;
    const gchar *name;
    gint res, ncurves, i, j;
    gdouble *xdata, *ydata;

    res = page->x_size;
    ncurves = page->y_size;

    gmodel = gwy_graph_model_new();
    pdata = (const gint32*)page->data;
    xdata = g_new(gdouble, res);
    ydata = g_new(gdouble, res);
    name = page->strings[RHK_STRING_LABEL];
    for (i = 0; i < ncurves; i++) {
        gcmodel = gwy_graph_curve_model_new();
        for (j = 0; j < res; j++) {
            xdata[j] = j*page->x_scale + page->x_offset;
            ydata[j] = (GINT32_FROM_LE(pdata[i*res + j])*page->z_scale + page->z_offset);
        }
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, res);
        gwy_graph_curve_model_enforce_order(gcmodel);
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "color", gwy_graph_get_preset_color(i),
                     NULL);
        if (name)
            g_object_set(gcmodel, "description", name, NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);
    }
    g_free(ydata);
    g_free(xdata);

    /* Units */
    siunit = gwy_si_unit_new(page->strings[RHK_STRING_X_UNITS]);
    g_object_set(gmodel, "si-unit-x", siunit, NULL);
    g_object_unref(siunit);

    siunit = gwy_si_unit_new(page->strings[RHK_STRING_Z_UNITS]);
    g_object_set(gmodel, "si-unit-y", siunit, NULL);
    g_object_unref(siunit);

    if (name)
        g_object_set(gmodel, "title", name, NULL);

    return gmodel;
}

static void
rhk_sm4_meta_string(const RHKPage *page, RHKStringType stringid, const gchar *name, GwyContainer *meta)
{
    const gchar *s;

    g_return_if_fail(stringid < RHK_STRING_NSTRINGS);
    if ((s = page->strings[stringid]))
        gwy_container_set_const_string_by_name(meta, name, s);
}

static const gchar*
make_prefix(GString *str, const gchar *prefix, const gchar *name)
{
    if (!prefix || !*prefix)
        return name;
    g_string_assign(str, prefix);
    g_string_append(str, "::");
    g_string_append(str, name);
    return str->str;
}

static void
set_meta_double(GwyContainer *meta, GString *str, const gchar *prefix, const gchar *name,
                gdouble value, const gchar *unit)
{
    gchar *s = g_strdup_printf("%g%s%s", value, unit ? " " : "", unit ? unit : "");
    gwy_container_set_string_by_name(meta, make_prefix(str, prefix, name), s);
}

static void
set_meta_int(GwyContainer *meta, GString *str, const gchar *prefix, const gchar *name,
             gint value)
{
    gchar *s = g_strdup_printf("%d", value);
    gwy_container_set_string_by_name(meta, make_prefix(str, prefix, name), s);
}

static GwyContainer*
rhk_sm4_get_metadata(const RHKPageIndex *pi, const RHKPage *page, GwyContainer *basemeta)
{
    static const gchar hex[] = "0123456789abcdef";

    GwyContainer *meta;
    GString *key;
    const gchar *s;
    gchar *str;
    guint i, w;

    if (basemeta)
        meta = gwy_container_duplicate(basemeta);
    else
        meta = gwy_container_new();

    key = g_string_new(NULL);
    s = gwy_enuml_to_string(page->page_type,
                            "Topographic", RHK_PAGE_TOPOGRAPHIC,
                            "Current", RHK_PAGE_CURRENT,
                            "Aux", RHK_PAGE_AUX,
                            "Force", RHK_PAGE_FORCE,
                            "Signal", RHK_PAGE_SIGNAL,
                            "FFT transform", RHK_PAGE_FFT,
                            "Noise power spectrum", RHK_PAGE_NOISE_POWER_SPECTRUM,
                            "Line test", RHK_PAGE_LINE_TEST,
                            "Oscilloscope", RHK_PAGE_OSCILLOSCOPE,
                            "IV spectra", RHK_PAGE_IV_SPECTRA,
                            "Image IV 4x4", RHK_PAGE_IV_4x4,
                            "Image IV 8x8", RHK_PAGE_IV_8x8,
                            "Image IV 16x16", RHK_PAGE_IV_16x16,
                            "Image IV 32x32", RHK_PAGE_IV_32x32,
                            "Image IV Center", RHK_PAGE_IV_CENTER,
                            "Interactive spectra", RHK_PAGE_INTERACTIVE_SPECTRA,
                            "Autocorrelation", RHK_PAGE_AUTOCORRELATION,
                            "IZ spectra", RHK_PAGE_IZ_SPECTRA,
                            "4 gain topography", RHK_PAGE_4_GAIN_TOPOGRAPHY,
                            "8 gain topography", RHK_PAGE_8_GAIN_TOPOGRAPHY,
                            "4 gain current", RHK_PAGE_4_GAIN_CURRENT,
                            "8 gain current", RHK_PAGE_8_GAIN_CURRENT,
                            "Image IV 64x64", RHK_PAGE_IV_64x64,
                            "Autocorrelation spectrum", RHK_PAGE_AUTOCORRELATION_SPECTRUM,
                            "Counter data", RHK_PAGE_COUNTER,
                            "Multichannel analyser", RHK_PAGE_MULTICHANNEL_ANALYSER,
                            "AFM using AFM-100", RHK_PAGE_AFM_100,
                            "CITS", RHK_PAGE_CITS,
                            "GBIB", RHK_PAGE_GPIB,
                            "Video channel", RHK_PAGE_VIDEO_CHANNEL,
                            "Image OUT spectra", RHK_PAGE_IMAGE_OUT_SPECTRA,
                            "I_Datalog", RHK_PAGE_I_DATALOG,
                            "I_Ecset", RHK_PAGE_I_ECSET,
                            "I_Ecdata", RHK_PAGE_I_ECDATA,
                            "DSP channel", RHK_PAGE_I_DSP_AD,
                            "Discrete spectroscopy (present pos)", RHK_PAGE_DISCRETE_SPECTROSCOPY_PP,
                            "Image discrete spectroscopy", RHK_PAGE_IMAGE_DISCRETE_SPECTROSCOPY,
                            "Ramp spectroscopy (relative points)", RHK_PAGE_RAMP_SPECTROSCOPY_RP,
                            "Discrete spectroscopy (relative points)", RHK_PAGE_DISCRETE_SPECTROSCOPY_RP,
                            NULL);
    if (s && *s)
        gwy_container_set_const_string_by_name(meta, "Type", s);

    s = gwy_enum_to_string(page->scan_dir, scan_directions, G_N_ELEMENTS(scan_directions));
    if (s && *s)
        gwy_container_set_const_string_by_name(meta, "Scan Direction", s);

    s = gwy_enuml_to_string(pi->source,
                            "Raw", RHK_SOURCE_RAW,
                            "Processed", RHK_SOURCE_PROCESSED,
                            "Calculated", RHK_SOURCE_CALCULATED,
                            "Imported", RHK_SOURCE_IMPORTED,
                            NULL);
    if (s && *s)
        gwy_container_set_const_string_by_name(meta, "Source", s);

    set_meta_double(meta, key, NULL, "Bias", page->bias, "V");
    set_meta_double(meta, key, NULL, "Rotation angle", page->angle, "deg");
    set_meta_double(meta, key, NULL, "Period", page->period, "s");
    set_meta_int(meta, key, NULL, "X coordinate", page->x_coord);
    set_meta_int(meta, key, NULL, "Y coordinate", page->y_coord);
    set_meta_int(meta, key, NULL, "X size", page->x_size);
    set_meta_int(meta, key, NULL, "Y size", page->y_size);
    set_meta_int(meta, key, NULL, "Min Z value", page->min_z_value);
    set_meta_int(meta, key, NULL, "Max Z value", page->max_z_value);
    set_meta_double(meta, key, NULL, "X scale", page->x_scale, NULL);
    set_meta_double(meta, key, NULL, "Y scale", page->y_scale, NULL);
    set_meta_double(meta, key, NULL, "Z scale", page->z_scale, NULL);
    set_meta_double(meta, key, NULL, "XY scale", page->xy_scale, NULL);
    set_meta_double(meta, key, NULL, "X offset", page->x_offset, NULL);
    set_meta_double(meta, key, NULL, "Y offset", page->y_offset, NULL);
    set_meta_double(meta, key, NULL, "Z offset", page->z_offset, NULL);
    set_meta_double(meta, key, NULL, "Current", page->current, "A");
    set_meta_int(meta, key, NULL, "Color Info Count", page->color_info_count);
    set_meta_int(meta, key, NULL, "Grid X size", page->grid_x_size);
    set_meta_int(meta, key, NULL, "Grid Y size", page->grid_y_size);
    /* For "line type" and "image type" is needded "gwy_enuml_to_string" as "page type" above */
    set_meta_int(meta, key, NULL, "Line type", page->line_type);
    set_meta_int(meta, key, NULL, "Image type", page->image_type);

    s = page->strings[RHK_STRING_DATE];
    if (s && *s) {
        str = g_strconcat(s, " ", page->strings[RHK_STRING_TIME], NULL);
        gwy_container_set_string_by_name(meta, "Date", str);
    }

    rhk_sm4_meta_string(page, RHK_STRING_LABEL, "Label", meta);
    rhk_sm4_meta_string(page, RHK_STRING_PATH, "Path", meta);
    rhk_sm4_meta_string(page, RHK_STRING_SYSTEM_TEXT, "System comment", meta);
    rhk_sm4_meta_string(page, RHK_STRING_SESSION_TEXT, "Session comment", meta);
    rhk_sm4_meta_string(page, RHK_STRING_USER_TEXT, "User comment", meta);
    rhk_sm4_meta_string(page, RHK_STRING_X_UNITS, "X units", meta);
    rhk_sm4_meta_string(page, RHK_STRING_Y_UNITS, "Y units", meta);
    rhk_sm4_meta_string(page, RHK_STRING_Z_UNITS, "Z units", meta);
    rhk_sm4_meta_string(page, RHK_STRING_X_LABEL, "X label", meta);
    rhk_sm4_meta_string(page, RHK_STRING_Y_LABEL, "Y label", meta);
    rhk_sm4_meta_string(page, RHK_STRING_STATUS_CHANNEL_TEXT, "Status channel text", meta);
    rhk_sm4_meta_string(page, RHK_STRING_COMPLETED_LINE_COUNT, "Completed line count", meta);
    rhk_sm4_meta_string(page, RHK_STRING_OVERSAMPLING_COUNT, "Oversampling count", meta);
    rhk_sm4_meta_string(page, RHK_STRING_SLICED_VOLTAGE, "Sliced voltage", meta);

    rhk_sm4_add_ppl_pro_status_meta(page, meta);

    str = g_new(gchar, 33);
    for (i = 0; i < 16; i++) {
        str[2*i] = hex[pi->id[i]/16];
        str[2*i + 1] = hex[pi->id[i] % 16];
    }
    str[32] = '\0';
    gwy_container_set_string_by_name(meta, "Page ID", str);

    str = g_new(gchar, 9);
    w = page->group_id;
    for (i = 0; i < 8; i++) {
        str[7 - i] = hex[w & 0xf];
        w = w >> 4;
    }
    str[8] = '\0';
    gwy_container_set_string_by_name(meta, "Group ID", str);

    g_string_free(key, TRUE);

    return meta;
}

static void
rhk_sm4_add_ppl_pro_status_meta(const RHKPage *page, GwyContainer *meta)
{
    GString *key;
    gchar **lines;
    gchar *sep, *value, *second_prefix = NULL;
    gboolean looking_for_second_prefix = FALSE;
    guint i;

    if (!page->strings[RHK_STRING_PLL_PRO_STATUS])
        return;

    lines = g_strsplit(page->strings[RHK_STRING_PLL_PRO_STATUS], "\n", -1);
    if (!lines)
        return;

    key = g_string_new(NULL);
    for (i = 0; lines[i]; i++) {
        g_strstrip(lines[i]);
        if (!*lines[i]) {
            looking_for_second_prefix = TRUE;
            second_prefix = NULL;
            continue;
        }
        if ((sep = strstr(lines[i], " : "))) {
            *sep = '\0';
            value = sep + 3;
            g_strstrip(lines[i]);
            g_strstrip(value);
            g_string_truncate(key, 0);
            g_string_append(key, "PLLPro status::");
            if (second_prefix) {
                g_string_append(key, second_prefix);
                g_string_append(key, "::");
            }
            g_string_append(key, lines[i]);
            gwy_container_set_const_string_by_name(meta, key->str, value);
            looking_for_second_prefix = FALSE;
        }
        else if (looking_for_second_prefix) {
            second_prefix = lines[i];
            looking_for_second_prefix = FALSE;
        }
    }

    g_string_free(key, TRUE);
    g_strfreev(lines);
}

static GwyContainer*
rhk_sm4_read_prm(const RHKObject *prmheader, const RHKObject *prm, const guchar *buffer)
{
    GwyContainer *prmmeta = NULL;
    gchar *prmtext = NULL, *q, *line;
    gchar *header1 = NULL, *header2 = NULL, *header3 = NULL;
    gboolean compressed;
    gsize compsize, decompsize;
    GRegex *h1regex, *h2regex, *h3regex, *metaregex;
    GString *key;
    const guchar *p;

    if (prmheader->size != PRM_HEADER_SIZE)
        return NULL;

    p = buffer + prmheader->offset;
    compressed = gwy_get_guint32_le(&p);
    decompsize = gwy_get_guint32_le(&p);
    compsize = gwy_get_guint32_le(&p);
    gwy_debug("PRM (%d) compsisze=%zu, decompsize=%zu, prmsize=%u", compressed, compsize, decompsize, prm->size);

    if (compressed) {
        gchar *data;
        gsize truedecompsize = decompsize, csize = prm->size;

        if (prm->size != compsize)
            return NULL;

        data = gwyzlib_unpack_compressed_data(buffer + prm->offset, &csize, NULL, &truedecompsize, NULL);
        if (data) {
            prmtext = g_convert(data, truedecompsize, "UTF-8", "CP437", NULL, &truedecompsize, NULL);
            if (prmtext)
                prmtext[truedecompsize] = '\0';
        }
        g_free(data);
    }
    else {
        gsize truedecompsize = 0;

        if (prm->size != decompsize)
            return NULL;

        prmtext = g_convert(buffer + prm->offset, prm->size, "UTF-8", "CP437", NULL, &truedecompsize, NULL);
        if (prmtext)
            prmtext[truedecompsize] = '\0';
    }

    if (!prmtext)
        return NULL;

    h1regex = g_regex_new("^\\s*\\**\\[([^][]+)\\]\\*+$", G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(h1regex);
    h2regex = g_regex_new("^\\[([^][]+)\\]$", G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(h2regex);
    h3regex = g_regex_new("^\\s+-*([^][]+)-*$", G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(h3regex);
    metaregex = g_regex_new("^<[0-9]{4}>\\s+(.+?)\\s+::(.*)$", G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(metaregex);

    prmmeta = gwy_container_new();
    key = g_string_new(NULL);
    q = prmtext;
    while ((line = gwy_str_next_line(&q))) {
        GMatchInfo *matchinfo = NULL;

        if (g_regex_match(metaregex, line, 0, &matchinfo)) {
            gchar *name = g_match_info_fetch(matchinfo, 1), *value = g_match_info_fetch(matchinfo, 2);
            if (header1) {
                g_string_assign(key, header1);
                if (header2) {
                    g_string_append(key, "::");
                    g_string_append(key, header2);
                    if (header3) {
                        g_string_append(key, "::");
                        g_string_append(key, header3);
                    }
                }
                g_string_append(key, "::");
                g_string_append(key, name);
                gwy_container_set_string_by_name(prmmeta, key->str, value);
            }
            else
                g_free(value);
            g_free(name);
            g_match_info_free(matchinfo);
            continue;
        }
        g_match_info_free(matchinfo);

        if (g_regex_match(h1regex, line, 0, &matchinfo)) {
            g_free(header1);
            g_free(header2);
            g_free(header3);
            header1 = g_match_info_fetch(matchinfo, 1);
            g_strstrip(header1);
            header2 = NULL;
            header3 = NULL;
            g_match_info_free(matchinfo);
            continue;
        }
        g_match_info_free(matchinfo);

        if (g_regex_match(h2regex, line, 0, &matchinfo)) {
            g_free(header2);
            g_free(header3);
            header2 = g_match_info_fetch(matchinfo, 1);
            g_strstrip(header2);
            header3 = NULL;
            g_match_info_free(matchinfo);
            continue;
        }
        g_match_info_free(matchinfo);

        if (g_regex_match(h3regex, line, 0, &matchinfo)) {
            g_free(header3);
            header3 = g_match_info_fetch(matchinfo, 1);
            g_strstrip(header3);
            if (header3[0] == '*' || header3[strlen(header3)-1] == '*') {
                g_free(header3);
                header3 = NULL;
            }
            g_match_info_free(matchinfo);
            continue;
        }
        g_match_info_free(matchinfo);
    }

    g_string_free(key, TRUE);
    g_regex_unref(metaregex);
    g_regex_unref(h3regex);
    g_regex_unref(h2regex);
    g_regex_unref(h1regex);
    g_free(prmtext);
    g_free(header3);
    g_free(header2);
    g_free(header1);

    return prmmeta;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
