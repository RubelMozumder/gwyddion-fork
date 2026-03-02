/*
 *  $Id: andorsif.c 26093 2024-01-04 13:08:12Z yeti-dn $
 *  Copyright (C) 2023 David Necas (Yeti).
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
 * [FILE-MAGIC-USERGUIDE]
 * Anfor SIF
 * .sif
 * Read[1]
 * [1] The import module is unfinished due to the lack of documentation, testing files and/or people willing to help
 * with the testing.  If you can help please contact us.
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-andor-sif">
 *   <comment>Andor SIF SPM data</comment>
 *   <magic priority="60">
 *     <match type="string" offset="1:40" value=" Multi-Channel File"/>
 *   </magic>
 *   <glob pattern="*.sif"/>
 *   <glob pattern="*.SIF"/>
 * </mime-type>
 **/

#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyutils.h>
#include <libprocess/stats.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>
#include <app/data-browser.h>

#include "err.h"

#define MAGIC " Multi-Channel File\n"
#define MAGIC_SIZE (sizeof(MAGIC)-1)
#define EXTENSION ".sif"

/* NB: Each struct has its version. Versioned fields depend on the version of the particular struct. */

typedef struct {
    gint version;
    gint type;
    gint active;
    gint structure_version;
    gint timedate;
    gdouble temperature;
    gint head;
    gint store_type;
    gint data_type;
    gint mode;
    gint trigger_source;
    gdouble trigger_level;
    gdouble exposure_time;
    gdouble delay;
    gdouble integration_cycle_time;
    gint no_integrations;
    gint sync;
    gdouble kinetic_cycle_time;
    gdouble pixel_readout_time;
    gint no_points;
    gint no_fast_track_height;
    gint gain;
    gdouble gate_delay;
    gdouble gate_width;
    gdouble gate_step; /* ≥ 1.6 */
    gint track_height;
    gint series_length;
    gint read_pattern;
    gint shutter_delay;
    gint st_centre_row; /* ≥ 1.7 */
    gint mt_offset; /* ≥ 1.7 */
    gint operation_mode; /* ≥ 1.7 */
    gint flip_x; /* ≥ 1.8 */
    gint flip_y; /* ≥ 1.8 */
    gint clock; /* ≥ 1.8 */
    gint a_clock; /* ≥ 1.8 */
    gint mcp; /* ≥ 1.8 */
    gint prop; /* ≥ 1.8 */
    gint ioc; /* ≥ 1.8 */
    gint freq; /* ≥ 1.8 */
    gint vert_clock_amp; /* ≥ 1.9 */
    gdouble data_v_shift_speed; /* ≥ 1.9 */
    gint output_amp; /* ≥ 1.10 */
    gdouble pre_amp_gain; /* ≥ 1.10 */
    gint serial; /* ≥ 1.11 */
    gint num_pulses; /* ≥ 1.13 */
    gint m_frame_transfer_acq_mode; /* ≥ 1.14 */
    gdouble unstabilised_temperature; /* ≥ 1.15 */
    gint m_baseline_clamp; /* ≥ 1.15 */
    gint m_pre_scan; /* ≥ 1.16 */
    gint m_em_real_gain; /* ≥ 1.17 */
    gint m_baseline_offset; /* ≥ 1.18 */
    gint m_sw_version; /* ≥ 1.19 */
    gint mi_gate_mode; /* ≥ 1.20 */
    gint m_sw_dll_ver; /* ≥ 1.21 */
    gint m_sw_dll_rev; /* ≥ 1.21 */
    gint m_sw_dll_rel; /* ≥ 1.21 */
    gint m_sw_dll_bld; /* ≥ 1.21 */
    gint unknown_int1; /* ≥ 1.23 */
    gint unknown_int2; /* ≥ 1.23 */
    gdouble unknown_float1; /* ≥ 1.24 to 1.28 */
    gint unknown_ints1[3]; /* ≥ 1.24 to 1.28 */
    gdouble unknown_float2; /* ≥ 1.24 to 1.28 */
    gint unknown_ints2[9]; /* ≥ 1.24 to 1.28 */
    gint unknown_int3; /* ≥ 1.24 to 1.28 */
    gint unknown_int4; /* ≥ 1.24 to 1.28 */
    gchar *head_model;
    gint detector_format_x;
    gint detector_format_z;
    gchar *title;
} AndorSIFInstaImage;

typedef struct {
    gint version;
    gchar *text;
} AndorSIFUserText;

typedef struct {
    gint version;
    gint type;
    gint mode;
    gint custom_bg_mode;
    gint custom_mode;
    gdouble closing_time;
    gdouble opening_time;
} AndorSIFShutter;

