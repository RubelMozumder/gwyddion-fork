/*
 *  $Id: formats3d.c 25690 2023-09-21 08:05:30Z yeti-dn $
 *  Copyright (C) 2009-2020 David Necas (Yeti).
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
 * VTK structured grid file
 * .vtk
 * Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * PLY 3D Polygon File Format
 * .ply
 * Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Wavefront OBJ 3D geometry
 * .obj
 * Read Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Object File Format 3D geometry
 * .off
 * Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * Stereolitography STL 3D geometry (binary)
 * .stl
 * Read Export
 **/

/**
 * [FILE-MAGIC-USERGUIDE]
 * XYZ data
 * .xyz, .dat
 * Read Export
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <math.h>
#include <string.h>
#include <glib/gstdio.h>
#include <libgwyddion/gwymacros.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwyradiobuttons.h>
#include <libgwydgets/gwydgetutils.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"

/* Export params. */
enum {
    PARAM_ZSCALE_TYPE,
    PARAM_ZSCALE,
    PARAM_TRIANG_TYPE,
    PARAM_SWAP_XY,
    PARAM_FLIP_Z,
};

/* Import params. */
enum {
    PARAM_XY_UNITS,
    PARAM_Z_UNITS,
    PARAM_DO_RASTERISE,

    LABEL_NPOINTS,
    LABEL_XRANGE,
    LABEL_YRANGE,
    LABEL_ZRANGE,
};

typedef enum {
    TRIANGULATION_NONE = 0,    /* Only vertices, if the format supports it. */
    TRIANGULATION_PLAIN,
    TRIANGULATION_MIDPOINT,
    TRIANGULATION_RANDOM,
    TRIANGULATION_FOLLOW,
    NTRIANGULATIONS
} TriangulationType;

typedef enum {
    ZSCALE_USER = 0,
    ZSCALE_PHYSICAL,
    ZSCALE_AUTO,
    NZSCALES
} ZScaleType;

typedef struct {
    guint a;
    guint b;
    guint c;
} TriangleIndices;

typedef struct {
    gdouble dx;             /* Pixel x-length; 1 for square pixels. */
    gdouble dy;             /* Pixel y-length; 1 for square pixels. */
    gdouble zscale_1_1;     /* Z scale we consider 1:1. */
    gdouble zscale_auto;    /* Scale to get reasonable z vs. x and y. */
} ScalingInfo;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    /* Cached input data properties. */
    ScalingInfo scinfo;
    const guchar *title;
} ExportArgs;

typedef struct {
    GwyParams *params;
    GwySurface *surface;
    GwyDataField *image;
} ImportArgs;

typedef gboolean (*Export3DFunc)(FILE *fh,
                                 GArray *vertices,
                                 GArray *triangles,
                                 ExportArgs *args);
typedef GwySurface* (*Import3DFunc)(guchar *buffer,
                                    gsize size,
                                    GError **error);

typedef struct {
    ExportArgs *args;
    GwyParamTable *table;
    GtkWidget *dialog;
    const ScalingInfo *scinfo;
} ExportGUI;

typedef struct {
    ImportArgs *args;
    GwyParamTable *table;
    GtkWidget *dialog;
} ImportGUI;

typedef struct {
    const gchar *name;
    const gchar *title;
    const gchar *description;
    GwyFileDetectFunc detect;
    Export3DFunc export_;
    Import3DFunc import;
    gboolean has_triang_none;
    gboolean is_binary;
} Format3D;

static gboolean         module_register       (void);
static gint             detect3d_vtk          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static gboolean         export3d_vtk          (FILE *fh,
                                               GArray *vertices,
                                               GArray *triangles,
                                               ExportArgs *args);
static gint             detect3d_ply          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static gboolean         export3d_ply          (FILE *fh,
                                               GArray *vertices,
                                               GArray *triangles,
                                               ExportArgs *args);
static gint             detect3d_obj          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static gboolean         export3d_obj          (FILE *fh,
                                               GArray *vertices,
                                               GArray *triangles,
                                               ExportArgs *args);
static GwySurface*      import3d_obj          (guchar *buffer,
                                               gsize size,
                                               GError **error);
static gint             detect3d_off          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static gboolean         export3d_off          (FILE *fh,
                                               GArray *vertices,
                                               GArray *triangles,
                                               ExportArgs *args);
static gint             detect3d_stl          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static gboolean         export3d_stl          (FILE *fh,
                                               GArray *vertices,
                                               GArray *triangles,
                                               ExportArgs *args);
static GwySurface*      import3d_stl          (guchar *buffer,
                                               gsize size,
                                               GError **error);
static gint             detect3d_xyz          (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static GwySurface*      import3d_xyz          (guchar *buffer,
                                               gsize size,
                                               GError **error);
static gboolean         formats3d_export      (GwyContainer *data,
                                               const gchar *filename,
                                               GwyRunType runtype,
                                               GError **error,
                                               const gchar *name);
static void             export_sanitise_params(ExportArgs *args,
                                               const Format3D *fmt);
static GwyDialogOutcome export_run_gui        (const Format3D *fmt,
                                               ExportArgs *args);
static void             export_param_changed  (ExportGUI *gui,
                                               gint id);
static GwyContainer*    formats3d_import      (const gchar *filename,
                                               GwyRunType runtype,
                                               GError **error,
                                               const gchar *name);
static GwyDataField*    check_regular_grid    (GwySurface *surface);
static GwyDialogOutcome import_run_gui        (const Format3D *fmt,
                                               ImportArgs *args);
static void             import_param_changed  (ImportGUI *gui,
                                               gint id);
static void             update_range_lables   (GwyParamTable *table,
                                               gint id,
                                               gdouble min,
                                               gdouble max,
                                               const gchar *unitstring);

static const Format3D formats3d[] = {
    {
        "vtk3d", "VTK", N_("VTK structured grid (.vtk)"),
        &detect3d_vtk, &export3d_vtk, NULL,
        TRUE, FALSE,
    },
    {
        "ply3d", "PLY", N_("Polygon file format (.ply)"),
        &detect3d_ply, &export3d_ply, NULL,
        FALSE, FALSE,
    },
    {
        "obj3d", "OBJ", N_("Wavefront geometry definition (.obj)"),
        &detect3d_obj, &export3d_obj, &import3d_obj,
        FALSE, FALSE,
    },
    {
        "off3d", "OFF", N_("Object File Format (.off)"),
        &detect3d_off, &export3d_off, NULL,
        FALSE, FALSE,
    },
    {
        "stl3d", "STL", N_("Stereolitography STL (.stl)"),
        &detect3d_stl, &export3d_stl, &import3d_stl,
        FALSE, TRUE,
    },
    /* The function used to be called rawxyz when it has its own module; keep the name.  It is not a real 3D format
     * anyway. */
    {
        "rawxyz", "XYZ", N_("XYZ data files (.xyz)"),
        &detect3d_xyz, NULL, &import3d_xyz,
        FALSE, FALSE,
    }
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Exports images as miscellaneous 3D data formats and imports XYZ points from 3D formats."),
    "Yeti <yeti@gwyddion.net>",
    "2.1",
    "David Nečas (Yeti)",
    "2020",
};

GWY_MODULE_QUERY2(module_info, formats3d)

static gboolean
module_register(void)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(formats3d); i++) {
        const Format3D *fmt = formats3d + i;
        gwy_file_func_register(fmt->name, _(fmt->description),
                               fmt->detect,
                               fmt->import ? formats3d_import : NULL,
                               NULL,
                               fmt->export_ ? formats3d_export : NULL);
    }
    gwy_file_func_set_is_detectable("rawxyz", FALSE);

    return TRUE;
}

