/*
 *  $Id: hdrimage.cc 26164 2024-02-07 10:52:23Z yeti-dn $
 *  Copyright (C) 2011-2022 David Necas (Yeti).
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
 * OpenEXR images
 * .exr
 * Read Export
 **/

/**
 * [FILE-MAGIC-MISSING]
 * Avoding clash with a standard file format.
 **/

#include "config.h"
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#ifdef HAVE_PNG
#include <png.h>
#endif

#ifdef HAVE_EXR
#include <exception>
#include <OpenEXRConfig.h>
#include <half.h>
#include <ImfNamespace.h>
#include <ImfChannelList.h>
#include <ImfOutputFile.h>
#include <ImfInputFile.h>
#include <ImfRgbaFile.h>
#include <ImfFrameBuffer.h>
#include <ImfArray.h>
#include <ImfDoubleAttribute.h>
#include <ImfStringAttribute.h>
#else
#  define HALF_MIN 5.96046448e-08
#  define HALF_NRM_MIN 6.10351562e-05
#  define HALF_MAX 65504.0
#  define half guint16
#endif

#include <libgwyddion/gwymacros.h>
#include <libgwyddion/gwymath.h>
#include <libgwymodule/gwymodule-file.h>
#include <libprocess/stats.h>
#include <libgwydgets/gwydgets.h>
#include <app/gwyapp.h>
#include <app/gwymoduleutils.h>
#include <app/gwymoduleutils-file.h>

#include "err.h"
#include "gwytiff.h"
#include "image-keys.h"

#define EXR_EXTENSION ".exr"
#define EXR_MAGIC "\x76\x2f\x31\x01"
#define EXR_MAGIC_SIZE sizeof(EXR_MAGIC)-1

enum {
    PREVIEW_SIZE = 320,
    RESPONSE_USE_SUGGESTED = 12345,
};

enum {
    PIXMAP_HAS_COLOURS = 1u << 0,   /* Unset if there are RGB, but all are identical. */
    PIXMAP_HAS_ALPHA   = 1u << 1,
};

enum {
    PARAM_XREAL,
    PARAM_YREAL,
    PARAM_ZREAL,
    PARAM_XYMEASUREEQ,
    PARAM_SIZE_IN_PIXELS,
    PARAM_XYUNIT,
    PARAM_ZUNIT,
    PARAM_AS_VOLUME,

    WIDGET_IMAGE_INFO,
};

enum {
    PARAM_BIT_DEPTH,
    PARAM_ZSCALE,

    WIDGET_RANGES,
    BUTTON_USE_SUGGESTED,
};

typedef enum {
    /* Used with common image formats supporting 16bit greyscale */
    GWY_BIT_DEPTH_INT16 = 16,
    /* Used with HDR greyscale images */
    GWY_BIT_DEPTH_HALF  = 17,
    GWY_BIT_DEPTH_INT32 = 32,
    GWY_BIT_DEPTH_FLOAT = 33,
} GwyBitDepth;

typedef enum {
    BAD_FILE    = 0,
    PLAIN_IMAGE = 1,
    GWY_META    = 2,
} DetectionResult;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    /* Cached input data properties */
    gdouble pmin;
    gdouble pmax;
    gdouble pcentre;
    gdouble min;
    gdouble max;
} ExportArgs;

typedef struct {
    ExportArgs *args;
    GtkWidget *dialog;
    GwyResults *results;
    GwyParamTable *table;
} ExportGUI;

typedef struct {
    GwyParams *params;
    GwyDataField *field;
    /* Cached input properties. */
    gchar *channels;
    gint npages;
    gboolean pages_are_uniform;
} ImportArgs;

typedef struct {
    ImportArgs *args;
    GtkWidget *dialog;
    GwyParamTable *table_lateral;
    GwyParamTable *table_values;
} ImportGUI;

static gboolean module_register            (void);

#ifdef HAVE_EXR
static gint             exr_detect            (const GwyFileDetectInfo *fileinfo,
                                               gboolean only_name,
                                               const gchar *name);
static GwyContainer*    exr_load              (const gchar *filename,
                                               GwyRunType mode,
                                               GError **error,
                                               const gchar *name);
static GwyContainer*    exr_load_image        (const gchar *filename,
                                               GwyRunType mode,
                                               GSList **objects,
                                               GSList **buffers,
                                               GError **error);
static gboolean         exr_export            (GwyContainer *data,
                                               const gchar *filename,
                                               GwyRunType mode,
                                               GError **error);
static GwyDialogOutcome run_export_gui        (ExportArgs *args,
                                               const gchar *name);
static void             export_param_changed  (ExportGUI *gui,
                                               gint id);
static void             export_dialog_response(ExportGUI *gui,
                                               gint id);
static void             exr_write_image       (GwyDataField *field,
                                               gchar *imagedata,
                                               const gchar *filename,
                                               const gchar *title,
                                               GwyBitDepth bit_depth,
                                               gdouble zscale);
static gdouble          suggest_zscale        (GwyBitDepth bit_depth,
                                               gdouble pmin,
                                               gdouble pmax,
                                               gdouble pcentre);
static void             representable_range   (GwyBitDepth bit_depth,
                                               gdouble zscale,
                                               gdouble *min,
                                               gdouble *max);
static void             find_range            (GwyDataField *field,
                                               gdouble *fmin,
                                               gdouble *fmax,
                                               gdouble *pmin,
                                               gdouble *pmax,
                                               gdouble *pcentre);
static gchar*           create_image_data     (GwyDataField *field,
                                               GwyBitDepth bit_depth,
                                               gdouble zscale,
                                               gdouble zmin,
                                               gdouble zmax);
#endif

#ifdef HAVE_PNG
static gint          png16_detect        (const GwyFileDetectInfo *fileinfo,
                                          gboolean only_name,
                                          const gchar *name);
static GwyContainer* png16_load          (const gchar *filename,
                                          GwyRunType mode,
                                          GError **error,
                                          const gchar *name);
#endif

static gint             pgm16_detect           (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name,
                                                const gchar *name);
static GwyContainer*    pgm16_load             (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error,
                                                const gchar *name);
static gint             tiffbig_detect         (const GwyFileDetectInfo *fileinfo,
                                                gboolean only_name,
                                                const gchar *name);
static GwyContainer*    tiffbig_load           (const gchar *filename,
                                                GwyRunType mode,
                                                GError **error,
                                                const gchar *name);
static GwyDialogOutcome run_import_gui         (ImportArgs *args,
                                                const gchar *name);
static void             import_param_changed   (ImportGUI *gui,
                                                gint id);
static void             sanitise_import_params (ImportArgs *args);
static void             field_props_from_params(GwyParams *params,
                                                gdouble *xreal,
                                                gdouble *yreal,
                                                GwySIUnit **xyunit,
                                                gdouble *zmax,
                                                GwySIUnit **zunit);

static GwyModuleInfo module_info = {
    GWY_MODULE_ABI_VERSION,
    &module_register,
    N_("Imports 16bit grayscale PPM, PNG and TIFF images, imports and exports OpenEXR images (if available)."),
    "Yeti <yeti@gwyddion.net>",
    "3.4",
    "David Nečas (Yeti)",
    "2011",
};

GWY_MODULE_QUERY(module_info)

static gboolean
module_register(void)
{
#ifdef HAVE_EXR
    gwy_file_func_register("openexr",
                           N_("OpenEXR images (.exr)"),
                           (GwyFileDetectFunc)&exr_detect,
                           (GwyFileLoadFunc)&exr_load,
                           NULL,
                           (GwyFileSaveFunc)&exr_export);
#endif
#ifdef HAVE_PNG
    gwy_file_func_register("png16",
                           N_("PNG images with 16bit depth (.png)"),
                           (GwyFileDetectFunc)&png16_detect,
                           (GwyFileLoadFunc)&png16_load,
                           NULL,
                           NULL);
#endif
    gwy_file_func_register("pgm16",
                           N_("PGM images with 16bit depth (.pgm)"),
                           (GwyFileDetectFunc)&pgm16_detect,
                           (GwyFileLoadFunc)&pgm16_load,
                           NULL,
                           NULL);
    gwy_file_func_register("tiffbig",
                           N_("TIFF and BigTIFF images with high depth (.tiff)"),
                           (GwyFileDetectFunc)&tiffbig_detect,
                           (GwyFileLoadFunc)&tiffbig_load,
                           NULL,
                           NULL);

    return TRUE;
}

