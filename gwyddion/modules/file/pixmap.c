/*
 *  $Id: pixmap.c 25636 2023-09-07 12:19:33Z yeti-dn $
 *  Copyright (C) 2004-2022 David Necas (Yeti).
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
 * Pixmap images
 * .png, .jpeg, .tiff, .tga, .pnm, .bmp
 * Read[1] Export[2]
 * [1] Import support relies on Gdk-Pixbuf and hence may vary among systems.
 * [2] Usually lossy, intended for presentational purposes.  16bit grayscale export is possible to PNG, TIFF and PNM.
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwythreads.h>
#include <libdraw/gwypixfield.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include <app/gwymoduleutils-file.h>
#include "err.h"
#include "gwytiff.h"

enum {
    PREVIEW_SIZE = 320,
};

enum {
    PIXMAP_HAS_COLOURS = 1u << 0,   /* Unset if there are RGB, but all are identical. */
    PIXMAP_HAS_ALPHA   = 1u << 1,
};

enum {
    PARAM_MAP_TYPE,
    PARAM_HUE_OFFSET,
    PARAM_XREAL,
    PARAM_YREAL,
    PARAM_ZREAL,
    PARAM_XYMEASUREEQ,
    PARAM_SIZE_IN_PIXELS,
    PARAM_XYUNIT,
    PARAM_ZUNIT,

    WIDGET_IMAGE_INFO,
};

/* What value is used when importing an image. */
typedef enum {
    PIXMAP_MAP_RED = 1,
    PIXMAP_MAP_GREEN,
    PIXMAP_MAP_BLUE,
    PIXMAP_MAP_VALUE,
    PIXMAP_MAP_SUM,
    PIXMAP_MAP_ALPHA,
    PIXMAP_MAP_LUMA,
    PIXMAP_MAP_ALL,
    PIXMAP_MAP_HUE,
    PIXMAP_MAP_GREY,
    PIXMAP_MAP_NTYPES = PIXMAP_MAP_GREY,
} PixmapMapType;

typedef struct {
    GwyParams *params;
    GdkPixbuf *pixbuf;
    /* Cached properties. */
    guint flags;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_lateral;
    GwyParamTable *table_values;
    GdkPixbuf *small_pixbuf;
    GwyContainer *data;
} ModuleGUI;

typedef gboolean (*PixmapFilterFunc)(const GwyFileDetectInfo *fileinfo);

/* Static data about known (whitelisted) formats. */
typedef struct {
    const gchar *name;
    const gchar *description;
    PixmapFilterFunc filter_func;
} PixmapKnownFormat;

/* Actually registered formats. */
typedef struct {
    const gchar *name;
    const gchar *description;
    const GdkPixbufFormat *pixbuf_format;
    PixmapFilterFunc filter_func;
} PixmapFormatInfo;

static gboolean          module_register       (void);
static gint              pixmap_detect         (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name,
                                                const gchar *name);
static GwyContainer*     pixmap_load           (const gchar *filename,
                                                GwyRunType runtype,
                                                GError **error,
                                                const gchar *name);
static GdkPixbuf*        pixmap_load_pixbuf    (const gchar *filename,
                                                const gchar *name,
                                                GError **error);
static void              pixmap_set_field      (GwyContainer *container,
                                                gint id,
                                                ModuleArgs *args,
                                                PixmapMapType maptype);
static void              pixmap_pixbuf_to_field(GdkPixbuf *pixbuf,
                                                GwyDataField *field,
                                                PixmapMapType maptype,
                                                gdouble hue_offset);
static GwyDialogOutcome  run_gui               (ModuleArgs *args,
                                                const gchar *name);
static void              param_changed         (ModuleGUI *gui,
                                                gint id);
static void              preview               (gpointer user_data);
static gboolean          mapping_type_filter   (const GwyEnum *enumval,
                                                gpointer user_data);
static void              pixmap_add_import_log (GwyContainer *data,
                                                gint id,
                                                const gchar *filetype,
                                                const gchar *filename);
static PixmapFormatInfo* find_format           (const gchar *name);
static void              sanitise_params       (ModuleArgs *args);

