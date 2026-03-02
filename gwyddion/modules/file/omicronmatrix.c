/*
 *  $Id: omicronmatrix.c 26135 2024-01-15 12:54:27Z yeti-dn $
 *  Copyright (C) 2008-2021 Philipp Rahe, David Necas
 *  E-mail: hquerquadrat@gmail.com
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
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * [FILE-MAGIC-FREEDESKTOP]
 * <mime-type type="application/x-omicron-matrix-spm">
 *   <comment>Omicron MATRIX SPM data</comment>
 *   <magic priority="80">
 *     <match type="string" offset="0" value="ONTMATRX0101TLKB"/>
 *     <match type="string" offset="0" value="ONTMATRX0101ATEM"/>
 *   </magic>
 *   <glob pattern="*.mtrx"/>
 *   <glob pattern="*.MTRX"/>
 * </mime-type>
 **/

/**
 * [FILE-MAGIC-FILEMAGIC]
 * # Omicron MATRIX data format.
 * 0 string ONTMATRX0101TLKB Omicron MATRIX SPM image data
 * 0 string ONTMATRX0101ATEM Omicron MATRIX SPM parameter data
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Omicron MATRIX
 * .mtrx
 * Read SPS:Limited[1] Volume
 * [1] Spectra curves are imported as graphs, positional information is lost.
 **/

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/data-browser.h>
#include <app/gwymoduleutils-file.h>
#include <libprocess/datafield.h>
#include "err.h"


#define FILEIDENT "ONTMATRX0101"
#define FILEIDENT_SIZE (sizeof(FILEIDENT)-1)

#define IMGFILEIDENT "ONTMATRX0101TLKB"
#define IMGFILEIDENT_SIZE (sizeof(IMGFILEIDENT)-1)

#define PARFILEIDENT "ONTMATRX0101ATEM"
#define PARFILEIDENT_SIZE (sizeof(PARFILEIDENT)-1)

#define EXTENSION_HEADER ".mtrx"

#define STRING_MAXLENGTH 10000

/* defining OSNAVERSION, as used in the AFM group in Osnabrueck
 * inverts all df data and multiplies with 5.464
 * you shouldn't use this unless you know what you are doing
 */
//#define OSNAVERSION 1

/* Transferfunctions for correct scaling of Z/Df/I/Ext2... data */
typedef enum {
    TFF_LINEAR1D = 1,
    TFF_MULTILINEAR1D = 2,
} TransferFunctionType;

/* Maxim Krivenkov says 1 and 2 are like this, not the logical way. */
typedef enum {
    GRID_MODE_CONSTRAINT_NONE  = 0,
    GRID_MODE_CONSTRAINT_POINT = 1,
    GRID_MODE_CONSTRAINT_LINE  = 2,
} GridModeConstraintType;

/* Whether subgrid actually follows the main trace/retrace settings.
 * Should only be used when x_retrace is TRUE because otherwise there is just one direction anyway. */
typedef enum {
    SUBGRID_MATCH_BOTH    = 0,
    SUBGRID_MATCH_TRACE   = 1,
    SUBGRID_MATCH_RETRACE = 2,
} SubgridMatchMode;

/** States during parsing of parameterfile */
enum {
    IMAGE_FOUND = 1,
    UNKNOWN     = 0,
    FILE_END    = 2,
};

/** Datatypes for MATRIX files */
typedef enum {
    OMICRON_NONE   = 0,
    OMICRON_UINT32 = 1,
    OMICRON_DOUBLE = 2,
    OMICRON_STRING = 3,
    OMICRON_BOOL   = 4,
} OmicronDataType;

typedef struct {
    const gchar *name;
    gsize offset;
    OmicronDataType type;
} MatrixField;

/** Stores data for quick access.
 *  All supplement data is stored in a GwyContainer called meta
 */
typedef struct {
    guint32 xpoints;
    guint32 ypoints;
    guint32 zpoints;    /* volume spectroscopy */
    guint32 subgrid_x;
    guint32 subgrid_y;
    gboolean subgrid_enabled;
    gdouble width;
    gdouble height;
    gdouble zfrom;     /* volume spectroscopy */
    gdouble zto;       /* volume spectroscopy */
    gchar *rampunit;   /* volume spectroscopy */
    guint32 zoom;
    gdouble rastertime;
    gdouble preamp_range;
    GridModeConstraintType gridmode;
    gboolean x_retrace;
    gboolean y_retrace;
    SubgridMatchMode subgrid_match;
    gboolean dev1_ramp_reversal;
    gboolean dev2_ramp_reversal;

    // data for processing
    guint32 proc_cur_img_no;
    guint32 proc_intended_no;
    guint32 proc_available_no;

    // data during filereading
    guint32 state;

    // concerning the filename
    guint32 session;
    guint32 trace;
    gchar *channelname;

    /* what we figured out */
    gboolean use_paramfile;
    gchar *spectrum_x_axis;
    gchar *spectrum_y_axis;
} MatrixData;

/** stores information about scaling */
typedef struct {
    TransferFunctionType tfftype;
    gdouble factor_1;
    gdouble offset_1;
    gdouble neutralfactor_2;
    gdouble offset_2;
    gdouble prefactor_2;
    gdouble preoffset_2;
    gdouble raw1_2;
    guint32 cnumber;
    gchar *channelname;
    /* Final compound coefficients */
    gdouble z0;
    gdouble q;
} ValueScaling;

static gboolean      module_register       (void);
static gint          matrix_detect         (const GwyFileDetectInfo *fi,
                                            gboolean only_name);
static gchar*        matrix_readstr        (const guchar **buffer,
                                            const guchar *end,
                                            guint32 *size);
static gboolean      matrix_read_meta_value(const guchar **fp,
                                            const guchar *end,
                                            GwyContainer *hash,
                                            const gchar *hprefix,
                                            GwyContainer *meta,
                                            const gchar *mprefix,
                                            const gchar *inst,
                                            const gchar *prop,
                                            const gchar *unit,
                                            gboolean check);
static guint32       matrix_scanparamfile  (const guchar **buffer,
                                            const guchar *end,
                                            GwyContainer *hash,
                                            GwyContainer *meta,
                                            MatrixData *matrixdata);
static gboolean      matrix_scandatafile   (const guchar **buffer,
                                            const guchar *end,
                                            const gchar *filename,
                                            GwyContainer *container,
                                            GwyContainer *meta,
                                            GwyContainer *hash,
                                            MatrixData *matrixdata,
                                            gint depth);
static GwyContainer* matrix_load           (const gchar *filename,
                                            GwyRunType mode,
                                            GError **error);
static const gchar*  sstrconcat            (const gchar *s,
                                            ...);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Omicron MATRIX (param.mtrx & data.mtrx)"),
    "Philipp Rahe <hquerquadrat@gmail.com>",
#ifdef OSNAVERSION
    "0.90-Osnabruck",
#else
    "0.90",
#endif
    "Philipp Rahe",
    "2008",
};

GWY_MODULE_QUERY2(module_info, omicronmatrix)

static gboolean module_register(void)
{
    gwy_file_func_register("omicronmatrix",
                           N_("Omicron MATRIX (.mtrx & .mtrx)"),
                           (GwyFileDetectFunc)&matrix_detect,
                           (GwyFileLoadFunc)&matrix_load,
                           NULL,
                           NULL);

    return TRUE;
}

static gint
matrix_detect(const GwyFileDetectInfo *fileinfo,
              gboolean only_name)
{
    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXTENSION_HEADER) ? 15 : 0;

    if (fileinfo->buffer_len > IMGFILEIDENT_SIZE && !memcmp(fileinfo->head, IMGFILEIDENT, IMGFILEIDENT_SIZE))
         return 100;
    return 0;
}

/** read a string from the paramter or data file remember to free the result! */
static gchar*
matrix_readstr(const guchar **fp, const guchar *end, guint32 *size)
{
    // len is the number of characters (each 16Bit) encoded
    guint32 len;
    gchar *str = NULL;

    if (size)
        *size = 0;
    if (end - *fp < sizeof(guint32))
        return g_strdup("");

    len = gwy_get_guint32_le(fp);
    if (!len)
        return g_strdup("");

    if (end - *fp < len*sizeof(gunichar2) || len > STRING_MAXLENGTH) {
        g_warning("too long string, not readable");
        return g_strdup("");
    }

    str = gwy_utf16_to_utf8((const gunichar2*)*fp, len, GWY_BYTE_ORDER_LITTLE_ENDIAN);
    *fp += 2*len;
    if (!str) {
        g_warning("error reading or converting string");
        return g_strdup("");
    }

    if (size)
        *size = len;
    return str;
}

/* Read the four-byte identifier.  It is stored as little endian int32, so it would be reversed if read directly.
 * Reverse it here to obtain non-silly identifier names. */
static gboolean
read_ident(const guchar **p, const guchar *end, gchar *ident)
{
    if (end - *p < 4)
        return FALSE;

    ident[4] = '\0';
    ident[3] = (*p)[0];
    ident[2] = (*p)[1];
    ident[1] = (*p)[2];
    ident[0] = (*p)[3];
    *p += 4;

    return TRUE;
}

static void
set_structured_meta_value(GwyContainer *meta,
                          const gchar *prefix,
                          const gchar *inst,
                          const gchar *prop,
                          const gchar *unit,
                          const gchar *value)
{
    const gchar *key;

    if (!meta)
        return;

    if (unit && *unit && !gwy_stramong(unit, "--", "---", NULL))
        key = sstrconcat(prefix, ":", inst, ".", prop, " [", unit, "]", NULL);
    else
        key = sstrconcat(prefix, ":", inst, ".", prop, NULL);
    gwy_container_set_const_string_by_name(meta, key, value);
}

