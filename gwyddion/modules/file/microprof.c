/*
 *  $Id: microprof.c 25893 2023-10-22 05:25:07Z yeti-dn $
 *  Copyright (C) 2005-2020 David Necas (Yeti), Petr Klapetek.
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
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-microprof-txt">
 *   <comment>MicroProf FRT text data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="HeaderLines"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-microprof">
 *   <comment>MicroProf FRT data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="FRTM_"/>
 *   </magic>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # MicroProf FRT has binary and text data files.
 * 0 string FRTM_ MicroProf FRT profilometry data
 * 0 string HeaderLines=
 * >&0 search/80 ScanMode= MicroProf FRT profilometry text data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * MicroProf TXT
 * .txt
 * Read
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * MicroProf FRT
 * .frt
 * Read
 **/

#include "config.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwyutils.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/datafield.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>

#include "get.h"
#include "err.h"

#define MAGIC_PREFIX      "FRTM_GLIDERV"
#define MAGIC_PREFIX_SIZE (sizeof(MAGIC_PREFIX) - 1)

#define MAGIC_VER_SIZE (sizeof("1.00") - 1)

#define MAGIC_TXT      "HeaderLines="
#define MAGIC_TXT_SIZE (sizeof(MAGIC_TXT) - 1)

#define EXTENSION ".frt"
#define EXTENSION_TXT ".txt"

enum {
    MICROPROF_MIN_HEADER_SIZE = 122,
    MICROPROF_MIN_TEXT_SIZE = 80,
    MAX_BLOCK_ID = 0x100,
};

typedef enum {
    /* Types. */
    MICROPROF_PIEZO            = 0x00000001u,
    MICROPROF_INTENSITY        = 0x00000002u,
    MICROPROF_TOPOGRAPHY       = 0x00000004u,
    MICROPROF_REAL_PART        = 0x00000008u,
    MICROPROF_IMAG_PART        = 0x00000010u,
    MICROPROF_CAMERA           = 0x00000040u,
    MICROPROF_THICKNES         = 0x00000080u,
    MICROPROF_DIB_FROM_FILE    = 0x00000100u,
    MICROPROF_ABS_VAL          = 0x00000200u,
    MICROPROF_PHASE            = 0x00000400u,
    MICROPROF_SAMPLE_THICKNESS = 0x00000800u,
    MICROPROF_AFM              = 0x00001000u,
    MICROPROF_QUALITY          = 0x00002000u,
    /* Numbers above 0x4000 are some types of fit.  The above flags do not
     * apply here. */
    MICROPROF_FIT              = 0x00004001u,
    MICROPROF_SLOPE            = 0x00004001u,
    MICROPROF_TYPE_MASK        = 0x0000ffdfu,

    /* Flags. */
    MICROPROF_TOP_SENSOR       = 0x00000000u, /* Yes, there is no flag. */
    MICROPROF_BOTTOM_SENSOR    = 0x10000000u,
    MICROPROF_SENSOR_MASK      = 0x30000000u,

    /* Buffer counter mask.  Just a shifted number.  */
    MICROPROF_BUFFER_CNTR_MASK = 0x0f000000u,

    /* Flags. */
    MICROPROF_EXTENDED         = 0x00010000u,
    MICROPROF_COMPUTED         = 0x00020000u,
    MICROPROF_FILTERED         = 0x00000020u,
    MICROPROF_FLAG_MASK        = 0x00030020u,
} MicroProfDataType;

typedef enum {
    MICROPROF_POSX_LOWY          = 1,
    MICROPROF_NEGX_LOWY          = 2,
    MICROPROF_POSY_LOWX          = 3,
    MICROPROF_NEGY_LOWX          = 4,
    MICROPROF_POSX_HIGHY         = 5,
    MICROPROF_NEGX_HIGHY         = 6,
    MICROPROF_POSY_HIGHX         = 7,
    MICROPROF_NEGY_HIGHX         = 8,
    MICROPROF_MEANDER_POSX_LOWY  = 9,
    MICROPROF_MEANDER_POSY_LOWX  = 10,
    MICROPROF_MEANDER_POSX_HIGHY = 11,
    MICROPROF_MEANDER_POSY_HIGHX = 12,
} MicroProfScanDir;

typedef enum {
    MICROPROF_8um   = 0,
    MICROPROF_80um  = 1,
    MICROPROF_800um = 2,
    MICROPROF_1um   = 3,
    MICROPROF_10um  = 4,
    MICROPROF_100um = 5,
    MICROPROF_200um = 6,
} MicroProfMeasRange;

typedef enum {
    MICROPROF_STANDARD   = 0,
    MICROPROF_OPTIMIZED  = 1,
    MICROPROF_GREYSCALE  = 2,
    MICROPROF_BLUEWHITE  = 3,
    MICROPROF_REDWHITE   = 4,
    MICROPROF_GREENWHITE = 5,
} MicroProfPalette;

/* A small chunk before each image in the images data block 0x007d in the multi-image part.  We synthesize it for the
 * single-image part from block 0x0066. */
typedef struct {
    MicroProfDataType datatype;
    guint xres;
    guint yres;
    guint bpp;
    const guchar *data;
} MicroProfImageBlock;

/* Blocks occurring in the single-image part/version. */
/* Image data. */
typedef struct {
    guint size;
    /* Just a pointer to file buffer.  The data is actually either guint16 or gint32, depending on bpp in block 0065. */
    const guchar *data;
} MicroProfBlock000b;

/* Description. */
typedef struct {
    gchar *text;
} MicroProfBlock0065;

/* Image size. */
typedef struct {
    guint xres;        /* X pixel size. */
    guint yres;        /* Y pixel size. */
    guint bpp;         /* Bits per sample. */
} MicroProfBlock0066;

/* Scan size. */
typedef struct {
    gdouble xrange;    /* Horizontal dimension in m. */
    gdouble yrange;    /* Vertical dimension in m. */
    gdouble xoffset;   /* Horizontal offset of the centre in m. */
    gdouble yoffset;   /* Horizontal offset of the centre in m. */
    gdouble factor_range_y;  /* Y range relative to X, seems to be simply
                                equal to yrange/xrange. */
    MicroProfScanDir scandir;  /* Informative, not physical data layout. */
} MicroProfBlock0067;

/* Sensor. */
typedef struct {
    MicroProfMeasRange meas_range;   /* Full sensor range type (+-). */
    gdouble zscale;    /* Conversion factor to heights in m. */
} MicroProfBlock006c;

/* Z offset. */
typedef struct {
    gdouble zoffset;   /* User adjustment of the false colour bar scale, in
                          metres. */
} MicroProfBlock0071;

/* Name of the parameter set. */
typedef struct {
    gchar *parset_name;
} MicroProfBlock0077;

/* Blocks occurring in the multi-image part. */
/* Scan speed. */
typedef struct {
    gdouble xspeed;           /* Horizontal speed in m/s. */
    gdouble yspeed;           /* Vertical speed in m/s. */
    gboolean override_speed;  /* Use delay times instead. */
    gboolean check_sensor_error;
    gboolean scan_back_meas;
    guint sensor_delay;       /* In milliseconds (if override_speed = TRUE). */
    guint sensor_error_time;  /* In milliseconds. */
} MicroProfBlock0068;

/* Display scan units. */
typedef struct {
    /* These take values -3, -6, -9, corresponding to milli, micro, nano. */
    gint range_unit_type;
    gint offset_unit_type;
    gint xspeed_unit_type;
    gint yspeed_unit_type;
} MicroProfBlock0069;

/* Scan steps. */
typedef struct {
    guint step_xcount;
    guint step_ycount;
    gdouble xstep;     /* Always 5e-8. */
    gdouble ystep;     /* Always 5e-8. */
    guint step_delay;  /* In microseconds. */
    gboolean back_scan_step;
} MicroProfBlock006a;

/* Scan division */
typedef struct {
    guint wait_at_start_of_line;
    gboolean display_start_box;
    gboolean do_hysteresis_corr;
    gboolean back_scan_delay;
} MicroProfBlock006b;

/* Display setup. */
typedef struct {
    gdouble zrange;
    gdouble use_percentage;
    guint display_correction;  /* Some line levelling type. */
    MicroProfPalette palette_type;
    guint display_size;        /* Zoom type.  There are also pixelwise (3) vs. physical (4), but we do not follow
                                * this. */
    gboolean autorange;
} MicroProfBlock006d;

/* Hardware. */
typedef struct {
    guint sensor_type;    /* TODO: A long list, but users may want to see this in metadata. */
    guint xytable_type;
    guint ztable_type;
} MicroProfBlock006e;

/* Sensor. */
typedef struct {
    gboolean do_integrate;
    guint integrate_over;
    gboolean sensor_was_piezo;
    gboolean sensor_was_full;
} MicroProfBlock006f;

/* Valid values. */
typedef struct {
    guint first_valid;    /* The smallest valid value. */
    guint last_valid;     /* The largest valid value. */
} MicroProfBlock0070;

/* Time. */
typedef struct {
    guint meas_started;  /* Timestamp, seconds since 1970-01-01. */
    guint meas_ended;    /* Timestamp, seconds since 1970-01-01. */
    guint meas_time;     /* Measurement time, in seconds. */
} MicroProfBlock0072;

/* Hardware. */
typedef struct {
    guint dio_type;      /* No idea, and typical values 6 and 10 are not even
                            mentioned in the specs. */
} MicroProfBlock0073;

/* DDL version. */
typedef struct {
    guint dllver1;
    guint dllver2;
} MicroProfBlock0074;

/* Drift. */
typedef struct {
    guint nvalues;
    gboolean is_applied;
    gboolean do_drift_corr_scan;
    gboolean data_available;
    gboolean line_not_row;
    /* Then there are nvalues 32bit ints representing the drift data. */
    const guchar *data;
} MicroProfBlock0075;

/* Line scan. */
typedef struct {
    gdouble xstart;  /* In metres. */
    gdouble ystart;  /* In metres. */
    gdouble xend;    /* In metres. */
    gdouble yend;    /* In metres. */
} MicroProfBlock0076;

/* X position (for 2D scan x data). */
typedef struct {
    gboolean data_are_valid;
    const guchar *data;   /* Xres double values, in metres. */
} MicroProfBlock0078;

/* Display offset in Mark III. */
typedef struct {
    gdouble xdispoffset;
    gdouble ydispoffset;
} MicroProfBlock0079;

/* Optical sensor. */
typedef struct {
    guint meas_rate;    /* 0 means 1 Hz, then it goes 3, 10, 30, 100, ... */
    guint min_intensity;
} MicroProfBlock007a;

/* Hardware. */
typedef struct {
    guint sensor_subtype;
    guint xytable_subtype;
} MicroProfBlock007b;

/* Speed control. */
typedef struct {
    gboolean speed_control;  /* XY table uses speed instead of delay. */
} MicroProfBlock007c;

/* Multibuffer. */
typedef struct {
    /* This is some composite stuff containing buffer type, counters and flags for the current buffer.  The specs say
     * it is a single int, but that is obviously not the case. */
    guint currbuf_id1;
    guint currbuf_id2;
    guint currbuf_id3;
    guint currbuf_id4;
    guint nimages;
    MicroProfImageBlock *imgblocks;
} MicroProfBlock007d;

/* Maxtable range. */
typedef struct {
    gdouble max_xrange;
    gdouble max_yrange;
} MicroProfBlock007e;

/* Calibration x. */
typedef struct {
    guchar calibration[255];      /* Always Uncalibrated. */
    gboolean is_calibrated;
} MicroProfBlock007f;

/* Z motor control. */
typedef struct {
    gboolean is_z_motor_ctrl_on;
} MicroProfBlock0080;

/* Layers. */
/* This gives scanning ranges (for locating the layer boundaries, I guess). */
typedef struct {
    guint nlayers;
    gdouble range1;      /* Range for the first layer. */
    gdouble range_rest;  /* Range for the other layers. */
} MicroProfBlock0081;

/* Sensor 4 motion type. */
typedef struct {
    guint motion_type;
} MicroProfBlock0082;

/* Sensor 4 data type. */
typedef struct {
    guint data_type;
} MicroProfBlock0083;

/* Layer 2. */
typedef struct {
    gboolean use_std_schichthohe;
} MicroProfBlock0084;