static gboolean pixmap_filter_png     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_jpeg    (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_tiff    (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_pnm     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_bmp     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_tga     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_gif     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_jpeg2000(const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_jpegxl  (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_pcx     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_xpm     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_ras     (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_icns    (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_webp    (const GwyFileDetectInfo *fileinfo);
static gboolean pixmap_filter_heif    (const GwyFileDetectInfo *fileinfo);

static const GwyEnum map_types[PIXMAP_MAP_NTYPES] = {
    { N_("All channels"), PIXMAP_MAP_ALL,   },
    { N_("Red"),          PIXMAP_MAP_RED,   },
    { N_("Green"),        PIXMAP_MAP_GREEN, },
    { N_("Blue"),         PIXMAP_MAP_BLUE,  },
    { N_("Gray"),         PIXMAP_MAP_GREY,  },
    { N_("Value (max)"),  PIXMAP_MAP_VALUE, },
    { N_("RGB sum"),      PIXMAP_MAP_SUM,   },
    { N_("Luma"),         PIXMAP_MAP_LUMA,  },
    { N_("Hue"),          PIXMAP_MAP_HUE,   },
    { N_("Alpha"),        PIXMAP_MAP_ALPHA, },
};

/* Use a whitelist of safe formats for which we have at least basic weed-out pre-detection function.  GdkPixbuf
 * loaders tend to accept any rubbish as their format and then crash completely surprised when it isn't. */
static const PixmapKnownFormat known_formats[] = {
    { "png",      N_("Portable Network Graphics (.png)"),          pixmap_filter_png,      },
    { "jpeg",     N_("JPEG (.jpeg,.jpg)"),                         pixmap_filter_jpeg,     },
#ifndef __WIN64
    /* The TIFF loader (supposedly GDI-based) crashes on Win64.  Unclear why.  TIFF is madness.  There is a fallback
     * GwyTIFF-based loader in hdrimage which will take over when we exclude the GdkPixbuf-based one.  */
    { "tiff",     N_("TIFF (.tiff,.tif)"),                         pixmap_filter_tiff,     },
#endif
    { "pnm",      N_("Portable Pixmap (.ppm,.pnm)"),               pixmap_filter_pnm,      },
    { "bmp",      N_("Windows or OS2 Bitmap (.bmp)"),              pixmap_filter_bmp,      },
    { "tga",      N_("TARGA (.tga,.targa)"),                       pixmap_filter_tga,      },
    { "gif",      N_("Graphics Interchange Format GIF (.gif)"),    pixmap_filter_gif,      },
    { "jpeg2000", N_("JPEG 2000 (.jpx)"),                          pixmap_filter_jpeg2000, },
    { "jxl",      N_("JPEG XL (.jxl)"),                            pixmap_filter_jpegxl,   },
    { "pcx",      N_("PCX (.pcx)"),                                pixmap_filter_pcx,      },
    { "xpm",      N_("X Pixmap (.xpm)"),                           pixmap_filter_xpm,      },
    { "ras",      N_("Sun raster image (.ras)"),                   pixmap_filter_ras,      },
    { "icns",     N_("Apple icon (.icns)"),                        pixmap_filter_icns,     },
    { "webp",     N_("WebP (.webp)"),                              pixmap_filter_webp,     },
    { "heif",     N_("High Efficiency Image File Format (.heif)"), pixmap_filter_heif,     },
};

/* List of PixmapFormatInfo for all formats.  Created in module_register() and never freed. */
static GSList *pixmap_formats = NULL;

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports data from low-depth pixmap images (PNG, TIFF, JPEG, ...).  The set of available formats depends on "
       "available GDK pixbuf loaders."),
    "Yeti <yeti@gwyddion.net>",
    "10.2",
    "David Nečas (Yeti)",
    "2004-2014",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    GSList *formats, *l;

    formats = gdk_pixbuf_get_formats();
    for (l = formats; l; l = g_slist_next(l)) {
        GdkPixbufFormat *pixbuf_format = (GdkPixbufFormat*)l->data;
        const PixmapKnownFormat *known_format = NULL;
        PixmapFormatInfo *format_info;
        gchar *fmtname;
        guint i;

        fmtname = gdk_pixbuf_format_get_name(pixbuf_format);
        if (gwy_strequal(fmtname, "heif/avif"))
            gwy_assign_string(&fmtname, "heif");
        gwy_debug("Found format %s", fmtname);
        for (i = 0; i < G_N_ELEMENTS(known_formats); i++) {
            if (gwy_strequal(fmtname, known_formats[i].name)) {
                known_format = known_formats + i;
                break;
            }
        }
        if (!known_format) {
            gwy_debug("Ignoring GdkPixbuf format %s because it is not on the whitelist.", fmtname);
            g_free(fmtname);
            continue;
        }

        gwy_debug("Format %s is known and whitelisted.  Proceeding.", fmtname);
        format_info = g_new0(PixmapFormatInfo, 1);
        format_info->name = fmtname;
        format_info->pixbuf_format = pixbuf_format;
        /* Copy from static data. */
        format_info->description = known_format->description;
        format_info->filter_func = known_format->filter_func;

        gwy_debug("Found GdkPixbuf loader for new type: %s", fmtname);
        gwy_file_func_register(format_info->name, format_info->description, &pixmap_detect, &pixmap_load, NULL, NULL);
        pixmap_formats = g_slist_append(pixmap_formats, format_info);
    }

    g_slist_free(formats);

    return TRUE;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    /* Try to keep this in sync with hrdimage which uses the same keys. */
    gwy_param_def_set_function_name(paramdef, "pixmap");
    gwy_param_def_add_gwyenum(paramdef, PARAM_MAP_TYPE, "maptype", _("Use"),
                              map_types, G_N_ELEMENTS(map_types), PIXMAP_MAP_VALUE);
    gwy_param_def_add_double(paramdef, PARAM_HUE_OFFSET, "hue_offset", _("_Hue offset"), 0.0, 6.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_XREAL, "xreal", _("_Horizontal size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YREAL, "yreal", _("_Vertical size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ZREAL, "zreal", _("_Z-scale (per sample unit)"),
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_XYMEASUREEQ, "xymeasureeq", _("_Square pixels"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_SIZE_IN_PIXELS, "size_in_pixels", _("Just use _pixels"), FALSE);
    gwy_param_def_add_unit(paramdef, PARAM_XYUNIT, "xyunit", _("_Dimensions unit"), NULL);
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("_Value unit"), NULL);
    return paramdef;
}

static gint
pixmap_detect(const GwyFileDetectInfo *fileinfo,
              gboolean only_name,
              const gchar *name)
{
    GdkPixbufLoader *loader;
    GError *err = NULL;
    PixmapFormatInfo *format_info;
    const gchar *loadername;
    gint score;

    if (only_name)
        return 0;

    gwy_debug("Running detection for file type %s", name);

    format_info = find_format(name);
    g_return_val_if_fail(format_info, 0);

    /* This is not really correct, but no one is going to import data from such a small valid image anyway. */
    if (fileinfo->buffer_len < 32)
        return 0;

    /* GdkPixbuf does a terrible job regarding detection so we do some sanity check ourselves */
    if (!format_info->filter_func(fileinfo))
        return 0;

    loadername = (gwy_strequal(name, "heif") ? "heif/avif" : name);
    gwy_debug("Creating a loader for type %s", loadername);
    loader = gdk_pixbuf_loader_new_with_type(loadername, NULL);
    gwy_debug("Loader for type %s: %p", loadername, loader);
    if (!loader)
        return 0;

    /* The TIFF loaders (both libTIFF and GDI-based) seem to crash on broken TIFFs a way too often.  Do not try to
     * feed anything to it just accept the file is a TIFF and hope some other loader of a TIFF-based format will claim
     * it with a higher score. */
    score = 70;
    if (gwy_strequal(loadername, "tiff")) {
        gwy_debug("Avoiding feeding data to TIFF loader, calling gdk_pixbuf_loader_close().");
        gdk_pixbuf_loader_close(loader, NULL);
        gwy_debug("Unreferencing the TIFF loader");
        g_object_unref(loader);
        gwy_debug("Returning score %d for TIFF", score - 10);
        return score - 10;
    }

    /* For sane readers, try to feed the start of the file and see if it fails.
     * Success rarely means anything though. */
    if (!gdk_pixbuf_loader_write(loader, fileinfo->head, fileinfo->buffer_len, &err)) {
        gwy_debug("%s", err->message);
        g_clear_error(&err);
        score = 0;
    }
    gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);

    return score;
}

static GwyContainer*
pixmap_load(const gchar *filename,
            GwyRunType runtype,
            GError **error,
            const gchar *name)
{
    static const PixmapMapType rgbtypes[] = { PIXMAP_MAP_RED, PIXMAP_MAP_GREEN, PIXMAP_MAP_BLUE, PIXMAP_MAP_ALPHA };
    static const PixmapMapType greytypes[] = { PIXMAP_MAP_GREY, PIXMAP_MAP_ALPHA };

    GwyContainer *data = NULL;
    GdkPixbuf *pixbuf;
    guchar *pixels, *p;
    gint bpp, i, j, width, height, rowstride, nimages;
    gboolean has_alpha;
    GwyParams *params;
    PixmapMapType maptype;
    const PixmapMapType *imgtypes;
    GwyDialogOutcome outcome;
    ModuleArgs args;

    gwy_clear(&args, 1);
    if (!(pixbuf = pixmap_load_pixbuf(filename, name, error)))
        return NULL;
    args.pixbuf = pixbuf;

    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    bpp = has_alpha ? 4 : 3;

    /* Check which value mapping methods seem feasible. */
    for (i = 0; i < height && !args.flags; i++) {
        p = pixels + i*rowstride;
        for (j = 0; j < width; j++) {
            guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];
            if ((green ^ red) | (red ^ blue)) {
                args.flags |= PIXMAP_HAS_COLOURS;
                break;
            }
        }
    }
    if (has_alpha)
        args.flags |= PIXMAP_HAS_ALPHA;

    args.params = params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args);

    if (runtype == GWY_RUN_INTERACTIVE) {
        outcome = run_gui(&args, name);
        gwy_params_save_to_settings(params);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto end;
        }
    }

    data = gwy_container_new();
    maptype = gwy_params_get_enum(params, PARAM_MAP_TYPE);
    if (maptype == PIXMAP_MAP_ALL) {
        if (args.flags & PIXMAP_HAS_COLOURS) {
            imgtypes = rgbtypes;
            nimages = 3;
        }
        else {
            imgtypes = greytypes;
            nimages = 1;
        }
        if (args.flags & PIXMAP_HAS_ALPHA)
            nimages++;
    }
    else {
        imgtypes = &maptype;
        nimages = 1;
    }

    for (i = 0; i < nimages; i++) {
        pixmap_set_field(data, i, &args, imgtypes[i]);
        pixmap_add_import_log(data, i, name, filename);
    }

end:
    g_object_unref(pixbuf);
    g_object_unref(params);

    return data;
}

static GdkPixbuf*
pixmap_load_pixbuf(const gchar *filename,
                   const gchar *name,
                   GError **error)
{
    enum { buffer_length = 4096 };
    guchar pixmap_buf[buffer_length];
    GdkPixbufLoader *loader;
    GdkPixbuf *pixbuf;
    PixmapFormatInfo *format_info;
    const gchar *loadername;
    GError *err = NULL;
    FILE *fh;
    guint n;

    gwy_debug("Loading <%s> as %s", filename, name);

    format_info = find_format(name);
    if (!format_info) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_UNIMPLEMENTED,
                    _("Pixmap has not registered file type `%s'."), name);
        return NULL;
    }

    if (!(fh = gwy_fopen(filename, "rb"))) {
        err_READ(error);
        return NULL;
    }

    loadername = (gwy_strequal(name, "heif") ? "heif/avif" : name);
    gwy_debug("Creating a loader for type %s", loadername);
    loader = gdk_pixbuf_loader_new_with_type(loadername, &err);
    if (!loader) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cannot get pixbuf loader: %s."), err->message);
        g_clear_error(&err);
        fclose(fh);
        return NULL;
    }

    gwy_debug("Reading file content.");
    do {
        n = fread(pixmap_buf, 1, buffer_length, fh);
        gwy_debug("loaded %u bytes", n);
        if (!gdk_pixbuf_loader_write(loader, pixmap_buf, n, &err)) {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("Pixbuf loader refused data: %s."), err->message);
            g_clear_error(&err);
            g_object_unref(loader);
            fclose(fh);
            return NULL;
        }
    } while (n == buffer_length);
    fclose(fh);

    gwy_debug("Closing the loader.");
    if (!gdk_pixbuf_loader_close(loader, &err)) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Pixbuf loader refused data: %s."), err->message);
        g_clear_error(&err);
        g_object_unref(loader);
        return NULL;
    }

    gwy_debug("Trying to get the pixbuf.");
    pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    gwy_debug("Pixbuf is: %p.", pixbuf);
    g_assert(pixbuf);
    g_object_ref(pixbuf);
    gwy_debug("Finalizing loader.");
    g_object_unref(loader);

    return pixbuf;
}