static GwyParamDef*
define_export_params(void)
{
    static const GwyEnum zscales[] = {
        { N_("_Automatic Z-scale"), ZSCALE_AUTO,     },
        { N_("_Physical 1:1"),      ZSCALE_PHYSICAL, },
        { N_("Other _scale:"),      ZSCALE_USER,     },
    };
    static const GwyEnum triangulations[] = {
        { N_("None (only points)"),         TRIANGULATION_NONE,     },
        { N_("Plain along main diagonals"), TRIANGULATION_PLAIN,    },
        { N_("With pixel midpoints"),       TRIANGULATION_MIDPOINT, },
        { N_("Random orientation"),         TRIANGULATION_RANDOM,   },
        { N_("Following features"),         TRIANGULATION_FOLLOW,   },
    };

    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "export3d");
    gwy_param_def_add_double(paramdef, PARAM_ZSCALE, "zscale", NULL, G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZSCALE_TYPE, "zscale_type", _("Z scale"),
                              zscales, G_N_ELEMENTS(zscales), ZSCALE_AUTO);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TRIANG_TYPE, "triang_type", _("Triangulation type"),
                              triangulations, G_N_ELEMENTS(triangulations), TRIANGULATION_PLAIN);
    gwy_param_def_add_boolean(paramdef, PARAM_SWAP_XY, "swap_xy", _("Swap X and Y axes"), FALSE);
    gwy_param_def_add_boolean(paramdef, PARAM_FLIP_Z, "flip_z", _("Flip Z axis"), FALSE);
    return paramdef;
}

static GwyParamDef*
define_import_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "import3d");
    /* XXX: ParamTable does not support entry controls for units.  Maybe it should.  But we more or less treat it as
     * an anything-goes string here. */
    gwy_param_def_add_string(paramdef, PARAM_XY_UNITS, "xy-units", _("_Lateral units"),
                             GWY_PARAM_STRING_EMPTY_IS_NULL, NULL, "1");
    gwy_param_def_add_string(paramdef, PARAM_Z_UNITS, "z-units", _("_Value units"),
                             GWY_PARAM_STRING_EMPTY_IS_NULL, NULL, "1");
    gwy_param_def_add_boolean(paramdef, PARAM_DO_RASTERISE, "do-rasterise",
                              _("Create image _directly from regular points"), TRUE);
    return paramdef;
}

static gint
detect3d_vtk(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    g_return_val_if_fail(only_name, 0);
    return g_str_has_suffix(fileinfo->name_lowercase, ".vtk") ? 30 : 0;
}

static gint
detect3d_ply(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    g_return_val_if_fail(only_name, 0);
    return g_str_has_suffix(fileinfo->name_lowercase, ".ply") ? 30 : 0;
}

static inline gint
try_to_match_keyword(const gchar *s, const gchar *k, guint maxlen)
{
    gint i;

    for (i = 1; i < maxlen; i++) {
        /* End of keyword means we know matched the keyword, or not. */
        if (!k[i])
            return g_ascii_isspace(s[i]) ? i : -i;
        /* Hitting a different character means we know we did not match but we want to move to the keyword end. */
        if (k[i] != s[i]) {
            while (k[i])
                i++;
            return -i;
        }
    }

    /* We exhausted the buffer without deciding. */
    return 0;
}

static gint
detect3d_obj(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    enum { kw_min = 98, kw_max = 118 };
    static const gchar keywords[] =
        "bevel\0bmat\0bsp\0bzp\0\0"
        "c_interp\0call\0cdc\0cdp\0con\0csh\0cstype\0ctech\0curv\0curv2\0\0"
        "d_interp\0deg\0\0"
        "end\0\0"
        "f\0\0"
        "g\0\0"
        "hole\0\0"
        "l\0lod\0\0"
        "maplib\0mg\0mtllib\0\0"
        "o\0\0"
        "p\0parm\0\0"
        "res\0\0"
        "s\0scrv\0shadow\0shadow_obj\0sp\0stech\0step\0surf\0\0"
        "trace\0trace_obj\0trim\0\0"
        "usemap\0usemtl\0\0"
        "v\0vn\0vp\0vt\0";
    static const gint offsets[] = {
        0, 20, 75, 89, 94, 97, 100, -1, -1, -1, 106, 113, -1, 131, 134, -1, 142, 147, 192, 214, 229
    };

    const gchar *h;
    guint i, hlen, ngood = 0, nbad = 0;
    gboolean line_ended_with_backslash = FALSE;
    gint j, k;
    guchar c;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".obj") ? 15 : 0;

    if (fileinfo->buffer_len < 60)
        return 0;

    hlen = fileinfo->buffer_len-1;
    h = fileinfo->head;
    i = 0;
    while (TRUE) {
        while (i < hlen && g_ascii_isspace(h[i]))
            i++;
        if (i == hlen)
            goto decide;

        /* Try to weed out binary files quickly. */
        c = h[i];
        if (!g_ascii_isprint(c)) {
            gwy_debug("non-ASCII");
            return 0;
        }

        if (c >= kw_min && c <= kw_max && offsets[c - kw_min] != -1) {
            gwy_debug("promising character %c", c);
            /* This could be the start of a valid keyword. */
            j = offsets[c - kw_min];
            while (TRUE) {
                k = try_to_match_keyword(h + i, keywords + j, hlen - i);
                if (!k)
                    goto decide;
                if (k > 0) {
                    gwy_debug("matched keyword %s", keywords + j);
                    i += k;
                    ngood++;
                    break;
                }
                j -= k;
                j++;
                if (!keywords[j]) {
                    gwy_debug("failed to match any keyword");
                    i++;
                    nbad++;
                    break;
                }
                gwy_debug("failed to match keyword %s, but trying another", keywords-1 + j + k);
            }
        }
        else if (c == '#') {
            gwy_debug("comment");
            i++;
        }
        else if (!line_ended_with_backslash) {
            gwy_debug("bad line %.*s", 12, h + i);
            i++;
            nbad++;
        }
        else {
            gwy_debug("previous line must have ended with backlash");
        }

        while (i < hlen && h[i] != '\r' && h[i] != '\n') {
            if (!g_ascii_isprint(h[i])) {
                gwy_debug("non-ASCII");
                return 0;
            }
            line_ended_with_backslash = (h[i] == '\\');
            i++;
        }
        if (i == hlen)
            goto decide;

        if (nbad >= 3) {
            gwy_debug("too many bad lines");
            return 0;
        }
        if (ngood >= 12*(nbad + 1)) {
            gwy_debug("lots of keywords found");
            return 50;
        }
        if (line_ended_with_backslash) {
            gwy_debug("line ended with backslash");
        }
    }