/* PCL 816 params. */
typedef struct {
    guint volt_range;
    guint val_channel;
    guint int_channel;
    gdouble val_range;
    guint int_range;            /* In bits. */
    gdouble min_valid_val;      /* In Volts. */
    gdouble max_valid_val;      /* In Volts. */
    gdouble min_valid_intens;   /* In Volts. */
    gdouble max_valid_intens;   /* In Volts. */
    guchar unit_list[8*16];
    guint selected_unit;
} MicroProfBlock0085;

/* Dongle ID. */
typedef struct {
    guint product_id;
    guint series_no;
} MicroProfBlock0086;

/* Display absolute. */
typedef struct {
    gboolean use_frt_offset;
} MicroProfBlock0087;

/* PCL 816 params. */
typedef struct {
    guint volt_range;
    guint val_channel;
    guint int_channel;
    guint int_range;            /* In bits. */
    gdouble min_valid_val;      /* In Volts. */
    gdouble max_valid_val;      /* In Volts. */
    gdouble min_valid_intens;   /* In Volts. */
    gdouble max_valid_intens;   /* In Volts. */
    guchar unit_list[8*16];
    guint selected_unit;
    gdouble min_valid_unit_value;
    gdouble max_valid_unit_value;
} MicroProfBlock0088;

/* Approach and retract. */
typedef struct {
    gboolean auto_approach;
    gboolean auto_retract;
} MicroProfBlock0089;

/* Z drive after approach. */
typedef struct {
    gboolean zmotor_drive_allowed;
    gdouble zmotor_drive_way;
} MicroProfBlock008a;

/* Wait at box start. */
typedef struct {
    gboolean do_wait;
} MicroProfBlock008b;

/* Display setup for Mark III. */
typedef struct {
    gdouble tv_range;
    gdouble tv_offset;
    guint set_tv_offset;
    guint set_tv_automatic;
    gdouble tv_range_percent;
} MicroProfBlock008c;

/* Eddy sensor. */
typedef struct {
    guint meas_mode;
    gdouble height_edit;
    gdouble topo_edit;
    guint pref_mode;
    gdouble freq_edit;
    guint hf_edit;
    guint nf_edit;
    gdouble phase_edit;
    guint nf_mode;
    gdouble topo_scale;
} MicroProfBlock008d;

/* CHR angle correction. */
typedef struct {
    gchar *ser_num;
    guint day;  /* These all related to some correction file and are unset otherwise.  Never seen any. */
    guint month;
    guint year;
    gboolean was_created;
    guint nvalues;
    const guchar *data;   /* nvalues (alpha,z) pairs, we ignore this. */
} MicroProfBlock008e;

/* Z motor auto move. */
typedef struct {
    gboolean tracking_mode_activated;
} MicroProfBlock008f;

/* Automatic data filtering. */
typedef struct {
    gboolean despike_do_it;
    gdouble despike_threshold;
    gboolean filter_meas_do_it;
    guint filter_meas_type;
    gdouble filter_meas_param;
    gboolean tip_simul_do_it;
    gdouble tip_simul_angle;
    gdouble tip_simul_radius;
} MicroProfBlock0090;

/* WS98 EN. */
typedef struct {
    guint active;
    gdouble frequency;      /* In Hertzs. */
    gdouble ac_dB;          /* In deciBells. */
    gdouble low_pass;       /* In Hertzs. */
    gdouble high_pass;      /* In Hertzs. */
    gdouble out_gain;       /* In deciBells. */
    gdouble pre_gain;       /* In deciBells. */
} MicroProfBlockSub0091;

typedef struct {
    gboolean topography;
    gboolean differential;
    gdouble topo_edit;      /* In metres. */
    gdouble height_edit     /* In metres. */;
    gdouble topo_scale;
    guint nsubblocks;
    MicroProfBlockSub0091 *subblocks;
} MicroProfBlock0091;

/* Data manipulation something. */
typedef struct {
    guchar data[264];
} MicroProfBlock0092;

/* Defined colours. */
typedef struct {
    guint invalid_values;
    guint lower_values;
    guint upper_values;
} MicroProfBlock0093;

/* WS98 EN filter. */
typedef struct {
    gdouble min_teach;
    gdouble max_teach;
    guint min_norm_teach;
    guint max_norm_teach;
    guchar *name_of_teach;
    guint scale_teach;
} MicroProfBlock0094;

/* Thickness mode. */
typedef struct {
    gboolean thickness_mode;
    guint kind_of_thickness;         /* Some strange enum. */
    gdouble refractive_index;
} MicroProfBlock0095;

/* Interferometric thickness. */
typedef struct {
    gboolean thickness_lints_on;
    gdouble low_limit;    /* In metres. */
    gdouble high_limit;   /* In metres. */
} MicroProfBlock0096;

/* Konoscopic sensor. */
typedef struct {
    guint laser_power;
    guint laser_power_fine;
    guint laser_frequency;    /* In Hertzs. */
    guint intensity;
    guint min_valid_intens;   /* In percents. */
} MicroProfBlock0097;

/* Z table. */
typedef struct {
    gdouble meas_z_position;
} MicroProfBlock0098;

/* Dual scan mode. */
typedef struct {
    gboolean is_dual_scan;
    gdouble scan_frequency;     /* In Hertzs. */
    gdouble duty;               /* In percents. */
} MicroProfBlock0099;

/* TTV */
typedef struct {
    gboolean is_ttv;    /* Everything else is zero if FALSE. */
    guint meas_rate2;
    guint intensity2;
    gdouble zoffsets1;
    gdouble zoffsets2;
    gdouble scale1;
    gdouble scale2;     /* Equal to zscale (if nonzero). */
} MicroProfBlock009a;

/* Roundness. */
typedef struct {
    gboolean is_roundness;
    gboolean is_sample_used;
    gdouble radius;
    gdouble max_xrange;
    gdouble max_yrange;
} MicroProfBlock009b;

/* Display setup. */
typedef struct {
    gboolean do_despike;
    gboolean do_interpolate;
} MicroProfBlock009c;

/* Display setup. */
typedef struct {
    guint subtract_sinus;
} MicroProfBlock009d;

/* Sensor. */
typedef struct {
    guint layer_info;
    gdouble fit_threshold;
} MicroProfBlock009e;

/* Scan units. */
typedef struct {
    gchar *zunit;        /* Units of image data (which one?).  */
} MicroProfBlock009f;

/* WLI sensor. */
typedef struct {
    guint brightness;
    guint eval_method;
    guint focus;
    guint gain;
    guint meas_zrange;     /* In microns. */
    guint objective;
    guint shutter;
    gdouble zresolution;   /* In microns. */
} MicroProfBlock00a0;

/* WLI sensor 2. */
typedef struct {
    guint min_quality;
    gdouble focus;         /* In microns. */
} MicroProfBlock00a1;

/* PCL 1741. */
typedef struct {
    guint volt_range;
    guint val_channel;
    guint int_channel;
    guint int_range;            /* In bits. */
    gdouble min_valid_val;      /* In Volts. */
    gdouble max_valid_val;      /* In Volts. */
    gdouble min_valid_intens;   /* In Volts. */
    gdouble max_valid_intens;   /* In Volts. */
    guchar unit_list[8*16];
    guint selected_unit;
    gdouble min_valid_unit_value;
    gdouble max_valid_unit_value;
} MicroProfBlock00a2;

/* CMF sensor. */
typedef struct {
    guint cfm_objective;
    guint cfm_shutter;
    gdouble start_pos;                 /* In metres. */
    gdouble end_pos;                   /* In metres. */
    gdouble cfm_zresolution;           /* In metres. */
    gdouble lower_reflect_threshold;   /* In percents. */
    gdouble upper_reflect_threshold;   /* In percents. */
} MicroProfBlock00a3;

/* AFM SIS params. */
typedef struct {
    gdouble angle;                     /* In gradians. */
    gdouble I_zfb;
    gdouble P_zfb;
    gdouble retract_time;              /* In seconds. */
    gdouble xoffset;                   /* In metres. */
    gdouble yoffset;                   /* In metres. */
    gdouble zgain;                     /* In percents. */
} MicroProfBlock00a4;

/* CWL external timing. */
typedef struct {
    gboolean external_timing;
} MicroProfBlock00a5;

/* CFM sensor. */
typedef struct {
    /* XXX: Specs give objective_name name as variable, but do not list any
     * length field. */
    guchar *objective_name;
    guchar correction_file[256];       /* Specs says 260 bytes, but then the block would have to be 268 bytes long,
                                        * not 264. */
    gboolean show_measurement;
} MicroProfBlock00a6;

/* General sensor caps.
 * XXX: Fields not given in the specs.  Have seen too few of these for
 * statistics... */
typedef struct {
    guint len;
    guint int1;         /* 2. */
    guint int2;         /* 1. */
    guint int3;         /* 1. */
    guint int4;         /* 1. */
    guchar text[256];   /* Sample?  Software?  The same as in 0x008e. */
    gdouble float1;     /* 0.00066. */
    gdouble float2;     /* Equal to zscale. */
    guint int5;         /* Approx 10*32768.  Can be two 16bit integers. */
    gdouble float3;     /* Sometimes like 1.17552e-05, sometimes strange. */
    guchar zeros1[16];
    guint int6;         /* 1. */
    guint int7;         /* Zero. */
    gdouble float4;     /* 0.0065. */
    guint int8;         /* 1 */
    guint int9;         /* Zero or something very strange. */
    gdouble float5;     /* Small negative number of the order 10^{-5}. */
    guchar zeros2[24];
} MicroProfBlock368Sub00a7;

typedef struct {
    guint len;
    gdouble float1;     /* 1000.16 (equal to value in 0x0099). */
    gdouble float2;     /* 40 (equal to value in 0x0099). */
    guint zero1;
    guint int1;         /* 0 or 1. */
    guint int2;         /* 1 or 8. */
    guint zero2;
    gdouble float3;     /* 1. */
} MicroProfBlock36Sub00a7;

typedef struct {
    guint nsubblocks;   /* Appearently applies to both types of subblocks, which come alternatively.  So they may
                         * together be considered one subblock, execpt each also comes with its own size.  Confusing.
                         */
    MicroProfBlock368Sub00a7 *sub368;
    MicroProfBlock36Sub00a7 *sub36;
} MicroProfBlock00a7;

/* Roundness measurements.
 * XXX: Fields not given in the specs.  Have seen too few of these for statistics... */
typedef struct {
    gdouble zero1;
    gdouble float1;     /* Small signed number of the order 0.01 to 0.1. */
    gdouble float2;     /* Small signed number of the order 0.01 to 0.1. */
    gdouble float3;     /* Small signed number of the order 0.01 to 0.1. */
    gdouble float4;     /* Small signed number of the order 0.01 to 0.1. */
} MicroProfBlock00a8;

/* Reference axis subtraction. */
typedef struct {
    gboolean xaxis_subtracted;
    gboolean yaxis_subtracted;
} MicroProfBlock00a9;

/* WLIPL sensor. */
typedef struct {
    guchar sensor_ini_path[259];    /* 259 is given MAX_PATH in the specs. */
    gdouble start_pos;
    gdouble end_pos;
    gdouble zspeed;
    gdouble presampling_zlength;
    gdouble postsampling_zlength;
    guint pos_after_zscan;
    guint preprocessor;
    guint postprocessor;
} MicroProfBlock00aa;

/* Multibuffer. */
typedef struct {
    guint alias;
    gdouble scale;
    gdouble offset;
    gboolean absolute;
} MicroProfBlockSub00ab;

typedef struct {
    guint nsubblocks;
    MicroProfBlockSub00ab *subblocks;
} MicroProfBlock00ab;

/* User management. */
typedef struct {
    gchar *user_name;
    gchar *user_description;
} MicroProfBlock00ac;

/* User input. */
typedef struct {
    guchar *label;           /* Length should be given by textlen. */
    guint input_box_val;
    gchar *value;            /* Not clear if length is textlen or the other integer.  Do not try to read it until we
                              * see some examples. */
} MicroProfBlockSub00ad;

typedef struct {
    guint nsubblocks;
    MicroProfBlockSub00ad *subblocks;
} MicroProfBlock00ad;

/* FRT2 sensor. */
typedef struct {
    guint signal;
    guint filter;
    guint reference_type;
    guint layer_stack_id;
    gint reference_material_id;
    gdouble reference_constant;
    gdouble material_thickness;
} MicroProfBlock00ae;