static void
set_structured_meta_uint32(GwyContainer *meta,
                           const gchar *prefix,
                           const gchar *inst,
                           const gchar *prop,
                           const gchar *unit,
                           guint32 value)
{
    gchar buf[12];

    if (meta) {
        g_snprintf(buf, sizeof(buf), "%u", value);
        set_structured_meta_value(meta, prefix, inst, prop, unit, buf);
    }
}

static void
set_structured_meta_double(GwyContainer *meta,
                           const gchar *prefix,
                           const gchar *inst,
                           const gchar *prop,
                           const gchar *unit,
                           gdouble value)
{
    gchar buf[32];

    if (meta) {
        g_snprintf(buf, sizeof(buf), "%e", value);
        set_structured_meta_value(meta, prefix, inst, prop, unit, buf);
    }
}

/* XXX: There was some convoluted logic, which was clearly broken (using
 * sizeof() on pointers instead of target data, reading uint32 into anything,
 * ...
 *
 * I simplified it to a TRUE/FALSE check, because that's how it is called.
 * 0 means OK and read the following data.
 * 1 means something else; the code originally takes it as a 32bit integer
 *   value which is the result -- but how it knows the thing even is 32bit
 *   integer?  I currently treat is as failure.
 * We also rewind on failure because that's what the original code did. */
static inline gboolean
matrix_read_check(const guchar **fp, const guchar *end, gboolean check)
{
    guint32 a;

    if (!check)
        return TRUE;

    if (end - *fp < sizeof(guint32))
        return FALSE;

    a = gwy_get_guint32_le(fp);
    if (a)
        *fp -= sizeof(guint32);
    return !a;
}

/* Reads the next datafield and store it in the auxiliary container. If @meta is not NULL, it is also stored here.
 * These fields have a identifier in front.
 */
static gboolean
matrix_read_meta_value(const guchar **fp, const guchar *end,
                       GwyContainer *hash, const gchar *hprefix,
                       GwyContainer *meta, const gchar *mprefix,
                       const gchar *inst,
                       const gchar *prop,
                       const gchar *unit,
                       gboolean check)
{
    const gchar *name = sstrconcat(hprefix, inst, ".", prop, NULL);
    gchar id[5];

    if (!matrix_read_check(fp, end, check)
        || !read_ident(fp, end, id))
        return FALSE;

    if (gwy_strequal(id, "LONG") && end - *fp >= sizeof(guint32)) {
        guint32 v = gwy_get_guint32_le(fp);
        gwy_container_set_int32_by_name(hash, name, v);
        set_structured_meta_uint32(meta, mprefix, inst, prop, unit, v);
    }
    else if (gwy_strequal(id, "BOOL") && end - *fp >= sizeof(guint32)) {
        gboolean a = !!gwy_get_guint32_le(fp);
        gwy_container_set_boolean_by_name(hash, name, a);
        set_structured_meta_uint32(meta, mprefix, inst, prop, unit, a);
    }
    else if (gwy_strequal(id, "DOUB") && end - *fp >= sizeof(gdouble)) {
        gdouble v = gwy_get_gdouble_le(fp);
        gwy_container_set_double_by_name(hash, name, v);
        set_structured_meta_double(meta, mprefix, inst, prop, unit, v);
    }
    else if (gwy_strequal(id, "STRG")) {
        gchar *str = matrix_readstr(fp, end, NULL);
        set_structured_meta_value(meta, mprefix, inst, prop, unit, str);
        /* This consumes str. */
        gwy_container_set_string_by_name(hash, name, (guchar*)str);
    }
    else
        return FALSE;

    return TRUE;
}

static OmicronDataType
matrix_read_long(guint32 *value, const guchar **fp, const guchar *end,
                 gboolean check, const gchar *what)
{
    gchar id[5];

    if (!matrix_read_check(fp, end, check)
        || !read_ident(fp, end, id)
        || !gwy_strequal(id, "LONG")
        || end - *fp < sizeof(guint32)) {
        g_warning("%s unreadable", what);
        return OMICRON_NONE;
    }

    *value = gwy_get_guint32_le(fp);
    gwy_debug("%s %u", what, *value);
    return OMICRON_UINT32;
}

static OmicronDataType
matrix_read_bool(gboolean *value, const guchar **fp, const guchar *end,
                 gboolean check, const gchar *what)
{
    gchar id[5];

    if (!matrix_read_check(fp, end, check)
        || !read_ident(fp, end, id)
        || !gwy_strequal(id, "BOOL")
        || end - *fp < sizeof(guint32)) {
        g_warning("%s unreadable", what);
        return OMICRON_NONE;
    }

    *value = !!gwy_get_guint32_le(fp);
    gwy_debug("%s %s", what, *value ? "True" : "False");
    return OMICRON_BOOL;
}

static OmicronDataType
matrix_read_double(gdouble *value, const guchar **fp, const guchar *end,
                   gboolean check, const gchar *what)
{
    gchar id[5];

    if (!matrix_read_check(fp, end, check)
        || !read_ident(fp, end, id)
        || !gwy_strequal(id, "DOUB")
        || end - *fp < sizeof(gdouble)) {
        g_warning("%s unreadable", what);
        return OMICRON_NONE;
    }

    *value = gwy_get_gdouble_le(fp);
    gwy_debug("%s %g", what, *value);
    return OMICRON_DOUBLE;
}

static OmicronDataType
matrix_read_string(gchar **value, const guchar **fp, const guchar *end,
                   gboolean check, const gchar *what)
{
    gchar id[5];

    if (!matrix_read_check(fp, end, check)
        || !read_ident(fp, end, id)
        || !gwy_strequal(id, "STRG")) {
        g_warning("%s unreadable", what);
        return OMICRON_NONE;
    }

    g_free(*value);
    *value = matrix_readstr(fp, end, NULL);
    if (*value)
        return OMICRON_STRING;

    g_warning("%s unreadable", what);
    return OMICRON_NONE;
}

static gboolean
read_prop_fields(const guchar **fp, const guchar *end,
                 MatrixData *matrixdata, GwyContainer *meta,
                 const gchar *ident,
                 const gchar *inst, const gchar *prop,
                 const gchar *unit,
                 const MatrixField *fields, guint nfields)
{
    guint i;

    for (i = 0; i < nfields; i++) {
        if (gwy_strequal(prop, fields[i].name)) {
            if (fields[i].type == OMICRON_BOOL) {
                gboolean *p = G_STRUCT_MEMBER_P(matrixdata, fields[i].offset);
                if (matrix_read_bool(p, fp, end, TRUE, prop)) {
                    set_structured_meta_uint32(meta, ident, inst, prop, unit, *p);
                    return TRUE;
                }
            }
            else if (fields[i].type == OMICRON_UINT32) {
                guint32 *p = G_STRUCT_MEMBER_P(matrixdata, fields[i].offset);
                if (matrix_read_long(p, fp, end, TRUE, prop)) {
                    set_structured_meta_uint32(meta, ident, inst, prop, unit, *p);
                    return TRUE;
                }
            }
            else if (fields[i].type == OMICRON_DOUBLE) {
                gdouble *p = G_STRUCT_MEMBER_P(matrixdata, fields[i].offset);
                if (matrix_read_double(p, fp, end, TRUE, prop)) {
                    set_structured_meta_double(meta, ident, inst, prop, unit, *p);
                    return TRUE;
                }
            }
            else {
                g_assert_not_reached();
            }
        }
    }
    return FALSE;
}

static gboolean
handle_xyscanner_props(const guchar **fp, const guchar *end,
                       MatrixData *matrixdata, GwyContainer *meta,
                       const gchar *ident,
                       const gchar *inst, const gchar *prop,
                       const gchar *unit)
{
    static const MatrixField xyscanner_fields[] = {
        { "Enable_Subgrid",     G_STRUCT_OFFSET(MatrixData, subgrid_enabled), OMICRON_BOOL,   },
        { "Grid_Mode",          G_STRUCT_OFFSET(MatrixData, gridmode),        OMICRON_UINT32, },
        { "Height",             G_STRUCT_OFFSET(MatrixData, height),          OMICRON_DOUBLE, },
        { "Lines",              G_STRUCT_OFFSET(MatrixData, ypoints),         OMICRON_UINT32, },
        { "Points",             G_STRUCT_OFFSET(MatrixData, xpoints),         OMICRON_UINT32, },
        { "Raster_Period_Time", G_STRUCT_OFFSET(MatrixData, rastertime),      OMICRON_DOUBLE, },
        { "Raster_Time",        G_STRUCT_OFFSET(MatrixData, rastertime),      OMICRON_DOUBLE, },
        { "Scan_Constraint",    G_STRUCT_OFFSET(MatrixData, gridmode),        OMICRON_UINT32, },
        { "Subgrid_Match_Mode", G_STRUCT_OFFSET(MatrixData, subgrid_match),   OMICRON_UINT32, },
        { "Subgrid_X",          G_STRUCT_OFFSET(MatrixData, subgrid_x),       OMICRON_UINT32, },
        { "Subgrid_Y",          G_STRUCT_OFFSET(MatrixData, subgrid_y),       OMICRON_UINT32, },
        { "Width",              G_STRUCT_OFFSET(MatrixData, width),           OMICRON_DOUBLE, },
        { "X_Points",           G_STRUCT_OFFSET(MatrixData, xpoints),         OMICRON_UINT32, },
        { "X_Retrace",          G_STRUCT_OFFSET(MatrixData, x_retrace),       OMICRON_BOOL,   },
        { "Y_Points",           G_STRUCT_OFFSET(MatrixData, ypoints),         OMICRON_UINT32, },
        { "Y_Retrace",          G_STRUCT_OFFSET(MatrixData, y_retrace),       OMICRON_BOOL,   },
        { "Zoom",               G_STRUCT_OFFSET(MatrixData, zoom),            OMICRON_UINT32, },
    };

    return read_prop_fields(fp, end, matrixdata, meta, ident, inst, prop, unit,
                            xyscanner_fields, G_N_ELEMENTS(xyscanner_fields));
}