decide:
    gwy_debug("exhausted entire buffer, ngood=%u, nbad=%u -> score %u", ngood, nbad, 50*ngood/(12*(nbad + 1)));
    return 50*ngood/(12*(nbad + 1));
}

static gint
detect3d_off(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    g_return_val_if_fail(only_name, 0);
    return g_str_has_suffix(fileinfo->name_lowercase, ".off") ? 30 : 0;
}

static gint
detect3d_stl(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    guint i, j, ntri, ngood = 0;
    const guchar *p;
    gdouble block[12];

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".stl") ? 30 : 0;

    /* 80 bytes + 50 bytes * n-of-triangles */
    if (fileinfo->file_size < 134 || fileinfo->file_size % 50 != 34 || fileinfo->buffer_len < 134)
        return 0;

    p = fileinfo->head + 80;
    ntri = gwy_get_guint32_le(&p);
    if (ntri != (fileinfo->file_size - 84)/50)
        return 0;

    ntri = (fileinfo->buffer_len - 84)/50;
    ntri = MIN(ntri, 12);
    /* Check if attribute counts seem to be 0. */
    for (i = 0; i < ntri; i++) {
        if (fileinfo->head[132 + 50*i] != 0 || fileinfo->head[133 + 50*i] != 0)
            return 0;
    }

    /* Check the numbers.  Random stuff tends to produce infs, nans and numbers of weird magnitudes.  Normals can be
     * rubbish or zeros, so check only coordinates.  */
    for (i = 0; i < ntri; i++) {
        gwy_convert_raw_data(fileinfo->head + 84 + i*50, 12, 1,
                             GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             block, 1.0, 0.0);
        for (j = 0; j < 12; j++) {
            if (gwy_isnan(block[i]) || gwy_isinf(block[i]))
                return 0;
            if (block[i] && (fabs(block[i]) > 1e30 || fabs(block[i]) < 1e-30))
                return 0;
            if (j > 4 && (block[i] || (fabs(block[i]) < 1e12 && fabs(block[i]) > 1e-12)))
                ngood++;
        }
    }

    return 50*ngood/(12*ntri);
}

static gint
detect3d_xyz(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             G_GNUC_UNUSED const gchar *name)
{
    const gchar *s;
    gchar *end;
    guint i;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, ".xyz") ? 20 : 0;

    s = fileinfo->head;
    for (i = 0; i < 6; i++) {
        g_ascii_strtod(s, &end);
        if (end == s) {
            /* If we encounter garbage at the first line, give it a one more chance. */
            if (i || !(s = strchr(s, '\n')))
                return 0;
            goto next_line;
        }
        s = end;
        while (g_ascii_isspace(*s) || *s == ';' || *s == ',')
             s++;
        g_ascii_strtod(s, &end);
        if (end == s)
            return 0;
        s = end;
        while (g_ascii_isspace(*s) || *s == ';' || *s == ',')
             s++;
        g_ascii_strtod(s, &end);
        if (end == s)
            return 0;

        s = end;
        while (*s == ' ' || *s == '\t')
            s++;
        if (*s != '\n' && *s != '\r')
            return 0;

next_line:
        do {
            s++;
        } while (g_ascii_isspace(*s));
    }

    return 50;
}

static const Format3D*
find_format(const gchar *name)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(formats3d); i++) {
        const Format3D *fmt = formats3d + i;
        if (gwy_strequal(fmt->name, name))
            return fmt;
    }

    return NULL;
}

/********************************************************************************************************************
 *
 * Export
 *
 ********************************************************************************************************************/

static void
make_scaling_info(GwyDataField *dfield, ScalingInfo *scinfo)
{
    gdouble min, max, dx, dy, a;
    gint xres, yres;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    dx = gwy_data_field_get_dx(dfield);
    dy = gwy_data_field_get_dy(dfield);
    gwy_data_field_get_min_max(dfield, &min, &max);

    scinfo->zscale_1_1 = 1.0/sqrt(dx*dy);
    scinfo->dx = dx*scinfo->zscale_1_1;
    scinfo->dy = dy*scinfo->zscale_1_1;
    a = dx*xres*scinfo->dx * dy*yres*scinfo->dy;
    scinfo->zscale_auto = (max <= min) ? 0.0 : 0.2*sqrt(a)/(max - min);
}

static inline void
make_triangle_split(GArray *triangles, guint k, guint xres, gboolean main_diagonal)
{
    TriangleIndices abc;

    if (main_diagonal) {
        abc.a = k;
        abc.b = k+xres;
        abc.c = k+xres+1;
        g_array_append_val(triangles, abc);
        abc.b = k+xres+1;
        abc.c = k+1;
        g_array_append_val(triangles, abc);
    }
    else {
        abc.a = k+1;
        abc.b = k;
        abc.c = k+xres;
        g_array_append_val(triangles, abc);
        abc.b = k+xres;
        abc.c = k+xres+1;
        g_array_append_val(triangles, abc);
    }
}