typedef struct {
    gint version;
    gint is_active;
    gint wave_present;
    gdouble wavelength;
    gint grating_present;
    gint grating_index;
    gdouble grating_lines;
    gchar *grating_blaze;
    gint slit_present;
    gdouble slit_width;
    gint flipper_present;
    gint flipper_port;
    gint filter_present;
    gint filter_index;
    gchar *filter_label;
    gint accessory_present;
    gint port1_state;
    gint port2_state;
    gint port3_state;
    gint port4_state;
    gint output_slit_present;
    gdouble output_slit_width;
    gint is_step_and_glue; /* ≥ 1.1 */
    gchar *spectrograph_name; /* ≥ 1.2? */
    gint unknown_int1; /* ≥ 1.3 or 1.4 */
    gdouble unknown_float1; /* ≥ 1.3 or 1.4*/
    gint unknown_int2; /* ≥ 1.3 or 1.4 */
    gdouble unknown_float2; /* ≥ 1.3 or 1.4 */
    gint unknown_int3; /* ≥ 1.3 or 1.4 */
    gdouble unknown_float3; /* ≥ 1.3 or 1.4 */
} AndorSIFShamrockSave;

typedef struct {
    gint version;
    gint is_active;
    gdouble wavelength;
    gdouble grating_lines;
    gchar *spectrograph_name;
    gint unknown_int1;
    gint unknown_int2;
} AndorSIFSpectrographSave;

/* Like this:
65539 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
0 0 0 0
0 0 0 0
0 0 0 0
0 0 0

-1
*/
typedef struct {
    gint version;
    gint is_active;
    gint zero[30];
    gint unknown_int1;
} AndorSIFUnknownRecord1;

/* Like this:
65538 0 1004 1002 1004 1002 1 1 1 1
*/
typedef struct {
    gint version;
    gint is_active;
    gint unknown_int1;
    gint unknown_int2;
    gint unknown_int3;
    gint unknown_int4;
    gint unknown_int5;
    gint unknown_int6;
    gint unknown_int7;
    gint unknown_int8;
} AndorSIFUnknownRecord2;

typedef struct {
    gint version;
    gint x_type;
    gint x_unit;
    gint y_type;
    gint y_unit;
    gint z_type;
    gint z_unit;
    gdouble x_cal[4];
    gdouble y_cal[4];
    gdouble z_cal[4];
    gdouble rayleigh_wavelength; /* ≥ 1.3 */
    gdouble pixel_length; /* ≥ 1.3 */
    gdouble pixel_height; /* ≥ 1.3 */
    gint unknown_int1; /* ≥ 1.4 */
    gchar *x_text;
    gchar *y_text;
    gchar *z_text;
} AndorSIFCalibImage;

typedef struct {
    /* These pieces we read from the SubImage structs themselves. */
    gint left;
    gint top;
    gint right;
    gint bottom;
    gint vertical_bin;
    gint horizontal_bin;
    /* These pieces we read from arrays in the parent Image. */
    gint version;
    gint offset;
    gint timestamp;
} AndorSIFSubImage;

typedef struct {
    gint version;
    gint format_left;
    gint format_top;
    gint format_right;
    gint format_bottom;
    gint no_images;    /* refers to the number in kinetics series */
    gint no_subimages; /* number of images*/
    gint total_length;
    gint image_length;
    AndorSIFSubImage *subimages; /* array with no_subimages items. */
    gint no_unknown_ints1;
    gint *unknown_ints1; /* ≥ 1.3 to 1.5 */
} AndorSIFImage;

/* The documentations makes user_text, shutter and the two saves substructs of insta_image. But it seems kind of
 * arbitrary and we can just keep the layout flat. The only non-flat aspect is that the presence of the structs is
 * determined by the version of insta_image. */
typedef struct {
    AndorSIFInstaImage insta_image;
    AndorSIFUserText user_text;
    AndorSIFShutter shutter; /* insta_image ≥ 1.4 */
    AndorSIFShamrockSave shamrock_save; /* insta_image ≥ 1.12 */
    AndorSIFSpectrographSave spectrograph_save; /* insta_image ≥ 1.22 */
    AndorSIFUnknownRecord1 unknown1; /* insta_image ≥ 1.29 to 1.31 */
    AndorSIFUnknownRecord2 unknown2; /* insta_image ≥ 1.29 to 1.31 */
    AndorSIFCalibImage calib_image;
    AndorSIFImage image;
} AndorSIFChannel;

static gboolean      module_register           (void);
static gint          sif_detect                (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name);
static GwyContainer* sif_load                  (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error);
static gchar*        sif_read_insta_image      (AndorSIFInstaImage *image,
                                                gchar *p);
static gchar*        sif_read_user_text        (AndorSIFUserText *user_text,
                                                gchar *p);
static gchar*        sif_read_shutter          (AndorSIFShutter *shutter,
                                                gchar *p);
static gchar*        sif_read_shamrock_save    (AndorSIFShamrockSave *save,
                                                gchar *p);
static gchar*        sif_read_spectrograph_save(AndorSIFSpectrographSave *save,
                                                gchar *p);