static GwyParamDef*
define_import_params(void)
{
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    /* Share keys with the pixmap module so that people get the same paramters for low-depth and high-depth images. */
    gwy_param_def_set_function_name(paramdef, "pixmap");
    gwy_param_def_add_double(paramdef, PARAM_XREAL, "xreal", _("_Horizontal size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_YREAL, "yreal", _("_Vertical size"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_double(paramdef, PARAM_ZREAL, "zreal", _("_Z-scale (per sample unit)"),
                             -G_MAXDOUBLE, G_MAXDOUBLE, 1.0);
    gwy_param_def_add_boolean(paramdef, PARAM_XYMEASUREEQ, "xymeasureeq", _("_Square pixels"), TRUE);
    gwy_param_def_add_boolean(paramdef, PARAM_SIZE_IN_PIXELS, "size_in_pixels", _("Just use _pixels"), FALSE);
    gwy_param_def_add_unit(paramdef, PARAM_XYUNIT, "xyunit", _("_Dimensions unit"), NULL);
    gwy_param_def_add_unit(paramdef, PARAM_ZUNIT, "zunit", _("_Value unit"), NULL);
    gwy_param_def_add_boolean(paramdef, PARAM_AS_VOLUME, "as_volume", _("Import as _volume data"), TRUE);
    return paramdef;
}

static GwyParamDef*
define_export_params(void)
{
    static const GwyEnum bit_depths[] = {
        { _("Half (16bit float)"), GWY_BIT_DEPTH_HALF,  },
        { _("Float (32bit)"),      GWY_BIT_DEPTH_FLOAT, },
        { _("Integer (32bit)"),    GWY_BIT_DEPTH_INT32, },
    };
    static GwyParamDef *paramdef = NULL;

    if (paramdef)
        return paramdef;

    paramdef = gwy_param_def_new();
    gwy_param_def_set_function_name(paramdef, gwy_file_func_current());
    gwy_param_def_add_gwyenum(paramdef, PARAM_BIT_DEPTH, "bit_depth", _("_Data format"),
                              bit_depths, G_N_ELEMENTS(bit_depths), GWY_BIT_DEPTH_HALF);
    gwy_param_def_add_double(paramdef, PARAM_ZSCALE, "zscale", _("_Z-scale"), G_MINDOUBLE, G_MAXDOUBLE, 1.0);
    return paramdef;
}

/***************************************************************************************************************
 *
 * OpenEXR
 *
 ***************************************************************************************************************/

#ifdef HAVE_EXR
static gint
exr_detect(const GwyFileDetectInfo *fileinfo,
           gboolean only_name,
           G_GNUC_UNUSED const gchar *name)
{
    gint score = 0;

    if (only_name)
        return g_str_has_suffix(fileinfo->name_lowercase, EXR_EXTENSION) ? 20 : 0;

    if (fileinfo->buffer_len > EXR_MAGIC_SIZE && memcmp(fileinfo->head, EXR_MAGIC, EXR_MAGIC_SIZE) == 0)
        score = 100;

    return score;
}

static gboolean
exr_export(GwyContainer *data,
           const gchar *filename,
           GwyRunType mode,
           GError **error)
{
    ExportArgs args;
    gboolean ok = FALSE;
    GwyDialogOutcome outcome;
    const gchar *title = "Data";
    gchar *imagedata = NULL;
    GwyBitDepth bit_depth;
    gdouble zscale;
    gint id;

    gwy_clear(&args, 1);

    gwy_app_data_browser_get_current(GWY_APP_CONTAINER, &data,
                                     GWY_APP_DATA_FIELD, &args.field,
                                     GWY_APP_DATA_FIELD_ID, &id,
                                     0);
    if (!args.field) {
        err_NO_CHANNEL_EXPORT(error);
        return FALSE;
    }

    args.params = gwy_params_new_from_settings(define_export_params());
    find_range(args.field, &args.min, &args.max, &args.pmin, &args.pmax, &args.pcentre);

    if (mode == GWY_RUN_INTERACTIVE) {
        outcome = run_export_gui(&args, "OpenEXR");
        gwy_params_save_to_settings(args.params);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto end;
        }
    }

    bit_depth = (GwyBitDepth)gwy_params_get_enum(args.params, PARAM_BIT_DEPTH);
    zscale = gwy_params_get_double(args.params, PARAM_ZSCALE);
    imagedata = create_image_data(args.field, bit_depth, zscale, args.min, args.max);
    gwy_container_gis_string(data, gwy_app_get_data_title_key_for_id(id), (const guchar**)&title);

    try {
        exr_write_image(args.field, imagedata, filename, title, bit_depth, zscale);
        ok = TRUE;
    }
    catch (const std::exception &exc) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("EXR image writing failed with libImf error: %s"), exc.what());
    }

end:
    g_object_unref(args.params);
    g_free(imagedata);

    return ok;
}

static GwyDialogOutcome
run_export_gui(ExportArgs *args, const gchar *name)
{
    ExportGUI gui;
    GwyParamTable *table;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    GwyResults *results;
    gchar *title;

    gwy_clear(&gui, 1);
    gui.args = args;

    /* TRANSLATORS: Dialog title; %s is PNG, TIFF, ... */
    title = g_strdup_printf(_("Export %s"), name);
    gui.dialog = gwy_dialog_new(title);
    g_free(title);
    dialog = GWY_DIALOG(gui.dialog);
    gwy_dialog_add_buttons(dialog, GTK_RESPONSE_CANCEL, GTK_RESPONSE_OK, 0);

    results = gui.results = gwy_results_new();
    gwy_results_add_format(results, "datarange", N_("Data range"), FALSE, "%{zmin}v – %{zmax}v", NULL);
    gwy_results_add_format(results, "reprange", N_("Representable range"), FALSE, "%{rmin}v – %{rmax}v", NULL);
    gwy_results_add_value_plain(results, "suggscale", N_("Suggested Z-scale"));
    /* The rest is filled in param-update. */
    gwy_results_fill_format(results, "datarange", "zmin", args->min, "zmax", args->max, NULL);

    table = gui.table = gwy_param_table_new(args->params);
    gwy_param_table_append_radio(table, PARAM_BIT_DEPTH);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_entry(table, PARAM_ZSCALE);
    gwy_param_table_append_separator(table);
    gwy_param_table_append_results(table, WIDGET_RANGES, results, "datarange", "reprange", "suggscale", NULL);
    gwy_param_table_append_button(table, BUTTON_USE_SUGGESTED, -1, RESPONSE_USE_SUGGESTED, _("_Use Suggested"));
    gwy_dialog_add_content(dialog, gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(table, "param-changed", G_CALLBACK(export_param_changed), &gui);
    g_signal_connect_swapped(dialog, "response", G_CALLBACK(export_dialog_response), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(gui.results);

    return outcome;
}

static void
export_param_changed(ExportGUI *gui, gint id)
{
    ExportArgs *args = gui->args;
    GwyParams *params = args->params;
    GwyParamTable *table = gui->table;
    GwyBitDepth bit_depth = (GwyBitDepth)gwy_params_get_enum(params, PARAM_BIT_DEPTH);
    gdouble zscale = gwy_params_get_double(params, PARAM_ZSCALE);
    gdouble suggscale, rmin, rmax;

    if (id < 0 || id == PARAM_BIT_DEPTH) {
        gwy_param_table_set_sensitive(table, PARAM_ZSCALE, bit_depth == GWY_BIT_DEPTH_HALF);
        gwy_param_table_set_sensitive(table, WIDGET_RANGES, bit_depth == GWY_BIT_DEPTH_HALF);
        gwy_param_table_set_sensitive(table, BUTTON_USE_SUGGESTED, bit_depth == GWY_BIT_DEPTH_HALF);

        if (bit_depth != GWY_BIT_DEPTH_INT32) {
            suggscale = suggest_zscale(bit_depth, args->pmin, args->pmax, args->pcentre);
            gwy_results_fill_values(gui->results, "suggscale", suggscale, NULL);
        }
        else
            gwy_results_set_na(gui->results, "suggscale", NULL);
    }

    if (bit_depth != GWY_BIT_DEPTH_INT32) {
        representable_range(bit_depth, zscale, &rmin, &rmax);
        gwy_results_fill_format(gui->results, "reprange", "rmin", rmin, "rmax", rmax, NULL);
    }
    else
        gwy_results_set_na(gui->results, "reprange", NULL);

    gwy_param_table_results_fill(gui->table, WIDGET_RANGES);
}

static void
export_dialog_response(ExportGUI *gui, gint id)
{
    if (id == RESPONSE_USE_SUGGESTED) {
        ExportArgs *args = gui->args;
        GwyBitDepth bit_depth = (GwyBitDepth)gwy_params_get_enum(args->params, PARAM_BIT_DEPTH);
        gdouble suggscale = suggest_zscale(bit_depth, args->pmin, args->pmax, args->pcentre);
        gwy_param_table_set_double(gui->table, PARAM_ZSCALE, suggscale);
    }
}

// NB: This function raises a C++ exception instead of reporting the error via GError.  The caller must catch it.
static void
exr_write_image(GwyDataField *field,
                gchar *imagedata,
                const gchar *filename,
                const gchar *title,
                GwyBitDepth bit_depth,
                gdouble zscale)
{
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);

    Imf::Header header(xres, yres);
    header.lineOrder() = Imf::INCREASING_Y;

    Imf::PixelType pixel_type;
    if (bit_depth == GWY_BIT_DEPTH_HALF)
        pixel_type = Imf::HALF;
    else if (bit_depth == GWY_BIT_DEPTH_FLOAT)
        pixel_type = Imf::FLOAT;
    else if (bit_depth == GWY_BIT_DEPTH_INT32)
        pixel_type = Imf::UINT;
    else {
        g_assert_not_reached();
    }

    gdouble v;
    v = gwy_data_field_get_xreal(field);
    header.insert(GWY_IMGKEY_XREAL, Imf::DoubleAttribute(v));
    v = gwy_data_field_get_xreal(field);
    header.insert(GWY_IMGKEY_YREAL, Imf::DoubleAttribute(v));
    if (bit_depth == GWY_BIT_DEPTH_INT32) {
        gdouble zmin, zmax;
        gwy_data_field_get_min_max(field, &zmin, &zmax);
        header.insert(GWY_IMGKEY_ZMIN, Imf::DoubleAttribute(zmin));
        header.insert(GWY_IMGKEY_ZMAX, Imf::DoubleAttribute(zmax));
    }
    else if (zscale != 1.0)
        header.insert(GWY_IMGKEY_ZSCALE, Imf::DoubleAttribute(zscale));
    if ((v = gwy_data_field_get_xoffset(field)))
        header.insert(GWY_IMGKEY_XOFFSET, Imf::DoubleAttribute(v));
    if ((v = gwy_data_field_get_yoffset(field)))
        header.insert(GWY_IMGKEY_YOFFSET, Imf::DoubleAttribute(v));

    header.insert(GWY_IMGKEY_TITLE, Imf::StringAttribute(title));
    header.insert("Software", Imf::StringAttribute("Gwyddion"));

    gchar *s;
    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_xy(field), GWY_SI_UNIT_FORMAT_PLAIN);
    header.insert(GWY_IMGKEY_XYUNIT, Imf::StringAttribute(s));
    g_free(s);

    s = gwy_si_unit_get_string(gwy_data_field_get_si_unit_z(field), GWY_SI_UNIT_FORMAT_PLAIN);
    header.insert(GWY_IMGKEY_ZUNIT, Imf::StringAttribute(s));
    g_free(s);

    header.channels().insert("Y", Imf::Channel(pixel_type));

    Imf::OutputFile outfile(filename, header);
    Imf::FrameBuffer framebuffer;

    if (pixel_type == Imf::HALF)
        framebuffer.insert("Y", Imf::Slice(pixel_type, imagedata, sizeof(half), xres*sizeof(half)));
    else if (pixel_type == Imf::FLOAT)
        framebuffer.insert("Y", Imf::Slice(pixel_type, imagedata, sizeof(float), xres*sizeof(float)));
    else if (pixel_type == Imf::UINT)
        framebuffer.insert("Y", Imf::Slice(pixel_type, imagedata, sizeof(guint32), xres*sizeof(guint32)));
    else {
        g_assert_not_reached();
    }

    outfile.setFrameBuffer(framebuffer);
    outfile.writePixels(yres);
}

