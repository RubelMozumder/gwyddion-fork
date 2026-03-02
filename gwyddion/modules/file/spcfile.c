/*
 *  $Id: spcfile.c 25794 2023-10-06 15:19:07Z yeti-dn $
 *  Copyright (C) 2017 Daniil Bratashov (dn2010), David Necas (Yeti)..
 *
 *  E-mail: dn2010@gwyddion.net
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
 * <mime-type type="application/x-spc-spm">
 *   <comment>Thermo Fisher SPC File</comment>
 *   <magic>
 *     <match type="string" offset="1" value="\x4B"/>
 *   </magic>
 *   <glob pattern="*.spc"/>
 *   <glob pattern="*.SPC"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Thermo Fisher SPC File
 * .spc
 * SPS
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libprocess/datafield.h>
#include <libprocess/spectra.h>
#include <libprocess/brick.h>
#include <libgwydgets/gwygraphmodel.h>
#include <libgwydgets/gwygraphbasics.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "get.h"

#define EXTENSION ".spc"

typedef enum {
    SPC_XUNITS_ARBITRARY   = 0,  /* Arbitrary */
    SPC_XUNITS_WAVENUMBER  = 1,  /* Wavenumber (cm-1) */
    SPC_XUNITS_MICROMETER  = 2,  /* Micrometers (um) */
    SPC_XUNITS_NANOMETER   = 3,  /* Nanometers (nm) */
    SPC_XUNITS_SECS        = 4,  /* Seconds */
    SPC_XUNITS_MINUTES     = 5,  /* Minutes */
    SPC_XUNITS_HERTZ       = 6,  /* Hertz (Hz) */
    SPC_XUNITS_KILOHERTZ   = 7,  /* Kilohertz (KHz) */
    SPC_XUNITS_MEGAHERTZ   = 8,  /* Megahertz (MHz) */
    SPC_XUNITS_MASSNUMBER  = 9,  /* Mass (M/z) */
    SPC_XUNITS_PPM         = 10, /* Parts per million (PPM) */
    SPC_XUNITS_DAYS        = 11, /* Days */
    SPC_XUNITS_YEARS       = 12, /* Years */
    SPC_XUNITS_RAMAN_SHIFT = 13, /* Raman Shift (cm-1) */
    SPC_XUNITS_EV          = 14, /* eV */
    SPC_XUNITS_TEXTLABEL   = 15, /* XYZ text labels in fcatxt (old 0x4D version only) */
    SPC_XUNITS_DIODE       = 16, /* Diode Number */
    SPC_XUNITS_CHANNEL     = 17, /* Channel */
    SPC_XUNITS_DEGREES     = 18, /* Degrees */
    SPC_XUNITS_DEGREES_F   = 19, /* Temperature (F) */
    SPC_XUNITS_DEGREES_C   = 20, /* Temperature (C) */
    SPC_XUNITS_DEGREES_K   = 21, /* Temperature (K) */
    SPC_XUNITS_POINTS      = 22, /* Data Points */
    SPC_XUNITS_MILLISECS   = 23, /* Milliseconds (mSec) */
    SPC_XUNITS_MICROSECS   = 24, /* Microseconds (uSec) */
    SPC_XUNITS_NANOSECS    = 25, /* Nanoseconds (nSec) */
    SPC_XUNITS_GIGAHERTZ   = 26, /* Gigahertz (GHz) */
    SPC_XUNITS_CM          = 27, /* Centimeters (cm) */
    SPC_XUNITS_METER       = 28, /* Meters (m) */
    SPC_XUNITS_MM          = 29, /* Millimeters (mm) */
    SPC_XUNITS_HOURS       = 30, /* Hours */
    SPC_XUNITS_DOUBLE_IGM  = 255 /* Double interferogram (no display labels) */
} SPCXUnits;