static gboolean
handle_spectroscopy_props(const guchar **fp, const guchar *end,
                          MatrixData *matrixdata, GwyContainer *meta,
                          const gchar *ident,
                          const gchar *inst, const gchar *prop,
                          const gchar *unit)
{
    static const MatrixField spectroscopy_fields[] = {
        { "Device_1_Start",                G_STRUCT_OFFSET(MatrixData, zfrom),              OMICRON_DOUBLE, },
        { "Device_1_End",                  G_STRUCT_OFFSET(MatrixData, zto),                OMICRON_DOUBLE, },
        { "Device_1_Points",               G_STRUCT_OFFSET(MatrixData, zpoints),            OMICRON_UINT32, },
        { "Enable_Device_1_Ramp_Reversal", G_STRUCT_OFFSET(MatrixData, dev1_ramp_reversal), OMICRON_BOOL,   },
        { "Enable_Device_2_Ramp_Reversal", G_STRUCT_OFFSET(MatrixData, dev2_ramp_reversal), OMICRON_BOOL,   },
    };

    if (gwy_stramong(prop, "Device_1_Start", "Device_1_End", NULL))
        gwy_assign_string(&matrixdata->rampunit, unit);

    return read_prop_fields(fp, end, matrixdata, meta, ident, inst, prop, unit,
                            spectroscopy_fields, G_N_ELEMENTS(spectroscopy_fields));
}

static gboolean
handle_regulator_props(const guchar **fp, const guchar *end,
                       MatrixData *matrixdata, GwyContainer *meta, GwyContainer *hash,
                       const gchar *ident,
                       const gchar *inst, const gchar *prop,
                       const gchar *unit)
{
    if (gwy_strequal(prop, "Preamp_Range_1")) {
        gchar *str = NULL, *t;
        if (matrix_read_string(&str, fp, end, TRUE, prop)) {
            const gchar *name = sstrconcat(ident, inst, ".", prop, NULL);
            if ((t = strchr(str, ';')))
                matrixdata->preamp_range = g_strtod(t+1, NULL);
            set_structured_meta_value(meta, ident, inst, prop, unit, str);
            /* This consumes str. */
            gwy_container_set_string_by_name(hash, name, (guchar*)str);
        }
        return TRUE;
    }
    return FALSE;
}

/** Scans OMICRON MATRIX parameterfiles. */
static guint32
matrix_scanparamfile(const guchar **buffer,
                     const guchar *end,
                     GwyContainer *hash,
                     GwyContainer *meta,
                     MatrixData *matrixdata)
{
    const guchar *fp = NULL;
    gchar ident[5];
    gint32 len;

    if (matrixdata && (matrixdata->state == 1 || matrixdata->state == 2)) {
        /* File end reached or image has been found. Do not proceed with parsing the parameter file */
        return 0;
    }
    // use local fp,
    // advance buffer in the end by len
    fp = *buffer;
    if (!read_ident(&fp, end, ident))
        return 0;

    /* next 4B are the length of following block in Bytes.
     * As buffer points before the identifier,
     * advance by 8B more
     */
    if (end - fp < sizeof(guint32))
        return 0;
    len = gwy_get_guint32_le(&fp) + 8;
    gwy_debug("omicronmatrix::matrix_scanparamfile: %s, len: %u", ident, len);
    if (end - fp < len)
        return 0;

    if (!gwy_stramong(ident, "XFER", "SCAN", "DICT", "CHCS", "INST", "CNXS", "GENL", NULL)) {
        /* In the following blocks the timestamp is available */
        /* these are the blocks, which are NOT listed above */
        /* timestamp is time_t with 8B */
        //guint64 longtime = gwy_get_guint64_le(&fp);
        fp += 8;
        len += 8;
    }
    else {
        /* No timestamp available, but perhaps one is stored in timestamp from scanning before */
    }

    if (gwy_strequal(ident, "META")) {
        // Data at beginning of parameter file
        gchar *programmname = NULL;
        gchar *version = NULL;
        gchar *profil = NULL;
        gchar *user = NULL;

        // program
        programmname = matrix_readstr(&fp, end, NULL);
        gwy_container_set_string_by_name(meta, "META: Program", programmname);
        // version
        version = matrix_readstr(&fp, end, NULL);
        gwy_container_set_string_by_name(meta, "META: Version", version);
        fp += 4;
        // profile name
        profil = matrix_readstr(&fp, end, NULL);
        gwy_container_set_string_by_name(meta, "META: Profil", profil);
        // username
        user = matrix_readstr(&fp, end, NULL);
        gwy_container_set_string_by_name(meta, "META: User", user);
    }
    else if (gwy_strequal(ident, "EXPD")) {
        // Description and project files
        guint32 i = 0;

        fp += 4;
        for (i = 0; i < 7; i++) {
            // read 7 strings
            gchar *s1 = NULL;
            gchar key[30];
            g_snprintf(key, sizeof(key), "EXPD: s%d", i);
            s1 = matrix_readstr(&fp, end, NULL);
            gwy_container_set_string_by_name(meta, key, (guchar*)s1);
        }

    }
    else if (gwy_strequal(ident, "FSEQ")) {
    }
    else if (gwy_strequal(ident, "EXPS")) {
        // Initial Configuration of the OMICRON system
        fp += 4;
        while (fp - *buffer < len)
            matrix_scanparamfile(&fp, end, hash, meta, matrixdata);
    }
    else if (gwy_strequal(ident, "GENL")) {
        // description
        guint32 i = 0;
        for (i = 0; i < 3; i++) {
            // read strings
            gchar *s1 = NULL;
            gchar key[30];
            g_snprintf(key, sizeof(key), "GENL: s%d", i);
            s1 = matrix_readstr(&fp, end, NULL);
            gwy_container_set_string_by_name(meta, key, (guchar*)s1);
        }
    }
    else if (gwy_strequal(ident, "INST")) {
        // configuration of instances
        guint32 anz = gwy_get_guint32_le(&fp);
        guint32 i = 0;
        for (i = 0; i < anz; i++) {
            /* Instance and Elements are following */
            gchar *s1 = NULL, *s2 = NULL, *s3 = NULL;
            gchar key[100];
            guint32 count;

            s1 = matrix_readstr(&fp, end, NULL);
            s2 = matrix_readstr(&fp, end, NULL);
            s3 = matrix_readstr(&fp, end, NULL);

            g_snprintf(key, sizeof(key), "INST:%s::%s(%s)", s1, s2, s3);

            /* Number of following properties to instance */
            count = gwy_get_guint32_le(&fp);
            while (count > 0) {
                gchar *t1 = NULL;
                gchar *t2 = NULL;
                gchar key2[100];

                t1 = matrix_readstr(&fp, end, NULL);
                t2 = matrix_readstr(&fp, end, NULL);
                g_snprintf(key2, sizeof(key2), "%s.%s", key, t1);
                gwy_container_set_string_by_name(meta, key2, (guchar*)t2);
                g_free(t1);
                count--;
            }
            g_free(s1);
            g_free(s2);
            g_free(s3);
        }
    }
    else if (FALSE && gwy_strequal(ident, "CNXS")) {
        // configuration of boards
        // not relevant for correct opening
        guint32 count = 0;
        guint32 i = 0;

        count = gwy_get_guint32_le(&fp);
        for (i = 0; i < count; i++) {
            /* Name and state */
            // read two strings
            // read an int: number of following groups of
            //   two strings
        }
    }
    else if (gwy_strequal(ident, "EEPA")) {
        // configuration of experiment
        // altered values are recorded in PMOD
        // the most important parts are in XYScanner
        gchar *inst, *prop, *unit;
        guint32 a, charlen;
        guint32 gnum;
        gboolean is_xyscanner = FALSE, is_spectroscopy = FALSE, is_regulator = FALSE, handled;
        fp += 4;
        gnum = gwy_get_guint32_le(&fp);

        while (gnum > 0) {
            inst = matrix_readstr(&fp, end, &charlen);
            is_xyscanner = gwy_strequal(inst, "XYScanner");
            is_spectroscopy = gwy_strequal(inst, "Spectroscopy");
            is_regulator = gwy_strequal(inst, "Regulator");
            /* next 4B are number of Group items */
            a = gwy_get_guint32_le(&fp);
            while (a > 0) {
                prop = matrix_readstr(&fp, end, NULL);
                gwy_debug("EEPA::%s::%s", inst, prop);
                unit = matrix_readstr(&fp, end, NULL);
                handled = FALSE;
                if (is_xyscanner)
                    handled = handle_xyscanner_props(&fp, end, matrixdata, meta, ident, inst, prop, unit);
                else if (is_spectroscopy)
                    handled = handle_spectroscopy_props(&fp, end, matrixdata, meta, ident, inst, prop, unit);
                else if (is_regulator)
                    handled = handle_regulator_props(&fp, end, matrixdata, meta, hash, ident, inst, prop, unit);
                if (!handled)
                    matrix_read_meta_value(&fp, end, hash, "/0/meta", meta, ident, inst, prop, unit, TRUE);
                a -= 1;
                g_free(prop);
                g_free(unit);
            } // while a>0
            g_free(inst);
            gnum--;
        } // while gnum > 0
    }
    else if (gwy_strequal(ident, "PMOD")) {
        // modified parameter during scanning
        // Changed configuration of EEPA
        // parametername, unit, value
        gchar *inst, *prop, *unit;
        gboolean is_xyscanner = FALSE, is_spectroscopy = FALSE, is_regulator = FALSE;

        fp += 4;
        // read two strings: instance, property
        inst = matrix_readstr(&fp, end, NULL);
        is_xyscanner = gwy_strequal(inst, "XYScanner");
        is_spectroscopy = gwy_strequal(inst, "Spectroscopy");
        is_regulator = gwy_strequal(inst, "Regulator");
        prop = matrix_readstr(&fp, end, NULL);
        unit = matrix_readstr(&fp, end, NULL);
        gwy_debug("PMOD::%s::%s", inst, prop);
        // Use "EEPA" as the instance.  This is only for metadata and it is
        // less confusing for the user to always see the parameters in EEPA.
        if (is_xyscanner)
            handle_xyscanner_props(&fp, end, matrixdata, meta, ident, "EEPA", prop, unit);
        else if (is_spectroscopy)
            handle_spectroscopy_props(&fp, end, matrixdata, meta, ident, "EEPA", prop, unit);
        else if (is_regulator)
            handle_regulator_props(&fp, end, matrixdata, meta, hash, ident, "EEPA", prop, unit);
        // write to container as well
        matrix_read_meta_value(&fp, end, hash, "/meta/pmod/", meta, ident, inst, prop, unit, TRUE);
        g_free(inst);
        g_free(prop);
        g_free(unit);
    }
    else if (gwy_strequal(ident, "INCI")) {
        // State of Experiment
        // 4B 0x00 and following number
    }
    else if (gwy_strequal(ident, "MARK")) {
        // Calibration of system
        gchar *cal = NULL;
        cal = matrix_readstr(&fp, end, NULL);
        gwy_container_set_string_by_name(meta, "MARK: Calibration", (guchar*)cal);
    }
    else if (gwy_strequal(ident, "VIEW")) {
        // deals with the scanning windows
    }
    else if (gwy_strequal(ident, "PROC")) {
        // Processors of the scanning windows
    }
    else if (gwy_strequal(ident, "BREF")) {
        gchar *filename = NULL;
        const gchar *savedname = NULL;
        // Filename of images
        fp += 4;
        filename = matrix_readstr(&fp, end, NULL);
        savedname = gwy_container_get_string_by_name(hash, "/meta/datafilename");
        gwy_debug("filename <%s> vs <%s>", filename, savedname);
        if (g_str_has_suffix(savedname, filename) || g_str_has_suffix(filename, savedname)) {
            // Image is found
            // the valid values are now in matrixdata
            gwy_debug("data file found");
            matrixdata->state = IMAGE_FOUND;
        }
        g_free(filename);
    }
    else if (gwy_strequal(ident, "CCSY")) {
        // Unknown block
        fp += 4;
        while (fp - *buffer < len) {
            // has inner blocks TCID, SCHC, NACS, REFX
            matrix_scanparamfile(&fp, end, hash, meta, matrixdata);
        }
    }
    else if (gwy_strequal(ident, "DICT")) {
        // description and internal number of captured channels
        // has to be linkend to the physical devices
        // given in XFER to get the scaling
        gchar *s1 = NULL;
        gchar *s2 = NULL;
        guint32 a, number, i;
        // No timestamp, advance 8B
        fp += 8;
        number = gwy_get_guint32_le(&fp);
        for (i = 0; i < number; i++) {
            // whatever the following is
            fp += 16;
            s1 = matrix_readstr(&fp, end, NULL);
            s2 = matrix_readstr(&fp, end, NULL);
            g_free(s1);
            g_free(s2);
        }
        // Number of channels
        number = gwy_get_guint32_le(&fp);
        for (i = 0; i < number; i++) {
            gchar *name = NULL;
            gchar *unit = NULL;
            gchar key[30];
            fp += 4;
            a = gwy_get_guint32_le(&fp);
            fp += 8;
            name = matrix_readstr(&fp, end, NULL);
            unit = matrix_readstr(&fp, end, NULL);
            gwy_debug("channel%u <%s> %s", i, name, unit);
            // store information in GwyContainer
            g_snprintf(key, sizeof(key), "/channels/%u/", a);
            gwy_container_set_string_by_name(hash, sstrconcat(key, "name", NULL), (guchar*)name);
            gwy_container_set_string_by_name(hash, sstrconcat(key, "unit", NULL), (guchar*)unit);
        }
    }
    else if (gwy_strequal(ident, "CHCS")) {
        // header of triangle curves
    }
    else if (gwy_strequal(ident, "SCAN")) {
        // data of triangle curves
    }
    else if (gwy_strequal(ident, "XFER")) {
        // data after triangle curves,
        // these are factors for scaling, given for the physical devices
        guint32 number, i, a;
        while (fp - *buffer < len) {
            gchar *name = NULL;
            gchar *unit = NULL;
            gchar key[30];
            fp += 4;
            number = gwy_get_guint32_le(&fp);
            name = matrix_readstr(&fp, end, NULL);
            g_snprintf(key, sizeof(key), "/channels/%u/tff", number);
            // set string by name requires gchar *key
            gwy_container_set_const_string_by_name(hash, key, name);
            unit = matrix_readstr(&fp, end, NULL);
            a = gwy_get_guint32_le(&fp);
            for (i = 0; i < a; i++) {
                gchar *prop = NULL;
                prop = matrix_readstr(&fp, end, NULL);
                g_snprintf(key, sizeof(key), "/channels/%u/%s", number, prop);
                matrix_read_meta_value(&fp, end, hash, key, NULL, NULL, NULL, NULL, NULL, FALSE);
                g_free(prop);
            }
            g_free(name);
            g_free(unit);
        }
    }
    else if (gwy_strequal(ident, "EOED")) {
        // End of file
        matrixdata->state = FILE_END;
        return 0;
    }
    *buffer += len;
    return 1;
}

