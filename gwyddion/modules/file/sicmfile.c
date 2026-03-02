/*
 *  $Id: sicmfile.c 26163 2024-02-06 16:56:55Z yeti-dn $
 *  IonScope SICM file format importer
 *  Copyright (C) 2008-2010 Matthew Caldwell.
 *  E-mail: m.caldwell@ucl.ac.uk
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
 * <mime-type type="application/x-sicm-spm">
 *   <comment>IonScope SICM data</comment>
 *   <magic priority="50">
 *     <match type="string" offset="0" value="\x32\x00"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * IonScope SICM
 * .img
 * Read
 **/

/*--------------------------------------------------------------------------
  Dependencies
----------------------------------------------------------------------------*/

#include "config.h"
#include <libgwyddion/gwymath.h>
#include <libprocess/stats.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

/*--------------------------------------------------------------------------
  Constants
----------------------------------------------------------------------------*/

#define EXTENSION ".img"

enum
{
    SICM_VERSION = 50,
    HEADER_SIZE = 830
};

/*--------------------------------------------------------------------------
  Types
----------------------------------------------------------------------------*/

/* Structure to hold the file header data */
typedef struct _SICMImage
{
                                        /* offset in file */

    gint16  version; /* 50 */           /* 0 */
    gint16  xdim;                       /* 2 */
    gint16  ydim;                       /* 4 */

    /* STM Param Record */
    gdouble fsdHVA;                     /* 6 */
    gdouble fsdDAC;                     /* 12 */
    gdouble fsdADC;                     /* 18 */
    gdouble haGain;                     /* 24 */
    gdouble piezoCalX;                  /* 30 */
    gdouble piezoCalY;                  /* 36 */
    gdouble piezoCalZ;                  /* 42 */
    gdouble gainZ;                      /* 48 */
    gint16  maxADC;                     /* 54 */

    /* Scan Para Record */
    gdouble scanSize; /* 10^-8 m */     /* 56 */
    gint16  ctrlOS;                     /* 62 */
    gint16  imagOS;                     /* 64 */
    gint16  ctrlPts;                    /* 66 */
    gint16  xDimension; /* == xdim */   /* 68 */
    gint16  yDimension; /* == ydim */   /* 70 */

    /* Loop Record */
    gdouble loopGain;                   /* 72 */
    gdouble setPoint;   /* pA */        /* 78 */
    gdouble tipVoltage; /* mV (?) */    /* 84 */
    gdouble tipXPos;    /* 10^-8 m */   /* 90 */
    gdouble tipYPos;    /* 10^-8 m */   /* 96 */

    /* Plane Param Record */
    gdouble A;                          /* 102 */
    gdouble B;                          /* 108 */
    gdouble D;                          /* 114 */
    gint16  fitX;                       /* 120 */
    gint16  fitY;                       /* 122 */
    gint16  min;                        /* 124 */
    gint16  max;                        /* 126 */
    gdouble scale;                      /* 128 */

    /* Scan Setup Record */
    gdouble     scanAngle;              /* 134 */
    gdouble     xSlope;                 /* 140 */
    gdouble     ySlope;                 /* 146 */
    gboolean    fitting;                /* 152 */
    gboolean    polarity;               /* 153 */
    gboolean    scan1D;                 /* 154 */
    gboolean    startCenter;            /* 155 */

    /* Time Record */
    guchar  date[79];                   /* 156 */
    guchar  time[79];                   /* 235 */

    gint16  scanMode;                   /* 314 */
    guint16 version2; /* == version */  /* 316 */
    gdouble range;                      /* 318 */
    guchar  space2[7];                  /* 324 */
    guchar  comment[81];                /* 331 */
    guchar  title[81];                  /* 412 */

    /* CITS Record */
    gint16  NCITS;                      /* 493 */
    gint16  settle;                     /* 495 */
    gdouble vArray[8];                  /* 497 */
    gdouble offArray[8];                /* 545 */

    /* Break Record */
    gint16  noPts;                      /* 593 */
    gint16  settle2;                    /* 595 */
    gdouble vStart;                     /* 597 */
    gdouble vEnd;                       /* 603 */
    gdouble threshold;                  /* 609 */

    gint16  loopMode;                   /* 615 */

    /* Hopping Mode Record */
    guint16 hopAmp;                     /* 617 */
    guint16 riseRate;                   /* 619 */
    guint16 riseToFallTime;             /* 621 */
    guint16 fallRate;                   /* 623 */
    gint16  dcSetPoint;                 /* 625 */
    guchar  prescanSqrSize;             /* 627 */
    guint16 prescanHopAmp;              /* 628 */
    guint16 minHopAmp;                  /* 630 */
    guchar  absHopMode;                 /* 632 */
    guchar  fastPrescanMode;            /* 633 */
    guchar  resLevels[8];               /* 634 */
    guint16 resThresholds[8];           /* 642 */
    guchar  numResLevels;               /* 658 */

    guchar  space[7];                   /* 659 */

    /* Info Strings */
    guchar  modeStr[41];                /* 666 */
    guchar  loopStr[41];                /* 707 */
    guchar  sizeStr[41];                /* 748 */
    guchar  posStr[41];                 /* 789 */

    /* Start of heightfield data */     /* 830 */
    /* xdim x ydim array of gint16 */
}
SICMImage;

