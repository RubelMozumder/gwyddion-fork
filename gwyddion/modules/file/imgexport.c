/*
 *  $Id: imgexport.c 26269 2024-04-08 08:40:32Z yeti-dn $
 *  Copyright (C) 2014-2023 David Necas (Yeti).
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

/* TODO:
 * - alignment of image title (namely when it is on the top)
 * - JPEG2000 support? seems possible but messy
 */

/* XXX: There is some JPEG XL code, but the libjxl library is changing API quickly (inlcuding removal of deprecated
 * functions) and had to be disabled. So HAVE_JXL is never defined by configure. */

/**
 * [FILE-MAGIC-MISSING]
 * Export only.  Avoding clash with a standard file format.  Also, magic is in pixmap.c.
 **/

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <glib/gstdio.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <cairo.h>

/* We want cairo_ps_surface_set_eps().  So if we don't get it we just pretend cairo doesn't have the PS surface at
 * all. */
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 6, 0)
#undef CAIRO_HAS_PS_SURFACE
#endif

#ifdef CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>
#endif

#ifdef CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>
#endif

#ifdef CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>
#endif

#ifdef HAVE_PNG
#include <png.h>
#ifdef HAVE_ZLIB
#include <zlib.h>
#else
#define Z_BEST_COMPRESSION 9
#endif
#endif

#ifdef HAVE_WEBP
#include <webp/encode.h>
#endif

#ifdef HAVE_JXL
#include <jxl/version.h>
#include <jxl/encode.h>
#endif

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwyddion/gwyversion.h>
#include <libgwyddion/gwydebugobjects.h>
#include <libprocess/stats.h>
#include <libprocess/spline.h>
#include <libdraw/gwypixfield.h>
#include <libgwydgets/gwydgets.h>
#include <libgwymodule/gwymodule-file.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "gwytiff.h"
#include "image-keys.h"
#include "../process/preview.h"

#define APP_RANGE_KEY "/app/default-range-type"

#define mm2pt (72.0/25.4)
#define pangoscale ((gdouble)PANGO_SCALE)
#define fixzero(x) (fabs(x) < 1e-14 ? 0.0 : (x))

enum {
    RESPONSE_AUTO_LENGTH = 1000,
};

enum {
    COLUMN_SEL_NAME,
    COLUMN_SEL_TYPE,
    COLUMN_SEL_OBJECTS,
};

enum {
    COLUMN_PRESET_NAME,
    COLUMN_PRESET_LATERAL,
    COLUMN_PRESET_VALUE,
    COLUMN_PRESET_TITLE,
};

enum {
    PRESET_BUTTON_LOAD,
    PRESET_BUTTON_STORE,
    PRESET_BUTTON_RENAME,
    PRESET_BUTTON_DELETE,
    PRESET_NBUTTONS,
};

enum {
    PARAM_MODE,
    PARAM_ZOOM,
    PARAM_PXWIDTH,
    PARAM_FONT,
    PARAM_FONT_SIZE,
    PARAM_LINE_WIDTH,
    PARAM_BORDER_WIDTH,
    PARAM_TICK_LENGTH,
    PARAM_SCALE_FONT,
    PARAM_DECOMMA,
    PARAM_LINETEXT_COLOR,
    PARAM_BG_COLOR,
    PARAM_TRANSPARENT_BG,
    PARAM_XYTYPE,
    PARAM_INSET_LENGTH,
    PARAM_INSET_POS,
    PARAM_INSET_XGAP,
    PARAM_INSET_YGAP,
    PARAM_INSET_COLOR,
    PARAM_INSET_OUTLINE_COLOR,
    PARAM_INSET_OUTLINE_WIDTH,
    PARAM_INSET_DRAW_TICKS,
    PARAM_INSET_DRAW_LABEL,
    PARAM_INSET_DRAW_TEXT_ABOVE,
    PARAM_INTERPOLATION,
    PARAM_DRAW_FRAME,
    PARAM_DRAW_MASK,
    PARAM_DRAW_MASKKEY,
    PARAM_MASK_KEY,
    PARAM_MASKKEY_GAP,
    PARAM_ZTYPE,
    PARAM_FMSCALE_GAP,
    PARAM_FIX_FMSCALE_PRECISION,
    PARAM_FMSCALE_PRECISION,
    PARAM_FIX_KILO_THRESHOLD,
    PARAM_KILO_THRESHOLD,
    PARAM_TITLE_TYPE,
    PARAM_TITLE_GAP,
    PARAM_UNITS_IN_TITLE,
    PARAM_DRAW_SELECTION,
    PARAM_SEL_COLOR,
    PARAM_SEL_OUTLINE_COLOR,
    PARAM_SELECTION,
    PARAM_SEL_OUTLINE_WIDTH,
    PARAM_SEL_NUMBER_OBJECTS,
    PARAM_SEL_POINT_RADIUS,
    PARAM_SEL_LINE_THICKNESS,

    PARAM_VECTOR_WIDTH,
    PARAM_VECTOR_HEIGHT,
    PARAM_PPI,
    PARAM_BITMAP_WIDTH,
    PARAM_BITMAP_HEIGHT,
    PARAM_INSET_RGB,
    PARAM_INSET_ALPHA,
    PARAM_SEL_RGB,
    PARAM_SEL_ALPHA,
    PARAM_INTERPOLATION_VECTOR,
    PARAM_PRESET,
    PARAM_ACTIVE_PAGE,

    BUTTON_AUTO_LENGTH,
};

typedef enum {
    IMGEXPORT_MODE_PRESENTATION,
    IMGEXPORT_MODE_GREY16,
} ImgExportMode;

typedef enum {
    IMGEXPORT_LATERAL_NONE,
    IMGEXPORT_LATERAL_RULERS,
    IMGEXPORT_LATERAL_INSET,
} ImgExportLateralType;

typedef enum {
    IMGEXPORT_VALUE_NONE,
    IMGEXPORT_VALUE_FMSCALE,
} ImgExportValueType;

typedef enum {
    IMGEXPORT_TITLE_NONE,
    IMGEXPORT_TITLE_TOP,
    IMGEXPORT_TITLE_FMSCALE,
} ImgExportTitleType;

typedef enum {
    INSET_POS_TOP_LEFT,
    INSET_POS_TOP_CENTER,
    INSET_POS_TOP_RIGHT,
    INSET_POS_BOTTOM_LEFT,
    INSET_POS_BOTTOM_CENTER,
    INSET_POS_BOTTOM_RIGHT,
} InsetPosType;

struct ImgExportFormat;
typedef struct _ImgExportEnv ImgExportEnv;

typedef struct {
    gdouble font_size;
    gdouble line_width;
    gdouble inset_outline_width;
    gdouble sel_outline_width;
    gdouble border_width;
    gdouble tick_length;
} SizeSettings;

typedef struct {
    gdouble x, y;
    gdouble w, h;
} ImgExportRect;

typedef struct {
    gdouble from, to, step, base;
} RulerTicks;

typedef struct {
    /* Scaled parameters */
    SizeSettings sizes;

    /* Various component sizes */
    GwySIValueFormat *vf_hruler;
    GwySIValueFormat *vf_vruler;
    GwySIValueFormat *vf_fmruler;
    RulerTicks hruler_ticks;
    RulerTicks vruler_ticks;
    RulerTicks fmruler_ticks;
    gdouble hruler_label_height;
    gdouble vruler_label_width;
    gdouble fmruler_label_width;
    gdouble fmruler_units_width;
    gdouble fmruler_label_height;
    gdouble inset_length;
    gboolean zunits_nonempty;

    /* Actual rectangles, including positions, of the image parts. */
    ImgExportRect image;
    ImgExportRect hruler;
    ImgExportRect vruler;
    ImgExportRect inset;
    ImgExportRect fmgrad;
    ImgExportRect fmruler;
    ImgExportRect title;
    ImgExportRect maskkey;

    /* Union of all above (plus maybe borders). */
    ImgExportRect canvas;
} ImgExportSizes;

struct _ImgExportEnv {
    const struct _ImgExportFormat *format;
    GwyDataField *dfield;
    GwyDataField *mask;
    GwyContainer *data;
    GArray *selections;
    GwyRGBA mask_colour;
    GwyGradient *gradient;
    GwyGradient *grey;
    gchar *title;
    gchar *selection_name;
    const gchar *decimal_symbol;
    GwyLayerBasicRangeType fm_rangetype;
    gdouble fm_min;
    gdouble fm_max;
    gboolean fm_inverted;
    gboolean has_presentation;
    gint id;
    guint xres;
    guint yres;            /* Already after realsquare resampling! */
    gboolean realsquare;
    gdouble minzoom;
    gdouble maxzoom;
    GQuark vlayer_sel_key;
    gboolean sel_line_have_layer;
    gboolean sel_point_have_layer;
    gboolean sel_path_have_layer;
    gdouble sel_line_thickness;
    gdouble sel_point_radius;
};

typedef struct {
    ImgExportEnv *env;
    GwyParams *params;
} ModuleArgs;

typedef struct {
    ModuleArgs *args;
    GtkWidget *dialog;
    GtkWidget *preview;
    GtkWidget *notebook;

    GtkWidget *mode;
    GwyParamTable *table_basic;
    GtkWidget *font;
    GwyParamTable *table_lateral;
    GQuark rb_quark;
    GSList *inset_pos;
    GtkWidget *inset_pos_label[6];
    GwyParamTable *table_value;
    GwyParamTable *table_selection;
    GwyNullStore *sel_store;
    GtkWidget *selections;

    /* Presets */
    GtkWidget *presetlist;
    GtkWidget *presetname;
    GtkWidget *preset_buttons[PRESET_NBUTTONS];
    GtkWidget *preset_error;
} ModuleGUI;

typedef void (*SelDrawFunc)(ModuleArgs *args,
                            const ImgExportSizes *sizes,
                            GwySelection *sel,
                            gdouble qx,
                            gdouble qy,
                            PangoLayout *layout,
                            GString *s,
                            cairo_t *cr);

typedef struct {
    const gchar *typename;
    const gchar *description;
    const guint *has_options;
    SelDrawFunc draw;
} ImgExportSelectionType;

typedef gboolean (*WritePixbufFunc)(GdkPixbuf *pixbuf,
                                    const gchar *name,
                                    const gchar *filename,
                                    GError **error);
typedef gboolean (*WriteImageFunc)(ModuleArgs *args,
                                   const gchar *name,
                                   const gchar *filename,
                                   GError **error);

typedef struct _ImgExportFormat {
    const gchar *description;
    const gchar *name;
    const gchar *extensions;
    WritePixbufFunc write_pixbuf;   /* If NULL, use generic GdkPixbuf func. */
    WriteImageFunc write_grey16;    /* 16bit grey */
    WriteImageFunc write_vector;    /* scalable */
    gboolean supports_transparency;
} ImgExportFormat;

static gboolean         module_register      (void);
static GwyParamDef*     define_module_params (void);
static GwyParamDef*     define_preset_params (void);
static void             add_preset_params    (GwyParamDef *paramdef);
static ImgExportFormat* find_format          (const gchar *name,
                                              gboolean cairoext);
static gint             img_export_detect    (const GwyFileDetectInfo *fileinfo,
                                              gboolean only_name,
                                              const gchar *name);
static gboolean         img_export_export    (GwyContainer *data,
                                              const gchar *filename,
                                              GwyRunType mode,
                                              GError **error,
                                              const gchar *name);
static GwyDialogOutcome run_gui              (ModuleArgs *args);
static void             dialog_response      (ModuleGUI *gui,
                                              gint response);
static void             dialog_response_after(GtkDialog *dialog,
                                              gint response,
                                              ModuleGUI *gui);
static void             preview              (gpointer user_data);
static void             param_changed        (ModuleGUI *gui,
                                              gint id);
static GdkPixbuf*       render_pixbuf        (ModuleArgs *args,
                                              const gchar *name);
static ImgExportEnv*    img_export_load_env  (const ImgExportFormat *format,
                                              GwyContainer *data,
                                              gint id);
static void             img_export_free_env  (ImgExportEnv *env);
static void             select_a_real_font   (ModuleArgs *args,
                                              GtkWidget *widget);
static gchar*           scalebar_auto_length (GwyDataField *dfield,
                                              gdouble *p);
static gdouble          inset_length_ok      (GwyDataField *dfield,
                                              const gchar *inset_length);
static void             sanitise_params      (ModuleArgs *args,
                                              gboolean full_module,
                                              gboolean is_interactive);
static void             update_presets      (void);

#define DECLARE_WRITER(type,name) \
    static gboolean write_##type##_##name(ModuleArgs *args, const gchar *name, const gchar *filename, GError **error)

#define DECLARE_PIXBUF_WRITER(name) \
    static gboolean write_pixbuf_##name(GdkPixbuf *pixbuf, const gchar *name, const gchar *filename, GError **error)

#ifdef HAVE_PNG
DECLARE_WRITER(image, png16);
#else
#define write_image_png16 NULL
#endif

/* Give a chance to pixbuf loaders if we find them at runtime. At least the webp one should support writing now. */
#ifdef HAVE_WEBP
DECLARE_PIXBUF_WRITER(webp);
#else
#define write_pixbuf_webp NULL
#endif

#ifdef HAVE_JXL
DECLARE_PIXBUF_WRITER(jxl);
DECLARE_WRITER(image, jxl16);
#else
#define write_pixbuf_jxl NULL
#define write_image_jxl16 NULL
#endif

DECLARE_WRITER(image, tiff16);
DECLARE_WRITER(image, pgm16);
DECLARE_WRITER(vector, generic);

DECLARE_PIXBUF_WRITER(generic);
DECLARE_PIXBUF_WRITER(tiff);
DECLARE_PIXBUF_WRITER(ppm);
DECLARE_PIXBUF_WRITER(bmp);
DECLARE_PIXBUF_WRITER(targa);

#define DECLARE_SELECTION_DRAWING(name) \
    static void draw_sel_##name(ModuleArgs *args, const ImgExportSizes *sizes, \
                                GwySelection *sel, gdouble qx, gdouble qy, \
                                PangoLayout *layout, GString *s, cairo_t *cr)

DECLARE_SELECTION_DRAWING(axis);
DECLARE_SELECTION_DRAWING(cross);
DECLARE_SELECTION_DRAWING(ellipse);
DECLARE_SELECTION_DRAWING(line);
DECLARE_SELECTION_DRAWING(point);
DECLARE_SELECTION_DRAWING(rectangle);
DECLARE_SELECTION_DRAWING(lattice);
DECLARE_SELECTION_DRAWING(path);

static GType preset_resource_type = 0;

static const GwyRGBA black = { 0.0, 0.0, 0.0, 1.0 };
static const GwyRGBA white = { 1.0, 1.0, 1.0, 1.0 };

static ImgExportFormat image_formats[] = {
    {
        N_("Portable Network Graphics (.png)"),
        "png", ".png", NULL, write_image_png16, NULL, TRUE,
    },
    {
        N_("JPEG (.jpeg,.jpg)"),
        "jpeg", ".jpeg,.jpg,.jpe", NULL, NULL, NULL, FALSE,
    },
    {
        N_("TIFF (.tiff,.tif)"),
        "tiff", ".tiff,.tif", write_pixbuf_tiff, write_image_tiff16, NULL, FALSE,
    },
    {
        N_("Portable Pixmap (.ppm,.pnm)"),
        "pnm", ".ppm,.pnm", write_pixbuf_ppm, write_image_pgm16, NULL, FALSE,
    },
    {
        N_("Windows or OS2 Bitmap (.bmp)"),
        "bmp", ".bmp", write_pixbuf_bmp, NULL, NULL, FALSE,
    },
    {
        N_("TARGA (.tga,.targa)"),
        "tga", ".tga,.targa", write_pixbuf_targa, NULL, NULL, FALSE,
    },
    {
        N_("WebP (.webp)"),
        "webp", ".webp", write_pixbuf_webp, NULL, NULL, TRUE,
    },
    {
        N_("JPEG XL (.jxl)"),
        "jxl", ".jxl", write_pixbuf_jxl, write_image_jxl16, NULL, TRUE,
    },
#ifdef CAIRO_HAS_PDF_SURFACE
    {
        N_("Portable document format (.pdf)"),
        "pdf", ".pdf", NULL, NULL, write_vector_generic, TRUE,
    },
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    {
        N_("Encapsulated PostScript (.eps)"),
        "eps", ".eps", NULL, NULL, write_vector_generic, TRUE,
    },
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
    {
        N_("Scalable Vector Graphics (.svg)"),
        "svg", ".svg", NULL, NULL, write_vector_generic, TRUE,
    },
#endif
};

static const guint sel_options_all[] = {
    PARAM_SEL_NUMBER_OBJECTS, PARAM_SEL_LINE_THICKNESS, PARAM_SEL_POINT_RADIUS,
};
static const guint sel_line_options[] = {
    PARAM_SEL_NUMBER_OBJECTS, PARAM_SEL_LINE_THICKNESS, 0,
};
static const guint sel_point_options[] = {
    PARAM_SEL_NUMBER_OBJECTS, PARAM_SEL_POINT_RADIUS, 0,
};
static const guint sel_path_options[] = {
    PARAM_SEL_POINT_RADIUS, 0,
};

static const ImgExportSelectionType known_selections[] =
{
    { "GwySelectionAxis",      N_("Horiz./vert. lines"), NULL,              &draw_sel_axis,      },
    /* FIXME: we should support mode, but that is actually a tool setting, not an independent property of the
     * selection itself. */
    { "GwySelectionCross",     N_("Crosses"),            NULL,              &draw_sel_cross,     },
    { "GwySelectionEllipse",   N_("Ellipses"),           NULL,              &draw_sel_ellipse,   },
    { "GwySelectionLine",      N_("Lines"),              sel_line_options,  &draw_sel_line,      },
    { "GwySelectionPoint",     N_("Points"),             sel_point_options, &draw_sel_point,     },
    { "GwySelectionRectangle", N_("Rectangles"),         NULL,              &draw_sel_rectangle, },
    { "GwySelectionLattice",   N_("Lattice"),            NULL,              &draw_sel_lattice,   },
    { "GwySelectionPath",      N_("Path"),               sel_path_options,  &draw_sel_path,      },
};

static const GwyEnum lateral_types[] = {
    { N_("ruler|_None"),      IMGEXPORT_LATERAL_NONE,   },
    { N_("_Rulers"),          IMGEXPORT_LATERAL_RULERS, },
    { N_("_Inset scale bar"), IMGEXPORT_LATERAL_INSET,  },
};
static const GwyEnum value_types[] = {
    { N_("ruler|_None"),        IMGEXPORT_VALUE_NONE,    },
    { N_("_False color ruler"), IMGEXPORT_VALUE_FMSCALE, },
};
static const GwyEnum title_types[] = {
    { N_("title|None"),           IMGEXPORT_TITLE_NONE,    },
    { N_("At the top"),           IMGEXPORT_TITLE_TOP,     },
    { N_("Along the right edge"), IMGEXPORT_TITLE_FMSCALE, },
};

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Renders data into vector (SVG, PDF, EPS) and pixmap (PNG, JPEG, TIFF, WebP, PPM, BMP, TARGA) images. "
       "Export to some formats relies on GDK and other libraries thus may be installation-dependent."),
    "Yeti <yeti@gwyddion.net>",
    "3.1",
    "David Nečas (Yeti)",
    "2014",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
    GSList *l, *pixbuf_formats;
    guint i;

    if (!preset_resource_type) {
        preset_resource_type = gwy_param_def_make_resource_type(define_preset_params(), "GwyImgExportPreset", NULL);
        update_presets();
        gwy_resource_class_load(g_type_class_peek(preset_resource_type));
    }

    /* Find out which image formats we can write using generic GdkPixbuf functions. */
    pixbuf_formats = gdk_pixbuf_get_formats();
    for (l = pixbuf_formats; l; l = g_slist_next(l)) {
        GdkPixbufFormat *pixbuf_format = (GdkPixbufFormat*)l->data;
        ImgExportFormat *format;
        gchar *name;

        name = gdk_pixbuf_format_get_name(pixbuf_format);
        if (!gdk_pixbuf_format_is_writable(pixbuf_format)) {
            gwy_debug("Ignoring pixbuf format %s, not writable", name);
            g_free(name);
            continue;
        }

        if (!(format = find_format(name, FALSE))) {
            gwy_debug("Skipping writable pixbuf format %s because we don't know it.", name);
            g_free(name);
            continue;
        }

        if (format->write_pixbuf) {
            gwy_debug("Skipping pixbuf format %s, we have our own writer.", name);
            g_free(name);
            continue;
        }

        gwy_debug("Adding generic pixbuf writer for %s.", name);
        format->write_pixbuf = write_pixbuf_generic;
        g_free(name);
    }
    g_slist_free(pixbuf_formats);

    /* Register file functions for the formats.  We want separate functions so that users can see the formats listed
     * in the file dialog.  We must use names different from the pixmap module, so append "cairo". */
    for (i = 0; i < G_N_ELEMENTS(image_formats); i++) {
        ImgExportFormat *format = image_formats + i;
        gchar *caironame;

        if (!format->write_pixbuf && !format->write_grey16 && !format->write_vector)
            continue;

        caironame = g_strconcat(format->name, "cairo", NULL);
        gwy_file_func_register(caironame, format->description, &img_export_detect, NULL, NULL, &img_export_export);
    }

    return TRUE;
}

static GwyParamDef*
define_preset_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, "imgexport");
    add_preset_params(paramdef);

    return paramdef;
}

static GwyParamDef*
define_module_params(void)
{
    static GwyEnum vector_interps[] = {
        { N_("Round"),  GWY_INTERPOLATION_ROUND,  },
        { N_("Linear"), GWY_INTERPOLATION_LINEAR, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    /* Compatibility. Yes, it is different from the resource directory name. */
    gwy_param_def_set_function_name(paramdef, "pixmap");
    add_preset_params(paramdef);
    /* Derived/alternative parameters. */
    gwy_param_def_add_double(paramdef, PARAM_VECTOR_WIDTH, NULL, _("_Width"), 10.0, 1000.0, 100.0);
    gwy_param_def_add_double(paramdef, PARAM_VECTOR_HEIGHT, NULL, _("_Height"), 10.0, 1000.0, 100.0);
    gwy_param_def_add_double(paramdef, PARAM_PPI, NULL, _("Pixels per _inch"), 0.01, 2540.0, 1.0);
    gwy_param_def_add_int(paramdef, PARAM_BITMAP_WIDTH, NULL, _("_Width"), 2, 16384, 256);
    gwy_param_def_add_int(paramdef, PARAM_BITMAP_HEIGHT, NULL, _("_Height"), 2, 16384, 256);
    gwy_param_def_add_color(paramdef, PARAM_SEL_RGB, NULL, _("Colo_r"), FALSE, white);
    gwy_param_def_add_double(paramdef, PARAM_SEL_ALPHA, NULL, _("O_pacity"), 0.0, 1.0, 1.0);
    gwy_param_def_add_color(paramdef, PARAM_INSET_RGB, NULL, _("Colo_r"), FALSE, white);
    gwy_param_def_add_double(paramdef, PARAM_INSET_ALPHA, NULL, _("O_pacity"), 0.0, 1.0, 1.0);
    /* Restricted interpolation parameter we use for vector formats. */
    gwy_param_def_add_gwyenum(paramdef, PARAM_INTERPOLATION_VECTOR, NULL, _("_Interpolation type"),
                              vector_interps, G_N_ELEMENTS(vector_interps), GWY_INTERPOLATION_ROUND);
    /* Module-level weird shit. */
    gwy_param_def_add_resource(paramdef, PARAM_PRESET, "preset", NULL,
                               gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type)), "");
    gwy_param_def_add_active_page(paramdef, PARAM_ACTIVE_PAGE, "active_page", NULL);

    return paramdef;
}