/** Find the correct scaling for one channel
 */
static void
matrix_foreach(gpointer key, gpointer value, gpointer data)
{
    const gchar* sval = NULL;
    ValueScaling *zscale = NULL;
    gchar **split = NULL;

    zscale = (ValueScaling*)data;
    if (!G_VALUE_HOLDS(value, G_TYPE_STRING))
        return;

    sval = g_value_get_string(value);
    split = g_strsplit(g_quark_to_string(GPOINTER_TO_UINT(key)), "/", 4);
    if (g_strv_length(split) < 4) {
        g_strfreev(split);
        return;
    }

    /* split[1] = channels,
     * split[2] = number,
     * split[3] = name/unit/factor/offset
     */
    if (gwy_strequal(split[3], "name") && gwy_strequal(zscale->channelname, sval)) {
        // corresponding factor, offset and unit found!
        zscale->cnumber = atoi(split[2]);
    }
    g_strfreev(split);
}

static inline gdouble
get_prefixed_double(GwyContainer *hash, const gchar *prefix, const gchar *key)
{
    return gwy_container_get_double_by_name(hash, sstrconcat(prefix, key, NULL));
}

static inline const gchar*
get_prefixed_string(GwyContainer *hash, const gchar *prefix, const gchar *key)
{
    return gwy_container_get_string_by_name(hash, sstrconcat(prefix, key, NULL));
}

static const gchar*
figure_out_tff(GwyContainer *hash, MatrixData *matrixdata, ValueScaling *zscale,
               const gchar **zunit)
{
    static const GwyEnum tffs[] = {
        { "TFF_Linear1D",      TFF_LINEAR1D,      },
        { "TFF_MultiLinear1D", TFF_MULTILINEAR1D, },
    };

    const gchar *tffname;
    gint tfftype;
    gchar pfx[40];

    zscale->tfftype = TFF_LINEAR1D;
    zscale->factor_1 = zscale->q = 1.0;
    zscale->offset_1 = zscale->z0 = 0.0;
    *zunit = NULL;

    if (!matrixdata->use_paramfile)
        return " (raw)";

    // Get correct scaling factor
    zscale->channelname = matrixdata->channelname;
    zscale->cnumber = G_MAXUINT32;
    // look for correct Z/I/Df/.... scaling
    gwy_container_foreach(hash, "/channels/", matrix_foreach, zscale);
    if (zscale->cnumber == G_MAXUINT32) {
        g_warning("cannot find zscale for channel %s", zscale->channelname);
        return " (raw)";
    }
    g_snprintf(pfx, sizeof(pfx), "/channels/%u/", zscale->cnumber);
    tffname = get_prefixed_string(hash, pfx, "tff");
    if (!tffname) {
        g_warning("cannot find transfer function for channel %s", zscale->channelname);
        return " (raw)";
    }
    gwy_debug("tff type %s", tffname);
    if ((tfftype = gwy_string_to_enum(tffname, tffs, G_N_ELEMENTS(tffs))) != -1)
        zscale->tfftype = tfftype;

    if (tfftype == TFF_LINEAR1D) {
        zscale->factor_1 = get_prefixed_double(hash, pfx, "Factor");
        zscale->offset_1 = get_prefixed_double(hash, pfx, "Offset");
        /* Compactify linear1d: p = (r - n)/f */
        zscale->q = 1.0/zscale->factor_1;
        zscale->z0 = -zscale->offset_1*zscale->q;
    }
    else if (tfftype == TFF_MULTILINEAR1D) {
        zscale->neutralfactor_2 = get_prefixed_double(hash, pfx, "NeutralFactor");
        zscale->offset_2 = get_prefixed_double(hash, pfx, "Offset");
        zscale->prefactor_2 = get_prefixed_double(hash, pfx, "PreFactor");
        zscale->preoffset_2 = get_prefixed_double(hash, pfx, "PreOffset");
        zscale->raw1_2 = get_prefixed_double(hash, pfx, "Raw_1");
        gwy_debug("neutralfactor %g, offset %g, prefactor %g, preoffset %g, raw %g",
                  zscale->neutralfactor_2, zscale->offset_2, zscale->prefactor_2, zscale->preoffset_2, zscale->raw1_2);
        /* Compactify p = (r - n)*(r0 - n0)/(fn * f0) */
        //zscale->q = ((zscale->raw1_2 - zscale->preoffset_2)/(zscale->neutralfactor_2*zscale->prefactor_2));
        zscale->q = ((zscale->raw1_2 - zscale->preoffset_2)/(zscale->neutralfactor_2*zscale->prefactor_2));
        zscale->z0 = -zscale->offset_2*zscale->q;
    }
    else {
        // UNKNOWN Transfer Function is used
        // setting factor to 1.0 to obtain unscaled data
        g_warning("unknown transferfunction, scaling will be wrong");
        return " (raw)";
    }

    *zunit = get_prefixed_string(hash, pfx, "unit");

#ifdef OSNAVERSION
    if (gwy_strequal(zscale->channelname, "Df")) {
        gdouble fac = -1.0/5.464;
        zscale->q *= fac;
        zscale->z0 *= fac;
        return " (x 1/-5.464)";
    }
#endif
    return "";
}