static void
pixmap_set_field(GwyContainer *container,
                 gint id,
                 ModuleArgs *args,
                 PixmapMapType maptype)
{
    GwyParams *params = args->params;
    GdkPixbuf *pixbuf = args->pixbuf;
    GwyDataField *field;
    gint power10xy, power10z;

    field = gwy_data_field_new(gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), 1.0, 1.0, FALSE);
    pixmap_pixbuf_to_field(pixbuf, field, maptype, gwy_params_get_double(params, PARAM_HUE_OFFSET)/6.0);

    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), gwy_params_get_unit(params, PARAM_XYUNIT, &power10xy));
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), gwy_params_get_unit(params, PARAM_ZUNIT, &power10z));
    gwy_data_field_set_xreal(field, gwy_params_get_double(params, PARAM_XREAL)*pow10(power10xy));
    gwy_data_field_set_yreal(field, gwy_params_get_double(params, PARAM_YREAL)*pow10(power10xy));
    gwy_data_field_multiply(field, gwy_params_get_double(params, PARAM_ZREAL)*pow10(power10z));

    gwy_container_pass_object(container, gwy_app_get_data_key_for_id(id), field);
    gwy_container_set_const_string(container,
                                   gwy_app_get_data_title_key_for_id(id),
                                   gwy_enum_to_string(maptype, map_types, PIXMAP_MAP_NTYPES));
}