static void
add_preset_params(GwyParamDef *paramdef)
{
    /* Do not mark them as translatable, they do not appear in the GUI. They are just for the ride. */
    static GwyEnum modes[] = {
        { "Presentation",     IMGEXPORT_MODE_PRESENTATION, },
        { "16 bit grayscale", IMGEXPORT_MODE_GREY16,       },
    };
    static GwyEnum inset_positions[] = {
        { "top left",      INSET_POS_TOP_LEFT,      },
        { "top center",    INSET_POS_TOP_CENTER,    },
        { "top right",     INSET_POS_TOP_RIGHT,     },
        { "bottom left",   INSET_POS_BOTTOM_LEFT,   },
        { "bottom center", INSET_POS_BOTTOM_CENTER, },
        { "bottom right",  INSET_POS_BOTTOM_RIGHT,  },
    };

    gwy_param_def_add_gwyenum(paramdef, PARAM_MODE, "mode", NULL,
                              modes, G_N_ELEMENTS(modes), IMGEXPORT_MODE_PRESENTATION);

    /* Basic. */
    gwy_param_def_add_double(paramdef, PARAM_ZOOM, "zoom", _("_Zoom"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_PXWIDTH, "pxwidth", _("Pi_xel size"), 0.01, 254.0, 0.1);
    gwy_param_def_add_string(paramdef, PARAM_FONT, "font", _("_Font"),
                             GWY_PARAM_STRING_NULL_IS_EMPTY, NULL, "Arial");
    gwy_param_def_add_double(paramdef, PARAM_FONT_SIZE, "font_size", _("_Font size"), 1.0, 1024.0, 12.0);
    gwy_param_def_add_double(paramdef, PARAM_LINE_WIDTH, "line_width", _("Line t_hickness"), 0.0, 16.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_BORDER_WIDTH, "border_width", _("_Border width"), 0.0, 1024.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_TICK_LENGTH, "tick_length", _("_Tick length"), 0.0, 1024.0, 10.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SCALE_FONT, "scale_font", _("Tie sizes to _data pixels"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DECOMMA, "decomma", _("_Decimal separator is comma"), FALSE);
    gwy_param_def_add_color(paramdef, PARAM_LINETEXT_COLOR, "linetext_color", _("_Line and text color"), FALSE, black);
    gwy_param_def_add_color(paramdef, PARAM_BG_COLOR, "bg_color", _("_Background color"), FALSE, white);
    gwy_param_def_add_boolean(paramdef, PARAM_TRANSPARENT_BG, "transparent_bg", _("_Transparent background"), TRUE);

    /* Lateral. */
    gwy_param_def_add_gwyenum(paramdef, PARAM_XYTYPE, "xytype", _("_Type"),
                              lateral_types, G_N_ELEMENTS(lateral_types), IMGEXPORT_LATERAL_RULERS);
    gwy_param_def_add_string(paramdef, PARAM_INSET_LENGTH, "inset_length", _("_Length"),
                             GWY_PARAM_STRING_NULL_IS_EMPTY, NULL, "");
    gwy_param_def_add_gwyenum(paramdef, PARAM_INSET_POS, "inset_pos", _("Placement"),
                              inset_positions, G_N_ELEMENTS(inset_positions), INSET_POS_BOTTOM_RIGHT);
    gwy_param_def_add_double(paramdef, PARAM_INSET_XGAP, "inset_xgap", _("Hori_zontal gap"), 0.0, 4.0, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_INSET_YGAP, "inset_ygap", _("_Vertical gap"), 0.0, 2.0, 1.0);
    gwy_param_def_add_color(paramdef, PARAM_INSET_COLOR, "inset_color", _("Colo_r"), TRUE, white);
    gwy_param_def_add_color(paramdef, PARAM_INSET_OUTLINE_COLOR, "inset_outline_color", _("Out_line color"),
                            FALSE, white);
    gwy_param_def_add_double(paramdef, PARAM_INSET_OUTLINE_WIDTH, "inset_outline_width", _("O_utline thickness"),
                             0.0, 16.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_INSET_DRAW_TICKS, "inset_draw_ticks", _("Draw _ticks"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_INSET_DRAW_LABEL, "inset_draw_label", _("Draw _label"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_INSET_DRAW_TEXT_ABOVE, "inset_draw_text_above",
                              _("Draw text _above scale bar"), FALSE);

    /* Values. */
    gwy_param_def_add_enum(paramdef, PARAM_INTERPOLATION, "interpolation", NULL, GWY_TYPE_INTERPOLATION_TYPE,
                           GWY_INTERPOLATION_ROUND);
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_FRAME, "draw_frame", _("Draw _frame"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_MASK, "draw_mask", _("Draw _mask"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_MASKKEY, "draw_maskkey", _("Draw mask _legend"), TRUE);
    gwy_param_def_add_string(paramdef, PARAM_MASK_KEY, "mask_key", _("_Label"),
                             GWY_PARAM_STRING_NULL_IS_EMPTY, NULL, _("Mask"));
    gwy_param_def_add_double(paramdef, PARAM_MASKKEY_GAP, "maskkey_gap", _("_Vertical gap"), 0.0, 2.0, 1.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_ZTYPE, "ztype", NULL,
                              value_types, G_N_ELEMENTS(value_types), IMGEXPORT_VALUE_FMSCALE);
    gwy_param_def_add_double(paramdef, PARAM_FMSCALE_GAP, "fmscale_gap", _("Hori_zontal gap"), 0.0, 2.0, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_FIX_FMSCALE_PRECISION, "fix_fmscale_precision", NULL, FALSE);
    gwy_param_def_add_int(paramdef, PARAM_FMSCALE_PRECISION, "fmscale_precision", _("Fi_xed precision"), 0, 16, 2);
    gwy_param_def_add_boolean(paramdef, PARAM_FIX_KILO_THRESHOLD, "fix_kilo_threshold", NULL, FALSE);
    gwy_param_def_add_double(paramdef, PARAM_KILO_THRESHOLD, "kilo_threshold", _("Fixed _kilo threshold"),
                             1.0, 100000.0, 1200.0);
    gwy_param_def_add_gwyenum(paramdef, PARAM_TITLE_TYPE, "title_type", _("Posi_tion"),
                              title_types, G_N_ELEMENTS(title_types), IMGEXPORT_TITLE_NONE);
    gwy_param_def_add_double(paramdef, PARAM_TITLE_GAP, "title_gap", _("_Gap"), -2.0, 1.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_UNITS_IN_TITLE, "units_in_title", _("Put _units to title"), FALSE);

    /* Selection. */
    gwy_param_def_add_boolean(paramdef, PARAM_DRAW_SELECTION, "draw_selection", _("Draw _selection"), FALSE);
    gwy_param_def_add_color(paramdef, PARAM_SEL_COLOR, "sel_color", _("Colo_r"), TRUE, white);
    gwy_param_def_add_color(paramdef, PARAM_SEL_OUTLINE_COLOR, "sel_outline_color", _("Out_line color"), FALSE, white);
    gwy_param_def_add_string(paramdef, PARAM_SELECTION, "selection", NULL, GWY_PARAM_STRING_NULL_IS_EMPTY, NULL, "");
    gwy_param_def_add_double(paramdef, PARAM_SEL_OUTLINE_WIDTH, "sel_outline_width", _("O_utline thickness"),
                             0.0, 16.0, 0.0);
    gwy_param_def_add_boolean(paramdef, PARAM_SEL_NUMBER_OBJECTS, "sel_number_objects", _("Draw _numbers"), TRUE);
    gwy_param_def_add_double(paramdef, PARAM_SEL_POINT_RADIUS, "sel_point_radius", _("Marker _radius"),
                             0.0, 1024.0, 0.0);
    gwy_param_def_add_double(paramdef, PARAM_SEL_LINE_THICKNESS, "sel_line_thickness", _("_End marker length"),
                             0.0, 1024.0, 0.0);
}

static gint
img_export_detect(const GwyFileDetectInfo *fileinfo,
                  G_GNUC_UNUSED gboolean only_name,
                  const gchar *name)
{
    ImgExportFormat *format;
    gint score;
    gchar **extensions;
    guint i;

    gwy_debug("Running detection for file type %s", name);

    format = find_format(name, TRUE);
    g_return_val_if_fail(format, 0);

    extensions = g_strsplit(format->extensions, ",", 0);
    g_assert(extensions);
    for (i = 0; extensions[i]; i++) {
        if (g_str_has_suffix(fileinfo->name_lowercase, extensions[i]))
            break;
    }
    score = extensions[i] ? 20 : 0;
    g_strfreev(extensions);

    return score;
}

static gboolean
img_export_export(GwyContainer *data,
                  const gchar *filename,
                  GwyRunType mode,
                  GError **error,
                  const gchar *name)
{
    const ImgExportFormat *format;
    ModuleArgs args;
    gboolean ok = FALSE;
    gint id;

    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD_ID, &id, 0);
    if (id < 0) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    format = find_format(name, TRUE);
    g_return_val_if_fail(format, FALSE);

    args.env = img_export_load_env(format, data, id);
    args.params = gwy_params_new_from_settings(define_module_params());
    sanitise_params(&args, TRUE, mode == GWY_RUN_INTERACTIVE);

    if (mode == GWY_RUN_INTERACTIVE) {
        if (run_gui(&args) == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto end;
        }
    }

    if (format->write_vector)
        ok = format->write_vector(&args, format->name, filename, error);
    else if (format->write_grey16 && gwy_params_get_enum(args.params, PARAM_MODE) == IMGEXPORT_MODE_GREY16)
        ok = format->write_grey16(&args, format->name, filename, error);
    else if (format->write_pixbuf) {
        GdkPixbuf *pixbuf = render_pixbuf(&args, format->name);
        ok = format->write_pixbuf(pixbuf, format->name, filename, error);
        g_object_unref(pixbuf);
    }
    else {
        g_assert_not_reached();
    }

end:
    gwy_params_save_to_settings(args.params);
    g_object_unref(args.params);
    img_export_free_env(args.env);

    return ok;
}

static ImgExportFormat*
find_format(const gchar *name, gboolean cairoext)
{
    guint i, len;

    for (i = 0; i < G_N_ELEMENTS(image_formats); i++) {
        ImgExportFormat *format = image_formats + i;

        if (cairoext) {
            len = strlen(format->name);
            if (strncmp(name, format->name, len) == 0 && strcmp(name + len, "cairo") == 0)
                return format;
        }
        else {
            if (gwy_strequal(name, format->name))
                return format;
        }
    }

    return NULL;
}

static gchar*
scalebar_auto_length(GwyDataField *dfield, gdouble *p)
{
    static const double sizes[] = {
        1.0, 2.0, 3.0, 4.0, 5.0, 10.0, 20.0, 30.0, 40.0, 50.0, 100.0, 200.0, 300.0, 400.0, 500.0,
    };
    GwySIValueFormat *format;
    GwySIUnit *siunit;
    gdouble base, x, vmax, real;
    gchar *s;
    gint power10;
    guint i;

    real = gwy_data_field_get_xreal(dfield);
    siunit = gwy_data_field_get_si_unit_xy(dfield);
    vmax = 0.42*real;
    power10 = 3*(gint)(floor(log10(vmax)/3.0));
    base = pow10(power10 + 1e-14);
    x = vmax/base;
    for (i = 1; i < G_N_ELEMENTS(sizes); i++) {
        if (x < sizes[i])
            break;
    }
    x = sizes[i-1] * base;

    format = gwy_si_unit_get_format_for_power10(siunit, GWY_SI_UNIT_FORMAT_VFMARKUP, power10, NULL);
    s = g_strdup_printf("%.*f %s", format->precision, x/format->magnitude, format->units);
    gwy_si_unit_value_format_free(format);

    if (p)
        *p = x/real;

    return s;
}

static gdouble
inset_length_ok(GwyDataField *dfield,
                const gchar *inset_length)
{
    gdouble xreal, length;
    gint power10;
    GwySIUnit *siunit, *siunitxy;
    gchar *end, *plain_text_length = NULL;
    gboolean ok;

    if (!inset_length || !*inset_length)
        return 0.0;

    gwy_debug("checking inset <%s>", inset_length);
    if (!pango_parse_markup(inset_length, -1, 0, NULL, &plain_text_length, NULL, NULL))
        return 0.0;

    gwy_debug("plain_text version <%s>", plain_text_length);
    length = g_strtod(plain_text_length, &end);
    gwy_debug("unit part <%s>", end);
    siunit = gwy_si_unit_new_parse(end, &power10);
    gwy_debug("power10 %d", power10);
    length *= pow10(power10);
    xreal = gwy_data_field_get_xreal(dfield);
    siunitxy = gwy_data_field_get_si_unit_xy(dfield);
    ok = (gwy_si_unit_equal(siunit, siunitxy) && length > 0.07*xreal && length < 0.85*xreal);
    g_free(plain_text_length);
    g_object_unref(siunit);
    gwy_debug("xreal %g, length %g, ok: %d", xreal, length, ok);

    return ok ? length : 0.0;
}

static PangoLayout*
create_layout(const gchar *fontname, gdouble fontsize, cairo_t *cr)
{
    PangoContext *context;
    PangoFontDescription *fontdesc;
    PangoLayout *layout;

    /* This creates a layout with private context so we can modify the context at will. */
    layout = pango_cairo_create_layout(cr);

    fontdesc = pango_font_description_from_string(fontname);
    pango_font_description_set_size(fontdesc, PANGO_SCALE*fontsize);
    context = pango_layout_get_context(layout);
    pango_context_set_font_description(context, fontdesc);
    pango_font_description_free(fontdesc);
    pango_layout_context_changed(layout);
    /* XXX: Must call pango_cairo_update_layout() if we change the transformation afterwards. */

    return layout;
}

static void
format_layout(PangoLayout *layout,
              PangoRectangle *logical,
              GString *string,
              const gchar *format,
              ...)
{
    gchar *buffer;
    gint length;
    va_list ap;

    g_string_truncate(string, 0);
    va_start(ap, format);
    length = g_vasprintf(&buffer, format, ap);
    va_end(ap);
    g_string_append_len(string, buffer, length);
    g_free(buffer);

    pango_layout_set_markup(layout, string->str, string->len);
    pango_layout_get_extents(layout, NULL, logical);
}

static void
format_layout_numeric(ModuleArgs *args,
                      PangoLayout *layout,
                      PangoRectangle *logical,
                      GString *string,
                      const gchar *format,
                      ...)
{
    const gchar *decimal_symbol = args->env->decimal_symbol;
    gboolean decomma = gwy_params_get_boolean(args->params, PARAM_DECOMMA);
    gchar *buffer, *s;
    gint length;
    va_list ap;

    g_string_truncate(string, 0);
    va_start(ap, format);
    length = g_vasprintf(&buffer, format, ap);
    va_end(ap);
    g_string_append_len(string, buffer, length);
    g_free(buffer);

    /* Avoid negative zero, i.e. strings that start like negative zero-something but parse back as zero. */
    if (string->str[0] == '-' && string->str[1] == '0' && strtod(string->str, NULL) == 0.0)
        g_string_erase(string, 0, 1);

    /* Replace ASCII with proper minus */
    if (string->str[0] == '-') {
        g_string_erase(string, 0, 1);
        g_string_prepend_unichar(string, 0x2212);
    }

    if (decomma) {
        if (gwy_strequal(decimal_symbol, ".")) {
            if ((s = strchr(string->str, '.')))
                *s = ',';
        }
        /* Otherwise keep the locale symbol. Most likely it's a comma. If it isn't just close eyes and pretend it is. */
    }
    else {
        if (!gwy_strequal(decimal_symbol, ".")) {
            length = strlen(decimal_symbol);
            if (length == 1 && (s = strchr(string->str, decimal_symbol[0])))
                *s = '.';
            else if ((s = strstr(string->str, decimal_symbol))) {
                *s = '.';
                g_string_erase(string, s+1 - string->str, length-1);
            }
        }
        else {
            /* Keep the decimal dot. */
        }
    }

    pango_layout_set_markup(layout, string->str, string->len);
    pango_layout_get_extents(layout, NULL, logical);
}

static cairo_surface_t*
create_surface(const gchar *name,
               const gchar *filename,
               gdouble width, gdouble height, gboolean transparent_bg)
{
    cairo_surface_t *surface = NULL;
    guint i;

    if (width <= 0.0)
        width = 100.0;
    if (height <= 0.0)
        height = 100.0;

    if (!name) {
        /* This is here mainly to allow else-ifs in the #ifdefs. */
        g_assert_not_reached();
    }
#ifdef CAIRO_HAS_PDF_SURFACE
    else if (gwy_strequal(name, "pdf"))
        surface = cairo_pdf_surface_create(filename, width, height);
#endif
#ifdef CAIRO_HAS_PS_SURFACE
    else if (gwy_strequal(name, "eps")) {
        surface = cairo_ps_surface_create(filename, width, height);
        /* Requires cairo 1.6. */
        cairo_ps_surface_set_eps(surface, TRUE);
    }
#endif
#ifdef CAIRO_HAS_SVG_SURFACE
    else if (gwy_strequal(name, "svg")) {
        surface = cairo_svg_surface_create(filename, width, height);
    }
#endif
    else {
        for (i = 0; i < G_N_ELEMENTS(image_formats); i++) {
            if (gwy_strequal(name, image_formats[i].name)) {
                cairo_format_t imageformat = (transparent_bg ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24);
                gint iwidth = (gint)ceil(width);
                gint iheight = (gint)ceil(height);

                gwy_debug("%u %u %u", imageformat, iwidth, iheight);
                g_assert(!image_formats[i].write_vector);
                surface = cairo_image_surface_create(imageformat, iwidth, iheight);
                break;
            }
        }
        g_assert(surface);
    }

    return surface;
}

static gboolean
should_draw_frame(ModuleArgs *args)
{
    if (gwy_params_get_boolean(args->params, PARAM_DRAW_FRAME))
        return TRUE;
    if (gwy_params_get_enum(args->params, PARAM_XYTYPE) == IMGEXPORT_LATERAL_RULERS)
        return TRUE;
    if (gwy_params_get_enum(args->params, PARAM_ZTYPE) == IMGEXPORT_VALUE_FMSCALE)
        return TRUE;
    return FALSE;
}

static gboolean
precision_is_sufficient(gdouble bs, guint precision)
{
    gchar *s0 = g_strdup_printf("%.*f", precision, 0.0);
    gchar *s1 = g_strdup_printf("%.*f", precision, bs);
    gchar *s2 = g_strdup_printf("%.*f", precision, 2.0*bs);
    gchar *s3 = g_strdup_printf("%.*f", precision, 3.0*bs);
    gboolean ok = (!gwy_strequal(s0, s1) && !gwy_strequal(s1, s2) && !gwy_strequal(s2, s3));

    gwy_debug("<%s> vs <%s> vs <%s> vs <%s>: %s", s0, s1, s2, s3, ok ? "OK" : "NOT OK");
    g_free(s0);
    g_free(s1);
    g_free(s2);
    g_free(s3);
    return ok;
}

static void
find_hruler_ticks(ModuleArgs *args, ImgExportSizes *sizes,
                  PangoLayout *layout, GString *s)
{
    GwyDataField *dfield = args->env->dfield;
    GwySIUnit *xyunit = gwy_data_field_get_si_unit_xy(dfield);
    gdouble size = sizes->image.w;
    gdouble real = gwy_data_field_get_xreal(dfield);
    gdouble offset = gwy_data_field_get_xoffset(dfield);
    RulerTicks *ticks = &sizes->hruler_ticks;
    PangoRectangle logical1, logical2;
    GwySIValueFormat *vf;
    gdouble len, bs, height;
    guint n;

    vf = gwy_si_unit_get_format_with_resolution(xyunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, real/12, NULL);
    sizes->vf_hruler = vf;
    gwy_debug("unit '%s'", vf->units);
    offset /= vf->magnitude;
    real /= vf->magnitude;
    format_layout_numeric(args, layout, &logical2, s, "%.*f %s", vf->precision, offset, vf->units);
    gwy_debug("first '%s'", s->str);
    format_layout_numeric(args, layout, &logical1, s, "%.*f", vf->precision, real + offset);
    gwy_debug("right '%s'", s->str);

    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    sizes->hruler_label_height = height;
    len = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    gwy_debug("label len %g, height %g", len, height);
    n = CLAMP(GWY_ROUND(size/len), 1, 10);
    gwy_debug("nticks %u", n);
    ticks->step = real/n;
    ticks->base = pow10(floor(log10(ticks->step)));
    ticks->step /= ticks->base;
    if (ticks->step <= 2.0)
        ticks->step = 2.0;
    else if (ticks->step <= 5.0)
        ticks->step = 5.0;
    else {
        ticks->base *= 10.0;
        ticks->step = 1.0;
        if (vf->precision)
            vf->precision--;
    }

    bs = ticks->base * ticks->step;
    if (!precision_is_sufficient(bs, vf->precision)) {
        gwy_debug("precision %u insufficient, increasing by 1", vf->precision);
        vf->precision++;
    }
    else if (vf->precision && precision_is_sufficient(bs, vf->precision-1)) {
        gwy_debug("precision %u excessive, decreasing by 1", vf->precision);
        vf->precision--;
    }

    gwy_debug("base %g, step %g", ticks->base, ticks->step);
    ticks->from = ceil(offset/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor((real + offset)/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);
}

/* This must be called after find_hruler_ticks().  For unit consistency, we
 * choose the units in the horizontal ruler and force the same here. */
static void
find_vruler_ticks(ModuleArgs *args, ImgExportSizes *sizes,
                  PangoLayout *layout, GString *s)
{
    GwyDataField *dfield = args->env->dfield;
    gdouble size = sizes->image.h;
    gdouble real = gwy_data_field_get_yreal(dfield);
    gdouble offset = gwy_data_field_get_yoffset(dfield);
    RulerTicks *ticks = &sizes->vruler_ticks;
    PangoRectangle logical1, logical2;
    GwySIValueFormat *vf;
    gdouble height, bs, width;

    *ticks = sizes->hruler_ticks;
    vf = sizes->vf_vruler = gwy_si_unit_value_format_copy(sizes->vf_hruler);
    offset /= vf->magnitude;
    real /= vf->magnitude;
    format_layout_numeric(args, layout, &logical1, s, "%.*f", vf->precision, offset);
    gwy_debug("top '%s'", s->str);
    format_layout_numeric(args, layout, &logical2, s, "%.*f", vf->precision, offset + real);
    gwy_debug("last '%s'", s->str);

    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    gwy_debug("label height %g", height);

    /* Fix too dense ticks */
    while (ticks->base*ticks->step/real*size < 1.1*height) {
        if (ticks->step == 1.0)
            ticks->step = 2.0;
        else if (ticks->step == 2.0)
            ticks->step = 5.0;
        else {
            ticks->step = 1.0;
            ticks->base *= 10.0;
            if (vf->precision)
                vf->precision--;
        }
    }
    /* XXX: We also want to fix too sparse ticks but we do not want to make the verical ruler different from the
     * horizontal unless it really looks bad.  So some ‘looks really bad’ criterion is necessary. */
    gwy_debug("base %g, step %g", ticks->base, ticks->step);

    bs = ticks->base * ticks->step;
    ticks->from = ceil(offset/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor((real + offset)/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);

    /* Update widths for the new ticks. */
    format_layout_numeric(args, layout, &logical1, s, "%.*f", vf->precision, ticks->from);
    gwy_debug("top2 '%s'", s->str);
    format_layout_numeric(args, layout, &logical2, s, "%.*f", vf->precision, ticks->to);
    gwy_debug("last2 '%s'", s->str);

    width = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    sizes->vruler_label_width = width;
}

static void
measure_fmscale_label(GwySIValueFormat *vf,
                      ModuleArgs *args, ImgExportSizes *sizes,
                      PangoLayout *layout, GString *s)
{
    gboolean units_in_title = gwy_params_get_boolean(args->params, PARAM_UNITS_IN_TITLE);
    const ImgExportEnv *env = args->env;
    PangoRectangle logical1, logical2;
    gdouble min = env->fm_min/vf->magnitude, max = env->fm_max/vf->magnitude;
    gdouble width, height;

    sizes->fmruler_units_width = 0.0;

    /* Maximum, where we attach the units. */
    format_layout_numeric(args, layout, &logical1, s, "%.*f", vf->precision, max);
    if (!units_in_title) {
        sizes->fmruler_units_width -= logical1.width/pangoscale;
        format_layout_numeric(args, layout, &logical1, s, "%.*f %s", vf->precision, max, vf->units);
        sizes->fmruler_units_width += logical1.width/pangoscale;
    }
    gwy_debug("max '%s' (%g x %g)", s->str, logical1.width/pangoscale, logical1.height/pangoscale);

    /* Minimum, where we do not attach the units but must include them in size calculation due to alignment of the
     * numbers. */
    format_layout_numeric(args, layout, &logical2, s, "%.*f", vf->precision, min);
    if (!units_in_title) {
        sizes->fmruler_units_width -= logical2.width/pangoscale;
        format_layout_numeric(args, layout, &logical2, s, "%.*f %s", vf->precision, min, vf->units);
        sizes->fmruler_units_width += logical2.width/pangoscale;
    }
    gwy_debug("min '%s' (%g x %g)", s->str, logical2.width/pangoscale, logical2.height/pangoscale);

    width = MAX(logical1.width/pangoscale, logical2.width/pangoscale);
    sizes->fmruler_label_width = (width + sizes->sizes.tick_length + sizes->sizes.line_width);
    height = MAX(logical1.height/pangoscale, logical2.height/pangoscale);
    sizes->fmruler_label_height = height;
    gwy_debug("label width %g, height %g", width, height);
    sizes->fmruler_units_width *= 0.5;
    gwy_debug("units width %g", sizes->fmruler_units_width);
}

static GwySIValueFormat*
get_value_format_with_kilo_threshold(GwySIUnit *unit,
                                     GwySIUnitFormatStyle style,
                                     gdouble max, gdouble kilo_threshold,
                                     GwySIValueFormat *vf)
{
    gint p = 3*(gint)floor(log(max)/G_LN10/3.0 + 1e-14);
    gdouble b = pow10(p);

    while (max/b < 1e-3*kilo_threshold) {
        p -= 3;
        b /= 1000.0;
    }
    while (max/b >= kilo_threshold) {
        p += 3;
        b *= 1000.0;
    }

    /* NB: This does not touch the number of decimal places.  We must decide ourselves how many digits we want. */
    vf = gwy_si_unit_get_format_for_power10(unit, style, p, vf);
    vf->precision = 0;

    while (max/b < 120.0) {
        vf->precision++;
        b /= 10.0;
    }

    return vf;
}

static void
find_fmscale_ticks(ModuleArgs *args, ImgExportSizes *sizes,
                   PangoLayout *layout, GString *s)
{
    gboolean fix_kilo_threshold = gwy_params_get_boolean(args->params, PARAM_FIX_KILO_THRESHOLD);
    gdouble kilo_threshold = gwy_params_get_double(args->params, PARAM_KILO_THRESHOLD);
    gboolean fix_fmscale_precision = gwy_params_get_boolean(args->params, PARAM_FIX_FMSCALE_PRECISION);
    gint fmscale_precision = gwy_params_get_int(args->params, PARAM_FMSCALE_PRECISION);
    const ImgExportEnv *env = args->env;
    GwyDataField *dfield = env->dfield;
    GwySIUnit *zunit = gwy_data_field_get_si_unit_z(dfield);
    gdouble size = sizes->image.h;
    gdouble min, max, m, real;
    RulerTicks *ticks = &sizes->fmruler_ticks;
    GwySIValueFormat *vf;
    gdouble bs, height;
    guint n;

    min = env->fm_min;
    max = env->fm_max;
    real = max - min;
    m = MAX(fabs(min), fabs(max));

    /* This format is reused for the title. */
    if (fix_kilo_threshold && m)
        vf = get_value_format_with_kilo_threshold(zunit, GWY_SI_UNIT_FORMAT_VFMARKUP, m, kilo_threshold, NULL);
    else
        vf = gwy_si_unit_get_format_with_resolution(zunit, GWY_SI_UNIT_FORMAT_VFMARKUP, real, real/96.0, NULL);

    min /= vf->magnitude;
    max /= vf->magnitude;
    real /= vf->magnitude;

    sizes->vf_fmruler = vf;
    sizes->zunits_nonempty = !!strlen(vf->units);
    gwy_debug("unit '%s'", vf->units);

    /* Return after creating vf, we are supposed to do that. */
    if (env->has_presentation) {
        sizes->fmruler_label_width = (sizes->sizes.tick_length + sizes->sizes.line_width);
        sizes->fmruler_label_height = 0.0;
        sizes->fmruler_units_width = 0.0;
        return;
    }

    measure_fmscale_label(vf, args, sizes, layout, s);
    height = sizes->fmruler_label_height;

    /* We can afford somewhat denser ticks for adaptive mapping.  However, when there are labels we definitely want
     * the distance to the next tick to be much larger than to the correct tick. */
    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT)
        n = CLAMP(GWY_ROUND(1.2*size/height), 1, 40);
    else
        n = CLAMP(GWY_ROUND(0.7*size/height), 1, 15);

    gwy_debug("nticks %u", n);
    ticks->step = real/n;
    ticks->base = pow10(floor(log10(ticks->step)));
    ticks->step /= ticks->base;
    gwy_debug("estimated base %g, step %g", ticks->base, ticks->step);
    if (ticks->step <= 2.0)
        ticks->step = 2.0;
    else if (ticks->step <= 5.0)
        ticks->step = 5.0;
    else {
        ticks->base *= 10.0;
        ticks->step = 1.0;
        if (vf->precision) {
            vf->precision--;
            measure_fmscale_label(vf, args, sizes, layout, s);
        }
    }
    gwy_debug("base %g, step %g", ticks->base, ticks->step);
    gwy_debug("tick distance/label height ratio %g", size/n/height);

    if (fix_fmscale_precision) {
        /* Do everything as normal and override the precision at the end. */
        gwy_debug("overriding precision to %d", fmscale_precision);
        vf->precision = fmscale_precision;
        measure_fmscale_label(vf, args, sizes, layout, s);
    }

    bs = ticks->base * ticks->step;
    ticks->from = ceil(min/bs - 1e-14)*bs;
    ticks->from = fixzero(ticks->from);
    ticks->to = floor(max/bs + 1e-14)*bs;
    ticks->to = fixzero(ticks->to);
    gwy_debug("from %g, to %g", ticks->from, ticks->to);
}

static void
measure_inset(ModuleArgs *args, ImgExportSizes *sizes,
              PangoLayout *layout, GString *s)
{
    InsetPosType inset_pos = gwy_params_get_enum(args->params, PARAM_INSET_POS);
    const gchar *inset_length = gwy_params_get_string(args->params, PARAM_INSET_LENGTH);
    gboolean inset_draw_ticks = gwy_params_get_boolean(args->params, PARAM_INSET_DRAW_TICKS);
    gboolean inset_draw_label = gwy_params_get_boolean(args->params, PARAM_INSET_DRAW_LABEL);
    gboolean inset_draw_text_above = gwy_params_get_boolean(args->params, PARAM_INSET_DRAW_TEXT_ABOVE);
    gdouble inset_xgap = gwy_params_get_double(args->params, PARAM_INSET_XGAP);
    gdouble inset_ygap = gwy_params_get_double(args->params, PARAM_INSET_YGAP);
    GwyDataField *dfield = args->env->dfield;
    ImgExportRect *rect = &sizes->inset;
    gdouble hsize = sizes->image.w, vsize = sizes->image.h;
    gdouble real = gwy_data_field_get_xreal(dfield);
    PangoRectangle logical, ink;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble fs = sizes->sizes.font_size;

    sizes->inset_length = inset_length_ok(dfield, inset_length);
    if (!(sizes->inset_length > 0.0))
        return;

    rect->w = sizes->inset_length/real*(hsize - 2.0*lw);
    rect->h = lw;
    if (inset_draw_ticks)
        rect->h += tl + lw;

    if (inset_draw_label) {
        format_layout(layout, &logical, s, "%s", inset_length);
        rect->w = MAX(rect->w, logical.width/pangoscale);
        /* We need ink rectangle to position labels with no ink below baseline, such as 100 nm, as expected. */
        pango_layout_get_extents(layout, &ink, NULL);
        rect->h += (ink.height + ink.y)/pangoscale + lw;
    }

    if (inset_pos == INSET_POS_TOP_LEFT || inset_pos == INSET_POS_TOP_CENTER || inset_pos == INSET_POS_TOP_RIGHT)
        if (inset_draw_text_above)
            rect->y = lw + fs*inset_ygap + rect->h;
        else
            rect->y = lw + fs*inset_ygap;
    else
        if (inset_draw_text_above)
            if (inset_draw_ticks)
                rect->y = vsize - lw - tl - fs*inset_ygap;
            else
                rect->y = vsize - lw - fs*inset_ygap;
        else
            rect->y = vsize - lw - rect->h - fs*inset_ygap;

    if (inset_pos == INSET_POS_TOP_LEFT || inset_pos == INSET_POS_BOTTOM_LEFT)
        rect->x = 2.0 * lw + fs*inset_xgap;
    else if (inset_pos == INSET_POS_TOP_RIGHT || inset_pos == INSET_POS_BOTTOM_RIGHT)
        rect->x = hsize - 2.0*lw - rect->w - fs*inset_xgap;
    else
        rect->x = hsize/2 - 0.5 * rect->w;
}

static void
measure_title(ModuleArgs *args, ImgExportSizes *sizes,
              PangoLayout *layout, GString *s)
{
    ImgExportTitleType title_type = gwy_params_get_enum(args->params, PARAM_TITLE_TYPE);
    gboolean units_in_title = gwy_params_get_boolean(args->params, PARAM_UNITS_IN_TITLE);
    gdouble title_gap = gwy_params_get_double(args->params, PARAM_TITLE_GAP);
    const ImgExportEnv *env = args->env;
    ImgExportRect *rect = &sizes->title;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble gap;

    g_string_truncate(s, 0);
    if (units_in_title) {
        GwySIValueFormat *vf = sizes->vf_fmruler;
        if (*(vf->units))
            format_layout(layout, &logical, s, "%s [%s]", env->title, vf->units);
    }
    if (!s->len)
        format_layout(layout, &logical, s, "%s", env->title);

    /* Straight.  This is rotated according to the type when drawing. NB: rect->h can be negative; the measurement
     * must deal with it. */
    gap = fs*title_gap;
    if (title_type != IMGEXPORT_TITLE_FMSCALE)
        gap = MAX(gap, 0.0);
    rect->w = logical.width/pangoscale;
    rect->h = logical.height/pangoscale + gap;
}

static void
measure_mask_legend(ModuleArgs *args, ImgExportSizes *sizes,
                    PangoLayout *layout, GString *s)
{
    const gchar *mask_key = gwy_params_get_string(args->params, PARAM_MASK_KEY);
    gdouble maskkey_gap = gwy_params_get_double(args->params, PARAM_MASKKEY_GAP);
    ImgExportRect *rect = &sizes->maskkey;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble lw = sizes->sizes.line_width;
    gdouble h, hgap, vgap;

    g_string_truncate(s, 0);
    format_layout(layout, &logical, s, "%s", mask_key);

    h = 1.5*fs + 2.0*lw;    /* Match fmscale width */
    vgap = fs*maskkey_gap;
    hgap = 0.4*h;
    rect->h = h + vgap;
    rect->w = 1.4*h + hgap + logical.width/pangoscale;
}

static void
rect_move(ImgExportRect *rect, gdouble x, gdouble y)
{
    rect->x += x;
    rect->y += y;
}

static void
fill_sizes_from_params(SizeSettings *sizes, GwyParams *params)
{
    sizes->font_size = gwy_params_get_double(params, PARAM_FONT_SIZE);
    sizes->inset_outline_width = gwy_params_get_double(params, PARAM_INSET_OUTLINE_WIDTH);
    sizes->sel_outline_width = gwy_params_get_double(params, PARAM_SEL_OUTLINE_WIDTH);
    sizes->border_width = gwy_params_get_double(params, PARAM_BORDER_WIDTH);
    sizes->line_width = gwy_params_get_double(params, PARAM_LINE_WIDTH);
    sizes->tick_length = gwy_params_get_double(params, PARAM_TICK_LENGTH);
}

static void
scale_sizes_in_params(GwyParams *params, gdouble factor)
{
    static const int size_ids[] = {
        PARAM_FONT_SIZE, PARAM_INSET_OUTLINE_WIDTH, PARAM_SEL_OUTLINE_WIDTH, PARAM_BORDER_WIDTH,
        PARAM_LINE_WIDTH, PARAM_TICK_LENGTH,
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(size_ids); i++)
        gwy_params_set_double(params, size_ids[i], factor*gwy_params_get_double(params, size_ids[i]));
}

static void
scale_sizes(SizeSettings *sizes, gdouble factor)
{
    sizes->font_size *= factor;
    sizes->line_width *= factor;
    sizes->inset_outline_width *= factor;
    sizes->sel_outline_width *= factor;
    sizes->border_width *= factor;
    sizes->tick_length *= factor;
}

static ImgExportSizes*
calculate_sizes(ModuleArgs *args, const gchar *name)
{
    GwyParams *params = args->params;
    ImgExportTitleType title_type = gwy_params_get_enum(params, PARAM_TITLE_TYPE);
    ImgExportLateralType xytype = gwy_params_get_enum(params, PARAM_XYTYPE);
    ImgExportValueType ztype = gwy_params_get_enum(params, PARAM_ZTYPE);
    gdouble zoom = gwy_params_get_double(params, PARAM_ZOOM);
    gboolean scale_font = gwy_params_get_boolean(params, PARAM_SCALE_FONT);
    gboolean units_in_title = gwy_params_get_boolean(params, PARAM_UNITS_IN_TITLE);
    gboolean draw_mask = gwy_params_get_boolean(params, PARAM_DRAW_MASK);
    gboolean draw_maskkey = gwy_params_get_boolean(params, PARAM_DRAW_MASKKEY);
    gdouble fmscale_gap = gwy_params_get_double(params, PARAM_FMSCALE_GAP);
    const gchar *font = gwy_params_get_string(params, PARAM_FONT);
    ImgExportSizes *sizes = g_new0(ImgExportSizes, 1);
    const ImgExportEnv *env = args->env;
    GString *s = g_string_new(NULL);
    gdouble fw, lw, fs, borderw, tl;
    PangoLayout *layout;
    cairo_surface_t *surface;
    cairo_t *cr;

    gwy_debug("zoom %g", zoom);
    surface = create_surface(name, NULL, 0.0, 0.0, FALSE);
    g_return_val_if_fail(surface, NULL);
    cr = cairo_create(surface);
    fill_sizes_from_params(&sizes->sizes, params);
    if (scale_font)
        scale_sizes(&sizes->sizes, zoom);
    lw = sizes->sizes.line_width;
    fw = should_draw_frame(args) ? lw : 0.0;
    borderw = sizes->sizes.border_width;
    tl = sizes->sizes.tick_length;
    fs = sizes->sizes.font_size;
    layout = create_layout(font, fs, cr);

    gwy_debug("lw = %g, fw = %g, borderw = %g", lw, fw, borderw);
    gwy_debug("tl = %g, fs = %g", tl, fs);

    /* Data */
    sizes->image.w = zoom*env->xres + 2.0*fw;
    sizes->image.h = zoom*env->yres + 2.0*fw;

    /* Horizontal ruler */
    if (xytype == IMGEXPORT_LATERAL_RULERS) {
        find_hruler_ticks(args, sizes, layout, s);
        sizes->hruler.w = sizes->image.w;
        sizes->hruler.h = sizes->hruler_label_height + tl + fw;
    }

    /* Vertical ruler */
    if (xytype == IMGEXPORT_LATERAL_RULERS) {
        find_vruler_ticks(args, sizes, layout, s);
        sizes->vruler.w = sizes->vruler_label_width + tl + fw;
        sizes->vruler.h = sizes->image.h;
        rect_move(&sizes->hruler, sizes->vruler.w, 0.0);
        rect_move(&sizes->vruler, 0.0, sizes->hruler.h);
        rect_move(&sizes->image, sizes->vruler.w, sizes->hruler.h);
    }

    /* Inset scale bar */
    if (xytype == IMGEXPORT_LATERAL_INSET) {
        measure_inset(args, sizes, layout, s);
        rect_move(&sizes->inset, sizes->image.x, sizes->image.y);
    }

    /* False colour gradient. Always measure the false colour axis.  We may not draw it but we may want to know how it
     * would be drawn (e.g. units). */
    sizes->fmgrad = sizes->image;
    rect_move(&sizes->fmgrad, sizes->image.w + fs*fmscale_gap - fw, 0.0);
    find_fmscale_ticks(args, sizes, layout, s);
    if (ztype == IMGEXPORT_VALUE_FMSCALE) {
        /* Subtract fw here to make the fmscale visually just touch the image in the case of zero gap. */
        sizes->fmgrad.w = 1.5*fs + 2.0*fw;
    }
    else {
        sizes->fmgrad.x = sizes->image.x + sizes->image.w;
        sizes->fmgrad.w = 0;
        sizes->fmruler_label_width = sizes->fmruler_units_width = 0;
    }
    sizes->fmruler.x = sizes->fmgrad.x + sizes->fmgrad.w;
    sizes->fmruler.y = sizes->fmgrad.y;
    sizes->fmruler.w = sizes->fmruler_label_width;
    sizes->fmruler.h = sizes->fmgrad.h;

    /* Title, possibly with units */
    if (title_type != IMGEXPORT_TITLE_NONE) {
        measure_title(args, sizes, layout, s);
        if (title_type == IMGEXPORT_TITLE_FMSCALE) {
            gdouble ymove = sizes->image.y + sizes->image.h;

            ymove -= 0.5*(sizes->image.h - sizes->title.w);
            /* Center the title visually, not physically. */
            if (sizes->zunits_nonempty && !units_in_title)
                ymove += 0.5*sizes->fmruler_label_height;

            rect_move(&sizes->title, sizes->fmruler.x + sizes->fmruler.w, ymove);
        }
        else if (title_type == IMGEXPORT_TITLE_TOP) {
            gdouble xcentre = sizes->image.x + 0.5*sizes->image.w;

            rect_move(&sizes->title, xcentre - 0.5*sizes->title.w, 0.0);
            rect_move(&sizes->image, 0.0, sizes->title.h);
            rect_move(&sizes->vruler, 0.0, sizes->title.h);
            rect_move(&sizes->hruler, 0.0, sizes->title.h);
            rect_move(&sizes->inset, 0.0, sizes->title.h);
            rect_move(&sizes->fmgrad, 0.0, sizes->title.h);
            rect_move(&sizes->fmruler, 0.0, sizes->title.h);
        }
    }

    /* Mask key */
    if (env->mask && draw_mask && draw_maskkey) {
        measure_mask_legend(args, sizes, layout, s);
        rect_move(&sizes->maskkey, sizes->image.x, sizes->image.y + sizes->image.h);
    }

    /* Border */
    rect_move(&sizes->image, borderw, borderw);
    rect_move(&sizes->hruler, borderw, borderw);
    rect_move(&sizes->vruler, borderw, borderw);
    rect_move(&sizes->inset, borderw, borderw);
    rect_move(&sizes->fmgrad, borderw, borderw);
    rect_move(&sizes->fmruler, borderw, borderw);
    rect_move(&sizes->title, borderw, borderw);
    rect_move(&sizes->maskkey, borderw, borderw);

    /* Ensure the image starts at integer coordinates in pixmas */
    if (cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE) {
        gdouble xmove = ceil(sizes->image.x + fw) - (sizes->image.x + fw);
        gdouble ymove = ceil(sizes->image.y + fw) - (sizes->image.y + fw);

        if (xmove < 0.98 && ymove < 0.98) {
            gwy_debug("moving image by (%g,%g) to integer coordinates", xmove, ymove);
            rect_move(&sizes->image, xmove, ymove);
            rect_move(&sizes->hruler, xmove, ymove);
            rect_move(&sizes->vruler, xmove, ymove);
            rect_move(&sizes->inset, xmove, ymove);
            rect_move(&sizes->fmgrad, xmove, ymove);
            rect_move(&sizes->fmruler, xmove, ymove);
            rect_move(&sizes->title, xmove, ymove);
            rect_move(&sizes->maskkey, xmove, ymove);
        }
    }

    /* Canvas */
    sizes->canvas.w = sizes->fmruler.x + sizes->fmruler.w + borderw;
    if (title_type == IMGEXPORT_TITLE_FMSCALE)
        sizes->canvas.w += MAX(sizes->title.h, 0.0);

    sizes->canvas.h = (sizes->image.y + sizes->image.h + sizes->maskkey.h + borderw);

    gwy_debug("canvas %g x %g at (%g, %g)", sizes->canvas.w, sizes->canvas.h, sizes->canvas.x, sizes->canvas.y);
    gwy_debug("hruler %g x %g at (%g, %g)", sizes->hruler.w, sizes->hruler.h, sizes->hruler.x, sizes->hruler.y);
    gwy_debug("vruler %g x %g at (%g, %g)", sizes->vruler.w, sizes->vruler.h, sizes->vruler.x, sizes->vruler.y);
    gwy_debug("image %g x %g at (%g, %g)", sizes->image.w, sizes->image.h, sizes->image.x, sizes->image.y);
    gwy_debug("inset %g x %g at (%g, %g)", sizes->inset.w, sizes->inset.h, sizes->inset.x, sizes->inset.y);
    gwy_debug("fmgrad %g x %g at (%g, %g)", sizes->fmgrad.w, sizes->fmgrad.h, sizes->fmgrad.x, sizes->fmgrad.y);
    gwy_debug("fmruler %g x %g at (%g, %g)", sizes->fmruler.w, sizes->fmruler.h, sizes->fmruler.x, sizes->fmruler.y);
    gwy_debug("title %g x %g at (%g, %g)", sizes->title.w, sizes->title.h, sizes->title.x, sizes->title.y);
    gwy_debug("maskkey %g x %g at (%g, %g)", sizes->maskkey.w, sizes->maskkey.h, sizes->maskkey.x, sizes->maskkey.y);

    g_string_free(s, TRUE);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return sizes;
}

static void
destroy_sizes(ImgExportSizes *sizes)
{
    if (sizes->vf_hruler)
        gwy_si_unit_value_format_free(sizes->vf_hruler);
    if (sizes->vf_vruler)
        gwy_si_unit_value_format_free(sizes->vf_vruler);
    if (sizes->vf_fmruler)
        gwy_si_unit_value_format_free(sizes->vf_fmruler);
    g_free(sizes);
}

static void
set_cairo_source_rgba(cairo_t *cr, const GwyRGBA *rgba)
{
    cairo_set_source_rgba(cr, rgba->r, rgba->g, rgba->b, rgba->a);
}

static void
set_cairo_source_rgb(cairo_t *cr, const GwyRGBA *rgba)
{
    cairo_set_source_rgb(cr, rgba->r, rgba->g, rgba->b);
}

static void
draw_text_outline(cairo_t *cr, PangoLayout *layout, const GwyRGBA *outcolour, gdouble olw)
{
    gdouble x, y;

    cairo_get_current_point(cr, &x, &y);
    pango_cairo_layout_path(cr, layout);
    set_cairo_source_rgb(cr, outcolour);
    cairo_set_line_width(cr, 2.0*olw);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_move_to(cr, x, y);
}

static void
draw_text(cairo_t *cr, PangoLayout *layout, const GwyRGBA *colour)
{
    set_cairo_source_rgb(cr, colour);
    cairo_set_line_width(cr, 0.0);
    pango_cairo_show_layout(cr, layout);
}

/* Can be only used with closed paths and with SQUARE and ROUND line ends -- which we don't use because then we can't
 * get invisible line end ticks. */
static void
stroke_path_outline(cairo_t *cr, const GwyRGBA *outcolour, gdouble lw, gdouble olw)
{
    set_cairo_source_rgb(cr, outcolour);
    cairo_set_line_width(cr, lw + 2.0*olw);
    cairo_stroke_preserve(cr);
}

static void
stroke_path(cairo_t *cr,
            const GwyRGBA *colour, gdouble lw)
{
    set_cairo_source_rgb(cr, colour);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
}

/* Draw outline (only) for a BUTT line, adding correct outline at the ends. */
static void
draw_line_outline(cairo_t *cr,
                  gdouble xf, gdouble yf, gdouble xt, gdouble yt,
                  const GwyRGBA *outcolour,
                  gdouble lw, gdouble olw)
{
    gdouble vx = xt - xf, vy = yt - yf;
    gdouble len = sqrt(vx*vx + vy*vy);

    if (len < 1e-9 || olw <= 0.0)
        return;

    vx *= olw/len;
    vy *= olw/len;
    cairo_save(cr);
    cairo_move_to(cr, xf - vx, yf - vy);
    cairo_line_to(cr, xt + vx, yt + vy);
    cairo_set_line_width(cr, lw + 2.0*olw);
    set_cairo_source_rgb(cr, outcolour);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_background(ModuleArgs *args, cairo_t *cr)
{
    gboolean want_transp = gwy_params_get_boolean(args->params, PARAM_TRANSPARENT_BG);
    GwyRGBA bg_color = gwy_params_get_color(args->params, PARAM_BG_COLOR);
    gboolean can_transp = args->env->format->supports_transparency;

    if (can_transp && want_transp)
        return;

    set_cairo_source_rgb(cr, &bg_color);
    cairo_paint(cr);
}

static GdkPixbuf*
draw_data_pixbuf_1_1(ModuleArgs *args)
{
    const ImgExportEnv *env = args->env;
    ImgExportMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyGradient *gradient = ((mode == IMGEXPORT_MODE_GREY16) ? env->grey : env->gradient);
    GwyLayerBasicRangeType range_type = env->fm_rangetype;
    GwyDataField *dfield = env->dfield;
    GdkPixbuf *pixbuf;
    guint xres, yres;

    xres = gwy_data_field_get_xres(dfield);
    yres = gwy_data_field_get_yres(dfield);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, xres, yres);

    if (range_type == GWY_LAYER_BASIC_RANGE_ADAPT)
        gwy_pixbuf_draw_data_field_adaptive(pixbuf, dfield, gradient);
    else {
        gdouble min = env->fm_inverted ? env->fm_max : env->fm_min;
        gdouble max = env->fm_inverted ? env->fm_min : env->fm_max;
        gwy_pixbuf_draw_data_field_with_range(pixbuf, dfield, gradient, min, max);
    }
    return pixbuf;
}

static GdkPixbuf*
draw_data_pixbuf_resampled(ModuleArgs *args,
                           const ImgExportSizes *sizes)
{
    GwyInterpolationType interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);
    const ImgExportEnv *env = args->env;
    GwyDataField *dfield = env->dfield, *resampled;
    GwyGradient *gradient = env->gradient;
    GwyLayerBasicRangeType range_type = env->fm_rangetype;
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    GdkPixbuf *pixbuf;
    gdouble w = sizes->image.w - 2.0*fw;
    gdouble h = sizes->image.h - 2.0*fw;
    guint width, height;

    width = GWY_ROUND(MAX(w, 2.0));
    height = GWY_ROUND(MAX(h, 2.0));
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, width, height);

    resampled = gwy_data_field_new_resampled(dfield, width, height, interpolation);

    /* XXX: Resampling can influence adaptive mapping, i.e. adaptive-mapping resampled image is not the same as
     * adaptive-mapping the original and then resampling. But it should not be noticeable in normal circumstances. */
    if (range_type == GWY_LAYER_BASIC_RANGE_ADAPT)
        gwy_pixbuf_draw_data_field_adaptive(pixbuf, resampled, gradient);
    else {
        gdouble min = env->fm_inverted ? env->fm_max : env->fm_min;
        gdouble max = env->fm_inverted ? env->fm_min : env->fm_max;
        gwy_pixbuf_draw_data_field_with_range(pixbuf, resampled, gradient, min, max);
    }

    g_object_unref(resampled);

    return pixbuf;
}

static GdkPixbuf*
draw_mask_pixbuf(ModuleArgs *args)
{
    const ImgExportEnv *env = args->env;
    GwyDataField *mask = env->mask;
    guint xres, yres;
    GdkPixbuf *pixbuf;

    g_return_val_if_fail(mask, NULL);
    xres = gwy_data_field_get_xres(mask);
    yres = gwy_data_field_get_yres(mask);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, xres, yres);
    gwy_pixbuf_draw_data_field_as_mask(pixbuf, mask, &env->mask_colour);

    return pixbuf;
}

static void
stretch_pixbuf_source(cairo_t *cr,
                      GdkPixbuf *pixbuf,
                      ModuleArgs *args,
                      const ImgExportSizes *sizes)
{
    gdouble mw = gdk_pixbuf_get_width(pixbuf);
    gdouble mh = gdk_pixbuf_get_height(pixbuf);
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    gdouble w = sizes->image.w - 2.0*fw;
    gdouble h = sizes->image.h - 2.0*fw;

    cairo_scale(cr, w/mw, h/mh);
    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0.0, 0.0);
}

static void
draw_data(ModuleArgs *args,
          const ImgExportSizes *sizes,
          cairo_t *cr)
{
    ImgExportMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyInterpolationType interpolation;
    gboolean draw_mask = gwy_params_get_boolean(args->params, PARAM_DRAW_MASK);
    const ImgExportRect *rect = &sizes->image;
    ImgExportEnv *env = args->env;
    gboolean drawing_as_vector;
    cairo_filter_t interp;
    GdkPixbuf *pixbuf;
    gint xres = gwy_data_field_get_xres(env->dfield);
    gint yres = gwy_data_field_get_yres(env->dfield);
    gdouble lw = sizes->sizes.line_width;
    gdouble fw = should_draw_frame(args) ? lw : 0.0;
    gdouble w = rect->w - 2.0*fw;
    gdouble h = rect->h - 2.0*fw;

    /* Never draw pixmap images with anything else than CAIRO_FILTER_BILINEAR because it causes bleeding and fading at
     * the image borders. */
    interp = CAIRO_FILTER_NEAREST;
    drawing_as_vector = (cairo_surface_get_type(cairo_get_target(cr)) != CAIRO_SURFACE_TYPE_IMAGE);
    if (drawing_as_vector) {
        interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION_VECTOR);
        if (interpolation != GWY_INTERPOLATION_ROUND)
            interp = CAIRO_FILTER_BILINEAR;
    }
    else
        interpolation = gwy_params_get_enum(args->params, PARAM_INTERPOLATION);

    if (drawing_as_vector
        || mode == IMGEXPORT_MODE_GREY16
        || interpolation == GWY_INTERPOLATION_ROUND
        || (fabs(xres - w) < 0.001 && fabs(yres - h) < 0.001))
        pixbuf = draw_data_pixbuf_1_1(args);
    else {
        pixbuf = draw_data_pixbuf_resampled(args, sizes);
        interp = CAIRO_FILTER_NEAREST;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x + fw, rect->y + fw);
    stretch_pixbuf_source(cr, pixbuf, args, sizes);
    cairo_pattern_set_filter(cairo_get_source(cr), interp);
    cairo_paint(cr);
    cairo_restore(cr);
    g_object_unref(pixbuf);

    /* Mask must be drawn pixelated. */
    if (env->mask && draw_mask) {
        cairo_save(cr);
        cairo_translate(cr, rect->x + fw, rect->y + fw);
        pixbuf = draw_mask_pixbuf(args);
        stretch_pixbuf_source(cr, pixbuf, args, sizes);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_paint(cr);
        cairo_restore(cr);
        g_object_unref(pixbuf);
    }
}