static void
add_field_to_container(GwyContainer *data, GwyContainer *meta,
                       GwyDataField *dfield, const gchar *zunit,
                       gboolean fliph, gboolean flipv,
                       gint *id, const MatrixData *matrixdata,
                       const gchar *basename, const gchar *inverted,
                       const gchar *filename)
{
    GwyContainer *tmpmeta;
    gchar *title;
    GQuark quark;

    if (!dfield)
        return;

    gwy_data_field_invert(dfield, flipv, fliph, FALSE);
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_xy(dfield), "m");
    gwy_si_unit_set_from_string(gwy_data_field_get_si_unit_z(dfield), zunit);

    quark = gwy_app_get_data_key_for_id(*id);
    gwy_container_pass_object(data, quark, dfield);

    title = g_strdup_printf("%u-%u %s %s %s",
                            matrixdata->session, matrixdata->trace, matrixdata->channelname, basename, inverted);
    quark = gwy_app_get_data_title_key_for_id(*id);
    gwy_container_set_string(data, quark, title);

    tmpmeta = gwy_container_duplicate(meta);
    quark = gwy_app_get_data_meta_key_for_id(*id);
    gwy_container_pass_object(data, quark, tmpmeta);

    gwy_file_channel_import_log_add(data, *id, NULL, filename);

    gwy_debug("Image %d saved to container", *id);
    (*id)++;
}

static void
add_brick_to_container(GwyContainer *data, GwyContainer *meta,
                       GwyBrick *brick,
                       gdouble zfrom, gdouble zto,
                       const gchar *zunit, const gchar *wunit,
                       gboolean fliph, gboolean flipv, gboolean flipz,
                       gint *id, const MatrixData *matrixdata,
                       const gchar *basename, const gchar *inverted,
                       const gchar *filename)
{
    GwyContainer *tmpmeta;
    gchar *title;
    GQuark quark;

    if (!brick)
        return;

    gwy_brick_invert(brick, fliph, flipv, (zfrom > zto) ^ flipz, FALSE);
    if (zfrom > zto)
        GWY_SWAP(gdouble, zfrom, zto);
    gwy_brick_set_zreal(brick, zto - zfrom);
    gwy_brick_set_zoffset(brick, zfrom);

    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_x(brick), "m");
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_y(brick), "m");
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_z(brick), zunit);
    gwy_si_unit_set_from_string(gwy_brick_get_si_unit_w(brick), wunit);

    quark = gwy_app_get_brick_key_for_id(*id);
    gwy_container_set_object(data, quark, brick);

    title = g_strdup_printf("%u-%u %s %s %s",
                            matrixdata->session, matrixdata->trace, matrixdata->channelname, basename, inverted);
    quark = gwy_app_get_brick_title_key_for_id(*id);
    gwy_container_set_string(data, quark, title);

    tmpmeta = gwy_container_duplicate(meta);
    quark = gwy_app_get_brick_meta_key_for_id(*id);
    gwy_container_pass_object(data, quark, tmpmeta);

    g_object_unref(brick);

    gwy_file_volume_import_log_add(data, *id, NULL, filename);

    gwy_debug("Brick %d saved to container", *id);
    (*id)++;
}

/* Attempt to determine if we have one image or four by looking at the
 * half-image vertical split.  If there is a large discrepancy then we guess
 * four images. Otherwise we guess one image. */
static gboolean
looks_more_like_4_images(GwyDataField *dfield_tup,
                         GwyDataField **dfield_retup,
                         GwyDataField **dfield_tdown,
                         GwyDataField **dfield_retdown)
{
    guint xres = gwy_data_field_get_xres(dfield_tup);
    guint yres = gwy_data_field_get_yres(dfield_tup);
    gdouble udiv = 0.0, mdiv = 0.0, ddiv = 0.0;
    gdouble dx, dy;
    const gdouble *data, *rowuu, *rowu, *rowdd, *rowd;
    gdouble *dretup, *dtdown, *dretdown;
    guint i, j;

    if (yres < 16 || (xres & 1) || (yres & 1))
        return FALSE;

    data = gwy_data_field_get_data_const(dfield_tup);
    rowd = data + xres*(yres/2);
    rowu = rowd - xres;
    rowuu = rowu - xres;
    rowdd = rowd + xres;

    for (j = 0; j < xres; j++) {
        gdouble u = rowuu[j] - rowu[j];
        gdouble m = rowu[j] - rowd[j];
        gdouble d = rowd[j] - rowdd[j];

        udiv += u*u;
        mdiv += m*m;
        ddiv += d*d;
    }

    gwy_debug("mdiv %g, udiv %g, ddiv %g", mdiv, udiv, ddiv);
    /* Give it the benefit of doubt and only split to four images if the
     * difference is at least 3 times larger than for the neighbour rows. */
    if (mdiv < 1.5*(udiv + ddiv))
        return FALSE;

    dx = gwy_data_field_get_dx(dfield_tup);
    dy = gwy_data_field_get_dy(dfield_tup);
    *dfield_retup = gwy_data_field_new(xres/2, yres/2, dx*xres/2, dy*yres/2, FALSE);
    gwy_data_field_copy_units(dfield_tup, *dfield_retup);
    *dfield_tdown = gwy_data_field_new_alike(*dfield_retup, FALSE);
    *dfield_retdown = gwy_data_field_new_alike(*dfield_retup, FALSE);

    dretup = gwy_data_field_get_data(*dfield_retup);
    dtdown = gwy_data_field_get_data(*dfield_tdown);
    dretdown = gwy_data_field_get_data(*dfield_retdown);

    for (i = 0; i < yres/2; i++) {
        for (j = 0; j < xres/2; j++) {
            dretup[i*xres/2 + j] = data[i*xres + xres/2 + j];
            dtdown[i*xres/2 + j] = data[(i + yres/2)*xres + j];
            dretdown[i*xres/2 + j] = data[(i + yres/2)*xres + xres/2 + j];
        }
    }

    gwy_data_field_resize(dfield_tup, 0, 0, xres/2, yres/2);

    return TRUE;
}

static gsize
read_block(const guchar **fp, gsize *avail, gdouble *dest, gsize n, gdouble q, gdouble z0)
{
    gsize toread = MIN(n, *avail);

    gwy_convert_raw_data(*fp, toread, 1, GWY_RAW_DATA_SINT32, GWY_BYTE_ORDER_LITTLE_ENDIAN, dest, q, z0);
    *avail -= toread;
    *fp += toread * sizeof(gint32);
    return toread;
}

/*
 * Data are stored in acquisition order, i.e. from outer to inner:
 * Up/Down | scanline(row), Trace/Retrace, point(column)
 * This function handles the part from | to the right.
 */
static void
read_image_data(GwyDataField *trace_field, GwyDataField *retrace_field,
                const guchar **fp, gsize *avail,
                gdouble q, gdouble z0)
{
    gint xres = gwy_data_field_get_xres(trace_field);
    gint yres = gwy_data_field_get_yres(trace_field);
    gdouble *dt = gwy_data_field_get_data(trace_field);
    gdouble *dr = retrace_field ? gwy_data_field_get_data(retrace_field) : NULL;
    gint i;

    for (i = 0; *avail && i < yres; i++) {
        read_block(fp, avail, dt + i*xres, xres, q, z0);
        if (dr)
            read_block(fp, avail, dr + i*xres, xres, q, z0);
    }
}