static void
make_triangulation(ExportArgs *args,
                   GArray *vertices,
                   GArray *triangles)
{
    ZScaleType zscale_type = gwy_params_get_enum(args->params, PARAM_ZSCALE_TYPE);
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);
    const ScalingInfo *scinfo = &args->scinfo;
    GwyDataField *field = args->field;
    gint xres, yres, i, j, k, stride;
    gdouble z, min, dx = scinfo->dx, dy = scinfo->dy, qz = scinfo->zscale_1_1;
    const gdouble *d;
    GwyXYZ xyz;

    xres = gwy_data_field_get_xres(field);
    yres = gwy_data_field_get_yres(field);
    min = gwy_data_field_get_min(field);

    if (zscale_type == ZSCALE_USER)
        qz *= gwy_params_get_double(args->params, PARAM_ZSCALE);
    else if (zscale_type == ZSCALE_AUTO)
        qz *= scinfo->zscale_auto;

    g_array_set_size(vertices, 0);
    g_array_set_size(triangles, 0);
    d = gwy_data_field_get_data_const(field);

    if (triang_type == TRIANGULATION_MIDPOINT) {
        stride = 2*xres - 1;
        for (i = 0; i < yres-1; i++) {
            xyz.y = i*dy;
            for (j = 0; j < xres; j++) {
                k = i*xres + j;
                z = d[k];
                xyz.x = j*dx;
                xyz.z = qz*(z - min);
                g_array_append_val(vertices, xyz);
            }

            xyz.y = (i + 0.5)*dy;
            for (j = 0; j < xres-1; j++) {
                TriangleIndices abc;

                k = i*xres + j;
                z = 0.25*(d[k] + d[k+1] + d[k+xres] + d[k+xres+1]);
                xyz.x = (j + 0.5)*dx;
                xyz.z = qz*(z - min);
                g_array_append_val(vertices, xyz);

                k = i*stride + j;
                abc.a = k;
                abc.b = k + xres;
                abc.c = k+1;
                g_array_append_val(triangles, abc);
                abc.a = abc.c;
                abc.c = k + stride+1;
                g_array_append_val(triangles, abc);
                abc.a = abc.c;
                abc.c = k + stride;
                g_array_append_val(triangles, abc);
                abc.a = abc.c;
                abc.c = k;
                g_array_append_val(triangles, abc);
            }
        }

        xyz.y = (yres - 1)*dy;
        for (j = 0; j < xres; j++) {
            k = i*xres + j;
            z = d[k];
            xyz.x = j*dx;
            xyz.z = qz*(z - min);
            g_array_append_val(vertices, xyz);
        }
        return;
    }

    for (i = 0; i < yres; i++) {
        xyz.y = i*dy;
        for (j = 0; j < xres; j++, d++) {
            xyz.x = j*dx;
            xyz.z = qz*(*d - min);
            g_array_append_val(vertices, xyz);
        }
    }
    if (triang_type == TRIANGULATION_NONE)
        return;

    if (triang_type == TRIANGULATION_RANDOM) {
        GRand *rng = g_rand_new();
        guint ranval = 0, havebits = 0;

        for (i = 0; i < yres-1; i++) {
            for (j = 0; j < xres-1; j++) {
                if (!havebits) {
                    ranval = g_rand_int(rng);
                    havebits = 31;
                }
                make_triangle_split(triangles, i*xres + j, xres, (ranval & 1));
                ranval >>= 1;
                havebits--;
            }
        }

        g_rand_free(rng);
    }
    else if (triang_type == TRIANGULATION_FOLLOW && xres > 4 && yres > 4) {
        gdouble zmaj, zmin;
        gboolean ismaj;

        for (i = 1; i < yres-2; i++) {
            for (j = 1; j < xres-2; j++) {
                k = i*xres + j;
                zmaj = fabs(d[k] + d[k+xres+1] - d[k-xres-1] - d[k + 2*xres+2]);
                zmin = fabs(d[k+1] + d[k+xres] - d[k-xres+2] - d[k + 2*xres-1]);
                ismaj = (zmaj <= zmin);
                make_triangle_split(triangles, i*xres + j, xres, ismaj);
                if (j == 1)
                    make_triangle_split(triangles, k-1, xres, ismaj);
                if (i == 1 && j == 1)
                    make_triangle_split(triangles, 0, xres, ismaj);
                if (i == 1)
                    make_triangle_split(triangles, k-xres, xres, ismaj);
                if (i == 1 && j == xres-3)
                    make_triangle_split(triangles, k+1-xres, xres, ismaj);
                if (j == xres-3)
                    make_triangle_split(triangles, k+1, xres, ismaj);
                if (i == yres-3 && j == 1)
                    make_triangle_split(triangles, k+xres-1, xres, ismaj);
                if (i == yres-3)
                    make_triangle_split(triangles, k+xres, xres, ismaj);
                if (i == yres-3 && j == xres-3)
                    make_triangle_split(triangles, k+xres+1, xres, ismaj);
            }
        }
    }
    else {
        for (i = 0; i < yres-1; i++) {
            for (j = 0; j < xres-1; j++)
                make_triangle_split(triangles, i*xres + j, xres, TRUE);
        }
    }
}

static void
fix_triangulation(GArray *vertices, GArray *triangles,
                  gboolean flip_z, gboolean swap_xy)
{
    guint k, nvert = vertices->len, ntri = triangles->len;

    if (flip_z) {
        for (k = 0; k < nvert; k++) {
            GwyXYZ *xyz = &g_array_index(vertices, GwyXYZ, k);
            xyz->z *= 1.0;
        }
    }

    if (swap_xy) {
        for (k = 0; k < nvert; k++) {
            GwyXYZ *xyz = &g_array_index(vertices, GwyXYZ, k);
            GWY_SWAP(gdouble, xyz->x, xyz->y);
        }
    }
    else {
        for (k = 0; k < ntri; k++) {
            TriangleIndices *abc = &g_array_index(triangles, TriangleIndices, k);
            GWY_SWAP(guint, abc->b, abc->c);
        }
    }
}