static gchar*        sif_read_unknown_record1  (AndorSIFUnknownRecord1 *unknown,
                                                gchar *p);
static gchar*        sif_read_unknown_record2  (AndorSIFUnknownRecord2 *unknown,
                                                gchar *p);
static gchar*        sif_read_calib_image      (AndorSIFCalibImage *image,
                                                gchar *p);
static gchar*        sif_read_image            (AndorSIFImage *image,
                                                gchar *p);
static void          sif_channel_free          (AndorSIFChannel *datasource);
static gboolean      version_at_least          (gint version,
                                                gint major,
                                                gint minor);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Andor SIF data files."),
    "Yeti <yeti@gwyddion.net>",
    "0.1",
    "David Nečas (Yeti)",
    "2023",
};

GWY_MODULE_QUERY2(module_info, andorsif)

static gboolean
module_register(void)
{
    gwy_file_func_register("andorsif",
                           N_("Andor SIF files (.sif)"),
                           (GwyFileDetectFunc)&sif_detect,
                           (GwyFileLoadFunc)&sif_load,
                           NULL,
                           NULL);
    gwy_file_func_set_is_unfinished("andorsif", TRUE);

    return TRUE;
}

static gint
sif_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    const gchar *p;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;

    /* The file starts ’Company Name Multi-Channel File‘. The company name changes but it should be reasonably long. */
    if (fileinfo->buffer_len <= MAGIC_SIZE)
        return 0;
    if (!(p = gwy_memmem(fileinfo->head, MIN(fileinfo->buffer_len, 120), MAGIC, MAGIC_SIZE)))
        return 0;

    p += MAGIC_SIZE;
    /* After that, there is a newline, 65538 and some other number. */
    if (strncmp(p, "65538 ", 6))
        return 0;

    return 95;
}

static GwyContainer*
sif_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    static const gchar *channels[] = {
        "Signal", "Reference", "Background", "Live", "Source",
    };
    GwyContainer *container = NULL;
    AndorSIFChannel datasource;
    gchar *p, *buffer = NULL;
    gsize size = 0, expected_size, available_size;
    GwyDataField *field;
    GError *err = NULL;
    gboolean is_present, ok = FALSE;
    guint cno;
    gint i, xres, yres, nimages = 0;

    /* NB: We need the file contents nul-terminated because of the idiosyncratic parsing. */
    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&datasource, 1);
    if (!(p = strchr(buffer, '\n'))) {
        err_FILE_TYPE(error, "Andor SIF");
        goto end;
    }
    *(p++) = '\0';
    gwy_debug("magic line <%s>", buffer);
    gwy_debug("magic version number %.6s", p);
    if (strncmp(p, "65538 ", 6)) {
        err_FILE_TYPE(error, "Andor SIF");
        goto end;
    }
    p += 6;

    container = gwy_container_new();
    for (cno = 0; cno < G_N_ELEMENTS(channels); cno++) {
        if (p - buffer > size-2 || (p[0] != '1' && p[0] != '0') || p[1] != '\n') {
            err_FILE_TYPE(error, "Andor SIF");
            goto end;
        }
        is_present = (p[0] == '1');
        gwy_debug("DATA SOURCE %s(%u) is %d",
                  cno < G_N_ELEMENTS(channels) ? channels[cno] : "Uknown channel", cno, is_present);
        p += 2;
        if (!is_present)
            continue;

        if (!(p = sif_read_insta_image(&datasource.insta_image, p))) {
            err_UNSUPPORTED(error, "InstaImage");
            goto end;
        }
        if (!(p = sif_read_user_text(&datasource.user_text, p))) {
            err_UNSUPPORTED(error, "UserText");
            goto end;
        }
        if (version_at_least(datasource.insta_image.version, 1, 4)) {
            if (!(p = sif_read_shutter(&datasource.shutter, p))) {
                err_UNSUPPORTED(error, "Shutter");
                goto end;
            }
        }
        if (version_at_least(datasource.insta_image.version, 1, 12)) {
            if (!(p = sif_read_shamrock_save(&datasource.shamrock_save, p))) {
                err_UNSUPPORTED(error, "ShamrockSave");
                goto end;
            }
        }
        if (version_at_least(datasource.insta_image.version, 1, 22)) {
            if (!(p = sif_read_spectrograph_save(&datasource.spectrograph_save, p))) {
                err_UNSUPPORTED(error, "SpectrographSave");
                goto end;
            }
        }
        /* FIXME: Can be anything between 1.29 and 1.31. */
        if (version_at_least(datasource.insta_image.version, 1, 31)) {
            if (!(p = sif_read_unknown_record1(&datasource.unknown1, p))) {
                err_UNSUPPORTED(error, "UnknownRecord1");
                goto end;
            }
        }
        /* FIXME: Can be anything between 1.29 and 1.31. */
        if (version_at_least(datasource.insta_image.version, 1, 31)) {
            if (!(p = sif_read_unknown_record2(&datasource.unknown2, p))) {
                err_UNSUPPORTED(error, "UnknownRecord2");
                goto end;
            }
        }
        if (!(p = sif_read_calib_image(&datasource.calib_image, p))) {
            err_UNSUPPORTED(error, "CalibImage");
            goto end;
        }
        if (!(p = sif_read_image(&datasource.image, p))) {
            err_UNSUPPORTED(error, "Image");
            goto end;
        }

        expected_size = datasource.image.image_length*sizeof(gfloat);
        for (i = 0; i < datasource.image.no_subimages; i++) {
            AndorSIFSubImage *subimage = datasource.image.subimages + i;

            available_size = size - (p - buffer);
            gwy_debug("expected data size %lu, available %lu", (gulong)expected_size, (gulong)available_size);
            if (err_SIZE_MISMATCH(error, expected_size, available_size, FALSE))
                goto end;

            xres = abs(subimage->right - subimage->left)+1;
            yres = abs(subimage->top - subimage->bottom)+1;
            available_size = expected_size;
            expected_size = sizeof(gfloat) * xres * yres;
            if (err_SIZE_MISMATCH(error, expected_size, available_size, FALSE))
                goto end;

            field = gwy_data_field_new(xres, yres, xres, yres, FALSE);
            gwy_convert_raw_data((guchar*)p, xres*yres, 1, GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                                 gwy_data_field_get_data(field), 1.0, 0.0);
            gwy_container_pass_object(container, gwy_app_get_data_key_for_id(nimages), field);

            nimages++;
            p += expected_size;
        }
        sif_channel_free(&datasource);
    }
    /* XXX: In new files, there can be some XML appended at the end. */

    if (nimages)
        ok = TRUE;
    else
        err_NO_DATA(error);