static GwyContainer*
exr_load(const gchar *filename,
         GwyRunType mode,
         GError **error,
         G_GNUC_UNUSED const gchar *name)
{
    GwyContainer *container = NULL;
    GSList *objects = NULL, *buffers = NULL;

    try {
        container = exr_load_image(filename, mode, &objects, &buffers, error);
    }
    catch (const std::exception &exc) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_IO,
                    _("EXR image loading failed with libImf error: %s"), exc.what());
    }

    for (GSList *l = buffers; l; l = g_slist_next(l)) {
        g_free(l->data);
    }
    g_slist_free(buffers);

    for (GSList *l = objects; l; l = g_slist_next(l)) {
        g_object_unref(l->data);
    }
    g_slist_free(objects);


    return container;
}

static const Imf::DoubleAttribute*
exr_get_double_attr(const Imf::InputFile &infile,
                    const gchar *name)
{
    const Imf::DoubleAttribute *attr = infile.header().findTypedAttribute<Imf::DoubleAttribute>(name);

    if (attr) {
        gwy_debug("%s = %g", name, attr->value());
    }
    return attr;
}

static const Imf::StringAttribute*
exr_get_string_attr(const Imf::InputFile &infile,
                    const gchar *name)
{
    const Imf::StringAttribute *attr = infile.header().findTypedAttribute<Imf::StringAttribute>(name);

    if (attr) {
        gwy_debug("%s = <%s>", name, attr->value().c_str());
    }
    return attr;
}

static inline GwyRawDataType
exr_type_to_gwy_type(Imf::PixelType type)
{
    if (type == Imf::UINT)
        return GWY_RAW_DATA_UINT32;
    else if (type == Imf::HALF)
        return GWY_RAW_DATA_HALF;
    else if (type == Imf::FLOAT)
        return GWY_RAW_DATA_FLOAT;

    g_return_val_if_reached((GwyRawDataType)0);
}

static gchar*
exr_format_channel_names(const Imf::ChannelList &channels)
{
    GString *str = g_string_new(NULL);

    for (Imf::ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
        if (str->len)
            g_string_append(str, ", ");
        g_string_append(str, i.name());
    }
    return g_string_free(str, FALSE);
}

// NB: This function either raises a C++ exception or it reports the error via via GError.  In the latter case the
// return value is NULL.
static GwyContainer*
exr_load_image(const gchar *filename,
               GwyRunType mode,
               GSList **objects,
               GSList **buffers,
               GError **error)
{
    GwyContainer *container = NULL;
    Imf::InputFile infile(filename);

    Imath::Box2i dw = infile.header().dataWindow();
    gint width = dw.max.x - dw.min.x + 1;
    gint height = dw.max.y - dw.min.y + 1;
    gwy_debug("width: %d, height: %d", width, height);

    const Imf::DoubleAttribute
        *xreal_attr = exr_get_double_attr(infile, GWY_IMGKEY_XREAL),
        *yreal_attr = exr_get_double_attr(infile, GWY_IMGKEY_YREAL),
        *xoff_attr = exr_get_double_attr(infile, GWY_IMGKEY_XOFFSET),
        *yoff_attr = exr_get_double_attr(infile, GWY_IMGKEY_YOFFSET),
        *zscale_attr = exr_get_double_attr(infile, GWY_IMGKEY_ZSCALE),
        *zmin_attr = exr_get_double_attr(infile, GWY_IMGKEY_ZMIN),
        *zmax_attr = exr_get_double_attr(infile, GWY_IMGKEY_ZMAX);
    const Imf::StringAttribute
        *xyunit_attr = exr_get_string_attr(infile, GWY_IMGKEY_XYUNIT),
        *zunit_attr = exr_get_string_attr(infile, GWY_IMGKEY_ZUNIT),
        *title_attr = exr_get_string_attr(infile, GWY_IMGKEY_TITLE);

    const Imf::ChannelList &channels = infile.header().channels();
    Imf::FrameBuffer framebuffer;
    guint nchannels = 0;
    ImportArgs args;

    gwy_clear(&args, 1);
    for (Imf::ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i, ++nchannels) {
        const Imf::Channel &channel = i.channel();
        guint xs = channel.xSampling, ys = channel.ySampling;
        // Round up; it's better to allocate too large buffer than too small.
        guint xres = (width + xs-1)/xs, yres = (height + ys-1)/ys;
        gwy_debug("channel: <%s>, type: %u", i.name(), (guint)channel.type);
        gwy_debug("samplings: %u, %u", xs, ys);

        if (channel.type == Imf::UINT) {
            guint32 *buffer = g_new(guint32, xres*yres);
            char *base = (char*)(buffer - dw.min.x - xres*dw.min.y);
            *buffers = g_slist_append(*buffers, (gpointer)buffer);
            framebuffer.insert(i.name(),
                               Imf::Slice(Imf::UINT, base, sizeof(buffer[0]), xres*sizeof(buffer[0]), xs, ys));
        }
        else if (channel.type == Imf::HALF) {
            half *buffer = g_new(half, xres*yres);
            char *base = (char*)(buffer - dw.min.x - xres*dw.min.y);
            *buffers = g_slist_append(*buffers, (gpointer)buffer);
            framebuffer.insert(i.name(),
                               Imf::Slice(Imf::HALF, base, sizeof(buffer[0]), xres*sizeof(buffer[0]), xs, ys));
        }
        else if (channel.type == Imf::FLOAT) {
            gfloat *buffer = g_new(gfloat, xres*yres);
            char *base = (char*)(buffer - dw.min.x - xres*dw.min.y);
            *buffers = g_slist_append(*buffers, (gpointer)buffer);
            framebuffer.insert(i.name(),
                               Imf::Slice(Imf::FLOAT, base, sizeof(buffer[0]), xres*sizeof(buffer[0]), xs, ys));
        }
        else {
            g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                        _("OpenEXR data type %u is invalid or unsupported."), (guint)channel.type);
            return NULL;
        }

    }

    if (!nchannels) {
        err_NO_DATA(error);
        return NULL;
    }

    infile.setFrameBuffer(framebuffer);
    infile.readPixels(dw.min.y, dw.max.y);

    gdouble xreal, yreal, xoff = 0.0, yoff = 0.0;
    gdouble q = 1.0, z0 = 0.0;
    GwySIUnit *unitxy = NULL, *unitz = NULL;

    args.params = gwy_params_new_from_settings(define_import_params());
    if (xreal_attr && yreal_attr) {
        gwy_debug("Found Gwyddion image keys, using for direct import.");

        xreal = xreal_attr->value();
        yreal = yreal_attr->value();
        if (xoff_attr)
            xoff = xoff_attr->value();
        if (yoff_attr)
            yoff = yoff_attr->value();

        /* We set zmin and zmax only for UINT data type. */
        if (zmin_attr && zmax_attr) {
            z0 = zmin_attr->value();
            q = (zmax_attr->value() - z0)/(G_MAXUINT32 + 0.999);
        }
        else if (zmax_attr) {
            q = zmax_attr->value()/(G_MAXUINT32 + 0.999);
        }
        else if (zscale_attr) {
            q = zscale_attr->value();
        }

        gint power10;

        if (xyunit_attr) {
            unitxy = gwy_si_unit_new_parse(xyunit_attr->value().c_str(), &power10);
            *objects = g_slist_prepend(*objects, (gpointer)unitxy);
            xreal *= pow10(power10);
            yreal *= pow10(power10);
            xoff *= pow10(power10);
            yoff *= pow10(power10);
        }

        if (zunit_attr) {
            unitz = gwy_si_unit_new_parse(zunit_attr->value().c_str(), &power10);
            *objects = g_slist_prepend(*objects, (gpointer)unitz);
            q *= pow10(power10);
            z0 *= pow10(power10);
        }
    }
    else if (mode == GWY_RUN_INTERACTIVE) {
        // XXX: This is sort of completely wrong as each channel can have a different scaling.  But presenting half
        // a dozen physical scale choosers is hardly better.  Just import the data and let the user sort it out.  For
        // plain images, only lateral measurements will probably make sense anyway.
        gwy_debug("Manual import is necessary.");

        Imf::ChannelList::ConstIterator first = channels.begin();
        const Imf::Channel &channel = first.channel();
        guint xs = channel.xSampling, ys = channel.ySampling;
        guint xres = (width + xs-1)/xs, yres = (height + ys-1)/ys;
        GwyRawDataType rawdatatype = exr_type_to_gwy_type(channel.type);

        args.field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);
        gdouble *d = gwy_data_field_get_data(args.field);
        gwy_convert_raw_data((*buffers)->data, xres*yres, 1, rawdatatype, GWY_BYTE_ORDER_NATIVE, d, 1.0, 0.0);

        gchar *channel_names = exr_format_channel_names(channels);
        args.npages = 1;
        args.pages_are_uniform = TRUE;
        args.channels = channel_names;

        GwyDialogOutcome outcome = run_import_gui(&args, "OpenEXR");
        gwy_params_save_to_settings(args.params);
        GWY_OBJECT_UNREF(args.field);
        GWY_FREE(args.channels);
        if (outcome == GWY_DIALOG_CANCEL) {
            g_object_unref(args.params);
            err_CANCELLED(error);
            return NULL;
        }
    }

    if (!(xreal_attr && yreal_attr))
        field_props_from_params(args.params, &xreal, &yreal, &unitxy, &q, &unitz);
    GWY_OBJECT_UNREF(args.params);

    container = gwy_container_new();
    *objects = g_slist_prepend(*objects, (gpointer)container);

    GSList *l = *buffers;
    gint id = 0;

    for (Imf::ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i, ++id, l = g_slist_next(l)) {
        const Imf::Channel &channel = i.channel();
        guint xs = channel.xSampling, ys = channel.ySampling;
        guint xres = (width + xs-1)/xs, yres = (height + ys-1)/ys;
        GwyRawDataType rawdatatype = exr_type_to_gwy_type(channel.type);

        g_assert(l);

        GwyDataField *dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
        gdouble *d = gwy_data_field_get_data(dfield);
        *objects = g_slist_prepend(*objects, (gpointer)dfield);
        gwy_convert_raw_data(l->data, xres*yres, 1, rawdatatype, GWY_BYTE_ORDER_NATIVE, d, q, z0);
        GwyDataField *mask = gwy_app_channel_mask_of_nans(dfield, TRUE);

        if (unitxy)
            gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), unitxy);
        if (unitz)
            gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), unitz);

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(id), dfield);

        if (mask) {
            gwy_container_set_object(container, gwy_app_get_mask_key_for_id(id), mask);
            g_object_unref(mask);
        }

        gchar *title;
        if (title_attr && nchannels > 1)
            title = g_strconcat(title_attr->value().c_str(), " ", i.name(), NULL);
        else if (title_attr)
            title = g_strdup(title_attr->value().c_str());
        else
            title = g_strdup(i.name());
        gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(id), (const guchar*)title);

        gwy_file_channel_import_log_add(container, id, "openexr", filename);
    }

    // We have container on the unref-me-list so another reference must be taken to retain it.
    g_object_ref(container);

    return container;
}
#endif