static void
pixmap_pixbuf_to_field(GdkPixbuf *pixbuf,
                       GwyDataField *field,
                       PixmapMapType maptype,
                       gdouble hue_offset)
{
    gint width, height, rowstride, i, bpp;
    guchar *pixels;
    gdouble *val;

    gwy_debug("%d", maptype);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    width = gdk_pixbuf_get_width(pixbuf);
    height = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    bpp = gdk_pixbuf_get_has_alpha(pixbuf) ? 4 : 3;
    gwy_data_field_resample(field, width, height, GWY_INTERPOLATION_NONE);
    val = gwy_data_field_get_data(field);

#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            private(i) \
            shared(pixels,val,width,height,rowstride,bpp,maptype,hue_offset)
#endif
    for (i = 0; i < height; i++) {
        guchar *p = pixels + i*rowstride;
        gdouble *r = val + i*width;
        gint j;

        switch (maptype) {
            case PIXMAP_MAP_ALPHA:
            p++;
            case PIXMAP_MAP_BLUE:
            p++;
            case PIXMAP_MAP_GREEN:
            p++;
            case PIXMAP_MAP_RED:
            case PIXMAP_MAP_GREY:
            for (j = 0; j < width; j++)
                r[j] = p[bpp*j]/255.0;
            break;

            case PIXMAP_MAP_VALUE:
            for (j = 0; j < width; j++) {
                guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];
                guchar v = MAX(MAX(red, green), blue);

                r[j] = v/255.0;
            }
            break;

            case PIXMAP_MAP_SUM:
            for (j = 0; j < width; j++) {
                guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];

                r[j] = (red + green + blue)/(3*255.0);
            }
            break;

            case PIXMAP_MAP_LUMA:
            for (j = 0; j < width; j++) {
                guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];

                r[j] = (0.2126*red + 0.7152*green + 0.0722*blue)/255.0;
            }
            break;

            case PIXMAP_MAP_HUE:
            for (j = 0; j < width; j++) {
                guchar red = p[bpp*j], green = p[bpp*j+1], blue = p[bpp*j+2];
                gint cmax = MAX(MAX(red, green), blue);
                gint cmin = MIN(MIN(red, green), blue);
                gint delta = cmax - cmin;

                if (!delta)
                    r[j] = 0.0;
                else if (cmax == red)
                    r[j] = fmod(1.0/6.0*(green - blue)/delta + 1.0, 1.0);
                else if (cmax == green)
                    r[j] = 1.0/6.0*(blue - red)/delta + 1.0/3.0;
                else
                    r[j] = 1.0/6.0*(red - green)/delta + 2.0/3.0;

                r[j] -= hue_offset;
                if (r[j] < 0.0)
                    r[j] += 1.0;
            }
            break;

            default:
            g_assert_not_reached();
            break;
        }
    }
}