static void
draw_data_frame(ModuleArgs *args,
                const ImgExportSizes *sizes,
                cairo_t *cr)
{
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    const ImgExportRect *rect = &sizes->image;
    gdouble fw = sizes->sizes.line_width;
    gdouble w = rect->w - 2.0*fw;
    gdouble h = rect->h - 2.0*fw;

    if (!should_draw_frame(args))
        return;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, fw);
    cairo_rectangle(cr, 0.5*fw, 0.5*fw, w + fw, h + fw);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_hruler(ModuleArgs *args,
            const ImgExportSizes *sizes,
            PangoLayout *layout,
            GString *s,
            cairo_t *cr)
{
    ImgExportLateralType xytype = gwy_params_get_enum(args->params, PARAM_XYTYPE);
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    GwySIValueFormat *vf = sizes->vf_hruler;
    GwyDataField *dfield = args->env->dfield;
    const ImgExportRect *rect = &sizes->hruler;
    const RulerTicks *ticks = &sizes->hruler_ticks;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble x, bs, scale, ximg, xreal, xoffset;
    gboolean units_placed = FALSE;

    if (xytype != IMGEXPORT_LATERAL_RULERS)
        return;

    xreal = gwy_data_field_get_xreal(dfield)/vf->magnitude;
    xoffset = gwy_data_field_get_xoffset(dfield)/vf->magnitude;
    scale = (rect->w - lw)/xreal;
    bs = ticks->step*ticks->base;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    for (x = ticks->from; x <= ticks->to + 1e-14*bs; x += bs) {
        ximg = (x - xoffset)*scale + 0.5*lw;
        gwy_debug("x %g -> %g", x, ximg);
        cairo_move_to(cr, ximg, rect->h);
        cairo_line_to(cr, ximg, rect->h - tl);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    for (x = ticks->from; x <= ticks->to + 1e-14*bs; x += bs) {
        PangoRectangle logical;

        x = fixzero(x);
        ximg = (x - xoffset)*scale + 0.5*lw;
        if (!units_placed && (x >= 0.0 || ticks->to <= -1e-14)) {
            format_layout_numeric(args, layout, &logical, s, "%.*f %s", vf->precision, x, vf->units);
            units_placed = TRUE;
        }
        else
            format_layout_numeric(args, layout, &logical, s, "%.*f", vf->precision, x);

        if (ximg + logical.width/pangoscale <= rect->w) {
            cairo_move_to(cr, ximg, rect->h - tl - lw);
            cairo_rel_move_to(cr, 0.0, -logical.height/pangoscale);
            pango_cairo_show_layout(cr, layout);
        }
    };
    cairo_restore(cr);
}

static void
draw_vruler(ModuleArgs *args,
            const ImgExportSizes *sizes,
            PangoLayout *layout,
            GString *s,
            cairo_t *cr)
{
    ImgExportLateralType xytype = gwy_params_get_enum(args->params, PARAM_XYTYPE);
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    GwySIValueFormat *vf = sizes->vf_vruler;
    GwyDataField *dfield = args->env->dfield;
    const ImgExportRect *rect = &sizes->vruler;
    const RulerTicks *ticks = &sizes->vruler_ticks;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble y, bs, scale, yimg, yreal, yoffset;

    if (xytype != IMGEXPORT_LATERAL_RULERS)
        return;

    yreal = gwy_data_field_get_yreal(dfield)/vf->magnitude;
    yoffset = gwy_data_field_get_yoffset(dfield)/vf->magnitude;
    scale = (rect->h - lw)/yreal;
    bs = ticks->step*ticks->base;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    for (y = ticks->from; y <= ticks->to + 1e-14*bs; y += bs) {
        yimg = (y - yoffset)*scale + 0.5*lw;
        gwy_debug("y %g -> %g", y, yimg);
        cairo_move_to(cr, rect->w, yimg);
        cairo_line_to(cr, rect->w - tl, yimg);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    for (y = ticks->from; y <= ticks->to + 1e-14*bs; y += bs) {
        PangoRectangle logical;

        y = fixzero(y);
        yimg = (y - yoffset)*scale + 0.5*lw;
        format_layout_numeric(args, layout, &logical, s, "%.*f", vf->precision, y);
        if (yimg + logical.height/pangoscale <= rect->h) {
            cairo_move_to(cr, rect->w - tl - lw, yimg);
            cairo_rel_move_to(cr, -logical.width/pangoscale, 0.0);
            pango_cairo_show_layout(cr, layout);
        }
    };
    cairo_restore(cr);
}

static void
draw_inset(ModuleArgs *args,
           const ImgExportSizes *sizes,
           PangoLayout *layout,
           GString *s,
           cairo_t *cr)
{
    GwyParams *params = args->params;
    ImgExportLateralType xytype = gwy_params_get_enum(params, PARAM_XYTYPE);
    GwyRGBA colour = gwy_params_get_color(params, PARAM_INSET_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(params, PARAM_INSET_OUTLINE_COLOR);
    gboolean inset_draw_ticks = gwy_params_get_boolean(params, PARAM_INSET_DRAW_TICKS);
    gboolean inset_draw_text_above = gwy_params_get_boolean(params, PARAM_INSET_DRAW_TEXT_ABOVE);
    gboolean inset_draw_label = gwy_params_get_boolean(params, PARAM_INSET_DRAW_LABEL);
    const gchar *inset_length = gwy_params_get_string(params, PARAM_INSET_LENGTH);
    GwyDataField *dfield = args->env->dfield;
    gdouble xreal = gwy_data_field_get_xreal(dfield);
    const ImgExportRect *rect = &sizes->inset, *imgrect = &sizes->image;
    PangoRectangle logical, ink;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble olw = sizes->sizes.inset_outline_width;
    gdouble xcentre, length, y;
    gdouble w = imgrect->w - 2.0*lw;
    gdouble h = imgrect->h - 2.0*lw;

    if (xytype != IMGEXPORT_LATERAL_INSET)
        return;

    if (!(sizes->inset_length > 0.0))
        return;

    length = (sizes->image.w - 2.0*lw)/xreal*sizes->inset_length;
    xcentre = 0.5*rect->w;
    y = 0.5*lw;

    cairo_save(cr);
    cairo_rectangle(cr, imgrect->x + lw, imgrect->y + lw, w, h);
    cairo_clip(cr);
    cairo_translate(cr, rect->x, rect->y);
    cairo_push_group(cr);

    cairo_save(cr);
    if (inset_draw_ticks)
        y = 0.5*tl;

    if (olw > 0.0) {
        if (inset_draw_ticks) {
            draw_line_outline(cr, xcentre - 0.5*length, 0.0, xcentre - 0.5*length, tl + lw, &outcolour, lw, olw);
            draw_line_outline(cr, xcentre + 0.5*length, 0.0, xcentre + 0.5*length, tl + lw, &outcolour, lw, olw);
        }
        draw_line_outline(cr, xcentre - 0.5*length, y + 0.5*lw, xcentre + 0.5*length, y + 0.5*lw, &outcolour, lw, olw);

        if (inset_draw_text_above)
            y = -2.0*lw;
        else if (inset_draw_ticks)
            y = tl + 2.0*lw;
        else
            y = 2.0*lw;

        if (inset_draw_label) {
            cairo_save(cr);
            format_layout(layout, &logical, s, "%s", inset_length);
            /* We need ink rectangle to position labels with no ink below baseline, such as 100 nm, as expected. */
            pango_layout_get_extents(layout, &ink, NULL);
            if (inset_draw_text_above)
                y -= (ink.y + ink.height)/pangoscale;
            cairo_move_to(cr, xcentre - 0.5*ink.width/pangoscale, y);
            draw_text_outline(cr, layout, &outcolour, olw);
            cairo_restore(cr);
        }
    }

    y = 0.5*lw;
    if (inset_draw_ticks) {
        y = 0.5*tl;
        cairo_move_to(cr, xcentre - 0.5*length, 0.0);
        cairo_rel_line_to(cr, 0.0, tl + lw);
        cairo_move_to(cr, xcentre + 0.5*length, 0.0);
        cairo_rel_line_to(cr, 0.0, tl + lw);
    }
    cairo_move_to(cr, xcentre - 0.5*length, y + 0.5*lw);
    cairo_line_to(cr, xcentre + 0.5*length, y + 0.5*lw);
    cairo_set_line_width(cr, lw);
    set_cairo_source_rgba(cr, &colour);
    cairo_stroke(cr);
    cairo_restore(cr);

    if (inset_draw_text_above)
        y = -2.0*lw;
    else if (inset_draw_ticks)
        y = tl + 2.0*lw;
    else
        y = 2.0*lw;

    if (inset_draw_label) {
        cairo_save(cr);
        format_layout(layout, &logical, s, "%s", inset_length);
        /* We need ink rectangle to position labels with no ink below baseline, such as 100 nm, as expected. */
        pango_layout_get_extents(layout, &ink, NULL);
        if (inset_draw_text_above)
            y -= (ink.y + ink.height)/pangoscale;
        cairo_move_to(cr, xcentre - 0.5*ink.width/pangoscale, y);
        draw_text(cr, layout, &colour);
        cairo_restore(cr);
    }
    cairo_pop_group_to_source(cr);
    /* Unlike cairo_set_source_rgb() vs cairo_set_source_rgba(), this does make a difference. */
    if (colour.a < 1.0 - 1e-14)
        cairo_paint_with_alpha(cr, colour.a);
    else
        cairo_paint(cr);

    cairo_restore(cr);
}

static void
draw_title(ModuleArgs *args,
           const ImgExportSizes *sizes,
           PangoLayout *layout,
           GString *s,
           cairo_t *cr)
{
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->title;
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    ImgExportTitleType title_type = gwy_params_get_enum(args->params, PARAM_TITLE_TYPE);
    gboolean units_in_title = gwy_params_get_boolean(args->params, PARAM_UNITS_IN_TITLE);
    gdouble title_gap = gwy_params_get_double(args->params, PARAM_TITLE_GAP);
    GwySIValueFormat *vf = sizes->vf_fmruler;
    gdouble fs = sizes->sizes.font_size;
    PangoRectangle logical;
    gdouble gap = 0.0;

    if (title_type == IMGEXPORT_TITLE_NONE)
        return;

    if (title_type == IMGEXPORT_TITLE_FMSCALE)
        gap = fs*title_gap;

    cairo_save(cr);
    cairo_translate(cr, rect->x + gap, rect->y);
    set_cairo_source_rgba(cr, &color);
    if (units_in_title && strlen(vf->units))
        format_layout(layout, &logical, s, "%s [%s]", env->title, vf->units);
    else
        format_layout(layout, &logical, s, "%s", env->title);
    cairo_move_to(cr, 0.0, 0.0);
    if (title_type == IMGEXPORT_TITLE_FMSCALE)
        cairo_rotate(cr, -0.5*G_PI);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
draw_mask_legend(ModuleArgs *args,
                 const ImgExportSizes *sizes,
                 PangoLayout *layout,
                 GString *s,
                 cairo_t *cr)
{
    gdouble maskkey_gap = gwy_params_get_double(args->params, PARAM_MASKKEY_GAP);
    const gchar *mask_key = gwy_params_get_string(args->params, PARAM_MASK_KEY);
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    gboolean draw_mask = gwy_params_get_boolean(args->params, PARAM_DRAW_MASK);
    gboolean draw_maskkey = gwy_params_get_boolean(args->params, PARAM_DRAW_MASKKEY);
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->maskkey;
    PangoRectangle logical;
    gdouble fs = sizes->sizes.font_size;
    gdouble lw = sizes->sizes.line_width;
    gdouble h, hgap, vgap, yoff;

    if (!draw_mask || !draw_maskkey || !env->mask)
        return;

    h = 1.5*fs + 2.0*lw;    /* Match fmscale width */
    vgap = fs*maskkey_gap;
    hgap = 0.5*h;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y + vgap);
    cairo_rectangle(cr, 0.5*lw, 0.5*lw, 1.4*h - lw, h - lw);
    set_cairo_source_rgba(cr, &env->mask_colour);
    cairo_fill_preserve(cr);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
    cairo_restore(cr);

    cairo_save(cr);
    format_layout(layout, &logical, s, "%s", mask_key);
    yoff = 0.5*(logical.height/pangoscale - h);
    cairo_translate(cr, rect->x + 1.4*h + hgap, rect->y + vgap - yoff);
    set_cairo_source_rgba(cr, &color);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);
}

static void
draw_fmgrad(ModuleArgs *args,
            const ImgExportSizes *sizes,
            cairo_t *cr)
{
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    ImgExportValueType ztype = gwy_params_get_enum(args->params, PARAM_ZTYPE);
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->fmgrad;
    const GwyGradientPoint *points;
    cairo_pattern_t *pat;
    gint npoints, i;
    gdouble lw = sizes->sizes.line_width;
    gboolean inverted = env->fm_inverted;
    gdouble w = rect->w - 2.0*lw;
    gdouble h = rect->h - 2.0*lw;

    if (ztype != IMGEXPORT_VALUE_FMSCALE)
        return;

    if (inverted)
        pat = cairo_pattern_create_linear(0.0, lw, 0.0, lw + h);
    else
        pat = cairo_pattern_create_linear(0.0, lw + h, 0.0, lw);

    /* We don't get here in grey16 export mode so we don't care. */
    points = gwy_gradient_get_points(env->gradient, &npoints);
    for (i = 0; i < npoints; i++) {
        const GwyGradientPoint *gpt = points + i;
        const GwyRGBA *ptcolor = &gpt->color;

        cairo_pattern_add_color_stop_rgb(pat, gpt->x, ptcolor->r, ptcolor->g, ptcolor->b);
    }
    cairo_pattern_set_filter(pat, CAIRO_FILTER_BILINEAR);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    cairo_rectangle(cr, lw, lw, w, h);
    cairo_clip(cr);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_restore(cr);

    cairo_pattern_destroy(pat);

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    cairo_rectangle(cr, 0.5*lw, 0.5*lw, w + lw, h + lw);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static void
draw_fmruler(ModuleArgs *args,
             const ImgExportSizes *sizes,
             PangoLayout *layout,
             GString *s,
             cairo_t *cr)
{
    ImgExportValueType ztype = gwy_params_get_enum(args->params, PARAM_ZTYPE);
    GwyRGBA color = gwy_params_get_color(args->params, PARAM_LINETEXT_COLOR);
    gboolean units_in_title = gwy_params_get_boolean(args->params, PARAM_UNITS_IN_TITLE);
    const ImgExportEnv *env = args->env;
    const ImgExportRect *rect = &sizes->fmruler;
    const RulerTicks *ticks = &sizes->fmruler_ticks;
    GwySIValueFormat *vf = sizes->vf_fmruler;
    gdouble lw = sizes->sizes.line_width;
    gdouble tl = sizes->sizes.tick_length;
    gdouble uw = sizes->fmruler_units_width;
    gdouble z, bs, scale, yimg, min, max, real, w, yoff;
    PangoRectangle logical, ink;
    GArray *mticks;
    guint nticks, i;

    if (ztype != IMGEXPORT_VALUE_FMSCALE)
        return;

    min = env->fm_min/vf->magnitude;
    max = env->fm_max/vf->magnitude;
    real = max - min;

    /* Draw the edge ticks first */
    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    cairo_move_to(cr, 0.0, 0.5*lw);
    cairo_rel_line_to(cr, tl, 0.0);
    cairo_move_to(cr, 0.0, rect->h - 0.5*lw);
    cairo_rel_line_to(cr, tl, 0.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    if (env->has_presentation)
        return;

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    if (units_in_title)
        format_layout_numeric(args, layout, &logical, s, "%.*f", vf->precision, max);
    else
        format_layout_numeric(args, layout, &logical, s, "%.*f %s", vf->precision, max, vf->units);
    w = logical.width/pangoscale;
    pango_layout_get_extents(layout, &ink, NULL);
    yoff = (logical.height - (ink.height + ink.y))/pangoscale;
    gwy_debug("max '%s' (%g x %g)", s->str, w, logical.height/pangoscale);
    cairo_move_to(cr, rect->w - w, lw - 0.5*yoff);
    pango_cairo_show_layout(cr, layout);
    format_layout_numeric(args, layout, &logical, s, "%.*f", vf->precision, min);
    w = logical.width/pangoscale;
    gwy_debug("min '%s' (%g x %g)", s->str, logical.width/pangoscale, logical.height/pangoscale);
    cairo_move_to(cr, rect->w - uw - w, rect->h - lw - logical.height/pangoscale);
    pango_cairo_show_layout(cr, layout);
    cairo_restore(cr);

    if (real < 1e-14)
        return;

    scale = (rect->h - lw)/real;
    bs = ticks->step*ticks->base;

    mticks = g_array_new(FALSE, FALSE, sizeof(gdouble));
    for (z = ticks->from; z <= ticks->to + 1e-14*bs; z += bs)
        g_array_append_val(mticks, z);
    nticks = mticks->len;

    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT && env->fm_min < env->fm_max) {
        gdouble *td;

        g_array_set_size(mticks, 2*nticks);
        td = (gdouble*)mticks->data;
        for (i = 0; i < nticks; i++)
            td[i] *= vf->magnitude;
        gwy_draw_data_field_map_adaptive(env->dfield, td, td + nticks, nticks);
        for (i = 0; i < nticks; i++)
            td[i] = ticks->from + (ticks->to - ticks->from)*td[i + nticks];

        g_array_set_size(mticks, nticks);
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    cairo_set_line_width(cr, lw);
    for (i = 0; i < nticks; i++) {
        z = g_array_index(mticks, gdouble, i);
        yimg = (max - z)*scale + lw;
        if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT) {
            if (yimg <= lw || yimg + lw >= rect->h)
                continue;
        }
        else {
            if (yimg <= sizes->fmruler_label_height + 4.0*lw
                || yimg + sizes->fmruler_label_height + 4.0*lw >= rect->h)
                continue;
        }

        cairo_move_to(cr, 0.0, yimg);
        cairo_rel_line_to(cr, tl, 0.0);
    };
    cairo_stroke(cr);
    cairo_restore(cr);

    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_ADAPT) {
        g_array_free(mticks, TRUE);
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x, rect->y);
    set_cairo_source_rgba(cr, &color);
    for (i = 0; i < nticks; i++) {
        z = g_array_index(mticks, gdouble, i);
        z = fixzero(z);
        yimg = (max - z)*scale + lw;
        if (yimg <= sizes->fmruler_label_height + 4.0*lw
            || yimg + 2.0*sizes->fmruler_label_height + 4.0*lw >= rect->h)
            continue;

        format_layout_numeric(args, layout, &logical, s, "%.*f", vf->precision, z);
        w = logical.width/pangoscale;
        cairo_move_to(cr, rect->w - uw - w, yimg - 0.5*yoff);
        pango_cairo_show_layout(cr, layout);
    };
    cairo_restore(cr);

    g_array_free(mticks, TRUE);
}

static const ImgExportSelectionType*
find_selection_type(ModuleArgs *args,
                    const gchar *name,
                    GwySelection **psel)
{
    const ImgExportEnv *env = args->env;
    const gchar *typename;
    GwySelection *sel;
    gchar *key;
    guint i;

    if (psel)
        *psel = NULL;

    if (!name || !strlen(name))
        return NULL;

    key = g_strdup_printf("/%d/select/%s", env->id, name);
    sel = GWY_SELECTION(gwy_container_get_object_by_name(env->data, key));
    g_free(key);

    if (psel)
        *psel = sel;

    typename = G_OBJECT_TYPE_NAME(sel);
    for (i = 0; i < G_N_ELEMENTS(known_selections); i++) {
        const ImgExportSelectionType *seltype = known_selections + i;
        if (gwy_strequal(typename, seltype->typename))
            return seltype;
    }
    return NULL;
}

static void
draw_selection(ModuleArgs *args,
               const ImgExportSizes *sizes,
               PangoLayout *layout,
               GString *s,
               cairo_t *cr)
{
    gboolean draw_selection = gwy_params_get_boolean(args->params, PARAM_DRAW_SELECTION);
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    const gchar *selection_name = gwy_params_get_string(args->params, PARAM_SELECTION);
    const ImgExportEnv *env = args->env;
    const ImgExportSelectionType *seltype;
    const ImgExportRect *rect = &sizes->image;
    gdouble lw = sizes->sizes.line_width;
    GwyDataField *dfield = env->dfield;
    gdouble xreal = gwy_data_field_get_xreal(dfield);
    gdouble yreal = gwy_data_field_get_yreal(dfield);
    gdouble w = rect->w - 2.0*lw;
    gdouble h = rect->h - 2.0*lw;
    gdouble qx = w/xreal;
    gdouble qy = h/yreal;
    GwySelection *sel;

    if (!draw_selection)
        return;

    if (!(seltype = find_selection_type(args, selection_name, &sel)))
        return;
    if (!seltype->draw) {
        g_warning("Can't draw %s yet.", seltype->typename);
        return;
    }

    cairo_save(cr);
    cairo_translate(cr, rect->x + lw, rect->y + lw);
    cairo_rectangle(cr, 0.0, 0.0, w, h);
    cairo_clip(cr);
    set_cairo_source_rgb(cr, &colour);
    cairo_set_line_width(cr, lw);
    cairo_push_group(cr);
    seltype->draw(args, sizes, sel, qx, qy, layout, s, cr);
    cairo_pop_group_to_source(cr);
    /* Unlike cairo_set_source_rgb() vs cairo_set_source_rgba(), this does make a difference. */
    if (colour.a < 1.0 - 1e-14)
        cairo_paint_with_alpha(cr, colour.a);
    else
        cairo_paint(cr);
    cairo_restore(cr);
}

/* We assume cr is already created for the layout with the correct scale(!). */
static void
image_draw_cairo(ModuleArgs *args,
                 const ImgExportSizes *sizes,
                 cairo_t *cr)
{
    const gchar *font = gwy_params_get_string(args->params, PARAM_FONT);
    PangoLayout *layout;
    GString *s = g_string_new(NULL);

    layout = create_layout(font, sizes->sizes.font_size, cr);

    draw_background(args, cr);
    draw_data(args, sizes, cr);
    draw_inset(args, sizes, layout, s, cr);
    draw_selection(args, sizes, layout, s, cr);
    draw_data_frame(args, sizes, cr);
    draw_hruler(args, sizes, layout, s, cr);
    draw_vruler(args, sizes, layout, s, cr);
    draw_fmgrad(args, sizes, cr);
    draw_fmruler(args, sizes, layout, s, cr);
    draw_title(args, sizes, layout, s, cr);
    draw_mask_legend(args, sizes, layout, s, cr);

    g_object_unref(layout);
    g_string_free(s, TRUE);
}

static GdkPixbuf*
render_pixbuf(ModuleArgs *args, const gchar *name)
{
    ImgExportSizes *sizes;
    cairo_surface_t *surface;
    GdkPixbuf *pixbuf;
    guchar *imgdata, *pixels;
    guint xres, yres, imgrowstride, pixrowstride, i;
    gboolean can_transp = args->env->format->supports_transparency;
    gboolean want_transp = gwy_params_get_boolean(args->params, PARAM_TRANSPARENT_BG);
    gboolean transparent_bg = (can_transp && want_transp);
    cairo_format_t imgformat;
    cairo_t *cr;

    gwy_debug("format name %s", name);
    sizes = calculate_sizes(args, name);
    g_return_val_if_fail(sizes, FALSE);
    surface = create_surface(name, NULL, sizes->canvas.w, sizes->canvas.h, transparent_bg);
    cr = cairo_create(surface);
    image_draw_cairo(args, sizes, cr);
    cairo_surface_flush(surface);
    cairo_destroy(cr);

    imgdata = cairo_image_surface_get_data(surface);
    xres = cairo_image_surface_get_width(surface);
    yres = cairo_image_surface_get_height(surface);
    imgrowstride = cairo_image_surface_get_stride(surface);
    imgformat = cairo_image_surface_get_format(surface);
    if (transparent_bg) {
        g_return_val_if_fail(imgformat == CAIRO_FORMAT_ARGB32, NULL);
    }
    else {
        g_return_val_if_fail(imgformat == CAIRO_FORMAT_RGB24, NULL);
    }
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, transparent_bg, 8, xres, yres);
    pixrowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    /* Things can get a bit confusing here due to the heavy impedance matching necessary.
     *
     * Byte order:
     * (1) GdkPixbuf is endian-independent.  The order is always R, G, B[, A], i.e. it stores individual components as
     *     bytes, not 24bit or 32bit integers.  If seen as an RGB[A] integer, it is always big-endian.
     * (2) Cairo is endian-dependent.  Pixel is a native-endian 32bit integer with alpha in the high 8 bits (if
     *     present) and then R, G, B from the highest to lowest remaining bits.
     *
     * Alpha:
     * (A) GdkPixbuf uses non-premultiplied alpha (as most image formats, except TIFF apparently).
     * (B) Cairo uses premultiplied alpha.
     **/
#ifdef _OPENMP
#pragma omp parallel for if (gwy_threads_are_enabled()) default(none) \
            shared(imgdata,pixels,imgrowstride,pixrowstride,xres,yres,transparent_bg) \
            private(i)
#endif
    for (i = 0; i < yres; i++) {
        const guchar *p = imgdata + i*imgrowstride;
        guchar *q = pixels + i*pixrowstride;
        guint j;

        if (G_BYTE_ORDER == G_LITTLE_ENDIAN) {
            if (transparent_bg) {
                /* Convert (A*B, A*G, A*R, A) to (R, G, B, A). */
                for (j = xres; j; j--, p += 4, q += 4) {
                    guint a = *(q + 3) = *(p + 3);
                    if (a == 0xff) {
                        *q = *(p + 2);
                        *(q + 1) = *(p + 1);
                        *(q + 2) = *p;
                    }
                    else if (a == 0x00) {
                        *q = *(q + 1) = *(q + 2) = 0;
                    }
                    else {
                        /* This is the same unpremultiplication formula as Cairo uses. */
                        *q = (*(p + 2)*0xff + a/2)/a;
                        *(q + 1) = (*(p + 1)*0xff + a/2)/a;
                        *(q + 2) = ((*p)*0xff + a/2)/a;
                    }
                }
            }
            else {
                /* Convert (B, G, R, unused) to (R, G, B). */
                for (j = xres; j; j--, p += 4, q += 3) {
                    *q = *(p + 2);
                    *(q + 1) = *(p + 1);
                    *(q + 2) = *p;
                }
            }
        }
        else {
            if (transparent_bg) {
                /* Convert (A, A*R, A*G, A*B) to (R, G, B, A). */
                for (j = xres; j; j--, p += 4, q += 4) {
                    guint a = *(q + 3) = *p;
                    if (a == 0xff) {
                        *q = *(p + 1);
                        *(q + 1) = *(p + 2);
                        *(q + 2) = *(p + 3);
                    }
                    else if (a == 0x00) {
                        *q = *(q + 1) = *(q + 2) = 0;
                    }
                    else {
                        /* This is the same unpremultiplication formula as Cairo uses. */
                        *q = (*(p + 1)*0xff + a/2)/a;
                        *(q + 1) = (*(p + 2)*0xff + a/2)/a;
                        *(q + 2) = (*(p + 3)*0xff + a/2)/a;
                    }
                }
            }
            else {
                /* Convert (unused, R, G, B) to (R, G, B). */
                for (j = xres; j; j--, p += 4, q += 3) {
                    *q = *(p + 1);
                    *(q + 1) = *(p + 2);
                    *(q + 2) = *(p + 3);
                }
            }
        }
    }

    cairo_surface_destroy(surface);
    destroy_sizes(sizes);

    return pixbuf;
}

/* Try to ensure the preview looks at least a bit like the final rendering. Slight sizing issues can be forgiven but
 * we must not change tick step and tick label precision between preview and final rendering. */
static void
preview(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    ModuleArgs *args = gui->args;
    gboolean is_vector = !!args->env->format->write_vector;
    GwyParams *params = args->params;
    GwyParams *previewparams;
    gboolean scale_font = gwy_params_get_boolean(params, PARAM_SCALE_FONT);
    gdouble required_zoom = gwy_params_get_double(params, PARAM_ZOOM);
    ImgExportSizes *sizes;
    gdouble r, zoomcorr, rescale = 1.0;
    guint width, height, iter;
    GdkPixbuf *pixbuf = NULL;

    args->params = previewparams = gwy_params_duplicate(params);

    if (gwy_params_get_enum(previewparams, PARAM_MODE) == IMGEXPORT_MODE_GREY16) {
        gwy_params_set_enum(previewparams, PARAM_XYTYPE, IMGEXPORT_LATERAL_NONE);
        gwy_params_set_enum(previewparams, PARAM_ZTYPE, IMGEXPORT_VALUE_NONE);
        gwy_params_set_enum(previewparams, PARAM_TITLE_TYPE, IMGEXPORT_TITLE_NONE);
        gwy_params_set_double(previewparams, PARAM_LINE_WIDTH, 0.0);
        gwy_params_set_boolean(previewparams, PARAM_DRAW_MASK, FALSE);
        gwy_params_set_boolean(previewparams, PARAM_DRAW_MASKKEY, FALSE);
        gwy_params_set_boolean(previewparams, PARAM_DRAW_SELECTION, FALSE);
        gwy_params_set_enum(previewparams, PARAM_INTERPOLATION, GWY_INTERPOLATION_ROUND);
    }
    if (is_vector)
        gwy_params_set_double(previewparams, PARAM_ZOOM, 1.0);

    sizes = calculate_sizes(args, "png");
    g_return_if_fail(sizes);
    /* Make all things in the preview scale. */
    gwy_params_set_boolean(previewparams, PARAM_SCALE_FONT, TRUE);
    zoomcorr = PREVIEW_SIZE/MAX(sizes->canvas.w, sizes->canvas.h);
    destroy_sizes(sizes);
    if (is_vector) {
        r = 1.0/(mm2pt*gwy_params_get_double(previewparams, PARAM_PXWIDTH));
        zoomcorr *= r;
        if (!scale_font)
            rescale = r;
    }
    else {
        zoomcorr *= required_zoom;
        if (!scale_font)
            rescale = 1.0/required_zoom;
    }
    gwy_params_set_double(previewparams, PARAM_ZOOM, zoomcorr);
    scale_sizes_in_params(previewparams, rescale);

    for (iter = 0; iter < 4; iter++) {
        GWY_OBJECT_UNREF(pixbuf);
        pixbuf = render_pixbuf(args, "png");
        /* The sizes may be way off when the fonts are huge compared to the image and so on.  Try to correct that and
         * render again. */
        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        if (MAX(width, height) == PREVIEW_SIZE)
            break;
        zoomcorr = (gdouble)PREVIEW_SIZE/MAX(width, height);
        gwy_debug("zoomcorr#%u %g", iter, zoomcorr);
        gwy_params_set_double(previewparams, PARAM_ZOOM,
                              pow(zoomcorr, 0.96)*gwy_params_get_double(previewparams, PARAM_ZOOM));
    }

    gtk_image_set_from_pixbuf(GTK_IMAGE(gui->preview), pixbuf);
    g_object_unref(pixbuf);

    args->params = params;
    g_object_unref(previewparams);
}

static void
append_slider(GwyParamTable *table, gint id,
              GwyScaleMappingType mapping, const gchar *unitstr, gint digits)
{
    gwy_param_table_append_slider(table, id);
    gwy_param_table_slider_set_mapping(table, id, mapping);
    if (digits >= 0)
        gwy_param_table_slider_set_digits(table, id, digits);
    if (unitstr)
        gwy_param_table_set_unitstr(table, id, unitstr);
}

static void
append_color(GwyParamTable *table, gint id)
{
    gwy_param_table_append_color(table, id);
    gwy_param_table_color_add_preset(table, id, black, _("Black"));
    gwy_param_table_color_add_preset(table, id, white, _("White"));
}

static void
font_changed(ModuleGUI *gui, GtkFontButton *button)
{
    GwyParams *params = gui->args->params;
    const gchar *full_font = gtk_font_button_get_font_name(button);
    const gchar *size_pos = strrchr(full_font, ' ');
    gchar *end, *font_name;
    gdouble size;
    gboolean changed;

    if (!size_pos) {
        g_warning("Cannot parse font description `%s' into name and size.", full_font);
        return;
    }
    size = g_ascii_strtod(size_pos+1, &end);
    if (end == size_pos+1) {
        g_warning("Cannot parse font description `%s' into name and size.", full_font);
        return;
    }

    /* NB: full_font obtained this way can have a trailing comma when the comma is needed when specifying the font,
     * so we preserve it in the font name. */
    font_name = g_strndup(full_font, size_pos-full_font);
    g_strchomp(font_name);
    changed = gwy_params_set_string(params, PARAM_FONT, font_name);
    g_free(font_name);

    if (size > 0.0)
        gwy_param_table_set_double(gui->table_basic, PARAM_FONT_SIZE, size);
    if (changed)
        gwy_param_table_param_changed(gui->table_basic, PARAM_FONT);
}

static void
update_selected_font(ModuleGUI *gui)
{
    GwyParams *params = gui->args->params;
    gchar *full_font;
    gdouble font_size = gwy_params_get_double(params, PARAM_FONT_SIZE);

    full_font = g_strdup_printf("%s %.1f", gwy_params_get_string(params, PARAM_FONT), font_size);
    gtk_font_button_set_font_name(GTK_FONT_BUTTON(gui->font), full_font);
    g_free(full_font);
}

static GtkWidget*
create_font_button(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GtkWidget *hbox, *label;

    gui->font = gtk_font_button_new();
    gtk_font_button_set_show_size(GTK_FONT_BUTTON(gui->font), FALSE);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(gui->font), TRUE);
    update_selected_font(gui);

    label = gtk_label_new_with_mnemonic(_("_Font:"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), gui->font);

    hbox = gwy_hbox_new(0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), gui->font, FALSE, FALSE, 0);
    g_signal_connect_swapped(gui->font, "font-set", G_CALLBACK(font_changed), gui);

    gtk_widget_show_all(hbox);

    return hbox;
}