static gboolean
formats3d_export(G_GNUC_UNUSED GwyContainer *data,
                 const gchar *filename,
                 GwyRunType runtype,
                 GError **error,
                 const gchar *name)
{
    const Format3D *fmt;
    ExportArgs args;
    GwyParams *params;
    GArray *vertices, *triangles;
    gboolean ok = FALSE, tried_to_write_file = FALSE;
    GwyDialogOutcome outcome;
    gint id;
    FILE *fh;

    fmt = find_format(name);
    g_return_val_if_fail(fmt, FALSE);
    g_return_val_if_fail(fmt->export_, FALSE);

    gwy_clear(&args, 1);
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);

    if (!args.field) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    make_scaling_info(args.field, &args.scinfo);
    args.params = params = gwy_params_new_from_settings(define_export_params());
    export_sanitise_params(&args, fmt);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = export_run_gui(fmt, &args);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto end;
        }
    }

    if (!gwy_container_gis_string(data, gwy_app_get_data_title_key_for_id(id), &args.title))
        args.title = _("Untitled");

    if (!(fh = gwy_fopen(filename, fmt->is_binary ? "wb" : "w"))) {
        err_OPEN_WRITE(error);
        goto end;
    }

    vertices = g_array_new(FALSE, FALSE, sizeof(GwyXYZ));
    triangles = g_array_new(FALSE, FALSE, sizeof(TriangleIndices));
    make_triangulation(&args, vertices, triangles);
    fix_triangulation(vertices, triangles,
                      gwy_params_get_boolean(params, PARAM_SWAP_XY), gwy_params_get_boolean(params, PARAM_FLIP_Z));

    tried_to_write_file = TRUE;
    if (!(ok = fmt->export_(fh, vertices, triangles, &args)))
        err_WRITE(error);
    fclose(fh);

    g_array_free(vertices, TRUE);
    g_array_free(triangles, TRUE);

end:
    g_object_unref(args.params);
    if (!ok && tried_to_write_file)
        g_unlink(filename);

    return ok;
}

static gboolean
write_vertex_lines(FILE *fh,
                   GArray *vertices,
                   gchar sep,
                   const gchar *prefix)
{
    guint k, nvert = vertices->len;
    const GwyXYZ *xyz;
    gchar bufx[24], bufy[24], bufz[24];

    for (k = 0; k < nvert; k++) {
        xyz = &g_array_index(vertices, GwyXYZ, k);
        g_ascii_formatd(bufx, sizeof(bufx), "%.9g", xyz->x);
        g_ascii_formatd(bufy, sizeof(bufy), "%.9g", xyz->y);
        g_ascii_formatd(bufz, sizeof(bufz), "%.9g", xyz->z);
        gwy_fprintf(fh, "%s%s%c%s%c%s", prefix, bufx, sep, bufy, sep, bufz);
        if (fputc('\n', fh) == EOF)
            return FALSE;
    }
    return TRUE;
}

static gboolean
write_triangle_lines(FILE *fh,
                     GArray *triangles,
                     gchar sep,
                     const gchar *prefix)
{
    guint k, ntri = triangles->len;
    const TriangleIndices *abc;

    for (k = 0; k < ntri; k++) {
        abc = &g_array_index(triangles, TriangleIndices, k);
        gwy_fprintf(fh, "%s%u%c%u%c%u", prefix, abc->a, sep, abc->b, sep, abc->c);
        if (fputc('\n', fh) == EOF)
            return FALSE;
    }
    return TRUE;
}

static gboolean
export3d_vtk(FILE *fh, GArray *vertices, GArray *triangles, ExportArgs *args)
{
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);
    guint xres, yres, nvert, ntri;

    xres = gwy_data_field_get_xres(args->field);
    yres = gwy_data_field_get_yres(args->field);
    nvert = vertices->len;
    ntri = triangles->len;

    /* Do not bother checking errors here.  If some write fails we will get more errors below. */
    fputs("# vtk DataFile Version 2.0\n", fh);
    gwy_fprintf(fh, "%s\n", args->title);
    fputs("ASCII\n", fh);
    if (triang_type == TRIANGULATION_NONE) {
        g_assert(nvert == xres*yres);
        fputs("DATASET STRUCTURED_GRID\n", fh);
        gwy_fprintf(fh, "DIMENSIONS %u %u 1\n", xres, yres);
        gwy_fprintf(fh, "POINTS %u float\n", nvert);
        return write_vertex_lines(fh, vertices, '\n', "");
    }

    fputs("DATASET POLYDATA\n", fh);
    gwy_fprintf(fh, "POINTS %u float\n", nvert);
    if (!write_vertex_lines(fh, vertices, '\n', ""))
        return FALSE;

    gwy_fprintf(fh, "POLYGONS %u %u\n", ntri, 4*ntri);
    if (!write_triangle_lines(fh, triangles, ' ', "3 "))
        return FALSE;

    return TRUE;
}

static gboolean
export3d_ply(FILE *fh, GArray *vertices, GArray *triangles, ExportArgs *args)
{
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);
    guint nvert, ntri;

    g_return_val_if_fail(triang_type != TRIANGULATION_NONE, FALSE);
    nvert = vertices->len;
    ntri = triangles->len;

    /* Do not bother checking errors here.  If some write fails we will get more errors below. */
    fputs("ply\n", fh);
    fputs("format ascii 1.0\n", fh);
    fputs("comment exported from Gwyddion\n", fh);
    gwy_fprintf(fh, "comment title %s\n", args->title);
    gwy_fprintf(fh, "element vertex %u\n", nvert);
    fputs("property float x\n", fh);
    fputs("property float y\n", fh);
    fputs("property float z\n", fh);
    gwy_fprintf(fh, "element face %u\n", ntri);
    fputs("property list uchar int vertex_index\n", fh);
    fputs("end_header\n", fh);

    if (!write_vertex_lines(fh, vertices, ' ', ""))
        return FALSE;
    if (!write_triangle_lines(fh, triangles, ' ', "3 "))
        return FALSE;

    return TRUE;
}

static gboolean
export3d_obj(FILE *fh, GArray *vertices, GArray *triangles, ExportArgs *args)
{
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);

    g_return_val_if_fail(triang_type != TRIANGULATION_NONE, FALSE);

    /* Do not bother checking errors here.  If some write fails we will get more errors below. */
    fputs("# exported from Gwyddion\n", fh);
    gwy_fprintf(fh, "# title %s\n", args->title);
    fputc('\n', fh);
    fputs("g surface\n", fh);
    fputc('\n', fh);

    if (!write_vertex_lines(fh, vertices, ' ', "v "))
        return FALSE;
    fputc('\n', fh);

    if (!write_triangle_lines(fh, triangles, ' ', "f "))
        return FALSE;

    return TRUE;
}