/* CFMDT sensor. */
typedef struct {
    gboolean auto_focus;
    gboolean auto_brightness;
    gdouble focus_search_length;
    guint max_brightness;
    gboolean move_back_after_meas;
    gboolean move_back_below_scan_range;
} MicroProfBlock00af;

/* AAXT scan info. */
typedef struct {
    gboolean is_set;
    guchar *position_on_sample;
    guchar *aaxt_version;
    guchar *die_index;
    guchar *lot_id;
    guchar *recipe_name;
    guchar *wafer_id;
} MicroProfBlock00b0;

/* TODO: There are blocks defined at least up to 0x00bf.
 * I have never seen them in the wild at least present, let alone carrying useful info. */

/* File (single + multi). */
typedef struct {
    guint version;   /* 100 or 101 */
    gboolean *seen_blocks;
    guint int1;
    MicroProfBlock000b block000b;
    MicroProfBlock0065 block0065;
    MicroProfBlock0066 block0066;
    MicroProfBlock0067 block0067;
    MicroProfBlock0068 block0068;
    MicroProfBlock0069 block0069;
    MicroProfBlock006a block006a;
    MicroProfBlock006b block006b;
    MicroProfBlock006c block006c;
    MicroProfBlock006d block006d;
    MicroProfBlock006e block006e;
    MicroProfBlock006f block006f;
    MicroProfBlock0070 block0070;
    MicroProfBlock0071 block0071;
    MicroProfBlock0072 block0072;
    MicroProfBlock0073 block0073;
    MicroProfBlock0074 block0074;
    MicroProfBlock0075 block0075;
    MicroProfBlock0076 block0076;
    MicroProfBlock0077 block0077;
    MicroProfBlock0078 block0078;
    MicroProfBlock0079 block0079;
    MicroProfBlock007a block007a;
    MicroProfBlock007b block007b;
    MicroProfBlock007c block007c;
    MicroProfBlock007d block007d;
    MicroProfBlock007e block007e;
    MicroProfBlock007f block007f;
    MicroProfBlock0080 block0080;
    MicroProfBlock0081 block0081;
    MicroProfBlock0082 block0082;
    MicroProfBlock0083 block0083;
    MicroProfBlock0084 block0084;
    MicroProfBlock0085 block0085;
    MicroProfBlock0086 block0086;
    MicroProfBlock0087 block0087;
    MicroProfBlock0088 block0088;
    MicroProfBlock0089 block0089;
    MicroProfBlock008a block008a;
    MicroProfBlock008b block008b;
    MicroProfBlock008c block008c;
    MicroProfBlock008d block008d;
    MicroProfBlock008e block008e;
    MicroProfBlock008f block008f;
    MicroProfBlock0090 block0090;
    MicroProfBlock0091 block0091;
    MicroProfBlock0092 block0092;
    MicroProfBlock0093 block0093;
    MicroProfBlock0094 block0094;
    MicroProfBlock0095 block0095;
    MicroProfBlock0096 block0096;
    MicroProfBlock0097 block0097;
    MicroProfBlock0098 block0098;
    MicroProfBlock0099 block0099;
    MicroProfBlock009a block009a;
    MicroProfBlock009b block009b;
    MicroProfBlock009c block009c;
    MicroProfBlock009d block009d;
    MicroProfBlock009e block009e;
    MicroProfBlock009f block009f;
    MicroProfBlock00a0 block00a0;
    MicroProfBlock00a1 block00a1;
    MicroProfBlock00a2 block00a2;
    MicroProfBlock00a3 block00a3;
    MicroProfBlock00a4 block00a4;
    MicroProfBlock00a5 block00a5;
    MicroProfBlock00a6 block00a6;
    MicroProfBlock00a7 block00a7;
    MicroProfBlock00a8 block00a8;
    MicroProfBlock00a9 block00a9;
    MicroProfBlock00aa block00aa;
    MicroProfBlock00ab block00ab;
    MicroProfBlock00ac block00ac;
    MicroProfBlock00ad block00ad;
    MicroProfBlock00ae block00ae;
    MicroProfBlock00af block00af;
    MicroProfBlock00b0 block00b0;
} MicroProfFile;

static gboolean      module_register          (void);
static gint          microprof_detect         (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name);
static gint          microprof_txt_detect     (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name);
static guint         microprof_get_version    (const guchar *buffer,
                                               gsize size,
                                               GError **error);
static GwyContainer* microprof_load           (const gchar *filename,
                                               GwyRunType mode,
                                               GError **error);
static void          microprof_file_free      (MicroProfFile *mfile);
static gboolean      read_blocks              (const guchar *buffer,
                                               gsize size,
                                               MicroProfFile *mfile,
                                               GError **error);
static gboolean      read_images_block        (const guchar *p,
                                               gsize size,
                                               MicroProfBlock007d *block,
                                               GError **error);
static gboolean      check_imgblock           (const MicroProfImageBlock *imgblock,
                                               gsize size,
                                               GError **error);
static void          microprof_read_data_field(GwyContainer *container,
                                               gint id,
                                               const MicroProfImageBlock *imgblock,
                                               gdouble xrange,
                                               gdouble yrange,
                                               gdouble zscale,
                                               const guchar *buffer);
static GwyContainer* create_meta              (const MicroProfFile *mfile);
static GwyContainer* microprof_txt_load       (const gchar *filename,
                                               GwyRunType mode,
                                               GError **error);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports MicroProf FRT profilometer data files."),
    "Yeti <yeti@gwyddion.net>",
    "1.1",
    "David Nečas (Yeti) & Petr Klapetek",
    "2006",
};

GWY_MODULE_QUERY2(module_info, microprof)

static gboolean
module_register(void)
{
    gwy_file_func_register("microprof",
                           N_("MicroProf FRT files (.frt)"),
                           (GwyFileDetectFunc)&microprof_detect,
                           (GwyFileLoadFunc)&microprof_load,
                           NULL,
                           NULL);
    gwy_file_func_register("microprof_txt",
                           N_("MicroProf FRT text files (.txt)"),
                           (GwyFileDetectFunc)&microprof_txt_detect,
                           (GwyFileLoadFunc)&microprof_txt_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
microprof_detect(const GwyFileDetectInfo *fileinfo,
                 gboolean only_name)
{
    guint version;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION) ? 10 : 0;

    if (!(version = microprof_get_version(fileinfo->head, fileinfo->buffer_len, NULL)))
        return 0;
    if (version == 100 || version == 101)
        return 100;

    /* We recognise the file, but are unable to load it (most likely). */
    return 60;
}

static gint
microprof_txt_detect(const GwyFileDetectInfo *fileinfo,
                     gboolean only_name)
{
    GwyTextHeaderParser parser;
    GHashTable *meta;
    const guchar *p;
    gchar *buffer;
    gsize size;
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_TXT) ? 10 : 0;

    if (fileinfo->buffer_len < MICROPROF_MIN_TEXT_SIZE || memcmp(fileinfo->head, MAGIC_TXT, MAGIC_TXT_SIZE) != 0)
        return 0;

    if (!(p = strstr(fileinfo->head, "\n\n"))
        && !(p = strstr(fileinfo->head, "\r\r"))
        && !(p = strstr(fileinfo->head, "\r\n\r\n")))
        return 0;

    size = p - (const guchar*)fileinfo->head;
    buffer = g_memdup(fileinfo->head, size+1);
    buffer[size] = '\0';
    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    meta = gwy_text_header_parse(buffer, &parser, NULL, NULL);
    if (g_hash_table_lookup(meta, "XSize") && g_hash_table_lookup(meta, "YSize")
        && g_hash_table_lookup(meta, "XRange") && g_hash_table_lookup(meta, "YRange")
        && g_hash_table_lookup(meta, "ZScale"))
        score = 90;

    g_free(buffer);
    if (meta)
        g_hash_table_destroy(meta);

    return score;
}

static guint
microprof_get_version(const guchar *buffer, gsize size, GError **error)
{
    /* Handle error == NULL explicitly to avoid varions nested function calls when detecting. */
    if (size < MICROPROF_MIN_HEADER_SIZE) {
        if (error)
            err_TOO_SHORT(error);
        return 0;
    }
    if (memcmp(buffer, MAGIC_PREFIX, MAGIC_PREFIX_SIZE) != 0) {
        if (error)
            err_FILE_TYPE(error, "MicroProf");
        return 0;
    }
    buffer += MAGIC_PREFIX_SIZE;

    if (buffer[0] != '1' || buffer[1] != '.')
        return 0;
    if (!g_ascii_isdigit(buffer[2]) || !g_ascii_isdigit(buffer[3]))
        return 0;

    return 100 + 10*g_ascii_digit_value(buffer[2]) + g_ascii_digit_value(buffer[3]);
}

static GwyContainer*
microprof_load(const gchar *filename,
               G_GNUC_UNUSED GwyRunType mode,
               GError **error)
{
    GwyContainer *container = NULL, *meta = NULL;
    guchar *buffer = NULL;
    const guchar *p;
    gsize size = 0;
    MicroProfFile mfile;
    GError *err = NULL;
    GQuark quark;
    guint i;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&mfile, 1);
    mfile.version = microprof_get_version(buffer, size, error);
    if (!mfile.version)
        goto fail;
    if (mfile.version != 100 && mfile.version != 101) {
        err_FILE_TYPE(error, "MicroProf");
        goto fail;
    }

    mfile.seen_blocks = g_new0(gboolean, MAX_BLOCK_ID);
    p = buffer + MAGIC_PREFIX_SIZE + MAGIC_VER_SIZE;
    mfile.int1 = gwy_get_guint16_le(&p);
    if (!read_blocks(p, size - (p - buffer), &mfile, error))
        goto fail;

    sanitise_real_size(&mfile.block0067.xrange, "x size");
    sanitise_real_size(&mfile.block0067.yrange, "y size");

    container = gwy_container_new();
    meta = create_meta(&mfile);

    /* Create images.  If a multi-image part is present we throw away the single image because it is duplicated in the
     * multi-image part. */
    if (mfile.block007d.nimages) {
        for (i = 0; i < mfile.block007d.nimages; i++) {
            MicroProfImageBlock *imgblock = mfile.block007d.imgblocks + i;
            GwyContainer *tmpmeta;

            microprof_read_data_field(container, i, imgblock,
                                      mfile.block0067.xrange, mfile.block0067.yrange, mfile.block006c.zscale,
                                      mfile.block007d.imgblocks[i].data);
            quark = gwy_app_get_data_meta_key_for_id(i);
            tmpmeta = gwy_container_duplicate(meta);
            gwy_container_pass_object(container, quark, tmpmeta);
            gwy_file_channel_import_log_add(container, i, NULL, filename);
        }
    }
    else if (mfile.block000b.data) {
        MicroProfImageBlock imgblock;

        imgblock.datatype = MICROPROF_TOPOGRAPHY;
        imgblock.xres = mfile.block0066.xres;
        imgblock.yres = mfile.block0066.yres;
        imgblock.bpp = mfile.block0066.bpp;
        if (!check_imgblock(&imgblock, mfile.block000b.size, error))
            goto fail;

        microprof_read_data_field(container, 0, &imgblock,
                                  mfile.block0067.xrange, mfile.block0067.yrange, mfile.block006c.zscale,
                                  mfile.block000b.data);
        quark = gwy_app_get_data_meta_key_for_id(0);
        gwy_container_set_object(container, quark, meta);
        gwy_file_channel_import_log_add(container, 0, NULL, filename);
    }
    else {
        err_NO_DATA(error);
        goto fail;
    }

fail:
    GWY_OBJECT_UNREF(meta);
    gwy_file_abandon_contents(buffer, size, NULL);
    microprof_file_free(&mfile);
    return container;
}

static void
microprof_file_free(MicroProfFile *mfile)
{
    g_free(mfile->seen_blocks);
    g_free(mfile->block007d.imgblocks);
    g_free(mfile->block0065.text);
    g_free(mfile->block0077.parset_name);
    g_free(mfile->block008e.ser_num);
    g_free(mfile->block0091.subblocks);
    g_free(mfile->block0094.name_of_teach);
    g_free(mfile->block009f.zunit);
    g_free(mfile->block00a6.objective_name);
    g_free(mfile->block00a7.sub368);
    g_free(mfile->block00a7.sub36);
    g_free(mfile->block00ab.subblocks);
    g_free(mfile->block00ac.user_name);
    g_free(mfile->block00ac.user_description);
    /* FIXME: block00ad also can have dynamically allocated data, but we do not read it at this moment. */
    g_free(mfile->block00b0.position_on_sample);
    g_free(mfile->block00b0.aaxt_version);
    g_free(mfile->block00b0.die_index);
    g_free(mfile->block00b0.lot_id);
    g_free(mfile->block00b0.recipe_name);
    g_free(mfile->block00b0.wafer_id);
}