/***************************************************************************************************************
 *
 * Common HDR image functions
 * Used only for OpenEXR at this moment.
 *
 ***************************************************************************************************************/

#ifdef HAVE_EXR
static gdouble
suggest_zscale(GwyBitDepth bit_depth,
               gdouble pmin, gdouble pmax, gdouble pcentre)
{
    if (bit_depth == GWY_BIT_DEPTH_FLOAT)
        return 1.0;

    g_return_val_if_fail(bit_depth == GWY_BIT_DEPTH_HALF, 1.0);

    // Range OK as-is
    if (pmin >= HALF_NRM_MIN && pmax <= HALF_MAX)
        return 1.0;

    // Range OK if scaled
    if (pmax/pmin < (double)HALF_MAX/HALF_NRM_MIN)
        return sqrt(pmax/HALF_MAX * pmin/HALF_NRM_MIN);

    // Range not OK, may need a bit more sopistication here...
    return pcentre;
}

static void
representable_range(GwyBitDepth bit_depth, gdouble zscale,
                    gdouble *min, gdouble *max)
{
    if (bit_depth == GWY_BIT_DEPTH_FLOAT) {
        *min = zscale*G_MINFLOAT;
        *max = zscale*G_MAXFLOAT;
    }
    else if (bit_depth == GWY_BIT_DEPTH_HALF) {
        *min = zscale*HALF_NRM_MIN;
        *max = zscale*HALF_MAX;
    }
    else {
        g_assert_not_reached();
    }
}

static gchar*
create_image_data(GwyDataField *field,
                  GwyBitDepth bit_depth,
                  gdouble zscale,
                  gdouble zmin,
                  gdouble zmax)
{
    guint xres = gwy_data_field_get_xres(field);
    guint yres = gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    gchar *retval = NULL;
    guint i;

    if (zscale == GWY_BIT_DEPTH_INT16) {
        guint16 *imagedata = g_new(guint16, xres*yres);
        gdouble q = (G_MAXUINT16 + 0.999)/(zmax - zmin);
        retval = (gchar*)imagedata;

        for (i = xres*yres; i; i--, d++, imagedata++)
            *imagedata = (guint16)CLAMP(q*(*d - zmin), 0.0, G_MAXUINT16 + 0.999);
    }
    else if (bit_depth == GWY_BIT_DEPTH_INT32) {
        guint32 *imagedata = g_new(guint32, xres*yres);
        gdouble q = (G_MAXUINT32 + 0.999)/(zmax - zmin);
        retval = (gchar*)imagedata;

        for (i = xres*yres; i; i--, d++, imagedata++)
            *imagedata = (guint32)CLAMP(q*(*d - zmin), 0.0, G_MAXUINT32 + 0.999);
    }
    else if (bit_depth == GWY_BIT_DEPTH_FLOAT) {
        gfloat *imagedata = g_new(gfloat, xres*yres);
        retval = (gchar*)imagedata;

        for (i = xres*yres; i; i--, d++, imagedata++)
            *imagedata = (gfloat)(*d/zscale);
    }
    else if (bit_depth == GWY_BIT_DEPTH_HALF) {
        half *imagedata = g_new(half, xres*yres);
        retval = (gchar*)imagedata;

        for (i = xres*yres; i; i--, d++, imagedata++)
            *imagedata = (half)(*d/zscale);
    }
    else {
        g_assert_not_reached();
    }

    return retval;
}

static void
find_range(GwyDataField *field,
           gdouble *fmin, gdouble *fmax,
           gdouble *pmin, gdouble *pmax, gdouble *pcentre)
{
    gdouble min = G_MAXDOUBLE, max = G_MINDOUBLE, logcentre = 0.0;
    guint i, nc = 0;
    guint xres = gwy_data_field_get_xres(field),
          yres = gwy_data_field_get_yres(field);
    const gdouble *d = gwy_data_field_get_data_const(field);
    gdouble v;

    for (i = xres*yres; i; i--, d++) {
        if (!(v = *d))
            continue;

        v = fabs(v);
        if (v < min)
            min = v;
        if (v > max)
            max = v;
        logcentre += log(v);
        nc++;
    }

    *pmax = max;
    *pmin = min;
    *pcentre = exp(logcentre/nc);

    gwy_data_field_get_min_max(field, fmin, fmax);
}
#endif

static gchar*
describe_channels(guint flags)
{
    if (flags & PIXMAP_HAS_COLOURS)
        return g_strdup((flags & PIXMAP_HAS_ALPHA) ? "R, G, B, A" : "R, G, B");
    return g_strdup((flags & PIXMAP_HAS_ALPHA) ? "G, A" : "G");
}

static const gchar*
channel_name(guint nchannels, guint id)
{
    if (nchannels == 1)
        return "Gray";

    if (nchannels == 2)
        return id ? "Alpha" : "Gray";

    if (nchannels == 3)
        return id ? (id == 1 ? "Green" : "Blue") : "Red";

    if (nchannels == 4)
        return id ? (id == 1 ? "Green" : (id == 2 ? "Blue" : "Alpha")) : "Red";

    return NULL;
}

/***************************************************************************************************************
 *
 * PNG
 *
 ***************************************************************************************************************/

#ifdef HAVE_PNG
static gint
png16_detect(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             const gchar *name)
{
    typedef struct {
        guint width;
        guint height;
        guint bit_depth;
        guint colour_type;
        guint compression_method;
        guint filter_method;
        guint interlace_method;
    } IHDR;

    IHDR header;
    const guchar *p;

    // Export is done in pixmap.c, we cannot have multiple exporters of the
    // same type (unlike loaders).
    if (only_name)
        return 0;

    if (fileinfo->buffer_len < 64)
        return 0;
    if (memcmp(fileinfo->head, "\x89PNG\r\n\x1a\n\x00\x00\x00\x0dIHDR", 16)
        != 0)
        return 0;

    p = fileinfo->head + 16;
    header.width = gwy_get_guint32_be(&p);
    header.height = gwy_get_guint32_be(&p);
    header.bit_depth = *(p++);
    header.colour_type = *(p++);
    header.compression_method = *(p++);
    header.filter_method = *(p++);
    header.interlace_method = *(p++);
    if (!header.width || !header.height || header.bit_depth != 16)
        return 0;

    return 95;
}