typedef enum {
    SPC_YUNITS_ARBITRARY     = 0,   /* Arbitrary Intensity */
    SPC_YUNITS_INTERFEROGRAM = 1,   /* Interferogram */
    SPC_YUNITS_ABSORBANCE    = 2,   /* Absorbance */
    SPC_YUNITS_KUBELKA_MONK  = 3,   /* Kubelka-Monk */
    SPC_YUNITS_COUNTS        = 4,   /* Counts */
    SPC_YUNITS_VOLTS         = 5,   /* Volts */
    SPC_YUNITS_DEGREES       = 6,   /* Degrees */
    SPC_YUNITS_MILLIAMPS     = 7,   /* Milliamps */
    SPC_YUNITS_MILLIMETERS   = 8,   /* Millimeters */
    SPC_YUNITS_MVOLTS        = 9,   /* Millivolts */
    SPC_YUNITS_YLOGONEDIVR   = 10,  /* Log(1/R) */
    SPC_YUNITS_YPERCENT      = 11,  /* Percent */
    SPC_YUNITS_YINTENSITY    = 12,  /* Intensity */
    SPC_YUNITS_YRELINTENSITY = 13,  /* Relative Intensity */
    SPC_YUNITS_YENERGY       = 14,  /* Energy */
    SPC_YUNITS_YDECIBEL      = 16,  /* Decibel */
    SPC_YUNITS_YDEGREEF      = 19,  /* Temperature (F) */
    SPC_YUNITS_YDEGREEC      = 20,  /* Temperature (C) */
    SPC_YUNITS_YDEGREEK      = 21,  /* Temperature (K) */
    SPC_YUNITS_YINDEXREFLECT = 22,  /* Index of Refraction [N] */
    SPC_YUNITS_YEXTINCTIONCF = 23,  /* Extinction Coeff. [K] */
    SPC_YUNITS_YREAL         = 24,  /* Real */
    SPC_YUNITS_YIMAGINARY    = 25,  /* Imaginary */
    SPC_YUNITS_YCOMPLEX      = 26,  /* Complex */
    SPC_YUNITS_YTRANSMISSION = 128, /* Transmission (ALL HIGHER MUST HAVE VALLEYS!) */
    SPC_YUNITS_YREFLECTANCE  = 129, /* Reflectance */
    SPC_YUNITS_YVALLEY       = 130, /* Arbitrary or Single Beam with Valley Peaks */
    SPC_YUNITS_YEMISN        = 131  /* Emission */
} SPCYUnits;

typedef enum {
    SPC_GENERAL      = 0,  /* General SPC (could be anything) */
    SPC_GC           = 1,  /* Gas Chromatogram */
    SPC_CHROMATOGRAM = 2,  /* General Chromatogram (same as SPCGEN with TCGRAM) */
    SPC_HPLC         = 3,  /* HPLC Chromatogram */
    SPC_FTIR         = 4,  /* FT-IR, FT-NIR, FT-Raman Spectrum or Igram (Can also be used for scanning IR.) */
    SPC_NIR          = 5,  /* NIR Spectrum (Usually multi-spectral data sets for calibration.) */
    SPC_UVVIS        = 7,  /* UV-VIS Spectrum (Can be used for single scanning UV-VIS-NIR.) */
    SPC_XRAY         = 8,  /* X-ray Diffraction Spectrum */
    SPC_MS           = 9,  /* Mass Spectrum  (Can be single, GC-MS, Continuum, Centroid or TOF.) */
    SPC_NMR          = 10, /* NMR Spectrum or FID */
    SPC_RAMAN        = 11, /* Raman Spectrum (Usually Diode Array, CCD, etc. use SPCFTIR for FT-Raman.) */
    SPC_FLUORESCENCE = 12, /* Fluorescence Spectrum */
    SPC_ATOMIC       = 13, /* Atomic Spectrum */
    SPC_DIODEARRAY   = 14  /* Chromatography Diode Array Spectra */
} SPCExperimentType;