static GtkWidget*
basic_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    ImgExportEnv *env = args->env;
    gboolean is_vector = !!env->format->write_vector, can_transp = env->format->supports_transparency;
    GwyParamTable *table;

    table = gui->table_basic = gwy_param_table_new(args->params);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gwy_param_table_append_header(table, -1, _("Physical Dimensions"));
    if (is_vector) {
        /* TODO: check if explicit digits on everything are really necessary. */
        append_slider(table, PARAM_PXWIDTH, GWY_SCALE_MAPPING_LOG, "mm", 3);
        append_slider(table, PARAM_PPI, GWY_SCALE_MAPPING_LOG, NULL, 3);
        append_slider(table, PARAM_VECTOR_WIDTH, GWY_SCALE_MAPPING_LOG, "mm", 1);
        append_slider(table, PARAM_VECTOR_HEIGHT, GWY_SCALE_MAPPING_LOG, "mm", 1);
        gwy_param_table_set_no_reset(table, PARAM_VECTOR_WIDTH, TRUE);
        gwy_param_table_set_no_reset(table, PARAM_VECTOR_HEIGHT, TRUE);
    }
    else {
        append_slider(table, PARAM_ZOOM, GWY_SCALE_MAPPING_LOG, NULL, 3);
        gwy_param_table_slider_restrict_range(table, PARAM_ZOOM, env->minzoom, env->maxzoom);
        append_slider(table, PARAM_BITMAP_WIDTH, GWY_SCALE_MAPPING_LOG, "px", -1);
        append_slider(table, PARAM_BITMAP_HEIGHT, GWY_SCALE_MAPPING_LOG, "px", -1);
        gwy_param_table_set_no_reset(table, PARAM_BITMAP_WIDTH, TRUE);
        gwy_param_table_set_no_reset(table, PARAM_BITMAP_HEIGHT, TRUE);
    }

    gwy_param_table_append_header(table, -1, _("Parameters"));
    gwy_param_table_append_foreign(table, PARAM_FONT, create_font_button, gui, NULL);
    append_slider(table, PARAM_FONT_SIZE, GWY_SCALE_MAPPING_LOG, NULL, -1);
    gwy_param_table_append_slider(table, PARAM_LINE_WIDTH);
    gwy_param_table_append_slider(table, PARAM_BORDER_WIDTH);
    gwy_param_table_append_slider(table, PARAM_TICK_LENGTH);
    gwy_param_table_append_checkbox(table, PARAM_SCALE_FONT);
    gwy_param_table_append_checkbox(table, PARAM_DECOMMA);

    gwy_param_table_append_header(table, -1, _("Colors"));
    append_color(table, PARAM_LINETEXT_COLOR);
    if (can_transp)
        gwy_param_table_append_checkbox(table, PARAM_TRANSPARENT_BG);
    append_color(table, PARAM_BG_COLOR);

    return gwy_param_table_widget(table);
}