static gboolean
export3d_off(FILE *fh, GArray *vertices, GArray *triangles, ExportArgs *args)
{
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);
    guint nvert, ntri;

    g_return_val_if_fail(triang_type != TRIANGULATION_NONE, FALSE);
    nvert = vertices->len;
    ntri = triangles->len;

    /* Do not bother checking errors here.  If some write fails we will get more errors below. */
    gwy_fprintf(fh, "OFF %u %u 0\n", nvert, ntri);
    fputs("# exported from Gwyddion\n", fh);
    gwy_fprintf(fh, "# title %s\n", args->title);
    fputc('\n', fh);

    if (!write_vertex_lines(fh, vertices, ' ', ""))
        return FALSE;
    fputc('\n', fh);

    if (!write_triangle_lines(fh, triangles, ' ', "3 "))
        return FALSE;

    return TRUE;
}

static gboolean
export3d_stl(FILE *fh, GArray *vertices, GArray *triangles, ExportArgs *args)
{
    TriangulationType triang_type = gwy_params_get_enum(args->params, PARAM_TRIANG_TYPE);
    guchar buf[80];
    const TriangleIndices *abc;
    const GwyXYZ *a, *b, *c;
    guint ntri, k;
    guint swapme = (G_BYTE_ORDER == G_BIG_ENDIAN ? 3 : 0);
    gfloat values[4*3];
    guint32 t;

    g_return_val_if_fail(triang_type != TRIANGULATION_NONE, FALSE);
    ntri = triangles->len;

    /* 80 bytes long text header/comment/what-have-you. */
    gwy_clear(buf, sizeof(buf));
    g_snprintf(buf, sizeof(buf), "STL binary data exported from Gwyddion");
    if (fwrite(buf, 1, sizeof(buf), fh) != sizeof(buf))
        return FALSE;

    /* Number of triangles. */
    t = GUINT32_TO_LE(ntri);
    if (fwrite(&t, sizeof(guint32), 1, fh) != 1)
        return FALSE;

    /* Triangle loop, each block 50 bytes long. */
    buf[48] = buf[49] = 0;   /* Attribute byte count, i.e. zero. */
    for (k = 0; k < ntri; k++) {
        abc = &g_array_index(triangles, TriangleIndices, k);
        a = &g_array_index(vertices, GwyXYZ, abc->a);
        b = &g_array_index(vertices, GwyXYZ, abc->b);
        c = &g_array_index(vertices, GwyXYZ, abc->c);

        values[0] = (a->y*b->z - a->z*b->y + b->y*c->z - b->z*c->y + c->y*a->z - c->z*a->y);
        values[1] = (a->z*b->x - a->x*b->z + b->z*c->x - b->x*c->z + c->z*a->x - c->x*a->z);
        values[2] = (a->x*b->y - a->y*b->x + b->x*c->y - b->y*c->x + c->x*a->y - c->y*a->x);
        values[3] = a->x;
        values[4] = a->y;
        values[5] = a->z;
        values[6] = b->x;
        values[7] = b->y;
        values[8] = b->z;
        values[9] = c->x;
        values[10] = c->y;
        values[11] = c->z;
        gwy_memcpy_byte_swap((guint8*)values, buf, sizeof(gfloat), 4*3, swapme);
        if (fwrite(buf, 1, 50, fh) != 50)
            return FALSE;
    }

    return TRUE;
}

static GwyDialogOutcome
export_run_gui(const Format3D *fmt, ExportArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    gchar *title = NULL;
    ExportGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    title = g_strdup_printf(_("Export %s"), fmt->title);
    gui.dialog = gwy_dialog_new(title);
    g_free(title);
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_ZSCALE_TYPE);
    gwy_param_table_append_entry(table, PARAM_ZSCALE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_radio(table, PARAM_TRIANG_TYPE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_checkbox(table, PARAM_SWAP_XY);
    gwy_param_table_append_checkbox(table, PARAM_FLIP_Z);
    if (!fmt->has_triang_none)
        gwy_param_table_radio_set_sensitive(table, PARAM_TRIANG_TYPE, TRIANGULATION_NONE, FALSE);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(export_param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
export_param_changed(ExportGUI *gui, gint id)
{
    ExportArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;

    if (id < 0 || id == PARAM_ZSCALE_TYPE) {
        ZScaleType zscale_type = gwy_params_get_enum(params, PARAM_ZSCALE_TYPE);
        if (zscale_type == ZSCALE_AUTO)
            gwy_param_table_set_double(table, PARAM_ZSCALE, args->scinfo.zscale_auto);
        else if (zscale_type == ZSCALE_PHYSICAL)
            gwy_param_table_set_double(table, PARAM_ZSCALE, args->scinfo.zscale_1_1);

        gwy_param_table_set_sensitive(table, PARAM_ZSCALE, zscale_type == ZSCALE_USER);
    }
}

static void
export_sanitise_params(ExportArgs *args, const Format3D *fmt)
{
    GwyParams *params = args->params;
    ZScaleType zscale_type = gwy_params_get_enum(params, PARAM_ZSCALE_TYPE);

    if (!fmt->has_triang_none && gwy_params_get_enum(params, PARAM_TRIANG_TYPE) == TRIANGULATION_NONE)
        gwy_params_set_enum(params, PARAM_TRIANG_TYPE, TRIANGULATION_PLAIN);

    if (zscale_type == ZSCALE_AUTO)
        gwy_params_set_double(params, PARAM_ZSCALE, args->scinfo.zscale_auto);
    else if (zscale_type == ZSCALE_PHYSICAL)
        gwy_params_set_double(params, PARAM_ZSCALE, args->scinfo.zscale_1_1);
}

/********************************************************************************************************************
 *
 * Import
 *
 ********************************************************************************************************************/

static GwyContainer*
formats3d_import(const gchar *filename,
                 GwyRunType runtype,
                 GError **error,
                 const gchar *name)
{
    const Format3D *fmt;
    GwyContainer *container = NULL;
    ImportArgs args;
    GwyDialogOutcome outcome;
    GwyDataField *image;
    GwySurface *surface;
    GwySIUnit *xyunit = NULL, *zunit = NULL;
    gint power10xy, power10z;
    gdouble q;
    gchar *buffer = NULL;
    gsize size;
    GError *err = NULL;
    GwyXYZ *data;
    guint n, k;

    fmt = find_format(name);
    g_return_val_if_fail(fmt, NULL);
    g_return_val_if_fail(fmt->import, NULL);

    if (!g_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }

    gwy_clear(&args, 1);
    args.surface = surface = fmt->import(buffer, size, error);
    g_free(buffer);
    if (!surface)
        return NULL;

    if (!(n = gwy_surface_get_npoints(surface))) {
        err_NO_DATA(error);
        goto fail;
    }

    args.image = image = check_regular_grid(surface);
    args.params = gwy_params_new_from_settings(define_import_params());
    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = import_run_gui(fmt, &args);
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto fail;
        }
    }

    container = gwy_container_new();
    xyunit = gwy_si_unit_new_parse(gwy_params_get_string(args.params, PARAM_XY_UNITS), &power10xy);
    zunit = gwy_si_unit_new_parse(gwy_params_get_string(args.params, PARAM_Z_UNITS), &power10z);

    if (image && gwy_params_get_boolean(args.params, PARAM_DO_RASTERISE)) {
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(image), xyunit);
        if (power10xy) {
            q = pow10(power10xy);
            gwy_data_field_set_xreal(image, q*gwy_data_field_get_xreal(image));
            gwy_data_field_set_yreal(image, q*gwy_data_field_get_yreal(image));
            gwy_data_field_set_xoffset(image, q*gwy_data_field_get_xoffset(image));
            gwy_data_field_set_yoffset(image, q*gwy_data_field_get_yoffset(image));
        }

        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(image), zunit);
        if (power10z)
            gwy_data_field_multiply(image, pow10(power10z));

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(0), image);
        gwy_app_channel_title_fall_back(container, 0);
        gwy_file_channel_import_log_add(container, 0, NULL, filename);
    }
    else {
        data = gwy_surface_get_data(surface);
        gwy_si_unit_assign(gwy_surface_get_si_unit_xy(surface), xyunit);
        if (power10xy) {
            q = pow10(power10xy);
            for (k = 0; k < n; k++) {
                data[k].x *= q;
                data[k].y *= q;
            }
            gwy_surface_invalidate(surface);
        }

        gwy_si_unit_assign(gwy_surface_get_si_unit_z(surface), zunit);
        if (power10z) {
            q = pow10(power10z);
            for (k = 0; k < n; k++)
                data[k].z *= q;
            gwy_surface_invalidate(surface);
        }

        gwy_container_set_object(container, gwy_app_get_surface_key_for_id(0), surface);
        gwy_app_xyz_title_fall_back(container, 0);
        gwy_file_xyz_import_log_add(container, 0, NULL, filename);
    }