static const gchar*
describe_channels(guint flags)
{
    if (flags & PIXMAP_HAS_COLOURS)
        return (flags & PIXMAP_HAS_ALPHA) ? "R, G, B, A" : "R, G, B";
    else
        return (flags & PIXMAP_HAS_ALPHA) ? "G, A" : "G";
}

static GwyDialogOutcome
run_gui(ModuleArgs *args, const gchar *name)
{
    ModuleGUI gui;
    GwyParamTable *table, *infotable;
    GtkWidget *align, *hbox, *view, *label;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    GwyResults *results;
    GwyDataField *field;
    gint xres, yres, sxres, syres;
    gchar *s, *title;
    gdouble zoom;

    gwy_clear(&gui, 1);
    gui.args = args;

    xres = gdk_pixbuf_get_width(args->pixbuf);
    yres = gdk_pixbuf_get_height(args->pixbuf);

    zoom = PREVIEW_SIZE/(gdouble)MAX(xres, yres);
    sxres = MAX(GWY_ROUND(zoom*xres), 1);
    syres = MAX(GWY_ROUND(zoom*yres), 1);
    gui.small_pixbuf = gdk_pixbuf_scale_simple(args->pixbuf, sxres, syres, GDK_INTERP_TILES);

    gui.data = gwy_container_new();
    field = gwy_data_field_new(sxres, syres, sxres, syres, TRUE);
    gwy_container_pass_object(gui.data, gwy_app_get_data_key_for_id(0), field);

    s = g_ascii_strup(name, -1);
    /* TRANSLATORS: Dialog title; %s is PNG, TIFF, ... */
    title = g_strdup_printf(_("Import %s"), s);
    g_free(s);
    gui.dialog = gwy_dialog_new(title);
    g_free(title);
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(dialog, hbox, FALSE, FALSE, 0);

    align = gtk_alignment_new(0.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, TRUE, 0);

    results = gwy_results_new();
    gwy_results_add_value(results, "xres", N_("Horizontal size"),
                          "type", GWY_RESULTS_VALUE_INT,
                          "unit-str", "px",
                          NULL);
    gwy_results_add_value(results, "yres", N_("Vertical size"),
                          "type", GWY_RESULTS_VALUE_INT,
                          "unit-str", "px",
                          NULL);
    gwy_results_add_value_str(results, "channels", N_("Channels"));
    gwy_results_fill_values(results, "xres", xres, "yres", yres, "channels", describe_channels(args->flags), NULL);

    infotable = gwy_param_table_new(args->params);
    gwy_param_table_append_header(infotable, -1, _("Image Information"));
    gwy_param_table_append_results(infotable, WIDGET_IMAGE_INFO, results, "xres", "yres", "channels", NULL);
    /* TODO: If the file contains resultion/size in physical units, show it here. */
    gwy_param_table_results_fill(infotable, WIDGET_IMAGE_INFO);
    gwy_dialog_add_param_table(dialog, infotable);
    gtk_container_add(GTK_CONTAINER(align), gwy_param_table_widget(infotable));

    align = gtk_alignment_new(1.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, TRUE, 0);

    view = gwy_create_preview(gui.data, 0, PREVIEW_SIZE, FALSE);
    gtk_container_add(GTK_CONTAINER(align), view);

    hbox = gwy_hbox_new(20);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    table = gui.table_lateral = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Physical Dimensions"));
    gwy_param_table_append_checkbox(table, PARAM_SIZE_IN_PIXELS);
    gwy_param_table_append_entry(table, PARAM_XREAL);
    gwy_param_table_append_entry(table, PARAM_YREAL);
    gwy_param_table_append_checkbox(table, PARAM_XYMEASUREEQ);
    gwy_param_table_append_unit_chooser(table, PARAM_XYUNIT);
    /* TODO: Add a button for taking dimensions from file. */
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_values = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Value Mapping"));
    gwy_param_table_append_entry(table, PARAM_ZREAL);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);
    gwy_param_table_append_combo(table, PARAM_MAP_TYPE);
    gwy_param_table_set_unitstr(table, PARAM_MAP_TYPE, _("as data"));
    gwy_param_table_combo_set_filter(table, PARAM_MAP_TYPE, mapping_type_filter, GUINT_TO_POINTER(args->flags), NULL);
    if (args->flags & PIXMAP_HAS_COLOURS)
        gwy_param_table_append_slider(table, PARAM_HUE_OFFSET);
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    if (args->flags & PIXMAP_HAS_COLOURS) {
        label = gtk_label_new(_("Warning: Colorful images cannot be reliably mapped to meaningful values."));
        gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
        gtk_misc_set_padding(GTK_MISC(label), 4, 6);
        gwy_dialog_add_content(dialog, label, FALSE, FALSE, 0);
    }

    g_signal_connect_swapped(infotable, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_lateral, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_values, "param-changed", G_CALLBACK(param_changed), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.small_pixbuf);
    g_object_unref(gui.data);

    return outcome;
}