// Extract image data from the data file
static void
create_image_data(GwyContainer *data, GwyContainer *meta, GwyContainer *hash,
                  const guchar **fp, const guchar *end,
                  MatrixData *matrixdata, const gchar *filename)
{
    // GwyDataField for TraceUp, ReTraceUp, TraceDown, ReTraceDown
    GwyDataField *dfield_tup = NULL, *dfield_retup = NULL, *dfield_tdown = NULL, *dfield_retdown = NULL;
    guint32 xres, yres, i, mult;
    gsize avail, intend;
    gdouble width, height;
    gboolean guess_sizes = FALSE, x_retrace = TRUE, y_retrace = TRUE;
    const gchar *zunit;
    ValueScaling zscale;
    const gchar *inverted;

    intend = matrixdata->proc_intended_no;
    avail = MIN(matrixdata->proc_available_no, intend);
    if (end - *fp < avail*sizeof(guint32)) {
        g_warning("captured number of points does not fit in the file");
        avail = (end - *fp)/sizeof(guint32);
    }

    if (matrixdata->use_paramfile) {
        xres = matrixdata->xpoints;
        yres = matrixdata->ypoints;
        width = matrixdata->width/(gdouble)matrixdata->zoom;
        height = matrixdata->height/(gdouble)matrixdata->zoom;
        x_retrace = matrixdata->x_retrace;
        y_retrace = matrixdata->y_retrace;
        mult = (x_retrace ? 2 : 1)*(y_retrace ? 2 : 1);
        /* We cannot reduce according to avail, because smaller number of values can occur due to interrupted
         * measurements.  I think. But if xres and yres are too large for the intended points something is wrong.  Try
         * to work around by using estimation. We already ensured available ≤ intended. */
        gwy_debug("proc_available_no %u", matrixdata->proc_available_no);
        gwy_debug("proc_intended_no %lu", (gulong)intend);
        gwy_debug("xres %u, yres %u, mult %u -> %lu", xres, yres, mult, (gulong)xres*yres*mult);
        if (xres*yres*mult > intend) {
            g_warning("intended number of points too small for the pixel sizes, guessing sizes");
            guess_sizes = TRUE;
        }
    }
    else {
        guess_sizes = TRUE;
        width = height = 1.0;
        mult = 4;
        g_warning("no parameter file: image sizes are probably incorrect");
    }

    if (guess_sizes) {
        xres = (guint32)floor(sqrt(intend/mult) + 0.1);
        yres = (guint32)floor(sqrt(intend/mult) + 0.1);
        /* Try to make xres*yres match the intended number of points exactly. */
        if (xres*yres != intend*mult) {
            guint newxres = xres, newyres = yres, newmult = mult;
            if (mult == 4 || mult == 1) {
                newmult = 2;
                newxres = (guint32)floor(sqrt(intend/newmult) + 0.1);
                newyres = (guint32)floor(sqrt(intend/newmult) + 0.1);
            }
            if (mult == 2) {
                /* We do not know if there should be one image or four.
                 * Try to deal with it later. */
                newmult = 1;
                newxres = (guint32)floor(sqrt(intend/newmult) + 0.1);
                newyres = (guint32)floor(sqrt(intend/newmult) + 0.1);
            }
            if (newxres*newyres*newmult == intend) {
                xres = newxres;
                yres = newyres;
                mult = newmult;
                if (newmult == 1)
                    x_retrace = y_retrace = FALSE;
                else if (newmult == 2) {
                    x_retrace = TRUE;
                    y_retrace = FALSE;
                }
                else
                    x_retrace = y_retrace = TRUE;
            }
        }
    }

    inverted = figure_out_tff(hash, matrixdata, &zscale, &zunit);

    gwy_debug("loading image data");
    dfield_tup = gwy_data_field_new(xres, yres, width, height, TRUE);
    if (x_retrace)
        dfield_retup = gwy_data_field_new_alike(dfield_tup, TRUE);
    read_image_data(dfield_tup, dfield_retup, fp, &avail, zscale.q, zscale.z0);
    if (y_retrace) {
        dfield_tdown = gwy_data_field_new_alike(dfield_tup, TRUE);
        if (x_retrace)
            dfield_retdown = gwy_data_field_new_alike(dfield_tup, TRUE);
        read_image_data(dfield_tdown, dfield_retdown, fp, &avail, zscale.q, zscale.z0);
    }
    if (!x_retrace && !y_retrace && guess_sizes)
        looks_more_like_4_images(dfield_tup, &dfield_retup, &dfield_tdown, &dfield_retdown);

    i = 0;
    add_field_to_container(data, meta, dfield_tup, zunit, FALSE, TRUE, &i,
                           matrixdata, "TraceUp", inverted, filename);
    add_field_to_container(data, meta, dfield_retup, zunit, TRUE, TRUE, &i,
                           matrixdata, "RetraceUp", inverted, filename);
    add_field_to_container(data, meta, dfield_tdown, zunit, FALSE, FALSE, &i,
                           matrixdata, "TraceDown", inverted, filename);
    add_field_to_container(data, meta, dfield_retdown, zunit, TRUE, FALSE, &i,
                           matrixdata, "RetraceDown", inverted, filename);

    gwy_debug("Data successfully read");
}

// Create SPS data from the data file
static void
create_spectra_graph(GwyContainer *data, GwyContainer *hash,
                     const guchar **fp, const guchar *end,
                     MatrixData *matrixdata)
{
    gdouble *xdata, *ydata;
    guint32 i, j, res, n, ncurves;
    gdouble zfrom, zto;
    gsize avail;
    const gchar *xunit, *yunit;
    GwySIUnit *siunitx, *siunity;
    ValueScaling yscale;
    GwyGraphModel *gmodel;
    GwyGraphCurveModel *gcmodel;
    GQuark quark;
    const gchar *description;
    gchar *title;
    const gchar *inverted;

    g_return_if_fail(matrixdata->use_paramfile);

    res = matrixdata->zpoints;
    zfrom = matrixdata->zfrom;
    zto = matrixdata->zto;
    xunit = matrixdata->rampunit;
    g_return_if_fail(res >= 1);

    gwy_debug("Dev1 ramp reversal: %d, Dev2: %d", matrixdata->dev1_ramp_reversal, matrixdata->dev2_ramp_reversal);
    avail = matrixdata->proc_available_no;
    if (end - *fp < avail*sizeof(guint32)) {
        g_warning("captured number of points does not fit in the file");
        avail = (end - *fp)/sizeof(guint32);
    }

    inverted = figure_out_tff(hash, matrixdata, &yscale, &yunit);
    /* There are two preamplifier settings for current.  There are probably multiple settings for other things… */
    if (matrixdata->preamp_range > 0.0 && gwy_strequal(yunit, "A")) {
        yscale.q *= matrixdata->preamp_range/3.33e-07;
        yscale.z0 *= matrixdata->preamp_range/3.33e-07;
    }

    gwy_debug("loading single point spectra data");
    gmodel = gwy_graph_model_new();
    title = g_strdup_printf("%u-%u %s %s", matrixdata->session, matrixdata->trace, matrixdata->channelname, inverted);
    siunitx = gwy_si_unit_new(xunit);
    siunity = gwy_si_unit_new(yunit);
    g_object_set(gmodel,
                 "title", title,
                 "si-unit-x", siunitx,
                 "si-unit-y", siunity,
                 "axis-label-bottom", matrixdata->spectrum_x_axis,
                 "axis-label-left", matrixdata->spectrum_y_axis,
                 NULL);
    g_free(title);
    g_object_unref(siunitx);
    g_object_unref(siunity);

    xdata = g_new(gdouble, res);
    ydata = g_new0(gdouble, res);
    for (i = 0; i < res; i++)
        xdata[i] = zfrom + (zto - zfrom)*(i + 0.5)/res;

    ncurves = matrixdata->dev1_ramp_reversal ? 2 : 1;
    for (i = 0; i < ncurves && avail; i++) {
        n = read_block(fp, &avail, ydata, res, yscale.q, yscale.z0);
        gcmodel = gwy_graph_curve_model_new();
        gwy_graph_curve_model_set_data(gcmodel, xdata, ydata, n);
        gwy_graph_curve_model_enforce_order(gcmodel);

        /* FIXME */
        description = ((i || ncurves == 1) ? "RampUp" : "RampDown");
        g_object_set(gcmodel,
                     "mode", GWY_GRAPH_CURVE_LINE,
                     "description", description,
                     "color", gwy_graph_get_preset_color(i),
                     NULL);
        gwy_graph_model_add_curve(gmodel, gcmodel);
        g_object_unref(gcmodel);

        for (j = 0; j < res/2; j++) {
            GWY_SWAP(gdouble, xdata[j], xdata[res-1-j]);
        }
    }

    g_free(xdata);
    g_free(ydata);

    quark = gwy_app_get_graph_key_for_id(0);
    gwy_container_pass_object(data, quark, gmodel);

    gwy_debug("Data successfully read");
}

/*
 * Data are stored in acquisition order, i.e. from outer to inner:
 * Up/Down | scanline(row), Trace/Retrace, point(column), Approach/Retract, spectrum-point(level)
 * This function handles the part from | to the right.
 */
static void
read_volume_data(GwyBrick *trace_brick, GwyBrick *trace_rbrick, GwyBrick *retrace_brick, GwyBrick *retrace_rbrick,
                 const guchar **fp, gsize *avail,
                 gdouble q, gdouble z0)
{
    gint xres = gwy_brick_get_xres(trace_brick);
    gint yres = gwy_brick_get_yres(trace_brick);
    gint zres = gwy_brick_get_zres(trace_brick);
    gdouble *ft = gwy_brick_get_data(trace_brick);
    gdouble *fr = retrace_brick ? gwy_brick_get_data(retrace_brick) : NULL;
    gdouble *rt = trace_rbrick ? gwy_brick_get_data(trace_rbrick) : NULL;
    gdouble *rr = retrace_rbrick ? gwy_brick_get_data(retrace_rbrick) : NULL;
    gdouble *d, *buf = g_new(gdouble, zres);
    gsize toread;
    gint i, j, k, n = xres*yres;

    for (i = 0; *avail && i < yres; i++) {
        for (j = 0; *avail && j < xres; j++) {
            toread = read_block(fp, avail, buf, zres, q, z0);
            d = ft + i*xres + j;
            for (k = 0; k < toread; k++)
                d[n*k] = buf[k];

            if (rt) {
                toread = read_block(fp, avail, buf, zres, q, z0);
                d = rt + i*xres + j;
                for (k = 0; k < toread; k++)
                    d[n*k] = buf[k];
            }
        }
        if (!fr)
            continue;
        for (j = 0; *avail && j < xres; j++) {
            toread = read_block(fp, avail, buf, zres, q, z0);
            d = fr + i*xres + j;
            for (k = 0; k < toread; k++)
                d[n*k] = buf[k];

            if (rr) {
                toread = read_block(fp, avail, buf, zres, q, z0);
                d = rr + i*xres + j;
                for (k = 0; k < toread; k++)
                    d[n*k] = buf[k];
            }
        }
    }
    g_free(buf);
}