static gboolean
get_png_text_double(const png_textp text_chunks, guint ncomments,
                    const gchar *key, gdouble *value)
{
    guint i;

    for (i = 0; i < ncomments; i++) {
        if (gwy_strequal(text_chunks[i].key, key)) {
            *value = g_ascii_strtod(text_chunks[i].text, NULL);
            return TRUE;
        }
    }
    return FALSE;
}

static const gchar*
get_png_text_string(const png_textp text_chunks, guint ncomments,
                    const gchar *key)
{
    guint i;

    for (i = 0; i < ncomments; i++) {
        if (gwy_strequal(text_chunks[i].key, key))
            return text_chunks[i].text;
    }
    return NULL;
}

static GwyContainer*
png16_load(const gchar *filename,
           GwyRunType mode,
           GError **error,
           const gchar *name)
{
    png_structp reader = NULL;
    png_infop reader_info = NULL;
    png_bytepp rows = NULL;
    png_textp text_chunks = NULL;
    png_int_32 pcal_X0, pcal_X1;
    png_charp pcal_purpose, pcal_units;
    png_charpp pcal_params;
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_SWAP_ENDIAN;
#endif
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
    guint transform_flags = PNG_TRANSFORM_IDENTITY;
#endif
    GwyContainer *container = NULL;
    GwyDataField **fields = NULL;
    gdouble **data = NULL;
    FILE *fr = NULL;
    GwySIUnit *unitxy = NULL, *unitz = NULL;
    gboolean have_sCAL, have_pCAL, manual_import = TRUE;
    guint xres, yres, bit_depth, nchannels, ncomments;
    G_GNUC_UNUSED guint colour_type, rowbytes;
    guint id, i, j;
    int scal_unit, pcal_type, pcal_nparams, power10;
    gdouble xreal, yreal, xoff = 0.0, yoff = 0.0, zmin, zmax, q, scal_xreal, scal_yreal;
    ImportArgs args;
    const gchar *title = NULL;
    png_byte magic[8];

    gwy_clear(&args, 1);
    if (!(fr = g_fopen(filename, "rb"))) {
        err_OPEN_READ(error);
        goto fail;
    }
    if (fread(magic, 1, sizeof(magic), fr) != sizeof(magic)) {
        err_READ(error);
        goto fail;
    }
    if (png_sig_cmp(magic, 0, sizeof(magic)) != 0) {
        err_FILE_TYPE(error, "PNG");
        goto fail;
    }

    reader = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!reader) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"),
                    "png_create_read_struct");
        goto fail;
    }

    reader_info = png_create_info_struct(reader);
    if (!reader_info) {
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng initialization error (in %s)"),
                    "png_create_info_struct");
        goto fail;
    }

    if (setjmp(png_jmpbuf(reader))) {
        /* FIXME: Not very helpful.  Thread-unsafe. */
        g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_SPECIFIC,
                    _("libpng error occurred"));
        goto fail;
    }

    png_init_io(reader, fr);
    png_set_sig_bytes(reader, sizeof(magic));
    /* The same as in err_DIMENSIONS(). */
    png_set_user_limits(reader, 1 << 15, 1 << 15);
    png_read_png(reader, reader_info, transform_flags, NULL);
    /* png_get_IHDR() causes trouble with the too type-picky C++ compiler. */
    xres = png_get_image_width(reader, reader_info);
    yres = png_get_image_height(reader, reader_info);
    bit_depth = png_get_bit_depth(reader, reader_info);
    if (bit_depth != 16) {
        err_BPP(error, bit_depth);
        goto fail;
    }
    colour_type = png_get_color_type(reader, reader_info);
    nchannels = png_get_channels(reader, reader_info);
    gwy_debug("xres: %u, yres: %u, bit_depth: %u, type: %u, nchannels: %u",
              xres, yres, bit_depth, colour_type, nchannels);
    rowbytes = png_get_rowbytes(reader, reader_info);
    ncomments = png_get_text(reader, reader_info, &text_chunks, NULL);
    have_sCAL = png_get_sCAL(reader, reader_info, &scal_unit, &scal_xreal, &scal_yreal);
    have_pCAL = png_get_pCAL(reader, reader_info, &pcal_purpose, &pcal_X0, &pcal_X1, &pcal_type,
                             &pcal_nparams, &pcal_units, &pcal_params);
    gwy_debug("ncomments: %u, sCAL: %d, pCAL: %d", ncomments, have_sCAL, have_pCAL);
    rows = png_get_rows(reader, reader_info);

    /* Gwyddion tEXT chunks. */
    if (get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_XREAL, &xreal)
        && get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_YREAL, &yreal)
        && get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_ZMIN, &zmin)
        && get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_ZMAX, &zmax)) {
        gwy_debug("Found Gwyddion image keys, using for direct import.");
        xoff = yoff = 0.0;
        get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_XOFFSET, &xoff);
        get_png_text_double(text_chunks, ncomments, GWY_IMGKEY_YOFFSET, &yoff);
        unitxy = gwy_si_unit_new_parse(get_png_text_string(text_chunks, ncomments, GWY_IMGKEY_XYUNIT), &power10);
        q = pow10(power10);
        xreal *= q;
        yreal *= q;
        xoff *= q;
        yoff *= q;
        unitz = gwy_si_unit_new_parse(get_png_text_string(text_chunks, ncomments, GWY_IMGKEY_ZUNIT), &power10);
        q = pow10(power10);
        zmin *= q;
        zmax *= q;
        title = get_png_text_string(text_chunks, ncomments, GWY_IMGKEY_TITLE);

        if (!((xreal = fabs(xreal)) > 0.0)) {
            g_warning("Real y size is 0.0, fixing to 1.0");
            xreal = 1.0;
        }
        if (!((xreal = fabs(xreal)) > 0.0)) {
            g_warning("Real y size is 0.0, fixing to 1.0");
            xreal = 1.0;
        }
        manual_import = FALSE;
    }
    else if (have_sCAL && have_pCAL && pcal_nparams == 2 && gwy_strequal(pcal_purpose, "Z")) {
        gwy_debug("Found sCAL and pCAL chnunks, using for direct import.");
        if (pcal_X0 != 0 || pcal_X1 != G_MAXUINT16)
            g_warning("PNG pCAL X0 and X1 transform is not implemented");

        xreal = scal_xreal;
        yreal = scal_yreal;
        xoff = yoff = 0.0;
        zmin = g_ascii_strtod(pcal_params[0], NULL);
        zmax = zmin + G_MAXUINT16*g_ascii_strtod(pcal_params[0], NULL);

        unitxy = gwy_si_unit_new("m");
        unitz = gwy_si_unit_new_parse(pcal_units, &power10);
        q = pow10(power10);
        zmin *= q;
        zmax *= q;

        if (!((xreal = fabs(xreal)) > 0.0)) {
            g_warning("Real y size is 0.0, fixing to 1.0");
            xreal = 1.0;
        }
        if (!((xreal = fabs(xreal)) > 0.0)) {
            g_warning("Real y size is 0.0, fixing to 1.0");
            xreal = 1.0;
        }
        manual_import = FALSE;
    }

    if (!title)
        title = get_png_text_string(text_chunks, ncomments, "Title");

    args.params = gwy_params_new_from_settings(define_import_params());
    // Loading alpha from a separate chunk is not supported
    args.npages = 1;
    args.pages_are_uniform = TRUE;
    args.channels = describe_channels(nchannels > 1 ? PIXMAP_HAS_COLOURS : 0);
    if (mode == GWY_RUN_INTERACTIVE && manual_import) {
        gwy_debug("Manual import is necessary.");
        args.field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);
        gdouble *d = gwy_data_field_get_data(args.field);

        // Use the first channel for preview.
        for (i = 0; i < yres; i++) {
            const guint16 *row = (const guint16*)rows[i];
            for (j = 0; j < xres; j++) {
                d[i*xres + j] = row[j*nchannels];
            }
        }

        GwyDialogOutcome outcome = run_import_gui(&args, "PNG");
        gwy_params_save_to_settings(args.params);
        GWY_OBJECT_UNREF(args.field);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto fail;
        }
    }
    if (manual_import) {
        zmin = 0.0;
        field_props_from_params(args.params, &xreal, &yreal, &unitxy, &zmax, &unitz);
    }

    fields = g_new(GwyDataField*, nchannels);
    data = g_new(gdouble*, nchannels);
    for (id = 0; id < nchannels; id++) {
        GwyDataField *f;

        fields[id] = f = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(f), unitxy);
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(f), unitz);
        gwy_data_field_set_xoffset(f, xoff);
        gwy_data_field_set_yoffset(f, yoff);
        data[id] = gwy_data_field_get_data(f);
    }

    q = (zmax - zmin)/G_MAXUINT16;
    for (i = 0; i < yres; i++) {
        const guint16 *row = (const guint16*)rows[i];
        for (j = 0; j < xres; j++) {
            for (id = 0; id < nchannels; id++) {
                data[id][i*xres + j] = q*row[j*nchannels + id] + zmin;
            }
        }
    }

    container = gwy_container_new();
    for (id = 0; id < nchannels; id++) {
        const gchar *basetitle;
        gchar *t = NULL;

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(id), fields[id]);
        g_object_unref(fields[id]);

        basetitle = channel_name(nchannels, id);
        if (title && (nchannels == 1 || !basetitle))
            t = g_strdup(title);
        else if (title)
            t = g_strdup_printf("%s %s", _(basetitle), title);
        else if (basetitle)
            t = g_strdup(_(basetitle));

        if (t)
            gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(id), (const guchar*)t);

        if (basetitle && gwy_stramong(basetitle, "Red", "Green", "Blue", NULL)) {
            gchar *palette = g_strconcat("RGB-", basetitle, NULL);
            gwy_container_set_string(container, gwy_app_get_data_palette_key_for_id(id), (const guchar*)palette);
        }

        gwy_file_channel_import_log_add(container, id, "png16", filename);
    }