static void
inset_pos_changed(ModuleGUI *gui, G_GNUC_UNUSED GtkToggleButton *button)
{
    if (gwy_params_set_enum(gui->args->params, PARAM_INSET_POS, gwy_radio_buttons_get_current(gui->inset_pos)))
        gwy_param_table_param_changed(gui->table_lateral, PARAM_INSET_POS);
}

static void
inset_pos_add(ModuleGUI *gui, GtkTable *table, gint row, gint col, InsetPosType pos)
{
    GtkWidget *button, *align;

    align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
    gtk_table_attach(table, align, col, col+1, row, row+1, GTK_FILL, 0, 0, 0);
    button = gtk_radio_button_new_with_label(gui->inset_pos, NULL);
    gtk_container_add(GTK_CONTAINER(align), button);
    gui->inset_pos = gtk_radio_button_get_group(GTK_RADIO_BUTTON(button));
    g_object_set_qdata(G_OBJECT(button), gui->rb_quark, GUINT_TO_POINTER(pos));
}

static GtkWidget*
create_inset_pos_table(gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    GtkWidget *label;
    GtkTable *table;
    GSList *l;

    gui->rb_quark = g_quark_from_string("gwy-radiobuttons-key");

    table = GTK_TABLE(gtk_table_new(3, 4, FALSE));
    gtk_table_set_row_spacings(table, 2);
    gtk_table_set_col_spacings(table, 6);

    gui->inset_pos_label[0] = label = gwy_label_new_header(_("Placement"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    gui->inset_pos_label[1] = label = gtk_label_new(_("left"));
    gtk_table_attach(table, label, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);

    gui->inset_pos_label[2] = label = gtk_label_new(_("center"));
    gtk_table_attach(table, label, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);

    gui->inset_pos_label[3] = label = gtk_label_new(_("right"));
    gtk_table_attach(table, label, 3, 4, 0, 1, GTK_FILL, 0, 0, 0);

    gui->inset_pos_label[4] = label = gtk_label_new(_("top"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    inset_pos_add(gui, table, 1, 1, INSET_POS_TOP_LEFT);
    inset_pos_add(gui, table, 1, 2, INSET_POS_TOP_CENTER);
    inset_pos_add(gui, table, 1, 3, INSET_POS_TOP_RIGHT);

    gui->inset_pos_label[5] = label = gtk_label_new(_("bottom"));
    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
    gtk_table_attach(table, label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL, 0, 0, 0);

    inset_pos_add(gui, table, 2, 1, INSET_POS_BOTTOM_LEFT);
    inset_pos_add(gui, table, 2, 2, INSET_POS_BOTTOM_CENTER);
    inset_pos_add(gui, table, 2, 3, INSET_POS_BOTTOM_RIGHT);

    gwy_radio_buttons_set_current(gui->inset_pos, gwy_params_get_enum(gui->args->params, PARAM_INSET_POS));
    for (l = gui->inset_pos; l; l = g_slist_next(l))
        g_signal_connect_swapped(l->data, "clicked", G_CALLBACK(inset_pos_changed), gui);

    gtk_widget_show_all(GTK_WIDGET(table));

    return GTK_WIDGET(table);
}

static GtkWidget*
lateral_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_lateral = gwy_param_table_new(args->params);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gwy_param_table_append_header(table, -1, _("Lateral Scale"));
    gwy_param_table_append_combo(table, PARAM_XYTYPE);
    gwy_param_table_append_entry(table, PARAM_INSET_LENGTH);
    gwy_param_table_append_button(table, BUTTON_AUTO_LENGTH, -1, RESPONSE_AUTO_LENGTH,
                                  _("Choose Length _Automatically"));
    gwy_param_table_append_separator(table);
    gwy_param_table_append_foreign(table, PARAM_INSET_POS, create_inset_pos_table, gui, NULL);
    gwy_param_table_append_separator(table);
    append_slider(table, PARAM_INSET_XGAP, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    append_slider(table, PARAM_INSET_YGAP, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_append_header(table, -1, _("Options"));
    append_color(table, PARAM_INSET_RGB);
    append_color(table, PARAM_INSET_OUTLINE_COLOR);
    append_slider(table, PARAM_INSET_OUTLINE_WIDTH, GWY_SCALE_MAPPING_SQRT, NULL, 2);
    append_slider(table, PARAM_INSET_ALPHA, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_append_checkbox(table, PARAM_INSET_DRAW_TICKS);
    gwy_param_table_append_checkbox(table, PARAM_INSET_DRAW_LABEL);
    gwy_param_table_append_checkbox(table, PARAM_INSET_DRAW_TEXT_ABOVE);

    return gwy_param_table_widget(table);
}

static GtkWidget*
value_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    ImgExportEnv *env = args->env;
    GwyParamTable *table;

    table = gui->table_value = gwy_param_table_new(args->params);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gwy_param_table_append_header(table, -1, _("Image"));
    gwy_param_table_append_combo(table, env->format->write_vector ? PARAM_INTERPOLATION_VECTOR : PARAM_INTERPOLATION);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_FRAME);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_MASK);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_MASKKEY);
    gwy_param_table_append_entry(table, PARAM_MASK_KEY);
    append_slider(table, PARAM_MASKKEY_GAP, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_append_header(table, -1, _("Value Scale"));
    gwy_param_table_append_radio(table, PARAM_ZTYPE);
    append_slider(table, PARAM_FMSCALE_GAP, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    append_slider(table, PARAM_FMSCALE_PRECISION, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_add_enabler(table, PARAM_FIX_FMSCALE_PRECISION, PARAM_FMSCALE_PRECISION);
    append_slider(table, PARAM_KILO_THRESHOLD, GWY_SCALE_MAPPING_LOG, NULL, -1);
    gwy_param_table_add_enabler(table, PARAM_FIX_KILO_THRESHOLD, PARAM_KILO_THRESHOLD);
    gwy_param_table_append_header(table, -1, _("Title"));
    gwy_param_table_append_combo(table, PARAM_TITLE_TYPE);
    append_slider(table, PARAM_TITLE_GAP, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_append_checkbox(table, PARAM_UNITS_IN_TITLE);

    return gwy_param_table_widget(table);
}

static void
sel_render_cell(GtkTreeViewColumn *column,
                GtkCellRenderer *renderer,
                GtkTreeModel *model,
                GtkTreeIter *iter,
                gpointer user_data)
{
    ModuleGUI *gui = (ModuleGUI*)user_data;
    const ImgExportEnv *env = gui->args->env;
    GArray *selections = env->selections;
    const ImgExportSelectionType *seltype;
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "id"));
    GwySelection *sel;
    const gchar *name;
    gchar *s;
    guint id;

    gtk_tree_model_get(model, iter, 0, &id, -1);
    name = g_quark_to_string(g_array_index(selections, GQuark, id));
    if (i == COLUMN_SEL_NAME)
        g_object_set(renderer, "text", name, NULL);
    else if (i == COLUMN_SEL_TYPE) {
        seltype = find_selection_type(gui->args, name, NULL);
        g_object_set(renderer, "text", _(seltype->description), NULL);
    }
    else if (i == COLUMN_SEL_OBJECTS) {
        s = g_strdup_printf("/%d/select/%s", env->id, name);
        sel = GWY_SELECTION(gwy_container_get_object_by_name(env->data, s));
        g_free(s);

        s = g_strdup_printf("%u", gwy_selection_get_data(sel, NULL));
        g_object_set(renderer, "text", s, NULL);
        g_free(s);
    }
}

static GtkTreeSelection*
select_selection(ModuleGUI *gui)
{
    const gchar *selection_name = gwy_params_get_string(gui->args->params, PARAM_SELECTION);
    GArray *selections = gui->args->env->selections;
    GtkTreeSelection *treesel;
    GtkTreeIter iter;
    GQuark quark;
    guint i;

    treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->selections));
    gtk_tree_selection_set_mode(treesel, GTK_SELECTION_BROWSE);
    for (i = 0; i < selections->len; i++) {
        quark = g_array_index(selections, GQuark, i);
        if (gwy_strequal(selection_name, g_quark_to_string(quark))) {
            gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(gui->sel_store), &iter, NULL, i);
            gtk_tree_selection_select_iter(treesel, &iter);
            break;
        }
    }
    if (i == selections->len) {
        g_assert(selections->len == 0);
    }
    return treesel;
}

static void
selection_selected(ModuleGUI *gui,
                   GtkTreeSelection *selection)
{
    ModuleArgs *args = gui->args;
    GArray *selections = args->env->selections;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean changed;
    guint id;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &id, -1);
        changed = gwy_params_set_string(args->params, PARAM_SELECTION,
                                        g_quark_to_string(g_array_index(selections, GQuark, id)));
    }
    else
        changed = gwy_params_set_string(args->params, PARAM_SELECTION, NULL);

    if (changed)
        gwy_param_table_param_changed(gui->table_selection, PARAM_SELECTION);
}

static GtkWidget*
create_selection_list(gpointer user_data)
{
    static const GwyEnum columns[] = {
        { N_("Name"),    COLUMN_SEL_NAME,    },
        { N_("Type"),    COLUMN_SEL_TYPE,    },
        { N_("Objects"), COLUMN_SEL_OBJECTS, },
    };

    ModuleGUI *gui = (ModuleGUI*)user_data;
    GtkTreeViewColumn *column;
    GtkCellRenderer *renderer;
    GtkTreeView *treeview;
    GtkTreeSelection *treesel;
    guint i;

    gui->selections = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->sel_store));
    treeview = GTK_TREE_VIEW(gui->selections);

    for (i = 0; i < G_N_ELEMENTS(columns); i++) {
        column = gtk_tree_view_column_new();
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(columns[i].value));
        gtk_tree_view_column_set_title(column, _(columns[i].name));
        gtk_tree_view_append_column(treeview, column);
        renderer = gtk_cell_renderer_text_new();
        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_set_cell_data_func(column, renderer, sel_render_cell, gui, NULL);
    }

    treesel = select_selection(gui);
    g_signal_connect_swapped(treesel, "changed", G_CALLBACK(selection_selected), gui);

    return gui->selections;
}

static GtkWidget*
selection_tab_new(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParamTable *table;

    table = gui->table_selection = gwy_param_table_new(args->params);
    gwy_dialog_add_param_table(GWY_DIALOG(gui->dialog), table);
    gwy_param_table_append_checkbox(table, PARAM_DRAW_SELECTION);
    gwy_param_table_append_foreign(table, PARAM_SELECTION, create_selection_list, gui, NULL);
    append_color(table, PARAM_SEL_RGB);
    append_color(table, PARAM_SEL_OUTLINE_COLOR);
    append_slider(table, PARAM_SEL_OUTLINE_WIDTH, GWY_SCALE_MAPPING_SQRT, NULL, 2);
    append_slider(table, PARAM_SEL_ALPHA, GWY_SCALE_MAPPING_LINEAR, NULL, -1);
    gwy_param_table_append_header(table, -1, _("Options"));
    gwy_param_table_append_checkbox(table, PARAM_SEL_NUMBER_OBJECTS);
    gwy_param_table_append_slider(table, PARAM_SEL_LINE_THICKNESS);
    gwy_param_table_append_slider(table, PARAM_SEL_POINT_RADIUS);

    return gwy_param_table_widget(table);
}

/* NB: Does not called when the row changes (e.g. preset is renamed), but the selection stays the same. */
static void
preset_selected(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GtkTreeModel *store;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;
    const gchar *name = "";
    gboolean sens = FALSE;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    g_return_if_fail(tselect);
    if (gtk_tree_selection_get_selected(tselect, &store, &iter)) {
        gtk_tree_model_get(store, &iter, 0, &preset, -1);
        name = gwy_resource_get_name(GWY_RESOURCE(preset));
        sens = TRUE;
    }
    gwy_params_set_resource(gui->args->params, PARAM_PRESET, name);
    gtk_entry_set_text(GTK_ENTRY(gui->presetname), name);

    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_LOAD], sens);
    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_DELETE], sens);
    gtk_widget_set_sensitive(gui->preset_buttons[PRESET_BUTTON_RENAME], sens);
}

static void
render_preset_cell(GtkTreeViewColumn *column,
                   GtkCellRenderer *cell,
                   GtkTreeModel *model,
                   GtkTreeIter *iter,
                   G_GNUC_UNUSED gpointer user_data)
{
    guint i = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column), "id"));
    GwyParamResource *preset;
    GwyParams *params;
    const gchar *type = "";
    gchar *s;

    gtk_tree_model_get(model, iter, 0, &preset, -1);
    if (i == COLUMN_PRESET_NAME) {
        g_object_set(cell, "text", gwy_resource_get_name(GWY_RESOURCE(preset)), NULL);
        return;
    }
    params = gwy_param_resource_get_params(preset);
    if (i == COLUMN_PRESET_LATERAL) {
        type = gwy_enum_to_string(gwy_params_get_enum(params, PARAM_XYTYPE),
                                  lateral_types, G_N_ELEMENTS(lateral_types));
    }
    else if (i == COLUMN_PRESET_VALUE) {
        type = gwy_enum_to_string(gwy_params_get_enum(params, PARAM_ZTYPE),
                                  value_types, G_N_ELEMENTS(value_types));
    }
    else if (i == COLUMN_PRESET_TITLE) {
        type = gwy_enum_to_string(gwy_params_get_enum(params, PARAM_TITLE_TYPE),
                                  title_types, G_N_ELEMENTS(title_types));
    }
    else {
        g_return_if_reached();
    }

    s = gwy_strkill(g_strdup(gwy_sgettext(type)), "_:");
    g_object_set(cell, "text", s, NULL);
    g_free(s);
}

static gboolean
preset_validate_name(ModuleGUI *gui,
                     const gchar *name,
                     gboolean show_warning)
{
    gboolean ok;

    ok = (name && *name && !strchr(name, '/') && !strchr(name, '\\'));
    if (show_warning)
        gtk_label_set_text(GTK_LABEL(gui->preset_error), ok ? "" : _("Invalid preset name."));

    return ok;
}

static void
load_preset(ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    GwyParams *params;
    GwyParamResource *preset;
    GtkTreeModel *store;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &store, &iter))
        return;

    gtk_tree_model_get(store, &iter, 0, &preset, -1);
    params = gwy_param_resource_get_params(preset);
    sanitise_params(args, FALSE, TRUE);
    if (gui->mode) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui->mode),
                                     gwy_params_get_enum(params, PARAM_MODE) == IMGEXPORT_MODE_GREY16);
    }
    gwy_param_table_set_from_params(gui->table_basic, params);
    gwy_param_table_set_from_params(gui->table_lateral, params);
    gwy_param_table_set_from_params(gui->table_value, params);
    gwy_param_table_set_from_params(gui->table_selection, params);
}

static void
save_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GwyInventory *inventory;
    const gchar *name;

    name = gtk_entry_get_text(GTK_ENTRY(gui->presetname));
    if (!preset_validate_name(gui, name, TRUE))
        return;

    /* XXX: This part is completely generic, should work for any parameter preset and can be put to some library
     * function. */
    gwy_debug("Now I'm saving `%s'", name);
    inventory = gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type));
    preset = gwy_inventory_get_item(inventory, name);
    if (!preset) {
        gwy_debug("Appending `%s'", name);
        preset = g_object_new(preset_resource_type, "name", name, NULL);
        gwy_params_assign(gwy_param_resource_get_params(preset), gui->args->params);
        gwy_inventory_insert_item(inventory, preset);
        g_object_unref(preset);
    }
    else {
        gwy_debug("Setting `%s'", name);
        gwy_params_assign(gwy_param_resource_get_params(preset), gui->args->params);
    }
    /* FIXME: Handle error? Show it in gui->preset_error? */
    gwy_resource_save(GWY_RESOURCE(preset), NULL);

    gwy_select_in_filtered_inventory_treeeview(GTK_TREE_VIEW(gui->presetlist), name);
    preset_selected(gui);
}

static void
rename_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GwyInventory *inventory;
    GtkTreeModel *model;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;
    const gchar *newname, *oldname;
    gboolean ok = FALSE;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &model, &iter))
        return;

    newname = gtk_entry_get_text(GTK_ENTRY(gui->presetname));
    if (!preset_validate_name(gui, newname, TRUE))
        return;

    gtk_tree_model_get(model, &iter, 0, &preset, -1);

    /* XXX: This part is completely generic, should work for any parameter preset and can be put to some library
     * function. */
    inventory = gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type));
    oldname = gwy_resource_get_name(GWY_RESOURCE(preset));
    if (!gwy_strequal(newname, oldname) && !gwy_inventory_get_item(inventory, newname)) {
        gwy_debug("Now I will rename `%s' to `%s'", oldname, newname);
        ok = gwy_resource_rename(GWY_RESOURCE(preset), newname);
    }

    if (ok) {
        gwy_select_in_filtered_inventory_treeeview(GTK_TREE_VIEW(gui->presetlist), newname);
        preset_selected(gui);
    }
}

static void
delete_preset(ModuleGUI *gui)
{
    GwyParamResource *preset;
    GtkTreeModel *model;
    GtkTreeSelection *tselect;
    GtkTreeIter iter;

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    if (!gtk_tree_selection_get_selected(tselect, &model, &iter))
        return;

    gtk_tree_model_get(model, &iter, 0, &preset, -1);
    gwy_resource_delete(GWY_RESOURCE(preset));
}