static const GwyEnum spc_xunits[] = {
    { "",        SPC_XUNITS_ARBITRARY,   },
    { "1/cm",    SPC_XUNITS_WAVENUMBER,  },
    { "µm",      SPC_XUNITS_MICROMETER,  },
    { "nm",      SPC_XUNITS_NANOMETER,   },
    { "s",       SPC_XUNITS_SECS,        },
    { "minutes", SPC_XUNITS_MINUTES,     },
    { "Hz",      SPC_XUNITS_HERTZ,       },
    { "kHz",     SPC_XUNITS_KILOHERTZ,   },
    { "MHz",     SPC_XUNITS_MEGAHERTZ,   },
    { "",        SPC_XUNITS_MASSNUMBER,  },
    { "PPM",     SPC_XUNITS_PPM,         },
    { "days",    SPC_XUNITS_DAYS,        },
    { "years",   SPC_XUNITS_YEARS,       },
    { "1/cm",    SPC_XUNITS_RAMAN_SHIFT, },
    { "eV",      SPC_XUNITS_EV,          },
    { "",        SPC_XUNITS_TEXTLABEL,   },
    { "Diode",   SPC_XUNITS_DIODE,       },
    { "Channel", SPC_XUNITS_CHANNEL,     },
    { "deg",     SPC_XUNITS_DEGREES,     },
    { "°F",      SPC_XUNITS_DEGREES_F,   },
    { "°C",      SPC_XUNITS_DEGREES_C,   },
    { "°K",      SPC_XUNITS_DEGREES_K,   },
    { "pt",      SPC_XUNITS_POINTS,      },
    { "ms",      SPC_XUNITS_MILLISECS,   },
    { "µs",      SPC_XUNITS_MICROSECS,   },
    { "ns",      SPC_XUNITS_NANOSECS,    },
    { "GHz",     SPC_XUNITS_GIGAHERTZ,   },
    { "cm",      SPC_XUNITS_CM,          },
    { "m",       SPC_XUNITS_METER,       },
    { "mm",      SPC_XUNITS_MM,          },
    { "hours",   SPC_XUNITS_HOURS,       },
    { "",        SPC_XUNITS_DOUBLE_IGM,  },
};

static const GwyEnum spc_yunits[] = {
    { "",       SPC_YUNITS_ARBITRARY,     },
    { "",       SPC_YUNITS_INTERFEROGRAM, },
    { "",       SPC_YUNITS_ABSORBANCE,    },
    { "",       SPC_YUNITS_KUBELKA_MONK,  },
    { "Counts", SPC_YUNITS_COUNTS,        },
    { "V",      SPC_YUNITS_VOLTS,         },
    { "deg",    SPC_YUNITS_DEGREES,       },
    { "mA",     SPC_YUNITS_MILLIAMPS,     },
    { "mm",     SPC_YUNITS_MILLIMETERS,   },
    { "mV",     SPC_YUNITS_MVOLTS,        },
    { "",       SPC_YUNITS_YLOGONEDIVR,   },
    { "%",      SPC_YUNITS_YPERCENT,      },
    { "",       SPC_YUNITS_YINTENSITY,    },
    { "",       SPC_YUNITS_YRELINTENSITY, },
    { "",       SPC_YUNITS_YENERGY,       },
    { "dB",     SPC_YUNITS_YDECIBEL,      },
    { "°F",     SPC_YUNITS_YDEGREEF,      },
    { "°C",     SPC_YUNITS_YDEGREEC,      },
    { "°K",     SPC_YUNITS_YDEGREEK,      },
    { "",       SPC_YUNITS_YINDEXREFLECT, },
    { "",       SPC_YUNITS_YEXTINCTIONCF, },
    { "",       SPC_YUNITS_YREAL,         },
    { "",       SPC_YUNITS_YIMAGINARY,    },
    { "",       SPC_YUNITS_YCOMPLEX,      },
    { "",       SPC_YUNITS_YTRANSMISSION, },
    { "",       SPC_YUNITS_YREFLECTANCE,  },
    { "",       SPC_YUNITS_YVALLEY,       },
    { "",       SPC_YUNITS_YEMISN,        },
};