fail:
    g_free(args.channels);
    g_free(data);
    g_free(fields);
    GWY_OBJECT_UNREF(args.params);
    GWY_OBJECT_UNREF(unitxy);
    GWY_OBJECT_UNREF(unitz);
    if (reader)
        png_destroy_read_struct(&reader, reader_info ? &reader_info : NULL, NULL);
    if (fr)
        fclose(fr);
    return container;
}
#endif

/***************************************************************************************************************
 *
 * PGM
 *
 ***************************************************************************************************************/

/* Pixel properties are set if detection is successfull, real properties are set only if return value is GWY_META. */
static DetectionResult
read_pgm_head(const gchar *buffer, gsize len, guint *headersize,
              guint *xres, guint *yres, guint *maxval,
              gdouble *xreal, gdouble *yreal,
              gdouble *yoff, gdouble *xoff,
              gdouble *zmin, gdouble *zmax,
              GwySIUnit **unitxy, GwySIUnit **unitz,
              gchar **title)
{
    const gchar *p = buffer, *q;
    gboolean seen_comments = FALSE,
             seen_xreal = FALSE, seen_yreal = FALSE,
             seen_zmin = FALSE, seen_zmax = FALSE;
    gint power10xy = 0, power10z = 0;
    gchar *text, *line, *s, *t;
    guint i;

    /* Quickly weed out non-PGM files */
    if (len < 3)
        return BAD_FILE;
    if (p[0] != 'P' || p[1] != '5' || !g_ascii_isspace(p[2]))
        return BAD_FILE;
    p += 3;

    for (i = 0; i < 3; i++) {
        if (p == buffer)
            return BAD_FILE;

        while (TRUE) {
            /* Skip whitespace */
            while ((gsize)(p - buffer) < len && g_ascii_isspace(*p))
                p++;
            if (p == buffer)
                return BAD_FILE;

            /* Possibly skip comments */
            if (*p != '#')
                break;

            seen_comments = TRUE;
            while ((gsize)(p - buffer) < len && *p != '\n' && *p != '\r')
                p++;
            if (p == buffer)
                return BAD_FILE;
        }

        /* Find the number */
        if (!g_ascii_isdigit(*p))
            return BAD_FILE;
        q = p;
        while ((gsize)(p - buffer) < len && g_ascii_isdigit(*p))
            p++;
        if (p == buffer)
            return BAD_FILE;
        if (!g_ascii_isspace(*p))
            return BAD_FILE;

        /* Store the number */
        if (i == 0)
            *xres = atoi(q);
        else if (i == 1)
            *yres = atoi(q);
        else if (i == 2)
            *maxval = atoi(q);
        else {
            g_assert_not_reached();
        }
    }

    /* If i == 3 and we got here then p points to the single white space character after the last number (maxval). */
    p++;
    *headersize = p - buffer;

    /* Sanity check. */
    if (*maxval < 0x100 || *maxval >= 0x10000)
        return BAD_FILE;
    if (*xres < 1 || *xres >= 1 << 15)
        return BAD_FILE;
    if (*yres < 1 || *yres >= 1 << 15)
        return BAD_FILE;

    if (!seen_comments)
        return PLAIN_IMAGE;

    *xoff = *yoff = 0.0;
    *unitxy = *unitz = NULL;
    *title = NULL;
    text = t = g_strndup(buffer, *headersize);
    for (line = gwy_str_next_line(&t); line; line = gwy_str_next_line(&t)) {
        g_strstrip(line);
        if (line[0] != '#')
            continue;
        line++;
        while (g_ascii_isspace(*line))
            line++;
        s = line;
        while (g_ascii_isalnum(*line) || *line == ':')
            line++;
        *line = '\0';
        line++;
        while (g_ascii_isspace(*line))
            line++;

        if (gwy_strequal(s, GWY_IMGKEY_XREAL)) {
            *xreal = g_ascii_strtod(line, NULL);
            seen_xreal = TRUE;
        }
        else if (gwy_strequal(s, GWY_IMGKEY_YREAL)) {
            *yreal = g_ascii_strtod(line, NULL);
            seen_yreal = TRUE;
        }
        else if (gwy_strequal(s, GWY_IMGKEY_ZMIN)) {
            *zmin = g_ascii_strtod(line, NULL);
            seen_zmin = TRUE;
        }
        else if (gwy_strequal(s, GWY_IMGKEY_ZMAX)) {
            *zmax = g_ascii_strtod(line, NULL);
            seen_zmax = TRUE;
        }
        else if (gwy_strequal(s, GWY_IMGKEY_XOFFSET))
            *xoff = g_ascii_strtod(line, NULL);
        else if (gwy_strequal(s, GWY_IMGKEY_YOFFSET))
            *yoff = g_ascii_strtod(line, NULL);
        else if (gwy_strequal(s, GWY_IMGKEY_XYUNIT)) {
            GWY_OBJECT_UNREF(*unitxy);
            *unitxy = gwy_si_unit_new_parse(line, &power10xy);
        }
        else if (gwy_strequal(s, GWY_IMGKEY_ZUNIT)) {
            GWY_OBJECT_UNREF(*unitz);
            *unitz = gwy_si_unit_new_parse(line, &power10z);
        }
        else if (gwy_strequal(s, GWY_IMGKEY_TITLE)) {
            g_free(*title);
            *title = *line ? g_strdup(line) : NULL;
        }
    }

    g_free(text);

    if (seen_xreal && seen_yreal && seen_zmin && seen_zmax) {
        *xreal *= pow10(power10xy);
        *yreal *= pow10(power10xy);
        *xoff *= pow10(power10xy);
        *yoff *= pow10(power10xy);
        *zmin *= pow10(power10z);
        *zmax *= pow10(power10z);
        return GWY_META;
    }

    GWY_OBJECT_UNREF(*unitxy);
    GWY_OBJECT_UNREF(*unitz);
    g_free(*title);
    return PLAIN_IMAGE;
}

static gint
pgm16_detect(const GwyFileDetectInfo *fileinfo,
             gboolean only_name,
             const gchar *name)
{
    GwySIUnit *unitxy = NULL, *unitz = NULL;
    gchar *title = NULL;
    gdouble xreal, yreal, xoff, yoff, zmin, zmax;
    guint xres, yres, maxval, headersize;

    // Export is done in pixmap.c, we cannot have multiple exporters of the same type (unlike loaders).
    if (only_name)
        return 0;

    if (!read_pgm_head((const gchar*)fileinfo->head, fileinfo->buffer_len, &headersize,
                       &xres, &yres, &maxval, &xreal, &yreal, &yoff, &xoff,
                       &zmin, &zmax, &unitxy, &unitz, &title))
        return 0;

    GWY_OBJECT_UNREF(unitxy);
    GWY_OBJECT_UNREF(unitz);
    g_free(title);

    return 95;
}

static GwyContainer*
pgm16_load(const gchar *filename,
           GwyRunType mode,
           GError **error,
           const gchar *name)
{
    GwyContainer *container = NULL;
    GwyDataField *field = NULL;
    GError *err = NULL;
    guchar *buffer = NULL;
    gchar *title = NULL;
    GwySIUnit *unitxy = NULL, *unitz = NULL;
    gdouble xreal, yreal, xoff = 0.0, yoff = 0.0, zmin, zmax, q;
    guint xres, yres, maxval, headersize;
    DetectionResult detected;
    ImportArgs args;
    gsize size = 0;

    if (!gwy_file_get_contents(filename, &buffer, &size, &err)) {
        err_GET_FILE_CONTENTS(error, &err);
        return NULL;
    }
    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_import_params());

    // TODO: Read images in cycle, PNM is a multi-image format.
    detected = read_pgm_head((const gchar*)buffer, size, &headersize,
                             &xres, &yres, &maxval, &xreal, &yreal, &yoff, &xoff,
                             &zmin, &zmax, &unitxy, &unitz, &title);

    gwy_debug("Detected: %s", detected == GWY_META ? "Gwyddion image keys" : "Plain image");

    args.npages = 1;
    args.pages_are_uniform = TRUE;
    args.channels = describe_channels(0);
    if (detected != GWY_META && mode == GWY_RUN_INTERACTIVE) {
        gwy_debug("Manual import is necessary.");
        args.field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);
        gwy_convert_raw_data(buffer + headersize, xres*yres, 1,
                             GWY_RAW_DATA_UINT16, GWY_BYTE_ORDER_BIG_ENDIAN,
                             gwy_data_field_get_data(args.field), 1.0, 0.0);
        GwyDialogOutcome outcome = run_import_gui(&args, "PGM");
        gwy_params_save_to_settings(args.params);
        GWY_OBJECT_UNREF(args.field);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto fail;
        }
    }
    if (detected != GWY_META) {
        zmin = 0.0;
        field_props_from_params(args.params, &xreal, &yreal, &unitxy, &zmax, &unitz);
    }

    if (err_SIZE_MISMATCH(error, 2*xres*yres + headersize, size, FALSE))
        goto fail;

    if (!((xreal = fabs(xreal)) > 0.0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }
    if (!((xreal = fabs(xreal)) > 0.0)) {
        g_warning("Real y size is 0.0, fixing to 1.0");
        xreal = 1.0;
    }

    field = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(field), unitxy);
    gwy_si_unit_assign(gwy_data_field_get_si_unit_z(field), unitz);
    gwy_data_field_set_xoffset(field, xoff);
    gwy_data_field_set_yoffset(field, yoff);

    q = (zmax - zmin)/G_MAXUINT16;
    gwy_convert_raw_data(buffer + headersize, xres*yres, 1,
                         GWY_RAW_DATA_UINT16, GWY_BYTE_ORDER_BIG_ENDIAN,
                         gwy_data_field_get_data(field), q, zmin);

    container = gwy_container_new();
    gwy_container_set_object(container, gwy_app_get_data_key_for_id(0), field);
    g_object_unref(field);
    if (title) {
        gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(0), (const guchar*)title);
        title = NULL;
    }

    gwy_file_channel_import_log_add(container, 0, "pgm16", filename);