// Extract volume spectroscopy data from the data file
static void
create_volume_data(GwyContainer *data, GwyContainer *meta, GwyContainer *hash,
                   const guchar **fp, const guchar *end,
                   MatrixData *matrixdata, const gchar *filename)
{
    // GwyBrick for TraceUp, ReTraceUp, TraceDown, ReTraceDown
    GwyBrick *brick_tup = NULL, *brick_retup = NULL, *brick_tdown = NULL, *brick_retdown = NULL;
    // The same for reverse ramp direction.
    GwyBrick *rbrick_tup = NULL, *rbrick_retup = NULL, *rbrick_tdown = NULL, *rbrick_retdown = NULL;
    guint32 xres, yres, zres, mult;
    gsize avail, intend;
    gdouble width, height, zfrom, zto;
    const gchar *zunit, *wunit;
    gboolean x_retrace, y_retrace, ramp_rev;
    ValueScaling wscale;
    const gchar *inverted;
    gint i;

    if (!matrixdata->use_paramfile) {
        g_warning("no parameter file: cannot load spectroscopy");
        return;
    }
    intend = matrixdata->proc_intended_no;
    avail = MIN(matrixdata->proc_available_no, intend);
    gwy_debug("proc_available_no %u", matrixdata->proc_available_no);
    gwy_debug("proc_intended_no %lu", (gulong)intend);

    xres = matrixdata->xpoints;
    yres = matrixdata->ypoints;
    zres = matrixdata->zpoints;
    if (zres < 1) {
        g_warning("no zpoints, cannot load as spectra");
        return;
    }
    /* FIXME: I do not know how to tell for sure if we have volume spectroscopy or just single curves.  Use
     * a heuristic. */
    if ((xres == 1 && yres == 1) || avail <= 2*zres) {
        create_spectra_graph(data, hash, fp, end, matrixdata);
        return;
    }

    width = matrixdata->width/(gdouble)matrixdata->zoom;
    height = matrixdata->height/(gdouble)matrixdata->zoom;
    x_retrace = matrixdata->x_retrace;
    y_retrace = matrixdata->y_retrace;
    zfrom = matrixdata->zfrom;
    zto = matrixdata->zto;
    zunit = matrixdata->rampunit;
    ramp_rev = matrixdata->dev1_ramp_reversal;
    /* XXX: We ignore subgrid_enabled for now.  Apparently it might be FALSE even when there are subgrids in use.
     * The subgrid resolution formula is not a simple integer division; we need to round up.  For instance for
     * resolution 10 and subgrid 3, we have subgrid samples at indices 0, 3, 6 and 9, i.e. there are four (not 3). */
    if (matrixdata->subgrid_x > 1)
        xres = (xres + matrixdata->subgrid_x-1)/matrixdata->subgrid_x;
    if (matrixdata->subgrid_y > 1)
        yres = (yres + matrixdata->subgrid_y-1)/matrixdata->subgrid_y;
    /* FIXME: If subgrid_match = 2 we probably have just the retrace brick and should flip it. */
    if (matrixdata->subgrid_x > 1 && matrixdata->subgrid_match)
        x_retrace = FALSE;

    mult = (x_retrace ? 2 : 1)*(y_retrace ? 2 : 1)*(ramp_rev ? 2 : 1);
    gwy_debug("mult %u", mult);
    if (xres*yres*zres*mult > intend) {
        g_warning("intended number of points too small for the pixel sizes, guessing sizes");
        if (xres*yres*zres*mult == 2*intend) {
            if (x_retrace)
                x_retrace = FALSE;
            else if (y_retrace)
                y_retrace = FALSE;
            mult /= 2;
        }
    }

    gwy_debug("x_retrace: %d, y_retrace: %d", x_retrace, y_retrace);
    gwy_debug("Dev1 ramp reversal: %d, Dev2: %d", ramp_rev, matrixdata->dev2_ramp_reversal);
    gwy_debug("preamp_range %g", matrixdata->preamp_range);
    gwy_debug("brick %dx%dx%d = %d", xres, yres, zres, xres*yres*zres);

    if (end - *fp < avail*sizeof(guint32)) {
        g_warning("captured number of points does not fit in the file");
        avail = (end - *fp)/sizeof(guint32);
    }

    inverted = figure_out_tff(hash, matrixdata, &wscale, &wunit);
    /* There are two preamplifier settings for current.  There are probably multiple settings for other things… */
    if (matrixdata->preamp_range > 0.0 && gwy_strequal(wunit, "A")) {
        wscale.q *= matrixdata->preamp_range/3.33e-07;
        wscale.z0 *= matrixdata->preamp_range/3.33e-07;
    }

    gwy_debug("loading volume spectra data");
    /* Do not bother with real z range, we have to fix it later anyway. */
    brick_tup = gwy_brick_new(xres, yres, zres, width, height, 1.0, TRUE);
    if (ramp_rev)
        rbrick_tup = gwy_brick_new_alike(brick_tup, TRUE);
    if (x_retrace) {
        brick_retup = gwy_brick_new_alike(brick_tup, TRUE);
        if (ramp_rev)
            rbrick_retup = gwy_brick_new_alike(brick_tup, TRUE);
    }
    read_volume_data(brick_tup, rbrick_tup, brick_retup, rbrick_retup, fp, &avail, wscale.q, wscale.z0);
    if (y_retrace) {
        brick_tdown = gwy_brick_new_alike(brick_tup, TRUE);
        if (ramp_rev)
            rbrick_tdown = gwy_brick_new_alike(brick_tup, TRUE);
        if (x_retrace) {
            brick_retdown = gwy_brick_new_alike(brick_tup, TRUE);
            if (ramp_rev)
                rbrick_retdown = gwy_brick_new_alike(brick_tup, TRUE);
        }
        read_volume_data(brick_tdown, rbrick_tdown, brick_retdown, rbrick_retdown, fp, &avail, wscale.q, wscale.z0);
    }

    i = 0;
    add_brick_to_container(data, meta, brick_tup, zfrom, zto, zunit, wunit, FALSE, TRUE, FALSE, &i,
                           matrixdata, "TraceUp", inverted, filename);
    add_brick_to_container(data, meta, rbrick_tup, zfrom, zto, zunit, wunit, FALSE, TRUE, TRUE, &i,
                           matrixdata, "TraceUpBack", inverted, filename);
    add_brick_to_container(data, meta, brick_retup, zfrom, zto, zunit, wunit, TRUE, TRUE, FALSE, &i,
                           matrixdata, "RetraceUp", inverted, filename);
    add_brick_to_container(data, meta, rbrick_retup, zfrom, zto, zunit, wunit, TRUE, TRUE, TRUE, &i,
                           matrixdata, "RetraceUpBack", inverted, filename);
    add_brick_to_container(data, meta, brick_tdown, zfrom, zto, zunit, wunit, FALSE, FALSE, FALSE, &i,
                           matrixdata, "TraceDown", inverted, filename);
    add_brick_to_container(data, meta, rbrick_tdown, zfrom, zto, zunit, wunit, FALSE, FALSE, TRUE, &i,
                           matrixdata, "TraceDownBack", inverted, filename);
    add_brick_to_container(data, meta, brick_retdown, zfrom, zto, zunit, wunit, TRUE, FALSE, FALSE, &i,
                           matrixdata, "RetraceDown", inverted, filename);
    add_brick_to_container(data, meta, rbrick_retdown, zfrom, zto, zunit, wunit, TRUE, FALSE, TRUE, &i,
                           matrixdata, "RetraceDownBack", inverted, filename);

    gwy_debug("Data successfully read");
}

/** scandatafile
  * reads an OMICRON data/image file
  */