#ifdef DEBUG
static const gchar*
format_hexdump(GString *str, const guchar *p, guint len)
{
    g_string_assign(str, "data");

    while (len--) {
        g_string_append_printf(str, " %02x", *p);
        p++;
    }

    return str->str;
}
#endif

#define VS G_MAXUINT

static gboolean
read_blocks(const guchar *p, gsize size,
            MicroProfFile *mfile, GError **error)
{
    enum { BLOCK_HEADER_SIZE = 2 + 4 };

    static const guint block_sizes[MAX_BLOCK_ID] = {
    /* 0   1    2   3   4    5    6    7    8   9    a   b   c   d   e    f*/
/*0*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0, VS,  0,  0,  0,   0,
/*1*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*2*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*3*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*4*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*5*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*6*/  0,  0,   0,  0,  0,  VS,  12,  44,  36, 16,  32, 16, 12, 32, 12,  16,
/*7*/  8,  8,  12,  4,  8,  VS,  32,  VS,  VS, 16,   8,  8,  4, VS, 16, 256,
/*8*/  4, 20,   4,  4,  4, 188,   4,   4, 196,  8,  12,  4, 22, 60, VS,   4,
/*9*/ 48, VS, 264, 12, VS,  16,  20,  20,   8, 16,  44, 32,  8,  4, 12,  VS,
/*a*/ 22, 10, 196, 44, 56,   4,  VS,  VS,  40,  8, 311, VS, VS, VS, 36,  28,
/*b*/ VS,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*c*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*d*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*e*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
/*f*/  0,  0,   0,  0,  0,   0,   0,   0,   0,  0,   0,  0,  0,  0,  0,   0,
    };

    GString *str = g_string_new(NULL);
    gboolean ok = FALSE;

    while (size >= BLOCK_HEADER_SIZE) {
        guint blocktype, ii;
        gsize blocksize, textlen, textlensum;
        gboolean skipme = TRUE;
        const guchar *q;

        blocktype = gwy_get_guint16_le(&p);
        if (mfile->version == 100)
            blocksize = gwy_get_guint32_le(&p);
        else
            blocksize = gwy_get_guint64_le(&p);

        q = p;
        size -= BLOCK_HEADER_SIZE;

        gwy_debug("block of type 0x%04lx and size %lu", (gulong)blocktype, (gulong)blocksize);
        if (blocksize > size) {
            gwy_debug("too long block, only %lu bytes remaining", (gulong)size);
            goto fail;
        }

#if 0
        gwy_debug("[%04x] rawdata %s", blocktype, format_hexdump(str, q, MIN(blocksize, 4096)));
#endif

        if (blocktype >= MAX_BLOCK_ID) {
            g_warning("Too large block id %02x", blocktype);
        }
        else {
            guint expected_size = block_sizes[blocktype];
            if (expected_size) {
                if (expected_size == VS || blocksize == expected_size)
                    skipme = FALSE;
                else {
                    g_warning("Wrong block %02x length %lu (expecting %lu)",
                              blocktype, (gulong)blocksize, (gulong)expected_size);
                }
            }
        }

        if (skipme) {
            gwy_debug("unhandled block %s", format_hexdump(str, p, blocksize));
            p += blocksize;
            size -= blocksize;
            continue;
        }

        if (mfile->seen_blocks[blocktype]) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Duplicate block %02x."), blocktype);
            goto fail;
        }
        mfile->seen_blocks[blocktype] = TRUE;

        if (blocktype == 0x000b) {
            MicroProfBlock000b *block = &mfile->block000b;
            block->size = blocksize;
            block->data = q;
        }
        else if (blocktype == 0x0065 && blocksize > 0) {
            MicroProfBlock0065 *block = &mfile->block0065;
            block->text = g_strndup(q, blocksize);
            gwy_debug("[%04x] text \"%s\"", blocktype, block->text);
#if 0
            gwy_debug("[%04x] text \"%s\"",
                      blocktype,
                      gwy_strreplace(block->text, "\n", " ", (gsize)-1));
#endif
        }
        else if (blocktype == 0x0066) {
            MicroProfBlock0066 *block = &mfile->block0066;
            block->xres = gwy_get_guint32_le(&q);
            block->yres = gwy_get_guint32_le(&q);
            block->bpp = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] xres %u, yres %u, bpp %u",
                      blocktype, block->xres, block->yres, block->bpp);
        }
        else if (blocktype == 0x0067) {
            MicroProfBlock0067 *block = &mfile->block0067;
            block->xrange = gwy_get_gdouble_le(&q);
            block->yrange = gwy_get_gdouble_le(&q);
            block->xoffset = gwy_get_gdouble_le(&q);
            block->yoffset = gwy_get_gdouble_le(&q);
            block->factor_range_y = gwy_get_gdouble_le(&q);
            block->scandir = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] xrange %g, yrange %g, xoffset %g, yoffset %g, factor_range_y %g, scandir %u",
                      blocktype,
                      block->xrange, block->yrange, block->xoffset, block->yoffset,
                      block->factor_range_y, block->scandir);
        }
        else if (blocktype == 0x0068) {
            MicroProfBlock0068 *block = &mfile->block0068;
            block->xspeed = gwy_get_gdouble_le(&q);
            block->yspeed = gwy_get_gdouble_le(&q);
            block->override_speed = gwy_get_guint32_le(&q);
            block->check_sensor_error = gwy_get_guint32_le(&q);
            block->scan_back_meas = gwy_get_guint32_le(&q);
            block->sensor_delay = gwy_get_guint32_le(&q);
            block->sensor_error_time = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] xspeed %g, yspeed %g, "
                      "override_speed %u, "
                      "check_sensor_error %u, "
                      "scan_back_meas %u, "
                      "sensor_delay %u, sensor_error_time %u",
                      blocktype,
                      block->xspeed, block->yspeed,
                      block->override_speed,
                      block->check_sensor_error,
                      block->scan_back_meas,
                      block->sensor_delay, block->sensor_error_time);
        }
        else if (blocktype == 0x0069) {
            MicroProfBlock0069 *block = &mfile->block0069;
            block->range_unit_type = gwy_get_guint32_le(&q);
            block->offset_unit_type = gwy_get_guint32_le(&q);
            block->xspeed_unit_type = gwy_get_guint32_le(&q);
            block->yspeed_unit_type = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] range_unit_type %d, offset_unit_type %d, xspeed_unit_type %d, yspeed_unit_type %d",
                      blocktype,
                      block->range_unit_type, block->offset_unit_type,
                      block->xspeed_unit_type, block->yspeed_unit_type);
        }
        else if (blocktype == 0x006a) {
            MicroProfBlock006a *block = &mfile->block006a;
            block->step_xcount = gwy_get_guint32_le(&q);
            block->step_ycount = gwy_get_guint32_le(&q);
            block->xstep = gwy_get_gdouble_le(&q);
            block->ystep = gwy_get_gdouble_le(&q);
            block->step_delay = gwy_get_guint32_le(&q);
            block->back_scan_step = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] step_xcount %u, step_ycount %u, xstep %g, ystep %g, step_delay %u, back_scan_step %d",
                      blocktype,
                      block->step_xcount, block->step_ycount,
                      block->xstep, block->ystep,
                      block->step_delay, block->back_scan_step);
        }
        else if (blocktype == 0x006b) {
            MicroProfBlock006b *block = &mfile->block006b;
            block->wait_at_start_of_line = gwy_get_guint32_le(&q);
            block->display_start_box = gwy_get_guint32_le(&q);
            block->do_hysteresis_corr = gwy_get_guint32_le(&q);
            block->back_scan_delay = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] wait_at_start_of_line %u, display_start_box %u, "
                      "do_hysteresis_corr %u, back_scan_delay %d",
                      blocktype,
                      block->wait_at_start_of_line,
                      block->display_start_box,
                      block->do_hysteresis_corr,
                      block->back_scan_delay);
        }
        else if (blocktype == 0x006c) {
            MicroProfBlock006c *block = &mfile->block006c;
            block->meas_range = gwy_get_guint32_le(&q);
            block->zscale = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] meas_range %u, zscale %g",
                      blocktype, block->meas_range, block->zscale);
        }
        else if (blocktype == 0x006d) {
            MicroProfBlock006d *block = &mfile->block006d;
            block->zrange = gwy_get_gdouble_le(&q);
            block->use_percentage = gwy_get_gdouble_le(&q);
            block->display_correction = gwy_get_guint32_le(&q);
            block->palette_type = gwy_get_guint32_le(&q);
            block->display_size = gwy_get_guint32_le(&q);
            block->autorange = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] zrange %g, use_percentage %g, "
                      "display_correction %u, palette_type %u, "
                      "display_size %u, autorange %d",
                      blocktype,
                      block->zrange, block->use_percentage,
                      block->display_correction, block->palette_type,
                      block->display_size, block->autorange);
        }
        else if (blocktype == 0x006e) {
            MicroProfBlock006e *block = &mfile->block006e;
            block->sensor_type = gwy_get_guint32_le(&q);
            block->xytable_type = gwy_get_guint32_le(&q);
            block->ztable_type = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] sensor_type %u, xytable_type %u, ztable_type %u",
                      blocktype,
                      block->sensor_type, block->xytable_type, block->ztable_type);
        }
        else if (blocktype == 0x006f) {
            MicroProfBlock006f *block = &mfile->block006f;
            block->do_integrate = gwy_get_guint32_le(&q);
            block->integrate_over = gwy_get_guint32_le(&q);
            block->sensor_was_piezo = gwy_get_guint32_le(&q);
            block->sensor_was_full = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] do_integrate %d, integrate_over %u, "
                      "sensor_was_piezo %d, sensor_was_full %d",
                      blocktype,
                      block->do_integrate, block->integrate_over,
                      block->sensor_was_piezo, block->sensor_was_full);
        }
        else if (blocktype == 0x0070) {
            MicroProfBlock0070 *block = &mfile->block0070;
            block->first_valid = gwy_get_guint32_le(&q);
            block->last_valid = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] first_valid %u, last_valid %u", blocktype, block->first_valid, block->last_valid);
        }
        else if (blocktype == 0x0071) {
            MicroProfBlock0071 *block = &mfile->block0071;
            block->zoffset = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] zoffset %g", blocktype, block->zoffset);
        }
        else if (blocktype == 0x0072) {
            MicroProfBlock0072 *block = &mfile->block0072;
            block->meas_started = gwy_get_guint32_le(&q);
            block->meas_ended = gwy_get_guint32_le(&q);
            block->meas_time = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] meas_started %u, meas_ended %u, meas_time %u",
                      blocktype, block->meas_started, block->meas_ended, block->meas_time);
        }
        else if (blocktype == 0x0073) {
            MicroProfBlock0073 *block = &mfile->block0073;
            block->dio_type = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] dio_type %u", blocktype, block->dio_type);
        }
        else if (blocktype == 0x0074) {
            MicroProfBlock0074 *block = &mfile->block0074;
            block->dllver1 = gwy_get_guint32_le(&q);
            block->dllver2 = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] dllver1 %u, dllver2 %u", blocktype, block->dllver1, block->dllver2);
        }
        else if (blocktype == 0x0075 && blocksize >= 20) {
            MicroProfBlock0075 *block = &mfile->block0075;
            block->nvalues = gwy_get_guint32_le(&q);
            block->is_applied = gwy_get_guint32_le(&q);
            block->do_drift_corr_scan = gwy_get_guint32_le(&q);
            block->data_available = gwy_get_guint32_le(&q);
            block->line_not_row = gwy_get_guint32_le(&q);
            /* XXX: We do not use this, but it must be checked if we want to
             * use it. */
            block->data = q;
            gwy_debug("[%04x] nvalues %u, "
                      "is_applied %d, do_drift_corr_scan %d, "
                      "data_available %d, line_not_row %d, "
                      "remainder %lu bytes (expecting %lu)",
                      blocktype,
                      block->nvalues,
                      block->is_applied, block->do_drift_corr_scan,
                      block->data_available, block->line_not_row,
                      (gulong)(blocksize - 20), (gulong)(4*block->nvalues));
        }
        else if (blocktype == 0x0076) {
            MicroProfBlock0076 *block = &mfile->block0076;
            block->xstart = gwy_get_gdouble_le(&q);
            block->ystart = gwy_get_gdouble_le(&q);
            block->xend = gwy_get_gdouble_le(&q);
            block->yend = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] xstart %g, ystart %g, xend %g, yend %g",
                      blocktype, block->xstart, block->ystart, block->xend, block->yend);
        }
        else if (blocktype == 0x0077 && blocksize > 0) {
            MicroProfBlock0077 *block = &mfile->block0077;
            block->parset_name = g_strndup(q, blocksize);
            gwy_debug("[%04x] parset_name \"%s\"", blocktype, block->parset_name);
        }
        else if (blocktype == 0x0078 && blocksize >= 4) {
            MicroProfBlock0078 *block = &mfile->block0078;
            block->data_are_valid = gwy_get_guint32_le(&q);
            /* XXX: We do not use this, but it must be checked if we want to
             * use it. */
            block->data = q;
            gwy_debug("[%04x] data_are_valid %d, remainder %lu bytes (expecting %lu)",
                      blocktype,
                      block->data_are_valid, (gulong)(blocksize - 4), (gulong)(8*mfile->block0066.xres));
        }
        else if (blocktype == 0x0079) {
            MicroProfBlock0079 *block = &mfile->block0079;
            block->xdispoffset = gwy_get_gdouble_le(&q);
            block->ydispoffset = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] xdispoffset %g, ydispoffset %g", blocktype, block->xdispoffset, block->ydispoffset);
        }
        else if (blocktype == 0x007a) {
            MicroProfBlock007a *block = &mfile->block007a;
            block->meas_rate = gwy_get_guint32_le(&q);
            block->min_intensity = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] meas_rate %u, min_intensity %u", blocktype, block->meas_rate, block->min_intensity);
        }
        else if (blocktype == 0x007b) {
            MicroProfBlock007b *block = &mfile->block007b;
            block->sensor_subtype = gwy_get_guint32_le(&q);
            block->xytable_subtype = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] sensor_subtype %u, xytable_subtype %u",
                      blocktype, block->sensor_subtype, block->xytable_subtype);
        }
        else if (blocktype == 0x007c) {
            MicroProfBlock007c *block = &mfile->block007c;
            block->speed_control = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] speed_control %d", blocktype, block->speed_control);
        }
        else if (blocktype == 0x007d) {
            MicroProfBlock007d *block = &mfile->block007d;
            if (!read_images_block(p, blocksize, block, error))
                goto fail;
        }
        else if (blocktype == 0x007e) {
            MicroProfBlock007e *block = &mfile->block007e;
            block->max_xrange = gwy_get_gdouble_le(&q);
            block->max_yrange = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] max_xrange %g, max_yrange %g", blocktype, block->max_xrange, block->max_yrange);
        }
        else if (blocktype == 0x007f) {
            MicroProfBlock007f *block = &mfile->block007f;
            get_CHARARRAY0(block->calibration, &q);
            block->is_calibrated = *(q++);
            gwy_debug("[%04x] calibration \"%.255s\", is_calibrated %d",
                      blocktype, block->calibration, block->is_calibrated);
        }
        else if (blocktype == 0x0080) {
            MicroProfBlock0080 *block = &mfile->block0080;
            block->is_z_motor_ctrl_on = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] is_z_motor_ctrl_on %u", blocktype, block->is_z_motor_ctrl_on);
        }
        else if (blocktype == 0x0081) {
            MicroProfBlock0081 *block = &mfile->block0081;
            block->nlayers = gwy_get_guint32_le(&q);
            block->range1 = gwy_get_gdouble_le(&q);
            block->range_rest = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] nlayers %u, range1 %g, range_rest %g",
                      blocktype, block->nlayers, block->range1, block->range_rest);
        }
        else if (blocktype == 0x0082) {
            MicroProfBlock0082 *block = &mfile->block0082;
            block->motion_type = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] motion_type %u", blocktype, block->motion_type);
        }
        else if (blocktype == 0x0083) {
            MicroProfBlock0083 *block = &mfile->block0083;
            block->data_type = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] data_type %u", blocktype, block->data_type);
        }
        else if (blocktype == 0x0084) {
            MicroProfBlock0084 *block = &mfile->block0084;
            block->use_std_schichthohe = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] use_std_schichthohe %u",
                      blocktype, block->use_std_schichthohe);
        }
        else if (blocktype == 0x0085) {
            MicroProfBlock0085 *block = &mfile->block0085;
            block->volt_range = gwy_get_guint32_le(&q);
            block->val_channel = gwy_get_guint32_le(&q);
            block->int_channel = gwy_get_guint32_le(&q);
            block->val_range = gwy_get_gdouble_le(&q);
            block->int_range = gwy_get_guint32_le(&q);
            block->min_valid_val = gwy_get_gdouble_le(&q);
            block->max_valid_val = gwy_get_gdouble_le(&q);
            block->min_valid_intens = gwy_get_gdouble_le(&q);
            block->max_valid_intens = gwy_get_gdouble_le(&q);
            get_CHARARRAY(block->unit_list, &q);
            block->selected_unit = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] volt_range %u, val_channel %u, int_channel %u, "
                      "val_range %g, int_range %d, "
                      "min_valid_val %g, max_valid_val %g, "
                      "min_valid_intens %g, max_valid_intens %g, "
                      "selected_unit %u",
                      blocktype,
                      block->volt_range, block->val_channel, block->int_channel,
                      block->val_range, block->int_range,
                      block->min_valid_val, block->max_valid_val,
                      block->min_valid_intens, block->max_valid_intens,
                      block->selected_unit);
        }
        else if (blocktype == 0x0086) {
            MicroProfBlock0086 *block = &mfile->block0086;
            block->product_id = gwy_get_guint16_le(&q);
            block->series_no = gwy_get_guint16_le(&q);
            gwy_debug("[%04x] product_id %u, series_no %u", blocktype, block->product_id, block->series_no);
        }
        else if (blocktype == 0x0087) {
            MicroProfBlock0087 *block = &mfile->block0087;
            block->use_frt_offset = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] use_frt_offset %d", blocktype, block->use_frt_offset);
        }
        else if (blocktype == 0x0088) {
            MicroProfBlock0088 *block = &mfile->block0088;
            block->volt_range = gwy_get_guint32_le(&q);
            block->val_channel = gwy_get_guint32_le(&q);
            block->int_channel = gwy_get_guint32_le(&q);
            block->int_range = gwy_get_guint32_le(&q);
            block->min_valid_val = gwy_get_gdouble_le(&q);
            block->max_valid_val = gwy_get_gdouble_le(&q);
            block->min_valid_intens = gwy_get_gdouble_le(&q);
            block->max_valid_intens = gwy_get_gdouble_le(&q);
            get_CHARARRAY(block->unit_list, &q);
            block->selected_unit = gwy_get_guint32_le(&q);
            block->min_valid_unit_value = gwy_get_gdouble_le(&q);
            block->max_valid_unit_value = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] volt_range %u, val_channel %u, int_channel %u, "
                      "int_range %d, "
                      "min_valid_val %g, max_valid_val %g, "
                      "min_valid_intens %g, max_valid_intens %g, "
                      "selected_unit %u, "
                      "min_valid_unit_value %g, max_valid_unit_value %g",
                      blocktype,
                      block->volt_range, block->val_channel, block->int_channel,
                      block->int_range,
                      block->min_valid_val, block->max_valid_val,
                      block->min_valid_intens, block->max_valid_intens,
                      block->selected_unit,
                      block->min_valid_unit_value, block->max_valid_unit_value);
        }
        else if (blocktype == 0x0089) {
            MicroProfBlock0089 *block = &mfile->block0089;
            block->auto_approach = gwy_get_guint32_le(&q);
            block->auto_retract = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] auto_approach %d, auto_retract %d",
                      blocktype, block->auto_approach, block->auto_retract);
        }
        else if (blocktype == 0x008a) {
            MicroProfBlock008a *block = &mfile->block008a;
            block->zmotor_drive_allowed = gwy_get_guint32_le(&q);
            block->zmotor_drive_way = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] zmotor_drive_way %d, zmotor_drive_way %g",
                      blocktype, block->zmotor_drive_allowed, block->zmotor_drive_way);
        }
        else if (blocktype == 0x008b) {
            MicroProfBlock008b *block = &mfile->block008b;
            block->do_wait = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] do_wait %u", blocktype, block->do_wait);
        }
        else if (blocktype == 0x008c) {
            MicroProfBlock008c *block = &mfile->block008c;
            block->tv_range = gwy_get_gdouble_le(&q);
            block->tv_offset = gwy_get_gdouble_le(&q);
            block->set_tv_offset = *(q++);
            block->set_tv_automatic = *(q++);
            block->tv_range_percent = gwy_get_gfloat_le(&q);
            gwy_debug("[%04x] tv_range %g, tv_offset %g, "
                      "set_tv_offset %u, set_tv_automatic %u, "
                      "tv_range_percent %g",
                      blocktype, block->tv_range, block->tv_offset,
                      block->set_tv_offset, block->set_tv_automatic,
                      block->tv_range_percent);
        }
        else if (blocktype == 0x008d) {
            MicroProfBlock008d *block = &mfile->block008d;
            block->meas_mode = gwy_get_guint32_le(&q);
            block->height_edit = gwy_get_gdouble_le(&q);
            block->topo_edit = gwy_get_gdouble_le(&q);
            block->pref_mode = gwy_get_guint32_le(&q);
            block->freq_edit = gwy_get_gdouble_le(&q);
            block->hf_edit = gwy_get_guint32_le(&q);
            block->nf_edit = gwy_get_guint32_le(&q);
            block->phase_edit = gwy_get_gdouble_le(&q);
            block->nf_mode = gwy_get_guint32_le(&q);
            block->topo_scale = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] meas_mode %u, height_edit %g, topo_edit %g, "
                      "pref_mode %u, freq_edit %g, "
                      "hf_edit %u, nf_edit %u, phase_edit %g, "
                      "nf_mode %u, topo_scale %g",
                      blocktype,
                      block->meas_mode, block->height_edit, block->topo_edit,
                      block->pref_mode, block->freq_edit,
                      block->hf_edit, block->nf_edit, block->phase_edit,
                      block->nf_mode, block->topo_scale);
        }
        else if (blocktype == 0x008e && blocksize >= 16) {
            MicroProfBlock008e *block = &mfile->block008e;
            textlen = gwy_get_guint32_le(&q);
            if (textlen == blocksize - 16) {
                block->ser_num = g_strndup(q, textlen);
                q += textlen;
                block->day = *(q++);
                block->month = *(q++);
                block->year = gwy_get_guint16_le(&q);
                block->was_created = gwy_get_guint32_le(&q);
                block->nvalues = gwy_get_guint32_le(&q);
                /* XXX: We do not use this, but it must be checked if we want
                 * to use it. */
                block->data = q;
                gwy_debug("[%04x] ser_num \"%s\", "
                          "year-month-day %u-%u-%u, "
                          "was_created %d, nvalues %u",
                          blocktype, block->ser_num,
                          block->year, block->month, block->day,
                          block->was_created, block->nvalues);
            }
        }
        else if (blocktype == 0x008f) {
            MicroProfBlock008f *block = &mfile->block008f;
            block->tracking_mode_activated = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] tracking_mode_activated %d", blocktype, block->tracking_mode_activated);
        }
        else if (blocktype == 0x0090) {
            MicroProfBlock0090 *block = &mfile->block0090;
            block->despike_do_it = gwy_get_guint32_le(&q);
            block->despike_threshold = gwy_get_gdouble_le(&q);
            block->filter_meas_do_it = gwy_get_guint32_le(&q);
            block->filter_meas_type = gwy_get_guint32_le(&q);
            block->filter_meas_param = gwy_get_gdouble_le(&q);
            block->tip_simul_do_it = gwy_get_guint32_le(&q);
            block->tip_simul_angle = gwy_get_gdouble_le(&q);
            block->tip_simul_radius = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] despike_do_it %d, despike_threshold %g, "
                      "filter_meas_do_it %d, "
                      "filter_meas_type %u, filter_meas_param %g, "
                      "tip_simul_do_it %d, "
                      "tip_simul_angle %g, tip_simul_radius %g",
                      blocktype,
                      block->despike_do_it, block->despike_threshold,
                      block->filter_meas_do_it,
                      block->filter_meas_type, block->filter_meas_param,
                      block->tip_simul_do_it,
                      block->tip_simul_angle, block->tip_simul_radius);
        }
        else if (blocktype == 0x0091 && blocksize >= 36) {
            MicroProfBlock0091 *block = &mfile->block0091;
            block->topography = gwy_get_guint32_le(&q);
            block->differential = gwy_get_guint32_le(&q);
            block->topo_edit = gwy_get_gdouble_le(&q);
            block->height_edit = gwy_get_gdouble_le(&q);
            block->topo_scale = gwy_get_gdouble_le(&q);
            block->nsubblocks = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] topography %d, differential %d, "
                      "topo_edit %g, height_edit %g, topo_scale %g, "
                      "nsubblocks %u",
                      blocktype, block->topography, block->differential,
                      block->topo_edit, block->height_edit, block->topo_scale,
                      block->nsubblocks);
            if ((4 + 6*4)*block->nsubblocks == blocksize - 36) {
                block->subblocks = g_new(MicroProfBlockSub0091,
                                         block->nsubblocks);
                for (ii = 0; ii < block->nsubblocks; ii++) {
                    MicroProfBlockSub0091 *sub = block->subblocks + ii;
                    sub->active = gwy_get_guint32_le(&q);
                    sub->frequency = gwy_get_gfloat_le(&q);
                    sub->ac_dB = gwy_get_gfloat_le(&q);
                    sub->low_pass = gwy_get_gfloat_le(&q);
                    sub->high_pass = gwy_get_gfloat_le(&q);
                    sub->out_gain = gwy_get_gfloat_le(&q);
                    sub->pre_gain = gwy_get_gfloat_le(&q);
                    gwy_debug("[%04x:%u] active %u, "
                              "frequency %g, ac_dB %g, low_pass %g, "
                              "high_pass %g, out_gain %g, pre_gain %g",
                              blocktype, ii, sub->active,
                              sub->frequency, sub->ac_dB, sub->low_pass,
                              sub->high_pass, sub->out_gain, sub->pre_gain);
                }
            }
        }
        else if (blocktype == 0x0092
                 && (blocksize == 260 || blocksize == 264)) {
            /* The block has two different sizes. */
            MicroProfBlock0092 *block = &mfile->block0092;
            memcpy(block->data, q, blocksize);
            q += blocksize;
        }
        else if (blocktype == 0x0093) {
            MicroProfBlock0093 *block = &mfile->block0093;
            block->invalid_values = gwy_get_guint32_le(&q);
            block->lower_values = gwy_get_guint32_le(&q);
            block->upper_values = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] invalid_values %u, lower_values %u, upper_values %d",
                      blocktype, block->invalid_values, block->lower_values, block->upper_values);
        }
        else if (blocktype == 0x0094 && blocksize >= 24) {
            MicroProfBlock0094 *block = &mfile->block0094;
            block->min_teach = gwy_get_gdouble_le(&q);
            block->max_teach = gwy_get_gdouble_le(&q);
            block->min_norm_teach = gwy_get_guint32_le(&q);
            block->max_norm_teach = gwy_get_guint32_le(&q);
            textlen = gwy_get_guint32_le(&q);
            if (textlen == blocksize - 24) {
                block->name_of_teach = g_strndup(q, textlen);
                q += textlen;
            }
            block->scale_teach = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] min_teach %g, max_teach %g, "
                      "min_norm_teach %u, max_norm_teach %u, "
                      "name_of_teach \"%s\", scale_teach %u",
                      blocktype,
                      block->min_teach, block->max_teach,
                      block->min_norm_teach, block->max_norm_teach,
                      block->name_of_teach, block->scale_teach);
        }
        else if (blocktype == 0x0095) {
            MicroProfBlock0095 *block = &mfile->block0095;
            block->thickness_mode = gwy_get_guint32_le(&q);
            block->kind_of_thickness = gwy_get_guint32_le(&q);
            block->refractive_index = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] thickness_mode %d, kind_of_thickness %u, refractive_index %g",
                      blocktype, block->thickness_mode, block->kind_of_thickness, block->refractive_index);
        }
        else if (blocktype == 0x0096) {
            MicroProfBlock0096 *block = &mfile->block0096;
            block->thickness_lints_on = gwy_get_guint32_le(&q);
            block->low_limit = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] thickness_lints_on %d, low_limit %g, high_limit %g",
                      blocktype, block->thickness_lints_on, block->low_limit, block->high_limit);
        }
        else if (blocktype == 0x0097) {
            MicroProfBlock0097 *block = &mfile->block0097;
            block->laser_power = gwy_get_guint32_le(&q);
            block->laser_power_fine = gwy_get_guint32_le(&q);
            block->laser_frequency = gwy_get_guint32_le(&q);
            block->intensity = gwy_get_guint32_le(&q);
            block->min_valid_intens = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] laser_power %u, laser_power_fine %u, "
                      "laser_frequency %u, "
                      "intensity %u, min_valid_intens %u",
                      blocktype, block->laser_power, block->laser_power_fine,
                      block->laser_frequency,
                      block->intensity, block->min_valid_intens);
        }
        else if (blocktype == 0x0098) {
            MicroProfBlock0098 *block = &mfile->block0098;
            block->meas_z_position = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] meas_z_position %g", blocktype, block->meas_z_position);
        }
        else if (blocktype == 0x0099) {
            MicroProfBlock0099 *block = &mfile->block0099;
            block->is_dual_scan = gwy_get_guint32_le(&q);
            block->scan_frequency = gwy_get_gdouble_le(&q);
            block->duty = gwy_get_gfloat_le(&q);
            gwy_debug("[%04x] is_dual_scan %d, scan_frequency %g, duty %g",
                      blocktype, block->is_dual_scan, block->scan_frequency, block->duty);
        }
        else if (blocktype == 0x009a) {
            MicroProfBlock009a *block = &mfile->block009a;
            block->is_ttv = gwy_get_guint32_le(&q);
            block->meas_rate2 = gwy_get_guint32_le(&q);
            block->intensity2 = gwy_get_guint32_le(&q);
            block->zoffsets1 = gwy_get_gdouble_le(&q);
            block->zoffsets2 = gwy_get_gdouble_le(&q);
            block->scale1 = gwy_get_gdouble_le(&q);
            block->scale2 = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] is_ttv %d, meas_rate2 %u, intensity2 %u, "
                      "zoffsets1 %g, zoffsets2 %g, scale1 %g, scale2 %g",
                      blocktype,
                      block->is_ttv, block->meas_rate2, block->intensity2,
                      block->zoffsets1, block->zoffsets2,
                      block->zoffsets2, block->scale2);
        }
        else if (blocktype == 0x009b) {
            MicroProfBlock009b *block = &mfile->block009b;
            block->is_roundness = gwy_get_guint32_le(&q);
            block->is_sample_used = gwy_get_guint32_le(&q);
            block->radius = gwy_get_gdouble_le(&q);
            block->max_xrange = gwy_get_gdouble_le(&q);
            block->max_yrange = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] is_roundness %d, is_sample_used %d, radius %g, max_xrange %g, max_yrange %g",
                      blocktype, block->is_roundness, block->is_sample_used,
                      block->radius, block->max_xrange, block->max_yrange);
        }
        else if (blocktype == 0x009c) {
            MicroProfBlock009c *block = &mfile->block009c;
            block->do_despike = gwy_get_guint32_le(&q);
            block->do_interpolate = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] do_despike %u, do_interpolate %u", blocktype, block->do_despike, block->do_interpolate);
        }
        else if (blocktype == 0x009d) {
            MicroProfBlock009d *block = &mfile->block009d;
            block->subtract_sinus = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] subtract_sinus %u", blocktype, block->subtract_sinus);
        }
        else if (blocktype == 0x009e) {
            MicroProfBlock009e *block = &mfile->block009e;
            block->layer_info = gwy_get_guint32_le(&q);
            block->fit_threshold = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] layer_info %u, fit_threshold %g", blocktype, block->layer_info, block->fit_threshold);
        }
        else if (blocktype == 0x009f && blocksize >= 2) {
            MicroProfBlock009f *block = &mfile->block009f;
            textlen = gwy_get_guint16_le(&q);
            if (textlen == blocksize - 2) {
                block->zunit = g_strndup(q, textlen);
                gwy_debug("[%04x] zunit \"%s\"", blocktype, block->zunit);
            }
        }
        else if (blocktype == 0x00a0) {
            MicroProfBlock00a0 *block = &mfile->block00a0;
            block->brightness = gwy_get_guint16_le(&q);
            block->eval_method = gwy_get_guint16_le(&q);
            block->focus = gwy_get_guint16_le(&q);
            block->gain = gwy_get_guint16_le(&q);
            block->meas_zrange = gwy_get_guint16_le(&q);
            block->objective = gwy_get_guint16_le(&q);
            block->shutter = gwy_get_guint16_le(&q);
            block->zresolution = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] brightness %u, eval_method %u, focus %u, "
                      "gain %u, meas_zrange %u, objective %u, shutter %u, "
                      "zresolution %g",
                      blocktype,
                      block->brightness, block->eval_method, block->focus,
                      block->gain, block->meas_zrange, block->objective,
                      block->shutter,
                      block->zresolution);
        }
        else if (blocktype == 0x00a1) {
            MicroProfBlock00a1 *block = &mfile->block00a1;
            block->min_quality = gwy_get_guint16_le(&q);
            block->focus = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] min_quality %u, focus %g", blocktype, block->min_quality, block->focus);
        }
        else if (blocktype == 0x00a2) {
            MicroProfBlock00a2 *block = &mfile->block00a2;
            block->volt_range = gwy_get_guint32_le(&q);
            block->val_channel = gwy_get_guint32_le(&q);
            block->int_channel = gwy_get_guint32_le(&q);
            block->int_range = gwy_get_guint32_le(&q);
            block->min_valid_val = gwy_get_gdouble_le(&q);
            block->max_valid_val = gwy_get_gdouble_le(&q);
            block->min_valid_intens = gwy_get_gdouble_le(&q);
            block->max_valid_intens = gwy_get_gdouble_le(&q);
            get_CHARARRAY(block->unit_list, &q);
            block->selected_unit = gwy_get_guint32_le(&q);
            block->min_valid_unit_value = gwy_get_gdouble_le(&q);
            block->max_valid_unit_value = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] volt_range %u, val_channel %u, int_channel %u, "
                      "int_range %d, "
                      "min_valid_val %g, max_valid_val %g, "
                      "min_valid_intens %g, max_valid_intens %g, "
                      "selected_unit %u, "
                      "min_valid_unit_value %g, max_valid_unit_value %g",
                      blocktype,
                      block->volt_range, block->val_channel, block->int_channel,
                      block->int_range,
                      block->min_valid_val, block->max_valid_val,
                      block->min_valid_intens, block->max_valid_intens,
                      block->selected_unit,
                      block->min_valid_unit_value, block->max_valid_unit_value);
        }
        else if (blocktype == 0x00a3) {
            MicroProfBlock00a3 *block = &mfile->block00a3;
            block->cfm_objective = gwy_get_guint16_le(&q);
            block->cfm_shutter = gwy_get_guint16_le(&q);
            block->start_pos = gwy_get_gdouble_le(&q);
            block->end_pos = gwy_get_gdouble_le(&q);
            block->cfm_zresolution = gwy_get_gdouble_le(&q);
            block->lower_reflect_threshold = gwy_get_gdouble_le(&q);
            block->upper_reflect_threshold = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] cfm_objective %u, cfm_shutter %u, "
                      "start_pos %g, end_pos %g, "
                      "cfm_zresolution %g, "
                      "lower_reflect_threshold %g, "
                      "upper_reflect_threshold %g",
                      blocktype, block->cfm_objective, block->cfm_shutter,
                      block->start_pos, block->end_pos,
                      block->cfm_zresolution,
                      block->lower_reflect_threshold,
                      block->upper_reflect_threshold);
        }
        else if (blocktype == 0x00a4) {
            MicroProfBlock00a4 *block = &mfile->block00a4;
            block->angle = gwy_get_gdouble_le(&q);
            block->I_zfb = gwy_get_gdouble_le(&q);
            block->P_zfb = gwy_get_gdouble_le(&q);
            block->retract_time = gwy_get_gdouble_le(&q);
            block->xoffset = gwy_get_gdouble_le(&q);
            block->yoffset = gwy_get_gdouble_le(&q);
            block->zgain = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] angle %g, I_zfb %g, P_zfb %g, retract_time %g, xoffset %g, yoffset %g, zgain %g",
                      blocktype, block->angle, block->I_zfb, block->P_zfb,
                      block->retract_time, block->xoffset, block->yoffset, block->zgain);
        }
        else if (blocktype == 0x00a5) {
            MicroProfBlock00a5 *block = &mfile->block00a5;
            block->external_timing = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] external_timing %d", blocktype, block->external_timing);
        }
        else if (blocktype == 0x00a6 && blocksize >= 264) {
            MicroProfBlock00a6 *block = &mfile->block00a6;
            textlen = gwy_get_guint32_le(&q);
            if (textlen == blocksize - 264) {
                block->objective_name = g_strndup(q, textlen);
                get_CHARARRAY(block->correction_file, &q);
                block->show_measurement = gwy_get_guint32_le(&q);
                gwy_debug("[%04x] objective_name \"%s\", correction_file \"%.256s\", show_measurement %d",
                          blocktype, block->objective_name, block->correction_file, block->show_measurement);
            }
        }
        else if (blocktype == 0x00a7 && blocksize >= 4) {
            /* The subblocks give their own sizes, but I have only seen a
             * fixed structure so far.  So read it so. */
            MicroProfBlock00a7 *block = &mfile->block00a7;
            block->nsubblocks = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] nsubblocks %u", blocktype, block->nsubblocks);
            if (block->nsubblocks*(368 + 36) == blocksize - 4) {
                block->sub368 = g_new(MicroProfBlock368Sub00a7, block->nsubblocks);
                block->sub36 = g_new(MicroProfBlock36Sub00a7, block->nsubblocks);
                for (ii = 0; ii < block->nsubblocks; ii++) {
                    MicroProfBlock368Sub00a7 *sub368 = block->sub368 + ii;
                    MicroProfBlock36Sub00a7 *sub36 = block->sub36 + ii;

                    sub368->len = gwy_get_guint32_le(&q);
                    sub368->int1 = gwy_get_guint32_le(&q);
                    sub368->int2 = gwy_get_guint32_le(&q);
                    sub368->int3 = gwy_get_guint32_le(&q);
                    sub368->int4 = gwy_get_guint32_le(&q);
                    get_CHARARRAY0(sub368->text, &q);
                    sub368->float1 = gwy_get_gdouble_le(&q);
                    sub368->float2 = gwy_get_gdouble_le(&q);
                    sub368->int5 = gwy_get_guint32_le(&q);
                    sub368->float3 = gwy_get_gfloat_le(&q);
                    get_CHARARRAY(sub368->zeros1, &q);
                    sub368->int6 = gwy_get_guint32_le(&q);
                    sub368->int7 = gwy_get_guint32_le(&q);
                    sub368->float4 = gwy_get_gdouble_le(&q);
                    sub368->int8 = gwy_get_guint32_le(&q);
                    sub368->int9 = gwy_get_guint32_le(&q);
                    sub368->float5 = gwy_get_gdouble_le(&q);
                    get_CHARARRAY(sub368->zeros2, &q);
                    gwy_debug("[%04x:%u] len %u, "
                              "int1 %u, int2 %u, "
                              "int3 %u, int4 %u, "
                              "text \"%s\", float1 %g, float2 %g, "
                              "int5 %u, float3 %g, int6 %u, int7 %u, "
                              "float4 %g, int8 %u, int9 %u, float5 %g",
                              blocktype, 2*ii, sub368->len,
                              sub368->int1, sub368->int2,
                              sub368->int3, sub368->int4,
                              sub368->text, sub368->float1, sub368->float2,
                              sub368->int5, sub368->float3,
                              sub368->int6, sub368->int7, sub368->float4,
                              sub368->int8, sub368->int9, sub368->float5);

                    sub36->len = gwy_get_guint32_le(&q);
                    sub36->float1 = gwy_get_gdouble_le(&q);
                    sub36->float2 = gwy_get_gfloat_le(&q);
                    sub36->zero1 = gwy_get_guint32_le(&q);
                    sub36->int1 = gwy_get_guint32_le(&q);
                    sub36->int2 = gwy_get_guint32_le(&q);
                    sub36->zero2 = gwy_get_guint32_le(&q);
                    sub36->float3 = gwy_get_gdouble_le(&q);
                    gwy_debug("[%04x:%u] len %u, "
                              "float1 %g, float2 %g, "
                              "zero1 %u, "
                              "int1 %u, int2 %u, zero2 %u, "
                              "float3 %g",
                              blocktype, 2*ii+1, sub36->len,
                              sub36->float1, sub36->float2,
                              sub36->zero1,
                              sub36->int1, sub36->int2, sub36->zero2,
                              sub36->float3);
                }
            }
        }
        else if (blocktype == 0x00a8) {
            MicroProfBlock00a8 *block = &mfile->block00a8;
            block->zero1 = gwy_get_gdouble_le(&q);
            block->float1 = gwy_get_gdouble_le(&q);
            block->float2 = gwy_get_gdouble_le(&q);
            block->float3 = gwy_get_gdouble_le(&q);
            block->float4 = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] zero1 %g, "
                      "float1 %g, float2 %g, float3 %g, float4 %g",
                      blocktype, block->zero1,
                      block->float1, block->float2,
                      block->float3, block->float4);
        }
        else if (blocktype == 0x00a9) {
            MicroProfBlock00a9 *block = &mfile->block00a9;
            block->xaxis_subtracted = gwy_get_guint32_le(&q);
            block->yaxis_subtracted = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] xaxis_subtracted %d, yaxis_subtracted %d",
                      blocktype, block->xaxis_subtracted, block->yaxis_subtracted);
        }
        else if (blocktype == 0x00aa) {
            MicroProfBlock00aa *block = &mfile->block00aa;
            get_CHARARRAY(block->sensor_ini_path, &q);
            block->start_pos = gwy_get_gdouble_le(&q);
            block->end_pos = gwy_get_gdouble_le(&q);
            block->zspeed = gwy_get_gdouble_le(&q);
            block->presampling_zlength = gwy_get_gdouble_le(&q);
            block->postsampling_zlength = gwy_get_gdouble_le(&q);
            block->pos_after_zscan = gwy_get_guint32_le(&q);
            block->preprocessor = gwy_get_guint32_le(&q);
            block->postprocessor = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] sensor_ini_path \"%.259s\", "
                      "start_pos %g, end_pos %g, zspeed %g, "
                      "presampling_zlength %g, postsampling_zlength %g, "
                      "pos_after_zscan %u, "
                      "preprocessor %u, postprocessor %u",
                      blocktype, block->sensor_ini_path,
                      block->start_pos, block->end_pos, block->zspeed,
                      block->presampling_zlength, block->postsampling_zlength,
                      block->pos_after_zscan,
                      block->preprocessor, block->postprocessor);
        }
        else if (blocktype == 0x00ab && blocksize >= 4) {
            MicroProfBlock00ab *block = &mfile->block00ab;
            block->nsubblocks = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] nsubblocks %u", blocktype, block->nsubblocks);
            if (24*block->nsubblocks == blocksize - 4) {
                block->subblocks = g_new(MicroProfBlockSub00ab, block->nsubblocks);
                for (ii = 0; ii < block->nsubblocks; ii++) {
                    MicroProfBlockSub00ab *sub = block->subblocks + ii;
                    sub->alias = gwy_get_guint32_le(&q);
                    sub->scale = gwy_get_gdouble_le(&q);
                    sub->offset = gwy_get_gdouble_le(&q);
                    sub->absolute = gwy_get_guint32_le(&q);
                    gwy_debug("[%04x:%u] alias %u, scale %g, offset %g, absolute %d",
                              blocktype, ii, sub->alias, sub->scale, sub->offset, sub->absolute);
                }
            }
        }
        else if (blocktype == 0x00ac && blocksize >= 8) {
            MicroProfBlock00ac *block = &mfile->block00ac;
            textlensum = textlen = gwy_get_guint32_le(&q);
            if (textlen <= blocksize - 8) {
                block->user_name = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 8) {
                block->user_description = g_strndup(q, textlen);
                q += textlen;
                gwy_debug("[%04x] user_name \"%s\", user_description \"%s\"",
                          blocktype, block->user_name, block->user_description);
            }
        }
        else if (blocktype == 0x00ad) {
            MicroProfBlock00ad *block = &mfile->block00ad;
            block->nsubblocks = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] nsubblocks %u (unclear how to read)",
                      blocktype, block->nsubblocks);
        }
        else if (blocktype == 0x00ae) {
            MicroProfBlock00ae *block = &mfile->block00ae;
            block->signal = gwy_get_guint32_le(&q);
            block->filter = gwy_get_guint32_le(&q);
            block->reference_type = gwy_get_guint32_le(&q);
            block->layer_stack_id = gwy_get_guint32_le(&q);
            block->reference_material_id = gwy_get_gint32_le(&q);
            block->reference_constant = gwy_get_gdouble_le(&q);
            block->material_thickness = gwy_get_gdouble_le(&q);
            gwy_debug("[%04x] signal %u, filter %u, reference_type %u, "
                      "layer_stack_id %u, reference_material_id %d, "
                      "reference_constant %g, material_thickness %g",
                      blocktype,
                      block->signal, block->filter, block->reference_type,
                      block->layer_stack_id, block->reference_material_id,
                      block->reference_constant, block->material_thickness);
        }
        else if (blocktype == 0x00af) {
            MicroProfBlock00af *block = &mfile->block00af;
            block->auto_focus = gwy_get_guint32_le(&q);
            block->auto_brightness = gwy_get_guint32_le(&q);
            block->focus_search_length = gwy_get_gdouble_le(&q);
            block->max_brightness = gwy_get_guint32_le(&q);
            block->move_back_after_meas = gwy_get_guint32_le(&q);
            block->move_back_below_scan_range = gwy_get_guint32_le(&q);
            gwy_debug("[%04x] auto_focus %d, auto_brightness %d, "
                      "focus_search_length %g, max_brightness %u, "
                      "move_back_after_meas %u, "
                      "move_back_below_scan_range %u",
                      blocktype, block->auto_focus, block->auto_brightness,
                      block->focus_search_length, block->max_brightness,
                      block->move_back_after_meas,
                      block->move_back_below_scan_range);
        }
        else if (blocktype == 0x00b0) {
            MicroProfBlock00b0 *block = &mfile->block00b0;
            block->is_set = gwy_get_guint32_le(&q);
            textlensum = textlen = gwy_get_guint32_le(&q);
            if (textlen <= blocksize - 4) {
                block->position_on_sample = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 4) {
                block->aaxt_version = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 4) {
                block->die_index = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 4) {
                block->lot_id = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 4) {
                block->recipe_name = g_strndup(q, textlen);
                q += textlen;
            }
            textlensum += textlen = gwy_get_guint32_le(&q);
            if (textlensum <= blocksize - 4) {
                block->wafer_id = g_strndup(q, textlen);
                q += textlen;
            }
            gwy_debug("[%04x] is_set %d, "
                      "position_on_sample \"%s\", aaxt_version \"%s\", "
                      "die_index \"%s\", lot_id \"%s\", "
                      "recipe_name \"%s\", wafer_id \"%s\"",
                      blocktype, block->is_set,
                      block->position_on_sample, block->aaxt_version,
                      block->die_index, block->lot_id,
                      block->recipe_name, block->wafer_id);
        }
        else {
            g_warning("Failure in reading variable-sized block %04x?", blocktype);
        }

        p += blocksize;
        size -= blocksize;
    }
    ok = TRUE;