fail:
    gwy_file_abandon_contents(buffer, size, NULL);
    GWY_OBJECT_UNREF(args.params);
    GWY_OBJECT_UNREF(unitxy);
    GWY_OBJECT_UNREF(unitz);
    g_free(args.channels);
    g_free(title);

    return container;
}

/***************************************************************************************************************
 *
 * TIFF
 *
 ***************************************************************************************************************/

static gint
tiffbig_detect(const GwyFileDetectInfo *fileinfo,
               gboolean only_name,
               const gchar *name)
{
    // Export is done in pixmap.c, we cannot have multiple exporters of the
    // same type (unlike loaders).
    if (only_name)
        return 0;

    if (!gwy_tiff_detect(fileinfo->head, fileinfo->buffer_len, NULL, NULL))
        return 0;

    GwyTIFF *tiff = gwy_tiff_load(fileinfo->name, NULL);
    if (!tiff)
        return 0;

    gwy_tiff_allow_compressed(tiff, TRUE);
    GwyTIFFImageReader *reader = gwy_tiff_get_image_reader(tiff, 0, G_MAXUINT, NULL);

    guint score = 0;
    if (reader) {
        // If nothing else wants to load the image we can give it a try.
        score = 20;
        // A bit larger value than in pixmap.c for more bits.
        if (reader->bits_per_sample > 8)
            score = 75;
        // Also for more channels.
        if (reader->samples_per_pixel > 4)
            score = 75;
        // More than one image for which GdkPixbuf does not provide sane interface.
        if (gwy_tiff_get_n_dirs(tiff) > 1)
            score = 75;
        // An even larger value for BigTIFF, but still permit specific BigTIFF sub-formats to get a higher score.
        if (tiff->version == GWY_TIFF_BIG)
            score = 85;
    }

    gwy_tiff_image_reader_free(reader);
    gwy_tiff_free(tiff);

    return score;
}

static gboolean
load_tiff_channels(GwyContainer *container,
                   const GwyTIFF *tiff,
                   GwyTIFFImageReader *reader,
                   const gchar *filename,
                   gdouble xreal, gdouble yreal, gdouble zreal,
                   GwySIUnit *unitxy, GwySIUnit *unitz,
                   guint *id,
                   GError **error)
{
    guint xres = reader->width;
    guint yres = reader->height;
    guint nchannels = reader->samples_per_pixel;

    for (guint cid = 0; cid < nchannels; cid++) {
        GwyDataField *dfield = gwy_data_field_new(xres, yres, xreal, yreal, FALSE);
        GwyDataField *mask = NULL;
        gdouble *d = gwy_data_field_get_data(dfield);
        gchar *key;
        const gchar *title;

        for (guint i = 0; i < yres; i++) {
            if (!gwy_tiff_read_image_row(tiff, reader, cid, i, zreal, 0.0, d + i*xres)) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Failed to read image data."));
                g_object_unref(dfield);
                return FALSE;
            }
        }

        if (reader->sample_format == GWY_TIFF_SAMPLE_FORMAT_FLOAT)
            mask = gwy_app_channel_mask_of_nans(dfield, TRUE);

        gwy_si_unit_assign(gwy_data_field_get_si_unit_xy(dfield), unitxy);
        gwy_si_unit_assign(gwy_data_field_get_si_unit_z(dfield), unitz);

        gwy_container_set_object(container, gwy_app_get_data_key_for_id(*id), dfield);
        g_object_unref(dfield);

        title = channel_name(nchannels, cid);
        if (title)
            gwy_container_set_const_string(container, gwy_app_get_data_title_key_for_id(*id), (const guchar*)title);
        else {
            gwy_container_set_string(container, gwy_app_get_data_title_key_for_id(*id),
                                     (const guchar*)g_strdup_printf("%s %u", _("Channel"), *id));
        }

        if (mask) {
            gwy_container_set_object(container, gwy_app_get_mask_key_for_id(*id), mask);
            GWY_OBJECT_UNREF(mask);
        }

        if (title && gwy_stramong(title, "Red", "Green", "Blue", NULL)) {
            gchar *palette = g_strconcat("RGB-", title, NULL);
            gwy_container_set_string(container, gwy_app_get_data_palette_key_for_id(*id), (const guchar*)palette);
        }

        gwy_file_channel_import_log_add(container, *id, "tiffbig", filename);

        (*id)++;
    }

    return TRUE;
}

static gboolean
load_tiff_brick_levels(const GwyTIFF *tiff, GwyTIFFImageReader *reader, gint nchannels,
                       gdouble zreal, gint idx,
                       GwyBrick **bricks, GError **error)
{
    for (guint cid = 0; cid < nchannels; cid++) {
        GwyBrick *brick = bricks[cid];
        gint xres = gwy_brick_get_xres(brick), yres = gwy_brick_get_yres(brick);
        gdouble *d = gwy_brick_get_data(brick);

        for (guint i = 0; i < yres; i++) {
            if (!gwy_tiff_read_image_row(tiff, reader, cid, i, zreal, 0.0, d + xres*(i + idx*yres))) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA,
                            _("Failed to read image data."));
                return FALSE;
            }
        }

        /* FIXME: Should do something similar for bricks.
        if (reader->sample_format == GWY_TIFF_SAMPLE_FORMAT_FLOAT)
            mask = gwy_app_channel_mask_of_nans(dfield, TRUE);
            */
    }
    return TRUE;
}

static gboolean
tiff_images_are_uniform(const GwyTIFF *tiff)
{
    guint64 xres, yres, i, n = gwy_tiff_get_n_dirs(tiff);
    guint samples_per_pixel, bits_per_sample;

    if (n == 1)
        return TRUE;

    /* We will fail properly later… */
    if (!gwy_tiff_get_size(tiff, 0, GWY_TIFFTAG_IMAGE_WIDTH, &xres)
        || !gwy_tiff_get_size(tiff, 0, GWY_TIFFTAG_IMAGE_LENGTH, &yres)
        || !gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_SAMPLES_PER_PIXEL, &samples_per_pixel)
        || !gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_BITS_PER_SAMPLE, &bits_per_sample))
        return FALSE;

    for (i = 1; i < n; i++) {
        guint64 xres_i, yres_i;
        guint samples_per_pixel_i, bits_per_sample_i;

        if (!gwy_tiff_get_size(tiff, 0, GWY_TIFFTAG_IMAGE_WIDTH, &xres_i)
            || !gwy_tiff_get_size(tiff, 0, GWY_TIFFTAG_IMAGE_LENGTH, &yres_i))
            return FALSE;
        if (xres_i != xres || yres_i != yres)
            return FALSE;
        if (!gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_SAMPLES_PER_PIXEL, &samples_per_pixel_i)
            || !gwy_tiff_get_uint(tiff, 0, GWY_TIFFTAG_BITS_PER_SAMPLE, &bits_per_sample_i))
            return FALSE;
        if (samples_per_pixel_i != samples_per_pixel || bits_per_sample_i != bits_per_sample)
            return FALSE;
    }
    return TRUE;
}