typedef struct {
    gboolean   precision16bit;
    gboolean   experiment_extension;
    gboolean   multifile;
    gboolean   z_random;
    gboolean   z_noneven;
    gboolean   custom_axis_labels;
    gboolean   x_for_all;
    gboolean   xy_file;
    gchar      version;
    gchar      experiment_type_code;
    guchar     exponent;
    guint32    point_number;
    gdouble    x_first;
    gdouble    x_last;
    guint32    subfiles_number;
    gchar      x_units;
    gchar      y_units;
    gchar      z_units;
    gchar      posting_disposition;
    guint32    date;
    gchar*     resolution_description;
    gchar*     source_instrument;
    guint16    peak_points;
    gfloat     spare[8];
    gchar*     memo;
    gchar*     custom_axis_strings;
    guint32    logblock_offset;
    guint32    file_modification;
    gchar      processing_code;
    gchar      calibration_level;
    guint16    submetod_sample_injection;
    gfloat     concentration_factor;
    gchar*     method_file;
    gfloat     z_subfile_increment;
    gint32     w_planes;
    gfloat     w_increment;
    gchar      w_units;
    gchar*     reserved;
} SPCMainHeader;

typedef struct {
    gboolean subfile_changed;
    gboolean do_not_use_peak_table;
    gboolean subfile_modified_by_arithmetic;
    gchar    exponent;
    guint16  subfile_index;
    gfloat   z_start_value;
    gfloat   z_end_value;
    gfloat   z_noise_value;
    gint32   point_number;
    gint32   coadded_number;
    gfloat   w_axis_value;
    gchar*   reserved; // 4
} SPCSubHeader;

static gboolean      module_register     (void);
static gint          spc_detect          (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name);
static GwyContainer* spc_load            (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error);
static void          spc_read_main_header(const guchar *buffer,
                                          SPCMainHeader *header,
                                          GError **error);
static void          spc_read_subheader  (const guchar *buffer,
                                          SPCSubHeader *header);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports Thermo Fisher SPC files."),
    "Daniil Bratashov <dn2010@gwyddion.net>",
    "0.3",
    "Daniil Bratashov (dn2010), David Necas (Yeti)",
    "2018",
};

GWY_MODULE_QUERY2(module_info, spcfile)

static gboolean
module_register(void)
{
    gwy_file_func_register("spcfile",
                           N_("Thermo Fisher SPC files"),
                           (GwyFileDetectFunc)&spc_detect,
                           (GwyFileLoadFunc)&spc_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
spc_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name)
{
    gint score = 0;

    if (only_name)
        return (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION)) ? 10 : 0;

    if ((fileinfo->buffer_len > 512) && (*(fileinfo->head + 1) == 0x4B)) {
        score = 20;
        if (g_str_has_suffix(fileinfo->name_lowercase, EXTENSION))
            score += 10;
        if (*(fileinfo->head + 2) > 14)
            score = 0;
    }

    return score;
}