fail:
    g_string_free(str, TRUE);

    return ok;
}

static gboolean
read_images_block(const guchar *p, gsize size,
                  MicroProfBlock007d *block,
                  GError **error)
{
    enum {
        BLOCK_HEADER_PREFIX_SIZE = 2*4,
        IMAGE_BLOCK_SIZE = 4*4,
    };

    GArray *imgblocks;

    if (size < BLOCK_HEADER_PREFIX_SIZE) {
        err_TRUNCATED_PART(error, "block 0x7d");
        return FALSE;
    }

    block->currbuf_id1 = gwy_get_guint16_le(&p);
    block->currbuf_id2 = gwy_get_guint16_le(&p);
    block->currbuf_id3 = gwy_get_guint16_le(&p);
    block->currbuf_id4 = gwy_get_guint16_le(&p);
    size -= BLOCK_HEADER_PREFIX_SIZE;
    gwy_debug("[%04x] currbuf_id1 %u, currbuf_id2 %u, currbuf_id3 %u, currbuf_id4 %u",
              0x7d, block->currbuf_id1, block->currbuf_id2, block->currbuf_id3, block->currbuf_id4);

    imgblocks = g_array_new(FALSE, FALSE, sizeof(MicroProfImageBlock));
    while (size > IMAGE_BLOCK_SIZE) {
        MicroProfImageBlock imgblock;
        gulong datasize;

        imgblock.datatype = gwy_get_guint32_le(&p);
        imgblock.xres = gwy_get_guint32_le(&p);
        imgblock.yres = gwy_get_guint32_le(&p);
        imgblock.bpp = gwy_get_guint32_le(&p);
        imgblock.data = p;
        gwy_debug("[%04x:%u] datatype 0x%04x, xres %u, yres %u, bpp %u",
                  0x7d, (guint)imgblocks->len, imgblock.datatype,
                  imgblock.xres, imgblock.yres, imgblock.bpp);
        size -= IMAGE_BLOCK_SIZE;
        if (!check_imgblock(&imgblock, size, error))
            goto fail;

        g_array_append_val(imgblocks, imgblock);
        datasize = imgblock.xres * imgblock.yres * (imgblock.bpp/8);
        p += datasize;
        size -= datasize;
    }

    if (size > 0)
        g_warning("Images data block was not fully consumed.");

    block->nimages = imgblocks->len;
    block->imgblocks = (MicroProfImageBlock*)g_array_free(imgblocks, FALSE);
    return TRUE;

fail:
    g_array_free(imgblocks, TRUE);
    return FALSE;
}