static GwyContainer*
tiffbig_load(const gchar *filename,
             GwyRunType mode,
             GError **error,
             const gchar *name)
{
    GwyContainer *container = NULL;
    guint id, idx, nchannels, xres, yres;
    gdouble xreal, yreal, zreal;
    GwySIUnit *unitxy = NULL, *unitz = NULL;
    GwyTIFF *tiff = NULL;
    GwyTIFFImageReader *reader = NULL;
    GwyBrick **bricks = NULL;
    gboolean load_as_volume;
    ImportArgs args;

    if (!(tiff = gwy_tiff_load(filename, error)))
        return NULL;

    gwy_clear(&args, 1);
    args.params = gwy_params_new_from_settings(define_import_params());

    gwy_tiff_allow_compressed(tiff, TRUE);
    if (!(reader = gwy_tiff_get_image_reader(tiff, 0, G_MAXUINT, error)))
        goto fail;

    // Use the first channel for preview.
    nchannels = reader->samples_per_pixel;
    args.npages = gwy_tiff_get_n_dirs(tiff);
    args.pages_are_uniform = tiff_images_are_uniform(tiff);
    if (nchannels > 4)
        args.channels = g_strdup_printf("%u", nchannels);
    else {
        args.channels = describe_channels((nchannels > 2 ? PIXMAP_HAS_COLOURS : 0)
                                          | (nchannels % 2 == 0 ? PIXMAP_HAS_ALPHA : 0));
    }
    xres = reader->width;
    yres = reader->height;
    if (mode == GWY_RUN_INTERACTIVE) {
        gwy_debug("Manual import is necessary.");
        args.field = gwy_data_field_new(xres, yres, 1.0, 1.0, FALSE);
        gdouble *d = gwy_data_field_get_data(args.field);
        for (guint i = 0; i < yres; i++) {
            if (!gwy_tiff_read_image_row(tiff, reader, 0, i, 1.0, 0.0, d + i*xres)) {
                g_set_error(error, GWY_MODULE_FILE_ERROR, GWY_MODULE_FILE_ERROR_DATA, _("Failed to read image data."));
                GWY_OBJECT_UNREF(args.field);
                goto fail;
            }
        }
        GwyDialogOutcome outcome = run_import_gui(&args, "TIFF");
        gwy_params_save_to_settings(args.params);
        GWY_OBJECT_UNREF(args.field);
        if (outcome == GWY_DIALOG_CANCEL) {
            err_CANCELLED(error);
            goto fail;
        }
    }
    field_props_from_params(args.params, &xreal, &yreal, &unitxy, &zreal, &unitz);

    load_as_volume = args.npages > 1 && args.pages_are_uniform && gwy_params_get_boolean(args.params, PARAM_AS_VOLUME);
    if (load_as_volume) {
        bricks = g_new(GwyBrick*, nchannels);
        bricks[0] = gwy_brick_new(xres, yres, args.npages, xreal, yreal, args.npages, FALSE);
        if (unitxy) {
            gwy_si_unit_assign(gwy_brick_get_si_unit_x(bricks[0]), unitxy);
            gwy_si_unit_assign(gwy_brick_get_si_unit_y(bricks[0]), unitxy);
        }
        if (unitz)
            gwy_si_unit_assign(gwy_brick_get_si_unit_w(bricks[0]), unitz);
        for (guint i = 1; i < nchannels; i++)
            bricks[i] = gwy_brick_new_alike(bricks[0], FALSE);
    }

    container = gwy_container_new();

    for (idx = id = 0; idx < args.npages; idx++) {
        GError *err = NULL;

        reader = gwy_tiff_image_reader_free(reader);
        reader = gwy_tiff_get_image_reader(tiff, idx, G_MAXUINT, &err);
        if (!reader) {
            g_warning("Ignoring directory %u: %s.", idx, err->message);
            g_clear_error(&err);
            continue;
        }

        if (load_as_volume) {
            if (!load_tiff_brick_levels(tiff, reader, nchannels, zreal, idx, bricks, error)) {
                GWY_OBJECT_UNREF(container);
                goto fail;
            }
        }
        else {
            if (!load_tiff_channels(container, tiff, reader, filename,
                                    xreal, yreal, zreal, unitxy, unitz, &id, error)) {
                GWY_OBJECT_UNREF(container);
                goto fail;
            }
        }
    }

    if (container && load_as_volume) {
        for (guint i = 0; i < nchannels; i++) {
            gwy_container_set_object(container, gwy_app_get_brick_key_for_id(i), bricks[i]);

            const gchar *title = channel_name(nchannels, i);
            if (title)
                gwy_container_set_const_string(container, gwy_app_get_brick_title_key_for_id(i), (const guchar*)title);
            else {
                gwy_container_set_string(container, gwy_app_get_brick_title_key_for_id(i),
                                         (const guchar*)g_strdup_printf("%s %u", _("Channel"), i));
            }

            if (title && gwy_stramong(title, "Red", "Green", "Blue", NULL)) {
                gchar *palette = g_strconcat("RGB-", title, NULL);
                gwy_container_set_string(container, gwy_app_get_brick_palette_key_for_id(i), (const guchar*)palette);
            }
            gwy_file_volume_import_log_add(container, i, "tiffbig", filename);
        }
        id = nchannels;
    }

    if (!id) {
        err_NO_DATA(error);
        GWY_OBJECT_UNREF(container);
    }

fail:
    g_free(args.channels);
    if (bricks) {
        for (guint i = 0; i < nchannels; i++)
            GWY_OBJECT_UNREF(bricks[i]);
        g_free(bricks);
    }
    GWY_OBJECT_UNREF(args.params);
    GWY_OBJECT_UNREF(unitxy);
    GWY_OBJECT_UNREF(unitz);
    gwy_tiff_image_reader_free(reader);
    gwy_tiff_free(tiff);

    return container;
}

/***************************************************************************************************************
 *
 * Manual high-depth image loading
 *
 ***************************************************************************************************************/

static GwyDialogOutcome
run_import_gui(ImportArgs *args, const gchar *name)
{
    ImportGUI gui;
    GwyParamTable *table, *infotable;
    GtkWidget *align, *hbox, *view, *label;
    GwyDialog *dialog;
    GwyDialogOutcome outcome;
    GwyResults *results;
    GwyContainer *data;
    gint xres, yres;
    gchar *title;
    gdouble zoom;

    gwy_clear(&gui, 1);
    gui.args = args;

    xres = gwy_data_field_get_xres(args->field);
    yres = gwy_data_field_get_yres(args->field);

    data = gwy_container_new();
    gwy_container_set_object(data, gwy_app_get_data_key_for_id(0), args->field);

    /* TRANSLATORS: Dialog title; %s is PNG, TIFF, ... */
    title = g_strdup_printf(_("Import %s"), name);
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
    gwy_results_add_value_int(results, "pages", N_("Pages"));
    gwy_results_fill_values(results, "xres", xres, "yres", yres, "pages", args->npages,
                            "channels", args->channels, NULL);

    infotable = gwy_param_table_new(args->params);
    gwy_param_table_append_header(infotable, -1, _("Image Information"));
    gwy_param_table_append_results(infotable, WIDGET_IMAGE_INFO, results, "xres", "yres", "channels", "pages", NULL);
    gwy_param_table_results_fill(infotable, WIDGET_IMAGE_INFO);
    gwy_dialog_add_param_table(dialog, infotable);
    gtk_container_add(GTK_CONTAINER(align), gwy_param_table_widget(infotable));

    align = gtk_alignment_new(1.0, 0.0, 0.0, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), align, TRUE, TRUE, 0);

    view = gwy_create_preview(data, 0, PREVIEW_SIZE, FALSE);
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
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    table = gui.table_values = gwy_param_table_new(args->params);
    gwy_param_table_append_header(table, -1, _("Value Mapping"));
    gwy_param_table_append_entry(table, PARAM_ZREAL);
    gwy_param_table_append_unit_chooser(table, PARAM_ZUNIT);
    if (args->npages > 1 && args->pages_are_uniform) {
        gwy_param_table_append_header(table, -1, _("Image Stack"));
        gwy_param_table_append_checkbox(table, PARAM_AS_VOLUME);
    }
    gtk_box_pack_start(GTK_BOX(hbox), gwy_param_table_widget(table), TRUE, TRUE, 0);
    gwy_dialog_add_param_table(dialog, table);

    g_signal_connect_swapped(infotable, "param-changed", G_CALLBACK(import_param_changed), &gui);
    g_signal_connect_swapped(gui.table_lateral, "param-changed", G_CALLBACK(import_param_changed), &gui);
    g_signal_connect_swapped(gui.table_values, "param-changed", G_CALLBACK(import_param_changed), &gui);

    outcome = gwy_dialog_run(dialog);

    g_object_unref(data);
    g_object_unref(results);

    return outcome;
}

static void
import_param_changed(ImportGUI *gui, gint id)
{
    ImportArgs *args = gui->args;
    GwyParams *params = args->params;
    gboolean size_in_pixels = gwy_params_get_boolean(params, PARAM_SIZE_IN_PIXELS);
    gboolean xymeasureeq = gwy_params_get_boolean(params, PARAM_XYMEASUREEQ);
    gint xres = gwy_data_field_get_xres(args->field);
    gint yres = gwy_data_field_get_yres(args->field);
    GwySIUnit *unit;
    gint power10;
    GwySIValueFormat *vf = NULL;

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
}

static void
sanitise_import_params(ImportArgs *args)
{
    GwyParams *params = args->params;
    gint xres = gwy_data_field_get_xres(args->field);
    gint yres = gwy_data_field_get_yres(args->field);

    if (gwy_params_get_boolean(params, PARAM_SIZE_IN_PIXELS)) {
        gwy_params_set_unit(params, PARAM_XYUNIT, NULL);
        gwy_params_set_boolean(params, PARAM_XYMEASUREEQ, TRUE);
        gwy_params_set_double(params, PARAM_XREAL, xres);
        gwy_params_set_double(params, PARAM_YREAL, yres);
    }
    else if (gwy_params_get_boolean(params, PARAM_XYMEASUREEQ)) {
        gdouble xreal = gwy_params_get_double(params, PARAM_XREAL);
        gint xres = gwy_data_field_get_xres(args->field);
        gint yres = gwy_data_field_get_yres(args->field);
        gwy_params_set_double(params, PARAM_YREAL, yres*xreal/xres);
    }
}

static void
field_props_from_params(GwyParams *params,
                        gdouble *xreal, gdouble *yreal, GwySIUnit **xyunit,
                        gdouble *zmax, GwySIUnit **zunit)
{
    gint power10;

    gwy_debug("Using parameters from settings.");
    *xyunit = gwy_si_unit_duplicate(gwy_params_get_unit(params, PARAM_XYUNIT, &power10));
    *xreal = gwy_params_get_double(params, PARAM_XREAL)*pow10(power10);
    *yreal = gwy_params_get_double(params, PARAM_YREAL)*pow10(power10);
    *zunit = gwy_si_unit_duplicate(gwy_params_get_unit(params, PARAM_ZUNIT, &power10));
    *zmax = gwy_params_get_double(params, PARAM_ZREAL)*pow10(power10);
}

/* vim: set cin columns=120 tw=118 et ts=4 sw=4 cino=>1s,e0,n0,f0,{0,}0,^0,\:1s,=0,g1s,h0,t0,+1s,c3,(0,u0 : */