/*--------------------------------------------------------------------------
  Prototypes
----------------------------------------------------------------------------*/

static gboolean      module_register(void);
static gint          sicm_detect    (const GwyFileDetectInfo *fileinfo,
                                     gboolean only_name);
static GwyContainer* sicm_load      (const gchar *filename,
                                     GwyRunType mode,
                                     GError **error);
static void          set_string_meta(GwyContainer *meta,
                                     const gchar *name,
                                     const gchar *latin1value);
static void          set_double_meta(GwyContainer *meta,
                                     const gchar *name,
                                     const gchar *unit,
                                     gdouble value);
static void          set_int_meta   (GwyContainer *meta,
                                     const gchar *name,
                                     gint value);
static void          set_yesno_meta (GwyContainer *meta,
                                     const gchar *name,
                                     gboolean value);

/*--------------------------------------------------------------------------
  Implementation
----------------------------------------------------------------------------*/

/* module details */
static GwyModuleInfo module_info =
{
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports IonScope SICM data files."),
    "Matthew Caldwell <m.caldwell@ucl.ac.uk>",
    "1.3",
    "Matthew Caldwell",
    "2007-2010",
};
GWY_MODULE_QUERY2(module_info, sicmfile)

/*--------------------------------------------------------------------------*/

/* announce ourselves to gwyddion */
static gboolean
module_register(void)
{
    gwy_file_func_register("sicm",
                           N_("IonScope SICM files (.img)"),
                           (GwyFileDetectFunc) &sicm_detect,
                           (GwyFileLoadFunc) &sicm_load,
                           NULL,
                           NULL);

    return TRUE;
}

/*--------------------------------------------------------------------------*/

/* check a candidate file for likely SICMness */
static gint
sicm_detect(const GwyFileDetectInfo *fileinfo,
            gboolean only_name)
{
    /* name isn't much of a test since every second SPM format -- and various non-SPM ones too -- use a .IMG
     * extension, but we have nothing else to go on */
    if (only_name) {
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 20 : 0;
    }
    else {
        /* otherwise, we can perform a slightly more discriminating test: */
        const guchar* p = fileinfo->head;

        /* big enough to test */
        if (fileinfo->buffer_len > 6
            /* version has to serve as magic number */
            && gwy_get_gint16_le(&p) == SICM_VERSION
            /* file size is consistent */
            && (fileinfo->file_size == HEADER_SIZE + 2 * gwy_get_gint16_le(&p) * gwy_get_gint16_le(&p)))
            return 100;
    }

    return 0;
}

/*--------------------------------------------------------------------------*/