static GwyContainer*
spc_load(const gchar *filename,
         G_GNUC_UNUSED GwyRunType mode,
         GError **error)
{
    GwyContainer *container = NULL;
    guchar *buffer = NULL;
    gsize size = 0;
    gint remaining = 0;
    GError *err = NULL;
    const guchar *p;
    SPCMainHeader *header = NULL;
    SPCSubHeader *subheader;
    GwySIUnit *siunitx, *siunity;
    const gchar *unit;
    gdouble *xdata, *ydata;
    gdouble scale, xscale, yscale;
    gint i, zres, power10x, power10y, npoints;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    gchar **axesstrings;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        goto fail;
    }

    if (size < 512) { /* header too short */
        err_TOO_SHORT(error);
        goto fail;
    }

    p = buffer;
    remaining = size;

    header = g_new0(SPCMainHeader, 1);
    spc_read_main_header(p, header, &err);
    p += 512;
    remaining -= 512;
    npoints = header->point_number;
    if (remaining < npoints*sizeof(gfloat) + 32) {
        err_TOO_SHORT(error);
        goto fail;
    }

    /* We can only import a single spectrum. */
    if (header->subfiles_number != 1) {
        err_NO_DATA(error);
        goto fail;
    }

    container = gwy_container_new();
    xscale = 1.0;

    gwy_debug("x units = %d", header->x_units);
    unit = gwy_enum_to_string(header->x_units, spc_xunits, 32);
    siunitx = gwy_si_unit_new_parse(unit, &power10x);
    xscale = pow10(power10x);
    if (xscale == 0.0)
        xscale = 1.0;

    gwy_debug("y units = %d", header->y_units);
    unit = gwy_enum_to_string(header->y_units, spc_yunits, 28);
    siunity = gwy_si_unit_new_parse(unit, &power10y);
    yscale = pow10(power10y);
    if (yscale == 0.0)
        yscale = 1.0;

    xdata = g_new0(gdouble, npoints);
    if (header->xy_file) {
        if (!header->x_for_all) {
            gwy_convert_raw_data(p, npoints, 1, GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN, xdata, xscale, 0.0);
            p += npoints * sizeof(gfloat);
            remaining -= npoints * sizeof(gfloat);
        }
    }
    else {
        for (i = 0; i < npoints; i++) {
            xdata[i] = xscale*(header->x_first + i/(npoints - 1.0) * (header->x_last - header->x_first));
        }
    }

    subheader = g_new0(SPCSubHeader, 1);
    spc_read_subheader(p, subheader);
    p += 32;
    remaining -= 32;
    zres = npoints;

    if (header->x_for_all) {
        zres = subheader->point_number;
        g_free(xdata);
        xdata = g_new0(gdouble, zres);
        gwy_debug("converting x data, remaining=%d", remaining);
        gwy_convert_raw_data(p, zres, 1, GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN, xdata, xscale, 0.0);
        p += zres * sizeof(gfloat);
        remaining -= zres * sizeof(gfloat);
    }

    ydata = g_new0(gdouble, zres);
    gwy_debug("converting y data, remaining=%d", remaining);
    if (header->exponent == 0x80) {
        gwy_convert_raw_data(p, zres, 1, GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN, ydata, yscale, 0.0);
        p += zres * sizeof(gfloat);
        remaining -= zres * sizeof(gfloat);
    }
    else if (header->precision16bit) {
        scale = exp2(header->exponent)/65536.0 * yscale;
        gwy_convert_raw_data(p, zres, 1, GWY_RAW_DATA_SINT16, GWY_BYTE_ORDER_LITTLE_ENDIAN, ydata, scale, 0.0);
        p += zres * sizeof(gint16);
        remaining -= zres * sizeof(gint16);
    }
    else {
        scale = exp2(header->exponent)/4294967296.0 * yscale;
        gwy_convert_raw_data(p, zres, 1, GWY_RAW_DATA_SINT32, GWY_BYTE_ORDER_LITTLE_ENDIAN, ydata, scale, 0.0);
        p += zres * sizeof(gint32);
        remaining -= zres * sizeof(gint32);
    }

    gmodel = g_object_new(GWY_TYPE_GRAPH_MODEL,
                          "si-unit-x", siunitx,
                          "si-unit-y", siunity,
                          NULL);

    if (header->custom_axis_labels) {
        axesstrings = g_strsplit(header->custom_axis_strings, " ", -1);
        if (g_strv_length(axesstrings) >= 2) {
            g_object_set(gmodel,
                         "axis-label-bottom", axesstrings[0],
                         "axis-label-left", axesstrings[1],
                         NULL);
        }
        g_strfreev(axesstrings);
    }

    gcmodel = g_object_new(GWY_TYPE_GRAPH_CURVE_MODEL,
                           "mode", GWY_GRAPH_CURVE_LINE,
                           "color", gwy_graph_get_preset_color(0),
                           NULL);
    gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, zres);
    g_free(xdata);
    g_free(ydata);
    gwy_graph_curve_model_enforce_order(gcmodel);
    gwy_graph_model_add_curve(gmodel, gcmodel);
    g_object_unref(gcmodel);
    gwy_container_pass_object_by_name(container, "/0/graph/graph/1", gmodel);
    g_free(subheader);

    g_object_unref(siunitx);
    g_object_unref(siunity);
fail:
    g_free(header);
    gwy_file_abandon_contents(buffer, size, NULL);

    return container;
}