static gboolean
check_imgblock(const MicroProfImageBlock *imgblock, gsize size,
               GError **error)
{
    gsize datasize;

    if (imgblock->bpp != 16 && imgblock->bpp != 32) {
        err_BPP(error, imgblock->bpp);
        return FALSE;
    }

    if (err_DIMENSION(error, imgblock->xres)
        || err_DIMENSION(error, imgblock->yres))
        return FALSE;

    datasize = imgblock->xres * imgblock->yres * (imgblock->bpp/8);
    if (err_SIZE_MISMATCH(error, datasize, size, FALSE))
        return FALSE;

    return TRUE;
}

static void
microprof_read_data_field(GwyContainer *container, gint id,
                          const MicroProfImageBlock *imgblock,
                          gdouble xrange, gdouble yrange, gdouble zscale,
                          const guchar *buffer)
{
    guint i, n = imgblock->xres*imgblock->yres, bpp = imgblock->bpp;
    GwyRawDataType datatype;
    MicroProfDataType mpdtype;
    GwyDataField *dfield, *mask = NULL;
    GString *title;
    gdouble *d, *m;
    const gchar *s;

    dfield = gwy_data_field_new(imgblock->xres, imgblock->yres, xrange, yrange, FALSE);
    gwy_debug("bpp %d", bpp);
    datatype = (bpp == 16 ? GWY_RAW_DATA_UINT16 : GWY_RAW_DATA_SINT32);
    d = gwy_data_field_get_data(dfield);
    gwy_convert_raw_data(buffer, n, 1, datatype, GWY_BYTE_ORDER_LITTLE_ENDIAN, d, 1.0, 0.0);

    gwy_data_field_invert(dfield, TRUE, FALSE, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");

    /* XXX: Invalid data seem to be marked by the special value 1.
     * TODO: Use first_valid and last_valid. */
    d = gwy_data_field_get_data(dfield);
    for (i = 0; i < n; i++) {
        if (d[i] == 1.0)
            break;
    }
    if (i < n) {
        mask = gwy_data_field_new_alike(dfield, TRUE);
        m = gwy_data_field_get_data(mask);
        for (i = 0; i < n; i++) {
            if (d[i] != 1.0)
                m[i] = 1.0;
        }
        gwy_app_channel_remove_bad_data(dfield, mask);
    }

    mpdtype = (imgblock->datatype & MICROPROF_TYPE_MASK);
    /* FIXME: What about the other types?  Phase?  Piezo? */
    if (mpdtype & (MICROPROF_TOPOGRAPHY | MICROPROF_THICKNES | MICROPROF_SAMPLE_THICKNESS | MICROPROF_AFM)) {
        gwy_data_field_multiply(dfield, zscale);
        gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");
    }

    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), dfield);
    if (mask)
        gwy_container_pass_object(container, gwy_app_get_mask_key_for_id(id), mask);

    s = gwy_enuml_to_string(mpdtype,
                            "Piezo", MICROPROF_PIEZO,
                            "Intensity", MICROPROF_INTENSITY,
                            "Topography", MICROPROF_TOPOGRAPHY,
                            "Real part", MICROPROF_REAL_PART,
                            "Imaginary part", MICROPROF_IMAG_PART,
                            "Camera", MICROPROF_CAMERA,
                            "Thickness", MICROPROF_THICKNES,
                            "DIB from file", MICROPROF_DIB_FROM_FILE,
                            "Absolute value", MICROPROF_ABS_VAL,
                            "Phase", MICROPROF_PHASE,
                            "Sample thickness", MICROPROF_SAMPLE_THICKNESS,
                            "AFM", MICROPROF_AFM,
                            "Quality", MICROPROF_QUALITY,
                            "Fit", MICROPROF_FIT,
                            "Slope", MICROPROF_SLOPE,
                            NULL);
    title = g_string_new(s ? s : "Unknown");

    if (imgblock->datatype & MICROPROF_BOTTOM_SENSOR)
        g_string_append(title, " (bottom sensor)");
    else
        g_string_append(title, " (top sensor)");

    i = ((imgblock->datatype & MICROPROF_BUFFER_CNTR_MASK) >> 24) + 1;
    g_string_append_printf(title, " %u", i);

    if (imgblock->datatype & MICROPROF_EXTENDED)
        g_string_append(title, ", extended");
    if (imgblock->datatype & MICROPROF_COMPUTED)
        g_string_append(title, ", computed");
    if (imgblock->datatype & MICROPROF_FILTERED)
        g_string_append(title, ", filtered");

    gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(id), title->str);
    g_string_free(title, TRUE);
}