/* load file data. */
static GwyContainer*
sicm_load(const gchar *filename,
          G_GNUC_UNUSED GwyRunType mode,
          GError **error)
{
    SICMImage sicm;
    GwyContainer *container = NULL, *meta = NULL;
    GwyDataField *dfield;
    guchar *buffer;
    const guchar *p, *unit_name;
    GError *err = NULL;
    gsize size, expected_size;
    gdouble scaling;
    gint i;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (size <= HEADER_SIZE) {
        err_TOO_SHORT(error);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    p = buffer;

    sicm.version = gwy_get_gint16_le(&p);

    /* version and size tests as in sicm_detect above */
    if (sicm.version != SICM_VERSION) {
        err_FILE_TYPE(error, "SICM");
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    sicm.xdim = gwy_get_gint16_le(&p);
    sicm.ydim = gwy_get_gint16_le(&p);

    expected_size = 2 * sicm.xdim * sicm.ydim + HEADER_SIZE;
    if (size != expected_size) {
        err_SIZE_MISMATCH(error, expected_size, size, TRUE);
        gwy_file_abandon_contents(buffer, size, NULL);
        return NULL;
    }

    /* load in the header data */
    sicm.fsdHVA = gwy_get_pascal_real_le(&p);
    sicm.fsdDAC = gwy_get_pascal_real_le(&p);
    sicm.fsdADC = gwy_get_pascal_real_le(&p);
    sicm.haGain = gwy_get_pascal_real_le(&p);
    sicm.piezoCalX = gwy_get_pascal_real_le(&p);
    sicm.piezoCalY = gwy_get_pascal_real_le(&p);
    sicm.piezoCalZ = gwy_get_pascal_real_le(&p);
    sicm.gainZ = gwy_get_pascal_real_le(&p);
    sicm.maxADC = gwy_get_gint16_le(&p);

    sicm.scanSize = gwy_get_pascal_real_le(&p);
    sicm.ctrlOS = gwy_get_gint16_le(&p);
    sicm.imagOS = gwy_get_gint16_le(&p);
    sicm.ctrlPts = gwy_get_gint16_le(&p);
    sicm.xDimension = gwy_get_gint16_le(&p);
    sicm.yDimension = gwy_get_gint16_le(&p);

    sicm.loopGain = gwy_get_pascal_real_le(&p);
    sicm.setPoint = gwy_get_pascal_real_le(&p);
    sicm.tipVoltage = gwy_get_pascal_real_le(&p);
    sicm.tipXPos = gwy_get_pascal_real_le(&p);
    sicm.tipYPos = gwy_get_pascal_real_le(&p);

    sicm.A = gwy_get_pascal_real_le(&p);
    sicm.B = gwy_get_pascal_real_le(&p);
    sicm.D = gwy_get_pascal_real_le(&p);
    sicm.fitX = gwy_get_gint16_le(&p);
    sicm.fitY = gwy_get_gint16_le(&p);
    sicm.min = gwy_get_gint16_le(&p);
    sicm.max = gwy_get_gint16_le(&p);
    sicm.scale = gwy_get_pascal_real_le(&p);

    sicm.scanAngle = gwy_get_pascal_real_le(&p);
    sicm.xSlope = gwy_get_pascal_real_le(&p);
    sicm.ySlope = gwy_get_pascal_real_le(&p);

    sicm.fitting = gwy_get_gboolean8(&p);
    sicm.polarity = gwy_get_gboolean8(&p);
    sicm.scan1D = gwy_get_gboolean8(&p);
    sicm.startCenter = gwy_get_gboolean8(&p);

    memcpy(sicm.date, p+1, 78);
    sicm.date[78] = 0;
    p += 79;

    memcpy(sicm.time, p+1, 78);
    sicm.time[78] = 0;
    p += 79;

    sicm.scanMode = gwy_get_gint16_le(&p);
    sicm.version2 = gwy_get_guint16_le(&p);
    sicm.range = gwy_get_pascal_real_le(&p);

    memcpy(sicm.space2, p+1, 6);
    sicm.space2[6] = 0;
    p += 7;
    memcpy(sicm.comment, p+1, 80);
    sicm.comment[80] = 0;
    p += 81;
    memcpy(sicm.title, p+1, 80);
    sicm.title[80] = 0;
    p += 81;

    sicm.NCITS = gwy_get_gint16_le(&p);
    sicm.settle = gwy_get_gint16_le(&p);
    for (i = 0; i < 8; ++i)
        sicm.vArray[i] = gwy_get_pascal_real_le(&p);
    for (i = 0; i < 8; ++i)
        sicm.offArray[i] = gwy_get_pascal_real_le(&p);

    sicm.noPts = gwy_get_gint16_le(&p);
    sicm.settle2 = gwy_get_gint16_le(&p);
    sicm.vStart = gwy_get_pascal_real_le(&p);
    sicm.vEnd = gwy_get_pascal_real_le(&p);
    sicm.threshold = gwy_get_pascal_real_le(&p);

    sicm.loopMode = gwy_get_gint16_le(&p);

    sicm.hopAmp = gwy_get_guint16_le(&p);
    sicm.riseRate = gwy_get_guint16_le(&p);
    sicm.riseToFallTime = gwy_get_guint16_le(&p);
    sicm.fallRate = gwy_get_guint16_le(&p);
    sicm.dcSetPoint = gwy_get_gint16_le(&p);
    sicm.prescanSqrSize = *p++;
    sicm.prescanHopAmp = gwy_get_guint16_le(&p);
    sicm.minHopAmp = gwy_get_guint16_le(&p);
    sicm.absHopMode = *p++;
    sicm.fastPrescanMode = *p++;
    for (i = 0; i < 8; ++i)
        sicm.resLevels[i] = *p++;
    for (i = 0; i < 8; ++i)
        sicm.resThresholds[i] = gwy_get_guint16_le(&p);
    sicm.numResLevels = *p++;

    memcpy(sicm.space, p+1, 6);
    sicm.space[6] = 0;
    p += 7;

    memcpy(sicm.modeStr, p+1, 40);
    sicm.modeStr[40] = 0;
    p += 41;
    memcpy(sicm.loopStr, p+1, 40);
    sicm.loopStr[40] = 0;
    p += 41;
    memcpy(sicm.sizeStr, p+1, 40);
    sicm.sizeStr[40] = 0;
    p += 41;
    memcpy(sicm.posStr, p+1, 40);
    sicm.posStr[40] = 0;
    p += 41;

    dfield = gwy_data_field_new(sicm.xdim, sicm.ydim,
                                sicm.scanSize * 1e-8, sicm.scanSize * 1e-8,
                                FALSE);

    /* scale factor depends on the channel data type, which we get from the mode string */
    if (sicm.modeStr[0] == 'C') {
        /* current: we don't have any sensible way of scaling this, because the current is measured by an external
         * patch amplifier and delivered as a voltage using a mV/pA conversion rate that is not recorded in the file.
         * all we can realistically do is present that voltage, so fall through to case below */
        scaling = 1.0;
        unit_name = NULL;
    }
    else if (sicm.modeStr[0] == 'A') {
        /* ADC channels: scale to voltage range */
        scaling = sicm.fsdADC / 32767;
        unit_name = "V";
    }
    else {
        /* topography: scale to piezo range, which we don't explicitly know but can determine from sensitivity and
         * voltage range with a 1e-6 factor to convert from microns to
         metres */
        scaling = sicm.piezoCalZ * sicm.fsdHVA * sicm.fsdDAC * 1e-6 / 32767.0;
        unit_name = "m";
    }

    /* image data is stored bottom-up, so reverse order of rows */
    gwy_convert_raw_data(p, sicm.xdim*sicm.ydim, 1, GWY_RAW_DATA_SINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                         gwy_data_field_get_data(dfield), scaling, 0.0);
    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), unit_name);

    /* build the data container */
    container = gwy_container_new();
    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(0), dfield);
    gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(0),
                             gwy_convert_to_utf8(sicm.modeStr, -1, "ISO-8859-1"));

    /* add pretty much all metadata from the header a bunch of this doesn't seem to be used meaningfully; we prefix
     * probably useless or placeholder fields with a tilde so they sort to the bottom of the browser -- these should
     * probably be omitted entirely when we're sure they're not needed */
    meta = gwy_container_new();

    set_string_meta(meta, "Title", sicm.title);
    set_string_meta(meta, "Comment", sicm.comment);
    set_string_meta(meta, "Time Begun", sicm.date);
    set_string_meta(meta, "Time Completed", sicm.time);
    set_string_meta(meta, "Channel Mode", sicm.modeStr);
    set_string_meta(meta, "Loop Info", sicm.loopStr);
    set_string_meta(meta, "Scan Size", sicm.sizeStr);
    set_string_meta(meta, "Probe Position", sicm.posStr);

    set_double_meta(meta, "Head Voltage Amplifier FSD", "V", sicm.fsdHVA);
    set_double_meta(meta, "DA Converter FSD", "V", sicm.fsdDAC);
    set_double_meta(meta, "AD Converter FSD", "V", sicm.fsdADC);
    set_double_meta(meta, "Head Amplifier Gain", NULL, sicm.haGain);
    set_double_meta(meta, "Piezo X Sensitivity", NULL, sicm.piezoCalX);
    set_double_meta(meta, "Piezo Y Sensitivity", NULL, sicm.piezoCalY);
    set_double_meta(meta, "Piezo Z Sensitivity", NULL, sicm.piezoCalZ);
    set_double_meta(meta, "Z Gain", NULL, sicm.gainZ);
    set_double_meta(meta, "Size", "×10⁻⁸ m", sicm.scanSize);
    set_double_meta(meta, "Loop Gain", NULL, sicm.loopGain);
    set_double_meta(meta, "Set Point", NULL, sicm.setPoint);
    set_double_meta(meta, "~ Tip Voltage", "mV", sicm.tipVoltage);
    set_double_meta(meta, "Tip Position X", "×10⁻⁸ m", sicm.tipXPos);
    set_double_meta(meta, "Tip Position Y", "×10⁻⁸ m", sicm.tipYPos);
    set_double_meta(meta, "~ Plane Param A", NULL, sicm.A);
    set_double_meta(meta, "~ Plane Param B", NULL, sicm.B);
    set_double_meta(meta, "~ Plane Param D", NULL, sicm.D);
    set_double_meta(meta, "~ Plane Param Scale", NULL, sicm.scale);
    set_double_meta(meta, "Scan Angle", NULL, sicm.scanAngle);
    set_double_meta(meta, "~ Slope X", NULL, sicm.xSlope);
    set_double_meta(meta, "~ Slope Y", NULL, sicm.ySlope);
    set_double_meta(meta, "~ Range", NULL, sicm.range);

    gwy_container_set_string_by_name(meta,
                                     "~ vArray",
                                     g_strdup_printf("[%g, %g, %g, %g, %g, %g, %g, %g]",
                                                     sicm.vArray[0], sicm.vArray[1], sicm.vArray[2], sicm.vArray[3],
                                                     sicm.vArray[4], sicm.vArray[4], sicm.vArray[6], sicm.vArray[7]));
    gwy_container_set_string_by_name(meta,
                                     "~ offArray",
                                     g_strdup_printf("[%g, %g, %g, %g, %g, %g, %g, %g]",
                                                     sicm.offArray[0], sicm.offArray[1],
                                                     sicm.offArray[2], sicm.offArray[3],
                                                     sicm.offArray[4], sicm.offArray[4],
                                                     sicm.offArray[6], sicm.offArray[7]));
    set_double_meta(meta, "~ V Start", NULL, sicm.vStart);
    set_double_meta(meta, "~ V End", NULL, sicm.vEnd);
    set_double_meta(meta, "~ Threshold", NULL, sicm.threshold);

    set_int_meta(meta, "ADC Max", sicm.maxADC);
    set_int_meta(meta, "Oversampling (Control)", sicm.ctrlOS);
    set_int_meta(meta, "Oversampling (Image)", sicm.imagOS);
    set_int_meta(meta, "Control Points", sicm.ctrlPts);
    set_int_meta(meta, "~ Fit X", sicm.fitX);
    set_int_meta(meta, "~ Fit Y", sicm.fitY);
    set_int_meta(meta, "~ Min", sicm.min);
    set_int_meta(meta, "~ Max", sicm.max);
    set_int_meta(meta, "~ Scan Mode", sicm.scanMode);
    set_int_meta(meta, "~ NCITS", sicm.NCITS);
    set_int_meta(meta, "~ CITS Settle", sicm.settle);
    set_int_meta(meta, "Break Points", sicm.noPts);
    set_int_meta(meta, "~ Break Settle", sicm.settle2);
    set_int_meta(meta, "Loop Mode", sicm.loopMode);

    set_yesno_meta(meta, "~ Fitting", sicm.fitting);
    gwy_container_set_const_string_by_name(meta, "Polarity", sicm.polarity ? "positive" : "negative");
    set_yesno_meta(meta, "~ 1D Scan", sicm.scan1D);
    set_yesno_meta(meta, "Start Center", sicm.startCenter);

    /* Format has been updated to add fields that are only valid in hopping mode.
     * We prefix these with an asterisk so they sort together. In earlier versions this part of the header was left
     * empty, so for older files all these fields will be zero even when hopping was used. */
    /* The variables are actually ints here but that is OK. */
    set_double_meta(meta, "* Hop Amplitude", "nm", sicm.hopAmp);
    set_double_meta(meta, "* Rise Rate", "nm/ms", sicm.riseRate);
    set_double_meta(meta, "* Rise-to-Fall Time", "ms", sicm.riseToFallTime);
    set_double_meta(meta, "* Fall Rate", "nm/ms", sicm.fallRate);
    set_double_meta(meta, "* DC Break Set Point", "permille", sicm.dcSetPoint);
    set_double_meta(meta, "* Prescan Square Size", "pixels", sicm.prescanSqrSize);
    set_double_meta(meta, "* Prescan Hop Amplitude", "nm", sicm.prescanHopAmp);
    set_double_meta(meta, "* Minimum Hop Amplitude", "nm", sicm.minHopAmp);
    set_yesno_meta(meta, "* Absolute Hopping Mode", sicm.absHopMode);
    set_yesno_meta(meta, "* Fast Prescan Mode", sicm.fastPrescanMode);
    gwy_container_set_string_by_name(meta,
                                     "* Resolution Levels",
                                     g_strdup_printf("[%d, %d, %d, %d, %d, %d, %d, %d] (pixels)",
                                                     sicm.resLevels[0], sicm.resLevels[1],
                                                     sicm.resLevels[2], sicm.resLevels[3],
                                                     sicm.resLevels[4], sicm.resLevels[4],
                                                     sicm.resLevels[6], sicm.resLevels[7]));
    gwy_container_set_string_by_name(meta,
                                     "* Resolution Thresholds",
                                     g_strdup_printf("[%d, %d, %d, %d, %d, %d, %d, %d] (nm)",
                                                     sicm.resThresholds[0], sicm.resThresholds[1],
                                                     sicm.resThresholds[2], sicm.resThresholds[3],
                                                     sicm.resThresholds[4], sicm.resThresholds[4],
                                                     sicm.resThresholds[6], sicm.resThresholds[7]));
    set_int_meta(meta, "* Resolution Levels Used", sicm.numResLevels);

    gwy_container_pass_object(container, gwy_app_get_data_meta_key_for_id(0), meta);

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

/* Metadata helpers. */
static inline void
set_string_meta(GwyContainer *meta, const gchar *name, const gchar *latin1value)
{
    gchar *s = gwy_convert_to_utf8(latin1value, -1, "ISO-8859-1");
    if (s) {
        if (*s)
            gwy_container_set_string_by_name(meta, name, s);
        else
            g_free(s);
    }
}

static inline void
set_double_meta(GwyContainer *meta, const gchar *name, const gchar *unit, gdouble value)
{
    gboolean has_unit = unit && *unit;
    gchar *s = g_strdup_printf("%g%s%s", value, has_unit ? " " : "", has_unit ? unit : "");
    gwy_container_set_string_by_name(meta, name, s);
}

static inline void
set_int_meta(GwyContainer *meta, const gchar *name, gint value)
{
    gchar *s = g_strdup_printf("%d", value);
    gwy_container_set_string_by_name(meta, name, s);
}

static inline void
set_yesno_meta(GwyContainer *meta, const gchar *name, gboolean value)
{
    gwy_container_set_const_string_by_name(meta, name, value ? "yes" : "no");
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