static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyDataField *field = gwy_container_get_object(gui->data, gwy_app_get_data_key_for_id(0));
    PixmapMapType maptype = gwy_params_get_enum(params, PARAM_MAP_TYPE);

    if (maptype == PIXMAP_MAP_ALL)
        maptype = (args->flags & PIXMAP_HAS_COLOURS) ? PIXMAP_MAP_RED : PIXMAP_MAP_GREY;
    pixmap_pixbuf_to_field(gui->small_pixbuf, field, maptype, gwy_params_get_double(params, PARAM_HUE_OFFSET)/6.0);
    gwy_data_field_data_changed(field);
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    GwyParams *params = args->params;
    PixmapMapType maptype = gwy_params_get_enum(params, PARAM_MAP_TYPE);
    gboolean size_in_pixels = gwy_params_get_boolean(params, PARAM_SIZE_IN_PIXELS);
    gboolean xymeasureeq = gwy_params_get_boolean(params, PARAM_XYMEASUREEQ);
    gint xres = gdk_pixbuf_get_width(args->pixbuf);
    gint yres = gdk_pixbuf_get_height(args->pixbuf);
    GwySIUnit *unit;
    gint power10;
    GwySIValueFormat *vf = NULL;

    if (id < 0 || id == PARAM_MAP_TYPE)
        gwy_param_table_set_sensitive(gui->table_values, PARAM_HUE_OFFSET, maptype == PIXMAP_MAP_HUE);

    if (id < 0 || id == PARAM_SIZE_IN_PIXELS) {
        if (size_in_pixels) {
            gwy_param_table_set_string(gui->table_lateral, PARAM_XYUNIT, NULL);
            gwy_param_table_set_boolean(gui->table_lateral, PARAM_XYMEASUREEQ, (xymeasureeq = TRUE));
            gwy_param_table_set_double(gui->table_lateral, PARAM_XREAL, xres);
            gwy_param_table_set_double(gui->table_lateral, PARAM_YREAL, yres);
            id = -1;
        }
        gwy_param_table_set_sensitive(gui->table_lateral, PARAM_XYUNIT, !size_in_pixels);
        gwy_param_table_set_sensitive(gui->table_lateral, PARAM_XREAL, !size_in_pixels);
        gwy_param_table_set_sensitive(gui->table_lateral, PARAM_YREAL, !size_in_pixels);
        gwy_param_table_set_sensitive(gui->table_lateral, PARAM_XYMEASUREEQ, !size_in_pixels);
    }

    if (xymeasureeq) {
        if (id < 0 || id == PARAM_XYMEASUREEQ || id == PARAM_XREAL) {
            gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
            gwy_param_table_set_double(gui->table_lateral, PARAM_YREAL, yres*xreal/xres);
        }
        else if (id == PARAM_YREAL) {
            gdouble yreal = gwy_params_get_double(params, PARAM_YREAL);
            gwy_param_table_set_double(gui->table_lateral, PARAM_XREAL, xres*yreal/yres);
        }
    }

    if (id < 0 || id == PARAM_XYUNIT) {
        unit = gwy_params_get_unit(params, PARAM_XYUNIT, &power10);
        vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, vf);
        gwy_param_table_set_unitstr(gui->table_lateral, PARAM_XREAL, vf->units);
        gwy_param_table_set_unitstr(gui->table_lateral, PARAM_YREAL, vf->units);
    }

    if (id < 0 || id == PARAM_ZUNIT) {
        unit = gwy_params_get_unit(params, PARAM_ZUNIT, &power10);
        vf = gwy_si_unit_get_format_for_power10(unit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, vf);
        gwy_param_table_set_unitstr(gui->table_values, PARAM_ZREAL, vf->units);
    }

    GWY_SI_VALUE_FORMAT_FREE(vf);

    if (id < 0 || id == PARAM_MAP_TYPE || id == PARAM_HUE_OFFSET)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static gboolean