end:
    if (!ok)
        GWY_OBJECT_UNREF(container);
    sif_channel_free(&datasource);
    g_free(buffer);

    return container;
}

static inline gchar*
next_separator(gchar **p)
{
    gchar *s = *p;
    gsize len;

    if ((len = strcspn(s, " \n"))) {
        s[len] = '\0';
        *p = s + len+1;
        return s;
    }

    return NULL;
}

static inline gboolean
read_int(gchar **p, gint *value, G_GNUC_UNUSED const gchar *name)
{
    gchar *s, *end;

    /* There can also be leading spaces matching the separator. So we cannot just diectly look for it. (Since integers
     * cannot be empty it is still technically well-defined). */
    while (**p == ' ' || **p == '\n')
        (*p)++;

    if (!(s = next_separator(p)))
        return FALSE;

    *value = strtol(s, &end, 10);
    if (end > s) {
        if (gwy_strequal(name, "version")) {
            gwy_debug("%s = %d.%d", name, (*value) >> 16, (*value) & 0xffff);
        }
        else {
            gwy_debug("%s = %d", name, *value);
        }
    }
    return end != s;
}

static inline gboolean
read_float(gchar **p, gdouble *value, G_GNUC_UNUSED const gchar *name)
{
    gchar *s, *end;

    /* There can also be leading spaces matching the separator. So we cannot just diectly look for it. (Since floats
     * cannot be empty it is still technically well-defined). */
    while (**p == ' ' || **p == '\n')
        (*p)++;

    if (!(s = next_separator(p)))
        return FALSE;

    *value = g_ascii_strtod(s, &end);
    if (end > s) {
        gwy_debug("%s = %g", name, *value);
    }
    return end != s;
}

static inline gboolean
read_byte(gchar **p, gint *value, G_GNUC_UNUSED const gchar *name)
{
    *value = *(guchar*)(*p);
    (*p)++;
    if (**p != ' ' && **p != '\n')
        return FALSE;

    gwy_debug("%s = %d", name, *value);
    (*p)++;
    return TRUE;
}

/* Each string has a different terminator, from nothing to SPACE-NL-SPACE. Some of them have the length specified,
 * some rely on the terminator. Bite me. */
static inline gboolean
read_string_len_sep(gchar **p, const gchar *separator, gchar **value, G_GNUC_UNUSED const gchar *name)
{
    gchar *s;
    gint len, seplen;

    if (!read_int(p, &len, "(string length)"))
        return FALSE;

    s = *p;
    seplen = (separator ? strlen(separator) : 0);
    if (seplen && strncmp(s + len, separator, seplen))
        return FALSE;

    *value = g_strndup(s, len);
    *p = s + len + seplen;

    gwy_debug("%s = %s", name, *value);
    return TRUE;
}

static inline gboolean
read_string_sep(gchar **p, const gchar *separator, gchar **value, G_GNUC_UNUSED const gchar *name)
{
    gchar *s;
    gint seplen;

    g_return_val_if_fail(separator, FALSE);
    seplen = strlen(separator);
    g_return_val_if_fail(seplen, FALSE);
    if (!(s = strstr(*p, separator)))
        return FALSE;

    *value = g_strndup(*p, s - *p);
    *p = s + seplen;

    gwy_debug("%s = %s", name, *value);
    return TRUE;
}