static void
set_meta_string(GwyContainer *meta, const gchar *name, GString *str)
{
    g_strstrip(str->str);
    if ((str->len = strlen(str->str)))
        gwy_container_set_const_string_by_name(meta, name, str->str);
}

static void
set_meta_str_field(GwyContainer *meta, GString *str,
                   const gchar *name, const guchar *s)
{
    if (!s || !*s)
        return;

    g_string_assign(str, s);
    set_meta_string(meta, name, str);
}

static GwyContainer*
create_meta(const MicroProfFile *mfile)
{
    GwyContainer *meta = gwy_container_new();
    GString *str = g_string_new(NULL);

    set_meta_str_field(meta, str, "Description", mfile->block0065.text);
    set_meta_str_field(meta, str, "Parameter set name", mfile->block0077.parset_name);
    set_meta_str_field(meta, str, "Calibration", mfile->block007f.calibration);
    set_meta_str_field(meta, str, "CHR serial number", mfile->block008e.ser_num);
    set_meta_str_field(meta, str, "User name", mfile->block00ac.user_name);
    set_meta_str_field(meta, str, "User description", mfile->block00ac.user_description);

    g_string_printf(str, "%.1f µm", 1e6*mfile->block0067.xoffset);
    set_meta_string(meta, "Offset X", str);
    g_string_printf(str, "%.1f µm", 1e6*mfile->block0067.yoffset);
    set_meta_string(meta, "Offset Y", str);

    g_string_printf(str, "%.1f m/s", mfile->block0068.xspeed);
    set_meta_string(meta, "Speed X", str);
    g_string_printf(str, "%.1f m/s", mfile->block0068.yspeed);
    set_meta_string(meta, "Speed Y", str);

    g_string_printf(str, "%u ms", mfile->block0068.sensor_delay);
    set_meta_string(meta, "Sensor delay", str);
    g_string_printf(str, "%u ms", mfile->block0068.sensor_error_time);
    set_meta_string(meta, "Sensor error time", str);

    if (mfile->seen_blocks[0x0072]) {
        time_t t1 = mfile->block0072.meas_started,
               t2 = mfile->block0072.meas_ended;

        g_string_assign(str, ctime(&t1));
        set_meta_string(meta, "Measurement started", str);
        g_string_assign(str, ctime(&t2));
        set_meta_string(meta, "Measurement finished", str);
        g_string_printf(str, "%u s", mfile->block0072.meas_time);
        set_meta_string(meta, "Measurement time", str);
    }

    g_string_free(str, TRUE);

    return meta;
}