static GtkWidget*
preset_tab_new(ModuleGUI *gui)
{
    static const struct {
        const gchar *label;
        GCallback callback;
    }
    buttons[PRESET_NBUTTONS] = {
        { N_("verb|_Load"),  G_CALLBACK(load_preset),   },
        { N_("verb|_Store"), G_CALLBACK(save_preset),   },
        { N_("_Rename"),     G_CALLBACK(rename_preset), },
        { N_("_Delete"),     G_CALLBACK(delete_preset), },
    };

    static const GwyEnum columns[] = {
        { N_("Name"),    COLUMN_PRESET_NAME,    },
        { N_("Lateral"), COLUMN_PRESET_LATERAL, },
        { N_("Value"),   COLUMN_PRESET_VALUE,   },
        { N_("Title"),   COLUMN_PRESET_TITLE,   },
    };

    GwyInventoryStore *store;
    GtkTreeSelection *tselect;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeModel *filtermodel;
    GtkWidget *vbox, *label, *button, *scroll, *hbox;
    guint i;

    vbox = gwy_vbox_new(0);   /* to prevent notebook expanding tables */
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

    store = gwy_inventory_store_new(gwy_resource_class_get_inventory(g_type_class_peek(preset_resource_type)));
    filtermodel = gwy_create_inventory_model_without_default(store);
    gui->presetlist = gtk_tree_view_new_with_model(filtermodel);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(gui->presetlist), TRUE);
    g_object_unref(filtermodel);
    g_object_unref(store);

    for (i = 0; i < G_N_ELEMENTS(columns); i++) {
        renderer = gtk_cell_renderer_text_new();
        if (columns[i].value != COLUMN_PRESET_NAME)
            g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, "ellipsize-set", TRUE, NULL);
        column = gtk_tree_view_column_new_with_attributes(_(columns[i].name), renderer, NULL);
        g_object_set_data(G_OBJECT(column), "id", GUINT_TO_POINTER(columns[i].value));
        gtk_tree_view_column_set_cell_data_func(column, renderer,
                                                render_preset_cell, GUINT_TO_POINTER(columns[i].value), NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(gui->presetlist), column);
    }

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(scroll), gui->presetlist);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    hbox = gtk_hbutton_box_new();
    gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_START);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    for (i = 0; i < G_N_ELEMENTS(buttons); i++) {
        button = gui->preset_buttons[i] = gtk_button_new_with_mnemonic(gwy_sgettext(buttons[i].label));
        gtk_container_add(GTK_CONTAINER(hbox), button);
        g_signal_connect_swapped(button, "clicked", buttons[i].callback, gui);
    }

    hbox = gwy_hbox_new(6);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);

    label = gtk_label_new_with_mnemonic(_("Preset _name:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gui->presetname = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gui->presetname), gwy_params_get_string(gui->args->params, PARAM_PRESET));
    gtk_entry_set_max_length(GTK_ENTRY(gui->presetname), 40);
    gtk_box_pack_start(GTK_BOX(hbox), gui->presetname, FALSE, FALSE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), gui->presetname);

    gui->preset_error = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(gui->preset_error), 0.0, 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), gui->preset_error, FALSE, FALSE, 4);
    set_widget_as_warning_message(gui->preset_error);

    tselect = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->presetlist));
    gtk_tree_selection_set_mode(tselect, GTK_SELECTION_SINGLE);
    g_signal_connect_swapped(tselect, "changed", G_CALLBACK(preset_selected), gui);

    return vbox;
}

static void
mode_changed(ModuleGUI *gui, GtkToggleButton *toggle)
{
    if (gtk_toggle_button_get_active(toggle)) {
        gwy_params_set_enum(gui->args->params, PARAM_MODE, IMGEXPORT_MODE_GREY16);
        gtk_widget_set_sensitive(gui->notebook, FALSE);
    }
    else {
        gwy_params_set_enum(gui->args->params, PARAM_MODE, IMGEXPORT_MODE_PRESENTATION);
        gtk_widget_set_sensitive(gui->notebook, TRUE);
    }
    gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static GwyDialogOutcome
run_gui(ModuleArgs *args)
{
    const ImgExportFormat *format = args->env->format;
    ImgExportMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    GwyDialogOutcome outcome;
    GtkWidget *vbox, *hbox;
    GtkNotebook *notebook;
    GwyDialog *dialog;
    ModuleGUI gui;
    gchar *s, *title;

    gwy_clear(&gui, 1);
    gui.args = args;
    gui.sel_store = gwy_null_store_new(args->env->selections->len);

    s = g_ascii_strup(format->name, -1);
    title = g_strdup_printf(_("Export %s"), s);
    g_free(s);
    gui.dialog = gwy_dialog_new(title);
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GWY_RESPONSE_RESET, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);
    g_free(title);
    /* This is still sanitise_params(), but the font enumeration bit only works in GUI. */
    select_a_real_font(args, gui.dialog);

    if (format->write_grey16) {
        gui.mode = gtk_check_button_new_with_mnemonic(_("Export as 1_6 bit grayscale"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gui.mode), mode == IMGEXPORT_MODE_GREY16);
        gwy_dialog_add_content(dialog, gui.mode, FALSE, FALSE, 0);
        g_signal_connect_swapped(gui.mode, "toggled", G_CALLBACK(mode_changed), &gui);
    }

    hbox = gwy_hbox_new(20);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gwy_dialog_add_content(dialog, hbox, TRUE, TRUE, 0);

    vbox = gwy_vbox_new(8);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    gui.notebook = gtk_notebook_new();
    notebook = GTK_NOTEBOOK(gui.notebook);
    if (mode == IMGEXPORT_MODE_GREY16)
        gtk_widget_set_sensitive(gui.notebook, FALSE);
    gtk_box_pack_start(GTK_BOX(vbox), gui.notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(notebook, basic_tab_new(&gui), gtk_label_new(gwy_sgettext("adjective|Basic")));
    gtk_notebook_append_page(notebook, lateral_tab_new(&gui), gtk_label_new(_("Lateral Scale")));
    gtk_notebook_append_page(notebook, value_tab_new(&gui), gtk_label_new(_("Values")));
    gtk_notebook_append_page(notebook, selection_tab_new(&gui), gtk_label_new(_("Selection")));
    gtk_notebook_append_page(notebook, preset_tab_new(&gui), gtk_label_new(_("Presets")));
    gwy_param_active_page_link_to_notebook(args->params, PARAM_ACTIVE_PAGE, notebook);

    gui.preview = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(hbox), gui.preview, FALSE, FALSE, 0);

    g_signal_connect_swapped(gui.table_basic, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_lateral, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_value, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(gui.table_selection, "param-changed", G_CALLBACK(param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(dialog_response), &gui);
    g_signal_connect_after(dialog, "response", G_CALLBACK(dialog_response_after), &gui);
    gwy_dialog_set_preview_func(dialog, GWY_PREVIEW_IMMEDIATE, preview, &gui, NULL);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.sel_store);

    return outcome;
}

static void
dialog_response(ModuleGUI *gui, gint response)
{
    if (response == RESPONSE_AUTO_LENGTH)
        gwy_params_set_string(gui->args->params, PARAM_INSET_LENGTH, NULL);
}

static void
dialog_response_after(G_GNUC_UNUSED GtkDialog *dialog, gint response, ModuleGUI *gui)
{
    ModuleArgs *args = gui->args;
    ImgExportEnv *env = args->env;
    GwyParams *params = args->params;
    guint i;

    if (response == RESPONSE_RESET) {
        const ImgExportSelectionType *seltype = find_selection_type(args, env->selection_name, NULL);
        gint id;

        gwy_params_set_string(params, PARAM_SELECTION, env->selection_name);
        select_selection(gui);
        if (seltype->has_options) {
            for (i = 0; (id = seltype->has_options[i]); i++) {
                if (id == PARAM_SEL_LINE_THICKNESS)
                    gwy_param_table_set_double(gui->table_selection, id, env->sel_line_thickness);
                else if (id == PARAM_SEL_POINT_RADIUS)
                    gwy_param_table_set_double(gui->table_selection, id, env->sel_point_radius);
            }
        }
    }
}

static gdouble
pxwidth_to_ppi(gdouble pxwidth)
{
    return 25.4/pxwidth;
}

static gdouble
ppi_to_pxwidth(gdouble pxwidth)
{
    return 25.4/pxwidth;
}

static void
param_changed(ModuleGUI *gui, gint id)
{
    ModuleArgs *args = gui->args;
    ImgExportEnv *env = args->env;
    GwyParams *params = args->params;
    ImgExportLateralType xytype = gwy_params_get_enum(params, PARAM_XYTYPE);
    ImgExportValueType ztype = gwy_params_get_enum(params, PARAM_ZTYPE);
    ImgExportTitleType title_type = gwy_params_get_enum(params, PARAM_TITLE_TYPE);
    gboolean is_vector = !!env->format->write_vector, can_transp = env->format->supports_transparency;
    GSList *l;
    guint i;

    /* Basic. */
    if (id < 0 || id == PARAM_TRANSPARENT_BG) {
        gwy_param_table_set_sensitive(gui->table_basic, PARAM_BG_COLOR,
                                      !can_transp || !gwy_params_get_boolean(params, PARAM_TRANSPARENT_BG));
    }
    if (is_vector) {
        if (id < 0 || id == PARAM_PPI || id == PARAM_PXWIDTH || id == PARAM_VECTOR_WIDTH || id == PARAM_VECTOR_HEIGHT) {
            gdouble pxwidth;

            if (id == PARAM_VECTOR_WIDTH)
                pxwidth = gwy_params_get_double(params, PARAM_VECTOR_WIDTH)/env->xres;
            else if (id == PARAM_VECTOR_HEIGHT)
                pxwidth = gwy_params_get_double(params, PARAM_VECTOR_HEIGHT)/env->yres;
            else if (id == PARAM_PPI)
                pxwidth = ppi_to_pxwidth(gwy_params_get_double(params, PARAM_PPI));
            else
                pxwidth = gwy_params_get_double(params, PARAM_PXWIDTH);

            gwy_param_table_set_double(gui->table_basic, PARAM_PXWIDTH, pxwidth);
            gwy_param_table_set_double(gui->table_basic, PARAM_PPI, pxwidth_to_ppi(pxwidth));
            gwy_param_table_set_double(gui->table_basic, PARAM_VECTOR_WIDTH, pxwidth*env->xres);
            gwy_param_table_set_double(gui->table_basic, PARAM_VECTOR_HEIGHT, pxwidth*env->yres);
        }
    }
    else {
        if (id < 0 || id == PARAM_ZOOM || id == PARAM_BITMAP_WIDTH || id == PARAM_BITMAP_HEIGHT) {
            gdouble zoom;

            if (id == PARAM_BITMAP_WIDTH)
                zoom = gwy_params_get_int(params, PARAM_BITMAP_WIDTH)/(gdouble)env->xres;
            else if (id == PARAM_BITMAP_HEIGHT)
                zoom = gwy_params_get_int(params, PARAM_BITMAP_HEIGHT)/(gdouble)env->yres;
            else
                zoom = gwy_params_get_double(params, PARAM_ZOOM);

            gwy_debug("zoom: %g (width %g, height %g)", zoom, zoom*env->xres, zoom*env->yres);
            gwy_param_table_set_double(gui->table_basic, PARAM_ZOOM, zoom);
            gwy_param_table_set_int(gui->table_basic, PARAM_BITMAP_WIDTH, GWY_ROUND(zoom*env->xres));
            gwy_param_table_set_int(gui->table_basic, PARAM_BITMAP_HEIGHT, GWY_ROUND(zoom*env->yres));
        }
    }

    /* Lateral. */
    if (id < 0 || id == PARAM_INSET_LENGTH) {
        const gchar *text = gwy_params_get_string(params, PARAM_INSET_LENGTH);
        if (!inset_length_ok(env->dfield, text))
            gwy_param_table_set_string(gui->table_lateral, PARAM_INSET_LENGTH, scalebar_auto_length(env->dfield, NULL));
    }
    if (id < 0 || id == PARAM_XYTYPE) {
        static const gint inset_ids[] = {
            PARAM_INSET_LENGTH, BUTTON_AUTO_LENGTH, PARAM_INSET_POS, PARAM_INSET_YGAP,
            PARAM_INSET_RGB, PARAM_INSET_OUTLINE_COLOR, PARAM_INSET_OUTLINE_WIDTH, PARAM_INSET_ALPHA,
            PARAM_INSET_DRAW_TICKS, PARAM_INSET_DRAW_LABEL, PARAM_INSET_DRAW_TEXT_ABOVE,
        };
        gboolean insetsens = (xytype == IMGEXPORT_LATERAL_INSET);

        for (i = 0; i < G_N_ELEMENTS(inset_ids); i++)
            gwy_param_table_set_sensitive(gui->table_lateral, inset_ids[i], insetsens);
        for (i = 0; i < G_N_ELEMENTS(gui->inset_pos_label); i++)
            gtk_widget_set_sensitive(gui->inset_pos_label[i], insetsens);
        for (l = gui->inset_pos; l; l = g_slist_next(l))
            gtk_widget_set_sensitive(GTK_WIDGET(l->data), insetsens);
    }
    if (id < 0 || id == PARAM_XYTYPE || id == PARAM_INSET_POS) {
        gboolean insetsens = (xytype == IMGEXPORT_LATERAL_INSET);
        InsetPosType pos = gwy_params_get_enum(params, PARAM_INSET_POS);
        gboolean hgapsens = (pos != INSET_POS_BOTTOM_CENTER && pos != INSET_POS_TOP_CENTER);

        gwy_param_table_set_sensitive(gui->table_lateral, PARAM_INSET_XGAP, insetsens && hgapsens);
    }
    if (id < 0 || id == PARAM_INSET_RGB || id == PARAM_INSET_ALPHA) {
        GwyRGBA rgba = gwy_params_get_color(params, PARAM_INSET_RGB);
        rgba.a = gwy_params_get_double(params, PARAM_INSET_ALPHA);
        gwy_params_set_color(params, PARAM_INSET_COLOR, rgba);
    }

    /* Value. */
    if (id < 0
        || id == PARAM_XYTYPE || id == PARAM_ZTYPE || id == PARAM_TITLE_TYPE
        || id == PARAM_DRAW_MASK || id == PARAM_DRAW_MASKKEY) {
        gboolean masksens = env->mask && gwy_params_get_boolean(params, PARAM_DRAW_MASK);
        gboolean maskkeysens = masksens && gwy_params_get_boolean(params, PARAM_DRAW_MASKKEY);
        gboolean fmsens = (ztype == IMGEXPORT_VALUE_FMSCALE);
        gboolean titlesens = (title_type != IMGEXPORT_TITLE_NONE);
        gboolean framesens = (ztype == IMGEXPORT_VALUE_NONE
                              && (xytype == IMGEXPORT_LATERAL_NONE || xytype == IMGEXPORT_LATERAL_INSET)
                              && !maskkeysens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_FMSCALE_GAP, fmsens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_FMSCALE_PRECISION, fmsens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_KILO_THRESHOLD, fmsens || titlesens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_TITLE_GAP, titlesens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_DRAW_FRAME, framesens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_DRAW_MASK, !!env->mask);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_DRAW_MASKKEY, masksens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_MASK_KEY, maskkeysens);
        gwy_param_table_set_sensitive(gui->table_value, PARAM_MASKKEY_GAP, maskkeysens);
    }
    if (id == PARAM_INTERPOLATION_VECTOR)
        gwy_params_set_enum(params, PARAM_INTERPOLATION, gwy_params_get_enum(params, PARAM_INTERPOLATION_VECTOR));

    /* Selection. */
    if (id < 0 || id == PARAM_DRAW_SELECTION) {
        gboolean selsens = gwy_params_get_boolean(params, PARAM_DRAW_SELECTION);

        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SELECTION, selsens);
        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SEL_RGB, selsens);
        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SEL_OUTLINE_COLOR, selsens);
        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SEL_ALPHA, selsens);
        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SEL_LINE_THICKNESS, selsens);
        gwy_param_table_set_sensitive(gui->table_selection, PARAM_SEL_OUTLINE_WIDTH, selsens);
    }
    if (id < 0 || id == PARAM_SELECTION) {
        const gchar *selection_name = gwy_params_get_string(args->params, PARAM_SELECTION);
        const ImgExportSelectionType *seltype = find_selection_type(args, selection_name, NULL);
        gboolean selsens = gwy_params_get_boolean(params, PARAM_DRAW_SELECTION);

        for (i = 0; i < G_N_ELEMENTS(sel_options_all); i++)
            gwy_param_table_set_sensitive(gui->table_selection, sel_options_all[i], FALSE);
        if (seltype && seltype->has_options) {
            for (i = 0; seltype->has_options[i]; i++)
                gwy_param_table_set_sensitive(gui->table_selection, seltype->has_options[i], selsens);
        }
    }
    if (id < 0 || id == PARAM_SEL_RGB || id == PARAM_SEL_ALPHA) {
        GwyRGBA rgba = gwy_params_get_color(params, PARAM_SEL_RGB);
        rgba.a = gwy_params_get_double(params, PARAM_SEL_ALPHA);
        gwy_params_set_color(params, PARAM_SEL_COLOR, rgba);
    }

    if (id != PARAM_PRESET)
        gwy_dialog_invalidate(GWY_DIALOG(gui->dialog));
}

static void
add_selection(gpointer hkey, gpointer hvalue, gpointer data)
{
    GQuark quark = GPOINTER_TO_UINT(hkey);
    GValue *value = (GValue*)hvalue;
    GArray *selections = (GArray*)data;
    GwySelection *sel = g_value_get_object(value);
    const gchar *s = g_quark_to_string(quark), *typename;
    guint i;

    if (!gwy_selection_get_data(sel, NULL)) {
        gwy_debug("ignoring empty selection %s", s);
        return;
    }

    typename = G_OBJECT_TYPE_NAME(sel);
    for (i = 0; i < G_N_ELEMENTS(known_selections); i++) {
        if (gwy_strequal(typename, known_selections[i].typename)) {
            if (known_selections[i].draw)
                break;
            gwy_debug("we know %s but don't have a drawing func for it", typename);
        }
    }
    if (i == G_N_ELEMENTS(known_selections)) {
        gwy_debug("ignoring unknown selection %s (%s)", s, typename);
        return;
    }
    gwy_debug("found selection %s (%s)", s, typename);

    g_return_if_fail(*s == '/');
    s++;
    while (g_ascii_isdigit(*s))
        s++;
    g_return_if_fail(g_str_has_prefix(s, "/select/"));
    s += strlen("/select/");
    quark = g_quark_from_string(s);
    g_array_append_val(selections, quark);
}

static guint16*
render_image_grey16(GwyDataField *dfield)
{
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    gdouble min, max;
    guint16 *pixels;

    pixels = g_new(guint16, xres*yres);
    gwy_data_field_get_min_max(dfield, &min, &max);
    if (min == max)
        memset(pixels, 0, xres*yres*sizeof(guint16));
    else {
        const gdouble *d = gwy_data_field_get_data_const(dfield);
        gdouble q = 65535.999999/(max - min);
        guint i;

        for (i = 0; i < xres*yres; i++)
            pixels[i] = (guint16)(q*(d[i] - min));
    }

    return pixels;
}

#ifdef HAVE_PNG
static void
add_png_text_chunk_string(png_text *chunk,
                          const gchar *key,
                          const gchar *str,
                          gboolean take)
{
    chunk->compression = PNG_TEXT_COMPRESSION_NONE;
    chunk->key = (char*)key;
    chunk->text = take ? (char*)str : g_strdup(str);
    chunk->text_length = strlen(chunk->text);
}

static void
add_png_text_chunk_float(png_text *chunk,
                         const gchar *key,
                         gdouble value)
{
    gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

    chunk->compression = PNG_TEXT_COMPRESSION_NONE;
    chunk->key = (char*)key;
    g_ascii_dtostr(buffer, sizeof(buffer), value);
    chunk->text = g_strdup(buffer);
    chunk->text_length = strlen(chunk->text);
}

static gboolean
write_image_png16(ModuleArgs *args,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    enum { NCHUNKS = 11 };

    const guchar *title = "Data";

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint16 *pixels;
    png_structp writer;
    png_infop writer_info;
    png_byte **rows = NULL;
    png_text *text_chunks = NULL;
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_SWAP_ENDIAN;
#endif
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_IDENTITY;
#endif
    /* A bit of convoluted typing to get a png_charpp equivalent. */
    gchar param0[G_ASCII_DTOSTR_BUF_SIZE], param1[G_ASCII_DTOSTR_BUF_SIZE];
    gchar *s, *params[2];
    gdouble min, max;
    gboolean ok = FALSE;
    FILE *fh = NULL;
    guint i;

    g_return_val_if_fail(gwy_strequal(name, "png"), FALSE);

    writer = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!writer) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"), "png_create_write_struct");
        return FALSE;
    }

    writer_info = png_create_info_struct(writer);
    if (!writer_info) {
        png_destroy_read_struct(&writer, NULL, NULL);
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"), "png_create_info_struct");
        return FALSE;
    }

    gwy_data_field_get_min_max(dfield, &min, &max);
    s = g_strdup_printf("/%d/data/title", args->env->id);
    gwy_container_gis_string_by_name(args->env->data, s, &title);
    g_free(s);

    /* Create the chunks dynamically because the fields of png_text are variable. */
    text_chunks = g_new0(png_text, NCHUNKS);
    i = 0;
    /* Standard PNG keys */
    add_png_text_chunk_string(text_chunks + i++, "Title", title, FALSE);
    add_png_text_chunk_string(text_chunks + i++, "Software", "Gwyddion", FALSE);
    /* Gwyddion GSF keys */
    gwy_data_field_get_min_max(dfield, &min, &max);
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_XREAL, gwy_data_field_get_xreal(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_YREAL, gwy_data_field_get_yreal(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_XOFFSET, gwy_data_field_get_xoffset(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_YOFFSET, gwy_data_field_get_yoffset(dfield));
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_ZMIN, min);
    add_png_text_chunk_float(text_chunks + i++, GWY_IMGKEY_ZMAX, max);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_XYUNIT, s, TRUE);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_ZUNIT, s, TRUE);
    add_png_text_chunk_string(text_chunks + i++, GWY_IMGKEY_TITLE, title, FALSE);
    g_assert(i == NCHUNKS);

    png_set_text(writer, writer_info, text_chunks, NCHUNKS);

    /* Present the scaling information also as calibration chunks. Unfortunately, they cannot represent it fully – the
     * rejected xCAL and yCAL chunks would be necessary for that. */
    png_set_sCAL(writer, writer_info, PNG_SCALE_METER,  /* Usually... */
                 gwy_data_field_get_xreal(dfield), gwy_data_field_get_yreal(dfield));
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    g_ascii_dtostr(param0, sizeof(param0), min);
    g_ascii_dtostr(param1, sizeof(param1), (max - min)/G_MAXUINT16);
    params[0] = param0;
    params[1] = param1;
    png_set_pCAL(writer, writer_info, "Z", 0, G_MAXUINT16, 0, 2, s, params);
    g_free(s);

    pixels = render_image_grey16(dfield);
    rows = g_new(png_bytep, yres);
    for (i = 0; i < yres; i++)
        rows[i] = (png_bytep)pixels + i*xres*sizeof(guint16);

    if (setjmp(png_jmpbuf(writer))) {
        /* FIXME: Not very helpful.  Thread-unsafe. */
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC, _("libpng error occurred"));
        ok = FALSE;   /* might be clobbered by longjmp otherwise, says gcc. */
        if (fh) {
            fclose(fh);
            fh = NULL;
        }
        goto end;
    }

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        goto end;
    }

    png_init_io(writer, fh);
    png_set_filter(writer, 0, PNG_ALL_FILTERS);
    png_set_compression_level(writer, Z_BEST_COMPRESSION);
    png_set_IHDR(writer, writer_info, xres, yres,
                 16, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    /* XXX */
    png_set_rows(writer, writer_info, rows);
    png_write_png(writer, writer_info, transform_flags, NULL);
    fclose(fh);
    fh = NULL;

    ok = TRUE;

end:
    g_free(rows);
    g_free(pixels);
    png_destroy_write_struct(&writer, &writer_info);
    for (i = 0; i < NCHUNKS; i++)
        g_free(text_chunks[i].text);
    g_free(text_chunks);

    return ok;
}
#endif

/* Expand a word and double-word into LSB-ordered sequence of bytes */
#define W(x) (x)&0xff, (x)>>8
#define Q(x) (x)&0xff, ((x)>>8)&0xff, ((x)>>16)&0xff, (x)>>24

static gboolean
write_image_tiff16(ModuleArgs *args,
                   const gchar *name,
                   const gchar *filename,
                   GError **error)
{
    enum {
        N_ENTRIES = 11,
        ESTART = 4 + 4 + 2,
        HEAD_SIZE = ESTART + 12*N_ENTRIES + 4,  /* head + 0th directory */
        /* offsets of things we have to fill run-time */
        WIDTH_OFFSET = ESTART + 12*0 + 8,
        HEIGHT_OFFSET = ESTART + 12*1 + 8,
        BPS_OFFSET = ESTART + 12*2 + 8,
        ROWS_OFFSET = ESTART + 12*8 + 8,
        BYTES_OFFSET = ESTART + 12*9 + 8,
        BIT_DEPTH = 16,
    };

    static guchar tiff_head[] = {
        0x49, 0x49,   /* magic (LSB) */
        W(42),        /* more magic */
        Q(8),         /* 0th directory offset */
        W(N_ENTRIES), /* number of entries */
        W(GWY_TIFFTAG_IMAGE_WIDTH),       W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_IMAGE_LENGTH),      W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_BITS_PER_SAMPLE),   W(GWY_TIFF_SHORT), Q(1), Q(BIT_DEPTH),
        W(GWY_TIFFTAG_COMPRESSION),       W(GWY_TIFF_SHORT), Q(1), Q(GWY_TIFF_COMPRESSION_NONE),
        W(GWY_TIFFTAG_PHOTOMETRIC),       W(GWY_TIFF_SHORT), Q(1), Q(GWY_TIFF_PHOTOMETRIC_MIN_IS_BLACK),
        W(GWY_TIFFTAG_STRIP_OFFSETS),     W(GWY_TIFF_LONG),  Q(1), Q(HEAD_SIZE),
        W(GWY_TIFFTAG_ORIENTATION),       W(GWY_TIFF_SHORT), Q(1), Q(GWY_TIFF_ORIENTATION_TOPLEFT),
        W(GWY_TIFFTAG_SAMPLES_PER_PIXEL), W(GWY_TIFF_SHORT), Q(1), Q(1),
        W(GWY_TIFFTAG_ROWS_PER_STRIP),    W(GWY_TIFF_SHORT), Q(1), Q(0),
        W(GWY_TIFFTAG_STRIP_BYTE_COUNTS), W(GWY_TIFF_LONG),  Q(1), Q(0),
        W(GWY_TIFFTAG_PLANAR_CONFIG),     W(GWY_TIFF_SHORT), Q(1), Q(GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS),
        Q(0),              /* next directory (0 = none) */
        /* here the image data start */
    };

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint nbytes = BIT_DEPTH*xres*yres;
    guint16 *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tiff"), FALSE);

    *(guint32*)(tiff_head + WIDTH_OFFSET) = GUINT32_TO_LE(xres);
    *(guint32*)(tiff_head + HEIGHT_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + ROWS_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + BYTES_OFFSET) = GUINT32_TO_LE(nbytes);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(tiff_head, 1, sizeof(tiff_head), fh) != sizeof(tiff_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    pixels = render_image_grey16(dfield);
    if (fwrite(pixels, sizeof(guint16), xres*yres, fh) != xres*yres) {
        err_WRITE(error);
        fclose(fh);
        g_free(pixels);
        return FALSE;
    }

    fclose(fh);
    g_free(pixels);

    return TRUE;
}

#undef Q
#undef W

static void
add_ppm_comment_string(GString *str,
                       const gchar *key,
                       const gchar *value,
                       gboolean take)
{
    g_string_append_printf(str, "# %s %s\n", key, value);
    if (take)
        g_free((gpointer)value);
}

static void
add_ppm_comment_float(GString *str,
                      const gchar *key,
                      gdouble value)
{
    gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_dtostr(buffer, sizeof(buffer), value);
    g_string_append_printf(str, "# %s %s\n", key, buffer);
}