fail:
    g_object_unref(args.params);
    GWY_OBJECT_UNREF(args.surface);
    GWY_OBJECT_UNREF(args.image);
    GWY_OBJECT_UNREF(xyunit);
    GWY_OBJECT_UNREF(zunit);

    return container;
}

/* Create a data field directly if the XY positions form a complete regular grid.  */
static GwyDataField*
check_regular_grid(GwySurface *surface)
{
    GwyXY xymin, dxy;
    guint n, xres, yres, k;
    GwyDataField *dfield;
    gdouble *data;
    guint *map;

    n = surface->n;
    if (!(map = gwy_check_regular_2d_grid((const gdouble*)surface->data, 3, n, -1.0, &xres, &yres, &xymin, &dxy)))
        return NULL;

    dfield = gwy_data_field_new(xres, yres, xres*dxy.x, yres*dxy.y, FALSE);
    data = gwy_data_field_get_data(dfield);
    for (k = 0; k < n; k++)
        data[k] = surface->data[map[k]].z;
    g_free(map);

    gwy_data_field_set_xoffset(dfield, xymin.x);
    gwy_data_field_set_yoffset(dfield, xymin.y);
    gwy_surface_copy_units_to_data_field(surface, dfield);

    return dfield;
}

static gchar
figure_out_comma_fix_char(const gchar *line)
{
    gchar *comma, *end;

    /* Not a number, try again. */
    if (!g_ascii_strtod(line, &end) && end == line)
        return 0;

    /* There are decimal dots => POSIX. */
    if (strchr(line, '.'))
        return ' ';

    /* There are no commas => POSIX. */
    comma = strchr(line, ',');
    if (!comma)
        return ' ';

    /* There are spaces after commas => POSIX. */
    if (g_regex_match_simple(",[ \t]", line, G_REGEX_NO_AUTO_CAPTURE, 0))
        return ' ';

    /* There is a contiguous block of digits and commas => POSIX. */
    if (g_regex_match_simple("[0-9],[0-9]+,[0-9]", line, G_REGEX_NO_AUTO_CAPTURE, 0))
        return ' ';

    /* There are commas and may actually be inside numbers.  Assume the decimal separator is comma. */
    return '.';
}

static gboolean
read_one_point(const gchar *s, GwyXYZ *pt)
{
    gchar *end;

    if (!(pt->x = g_ascii_strtod(s, &end)) && end == s)
        return FALSE;

    s = end;
    while (g_ascii_isspace(*s))
        s++;

    if (!(pt->y = g_ascii_strtod(s, &end)) && end == s)
        return FALSE;

    s = end;
    while (g_ascii_isspace(*s))
        s++;

    if (!(pt->z = g_ascii_strtod(s, &end)) && end == s)
        return FALSE;

    return TRUE;
}

static GwySurface*
import3d_xyz(guchar *buffer, G_GNUC_UNUSED gsize size, G_GNUC_UNUSED GError **error)
{
    GwySurface *surface;
    GArray *points;
    gchar *line, *end, *p = buffer;
    char comma_fix_char = 0;
    GwyXYZ pt;

    points = g_array_new(FALSE, FALSE, sizeof(GwyXYZ));
    for (line = gwy_str_next_line(&p); line; line = gwy_str_next_line(&p)) {
        if (!line[0] || line[0] == '#')
            continue;

        if (!comma_fix_char) {
            comma_fix_char = figure_out_comma_fix_char(line);
            if (!comma_fix_char)
                continue;
        }

        for (end = line; *end; end++) {
            if (*end == ';')
                *end = ' ';
            else if (*end == ',')
                *end = comma_fix_char;
        }

        if (read_one_point(line, &pt))
            g_array_append_val(points, pt);
    }

    surface = gwy_surface_new_from_data((GwyXYZ*)points->data, points->len);
    g_array_free(points, TRUE);

    return surface;
}