static GwyContainer*
microprof_txt_load(const gchar *filename,
                   G_GNUC_UNUSED GwyRunType mode,
                   GError **error)
{
    GwyContainer *container = NULL;
    guchar *p, *buffer = NULL;
    GwyTextHeaderParser parser;
    GHashTable *meta = NULL;
    gchar *header = NULL, *s, *prev;
    gsize size = 0;
    GError *err = NULL;
    GwyDataField *dfield = NULL;
    gdouble xreal, yreal, zscale, v;
    gint hlines, xres, yres, i, j;
    gdouble *d;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    if (size < MICROPROF_MIN_TEXT_SIZE || memcmp(buffer, MAGIC_TXT, MAGIC_TXT_SIZE) != 0) {
        err_FILE_TYPE(error, "MicroProf");
        goto fail;
    }

    hlines = atoi(buffer + MAGIC_TXT_SIZE);
    if (hlines < 7) {
        err_FILE_TYPE(error, "MicroProf");
        goto fail;
    }

    /* Skip specified number of lines */
    for (p = buffer, i = 0; i < hlines; i++) {
        while (*p != '\n' && (gsize)(p - buffer) < size)
            p++;
        if ((gsize)(p - buffer) == size) {
            err_FILE_TYPE(error, "MicroProf");
            goto fail;
        }
        /* Now skip the \n */
        p++;
    }

    header = g_memdup(buffer, p - buffer + 1);
    header[p - buffer] = '\0';

    gwy_clear(&parser, 1);
    parser.key_value_separator = "=";
    meta = gwy_text_header_parse(header, &parser, NULL, NULL);

    if (!(s = g_hash_table_lookup(meta, "XSize")) || !((xres = atoi(s)) > 0)) {
        err_INVALID(error, "XSize");
        goto fail;
    }

    if (!(s = g_hash_table_lookup(meta, "YSize")) || !((yres = atoi(s)) > 0)) {
        err_INVALID(error, "YSize");
        goto fail;
    }

    if (!(s = g_hash_table_lookup(meta, "XRange")) || !((xreal = g_ascii_strtod(s, NULL)) > 0.0)) {
        err_INVALID(error, "YRange");
        goto fail;
    }

    if (!(s = g_hash_table_lookup(meta, "YRange")) || !((yreal = g_ascii_strtod(s, NULL)) > 0.0)) {
        err_INVALID(error, "YRange");
        goto fail;
    }

    if (!(s = g_hash_table_lookup(meta, "ZScale")) || !((zscale = g_ascii_strtod(s, NULL)) > 0.0)) {
        err_INVALID(error, "ZScale");
        goto fail;
    }

    dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    d = gwy_data_field_get_data(dfield);
    s = (gchar*)p;
    for (i = 0; i < yres; i++) {
        for (j = 0; j < xres; j++) {
            prev = s;
            /* Skip x */
            v = strtol(s, &s, 10);
            if (v != j)
                g_warning("Column number mismatch");
            /* Skip y */
            v = strtol(s, &s, 10);
            if (v != i)
                g_warning("Row number mismatch");
            /* Read value */
            d[(yres-1 - i)*xres + j] = strtol(s, &s, 10)*zscale;

            /* Check whether we moved in the file */
            if (s == prev) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("File contains fewer than XSize*YSize data points."));
                goto fail;
            }
        }
    }

    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), "m");

    container = gwy_container_new();
    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(0), dfield);
    gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(0), "Topography");

    gwy_file_channel_import_log_add(container, 0, NULL, filename);

fail:
    gwy_file_abandon_contents(buffer, size, NULL);
    if (meta)
        g_hash_table_destroy(meta);
    g_free(header);

    return container;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