static gchar*
sif_read_insta_image(AndorSIFInstaImage *image, gchar *p)
{
    gwy_debug("reading InstaImage (main struct)");
    if (!read_int(&p, &image->version, "version")
        || !read_int(&p, &image->type, "type")
        || !read_int(&p, &image->active, "active")
        || !read_int(&p, &image->structure_version, "structure_version")
        || !read_int(&p, &image->timedate, "timedate")
        || !read_float(&p, &image->temperature, "temperature")
        || !read_byte(&p, &image->head, "head")
        || !read_byte(&p, &image->store_type, "store_type")
        || !read_byte(&p, &image->data_type, "data_type")
        || !read_byte(&p, &image->mode, "mode")
        || !read_byte(&p, &image->trigger_source, "trigger_source")
        || !read_float(&p, &image->trigger_level, "trigger_level")
        || !read_float(&p, &image->exposure_time, "exposure_time")
        || !read_float(&p, &image->delay, "delay")
        || !read_float(&p, &image->integration_cycle_time, "integration_cycle_time")
        || !read_int(&p, &image->no_integrations, "no_integrations")
        || !read_byte(&p, &image->sync, "sync")
        || !read_float(&p, &image->kinetic_cycle_time, "kinetic_cycle_time")
        || !read_float(&p, &image->pixel_readout_time, "pixel_readout_time")
        || !read_int(&p, &image->no_points, "no_points")
        || !read_int(&p, &image->no_fast_track_height, "no_fast_track_height")
        || !read_int(&p, &image->gain, "gain")
        || !read_float(&p, &image->gate_delay, "gate_delay")
        || !read_float(&p, &image->gate_width, "gate_width"))
        return NULL;

    if (version_at_least(image->version, 1, 6)) {
        if (!read_float(&p, &image->gate_step, "gate_step"))
            return NULL;
    }

    if (!read_int(&p, &image->track_height, "track_height")
        || !read_int(&p, &image->series_length, "series_length")
        || !read_byte(&p, &image->read_pattern, "read_pattern")
        || !read_byte(&p, &image->shutter_delay, "shutter_delay"))
        return NULL;

    if (version_at_least(image->version, 1, 7)) {
        if (!read_int(&p, &image->st_centre_row, "st_centre_row")
            || !read_int(&p, &image->mt_offset, "mt_offset")
            || !read_int(&p, &image->operation_mode, "operation_mode"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 8)) {
        if (!read_int(&p, &image->flip_x, "flip_x")
            || !read_int(&p, &image->flip_y, "flip_y")
            || !read_int(&p, &image->clock, "clock")
            || !read_int(&p, &image->a_clock, "a_clock")
            || !read_int(&p, &image->mcp, "mcp")
            || !read_int(&p, &image->prop, "prop")
            || !read_int(&p, &image->ioc, "ioc")
            || !read_int(&p, &image->freq, "freq"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 9)) {
        if (!read_int(&p, &image->vert_clock_amp, "vert_clock_amp")
            || !read_float(&p, &image->data_v_shift_speed, "data_v_shift_speed"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 10)) {
        if (!read_int(&p, &image->output_amp, "output_amp")
            || !read_float(&p, &image->pre_amp_gain, "pre_amp_gain"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 11)) {
        if (!read_int(&p, &image->serial, "serial"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 13)) {
        if (!read_int(&p, &image->num_pulses, "num_pulses"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 14)) {
        if (!read_int(&p, &image->m_frame_transfer_acq_mode, "m_frame_transfer_acq_mode"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 15)) {
        if (!read_float(&p, &image->unstabilised_temperature, "unstabilised_temperature")
            || !read_int(&p, &image->m_baseline_clamp, "m_baseline_clamp"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 16)) {
        if (!read_int(&p, &image->m_pre_scan, "m_pre_scan"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 17)) {
        if (!read_int(&p, &image->m_em_real_gain, "m_em_real_gain"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 18)) {
        if (!read_int(&p, &image->m_baseline_offset, "m_baseline_offset"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 19)) {
        if (!read_int(&p, &image->m_sw_version, "m_sw_version"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 20)) {
        if (!read_int(&p, &image->mi_gate_mode, "mi_gate_mode"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 21)) {
        if (!read_int(&p, &image->m_sw_dll_ver, "m_sw_dll_ver")
            || !read_int(&p, &image->m_sw_dll_rev, "m_sw_dll_rev")
            || !read_int(&p, &image->m_sw_dll_rel, "m_sw_dll_rel")
            || !read_int(&p, &image->m_sw_dll_bld, "m_sw_dll_bld"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 23)) {
        if (!read_int(&p, &image->unknown_int1, "unknown_int1")
            || !read_int(&p, &image->unknown_int2, "unknown_int2"))
            return NULL;
    }

    /* FIXME: The following were added between 1.24 and 1.28. I do not know which one when. */
    if (version_at_least(image->version, 1, 28)) {
        if (!read_float(&p, &image->unknown_float1, "unknown_float1")
            || !read_int(&p, image->unknown_ints1 + 0, "unknown_ints1[0]")
            || !read_int(&p, image->unknown_ints1 + 1, "unknown_ints1[1]")
            || !read_int(&p, image->unknown_ints1 + 2, "unknown_ints1[2]")
            || !read_float(&p, &image->unknown_float2, "unknown_float2")
            || !read_int(&p, image->unknown_ints2 + 0, "unknown_ints2[0]")
            || !read_int(&p, image->unknown_ints2 + 1, "unknown_ints2[1]")
            || !read_int(&p, image->unknown_ints2 + 2, "unknown_ints2[2]")
            || !read_int(&p, image->unknown_ints2 + 3, "unknown_ints2[3]")
            || !read_int(&p, image->unknown_ints2 + 4, "unknown_ints2[4]")
            || !read_int(&p, image->unknown_ints2 + 5, "unknown_ints2[5]")
            || !read_int(&p, image->unknown_ints2 + 6, "unknown_ints2[6]")
            || !read_int(&p, image->unknown_ints2 + 7, "unknown_ints2[7]")
            || !read_int(&p, image->unknown_ints2 + 8, "unknown_ints2[8]")
            || !read_int(&p, &image->unknown_int3, "unknown_int3")
            || !read_int(&p, &image->unknown_int4, "unknown_int4"))
            return NULL;
    }

    if (version_at_least(image->version, 1, 5)) {
        if (!read_string_len_sep(&p, " \n ", &image->head_model, "head_model")
            || !read_int(&p, &image->detector_format_x, "detector_format_x")
            || !read_int(&p, &image->detector_format_z, "detector_format_z"))
            return NULL;
    }
    else if (version_at_least(image->version, 1, 3)) {
        gint head_model;
        if (!read_int(&p, &head_model, "head_model")
            || !read_int(&p, &image->detector_format_x, "detector_format_x")
            || !read_int(&p, &image->detector_format_z, "detector_format_z"))
            return NULL;
        image->head_model = g_strdup_printf("%d", head_model);
    }
    else {
        image->detector_format_x = 1024;
        image->detector_format_x = 256;
        image->head_model = g_strdup("Unknown");
    }

    if (!read_string_len_sep(&p, " \n", &image->title, "title"))
        return NULL;

    return p;
}

static gchar*
sif_read_user_text(AndorSIFUserText *user_text, gchar *p)
{
    gwy_debug("reading UserText (part of InstaImage)");
    if (!read_int(&p, &user_text->version, "version")
        || !read_string_len_sep(&p, "\n", &user_text->text, "text"))
        return NULL;

    return p;
}

static gchar*
sif_read_shutter(AndorSIFShutter *shutter, gchar *p)
{
    gwy_debug("reading Shutter (part of InstaImage)");
    if (!read_int(&p, &shutter->version, "version"))
        return NULL;
    /* XXX: There may be an extra space. Ufortunately, we are reading raw bytes afterwards so in principle we might
     * get completely out of sync here. */
    if (*p == ' ')
        p++;
    if (!read_byte(&p, &shutter->type, "type")
        || !read_byte(&p, &shutter->mode, "mode")
        || !read_byte(&p, &shutter->custom_bg_mode, "custom_bg_mode")
        || !read_byte(&p, &shutter->custom_mode, "custom_mode")
        || !read_float(&p, &shutter->closing_time, "closing_time")
        || !read_float(&p, &shutter->opening_time, "opening_time"))
        return NULL;

    return p;
}

static gchar*
sif_read_shamrock_save(AndorSIFShamrockSave *save, gchar *p)
{
    gwy_debug("reading ShamrockSave (part of InstaImage)");
    if (!read_int(&p, &save->version, "version")
        || !read_int(&p, &save->is_active, "is_active")
        || !read_int(&p, &save->wave_present, "wave_present")
        || !read_float(&p, &save->wavelength, "wavelength")
        || !read_int(&p, &save->grating_present, "grating_present")
        || !read_float(&p, &save->grating_lines, "grating_lines")
        || !read_string_sep(&p, "\n", &save->grating_blaze, "grating_blaze")
        || !read_int(&p, &save->slit_present, "slit_present")
        || !read_float(&p, &save->slit_width, "slit_width")
        || !read_int(&p, &save->flipper_present, "flipper_present")
        || !read_int(&p, &save->flipper_port, "flipper_port")
        || !read_int(&p, &save->filter_present, "filter_present")
        || !read_int(&p, &save->filter_index, "filter_index")
        || !read_string_len_sep(&p, " ", &save->filter_label, "filter_label")
        || !read_int(&p, &save->accessory_present, "accessory_present")
        || !read_int(&p, &save->port1_state, "port1_state")
        || !read_int(&p, &save->port2_state, "port2_state")
        || !read_int(&p, &save->port3_state, "port3_state")
        || !read_int(&p, &save->port4_state, "port4_state")
        || !read_int(&p, &save->output_slit_present, "accessory_present")
        || !read_float(&p, &save->output_slit_width, "output_slit_width"))
        return NULL;

    if (version_at_least(save->version, 1, 1)) {
        if (!read_int(&p, &save->is_step_and_glue, "is_step_and_glue"))
            return NULL;
    }

    if (version_at_least(save->version, 1, 2)) {
        if (!read_string_sep(&p, "\n", &save->spectrograph_name, "spectrograph_name"))
            return NULL;
    }

    /* FIXME: Could be 1.3 or 1.4. */
    if (version_at_least(save->version, 1, 3)) {
        if (!read_int(&p, &save->unknown_int1, "unknown_int1")
            || !read_float(&p, &save->unknown_float1, "unknown_float1")
            || !read_int(&p, &save->unknown_int2, "unknown_int2")
            || !read_float(&p, &save->unknown_float2, "unknown_float2")
            || !read_int(&p, &save->unknown_int3, "unknown_int3")
            || !read_float(&p, &save->unknown_float3, "unknown_float3"))
            return NULL;
    }

    return p;
}

static gchar*
sif_read_spectrograph_save(AndorSIFSpectrographSave *save, gchar *p)
{
    gwy_debug("reading SpectrographSave (part of InstaImage)");
    if (!read_int(&p, &save->version, "version")
        || !read_int(&p, &save->is_active, "is_active")
        || !read_float(&p, &save->wavelength, "wavelength")
        || !read_float(&p, &save->grating_lines, "grating_lines"))
        return NULL;

    if (version_at_least(save->version, 1, 1)) {
        if (!read_string_len_sep(&p, "", &save->spectrograph_name, "spectrograph_name")
            || !read_int(&p, &save->unknown_int1, "unknown_int1")
            || !read_int(&p, &save->unknown_int2, "unknown_int2"))
        return NULL;
    }
    else {
        if (!read_string_sep(&p, "\n", &save->spectrograph_name, "spectrograph_name"))
            return NULL;
    }

    return p;
}

static gchar*
sif_read_unknown_record1(AndorSIFUnknownRecord1 *unknown, gchar *p)
{
    guint i;

    gwy_debug("reading UknownRecord1 (part of InstaImage)");
    if (!read_int(&p, &unknown->version, "version")
        || !read_int(&p, &unknown->is_active, "is_active"))
        return NULL;

    for (i = 0; i < G_N_ELEMENTS(unknown->zero); i++) {
        if (!read_int(&p, unknown->zero + i, "zero"))
            return NULL;
    }

    if (!read_int(&p, &unknown->unknown_int1, "unknown_int1"))
        return NULL;

    return p;
}

static gchar*
sif_read_unknown_record2(AndorSIFUnknownRecord2 *unknown, gchar *p)
{
    gwy_debug("reading UknownRecord2 (part of InstaImage)");
    if (!read_int(&p, &unknown->version, "version")
        || !read_int(&p, &unknown->is_active, "is_active")
        || !read_int(&p, &unknown->unknown_int1, "unknown_int1")
        || !read_int(&p, &unknown->unknown_int2, "unknown_int2")
        || !read_int(&p, &unknown->unknown_int3, "unknown_int3")
        || !read_int(&p, &unknown->unknown_int4, "unknown_int4")
        || !read_int(&p, &unknown->unknown_int5, "unknown_int5")
        || !read_int(&p, &unknown->unknown_int6, "unknown_int6")
        || !read_int(&p, &unknown->unknown_int7, "unknown_int7")
        || !read_int(&p, &unknown->unknown_int8, "unknown_int8"))
        return NULL;

    return p;
}

static gchar*
sif_read_calib_image(AndorSIFCalibImage *image, gchar *p)
{
    gwy_debug("reading CalibImage");
    if (!read_int(&p, &image->version, "version")
        || !read_byte(&p, &image->x_type, "x_type")
        || !read_byte(&p, &image->x_unit, "x_unit")
        || !read_byte(&p, &image->y_type, "y_type")
        || !read_byte(&p, &image->y_unit, "y_unit")
        || !read_byte(&p, &image->z_type, "z_type")
        || !read_byte(&p, &image->z_unit, "z_unit"))
        return NULL;

    if (!read_float(&p, image->x_cal + 0, "x_cal[0]")
        || !read_float(&p, image->x_cal + 1, "x_cal[1]")
        || !read_float(&p, image->x_cal + 2, "x_cal[2]")
        || !read_float(&p, image->x_cal + 3, "x_cal[3]"))
        return NULL;

    if (!read_float(&p, image->y_cal + 0, "y_cal[0]")
        || !read_float(&p, image->y_cal + 1, "y_cal[1]")
        || !read_float(&p, image->y_cal + 2, "y_cal[2]")
        || !read_float(&p, image->y_cal + 3, "y_cal[3]"))
        return NULL;

    if (!read_float(&p, image->z_cal + 0, "z_cal[0]")
        || !read_float(&p, image->z_cal + 1, "z_cal[1]")
        || !read_float(&p, image->z_cal + 2, "z_cal[2]")
        || !read_float(&p, image->z_cal + 3, "z_cal[3]"))
        return NULL;

    if (version_at_least(image->version, 1, 3)) {
        if (!read_float(&p, &image->rayleigh_wavelength, "rayleigh_wavelength")
            || !read_float(&p, &image->pixel_length, "pixel_length")
            || !read_float(&p, &image->pixel_height, "pixel_height"))
            return NULL;
    }
    if (version_at_least(image->version, 1, 4)) {
        if (!read_int(&p, &image->unknown_int1, "unknown_int1"))
            return NULL;
    }

    if (!read_string_len_sep(&p, "", &image->x_text, "x_text")
        || !read_string_len_sep(&p, "", &image->y_text, "y_text")
        || !read_string_len_sep(&p, "", &image->z_text, "z_text"))
        return NULL;

    return p;
}

static gchar*
sif_read_image(AndorSIFImage *image, gchar *p)
{
    gint i;

    gwy_debug("reading Image");
    if (!read_int(&p, &image->version, "version")
        || !read_int(&p, &image->format_left, "format_left")
        || !read_int(&p, &image->format_top, "format_top")
        || !read_int(&p, &image->format_right, "format_right")
        || !read_int(&p, &image->format_bottom, "format_bottom")
        || !read_int(&p, &image->no_images, "no_images")
        || !read_int(&p, &image->no_subimages, "no_subimages")
        || !read_int(&p, &image->total_length, "total_length")
        || !read_int(&p, &image->image_length, "image_length"))
        return NULL;

    if (image->no_subimages <= 0 || image->total_length <= 0 || image->image_length <= 0)
        return NULL;
    if (image->total_length % image->image_length || image->total_length/image->image_length != image->no_subimages)
        return NULL;

    image->subimages = g_new0(AndorSIFSubImage, image->no_subimages);
    for (i = 0; i < image->no_subimages; i++) {
        AndorSIFSubImage *subimage = image->subimages + i;

        gwy_debug("reading SubImage%d", i);
        if (!read_int(&p, &subimage->version, "version")
            || !read_int(&p, &subimage->left, "left")
            || !read_int(&p, &subimage->top, "top")
            || !read_int(&p, &subimage->right, "right")
            || !read_int(&p, &subimage->bottom, "bottom")
            || !read_int(&p, &subimage->vertical_bin, "vertical_bin")
            || !read_int(&p, &subimage->horizontal_bin, "horizontal_bin")
            || !read_int(&p, &subimage->offset, "offset"))
            return NULL;
    }

    gwy_debug("reading timestamps");
    for (i = 0; i < image->no_subimages; i++) {
        AndorSIFSubImage *subimage = image->subimages + i;

        if (!read_int(&p, &subimage->timestamp, "timestamp"))
            return NULL;
    }

    /* FIXME: I am guessing basically everything here. */
    if (version_at_least(image->version, 1, 5)) {
        gwy_debug("reading unknown values 1");
        if (!read_int(&p, &image->no_unknown_ints1, "no_unknown_ints1") || image->no_unknown_ints1 > 1000)
            return NULL;

        image->unknown_ints1 = g_new(gint, image->no_unknown_ints1);
        for (i = 0; i < image->no_unknown_ints1; i++) {
            if (!read_int(&p, image->unknown_ints1 + i, "unknown_ints1"))
                return NULL;
        }
    }

    /* Now the image data follows. Let the caller validate them because it knows the file size. */

    return p;
}

static void
sif_channel_free(AndorSIFChannel *datasource)
{
    g_free(datasource->insta_image.head_model);
    g_free(datasource->insta_image.title);
    g_free(datasource->user_text.text);
    g_free(datasource->shamrock_save.grating_blaze);
    g_free(datasource->shamrock_save.filter_label);
    g_free(datasource->shamrock_save.spectrograph_name);
    g_free(datasource->calib_image.x_text);
    g_free(datasource->calib_image.y_text);
    g_free(datasource->calib_image.z_text);
    g_free(datasource->image.subimages);
    g_free(datasource->image.unknown_ints1);
    gwy_clear(datasource, 1);
}

static gboolean
version_at_least(gint version, gint major, gint minor)
{
    return (version >> 16) > major || ((version >> 16) == major && (version & 0xfff) >= minor);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