static gboolean
write_image_pgm16(ModuleArgs *args,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    static const gchar pgm_header[] = "P5\n%s%u\n%u\n65535\n";
    const guchar *title = "Data";

    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint i;
    gdouble min, max;
    gboolean ok = FALSE;
    gchar *s, *ppmh = NULL;
    GString *str;
    guint16 *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "pnm"), FALSE);

    pixels = render_image_grey16(dfield);
    gwy_data_field_get_min_max(dfield, &min, &max);

    s = g_strdup_printf("/%d/data/title", args->env->id);
    gwy_container_gis_string_by_name(args->env->data, s, &title);
    g_free(s);

    /* Gwyddion GSF keys */
    str = g_string_new(NULL);
    add_ppm_comment_float(str, GWY_IMGKEY_XREAL, gwy_data_field_get_xreal(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_YREAL, gwy_data_field_get_yreal(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_XOFFSET, gwy_data_field_get_xoffset(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_YOFFSET, gwy_data_field_get_yoffset(dfield));
    add_ppm_comment_float(str, GWY_IMGKEY_ZMIN, min);
    add_ppm_comment_float(str, GWY_IMGKEY_ZMAX, max);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    add_ppm_comment_string(str, GWY_IMGKEY_XYUNIT, s, TRUE);
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(dfield), GWY_SI_UNIT_FORMAT_PLAIN);
    add_ppm_comment_string(str, GWY_IMGKEY_ZUNIT, s, TRUE);
    add_ppm_comment_string(str, GWY_IMGKEY_TITLE, title, FALSE);

    ppmh = g_strdup_printf(pgm_header, str->str, xres, yres);
    g_string_free(str, TRUE);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(ppmh, 1, strlen(ppmh), fh) != strlen(ppmh)) {
        err_WRITE(error);
        goto end;
    }

    if (G_BYTE_ORDER != G_BIG_ENDIAN) {
        for (i = 0; i < xres*yres; i++)
            pixels[i] = GUINT16_TO_BE(pixels[i]);
    }

    if (fwrite(pixels, sizeof(guint16), xres*yres, fh) != xres*yres) {
        err_WRITE(error);
        goto end;
    }
    ok = TRUE;

end:
    g_free(pixels);
    g_free(ppmh);
    fclose(fh);

    return ok;
}

static gboolean
write_vector_generic(ModuleArgs *args,
                     const gchar *name,
                     const gchar *filename,
                     GError **error)
{
    gdouble pxwidth = gwy_params_get_double(args->params, PARAM_PXWIDTH);
    gdouble zoom = gwy_params_get_double(args->params, PARAM_ZOOM);
    ImgExportSizes *sizes;
    cairo_surface_t *surface;
    cairo_status_t status;
    gboolean ok = TRUE;
    cairo_t *cr;

    gwy_debug("requested width %g mm", pxwidth*args->env->xres);
    gwy_params_set_double(args->params, PARAM_ZOOM, mm2pt*pxwidth);
    gwy_debug("must set zoom to %g", gwy_params_get_double(args->params, PARAM_ZOOM));
    sizes = calculate_sizes(args, name);
    g_return_val_if_fail(sizes, FALSE);
    gwy_debug("image width %g, canvas width %g", sizes->image.w/mm2pt, sizes->canvas.w/mm2pt);
    surface = create_surface(name, filename, sizes->canvas.w, sizes->canvas.h, TRUE);
    g_return_val_if_fail(surface, FALSE);
    cr = cairo_create(surface);
    image_draw_cairo(args, sizes, cr);
    cairo_surface_flush(surface);
    if ((status = cairo_status(cr)) || (status = cairo_surface_status(surface))) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("Cairo error occurred: %s"), cairo_status_to_string(status));
        ok = FALSE;
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    destroy_sizes(sizes);
    gwy_params_set_double(args->params, PARAM_ZOOM, zoom);

    return ok;
}

static gboolean
write_pixbuf_generic(GdkPixbuf *pixbuf,
                     const gchar *name,
                     const gchar *filename,
                     GError **error)
{
    GError *err = NULL;

    if (gdk_pixbuf_save(pixbuf, filename, name, &err, NULL))
        return TRUE;

    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                _("Pixbuf save failed: %s."), err->message);
    g_clear_error(&err);
    return FALSE;
}

/* Expand a word and double-word into LSB-ordered sequence of bytes */
#define W(x) (x)&0xff, (x)>>8
#define Q(x) (x)&0xff, ((x)>>8)&0xff, ((x)>>16)&0xff, (x)>>24

static gboolean
write_pixbuf_tiff(GdkPixbuf *pixbuf,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    enum {
        N_ENTRIES = 14,
        ESTART = 4 + 4 + 2,
        HEAD_SIZE = ESTART + 12*N_ENTRIES + 4,  /* head + 0th directory */
        /* offsets of things we have to fill run-time */
        WIDTH_OFFSET = ESTART + 12*0 + 8,
        HEIGHT_OFFSET = ESTART + 12*1 + 8,
        ROWS_OFFSET = ESTART + 12*8 + 8,
        BYTES_OFFSET = ESTART + 12*9 + 8,
        BIT_DEPTH = 8,
        NCHANNELS = 3,
    };

    static guchar tiff_head[] = {
        0x49, 0x49,   /* magic (LSB) */
        W(42),        /* more magic */
        Q(8),         /* 0th directory offset */
        W(N_ENTRIES), /* number of entries */
        W(GWY_TIFFTAG_IMAGE_WIDTH),       W(GWY_TIFF_SHORT),    Q(1), Q(0),
        W(GWY_TIFFTAG_IMAGE_LENGTH),      W(GWY_TIFF_SHORT),    Q(1), Q(0),
        W(GWY_TIFFTAG_BITS_PER_SAMPLE),   W(GWY_TIFF_SHORT),    Q(3), Q(HEAD_SIZE),
        W(GWY_TIFFTAG_COMPRESSION),       W(GWY_TIFF_SHORT),    Q(1), Q(GWY_TIFF_COMPRESSION_NONE),
        W(GWY_TIFFTAG_PHOTOMETRIC),       W(GWY_TIFF_SHORT),    Q(1), Q(GWY_TIFF_PHOTOMETRIC_RGB),
        W(GWY_TIFFTAG_STRIP_OFFSETS),     W(GWY_TIFF_LONG),     Q(1), Q(HEAD_SIZE + 22),
        W(GWY_TIFFTAG_ORIENTATION),       W(GWY_TIFF_SHORT),    Q(1), Q(GWY_TIFF_ORIENTATION_TOPLEFT),
        W(GWY_TIFFTAG_SAMPLES_PER_PIXEL), W(GWY_TIFF_SHORT),    Q(1), Q(NCHANNELS),
        W(GWY_TIFFTAG_ROWS_PER_STRIP),    W(GWY_TIFF_SHORT),    Q(1), Q(0),
        W(GWY_TIFFTAG_STRIP_BYTE_COUNTS), W(GWY_TIFF_LONG),     Q(1), Q(0),
        W(GWY_TIFFTAG_X_RESOLUTION),      W(GWY_TIFF_RATIONAL), Q(1), Q(HEAD_SIZE + 6),
        W(GWY_TIFFTAG_Y_RESOLUTION),      W(GWY_TIFF_RATIONAL), Q(1), Q(HEAD_SIZE + 14),
        W(GWY_TIFFTAG_PLANAR_CONFIG),     W(GWY_TIFF_SHORT),    Q(1), Q(GWY_TIFF_PLANAR_CONFIG_CONTIGNUOUS),
        W(GWY_TIFFTAG_RESOLUTION_UNIT),   W(GWY_TIFF_SHORT),    Q(1), Q(GWY_TIFF_RESOLUTION_UNIT_INCH),
        Q(0),              /* next directory (0 = none) */
        /* header data */
        W(BIT_DEPTH), W(BIT_DEPTH), W(BIT_DEPTH),
        Q(72), Q(1),       /* x-resolution */
        Q(72), Q(1),       /* y-resolution */
        /* here the image data start */
    };

    guint xres, yres, rowstride, i, nbytes, nchannels;
    guchar *pixels;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tiff"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    nbytes = xres*yres*NCHANNELS;

    *(guint32*)(tiff_head + WIDTH_OFFSET) = GUINT32_TO_LE(xres);
    *(guint32*)(tiff_head + HEIGHT_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + ROWS_OFFSET) = GUINT32_TO_LE(yres);
    *(guint32*)(tiff_head + BYTES_OFFSET) = GUINT32_TO_LE(nbytes);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(tiff_head, 1, sizeof(tiff_head), fh) != sizeof(tiff_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    for (i = 0; i < yres; i++) {
        if (fwrite(pixels + i*rowstride, NCHANNELS, xres, fh) != xres) {
            err_WRITE(error);
            fclose(fh);
            return FALSE;
        }
    }

    fclose(fh);
    return TRUE;
}

#undef Q
#undef W

static gboolean
write_pixbuf_ppm(GdkPixbuf *pixbuf,
                 const gchar *name,
                 const gchar *filename,
                 GError **error)
{
    static const gchar ppm_header[] = "P6\n%u\n%u\n255\n";

    guint xres, yres, rowstride, nchannels, i;
    guchar *pixels;
    gboolean ok = FALSE;
    gchar *ppmh = NULL;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "pnm"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    ppmh = g_strdup_printf(ppm_header, xres, yres);
    if (fwrite(ppmh, 1, strlen(ppmh), fh) != strlen(ppmh)) {
        err_WRITE(error);
        goto end;
    }

    for (i = 0; i < yres; i++) {
        if (fwrite(pixels + i*rowstride, nchannels, xres, fh) != xres) {
            err_WRITE(error);
            goto end;
        }
    }

    ok = TRUE;

end:
    fclose(fh);
    g_free(ppmh);
    return ok;
}

static gboolean
write_pixbuf_bmp(GdkPixbuf *pixbuf,
                 const gchar *name,
                 const gchar *filename,
                 GError **error)
{
    static guchar bmp_head[] = {
        'B', 'M',    /* magic */
        0, 0, 0, 0,  /* file size */
        0, 0, 0, 0,  /* reserved */
        54, 0, 0, 0, /* offset */
        40, 0, 0, 0, /* header size */
        0, 0, 0, 0,  /* width */
        0, 0, 0, 0,  /* height */
        1, 0,        /* bit planes */
        24, 0,       /* bpp */
        0, 0, 0, 0,  /* compression type */
        0, 0, 0, 0,  /* (compressed) image size */
        0, 0, 0, 0,  /* x resolution */
        0, 0, 0, 0,  /* y resolution */
        0, 0, 0, 0,  /* ncl */
        0, 0, 0, 0,  /* nic */
    };

    guchar *pixels, *buffer = NULL;
    guint i, j, xres, yres, nchannels, rowstride, bmplen, bmprowstride;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "bmp"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    bmprowstride = ((nchannels*xres + 3)/4)*4;
    bmplen = yres*bmprowstride + sizeof(bmp_head);

    *(guint32*)(bmp_head + 2) = GUINT32_TO_LE(bmplen);
    *(guint32*)(bmp_head + 18) = GUINT32_TO_LE(xres);
    *(guint32*)(bmp_head + 22) = GUINT32_TO_LE(yres);
    *(guint32*)(bmp_head + 34) = GUINT32_TO_LE(yres*bmprowstride);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(bmp_head, 1, sizeof(bmp_head), fh) != sizeof(bmp_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    /* The ugly part: BMP uses BGR instead of RGB and is written upside down, this silliness may originate nowhere
     * else than in MS... */
    buffer = g_new(guchar, bmprowstride);
    memset(buffer, 0xff, sizeof(bmprowstride));
    for (i = 0; i < yres; i++) {
        const guchar *p = pixels + (yres-1 - i)*rowstride;
        guchar *q = buffer;

        for (j = xres; j; j--, p += 3, q += 3) {
            *q = *(p + 2);
            *(q + 1) = *(p + 1);
            *(q + 2) = *p;
        }
        if (fwrite(buffer, 1, bmprowstride, fh) != bmprowstride) {
            err_WRITE(error);
            fclose(fh);
            g_free(buffer);
            return FALSE;
        }
    }
    g_free(buffer);
    fclose(fh);

    return TRUE;
}

static gboolean
write_pixbuf_targa(GdkPixbuf *pixbuf,
                   const gchar *name,
                   const gchar *filename,
                   GError **error)
{
   static guchar targa_head[] = {
     0,           /* idlength */
     0,           /* colourmaptype */
     2,           /* datatypecode: uncompressed RGB */
     0, 0, 0, 0,  /* colourmaporigin, colourmaplength */
     0,           /* colourmapdepth */
     0, 0, 0, 0,  /* x-origin, y-origin */
     0, 0,        /* width */
     0, 0,        /* height */
     24,          /* bits per pixel */
     0x20,        /* image descriptor flags: origin upper */
    };

    guchar *pixels, *buffer = NULL;
    guint nchannels, xres, yres, rowstride, i, j;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "tga"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    if (xres >= 65535 || yres >= 65535) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                    _("Image is too large to be stored as TARGA."));
        return FALSE;
    }

    *(guint16*)(targa_head + 12) = GUINT16_TO_LE((guint16)xres);
    *(guint16*)(targa_head + 14) = GUINT16_TO_LE((guint16)yres);

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        return FALSE;
    }

    if (fwrite(targa_head, 1, sizeof(targa_head), fh) != sizeof(targa_head)) {
        err_WRITE(error);
        fclose(fh);
        return FALSE;
    }

    /* The ugly part: TARGA uses BGR instead of RGB */
    buffer = g_new(guchar, nchannels*xres);
    memset(buffer, 0xff, nchannels*xres);
    for (i = 0; i < yres; i++) {
        const guchar *p = pixels + i*rowstride;
        guchar *q = buffer;

        for (j = xres; j; j--, p += 3, q += 3) {
            *q = *(p + 2);
            *(q + 1) = *(p + 1);
            *(q + 2) = *p;
        }
        if (fwrite(buffer, nchannels, xres, fh) != xres) {
            err_WRITE(error);
            fclose(fh);
            g_free(buffer);
            return FALSE;
        }
    }
    fclose(fh);
    g_free(buffer);

    return TRUE;
}

#ifdef HAVE_WEBP
static gboolean
write_pixbuf_webp(GdkPixbuf *pixbuf,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    const guchar *pixels;
    guchar *buffer = NULL;
    guint xres, yres, nchannels, rowstride;
    size_t size;
    gboolean ok;
    FILE *fh;

    g_return_val_if_fail(gwy_strequal(name, "webp"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3 || nchannels == 4, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    if (nchannels == 3)
        size = WebPEncodeLosslessRGB(pixels, xres, yres, rowstride, &buffer);
    else if (nchannels == 4)
        size = WebPEncodeLosslessRGBA(pixels, xres, yres, rowstride, &buffer);
    else {
        g_assert_not_reached();
    }

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        free(buffer);
        return FALSE;
    }

    ok = (fwrite(buffer, 1, size, fh) == size);
    if (!ok)
        err_WRITE(error);

    fclose(fh);
    /* XXX: Version at least 0.5.0 needed for WebPFree(buffer); */
    free(buffer);

    return ok;
}
#endif

#ifdef HAVE_JXL
static void
err_JXL(GError **error, const gchar *function_name)
{
    g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                _("libjxl error occurred (%s)"), function_name);
}

static gboolean
encode_jxl_to_file(const gchar *filename, JxlEncoder *encoder, GError **error)
{
    JxlEncoderStatus process_result;
    size_t avail_out, offset;
    guchar *next_out;
    GByteArray *out = NULL;
    gboolean ok = FALSE;
    FILE *fh;

    out = g_byte_array_new();
    g_byte_array_set_size(out, 4096);
    next_out = out->data;
    avail_out = out->len;
    while ((process_result = JxlEncoderProcessOutput(encoder, &next_out, &avail_out))) {
        if (process_result == JXL_ENC_NEED_MORE_OUTPUT) {
            offset = next_out - out->data;
            g_byte_array_set_size(out, 2*out->len);
            next_out = out->data + offset;
            avail_out = out->len - offset;
        }
    }
    g_byte_array_set_size(out, next_out - out->data);
    if (JXL_ENC_SUCCESS != process_result) {
        err_JXL(error, "JxlEncoderProcessOutput");
        goto end;
    }

    if (!(fh = gwy_fopen(filename, "wb"))) {
        err_OPEN_WRITE(error);
        goto end;
    }

    ok = (fwrite(out->data, 1, out->len, fh) == out->len);
    if (!ok)
        err_WRITE(error);
    fclose(fh);

end:
    g_byte_array_free(out, TRUE);
    return ok;
}

static gboolean
write_pixbuf_jxl(GdkPixbuf *pixbuf,
                 const gchar *name,
                 const gchar *filename,
                 GError **error)
{
    const guchar *pixels, *img;
    guchar *compactified = NULL;
    guint i, xres, yres, nchannels, rowstride;
    JxlPixelFormat pixel_format = { 3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    JxlColorEncoding color_encoding;
#ifdef JXL_VERSION_IS_AT_LEAST_0_7_BUT_IT_DOES_NOT_HAVE_ANY_PREPROCESSOR_LEVEL_VERSION_CHECK
    JxlEncoderFrameOptions *frame_settings;
#else
    JxlEncoderOptions *frame_settings;
#endif
    JxlBasicInfo basic_info;
    JxlEncoder *encoder;
    gboolean ok = FALSE;

    g_return_val_if_fail(gwy_strequal(name, "jxl"), FALSE);

    nchannels = gdk_pixbuf_get_n_channels(pixbuf);
    g_return_val_if_fail(nchannels == 3 || nchannels == 4, FALSE);

    xres = gdk_pixbuf_get_width(pixbuf);
    yres = gdk_pixbuf_get_height(pixbuf);
    img = pixels = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    encoder = JxlEncoderCreate(NULL);
    if (JxlEncoderSetParallelRunner(encoder, NULL, NULL) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetParallelRunner");
        goto end;
    }

    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = xres;
    basic_info.ysize = yres;
    basic_info.bits_per_sample = 8;
    basic_info.num_color_channels = 3;
    basic_info.num_extra_channels = (nchannels == 4 ? 1 : 0);
    basic_info.alpha_bits = (nchannels == 4 ? 8 : 0);
    basic_info.alpha_premultiplied = JXL_TRUE;
    basic_info.uses_original_profile = JXL_FALSE;
    if (JxlEncoderSetBasicInfo(encoder, &basic_info) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetBasicInfo");
        goto end;
    }

    JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
    if (JxlEncoderSetColorEncoding(encoder, &color_encoding) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetColorEncoding");
        goto end;
    }

    if (rowstride != xres*nchannels) {
        img = compactified = g_new(guchar, nchannels*xres*yres);
        for (i = 0; i < yres; i++)
            gwy_assign(compactified + nchannels*xres*i, pixels + rowstride*i, nchannels*xres);
    }

#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0,7,0)
    frame_settings = JxlEncoderOptionsCreate(encoder, NULL);
#else
    frame_settings = JxlEncoderFrameSettingsCreate(encoder, NULL);
#endif
    pixel_format.num_channels = nchannels;
    if (JxlEncoderAddImageFrame(frame_settings, &pixel_format, img, nchannels*xres*yres) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderAddImageFrame");
        GWY_FREE(compactified);
        goto end;
    }
    JxlEncoderCloseInput(encoder);
    GWY_FREE(compactified);

    ok = encode_jxl_to_file(filename, encoder, error);

end:
    GWY_FREE(compactified);
    JxlEncoderDestroy(encoder);

    return ok;
}

static gboolean
write_image_jxl16(ModuleArgs *args,
                  const gchar *name,
                  const gchar *filename,
                  GError **error)
{
    GwyDataField *dfield = args->env->dfield;
    guint xres = gwy_data_field_get_xres(dfield);
    guint yres = gwy_data_field_get_yres(dfield);
    guint16 *pixels;
    JxlPixelFormat pixel_format = { 1, JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0 };
    JxlColorEncoding color_encoding;
#ifdef JXL_VERSION_IS_AT_LEAST_0_7_BUT_IT_DOES_NOT_HAVE_ANY_PREPROCESSOR_LEVEL_VERSION_CHECK
    JxlEncoderFrameOptions *frame_settings;
#else
    JxlEncoderOptions *frame_settings;
#endif
    JxlBasicInfo basic_info;
    JxlEncoder *encoder;
    gboolean ok = FALSE;

    g_return_val_if_fail(gwy_strequal(name, "jxl"), FALSE);

    pixels = render_image_grey16(dfield);

    encoder = JxlEncoderCreate(NULL);
    if (JxlEncoderSetParallelRunner(encoder, NULL, NULL) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetParallelRunner");
        goto end;
    }

    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = xres;
    basic_info.ysize = yres;
    basic_info.bits_per_sample = 16;
    basic_info.num_color_channels = 1;
    basic_info.uses_original_profile = JXL_FALSE;
    if (JxlEncoderSetBasicInfo(encoder, &basic_info) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetBasicInfo");
        goto end;
    }

    JxlColorEncodingSetToSRGB(&color_encoding, JXL_TRUE);
    if (JxlEncoderSetColorEncoding(encoder, &color_encoding) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderSetColorEncoding");
        goto end;
    }

#if JPEGXL_NUMERIC_VERSION < JPEGXL_COMPUTE_NUMERIC_VERSION(0,7,0)
    frame_settings = JxlEncoderOptionsCreate(encoder, NULL);
#else
    frame_settings = JxlEncoderFrameSettingsCreate(encoder, NULL);
#endif
    pixel_format.num_channels = 1;
    if (JxlEncoderAddImageFrame(frame_settings, &pixel_format, pixels, xres*yres*sizeof(guint16)) != JXL_ENC_SUCCESS) {
        err_JXL(error, "JxlEncoderAddImageFrame");
        goto end;
    }
    JxlEncoderCloseInput(encoder);

    ok = encode_jxl_to_file(filename, encoder, error);

end:
    g_free(pixels);
    JxlEncoderDestroy(encoder);

    return ok;
}
#endif

/* Borrowed from libgwyui4 */
static void
draw_ellipse(cairo_t *cr, gdouble x, gdouble y, gdouble xr, gdouble yr)
{
    const gdouble q = 0.552;

    cairo_move_to(cr, x + xr, y);
    cairo_curve_to(cr, x + xr, y + q*yr, x + q*xr, y + yr, x, y + yr);
    cairo_curve_to(cr, x - q*xr, y + yr, x - xr, y + q*yr, x - xr, y);
    cairo_curve_to(cr, x - xr, y - q*yr, x - q*xr, y - yr, x, y - yr);
    cairo_curve_to(cr, x + q*xr, y - yr, x + xr, y - q*yr, x + xr, y);
    cairo_close_path(cr);
}

static void
draw_sel_axis(ModuleArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              G_GNUC_UNUSED PangoLayout *layout,
              G_GNUC_UNUSED GString *s,
              cairo_t *cr)
{
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble p, xy[1];
    GwyOrientation orientation;
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n, i;

    g_object_get(sel, "orientation", &orientation, NULL);
    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            if (orientation == GWY_ORIENTATION_HORIZONTAL) {
                p = qy*xy[0];
                draw_line_outline(cr, 0.0, p, w, p, &outcolour, lw, olw);
            }
            else {
                p = qx*xy[0];
                draw_line_outline(cr, p, 0.0, p, h, &outcolour, lw, olw);
            }
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            if (orientation == GWY_ORIENTATION_HORIZONTAL) {
                p = qy*xy[0];
                cairo_move_to(cr, 0.0, p);
                cairo_line_to(cr, w, p);
            }
            else {
                p = qx*xy[0];
                cairo_move_to(cr, p, 0.0);
                cairo_line_to(cr, p, h);
            }
            cairo_stroke(cr);
        }
    }
}

static void
draw_sel_cross(ModuleArgs *args,
               const ImgExportSizes *sizes,
               GwySelection *sel,
               gdouble qx, gdouble qy,
               G_GNUC_UNUSED PangoLayout *layout,
               G_GNUC_UNUSED GString *s,
               cairo_t *cr)
{
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble p, xy[2];
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            p = qy*xy[1];
            draw_line_outline(cr, 0.0, p, w, p, &outcolour, lw, olw);
            p = qx*xy[0];
            draw_line_outline(cr, p, 0.0, p, h, &outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            p = qy*xy[1];
            cairo_move_to(cr, 0.0, p);
            cairo_line_to(cr, w, p);
            cairo_stroke(cr);

            p = qx*xy[0];
            cairo_move_to(cr, p, 0.0);
            cairo_line_to(cr, p, h);
            cairo_stroke(cr);
        }
    }
}

static void
draw_sel_ellipse(ModuleArgs *args,
                 const ImgExportSizes *sizes,
                 GwySelection *sel,
                 gdouble qx, gdouble qy,
                 G_GNUC_UNUSED PangoLayout *layout,
                 G_GNUC_UNUSED GString *s,
                 cairo_t *cr)
{
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble xf, yf, xt, yt, xy[4];
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            draw_ellipse(cr, 0.5*(xf + xt), 0.5*(yf + yt), 0.5*(xt - xf), 0.5*(yt - yf));
            stroke_path_outline(cr, &outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            draw_ellipse(cr, 0.5*(xf + xt), 0.5*(yf + yt), 0.5*(xt - xf), 0.5*(yt - yf));
            stroke_path(cr, &colour, lw);
        }
    }
}