mapping_type_filter(const GwyEnum *enumval, gpointer user_data)
{
    guint flags = GPOINTER_TO_UINT(user_data);

    if (enumval->value == PIXMAP_MAP_ALPHA)
        return flags & PIXMAP_HAS_ALPHA;
    if (enumval->value == PIXMAP_MAP_ALL)
        return flags;
    if (enumval->value == PIXMAP_MAP_GREY)
        return !(flags & PIXMAP_HAS_COLOURS);
    return flags & PIXMAP_HAS_COLOURS;
}

static void
pixmap_add_import_log(GwyContainer *data,
                      gint id,
                      const gchar *filetype,
                      const gchar *filename)
{
    GwyContainer *settings;
    GQuark quark;
    gchar *myfilename = NULL, *fskey, *qualname;

    g_return_if_fail(filename);
    g_return_if_fail(filetype);
    g_return_if_fail(data);

    if (g_utf8_validate(filename, -1, NULL))
        myfilename = g_strdup(filename);
    if (!myfilename)
        myfilename = g_filename_to_utf8(filename, -1, NULL, NULL, NULL);
    if (!myfilename)
        myfilename = g_strescape(filename, NULL);

    fskey = g_strdup_printf("/module/%s/filename", filetype);
    quark = g_quark_from_string(fskey);
    g_free(fskey);

    /* Eats myfilename. */
    settings = gwy_app_settings_get();
    gwy_container_set_string(settings, quark, myfilename);

    qualname = g_strconcat("file::", filetype, NULL);
    gwy_app_channel_log_add(data, -1, id, qualname, NULL);
    g_free(qualname);

    /* We know pixmap functions have no such setting as "filename". */
    gwy_container_remove(settings, quark);
}

static PixmapFormatInfo*
find_format(const gchar *name)
{
    GSList *l;

    for (l = pixmap_formats; l; l = g_slist_next(l)) {
        PixmapFormatInfo *format_info = (PixmapFormatInfo*)l->data;
        if (gwy_strequal(format_info->name, name))
            return format_info;
    }

    return NULL;
}

static void
sanitise_params(ModuleArgs *args)
{
    GwyParams *params = args->params;
    PixmapMapType maptype = gwy_params_get_enum(params, PARAM_MAP_TYPE);
    gint xres = gdk_pixbuf_get_width(args->pixbuf);
    gint yres = gdk_pixbuf_get_height(args->pixbuf);
    guint i;

    if (gwy_params_get_boolean(params, PARAM_SIZE_IN_PIXELS)) {
        gwy_params_set_unit(params, PARAM_XYUNIT, NULL);
        gwy_params_set_boolean(params, PARAM_XYMEASUREEQ, TRUE);
        gwy_params_set_double(params, PARAM_XREAL, xres);
        gwy_params_set_double(params, PARAM_YREAL, yres);
    }
    else if (gwy_params_get_boolean(params, PARAM_XYMEASUREEQ)) {
        gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
        gwy_params_set_double(params, PARAM_YREAL, yres*xreal/xres);
    }

    for (i = 0; i < G_N_ELEMENTS(map_types); i++) {
        const GwyEnum *enumval = map_types + i;
        if (enumval->value == maptype) {
            if (!mapping_type_filter(enumval, GUINT_TO_POINTER(args->flags))) {
                gwy_params_set_enum(params, PARAM_MAP_TYPE,
                                    (args->flags & PIXMAP_HAS_COLOURS) ? PIXMAP_MAP_VALUE : PIXMAP_MAP_GREY);
            }
            return;
        }
    }
    g_assert_not_reached();
}