static gboolean
matrix_scandatafile(const guchar **fp, const guchar *end,
                    const gchar *filename,
                    GwyContainer *container,
                    GwyContainer *meta,
                    GwyContainer *hash,
                    MatrixData *matrixdata,
                    gint depth)
{
    gchar ident[5];
    guint32 len;

    gwy_debug("fp = %p, end = %p, remaining = %ld", *fp, end, (glong)(end - *fp));

    if (!read_ident(fp, end, ident))
        return FALSE;
    if (end - *fp < sizeof(guint32))
        return FALSE;
    len = gwy_get_guint32_le(fp);
    gwy_debug("omicronmatrix::matrix_scandatafile[%d]: %s, length: %d", depth, ident, len);

    if (matrixdata->xpoints == 0 || matrixdata->ypoints == 0) {
        // parameters are not correct. Use those from the image file
        matrixdata->use_paramfile = FALSE;
    }

    if (gwy_strequal(ident, "BKLT")) {
        // ImageFile
        // next 8B: timestamp
        gchar times[40];
        guint64 date = gwy_get_guint64_le(fp);
        time_t timestamp = date;
        struct tm *sdate = localtime(&timestamp);
        strftime(times, sizeof(times), "%H:%M:%S %d.%m.%Y", sdate);
        //g_snprintf(times, sizeof(times), "%i", date);
        gwy_container_set_string_by_name(meta, "Image ended at", (guchar*)g_strdup(times));
        len += 8;
        *fp += 4;
        while (matrix_scandatafile(fp, end, filename, container, meta, hash, matrixdata, depth+1)) {
            gwy_debug("next data[%u]", depth); // scans data file
        }
    }
    else if (gwy_strequal(ident, "DESC")) {
        // headerdata
        // the next 20 B are unknown
        *fp += 20;
        // intended number of points
        matrixdata->proc_intended_no = gwy_get_guint32_le(fp);
        // captured number of points
        matrixdata->proc_available_no = gwy_get_guint32_le(fp);
        *fp += len - (20 + 4 + 4);
    }
    else if (gwy_strequal(ident, "DATA")) {
        if (matrixdata->spectrum_y_axis) {
            /* It can also create SPS when it thinks the data are not volume spectroscopy data. */
            create_volume_data(container, meta, hash, fp, end, matrixdata, filename);
        }
        else {
            create_image_data(container, meta, hash, fp, end, matrixdata, filename);
        }
    }
    else if (!*ident) {
        /* Empty block identifier seems to occur commonly at the end of data.
         * Do not warn about it. */
        gwy_debug("empty block ident[%u]", depth);
        return FALSE;
    }
    else {
        // Block identifier is unknown, perhaps the fileend is reached
        g_warning("omicronmatrix::matrix_scandatafile[%d]: Block identifier <%s> unknown", depth, ident);
        return FALSE;
    }
    return TRUE;
}

/* Split the file name at the last --.  It seems files created by version 4 can
 * have two -- and the file name prefix is the part up to the last one. */
static gchar**
split_file_name(const gchar *filename)
{
    const gchar *p = g_strrstr(filename, "--");
    gchar **retval = g_new0(gchar*, 3);

    if (p) {
        retval[0] = g_strndup(filename, p-filename);
        retval[1] = g_strdup(p+2);
    }
    else
        retval[0] = g_strdup(filename);

    return retval;
}

/* Check if channel name looks like Blah3(V).  These should be spectra. */
static void
looks_like_spectroscopy(const gchar *channelname,
                        gchar **xaxis, gchar **yaxis)
{
    const gchar *p = channelname, *q;

    *xaxis = *yaxis = NULL;

    while (g_ascii_isalpha(*p))
        p++;
    while (g_ascii_isdigit(*p))
        p++;
    if (*p != '(')
        return;
    q = p+1;
    while (g_ascii_isalpha(*q))
        q++;
    if (*q != ')')
        return;
    q++;
    if (*q)
        return;

    *xaxis = g_strndup(p+1, q-p-2);
    *yaxis = g_strndup(channelname, p-channelname);
}

/* Load a single data file.  For correct sizes and scaling the corresponding
 * parameter file is needed.  This is not how we normally do things; preferably
 * the user selects the parameter file and we load all data it refers to.
 * But there are some provisions for loading data without the parameter file
 * so preserve this possibility.  */
static GwyContainer*
matrix_load(const gchar *filename,
            G_GNUC_UNUSED GwyRunType mode,
            GError **error)
{
    GwyContainer *container = NULL, *meta = NULL, *hash = NULL;
    guchar *imgbuffer = NULL;
    guchar *parbuffer = NULL;
    const guchar *fp = NULL;
    GError *err = NULL;
    gsize imgsize, parsize;
    MatrixData matrixdata;
    gchar **fsplit = NULL;
    gchar **ifsplit1 = NULL;
    gchar **ifsplit2 = NULL;
    gchar *lastpart = NULL;
    gchar *paramfilename = NULL;
    const gchar *delimiter = ".";
    gchar newdelimiter = '_';

    // Some default values
    gwy_clear(&matrixdata, 1);
    matrixdata.rastertime = 1.0;
    matrixdata.zoom = 1;
    matrixdata.width = matrixdata.height = 1.0;
    // TODO: correct error-management

    /* start with the image file */
    if (!gwy_file_get_contents(filename, &imgbuffer, &imgsize, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    if (imgsize < IMGFILEIDENT_SIZE
        || memcmp(imgbuffer, IMGFILEIDENT, IMGFILEIDENT_SIZE) != 0) {
        err_FILE_TYPE(error, "Omicron Matrix");
        gwy_file_abandon_contents(imgbuffer, imgsize, NULL);
        return NULL;
    }
    /******* Image file is existing and seems to be valid, ********/
    gwy_debug("Now check parameter file: %s", filename);

    /* now check parameter file to get correct sizes */
    fsplit = split_file_name(filename);
    if (g_strv_length(fsplit) == 2) {
        paramfilename = g_strconcat(*fsplit, "_0001.mtrx", NULL);
        matrixdata.use_paramfile = TRUE;
    }

    if (matrixdata.use_paramfile
        && !gwy_file_get_contents(paramfilename, &parbuffer, &parsize, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        g_clear_error(&err);
        matrixdata.use_paramfile = FALSE;
        g_warning("omicronmatrix: Cannot open parameter file: %s", paramfilename);
    }
    if (matrixdata.use_paramfile && parsize >= PARFILEIDENT_SIZE
        && memcmp(parbuffer, PARFILEIDENT, PARFILEIDENT_SIZE) != 0) {
        gwy_file_abandon_contents(parbuffer, parsize, NULL);
        matrixdata.use_paramfile = FALSE;
        g_warning("omicronmatrix: Cannot read parameter file: %s", paramfilename);
    }
    /******** Parameter file is existing and seems to be valid *****/

    gwy_debug("omicronmatrix: parameter file: %s", paramfilename);
    container = gwy_container_new();
    meta = gwy_container_new();
    /* Use a GwyContainer also for various auxiliary information. */
    hash = gwy_container_new();

    if (g_strv_length(fsplit) == 2) {
        /* Parse image filename to obtain numbers and channel
           default_.....--1_1.Df_mtrx
           (*fsplit)    (*fsplit+1)    */
        // Convert necessary due to differences in MATRIX V1.0 and V2.1
        lastpart = g_strdelimit(fsplit[1], delimiter, newdelimiter);
        ifsplit1 = g_strsplit(lastpart, "_", 4);
        /* sess_trace_channel_mtrx
           0    1     2      3    */
        matrixdata.session = (guint32)g_strtod(ifsplit1[0], NULL);
        matrixdata.trace   = (guint32)g_strtod(ifsplit1[1], NULL);
        matrixdata.channelname = g_strdup(ifsplit1[2]);
        gwy_debug("omicronmatrix::matrix_load channel: %s", matrixdata.channelname);
        looks_like_spectroscopy(ifsplit1[2], &matrixdata.spectrum_x_axis, &matrixdata.spectrum_y_axis);
        gwy_debug("omicronmatrix::matrix_load channel %s like spectroscopy",
                  matrixdata.spectrum_x_axis ? "looks" : "does not look");
    }
    else {
        g_warning("omicronmatrix::matrix_load: cannot parse image filename");
        matrixdata.session = 0;
        matrixdata.trace   = 0;
        matrixdata.channelname = g_strdup("unknown");
    }

    gwy_debug("omicronmatrix::matrix_load: Try loading parameter file, if available.");
    if (matrixdata.use_paramfile) {
        // parameter file seems to be valid
        fp = parbuffer + FILEIDENT_SIZE;
        gwy_container_set_const_string_by_name(hash, "/meta/datafilename", filename);
        gwy_debug("omicronmatrix::matrix_load Scanning parameterfile");
        while (fp < parbuffer + parsize
               && matrix_scanparamfile(&fp, parbuffer + parsize, hash, meta, &matrixdata))
            ;
    }
    else {
        // parameterfile is invalid, open the images with arb units
        g_warning("omicronmatrix::matrix_load: The lateral sizes are incorrect, parameterfile is not available.");
        // get xpoints, ypoints via scan_image!
    }

    matrixdata.proc_cur_img_no = 0;
    fp = imgbuffer + FILEIDENT_SIZE;

    // Scan the imagefile. Store to the file container.
    gwy_debug("omicronmatrix::matrix_load: starting the image scan loop.");
    matrix_scandatafile(&fp, imgbuffer + imgsize, filename, container, meta, hash, &matrixdata, 1);

    gwy_debug("omicronmatrix::matrix_load Ending...");
    if (parbuffer)
        gwy_file_abandon_contents(parbuffer, parsize, NULL);
    gwy_file_abandon_contents(imgbuffer, imgsize, NULL);
    g_free(paramfilename);
    g_strfreev(fsplit);
    g_strfreev(ifsplit1);
    g_strfreev(ifsplit2);
    g_free(matrixdata.channelname);
    g_free(matrixdata.rampunit);
    g_free(matrixdata.spectrum_x_axis);
    g_free(matrixdata.spectrum_y_axis);
    g_object_unref(meta);
    g_object_unref(hash);
    sstrconcat(NULL);

    if (!gwy_container_get_n_items(container)) {
        GWY_OBJECT_UNREF(container);
        /* This is lame but we are not sure what is the primary problem. */
        err_NO_DATA(error);
    }

    return container;
}

/* Strconcat using static storage. */
static const gchar*
sstrconcat(const gchar *s, ...)
{
    static GString *str = NULL;
    va_list ap;

    if (!s) {
        if (str) {
            g_string_free(str, TRUE);
            str = NULL;
        }
        return NULL;
    }

    if (!str)
        str = g_string_new(NULL);

    g_string_assign(str, s);
    va_start(ap, s);

    while ((s = va_arg(ap, const gchar*)))
        g_string_append(str, s);

    return str->str;
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