static void
draw_sel_line(ModuleArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              PangoLayout *layout,
              GString *s,
              cairo_t *cr)
{
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lt = gwy_params_get_double(args->params, PARAM_SEL_LINE_THICKNESS);
    gboolean number_objects = gwy_params_get_boolean(args->params, PARAM_SEL_NUMBER_OBJECTS);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble px, py, xf, yf, xt, yt, xy[4];
    guint n, i;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];

            draw_line_outline(cr, xf, yf, xt, yt, &outcolour, lw, olw);
            if (lt > 0.0) {
                gdouble xd = yt - yf, yd = xf - xt;
                gdouble len = sqrt(xd*xd + yd*yd);
                xd *= lt*px/len;
                yd *= lt*py/len;

                draw_line_outline(cr, xf - 0.5*xd, yf - 0.5*yd, xf + 0.5*xd, yf + 0.5*yd, &outcolour, lw, olw);
                draw_line_outline(cr, xt - 0.5*xd, yt - 0.5*yd, xt + 0.5*xd, yt + 0.5*yd, &outcolour, lw, olw);
            }

            if (number_objects) {
                PangoRectangle logical;
                gdouble xc = 0.5*(xf + xt), yc = 0.5*(yf + yt);
                gdouble xd = yt - yf, yd = xf - xt;
                gdouble len = sqrt(xd*xd + yd*yd);

                if (yd < -1e-14) {
                    xd = -xd;
                    yd = -yd;
                }
                xd /= len;
                yd /= len;
                format_layout(layout, &logical, s, "%u", i+1);
                xc -= 0.5*logical.width/pangoscale;
                yc -= 0.5*logical.height/pangoscale;
                xd *= (0.5*lw + 0.45*logical.height/pangoscale);
                yd *= (0.5*lw + 0.45*logical.height/pangoscale);
                cairo_save(cr);
                cairo_move_to(cr, xc + xd, yc + yd);
                draw_text_outline(cr, layout, &outcolour, olw);
                cairo_restore(cr);
            }
        }
    }
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        xf = qx*xy[0];
        yf = qy*xy[1];
        xt = qx*xy[2];
        yt = qy*xy[3];

        cairo_move_to(cr, xf, yf);
        cairo_line_to(cr, xt, yt);

        gwy_debug("sel_line_thickness %g", lt);
        if (lt > 0.0) {
            gdouble xd = yt - yf, yd = xf - xt;
            gdouble len = sqrt(xd*xd + yd*yd);
            xd *= lt*px/len;
            yd *= lt*py/len;

            cairo_move_to(cr, xf - 0.5*xd, yf - 0.5*yd);
            cairo_rel_line_to(cr, xd, yd);
            cairo_move_to(cr, xt - 0.5*xd, yt - 0.5*yd);
            cairo_rel_line_to(cr, xd, yd);
        }

        set_cairo_source_rgb(cr, &colour);
        cairo_stroke(cr);

        if (number_objects) {
            PangoRectangle logical;
            gdouble xc = 0.5*(xf + xt), yc = 0.5*(yf + yt);
            gdouble xd = yt - yf, yd = xf - xt;
            gdouble len = sqrt(xd*xd + yd*yd);

            if (yd < -1e-14) {
                xd = -xd;
                yd = -yd;
            }
            xd /= len;
            yd /= len;
            format_layout(layout, &logical, s, "%u", i+1);
            xc -= 0.5*logical.width/pangoscale;
            yc -= 0.5*logical.height/pangoscale;
            xd *= (0.5*lw + 0.45*logical.height/pangoscale);
            yd *= (0.5*lw + 0.45*logical.height/pangoscale);
            cairo_save(cr);
            cairo_move_to(cr, xc + xd, yc + yd);
            draw_text(cr, layout, &colour);
            cairo_restore(cr);
        }
    }
}

static void
draw_sel_point(ModuleArgs *args,
               const ImgExportSizes *sizes,
               GwySelection *sel,
               gdouble qx, gdouble qy,
               PangoLayout *layout,
               GString *s,
               cairo_t *cr)
{
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble pr = gwy_params_get_double(args->params, PARAM_SEL_POINT_RADIUS);
    gboolean number_objects = gwy_params_get_boolean(args->params, PARAM_SEL_NUMBER_OBJECTS);
    gdouble tl = G_SQRT2*sizes->sizes.tick_length;
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble px, py, x, y, xy[2];
    guint n, i;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            x = qx*xy[0];
            y = qy*xy[1];
            draw_line_outline(cr, x - 0.5*tl, y, x + 0.5*tl, y, &outcolour, lw, olw);
            draw_line_outline(cr, x, y - 0.5*tl, x, y + 0.5*tl, &outcolour, lw, olw);

            cairo_save(cr);
            if (pr > 0.0) {
                gdouble xr = pr*px, yr = pr*py;
                draw_ellipse(cr, x, y, xr, yr);
                stroke_path_outline(cr, &outcolour, lw, olw);
            }

            if (number_objects) {
                PangoRectangle logical;

                format_layout(layout, &logical, s, "%u", i+1);
                cairo_move_to(cr, x + lw + 0.05*logical.height/pangoscale, y + lw + 0.05*logical.height/pangoscale);
                draw_text_outline(cr, layout, &outcolour, olw);
            }
            cairo_restore(cr);
        }
    }
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        x = qx*xy[0];
        y = qy*xy[1];
        cairo_move_to(cr, x - 0.5*tl, y);
        cairo_rel_line_to(cr, tl, 0.0);
        cairo_move_to(cr, x, y - 0.5*tl);
        cairo_rel_line_to(cr, 0.0, tl);
        cairo_stroke(cr);

        cairo_save(cr);
        if (pr > 0.0) {
            gdouble xr = pr*px, yr = pr*py;
            draw_ellipse(cr, x, y, xr, yr);
            stroke_path(cr, &colour, lw);
        }

        if (number_objects) {
            PangoRectangle logical;

            format_layout(layout, &logical, s, "%u", i+1);
            cairo_move_to(cr, x + lw + 0.05*logical.height/pangoscale, y + lw + 0.05*logical.height/pangoscale);
            draw_text(cr, layout, &colour);
        }
        cairo_restore(cr);
    }
}

static void
draw_sel_rectangle(ModuleArgs *args,
                   const ImgExportSizes *sizes,
                   GwySelection *sel,
                   gdouble qx, gdouble qy,
                   G_GNUC_UNUSED PangoLayout *layout,
                   G_GNUC_UNUSED GString *s,
                   cairo_t *cr)
{
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble xf, yf, xt, yt, xy[4];
    guint n, i;

    n = gwy_selection_get_data(sel, NULL);
    if (olw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            cairo_rectangle(cr, xf, yf, xt - xf, yt - yf);
            stroke_path_outline(cr, &outcolour, lw, olw);
        }
    }
    if (lw > 0.0) {
        for (i = 0; i < n; i++) {
            gwy_selection_get_object(sel, i, xy);
            xf = qx*xy[0];
            yf = qy*xy[1];
            xt = qx*xy[2];
            yt = qy*xy[3];
            cairo_rectangle(cr, xf, yf, xt - xf, yt - yf);
            stroke_path(cr, &colour, lw);
        }
    }
}

static void
draw_sel_lattice(ModuleArgs *args,
                 const ImgExportSizes *sizes,
                 GwySelection *sel,
                 gdouble qx, gdouble qy,
                 G_GNUC_UNUSED PangoLayout *layout,
                 G_GNUC_UNUSED GString *s,
                 cairo_t *cr)
{
    enum { maxlines = 80 };

    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble xf, yf, xt, yt, xy[4];
    gdouble w = sizes->image.w - 2.0*lw;
    gdouble h = sizes->image.h - 2.0*lw;
    guint n;
    gint i;

    n = gwy_selection_get_data(sel, NULL);
    if (n < 1)
        return;

    /* XXX: Draw the first lattice.  It makes little sense to have multiple objects in this selection type. */
    gwy_selection_get_object(sel, 0, xy);
    if (olw > 0.0) {
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(i*xy[0] - maxlines*xy[2]) + 0.5*w;
            yf = qy*(i*xy[1] - maxlines*xy[3]) + 0.5*h;
            xt = qx*(i*xy[0] + maxlines*xy[2]) + 0.5*w;
            yt = qy*(i*xy[1] + maxlines*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(-maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yf = qy*(-maxlines*xy[1] + i*xy[3]) + 0.5*h;
            xt = qx*(maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yt = qy*(maxlines*xy[1] + i*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        stroke_path_outline(cr, &outcolour, lw, olw);
    }
    if (lw > 0.0) {
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(i*xy[0] - maxlines*xy[2]) + 0.5*w;
            yf = qy*(i*xy[1] - maxlines*xy[3]) + 0.5*h;
            xt = qx*(i*xy[0] + maxlines*xy[2]) + 0.5*w;
            yt = qy*(i*xy[1] + maxlines*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        for (i = -maxlines; i <= maxlines; i++) {
            xf = qx*(-maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yf = qy*(-maxlines*xy[1] + i*xy[3]) + 0.5*h;
            xt = qx*(maxlines*xy[0] + i*xy[2]) + 0.5*w;
            yt = qy*(maxlines*xy[1] + i*xy[3]) + 0.5*h;
            cairo_move_to(cr, xf, yf);
            cairo_line_to(cr, xt, yt);
        }
        stroke_path(cr, &colour, lw);
    }
}

static void
draw_sel_path(ModuleArgs *args,
              const ImgExportSizes *sizes,
              GwySelection *sel,
              gdouble qx, gdouble qy,
              G_GNUC_UNUSED PangoLayout *layout,
              G_GNUC_UNUSED GString *s,
              cairo_t *cr)
{
    GwyRGBA colour = gwy_params_get_color(args->params, PARAM_SEL_COLOR);
    GwyRGBA outcolour = gwy_params_get_color(args->params, PARAM_SEL_OUTLINE_COLOR);
    gdouble lt = gwy_params_get_double(args->params, PARAM_SEL_LINE_THICKNESS);
    gboolean is_vector = !!args->env->format->write_vector;
    gdouble lw = sizes->sizes.line_width;
    gdouble olw = sizes->sizes.sel_outline_width;
    gdouble slackness, q, px, py, vx, vy, len, xy[2];
    GwyXY *pts;
    const GwyXY *natpts, *tangents;
    GwySpline *spline;
    gboolean closed;
    guint n, nn, i;

    g_object_get(sel, "slackness", &slackness, "closed", &closed, NULL);
    n = gwy_selection_get_data(sel, NULL);
    if (n < 2)
        return;

    px = sizes->image.w/gwy_data_field_get_xres(args->env->dfield);
    py = sizes->image.h/gwy_data_field_get_yres(args->env->dfield);

    /* XXX: This is dirty. Unfortunately, we need to know the natural units for good spline sampling and the vector
     * ones are too coarse. Hence we artificially refine them and cross fingers. */
    q = is_vector ? 8.0 : 1.0;
    pts = g_new(GwyXY, n);
    for (i = 0; i < n; i++) {
        gwy_selection_get_object(sel, i, xy);
        pts[i].x = q*qx*xy[0];
        pts[i].y = q*qy*xy[1];
    }
    spline = gwy_spline_new_from_points(pts, n);
    gwy_spline_set_slackness(spline, slackness);
    gwy_spline_set_closed(spline, closed);

    tangents = gwy_spline_get_tangents(spline);
    natpts = gwy_spline_sample_naturally(spline, &nn);
    g_return_if_fail(nn >= 2);

    /* Path outline */
    if (olw > 0.0) {
        cairo_save(cr);
        cairo_set_line_width(cr, lw + 2.0*olw);
        set_cairo_source_rgb(cr, &outcolour);

        if (closed)
            cairo_move_to(cr, natpts[0].x/q, natpts[0].y/q);
        else {
            /* BUTT caps */
            vx = natpts[0].x - natpts[1].x;
            vy = natpts[0].y - natpts[1].y;
            len = sqrt(vx*vx + vy*vy);
            vx *= olw/len;
            vy *= olw/len;
            cairo_move_to(cr, natpts[0].x/q + vx, natpts[0].y/q + vy);
        }

        for (i = 1; i < nn-1; i++)
            cairo_line_to(cr, natpts[i].x/q, natpts[i].y/q);

        if (closed) {
            cairo_line_to(cr, natpts[nn-1].x/q, natpts[nn-1].y/q);
            cairo_close_path(cr);
        }
        else {
            /* BUTT caps */
            vx = natpts[nn-1].x - natpts[nn-2].x;
            vy = natpts[nn-1].y - natpts[nn-2].y;
            len = sqrt(vx*vx + vy*vy);
            vx *= olw/len;
            vy *= olw/len;
            cairo_line_to(cr, natpts[nn-1].x/q + vx, natpts[nn-1].y/q + vy);
        }

        cairo_stroke(cr);
        cairo_restore(cr);
    }

    /* Tick outline */
    if (olw > 0.0 && lt > 0.0) {
        for (i = 0; i < n; i++) {
            vx = tangents[i].y;
            vy = -tangents[i].x;
            len = sqrt(vx*vx + vy*vy);
            vx *= lt*px/len;
            vy *= lt*py/len;
            draw_line_outline(cr, pts[i].x/q - 0.5*vx, pts[i].y/q - 0.5*vy, pts[i].x/q + 0.5*vx, pts[i].y/q + 0.5*vy,
                              &outcolour, lw, olw);
        }
    }

    /* Path */
    if (lw > 0.0) {
        cairo_set_line_width(cr, lw);
        set_cairo_source_rgb(cr, &colour);
        cairo_move_to(cr, natpts[0].x/q, natpts[0].y/q);
        for (i = 1; i < nn; i++)
            cairo_line_to(cr, natpts[i].x/q, natpts[i].y/q);
        if (closed)
            cairo_close_path(cr);
        cairo_stroke(cr);
    }

    /* Tick */
    if (lw > 0.0 && lt > 0.0) {
        for (i = 0; i < n; i++) {
            vx = tangents[i].y;
            vy = -tangents[i].x;
            len = sqrt(vx*vx + vy*vy);
            vx *= lt*px/len;
            vy *= lt*py/len;
            cairo_move_to(cr, pts[i].x/q - 0.5*vx, pts[i].y/q - 0.5*vy);
            cairo_line_to(cr, pts[i].x/q + 0.5*vx, pts[i].y/q + 0.5*vy);
        }
        cairo_stroke(cr);
    }

    gwy_spline_free(spline);
    g_free(pts);
}

static void
select_a_real_font(ModuleArgs *args, GtkWidget *widget)
{
    static const gchar *fonts_to_try[] = {
        /* Linux */
        "Liberation Sans", "Nimbus Sans L", "DejaVu Sans", "Cantarell",
        /* OS X */
        "San Francisco", "Lucida Grande", "Helvetica Neue",
        /* Windows, but Arial is quite ubiquitous. */
        "Tahoma", "Arial", "Helvetica",
        /* Alias, can be something odd... */
        "Sans",
    };

    PangoContext *context;
    PangoFontFamily **families = NULL;
    const gchar *font, *name, *cname;
    gchar *currname;
    gint nfamilies, i;
    guint j;

    context = gtk_widget_get_pango_context(widget);
    pango_context_list_families(context, &families, &nfamilies);

    /* Handle possible trailing comma in the font name. */
    font = gwy_params_get_string(args->params, PARAM_FONT);
    j = strlen(font);
    if (j > 0 && font[j-1] == ',')
        j--;
    currname = g_strndup(font, j);

    for (i = 0; i < nfamilies; i++) {
        name = pango_font_family_get_name(families[i]);
        gwy_debug("available family <%s>", name);
        if (g_ascii_strcasecmp(currname, name) == 0) {
            /* The font from settings seems available.   Use it. */
            gwy_debug("found font %s", currname);
            g_free(currname);
            g_free(families);
            return;
        }
    }
    gwy_debug("did not find font %s", currname);
    g_free(currname);

    /* We do not have the font from settings.  Try to find some other sane sans serif font. */
    for (j = 0; j < G_N_ELEMENTS(fonts_to_try); j++) {
        cname = fonts_to_try[j];
        for (i = 0; i < nfamilies; i++) {
            name = pango_font_family_get_name(families[i]);
            if (g_ascii_strcasecmp(cname, name) == 0) {
                gwy_debug("found font %s", cname);
                gwy_params_set_string(args->params, PARAM_FONT, cname);
                g_free(families);
                return;
            }
        }
    }

    /* Shrug and proceed... */
    g_free(families);
}

/* Gather all kind of information about the data and how they are currently displayed in Gwyddion so that we can mimic
 * it. */
static ImgExportEnv*
img_export_load_env(const ImgExportFormat *format,
                    GwyContainer *data,
                    gint id)
{
    GwyDataField *dfield, *show;
    GwyContainer *settings;
    GwyDataView *dataview;
    GwyVectorLayer *vlayer;
    GObject *sel;
    GwyInventory *gradients;
    const guchar *gradname = NULL, *key;
    guint xres, yres;
    GString *s;
    ImgExportEnv *env;

    settings = gwy_app_settings_get();
    s = g_string_new(NULL);

    env = g_new0(ImgExportEnv, 1);
    env->decimal_symbol = gwy_get_decimal_separator();
    env->format = format;
    env->data = data;
    env->id = id;
    gwy_app_data_browser_get_current(GWY_APP_DATA_FIELD, &dfield,
                                     GWY_APP_MASK_FIELD, &env->mask,
                                     GWY_APP_SHOW_FIELD, &show,
                                     GWY_APP_DATA_VIEW, &dataview,
                                     0);

    if (show) {
        env->has_presentation = TRUE;
        env->dfield = show;
    }
    else
        env->dfield = dfield;

    gwy_container_gis_boolean(data, gwy_app_get_data_real_square_key_for_id(env->id), &env->realsquare);
    if (!gwy_rgba_get_from_container(&env->mask_colour, data, g_quark_to_string(gwy_app_get_mask_key_for_id(env->id))))
        gwy_rgba_get_from_container(&env->mask_colour, settings, "/mask");

    /* Find out native pixel sizes for the data bitmaps. */
    xres = gwy_data_field_get_xres(env->dfield);
    yres = gwy_data_field_get_yres(env->dfield);
    if (env->realsquare) {
        gdouble xreal = gwy_data_field_get_xreal(env->dfield);
        gdouble yreal = gwy_data_field_get_yreal(env->dfield);
        gdouble scale = MAX(xres/xreal, yres/yreal);
        /* This is how GwyDataView rounds it so we should get a pixmap of this size. */
        env->xres = GWY_ROUND(xreal*scale);
        env->yres = GWY_ROUND(yreal*scale);
    }
    else {
        env->xres = xres;
        env->yres = yres;
    }
    gwy_debug("env->xres %u, env->yres %u", env->xres, env->yres);
    env->minzoom = 2.0/MIN(env->xres, env->yres);
    env->maxzoom = 16384.0/MAX(env->xres, env->yres);

    /* False colour mapping. */
    gwy_container_gis_string(data, gwy_app_get_data_palette_key_for_id(env->id), &gradname);
    gradients = gwy_gradients();
    env->gradient = gwy_inventory_get_item_or_default(gradients, gradname);
    gwy_resource_use(GWY_RESOURCE(env->gradient));

    env->fm_rangetype = GWY_LAYER_BASIC_RANGE_FULL;
    gwy_container_gis_enum_by_name(settings, APP_RANGE_KEY, &env->fm_rangetype);
    gwy_debug("default range type: %u", env->fm_rangetype);

    gwy_container_gis_enum(data, gwy_app_get_data_range_type_key_for_id(env->id), &env->fm_rangetype);
    gwy_debug("data range type: %u", env->fm_rangetype);

    /* The current behaviour is that all mappings work on presentations, but
     * fixed range is ignored so it means full. */
    gwy_data_field_get_min_max(env->dfield, &env->fm_min, &env->fm_max);
    if (env->fm_rangetype == GWY_LAYER_BASIC_RANGE_AUTO)
        gwy_data_field_get_autorange(env->dfield, &env->fm_min, &env->fm_max);
    if (!env->has_presentation && env->fm_rangetype == GWY_LAYER_BASIC_RANGE_FIXED) {
        /* These may not be actually set; for this we always init with full. */
        gwy_container_gis_double(data, gwy_app_get_data_range_min_key_for_id(env->id), &env->fm_min);
        gwy_container_gis_double(data, gwy_app_get_data_range_max_key_for_id(env->id), &env->fm_max);
    }

    if ((env->fm_inverted = (env->fm_max < env->fm_min)))
        GWY_SWAP(gdouble, env->fm_min, env->fm_max);

    /* Selections. */
    env->selections = g_array_new(FALSE, FALSE, sizeof(GQuark));
    g_string_printf(s, "/%d/select/", env->id);
    gwy_container_foreach(data, s->str, &add_selection, env->selections);

    if (dataview
        && (vlayer = gwy_data_view_get_top_layer(dataview))
        && (key = gwy_vector_layer_get_selection_key(vlayer))
        && g_str_has_prefix(key, s->str)
        && gwy_container_gis_object_by_name(data, key, &sel)) {
        const gchar *typename = G_OBJECT_TYPE_NAME(sel);

        env->vlayer_sel_key = g_quark_from_string(key + s->len);

        if (gwy_strequal(typename, "GwySelectionLine")) {
            gint lt;

            env->sel_line_have_layer = TRUE;
            g_object_get(vlayer, "thickness", &lt, NULL);
            gwy_debug("got thickness from layer %d", lt);
            env->sel_line_thickness = lt;
        }
        else if (gwy_strequal(typename, "GwySelectionPoint")) {
            gint pr;

            env->sel_point_have_layer = TRUE;
            g_object_get(vlayer, "marker-radius", &pr, NULL);
            gwy_debug("got radius from layer %d", pr);
            env->sel_point_radius = pr;
        }
        else if (gwy_strequal(typename, "GwySelectionPath")) {
            gint lt;

            env->sel_path_have_layer = TRUE;
            g_object_get(vlayer, "thickness", &lt, NULL);
            gwy_debug("got thickness from layer %d", lt);
            env->sel_line_thickness = lt;
        }
    }

    /* Miscellaneous stuff. */
    env->title = gwy_app_get_data_field_title(data, env->id);
    g_strstrip(env->title);

    if (format->write_grey16) {
        env->grey = gwy_inventory_get_item(gradients, "Gray");
        gwy_resource_use(GWY_RESOURCE(env->grey));
    }

    g_string_free(s, TRUE);

    return env;
}

static void
img_export_free_env(ImgExportEnv *env)
{
    if (env->grey)
        gwy_resource_release(GWY_RESOURCE(env->grey));
    gwy_resource_release(GWY_RESOURCE(env->gradient));
    g_free(env->title);
    g_free(env->selection_name);
    g_array_free(env->selections, TRUE);
    g_free(env);
}

static void
sanitise_params(ModuleArgs *args, gboolean full_module, gboolean is_interactive)
{
    GwyParams *params = args->params;
    const gchar *selection_name = gwy_params_get_string(params, PARAM_SELECTION);
    ImgExportMode mode = gwy_params_get_enum(args->params, PARAM_MODE);
    ImgExportEnv *env = args->env;
    GwyRGBA rgba;
    guint i;

    if (mode == IMGEXPORT_MODE_GREY16 && !env->format->write_grey16)
        gwy_params_set_enum(params, PARAM_MODE, (mode = IMGEXPORT_MODE_PRESENTATION));

    if (!inset_length_ok(env->dfield, gwy_params_get_string(params, PARAM_INSET_LENGTH)))
        gwy_params_set_string(params, PARAM_INSET_LENGTH, scalebar_auto_length(env->dfield, NULL));

    /* Split colours to the traditional parameters. */
    rgba = gwy_params_get_color(params, PARAM_INSET_COLOR);
    gwy_params_set_double(params, PARAM_INSET_ALPHA, rgba.a);
    rgba.a = 1.0;
    gwy_params_set_color(params, PARAM_INSET_RGB, rgba);

    rgba = gwy_params_get_color(params, PARAM_SEL_COLOR);
    gwy_params_set_double(params, PARAM_SEL_ALPHA, rgba.a);
    rgba.a = 1.0;
    gwy_params_set_color(params, PARAM_SEL_RGB, rgba);

    /* Width, height and ppi do not need any setup here because they are slave parameters. */
    if (full_module) {
        GwyInterpolationType interp = gwy_params_get_enum(params, PARAM_INTERPOLATION);
        gwy_params_set_enum(params, PARAM_INTERPOLATION_VECTOR,
                            interp == GWY_INTERPOLATION_ROUND ? interp : GWY_INTERPOLATION_LINEAR);
    }

    /* When run interactively, try to show the same selection that is currently shown on the data, if any.  But in
     * non-interactive usage strive for predictable behaviour and honour selection name from settings. */
    if (is_interactive && env->vlayer_sel_key) {
        selection_name = g_quark_to_string(env->vlayer_sel_key);
        gwy_params_set_string(params, PARAM_SELECTION, selection_name);
    }

    gwy_debug("settings selection %s", selection_name);
    for (i = 0; i < env->selections->len; i++) {
        GQuark quark = g_array_index(env->selections, GQuark, i);
        if (gwy_strequal(selection_name, g_quark_to_string(quark)))
            break;
    }
    if (i == env->selections->len) {
        if (env->selections->len && is_interactive)
            selection_name = g_quark_to_string(g_array_index(env->selections, GQuark, 0));
        else
            selection_name = NULL;
        gwy_debug("not found, trying %s", selection_name);
        gwy_params_set_string(params, PARAM_SELECTION, selection_name);
    }
    /* This is the ‘default’ we will reset to. */
    env->selection_name = g_strdup(selection_name);
    gwy_debug("feasible selection %s", selection_name);
    if (is_interactive) {
        const ImgExportSelectionType *seltype = find_selection_type(args, selection_name, NULL);
        gint id;
        gwy_debug("selection type %p", seltype);
        if (seltype && seltype->has_options) {
            for (i = 0; (id = seltype->has_options[i]); i++) {
                if (id == PARAM_SEL_LINE_THICKNESS)
                    gwy_params_set_double(params, id, env->sel_line_thickness);
                else if (id == PARAM_SEL_POINT_RADIUS)
                    gwy_params_set_double(params, id, env->sel_point_radius);
            }
        }
    }
}

static void
update_presets(void)
{
    gchar *dirname = g_build_filename(gwy_get_user_dir(),
                                      gwy_resource_class_get_name(g_type_class_peek(preset_resource_type)),
                                      NULL);
    gchar *marker = g_build_filename(dirname, ".version", NULL);
    gchar *fixed, *fullpath, *buffer = NULL;
    const gchar *filename;
    GRegex *regex;
    gsize size, len;
    gint major, minor;
    gboolean ok;
    GDir *dir;
    FILE *fh;

    if (g_file_get_contents(marker, &buffer, &size, NULL)) {
        gwy_debug("found marker %s", buffer);
        ok = (sscanf(buffer, "%d.%d", &major, &minor) && major == 2 && minor >= 64);
        g_free(buffer);
        if (ok) {
            gwy_debug("version is current");
            goto end;
        }
    }

    gwy_debug("upgrading presets");
    dir = g_dir_open(dirname, 0, NULL);
    if (!dir)
        goto end;

    regex = g_regex_new("^outline_width (?P<value>[0-9.]+)$", G_REGEX_MULTILINE | G_REGEX_OPTIMIZE, 0, NULL);
    g_assert(regex);

    ok = TRUE;
    while ((filename = g_dir_read_name(dir))) {
        fullpath = g_build_filename(dirname, filename, NULL);
        if (g_file_get_contents(fullpath, &buffer, &size, NULL)) {
            fixed = g_regex_replace(regex, buffer, size, 0,
                                    "inset_outline_width \\g<value>\nsel_outline_width \\g<value>", 0, NULL);
            if (fixed && (len = strlen(fixed)) > size) {
                if ((fh = fopen(fullpath, "w"))) {
                    fwrite(fixed, 1, len, fh);
                    fclose(fh);
                    gwy_debug("preset %s upgraded", filename);
                }
                else
                    ok = FALSE;
            }
            g_free(fixed);
            g_free(buffer);
        }
        else
            ok = FALSE;
        g_free(fullpath);
    }

    g_regex_unref(regex);
    g_dir_close(dir);

    if (ok) {
        gwy_debug("creating marker 2.64");
        if ((fh = fopen(marker, "w"))) {
            fwrite("2.64", 1, strlen("2.64"), fh);
            fclose(fh);
        }
    }

end:
    g_free(marker);
    g_free(dirname);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