static gboolean
pixmap_filter_png(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 8 && !memcmp(fileinfo->head, "\x89PNG\r\n\x1a\n", 8);
}

static gboolean
pixmap_filter_jpeg(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 2 && !memcmp(fileinfo->head, "\xff\xd8", 2);
}

/* Unused on Win64 */
G_GNUC_UNUSED
static gboolean
pixmap_filter_tiff(const GwyFileDetectInfo *fileinfo)
{
    /* The pixbuf loader is unlikely to load BigTIFFs any time soon. */
    GwyTIFFVersion version = GWY_TIFF_CLASSIC;
    gwy_debug("Checking TIFF header");
    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len, &version, NULL))
        return FALSE;
    gwy_debug("TIFF header OK (type %.2s)", fileinfo->head);
    return TRUE;
}

static gboolean
pixmap_filter_pnm(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 2 && fileinfo->head[0] == 'P' && g_ascii_isdigit(fileinfo->head[1]);
}

static gboolean
pixmap_filter_bmp(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 2 && !memcmp(fileinfo->head, "BM", 2);
}

static gboolean
pixmap_filter_tga(const GwyFileDetectInfo *fileinfo)
{
    guint8 cmtype, dtype;

    if (fileinfo->buffer_len < 2)
        return FALSE;

    cmtype = fileinfo->head[1];
    dtype = fileinfo->head[2];

    if (dtype == 1 || dtype == 9 || dtype == 32 || dtype == 33) {
        if (cmtype != 1)
            return FALSE;
    }
    else if (dtype == 2 || dtype == 3 || dtype == 10 || dtype == 11) {
        if (cmtype != 0)
            return FALSE;
    }
    else
        return FALSE;

    return TRUE;
}

static gboolean
pixmap_filter_gif(const GwyFileDetectInfo *fileinfo)
{
    return (fileinfo->buffer_len >= 4 && !memcmp(fileinfo->head, "GIF8", 4));
}

static gboolean
pixmap_filter_jpeg2000(const GwyFileDetectInfo *fileinfo)
{
    return (fileinfo->buffer_len >= 23
            && !memcmp(fileinfo->head,
                       "\x00\x00\x00\x0C\x6A\x50\x20\x20\x0D\x0A\x87\x0A\x00\x00\x00\x14\x66\x74\x79\x70\x6A\x70\x32",
                       23));
}

static gboolean
pixmap_filter_jpegxl(const GwyFileDetectInfo *fileinfo)
{
    /* The two-byte one is silly, but used for standalone images in the wild. */
    return (fileinfo->buffer_len >= 12
            && (!memcmp(fileinfo->head, "\x00\x00\x00\x0C\x4A\x58\x4C\x20\x0D\x0A\x87\x0A", 12)
                || !memcmp(fileinfo->head, "\xFF\x0A", 2)));
}

static gboolean
pixmap_filter_pcx(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 2 && fileinfo->head[0] == '\x0a' && fileinfo->head[1] <= 0x05;
}

static gboolean
pixmap_filter_xpm(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 9 && !memcmp(fileinfo->head, "/* XPM */", 9);
}

static gboolean
pixmap_filter_ras(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 4 && !memcmp(fileinfo->head, "\x59\xa6\x6a\x95", 4);
}

static gboolean
pixmap_filter_icns(const GwyFileDetectInfo *fileinfo)
{
    return fileinfo->buffer_len >= 4 && !memcmp(fileinfo->head, "icns", 4);
}

static gboolean
pixmap_filter_webp(const GwyFileDetectInfo *fileinfo)
{
    return (fileinfo->buffer_len >= 15
            && !memcmp(fileinfo->head, "RIFF", 4)
            && !memcmp(fileinfo->head + 8, "WEBPVP8", 7));
}

static gboolean
pixmap_filter_heif(const GwyFileDetectInfo *fileinfo)
{
    return (fileinfo->buffer_len >= 12
            && !memcmp(fileinfo->head + 4, "ftyp", 4)
            && (!memcmp(fileinfo->head + 8, "mif1", 4)
                || !memcmp(fileinfo->head + 8, "heic", 4)
                || !memcmp(fileinfo->head + 8, "heix", 4)));
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