static GwySurface*
import3d_obj(guchar *buffer, G_GNUC_UNUSED gsize size, G_GNUC_UNUSED GError **error)
{
    GwySurface *surface;
    GArray *points;
    gchar *p = buffer, *line;
    GwyXYZ pt;

    points = g_array_new(FALSE, FALSE, sizeof(GwyXYZ));
    while ((line = gwy_str_next_line(&p))) {
        while (*line == ' ' || *line == '\t')
            line++;

        /* We have a fairly simplistic view of vertex lines and we do not care about anything else. */
        if (line[0] != 'v' || (line[1] != ' ' && line[1] != '\t'))
            continue;

        if (read_one_point(line + 2, &pt))
            g_array_append_val(points, pt);
    }

    surface = gwy_surface_new_from_data((GwyXYZ*)points->data, points->len);
    g_array_free(points, TRUE);

    return surface;
}

static guint
point_hash(gconstpointer key)
{
    const guint32 *data = (const guint32*)key;
    guint32 h;

    h = data[0];
    h ^= (data[1] >> 10) | ((data[1] & 0x3ff) << 22);
    h ^= (data[2] >> 22) | ((data[2] & 0x3fffff) << 10);
    h ^= (data[3] >> 10) | ((data[3] & 0x3ff) << 22);
    h ^= (data[4] >> 22) | ((data[4] & 0x3fffff) << 10);
    h ^= data[5];
    return h;
}

static gboolean
point_equal(gconstpointer keya, gconstpointer keyb)
{
    const guint32 *a = (const guint32*)keya;
    const guint32 *b = (const guint32*)keyb;

    return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3] && a[4] == b[4] && a[5] == b[5]);
}

static GwySurface*
import3d_stl(guchar *buffer, gsize size, GError **error)
{
    GHashTable *hash;
    GwySurface *surface;
    GwyXYZ *points;
    guint ntri, i, j, npt = 0;
    gdouble ptblock[9];
    const guchar *p;
    GwyXYZ pt;

    if (size < 134 || size % 50 != 34) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("File is truncated."));
        return NULL;
    }

    p = buffer + 80;
    ntri = gwy_get_guint32_le(&p);
    if (err_SIZE_MISMATCH(error, 84 + ntri*50, size, TRUE))
        return NULL;

    /* This is an upper bound. */
    points = g_new(GwyXYZ, 3*ntri);
    hash = g_hash_table_new(point_hash, point_equal);
    for (i = 0; i < ntri; i++) {
        gwy_convert_raw_data(p + 50*i + 3*4, 9, 1,
                             GWY_RAW_DATA_FLOAT, GWY_BYTE_ORDER_LITTLE_ENDIAN,
                             ptblock, 1.0, 0.0);
        for (j = 0; j < 9; j++) {
            if (gwy_isinf(ptblock[j]) || gwy_isnan(ptblock[j])) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("File contains NaNs or infinities."));
                g_hash_table_destroy(hash);
                g_free(points);
                return NULL;
            }
        }
        for (j = 0; j < 3; j++) {
            pt.x = ptblock[3*j + 0];
            pt.y = ptblock[3*j + 1];
            pt.z = ptblock[3*j + 2];
            if (!g_hash_table_lookup(hash, &pt)) {
                points[npt] = pt;
                g_hash_table_insert(hash, points + npt, GUINT_TO_POINTER(TRUE));
                npt++;
            }
        }
    }
    g_hash_table_destroy(hash);

    surface = gwy_surface_new_from_data(points, npt);
    g_free(points);

    return surface;
}

static GwyDialogOutcome
import_run_gui(const Format3D *fmt, ImportArgs *args)
{
    GwyDialog *dialog;
    GwyParamTable *table;
    gchar *title = NULL, *s;
    ImportGUI gui;

    gwy_clear(&gui, 1);
    gui.args = args;

    title = g_strdup_printf(_("Import %s"), fmt->title);
    gui.dialog = gwy_dialog_new(title);
    g_free(title);
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_info(table, LABEL_NPOINTS, _("Number of points"));
    gwy_param_table_append_info(table, LABEL_XRANGE, _("X-range"));
    gwy_param_table_append_info(table, LABEL_YRANGE, _("Y-range"));
    gwy_param_table_append_info(table, LABEL_ZRANGE, _("Z-range"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_entry(table, PARAM_XY_UNITS);
    gwy_param_table_entry_set_width(table, PARAM_XY_UNITS, 8);
    gwy_param_table_append_entry(table, PARAM_Z_UNITS);
    gwy_param_table_entry_set_width(table, PARAM_Z_UNITS, 8);
    if (args->image)
        gwy_param_table_append_checkbox(table, PARAM_DO_RASTERISE);

    s = g_strdup_printf("%u", gwy_surface_get_npoints(args->surface));
    gwy_param_table_info_set_valuestr(table, LABEL_NPOINTS, s);
    g_free(s);

    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(import_param_changed), &gui);

    return gwy_dialog_run(dialog);
}

static void
import_param_changed(ImportGUI *gui, gint id)
{
    ImportArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    gdouble min, max;

    if (id < 0 || id == PARAM_XY_UNITS) {
        gwy_surface_get_xrange(args->surface, &min, &max);
        update_range_lables(table, LABEL_XRANGE, min, max, gwy_params_get_string(params, PARAM_XY_UNITS));
        gwy_surface_get_yrange(args->surface, &min, &max);
        update_range_lables(table, LABEL_YRANGE, min, max, gwy_params_get_string(params, PARAM_XY_UNITS));
    }
    if (id < 0 || id == PARAM_Z_UNITS) {
        gwy_surface_get_min_max(args->surface, &min, &max);
        update_range_lables(table, LABEL_ZRANGE, min, max, gwy_params_get_string(params, PARAM_Z_UNITS));
    }
}

static void
update_range_lables(GwyParamTable *table, gint id,
                    gdouble min, gdouble max, const gchar *unitstring)
{
    GwySIValueFormat *vf;
    GwySIUnit *siunit;
    gint power10;
    gchar *s;

    siunit = gwy_si_unit_new_parse(unitstring, &power10);
    min *= pow10(power10);
    max *= pow10(power10);
    vf = gwy_si_unit_get_format_with_digits(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, fmax(fabs(min), fabs(max)), 3, NULL);
    s = g_strdup_printf("%.*f – %.*f", vf->precision, min/vf->magnitude, vf->precision, max/vf->magnitude);
    gwy_param_table_info_set_valuestr(table, id, s);
    g_free(s);
    gwy_param_table_set_unitstr(table, id, vf->units);
    gwy_si_unit_value_format_free(vf);
    g_object_unref(siunit);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