static void
spc_read_main_header(const guchar *buffer,
                     SPCMainHeader *header,
                     GError **error)
{
    guchar flags;
    const guchar *p;
    gint i;

    p = (guchar *)buffer;
    flags = *(p++);
    gwy_debug("flags=%d", flags);
    header->precision16bit = (flags & 1) ? TRUE : FALSE;
    header->experiment_extension = (flags & 2) ? TRUE : FALSE;
    header->multifile = (flags & 4) ? TRUE : FALSE;
    header->z_random = (flags & 8) ? TRUE : FALSE;
    header->z_noneven = (flags & 16) ? TRUE : FALSE;
    header->custom_axis_labels = (flags & 32) ? TRUE : FALSE;
    header->x_for_all = (flags & 64) ? TRUE : FALSE;
    header->xy_file = (flags & 128) ? TRUE : FALSE;

    header->version = *(p++);
    gwy_debug("version=%d", header->version);
    header->experiment_type_code = *(p++);
    gwy_debug("experiment type=%d", header->experiment_type_code);

    if ((header->version != 0x4B) || (header->experiment_type_code > 14)) {
        err_FILE_TYPE(error, "Thermo Fisher SPC");
        return;
    }

    header->exponent = *(p++);
    gwy_debug("exponent=%d", header->exponent);
    header->point_number = gwy_get_guint32_le(&p);
    gwy_debug("point_number=%d", header->point_number);
    header->x_first = gwy_get_gdouble_le(&p);
    header->x_last  = gwy_get_gdouble_le(&p);
    header->subfiles_number = gwy_get_guint32_le(&p);
    gwy_debug("subfiles=%d", header->subfiles_number);
    header->x_units = *(p++);
    header->y_units = *(p++);
    header->z_units = *(p++);
    header->posting_disposition = *(p++);
    header->date = gwy_get_guint32_le(&p);

    header->resolution_description = (guchar*)p;
    p += 9;
    header->source_instrument = (guchar*)p;
    p += 9;
    header->peak_points = gwy_get_guint16_le(&p);
    for (i = 0; i < 8; i++) {
        header->spare[i] = gwy_get_gfloat_le(&p);
    }
    header->memo = (guchar*)p;
    p += 130;
    header->custom_axis_strings = (guchar*)p;
    p += 30;
    header->logblock_offset = gwy_get_guint32_le(&p);
    header->file_modification = gwy_get_guint32_le(&p);
    header->processing_code = *(p++);
    header->calibration_level = *(p++);
    header->submetod_sample_injection = gwy_get_guint16_le(&p);
    header->concentration_factor = gwy_get_gfloat_le(&p);
    header->method_file = (guchar*)p;
    p += 48;
    header->z_subfile_increment = gwy_get_gfloat_le(&p);
    header->w_planes = gwy_get_gint32_le(&p);
    gwy_debug("w planes= %d", header->w_planes);
    header->w_increment = gwy_get_gfloat_le(&p);
    header->w_units = *(p++);
    header->reserved = (guchar*)p;
    p += 187;
}

static void
spc_read_subheader(const guchar *buffer,
                   SPCSubHeader *header)
{
    guchar flags;
    const guchar *p;

    p = buffer;
    flags = *(p++);
    header->subfile_changed = (flags & 1) ? TRUE : FALSE;
    header->do_not_use_peak_table = (flags & 8) ? TRUE : FALSE;
    header->subfile_modified_by_arithmetic = (flags & 128) ? TRUE : FALSE;
    header->exponent = *(p++);
    header->subfile_index = gwy_get_guint16_le(&p);
    header->z_start_value = gwy_get_gfloat_le(&p);
    header->z_end_value = gwy_get_gfloat_le(&p);
    header->z_noise_value = gwy_get_gfloat_le(&p);
    header->point_number = gwy_get_gint32_le(&p);
    gwy_debug("point number = %d", header->point_number);
    header->coadded_number = gwy_get_gint32_le(&p);
    header->w_axis_value = gwy_get_gfloat_le(&p);
    header->reserved = (guchar *)p;
    p += 4;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
